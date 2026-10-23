#ifndef CS16_EXCEPTION_H
#define CS16_EXCEPTION_H
#include <setjmp.h>
#include <stdint.h>
typedef struct cs16_eh_frame {
 struct cs16_eh_frame *previous;
 jmp_buf jump;
 uint32_t entry_esp,resume,function;
 unsigned call_scope;
} cs16_eh_frame;
cs16_eh_frame *cs16_eh_enter(uint32_t esp,uint32_t function);
void cs16_eh_leave(cs16_eh_frame *frame);
int cs16_eh_dispatch(uint32_t object,uint32_t throw_info);
unsigned cs16_guest_call_scope(void);
#endif
