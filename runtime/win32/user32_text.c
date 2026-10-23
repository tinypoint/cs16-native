/* ANSI guest formatting, adapted from the MIT HLE identified in UPSTREAM.md. */
#include <stdio.h>
#include <string.h>
#include "win32hle.h"
/* Windows ANSI formatter consumes 32-bit guest argument words, never a host va_list. */
static int guest_format(char *out,const char *fmt,uint32_t args) {
    size_t used=0;
    while(*fmt && used<1023) {
        if(*fmt!='%') {out[used++]=*fmt++;continue;}
        fmt++;if(*fmt=='%'){out[used++]='%';fmt++;continue;}
        char spec[48]="%";size_t k=1;
        while(*fmt && strchr("-+ #0.0123456789",*fmt)) {
            if(k>=sizeof(spec)-3)hle_fatal("format too long");spec[k++]=*fmt++;
        }
        if(*fmt=='l' || *fmt=='h')fmt++; /* Win32 long/int are 32 bits. */
        char c=*fmt++;if(!c)hle_fatal("incomplete guest format");
        spec[k++]=c;spec[k]=0;uint32_t v=MEM32(args);args+=4;
        int n;
        if(c=='s')n=snprintf(out+used,1024-used,spec,v?(char*)gm_ptr(v):"(null)");
        else if(c=='d'||c=='i')n=snprintf(out+used,1024-used,spec,(int32_t)v);
        else if(strchr("uoxXc",c))n=snprintf(out+used,1024-used,spec,v);
        else hle_fatal("unsupported guest format %c",c);
        if(n<0)hle_fatal("format failed");used+=(size_t)n<1024-used?(size_t)n:1023-used;
    }
    out[used]=0;return (int)used;
}
static void u_wsprintfA(void) { int n=guest_format(ASTR(0),ASTR(1),g_esp+12);RET(n,0); }
static void u_wvsprintfA(void) {int n=guest_format(ASTR(0),ASTR(1),A32(2));RET(n,3);}


const win32hle_shim win32hle_user32_text[]={{"wsprintfA",u_wsprintfA},{"wvsprintfA",u_wvsprintfA},{0,0}};
