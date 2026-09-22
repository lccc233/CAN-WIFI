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

  <!-- 顶层页签：监控 / 电压电流曲线 / 自定义曲线 -->
  <div class="top-tabs">
    <button class="tab active" id="tabMonitor">CAN Monitor</button>
    <button class="tab" id="tabVI">电压电流曲线</button>
    <button class="tab" id="tabCharts">自定义曲线</button>
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
    <!-- 信号定义（自定义曲线） -->
    <div class="sig-card">
      <div class="rowline">
        <strong class="sig-title">信号定义</strong>
        <span class="grow"></span>
        <button class="btn primary" id="addSigBtn">+ 添加曲线</button>
      </div>
      <div id="sigList"></div>
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
    <div class="chart-wrap">
      <canvas id="tqCanvas"></canvas>
      <div class="chart-note">
        <span id="tqNote"></span>
        <span class="chart-ctl">
          <span style="color:#188038;">&#9632; 转矩 T (Nm)</span>
          <span style="color:#9334e6;">&#9632; 转速 n (rpm)</span>
        </span>
      </div>
    </div>
  </div>

  <!-- 自定义曲线视图：每信号一条曲线带 -->
  <div class="content hidden" id="viewCharts">
    <div class="charts-bar">
      <button class="btn primary" id="addSig2Btn">+ 添加曲线</button>
      <label class="win-label">窗口
        <select id="winSel">
          <option value="10000">10s</option>
          <option value="30000" selected>30s</option>
          <option value="60000">60s</option>
        </select>
      </label>
      <button class="btn" id="pauseBtn">暂停</button>
      <button class="btn" id="curveRecBtn">&#9679; Record</button>
      <span class="msg-count" id="curveRecStatus"></span>
      <button class="btn" id="csvExportBtn">导出记录CSV</button>
      <button class="btn" id="cfgExportBtn">导出配置</button>
    </div>
    <div class="hint">曲线 = 用信号定义对原始帧实时解码（仅积累打开页面后的数据）；Record 记录已启用信号的解码值，可导出 CSV 离线分析</div>
    <div id="chartList"></div>
  </div>

  <div class="send-panel hidden" id="backRow">
    <button class="btn back" id="backBtn">&larr; Back to List</button>
    <span class="msg-count" id="detailIdLabel"></span>
  </div>

  <div class="send-panel" id="sendPanel">
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

  <!-- 添加/编辑信号表单（模态框） -->
  <div class="modal-mask hidden" id="formMask">
    <div class="modal">
      <h3 id="formTitle">添加曲线</h3>
      <div class="fgrid">
        <label>CAN ID</label><input type="text" id="f_id" placeholder="0x18FF0182">
        <label>信号名</label><input type="text" id="f_name" placeholder="Torque">
        <label>起始位 (DBC)</label><input type="number" id="f_start" value="0" min="0" max="63">
        <label>位长</label><input type="number" id="f_len" value="16" min="1" max="64">
        <label>字节序</label>
        <select id="f_endian"><option value="moto">Motorola (大端)</option><option value="intel">Intel (小端)</option></select>
        <label>符号</label>
        <select id="f_signed"><option value="1">signed</option><option value="0">unsigned</option></select>
        <label>factor (×)</label><input type="number" id="f_factor" value="1" step="0.001">
        <label>offset (+)</label><input type="number" id="f_offset" value="0" step="0.001">
        <label>单位</label><input type="text" id="f_unit" placeholder="Nm">
        <label>颜色</label><input type="color" id="f_color" value="#188038">
      </div>
      <div class="hint">Motorola: 起始位为 MSB 的 DBC 位号（byte0 整字节 = 7）；Intel: 起始位为 LSB 位号（byte0 整字节 = 0）。值 = raw × factor + offset</div>
      <div class="rowline mt">
        <span class="grow"></span>
        <button id="formCancel">取消</button>
        <button class="btn primary" id="formSave">保存</button>
      </div>
    </div>
  </div>

  <div class="toast" id="toast"></div>
)rawliteral"
