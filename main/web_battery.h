#pragma once

// CATL A-CAN V3.2.1, PDF pp.3-4/8-11/14-15. No CAN transmit or request path.
#define PAGE_BATTERY_JS R"rawliteral(
<script>
var batteryFrames = {}, batteryConnected = false;
var batteryBootNow = 0, batteryReceivedAt = 0;
var BATTERY_TIMEOUT_MS = 10000;
// Exact IDs sent by BMU, including its TMS message with source address F4.
// VCU/charger/TMS -> BMS messages are deliberately excluded.
var BATTERY_IDS = {
  '0x1801eff3':'B2V_RQ', '0x18e1eff3':'B2V_SysCode1',
  '0x18e2eff3':'B2V_BattInfo1', '0x18e3eff3':'B2V_BattInfo2',
  '0x18e4eff3':'B2V_BattInfo3', '0x18c1eff3':'B2V_Ucell',
  '0x18c2eff3':'B2V_Temp', '0x1881eff3':'B2V_Fult1_32960',
  '0x1882eff3':'B2V_Fult2', '0x1883eff3':'B2V_ST1',
  '0x1884eff3':'B2V_ST2', '0x1885eff3':'B2V_ST3',
  '0x1886eff3':'B2V_ST4', '0x1887eff3':'B2V_ST5',
  '0x1888eff3':'B2V_ST6', '0x1889eff3':'B2V_ST7',
  '0x18c3eff3':'B2V_ElecEnergy', '0x18c4eff3':'B2V_CurrentLimit',
  '0x180156f3':'B2C_Charger', '0x188aeff3':'B2V_StatusOfRelay',
  '0x18ff45f4':'B2TM_Info'
};
var BATTERY_SOURCES = ['0x1884eff3','0x1886eff3','0x1881eff3','0x1882eff3'];
var BATTERY_LEVELS = ['正常','1 级 · 轻微故障','2 级 · 较严重故障','3 级 · 最严重故障'];
// [Intel start bit, bit length, Chinese description]. Reserved bits and counts
// are not faults. Two-bit faults use all four levels (3 is NOT invalid).
var BATTERY_FAULT1 = [
  [2,2,'电芯温差异常'], [4,2,'电芯温度过高'], [6,2,'PACK 过压'],
  [8,2,'PACK 欠压'], [10,2,'SOC 过低'], [12,2,'单体电压过高'],
  [14,2,'单体电压欠压'], [16,2,'绝缘报警'], [18,2,'电池一致性差'],
  [20,2,'储能装置过充'], [22,2,'SOC 过高'], [24,1,'SOC 跳变'],
  [25,1,'储能系统不匹配'], [26,1,'高压互锁报警']
];
var BATTERY_FAULT2 = [
  [8,1,'电芯过放'], [9,1,'电芯低温'], [10,1,'电池包火灾报警'],
  [11,2,'放电电流超限'], [13,2,'行驶回充电流超限'], [15,1,'电流传感器故障'],
  [16,1,'BMS 低压供电异常'], [17,1,'热管理机组故障'], [18,1,'加热膜加热回路故障'],
  [19,1,'电池包自保护'], [20,1,'主回路预充或预检故障'], [21,1,'辅件回路预充或预检故障'],
  [22,1,'充电绝缘阻值低'], [23,1,'A-CAN 通讯故障'], [24,1,'BMS 内部通信故障'],
  [25,1,'BMS DC-DC 故障'], [26,1,'支路断路'],
  [27,1,'加热膜或 TMS 接触器无法闭合'], [28,1,'加热膜或 TMS 接触器无法断开'],
  [29,1,'主正接触器无法闭合'], [30,1,'主正接触器无法断开'],
  [31,1,'主负接触器无法闭合'], [32,1,'主负接触器无法断开'],
  [33,1,'直流充电 A 正接触器无法闭合'], [34,1,'直流充电 A 正接触器无法断开'],
  [35,1,'直流充电 A 负接触器无法闭合'], [36,1,'直流充电 A 负接触器无法断开'],
  [37,1,'直流充电 B 正接触器无法闭合'], [38,1,'直流充电 B 正接触器无法断开'],
  [39,1,'直流充电 B 负接触器无法闭合'], [40,1,'直流充电 B 负接触器无法断开'],
  [41,1,'交流充电正接触器无法闭合'], [42,1,'交流充电正接触器无法断开'],
  [43,1,'交流充电负接触器无法闭合'], [44,1,'交流充电负接触器无法断开'],
  [45,1,'集电弓正接触器无法闭合'], [46,1,'集电弓正接触器无法断开'],
  [47,1,'集电弓负接触器无法闭合'], [48,1,'集电弓负接触器无法断开'],
  [49,1,'插枪连接信号异常'], [50,1,'充电时放电电流过大'], [51,1,'充电电流超限'],
  [52,1,'充电插座 NTC 故障'], [53,2,'充电插座过温'], [55,1,'电子锁故障'],
  [56,1,'火警探测器故障'], [57,2,'持续回充过流'], [59,1,'SOC 差异过大'],
  [60,2,'持续放电过流']
];
function isBatteryFrame(m) {
  return !!(m && m.ext && Object.prototype.hasOwnProperty.call(BATTERY_IDS, normId(m.id)));
}
function resetBattery() {
  batteryFrames = {}; batteryConnected = false; batteryReceivedAt = 0;
}
function ingestBattery(d) {
  batteryConnected = true;
  batteryBootNow = d.clk ? Number(d.clk.boot) : 0;
  batteryReceivedAt = Date.now();
  // This independent device cache excludes RTR and survives monitor eviction.
  // Never fall back to monitor rows, which do not carry an RTR flag.
  batteryFrames = {};
  (d.battery || []).forEach(function(m) {
    var id = normId(m.id);
    if (!isBatteryFrame(m) || BATTERY_SOURCES.indexOf(id) < 0 || !Number.isInteger(m.t)) return;
    var tokens = String(m.data || '').trim().split(/\s+/);
    var valid = m.dlc === 8 && tokens.length === 8 && tokens.every(function(x) { return /^[0-9a-f]{2}$/i.test(x); });
    batteryFrames[id] = { t: m.t, valid: valid, data: valid ? tokens.map(function(x) { return parseInt(x,16); }) : [], hex: String(m.data || '') };
  });
}
function batterySource(id, now) {
  var f = batteryFrames[id];
  if (!f) return { ok:false, reason:'未收到报文' };
  if (!batteryConnected) return { ok:false, reason:'网页与设备断线', frame:f };
  var age = ((batteryBootNow + Math.max(0, now - batteryReceivedAt)) - f.t) >>> 0;
  if (age >= BATTERY_TIMEOUT_MS) return { ok:false, reason:'超时 · ' + (age/1000).toFixed(1) + ' 秒未更新', frame:f };
  if (!f.valid) return { ok:false, reason:'报文长度或数据无效（需 8 字节）', frame:f };
  return { ok:true, reason:(age/1000).toFixed(1) + ' 秒前更新', frame:f };
}
function batteryBits(bytes, start, length) {
  var value = 0;
  for (var i=0; i<length; i++) value += ((bytes[(start+i)>>3] >> ((start+i)%8)) & 1) * Math.pow(2,i);
  return value;
}
function batteryMetric(source, start, length, factor, offset, invalid, min, max) {
  if (!source.ok) return { value:null, reason:source.reason };
  var raw = batteryBits(source.frame.data, start, length);
  if (invalid && Object.prototype.hasOwnProperty.call(invalid, raw)) return { value:null, reason:invalid[raw] };
  var value = raw*factor + offset;
  if (value < min - 1e-8 || value > max + 1e-8) return { value:null, reason:'超出协议范围（原始值 ' + raw + '）' };
  return { value:Math.round(value*1e6)/1e6, reason:source.reason };
}
function batteryFaultList(source, defs) {
  if (!source.ok) return [];
  return defs.map(function(def) {
    var level = batteryBits(source.frame.data,def[0],def[1]);
    return level ? { name:def[2], value:level, text:def[1]===2 ? BATTERY_LEVELS[level] : '故障', bit:def[0] } : null;
  }).filter(Boolean);
}
function batteryModel(now) {
  var st = batterySource(BATTERY_SOURCES[0],now), temp = batterySource(BATTERY_SOURCES[1],now);
  var f1 = batterySource(BATTERY_SOURCES[2],now), f2 = batterySource(BATTERY_SOURCES[3],now);
  var invalid8 = {255:'信号无效 (0xFF)'}, invalid16 = {65535:'信号无效 (0xFFFF)'};
  var invalidTemp = {254:'传感器异常 (0xFE)',255:'信号无效 (0xFF)'};
  var metrics = [
    ['soc','SOC','%',1,batteryMetric(st,0,8,.4,0,invalid8,0,100)],
    ['voltage','电池总电压','V',1,batteryMetric(st,32,16,.1,0,invalid16,0,6553.4)],
    ['current','电池总电流','A',1,batteryMetric(st,16,16,.1,-1000,invalid16,-1000,1000)],
    ['tempAvg','平均温度','°C',0,batteryMetric(temp,16,8,1,-40,invalidTemp,-40,213)],
    ['tempMax','最高温度','°C',0,batteryMetric(temp,0,8,1,-40,invalidTemp,-40,213)],
    ['tempMin','最低温度','°C',0,batteryMetric(temp,8,8,1,-40,invalidTemp,-40,213)]
  ];
  var faults1 = batteryFaultList(f1,BATTERY_FAULT1), faults2 = batteryFaultList(f2,BATTERY_FAULT2);
  var level = st.ok ? batteryBits(st.frame.data,56,2) : null;
  var code = st.ok && st.frame.data[6] !== 255 ? st.frame.data[6] : null;
  var count1 = f1.ok ? batteryBits(f1.frame.data,27,5) : null;
  var count2 = f2.ok ? f2.frame.data[0] : null;
  var fault = !!(level || (code !== null && code !== 0) || count1 || count2 || faults1.length || faults2.length);
  var complete = st.ok && f1.ok && f2.ok && code !== null;
  return { metrics:metrics, st:st, temp:temp, f1:f1, f2:f2, faults1:faults1, faults2:faults2,
    level:level, code:code, count1:count1, count2:count2, fault:fault,
    status:fault ? '检测到故障' : complete ? '当前无故障' : '故障状态待确认',
    tone:fault ? 'error' : complete ? 'ok' : 'warn' };
}
function renderBattery() {
  var model = batteryModel(Date.now());
  var badge = document.getElementById('batteryStatus');
  badge.textContent = batteryConnected ? model.status : batteryReceivedAt ? '设备断线' : '等待报文';
  badge.className = 'battery-badge ' + (batteryConnected ? model.tone : 'warn');
  document.getElementById('batteryMetrics').innerHTML = model.metrics.map(function(m) {
    var reading = m[4], text = reading.value === null ? '—' : reading.value.toFixed(m[3]);
    var hint = reading.reason;
    if (m[0] === 'current' && reading.value !== null) hint += ' · ' + (reading.value < 0 ? '充电（负值）' : reading.value > 0 ? '放电（正值）' : '零电流');
    return '<article class="battery-metric' + (reading.value === null ? ' unavailable' : '') + '"><h3>' + m[1] + '</h3><div class="battery-value" id="battery-' + m[0] + '">' + text + ' <small>' + m[2] + '</small></div><p>' + escHtml(hint) + '</p></article>';
  }).join('');
  var summary = '<div class="battery-fault-group"><p><strong>最高故障等级</strong> · ' + (model.level === null ? escHtml(model.st.reason) : BATTERY_LEVELS[model.level]) + '</p><p>故障码：' + (model.code === null ? '未取得有效故障码' : model.code + ' (0x' + model.code.toString(16).toUpperCase().padStart(2,'0') + ')') + '</p></div>';
  function faultGroup(title, source, count, faults) {
    var html = '<div class="battery-fault-group"><p><strong>' + title + '</strong> · ' + escHtml(source.reason) + '</p>';
    if (source.ok) {
      html += '<p>BMS 上报数目：' + count + '</p>';
      html += faults.length ? '<ul>' + faults.map(function(f) { return '<li>' + f.name + ' · ' + f.text + '</li>'; }).join('') + '</ul>' : '<p>' + (count ? '存在故障，当前已定义故障位未置位，请结合故障码核对。' : '当前报文未报告故障') + '</p>';
    }
    return html + '</div>';
  }
  document.getElementById('batteryFaults').innerHTML = summary + faultGroup('国标报警 · 0x1881EFF3',model.f1,model.count1,model.faults1) + faultGroup('BMS 故障 · 0x1882EFF3',model.f2,model.count2,model.faults2);
  document.getElementById('batterySources').innerHTML = BATTERY_SOURCES.map(function(id) {
    var source = batterySource(id,Date.now());
    return '<tr><td>' + id.toUpperCase().replace('0X','0x') + '<br>' + BATTERY_IDS[id] + '</td><td>' + escHtml(source.reason) + (source.frame ? '<br>' + fmtFrameTime(source.frame.t) : '') + '</td><td>' + (source.frame ? escHtml(source.frame.hex) : '—') + '</td></tr>';
  }).join('');
}
function showBattery() {
  currentView = 'battery'; setTopTab('tabBattery');
  contentMain.classList.add('hidden'); contentDetail.classList.add('hidden'); contentCharts.classList.add('hidden');
  document.getElementById('viewBattery').classList.remove('hidden');
  document.getElementById('backRow').classList.add('hidden'); sendPanel.classList.add('hidden');
  document.getElementById('headerTitle').textContent = '电池信息'; renderBattery();
}
</script>)rawliteral"
