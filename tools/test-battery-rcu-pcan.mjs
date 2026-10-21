// Isolated 250 kbps PCAN/ESP32 bench only. Sends synthetic BMS+RCU broadcasts
// concurrently, then explicitly exercises the retained manual Send button.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {createRequire} from 'node:module';
import {batteryPacket} from './battery-fixtures.mjs';
import {rcuPacket,rcuPack} from './rcu-fixtures.mjs';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const base=process.argv[2]||'http://10.31.134.250';
const out=path.resolve('tmp/rcu/hardware');fs.mkdirSync(out,{recursive:true});
const report={base,phases:[],javascript_errors:[]};
function save(){fs.writeFileSync(path.join(out,'joint-pcan-report.json'),JSON.stringify(report,null,2));}
function adapter(frames,duration){
  let a,b,c,d,result,stderr='';const ready=new Promise((r,j)=>{a=r;b=j;}),done=new Promise((r,j)=>{c=r;d=j;});done.catch(()=>{});
  const child=spawn(process.env.CAN_PYTHON||'python',[path.resolve('tools/battery-pcan-adapter.py'),'--duration',String(duration),'--channel',process.env.PCAN_CHANNEL||'PCAN_USBBUS1'],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  child.stderr.on('data',x=>stderr+=x);
  createInterface({input:child.stdout}).on('line',line=>{const value=JSON.parse(line);if(value.ready)a();else result=value;});
  child.on('error',e=>{b(e);d(e);});
  child.on('exit',code=>{if(code===0&&result)c(result);else{const e=Error('PCAN adapter failed: '+code+' '+stderr);b(e);d(e);}});
  child.stdin.end(JSON.stringify(frames)+'\n');return{ready,done};
}
function frames(battery,rcu){
  return [...battery.map(f=>({...f,period_ms:f.id==='0x1884EFF3'?200:500})),
    ...rcu.map(f=>({...f,period_ms:['0x1891949C','0x1892949C'].includes(f.id)?20:f.id==='0x1894949C'?5000:1000})),
    {id:'0x1801EFF3',data:'01 00 00 00 00 00 00 00',period_ms:500},
    {id:'0x123',ext:false,data:'12 34 56 78 00 00 00 00',period_ms:500}];
}
const browser=await chromium.launch({headless:true,args:['--no-proxy-server'],...(process.env.BROWSER_EXECUTABLE?{executablePath:process.env.BROWSER_EXECUTABLE}:{})});
try{
  const context=await browser.newContext(),batteryPage=await context.newPage(),rcuPage=await context.newPage();
  await batteryPage.setViewportSize({width:1280,height:960});await rcuPage.setViewportSize({width:390,height:960});
  let manualAllowed=false;const sendRequests=[];
  for(const page of [batteryPage,rcuPage]){
    page.on('pageerror',e=>report.javascript_errors.push(String(e)));
    // Catch regressions before they can put an acquisition frame on the bus.
    await page.route('**/api/send',route=>{sendRequests.push(route.request().postData());return manualAllowed?route.continue():route.abort();});
    await page.goto(base,{waitUntil:'domcontentloaded',timeout:30000});
    await page.waitForFunction(()=>document.getElementById('statusText').textContent==='Connected');
  }
  await batteryPage.locator('#tabBattery').click();await rcuPage.locator('#tabRcu').click();
  report.initial=await batteryPage.evaluate(()=>({total:lastTotal,boot:monitorBoot}));
  async function phase(name,bat,rcu,expectedBattery,expectedRcu,dtc,seconds=8){
    const tx=adapter(frames(bat,rcu),seconds);await tx.ready;let phaseReport;
    try{
      await batteryPage.waitForFunction(values=>JSON.stringify(batteryModel(Date.now()).metrics.map(x=>x[4].value))===JSON.stringify(values),expectedBattery,{timeout:9000});
      await rcuPage.waitForFunction(({expected,dtc})=>{
        const m=rcuModel(Date.now());
        return Object.entries(expected).every(([k,v])=>m.readings[k].value===v)
          &&JSON.stringify(m.dtc.map(g=>g.active))===JSON.stringify(dtc);
      },{expected:expectedRcu,dtc},{timeout:9000});
      const battery=await batteryPage.evaluate(()=>{const m=batteryModel(Date.now());return{values:m.metrics.map(x=>x[4].value),status:m.status,faults1:m.faults1.map(f=>f.bit),faults2:m.faults2.map(f=>f.bit)};});
      const rex=await rcuPage.evaluate(()=>{const m=rcuModel(Date.now());return{status:m.status,values:Object.fromEntries(Object.entries(m.readings).map(([k,v])=>[k,v.value])),groups:m.dtc.map(g=>({page:g.page,active:g.active,ready:g.source.ok})),cache_slots:Object.keys(rcuFrames).length};});
      assert(rex.groups.every(g=>g.ready),'All five DTC pages must remain independently available');
      if(name==='normal'){
        assert.equal(battery.status,'当前无故障');assert.equal(rex.status,'正常充电');assert.equal(rex.cache_slots,13);
      }
      if(name==='fault'){
        assert.equal(battery.status,'检测到故障');assert.deepEqual(battery.faults1,[4,26]);
        assert.deepEqual(battery.faults2,[11,31,57,59,60]);assert.match(rex.status,/存在报警/);
        assert.equal(rex.values.stREXErrDeg,3);assert.equal(rex.values.stREXSysErrLvl,3);assert.equal(rex.values.bEmgyHVOffReq,1);
      }
      if(name==='partial-clear'){assert.equal(battery.status,'当前无故障');assert.match(rex.status,/存在报警/);}
      if(name==='invalid-short'){
        assert.equal(battery.status,'故障状态待确认');
        assert.match(await batteryPage.locator('#batteryMetrics').textContent(),/传感器异常/);
        assert.match(await rcuPage.locator('#rcuMetrics').textContent(),/报文长度或数据无效/);
      }
      await batteryPage.screenshot({path:path.join(out,`joint-battery-${name}.png`),fullPage:true});
      await rcuPage.screenshot({path:path.join(out,`joint-rcu-${name}.png`),fullPage:true});
      if(name==='normal'){
        await batteryPage.locator('#tabMonitor').click();
        await batteryPage.waitForFunction(()=>document.querySelectorAll('#mainBody .rcu-row').length===9&&document.querySelectorAll('#mainBody .battery-row').length===5);
        assert.equal(await batteryPage.locator('#mainBody tr').count(),15);
        assert.equal(await batteryPage.locator('#mainBody .rcu-row td').first().evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(255, 225, 225)');
        assert.equal(await batteryPage.locator('#mainBody .battery-row td').first().evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(204, 238, 255)');
        await batteryPage.screenshot({path:path.join(out,'joint-monitor.png'),fullPage:true});
        await batteryPage.locator('#tabBattery').click();
      }
      phaseReport={name,battery,rcu:rex};
    }finally{
      const result=await tx.done;
      report.phases.push({...phaseReport,name,...result});save();
      assert.equal(result.adapter_status,0);assert.deepEqual(result.received,[],'No unsolicited CAN data or VIN answer is permitted');
    }
    console.log('PASS simultaneous '+name+': '+Object.values(report.phases.at(-1).sent).reduce((a,b)=>a+b,0)+' frames');
  }
  const bat=batteryPacket().battery,rcu=rcuPacket().rcu,empty=[[],[],[],[],[]];
  await phase('normal',bat,rcu,[80,600,100,28,35,20],{stREX:7,nEngActSpdTxi:1600,nFISGActSpdTx:1500,trqFISGActTx:-250,iREXOutCur:-100,pwrREXOut:-60,uREXOutVlt:600,wRexGenrTot:123456,numProdCod:0x1234567},empty);
  const bf=structuredClone(bat),rf=structuredClone(rcu);
  bf[0].data='64 FA 1C 25 A8 16 2A 13';bf[1].data='58 41 4B 01 02 03 04 00';
  bf[2].data='30 00 00 0C 00 00 00 00';bf[3].data='03 10 00 80 00 00 00 2C';
  rf[2].data='7B DC 05 8E 12 5A 6E AF';rf[3].data='24 13 94 07 6C 07 27 01';
  const faultBits=[[8,63],[8,45],[9,62],[8,60],[8,9]],faultNumbers=[[1,45],[46,78],[79,99],[100,135],[136,137]];
  for(let p=1;p<=5;p++)rf[7+p].data=rcuPack([[0,8,p],...faultBits[p-1].map(bit=>[bit,1,1])]);
  await phase('fault',bf,rf,[40,580,-50,35,48,25],{stREX:7,stREXErrDeg:3,stREXSysErrLvl:3,bEmgyHVOffReq:1},faultNumbers);
  // Only group1 is refreshed with zeros; groups2..5 retain their actual reports.
  await phase('partial-clear',bat,rcu.filter(f=>f.id!=='0x18019894'||f.data.startsWith('01 ')),[80,600,100,28,35,20],{stREXErrDeg:0,stREXSysErrLvl:0},[[],...faultNumbers.slice(1)],4);
  const bi=structuredClone(bat),ri=structuredClone(rcu);
  bi[0].data='FF FF FF FF FF FF FF 10';bi[1].data='FE FF FF 00 00 00 00 00';
  ri[0].data='FF FF FF FF FF FF FF FF';ri[2].data='70';ri[2].dlc=1;
  ri[3].data='FF FF FF FF FF FF 20 00';ri[4].data='00 00 00 FF FF 00 00 00';
  await phase('invalid-short',bi,ri,[null,null,null,null,null,null],{nEngActSpdTxi:null,nFISGActSpdTx:null,iREXOutCur:null,pwrREXOut:null,uREXOutVlt:null},empty,6);
  const bz=structuredClone(bat),rz=structuredClone(rcu);
  bz[0].data='00 FA 10 27 00 00 00 10';bz[1].data='28 28 28 00 00 00 00 00';
  rz[0].data='00 00 28 28 00 00 00 00';rz[2].data='20 00 00 88 13 28 28 AF';rz[3].data='88 13 D0 07 D0 07 20 00';
  rz[4].data='FF FF FF 00 00 00 00 00';rz[5].data=rcuPack([[0,10,1023],[10,8,255],[18,30,0x3fffffff],[48,7,127],[55,4,12],[59,5,31]]);
  rz[6].data='FF FF FF FF FF FF 00 00';rz[7].data='FF 00 00 00 00 00 00 00';
  await phase('zero-boundaries',bz,rz,[0,0,0,0,0,0],{stREX:2,nEngActSpdTxi:0,nFISGActSpdTx:0,iREXOutCur:0,pwrREXOut:0,uREXOutVlt:0,wRexGenrTot:16777215,pwrREReq:2095,numProdCod:0x3fffffff,numSendVers:255,numRcuFaultCod:255},empty);
  assert.deepEqual(sendRequests,[]);
  const listener=adapter([],4);await listener.ready;manualAllowed=true;
  await batteryPage.locator('#tabMonitor').click();await batteryPage.locator('#sendId').fill('0x123');await batteryPage.locator('#sendDlc').fill('4');await batteryPage.locator('#sendData').fill('CA FE 12 34');await batteryPage.locator('#sendExt').uncheck();
  const sent=batteryPage.waitForResponse(r=>r.url()===base+'/api/send');await batteryPage.locator('#sendBtn').click();assert.equal((await(await sent).json()).ok,true);
  const manual=await listener.done;manualAllowed=false;
  assert.deepEqual(manual.received,[{id:'0x123',ext:false,rtr:false,data:'CA FE 12 34'}]);assert.equal(sendRequests.length,1);
  report.manual=manual;console.log('PASS retained manual send received independently by PCAN');
  await batteryPage.locator('#tabBattery').click();
  await batteryPage.waitForFunction(()=>batteryModel(Date.now()).metrics.every(x=>x[4].value===null&&x[4].reason.includes('超时'))&&document.getElementById('batteryStatus').textContent==='故障状态待确认',null,{timeout:16000});
  await rcuPage.waitForFunction(()=>Object.values(rcuModel(Date.now()).readings).every(r=>r.value===null&&r.reason.includes('超时'))&&document.getElementById('rcuStatus').textContent.includes('超时'),null,{timeout:18000});
  report.timeout_passed=true;
  for(const [name,page] of [['battery',batteryPage],['rcu',rcuPage]])await page.screenshot({path:path.join(out,`joint-${name}-timeout.png`),fullPage:true});
  report.final=await batteryPage.evaluate(()=>({total:lastTotal,boot:monitorBoot}));
  report.transmitted=report.phases.reduce((sum,p)=>sum+Object.values(p.sent).reduce((a,b)=>a+b,0),0);
  report.received_by_esp=report.final.total-report.initial.total;
  assert.equal(report.final.boot,report.initial.boot,'Device must not reboot under concurrent traffic');
  assert.equal(report.received_by_esp,report.transmitted,'All broadcast frames must reach ESP32; manual TX is not self-received');
  assert.deepEqual(report.javascript_errors,[]);report.passed=true;save();
  console.log('PASS joint timeout, stable boot and exact reception: '+report.received_by_esp+'/'+report.transmitted);
}catch(e){report.error=String(e);save();throw e;}finally{await browser.close();}
