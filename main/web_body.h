#pragma once

#define PAGE_BODY R"rawliteral(
  <div class="header">
    <h1 id="headerTitle">CAN Bus Monitor</h1>
    <div class="header-right">
      <span class="msg-count" id="msgCount">0 messages</span>
      <span class="status"><span class="dot" id="statusDot"></span><span id="statusText">Connecting...</span></span>
      <button class="btn danger" id="clearBtn">Clear</button>
    </div>
  </div>

  <!-- 顶层页签：监控 / 自定义曲线 -->
  <div class="top-tabs">
    <button class="tab active" id="tabMonitor">CAN Monitor</button>
    <button class="tab" id="tabCharts">自定义曲线</button>
    <button class="tab" id="tabBattery">电池信息</button>
    <button class="tab" id="tabRcu">增程器</button>
  </div>

  <!-- 主视图：按 ID 分组 -->
  <div class="content" id="viewMain">
    <div class="hint">天蓝色：BMS 电池报文　淡红色：RCU 增程器报文</div>
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

  <div class="content hidden" id="viewBattery">
    <div class="battery-page">
      <div class="battery-heading"><div><h2>电池信息</h2><p>CATL A-CAN · V3.2.1 · 被动读取</p></div><span class="battery-badge" id="batteryStatus" role="status">等待报文</span></div>
      <p class="battery-note">仅解析已接收的 BMS 广播，不发送查询、握手或唤醒报文。手动发送功能仍在 CAN Monitor 页。</p>
      <div class="battery-metrics" id="batteryMetrics"></div>
      <section class="battery-section"><h3>故障信息</h3><p class="battery-note">显示当前报文中的故障；未收到或超时不代表无故障。</p><div id="batteryFaults"></div></section>
      <details class="battery-section"><summary>报文来源与更新时间</summary><div class="battery-source-wrap"><table><thead><tr><th>ID / 报文</th><th>状态</th><th>原始数据</th></tr></thead><tbody id="batterySources"></tbody></table></div></details>
      <p class="battery-note">天蓝色背景标记协议中由 BMS 发出的 ID。读数超过 10 秒未更新即标为超时。</p>
    </div>
  </div>

  <div class="content hidden" id="viewRcu">
    <div class="battery-page rcu-page">
      <div class="battery-heading"><div><h2>增程器</h2><p>玉柴 RCU · CAN Matrix V3.5 · 被动读取</p></div><span class="battery-badge" id="rcuStatus" role="status">等待报文</span></div>
      <p class="battery-note">读取 RCU 已广播的运行状态，不发送控制、查询或启动请求。</p>
      <div class="battery-metrics rcu-metrics" id="rcuMetrics"></div>
      <section class="battery-section"><h3>运行与故障状态</h3><div class="rcu-table-wrap" id="rcuState"></div></section>
      <section class="battery-section"><h3>故障位图</h3><p class="battery-note">按报文分组分别显示。仅按原表提供的故障编号解释，不推断未提供的故障含义；未收到或超时不能视为无故障。</p><div id="rcuFaults"></div></section>
      <details class="battery-section" open><summary>运行参数</summary><div class="rcu-table-wrap" id="rcuParameters"></div></details>
      <details class="battery-section"><summary>发动机指示灯</summary><div class="rcu-table-wrap" id="rcuLamps"></div></details>
      <details class="battery-section"><summary>软件版本与报文来源</summary><div class="rcu-table-wrap" id="rcuVersion"></div><div class="battery-source-wrap"><table><thead><tr><th>报文来源</th><th>状态 / 时间</th><th>原始数据</th></tr></thead><tbody id="rcuSources"></tbody></table></div></details>
      <p class="battery-note">电流和功率：负值为发电，正值为耗电。一般报文超过 10 秒未更新即超时，5 秒周期的版本报文使用 15 秒。</p>
    </div>
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

  <!-- 自定义曲线视图：每信号一条曲线带 -->
  <div class="content hidden" id="viewCharts">
    <div class="charts-bar">
      <button class="btn primary" id="addSig2Btn">+ 添加曲线</button>
      <label class="win-label">窗口
        <select id="winSel">
          <option value="10000">10s</option>
          <option value="20000" selected>20s</option>
          <option value="30000">30s</option>
          <option value="60000">60s</option>
        </select>
      </label>
      <span id="liveStatus" role="status" aria-live="polite"></span>
      <button class="btn" id="pauseBtn">暂停</button>
      <button class="btn" id="curveRecBtn">&#9679; Record</button>
      <span class="record-status" id="curveRecStatus" role="status" aria-live="polite"></span>
      <button class="btn" id="recordRetryBtn" hidden>重试同步</button>
      <span class="csv-actions">
        <button class="btn" id="csvExportBtn">导出记录CSV</button>
        <button class="btn mail-btn" id="csvMailBtn" title="发送记录 CSV 到邮箱" aria-label="发送记录 CSV 到邮箱"><svg viewBox="0 0 24 24" width="20" height="20" aria-hidden="true"><rect x="3" y="5" width="18" height="14" rx="2"/><path d="m3 6 9 7 9-7"/></svg></button>
      </span>
      <button class="btn" id="cfgExportBtn">导出配置</button>
      <button class="btn" id="cfgImportBtn">导入配置</button>
      <input type="file" id="cfgImportFile" accept=".json,application/json" class="hidden">
    </div>
    <div class="hint">Record 由设备逐帧保存，网页解码一次用于曲线和 CSV；停止后自动补齐。曲线仅显示最近窗口，CSV 保留每帧采样。本次录制使用固定配置，后续编辑用于下次录制。</div>
    <div class="config-save-row"><span id="cfgStatus" class="cfg-status" role="status" aria-live="polite">正在加载设备配置…</span><button class="btn" id="cfgRetryBtn">重试配置保存/加载</button></div>
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
