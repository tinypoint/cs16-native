/* Independent guest-layout fixtures for typed/ref/value/catch-all semantics. */
#include <stdio.h>
#include <string.h>
#include "win32hle.h"
#include "exception.h"
static uint32_t data,trace,mode,bindings;
enum {FI=0x40,HANDLER=0x80,UNWIND=0xa0,TRIES=0xc0,CATCHES=0xe0,THROW=0x120,TYPES=0x140,DERIVED_CT=0x160,BASE_CT=0x180,DERIVED_TD=0x200,BASE_TD=0x240,MISSING_TD=0x280,CATCH_TD=0x2c0,OBJECT=0x300};
#define M(offset) MEM32(data+(offset))
static void cleanup(void){trace=trace*10+1;RET(0,0);}
static void destroy_thrown(void){if(g_ecx!=data+OBJECT)hle_fatal("EH probe destructor this mismatch");trace=trace*10+3;RET(0,0);}
static void caught(void){
 if(mode==0 && MEM32(g_ebp-0x24)!=data+OBJECT+8)hle_fatal("EH probe reference adjustment mismatch");
 if(mode==1 && MEM32(g_ebp-0x24)!=0x1234abcd)hle_fatal("EH probe value copy mismatch");
 bindings++;trace=trace*10+2;RET(0x0ef01234,0);
}
static void root(void){
 uint32_t const old_fs=MEM32(g_fs_base),old_ebp=g_ebp,entry=g_esp,ebp=entry-4,rec=ebp-12;
 cs16_eh_frame *const landing=cs16_eh_enter(entry,g_cur_func);
 if(setjmp(landing->jump)){
  if(landing->resume!=0x0ef01234 || g_esp!=ebp-0x50 || trace!=123 || bindings!=1)hle_fatal("EH probe catch continuation mismatch");
  MEM32(g_fs_base)=old_fs;g_ebp=old_ebp;g_esp=entry;cs16_eh_leave(landing);RET(1,0);
 }
 MEM32(rec)=old_fs;MEM32(rec+4)=data+HANDLER;MEM32(rec+8)=1;MEM32(ebp-16)=ebp-0x50;
 MEM32(g_fs_base)=rec;g_ebp=ebp;g_esp=ebp-0x80;
 int result=cs16_eh_dispatch(data+OBJECT,data+THROW);
 if(mode!=3 || result || trace || bindings || MEM32(rec+8)!=1)hle_fatal("EH probe unmatched exception changed state");
 MEM32(g_fs_base)=old_fs;g_ebp=old_ebp;g_esp=entry;cs16_eh_leave(landing);RET(1,0);
}
static const win32hle_shim methods[]={{"EH.ProbeCleanup",cleanup},{"EH.ProbeCatch",caught},{"EH.ProbeDestroy",destroy_thrown},{"EH.ProbeRoot",root},{0,0}};
int cs16_probe_exceptions(void){
 win32hle_register(methods);data=gm_va(gm_calloc(1,4096));
 M(FI)=0x19930520;M(FI+4)=2;M(FI+8)=data+UNWIND;M(FI+12)=1;M(FI+16)=data+TRIES;
 MEM8(data+HANDLER)=0xb8;M(HANDLER+1)=data+FI;MEM8(data+HANDLER+5)=0xe9;
 M(UNWIND)=UINT32_MAX;M(UNWIND+8)=0;M(UNWIND+12)=hle_resolve("EH.ProbeCleanup");
 M(TRIES)=0;M(TRIES+4)=1;M(TRIES+8)=2;M(TRIES+12)=2;M(TRIES+16)=data+CATCHES;
 M(CATCHES+4)=data+MISSING_TD;M(CATCHES+8)=(uint32_t)-0x24;M(CATCHES+12)=hle_resolve("EH.ProbeCatch");
 M(CATCHES+16+8)=(uint32_t)-0x24;M(CATCHES+16+12)=hle_resolve("EH.ProbeCatch");
 M(THROW+4)=hle_resolve("EH.ProbeDestroy");M(THROW+12)=data+TYPES;M(TYPES)=2;M(TYPES+4)=data+DERIVED_CT;M(TYPES+8)=data+BASE_CT;
 M(DERIVED_CT+4)=data+DERIVED_TD;M(DERIVED_CT+12)=UINT32_MAX;M(DERIVED_CT+20)=12;
 M(BASE_CT+4)=data+BASE_TD;M(BASE_CT+8)=8;M(BASE_CT+12)=UINT32_MAX;M(BASE_CT+20)=4;
 strcpy(gm_ptr(data+DERIVED_TD+8),".?AVDerived@Fixture@@");strcpy(gm_ptr(data+BASE_TD+8),".?AVBase@Fixture@@");strcpy(gm_ptr(data+CATCH_TD+8),".?AVBase@Fixture@@");strcpy(gm_ptr(data+MISSING_TD+8),".?AVMissing@Fixture@@");M(OBJECT+8)=0x1234abcd;
 for(mode=0;mode<4;mode++){
  trace=bindings=0;M(CATCHES+16)=mode==0?8:0;M(CATCHES+16+4)=mode==2?0:mode==3?data+MISSING_TD:data+CATCH_TD;
  if(hle_call_guest(hle_resolve("EH.ProbeRoot"),0,NULL)!=1)return 11;
 }
 printf("{\"exceptionProbe\":true,\"cases\":4,\"typedBaseReference\":true,\"typedValueCopy\":true,\"crossDescriptorNameMatch\":true,\"catchAll\":true,\"unmatchedPreservesState\":true,\"cleanupCatchDestructorOrder\":true}\n");return 0;
}
