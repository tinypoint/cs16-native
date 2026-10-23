/* Partial WebBrowser COM ABI backed by system WebKit; no guest engine patches.
 * Each supported interface has its own guest vtable and shared IUnknown identity.
 * Unimplemented methods stop, rather than returning a fabricated success. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "../platform/browser_native.h"
#define S_OK 0u
#define E_NOTIMPL 0x80004001u
#define E_NOINTERFACE 0x80004002u
#define E_POINTER 0x80004003u
#define E_FAIL 0x80004005u
#define E_INVALIDARG 0x80070057u
#define MAX_BROWSERS 16
#define INTERFACES 11
enum {UNKNOWN,VIEW,OLE,PERSIST,INPLACE,BROWSER,CONTAINER,POINT,DOCUMENT,ELEMENT,TEXT};
static const uint8_t clsid[16]={0x61,0xf9,0x56,0x88,0x0a,0x34,0xd0,0x11,0xa9,0x6b,0,0xc0,0x4f,0xd7,5,0xa2};
static const uint8_t ids[INTERFACES][16]={
 {0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46},
 {0x0d,1,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46},
 {0x12,1,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46},
 {0x80,0x23,0xd5,0x7f,7,0x4e,0x1b,0x10,0xae,0x2d,8,0,0x2b,0x2e,0xc7,0x13},
 {0x13,1,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46},
 {0x61,0x16,0x0c,0xd3,0xaf,0xcd,0xd0,0x11,0x8a,0x3e,0,0xc0,0x4f,0xc9,0xe2,0x6e},
 {0x84,0xb2,0x96,0xb1,0xb4,0xba,0x1a,0x10,0xb6,0x9c,0,0xaa,0,0x34,0x1d,7},
 {0x86,0xb2,0x96,0xb1,0xb4,0xba,0x1a,0x10,0xb6,0x9c,0,0xaa,0,0x34,0x1d,7},
 {0x25,0x44,0x2c,0x33,0xcb,0x26,0xd0,0x11,0xb4,0x83,0,0xc0,0x4f,0xd9,1,0x19},
 {0xff,0xf1,0x50,0x30,0xb5,0x98,0xcf,0x11,0xbb,0x82,0,0xaa,0,0xbd,0xce,0x0b},
 {0x30,0xf2,0x50,0x30,0xb5,0x98,0xcf,0x11,0xbb,0x82,0,0xaa,0,0xbd,0xce,0x0b}};
static const uint8_t events_iid[16]={0xa0,0x15,0xa7,0x34,0x87,0x65,0xd0,0x11,0x92,0x4a,0,0x20,0xaf,0xc7,0xac,0x4d};
typedef struct {
 uint32_t object,refs,site,sink,view_sink,view_flags,view_aspect,cookie,hwnd;
 int left,top,width,height,visible,silent,initialized,active,loaded,view_dirty;
 void *native;char url[8192];
} browser_t;
static browser_t browsers[MAX_BROWSERS];
static uint32_t vtables[INTERFACES];
static browser_t *self(void){
 uint32_t p=A32(0);for(unsigned i=0;i<MAX_BROWSERS;i++)if(browsers[i].object && p>=browsers[i].object && p<browsers[i].object+INTERFACES*8 && !((p-browsers[i].object)%8))return &browsers[i];
 hle_fatal("invalid WebBrowser interface %08x",p);return NULL;
}
static uint32_t iface(browser_t *b,unsigned i){return b->object+i*8;}
static uint32_t vcall(uint32_t obj,unsigned slot,unsigned count,const uint32_t *args){
 if(!gm_accessible(obj,4,0)||!gm_accessible(MEM32(obj)+slot*4,4,0))hle_fatal("WebBrowser callback vtable unavailable");
 uint32_t all[12]={obj};if(count>11)hle_fatal("WebBrowser callback argument count");for(unsigned i=0;i<count;i++)all[i+1]=args[i];
 return hle_call_guest(MEM32(MEM32(obj)+slot*4),count+1,all);
}
static void replace_ref(uint32_t *dest,uint32_t value){if(*dest==value)return;if(value)vcall(value,1,0,NULL);uint32_t old=*dest;*dest=value;if(old)vcall(old,2,0,NULL);}
static void dispose(browser_t *b){
 replace_ref(&b->site,0);replace_ref(&b->sink,0);replace_ref(&b->view_sink,0);
 if(b->hwnd){uint32_t a=b->hwnd;hle_call_guest(hle_resolve("DestroyWindow"),1,&a);}
 cs16_browser_native_destroy(b->native);gm_free(gm_ptr(b->object));memset(b,0,sizeof *b);
}
static uint32_t release(browser_t *b){if(!b->refs)hle_fatal("WebBrowser refcount underflow");uint32_t n=--b->refs;if(!n)dispose(b);return n;}
static int match(const uint8_t *a,const uint8_t *b){return !memcmp(a,b,16);}
static int find_interface(const uint8_t *id){
 for(unsigned i=0;i<INTERFACES;i++)if(match(id,ids[i]))return (int)i;
 static const uint8_t dispatch[16]={0,4,2,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
 static const uint8_t olewindow[16]={0x14,1,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
 if(match(id,dispatch))return BROWSER;if(match(id,olewindow))return INPLACE;return -1;
}
static void query(void){
 browser_t *b=self();if(!gm_accessible(A32(2),4,1))RET(E_POINTER,3);MEM32(A32(2))=0;
 if(!gm_accessible(A32(1),16,0))RET(E_POINTER,3);int index=find_interface(APTR(1));
 /* A connection point is its own COM identity; only IUnknown/IConnectionPoint. */
 if(A32(0)==iface(b,POINT)){if(index==UNKNOWN||index==POINT)index=POINT;else index=-1;}
 else if(A32(0)==iface(b,DOCUMENT)){if(index==UNKNOWN||index==BROWSER||index==DOCUMENT)index=DOCUMENT;else index=-1;}
 else if(A32(0)==iface(b,ELEMENT)||A32(0)==iface(b,TEXT)){if(index==UNKNOWN||index==BROWSER)index=ELEMENT;else if(index!=ELEMENT&&index!=TEXT)index=-1;}
 else if(index==POINT||index>=DOCUMENT)index=-1;
 if(index<0){char id[40];hle_guid_string(APTR(1),id);fprintf(stderr,"[webbrowser] unsupported QI %s\n",id);RET(E_NOINTERFACE,3);}
 b->refs++;MEM32(A32(2))=iface(b,(unsigned)index);RET(S_OK,3);
}
static void addref(void){RET(++self()->refs,1);}
static void delref(void){RET(release(self()),1);}
static void unsupported(void){hle_fatal("WebBrowser method not implemented: %s guest=%08x",g_cur_import?g_cur_import:"unknown",g_cur_func);}
static void set_site(void){browser_t *b=self();replace_ref(&b->site,A32(1));RET(S_OK,2);}
static void get_site(void){browser_t *b=self();if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);MEM32(A32(1))=b->site;if(b->site)vcall(b->site,1,0,NULL);RET(S_OK,2);}
static void get_misc(void){self();if(A32(1)!=1)RET(E_INVALIDARG,3);if(!gm_accessible(A32(2),4,1))RET(E_POINTER,3);MEM32(A32(2))=0x20000;RET(S_OK,3);}
static void get_class(void){self();if(!gm_accessible(A32(1),16,1))RET(E_POINTER,2);memcpy(APTR(1),clsid,16);RET(S_OK,2);}
static int resize(browser_t *b,int w,int h){if(w<1||h<1||w>4096||h>4096)return 0;if(b->width==w&&b->height==h)return 1;if(!cs16_browser_native_resize(b->native,w,h))return 0;b->width=w;b->height=h;return 1;}
static void view_notify(browser_t *b){if(b->view_sink){uint32_t args[]={b->view_aspect,(uint32_t)-1};vcall(b->view_sink,4,2,args);if(b->view_flags&4)replace_ref(&b->view_sink,0);}}
static void set_advise(void){browser_t *b=self();if(A32(1)!=1 || (A32(2)&~6u))RET(E_INVALIDARG,4);replace_ref(&b->view_sink,A32(3));b->view_aspect=A32(1);b->view_flags=A32(2);if(b->view_flags&2)view_notify(b);RET(S_OK,4);}
static void get_advise(void){browser_t *b=self();for(unsigned i=1;i<4;i++)if(A32(i)&&!gm_accessible(A32(i),4,1))RET(E_POINTER,4);if(A32(1))MEM32(A32(1))=b->view_aspect;if(A32(2))MEM32(A32(2))=b->view_flags;if(A32(3)){MEM32(A32(3))=b->view_sink;if(b->view_sink)vcall(b->view_sink,1,0,NULL);}RET(S_OK,4);}
static void init_new(void){browser_t *b=self();if(b->initialized)RET(E_FAIL,1);if(!cs16_browser_native_navigate(b->native,"about:blank"))RET(E_FAIL,1);b->initialized=1;strcpy(b->url,"about:blank");RET(S_OK,1);}
static void is_dirty(void){self();RET(1,1);}
static void set_rects(void){browser_t *b=self();if(!gm_accessible(A32(1),16,0)||!gm_accessible(A32(2),16,0))RET(E_POINTER,3);int32_t *r=APTR(1);int64_t w=(int64_t)r[2]-r[0],h=(int64_t)r[3]-r[1];if(w<1||h<1||w>4096||h>4096)RET(E_INVALIDARG,3);if(!resize(b,(int)w,(int)h))RET(E_FAIL,3);b->left=r[0];b->top=r[1];RET(S_OK,3);}
static void do_verb(void){
 browser_t *b=self();int32_t verb=(int32_t)A32(1);if(verb!=-5&&verb!=-4&&verb!=-1&&verb!=0)RET(E_NOTIMPL,7);
 if(!gm_accessible(A32(6),16,0))RET(E_POINTER,7);int32_t *r=APTR(6);int64_t w=(int64_t)r[2]-r[0],h=(int64_t)r[3]-r[1];
 if(w<1||h<1||w>4096||h>4096)RET(E_INVALIDARG,7);if(!resize(b,(int)w,(int)h))RET(E_FAIL,7);
 if(!b->hwnd){uint32_t title=gm_va(gm_strdup("STATIC"));uint32_t args[]={0,title,0,0x40000000,(uint32_t)r[0],(uint32_t)r[1],(uint32_t)w,(uint32_t)h,A32(5),0,0,0};
 b->hwnd=hle_call_guest(hle_resolve("CreateWindowExA"),12,args);gm_free(gm_ptr(title));if(!b->hwnd)RET(E_FAIL,7);}
 b->left=r[0];b->top=r[1];b->active=1;RET(S_OK,7);
}
static void get_window(void){browser_t *b=self();if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);MEM32(A32(1))=b->hwnd;RET(b->hwnd?S_OK:E_FAIL,2);}
static void deactivate(void){browser_t *b=self();b->active=b->visible=0;RET(S_OK,1);}
static void close_object(void){browser_t *b=self();cs16_browser_native_action(b->native,0);b->active=b->visible=0;replace_ref(&b->site,0);RET(S_OK,2);}
static void extent(void){browser_t *b=self();if(A32(1)!=1)RET(E_INVALIDARG,3);if(!gm_accessible(A32(2),8,1))RET(E_POINTER,3);MEM32(A32(2))=(uint32_t)(b->width*2540/96);MEM32(A32(2)+4)=(uint32_t)(b->height*2540/96);RET(S_OK,3);}
static void set_extent(void){browser_t *b=self();if(A32(1)!=1)RET(E_INVALIDARG,3);if(!gm_accessible(A32(2),8,0))RET(E_POINTER,3);int32_t *size=APTR(2);int64_t w=(int64_t)size[0]*96/2540,h=(int64_t)size[1]*96/2540;if(w<1||h<1||w>4096||h>4096)RET(E_INVALIDARG,3);RET(resize(b,(int)w,(int)h)?S_OK:E_FAIL,3);}
static void find_point(void){browser_t *b=self();if(!gm_accessible(A32(2),4,1))RET(E_POINTER,3);MEM32(A32(2))=0;if(!gm_accessible(A32(1),16,0))RET(E_POINTER,3);if(!match(APTR(1),events_iid))RET(0x80040200u,3);MEM32(A32(2))=iface(b,POINT);b->refs++;RET(S_OK,3);}
static void point_interface(void){self();if(!gm_accessible(A32(1),16,1))RET(E_POINTER,2);memcpy(APTR(1),events_iid,16);RET(S_OK,2);}
static void point_container(void){browser_t *b=self();if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);MEM32(A32(1))=iface(b,CONTAINER);b->refs++;RET(S_OK,2);}
static void point_advise(void){browser_t *b=self();if(!gm_accessible(A32(2),4,1))RET(E_POINTER,3);MEM32(A32(2))=0;if(!A32(1))RET(E_POINTER,3);if(b->sink)RET(0x80040201u,3);replace_ref(&b->sink,A32(1));if(!++b->cookie)b->cookie++;MEM32(A32(2))=b->cookie;RET(S_OK,3);}
static void point_unadvise(void){browser_t *b=self();if(!b->sink||A32(1)!=b->cookie)RET(0x80040200u,2);replace_ref(&b->sink,0);RET(S_OK,2);}
static char *bstr_utf8(uint32_t p){
 if(!p||p<4||!gm_accessible(p-4,4,0))return NULL;uint32_t bytes=MEM32(p-4);if(bytes>1024*1024||bytes%2||!gm_accessible(p,bytes+2,0))return NULL;
 char *s=malloc((size_t)bytes*2+1);if(!s)return NULL;unsigned out=0;
 for(unsigned i=0;i<bytes;i+=2){uint32_t c=MEM16(p+i);if(c>=0xd800&&c<=0xdbff){if(i+2>=bytes){free(s);return NULL;}uint32_t low=MEM16(p+i+2);if(low<0xdc00||low>0xdfff){free(s);return NULL;}c=0x10000+((c-0xd800)<<10)+(low-0xdc00);i+=2;}else if(c>=0xdc00&&c<=0xdfff){free(s);return NULL;}
 if(c<0x80)s[out++]=(char)c;else if(c<0x800){s[out++]=(char)(0xc0|(c>>6));s[out++]=(char)(0x80|(c&63));}else if(c<0x10000){s[out++]=(char)(0xe0|(c>>12));s[out++]=(char)(0x80|((c>>6)&63));s[out++]=(char)(0x80|(c&63));}else{s[out++]=(char)(0xf0|(c>>18));s[out++]=(char)(0x80|((c>>12)&63));s[out++]=(char)(0x80|((c>>6)&63));s[out++]=(char)(0x80|(c&63));}}
 s[out]=0;return s;
}
static uint32_t ascii_bstr(const char *text){size_t len=strlen(text);uint32_t p=gm_va(gm_calloc(1,len*2+6));MEM32(p)=(uint32_t)(len*2);for(unsigned i=0;i<len;i++)MEM16(p+4+i*2)=(uint8_t)text[i];return p+4;}
static void navigate(int variant){
 browser_t *b=self();uint32_t url=A32(1);
 if(variant){if(!gm_accessible(url,16,0)||MEM16(url)!=8)RET(E_INVALIDARG,6);url=MEM32(url+8);}
 /* Optional flags/target/post data/headers are not yet supported. */
 for(unsigned i=2;i<6;i++)if(A32(i)){uint32_t p=A32(i);if(!gm_accessible(p,16,0))RET(E_POINTER,6);uint16_t vt=MEM16(p);if(vt && vt!=1 && vt!=10)RET(E_NOTIMPL,6);}
 char *s=bstr_utf8(url);if(!s)RET(E_INVALIDARG,6);if(strlen(s)>=sizeof b->url){free(s);RET(E_INVALIDARG,6);}
 int ok=cs16_browser_native_navigate(b->native,s);if(ok){b->loaded=0;strcpy(b->url,s);b->initialized=1;fprintf(stderr,"[webbrowser] navigation requested %s\n",s);}free(s);RET(ok?S_OK:E_FAIL,6);
}
static void navigate1(void){navigate(0);}
static void navigate2(void){navigate(1);}
static void back(void){RET(cs16_browser_native_action(self()->native,2)?S_OK:E_FAIL,1);}
static void forward(void){RET(cs16_browser_native_action(self()->native,3)?S_OK:E_FAIL,1);}
static void refresh(void){RET(cs16_browser_native_action(self()->native,1)?S_OK:E_FAIL,1);}
static void stop(void){RET(cs16_browser_native_action(self()->native,0)?S_OK:E_FAIL,1);}
static void ready(void){browser_t *b=self();if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);MEM32(A32(1))=cs16_browser_native_busy(b->native)?1:b->loaded?4:b->initialized?1:0;RET(S_OK,2);}
static void busy(void){browser_t *b=self();if(!gm_accessible(A32(1),2,1))RET(E_POINTER,2);MEM16(A32(1))=cs16_browser_native_busy(b->native)?0xffff:0;RET(S_OK,2);}
static void location(void){browser_t *b=self();if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);MEM32(A32(1))=ascii_bstr(b->url);RET(S_OK,2);}
static void get_document(void){browser_t *b=self();if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);MEM32(A32(1))=0;if(!b->loaded)RET(E_FAIL,2);MEM32(A32(1))=iface(b,DOCUMENT);b->refs++;RET(S_OK,2);}
static void get_body(void){browser_t *b=self();if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);MEM32(A32(1))=0;char result[32];if(!cs16_browser_native_script(b->native,"!!document.body",result,sizeof result))RET(E_FAIL,2);if(strcmp(result,"1")&&strcmp(result,"true"))RET(S_OK,2);MEM32(A32(1))=iface(b,ELEMENT);b->refs++;RET(S_OK,2);}
static void scroll_property(void){
 browser_t *b=self();const char *method=strrchr(g_cur_import,'.')+1;const char *field=strstr(method,"Left")?"scrollLeft":strstr(method,"Top")?"scrollTop":strstr(method,"Width")?"scrollWidth":"scrollHeight";char script[256],result[64];
 if(!strncmp(method,"put_",4))snprintf(script,sizeof script,"(()=>{const e=document.scrollingElement,old=e.%s;e.%s=%d;return e.%s!==old})()",field,field,(int32_t)A32(1),field);
 else {if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);snprintf(script,sizeof script,"document.scrollingElement.%s",field);}
 if(!cs16_browser_native_script(b->native,script,result,sizeof result))RET(E_FAIL,2);
 if(!strncmp(method,"get_",4))MEM32(A32(1))=(uint32_t)strtol(result,NULL,10);else if(!strcmp(result,"1")||!strcmp(result,"true"))b->view_dirty=1;RET(S_OK,2);
}
static void typeinfo_count(void){self();if(!gm_accessible(A32(1),4,1))RET(E_POINTER,2);MEM32(A32(1))=0;RET(S_OK,2);}
static void property(void){
 browser_t *b=self();const char *name=g_cur_import?strrchr(g_cur_import,'.'):NULL;if(!name)hle_fatal("WebBrowser property dispatch");name++;
 int get=!strncmp(name,"get_",4);const char *key=name+4;int *value=NULL;
 if(!strcmp(key,"Visible"))value=&b->visible;else if(!strcmp(key,"Silent"))value=&b->silent;else if(!strcmp(key,"Left"))value=&b->left;else if(!strcmp(key,"Top"))value=&b->top;else if(!strcmp(key,"Width"))value=&b->width;else if(!strcmp(key,"Height"))value=&b->height;
 int number=!strcmp(key,"Left")||!strcmp(key,"Top")||!strcmp(key,"Width")||!strcmp(key,"Height")||!strcmp(key,"ToolBar");
 if(get){uint32_t bytes=number?4:2;if(!gm_accessible(A32(1),bytes,1))RET(E_POINTER,2);if(number)MEM32(A32(1))=value?(uint32_t)*value:0;else MEM16(A32(1))=value&&*value?0xffff:0;RET(S_OK,2);}
 int requested=(int32_t)A32(1);
 if(!value){if(requested)RET(E_NOTIMPL,2);RET(S_OK,2);}
 if(!strcmp(key,"Width")){RET(resize(b,requested,b->height)?S_OK:E_INVALIDARG,2);}
 if(!strcmp(key,"Height")){RET(resize(b,b->width,requested)?S_OK:E_INVALIDARG,2);}
 if(!strcmp(key,"Silent")&&!requested)RET(E_NOTIMPL,2);
 *value=number?requested:requested!=0;RET(S_OK,2);
}
extern int cs16_gdi_write_rgba(uint32_t,const uint8_t *,int,int,int,int,int,int);
static void draw(void){browser_t *b=self();if(A32(1)!=1)RET(E_INVALIDARG,11);if(A32(4)||A32(5)||A32(8)||A32(9))RET(E_NOTIMPL,11);if(!gm_accessible(A32(7),16,0))RET(E_POINTER,11);int32_t *r=APTR(7);int64_t w=(int64_t)r[2]-r[0],h=(int64_t)r[3]-r[1];if(w<1||h<1||w>4096||h>4096)RET(E_INVALIDARG,11);
 uint8_t *pixels=malloc((size_t)b->width*b->height*4);if(!pixels)RET(E_FAIL,11);
 int ok=cs16_browser_native_snapshot(b->native,pixels,b->width,b->height);
 if(ok)ok=cs16_gdi_write_rgba(A32(6),pixels,b->width,b->height,r[0],r[1],(int)w,(int)h);free(pixels);RET(ok?S_OK:E_FAIL,11);
}
static void event(browser_t *b,uint32_t dispid){
 if(!b->sink)return;uint32_t data=gm_va(gm_calloc(1,96)),url=ascii_bstr(b->url);
 MEM16(data)=0x400c;MEM32(data+8)=data+32;MEM16(data+16)=9;MEM32(data+24)=iface(b,BROWSER);MEM16(data+32)=8;MEM32(data+40)=url;
 MEM32(data+48)=data;MEM32(data+56)=2; /* DISPPARAMS: args,named,count,namedCount */
 uint32_t args[]={dispid,data+80,0,1,data+48,0,0,0};uint32_t sink=b->sink;vcall(sink,1,0,NULL);vcall(sink,6,8,args);vcall(sink,2,0,NULL);gm_free(gm_ptr(url-4));gm_free(gm_ptr(data));
}
void cs16_webbrowser_pump(void){
 cs16_browser_native_pump();for(unsigned i=0;i<MAX_BROWSERS;i++){browser_t *b=&browsers[i];if(!b->object)continue;unsigned flags=cs16_browser_native_events(b->native);if(!flags&&!b->view_dirty)continue;b->refs++;int dirty=b->view_dirty;b->view_dirty=0;
 if(dirty&&!(flags&1))view_notify(b);
 if(flags&1){b->loaded=1;fprintf(stderr,"[webbrowser] navigation completed\n");view_notify(b);event(b,252);event(b,259);}
 if(flags&2)fprintf(stderr,"[webbrowser] navigation error (no completion event)\n");release(b);}
}
/* Build canonical slot layouts. Unknown entries remain explicit stop shims. */
static uint32_t table(const char *prefix,unsigned count,const char *const *names,void (**functions)(void)){
 win32hle_shim *m=calloc(count+1,sizeof *m);if(!m)hle_fatal("WebBrowser vtable allocation");
 for(unsigned i=0;i<count;i++){char text[128];snprintf(text,sizeof text,"Browser.%s.%s",prefix,names[i]);m[i].name=strdup(text);m[i].fn=functions[i]?functions[i]:unsupported;}
 uint32_t result=hle_com_vtable(m);free(m);return result;
}
static uint32_t sparse_table(const char *prefix,unsigned count,unsigned selected,void (*function)(void)){
 const char **names=calloc(count,sizeof *names);void (**functions)(void)=calloc(count,sizeof *functions);
 for(unsigned i=0;i<count;i++){char text[32];snprintf(text,sizeof text,"slot%u",i);names[i]=strdup(text);}
 functions[0]=query;functions[1]=addref;functions[2]=delref;functions[selected]=function;uint32_t result=table(prefix,count,names,functions);
 for(unsigned i=0;i<count;i++)free((void *)names[i]);free(names);free(functions);return result;
}
static void build_tables(void){
 vtables[DOCUMENT]=sparse_table("IHTMLDocument2",116,9,get_body);vtables[ELEMENT]=sparse_table("IHTMLElement",94,0,query);
 static const char *text[]={"QueryInterface","AddRef","Release","GetTypeInfoCount","GetTypeInfo","GetIDsOfNames","Invoke","createControlRange","get_scrollHeight","get_scrollWidth","put_scrollTop","get_scrollTop","put_scrollLeft","get_scrollLeft","put_onscroll","get_onscroll"};
 void (*text_fn[16])(void)={query,addref,delref};for(unsigned i=8;i<14;i++)text_fn[i]=scroll_property;vtables[TEXT]=table("IHTMLTextContainer",16,text,text_fn);
 static const char *basic[]={"QueryInterface","AddRef","Release"};void (*basic_fn[])(void)={query,addref,delref};vtables[UNKNOWN]=table("IUnknown",3,basic,basic_fn);
 static const char *view[]={"QueryInterface","AddRef","Release","Draw","GetColorSet","Freeze","Unfreeze","SetAdvise","GetAdvise"};void (*view_fn[])(void)={query,addref,delref,draw,0,0,0,set_advise,get_advise};vtables[VIEW]=table("IViewObject",9,view,view_fn);
 static const char *ole[]={"QueryInterface","AddRef","Release","SetClientSite","GetClientSite","SetHostNames","Close","SetMoniker","GetMoniker","InitFromData","GetClipboardData","DoVerb","EnumVerbs","Update","IsUpToDate","GetUserClassID","GetUserType","SetExtent","GetExtent","Advise","Unadvise","EnumAdvise","GetMiscStatus","SetColorScheme"};void (*ole_fn[24])(void)={query,addref,delref,set_site,get_site,0,close_object,0,0,0,0,do_verb,0,0,0,get_class,0,set_extent,extent,0,0,0,get_misc,0};vtables[OLE]=table("IOleObject",24,ole,ole_fn);
 static const char *persist[]={"QueryInterface","AddRef","Release","GetClassID","IsDirty","Load","Save","GetSizeMax","InitNew"};void (*persist_fn[])(void)={query,addref,delref,get_class,is_dirty,0,0,0,init_new};vtables[PERSIST]=table("IPersistStreamInit",9,persist,persist_fn);
 static const char *place[]={"QueryInterface","AddRef","Release","GetWindow","ContextSensitiveHelp","InPlaceDeactivate","UIDeactivate","SetObjectRects","ReactivateAndUndo"};void (*place_fn[])(void)={query,addref,delref,get_window,0,deactivate,deactivate,set_rects,0};vtables[INPLACE]=table("IOleInPlaceObject",9,place,place_fn);
 static const char *container[]={"QueryInterface","AddRef","Release","EnumConnectionPoints","FindConnectionPoint"};void (*container_fn[])(void)={query,addref,delref,0,find_point};vtables[CONTAINER]=table("IConnectionPointContainer",5,container,container_fn);
 static const char *point[]={"QueryInterface","AddRef","Release","GetConnectionInterface","GetConnectionPointContainer","Advise","Unadvise","EnumConnections"};void (*point_fn[])(void)={query,addref,delref,point_interface,point_container,point_advise,point_unadvise,0};vtables[POINT]=table("IConnectionPoint",8,point,point_fn);
 static const char *web[]={"QueryInterface","AddRef","Release","GetTypeInfoCount","GetTypeInfo","GetIDsOfNames","Invoke","GoBack","GoForward","GoHome","GoSearch","Navigate","Refresh","Refresh2","Stop","get_Application","get_Parent","get_Container","get_Document","get_TopLevelContainer","get_Type","get_Left","put_Left","get_Top","put_Top","get_Width","put_Width","get_Height","put_Height","get_LocationName","get_LocationURL","get_Busy","Quit","ClientToWindow","PutProperty","GetProperty","get_Name","get_HWND","get_FullName","get_Path","get_Visible","put_Visible","get_StatusBar","put_StatusBar","get_StatusText","put_StatusText","get_ToolBar","put_ToolBar","get_MenuBar","put_MenuBar","get_FullScreen","put_FullScreen","Navigate2","QueryStatusWB","ExecWB","ShowBrowserBar","get_ReadyState","get_Offline","put_Offline","get_Silent","put_Silent","get_RegisterAsBrowser","put_RegisterAsBrowser","get_RegisterAsDropTarget","put_RegisterAsDropTarget","get_TheaterMode","put_TheaterMode","get_AddressBar","put_AddressBar","get_Resizable","put_Resizable"};
 void (*web_fn[71])(void)={0};web_fn[0]=query;web_fn[1]=addref;web_fn[2]=delref;web_fn[3]=typeinfo_count;web_fn[7]=back;web_fn[8]=forward;web_fn[11]=navigate1;web_fn[12]=refresh;web_fn[14]=stop;web_fn[18]=get_document;web_fn[30]=location;web_fn[31]=busy;web_fn[37]=get_window;web_fn[52]=navigate2;web_fn[56]=ready;
 for(unsigned i=21;i<=28;i++)web_fn[i]=property;for(unsigned i=40;i<=51;i++)if(i!=44&&i!=45)web_fn[i]=property;for(unsigned i=57;i<=70;i++)web_fn[i]=property;vtables[BROWSER]=table("IWebBrowser2",71,web,web_fn);
}
static uint32_t create(const uint8_t *iid,uint32_t *out){
 if(!out||!iid)return E_POINTER;*out=0;int index=find_interface(iid);if(index<0||index>=POINT)return E_NOINTERFACE;
 browser_t *b=NULL;for(unsigned i=0;i<MAX_BROWSERS;i++)if(!browsers[i].object){b=&browsers[i];break;}if(!b)return 0x8007000e;
 void *native=cs16_browser_native_create();if(!native)return E_FAIL;
 b->object=gm_va(gm_calloc(INTERFACES,8));if(!b->object){cs16_browser_native_destroy(native);return 0x8007000e;}b->refs=1;b->native=native;b->width=320;b->height=240;b->silent=1;
 for(unsigned i=0;i<INTERFACES;i++)MEM32(iface(b,i))=vtables[i];*out=iface(b,(unsigned)index);return S_OK;
}
void cs16_webbrowser_install(void){build_tables();hle_com_register_class(clsid,create);}

/* Integration fixture: public COM entry -> real WebKit -> guest DIB pixels. */
#include <unistd.h>
#include <time.h>
static unsigned probe_references=1,probe_views,probe_documents,probe_navigations;
static int probe_callback_error;
static uint32_t probe_scroll_object;
static void probe_addref(void){RET(++probe_references,1);}
static void probe_release(void){if(probe_references<2)probe_callback_error=1;RET(--probe_references,1);}
static void probe_view(void){if(A32(1)!=1||(int32_t)A32(2)!=-1)probe_callback_error=1;probe_views++;if(probe_scroll_object){uint32_t scroll=25;if(vcall(probe_scroll_object,10,1,&scroll))probe_callback_error=1;}RET(0,3);}
static void probe_invoke(void){
 uint32_t params=A32(5);if(!gm_accessible(params,16,0)||MEM32(params+8)!=2)probe_callback_error=1;
 else {uint32_t args=MEM32(params);if(!gm_accessible(args,32,0)||MEM16(args)!=0x400c||MEM16(args+16)!=9)probe_callback_error=1;}
 if(A32(1)==252)probe_navigations++;else if(A32(1)==259)probe_documents++;else probe_callback_error=1;RET(0,9);
}
static const win32hle_shim probe_methods[]={{"BrowserProbe.QI",unsupported},{"BrowserProbe.AddRef",probe_addref},{"BrowserProbe.Release",probe_release},{"BrowserProbe.TypeCount",unsupported},{"BrowserProbe.ViewChange",probe_view},{"BrowserProbe.Names",unsupported},{"BrowserProbe.Invoke",probe_invoke},{0,0}};
#define B_CHECK(c) do {if(!(c)){fprintf(stderr,"[browser-probe] failed line %d: %s\n",__LINE__,#c);return 12;}}while(0)
static uint32_t system_call(const char *name,unsigned count,uint32_t *a){return hle_call_guest(hle_resolve(name),(int)count,a);}
static int wait_loaded(browser_t *b){
 struct timespec begin,now;clock_gettime(CLOCK_MONOTONIC,&begin);
 do {cs16_webbrowser_pump();if(b->loaded)return 1;clock_gettime(CLOCK_MONOTONIC,&now);}while(now.tv_sec-begin.tv_sec<6);return 0;
}
int cs16_probe_browser(void){
 uint32_t ids_guest=gm_va(gm_alloc(sizeof ids)),class_guest=gm_va(gm_alloc(16)),out=gm_va(gm_calloc(1,4));memcpy(gm_ptr(ids_guest),ids,sizeof ids);memcpy(gm_ptr(class_guest),clsid,16);
 uint32_t args[12]={class_guest,1,7,ids_guest,out};B_CHECK(system_call("CoCreateInstance",5,args)==0x80040110u&&!MEM32(out));args[1]=0;B_CHECK(!system_call("CoCreateInstance",5,args));uint32_t root=MEM32(out);B_CHECK(root);
 browser_t *b=NULL;for(unsigned i=0;i<MAX_BROWSERS;i++)if(browsers[i].object==root)b=&browsers[i];B_CHECK(b);
 for(unsigned i=1;i<INTERFACES;i++)if(i!=POINT){uint32_t a[]={ids_guest+i*16,out};uint32_t hr=vcall(root,0,2,a);if(i>=DOCUMENT){B_CHECK(hr==E_NOINTERFACE&&!MEM32(out));continue;}B_CHECK(!hr);uint32_t obj=MEM32(out);a[0]=ids_guest;B_CHECK(!vcall(obj,0,2,a)&&MEM32(out)==root);vcall(root,2,0,NULL);vcall(obj,2,0,NULL);}
 uint32_t unknown=gm_va(gm_calloc(1,16));MEM32(unknown)=0xdecafbad;uint32_t qi[]={unknown,out};B_CHECK(vcall(root,0,2,qi)==E_NOINTERFACE&&!MEM32(out));
 uint32_t ready_out[]={out};B_CHECK(!vcall(iface(b,BROWSER),56,1,ready_out)&&MEM32(out)==0);
 B_CHECK(vcall(iface(b,BROWSER),18,1,ready_out)==E_FAIL&&!MEM32(out));
 B_CHECK(!vcall(iface(b,PERSIST),8,0,NULL));B_CHECK(wait_loaded(b));
 B_CHECK(!vcall(iface(b,BROWSER),56,1,ready_out)&&MEM32(out)==4);
 uint32_t docout[]={out};B_CHECK(!vcall(iface(b,BROWSER),18,1,docout)&&MEM32(out)==iface(b,DOCUMENT));uint32_t doc=MEM32(out);
 qi[0]=ids_guest+DOCUMENT*16;B_CHECK(!vcall(doc,0,2,qi)&&MEM32(out)==doc);vcall(doc,2,0,NULL);vcall(doc,2,0,NULL);
 uint32_t sink=hle_com_new(hle_com_vtable(probe_methods),8);uint32_t advise[]={1,2,sink};B_CHECK(!vcall(iface(b,VIEW),7,3,advise));B_CHECK(probe_views==1&&probe_references==2);
 uint32_t event_id=gm_va(gm_alloc(16));memcpy(gm_ptr(event_id),events_iid,16);uint32_t point_args[]={event_id,out};B_CHECK(!vcall(iface(b,CONTAINER),4,2,point_args));uint32_t cp=MEM32(out);
 uint32_t cp_advise[]={sink,out};B_CHECK(!vcall(cp,5,2,cp_advise));uint32_t cookie=MEM32(out);B_CHECK(cookie&&probe_references==3);
 uint32_t rect=gm_va(gm_calloc(1,16));MEM32(rect+8)=160;MEM32(rect+12)=120;uint32_t rects[]={rect,rect};B_CHECK(!vcall(iface(b,INPLACE),7,2,rects));
 char directory[]="/tmp/cs16-browser-probe-XXXXXX";B_CHECK(mkdtemp(directory));char path[1024],url[1100];snprintf(path,sizeof path,"%s/test.html",directory);FILE *f=fopen(path,"w");B_CHECK(f);
 fputs("<!doctype html><meta charset='utf-8'><title>CS16 WebKit probe</title><style>html,body{margin:0;width:100%;height:100%}#top{height:60px;background:#ff0000}#bottom{height:300px;background:#0000ff}</style><div id='top'></div><div id='bottom'></div>",f);B_CHECK(!fclose(f));snprintf(url,sizeof url,"file://%s",path);
 uint32_t url_bstr=ascii_bstr(url),nav[]={url_bstr,0,0,0,0};B_CHECK(!vcall(iface(b,BROWSER),11,5,nav));B_CHECK(wait_loaded(b));B_CHECK(probe_navigations==1&&probe_documents==1&&!probe_callback_error);
 char title[128];B_CHECK(cs16_browser_native_script(b->native,"document.title",title,sizeof title));B_CHECK(!strcmp(title,"CS16 WebKit probe"));
 B_CHECK(!vcall(iface(b,DOCUMENT),9,1,docout));uint32_t body=MEM32(out);qi[0]=ids_guest+TEXT*16;B_CHECK(!vcall(body,0,2,qi));uint32_t text=MEM32(out);vcall(body,2,0,NULL);
 uint32_t scrolling[]={25};unsigned before=probe_views;probe_scroll_object=text;B_CHECK(!vcall(text,10,1,scrolling));B_CHECK(probe_views==before);cs16_webbrowser_pump();B_CHECK(probe_views==before+1&&!probe_callback_error);cs16_webbrowser_pump();B_CHECK(probe_views==before+1);probe_scroll_object=0;scrolling[0]=out;B_CHECK(!vcall(text,11,1,scrolling)&&MEM32(out)==25);scrolling[0]=0;B_CHECK(!vcall(text,10,1,scrolling));vcall(text,2,0,NULL);
 uint32_t null_dc=0;uint32_t dc=system_call("CreateCompatibleDC",1,&null_dc);B_CHECK(dc);uint32_t bmi=gm_va(gm_calloc(1,40)),bits=gm_va(gm_calloc(1,4));MEM32(bmi)=40;MEM32(bmi+4)=160;MEM32(bmi+8)=(uint32_t)-120;MEM16(bmi+12)=1;MEM16(bmi+14)=32;
 uint32_t make[]={dc,bmi,0,bits,0,0};uint32_t dib=system_call("CreateDIBSection",6,make);B_CHECK(dib&&MEM32(bits));uint32_t sel[]={dc,dib};uint32_t old=system_call("SelectObject",2,sel);
 uint32_t draw_args[]={1,(uint32_t)-1,0,0,0,dc,rect,0,0,0};B_CHECK(!vcall(iface(b,VIEW),3,10,draw_args));
 uint32_t top=MEM32(MEM32(bits)+30*160*4+80*4),bottom=MEM32(MEM32(bits)+90*160*4+80*4);
 fprintf(stderr,"[browser-probe] WebKit pixels top=%08x bottom=%08x\n",top,bottom);B_CHECK((top&0xffffff)==0xff0000&&(bottom&0xffffff)==0x0000ff);
 sel[1]=old;system_call("SelectObject",2,sel);system_call("DeleteObject",1,&dib);system_call("DeleteDC",1,&dc);
 B_CHECK(!vcall(cp,6,1,&cookie));B_CHECK(vcall(cp,6,1,&cookie)==0x80040200u);vcall(cp,2,0,NULL);advise[1]=0;advise[2]=0;B_CHECK(!vcall(iface(b,VIEW),7,3,advise));B_CHECK(probe_references==1);
 B_CHECK(vcall(root,2,0,NULL)==0);for(unsigned i=0;i<MAX_BROWSERS;i++)B_CHECK(!browsers[i].object);
 gm_free(gm_ptr(url_bstr-4));gm_free(gm_ptr(sink));unlink(path);rmdir(directory);
 printf("{\"browserProbe\":true,\"systemWebKit\":true,\"comIdentityAndReferences\":true,\"unknownInterfaceRejected\":true,\"realLocalDocumentLoaded\":true,\"domTitleVerified\":true,\"domScrollRoundtrip\":true,\"scrollNotificationsDeferredAndIdempotent\":true,\"nativeNavigationEvents\":true,\"viewAdviseLifecycle\":true,\"topDownDibPixels\":true,\"topPixel\":%u,\"bottomPixel\":%u,\"objectsReleased\":true}\n",top,bottom);return 0;
}
