/* Single browser producer / game-thread consumer. Never call guest code from
 * the browser UI thread: its register file and window queue belong to the game. */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <emscripten.h>
#include "win32hle.h"
#define CAPACITY 1024u
typedef struct {int kind,a,b,c;} input_event;
static input_event events[CAPACITY];
static _Atomic unsigned write_index,read_index,overflow;
static uint32_t held[256];
extern unsigned cs16_trace_transfers;
extern unsigned cs16_presented_frames;
static unsigned motion_frame=UINT32_MAX,button_frame=UINT32_MAX;
EMSCRIPTEN_KEEPALIVE int cs16_web_input(int kind,int a,int b,int c){
 unsigned w=atomic_load_explicit(&write_index,memory_order_relaxed);
 unsigned r=atomic_load_explicit(&read_index,memory_order_acquire);
 if(w-r>=CAPACITY){atomic_store(&overflow,1);return 0;}
 events[w%CAPACITY]=(input_event){kind,a,b,c};
 atomic_store_explicit(&write_index,w+1,memory_order_release);return 1;
}
static void release_all(void){
 for(unsigned vk=0;vk<256;vk++)if(held[vk]){hle_input_key(0,vk,held[vk]-1);held[vk]=0;}
 /* Release capture through the ordinary message route as well. */
 uint32_t point=gm_va(gm_alloc(8)),args[]={point};
 hle_call_guest(hle_resolve("GetCursorPos"),1,args);
 int x=(int)MEM32(point),y=(int)MEM32(point+4);gm_free(gm_ptr(point));
 hle_input_mouse(0x202,x,y);hle_input_mouse(0x205,x,y);
}
void cs16_web_input_pump(uint32_t hwnd){
 if(atomic_exchange(&overflow,0)){atomic_store(&read_index,atomic_load(&write_index));release_all();return;}
 unsigned r=atomic_load_explicit(&read_index,memory_order_relaxed);
 unsigned w=atomic_load_explicit(&write_index,memory_order_acquire);
 for(;r!=w;r++){
  input_event e=events[r%CAPACITY];
  /* VGUI polls its cursor once per rendered frame before handling presses.
   * Keep a synthetic absolute move visible for one frame before its click,
   * and never collapse a down/up pair into the same sampled frame. */
  int mouse_button=(e.kind==1||e.kind==5)&&(e.a==0x201||e.a==0x202||e.a==0x204||e.a==0x205);
  if(mouse_button&&(motion_frame==cs16_presented_frames||button_frame==cs16_presented_frames))break;
  if(e.kind==1&&e.a==0x200)motion_frame=cs16_presented_frames;
  if(mouse_button)button_frame=cs16_presented_frames;
  if(e.kind==1){int l,t,right,bottom;if(hle_window_screen_rect(hwnd,&l,&t,&right,&bottom))hle_input_mouse((uint32_t)e.a,l+e.b,t+e.c);}
  else if(e.kind==2){unsigned vk=(unsigned)e.b&255;uint32_t scan=(uint32_t)e.c;if(!scan){uint32_t args[]={vk,0};scan=hle_call_guest(hle_resolve("MapVirtualKeyA"),2,args);}held[vk]=e.a?scan+1:0;hle_input_key(e.a,vk,scan);}
  else if(e.kind==6){
   if(e.a==5){cs16_trace_transfers=30000;EM_ASM({Module.cs16GLTrace=true;});continue;}
   /* Original cl_enginefunc_t::pfnClientCmd, from the uploaded client ABI. */
   const char *commands[]={"setinfo _vgui_menus 0\n","jointeam 5\n","joinclass 5\n","map de_dust2\n"};
   if(e.a<1||e.a>4||!gm_accessible(0x1A1C228,4,0))continue;
   uint32_t fn=MEM32(0x1A1C228);if(!fn)continue;
   uint32_t text=gm_va(gm_strdup(commands[e.a-1])),args[]={text};
   fprintf(stderr,"[web-command] %s",commands[e.a-1]);hle_call_guest(fn,1,args);gm_free(gm_ptr(text));
  }
  else if(e.kind==3)release_all();
  else if(e.kind==4||e.kind==5){
   uint32_t point=gm_va(gm_alloc(8)),args[]={point};hle_call_guest(hle_resolve("GetCursorPos"),1,args);
   int x=(int)MEM32(point)+(e.kind==4?e.a:0),y=(int)MEM32(point+4)+(e.kind==4?e.b:0);gm_free(gm_ptr(point));hle_input_mouse(e.kind==4?0x200:(uint32_t)e.a,x,y);
  }
 }
 atomic_store_explicit(&read_index,r,memory_order_release);
}
