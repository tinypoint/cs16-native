/* Native small-object workload used by the original server's map startup. */
#include "guest_memory.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
ptrdiff_t g_mem_base;
int main(void){
 assert(gm_init());const unsigned count=100000;void **nodes=calloc(count,sizeof *nodes);assert(nodes);
 for(unsigned i=0;i<count;i++){nodes[i]=gm_calloc(1,16);assert(nodes[i]&&!(gm_va(nodes[i])&15));((uint32_t *)nodes[i])[0]=i+1;((uint32_t *)nodes[i])[3]=~i;}
 for(unsigned i=0;i<count;i++)assert(((uint32_t *)nodes[i])[0]==i+1&&((uint32_t *)nodes[i])[3]==~i&&gm_ptr(gm_va(nodes[i]))==nodes[i]);
 for(unsigned i=0;i<count;i+=2)gm_free(nodes[i]);
 for(unsigned i=0;i<count;i+=2){nodes[i]=gm_calloc(1,16);assert(nodes[i]&&!((uint32_t *)nodes[i])[0]&&!((uint32_t *)nodes[i])[3]);}
 for(unsigned i=1;i<count;i+=2)assert(((uint32_t *)nodes[i])[0]==i+1&&((uint32_t *)nodes[i])[3]==~i);
 void *grown=gm_realloc(nodes[1],70000);assert(grown&&((uint32_t *)grown)[0]==2&&((uint32_t *)grown)[3]==~1u);nodes[1]=gm_realloc(grown,8);assert(nodes[1]&&((uint32_t *)nodes[1])[0]==2);
 for(unsigned i=0;i<count;i++)gm_free(nodes[i]);free(nodes);puts("{\"nativeHeapProbe\":true,\"hundredThousandSmallAllocations\":true,\"reuseZeroing\":true,\"reallocPreservation\":true}");return 0;
}
