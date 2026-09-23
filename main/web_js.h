#pragma once

#define PAGE_JS R"rawliteral(
<script>
var allMessages = [];
var allFreqs = [];
var lastTotal = 0;
var currentView = 'main';   // 'main' | 'detail' | 'charts'
var detailId = '';
var autoScrollMain = true;
var autoScrollDetail = true;

var contentMain = document.getElementById('viewMain');
var contentDetail = document.getElementById('viewDetail');
var contentCharts = document.getElementById('viewCharts');
var sendPanel = document.getElementById('sendPanel');

// ===== 授时状态（来自 /api/messages 的 clk 字段） =====
var clkSync = false;   // 设备是否已授时
var clkBoot = 0;       // 设备校准时刻的开机 ms
var clkEp = 0;         // 设备校准时刻的真实 Unix ms

var clockBusy = false;
function syncClock() {
  if (clockBusy) return;
  clockBusy = true;
  fetch('/api/time', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      epoch_ms: Date.now(),
      tz: new Date().getTimezoneOffset()
    })
  }).then(function() { clockBusy = false; pollMessages(); })
    .catch(function() { clockBusy = false; });
}

function formatTime(ms) {
  var s = Math.floor(ms / 1000);
  var m = Math.floor(s / 60);
  return String(m).padStart(2,'0') + ':' + String(s % 60).padStart(2,'0') + '.' + String(ms % 1000).padStart(3,'0');
}

// 把帧时间戳(开机ms)格式化为真实本地时间；未授时回退相对时间
function fmtFrameTime(t) {
  if (clkSync) {
    var epoch = t - clkBoot + clkEp;
    var d = new Date(epoch);
    var p2 = function(x) { return String(x).padStart(2, '0'); };
    return p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds())
      + '.' + String(d.getMilliseconds()).padStart(3, '0');
  }
  return 'boot+' + formatTime(t);
}

// 频率格式化：f 为 0.1 条/秒，显示为 x.x/s
function fmtFreq(f) {
  if (f === undefined) return '-';
  return (f / 10).toFixed(1) + '/s';
}

// 按 ID 分组，每组取最新一条
function groupById(messages) {
  var groups = {};
  for (var i = 0; i < messages.length; i++) {
    var m = messages[i];
    if (!groups[m.id] || m.t > groups[m.id].t) {
      groups[m.id] = m;
      groups[m.id].count = 0;
    }
  }
  // 计算每个 ID 的消息数
  for (var i = 0; i < messages.length; i++) {
    if (groups[messages[i].id]) groups[messages[i].id].count++;
  }
  return groups;
}

function renderMain() {
  var groups = groupById(allMessages);
  var ids = Object.keys(groups).sort(function(a, b) { return parseInt(a.slice(2), 16) - parseInt(b.slice(2), 16); });
  var freqMap = {};
  for (var i = 0; i < allFreqs.length; i++) freqMap[allFreqs[i].id] = allFreqs[i].f;
  var tbody = document.getElementById('mainBody');
  var html = '';
  for (var i = 0; i < ids.length; i++) {
    var m = groups[ids[i]];
    html += '<tr class="clickable" onclick="showDetail(\'' + m.id + '\')">'
      + '<td class="col-id">' + m.id + '</td>'
      + '<td class="col-count">' + m.count + '</td>'
      + '<td class="col-freq">' + fmtFreq(freqMap[m.id]) + '</td>'
      + '<td class="col-dlc">' + m.dlc + '</td>'
      + '<td class="col-ext">' + (m.ext ? 'Yes' : '') + '</td>'
      + '<td class="col-data">' + colorizeData(m.id, m.data) + '</td>'
      + '<td class="col-time">' + fmtFrameTime(m.t) + '</td>'
      + '</tr>';
  }
  tbody.innerHTML = html;
  document.getElementById('msgCount').textContent = allMessages.length + ' msgs, ' + ids.length + ' IDs';
}

function renderDetailTable() {
  // 优先用前端积累的历史（最多 4000 条/ID，远多于服务器 128 条快照）
  var hist = histById[normId(detailId)] || [];
  var tbody = document.getElementById('detailBody');
  var html = '';
  var shown = 0;
  if (hist.length) {
    for (var i = hist.length - 1; i >= 0 && shown < 200; i--, shown++) {
      var p = hist[i];
      html += '<tr>'
        + '<td class="col-time">' + fmtFrameTime(p.t) + '</td>'
        + '<td class="col-dlc">' + p.d + '</td>'
        + '<td class="col-ext">' + (p.e ? 'Yes' : '') + '</td>'
        + '<td class="col-data">' + colorizeData(detailId, toHex(p.b)) + '</td>'
        + '</tr>';
    }
  } else {
    // 页面刚打开还没有积累：退回服务器快照
    for (var j = allMessages.length - 1; j >= 0; j--) {
      var m = allMessages[j];
      if (m.id !== detailId) continue;
      html += '<tr>'
        + '<td class="col-time">' + fmtFrameTime(m.t) + '</td>'
        + '<td class="col-dlc">' + m.dlc + '</td>'
        + '<td class="col-ext">' + (m.ext ? 'Yes' : '') + '</td>'
        + '<td class="col-data">' + colorizeData(m.id, m.data) + '</td>'
        + '</tr>';
    }
  }
  tbody.innerHTML = html;
  document.getElementById('detailCount').textContent = hist.length + ' messages';
  var wrap = document.getElementById('viewDetail');
  if (autoScrollDetail) wrap.scrollTop = 0;
}

// 生成易读的刻度步长（1/2/5 × 10^n）
function niceStep(range, target) {
  var raw = range / target;
  if (raw <= 0) return 1;
  var mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10));
  var norm = raw / mag;
  var step = norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 5 ? 5 : 10;
  return step * mag;
}

function showDetail(id) {
  detailId = id;
  currentView = 'detail';
  setTopTab(null);
  contentMain.classList.add('hidden');
  contentCharts.classList.add('hidden');
  contentDetail.classList.remove('hidden');
  document.getElementById('detailId').textContent = id;
  document.getElementById('detailIdLabel').textContent = id;
  document.getElementById('headerTitle').textContent = 'CAN Detail';
  document.getElementById('backRow').classList.remove('hidden');
  sendPanel.classList.add('hidden');
  renderSigList();
  renderDetailTable();
}

function showMain() {
  currentView = 'main';
  setTopTab('tabMonitor');
  contentDetail.classList.add('hidden');
  contentCharts.classList.add('hidden');
  contentMain.classList.remove('hidden');
  sendPanel.classList.remove('hidden');
  document.getElementById('headerTitle').textContent = 'CAN Bus Monitor';
  document.getElementById('backRow').classList.add('hidden');
  renderMain();
}

// ===== 自定义曲线（前端解码任意 ID 信号，固件零改动） =====
// 数据源：/api/messages 的 messages 原始字节（hex 字符串），200ms 轮询快照按 ID 去重追加
// 信号定义：DBC 风格（start/len/endian/signed/factor/offset/unit/color/enabled），存 localStorage

var CFG_KEY = 'cansignals';
var configs = [];
var histById = {};     // id(小写) -> [{t,b,d,e}]，每 ID 最多 4000 点
var histLastT = {};    // 每 ID 已入历史的最新 t（快照重叠去重水钟）
var editingKey = null; // 正在编辑的信号 key；null = 新增
var chartPaused = false;
var chartWinMs = 30000;
var stripRefs = [];

function normId(id) { return String(id || '').toLowerCase(); }
function sigKey(s) { return s.id + '|' + s.name; }

// ---- 注入防护：信号配置可来自设备 NVS / 导入文件（局域网内任何人可写）， ----
// ---- 所有用户可控字段进 innerHTML / CSV 前必须经过以下处理 ----
// 文本与双引号属性上下文的 HTML 转义
function escHtml(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
                  .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}
// onclick="fn('...')" 上下文：先 JS 字符串层（\ 和 '），再 HTML 属性层（& 和 "）。
// 注意 HTML 实体解码发生在 JS 解析之前，' 必须用 \' 防护，&#39; 会被还原成引号逃逸
function escAttrJs(s) {
  return String(s).replace(/\\/g, '\\\\').replace(/'/g, "\\'")
                  .replace(/&/g, '&amp;').replace(/"/g, '&quot;');
}
// 颜色只放行 #RGB/#RRGGBB（含 3~8 位十六进制）形式，其余替换为中性色
function safeColor(c) {
  return /^#[0-9a-fA-F]{3,8}$/.test(String(c)) ? c : '#888888';
}
// CSV 单元格：分隔符替换为下划线；= + - @ 开头加 ' 前缀，防 Excel 当公式执行
function csvSafe(s) {
  var v = String(s).replace(/[,;\r\n]/g, '_');
  if (/^[=+\-@\t]/.test(v)) v = "'" + v;
  return v;
}
function keyAttr(s) { return escAttrJs(s.id + '|' + s.name); }
function toHex(bytes) {
  var out = '';
  for (var i = 0; i < bytes.length; i++) {
    if (i) out += ' ';
    out += ('0' + bytes[i].toString(16).toUpperCase()).slice(-2);
  }
  return out;
}

// ---- 通用信号解码 ----
function extractRaw(bytes, dlc, sig) {
  var val = 0, i, p, byte, bit;
  if (sig.endian === 'intel') {
    var lastBit = sig.start + sig.len - 1;
    if ((lastBit >> 3) >= dlc) return null;
    for (i = 0; i < sig.len; i++) {
      var bp = sig.start + i;
      val |= ((bytes[bp >> 3] >> (bp & 7)) & 1) << i;
    }
  } else {
    var bitPos = (sig.start >> 3) * 8 + (7 - (sig.start & 7));
    var endPos = bitPos + sig.len - 1;
    if ((endPos >> 3) >= dlc) return null;
    for (i = 0; i < sig.len; i++) {
      p = bitPos + i; byte = p >> 3; bit = 7 - (p & 7);
      val = (val << 1) | ((bytes[byte] >> bit) & 1);
    }
  }
  if (sig.signed && val >= Math.pow(2, sig.len - 1)) val -= Math.pow(2, sig.len);
  return val;
}
function decodePoint(frame, sig) {
  var bytes = frame.b;
  if (frame.dlc < 1 || bytes.length < frame.dlc) return null;
  var raw = extractRaw(bytes, frame.dlc, sig);
  if (raw === null) return null;
  return raw * sig.factor + sig.offset;
}
function sigBytesCovered(sig) {
  var set = {}, i;
  if (sig.endian === 'intel') {
    for (i = sig.start; i < sig.start + sig.len; i++) set[i >> 3] = true;
  } else {
    var bitPos = (sig.start >> 3) * 8 + (7 - (sig.start & 7));
    for (i = bitPos; i < bitPos + sig.len; i++) set[i >> 3] = true;
  }
  return set;
}

// ---- 配置存取（localStorage 缓存 + 设备 NVS 双份持久化） ----
var cfgPushTimer = null;
function saveCfg() {
  try { localStorage.setItem(CFG_KEY, JSON.stringify(configs)); } catch (e) {}
  // 同步到设备 NVS（节流 500ms；离线/失败静默，下次保存自然重试）
  clearTimeout(cfgPushTimer);
  cfgPushTimer = setTimeout(pushCfgToDevice, 500);
}
function pushCfgToDevice() {
  fetch('/api/signals', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(configs)
  }).catch(function() {});
}
// 页面加载时从设备拉配置（设备为准；设备为空则把本地配置迁移上去）
function loadCfgFromDevice() {
  fetch('/api/signals')
    .then(function(r) { return r.text(); })
    .then(function(s) {
      var d = null;
      try { d = JSON.parse(s); } catch (e) { return; }
      var arr = Array.isArray(d) ? d : [];
      if (arr.length) {
        for (var i = 0; i < arr.length; i++) {
          if (!arr[i] || !/^0x[0-9a-fA-F]+$/.test(String(arr[i].id || ''))) return;   // 数据不合法，放弃本次拉取
          arr[i].id = normId(arr[i].id);
        }
        var before = localStorage.getItem(CFG_KEY);
        configs = arr;
        try { localStorage.setItem(CFG_KEY, JSON.stringify(configs)); } catch (e) {}
        if (before !== JSON.stringify(configs)) {
          if (currentView === 'detail') renderSigList();
          if (currentView === 'charts') { buildChartList(); renderCharts(); }
          toast('已从设备加载 ' + configs.length + ' 个信号定义');
        }
      } else if (configs.length) {
        pushCfgToDevice();
      }
    })
    .catch(function() {});
}
function loadCfg() {
  try {
    var s = localStorage.getItem(CFG_KEY);
    if (s) {
      var arr = JSON.parse(s);
      var ok = !!(arr && arr.length);
      for (var i = 0; ok && i < arr.length; i++) {
        ok = !!arr[i] && /^0x[0-9a-fA-F]+$/.test(String(arr[i].id || ''));
      }
      if (ok) {
        configs = arr;
        for (var j = 0; j < configs.length; j++) configs[j].id = normId(configs[j].id);
        return;
      }
    }
  } catch (e) {}
  // 默认信号：与本固件 signal_decode.c 协议表一致（Intel 小端，实测已验证）
  configs = [
    { id: '0x18ff0182', name: 'Torque',  start: 8,  len: 16, endian: 'intel', signed: true,  factor: 1,   offset: -3000,  unit: 'Nm',  color: '#188038', enabled: true },
    { id: '0x18ff0182', name: 'Speed',   start: 24, len: 16, endian: 'intel', signed: true,  factor: 1,   offset: -15000, unit: 'rpm', color: '#9334e6', enabled: true },
    { id: '0x18ff0282', name: 'Current', start: 0,  len: 16, endian: 'intel', signed: false, factor: 0.1, offset: -1000,  unit: 'A',   color: '#1a73e8', enabled: true },
    { id: '0x18ff0282', name: 'Voltage', start: 16, len: 16, endian: 'intel', signed: false, factor: 0.1, offset: 0,      unit: 'V',   color: '#ea4335', enabled: true }
  ];
  saveCfg();
}

// ---- 历史积累 ----
// 服务端每次返回最近 128 条快照（旧→新，重叠），用每 ID 水钟只取增量帧
function histFresh(m) {
  var id = normId(m.id);
  var last = histLastT[id] || 0;
  if (m.t > last) { histLastT[id] = m.t; return true; }
  if (m.t + 5000 < last) {   // 设备重启后 t 归零：清该 ID 历史重新积累
    delete histById[id];
    histLastT[id] = m.t;
    return true;
  }
  return false;
}
function histPush(id, t, bytes, dlc, ext) {
  if (!histById[id]) histById[id] = [];
  var arr = histById[id];
  if (arr.length && arr[arr.length - 1].t === t) return;
  arr.push({ t: t, b: bytes, d: dlc, e: ext ? 1 : 0 });
  if (arr.length > 4000) arr.splice(0, arr.length - 4000);
}

// ---- 字节高亮：已配置信号覆盖的字节着色（半透明底色 + 彩色下划线） ----
function colorizeData(id, dataHex) {
  id = normId(id);
  var sigs = [];
  for (var i = 0; i < configs.length; i++) {
    if (configs[i].id === id) sigs.push(configs[i]);
  }
  var bytes = dataHex.split(/\s+/);
  var html = '';
  for (var j = 0; j < bytes.length; j++) {
    var owner = null;
    for (var k = 0; k < sigs.length; k++) {
      if (sigBytesCovered(sigs[k])[j]) { owner = sigs[k]; break; }
    }
    if (owner) html += '<span style="background:' + safeColor(owner.color) + '33;border-bottom:2px solid ' + safeColor(owner.color) + '">' + bytes[j] + '</span>';
    else html += bytes[j];
    if (j < bytes.length - 1) html += ' ';
  }
  return html;
}

// ---- 曲线数据记录器：对每个已启用信号在其所属 ID 每帧到达时采样解码值 ----
// curveRecData[sigKey] = [{t,v}]；与设备端 PSRAM 录原始帧互不影响
var curveRecData = {};
var curveRecOn = false;
var curveRecStartT = 0;
var curveRecCount = 0;
var CURVE_REC_MAX = 200000;   // 每信号点数上限（约 1 小时 @50Hz）

function sigEnabledById(id) {
  var out = [];
  for (var i = 0; i < configs.length; i++) {
    if (configs[i].id === id && configs[i].enabled) out.push(configs[i]);
  }
  return out;
}
function curveRecSample(id, t, bytes, dlc) {
  if (!curveRecOn) return;
  var sigs = sigEnabledById(id);
  for (var i = 0; i < sigs.length; i++) {
    var key = sigKey(sigs[i]);
    var v = decodePoint({ b: bytes, dlc: dlc }, sigs[i]);
    if (v === null) continue;
    if (!curveRecData[key]) curveRecData[key] = [];
    var arr = curveRecData[key];
    if (arr.length && arr[arr.length - 1].t === t) continue;
    if (arr.length >= CURVE_REC_MAX) arr.splice(0, 2000);  // 环形淘汰，批量删防抖
    arr.push({ t: t, v: v });
    curveRecCount++;
  }
}
function curveRecUpdateUI() {
  var btn = document.getElementById('curveRecBtn');
  var st = document.getElementById('curveRecStatus');
  if (!btn || !st) return;
  if (curveRecOn) {
    btn.textContent = '\u25A0 Stop';
    btn.classList.add('recording');
    var dur = Math.floor((Date.now() - curveRecStartT) / 1000);
    var durStr = dur >= 60 ? Math.floor(dur / 60) + 'm' + (dur % 60) + 's' : dur + 's';
    st.textContent = '\u25CF REC ' + curveRecCount + ' pts ' + durStr;
  } else {
    btn.textContent = '\u25CF Record';
    btn.classList.remove('recording');
    st.textContent = curveRecCount > 0 ? curveRecCount + ' pts' : '';
  }
}
function curveRecToggle() {
  if (!curveRecOn) {
    var any = false;
    for (var i = 0; i < configs.length; i++) if (configs[i].enabled) { any = true; break; }
    if (!any) { toast('没有已启用的信号，先在详情页/本页添加并勾选'); return; }
    curveRecOn = true;
    curveRecStartT = Date.now();
    curveRecData = {};
    curveRecCount = 0;
    toast('开始记录曲线数据（已启用信号）');
  } else {
    curveRecOn = false;
    toast('记录完成: ' + curveRecCount + ' 个数据点');
  }
  curveRecUpdateUI();
}
function curveRecExportCsv() {
  // 汇总所有信号采样点，按时间归并为宽表：no,time_rel_ms,<信号列>...
  var sigs = [];
  for (var i = 0; i < configs.length; i++) {
    var k = sigKey(configs[i]);
    if (curveRecData[k] && curveRecData[k].length) sigs.push({ s: configs[i], pts: curveRecData[k] });
  }
  if (!sigs.length) { toast('暂无记录数据'); return; }
  var idx = {}, tAll = [];
  for (var a = 0; a < sigs.length; a++) idx[a] = 0;
  for (var b = 0; b < sigs.length; b++) {
    for (var c = 0; c < sigs[b].pts.length; c++) tAll.push(sigs[b].pts[c].t);
  }
  tAll.sort(function(x, y) { return x - y; });
  var tU = [];
  for (var d = 0; d < tAll.length; d++) {
    if (!tU.length || tU[tU.length - 1] !== tAll[d]) tU.push(tAll[d]);
  }
  var t0 = tU[0];
  var lines = [];
  var head = 'no,time_rel_ms';
  for (var h = 0; h < sigs.length; h++) {
    head += ',' + csvSafe(sigs[h].s.name)
      + (sigs[h].s.unit ? '(' + csvSafe(sigs[h].s.unit) + ')' : '') + '@' + sigs[h].s.id;
  }
  lines.push(head);
  for (var e = 0; e < tU.length; e++) {
    var t = tU[e];
    var row = String(e) + ',' + (t - t0);
    for (var f = 0; f < sigs.length; f++) {
      var pts = sigs[f].pts, cur = null;
      while (idx[f] < pts.length && pts[idx[f]].t <= t) { cur = pts[idx[f]].v; idx[f]++; }
      row += ',' + (cur === null ? '' : fmtVal(cur));
    }
    lines.push(row);
  }
  var blob = new Blob([lines.join('\n')], { type: 'text/csv' });
  var a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'can_signals_' + Date.now() + '.csv';
  a.click();
  URL.revokeObjectURL(a.href);
  toast('已导出 ' + tU.length + ' 行 × ' + sigs.length + ' 信号');
}

// ---- 配置导出（迷你 DBC，JSON 备份/分享） ----
function exportCfg() {
  var blob = new Blob([JSON.stringify(configs, null, 2)], { type: 'application/json' });
  var a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'cansignals.json';
  a.click();
  URL.revokeObjectURL(a.href);
}

// ---- 工具 ----
function fmtVal(v) {
  if (v === null || v === undefined || isNaN(v)) return '-';
  if (Math.abs(v) >= 10000) return (v / 1000).toFixed(1) + 'k';
  if (Math.abs(v) >= 100) return v.toFixed(1);
  if (Math.abs(v) >= 1) return v.toFixed(2);
  return v.toFixed(3);
}
function toast(msg) {
  var el = document.getElementById('toast');
  if (!el) return;
  el.textContent = msg;
  el.classList.add('show');
  clearTimeout(el._t);
  el._t = setTimeout(function() { el.classList.remove('show'); }, 2200);
}

// ---- 详情页：信号定义面板 ----
function renderSigList() {
  var id = normId(detailId);
  var sigs = [];
  for (var i = 0; i < configs.length; i++) {
    if (configs[i].id === id) sigs.push(configs[i]);
  }
  var html = '';
  for (var n = 0; n < sigs.length; n++) {
    var s = sigs[n];
    var latest = '-';
    var hist = histById[id];
    if (hist && hist.length) {
      var v = decodePoint(hist[hist.length - 1], s);
      latest = (v === null ? '(无效)' : fmtVal(v) + ' ' + escHtml(s.unit));
    }
    html += '<div class="sigrow">'
      + '<span class="cdot" style="background:' + safeColor(s.color) + '"></span>'
      + '<input type="checkbox" ' + (s.enabled ? 'checked' : '') + ' onchange="toggleSig(\'' + keyAttr(s) + '\')">'
      + '<span class="signame">' + escHtml(s.name) + '</span>'
      + '<span class="sigdef">start=' + (+s.start) + ' len=' + (+s.len) + ' ' + (s.endian === 'intel' ? 'Intel' : 'Motorola')
      + ' ' + (s.signed ? 'signed' : 'unsigned') + ' ×' + (+s.factor) + ' +' + (+s.offset) + '</span>'
      + '<span class="sigval">' + latest + '</span>'
      + '<button onclick="editSig(\'' + keyAttr(s) + '\')">编辑</button>'
      + '<button class="btn danger" onclick="delSig(\'' + keyAttr(s) + '\')">删除</button>'
      + '</div>';
  }
  document.getElementById('sigList').innerHTML = html || '<div class="empty">暂无信号定义，点击"+ 添加曲线"</div>';
}
function toggleSig(key) {
  for (var i = 0; i < configs.length; i++) {
    if (sigKey(configs[i]) === key) configs[i].enabled = !configs[i].enabled;
  }
  saveCfg();
}
function delSig(key) {
  if (!confirm('删除该信号定义？')) return;
  var kept = [];
  for (var i = 0; i < configs.length; i++) {
    if (sigKey(configs[i]) !== key) kept.push(configs[i]);
  }
  configs = kept;
  delete curveRecData[key];
  saveCfg();
  if (currentView === 'detail') renderSigList();
  buildChartList();
  if (currentView === 'charts') renderCharts();
}
function editSig(key) {
  for (var i = 0; i < configs.length; i++) {
    if (sigKey(configs[i]) === key) { openForm(null, configs[i]); return; }
  }
}

// ---- 添加/编辑信号表单（模态框） ----
function openForm(id, existing) {
  editingKey = existing ? sigKey(existing) : null;
  document.getElementById('formTitle').textContent = existing ? '编辑信号' : '添加曲线';
  document.getElementById('f_id').value = existing ? existing.id : (id || '');
  document.getElementById('f_id').disabled = !!existing;
  document.getElementById('f_name').value = existing ? existing.name : '';
  document.getElementById('f_start').value = existing ? existing.start : 0;
  document.getElementById('f_len').value = existing ? existing.len : 16;
  document.getElementById('f_endian').value = existing ? existing.endian : 'intel';
  document.getElementById('f_signed').value = existing ? (existing.signed ? '1' : '0') : '1';
  document.getElementById('f_factor').value = existing ? existing.factor : 1;
  document.getElementById('f_offset').value = existing ? existing.offset : 0;
  document.getElementById('f_unit').value = existing ? existing.unit : '';
  document.getElementById('f_color').value = existing ? existing.color : '#188038';
  document.getElementById('formMask').classList.remove('hidden');
}
function closeForm() { document.getElementById('formMask').classList.add('hidden'); }
function saveForm() {
  var id = normId(document.getElementById('f_id').value.trim());
  if (!/^0x[0-9a-f]+$/.test(id)) { alert('CAN ID 格式应为 0x 开头的十六进制'); return; }
  var name = document.getElementById('f_name').value.trim() || ('Sig' + (configs.length + 1));
  var start = parseInt(document.getElementById('f_start').value) || 0;
  var len = parseInt(document.getElementById('f_len').value) || 1;
  if (start < 0 || start > 63) { alert('起始位 0-63'); return; }
  if (len < 1 || len > 64) { alert('位长 1-64'); return; }
  var s = {
    id: id, name: name, start: start, len: len,
    endian: document.getElementById('f_endian').value,
    signed: document.getElementById('f_signed').value === '1',
    factor: parseFloat(document.getElementById('f_factor').value) || 1,
    offset: parseFloat(document.getElementById('f_offset').value) || 0,
    unit: document.getElementById('f_unit').value.trim(),
    color: document.getElementById('f_color').value,
    enabled: true
  };
  if (editingKey) {
    for (var i = 0; i < configs.length; i++) {
      if (sigKey(configs[i]) === editingKey) { s.enabled = configs[i].enabled; configs[i] = s; break; }
    }
  } else {
    for (var j = 0; j < configs.length; j++) {
      if (sigKey(configs[j]) === sigKey(s)) { alert('同名信号已存在'); return; }
    }
    configs.push(s);
  }
  saveCfg();
  closeForm();
  if (currentView === 'detail') renderSigList();
  buildChartList();
  if (currentView === 'charts') renderCharts();
  toast('已保存 ' + s.name);
}

// ===== 自定义曲线页（每信号一条曲线带，独立量程） =====
function showCharts() {
  currentView = 'charts';
  setTopTab('tabCharts');
  contentMain.classList.add('hidden');
  contentDetail.classList.add('hidden');
  contentCharts.classList.remove('hidden');
  document.getElementById('headerTitle').textContent = '自定义曲线';
  document.getElementById('backRow').classList.add('hidden');
  sendPanel.classList.add('hidden');
  buildChartList();
  renderCharts();
}
function buildChartList() {
  stripRefs = [];
  var wrap = document.getElementById('chartList');
  var html = '';
  for (var i = 0; i < configs.length; i++) {
    var s = configs[i];
    var key = keyAttr(s);
    html += '<div class="strip" id="strip_' + i + '">'
      + '<div class="strip-head">'
      + '<span class="cdot" style="background:' + safeColor(s.color) + '"></span>'
      + '<input type="checkbox" ' + (s.enabled ? 'checked' : '') + ' onchange="toggleSig(\'' + key + '\');buildChartList();renderCharts();">'
      + '<span class="signame">' + escHtml(s.name) + '</span>'
      + '<span class="sigdef">@' + escHtml(s.id) + (s.unit ? ' (' + escHtml(s.unit) + ')' : '') + '</span>'
      + '<span class="sigval" id="lv_' + i + '">-</span>'
      + '<button onclick="editSig(\'' + key + '\')">编辑</button>'
      + '<button class="btn danger" onclick="delSig(\'' + key + '\')">删除</button>'
      + '</div>'
      + '<canvas id="cv_' + i + '"></canvas>'
      + '</div>';
  }
  wrap.innerHTML = html || '<div class="empty">暂无信号，点击"+ 添加曲线"定义一个信号</div>';
  for (var j = 0; j < configs.length; j++) stripRefs[j] = document.getElementById('cv_' + j);
}
function renderCharts() {
  chartWinMs = parseInt(document.getElementById('winSel').value) || 30000;
  var maxT = 0;
  for (var id in histById) {
    var a = histById[id];
    if (a.length && a[a.length - 1].t > maxT) maxT = a[a.length - 1].t;
  }
  var t0 = maxT - chartWinMs;
  var wrap = document.getElementById('viewCharts');
  var cssW = Math.max(280, (wrap.clientWidth || window.innerWidth) - 24);
  var dpr = window.devicePixelRatio || 1;
  for (var i = 0; i < configs.length; i++) {
    var s = configs[i];
    var cv = stripRefs[i];
    if (!cv) continue;
    var strip = document.getElementById('strip_' + i);
    if (strip) strip.style.display = s.enabled ? '' : 'none';
    if (!s.enabled) continue;
    var cssH = 150;
    cv.style.width = cssW + 'px';
    cv.style.height = cssH + 'px';
    cv.width = Math.round(cssW * dpr);
    cv.height = Math.round(cssH * dpr);
    var ctx = cv.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    var pts = [], lastV = null;
    var hist = histById[s.id] || [];
    for (var k = 0; k < hist.length; k++) {
      var p = hist[k];
      if (p.t < t0) continue;
      var v = decodePoint(p, s);
      pts.push({ t: p.t, v: v });
      if (v !== null) lastV = v;
    }
    var lv = document.getElementById('lv_' + i);
    if (lv) lv.textContent = (lastV === null ? '-' : fmtVal(lastV) + ' ' + s.unit);
    drawStrip(ctx, cssW, cssH, pts, s, t0, maxT);
  }
}
function drawStrip(ctx, cssW, cssH, pts, s, t0, t1) {
  var padL = 52, padR = 14, padT = 8, padB = 20;
  var plotW = cssW - padL - padR, plotH = cssH - padT - padB;
  ctx.clearRect(0, 0, cssW, cssH);
  ctx.font = '10px Consolas, monospace';
  if (!pts.length) {
    ctx.fillStyle = '#aaa'; ctx.textAlign = 'center';
    ctx.fillText('窗口内暂无 ' + s.id + ' 数据', cssW / 2, cssH / 2);
    return;
  }
  var lo = Infinity, hi = -Infinity;
  for (var i = 0; i < pts.length; i++) {
    if (pts[i].v === null) continue;
    if (pts[i].v < lo) lo = pts[i].v;
    if (pts[i].v > hi) hi = pts[i].v;
  }
  if (lo === Infinity) { lo = 0; hi = 1; }
  if (hi - lo < 1e-9) { hi = lo + 1; }
  var r = hi - lo;
  lo -= r * 0.12; hi += r * 0.12;
  var step = niceStep(hi - lo, 4);
  lo = Math.floor(lo / step) * step; hi = Math.ceil(hi / step) * step;
  var yOf = function(v) { return padT + (hi - v) / (hi - lo) * plotH; };
  var xOf = function(t) { return padL + (t - t0) / (t1 - t0) * plotW; };
  ctx.textBaseline = 'middle';
  ctx.textAlign = 'right';
  for (var v = lo; v <= hi + step * 0.5; v += step) {
    var y = yOf(v);
    ctx.strokeStyle = (v === 0) ? '#dcdcdc' : '#f1f1f1';
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(padL + plotW, y); ctx.stroke();
    ctx.fillStyle = '#666';
    ctx.fillText(fmtVal(v), padL - 5, y);
  }
  var spanS = (t1 - t0) / 1000;
  var stepT = niceStep(spanS, 6) || 1;
  ctx.textAlign = 'center'; ctx.textBaseline = 'top'; ctx.fillStyle = '#999';
  for (var ts = 0; ts <= spanS + stepT * 0.5; ts += stepT) {
    var x = padL + (1 - ts / spanS) * plotW;
    if (x < padL - 0.5) break;
    ctx.strokeStyle = '#f7f7f7';
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, padT + plotH); ctx.stroke();
    ctx.fillText(ts === 0 ? 'now' : '-' + (stepT < 1 ? (spanS - ts).toFixed(1) : String(Math.round(spanS - ts))) + 's', x, padT + plotH + 4);
  }
  ctx.strokeStyle = '#ccc';
  ctx.beginPath();
  ctx.moveTo(padL, padT); ctx.lineTo(padL, padT + plotH);
  ctx.lineTo(padL + plotW, padT + plotH); ctx.lineTo(padL + plotW, padT);
  ctx.stroke();
  ctx.save();
  ctx.beginPath(); ctx.rect(padL, padT, plotW, plotH); ctx.clip();
  ctx.strokeStyle = s.color; ctx.lineWidth = 1.4; ctx.lineJoin = 'round';
  ctx.beginPath();
  var started = false;
  for (var j = 0; j < pts.length; j++) {
    if (pts[j].v === null) { started = false; continue; }
    var px = xOf(pts[j].t), py = yOf(pts[j].v);
    if (!started) { ctx.moveTo(px, py); started = true; }
    else ctx.lineTo(px, py);
  }
  ctx.stroke();
  ctx.restore();
  ctx.lineWidth = 1;
}

// ---- 新增控件事件绑定 ----
document.getElementById('addSigBtn').addEventListener('click', function() { openForm(detailId, null); });
document.getElementById('addSig2Btn').addEventListener('click', function() { openForm('', null); });
document.getElementById('formCancel').addEventListener('click', closeForm);
document.getElementById('formSave').addEventListener('click', saveForm);
document.getElementById('pauseBtn').addEventListener('click', function() {
  chartPaused = !chartPaused;
  this.textContent = chartPaused ? '继续' : '暂停';
});
document.getElementById('winSel').addEventListener('change', renderCharts);
document.getElementById('curveRecBtn').addEventListener('click', curveRecToggle);
document.getElementById('csvExportBtn').addEventListener('click', curveRecExportCsv);
document.getElementById('cfgExportBtn').addEventListener('click', exportCfg);
document.getElementById('cfgImportBtn').addEventListener('click', function() {
  document.getElementById('cfgImportFile').click();
});
document.getElementById('cfgImportFile').addEventListener('change', function() {
  var f = this.files && this.files[0];
  this.value = '';
  if (!f) return;
  var reader = new FileReader();
  reader.onload = function() {
    var arr = null;
    try { arr = JSON.parse(reader.result); } catch (e) { arr = null; }
    if (!Array.isArray(arr) || !arr.length) { alert('配置文件无效（应为信号数组）'); return; }
    for (var i = 0; i < arr.length; i++) {
      var s = arr[i];
      if (!s || !/^0x[0-9a-fA-F]+$/.test(String(s.id || '')) || typeof s.name !== 'string') {
        alert('配置文件无效（第 ' + (i + 1) + ' 个信号缺少 id/name）');
        return;
      }
      s.id = normId(s.id);
    }
    configs = arr;
    saveCfg();   // localStorage + 设备 NVS
    if (currentView === 'detail') renderSigList();
    buildChartList();
    if (currentView === 'charts') renderCharts();
    toast('已导入 ' + arr.length + ' 个信号定义');
  };
  reader.readAsText(f);
});

// 曲线绘制节拍：Charts 页可见时重绘；记录中时刷新状态
setInterval(function() {
  if (currentView === 'charts' && !chartPaused) renderCharts();
  if (curveRecOn) curveRecUpdateUI();
}, 250);

loadCfg();
loadCfgFromDevice();

function setTopTab(id) {
  var tabs = ['tabMonitor', 'tabCharts'];
  for (var i = 0; i < tabs.length; i++) {
    var el = document.getElementById(tabs[i]);
    if (!el) continue;
    if (tabs[i] === id) el.classList.add('active');
    else el.classList.remove('active');
  }
}

document.getElementById('backBtn').addEventListener('click', showMain);

document.getElementById('viewDetail').addEventListener('scroll', function() {
  var el = document.getElementById('viewDetail');
  autoScrollDetail = el.scrollHeight - el.scrollTop - el.clientHeight < 50;
});

// 顶层页签切换
document.getElementById('tabMonitor').addEventListener('click', showMain);
document.getElementById('tabCharts').addEventListener('click', showCharts);

// 窗口尺寸变化时重绘（画布尺寸依赖像素，不能只靠 CSS 拉伸）
var chartResizeTimer = null;
window.addEventListener('resize', function() {
  clearTimeout(chartResizeTimer);
  chartResizeTimer = setTimeout(function() {
    if (currentView === 'charts') renderCharts();
  }, 150);
});

var pollBusy = false;
function pollMessages() {
  if (pollBusy) return;
  pollBusy = true;
  fetch('/api/messages')
    .then(function(r) { return r.text(); })
    .then(function(s) {
      var d = null;
      try { d = JSON.parse(s); } catch (e) { d = null; }
      if (d) {
        try { processMessages(d); } catch (e) { /* render errors keep polling */ }
      } else {
        document.getElementById('statusDot').className = 'dot off';
        document.getElementById('statusText').textContent = 'Bad: ' + s.slice(0, 60);
      }
      pollBusy = false;   // success path always releases the gate
    }, function() {
      document.getElementById('statusDot').className = 'dot off';
      document.getElementById('statusText').textContent = 'Disconnected';
      pollBusy = false;
    });
}

function processMessages(d) {
  if (!d || d.busy) return;   // server busy: keep local data, skip this cycle
  if (d.clk) {
    clkSync = !!d.clk.sync;
    clkBoot = d.clk.boot || 0;
    clkEp = d.clk.ep || 0;
  }
  if (d.total !== lastTotal) {
    allMessages = d.messages || [];
    allFreqs = d.freqs || [];
    lastTotal = d.total;
    // 追加到前端历史并采样曲线记录（只处理本次新到的帧）
    for (var i = 0; i < allMessages.length; i++) {
      var m = allMessages[i];
      if (!histFresh(m)) continue;
      var bytes = [];
      var hexParts = m.data.split(/\s+/);
      for (var k = 0; k < hexParts.length; k++) {
        if (hexParts[k]) bytes.push(parseInt(hexParts[k], 16));
      }
      var nid = normId(m.id);
      histPush(nid, m.t, bytes, m.dlc, m.ext);
      curveRecSample(nid, m.t, bytes, m.dlc);
    }
    if (currentView === 'main') renderMain();
    else if (currentView === 'detail') { renderSigList(); renderDetailTable(); }
  }
  document.getElementById('statusDot').className = 'dot on';
  document.getElementById('statusText').textContent = 'Connected';
}

setInterval(pollMessages, 200);
pollMessages();

// 页面加载即授时，之后每 10 分钟校准一次
syncClock();
setInterval(syncClock, 600000);

document.getElementById('clearBtn').addEventListener('click', function() {
  fetch('/api/clear', { method: 'POST' }).then(function() {
    allMessages = [];
    allFreqs = [];
    lastTotal = 0;
    if (currentView === 'main') renderMain();
    else renderDetailTable();
  });
});

document.getElementById('sendBtn').addEventListener('click', function() {
  var id = document.getElementById('sendId').value.trim();
  var dlc = parseInt(document.getElementById('sendDlc').value) || 0;
  var ext = document.getElementById('sendExt').checked;
  var data = document.getElementById('sendData').value.trim();
  if (!id) { alert('Please enter CAN ID'); return; }
  if (dlc < 0 || dlc > 8) { alert('DLC must be 0-8'); return; }
  fetch('/api/send', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: id, dlc: dlc, data: data, extended: ext })
  }).then(function(r) { return r.json(); })
    .then(function(d) { if (!d.ok) alert('Send failed'); })
    .catch(function(e) { alert('Send error: ' + e); });
});

document.getElementById('sendData').addEventListener('keydown', function(e) {
  if (e.key === 'Enter') document.getElementById('sendBtn').click();
});
</script>
</body>
</html>)rawliteral"
