/* Diagnostic input through the same Win32 queue as the browser adapter. */
#include "win32hle.h"
#include <time.h>
#include <stdio.h>
extern void cs16_native_pump(void);
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
void cs16_offline_probe_pump(void){
 cs16_native_pump();static double start;static unsigned step;
 if(!gm_accessible(0x2DAF900,4,0)||MEM32(0x2DAF900)!=5||MEM32(0x25544AC)!=2)return;
 if(!start)start=seconds();double elapsed=seconds()-start;
 if(step==0&&elapsed>.7){uint32_t args[]={gm_va(gm_strdup("Valve001")),0};uint32_t window=hle_call_guest(hle_resolve("FindWindowA"),2,args);int l,t,r,b;if(!hle_window_screen_rect(window,&l,&t,&r,&b))return;int x=l+(r-l)*.22,y=t+(b-t)*.78;hle_input_mouse(0x200,x,y);hle_input_mouse(0x201,x,y);hle_input_mouse(0x202,x,y);fprintf(stderr,"[offline-probe] close welcome\n");step++;}
 else if(step==1&&elapsed>1.7){hle_input_key(1,'1',2);hle_input_key(0,'1',2);fprintf(stderr,"[offline-probe] choose team\n");step++;}
 else if(step==2&&elapsed>2.7){hle_input_key(1,'1',2);hle_input_key(0,'1',2);fprintf(stderr,"[offline-probe] choose class\n");step++;}
 else if(step==3&&elapsed>5){hle_input_key(1,'W',17);fprintf(stderr,"[offline-probe] move forward\n");step++;}
 else if(step==4&&elapsed>6.5){hle_input_key(0,'W',17);fprintf(stderr,"[offline-probe] stop movement\n");step++;}
 else if(step==5&&elapsed>7){hle_input_mouse(0x201,320,240);fprintf(stderr,"[offline-probe] fire down\n");step++;}
 else if(step==6&&elapsed>7.5){hle_input_mouse(0x202,320,240);fprintf(stderr,"[offline-probe] fire up\n");step++;}
}
