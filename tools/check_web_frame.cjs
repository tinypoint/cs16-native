#!/usr/bin/env node
// Browser evidence from the published loader. No reference image is supplied.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const root=path.resolve(__dirname,'..');
const playwright=require(process.env.CS16_PLAYWRIGHT||'playwright');
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
(async()=>{
 const engine=process.env.CS16_BROWSER||'chromium';
 const options={headless:true,timeout:30000};
 if(process.env.CS16_BROWSER_EXECUTABLE)options.executablePath=process.env.CS16_BROWSER_EXECUTABLE;
 if(engine==='chromium')options.args=['--enable-unsafe-swiftshader'];
 const manifest=JSON.parse(fs.readFileSync(path.join(root,'.local/web-publish/manifest.json')));
 const preview=manifest.preview;
 const asset=name=>path.join(root,'.local/web-publish',name.replace(/^\//,''));
 const build={loaderSha256:sha(asset(preview.base+'cs16-web.js')),runtimeSha256:preview.runtime.sha256,resourceSha256:preview.resources.sha256};
 console.log('Launching',engine);
 const browser=await playwright[engine].launch(options);console.log('Browser launched');
 const page=await browser.newPage({viewport:{width:844,height:390}});
 const logs=[],requests=[];let frame=false,error=null,finish;
 let completed=new Promise(r=>finish=r);
 page.on('console',message=>{const text=message.text();logs.push(text);
  if(text.includes('[web-preview] original pixels presented from this Wasm instance')){frame=true;finish()}
  else if(/\[web-unloaded\]|FATAL:|Aborted\(/.test(text)){error=text;finish()}
 });
 page.on('pageerror',e=>{error=e.message;logs.push(e.stack);finish()});
 page.on('request',r=>requests.push(r.url()));
 let timer;
 try{
  await page.goto(process.argv[2]||'http://127.0.0.1:8897/',{timeout:30000});
  await Promise.race([completed,new Promise(r=>timer=setTimeout(()=>{error='First frame timeout';r()},90000))]);
  clearTimeout(timer);
  if(frame)await page.screenshot({path:path.join(root,'.local/web-first-production.png'),timeout:10000});
  let reload=null;
  if(frame&&process.env.CS16_CHECK_RELOAD==='1'){
   const first=requests.length;frame=false;error=null;completed=new Promise(r=>finish=r);
   await page.reload({timeout:30000});
   await Promise.race([completed,new Promise(r=>timer=setTimeout(()=>{error='Reload first frame timeout';r()},90000))]);clearTimeout(timer);
   const largeResourceRequests=requests.slice(first).filter(url=>/\.wasm\.gz|startup\.cs16pack\.gz/.test(url));
   reload={frame,largeResourceRequests};if(largeResourceRequests.length)error='Large assets downloaded again';
  }
  const report={frame,error,engine,build,version:manifest.version,reload,logs,requests};
  fs.writeFileSync(path.join(root,'logs',process.env.CS16_EVIDENCE||'web-browser-first.json'),JSON.stringify(report,null,2)+'\n');
  console.log(JSON.stringify({frame,error,engine,build}));
 }finally{
  clearTimeout(timer);
  await Promise.race([browser.close(),new Promise(r=>setTimeout(r,2000))]);
 }
 process.exit(frame&&!error?0:1);
})().catch(e=>{console.error(e.message);process.exit(1)});
