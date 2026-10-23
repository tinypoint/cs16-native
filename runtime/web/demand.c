/* A missing original function body is a hard diagnostic stop, never a no-op. */
#include <stdio.h>
#include <stdlib.h>
#include "win32hle.h"
__attribute__((noreturn)) void cs16_web_unloaded(uint32_t address){fprintf(stderr,"[web-unloaded] original function=%08x from=%08x\n",address,g_cur_func);abort();}
