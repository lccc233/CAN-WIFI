#pragma once

// Extracted from the supplied YCF CAN Matrix V3.5, VCU-RCU sheet rows 63-257.
// Intel fields follow source physical ranges; blank invalid-value cells do not imply FF.
#define PAGE_RCU_JS R"rawliteral(
<script>
var RCU_FIELDS = {
  "nEngActSpdTxi":{"id":"0x1404949c","start":0,"len":16,"factor":0.125,"offset":0.0,"min":0.0,"max":8032.0,"row":64,"label":"发动机转速","unit":"rpm"},
  "tEngCoTempTx":{"id":"0x1404949c","start":16,"len":8,"factor":1.0,"offset":-40.0,"min":-40.0,"max":210.0,"row":65,"label":"冷却液温度","unit":"°C"},
  "tEngUrTempTx":{"id":"0x1404949c","start":24,"len":8,"factor":1.0,"offset":-40.0,"min":-40.0,"max":210.0,"row":66,"label":"尿素温度","unit":"°C"},
  "rEngUrFldLvl":{"id":"0x1404949c","start":32,"len":8,"factor":0.4,"offset":0.0,"min":0.0,"max":100.0,"row":67,"label":"尿素液位","unit":"%"},
  "pEngOilPrsFrmRCU":{"id":"0x1404949c","start":40,"len":8,"factor":4.0,"offset":0.0,"min":0.0,"max":1000.0,"row":68,"label":"机油压力","unit":"kPa"},
  "EngUrConc":{"id":"0x1404949c","start":48,"len":8,"factor":0.25,"offset":0.0,"min":0.0,"max":62.5,"row":69,"label":"尿素浓度","unit":"%"},
  "bEngInAirLamp":{"id":"0x1405949c","start":0,"len":2,"factor":1.0,"offset":0.0,"min":0.0,"max":3.0,"row":72,"label":"进气预热灯","unit":""},
  "bEngWtrInOilLamp":{"id":"0x1405949c","start":2,"len":2,"factor":1.0,"offset":0.0,"min":0.0,"max":3.0,"row":73,"label":"油中有水报警灯","unit":""},
  "bEngOilLpLamp":{"id":"0x1405949c","start":4,"len":2,"factor":1.0,"offset":0.0,"min":0.0,"max":3.0,"row":74,"label":"机油压力低报警灯","unit":""},
  "bEngWtrOTLamp":{"id":"0x1405949c","start":6,"len":2,"factor":1.0,"offset":0.0,"min":0.0,"max":3.0,"row":75,"label":"水温过高报警灯","unit":""},
  "bPTORunLamp":{"id":"0x1405949c","start":8,"len":5,"factor":1.0,"offset":0.0,"min":0.0,"max":31.0,"row":76,"label":"PTO 工作灯","unit":""},
  "bCrsRunLamp":{"id":"0x1405949c","start":13,"len":3,"factor":1.0,"offset":0.0,"min":0.0,"max":7.0,"row":77,"label":"巡航工作灯","unit":""},
  "bCbkBrkLamp":{"id":"0x1405949c","start":16,"len":2,"factor":1.0,"offset":0.0,"min":0.0,"max":3.0,"row":78,"label":"缸内制动灯","unit":""},
  "bEgBrkLamp":{"id":"0x1405949c","start":18,"len":2,"factor":1.0,"offset":0.0,"min":0.0,"max":3.0,"row":79,"label":"排气制动灯","unit":""},
  "bEgHiTLamp":{"id":"0x1405949c","start":20,"len":3,"factor":1.0,"offset":0.0,"min":0.0,"max":7.0,"row":80,"label":"高排气温度 / DPF 灯","unit":""},
  "stREXErrDeg":{"id":"0x1891949c","start":0,"len":3,"factor":1.0,"offset":0.0,"min":0.0,"max":7.0,"row":83,"label":"RCU 控制处置等级","unit":""},
  "bEmgyHVOffReq":{"id":"0x1891949c","start":3,"len":1,"factor":1.0,"offset":0.0,"min":0.0,"max":1.0,"row":84,"label":"高压急停请求","unit":""},
  "stREX":{"id":"0x1891949c","start":4,"len":4,"factor":1.0,"offset":0.0,"min":0.0,"max":15.0,"row":85,"label":"RCU 运行状态","unit":""},
  "nFISGActSpdTx":{"id":"0x1891949c","start":8,"len":16,"factor":1.0,"offset":0.0,"min":0.0,"max":8032.0,"row":86,"label":"FISG 电机转速","unit":"rpm"},
  "trqFISGActTx":{"id":"0x1891949c","start":24,"len":16,"factor":1.0,"offset":-5000.0,"min":-5000.0,"max":5000.0,"row":87,"label":"FISG 实际扭矩","unit":"N·m"},
  "tISGInvTempTx":{"id":"0x1891949c","start":40,"len":8,"factor":1.0,"offset":-40.0,"min":-40.0,"max":210.0,"row":88,"label":"电机控制器温度","unit":"°C"},
  "tISGCoilTempTx":{"id":"0x1891949c","start":48,"len":8,"factor":1.0,"offset":-40.0,"min":-40.0,"max":210.0,"row":89,"label":"电机绕组温度","unit":"°C"},
  "numRcu6MsgCnt":{"id":"0x1891949c","start":56,"len":4,"factor":1.0,"offset":0.0,"min":0.0,"max":15.0,"row":90,"label":"RCU6 计数器","unit":""},
  "numRcu6MsgChks":{"id":"0x1891949c","start":60,"len":4,"factor":1.0,"offset":0.0,"min":0.0,"max":15.0,"row":91,"label":"RCU6 校验和字段","unit":""},
  "iREXOutCur":{"id":"0x1892949c","start":0,"len":16,"factor":1.0,"offset":-5000.0,"min":-5000.0,"max":1000.0,"row":93,"label":"输出电流","unit":"A"},
  "pwrREXOut":{"id":"0x1892949c","start":16,"len":16,"factor":1.0,"offset":-2000.0,"min":-2000.0,"max":2000.0,"row":94,"label":"输出功率","unit":"kW"},
  "pwrREXMax":{"id":"0x1892949c","start":32,"len":16,"factor":1.0,"offset":-2000.0,"min":-2000.0,"max":2000.0,"row":95,"label":"最大允许发电功率","unit":"kW"},
  "bPwrLimCmd":{"id":"0x1892949c","start":48,"len":1,"factor":1.0,"offset":0.0,"min":0.0,"max":1.0,"row":96,"label":"供能系统功率受限","unit":""},
  "stREXSysErrLvl":{"id":"0x1892949c","start":49,"len":2,"factor":1.0,"offset":0.0,"min":0.0,"max":3.0,"row":97,"label":"系统故障等级","unit":""},
  "stGCUActvDchrgFb":{"id":"0x1892949c","start":53,"len":3,"factor":1.0,"offset":0.0,"min":0.0,"max":7.0,"row":99,"label":"GCU 主动放电状态","unit":""},
  "bEngWrmReq":{"id":"0x1892949c","start":56,"len":1,"factor":1.0,"offset":0.0,"min":0.0,"max":1.0,"row":100,"label":"发动机暖机请求","unit":""},
  "wRexGenrTot":{"id":"0x1893949c","start":0,"len":24,"factor":1.0,"offset":0.0,"min":0.0,"max":16777215.0,"row":102,"label":"累计发电量","unit":"kWh"},
  "uREXOutVlt":{"id":"0x1893949c","start":24,"len":16,"factor":1.0,"offset":0.0,"min":0.0,"max":1000.0,"row":103,"label":"输出电压","unit":"V"},
  "pwrREMaxDischrg":{"id":"0x1893949c","start":40,"len":16,"factor":1.0,"offset":0.0,"min":0.0,"max":2000.0,"row":104,"label":"最大允许耗电功率","unit":"kW"},
  "numMajrVers":{"id":"0x1894949c","start":0,"len":10,"factor":1.0,"offset":0.0,"min":0.0,"max":1023.0,"row":106,"label":"软件主版本","unit":""},
  "numSendVers":{"id":"0x1894949c","start":10,"len":8,"factor":1.0,"offset":0.0,"min":0.0,"max":255.0,"row":107,"label":"软件次版本","unit":""},
  "numProdCod":{"id":"0x1894949c","start":18,"len":30,"factor":1.0,"offset":0.0,"min":0.0,"max":1073741823.0,"row":108,"label":"产品代号","unit":""},
  "tiYear":{"id":"0x1894949c","start":48,"len":7,"factor":1.0,"offset":0.0,"min":0.0,"max":127.0,"row":109,"label":"年份字段（原始值）","unit":""},
  "tiMth":{"id":"0x1894949c","start":55,"len":4,"factor":1.0,"offset":0.0,"min":0.0,"max":12.0,"row":110,"label":"月份字段","unit":""},
  "tiDay":{"id":"0x1894949c","start":59,"len":5,"factor":1.0,"offset":0.0,"min":0.0,"max":31.0,"row":111,"label":"日期字段","unit":""},
  "pwrREPowerOnLopAvrg":{"id":"0x1896949c","start":0,"len":12,"factor":1.0,"offset":-2000.0,"min":-2000.0,"max":2095.0,"row":113,"label":"上电循环平均发电功率","unit":"kW"},
  "pwrREGenrLopAvrg":{"id":"0x1896949c","start":12,"len":12,"factor":1.0,"offset":-2000.0,"min":-2000.0,"max":2095.0,"row":114,"label":"发电循环平均发电功率","unit":"kW"},
  "pwrRESetTiAvrg":{"id":"0x1896949c","start":24,"len":12,"factor":1.0,"offset":-2000.0,"min":-2000.0,"max":2095.0,"row":115,"label":"设定时间平均发电功率","unit":"kW"},
  "pwrREReq":{"id":"0x1896949c","start":36,"len":12,"factor":1.0,"offset":-2000.0,"min":-2000.0,"max":2095.0,"row":116,"label":"增程器需求功率","unit":"kW"},
  "numRcuFaultCod":{"id":"0x1895949c","start":0,"len":8,"factor":1.0,"offset":0.0,"min":0.0,"max":255.0,"row":257,"label":"RCU 故障代码（原始值）","unit":""},
};
// [fault number, Intel start bit], preserving sparse positions exactly.
var RCU_DTC = {"1":[[1,8],[2,9],[3,10],[4,11],[5,12],[6,13],[7,14],[8,15],[9,16],[10,17],[11,18],[12,19],[13,20],[14,21],[15,22],[16,23],[17,24],[18,25],[19,26],[20,27],[21,28],[22,29],[23,31],[24,32],[25,33],[26,34],[27,35],[28,36],[29,37],[30,38],[31,39],[32,40],[33,41],[34,42],[35,43],[36,44],[37,51],[38,52],[39,55],[40,56],[41,59],[42,60],[43,61],[44,62],[45,63]],"2":[[46,8],[47,9],[48,10],[49,11],[50,12],[51,13],[52,14],[53,15],[54,16],[55,17],[56,18],[57,19],[58,20],[59,21],[60,22],[61,23],[62,25],[63,26],[64,27],[65,28],[66,29],[67,30],[68,31],[69,32],[70,33],[71,34],[72,35],[73,36],[74,37],[75,38],[76,39],[77,40],[78,45]],"3":[[79,9],[80,19],[81,24],[82,36],[83,37],[84,38],[85,39],[86,40],[87,41],[88,42],[89,43],[90,44],[91,46],[92,47],[93,48],[94,49],[95,50],[96,51],[97,52],[98,53],[99,62]],"4":[[100,8],[101,10],[102,11],[103,13],[104,15],[105,16],[106,17],[107,18],[108,20],[109,29],[110,30],[111,31],[112,32],[113,33],[114,34],[115,35],[116,36],[117,37],[118,38],[119,39],[120,40],[121,41],[122,42],[123,43],[124,45],[125,46],[126,47],[127,48],[128,49],[129,50],[130,51],[131,55],[132,56],[133,58],[134,59],[135,60]],"5":[[136,8],[137,9]]};
var RCU_IDS = {
  '0x1404949c':'RCU4', '0x1405949c':'RCU5', '0x1891949c':'RCU6',
  '0x1892949c':'RCU7', '0x1893949c':'RCU8', '0x1894949c':'RCU9',
  '0x1896949c':'RCU10', '0x18019894':'RCUDTC1', '0x1895949c':'RCUDTC2'
};
var RCU_ENUMS = {
  stREX:{0:'程序初始化',1:'准备未就绪',2:'准备就绪',3:'启动发动机',4:'停机中',5:'怠速',6:'怠速充电',7:'正常充电',8:'靠边停车（跛行）',9:'高压电池耗电'},
  stREXErrDeg:{0:'正常',1:'降功率',2:'强制怠速',3:'停机',4:'预留'},
  stREXSysErrLvl:{0:'正常',1:'一级故障',2:'二级故障',3:'三级故障'},
  bEmgyHVOffReq:{0:'无请求',1:'请求高压急停'},
  bPwrLimCmd:{0:'未受限',1:'功率受限'}, bEngWrmReq:{0:'无暖机请求',1:'暖机请求'},
  stGCUActvDchrgFb:{1:'待机',2:'主动放电中',3:'主动放电完成',4:'主动放电失败'},
  bPTORunLamp:{1:'灯亮',5:'灯亮',6:'灯亮',8:'灯亮',10:'灯亮'},
  bCrsRunLamp:{0:'灯灭',1:'灯亮',2:'灯亮',3:'灯亮',4:'灯亮',5:'灯亮',6:'灯亮',7:'灯灭'}
};
['bEngInAirLamp','bEngWtrInOilLamp','bEngOilLpLamp','bEngWtrOTLamp','bCbkBrkLamp','bEgBrkLamp','bEgHiTLamp'].forEach(function(key){RCU_ENUMS[key]={0:'灯灭',1:'灯亮'};});
var RCU_MAIN = ['nEngActSpdTxi','nFISGActSpdTx','uREXOutVlt','iREXOutCur','pwrREXOut','tEngCoTempTx','tISGInvTempTx','tISGCoilTempTx'];
var RCU_STATES = ['stREX','stREXErrDeg','stREXSysErrLvl','bEmgyHVOffReq','bPwrLimCmd','stGCUActvDchrgFb','bEngWrmReq','numRcuFaultCod'];
var RCU_PARAMETERS = ['trqFISGActTx','pEngOilPrsFrmRCU','tEngUrTempTx','rEngUrFldLvl','EngUrConc','wRexGenrTot','pwrREXMax','pwrREMaxDischrg','pwrREPowerOnLopAvrg','pwrREGenrLopAvrg','pwrRESetTiAvrg','pwrREReq','numRcu6MsgCnt','numRcu6MsgChks'];
var RCU_LAMPS = ['bEngInAirLamp','bEngWtrInOilLamp','bEngOilLpLamp','bEngWtrOTLamp','bPTORunLamp','bCrsRunLamp','bCbkBrkLamp','bEgBrkLamp','bEgHiTLamp'];
var RCU_VERSION = ['numMajrVers','numSendVers','numProdCod','tiYear','tiMth','tiDay'];
var rcuFrames={}, rcuConnected=false, rcuBootNow=0, rcuReceivedAt=0;
function isRcuFrame(m){return !!(m&&m.ext&&Object.prototype.hasOwnProperty.call(RCU_IDS,normId(m.id)));}
function resetRcu(){rcuFrames={};rcuConnected=false;rcuReceivedAt=0;}
function rcuBits(bytes,start,length){
  var value=0;
  for(var i=0;i<length;i++)value+=((bytes[(start+i)>>3]>>((start+i)%8))&1)*Math.pow(2,i);
  return value;
}
function ingestRcu(d){
  rcuConnected=true;rcuBootNow=d.clk?Number(d.clk.boot):0;rcuReceivedAt=Date.now();rcuFrames={};
  (d.rcu||[]).forEach(function(m){
    if(!isRcuFrame(m)||!Number.isInteger(m.t))return;
    var id=normId(m.id), tokens=String(m.data||'').trim().split(/\s+/);
    var valid=m.dlc===8&&tokens.length===8&&tokens.every(function(x){return /^[0-9a-f]{2}$/i.test(x);});
    var page=tokens.length&&/^[0-9a-f]{2}$/i.test(tokens[0])?parseInt(tokens[0],16):0;
    var key=id==='0x18019894'?'dtc/'+(page>=1&&page<=5?page:'?'):id;
    rcuFrames[key]={id:id,t:m.t,valid:valid,page:page,b:valid?tokens.map(function(x){return parseInt(x,16);}):[],hex:String(m.data||'')};
  });
}
function rcuSource(key,now){
  var f=rcuFrames[key];
  if(!f)return {ok:false,reason:'未收到报文'};
  if(!rcuConnected)return {ok:false,reason:'网页与设备断线',frame:f};
  var age=((rcuBootNow+Math.max(0,now-rcuReceivedAt))-f.t)>>>0;
  var timeout=f.id==='0x1894949c'?15000:10000;
  if(age>=timeout)return {ok:false,reason:'超时 · '+(age/1000).toFixed(1)+' 秒未更新',frame:f};
  if(!f.valid)return {ok:false,reason:'报文长度或数据无效（需 8 字节）',frame:f};
  return {ok:true,reason:(age/1000).toFixed(1)+' 秒前更新',frame:f};
}
function rcuReading(key,now){
  var def=RCU_FIELDS[key],src=rcuSource(def.id,now);
  if(!src.ok)return {value:null,raw:null,text:'—',reason:src.reason,defined:false};
  var raw=rcuBits(src.frame.b,def.start,def.len), value=raw*def.factor+def.offset;
  if(value<def.min-1e-8||value>def.max+1e-8)return {value:null,raw:raw,text:'—',reason:'超出协议范围（原始值 '+raw+'）',defined:false};
  value=Math.round(value*1e6)/1e6;
  var map=RCU_ENUMS[key],defined=!map||Object.prototype.hasOwnProperty.call(map,raw);
  var text=map?(defined?map[raw]:'未定义（0x'+raw.toString(16).toUpperCase()+'）'):String(value);
  if(key==='iREXOutCur'||key==='pwrREXOut')text+=' · '+(value<0?'发电':value>0?'耗电':'零值');
  return {value:value,raw:raw,text:text,reason:src.reason,defined:defined};
}
function rcuModel(now){
  var readings={};Object.keys(RCU_FIELDS).forEach(function(key){readings[key]=rcuReading(key,now);});
  var dtc=[];
  for(var p=1;p<=5;p++){
    var src=rcuSource('dtc/'+p,now),active=[];
    if(src.ok)RCU_DTC[p].forEach(function(def){if(rcuBits(src.frame.b,def[1],1))active.push(def[0]);});
    dtc.push({page:p,source:src,active:active});
  }
  var state=readings.stREX;
  var alarm=['stREXErrDeg','stREXSysErrLvl','bEmgyHVOffReq'].some(function(key){var r=readings[key];return r.defined&&r.value!==null&&r.value>0&&r.value<=3;})||dtc.some(function(g){return g.active.length>0;});
  var uncertain=['stREXErrDeg','stREXSysErrLvl','bEmgyHVOffReq'].some(function(key){var r=readings[key];return r.value===null||!r.defined||(key==='stREXErrDeg'&&r.value===4);});
  var ready=state.defined&&state.value!==null&&[2,5,6,7,9].indexOf(state.value)>=0&&!uncertain;
  return {readings:readings,dtc:dtc,status:(state.value===null?state.reason:state.text)+(alarm?' · 存在报警':''),
    tone:alarm?'error':ready?'ok':'warn'};
}
function rcuTable(keys,readings){
  return '<table><thead><tr><th>项目</th><th>读数 / 状态</th><th>数据状态</th></tr></thead><tbody>'+keys.map(function(key){
    var d=RCU_FIELDS[key],r=readings[key];
    return '<tr><td>'+d.label+'</td><td>'+escHtml(r.text)+(r.value!==null&&d.unit?' '+d.unit:'')+'</td><td>'+escHtml(r.reason)+'</td></tr>';
  }).join('')+'</tbody></table>';
}
function renderRcu(){
  var now=Date.now(),m=rcuModel(now),badge=document.getElementById('rcuStatus');
  badge.textContent=rcuConnected?m.status:rcuReceivedAt?'设备断线':'等待报文';
  badge.className='battery-badge '+(rcuConnected?m.tone:'warn');
  document.getElementById('rcuMetrics').innerHTML=RCU_MAIN.map(function(key){
    var d=RCU_FIELDS[key],r=m.readings[key],hint=r.reason;
    if((key==='iREXOutCur'||key==='pwrREXOut')&&r.value!==null)hint+=' · '+(r.value<0?'发电（负值）':r.value>0?'耗电（正值）':'零值');
    return '<article class="battery-metric'+(r.value===null?' unavailable':'')+'"><h3>'+d.label+'</h3><div class="battery-value" id="rcu-'+key+'">'+(r.value===null?'—':r.value)+' <small>'+d.unit+'</small></div><p>'+escHtml(hint)+'</p></article>';
  }).join('');
  document.getElementById('rcuState').innerHTML=rcuTable(RCU_STATES,m.readings);
  document.getElementById('rcuParameters').innerHTML=rcuTable(RCU_PARAMETERS,m.readings);
  document.getElementById('rcuLamps').innerHTML=rcuTable(RCU_LAMPS,m.readings);
  document.getElementById('rcuVersion').innerHTML=rcuTable(RCU_VERSION,m.readings);
  document.getElementById('rcuFaults').innerHTML=m.dtc.map(function(g){
    return '<div class="battery-fault-group"><p><strong>故障组 '+g.page+'</strong> · '+escHtml(g.source.reason)+'</p>'+(g.source.ok?(g.active.length?'<ul class="rcu-dtc-list">'+g.active.map(function(n){return '<li>RCU 故障 '+n+'</li>';}).join('')+'</ul>':'<p>本组已定义故障位未置位</p>'):'')+'</div>';
  }).join('')+(rcuFrames['dtc/?']?'<p class="battery-note">收到未知故障分组 '+rcuFrames['dtc/?'].page+'，未按已知分组解码。</p>':'');
  var keys=Object.keys(RCU_IDS).filter(function(id){return id!=='0x18019894';}).concat(['dtc/1','dtc/2','dtc/3','dtc/4','dtc/5']);
  if(rcuFrames['dtc/?'])keys.push('dtc/?');
  document.getElementById('rcuSources').innerHTML=keys.map(function(key){
    var src=rcuSource(key,now),id=src.frame?src.frame.id:key.indexOf('dtc/')===0?'0x18019894':key;
    var name=RCU_IDS[id]+(key.indexOf('dtc/')===0?' / '+key.split('/')[1]:'');
    return '<tr><td>'+name+'<br>'+id+'</td><td>'+escHtml(src.reason)+(src.frame?'<br>'+fmtFrameTime(src.frame.t):'')+'</td><td>'+(src.frame?escHtml(src.frame.hex):'—')+'</td></tr>';
  }).join('');
}
function showRcu(){
  currentView='rcu';setTopTab('tabRcu');
  [contentMain,contentDetail,contentCharts,document.getElementById('viewBattery'),document.getElementById('backRow'),sendPanel].forEach(function(el){el.classList.add('hidden');});
  document.getElementById('viewRcu').classList.remove('hidden');
  document.getElementById('headerTitle').textContent='增程器';renderRcu();
}
</script>)rawliteral"
