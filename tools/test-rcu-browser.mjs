// Simulated HTTP only: exercises real HTML/JS without CAN traffic.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {rcuPacket,rcuFrame,rcuPack} from './rcu-fixtures.mjs';
import {batteryPacket} from './battery-fixtures.mjs';
const require=createRequire(import.meta.url);
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const out=path.resolve('tmp/rcu');fs.mkdirSync(out,{recursive:true});
const html=['web_head.h','web_body.h','web_battery.h','web_rcu.h','web_js.h'].map(name=>
  fs.readFileSync(new URL('../main/'+name,import.meta.url),'utf8').match(/R"rawliteral\(([\s\S]*?)\)rawliteral"/)[1]).join('');
fs.writeFileSync(path.join(out,'preview.html'),html);
const browser=await chromium.launch({headless:true,...(process.env.BROWSER_EXECUTABLE?{executablePath:process.env.BROWSER_EXECUTABLE}:{})});
try{
  for(const width of [1280,390,320]){
    const page=await browser.newPage({viewport:{width,height:960}});
    const errors=[],sends=[];let mode='normal';
    page.on('pageerror',e=>errors.push(String(e)));
    await page.route('**/*',async route=>{
      const url=new URL(route.request().url());let body;
      if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
      if(url.pathname==='/api/messages'){
        body=rcuPacket();body.battery=batteryPacket().battery;
        if(mode==='fault'){
          body.rcu[2].data='7B DC 05 8E 12 5A 6E AF';
          body.rcu[3].data='24 13 94 07 6C 07 27 01';
          body.rcu[9].data=rcuPack([[0,8,2],[8,1,1],[45,1,1]]);
          body.rcu[12].data=rcuPack([[0,8,5],[9,1,1]]);
        }
        if(mode==='missing')body.rcu=[];
        if(mode==='stale')body.clk.boot=26000;
        body.messages=[...body.rcu,...body.battery,rcuFrame('0x0C819C94','00 00 00 00 00 00 00 00'),rcuFrame('0x1891949D','00 00 00 00 00 00 00 00')];
        body.total=body.messages.length;
      }else if(url.pathname==='/api/signals')body=url.searchParams.has('meta')?{ok:true,configured:true,signals:[]}:[];
      else if(url.pathname==='/api/rec/status')body={ok:true,rec:{session:'',cnt:0,on:false,psram:true,cap:349525}};
      else if(url.pathname==='/api/time')body={ok:true};
      else if(url.pathname==='/api/live')body={boot:'rcu-test',ready:true,from:0,next:0,head:0,lost:0,frames:[]};
      else if(url.pathname==='/api/send'){sends.push(route.request().postData());body={ok:true};}
      else throw Error('Unexpected request '+url.pathname);
      return route.fulfill({contentType:'application/json',body:JSON.stringify(body)});
    });
    await page.goto('http://rcu.test/');
    await page.waitForFunction(()=>document.querySelectorAll('#mainBody .rcu-row').length===9);
    assert.equal(await page.locator('#mainBody .battery-row').count(),4);
    assert.equal(await page.locator('#mainBody .rcu-row td').first().evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(255, 225, 225)');
    assert.equal(await page.locator('#mainBody .battery-row td').first().evaluate(el=>getComputedStyle(el).backgroundColor),'rgb(204, 238, 255)');
    await page.screenshot({path:path.join(out,`monitor-${width}.png`),fullPage:true});
    await page.locator('#tabRcu').click();
    await page.waitForFunction(()=>document.getElementById('rcuStatus').textContent==='正常充电');
    assert.match(await page.locator('#rcu-nEngActSpdTxi').textContent(),/1600/);
    assert.match(await page.locator('#rcu-iREXOutCur').textContent(),/-100/);
    assert.match(await page.locator('#rcu-uREXOutVlt').textContent(),/600/);
    assert.equal(await page.locator('#sendPanel').isVisible(),false);
    assert.equal(await page.evaluate(()=>{const el=document.getElementById('viewRcu');return el.scrollWidth>el.clientWidth;}),false);
    assert(await page.locator('#tabRcu').isVisible());
    await page.screenshot({path:path.join(out,`rcu-${width}.png`),fullPage:true});
    mode='fault';await page.evaluate(()=>pollMessages());
    await page.waitForFunction(()=>document.getElementById('rcuFaults').textContent.includes('RCU 故障 137'));
    assert.match(await page.locator('#rcuFaults').textContent(),/RCU 故障 46/);
    assert.match(await page.locator('#rcuState').textContent(),/请求高压急停/);
    await page.screenshot({path:path.join(out,`fault-${width}.png`),fullPage:true});
    mode='stale';await page.evaluate(()=>pollMessages());
    await page.waitForFunction(()=>document.getElementById('rcu-nEngActSpdTxi').textContent.includes('—'));
    assert.match(await page.locator('#rcuMetrics').textContent(),/超时/);
    mode='missing';await page.evaluate(()=>pollMessages());
    await page.waitForFunction(()=>document.getElementById('rcuStatus').textContent==='未收到报文');
    await page.locator('#tabBattery').click();assert(await page.locator('#viewBattery').isVisible());
    assert.equal(await page.locator('#viewRcu').isVisible(),false);
    await page.locator('#tabCharts').click();assert(await page.locator('#viewCharts').isVisible());
    await page.locator('#tabMonitor').click();assert(await page.locator('#sendPanel').isVisible());
    assert(await page.locator('#sendBtn').isEnabled());
    assert.deepEqual(sends,[]);assert.deepEqual(errors,[]);
    console.log(`PASS browser ${width}px: RCU data, sparse multiplex faults, pale red/sky blue, missing/stale, passive navigation and no horizontal overflow`);
    await page.close();
  }
}finally{await browser.close();}
