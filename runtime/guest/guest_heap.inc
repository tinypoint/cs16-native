/* Small allocations share mapped arenas. A 16-byte STL node must not consume
 * a guest page and a reservation record. VirtualAlloc keeps page semantics. */
#define SLAB_BYTES (256u*1024u)
#define SLAB_CLASSES 13u
#define SLAB_NONE UINT32_MAX
typedef struct small_slab {
 uint8_t *host;uint32_t slot_size,count,free_head;
 uint32_t *next_free,*requested;struct small_slab *next;
} small_slab;
static small_slab *small_slabs[SLAB_CLASSES];
static pthread_mutex_t small_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned small_class(size_t size){unsigned i=0;size_t width=16;while(width<size&&i+1<SLAB_CLASSES){width*=2;i++;}return i;}
static void *small_alloc(size_t size){
 unsigned c=small_class(size);pthread_mutex_lock(&small_lock);
 small_slab *s=small_slabs[c];while(s&&s->free_head==SLAB_NONE)s=s->next;
 if(!s){
  s=calloc(1,sizeof *s);if(!s)goto failed;
  s->slot_size=16u<<c;s->count=SLAB_BYTES/s->slot_size;
  s->next_free=malloc(s->count*sizeof(uint32_t));s->requested=calloc(s->count,sizeof(uint32_t));
  if(!s->next_free||!s->requested){free(s->next_free);free(s->requested);free(s);goto failed;}
  uint32_t va=gm_reserve(0,SLAB_BYTES);
  if(!va||!gm_commit(va,SLAB_BYTES,0x04)){if(va)gm_release(va);free(s->next_free);free(s->requested);free(s);goto failed;}
  s->host=gm_ptr(va);for(uint32_t i=0;i<s->count;i++)s->next_free[i]=i+1<s->count?i+1:SLAB_NONE;
  s->next=small_slabs[c];small_slabs[c]=s;
 }
 uint32_t slot=s->free_head;s->free_head=s->next_free[slot];s->requested[slot]=(uint32_t)size;
 void *result=s->host+(size_t)slot*s->slot_size;memset(result,0,size);
 pthread_mutex_unlock(&small_lock);return result;
failed:pthread_mutex_unlock(&small_lock);return NULL;
}
/* 0 means a page allocation; -1 is a bad/interior/freed small allocation. */
static int small_find(void *p,small_slab **found,uint32_t *slot){
 uintptr_t address=(uintptr_t)p;
 for(unsigned c=0;c<SLAB_CLASSES;c++)for(small_slab *s=small_slabs[c];s;s=s->next){
  uintptr_t base=(uintptr_t)s->host;if(address<base||address-base>=SLAB_BYTES)continue;
  uintptr_t offset=address-base;if(offset%s->slot_size)return -1;
  uint32_t i=(uint32_t)(offset/s->slot_size);if(!s->requested[i])return -1;*found=s;*slot=i;return 1;
 }
 return 0;
}
void *gm_alloc(size_t size) {
 if(size>UINT32_MAX)return NULL;
 if(size<=65536)return small_alloc(size?size:1);
 uint32_t va=gm_reserve(0,(uint32_t)size);if(!va)return NULL;
 if(!gm_commit(va,(uint32_t)size,0x04)){gm_release(va);return NULL;}return gm_ptr(va);
}
void *gm_calloc(size_t n,size_t size){if(size&&n>SIZE_MAX/size)return NULL;return gm_alloc(n*size);}
void gm_free(void *p){
 if(!p)return;pthread_mutex_lock(&small_lock);small_slab *s=NULL;uint32_t slot=0;int kind=small_find(p,&s,&slot);
 if(kind==1){s->requested[slot]=0;s->next_free[slot]=s->free_head;s->free_head=slot;pthread_mutex_unlock(&small_lock);return;}
 pthread_mutex_unlock(&small_lock);if(kind<0)fail("free is not a live small allocation");
 if(!gm_release(gm_va(p)))fail("free is not an allocation base");
}
void *gm_realloc(void *p,size_t size){
 if(!p)return gm_alloc(size);if(!size){gm_free(p);return NULL;}
 pthread_mutex_lock(&small_lock);small_slab *s=NULL;uint32_t slot=0;int kind=small_find(p,&s,&slot);size_t old=kind==1?s->requested[slot]:0;
 if(kind==1&&size<=s->slot_size){if(size>old)memset((uint8_t *)p+old,0,size-old);s->requested[slot]=(uint32_t)size;pthread_mutex_unlock(&small_lock);return p;}
 pthread_mutex_unlock(&small_lock);if(kind<0)fail("realloc is not a live small allocation");
 if(!kind){uint32_t va=gm_va(p);pthread_mutex_lock(&lock);old=spans[va/page]*page;pthread_mutex_unlock(&lock);if(!old||va%page)fail("realloc is not an allocation base");}
 void *q=gm_alloc(size);if(q){memcpy(q,p,old<size?old:size);gm_free(p);}return q;
}
