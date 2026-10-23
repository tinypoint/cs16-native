/* Targeted integration checks for guest ABI, shared pixels and native HWNDs. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "win32hle.h"
#include "cs16_dispatch.h"
extern unsigned cs16_native_window_count(void);
extern int hle_gdi_dib_view(uint32_t,uint8_t **,int *,int *,int *,int *);
static uint32_t call(const char *name,int count,const uint32_t *args){return hle_call_guest(hle_resolve(name),count,args);}
#define CHECK(condition) do{if(!(condition)){fprintf(stderr,"[ui-probe] failed at line %d: %s\n",__LINE__,#condition);return 10;}}while(0)
static unsigned enum_calls;
static int enum_invalid;
static void enum_probe_callback(void){
 uint32_t elf=A32(0),metrics=A32(1);
 enum_calls++;
 if(!gm_accessible(elf,188,0) || !gm_accessible(metrics,96,0) ||
    strcmp(gm_ptr(elf+28),"Arial") || (int32_t)MEM32(metrics)<=0 ||
    MEM8(metrics+44)!=32 || MEM8(metrics+45)!=255 ||
    MEM8(metrics+46)!='?' || A32(2)!=4 || A32(3)!=0x12345678)enum_invalid=1;
 RET(0,4); /* Enum must stop when the callback returns zero. */
}
static const win32hle_shim enum_probe_methods[]={{"UI.EnumProbe",enum_probe_callback},{0,0}};
int cs16_probe_ui(void){
 uint32_t args[14]={0},cursor;
 args[1]=32512;cursor=call("LoadCursorA",2,args);CHECK(cursor);
 args[1]=32513;CHECK(call("LoadCursorA",2,args)!=cursor);
 args[1]=1;CHECK(!call("LoadCursorA",2,args));
 uint32_t cls=gm_va(gm_strdup("CS16.NativeProbe")),wc=gm_va(gm_calloc(1,40));
 MEM32(wc+4)=hle_resolve("DefWindowProcA");MEM32(wc+16)=0x1400000;MEM32(wc+36)=cls;
 args[0]=wc;uint32_t atom=call("RegisterClassA",1,args);CHECK(atom);
 memset(args,0,sizeof args);args[1]=cls;args[2]=cls;args[6]=320;args[7]=240;args[10]=0x1400000;
 uint32_t hwnd=call("CreateWindowExA",12,args);CHECK(hwnd && cs16_native_window_count()==1);
 uint32_t owner_out=gm_va(gm_calloc(1,12));MEM32(owner_out)=MEM32(owner_out+8)=0xcafebabe;
 args[0]=hwnd;args[1]=owner_out+4;CHECK(call("GetWindowThreadProcessId",2,args)==hle_thread_id());CHECK(MEM32(owner_out+4)==(uint32_t)getpid()&&MEM32(owner_out)==0xcafebabe&&MEM32(owner_out+8)==0xcafebabe);args[0]=0xdeadbeef;CHECK(!call("GetWindowThreadProcessId",2,args));
 args[0]=hwnd;uint32_t window_dc=call("GetDC",1,args);CHECK(window_dc);
 args[0]=window_dc;args[1]=0;CHECK(call("ChoosePixelFormat",2,args)==1);
 uint32_t context=call("wglCreateContext",1,args);CHECK(context);
 args[0]=window_dc;args[1]=context;CHECK(call("wglMakeCurrent",2,args));
 args[0]=0;args[1]=0;args[2]=320;args[3]=240;call("glViewport",4,args);
 args[0]=0x3f800000;args[1]=0;args[2]=0;args[3]=0x3f800000;call("glClearColor",4,args);
 args[0]=0x4000;call("glClear",1,args);
 args[0]=0;args[1]=0x3f800000;args[2]=0;call("glColor3f",3,args);
 args[0]=4;call("glBegin",1,args);
 args[0]=args[1]=0xbf800000;args[2]=0;call("glVertex3f",3,args);
 args[0]=0x3f800000;call("glVertex3f",3,args);
 args[0]=0;args[1]=0x3f800000;call("glVertex3f",3,args);call("glEnd",0,NULL);
 uint32_t rgba=gm_va(gm_calloc(1,4));args[0]=160;args[1]=120;args[2]=args[3]=1;args[4]=0x1908;args[5]=0x1401;args[6]=rgba;call("glReadPixels",7,args);
 CHECK(MEM8(rgba)==0 && MEM8(rgba+1)==255 && MEM8(rgba+2)==0 && MEM8(rgba+3)==255);CHECK(!call("glGetError",0,NULL));
 /* Guest-owned interleaved positions/UVs and byte colors, then indexed draw. */
 uint32_t vertices=gm_va(gm_calloc(12,4)),colors=gm_va(gm_alloc(12)),indices=gm_va(gm_alloc(6));
 float triangle[]={-1,-1,0,0,1,-1,1,0,0,1,0.5f,1};memcpy(gm_ptr(vertices),triangle,sizeof triangle);
 for(unsigned i=0;i<3;i++){MEM32(colors+4*i)=0xff0000ff;MEM16(indices+2*i)=2-i;}
 args[0]=2;args[1]=0x1406;args[2]=16;args[3]=vertices;call("glVertexPointer",4,args);args[3]=vertices+8;call("glTexCoordPointer",4,args);
 args[0]=4;args[1]=0x1401;args[2]=0;args[3]=colors;call("glColorPointer",4,args);
 args[0]=0x8074;call("glEnableClientState",1,args);args[0]=0x8078;call("glEnableClientState",1,args);args[0]=0x8076;call("glEnableClientState",1,args);
 args[0]=args[1]=args[2]=0;args[3]=0x3f800000;call("glClearColor",4,args);args[0]=0x4000;call("glClear",1,args);
 args[0]=4;args[1]=0;args[2]=3;call("glDrawArrays",3,args);
 args[0]=160;args[1]=120;args[2]=args[3]=1;args[4]=0x1908;args[5]=0x1401;args[6]=rgba;call("glReadPixels",7,args);CHECK(MEM8(rgba)==255&&!MEM8(rgba+1)&&!MEM8(rgba+2));
 for(unsigned i=0;i<3;i++)MEM32(colors+4*i)=0xffff0000;
 args[0]=4;args[1]=3;args[2]=0x1403;args[3]=indices;call("glDrawElements",4,args);
 args[0]=160;args[1]=120;args[2]=args[3]=1;args[4]=0x1908;args[5]=0x1401;args[6]=rgba;call("glReadPixels",7,args);CHECK(!MEM8(rgba)&&!MEM8(rgba+1)&&MEM8(rgba+2)==255);CHECK(!call("glGetError",0,NULL));
 args[0]=0x8074;call("glDisableClientState",1,args);args[0]=0x8078;call("glDisableClientState",1,args);args[0]=0x8076;call("glDisableClientState",1,args);
 gm_free(gm_ptr(vertices));gm_free(gm_ptr(colors));gm_free(gm_ptr(indices));
 args[0]=0x1701;call("glMatrixMode",1,args);call("glLoadIdentity",0,NULL);
 double bounds[]={-2,2,-2,2,-1,1};memcpy(args,bounds,sizeof bounds);call("glOrtho",12,args);
 uint32_t matrix=gm_va(gm_calloc(16,4));args[0]=0x0ba7;args[1]=matrix;call("glGetFloatv",2,args);
 CHECK(MEM32(matrix)==0x3f000000 && MEM32(matrix+20)==0x3f000000 && MEM32(matrix+40)==0xbf800000);
 args[0]=context;CHECK(call("wglDeleteContext",1,args));args[0]=hwnd;args[1]=window_dc;CHECK(call("ReleaseDC",2,args));
 args[0]=cls;args[1]=0x1400000;CHECK(!call("UnregisterClassA",2,args));
 args[0]=hwnd;CHECK(call("DestroyWindow",1,args));CHECK(cs16_native_window_count()==0);
 args[0]=cls;args[1]=0x1400000;CHECK(call("UnregisterClassA",2,args));
 memset(args,0,sizeof args);uint32_t dc=call("CreateCompatibleDC",1,args);CHECK(dc);
 args[0]=(uint32_t)-16;args[4]=400;args[13]=gm_va(gm_strdup("Arial"));uint32_t font=call("CreateFontA",14,args);CHECK(font);
 args[0]=dc;args[1]=font;uint32_t oldfont=call("SelectObject",2,args);CHECK(oldfont);
 uint32_t abc=gm_va(gm_calloc(1,20));MEM32(abc)=MEM32(abc+16)=0xcafebabe;
 args[0]=dc;args[1]=args[2]='A';args[3]=abc+4;CHECK(call("GetCharABCWidthsA",4,args));
 CHECK(MEM32(abc)==0xcafebabe && MEM32(abc+16)==0xcafebabe && (int32_t)(MEM32(abc+4)+MEM32(abc+8)+MEM32(abc+12))>0);
 uint32_t ansi_abc[3];memcpy(ansi_abc,gm_ptr(abc+4),12);
 CHECK(call("GetCharABCWidthsW",4,args));CHECK(!memcmp(ansi_abc,gm_ptr(abc+4),12));
 args[1]=0x03a9;args[2]=0x03a9;CHECK(call("GetCharABCWidthsW",4,args));
 CHECK(MEM32(abc)==0xcafebabe && MEM32(abc+16)==0xcafebabe);
 args[1]=0x10000;args[2]=0x10000;CHECK(!call("GetCharABCWidthsW",4,args));
 uint32_t metrics=gm_va(gm_calloc(1,64));MEM32(metrics)=MEM32(metrics+60)=0xcafebabe;
 args[0]=dc;args[1]=metrics+4;CHECK(call("GetTextMetricsA",2,args));
 CHECK(MEM32(metrics)==0xcafebabe && MEM32(metrics+60)==0xcafebabe);
 CHECK(MEM8(metrics+4+44)==32 && MEM8(metrics+4+45)==255 && MEM8(metrics+4+46)=='?' && MEM8(metrics+4+47)==32);
 uint32_t lf=gm_va(gm_calloc(1,60));strcpy(gm_ptr(lf+28),"Arial");
 win32hle_register(enum_probe_methods);args[0]=dc;args[1]=lf;args[2]=hle_resolve("UI.EnumProbe");args[3]=0x12345678;args[4]=0;
 CHECK(!call("EnumFontFamiliesExA",5,args));CHECK(enum_calls==1 && !enum_invalid);
 strcpy(gm_ptr(lf+28),"NoSuchFont-CS16-Probe");CHECK(call("EnumFontFamiliesExA",5,args)==1);CHECK(enum_calls==1);
 args[0]=dc;args[1]=1;CHECK(call("SetMapMode",2,args)==1);args[1]=8;CHECK(!call("SetMapMode",2,args));
 uint32_t points=gm_va(gm_calloc(1,24));MEM32(points)=MEM32(points+20)=0xcafebabe;
 MEM32(points+4)=(uint32_t)-23;MEM32(points+8)=41;MEM32(points+12)=17;MEM32(points+16)=(uint32_t)-5;
 uint32_t point_copy[4];memcpy(point_copy,gm_ptr(points+4),16);
 args[0]=dc;args[1]=points+4;args[2]=2;CHECK(call("LPtoDP",3,args));CHECK(call("DPtoLP",3,args));
 CHECK(!memcmp(point_copy,gm_ptr(points+4),16) && MEM32(points)==0xcafebabe && MEM32(points+20)==0xcafebabe);
 args[2]=0x20000000;CHECK(!call("LPtoDP",3,args));
 unsigned changed[2]={0};
 for(int bottom=0;bottom<2;bottom++){
  uint32_t bmi=gm_va(gm_calloc(1,40)),pointer=gm_va(gm_calloc(1,4));MEM32(bmi)=40;MEM32(bmi+4)=64;MEM32(bmi+8)=bottom?32:(uint32_t)-32;MEM16(bmi+12)=1;MEM16(bmi+14)=32;
  uint32_t make[]={dc,bmi,0,pointer,0,0};uint32_t bitmap=call("CreateDIBSection",6,make);CHECK(bitmap && MEM32(pointer));
  args[0]=dc;args[1]=bitmap;uint32_t old=call("SelectObject",2,args);CHECK(old);
  args[0]=dc;args[1]=0;args[2]=0;args[3]=0x0000ff;CHECK(call("SetPixel",4,args)==0x0000ff);
  CHECK(MEM32(MEM32(pointer)+(bottom?31*64*4:0))==0x00ff0000);
  memset(gm_ptr(MEM32(pointer)),0,64*32*4);
  args[0]=dc;args[1]=1;call("SetBkMode",2,args);args[1]=0xffffff;call("SetTextColor",2,args);
  args[0]=dc;args[1]=2;args[2]=2;args[3]=gm_va(gm_strdup("CS"));args[4]=2;CHECK(call("TextOutA",5,args));
  for(unsigned i=0;i<64*32;i++)if(MEM32(MEM32(pointer)+i*4))changed[bottom]++;
  CHECK(changed[bottom]>10);
  memset(gm_ptr(MEM32(pointer)),0,64*32*4);
  uint32_t wide=gm_va(gm_calloc(2,2)),clip=gm_va(gm_calloc(4,4));MEM16(wide)=0x03a9;MEM32(clip)=4;MEM32(clip+4)=3;MEM32(clip+8)=12;MEM32(clip+12)=20;
  uint32_t ext[]={dc,2,2,4,clip,wide,1,0};CHECK(call("ExtTextOutW",8,ext));unsigned ink=0;
  for(unsigned y=0;y<32;y++)for(unsigned x=0;x<64;x++)if(MEM32(MEM32(pointer)+((bottom?31-y:y)*64+x)*4)){CHECK(x>=4&&x<12&&y>=3&&y<20);ink++;}CHECK(ink>0);
  ext[3]=0x10;CHECK(!call("ExtTextOutW",8,ext));ext[3]=0;MEM16(wide)=0xd800;CHECK(!call("ExtTextOutW",8,ext));
  args[0]=dc;args[1]=0x0000ff;call("SetBkColor",2,args);ext[3]=2;ext[5]=0;ext[6]=0;CHECK(call("ExtTextOutW",8,ext));
  args[0]=dc;args[1]=5;args[2]=4;CHECK(call("GetPixel",3,args)==0x0000ff);
  gm_free(gm_ptr(wide));gm_free(gm_ptr(clip));

  args[0]=dc;args[1]=old;CHECK(call("SelectObject",2,args)==bitmap);args[0]=bitmap;CHECK(call("DeleteObject",1,args));
  uint8_t *pixels;int w,h,pitch,bpp;CHECK(!hle_gdi_dib_view(bitmap,&pixels,&w,&h,&pitch,&bpp));
 }
 CHECK(changed[0]==changed[1]);args[0]=dc;args[1]=oldfont;call("SelectObject",2,args);args[0]=font;CHECK(call("DeleteObject",1,args));args[0]=dc;CHECK(call("DeleteDC",1,args));
 uint32_t provider_out=gm_va(gm_calloc(1,4));args[0]=provider_out;args[1]=args[2]=0;args[3]=1;args[4]=0xf0000000u;CHECK(call("CryptAcquireContextA",5,args));
 uint32_t provider=MEM32(provider_out),random=gm_va(gm_calloc(1,72));MEM32(random)=MEM32(random+68)=0xcafebabe;
 args[0]=provider;args[1]=64;args[2]=random+4;CHECK(call("CryptGenRandom",3,args));CHECK(MEM32(random)==0xcafebabe && MEM32(random+68)==0xcafebabe);
 args[0]=provider;args[1]=0;CHECK(call("CryptReleaseContext",2,args));CHECK(!call("CryptReleaseContext",2,args));
 printf("{\"uiProbe\":true,\"nativeWindowLifecycle\":true,\"classLifecycle\":true,\"openGLTriangleReadback\":true,\"guestClientArraysAndIndexedPixels\":true,\"openGLDoubleArguments\":true,\"randomProviderLifecycleAndBounds\":true,\"distinctSystemCursors\":true,\"abcBounds\":true,\"unicodeAbcBounds\":true,\"textMetricLayout\":true,\"fontEnumerationCallback\":true,\"mmTextCoordinateRoundtrip\":true,\"extUnicodeClippingAndOpaqueRect\":true,\"sharedDibPixels\":true,\"topDownInkPixels\":%u,\"bottomUpInkPixels\":%u}\n",changed[0],changed[1]);return 0;
}
int cs16_probe_x87(void){
 static const uint16_t controls[]={0x27f,0x37f,0xb7f,0xf7f,0x240};
 mach_enter();
 printf("{\"x87Probe\":true,\"originalFunction\":\"0x01dd0824\",\"cases\":[");
 for(unsigned i=0;i<sizeof controls/sizeof *controls;i++){
  cs16_x87_init();g_fpu_cw=controls[i];memset(gm_ptr(0x1ee0004),0,28);
  PUSH32(g_esp,RECOMP_RETADDR);cs16_transfer_target(0x1dd0824)();
  CHECK(g_fpu_cw==(uint16_t)(controls[i]|0x3f));
  printf("%s{\"inputControl\":%u,\"controlAfter\":%u,\"environmentHex\":\"",i?",":"",controls[i],g_fpu_cw);
  for(unsigned b=0;b<28;b++)printf("%02x",MEM8(0x1ee0004+b));printf("\"}");
 }printf("]}\n");mach_leave();return 0;
}
