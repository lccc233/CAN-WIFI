// Read-only device/browser smoke test. Does not send CAN frames, clear data,
// change signal definitions, or start/stop recordings.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const base=process.argv[2]||'http://10.31.134.250';
const out=path.resolve('tmp/battery/hardware'); fs.mkdirSync(out,{recursive:true});
const browser=await chromium.launch({headless:true,args:['--no-proxy-server'],
  ...(process.env.BROWSER_EXECUTABLE?{executablePath:process.env.BROWSER_EXECUTABLE}:{})});
try {
  const report={base,views:[],can_send_requests:0,hardware_can_transmission:false};
  for(const width of [1280,390]) {
    const page=await browser.newPage({viewport:{width,height:900}});
    const errors=[],sends=[];
    page.on('pageerror',e=>errors.push(String(e)));
    await page.route('**/api/send',route=>{sends.push(route.request().url()); return route.abort();});
    await page.goto(base,{waitUntil:'domcontentloaded',timeout:30000});
    await page.waitForFunction(()=>document.getElementById('statusText').textContent==='Connected',null,{timeout:20000});
    await page.locator('#tabBattery').click();
    assert(await page.locator('#viewBattery').isVisible());
    assert.equal(await page.locator('#sendPanel').isVisible(),false);
    const state=await page.evaluate(()=>({total:lastTotal,boot:monitorBoot,battery_ids:Object.keys(batteryFrames),
      configured_signals:configs.length,metrics:Array.from(batteryModel(Date.now()).metrics,m=>({name:m[1],value:m[4].value,reason:m[4].reason})),
      status:document.getElementById('batteryStatus').textContent}));
    if(state.battery_ids.length===0) {
      assert.equal(state.status,'故障状态待确认');
      assert(state.metrics.every(m=>m.value===null&&m.reason==='未收到报文'));
    }
    assert.equal(await page.evaluate(()=>{let el=document.getElementById('viewBattery');return el.scrollWidth>el.clientWidth;}),false);
    await page.screenshot({path:path.join(out,`device-battery-${width}.png`),fullPage:true});
    await page.locator('#tabCharts').click(); assert(await page.locator('#viewCharts').isVisible());
    await page.locator('#tabMonitor').click(); assert(await page.locator('#sendPanel').isVisible());
    assert(await page.locator('#sendBtn').isEnabled());
    assert.equal(await page.locator('#viewBattery').isVisible(),false);
    assert.deepEqual(sends,[]); assert.deepEqual(errors,[]);
    report.views.push({width,...state,javascript_errors:errors});
    console.log('PASS real device '+width+'px: '+JSON.stringify(state));
    await page.close();
  }
  fs.writeFileSync(path.join(out,'device-browser-report.json'),JSON.stringify(report,null,2));
}finally{await browser.close();}
