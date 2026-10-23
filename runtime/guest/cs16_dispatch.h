#ifndef CS16_DISPATCH_H
#define CS16_DISPATCH_H
#include "recomp_types.h"
recomp_func_t cs16_transfer_target(uint32_t va);
void cs16_x87_store_env(uint32_t address);
int cs16_x87_load_env(uint32_t address);
void cs16_x87_note(uint32_t ip,uint32_t opcode,uint32_t data,int has_data,int compare,int compared);
void cs16_x87_init(void);
void cs16_x87_clear(void);

/* A guest JMP must not accumulate native C frames. All lifted/shim targets
   have the same void(void) ABI, so Clang musttail preserves guest state while
   guaranteeing tail transfer. This diagnostic native host requires Clang.
   A browser build must explicitly validate its tail-call lowering/support. */
#undef RECOMP_ITAIL
#define RECOMP_ITAIL(target_va) do { \
 uint32_t _tail_va=(uint32_t)(target_va); \
 g_icall_trace[g_icall_trace_idx&(ICALL_TRACE_SIZE-1)]=_tail_va; \
 g_icall_from[g_icall_trace_idx&(ICALL_TRACE_SIZE-1)]=g_cur_func; \
 g_icall_trace_idx++;g_icall_count++; \
 RECOMP_FLAGS_OUT();RECOMP_REGS_OUT(); \
 if(_tail_va==RECOMP_RETADDR)return; \
 recomp_func_t _tail_fn=cs16_transfer_target(_tail_va); \
 __attribute__((musttail)) return _tail_fn(); \
} while(0)

/* Unresolved indirect calls are fatal, rather than returning a fabricated 0. */
#undef RECOMP_ICALL
#define RECOMP_ICALL(target_va) do { \
 uint32_t _call_va=(uint32_t)(target_va); \
 g_icall_trace[g_icall_trace_idx&(ICALL_TRACE_SIZE-1)]=_call_va; \
 g_icall_from[g_icall_trace_idx&(ICALL_TRACE_SIZE-1)]=g_cur_func; \
 g_icall_trace_idx++;g_icall_count++; \
 recomp_func_t _call_fn=cs16_transfer_target(_call_va); \
 uint32_t _call_from=g_cur_func; \
 PUSH32(esp,RECOMP_RETADDR);RECOMP_FLAGS_OUT();RECOMP_REGS_OUT(); \
 _call_fn();RECOMP_REGS_IN();g_cur_func=_call_from; \
} while(0)
#endif
