#include "guest_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
int main(void){
 assert(gm_init());uint32_t low=gm_reserve(0x1400000,8192),high=gm_reserve(0x52000000,8192);assert(low==0x1400000&&high==0x52000000);
 assert(!gm_reserve(high,4096));assert(!gm_accessible(high,4,0));assert(gm_commit(low,8192,4)&&gm_commit(high,8192,4));
 strcpy(gm_ptr(high+4090),"cross-page-test");assert(!strcmp(gm_ptr(high+4090),"cross-page-test"));assert(gm_va(gm_ptr(high+4090))==high+4090);
 *(uint32_t *)gm_ptr(low)=0xc516;assert(!*(uint32_t *)gm_ptr(high));uint32_t old;
 assert(gm_protect(high,4096,2,&old)&&old==4&&gm_accessible(high,4,0)&&!gm_accessible(high,4,1));assert(gm_protect(high,4096,4,&old)&&old==2);
 assert(gm_decommit(high,8192)&&!gm_accessible(high,1,0));assert(gm_commit(high,8192,4)&&!*(uint32_t *)gm_ptr(high+4090));
 assert(!gm_accessible(high+8190,4,0));uint32_t info[7];assert(gm_query(high,info)&&info[0]==high&&info[1]==high&&info[4]==0x1000);
 void *objects[64];for(unsigned i=0;i<64;i++){objects[i]=gm_calloc(1,5000+i);assert(objects[i]);memset(objects[i],i,5000+i);assert(gm_ptr(gm_va(objects[i])+i)==(char *)objects[i]+i);}
 for(unsigned i=0;i<64;i++){void *q=gm_realloc(objects[i],10000);assert(q&&((unsigned char *)q)[4000]==i);gm_free(q);}
 assert(gm_release(high)&&!gm_accessible(high,1,0));assert(gm_reserve(high,8192)==high&&gm_commit(high,8192,4));assert(!*(uint32_t *)gm_ptr(high));assert(gm_release(high)&&gm_release(low));
 /* Reproduce the map's small-node workload beyond the former 32768 cap. */
 const unsigned count=100000;void **nodes=malloc(count*sizeof *nodes);assert(nodes);
 for(unsigned i=0;i<count;i++){nodes[i]=gm_calloc(1,16);assert(nodes[i]&&!(gm_va(nodes[i])&15));((uint32_t *)nodes[i])[0]=i+1;((uint32_t *)nodes[i])[3]=~i;}
 for(unsigned i=0;i<count;i++)assert(((uint32_t *)nodes[i])[0]==i+1&&((uint32_t *)nodes[i])[3]==~i&&gm_ptr(gm_va(nodes[i]))==nodes[i]);
 for(unsigned i=0;i<count;i+=2)gm_free(nodes[i]);
 for(unsigned i=0;i<count;i+=2){nodes[i]=gm_calloc(1,16);assert(nodes[i]&&!((uint32_t *)nodes[i])[0]&&!((uint32_t *)nodes[i])[3]);}
 for(unsigned i=1;i<count;i+=2)assert(((uint32_t *)nodes[i])[0]==i+1&&((uint32_t *)nodes[i])[3]==~i);
 void *grown=gm_realloc(nodes[1],70000);assert(grown&&((uint32_t *)grown)[0]==2&&((uint32_t *)grown)[3]==~1u);nodes[1]=gm_realloc(grown,8);assert(nodes[1]&&((uint32_t *)nodes[1])[0]==2);
 for(unsigned i=0;i<count;i++)gm_free(nodes[i]);free(nodes);
 puts("{\"sparseGuestMemoryProbe\":true,\"hundredThousandSmallAllocations\":true,\"originalHighAddressesPreserved\":true,\"crossPageBytes\":true,\"reversePointers\":true,\"protectionMetadata\":true,\"decommitZeroing\":true,\"reallocPreservation\":true,\"reuseZeroing\":true}");return 0;
}
