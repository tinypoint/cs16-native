/* Native presentation of guest windows. No engine/UI code is substituted. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
extern void (*hle_native_window_hook)(uint32_t,int);
typedef struct {uint32_t hwnd;SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;int tw,th,tbpp;} native_window;
static native_window windows[32];
static int initialized;
static unsigned presented;
extern int cs16_cursor_visible(void);
static void update_cursor(void){
 static struct {uint32_t handle;SDL_Cursor *cursor;} cursors[96];static unsigned count;
 uint32_t handle=hle_current_cursor();
 if(!handle || !cs16_cursor_visible()){SDL_ShowCursor(SDL_DISABLE);return;}
 SDL_Cursor *cursor=NULL;
 for(unsigned i=0;i<count;i++)if(cursors[i].handle==handle){cursor=cursors[i].cursor;break;}
 if(!cursor){
  if(handle>=0x1C200000u && handle<0x1C210000u){
   unsigned id=handle-0x1C200000u;SDL_SystemCursor shape=SDL_SYSTEM_CURSOR_ARROW;
   switch(id){case 32513:shape=SDL_SYSTEM_CURSOR_IBEAM;break;case 32514:shape=SDL_SYSTEM_CURSOR_WAIT;break;case 32515:shape=SDL_SYSTEM_CURSOR_CROSSHAIR;break;case 32642:shape=SDL_SYSTEM_CURSOR_SIZENWSE;break;case 32643:shape=SDL_SYSTEM_CURSOR_SIZENESW;break;case 32644:shape=SDL_SYSTEM_CURSOR_SIZEWE;break;case 32645:shape=SDL_SYSTEM_CURSOR_SIZENS;break;case 32640:case 32646:shape=SDL_SYSTEM_CURSOR_SIZEALL;break;case 32648:shape=SDL_SYSTEM_CURSOR_NO;break;case 32649:shape=SDL_SYSTEM_CURSOR_HAND;break;case 32650:shape=SDL_SYSTEM_CURSOR_WAITARROW;break;}
   cursor=SDL_CreateSystemCursor(shape);
  }else{
   const hle_cursor_image_t *image=hle_cursor_image(handle);
   if(image){uint8_t mask[128],data[128];for(unsigned i=0;i<128;i++){mask[i]=(uint8_t)~image->and_mask[i];data[i]=(uint8_t)~(image->and_mask[i]^image->xor_mask[i]);}cursor=SDL_CreateCursor(data,mask,32,32,image->hot_x,image->hot_y);}
  }
  if(!cursor)hle_fatal("native cursor %08x unavailable: %s",handle,SDL_GetError());
  if(count>=96)hle_fatal("native cursor capacity exceeded");cursors[count].handle=handle;cursors[count++].cursor=cursor;
 }
 SDL_SetCursor(cursor);SDL_ShowCursor(SDL_ENABLE);
}
static void init(void){
 if(initialized)return;
 SDL_SetMainReady();
 if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS))hle_fatal("SDL video initialization: %s",SDL_GetError());
 initialized=1;
}
static native_window *lookup(uint32_t hwnd){for(unsigned i=0;i<32;i++)if(windows[i].hwnd==hwnd)return &windows[i];return NULL;}
static void window_changed(uint32_t hwnd,int action){
 native_window *n=lookup(hwnd);
 if(action==2){if(n){if(n->texture)SDL_DestroyTexture(n->texture);if(n->renderer)SDL_DestroyRenderer(n->renderer);SDL_DestroyWindow(n->window);memset(n,0,sizeof *n);}return;}
 int l,t,r,b;if(!hle_window_screen_rect(hwnd,&l,&t,&r,&b))return;
 if(!n){
  for(unsigned i=0;i<32;i++)if(!windows[i].hwnd){n=&windows[i];break;}
  if(!n)hle_fatal("native window capacity exceeded");init();
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);
  n->window=SDL_CreateWindow(hle_window_class(hwnd),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,r>l?r-l:1,b>t?b-t:1,SDL_WINDOW_HIDDEN|SDL_WINDOW_OPENGL);
  if(!n->window)hle_fatal("native window creation: %s",SDL_GetError());n->hwnd=hwnd;
  fprintf(stderr,"[native-window] created hwnd=%08x class=%s size=%dx%d\n",hwnd,hle_window_class(hwnd),r-l,b-t);
 }
 SDL_SetWindowSize(n->window,r>l?r-l:1,b>t?b-t:1);
 if(hle_window_style(hwnd)&0x10000000u)SDL_ShowWindow(n->window);else SDL_HideWindow(n->window);
}
static uint32_t virtual_key(SDL_Keycode key){
 if(key>='a'&&key<='z')return (uint32_t)(key-'a'+'A');if(key>='0'&&key<='9')return (uint32_t)key;
 if(key>=SDLK_F1&&key<=SDLK_F12)return 0x70u+(uint32_t)(key-SDLK_F1);
 switch(key){case SDLK_ESCAPE:return 27;case SDLK_RETURN:return 13;case SDLK_SPACE:return 32;case SDLK_TAB:return 9;case SDLK_BACKSPACE:return 8;case SDLK_UP:return 38;case SDLK_DOWN:return 40;case SDLK_LEFT:return 37;case SDLK_RIGHT:return 39;case SDLK_LSHIFT:case SDLK_RSHIFT:return 16;case SDLK_LCTRL:case SDLK_RCTRL:return 17;case SDLK_LALT:case SDLK_RALT:return 18;default:return 0;}
}
static void pump(void){
 if(!initialized)return;update_cursor();SDL_Event event;
 while(SDL_PollEvent(&event)){
  uint32_t id=event.type==SDL_WINDOWEVENT?event.window.windowID:event.type==SDL_MOUSEMOTION?event.motion.windowID:event.type==SDL_KEYDOWN||event.type==SDL_KEYUP?event.key.windowID:event.button.windowID;
  native_window *n=NULL;for(unsigned i=0;i<32;i++)if(windows[i].hwnd && SDL_GetWindowID(windows[i].window)==id){n=&windows[i];break;}
  if(event.type==SDL_QUIT){hle_post_message(0,0x12,0,0);continue;}if(!n)continue;
  if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_CLOSE)hle_post_message(n->hwnd,0x10,0,0);
  if(event.type==SDL_KEYDOWN||event.type==SDL_KEYUP){uint32_t vk=virtual_key(event.key.keysym.sym);if(vk)hle_input_key(event.type==SDL_KEYDOWN,vk,0);}
  if(event.type==SDL_MOUSEMOTION||event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP){
   int l,t,r,b;hle_window_screen_rect(n->hwnd,&l,&t,&r,&b);
   int x=event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x,y=event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y;uint32_t message=0x200;
   if(event.type!=SDL_MOUSEMOTION){unsigned button=event.button.button;message=button==SDL_BUTTON_LEFT?0x201:button==SDL_BUTTON_RIGHT?0x204:button==SDL_BUTTON_MIDDLE?0x207:0;if(!message)continue;if(event.type==SDL_MOUSEBUTTONUP)message++;}
   hle_input_mouse(message,l+x,t+y);
  }
 }
 /* DirectDraw software surfaces can already be shown. OpenGL remains separate. */
 const uint8_t *pixels;const uint32_t *palette;int w,h,pitch,bpp;unsigned sequence;
 if(!hle_dd_frame(&pixels,&w,&h,&pitch,&bpp,&palette,&sequence) || !pixels || w<=0||h<=0)return;
 native_window *n=NULL;for(unsigned i=0;i<32;i++)if(windows[i].hwnd && (hle_window_style(windows[i].hwnd)&0x10000000u)){n=&windows[i];break;}if(!n)return;
 if(bpp!=16 && bpp!=32)hle_fatal("native presentation depth %d is not implemented",bpp);
 if(!n->renderer){n->renderer=SDL_CreateRenderer(n->window,-1,SDL_RENDERER_SOFTWARE);if(!n->renderer)hle_fatal("SDL renderer: %s",SDL_GetError());}
 if(!n->texture||n->tw!=w||n->th!=h||n->tbpp!=bpp){if(n->texture)SDL_DestroyTexture(n->texture);n->texture=SDL_CreateTexture(n->renderer,bpp==16?SDL_PIXELFORMAT_RGB565:SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h);n->tw=w;n->th=h;n->tbpp=bpp;if(!n->texture)hle_fatal("SDL texture: %s",SDL_GetError());}
 if(SDL_UpdateTexture(n->texture,NULL,pixels,pitch))hle_fatal("SDL texture update: %s",SDL_GetError());
 SDL_RenderSetLogicalSize(n->renderer,w,h);SDL_SetRenderDrawColor(n->renderer,0,0,0,255);SDL_RenderClear(n->renderer);SDL_RenderCopy(n->renderer,n->texture,NULL,NULL);SDL_RenderPresent(n->renderer);
 if(!presented++)fprintf(stderr,"[native-window] first DirectDraw presentation %dx%dx%d\n",w,h,bpp);
}
unsigned cs16_native_window_count(void){unsigned count=0;for(unsigned i=0;i<32;i++)if(windows[i].hwnd)count++;return count;}
void cs16_native_window_install(void){hle_native_window_hook=window_changed;hle_set_pump_hook(pump);}
SDL_Window *cs16_native_window(uint32_t hwnd){native_window *n=lookup(hwnd);return n?n->window:NULL;}
void cs16_native_pump(void){pump();}
