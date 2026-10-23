/* Isolated translated-function host. Contains no original game code. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "recomp_types.h"
#include "game_functions.h"
#define IMAGE_BASE 0x10000000u
#define SCRATCH 0x11000000u

uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp;
double g_st[8]; int g_fp_top;
uint16_t g_fpu_cw=0x027f;
uint16_t g_seg_cs,g_seg_ds,g_seg_es,g_seg_fs,g_seg_gs,g_seg_ss;
uint32_t g_fs_base,g_gs_base,g_cur_func,g_mxcsr=0x1f80;
uint64_t g_mm[8]; xmm_t g_xmm[8]; ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx,g_icall_count;
recomp_func_t recomp_lookup(uint32_t va) {
 for(size_t i=0;i<GAME_FUNCTION_COUNT;i++)if(game_functions[i].address==va)return game_functions[i].func;
 fprintf(stderr,"Unresolved guest call %08x\n",va);exit(3);
}
recomp_func_t recomp_lookup_manual(uint32_t va){(void)va;return NULL;}
recomp_func_t recomp_lookup_import(uint32_t va){(void)va;return NULL;}
static void read_exact(FILE *f,void *p,size_t n){if(!f||fread(p,1,n,f)!=n)exit(4);}
static void *guest(uint32_t va){return(void *)(uintptr_t)(g_mem_base+va);}
int main(int argc,char **argv){
 if(argc!=5)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 4;
 fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<=0||n>0x1000000)return 4;
#ifdef __EMSCRIPTEN__
 unsigned char *arena=calloc(1,0x2000000);if(!arena)return 4;
 g_mem_base=(ptrdiff_t)(uintptr_t)arena-IMAGE_BASE;
#else
 size_t span=(size_t)UINT32_MAX+1u;
 unsigned char *arena=mmap(NULL,span,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
 if(arena==MAP_FAILED||(uintptr_t)arena<=UINT32_MAX)return 4;
 g_mem_base=(ptrdiff_t)(uintptr_t)arena;
 size_t page=(size_t)sysconf(_SC_PAGESIZE),commit=((size_t)n+page-1)&~(page-1);
 if(mprotect(guest(IMAGE_BASE),commit,PROT_READ|PROT_WRITE)||mprotect(guest(SCRATCH),0x10000,PROT_READ|PROT_WRITE))return 4;
#endif
 read_exact(f,guest(IMAGE_BASE),(size_t)n);fclose(f);
 uint32_t regs[8];f=fopen(argv[2],"rb");read_exact(f,regs,sizeof regs);read_exact(f,guest(SCRATCH),0x10000);fclose(f);
 unsigned char *before=malloc((size_t)n);if(!before)return 4;memcpy(before,guest(IMAGE_BASE),(size_t)n);
 g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_ebx=regs[3];g_esp=regs[4];g_ebp=regs[5];g_esi=regs[6];g_edi=regs[7];
 g_flag_k=FK_EFLAGS;g_flag_a=0x202;g_flag_b=g_flag_cf=0;
 recomp_lookup((uint32_t)strtoul(argv[3],NULL,0))();
 regs[0]=g_eax;regs[1]=g_ecx;regs[2]=g_edx;regs[3]=g_ebx;regs[4]=g_esp;regs[5]=g_ebp;regs[6]=g_esi;regs[7]=g_edi;
 uint32_t flags=recomp_eflags(g_flag_k,g_flag_a,g_flag_b,g_flag_cf,1),unchanged=!memcmp(before,guest(IMAGE_BASE),(size_t)n);
 f=fopen(argv[4],"wb");if(!f)return 4;
 int ok=fwrite(regs,1,sizeof regs,f)==sizeof regs&&fwrite(&flags,4,1,f)==1&&fwrite(&unchanged,4,1,f)==1&&fwrite(guest(SCRATCH),1,0x10000,f)==0x10000;
 if(fclose(f))ok=0;free(before);
#ifdef __EMSCRIPTEN__
 free(arena);
#else
 munmap(arena,span);
#endif
 return ok?0:4;
}
