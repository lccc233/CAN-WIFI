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
var autoScrollMain = true;
var autoScrollDetail = true;

var contentMain = document.getElementById('viewMain');
var contentDetail = document.getElementById('viewDetail');
var contentVI = document.getElementById('viewVI');
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
      + '<td class="col-data">' + m.data + '</td>'
      + '<td class="col-time">' + fmtFrameTime(m.t) + '</td>'
      + '</tr>';
  }
  tbody.innerHTML = html;
  document.getElementById('msgCount').textContent = allMessages.length + ' msgs, ' + ids.length + ' IDs';
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
  contentVI.classList.add('hidden');
  contentDetail.classList.remove('hidden');
  document.getElementById('detailId').textContent = id;
  document.getElementById('detailIdLabel').textContent = id;
  document.getElementById('headerTitle').textContent = 'CAN Detail';
  document.getElementById('backRow').classList.remove('hidden');
  sendPanel.classList.add('hidden');
  renderDetailTable();
}

function showMain() {
  currentView = 'main';
  setTopTab('tabMonitor');
  contentDetail.classList.add('hidden');
  contentVI.classList.add('hidden');
  contentMain.classList.remove('hidden');
  sendPanel.classList.remove('hidden');
  document.getElementById('headerTitle').textContent = 'CAN Bus Monitor';
  document.getElementById('backRow').classList.add('hidden');
  renderMain();
}

// ===== 曲线页：电流/电压 + 转矩/转速（固定 20s 滚动窗，上下双画布双轴） =====

var VI_WIN_MS = 20000;      // 固定滚动时间窗（服务端缓冲 400 点 x 50ms ≈ 20s）
var VI_MAX_POINTS = 900;

// 轴刻度文本：大数用 k 缩写，小数位随步长自适应
function fmtAxisVal(v, step) {
  var dec = step >= 1 ? 0 : (step >= 0.1 ? 1 : 2);
  if (Math.abs(v) >= 10000) return (v / 1000).toFixed(1) + 'k';
  return v.toFixed(dec);
}

// cfg: { getA, getB: p -> 原始存储值或 null(无效点)；divA, divB: 存储->显示除数；
//        minSpanA, minSpanB: 最小显示跨度(存储单位)；colA, colB: 线色；
//        labA, labB: 画布内量纲文本；emptyMsg；noteFn(lastVA, lastVB, cnt) }
function drawDual(canvas, noteEl, cfg) {
  var pts = allVi.slice();
  if (pts.length > VI_MAX_POINTS) pts = pts.slice(pts.length - VI_MAX_POINTS);
  var total = pts.length;

  var wrap = document.getElementById('viewVI');
  var cssW = Math.max(280, (wrap.clientWidth || window.innerWidth) - 24);
  var cssH = Math.max(190, Math.floor((window.innerHeight || 700) * 0.38));
  var dpr = window.devicePixelRatio || 1;
  // CSS 显示尺寸与逻辑宽度一致，避免高 dpr 手机上 canvas 撑破页面
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
    ctx.fillText(cfg.emptyMsg, cssW / 2, cssH / 2);
    noteEl.textContent = '';
    return;
  }

  var t1 = pts[pts.length - 1].t;
  var t0 = t1 - VI_WIN_MS;   // 固定滚动窗：数据不足窗口时左侧自然留白

  // 窗口内有效值 min/max
  var aLo = Infinity, aHi = -Infinity, bLo = Infinity, bHi = -Infinity, cntA = 0, cntB = 0;
  for (var i = 0; i < pts.length; i++) {
    if (pts[i].t < t0) continue;
    var va = cfg.getA(pts[i]);
    if (va !== null) { if (va < aLo) aLo = va; if (va > aHi) aHi = va; cntA++; }
    var vb = cfg.getB(pts[i]);
    if (vb !== null) { if (vb < bLo) bLo = vb; if (vb > bHi) bHi = vb; cntB++; }
  }

  // 纵轴：10% 余量 + 最小跨度，再对齐 niceStep
  function axisOf(lo, hi, minSpan) {
    if (lo > hi) return null;
    var r = hi - lo;
    if (r < minSpan) { var m = (lo + hi) / 2; lo = m - minSpan / 2; hi = m + minSpan / 2; r = minSpan; }
    lo -= r * 0.10; hi += r * 0.10;
    var step = niceStep(hi - lo, 5);
    return [Math.floor(lo / step) * step, Math.ceil(hi / step) * step, step];
  }
  var aAx = axisOf(aLo, aHi, cfg.minSpanA);
  var bAx = axisOf(bLo, bHi, cfg.minSpanB);
  if (!aAx && !bAx) {
    ctx.fillStyle = '#aaa';
    ctx.font = '12px Consolas, monospace';
    ctx.textAlign = 'center';
    ctx.fillText(cfg.emptyMsg, cssW / 2, cssH / 2);
    noteEl.textContent = '';
    return;
  }
  if (!aAx) aAx = [0, 10, 1];
  if (!bAx) bAx = [0, 10, 1];
  var aStart = aAx[0], aEnd = aAx[1], stepA = aAx[2];
  var bStart = bAx[0], bEnd = bAx[1], stepB = bAx[2];

  var span = (t1 - t0) / 1000;
  if (span <= 0) span = 0.001;
  var stepT = niceStep(span, 6) || 1;

  var padL = 56, padR = 64, padT = 18, padB = 26;
  var plotW = cssW - padL - padR;
  var plotH = cssH - padT - padB;

  var xOf = function(t) { return padL + (t - t0) / VI_WIN_MS * plotW; };
  var yOfA = function(v) { return padT + (aEnd - v) / (aEnd - aStart) * plotH; };
  var yOfB = function(v) { return padT + (bEnd - v) / (bEnd - bStart) * plotH; };

  ctx.font = '10px Consolas, monospace';
  ctx.textBaseline = 'middle';

  // 水平网格（与左轴刻度一致）+ 左右轴数值
  ctx.textAlign = 'right';
  for (var v = aStart; v <= aEnd + stepA * 0.5; v += stepA) {
    var y = yOfA(v);
    ctx.strokeStyle = (v === 0) ? '#d8d8d8' : '#f0f0f0';
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(padL + plotW, y); ctx.stroke();
    ctx.fillStyle = cfg.colA;
    ctx.fillText(fmtAxisVal(v / cfg.divA, stepA / cfg.divA), padL - 6, y);
    // 右轴数值按就近网格行对齐绘制，避免两轴刻度线打架
  }
  ctx.textAlign = 'left';
  ctx.fillStyle = cfg.colB;
  for (var v = bStart; v <= bEnd + stepB * 0.5; v += stepB) {
    ctx.fillText(fmtAxisVal(v / cfg.divB, stepB / cfg.divB), padL + plotW + 6, yOfB(v));
  }

  // 垂直网格（时间）+ 标签
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  ctx.fillStyle = '#999';
  for (var ts = 0; ts <= span + stepT * 0.5; ts += stepT) {
    var x = padL + (ts / span) * plotW;
    if (x > padL + plotW + 0.5) break;
    ctx.strokeStyle = '#f5f5f5';
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, padT + plotH); ctx.stroke();
    ctx.fillText((ts === 0 ? '0s' : '-' + (stepT < 1 ? (span - ts).toFixed(1) : String(Math.round(span - ts))) + 's'), x, padT + plotH + 6);
  }

  // 坐标轴框
  ctx.strokeStyle = '#ccc';
  ctx.beginPath();
  ctx.moveTo(padL, padT); ctx.lineTo(padL, padT + plotH);
  ctx.lineTo(padL + plotW, padT + plotH); ctx.lineTo(padL + plotW, padT); ctx.stroke();

  // 量纲标注
  ctx.textBaseline = 'top';
  ctx.fillStyle = cfg.colA; ctx.textAlign = 'left';
  ctx.fillText(cfg.labA, padL + 4, padT + 4);
  ctx.fillStyle = cfg.colB; ctx.textAlign = 'right';
  ctx.fillText(cfg.labB, padL + plotW - 4, padT + 4);

  // 曲线（窗口内数据；无效值断线）
  ctx.lineWidth = 1.4; ctx.lineJoin = 'round';
  ctx.save();
  ctx.beginPath(); ctx.rect(padL, padT, plotW, plotH); ctx.clip();
  line(ctx, pts, t0, xOf, yOfA, cfg.getA, cfg.colA);
  line(ctx, pts, t0, xOf, yOfB, cfg.getB, cfg.colB);
  ctx.restore();
  ctx.lineWidth = 1;

  var last = pts[pts.length - 1];
  noteEl.textContent = total + ' 点 · ' + cfg.noteFn(
    cfg.getA(last), cfg.getB(last), Math.min(cntA, cntB));
}

var viTimer = null;

function line(ctx, pts, t0, xOf, yOf, get, color) {
  ctx.strokeStyle = color;
  ctx.beginPath();
  var started = false;
  for (var i = 0; i < pts.length; i++) {
    if (pts[i].t < t0) continue;
    var v = get(pts[i]);
    if (v === null) { started = false; continue; }
    var x = xOf(pts[i].t), y = yOf(v);
    if (!started) { ctx.moveTo(x, y); started = true; }
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

function drawVI() {
  drawDual(document.getElementById('viCanvas'), document.getElementById('viNote'), {
    getA: function(p) { return p.f ? null : p.c; },
    getB: function(p) { return p.f ? null : p.v; },
    divA: 10, divB: 10, minSpanA: 10, minSpanB: 10,
    colA: '#1a73e8', colB: '#ea4335',
    labA: 'I(A)', labB: 'U(V)',
    emptyMsg: '暂无 0x18FF0282 数据，等待报文…',
    noteFn: function(a, b, cnt) {
      return '最新 I=' + (a === null ? '(故障)' : (a / 10).toFixed(1) + 'A')
        + ' U=' + (b === null ? '(故障)' : (b / 10).toFixed(1) + 'V');
    }
  });
  drawDual(document.getElementById('tqCanvas'), document.getElementById('tqNote'), {
    getA: function(p) { return p.m ? p.q : null; },
    getB: function(p) { return p.m ? p.r : null; },
    divA: 1, divB: 1, minSpanA: 2, minSpanB: 200,
    colA: '#188038', colB: '#9334e6',
    labA: 'T(Nm)', labB: 'n(rpm)',
    emptyMsg: '等待 0x18FF0182 电机数据…',
    noteFn: function(a, b, cnt) {
      if (a === null && b === null) return '等待 0x18FF0182…';
      return '最新 T=' + fmtAxisVal(a === null ? 0 : a, 1) + 'Nm'
        + ' n=' + fmtAxisVal(b === null ? 0 : b, 1) + 'rpm';
    }
  });
}

function showVI() {
  currentView = 'vi';
  setTopTab('tabVI');
  contentMain.classList.add('hidden');
  contentDetail.classList.add('hidden');
  contentVI.classList.remove('hidden');
  document.getElementById('headerTitle').textContent = 'Bus Voltage / Current';
  document.getElementById('backRow').classList.add('hidden');
  sendPanel.classList.add('hidden');
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
  var el = document.getElementById('viewDetail');
  autoScrollDetail = el.scrollHeight - el.scrollTop - el.clientHeight < 50;
});

// 顶层页签切换
document.getElementById('tabMonitor').addEventListener('click', showMain);
document.getElementById('tabVI').addEventListener('click', showVI);

// 窗口尺寸变化时重绘（画布尺寸依赖像素，不能只靠 CSS 拉伸）
var chartResizeTimer = null;
window.addEventListener('resize', function() {
  clearTimeout(chartResizeTimer);
  chartResizeTimer = setTimeout(function() {
    if (currentView === 'vi') drawVI();
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
    else if (currentView === 'detail') renderDetailTable();
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
