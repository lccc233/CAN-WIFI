// Browser QA with a simulated HTTP device; no hardware connection or CAN TX.
// Optional PLAYWRIGHT_MODULE and BROWSER_EXECUTABLE select local installations.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { batteryPacket, batteryFrame } from './battery-fixtures.mjs';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const out = path.resolve('tmp/battery');
fs.mkdirSync(out,{recursive:true});
const html = ['web_head.h','web_body.h','web_battery.h','web_js.h'].map(name =>
  fs.readFileSync(new URL('../main/'+name,import.meta.url),'utf8').match(/R"rawliteral\(([\s\S]*?)\)rawliteral"/)[1]).join('');
fs.writeFileSync(path.join(out,'preview.html'),html);
const browser = await chromium.launch({headless:true, ...(process.env.BROWSER_EXECUTABLE ? {executablePath:process.env.BROWSER_EXECUTABLE} : {})});
try {
  for (const width of [1280,390]) {
    const page = await browser.newPage({viewport:{width,height:900},deviceScaleFactor:1});
    const errors = [], requests = [], sends = [];
    let mode = 'normal';
    page.on('pageerror',e=>errors.push(String(e)));
    await page.route('**/*',async route=>{
      const req=route.request(), url=new URL(req.url());
      requests.push(url.pathname);
      if(url.pathname==='/') return route.fulfill({contentType:'text/html',body:html});
      let body;
      if(url.pathname==='/api/messages') {
        body=batteryPacket();
        if(mode==='missing') body.battery=[];
        if(mode==='fault') {
          body.battery[0].data='C8 FA F8 2A 70 17 2A 13';
          body.battery[2].data='30 00 00 0C 00 00 00 00';
          body.battery[3].data='03 10 00 80 00 00 00 2C';
        }
        body.messages=[...body.battery,batteryFrame('0x1802F3EF','01 02 03 04 05 06 07 08')];
        body.total=mode==='missing'?1:5;
        if(mode==='stale') body.clk.boot=21000;
      } else if(url.pathname==='/api/signals') body=url.searchParams.has('meta')?{ok:true,configured:true,signals:[]}:[];
      else if(url.pathname==='/api/rec/status') body={ok:true,rec:{session:'',cnt:0,on:false,psram:true,cap:350000}};
      else if(url.pathname==='/api/send') { sends.push(JSON.parse(req.postData())); body={ok:true}; }
      else if(url.pathname==='/api/clear') body={ok:true};
      else if(url.pathname==='/api/time') body={ok:true};
      else if(url.pathname==='/api/live') body={boot:'battery-test',ready:true,from:0,next:0,head:0,lost:0,frames:[]};
      else throw Error('Unexpected HTTP request '+req.method()+' '+url.pathname);
      await route.fulfill({contentType:'application/json',body:JSON.stringify(body)});
    });
    await page.goto('http://battery.test/');
    await page.waitForFunction(()=>document.querySelectorAll('#mainBody .battery-row').length===4);
    assert.equal(await page.locator('#mainBody tr').count(),5);
    assert.equal(await page.locator('#mainBody .battery-row td').first().evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(204, 238, 255)');
    await page.screenshot({path:path.join(out,`monitor-${width}.png`),fullPage:true});
    await page.locator('#tabBattery').click();
    await page.waitForFunction(()=>document.getElementById('battery-soc').textContent.includes('80.0'));
    assert.equal(await page.locator('#sendPanel').isVisible(),false);
    assert.match(await page.locator('#battery-voltage').textContent(),/600\.0/);
    assert.match(await page.locator('#battery-current').textContent(),/100\.0/);
    assert.equal(await page.locator('#batteryStatus').textContent(),'当前无故障');
    assert.equal(await page.evaluate(()=>document.getElementById('viewBattery').scrollWidth>document.getElementById('viewBattery').clientWidth),false);
    await page.screenshot({path:path.join(out,`battery-${width}.png`),fullPage:true});
    mode='fault'; await page.evaluate(()=>pollMessages());
    await page.waitForFunction(()=>document.getElementById('batteryStatus').textContent==='检测到故障');
    assert.match(await page.locator('#batteryFaults').textContent(),/电芯温度过高/);
    assert.match(await page.locator('#batteryFaults').textContent(),/主负接触器无法闭合/);
    await page.screenshot({path:path.join(out,`faults-${width}.png`),fullPage:true});
    mode='stale'; await page.evaluate(()=>pollMessages());
    await page.waitForFunction(()=>document.getElementById('battery-soc').textContent.includes('—'));
    assert.match(await page.locator('#batteryMetrics').textContent(),/超时/);
    assert.notEqual(await page.locator('#batteryStatus').textContent(),'当前无故障');
    mode='missing'; await page.evaluate(()=>pollMessages());
    await page.waitForFunction(()=>document.getElementById('batteryMetrics').textContent.includes('未收到报文'));
    assert.equal(sends.length,0,'Battery page must never trigger a CAN send');
    await page.locator('#tabCharts').click(); assert(await page.locator('#viewCharts').isVisible());
    assert.equal(await page.locator('#viewBattery').isVisible(),false);
    await page.locator('#tabMonitor').click();
    await page.locator('#sendId').fill('0x123'); await page.locator('#sendDlc').fill('2');
    await page.locator('#sendData').fill('12 34'); await page.locator('#sendBtn').click();
    await page.waitForFunction(()=>true);
    assert.deepEqual(sends,[{id:'0x123',dlc:2,data:'12 34',extended:false}]);
    assert.deepEqual(errors,[]);
    console.log(`PASS browser ${width}px: tab isolation, readings, fault display, sky blue, no overflow, stale/missing, passive acquisition, manual send`);
    await page.close();
  }
} finally { await browser.close(); }
console.log('Browser QA screenshots: '+out);
