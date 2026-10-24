/* Sparse PE32 addresses backed by compact allocations for Wasm32.
 * Original module addresses are unchanged. Pages within one reservation map
 * contiguously, but distinct reservations need not be adjacent in linear memory.
 * Protection is checked by gm_accessible; raw lifted accesses do not enforce
 * write protection. There is no native mmap/mprotect isolation on this target. */
#include "guest_memory.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define PAGE 4096u
#define NPAGES (1u<<20)
#define MAX_ALLOCS 32768u
static uint32_t *owner,*spans;
static unsigned char *protection;
static unsigned char **pages;
static size_t page=PAGE,npages=NPAGES;
typedef struct {uintptr_t host;uint32_t va,bytes;} allocation;
static allocation allocations[MAX_ALLOCS];static unsigned allocation_count;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int initialized;
static void setup(void){
 owner=calloc(npages,sizeof *owner);spans=calloc(npages,sizeof *spans);
 protection=calloc(npages,1);pages=calloc(npages,sizeof *pages);
 initialized=owner&&spans&&protection&&pages;
 if(!initialized){free(owner);free(spans);free(protection);free(pages);}
}
int gm_init(void){pthread_once(&once,setup);return initialized;}
size_t gm_page_size(void){return gm_init()?page:0;}
extern void cs16_web_record_fatal(const char *,uint32_t) __attribute__((weak));
static void fail(const char *what){fprintf(stderr,"[guest-memory-web] %s\n",what);if(cs16_web_record_fatal)cs16_web_record_fatal(what,0);abort();}
extern uint32_t g_cur_func __attribute__((weak));
extern uint32_t g_icall_trace_idx __attribute__((weak)),g_icall_from[] __attribute__((weak)),g_icall_trace[] __attribute__((weak));
static inline __attribute__((always_inline)) void *translate_pointer(uint32_t va,int optional){
 if(optional&&!va)return NULL;
 if((!initialized&&!gm_init())||va<0x10000||!pages[va/PAGE]){fprintf(stderr,"[guest-memory-web] address=%08x function=%08x\n",va,&g_cur_func?g_cur_func:0);if(&g_icall_trace_idx)for(unsigned k=0;k<12;k++){unsigned n=(g_icall_trace_idx-12+k)&127;fprintf(stderr,"[guest-memory-chain] %08x -> %08x\n",g_icall_from[n],g_icall_trace[n]);}fail("unmapped guest pointer");}
 return pages[va/PAGE]+va%PAGE;
}
void *gm_ptr(uint32_t va){return translate_pointer(va,1);}
void *cs16_guest_deref(uint32_t va){return translate_pointer(va,0);}

uint32_t gm_va(const void *p){
 if(!p)return 0;if(!gm_init())fail("uninitialized mapping");uintptr_t pointer=(uintptr_t)p;
 pthread_mutex_lock(&lock);unsigned lo=0,hi=allocation_count;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(allocations[mid].host>pointer)hi=mid;else lo=mid+1;}
 if(lo){allocation *a=&allocations[lo-1];if(pointer-a->host<a->bytes){uint32_t result=a->va+(uint32_t)(pointer-a->host);pthread_mutex_unlock(&lock);return result;}}
 pthread_mutex_unlock(&lock);fail("host pointer is not guest-owned");return 0;
}
/* Locked helpers. All arithmetic involving an end address is 64-bit. */
static int range(uint32_t va,uint32_t size,size_t *start,size_t *count) {
    if (!size || va<0x10000 || (uint64_t)va+size>((uint64_t)1<<32)) return 0;
    *start=va/page;
    *count=(((uint64_t)va+size+page-1)/page)-*start;
    uint32_t id=owner[*start];
    if (!id) return 0;
    for (size_t k=0;k<*count;k++) if (owner[*start+k]!=id) return 0;
    return 1;
}
static int native_prot(uint32_t p){switch(p){case 0x01:return 0;case 0x02:case 0x20:return 1;case 0x04:case 0x40:return 3;default:return -1;}}
uint32_t gm_reserve(uint32_t preferred,uint32_t size) {
    if (!gm_init() || !size) return 0;
    size_t n=((uint64_t)size+page-1)/page,start=0;
    if (n>npages) return 0;
    pthread_mutex_lock(&lock);
    if (preferred) {
        if (preferred<0x10000 || preferred%page || (uint64_t)preferred+n*page>((uint64_t)1<<32)) goto done;
        start=preferred/page;
        for (size_t k=0;k<n;k++) if (owner[start+k]) {start=0;goto done;}
    } else {
        size_t run=0,limit=0x70000000/page;
        for (size_t k=0x10000000/page;k<limit;k++) {
            run=owner[k]?0:run+1;
            if (run==n) {start=k+1-n;break;}
        }
        if (!start) goto done;
    }
    if(n>UINT32_MAX/PAGE || allocation_count==MAX_ALLOCS){start=0;goto done;}
    unsigned char *backing=malloc(n*PAGE);if(!backing){start=0;goto done;}
    unsigned slot=allocation_count;while(slot&&allocations[slot-1].host>(uintptr_t)backing){allocations[slot]=allocations[slot-1];slot--;}
    allocations[slot]=(allocation){(uintptr_t)backing,(uint32_t)(start*PAGE),(uint32_t)(n*PAGE)};allocation_count++;
    for(size_t k=0;k<n;k++){owner[start+k]=(uint32_t)start+1;pages[start+k]=backing+k*PAGE;}
    spans[start]=(uint32_t)n;
done:
    if(!start)fprintf(stderr,"[guest-reserve-failed] preferred=%08x size=%u allocations=%u\n",preferred,size,allocation_count);
    pthread_mutex_unlock(&lock);
    return (uint32_t)(start*page);
}
int gm_commit(uint32_t va,uint32_t size,uint32_t prot) {
    if (!gm_init()) return 0;
    int flags=native_prot(prot),ok=0;size_t start,n;
    if (flags<0) return 0;
    pthread_mutex_lock(&lock);
    if (range(va,size,&start,&n)) {
        for (size_t k=0;k<n;k++){if(!protection[start+k])memset(pages[start+k],0,PAGE);protection[start+k]=(unsigned char)prot;}
        ok=1;
    }
    pthread_mutex_unlock(&lock);return ok;
}
static int discard(size_t start,size_t n){for(size_t k=0;k<n;k++)memset(pages[start+k],0,PAGE);return 1;}
int gm_decommit(uint32_t va,uint32_t size) {
    if (!gm_init() || va%page || size%page) return 0;
    size_t start,n;int ok=0;
    pthread_mutex_lock(&lock);
    if (range(va,size,&start,&n) && discard(start,n)) {
        memset(protection+start,0,n);ok=1;
    }
    pthread_mutex_unlock(&lock);return ok;
}
int gm_release(uint32_t base) {
    if (!gm_init() || base%page) return 0;
    size_t start=base/page;int ok=0;
    pthread_mutex_lock(&lock);
    size_t n=spans[start];
    if (n && owner[start]==start+1 && discard(start,n)) {
        uintptr_t backing=(uintptr_t)pages[start];free(pages[start]);
        for(unsigned i=0;i<allocation_count;i++)if(allocations[i].host==backing){memmove(allocations+i,allocations+i+1,(allocation_count-i-1)*sizeof *allocations);allocation_count--;break;}
        memset(pages+start,0,n*sizeof *pages);memset(owner+start,0,n*sizeof *owner);memset(protection+start,0,n);
        spans[start]=0;ok=1;
    }
    pthread_mutex_unlock(&lock);return ok;
}
int gm_protect(uint32_t va,uint32_t size,uint32_t prot,uint32_t *old) {
    if (!gm_init() || va%page || size%page) return 0;
    size_t start,n;int ok=0,p=native_prot(prot);
    if (p<0) return 0;
    pthread_mutex_lock(&lock);
    if (range(va,size,&start,&n)) {
        for (size_t k=0;k<n;k++) if (!protection[start+k]) goto done;
        uint32_t previous=protection[start];
        {
            for (size_t k=0;k<n;k++) protection[start+k]=(unsigned char)prot;
            if (old) *old=previous;
            ok=1;
        }
    }
done:pthread_mutex_unlock(&lock);return ok;
}
int gm_query(uint32_t va,uint32_t out[7]) {
    if (!gm_init() || !out) return 0;
    size_t i=va/page,end=i+1;pthread_mutex_lock(&lock);
    uint32_t id=owner[i];unsigned char p=protection[i];
    while (end<npages && owner[end]==id && protection[end]==p) end++;
    out[0]=(uint32_t)(i*page);out[1]=id?(uint32_t)((id-1)*page):0;
    out[2]=id?0x04:0;out[3]=(uint32_t)((end-i)*page);
    out[4]=id?(p?0x1000:0x2000):0x10000;out[5]=p?p:0x01;out[6]=id?0x20000:0;
    pthread_mutex_unlock(&lock);return 1;
}
int gm_accessible(uint32_t va,uint32_t size,int write_access) {
    if (!size) return 1;
    if (!gm_init()) return 0;
    size_t start,n;int ok=0;pthread_mutex_lock(&lock);
    if (range(va,size,&start,&n)) {
        ok=1;
        for (size_t k=0;k<n;k++) {
            int p=native_prot(protection[start+k]);
            if (p<0 || !(p&1) || (write_access && !(p&2))) {ok=0;break;}
        }
    }
    pthread_mutex_unlock(&lock);return ok;
}
#include "guest_heap.inc"
char *gm_strdup(const char *s) {
    size_t n=strlen(s)+1;char *p=gm_alloc(n);if (p) memcpy(p,s,n);return p;
}
