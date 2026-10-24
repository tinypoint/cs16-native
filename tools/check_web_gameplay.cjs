#!/usr/bin/env node
// Observe original HUD state while exercising the actual browser input route.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const root=path.resolve(__dirname,'..'),pw=require(process.env.CS16_PLAYWRIGHT||'playwright');
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
(async()=>{
 const options={headless:true,args:['--enable-unsafe-swiftshader']};
 if(process.env.CS16_BROWSER_EXECUTABLE)options.executablePath=process.env.CS16_BROWSER_EXECUTABLE;
 const browser=await pw.chromium.launch(options),context=await browser.newContext({viewport:{width:393,height:852},hasTouch:true,isMobile:true,deviceScaleFactor:2}),page=await context.newPage();
 const stage=path.join(root,'.local/web-publish'),manifest=JSON.parse(fs.readFileSync(path.join(stage,'manifest.json'))),preview=manifest.preview;
 const asset=p=>path.join(stage,p.replace(/^\//,''));
 const build={loaderSha256:sha(asset(preview.base+'cs16-web.js')),runtimeSha256:preview.runtime.sha256,resourceSha256:preview.resources.sha256,gameplaySha256:(preview.gameplay||[]).map(p=>p.sha256),frontendSha256:Object.fromEntries(['index.html','app.js','style.css'].map(n=>[n,sha(asset(n))]))};
 let cachePhase=false;const reloadDownloads=[];page.on('request',r=>{if(cachePhase&&/\.(?:wasm|cs16pack)\.gz(?:$|\?)/.test(r.url()))reloadDownloads.push(r.url())});
 const report={browser:await browser.version(),input:'touchscreen tap, touch view drag and virtual movement/fire controls',version:manifest.version,build,frame:false,error:null,checks:{},samples:{},screenshots:{},logs:[]};
 const stream=path.join(root,'logs/web-gameplay-stream.log');fs.writeFileSync(stream,'');
 let crashed=false;
 page.on('console',m=>{const line=m.text();report.logs.push(line);fs.appendFileSync(stream,line+'\n');if(/\[web-unloaded\]|FATAL:|Aborted\(/.test(line))report.error=line});
 page.on('pageerror',e=>{report.error=e.message});page.on('crash',()=>{crashed=true;report.error='Browser renderer crashed'});
 const state=()=>page.evaluate(()=>window.cs16RuntimeInfo);
 async function sample(name){if(report.error||crashed)throw Error(report.error);const value=await state();report.samples[name]=value;return value}
 async function shot(name){const target=path.join(root,'.local/web-gameplay-'+name+'.png');await page.screenshot({path:target,timeout:5000});report.screenshots[name]={path:path.relative(root,target),sha256:sha(target)}}
 let rect;
 const click=async(x,y)=>{if(report.error)throw Error(report.error);const px=rect.x+rect.width*x,py=rect.y+rect.height*y;const hit=await page.evaluate(({x,y})=>document.elementFromPoint(x,y)?.id,{x:px,y:py});if(hit!=='display')throw Error('Browser overlay intercepted a game tap: '+hit);await page.touchscreen.tap(px,py)};
 try{
  await page.goto(process.argv[2]||'http://127.0.0.1:8897/');
  await page.waitForFunction(()=>window.cs16FrameCount>30,null,{timeout:90000});report.frame=true;await shot('main-menu');
  // Turning touch controls on at the title screen must never disable menu taps.
  await page.locator('#touch-toggle').click();
  rect=await page.locator('#display').boundingBox();await click(.12,.66);await page.waitForTimeout(1200);await shot('new-game-menu');await click(.57,.94);await page.waitForTimeout(700);await shot('map-loading');
  await page.waitForFunction(()=>window.cs16RuntimeInfo?.connection===5&&window.cs16RuntimeInfo?.server===2,null,{timeout:60000});
  report.checks.map=true;await shot('welcome');await click(.22,.78);await page.waitForTimeout(1000);await click(.23,.265);await page.waitForTimeout(1200);await shot('class-menu');await click(.23,.265);
  await page.waitForFunction(()=>window.cs16RuntimeInfo?.weaponUpdates>0&&window.cs16RuntimeInfo?.clip>0&&window.cs16RuntimeInfo?.clip<255,null,{timeout:20000});
  await page.waitForTimeout(1500);const initial=await sample('spawn');report.checks.character=true;await shot('spawn');
  const pad=await page.locator('#move-pad').boundingBox();
  // A spawn can face a wall. Exercise both directions and observe the
  // original player's position only after the spawn transition has settled.
  let moved=initial;report.checks.movement=false;
  for(const [name,dy] of [['forward',12],['backward',pad.height-12]]){
   await page.mouse.move(pad.x+pad.width/2,pad.y+dy);await page.mouse.down();await page.waitForTimeout(1800);await page.mouse.up();await page.waitForTimeout(300);
   moved=await sample('moved-'+name);if(Math.hypot(...moved.origin.map((n,i)=>n-initial.origin[i]))>8){report.checks.movement=true;break}
  }
  report.samples.moved=moved;await shot('moved');
  const cdp=await context.newCDPSession(page),x=rect.x+rect.width*.65,y=rect.y+rect.height*.5;
  await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{x,y,id:1}]});
  for(let i=1;i<=8;i++){await cdp.send('Input.dispatchTouchEvent',{type:'touchMove',touchPoints:[{x:x+i*7,y,id:1}]});await page.waitForTimeout(40)}
  await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});await page.waitForTimeout(500);
  const turned=await sample('turned');report.checks.turning=Math.hypot(...turned.angles.map((n,i)=>n-moved.angles[i]))>2;await shot('turned');
  const fire=await page.locator('[data-mouse="1"]').boundingBox();await page.mouse.move(fire.x+fire.width/2,fire.y+fire.height/2);await page.mouse.down();await page.waitForTimeout(700);await page.mouse.up();await page.waitForTimeout(500);
  const fired=await sample('fired');report.checks.firing=fired.weaponId===turned.weaponId&&fired.clip<turned.clip&&fired.weaponUpdates>turned.weaponUpdates;await shot('fired');
  if(!Object.values(report.checks).every(Boolean))throw Error('An original-game state check did not pass');
  cachePhase=true;await page.reload();await page.waitForFunction(()=>window.cs16FrameCount>30,null,{timeout:90000});
  report.checks.cachedReload=reloadDownloads.length===0;report.reloadResourceRequests=reloadDownloads;
  if(!report.checks.cachedReload)throw Error('Reload downloaded unchanged program or game resource packs');
 }catch(e){report.error=report.error||e.message;await shot('failure').catch(()=>{});report.diagnostics=await page.evaluate(()=>window.cs16Diagnostics?.()).catch(()=>null)}
 fs.writeFileSync(path.join(root,'logs/web-browser-gameplay.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({checks:report.checks,samples:report.samples,error:report.error}));
 await Promise.race([browser.close(),new Promise(r=>setTimeout(r,2000))]);process.exit(report.error?1:0);
})().catch(e=>{console.error(e);process.exit(1)});
