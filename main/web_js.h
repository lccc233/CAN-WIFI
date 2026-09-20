#pragma once

#define PAGE_JS R"rawliteral(
<script>
var allMessages = [];
var allFreqs = [];
var allVi = [];
var lastTotal = 0;
var lastRecOn = false;
var currentView = 'main';   // 'main' | 'detail' | 'vi'
var detailId = '';
var detailMode = 'chart';   // 详情视图默认显示曲线
var autoScrollMain = true;
var autoScrollDetail = true;

var contentMain = document.getElementById('viewMain');
var contentDetail = document.getElementById('viewDetail');
var contentVI = document.getElementById('viewVI');

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
      + '<td class="col-data">' + m.data + '</td>'
      + '<td class="col-time">' + fmtFrameTime(m.t) + '</td>'
      + '</tr>';
  }
  tbody.innerHTML = html;
  document.getElementById('msgCount').textContent = allMessages.length + ' msgs, ' + ids.length + ' IDs';
}

function renderDetail() {
  if (detailMode === 'chart') renderChart();
  else renderDetailTable();
}

function renderDetailTable() {
  var filtered = [];
  for (var i = 0; i < allMessages.length; i++) {
    if (allMessages[i].id === detailId) filtered.push(allMessages[i]);
  }
  filtered.reverse();
  var tbody = document.getElementById('detailBody');
  var html = '';
  for (var i = 0; i < filtered.length; i++) {
    var m = filtered[i];
    html += '<tr>'
      + '<td class="col-time">' + fmtFrameTime(m.t) + '</td>'
      + '<td class="col-dlc">' + m.dlc + '</td>'
      + '<td class="col-ext">' + (m.ext ? 'Yes' : '') + '</td>'
      + '<td class="col-data">' + m.data + '</td>'
      + '</tr>';
  }
  tbody.innerHTML = html;
  document.getElementById('detailCount').textContent = filtered.length + ' messages';
  var wrap = document.getElementById('viewDetail');
  if (autoScrollDetail) wrap.scrollTop = 0;
}

// ---- 曲线视图 ----
// Y 值 = 报文前 4 个字节按所选字节序拼成的 32 位整数

var CHART_MAX_POINTS = 1200;   // 仅绘制最近这么多点，避免手机端卡顿

function setDetailMode(mode) {
  detailMode = mode;
  document.getElementById('chartWrap').classList
    .toggle('hidden', mode !== 'chart');
  document.getElementById('tableView').classList
    .toggle('hidden', mode !== 'table');
  document.getElementById('tabChart').classList
    .toggle('active', mode === 'chart');
  document.getElementById('tabTable').classList
    .toggle('active', mode === 'table');
  renderDetail();
}

// 取报文前 4 字节拼成整数（不足 4 字节时按现有字节数处理）
function dataToInt(m, be, signed) {
  var hex = m.data.split(' ');
  var n = Math.min(4, hex.length);
  if (n === 0) return null;
  var bytes = [];
  for (var i = 0; i < n; i++) {
    var b = parseInt(hex[i], 16);
    if (isNaN(b)) return null;
    bytes.push(b);
  }
  var v = 0;
  if (be) {
    // 大端：第一个字节是最高位（CAN 信号的常见约定）
    for (var i = 0; i < bytes.length; i++) v = v * 256 + bytes[i];
  } else {
    // 小端：第一个字节是最低位
    for (var i = bytes.length - 1; i >= 0; i--) v = v * 256 + bytes[i];
  }
  // 不足 4 字节时不按 32 位做符号扩展
  if (signed && bytes.length === 4 && v >= 2147483648) v -= 4294967296;
  return v;
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

function fmtAxisNum(v, step) {
  var dec = step >= 1 ? 0 : Math.min(3, Math.ceil(-Math.log(step) / Math.LN10));
  return v.toFixed(dec);
}

function fmtValue(v) {
  var s = Math.round(v).toString();
  return s.replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}

function renderChart() {
  var canvas = document.getElementById('chartCanvas');
  var note = document.getElementById('chartNote');

  var be = document.getElementById('orderBE').checked;
  var signed = document.getElementById('chartSigned').checked;

  var pts = [];
  for (var i = 0; i < allMessages.length; i++) {
    var m = allMessages[i];
    if (m.id !== detailId) continue;
    var v = dataToInt(m, be, signed);
    if (v !== null) pts.push({ t: m.t, v: v });
  }
  pts.sort(function(a, b) { return a.t - b.t; });

  var total = pts.length;
  if (total > CHART_MAX_POINTS) pts = pts.slice(total - CHART_MAX_POINTS);

  // 按容器尺寸设置画布（含高分屏缩放）
  var wrap = document.getElementById('chartWrap');
  var cssW = Math.max(320, wrap.clientWidth - 24);
  var cssH = Math.max(200, (window.innerHeight || 700) * 0.42);
  var dpr = window.devicePixelRatio || 1;
  canvas.style.height = cssH + 'px';
  canvas.width = Math.round(cssW * dpr);
  canvas.height = Math.round(cssH * dpr);
  var ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  if (total === 0) {
    ctx.fillStyle = '#aaa';
    ctx.font = '12px Consolas, monospace';
    ctx.textAlign = 'center';
    ctx.fillText('暂无数据', cssW / 2, cssH / 2);
    note.textContent = '';
    return;
  }

  var lo = pts[0].v, hi = pts[0].v;
  for (var i = 1; i < pts.length; i++) {
    if (pts[i].v < lo) lo = pts[i].v;
    if (pts[i].v > hi) hi = pts[i].v;
  }
  var t0 = pts[0].t, t1 = pts[pts.length - 1].t;

  // 时间轴退化（全部同一毫秒）时给个最小跨度，避免除零
  if (t1 <= t0) t1 = t0 + 1;

  var padL = 62, padR = 14, padT = 14, padB = 30;
  var plotW = cssW - padL - padR;
  var plotH = cssH - padT - padB;

  // Y 轴范围：退化成一条直线时上下各留 1
  var yLo = lo, yHi = hi;
  if (yHi === yLo) { yLo = lo - 1; yHi = hi + 1; }

  var stepY = niceStep(yHi - yLo, 5);
  var yStart = Math.floor(yLo / stepY) * stepY;
  var yEnd = Math.ceil(yHi / stepY) * stepY;

  var xOf = function(t) { return padL + (t - t0) / (t1 - t0) * plotW; };
  var yOf = function(v) { return padT + (yEnd - v) / (yEnd - yStart) * plotH; };

  // 网格 + Y 轴刻度
  ctx.font = '10px Consolas, monospace';
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  var first = true;
  for (var v = yStart; v <= yEnd + stepY * 0.5; v += stepY) {
    var y = yOf(v);
    ctx.strokeStyle = (v === 0) ? '#d0d0d0' : '#f0f0f0';
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(padL + plotW, y);
    ctx.stroke();

    ctx.fillStyle = '#999';
    ctx.fillText(fmtAxisNum(v, stepY), padL - 6, y);

    // 只在顶部标一次量纲
    if (first) {
      ctx.fillStyle = '#bbb';
      ctx.textAlign = 'left';
      ctx.fillText('hex→int', padL + 4, padT + 9);
      ctx.textAlign = 'right';
      first = false;
    }
  }

  // X 轴刻度（相对第一条报文的秒数）
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  var span = (t1 - t0) / 1000;
  var stepT = niceStep(span, 6);
  if (stepT <= 0) stepT = 1;
  for (var ts = 0; ts <= span + stepT * 0.5; ts += stepT) {
    var x = padL + (ts / span) * plotW;
    if (x > padL + plotW + 0.5) break;
    ctx.strokeStyle = '#f0f0f0';
    ctx.beginPath();
    ctx.moveTo(x, padT);
    ctx.lineTo(x, padT + plotH);
    ctx.stroke();

    ctx.fillStyle = '#999';
    ctx.fillText(ts.toFixed(stepT >= 1 ? 0 : 1) + 's', x, padT + plotH + 6);
  }

  // 坐标轴
  ctx.strokeStyle = '#ccc';
  ctx.beginPath();
  ctx.moveTo(padL, padT);
  ctx.lineTo(padL, padT + plotH);
  ctx.lineTo(padL + plotW, padT + plotH);
  ctx.stroke();

  // 数据折线
  ctx.strokeStyle = '#1a73e8';
  ctx.lineWidth = 1.5;
  ctx.lineJoin = 'round';
  ctx.beginPath();
  for (var i = 0; i < pts.length; i++) {
    var x = xOf(pts[i].t), y = yOf(pts[i].v);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();

  // 点数少时把采样点也画出来，否则单点/稀疏数据看不出东西
  if (pts.length <= 200) {
    ctx.fillStyle = '#1a73e8';
    for (var i = 0; i < pts.length; i++) {
      ctx.beginPath();
      ctx.arc(xOf(pts[i].t), yOf(pts[i].v), 2, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  note.textContent = total + ' 点 · 最新 ' + fmtValue(pts[pts.length - 1].v)
    + ' · 范围 ' + fmtValue(lo) + ' ~ ' + fmtValue(hi)
    + (total > CHART_MAX_POINTS ? '（仅绘制最近 ' + CHART_MAX_POINTS + ' 点）' : '');
}

function showDetail(id) {
  detailId = id;
  currentView = 'detail';
  setTopTab(null);
  contentMain.classList.add('hidden');
  contentVI.classList.add('hidden');
  contentDetail.classList.remove('hidden');
  document.getElementById('detailId').textContent = id;
  document.getElementById('detailIdLabel').textContent = id;
  document.getElementById('headerTitle').textContent = 'CAN Detail';
  document.getElementById('backRow').classList.remove('hidden');
  renderDetail();
}

function showMain() {
  currentView = 'main';
  setTopTab('tabMonitor');
  contentDetail.classList.add('hidden');
  contentVI.classList.add('hidden');
  contentMain.classList.remove('hidden');
  document.getElementById('headerTitle').textContent = 'CAN Bus Monitor';
  document.getElementById('backRow').classList.add('hidden');
  renderMain();
}

// ===== 电压/电流曲线页 =====

var VI_MAX_POINTS = 900;
var viTimer = null;

function drawVI() {
  var canvas = document.getElementById('viCanvas');
  var note = document.getElementById('viNote');
  var pts = allVi.slice();
  var total = pts.length;
  if (total > VI_MAX_POINTS) pts = pts.slice(total - VI_MAX_POINTS);

  var wrap = document.getElementById('viewVI');
  var cssW = Math.max(280, (wrap.clientWidth || window.innerWidth) - 24);
  var cssH = Math.max(260, Math.floor((window.innerHeight || 700) * 0.62));
  var dpr = window.devicePixelRatio || 1;
  // 关键：CSS 显示尺寸必须与逻辑宽度一致，否则高 dpr 手机上
  // canvas 属性宽度(cssW*dpr)会撑破页面导致整页缩放错乱
  canvas.style.width = cssW + 'px';
  canvas.style.height = cssH + 'px';
  canvas.width = Math.round(cssW * dpr);
  canvas.height = Math.round(cssH * dpr);
  var ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  if (total === 0) {
    ctx.fillStyle = '#aaa';
    ctx.font = '12px Consolas, monospace';
    ctx.textAlign = 'center';
    ctx.fillText('暂无 0x18FF0282 数据，等待报文…', cssW / 2, cssH / 2);
    note.textContent = '';
    return;
  }

  var t0 = pts[0].t, t1 = pts[pts.length - 1].t;
  if (t1 <= t0) t1 = t0 + 1;

  var padL = 56, padR = 64, padT = 18, padB = 30;
  var plotW = cssW - padL - padR;
  var plotH = cssH - padT - padB;

  // 左轴范围 = 电流有效点；右轴 = 电压
  var cLo = Infinity, cHi = -Infinity;
  var vLo = Infinity, vHi = -Infinity;
  for (var i = 0; i < pts.length; i++) {
    if (!pts[i].f) {
      if (pts[i].c < cLo) cLo = pts[i].c;
      if (pts[i].c > cHi) cHi = pts[i].c;
    }
    if (pts[i].v < vLo) vLo = pts[i].v;
    if (pts[i].v > vHi) vHi = pts[i].v;
  }
  if (cHi < cLo) { cLo = -10; cHi = 10; }
  if (vHi < vLo) { vLo = 0; vHi = 1; }
  if ((cHi - cLo) < 4) { var cm = (cHi + cLo) / 2; cLo = cm - 5; cHi = cm + 5; }
  if ((vHi - vLo) < 4) { var vm = (vHi + vLo) / 2; vLo = vm - 5; vHi = vm + 5; }

  var stepC = niceStep(cHi - cLo, 5);
  var stepV = niceStep(vHi - vLo, 5);
  var cStart = Math.floor(cLo / stepC) * stepC, cEnd = Math.ceil(cHi / stepC) * stepC;
  var vStart = Math.floor(vLo / stepV) * stepV, vEnd = Math.ceil(vHi / stepV) * stepV;

  var xOf = function(t) { return padL + (t - t0) / (t1 - t0) * plotW; };
  var yOfC = function(v) { return padT + (cEnd - v) / (cEnd - cStart) * plotH; };
  var yOfV = function(v) { return padT + (vEnd - v) / (vEnd - vStart) * plotH; };

  ctx.font = '10px Consolas, monospace';
  ctx.textBaseline = 'middle';

  // 网格 + 左轴（电流）
  ctx.textAlign = 'right';
  for (var v = cStart; v <= cEnd + stepC * 0.5; v += stepC) {
    var y = yOfC(v);
    ctx.strokeStyle = (v === 0) ? '#d8d8d8' : '#f0f0f0';
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(padL + plotW, y); ctx.stroke();
    ctx.fillStyle = '#1a73e8';
    ctx.fillText((v / 10).toFixed(1), padL - 6, y);
  }
  // 右轴（电压）
  ctx.textAlign = 'left';
  ctx.fillStyle = '#ea4335';
  for (var v = vStart; v <= vEnd + stepV * 0.5; v += stepV) {
    ctx.fillText((v / 10).toFixed(1), padL + plotW + 6, yOfV(v));
  }
  // 量纲标注
  ctx.fillStyle = '#1a73e8'; ctx.textAlign = 'left';
  ctx.fillText('I(A)', padL + 4, padT + 4);
  ctx.fillStyle = '#ea4335'; ctx.textAlign = 'right';
  ctx.fillText('U(V)', padL + plotW - 4, padT + 4);

  // X 轴（时间，秒）
  ctx.fillStyle = '#999'; ctx.textAlign = 'center'; ctx.textBaseline = 'top';
  var span = (t1 - t0) / 1000;
  var stepT = niceStep(span, 6) || 1;
  for (var ts = 0; ts <= span + stepT * 0.5; ts += stepT) {
    var x = padL + (ts / span) * plotW;
    if (x > padL + plotW + 0.5) break;
    ctx.fillText(ts.toFixed(stepT >= 1 ? 0 : 1) + 's', x, padT + plotH + 6);
  }

  // 坐标轴框
  ctx.strokeStyle = '#ccc';
  ctx.beginPath();
  ctx.moveTo(padL, padT); ctx.lineTo(padL, padT + plotH);
  ctx.lineTo(padL + plotW, padT + plotH); ctx.lineTo(padL + plotW, padT); ctx.stroke();

  // 电流曲线（跳过哨兵故障点）
  var nC = 0;
  ctx.strokeStyle = '#1a73e8'; ctx.lineWidth = 1.4; ctx.lineJoin = 'round';
  ctx.beginPath();
  for (var i = 0; i < pts.length; i++) {
    if (pts[i].f) continue;
    var x = xOf(pts[i].t), y = yOfC(pts[i].c);
    if (nC === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    nC++;
  }
  ctx.stroke();

  // 电压曲线
  ctx.strokeStyle = '#ea4335'; ctx.lineWidth = 1.4;
  ctx.beginPath();
  for (var i = 0; i < pts.length; i++) {
    var x = xOf(pts[i].t), y = yOfV(pts[i].v);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();
  ctx.lineWidth = 1;

  var last = pts[pts.length - 1];
  note.textContent = total + ' 点 · 最新 I=' + (last.f ? '(故障)' : (last.c / 10).toFixed(1) + 'A')
    + ' U=' + (last.v / 10).toFixed(1) + 'V';
}

function showVI() {
  currentView = 'vi';
  setTopTab('tabVI');
  contentMain.classList.add('hidden');
  contentDetail.classList.add('hidden');
  contentVI.classList.remove('hidden');
  document.getElementById('headerTitle').textContent = 'Bus Voltage / Current';
  document.getElementById('backRow').classList.add('hidden');
  drawVI();
}

function setTopTab(id) {
  var monitor = document.getElementById('tabMonitor');
  var vi = document.getElementById('tabVI');
  if (id === 'tabVI') {
    vi.classList.add('active'); monitor.classList.remove('active');
  } else if (id === 'tabMonitor') {
    monitor.classList.add('active'); vi.classList.remove('active');
  }
}

document.getElementById('backBtn').addEventListener('click', showMain);

document.getElementById('viewDetail').addEventListener('scroll', function() {
  if (detailMode !== 'table') return;   // 曲线视图下不参与表格的自动滚动
  var el = document.getElementById('viewDetail');
  autoScrollDetail = el.scrollHeight - el.scrollTop - el.clientHeight < 50;
});

// 顶层页签切换
document.getElementById('tabMonitor').addEventListener('click', showMain);
document.getElementById('tabVI').addEventListener('click', showVI);

// 视图切换与曲线选项
document.getElementById('tabChart').addEventListener('click', function() { setDetailMode('chart'); });
document.getElementById('tabTable').addEventListener('click', function() { setDetailMode('table'); });
document.getElementById('orderBE').addEventListener('change', renderDetail);
document.getElementById('orderLE').addEventListener('change', renderDetail);
document.getElementById('chartSigned').addEventListener('change', renderDetail);

// 窗口尺寸变化时重绘（画布尺寸依赖像素，不能只靠 CSS 拉伸）
var chartResizeTimer = null;
window.addEventListener('resize', function() {
  clearTimeout(chartResizeTimer);
  chartResizeTimer = setTimeout(function() {
    if (currentView === 'detail' && detailMode === 'chart') renderChart();
    else if (currentView === 'vi') drawVI();
  }, 150);
});

function updateRecUI(rec) {
  if (!rec) return;
  var statusEl = document.getElementById('recStatus');
  var btnEl = document.getElementById('recBtn');
  var exportEl = document.getElementById('exportBtn');

  exportEl.classList.toggle('disabled', !rec.cnt);

  if (rec.on) {
    btnEl.textContent = 'Stop Rec';
    btnEl.classList.add('recording');
    var dur = rec.ms >= 60000
      ? Math.floor(rec.ms / 60000) + 'm' + Math.floor(rec.ms % 60000 / 1000) + 's'
      : Math.floor(rec.ms / 1000) + 's';
    statusEl.className = 'rec-status';
    statusEl.textContent = '\u25CF REC ' + rec.cnt + '/' + rec.cap + ' ' + dur
      + (rec.drop ? ' (+' + rec.drop + ' lost)' : '');
  } else {
    btnEl.textContent = 'Record';
    btnEl.classList.remove('recording');
    if (rec.cnt > 0) {
      statusEl.className = 'rec-status idle';
      statusEl.textContent = rec.cnt + ' frames ready'
        + (rec.drop ? ' (' + rec.drop + ' lost)' : '');
    } else {
      statusEl.className = 'rec-status idle';
      statusEl.textContent = rec.psram ? '' : 'Recorder N/A';
    }
  }
}

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
  if (d.rec) {
    updateRecUI(d.rec);
    lastRecOn = d.rec.on;
  }
  if (d.vi) {
    allVi = d.vi;
    if (currentView === 'vi') drawVI();
  }
  if (d.total !== lastTotal) {
    allMessages = d.messages || [];
    allFreqs = d.freqs || [];
    lastTotal = d.total;
    if (currentView === 'main') renderMain();
    else if (currentView === 'detail') renderDetail();
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
    else renderDetail();
  });
});

document.getElementById('recBtn').addEventListener('click', function() {
  var url = lastRecOn ? '/api/rec/stop' : '/api/rec/start';
  fetch(url, { method: 'POST' })
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (!d.ok) alert(d.error || 'Recorder error');
      pollMessages();
    })
    .catch(function(e) { alert('Recorder error: ' + e); });
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
