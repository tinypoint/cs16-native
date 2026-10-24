/* A missing original function body is a hard diagnostic stop, never a no-op. */
#include <stdio.h>
#include <stdlib.h>
#include <emscripten.h>
#include "win32hle.h"
void cs16_web_record_fatal(const char *message,uint32_t function){
 MAIN_THREAD_EM_ASM({return(function(){const message=UTF8ToString($0),record={time:new Date().toISOString(),message,function:$1,runtime:window.cs16RuntimeInfo||null};window.cs16Fatal=record;try{localStorage.setItem('cs16-fatal',JSON.stringify(record));}catch{}console.error('[fatal-record] '+JSON.stringify(record));document.body.classList.add('failed');const status=document.getElementById('status');if(status)status.textContent=message;})();},message,function);
}
__attribute__((noreturn)) void cs16_web_unloaded(uint32_t address){char message[160];snprintf(message,sizeof message,"[web-unloaded] original function=%08x from=%08x",address,g_cur_func);fprintf(stderr,"%s\n",message);cs16_web_record_fatal(message,g_cur_func);abort();}
