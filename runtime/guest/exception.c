/* MSVC x86 frame-based C++ EH, adapted from MIT pcrecomp native32/eh32.c.
 * Uses original FuncInfo / ThrowInfo / catch and unwind code. Unsupported
 * layouts stop explicitly; no success return is substituted for a throw. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "cs16_dispatch.h"
#include "exception.h"
static _Thread_local cs16_eh_frame *frames;
static _Thread_local uint32_t current_object,current_info;
static void readable(uint32_t p,uint32_t n){if(!gm_accessible(p,n,0))hle_fatal("C++ EH metadata unavailable: %08x bytes=%u",p,n);}
static int signed_state(uint32_t p){readable(p,4);return (int32_t)MEM32(p);}
cs16_eh_frame *cs16_eh_enter(uint32_t esp,uint32_t function){
 cs16_eh_frame *f=calloc(1,sizeof *f);if(!f)hle_fatal("C++ EH landing pad allocation");
 f->entry_esp=esp;f->function=function;f->call_scope=cs16_guest_call_scope();f->previous=frames;frames=f;return f;
}
void cs16_eh_leave(cs16_eh_frame *f){if(frames!=f)hle_fatal("C++ EH host frame ordering mismatch");frames=f->previous;free(f);}
static uint32_t funcinfo(uint32_t handler){
 if(!gm_accessible(handler,11,0) || MEM8(handler)!=0xb8)return 0;
 if(MEM8(handler+5)!=0xe9 && !(MEM8(handler+5)==0xff && MEM8(handler+6)==0x25))return 0;
 uint32_t fi=MEM32(handler+1);if(!gm_accessible(fi,20,0))return 0;
 uint32_t magic=MEM32(fi);return magic>=0x19930520u && magic<=0x19930522u?fi:0;
}
static uint32_t invoke(uint32_t va,uint32_t ebp,uint32_t ecx,const uint32_t *args,unsigned count){
 recomp_func_t fn=cs16_transfer_target(va);
 uint32_t old[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi,g_cur_func,g_flag_k,g_flag_a,g_flag_b,g_flag_cf};
 g_ebp=ebp;g_ecx=ecx;for(unsigned i=count;i>0;i--)PUSH32(g_esp,args[i-1]);PUSH32(g_esp,RECOMP_RETADDR);fn();uint32_t result=g_eax;
 g_eax=old[0];g_ecx=old[1];g_edx=old[2];g_ebx=old[3];g_esp=old[4];g_ebp=old[5];g_esi=old[6];g_edi=old[7];g_cur_func=old[8];g_flag_k=old[9];g_flag_a=old[10];g_flag_b=old[11];g_flag_cf=old[12];return result;
}
static void unwind(uint32_t rec,uint32_t fi,int target){
 int state=signed_state(rec+8),maximum=signed_state(fi+4);uint32_t map=MEM32(fi+8);
 if(maximum<0||maximum>4096)hle_fatal("C++ EH unwind state count invalid");
 unsigned steps=0;
 while(state!=target && state>=0){
  if(state>=maximum || ++steps>4096)hle_fatal("C++ EH unwind state invalid");
  uint32_t at=map+(uint32_t)state*8;readable(at,8);int next=(int32_t)MEM32(at);uint32_t action=MEM32(at+4);
  if(next>=state || next< -1)hle_fatal("C++ EH unwind map does not descend");
  MEM32(rec+8)=(uint32_t)next;if(action)invoke(action,rec+12,g_ecx,NULL,0);state=next;
 }
 if(state!=target)hle_fatal("C++ EH unwind target not reached");
}
static int same_type(uint32_t a,uint32_t b){
 if(a==b)return 1;if(a>UINT32_MAX-1024||b>UINT32_MAX-1024)return 0;
 for(unsigned i=8;i<1024;i++){if(!gm_accessible(a+i,1,0)||!gm_accessible(b+i,1,0))hle_fatal("C++ EH type name unavailable");if(MEM8(a+i)!=MEM8(b+i))return 0;if(!MEM8(a+i))return 1;}hle_fatal("C++ EH type name too long");return 0;
}
static uint32_t catchable(uint32_t info,uint32_t type,uint32_t adjectives){
 readable(info,16);uint32_t attributes=MEM32(info),array=MEM32(info+12);
 if((attributes&1) && !(adjectives&1))return 0;if((attributes&2) && !(adjectives&2))return 0;
 readable(array,4);uint32_t count=MEM32(array);if(count>256)hle_fatal("C++ EH catchable type count invalid");readable(array,4+count*4);
 for(unsigned i=0;i<count;i++){uint32_t ct=MEM32(array+4+i*4);readable(ct,28);if(same_type(MEM32(ct+4),type))return ct;}return 0;
}
static void bind_object(uint32_t h,uint32_t ct,uint32_t object,uint32_t ebp){
 uint32_t displacement=MEM32(h+8),adjectives=MEM32(h);if(!ct || !displacement || !MEM32(h+4))return;
 uint32_t dst=ebp+displacement,src=object+MEM32(ct+8);int32_t virtual_disp=(int32_t)MEM32(ct+12);
 if(virtual_disp!=-1)hle_fatal("C++ EH virtual base adjustment is not implemented");
 if(adjectives&8){if(!gm_accessible(dst,4,1))hle_fatal("C++ EH reference destination unavailable");MEM32(dst)=src;return;}
 uint32_t bytes=MEM32(ct+20),copy=MEM32(ct+24),properties=MEM32(ct);
 if(properties&1 || !copy){readable(src,bytes);if(!gm_accessible(dst,bytes,1))hle_fatal("C++ EH value destination unavailable");memcpy(gm_ptr(dst),gm_ptr(src),bytes);}
 else {uint32_t args[]={src,1};invoke(copy,ebp,dst,args,(properties&4)?2:1);}
}
int cs16_eh_dispatch(uint32_t object,uint32_t info){
 if(!info){object=current_object;info=current_info;}if(!info)return 0;
 readable(info,16);readable(g_fs_base,4);uint32_t top=MEM32(g_fs_base),records[128],count=0;
 for(uint32_t rec=top;rec && rec!=UINT32_MAX;rec=MEM32(rec)){
  if(count==128)hle_fatal("C++ EH chain is too deep");readable(rec,12);for(unsigned j=0;j<count;j++)if(records[j]==rec)hle_fatal("C++ EH chain cycle");records[count++]=rec;
  uint32_t fi=funcinfo(MEM32(rec+4));if(!fi)continue;
  int state=(int32_t)MEM32(rec+8);uint32_t n=MEM32(fi+12),tries=MEM32(fi+16);if(n>1024)hle_fatal("C++ EH try count invalid");readable(tries,n*20);
  for(unsigned i=0;i<n;i++){
   uint32_t block=tries+i*20;int low=(int32_t)MEM32(block),high=(int32_t)MEM32(block+4);if(state<low||state>high)continue;
   uint32_t nh=MEM32(block+12),handlers=MEM32(block+16);if(nh>256)hle_fatal("C++ EH catch count invalid");readable(handlers,nh*16);
   for(unsigned j=0;j<nh;j++){
    uint32_t h=handlers+j*16,type=MEM32(h+4),ct=type?catchable(info,type,MEM32(h)):0;if(type&&!ct)continue;
    cs16_eh_frame *owner=frames;while(owner && owner->entry_esp!=rec+16)owner=owner->previous;
    if(!owner)hle_fatal("C++ EH catch has no supported host landing pad: record=%08x",rec);
    if(owner->call_scope!=cs16_guest_call_scope())hle_fatal("C++ EH crosses a host callback boundary; not implemented");
    /* Validate the entire unwind path before executing any cleanup. */
    for(unsigned k=0;k<count-1;k++)if(!funcinfo(MEM32(records[k]+4)))hle_fatal("C++ EH crosses an unsupported SEH frame");
    for(unsigned k=0;k<count-1;k++)unwind(records[k],funcinfo(MEM32(records[k]+4)),-1);
    unwind(rec,fi,low);MEM32(g_fs_base)=rec;MEM32(rec+8)=(uint32_t)(high+1);
    uint32_t ebp=rec+12;bind_object(h,ct,object,ebp);
    uint32_t old_object=current_object,old_info=current_info;current_object=object;current_info=info;
    fprintf(stderr,"[guest-eh] handling frame=%08x catch=%08x owner=%08x\n",rec,MEM32(h+12),owner->function);
    uint32_t resume=invoke(MEM32(h+12),ebp,g_ecx,NULL,0);
    current_object=old_object;current_info=old_info;
    if(MEM32(info+4))invoke(MEM32(info+4),ebp,object,NULL,0);
    readable(ebp-16,4);uint32_t saved_esp=MEM32(ebp-16);if(!gm_accessible(saved_esp,4,0))hle_fatal("C++ EH saved stack unavailable");
    while(frames!=owner){cs16_eh_frame *drop=frames;frames=drop->previous;free(drop);}
    g_ebp=ebp;g_esp=saved_esp;g_eax=resume;g_cur_func=owner->function;owner->resume=resume;
    fprintf(stderr,"[guest-eh] resume=%08x esp=%08x\n",resume,g_esp);longjmp(owner->jump,1);
   }
  }
 }return 0;
}
