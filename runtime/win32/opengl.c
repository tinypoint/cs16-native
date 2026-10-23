#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
extern SDL_Window *cs16_native_window(uint32_t);
extern uint32_t cs16_dc_window(uint32_t);
extern void cs16_native_pump(void);
static SDL_GLContext context;
static uint32_t context_dc;
#define CONTEXT_HANDLE 0x0C610001u
static void gl_require(void){if(!context || SDL_GL_GetCurrentContext()!=context)hle_fatal("OpenGL call without a current guest context");}
static void gl_buffer(uint32_t va,size_t size,int write){if(size>UINT32_MAX || !gm_accessible(va,(uint32_t)size,write))hle_fatal("OpenGL guest buffer unavailable: %08x bytes=%zu",va,size);}
static GLfloat gl_float(unsigned index){uint32_t bits=A32(index);GLfloat value;memcpy(&value,&bits,4);return value;}
static GLdouble gl_double(unsigned index){GLdouble value;memcpy(&value,gm_ptr(g_esp+4+index*4),8);return value;}
#include "opengl_bindings.inc"
static void choose_pixel_format(void){
 SDL_Window *window=cs16_native_window(cs16_dc_window(A32(0)));
 if(!window){hle_set_last_error(6);RET(0,2);}RET(1,2);
}
static void describe_pixel_format(void){
 if(!cs16_native_window(cs16_dc_window(A32(0))) || A32(1)!=1){hle_set_last_error(6);RET(0,4);}
 if(A32(3) && A32(2)>=40){uint32_t p=A32(3);gl_buffer(p,40,1);memset(gm_ptr(p),0,40);MEM16(p)=40;MEM16(p+2)=1;MEM32(p+4)=0x25;MEM8(p+9)=32;MEM8(p+10)=8;MEM8(p+11)=16;MEM8(p+12)=8;MEM8(p+13)=8;MEM8(p+14)=8;MEM8(p+16)=8;MEM8(p+23)=24;MEM8(p+24)=8;}
 RET(1,4);
}
static void set_pixel_format(void){if(A32(1)!=1||!cs16_native_window(cs16_dc_window(A32(0))))RET(0,3);RET(1,3);}
static void create_context(void){
 SDL_Window *window=cs16_native_window(cs16_dc_window(A32(0)));if(!window || context)RET(0,1);
 context=SDL_GL_CreateContext(window);if(!context){fprintf(stderr,"[opengl] context creation failed: %s\n",SDL_GetError());RET(0,1);}
 SDL_GL_MakeCurrent(NULL,NULL);context_dc=A32(0);fprintf(stderr,"[opengl] native context created\n");RET(CONTEXT_HANDLE,1);
}
static void make_current(void){
 if(!A32(0)&&!A32(1)){if(SDL_GL_MakeCurrent(NULL,NULL))RET(0,2);RET(1,2);}
 SDL_Window *window=cs16_native_window(cs16_dc_window(A32(0)));
 if(A32(1)!=CONTEXT_HANDLE||!context||!window)RET(0,2);
 if(SDL_GL_MakeCurrent(window,context))RET(0,2);context_dc=A32(0);RET(1,2);
}
static void delete_context(void){if(A32(0)!=CONTEXT_HANDLE||!context)RET(0,1);SDL_GL_DeleteContext(context);context=NULL;context_dc=0;RET(1,1);}
static void get_current_context(void){RET(context && SDL_GL_GetCurrentContext()==context?CONTEXT_HANDLE:0,0);}
static void get_current_dc(void){RET(context && SDL_GL_GetCurrentContext()==context?context_dc:0,0);}
static void proc_address(void){const char *name=ASTR(0);RET(name && !strncmp(name,"gl",2)?hle_resolve(name):0,1);}
static void capture_frame(SDL_Window *window){
 const char *path=getenv("CS16_FRAME_CAPTURE");if(!path || !*path)return;
 int w,h;SDL_GL_GetDrawableSize(window,&w,&h);if(w<=0||h<=0||w>8192||h>8192)return;
 size_t bytes=(size_t)w*h*4;unsigned char *pixels=malloc(bytes),*flipped=malloc(bytes);if(!pixels||!flipped){free(pixels);free(flipped);return;}
 GLint read_buffer,alignment,row_length,skip_rows,skip_pixels;
 glGetIntegerv(GL_READ_BUFFER,&read_buffer);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);glGetIntegerv(GL_PACK_ROW_LENGTH,&row_length);glGetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);
 glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
 glReadBuffer(read_buffer);glPixelStorei(GL_PACK_ALIGNMENT,alignment);glPixelStorei(GL_PACK_ROW_LENGTH,row_length);glPixelStorei(GL_PACK_SKIP_ROWS,skip_rows);glPixelStorei(GL_PACK_SKIP_PIXELS,skip_pixels);
 for(int y=0;y<h;y++)memcpy(flipped+(size_t)y*w*4,pixels+(size_t)(h-1-y)*w*4,(size_t)w*4);
 SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormatFrom(flipped,w,h,32,w*4,SDL_PIXELFORMAT_RGBA32);
 if(surface){if(!SDL_SaveBMP(surface,path))fprintf(stderr,"[opengl] captured first buffer %dx%d\n",w,h);SDL_FreeSurface(surface);}free(pixels);free(flipped);
}
static void swap_buffers(void){
 gl_require();SDL_Window *window=cs16_native_window(cs16_dc_window(A32(0)));if(!window)RET(0,1);
 static unsigned frames;if(!frames++){capture_frame(window);fprintf(stderr,"[opengl] first guest buffer swap\n");}SDL_GL_SwapWindow(window);cs16_native_pump();RET(1,1);
}
static void get_error(void){gl_require();RET(glGetError(),0);}
static void get_string(void){
 gl_require();GLenum name=A32(0);const GLubyte *value=glGetString(name);if(!value)RET(0,1);
 static uint32_t cached[4];unsigned index=name==GL_VENDOR?0:name==GL_RENDERER?1:name==GL_VERSION?2:name==GL_EXTENSIONS?3:4;if(index==4)RET(0,1);
 if(!cached[index]){
  /* Advertise only extensions implemented by the bridge and native driver. */
  const char *text=(const char *)value;if(index==3)text="";
  cached[index]=gm_va(gm_strdup(text));if(index<3)fprintf(stderr,"[opengl] string %04x: %s\n",name,text);
 }RET(cached[index],1);
}
static unsigned query_count(GLenum name){switch(name){case GL_MODELVIEW_MATRIX:case GL_PROJECTION_MATRIX:case GL_TEXTURE_MATRIX:return 16;case GL_VIEWPORT:case GL_SCISSOR_BOX:case GL_CURRENT_COLOR:case GL_COLOR_CLEAR_VALUE:return 4;case GL_DEPTH_RANGE:case GL_MAX_VIEWPORT_DIMS:return 2;
 case GL_MAX_TEXTURE_SIZE:case GL_MAX_TEXTURE_UNITS:case GL_MAX_LIGHTS:case GL_MAX_CLIP_PLANES:case GL_MAX_MODELVIEW_STACK_DEPTH:case GL_MAX_PROJECTION_STACK_DEPTH:case GL_TEXTURE_BINDING_2D:case GL_PACK_ALIGNMENT:case GL_UNPACK_ALIGNMENT:case GL_RED_BITS:case GL_GREEN_BITS:case GL_BLUE_BITS:case GL_ALPHA_BITS:case GL_DEPTH_BITS:case GL_STENCIL_BITS:case GL_DOUBLEBUFFER:case GL_DRAW_BUFFER:case GL_READ_BUFFER:case GL_MATRIX_MODE:return 1;
 default:hle_fatal("GL state query size not implemented for %x",name);return 0;}}
static void get_integer(void){gl_require();gl_buffer(A32(1),query_count(A32(0))*4,1);glGetIntegerv(A32(0),gm_ptr(A32(1)));RET(0,2);}
static void get_float(void){gl_require();gl_buffer(A32(1),query_count(A32(0))*4,1);glGetFloatv(A32(0),gm_ptr(A32(1)));RET(0,2);}
static void gen_textures(void){gl_require();if((int32_t)A32(0)<0)hle_fatal("negative GL texture count");gl_buffer(A32(1),(size_t)A32(0)*4,1);glGenTextures(A32(0),gm_ptr(A32(1)));RET(0,2);}
static void delete_textures(void){gl_require();if((int32_t)A32(0)<0)hle_fatal("negative GL texture count");gl_buffer(A32(1),(size_t)A32(0)*4,0);glDeleteTextures(A32(0),gm_ptr(A32(1)));RET(0,2);}
static size_t pixel_bytes(int w,int h,GLenum format,GLenum type,int pack){
 if(w<0||h<0 || type!=GL_UNSIGNED_BYTE)hle_fatal("GL pixel transfer format/type not implemented");
 GLint row_length,skip_rows,skip_pixels;glGetIntegerv(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,&row_length);glGetIntegerv(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,&skip_rows);glGetIntegerv(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,&skip_pixels);
 if(row_length||skip_rows||skip_pixels)hle_fatal("GL non-default pixel row layout is not implemented");
 unsigned channels=format==GL_RGBA?4:format==GL_RGB?3:format==GL_LUMINANCE_ALPHA?2:format==GL_LUMINANCE||format==GL_ALPHA?1:0;
 if(!channels)hle_fatal("GL pixel format %x not implemented",format);GLint alignment;glGetIntegerv(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,&alignment);
 size_t row=(size_t)w*channels;if(alignment!=1&&alignment!=2&&alignment!=4&&alignment!=8)hle_fatal("GL pixel alignment invalid");size_t pitch=(row+alignment-1)&~(size_t)(alignment-1);return h?pitch*(h-1)+row:0;
}
static void tex_image(void){gl_require();void *p=NULL;if(A32(8)){gl_buffer(A32(8),pixel_bytes(A32(3),A32(4),A32(6),A32(7),0),0);p=gm_ptr(A32(8));}glTexImage2D(A32(0),A32(1),A32(2),A32(3),A32(4),A32(5),A32(6),A32(7),p);RET(0,9);}
static void tex_sub_image(void){gl_require();gl_buffer(A32(8),pixel_bytes(A32(4),A32(5),A32(6),A32(7),0),0);glTexSubImage2D(A32(0),A32(1),A32(2),A32(3),A32(4),A32(5),A32(6),A32(7),gm_ptr(A32(8)));RET(0,9);}
static void read_pixels(void){gl_require();gl_buffer(A32(6),pixel_bytes(A32(2),A32(3),A32(4),A32(5),1),1);glReadPixels(A32(0),A32(1),A32(2),A32(3),A32(4),A32(5),gm_ptr(A32(6)));RET(0,7);}
static void load_matrix(void){gl_require();gl_buffer(A32(0),64,0);glLoadMatrixf(gm_ptr(A32(0)));RET(0,1);}
static void mult_matrix(void){gl_require();gl_buffer(A32(0),64,0);glMultMatrixf(gm_ptr(A32(0)));RET(0,1);}
static const win32hle_shim manual_gl_shims[]={
 {"ChoosePixelFormat",choose_pixel_format},{"DescribePixelFormat",describe_pixel_format},{"SetPixelFormat",set_pixel_format},
 {"wglCreateContext",create_context},{"wglMakeCurrent",make_current},{"wglDeleteContext",delete_context},{"wglGetCurrentContext",get_current_context},{"wglGetCurrentDC",get_current_dc},{"wglGetProcAddress",proc_address},{"wglSwapBuffers",swap_buffers},{"SwapBuffers",swap_buffers},
 {"glGetError",get_error},{"glGetString",get_string},{"glGetIntegerv",get_integer},{"glGetFloatv",get_float},{"glGenTextures",gen_textures},{"glDeleteTextures",delete_textures},{"glTexImage2D",tex_image},{"glTexSubImage2D",tex_sub_image},{"glReadPixels",read_pixels},{"glLoadMatrixf",load_matrix},{"glMultMatrixf",mult_matrix},{0,0}
};
void cs16_opengl_register(void){win32hle_register(scalar_gl_shims);win32hle_register(manual_gl_shims);for(unsigned i=0;i<sizeof unsupported_gl_entries/sizeof *unsupported_gl_entries;i++)hle_resolve_or_stub(unsupported_gl_entries[i]);}
