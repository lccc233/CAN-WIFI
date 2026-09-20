#pragma once

static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>CAN Bus Monitor</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body {
    font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
    background: #f5f5f5; color: #333;
    display: flex; flex-direction: column;
    height: 100dvh; overflow: hidden;
    position: relative;
  }
  body::before {
    content: '';
    position: fixed;
    top: 50%; left: 50%;
    transform: translate(-50%, -50%);
    width: 50%; max-width: 350px;
    height: auto;
    aspect-ratio: 1;
    background: url('data:image/png;base64,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') center/contain no-repeat;
    opacity: 0.08;
    pointer-events: none;
    z-index: 0;
  }
  .header, .content, .send-panel { position: relative; z-index: 1; }
  .header {
    display: flex; align-items: center; justify-content: space-between;
    padding: 8px 16px; background: #fff;
    border-bottom: 1px solid #ddd;
    box-shadow: 0 1px 3px rgba(0,0,0,0.08);
  }
  .header h1 { font-size: 1rem; color: #1a73e8; }
  .header-right { display: flex; align-items: center; gap: 12px; }
  .status { display: flex; align-items: center; gap: 4px; font-size: 0.8rem; }
  .dot { width: 8px; height: 8px; border-radius: 50%; }
  .dot.on { background: #34a853; }
  .dot.off { background: #ea4335; }
  .btn {
    padding: 5px 14px; border: 1px solid #ccc; border-radius: 4px;
    background: #fff; color: #333; cursor: pointer; font-size: 0.8rem;
    font-family: inherit; transition: background 0.15s;
  }
  .btn:hover { background: #f0f0f0; }
  .btn.danger { border-color: #ea4335; color: #ea4335; }
  .btn.danger:hover { background: #fce8e6; }
  .btn.primary { border-color: #1a73e8; color: #fff; background: #1a73e8; }
  .btn.primary:hover { background: #1557b0; }
  .btn.back { border-color: #666; color: #666; }

  .content { flex: 1; overflow-y: auto; overflow-x: auto; }
  .detail-header {
    padding: 8px 16px; background: #e8f0fe; border-bottom: 1px solid #c2d7f5;
    display: flex; align-items: center; gap: 12px; font-size: 0.85rem;
  }
  .detail-header .id-label { color: #1a73e8; font-weight: bold; font-size: 1rem; }

  /* 详情视图：Chart / Table 切换 */
  .view-tabs { display: flex; gap: 4px; margin-left: auto; }
  .tab {
    padding: 3px 12px; border: 1px solid #c2d7f5; border-radius: 4px;
    background: #fff; color: #1a73e8; cursor: pointer;
    font-size: 0.75rem; font-family: inherit;
  }
  .tab.active { background: #1a73e8; color: #fff; border-color: #1a73e8; }

  /* 曲线视图 */
  .chart-wrap { padding: 8px 12px 12px; }
  #chartCanvas { display: block; width: 100%; background: #fff; border: 1px solid #e0e0e0; border-radius: 4px; }
  .chart-note {
    color: #888; font-size: 0.72rem; padding: 6px 2px 0;
    display: flex; align-items: center; gap: 10px; flex-wrap: wrap;
  }
  .chart-ctl { display: flex; align-items: center; gap: 8px; margin-left: auto; }
  .chart-ctl .checkbox-label { font-size: 0.72rem; gap: 3px; }

  table { width: 100%; border-collapse: collapse; font-size: 0.82rem; }
  thead { position: sticky; top: 0; z-index: 1; }
  th {
    background: #f8f9fa; color: #555; font-weight: 600;
    padding: 6px 10px; text-align: left;
    border-bottom: 2px solid #ddd; white-space: nowrap;
  }
  td {
    padding: 5px 10px; border-bottom: 1px solid #eee;
    white-space: nowrap;
  }
  tr:hover td { background: #e8f0fe; }
  tr.clickable { cursor: pointer; }
  .col-time { color: #888; width: 100px; }
  .col-id { color: #e8710a; font-weight: bold; }
  .col-dlc { color: #888; width: 50px; text-align: center; }
  .col-ext { color: #9334e6; width: 50px; text-align: center; }
  .col-data { color: #188038; }
  .col-count { color: #666; width: 60px; text-align: center; }
  .col-freq { color: #1a73e8; width: 70px; text-align: center; }

  .send-panel {
    padding: 10px 16px; background: #fff;
    border-top: 1px solid #ddd;
    box-shadow: 0 -1px 3px rgba(0,0,0,0.06);
    padding-bottom: calc(10px + env(safe-area-inset-bottom, 20px));
  }
  .send-row {
    display: flex; align-items: center; gap: 8px; flex-wrap: wrap;
  }
  .send-row label { color: #555; font-size: 0.8rem; }
  .input-field {
    padding: 5px 8px; border: 1px solid #ccc; border-radius: 4px;
    background: #fff; color: #333; font-family: inherit;
    font-size: 0.82rem;
  }
  .input-field:focus { border-color: #1a73e8; outline: none; box-shadow: 0 0 0 2px rgba(26,115,232,0.2); }
  #sendId { width: 100px; }
  #sendDlc { width: 50px; }
  #sendData { width: 220px; }
  .checkbox-label {
    display: flex; align-items: center; gap: 4px;
    color: #555; font-size: 0.8rem; cursor: pointer;
  }
  .msg-count { color: #888; font-size: 0.75rem; }
  .top-tabs {
    display: flex; gap: 6px; padding: 6px 12px;
    background: #fff; border-bottom: 1px solid #ddd;
    position: relative; z-index: 1;
  }
  .top-tabs .tab { padding: 5px 16px; font-size: 0.8rem; }
  .rec-status { color: #ea4335; font-size: 0.75rem; font-weight: bold; white-space: nowrap; }
  .rec-status.idle { color: #5f6368; font-weight: normal; }
  #exportBtn { text-decoration: none; }
  #exportBtn.disabled { pointer-events: none; opacity: 0.4; }
  #recBtn.recording { background: #ea4335; color: #fff; border-color: #ea4335;
                      animation: recblink 1.2s infinite; }
  @keyframes recblink { 50% { opacity: 0.65; } }
  .hidden { display: none !important; }

  /* 触屏点击高亮与字体缩放 */
  button { touch-action: manipulation; }
  html { -webkit-text-size-adjust: 100%; }

  /* 手机窄屏适配 */
  @media (max-width: 640px) {
    .header { padding: 6px 10px; flex-wrap: wrap; row-gap: 4px; }
    .header h1 { font-size: 0.9rem; }
    .header-right { gap: 6px; flex-wrap: wrap; }
    .msg-count { display: none; }          /* 窄屏隐藏消息计数 */
    .top-tabs { padding: 5px 8px; }
    .top-tabs .tab { padding: 5px 12px; font-size: 0.78rem; }
    .chart-wrap { padding: 6px 8px 10px; }
    .send-panel { padding: 8px 10px; }
    #sendId { width: 84px; }
    #sendData { flex: 1; min-width: 120px; }
    .btn { padding: 6px 10px; }
    .col-time { width: auto; }
  }
</style>
</head>
<body>
  <div class="header">
    <h1 id="headerTitle">CAN Bus Monitor</h1>
    <div class="header-right">
      <span class="msg-count" id="msgCount">0 messages</span>
      <span class="rec-status" id="recStatus"></span>
      <button class="btn danger" id="recBtn">Record</button>
      <a class="btn" id="exportBtn" href="/api/export" download="can_log.csv">Export CSV</a>
      <span class="status"><span class="dot" id="statusDot"></span><span id="statusText">Connecting...</span></span>
      <button class="btn danger" id="clearBtn">Clear</button>
    </div>
  </div>

  <!-- 顶层页签：监控 / 电压电流曲线 -->
  <div class="top-tabs">
    <button class="tab active" id="tabMonitor">CAN Monitor</button>
    <button class="tab" id="tabVI">电压电流曲线</button>
  </div>

  <!-- 主视图：按 ID 分组 -->
  <div class="content" id="viewMain">
    <table>
      <thead>
        <tr>
          <th class="col-id">ID</th>
          <th class="col-count">Count</th>
          <th class="col-freq">Freq</th>
          <th class="col-dlc">DLC</th>
          <th class="col-ext">Ext</th>
          <th class="col-data">Data</th>
          <th class="col-time">Last Time</th>
        </tr>
      </thead>
      <tbody id="mainBody"></tbody>
    </table>
  </div>

  <!-- 详情视图：某 ID 的历史 -->
  <div class="content hidden" id="viewDetail">
    <div class="detail-header">
      <span>ID:</span><span class="id-label" id="detailId"></span>
      <span id="detailCount" class="msg-count"></span>
      <span class="view-tabs">
        <button class="tab active" id="tabChart">Chart</button>
        <button class="tab" id="tabTable">Table</button>
      </span>
    </div>

    <!-- 曲线视图：Y = 前 4 字节拼接成的整数 -->
    <div class="chart-wrap" id="chartWrap">
      <canvas id="chartCanvas"></canvas>
      <div class="chart-note">
        <span id="chartNote"></span>
        <span class="chart-ctl">
          Byte order:
          <label class="checkbox-label"><input type="radio" name="byteOrder" id="orderBE" value="be" checked> 大端 (BE)</label>
          <label class="checkbox-label"><input type="radio" name="byteOrder" id="orderLE" value="le"> 小端 (LE)</label>
          <label class="checkbox-label"><input type="checkbox" id="chartSigned"> 有符号</label>
        </span>
      </div>
    </div>

    <!-- 表格视图 -->
    <div id="tableView">
      <table>
        <thead>
          <tr>
            <th class="col-time">Time</th>
            <th class="col-dlc">DLC</th>
            <th class="col-ext">Ext</th>
            <th class="col-data">Data</th>
          </tr>
        </thead>
        <tbody id="detailBody"></tbody>
      </table>
    </div>
  </div>

  <!-- 电压/电流曲线视图 -->
  <div class="content hidden" id="viewVI">
    <div class="chart-wrap">
      <canvas id="viCanvas"></canvas>
      <div class="chart-note">
        <span id="viNote"></span>
        <span class="chart-ctl">
          <span style="color:#1a73e8;">&#9632; 电流 I (A)</span>
          <span style="color:#ea4335;">&#9632; 电压 U (V)</span>
        </span>
      </div>
    </div>
  </div>

  <div class="send-panel">
    <div class="send-row hidden" id="backRow">
      <button class="btn back" id="backBtn">&larr; Back to List</button>
      <span class="msg-count" id="detailIdLabel"></span>
    </div>
    <div class="send-row">
      <label>ID:</label>
      <input type="text" class="input-field" id="sendId" placeholder="0x7E8" value="0x7E0">
      <label>DLC:</label>
      <input type="number" class="input-field" id="sendDlc" min="0" max="8" value="8">
      <label class="checkbox-label"><input type="checkbox" id="sendExt"> Extended</label>
      <label>Data:</label>
      <input type="text" class="input-field" id="sendData" placeholder="01 02 03 04 05 06 07 08" value="01 02 03 04 05 06 07 08">
      <button class="btn primary" id="sendBtn">Send</button>
    </div>
  </div>

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
  var ids = Object.keys(groups).sort().reverse();
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
  // 前一请求未完成则跳过本轮：SoftAP 响应可能慢于 200ms，
  // 并发多个请求会拖垮 httpd 并导致数据乱序
  if (pollBusy) return;
  pollBusy = true;
  fetch('/api/messages')
    .then(function(r) { return r.json(); })
    .then(function(d) {
      document.getElementById('statusDot').className = 'dot on';
      document.getElementById('statusText').textContent = 'Connected';
      if (d.clk) {
        clkSync = d.clk.sync;
        clkBoot = d.clk.boot;
        clkEp = d.clk.ep;
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
        allMessages = d.messages;
        allFreqs = d.freqs || [];
        lastTotal = d.total;
        if (currentView === 'main') renderMain();
        else if (currentView === 'detail') renderDetail();
      }
      pollBusy = false;
    })
    .catch(function() {
      document.getElementById('statusDot').className = 'dot off';
      document.getElementById('statusText').textContent = 'Disconnected';
      pollBusy = false;
    });
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
</html>
)rawliteral";
