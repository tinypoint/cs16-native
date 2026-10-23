#include "win32hle.h"
recomp_func_t cs16_transfer_target(uint32_t va){
 recomp_func_t result=recomp_lookup_manual(va);
 if(!result)result=recomp_lookup(va);
 if(!result)result=recomp_lookup_import(va);
 if(!result)hle_fatal("Unresolved original control transfer: target=%08x from=%08x",va,g_cur_func);
 return result;
}
