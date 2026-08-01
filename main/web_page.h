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
  .hidden { display: none !important; }
</style>
</head>
<body>
  <div class="header">
    <h1 id="headerTitle">CAN Bus Monitor</h1>
    <div class="header-right">
      <span class="msg-count" id="msgCount">0 messages</span>
      <span class="status"><span class="dot" id="statusDot"></span><span id="statusText">Connecting...</span></span>
      <button class="btn danger" id="clearBtn">Clear</button>
    </div>
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
    </div>
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
var lastTotal = 0;
var currentView = 'main';
var detailId = '';
var autoScrollMain = true;
var autoScrollDetail = true;

var contentMain = document.getElementById('viewMain');
var contentDetail = document.getElementById('viewDetail');

function formatTime(ms) {
  var s = Math.floor(ms / 1000);
  var m = Math.floor(s / 60);
  return String(m).padStart(2,'0') + ':' + String(s % 60).padStart(2,'0') + '.' + String(ms % 1000).padStart(3,'0');
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
      + '<td class="col-time">' + formatTime(m.t) + '</td>'
      + '</tr>';
  }
  tbody.innerHTML = html;
  document.getElementById('msgCount').textContent = allMessages.length + ' msgs, ' + ids.length + ' IDs';
}

function renderDetail() {
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
      + '<td class="col-time">' + formatTime(m.t) + '</td>'
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

function showDetail(id) {
  detailId = id;
  currentView = 'detail';
  contentMain.classList.add('hidden');
  contentDetail.classList.remove('hidden');
  document.getElementById('detailId').textContent = id;
  document.getElementById('detailIdLabel').textContent = id;
  document.getElementById('headerTitle').textContent = 'CAN Detail';
  document.getElementById('backRow').classList.remove('hidden');
  renderDetail();
}

function showMain() {
  currentView = 'main';
  contentDetail.classList.add('hidden');
  contentMain.classList.remove('hidden');
  document.getElementById('headerTitle').textContent = 'CAN Bus Monitor';
  document.getElementById('backRow').classList.add('hidden');
  renderMain();
}

document.getElementById('backBtn').addEventListener('click', showMain);

document.getElementById('viewDetail').addEventListener('scroll', function() {
  var el = document.getElementById('viewDetail');
  autoScrollDetail = el.scrollHeight - el.scrollTop - el.clientHeight < 50;
});

function pollMessages() {
  fetch('/api/messages')
    .then(function(r) { return r.json(); })
    .then(function(d) {
      document.getElementById('statusDot').className = 'dot on';
      document.getElementById('statusText').textContent = 'Connected';
      if (d.total !== lastTotal) {
        allMessages = d.messages;
        allFreqs = d.freqs || [];
        lastTotal = d.total;
        if (currentView === 'main') renderMain();
        else renderDetail();
      }
    })
    .catch(function() {
      document.getElementById('statusDot').className = 'dot off';
      document.getElementById('statusText').textContent = 'Disconnected';
    });
}

setInterval(pollMessages, 200);
pollMessages();

document.getElementById('clearBtn').addEventListener('click', function() {
  fetch('/api/clear', { method: 'POST' }).then(function() {
    allMessages = [];
    allFreqs = [];
    lastTotal = 0;
    if (currentView === 'main') renderMain();
    else renderDetail();
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
</html>
)rawliteral";
