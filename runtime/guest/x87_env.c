/* The 28-byte protected-mode x87 environment for the existing lifted FPU
 * model. Arithmetic precision/exceptions remain limited by recomp32's double
 * stack; loading a different stack topology stops instead of losing values. */
#include <math.h>
#include <string.h>
#include "win32hle.h"
static uint16_t status,opcode;
static uint32_t instruction_pointer,data_pointer;
void cs16_x87_snapshot(uint32_t out[4]){out[0]=status|((uint32_t)opcode<<16);out[1]=instruction_pointer;out[2]=data_pointer;out[3]=0;}
void cs16_x87_restore(const uint32_t in[4]){status=(uint16_t)in[0];opcode=(uint16_t)(in[0]>>16);instruction_pointer=in[1];data_pointer=in[2];}
void cs16_x87_init(void){status=opcode=0;instruction_pointer=data_pointer=0;g_fp_top=0;g_fpu_cw=0x37f;memset(g_st,0,sizeof g_st);memset(g_st_i64,0,sizeof g_st_i64);}
void cs16_x87_clear(void){status&=(uint16_t)~0x80ffu;}
static uint16_t tags(void){
 if(g_fp_top<0 || g_fp_top>8)hle_fatal("x87 environment: invalid stack depth %d",g_fp_top);
 uint16_t result=0xffff;unsigned top=(unsigned)(-g_fp_top)&7;
 for(int i=0;i<g_fp_top;i++){
  unsigned tag=g_st[i]==0.0?1u:!isfinite(g_st[i])||fpclassify(g_st[i])==FP_SUBNORMAL?2u:0u;
  unsigned shift=((top+(unsigned)i)&7)*2;result=(uint16_t)((result&~(3u<<shift))|(tag<<shift));
 }return result;
}
static int compare_state(uint16_t sw){
 unsigned bits=sw&0x4700u;
 return bits==0x4500?2:bits==0x100?-1:bits==0x4000?0:bits==0?1:(int)(0x10000u|bits);
}
void cs16_x87_note(uint32_t ip,uint32_t op,uint32_t data,int has_data,int compare,int compared){
 instruction_pointer=ip;opcode=(uint16_t)op;if(has_data)data_pointer=data;
 if(compared){uint16_t bits=compare>=0x10000?(uint16_t)(compare&0x4700):compare==2?0x4500:compare<0?0x100:compare==0?0x4000:0;status=(uint16_t)((status&~0x4700u)|bits);}
}
void cs16_x87_store_env(uint32_t address){
 if(!gm_accessible(address,28,1))hle_fatal("x87 environment destination unavailable: %08x",address);
 uint16_t sw=(uint16_t)((status&~0x3800u)|(((unsigned)(-g_fp_top)&7)<<11));
 MEM32(address)=0xffff0000u|g_fpu_cw;MEM32(address+4)=0xffff0000u|sw;MEM32(address+8)=0xffff0000u|tags();
 MEM32(address+12)=instruction_pointer;MEM32(address+16)=g_seg_cs|((uint32_t)opcode<<16);
 MEM32(address+20)=data_pointer;MEM32(address+24)=0xffff0000u|g_seg_ds;
 g_fpu_cw|=0x3f; /* FNSTENV masks exceptions after storing the original CW. */
}
int cs16_x87_load_env(uint32_t address){
 if(!gm_accessible(address,28,0))hle_fatal("x87 environment source unavailable: %08x",address);
 uint16_t sw=MEM16(address+4);
 if(((sw>>11)&7)!=((unsigned)(-g_fp_top)&7) || MEM16(address+8)!=tags())
  hle_fatal("x87 environment restores a different stack topology; not supported yet");
 g_fpu_cw=MEM16(address);status=sw;instruction_pointer=MEM32(address+12);opcode=MEM16(address+18)&0x7ff;
 data_pointer=MEM32(address+20);return compare_state(status);
}
