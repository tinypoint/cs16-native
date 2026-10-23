'use strict';
// Startup-only experiment: the uploaded original program executes in Wasm.
// Only pixels read from that instance can complete this preview.
(async()=>{
 const status=document.getElementById('status'),version=document.getElementById('version');
 const logs=[];let reachedFrame=false;
 const save=()=>{try{localStorage.setItem('cs16-preview-last',JSON.stringify({time:new Date().toISOString(),version:version.textContent,frame:reachedFrame,logs:logs.slice(-160)}))}catch{}};
 function output(value){const line=String(value);logs.push(line);if(logs.length>400)logs.shift();console.log(line);
  if(line.includes('[web-preview] original pixels presented')){reachedFrame=true;document.body.classList.add('has-frame');status.textContent='原程序 Wasm 首帧 · 暂不可操作';save()}
  else if(/FATAL:|\[guest-memory-web\]|\[web-unloaded\]|Aborted\(|uncaught exception/.test(line)){if(!reachedFrame)status.textContent='原程序初始化停止：'+line;save()}
 }
 window.addEventListener('error',event=>{logs.push(event.message);if(!reachedFrame)status.textContent='启动出错：'+event.message;save()});
 const manifest=await(await fetch('manifest.json',{cache:'no-store'})).json();version.textContent='v'+manifest.version+' · Wasm 预览 · 暂不可操作';
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
 const module=await createCS16Web({canvas:document.getElementById('canvas'),noInitialRun:true,wasmBinary:program,locateFile:p=>preview.base+p,print:output,printErr:output,onAbort:error=>{if(!reachedFrame)status.textContent='原程序停止：'+String(error);save()}});program=null;
 const magic=new TextDecoder().decode(pack.subarray(0,8)),length=new DataView(pack.buffer).getUint32(8,true);if(magic!=='CS16PK01'||length>4*1048576||12+length>pack.length)throw new Error('无效的启动资源包');
 const table=JSON.parse(new TextDecoder().decode(pack.subarray(12,12+length)));if(table.schema!==1||!Array.isArray(table.files)||table.files.length>10000)throw new Error('无效的资源索引');
 const start=12+length;module.FS.mkdirTree('/game');
 for(const entry of table.files){if(!entry.path||entry.path.startsWith('/')||entry.path.includes('\\')||entry.path.split('/').some(p=>!p||p==='.'||p==='..')||!Number.isSafeInteger(entry.offset)||!Number.isSafeInteger(entry.bytes)||entry.offset<0||entry.bytes<0||start+entry.offset+entry.bytes>pack.length)throw new Error('资源路径或范围不符');const path='/game/'+entry.path;module.FS.mkdirTree(path.slice(0,path.lastIndexOf('/')));module.FS.writeFile(path,pack.subarray(start+entry.offset,start+entry.offset+entry.bytes))}
 pack=null;status.textContent='原程序正在初始化…';save();module.callMain(['hl.exe']);
})().catch(error=>{document.getElementById('status').textContent=String(error.message||error);console.error(error)});
