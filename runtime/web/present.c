/* Present pixels produced by this running original-program Wasm instance.
 * Separate UI canvas retains the result if a diagnostic worker stops. */
#include <emscripten.h>
#include <GL/gl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
int cs16_web_present(int width,int height){
 static unsigned colored_frames;
 if(width<1||height<1||width>4096||height>4096)return 0;
 size_t bytes=(size_t)width*height*4;uint8_t *rgba=malloc(bytes);if(!rgba)abort();glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
 unsigned colored=0;for(size_t i=0;i<bytes;i+=4)if(rgba[i]||rgba[i+1]||rgba[i+2])colored++;
 fprintf(stderr,"[web-preview] rendered RGB pixels=%u of %u\n",colored,width*height);
 if(colored>(unsigned)(width*height/8)){
  if(++colored_frames<30){free(rgba);return 0;}
  MAIN_THREAD_EM_ASM({return (function(){
   const output=document.getElementById('display');if(!output)throw new Error('Missing presentation canvas');output.width=$1;output.height=$2;
   const image=new ImageData($1,$2);const pitch=$1*4;
   for(let row=0;row<$2;row++){const start=$0+($2-1-row)*pitch;image.data.set(HEAPU8.subarray(start,start+pitch),row*pitch)}
   for(let i=3;i<image.data.length;i+=4)image.data[i]=255;
   output.getContext('2d',{alpha:false}).putImageData(image,0,0);window.cs16OriginalFrame=true;
  })();},rgba,width,height);
  free(rgba);fprintf(stderr,"[web-preview] original pixels presented from this Wasm instance\n");return 1;
 }
 free(rgba);return 0;
}
