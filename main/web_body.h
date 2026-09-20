#pragma once

#define PAGE_BODY R"rawliteral(
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
)rawliteral"
