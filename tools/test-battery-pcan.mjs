// Real isolated PCAN -> ESP32 -> HTTP -> browser integration test.
// Requires python-can in CAN_PYTHON (defaults to python) and a 250 kbps bench.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {createRequire} from 'node:module';
import {batteryPacket} from './battery-fixtures.mjs';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const base=process.argv[2]||'http://10.31.134.250';
const out=path.resolve('tmp/battery/hardware'); fs.mkdirSync(out,{recursive:true});
const reports=[];
function adapter(frames,duration=6){
  let readyResolve,readyReject,doneResolve,doneReject,result;
  const ready=new Promise((r,j)=>{readyResolve=r;readyReject=j;});
  const done=new Promise((r,j)=>{doneResolve=r;doneReject=j;});
  // A failed child may reject before the UI assertion has finished.
  done.catch(()=>{});
  const child=spawn(process.env.CAN_PYTHON||'python',[
    path.resolve('tools/battery-pcan-adapter.py'),'--duration',String(duration),
    '--channel',process.env.PCAN_CHANNEL||'PCAN_USBBUS1'],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  let stderr=''; child.stderr.on('data',b=>stderr+=b);
  createInterface({input:child.stdout}).on('line',line=>{
    const d=JSON.parse(line); if(d.ready)readyResolve(); else result=d;
  });
  child.on('error',e=>{readyReject(e);doneReject(e);});
  child.on('exit',code=>{
    if(code===0&&result)doneResolve(result);
    else {let e=new Error('PCAN adapter failed: '+code+' '+stderr);readyReject(e);doneReject(e);}
  });
  child.stdin.end(JSON.stringify(frames)+'\n');
  return {ready,done};
}
const browser=await chromium.launch({headless:true,args:['--no-proxy-server'],
  ...(process.env.BROWSER_EXECUTABLE?{executablePath:process.env.BROWSER_EXECUTABLE}:{})});
try{
  const page=await browser.newPage({viewport:{width:1280,height:900}});
  const errors=[],sendRequests=[];
  page.on('pageerror',e=>errors.push(String(e)));
  page.on('request',r=>{if(new URL(r.url()).pathname==='/api/send')sendRequests.push(r.postData());});
  await page.goto(base,{waitUntil:'domcontentloaded',timeout:30000});
  await page.waitForFunction(()=>document.getElementById('statusText').textContent==='Connected');
  const initialTotal=await page.evaluate(()=>lastTotal);
  const extra=[{id:'0x1801EFF3',data:'01 00 00 00 00 00 00 00'},
    {id:'0x123',ext:false,data:'12 34 56 78 00 00 00 00'}];
  async function phase(name,frames,expected,status,pattern){
    const tx=adapter([...frames,...extra]); await tx.ready;
    await page.locator('#tabBattery').click();
    await page.waitForFunction(({values,status})=>{
      const m=batteryModel(Date.now());
      return JSON.stringify(m.metrics.map(x=>x[4].value))===JSON.stringify(values)&&m.status===status;
    },{values:expected,status},{timeout:8000});
    if(pattern)assert.match(await page.locator('#batteryFaults').textContent(),pattern);
    await page.screenshot({path:path.join(out,`pcan-${name}.png`),fullPage:true});
    if(name==='normal'){
      await page.locator('#tabMonitor').click();
      await page.waitForFunction(()=>document.querySelectorAll('#mainBody .battery-row').length===5);
      assert.equal(await page.locator('#mainBody tr').count(),6);
      assert.equal(await page.locator('#mainBody .battery-row td').first().evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(204, 238, 255)');
      await page.screenshot({path:path.join(out,'pcan-monitor.png'),fullPage:true});
    }
    const result=await tx.done;
    assert.equal(result.adapter_status,0);
    assert.deepEqual(result.received,[],'ESP32 must not answer BMS/VIN requests or send acquisition frames');
    reports.push({name,expected,status,...result});
    console.log('PASS PCAN '+name+' '+JSON.stringify(result.sent));
  }
  const normal=batteryPacket().battery;
  await phase('normal',normal,[80,600,100,28,35,20],'当前无故障');
  const fault=structuredClone(normal);
  fault[0].data='64 FA 1C 25 A8 16 2A 13'; // 40%, -50 A, 580 V, fault code 42, level3
  fault[1].data='58 41 4B 01 02 03 04 00'; // max48,min25,avg35
  fault[2].data='30 00 00 0C 00 00 00 00';
  fault[3].data='03 10 00 80 00 00 00 2C';
  await page.setViewportSize({width:390,height:900});
  await phase('charge-fault',fault,[40,580,-50,35,48,25],'检测到故障',/主负接触器无法闭合/);
  const invalid=structuredClone(normal);
  invalid[0].data='FF FF FF FF FF FF FF 10'; invalid[1].data='FE FF FF 00 00 00 00 00';
  await phase('invalid',invalid,[null,null,null,null,null,null],'故障状态待确认');
  const zero=structuredClone(normal);
  zero[0].data='00 FA 10 27 00 00 00 10'; zero[1].data='28 28 28 00 00 00 00 00';
  await phase('zero-recovery',zero,[0,0,0,0,0,0],'当前无故障');
  assert.deepEqual(sendRequests,[],'No acquisition step may call /api/send');

  // Exercise the retained manual-send button; PCAN independently receives it.
  const listener=adapter([],4); await listener.ready;
  await page.locator('#tabMonitor').click();
  await page.locator('#sendId').fill('0x123'); await page.locator('#sendDlc').fill('4');
  await page.locator('#sendData').fill('CA FE 12 34'); await page.locator('#sendExt').uncheck();
  const sent=page.waitForResponse(r=>r.url()===base+'/api/send');
  await page.locator('#sendBtn').click(); assert.equal((await (await sent).json()).ok,true);
  const manual=await listener.done;
  assert.deepEqual(manual.received,[{id:'0x123',ext:false,rtr:false,data:'CA FE 12 34'}]);
  assert.equal(sendRequests.length,1); reports.push({name:'manual-send',...manual});
  console.log('PASS manual CAN send independently received by PCAN');

  await page.locator('#tabBattery').click();
  await page.waitForFunction(()=>batteryModel(Date.now()).metrics.every(x=>x[4].value===null&&x[4].reason.includes('超时'))
    &&document.getElementById('batteryStatus').textContent==='故障状态待确认',null,{timeout:14000});
  assert.equal(await page.locator('#batteryStatus').textContent(),'故障状态待确认');
  await page.screenshot({path:path.join(out,'pcan-timeout.png'),fullPage:true});
  const final=await page.evaluate(()=>({total:lastTotal,boot:monitorBoot}));
  const transmitted=reports.reduce((sum,r)=>sum+Object.values(r.sent).reduce((a,b)=>a+b,0),0);
  // TWAI self reception is disabled: the manual TX is checked by PCAN and
  // does not count as a received frame on the transmitting ESP32.
  assert.equal(final.total-initialTotal,transmitted,'Every PCAN broadcast must arrive at ESP32');
  assert.deepEqual(errors,[]);
  fs.writeFileSync(path.join(out,'pcan-report.json'),JSON.stringify({base,initialTotal,final,
    transmitted,manual_frames:1,received_by_esp:final.total-initialTotal,phases:reports,javascript_errors:errors},null,2));
  console.log('PASS timeout and exact receive count: '+(final.total-initialTotal)+' frames');
}finally{await browser.close();}
