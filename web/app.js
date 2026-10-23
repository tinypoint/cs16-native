'use strict';
// Hosting is ready before the original game runtime. Do not simulate a launch
// or download the game resources until a verified runtime is published.
(async()=>{
 const status=document.getElementById('status'),version=document.getElementById('version');
 try{
  const response=await fetch('manifest.json',{cache:'no-store'});
  if(!response.ok)throw new Error('HTTP '+response.status);
  const manifest=await response.json();
  if(manifest.schema!==1||typeof manifest.version!=='string')throw new Error('Invalid manifest');
  version.textContent='v'+manifest.version;
  if(manifest.ready!==false||manifest.runtime!==null)throw new Error('Runtime loader is not yet integrated');
  status.textContent=manifest.message;
 }catch(error){
  status.textContent='无法确认程序状态，请稍后刷新。';
  console.error('CS 1.6 entry:',error);
 }
})();
