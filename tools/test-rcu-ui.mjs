import assert from 'node:assert/strict';
import {boot,device} from './recording-ui-harness.mjs';
import {rcuFrame,rcuPacket,rcuPack} from './rcu-fixtures.mjs';
const app=await boot(device([])),c=app.context;
function load(packet=rcuPacket()){c.processMessages(packet);return packet;}
function model(){return c.rcuModel(c.rcuReceivedAt);}
function value(key){return model().readings[key].value;}
try{
  c.showRcu();assert.equal(model().readings.stREX.value,null);
  load();
  for(const [key,expected] of Object.entries({nEngActSpdTxi:1600,tEngCoTempTx:80,tEngUrTempTx:30,rEngUrFldLvl:50,pEngOilPrsFrmRCU:400,EngUrConc:32.5,
    stREX:7,nFISGActSpdTx:1500,trqFISGActTx:-250,tISGInvTempTx:50,tISGCoilTempTx:70,numRcu6MsgCnt:15,numRcu6MsgChks:10,
    iREXOutCur:-100,pwrREXOut:-60,pwrREXMax:-100,wRexGenrTot:123456,uREXOutVlt:600,pwrREMaxDischrg:50,
    numMajrVers:3,numSendVers:5,numProdCod:0x1234567,tiYear:25,tiMth:10,tiDay:21,
    pwrREPowerOnLopAvrg:-60,pwrREGenrLopAvrg:-55,pwrRESetTiAvrg:-50,pwrREReq:10}))assert.equal(value(key),expected,key);
  assert.equal(model().status,'正常充电');
  assert.equal(model().readings.bCrsRunLamp.text,'灯亮');
  assert.match(app.element('rcuMetrics').innerHTML,/发电（负值）/);
  console.log('PASS exact matrix scaling, unsigned-offset signs, 12/24/30-bit and unaligned fields');

  const fault=rcuPacket();fault.rcu[2].data='7B DC 05 8E 12 5A 6E AF';fault.rcu[3].data='24 13 94 07 6C 07 27 01';
  for(let p=1;p<=5;p++)fault.rcu[7+p].data=rcuPack([[0,8,p],...(p===1?[8,63]:p===2?[8,45]:p===3?[9,62]:p===4?[8,60]:[8,9]).map(bit=>[bit,1,1])]);
  load(fault);
  assert.equal(model().readings.stREXErrDeg.text,'停机');assert.equal(model().readings.stREXSysErrLvl.text,'三级故障');
  assert.equal(model().readings.bEmgyHVOffReq.text,'请求高压急停');
  assert.deepEqual(Array.from(model().dtc,g=>Array.from(g.active)),[[1,45],[46,78],[79,99],[100,135],[136,137]]);
  assert.equal(Object.keys(c.rcuFrames).length,13,'Five multiplex groups must coexist');
  assert.match(app.element('rcuFaults').innerHTML,/RCU 故障 137/);
  assert.equal(Object.values(c.RCU_DTC).flat().length,137);
  load();assert.equal(model().dtc.flatMap(g=>Array.from(g.active)).length,0,'A new zero page clears that group');
  console.log('PASS two fault levels, emergency flag and 137 sparse multiplex fault positions');

  const max=rcuPacket();max.rcu[4].data='FF FF FF 58 02 32 00 00';
  max.rcu[6].data='FF FF FF FF FF FF 00 00';
  max.rcu[5].data=rcuPack([[0,10,1023],[10,8,255],[18,30,0x3fffffff],[48,7,127],[55,4,12],[59,5,31]]);
  max.rcu[7].data='FF 00 00 00 00 00 00 00';load(max);
  assert.equal(value('wRexGenrTot'),16777215);assert.equal(value('pwrREReq'),2095);
  assert.equal(value('numProdCod'),0x3fffffff);assert.equal(value('numRcuFaultCod'),255);
  assert.equal(value('numSendVers'),255,'Blank invalid columns do not define FF as invalid');
  const invalid=rcuPacket();invalid.rcu[0].data='FF FF FF FF FF FF FF FF';invalid.rcu[2].data='F4 FF FF FF FF FF FF FF';load(invalid);
  assert.equal(value('nEngActSpdTxi'),null);assert.equal(value('tEngCoTempTx'),null);assert.equal(value('nFISGActSpdTx'),null);
  assert.match(model().status,/未定义/);assert.equal(model().readings.stREXErrDeg.text,'预留');
  assert.notEqual(model().tone,'error','Reserved disposition 4 must not be treated as stop fault');
  const unknown=rcuPacket();unknown.rcu.push(rcuFrame('0x18019894','06 FF FF FF FF FF FF FF'));load(unknown);
  assert.match(app.element('rcuFaults').innerHTML,/未知故障分组 6/);
  assert.equal(model().dtc.flatMap(g=>Array.from(g.active)).length,0);
  console.log('PASS valid all-ones boundaries, out-of-range values, reserved enums, unknown DTC pages');

  load();let stale=c.rcuModel(c.rcuReceivedAt+9950);
  assert.equal(stale.readings.stREX.value,null);assert.equal(stale.readings.numProdCod.value,0x1234567);
  assert.equal(c.rcuModel(c.rcuReceivedAt+14950).readings.numProdCod.value,null);
  const short=rcuPacket();short.rcu[2].data='70';short.rcu[2].dlc=1;load(short);assert.equal(value('stREX'),null);
  const bad=rcuPacket();bad.rcu[2].data='ZZ DC 05 8E 12 5A 6E AF';load(bad);assert.equal(value('stREX'),null);
  c.rcuConnected=false;assert.equal(value('nEngActSpdTxi'),null);
  const wrap=rcuPacket({clk:{boot:20,sync:false}});wrap.rcu.forEach(f=>f.t=0xfffffff0);load(wrap);assert.equal(value('nEngActSpdTxi'),1600);
  const cacheOnly=rcuPacket({total:0,messages:[]});load(cacheOnly);assert.equal(value('stREX'),7);
  load(rcuPacket({boot:'reboot',rcu:[],messages:[],total:0}));assert.equal(value('stREX'),null);
  load(rcuPacket({rcu:[]}));assert.equal(value('stREX'),null,'No fallback to monitor rows/RTR');
  console.log('PASS independent cache, timeouts, clock wrap, malformed data, disconnect and reboot');

  assert.equal(Object.keys(c.RCU_IDS).length,9);
  assert(c.isRcuFrame({id:'0x18019894',ext:true}),'Sender flags override source-address guess');
  for(const id of ['0x0C819C94','0x0C829C94','0x0C839C94','0x0C849C94','0x0C859C94','0x0C869C94','0x1891949D','0x18E00021'])assert.equal(c.isRcuFrame({id,ext:true}),false,id);
  assert.equal(c.isRcuFrame({id:'0x1891949C',ext:false}),false);
  const colors=rcuPacket();colors.messages.push(rcuFrame('0x1884EFF3','C8 FA F8 2A 70 17 00 10'),rcuFrame('0x0C819C94','00 00 00 00 00 00 00 00'));
  colors.total=colors.messages.length;
  load(colors);c.showMain();let html=app.element('mainBody').innerHTML;
  assert.equal((html.match(/rcu-row/g)||[]).length,9);assert.equal((html.match(/battery-row/g)||[]).length,1);
  for(const show of [c.showBattery,c.showMain,c.showCharts,()=>c.showDetail('0x1891949C')]){c.showRcu();show();assert(app.element('viewRcu').classList.contains('hidden'));}
  c.showRcu();assert(app.element('sendPanel').classList.contains('hidden'));c.showMain();assert(!app.element('sendPanel').classList.contains('hidden'));
  assert(!app.backend.calls.some(x=>x.url==='/api/send'));
  console.log('PASS nine exact RCU IDs pale red, battery sky blue, command IDs excluded, passive navigation');
}finally{app.dispose();}
