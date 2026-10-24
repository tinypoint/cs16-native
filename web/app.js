'use strict';
// The uploaded original program executes and receives input in Wasm.
// Only pixels read from that instance can complete this preview.
(async()=>{
 const status=document.getElementById('status'),version=document.getElementById('version');
 const logs=[];let reachedFrame=false;
 const save=()=>{try{localStorage.setItem('cs16-preview-last',JSON.stringify({time:new Date().toISOString(),version:version.textContent,frame:reachedFrame,logs:logs.slice(-160)}))}catch{}};
 function output(value){const line=String(value);logs.push(line);if(logs.length>400)logs.shift();console.log(line);
  if(line.includes('[web-preview] original pixels presented')){reachedFrame=true;document.body.classList.add('has-frame');status.textContent='原程序正在运行';save()}
  else if(/FATAL:|\[guest-memory-web\]|\[web-unloaded\]|Aborted\(|uncaught exception/.test(line)){document.body.classList.add('failed');status.textContent='原程序停止：'+line;save()}
 }
 window.addEventListener('error',event=>{logs.push(event.message);document.body.classList.add('failed');status.textContent='运行出错：'+event.message;save()});
 const manifest=await(await fetch('manifest.json',{cache:'no-store'})).json();version.textContent='v'+manifest.version+' · Wasm 开发版';
 const preview=manifest.preview;if(!preview?.ready){status.textContent=manifest.message;return}
 if(!crossOriginIsolated||typeof SharedArrayBuffer==='undefined')throw new Error('此预览需要支持隔离线程的 HTTPS 浏览器');
 if(!('OffscreenCanvas' in window)||typeof HTMLCanvasElement.prototype.transferControlToOffscreen!=='function')throw new Error('此浏览器暂不支持所需的离屏绘图接口');
 if(typeof DecompressionStream==='undefined')throw new Error('此浏览器暂不支持资源解压');
 async function cached(spec,label){
  const url=new URL(spec.url,location.href);if(url.origin!==location.origin||!Number.isSafeInteger(spec.bytes)||spec.bytes<1)throw new Error('无效的资源清单');
  let cache;try{cache=await caches.open('cs16-content-v1')}catch{}
  let response=await cache?.match(url.href);const hit=!!response;
  if(!response){response=await fetch(url.href);if(!response.ok)throw new Error(label+' HTTP '+response.status)}
  const chunks=[];let length=0;const reader=response.body.getReader();
  while(true){const {done,value}=await reader.read();if(done)break;length+=value.byteLength;if(length>spec.bytes)throw new Error(label+' 大小不符');chunks.push(value);status.textContent=(hit?'读取缓存：':'下载：')+label+' '+(length/1048576).toFixed(1)+' / '+(spec.bytes/1048576).toFixed(1)+' MiB'}
  if(length!==spec.bytes)throw new Error(label+' 下载未完成');const data=new Uint8Array(length);let at=0;for(const chunk of chunks){data.set(chunk,at);at+=chunk.length}
  const digest=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',data)),n=>n.toString(16).padStart(2,'0')).join('');
  if(digest!==spec.sha256){await cache?.delete(url.href);throw new Error(label+' 校验失败，请刷新重试')}
  if(!hit&&cache)try{await cache.put(url.href,new Response(data,{headers:{'Content-Type':'application/octet-stream'}}))}catch{}
  return data;
 }
 async function decoded(spec,label){const compressed=await cached(spec,label);const raw=await new Response(new Blob([compressed]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();if(raw.byteLength!==spec.decodedBytes)throw new Error(label+' 解压大小不符');return new Uint8Array(raw)}
 let program=await decoded(preview.runtime,'原程序'),pack=await decoded(preview.resources,'启动资源');status.textContent='正在编译 Wasm…';
 await new Promise((resolve,reject)=>{const script=document.createElement('script');script.src=preview.base+'cs16-web.js';script.onload=resolve;script.onerror=()=>reject(new Error('程序加载器下载失败'));document.head.append(script)});
 let compiled=await WebAssembly.compile(program);program=null;
 let failInstantiation;const failedInstantiation=new Promise((resolve,reject)=>{failInstantiation=reject});
 const module=await Promise.race([createCS16Web({canvas:document.getElementById('canvas'),noInitialRun:true,instantiateWasm:(imports,receive)=>{WebAssembly.instantiate(compiled,imports).then(instance=>receive(instance,compiled)).catch(failInstantiation);return {}},locateFile:p=>preview.base+p,print:output,printErr:output,onAbort:error=>{document.body.classList.add('failed');status.textContent='原程序停止：'+String(error);save()}}),failedInstantiation]);compiled=null;
 function mountPack(pack){
  const magic=new TextDecoder().decode(pack.subarray(0,8)),length=new DataView(pack.buffer).getUint32(8,true);if(magic!=='CS16PK01'||length>4*1048576||12+length>pack.length)throw new Error('无效的启动资源包');
 const table=JSON.parse(new TextDecoder().decode(pack.subarray(12,12+length)));if(table.schema!==1||!Array.isArray(table.files)||table.files.length>10000)throw new Error('无效的资源索引');
 const start=12+length;module.FS.mkdirTree('/game');
 for(const entry of table.files){if(!entry.path||entry.path.startsWith('/')||entry.path.includes('\\')||entry.path.split('/').some(p=>!p||p==='.'||p==='..')||!Number.isSafeInteger(entry.offset)||!Number.isSafeInteger(entry.bytes)||entry.offset<0||entry.bytes<0||start+entry.offset+entry.bytes>pack.length)throw new Error('资源路径或范围不符');const path='/game/'+entry.path;module.FS.mkdirTree(path.slice(0,path.lastIndexOf('/')));module.FS.writeFile(path,pack.subarray(start+entry.offset,start+entry.offset+entry.bytes))}

 }
 mountPack(pack);pack=null;
 if(preview.gameplay)for(const part of preview.gameplay){const gameplay=await decoded(part,'地图资源');mountPack(gameplay)}
 pack=null;window.cs16Module=module;bindInput(module);status.textContent='原程序正在初始化…';save();module.callMain(['hl.exe']);
})().catch(error=>{document.getElementById('status').textContent=String(error.message||error);console.error(error)});

function bindInput(module){
 const canvas=document.getElementById('display');canvas.tabIndex=0;let touchMode=false;const touches=new Map();
 const send=(kind,a=0,b=0,c=0)=>module._cs16_web_input(kind,a,b,c);
 const point=e=>{const r=canvas.getBoundingClientRect();return [Math.max(0,Math.min(canvas.width-1,Math.floor((e.clientX-r.left)*canvas.width/r.width))),Math.max(0,Math.min(canvas.height-1,Math.floor((e.clientY-r.top)*canvas.height/r.height)))];};
 const mouse=(message,e)=>{if(document.pointerLockElement===canvas){if(message!==0x200)send(5,message);return}const [x,y]=point(e);send(1,message,x,y)};
 canvas.addEventListener('contextmenu',e=>e.preventDefault());
 canvas.addEventListener('pointerdown',e=>{e.preventDefault();canvas.focus({preventScroll:true});canvas.setPointerCapture(e.pointerId);if(touchMode&&e.pointerType==='touch'){touches.set(e.pointerId,[e.clientX,e.clientY]);return}mouse(0x200,e);mouse(e.button===2?0x204:0x201,e)});
 canvas.addEventListener('pointermove',e=>{e.preventDefault();if(touchMode&&e.pointerType==='touch'){const old=touches.get(e.pointerId);if(old){send(4,Math.round((e.clientX-old[0])*canvas.width/canvas.clientWidth),Math.round((e.clientY-old[1])*canvas.height/canvas.clientHeight));touches.set(e.pointerId,[e.clientX,e.clientY])}return}if(document.pointerLockElement===canvas)send(4,e.movementX,e.movementY);else mouse(0x200,e)});
 canvas.addEventListener('pointerup',e=>{e.preventDefault();if(touches.delete(e.pointerId)){if(canvas.hasPointerCapture(e.pointerId))canvas.releasePointerCapture(e.pointerId);return}mouse(e.button===2?0x205:0x202,e);if(canvas.hasPointerCapture(e.pointerId))canvas.releasePointerCapture(e.pointerId)});
 canvas.addEventListener('pointercancel',()=>reset());
 const key=e=>{if(e.code.startsWith('Key'))return e.code.charCodeAt(3);if(e.code.startsWith('Digit'))return e.code.charCodeAt(5);return {Escape:27,Enter:13,Space:32,Tab:9,Backspace:8,ArrowUp:38,ArrowDown:40,ArrowLeft:37,ArrowRight:39,ShiftLeft:16,ShiftRight:16,ControlLeft:17,ControlRight:17,AltLeft:18,AltRight:18}[e.code]||0};
 const scans={KeyW:17,KeyA:30,KeyS:31,KeyD:32,KeyE:18,KeyR:19,KeyF:33,KeyB:48,KeyQ:16,KeyG:34,KeyC:46,KeyZ:44,KeyX:45,Space:57,Escape:1,Enter:28,Tab:15,ControlLeft:29,ShiftLeft:42};
 for(const type of ['keydown','keyup'])window.addEventListener(type,e=>{const vk=key(e);if(vk){e.preventDefault();send(2,type==='keydown'?1:0,vk,scans[e.code]||0)}});

 const lock=document.getElementById('mouse-lock');lock.hidden=!matchMedia('(pointer:fine)').matches;lock.addEventListener('click',()=>{canvas.requestPointerLock?.()?.catch?.(()=>{})});document.addEventListener('pointerlockchange',()=>{send(3);lock.textContent=document.pointerLockElement===canvas?'Esc 释放':'鼠标'});
 const toggle=document.getElementById('touch-toggle'),controls=document.getElementById('touch-controls'),pad=document.getElementById('move-pad'),knob=document.getElementById('move-knob');toggle.hidden=false;
 let stick=null;const movement=new Set();
 const reset=()=>{touches.clear();stick=null;movement.clear();knob.style.transform='';send(3)};
 toggle.addEventListener('click',()=>{reset();touchMode=!touchMode;controls.hidden=!touchMode;toggle.setAttribute('aria-pressed',String(touchMode))});
 const scan={87:17,83:31,65:30,68:32};
 function move(e){const r=pad.getBoundingClientRect(),dx=e.clientX-r.left-r.width/2,dy=e.clientY-r.top-r.height/2,length=Math.hypot(dx,dy),scale=Math.min(1,42/(length||1)),next=new Set();knob.style.transform=`translate(${dx*scale}px,${dy*scale}px)`;
  if(length>12){if(dy/length<-.38)next.add(87);if(dy/length>.38)next.add(83);if(dx/length<-.38)next.add(65);if(dx/length>.38)next.add(68)}
  for(const vk of movement)if(!next.has(vk)){send(2,0,vk,scan[vk]);movement.delete(vk)}for(const vk of next)if(!movement.has(vk)){send(2,1,vk,scan[vk]);movement.add(vk)}
 }
 pad.addEventListener('pointerdown',e=>{e.preventDefault();if(stick!==null)return;stick=e.pointerId;pad.setPointerCapture(stick);move(e)});
 pad.addEventListener('pointermove',e=>{if(e.pointerId===stick){e.preventDefault();move(e)}});
 const stop=e=>{if(e.pointerId!==stick)return;for(const vk of movement)send(2,0,vk,scan[vk]);movement.clear();stick=null;knob.style.transform=''};
 for(const type of ['pointerup','pointercancel','lostpointercapture'])pad.addEventListener(type,stop);
 for(const button of controls.querySelectorAll('button')){
  let active=null;const press=down=>button.dataset.mouse?send(5,Number(button.dataset.mouse)===2?(down?0x204:0x205):(down?0x201:0x202)):send(2,down?1:0,Number(button.dataset.key),Number(button.dataset.scan));
  button.addEventListener('pointerdown',e=>{e.preventDefault();if(active!==null)return;active=e.pointerId;button.setPointerCapture(active);press(true)});
  for(const type of ['pointerup','pointercancel','lostpointercapture'])button.addEventListener(type,e=>{if(active!==e.pointerId)return;active=null;press(false)});
 }
 window.addEventListener('blur',reset);document.addEventListener('visibilitychange',()=>{if(document.hidden)reset()});
}
