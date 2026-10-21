import assert from 'node:assert/strict';
import fs from 'node:fs';
import { boot, device, response, settle } from './recording-ui-harness.mjs';
import { batteryPacket, batteryFrame } from './battery-fixtures.mjs';

const app = await boot(device([]));
const c = app.context;
const model = () => c.batteryModel(c.batteryReceivedAt);
const values = () => Array.from(model().metrics, x => x[4].value);
function load(packet = batteryPacket()) { c.processMessages(packet); return packet; }
try {
  c.showBattery();
  assert.equal(model().metrics[0][4].value, null);
  assert.equal(model().status, '故障状态待确认');
  load();
  assert.deepEqual(values(), [80,600,100,28,35,20]);
  assert.equal(model().status, '当前无故障');
  assert.match(app.element('batteryMetrics').innerHTML, /放电（正值）/);
  const charge = load(); charge.battery[0].data = '00 FA 1C 25 00 00 00 10'; load(charge);
  assert.deepEqual(values().slice(0,3), [0,0,-50]);
  assert.match(app.element('batteryMetrics').innerHTML, /充电（负值）/);
  const bounds = batteryPacket(); bounds.battery[0].data = 'FA FA 20 4E FE FF 00 10'; load(bounds);
  assert.deepEqual(values().slice(0,3), [100,6553.4,1000]);
  const invalid = batteryPacket(); invalid.battery[0].data = 'FF FF FF FF FF FF FF 10';
  invalid.battery[1].data = 'FE FF 00 00 00 00 00 00'; load(invalid);
  assert.deepEqual(values(), [null,null,null,-40,null,null]);
  assert.match(app.element('batteryMetrics').innerHTML, /传感器异常/);
  assert.equal(model().status, '故障状态待确认');
  const range = batteryPacket(); range.battery[0].data = 'FB 00 21 4E 00 00 00 10'; load(range);
  assert.equal(values()[0], null); assert.equal(values()[2], null);
  console.log('PASS Intel scaling, charge/discharge sign, zero/bounds, reserved and invalid values');

  // Explicit bytes exercise bitfields crossing byte boundaries and high bits.
  const faults = batteryPacket();
  faults.battery[0].data = 'C8 FA F8 2A 70 17 2A 13';
  faults.battery[2].data = '30 00 00 0C 00 00 00 00'; // over-temp level 3, HVIL, count 1
  faults.battery[3].data = '03 10 00 80 00 00 00 2C'; // discharge level2, main-neg open,
  load(faults); // continued charge level2, SOC diff, continued discharge level2
  const m = model();
  assert.equal(m.level,3); assert.equal(m.code,42); assert.equal(m.count1,1); assert.equal(m.count2,3);
  assert.deepEqual(Array.from(m.faults1,x=>[x.bit,x.value]), [[4,3],[26,1]]);
  assert.deepEqual(Array.from(m.faults2,x=>[x.bit,x.value]), [[11,2],[31,1],[57,2],[59,1],[60,2]]);
  assert.equal(m.status,'检测到故障');
  assert.match(app.element('batteryFaults').innerHTML,/主负接触器无法闭合/);
  assert.match(app.element('batteryFaults').innerHTML,/3 级/);
  const reserved = batteryPacket(); reserved.battery[2].data = '03 00 00 00 FF FF FF FF';
  reserved.battery[3].data = '00 00 00 00 00 00 00 C0'; load(reserved);
  assert.equal(model().status,'当前无故障', 'Reserved bits must not raise faults');
  const countOnly = batteryPacket(); countOnly.battery[3].data = '01 00 00 00 00 00 00 00'; load(countOnly);
  assert.equal(model().status,'检测到故障');
  console.log('PASS fault levels, counts, cross-byte and bit 60 fields; reserved bits ignored');

  load();
  assert.equal(c.batteryModel(c.batteryReceivedAt+9950).metrics[0][4].value, null, '10-second timeout boundary');
  c.batteryConnected = false; assert.equal(values()[0],null);
  assert.notEqual(model().status,'当前无故障');
  const missing = batteryPacket(); missing.battery = missing.battery.slice(0,2); load(missing);
  assert.equal(model().status,'故障状态待确认');
  const short = batteryPacket(); short.battery[0].data='00'; short.battery[0].dlc=1; load(short);
  assert.equal(values()[0],null); assert.notEqual(model().status,'当前无故障');
  const bad = batteryPacket(); bad.battery[0].data='GG FA F8 2A 70 17 00 10'; load(bad);
  assert.equal(values()[0],null);
  const standard = batteryPacket(); standard.battery[0].ext=false; load(standard); assert.equal(values()[0],null);
  const wrap = batteryPacket({clk:{boot:20,sync:false}});
  wrap.battery.forEach(f=>f.t=0xfffffff0); load(wrap); assert.equal(values()[0],80);
  load(batteryPacket({boot:'restarted',battery:[],messages:[],total:0}));
  assert.equal(values()[0],null); assert.equal(Object.keys(c.batteryFrames).length,0);
  // Monitor rows alone (including unmarked RTR) must never be decoded as BMS data.
  load(batteryPacket({battery:[]})); assert.equal(values()[0],null);
  console.log('PASS missing/short/malformed/standard frames, timeout, disconnect, reboot and uint32 wrap');

  const ids = Object.keys(c.BATTERY_IDS);
  assert.equal(ids.length,21);
  for (const id of ids) assert(c.isBatteryFrame({id:id.toUpperCase().replace('0X','0x'),ext:true}));
  for (const id of ['0x1802F3EF','0x18E1F3EF','0x1801F356','0x18FFC13A','0x123','0x1884EFF4'])
    assert.equal(c.isBatteryFrame({id,ext:true}),false,id);
  const monitor = batteryPacket();
  monitor.messages.push(batteryFrame('0x1802F3EF','00 00 00 00 00 00 00 00'));
  monitor.total=5; load(monitor); c.showMain();
  assert.equal((app.element('mainBody').innerHTML.match(/battery-row/g)||[]).length,4);
  const before=app.backend.calls.length;
  for (let i=0;i<3;i++) { c.showBattery(); c.showCharts(); c.showMain(); }
  assert(!app.backend.calls.slice(before).some(x=>x.url==='/api/send'));
  c.showBattery(); assert(app.element('sendPanel').classList.contains('hidden'));
  c.showDetail('0x1884EFF3'); assert(app.element('viewBattery').classList.contains('hidden'));
  c.showMain(); assert(!app.element('sendPanel').classList.contains('hidden'));
  const fetch = c.fetch;
  let sent;
  c.fetch = async (url, options) => { if (url==='/api/send') { sent=JSON.parse(options.body); return response({ok:true}); } return fetch(url,options); };
  app.element('sendId').value='0x123'; app.element('sendDlc').value='2';
  app.element('sendData').value='12 34'; app.element('sendExt').checked=false;
  await app.element('sendBtn').click(); await settle();
  assert.deepEqual(sent,{id:'0x123',dlc:2,data:'12 34',extended:false});
  console.log('PASS 21 exact BMS IDs highlighted, page navigation passive, manual send retained');

  const can = fs.readFileSync(new URL('../main/can.c',import.meta.url),'utf8');
  assert.match(can,/TWAI_MODE_NORMAL/); assert.match(can,/twai_transmit\(&msg/);
  assert.match(can,/can_monitor_record\(&entry, !rx_msg.rtr\)/);
  assert.match(can,/if \(data_frame && entry->extended\)/);
} finally { app.dispose(); }
console.log('Battery UI checks passed. No CAN test frames sent.');
