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
        + '<td class="col-dlc">' + p.dlc + '</td>'
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
var histById = {};     // 有限的监控历史，不作为完整记录源
var monitorBoot = null;
var monitorSeq = -1;
var chartData = {};    // 已解码显示缓存，只保留最近 60 秒/12000 点
var editingKey = null; // 正在编辑的信号 key；null = 新增
var chartPaused = false;
var chartWinMs = 30000;
var stripRefs = [];

function normId(id) {
  var text = String(id || '').toLowerCase();
  return /^0x[0-9a-f]+$/.test(text) ? '0x' + parseInt(text.slice(2), 16).toString(16) : text;
}
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
  return /^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(String(c)) ? c : '#888888';
}
function signalTint(c) {
  c = safeColor(c);
  if (c.length === 4) c = '#' + c[1] + c[1] + c[2] + c[2] + c[3] + c[3];
  return c.slice(0, 7) + '33';
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
  var bitPos = sig.endian === 'intel' ? sig.start : (sig.start >> 3) * 8 + 7 - (sig.start & 7);
  if (!Number.isInteger(dlc) || dlc < 0 || dlc > 8 || bytes.length < dlc || bitPos + sig.len > dlc * 8) return null;
  // 原始整数用 BigInt 提取；物理值/曲线使用 JS Number，超过 53 位可能舍入。
  var val = BigInt(0);
  for (var i = 0; i < sig.len; i++) {
    var p = bitPos + i;
    var bit = sig.endian === 'intel' ? p & 7 : 7 - (p & 7);
    var b = BigInt((bytes[p >> 3] >> bit) & 1);
    if (sig.endian === 'intel') val |= b << BigInt(i);
    else val = (val << BigInt(1)) | b;
  }
  if (sig.signed && val >= (BigInt(1) << BigInt(sig.len - 1))) val -= BigInt(1) << BigInt(sig.len);
  return Number(val);
}
function decodePoint(frame, sig) {
  if (!frame || !frame.b || !Number.isInteger(frame.dlc) || frame.dlc < 1) return null;
  var raw = extractRaw(frame.b, frame.dlc, sig);
  if (raw === null) return null;
  var v = raw * sig.factor + sig.offset;
  return Number.isFinite(v) ? v : null;
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

// 所有请求都设超时，错误不清除记录或配置草稿。
async function apiJson(url, options, timeoutMs) {
  var controller = new AbortController();
  var timer = setTimeout(function() { controller.abort(); }, timeoutMs || 10000);
  try {
    var opts = Object.assign({}, options || {}, { signal: controller.signal });
    var response = await fetch(url, opts);
    var result;
    try { result = await response.json(); } catch (e) { throw new Error('设备响应格式错误'); }
    if (!response.ok || result.ok === false) throw new Error(result.error || result.message || ('HTTP ' + response.status));
    return result;
  } finally { clearTimeout(timer); }
}
function postJson(url, body) {
  return apiJson(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
}
function utf8Bytes(s) { return new TextEncoder().encode(s).length; }
function validateConfigs(arr) {
  if (!Array.isArray(arr) || arr.length > 64) throw new Error('信号定义应为数组，最多 64 个');
  var seen = {}, out = arr.map(function(s, i) {
    var error = '第 ' + (i + 1) + ' 个信号定义无效';
    if (!s || !/^0x[0-9a-f]{1,8}$/i.test(String(s.id)) || parseInt(s.id.slice(2), 16) > 0x1fffffff) throw new Error(error + '：CAN ID');
    if (typeof s.name !== 'string' || !s.name.trim() || utf8Bytes(s.name) > 64 || typeof s.unit !== 'string' || utf8Bytes(s.unit) > 32) throw new Error(error + '：名称/单位');
    if (!Number.isInteger(s.start) || s.start < 0 || s.start > 63 || !Number.isInteger(s.len) || s.len < 1 || s.len > 64) throw new Error(error + '：起始位/位长');
    if (s.endian !== 'intel' && s.endian !== 'moto') throw new Error(error + '：字节序');
    var bit = s.endian === 'intel' ? s.start : (s.start >> 3) * 8 + 7 - (s.start & 7);
    if (bit + s.len > 64) throw new Error(error + '：超出 8 字节报文');
    if (typeof s.signed !== 'boolean' || typeof s.enabled !== 'boolean' || !Number.isFinite(s.factor) || !Number.isFinite(s.offset)) throw new Error(error + '：符号/倍率/偏移');
    if (!/^#(?:[0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i.test(s.color)) throw new Error(error + '：颜色');
    var v = { id: normId(s.id), name: s.name, start: s.start, len: s.len, endian: s.endian,
      signed: s.signed, factor: s.factor, offset: s.offset, unit: s.unit, color: s.color, enabled: s.enabled };
    if (seen[sigKey(v)]) throw new Error(error + '：同 ID 同名信号');
    seen[sigKey(v)] = true;
    return v;
  });
  if (utf8Bytes(JSON.stringify(out)) > 3500) throw new Error('信号配置超过设备 3500 字节上限');
  return out;
}

// 配置草稿与录制快照互不覆盖；写请求串行，版本不同不显示旧响应为已保存。
var cfgPushTimer = null, cfgReady = false, cfgPushBusy = false;
var cfgVersion = 0, cfgSavedVersion = -1, cfgError = '', cfgRetry = 0;
function cfgStatus(message, error) {
  var el = document.getElementById('cfgStatus');
  if (el) { el.textContent = message; el.className = 'cfg-status' + (error ? ' error' : ''); }
}
function cacheCfg() {
  try { localStorage.setItem(CFG_KEY, JSON.stringify(configs)); } catch (e) { cfgStatus('浏览器缓存失败，设备保存仍可重试', true); }
}
function resetMonitorDecode() {
  if (recordState && recordState.session) return;
  chartData = {};
  var defs = displaySignals();
  for (var id in histById) {
    var hist = histById[id];
    for (var i = 0; i < hist.length; i++) cacheFrameDecode(id, hist[i], defs);
  }
}
function saveCfg() {
  try { configs = validateConfigs(configs); } catch (e) { cfgError = e.message; cfgStatus(cfgError, true); return false; }
  cfgVersion++; cfgRetry = 0; cfgError = '';
  cacheCfg(); resetMonitorDecode();
  cfgStatus('配置待保存' + (recordState && recordState.session ? '（仅用于下次录制）' : ''));
  clearTimeout(cfgPushTimer);
  if (cfgReady) cfgPushTimer = setTimeout(pushCfgToDevice, 500);
  return true;
}
async function pushCfgToDevice() {
  if (!cfgReady || cfgPushBusy || cfgSavedVersion === cfgVersion) return;
  cfgPushBusy = true;
  var version = cfgVersion, body;
  try {
    body = validateConfigs(configs);
    cfgStatus('正在保存配置…');
    var result = await postJson('/api/signals', body);
    if (result.ok !== true || result.n !== body.length) throw new Error('设备未确认完整保存');
    cfgSavedVersion = version; cfgError = ''; cfgRetry = 0;
    if (version === cfgVersion) cfgStatus('配置已保存' + (recordState && recordState.session ? '（下次录制生效）' : ''));
  } catch (e) {
    cfgError = e.message || '配置保存失败';
    cfgStatus('配置保存失败：' + cfgError + '；保留草稿', true);
    if (cfgRetry < 2 && version === cfgVersion) {
      cfgRetry++;
      clearTimeout(cfgPushTimer);
      cfgPushTimer = setTimeout(pushCfgToDevice, cfgRetry * 2000);
    }
  } finally {
    cfgPushBusy = false;
    if (version !== cfgVersion) { clearTimeout(cfgPushTimer); cfgPushTimer = setTimeout(pushCfgToDevice, 0); }
  }
}
async function loadCfgFromDevice() {
  var version = cfgVersion;
  try {
    var d = await apiJson('/api/signals?meta=1');
    if (typeof d.configured !== 'boolean' || !Array.isArray(d.signals)) throw new Error('设备配置状态无效');
    var arr = validateConfigs(d.signals);
    cfgReady = true;
    if (version !== cfgVersion) { cfgStatus('配置草稿待保存'); await pushCfgToDevice(); return; }
    if (d.configured) {
      configs = arr; cfgSavedVersion = cfgVersion; cfgError = ''; cacheCfg(); resetMonitorDecode();
      cfgStatus('已加载设备配置（' + arr.length + ' 个信号）');
    } else { cfgStatus('设备尚未配置，正在保存本地定义'); await pushCfgToDevice(); }
    if (currentView === 'detail') renderSigList();
    if (currentView === 'charts') { buildChartList(); renderCharts(); }
  } catch (e) {
    cfgStatus('无法加载设备配置：' + e.message + '；保留本地草稿', true);
  } finally { curveRecUpdateUI(); }
}
function loadCfg() {
  try {
    var s = localStorage.getItem(CFG_KEY);
    if (s !== null) { configs = validateConfigs(JSON.parse(s)); return; }
  } catch (e) {}
  configs = [
    { id: '0x18ff0182', name: 'Torque', start: 8, len: 16, endian: 'intel', signed: true, factor: 1, offset: -3000, unit: 'Nm', color: '#188038', enabled: true },
    { id: '0x18ff0182', name: 'Speed', start: 24, len: 16, endian: 'intel', signed: true, factor: 1, offset: -15000, unit: 'rpm', color: '#9334e6', enabled: true },
    { id: '0x18ff0282', name: 'Current', start: 0, len: 16, endian: 'intel', signed: false, factor: 0.1, offset: -1000, unit: 'A', color: '#1a73e8', enabled: true },
    { id: '0x18ff0282', name: 'Voltage', start: 16, len: 16, endian: 'intel', signed: false, factor: 0.1, offset: 0, unit: 'V', color: '#ea4335', enabled: true }
  ];
  cacheCfg(); // 等设备 configured 状态返回后才决定迁移，避免覆盖已清空配置。
}

// 监控快照只用于详情/非录制曲线，按开机标识+帧序号去重。
function histFresh(m) {
  if (!Number.isInteger(m.seq) || m.seq <= monitorSeq) return false;
  monitorSeq = m.seq;
  return true;
}
function histPush(id, t, bytes, dlc, ext) {
  if (!histById[id]) histById[id] = [];
  var arr = histById[id];
  arr.push({ t: t, b: bytes, dlc: dlc, e: ext ? 1 : 0 });
  if (arr.length > 4000) arr.splice(0, arr.length - 4000);
}
var CHART_CAP = 12000;
function displaySignals() { return recordState && recordState.session ? recordSignals : configs; }
function chartPush(sig, t, v) {
  var key = sigKey(sig), q = chartData[key];
  if (!q) q = chartData[key] = { t: new Float64Array(CHART_CAP), v: new Float64Array(CHART_CAP), head: 0, n: 0 };
  while (q.n && q.t[q.head] < t - 60000) { q.head = (q.head + 1) % CHART_CAP; q.n--; }
  var pos = (q.head + q.n) % CHART_CAP;
  q.t[pos] = t; q.v[pos] = v === null ? NaN : v;
  if (q.n === CHART_CAP) q.head = (q.head + 1) % CHART_CAP;
  else q.n++;
}
function cacheFrameDecode(id, frame, defs) {
  for (var i = 0; i < defs.length; i++) {
    var sig = defs[i];
    if (sig.id === id && sig.enabled) chartPush(sig, frame.t, decodePoint(frame, sig));
  }
}
function frameBytes(m) {
  if (!Number.isInteger(m.dlc) || m.dlc < 0 || m.dlc > 8) throw new Error('报文 DLC 无效');
  if (Array.isArray(m.data)) {
    if (m.data.length < m.dlc || m.data.some(function(v) { return !Number.isInteger(v) || v < 0 || v > 255; })) throw new Error('报文字节无效');
    return m.data.slice(0, m.dlc);
  }
  var text = String(m.data || '').trim(), parts = text ? text.split(/\s+/) : [];
  if (parts.length < m.dlc || parts.some(function(v) { return !/^[0-9a-f]{2}$/i.test(v); })) throw new Error('报文字节无效');
  return parts.slice(0, m.dlc).map(function(v) { return parseInt(v, 16); });
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
    if (owner) html += '<span style="background:' + signalTint(owner.color) + ';border-bottom:2px solid ' + safeColor(owner.color) + '">' + bytes[j] + '</span>';
    else html += bytes[j];
    if (j < bytes.length - 1) html += ' ';
  }
  return html;
}

// 完整记录来自设备端原始帧。每帧只解码一次，结果同时进入完整存储和曲线缓存。
var recordState = null, recordSignals = [], recordSigs = [], recordGroups = [], recordGroupById = {};
var recordChunks = [], recordNext = 0, recordMemoryBytes = 0;
var recordClient = (function() {
  if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
    var words = crypto.getRandomValues(new Uint32Array(2));
    return words[0].toString(16).padStart(8, '0') + words[1].toString(16).padStart(8, '0');
  }
  return Math.floor(Math.random() * 0x100000000).toString(16).padStart(8, '0')
    + ((Date.now() ^ Math.floor(Math.random() * 0x100000000)) >>> 0).toString(16).padStart(8, '0');
})();
var recordAcked = false;
var RECORD_CHUNK = 512, RECORD_MEMORY_MAX = 256 * 1024 * 1024;
var curveRecOn = false, curveRecStartT = 0, curveRecCount = 0;
var recordReady = false, recordOpBusy = false, recordSyncPromise = null, recordSyncError = '', recordRetryAt = 0;
var recordSourceError = '';
var csvExportBusy = false, csvMailBusy = false, csvMailGeneration = 0, csvProgress = '';
function recordComplete() {
  return !!(recordState && recordState.session && !recordState.on && recordNext === recordState.cnt && recordAcked && !recordSyncError);
}
function updateRecordState(rec) {
  if (!rec || !Number.isInteger(rec.cnt) || rec.cnt < 0 || typeof rec.on !== 'boolean') throw new Error('设备记录状态无效');
  if (recordState && rec.session === recordState.session) {
    // GET 的旧快照可能在 Stop 响应之后返回；拒绝状态倒退，重试当前游标。
    if (rec.cnt < recordState.cnt || (!recordState.on && rec.on)) throw new Error('收到过期记录状态，正在重新同步');
    if (rec.on || rec.cnt !== recordState.cnt) recordAcked = false;
    rec = Object.assign({}, rec, {
      full: !!(rec.full || recordState.full), rx_lost: Math.max(rec.rx_lost || 0, recordState.rx_lost || 0),
      drop: Math.max(rec.drop || 0, recordState.drop || 0) });
  }
  recordState = rec;
  curveRecOn = rec.on;
}
function adoptRecord(d, allowReplace) {
  if (!d || !d.rec) throw new Error('设备未返回记录状态');
  var rec = d.rec;
  if (recordState && recordState.session && rec.session !== recordState.session && !allowReplace) {
    var sourceError = new Error('设备记录已改变或重启；已有网页记录仍保留，请勿覆盖未保存数据');
    sourceError.sourceChanged = true;
    throw sourceError;
  }
  if (!rec.session || /^0+$/.test(String(rec.session))) {
    recordState = null; recordSignals = []; recordSigs = []; recordReady = true;
    curveRecOn = false; recordAcked = false; recordSyncError = ''; recordSourceError = ''; recordRetryAt = 0; curveRecUpdateUI(); return;
  }
  if (!/^[0-9a-f]{16}$/i.test(rec.session)) throw new Error('设备记录编号无效');
  var defs = validateConfigs(d.signals);
  // 若网页已接管同一会话，不重置游标和解码数据。
  if (recordState && recordState.session === rec.session) { updateRecordState(rec); recordReady = true; return; }
  recordSignals = defs; recordSigs = defs.filter(function(sig) { return sig.enabled; });
  recordChunks = []; recordGroups = []; recordGroupById = {};
  recordNext = 0; recordMemoryBytes = 0; curveRecCount = 0; chartData = {}; recordAcked = false;
  recordSyncError = ''; recordSourceError = ''; recordRetryAt = 0; csvMailGeneration++;
  recordSigs.forEach(function(sig, index) {
    var group = recordGroupById[sig.id];
    if (!group) {
      group = { id: sig.id, index: recordGroups.length, sigs: [], columns: [], blocks: [], count: 0 };
      recordGroupById[sig.id] = group; recordGroups.push(group);
    }
    group.sigs.push(sig); group.columns.push(index);
  });
  recordState = null; updateRecordState(rec); recordReady = true;
  curveRecStartT = Date.now() - (rec.ms || 0);
  buildChartList(); csvMailState(''); curveRecUpdateUI();
}
function reserveRecordArrays(bytes) {
  if (recordMemoryBytes + bytes > RECORD_MEMORY_MAX) throw new Error('浏览器记录缓存达到安全上限；设备原始记录仍保留，请勿开始新记录');
}
function appendRecordFrame(m, bytes) {
  var index = recordNext, blockNo = Math.floor(index / RECORD_CHUNK), pos = index % RECORD_CHUNK;
  var chunk = recordChunks[blockNo], group = recordGroupById[normId(m.id)];
  if (!chunk) {
    var size = RECORD_CHUNK * 13;
    reserveRecordArrays(size);
    chunk = { t: new Float64Array(RECORD_CHUNK), g: new Uint8Array(RECORD_CHUNK), p: new Uint32Array(RECORD_CHUNK), n: 0 };
    recordChunks.push(chunk); recordMemoryBytes += size;
  }
  var groupPos = group ? group.count : 0, values = [], groupBlock;
  if (group) {
    var groupBlockNo = Math.floor(groupPos / RECORD_CHUNK);
    groupBlock = group.blocks[groupBlockNo];
    if (!groupBlock) {
      var valueSize = RECORD_CHUNK * 8 * group.sigs.length;
      reserveRecordArrays(valueSize);
      var arrays = group.sigs.map(function() { return new Float64Array(RECORD_CHUNK); });
      groupBlock = { v: arrays }; group.blocks.push(groupBlock); recordMemoryBytes += valueSize;
    }
    for (var i = 0; i < group.sigs.length; i++) values.push(decodePoint({ b: bytes, dlc: m.dlc }, group.sigs[i]));
  }
  // 所有存储分配成功后提交本帧；任何失败都保留已提交游标及设备源数据。
  chunk.t[pos] = m.t; chunk.g[pos] = group ? group.index : 255; chunk.p[pos] = groupPos;
  if (group) {
    for (var j = 0; j < values.length; j++) {
      var value = values[j];
      groupBlock.v[j][groupPos % RECORD_CHUNK] = value === null ? NaN : value;
      if (value !== null) curveRecCount++;
    }
    group.count++;
  }
  chunk.n++; recordNext++;
  // 曲线分配失败不影响已提交完整记录；曲线允许缺显示点。
  if (group) for (var k = 0; k < values.length; k++) {
    try { chartPush(group.sigs[k], m.t, values[k]); } catch (e) { /* 原始记录和完整解码数据仍在 */ }
  }
}
function ingestRecordBatch(d) {
  if (!recordState || !d || !d.rec || d.rec.session !== recordState.session) throw new Error('记录编号改变；本地数据已保留');
  if (!Array.isArray(d.frames) || d.frames.length > 128 || d.from !== recordNext || d.next !== d.from + d.frames.length || d.next > d.rec.cnt) throw new Error('记录序号不连续，等待重试');
  var bytes = d.frames.map(function(m, i) {
    if (m.seq !== d.from + i || !Number.isFinite(m.t) || m.t < 0 || !/^0x[0-9a-f]+$/i.test(String(m.id))) throw new Error('记录帧顺序/内容无效，等待重试');
    return frameBytes(m);
  });
  updateRecordState(d.rec);
  for (var i = 0; i < d.frames.length; i++) appendRecordFrame(d.frames[i], bytes[i]);
  recordSyncError = '';
  curveRecUpdateUI();
}
function yieldUi() { return new Promise(function(resolve) { setTimeout(resolve, 0); }); }
async function ackRecord() {
  if (!recordState || recordState.on || recordNext !== recordState.cnt || recordAcked) return;
  var session = recordState.session, count = recordNext;
  var result = await postJson('/api/rec/ack', { session: session, client: recordClient, count: count });
  if (result.ok !== true || result.session !== session || result.count !== count || !recordState || recordState.session !== session || recordState.on || recordNext !== count || recordState.cnt !== count) throw new Error('设备未确认本次同步完整');
  recordAcked = true; recordSyncError = ''; recordRetryAt = 0;
}
function syncRecord() {
  if (recordSyncPromise) return recordSyncPromise;
  if (recordComplete()) return Promise.resolve();
  if (!recordReady || !recordState || !recordState.session || csvExportBusy || csvMailBusy) return Promise.resolve();
  var session = recordState.session;
  recordSyncPromise = (async function() {
    try {
      var cycles = 0;
      if (!recordState.on && recordNext === recordState.cnt && !recordAcked) {
        await ackRecord();
        return;
      }
      do {
        var d = await apiJson('/api/rec/data?session=' + encodeURIComponent(session) + '&from=' + recordNext + '&client=' + recordClient);
        if (!recordState || recordState.session !== session) return;
        ingestRecordBatch(d);
        if (recordNext >= recordState.cnt) break;
        if (!d.frames.length) throw new Error('设备未返回尚未同步的帧，等待重试');
        await yieldUi();
        // 录制期间限制每轮追赶工作量，停止后继续分批补齐全部帧。
        if (recordState.on && ++cycles >= 8) break;
      } while (true);
      await ackRecord();
      recordRetryAt = 0;
    } catch (e) {
      recordSyncError = e.message || '同步失败'; recordRetryAt = Date.now() + 2000;
    } finally {
      recordSyncPromise = null; curveRecUpdateUI();
    }
  })();
  curveRecUpdateUI();
  return recordSyncPromise;
}
async function recoverRecord() {
  if (recordOpBusy) return;
  recordOpBusy = true; curveRecUpdateUI();
  try {
    var d = await apiJson('/api/rec/status');
    adoptRecord(d);
    await syncRecord();
  } catch (e) {
    if (e.sourceChanged) recordSourceError = e.message;
    // 已确认完整的本地记录仍可离线导出；新设备状态不覆盖它。
    if (!recordComplete()) recordSyncError = '读取记录状态失败：' + e.message;
    recordRetryAt = Date.now() + 2000;
  }
  finally { recordOpBusy = false; curveRecUpdateUI(); }
}
function csvMailState(state, message) {
  var btn = document.getElementById('csvMailBtn');
  if (!btn) return;
  btn.classList.remove('sending', 'success', 'failed');
  if (state) btn.classList.add(state);
  btn.disabled = csvMailBusy || csvExportBusy || recordOpBusy || !recordComplete();
  btn.setAttribute('aria-busy', csvMailBusy ? 'true' : 'false');
  btn.title = message || '发送记录 CSV 到邮箱'; btn.setAttribute('aria-label', btn.title);
}
function curveRecUpdateUI() {
  var btn = document.getElementById('curveRecBtn'), st = document.getElementById('curveRecStatus');
  if (!btn || !st) return;
  btn.disabled = recordOpBusy || csvExportBusy || csvMailBusy || !recordReady || (!curveRecOn && !cfgReady);
  btn.textContent = curveRecOn ? '\u25A0 Stop' : '\u25CF Record';
  btn.classList.toggle('recording', curveRecOn);
  var text = '';
  if (recordState && recordState.session) {
    text = (curveRecOn ? 'REC ' : '') + recordNext + '/' + recordState.cnt + ' 帧 · ' + curveRecCount + ' 点';
    if (recordState.full) text += ' · 录满已停止';
    if (recordSyncError) text += ' · ' + recordSyncError;
    else if (!curveRecOn) text += recordComplete() ? ' · 同步完整' : (recordNext === recordState.cnt ? ' · 正在确认同步' : ' · 正在补齐');
    if (recordState.rx_lost || recordState.drop) text += ' · 采集异常：接收丢失 ' + (recordState.rx_lost || 0) + '，未保存 ' + (recordState.drop || 0);
    else if (recordState.quality_known === false) text += ' · 接收完整性未知';
  } else if (recordSyncError) text = recordSyncError;
  if (recordSourceError) text += ' · ' + recordSourceError;
  if (csvProgress) text += ' · ' + csvProgress;
  st.textContent = text;
  st.className = 'record-status' + (recordSourceError || recordSyncError || (recordState && (recordState.full || recordState.rx_lost || recordState.drop)) ? ' error' : '');
  var exp = document.getElementById('csvExportBtn');
  if (exp) exp.disabled = csvMailBusy || csvExportBusy || recordOpBusy || !recordComplete();
  var mail = document.getElementById('csvMailBtn');
  if (mail) { mail.disabled = csvMailBusy || csvExportBusy || recordOpBusy || !recordComplete(); mail.setAttribute('aria-busy', csvMailBusy ? 'true' : 'false'); }
  var retry = document.getElementById('recordRetryBtn');
  if (retry) { retry.hidden = !recordSyncError; retry.disabled = recordOpBusy || !!recordSyncPromise || csvExportBusy || csvMailBusy; }
}
async function curveRecToggle() {
  if (recordOpBusy || csvExportBusy || csvMailBusy || !recordReady) return;
  recordOpBusy = true; curveRecUpdateUI();
  var allowReplacement = false;
  try {
    if (curveRecOn) {
      var stopped = await postJson('/api/rec/stop', { session: recordState.session });
      if (!stopped.rec || stopped.rec.session !== recordState.session) throw new Error('停止记录编号不匹配');
      updateRecordState(stopped.rec);
      if (recordState.on) throw new Error('设备尚未确认停止');
      await syncRecord();
      toast(recordComplete() ? '已停止，记录已同步完整' : '已停止，正在补齐；源记录已保留');
    } else {
      if (!cfgReady) throw new Error('先重试加载设备配置');
      var defs = validateConfigs(configs);
      if (!defs.some(function(sig) { return sig.enabled; })) throw new Error('没有已启用的信号');
      if (recordState && recordState.session) {
        if (!recordComplete()) { await syncRecord(); if (!recordComplete()) throw new Error('先同步完整当前记录，再开始新记录'); }
        if (!confirm('开始新记录将替换设备内上一份记录。请确认已下载或邮件发送需要保留的数据。继续？')) return;
        await postJson('/api/rec/release', { session: recordState.session, client: recordClient });
      }
      allowReplacement = true; // 本次开始动作已通过上一记录的显式替换确认。
      var started = await postJson('/api/rec/start', { signals: defs });
      if (!started.rec || !started.rec.on) throw new Error('设备尚未确认开始');
      adoptRecord(started, true);
      await syncRecord(); toast('设备开始记录；实时曲线共用本次固定信号定义');
    }
  } catch (e) {
    toast(e.message || '记录操作失败；已有数据仍保留');
    // 请求超时可能已被设备执行；重新读取状态，避免网页误判。
    try { adoptRecord(await apiJson('/api/rec/status'), allowReplacement); await syncRecord(); }
    catch (err) { if (err.sourceChanged) recordSourceError = err.message; if (!recordComplete()) recordSyncError = err.message; }
  } finally { recordOpBusy = false; curveRecUpdateUI(); }
}
async function curveRecBuildCsv(maxBytes) {
  if (!recordComplete()) throw new Error('停止并同步完整记录后才能导出');
  if (!recordNext || !recordSigs.length) throw new Error('暂无记录数据');
  maxBytes = maxBytes || 128 * 1024 * 1024;
  var parts = [], pending = [], totalBytes = 0, generation = csvMailGeneration;
  var head = 'no,time_rel_ms';
  recordSigs.forEach(function(sig) { head += ',' + csvSafe(sig.name) + (sig.unit ? '(' + csvSafe(sig.unit) + ')' : '') + '@' + sig.id; });
  pending.push(head + '\n');
  var t0 = recordChunks[0].t[0];
  for (var i = 0; i < recordNext; i++) {
    if (generation !== csvMailGeneration) throw new Error('记录会话改变，已取消导出');
    var chunk = recordChunks[Math.floor(i / RECORD_CHUNK)], pos = i % RECORD_CHUNK;
    var cells = new Array(recordSigs.length).fill(''), group = recordGroups[chunk.g[pos]];
    if (group) {
      var gp = chunk.p[pos], values = group.blocks[Math.floor(gp / RECORD_CHUNK)].v;
      for (var j = 0; j < group.columns.length; j++) {
        var v = values[j][gp % RECORD_CHUNK];
        if (Number.isFinite(v)) cells[group.columns[j]] = String(v);
      }
    }
    pending.push(i + ',' + (chunk.t[pos] - t0) + ',' + cells.join(',') + '\n');
    if (pending.length >= 512 || i === recordNext - 1) {
      var text = pending.join(''); totalBytes += utf8Bytes(text);
      if (totalBytes > maxBytes) throw new Error('CSV 超过 ' + Math.round(maxBytes / 1048576) + 'MB 上限；设备记录和本地数据仍保留');
      parts.push(text); pending = [];
      csvProgress = '生成 CSV ' + Math.floor((i + 1) * 100 / recordNext) + '%'; curveRecUpdateUI();
      await yieldUi();
    }
  }
  return { blob: new Blob(parts, { type: 'text/csv; charset=utf-8' }), filename: 'can_signals_' + recordState.session + '.csv', rows: recordNext, signals: recordSigs.length };
}
async function curveRecExportCsv() {
  if (csvExportBusy || csvMailBusy || recordOpBusy || !recordComplete()) return;
  csvExportBusy = true; curveRecUpdateUI();
  try {
    var csv = await curveRecBuildCsv(), a = document.createElement('a'), url = URL.createObjectURL(csv.blob);
    a.href = url; a.download = csv.filename; a.click();
    setTimeout(function() { URL.revokeObjectURL(url); }, 1000);
    toast('已导出 ' + csv.rows + ' 行 × ' + csv.signals + ' 信号');
  } catch (e) { toast('导出失败：' + e.message + '；源记录已保留'); }
  finally { csvExportBusy = false; csvProgress = ''; curveRecUpdateUI(); }
}
async function curveRecSendCsv() {
  if (csvMailBusy || csvExportBusy || recordOpBusy || !recordComplete()) return;
  csvMailBusy = true; curveRecUpdateUI(); csvMailState('sending', '正在生成并发送记录 CSV…');
  var state = 'failed', message = '';
  try {
    var csv = await curveRecBuildCsv(20 * 1024 * 1024);
    var result = await apiJson('/api/mail/send', { method: 'POST', headers: { 'Content-Type': 'text/csv; charset=utf-8', 'X-CSV-Filename': csv.filename }, body: csv.blob }, 90000);
    if (result.ok !== true) throw new Error(result.message || '发送失败');
    state = 'success'; message = result.message || '邮件服务已接受发送'; toast(message);
  } catch (e) {
    message = e.message || '发送失败；源记录已保留';
    if (e.name === 'AbortError' || e instanceof TypeError) message = '连接中断或超时，发送结果未确认；源记录已保留，请检查邮箱后再决定是否重试';
    toast(message);
  } finally { csvMailBusy = false; csvProgress = ''; curveRecUpdateUI(); csvMailState(state, message); }
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
  var start = Number(document.getElementById('f_start').value);
  var len = Number(document.getElementById('f_len').value);
  if (start < 0 || start > 63) { alert('起始位 0-63'); return; }
  if (len < 1 || len > 64) { alert('位长 1-64'); return; }
  var s = {
    id: id, name: name, start: start, len: len,
    endian: document.getElementById('f_endian').value,
    signed: document.getElementById('f_signed').value === '1',
    factor: Number(document.getElementById('f_factor').value),
    offset: Number(document.getElementById('f_offset').value),
    unit: document.getElementById('f_unit').value.trim(),
    color: document.getElementById('f_color').value,
    enabled: true
  };
  var updated = configs.slice(), found = false;
  if (editingKey) {
    for (var i = 0; i < updated.length; i++) {
      if (sigKey(updated[i]) === editingKey) { s.enabled = updated[i].enabled; updated[i] = s; found = true; break; }
    }
    if (!found) { alert('信号定义已改变，请重新打开编辑'); return; }
  } else updated.push(s);
  try { configs = validateConfigs(updated); } catch (e) { alert(e.message); return; }
  saveCfg();
  closeForm();
  if (currentView === 'detail') renderSigList();
  buildChartList();
  if (currentView === 'charts') renderCharts();
  toast('已更新配置草稿 ' + s.name + '，等待设备保存');
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
  var wrap = document.getElementById('chartList'), defs = displaySignals(), frozen = !!(recordState && recordState.session);
  var html = '';
  for (var i = 0; i < defs.length; i++) {
    var s = defs[i], key = keyAttr(s);
    html += '<div class="strip" id="strip_' + i + '"><div class="strip-head">'
      + '<span class="cdot" style="background:' + safeColor(s.color) + '"></span>'
      + '<input type="checkbox" ' + (s.enabled ? 'checked' : '') + (frozen ? ' disabled' : '') + ' onchange="toggleSig(\'' + key + '\');buildChartList();renderCharts();">'
      + '<span class="signame">' + escHtml(s.name) + '</span>'
      + '<span class="sigdef">@' + escHtml(s.id) + (s.unit ? ' (' + escHtml(s.unit) + ')' : '') + '</span>'
      + '<span class="sigval" id="lv_' + i + '">-</span>'
      + (frozen ? '<span class="sigdef">本次固定配置</span>' : '<button onclick="editSig(\'' + key + '\')">编辑</button><button class="btn danger" onclick="delSig(\'' + key + '\')">删除</button>')
      + '</div><canvas id="cv_' + i + '"></canvas></div>';
  }
  wrap.innerHTML = html || '<div class="empty">暂无信号，点击"+ 添加曲线"定义一个信号</div>';
  for (var j = 0; j < defs.length; j++) stripRefs[j] = document.getElementById('cv_' + j);
}
function renderCharts() {
  chartWinMs = parseInt(document.getElementById('winSel').value) || 30000;
  var maxT = 0;
  for (var key in chartData) {
    var q = chartData[key];
    if (q.n) maxT = Math.max(maxT, q.t[(q.head + q.n - 1) % CHART_CAP]);
  }
  var t0 = maxT - chartWinMs, wrap = document.getElementById('viewCharts');
  var cssW = Math.max(280, (wrap.clientWidth || window.innerWidth) - 24), dpr = window.devicePixelRatio || 1, defs = displaySignals();
  for (var i = 0; i < defs.length; i++) {
    var s = defs[i], cv = stripRefs[i];
    if (!cv) continue;
    var strip = document.getElementById('strip_' + i);
    if (strip) strip.style.display = s.enabled ? '' : 'none';
    if (!s.enabled) continue;
    var cssH = 150;
    cv.style.width = cssW + 'px'; cv.style.height = cssH + 'px';
    if (cv.width !== Math.round(cssW * dpr)) cv.width = Math.round(cssW * dpr);
    if (cv.height !== Math.round(cssH * dpr)) cv.height = Math.round(cssH * dpr);
    var ctx = cv.getContext('2d'); ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    var pts = [], lastV = null, q = chartData[sigKey(s)];
    if (q) for (var k = 0; k < q.n; k++) {
      var index = (q.head + k) % CHART_CAP;
      if (q.t[index] < t0) continue;
      var v = Number.isFinite(q.v[index]) ? q.v[index] : null;
      pts.push({ t: q.t[index], v: v }); if (v !== null) lastV = v;
    }
    var lv = document.getElementById('lv_' + i);
    if (lv) lv.textContent = lastV === null ? '-' : fmtVal(lastV) + ' ' + s.unit;
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
document.getElementById('csvMailBtn').addEventListener('click', curveRecSendCsv);
document.getElementById('cfgExportBtn').addEventListener('click', exportCfg);
document.getElementById('cfgImportBtn').addEventListener('click', function() {
  document.getElementById('cfgImportFile').click();
});
document.getElementById('cfgImportFile').addEventListener('change', function() {
  var f = this.files && this.files[0];
  this.value = '';
  if (!f) return;
  if (f.size > 10000) { alert('配置文件过大'); return; }
  var reader = new FileReader();
  reader.onload = function() {
    var arr = null;
    try { arr = JSON.parse(reader.result); } catch (e) { arr = null; }
    try { arr = validateConfigs(arr); } catch (e) { alert(e.message); return; }
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
  curveRecUpdateUI();
}, 250);

loadCfg();
loadCfgFromDevice();
recoverRecord();
setInterval(function() {
  if (!recordReady) { if (!recordOpBusy && Date.now() >= recordRetryAt) recoverRecord(); return; }
  if (recordState && recordState.session && !recordComplete() && Date.now() >= recordRetryAt) syncRecord();
}, 200);
document.getElementById('recordRetryBtn').addEventListener('click', function() {
  recordRetryAt = 0;
  if (recordReady && recordState && recordState.session) syncRecord(); else recoverRecord();
});
document.getElementById('cfgRetryBtn').addEventListener('click', function() {
  cfgRetry = 0;
  if (cfgReady) pushCfgToDevice(); else loadCfgFromDevice();
});

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
async function pollMessages() {
  if (pollBusy) return;
  pollBusy = true;
  try { processMessages(await apiJson('/api/messages')); }
  catch (e) {
    document.getElementById('statusDot').className = 'dot off';
    document.getElementById('statusText').textContent = 'Disconnected';
  } finally { pollBusy = false; }
}

function processMessages(d) {
  if (!d || d.busy) return;   // server busy: keep local data, skip this cycle
  if (d.clk) {
    clkSync = !!d.clk.sync;
    clkBoot = d.clk.boot || 0;
    clkEp = d.clk.ep || 0;
  }
  var bootChanged = monitorBoot !== d.boot;
  if (bootChanged) { monitorBoot = d.boot; monitorSeq = -1; histById = {}; if (!recordState) chartData = {}; }
  if (d.total !== lastTotal || bootChanged) {
    allMessages = d.messages || []; allFreqs = d.freqs || []; lastTotal = d.total;
    for (var i = 0; i < allMessages.length; i++) {
      var m = allMessages[i];
      if (!histFresh(m)) continue;
      var bytes = frameBytes(m), nid = normId(m.id);
      histPush(nid, m.t, bytes, m.dlc, m.ext);
      // 已接管会话的目标帧由 record/data 单次解码，监控快照不重复解码。
      if (!(recordState && recordState.session && recordGroupById[nid])) cacheFrameDecode(nid, { t: m.t, b: bytes, dlc: m.dlc }, displaySignals());
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
