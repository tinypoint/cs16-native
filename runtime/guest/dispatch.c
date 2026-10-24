#include "win32hle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Profile only original dynamic roots; static callees remain linker-reachable. */
void cs16_profile_target(uint32_t va){
 static int enabled=-1;static uint32_t seen[16384];if(enabled<0)enabled=getenv("CS16_PROFILE_TARGETS")!=NULL;if(!enabled||!va||va>=0x7f000000u)return;
 unsigned slot=(va*2654435761u)&16383u;for(unsigned i=0;i<16384;i++,slot=(slot+1)&16383u){if(seen[slot]==va)return;if(!seen[slot]){seen[slot]=va;fprintf(stderr,"[aot-root] %08x\n",va);return;}}hle_fatal("Original dynamic-root profile capacity exceeded");
}
unsigned cs16_trace_transfers;
recomp_func_t cs16_transfer_target(uint32_t va){
 if(cs16_trace_transfers){
  static unsigned char category[4096];const char *name=NULL;int gl=0;
  if(va>=0x7f000000u && va<0x7f010000u){unsigned i=(va-0x7f000000u)/16;if(!category[i]){name=hle_name(va);category[i]=name&&(!strncmp(name,"gl",2)||!strncmp(name,"wgl",3))?2:1;}gl=category[i]==2;}
  if(!gl){cs16_trace_transfers--;if(!name)name=hle_name(va);fprintf(stderr,"[transfer] %08x -> %08x %s esp=%08x remaining=%u\n",g_cur_func,va,name?name:"original",g_esp,cs16_trace_transfers);}
 }
 cs16_profile_target(va);
 recomp_func_t result=recomp_lookup_manual(va);
 if(!result)result=recomp_lookup(va);
 if(!result)result=recomp_lookup_import(va);
 if(!result)hle_fatal("Unresolved original control transfer: target=%08x from=%08x",va,g_cur_func);
 return result;
}
