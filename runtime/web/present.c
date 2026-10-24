/* Continuous readback of the actual original-program GL framebuffer. */
#include <emscripten.h>
#include <GL/gl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "guest_memory.h"
/* Observation only: original HUD callbacks supply these values. */
static float client_data[8];static unsigned client_updates,weapon_updates,weapon_id,weapon_clip;
void cs16_web_observe_client(uint32_t data){if(gm_accessible(data,32,0)){memcpy(client_data,gm_ptr(data),32);client_updates++;}}
void cs16_web_observe_weapon(uint32_t data,uint32_t bytes){if(bytes>=3&&gm_accessible(data,3,0)){const uint8_t *p=gm_ptr(data);if(p[0]){weapon_id=p[1];weapon_clip=p[2];weapon_updates++;}}}
int cs16_web_present(int width,int height){
 static uint8_t *rgba;static size_t capacity;static unsigned frames;
 static double last;
 if(width<1||height<1||width>4096||height>4096)return 0;
 double now=emscripten_get_now();if(last&&now-last<1000.0/30.0)return 0;last=now;
 size_t bytes=(size_t)width*height*4;
 if(bytes>capacity){void *next=realloc(rgba,bytes);if(!next)abort();rgba=next;capacity=bytes;}
 glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
 uint32_t connection=gm_accessible(0x2DAF900,4,0)?*(uint32_t *)gm_ptr(0x2DAF900):0;
 uint32_t server=gm_accessible(0x25544AC,4,0)?*(uint32_t *)gm_ptr(0x25544AC):0;
 MAIN_THREAD_EM_ASM({return (function(){
  const output=document.getElementById('display');if(!output)throw new Error('Missing presentation canvas');
  if(output.width!==$1||output.height!==$2){output.width=$1;output.height=$2;document.getElementById('stage').style.aspectRatio=$1+'/'+$2;}
  let image=Module.cs16PresentationImage;
  if(!image||image.width!==$1||image.height!==$2)image=Module.cs16PresentationImage=new ImageData($1,$2);
  const pitch=$1*4;
  for(let row=0;row<$2;row++){const start=$0+($2-1-row)*pitch;image.data.set(HEAPU8.subarray(start,start+pitch),row*pitch);}
  for(let i=3;i<image.data.length;i+=4)image.data[i]=255;
  output.getContext('2d',{alpha:false}).putImageData(image,0,0);
  window.cs16FrameCount=(window.cs16FrameCount||0)+1;window.cs16OriginalFrame=true;window.cs16RuntimeInfo={connection:$3,server:$4,memoryBytes:HEAPU8.byteLength,clientUpdates:$6,origin:Array.from(HEAPF32.subarray($5/4,$5/4+3)),angles:Array.from(HEAPF32.subarray($5/4+3,$5/4+6)),weaponUpdates:$7,weaponId:$8,clip:$9};
 })();},rgba,width,height,connection,server,client_data,client_updates,weapon_updates,weapon_id,weapon_clip);
 if(++frames==30)fprintf(stderr,"[web-preview] original pixels presented from this Wasm instance\n");
 return 1;
}
