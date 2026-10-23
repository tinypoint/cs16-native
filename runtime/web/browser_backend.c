/* Real browser DOM for the partial COM backend. No screenshot substitutes.
 * Only about:blank is supported; HTML-to-canvas snapshots are not. */
#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../platform/browser_native.h"
void *cs16_browser_native_create(void){int id=MAIN_THREAD_EM_ASM_INT({return (function(){
 const list=Module.cs16Documents ||= new Map();const id=(Module.cs16DocumentId||0)+1;Module.cs16DocumentId=id;
 const frame=document.createElement('iframe');frame.sandbox='allow-same-origin';frame.style.cssText='position:fixed;left:-20000px;top:-20000px;width:320px;height:240px;border:0;pointer-events:none';
 const state={frame,events:0,busy:false};frame.onload=()=>{state.busy=false;state.events|=1};document.body.appendChild(frame);list.set(id,state);return id;
 })();});return (void *)(uintptr_t)id;}
void cs16_browser_native_destroy(void *p){MAIN_THREAD_EM_ASM({return (function(){const s=Module.cs16Documents?.get($0);if(s){s.frame.remove();Module.cs16Documents.delete($0)}})();},(int)(uintptr_t)p);}
int cs16_browser_native_resize(void *p,int w,int h){if(w<1||h<1||w>4096||h>4096)return 0;return MAIN_THREAD_EM_ASM_INT({return (function(){const s=Module.cs16Documents?.get($0);if(!s)return 0;s.frame.style.width=$1+'px';s.frame.style.height=$2+'px';return 1;})();},(int)(uintptr_t)p,w,h);}
int cs16_browser_native_navigate(void *p,const char *url){
 if(strcmp(url,"about:blank"))return 0;
 return MAIN_THREAD_EM_ASM_INT({return (function(){const s=Module.cs16Documents?.get($0);if(!s)return 0;s.events=0;s.busy=true;s.frame.srcdoc='<!doctype html><html><head></head><body></body></html>';return 1;})();},(int)(uintptr_t)p);
}
int cs16_browser_native_action(void *p,int action){if(action!=0)return 0;return MAIN_THREAD_EM_ASM_INT({return (function(){return Module.cs16Documents?.has($0)?1:0;})();},(int)(uintptr_t)p);}
int cs16_browser_native_busy(void *p){return MAIN_THREAD_EM_ASM_INT({return (function(){return Module.cs16Documents?.get($0)?.busy?1:0;})();},(int)(uintptr_t)p);}
unsigned cs16_browser_native_events(void *p){return MAIN_THREAD_EM_ASM_INT({return (function(){const s=Module.cs16Documents?.get($0);if(!s)return 0;const bits=s.events;s.events=0;return bits;})();},(int)(uintptr_t)p);}
int cs16_browser_native_snapshot(void *p,uint8_t *rgba,int w,int h){(void)p;(void)rgba;(void)w;(void)h;return 0;}
int cs16_browser_native_script(void *p,const char *script,char *out,unsigned size){
 return MAIN_THREAD_EM_ASM_INT({return (function(){
  try{const s=Module.cs16Documents?.get($0);if(!s)return 0;const code=UTF8ToString($1),doc=s.frame.contentDocument;if(!doc)return 0;
   // These expressions originate in the COM bridge, never in game HTML.
   // Evaluate only the fixed property operations exposed by that bridge.
   let value;if(code==='!!document.body')value=!!doc.body;else if(code==='document.title')value=doc.title;
   else{let match=code.match(/^document\.scrollingElement\.(scrollLeft|scrollTop|scrollWidth|scrollHeight)$/);if(match)value=doc.scrollingElement?.[match[1]]||0;
    else{match=code.match(/^\(\(\)=>\{const e=document.scrollingElement,old=e\.(scrollLeft|scrollTop);e\.(scrollLeft|scrollTop)=(-?\d+);return e\.(scrollLeft|scrollTop)!==old\}\)\(\)$/);if(!match||match[1]!==match[2]||match[1]!==match[4])return 0;const e=doc.scrollingElement,old=e[match[1]];e[match[1]]=Number(match[3]);value=e[match[1]]!==old;}}
   const text=String(value);if(lengthBytesUTF8(text)>=$3)return 0;stringToUTF8(text,$2,$3);return 1;
  }catch{return 0;}
 })();},(int)(uintptr_t)p,script,out,size);
}
void cs16_browser_native_pump(void){}
