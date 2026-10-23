/* The compatibility layer owns one unconfigured software socket interface.
 * This is NOT a physical host adapter or a claim of LAN connectivity. Browsers
 * expose neither physical NIC identities nor arbitrary UDP/TCP access. */
#include "win32hle.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
typedef struct {uint32_t next;char ip[16],mask[16];uint32_t context;} ip32;
typedef struct {
 uint32_t next,combo;char name[260],description[132];uint32_t address_length;
 uint8_t address[8];uint32_t index,type,dhcp,current_ip;
 ip32 ip,gateway,server;uint32_t have_wins;ip32 primary_wins,secondary_wins;
 int32_t lease_start,lease_end;
} adapter32;
_Static_assert(sizeof(adapter32)==640&&offsetof(adapter32,ip)==428,"XP adapter ABI");
static adapter32 software;static int initialized;
static uint32_t enumerate(uint32_t output,uint32_t size){
 if(!gm_accessible(size,4,1))return 87;
 if(!initialized){
  memset(&software,0,sizeof software);
  if(getentropy(software.address,6))return 13;
  software.address[0]=(software.address[0]|2)&0xfe;software.address_length=6;
  strcpy(software.name,"cs16-wasm-sockets");strcpy(software.description,"CS16 WebAssembly software socket interface (unconfigured)");
  software.index=1;software.type=1;
  ip32 *ips[]={&software.ip,&software.gateway,&software.server,&software.primary_wins,&software.secondary_wins};
  for(unsigned i=0;i<5;i++){strcpy(ips[i]->ip,"0.0.0.0");strcpy(ips[i]->mask,"0.0.0.0");}
  initialized=1;fprintf(stderr,"[web-network] unconfigured software socket interface; physical adapters and LAN unavailable\n");
 }
 uint32_t supplied=MEM32(size);MEM32(size)=sizeof software;
 if(!output||supplied<sizeof software)return 111;
 if(!gm_accessible(output,sizeof software,1))return 87;
 memcpy(gm_ptr(output),&software,sizeof software);return 0;
}
static void adapters(void){RET(enumerate(A32(0),A32(1)),2);}
const win32hle_shim win32hle_iphlpapi[]={{"GetAdaptersInfo",adapters},{0,0}};
int cs16_probe_adapters(void){
 uint32_t size=gm_va(gm_calloc(1,4)),buffer=gm_va(gm_alloc(sizeof software+8));int result=16;
 if(enumerate(0,0)!=87||enumerate(0,size)!=111||MEM32(size)!=640)goto done;
 memset(gm_ptr(buffer),0x5a,648);MEM32(size)=639;if(enumerate(buffer+4,size)!=111||MEM32(buffer+4)!=0x5a5a5a5a)goto done;
 MEM32(size)=640;if(enumerate(buffer+4,size)||MEM32(buffer)!=0x5a5a5a5a||MEM32(buffer+644)!=0x5a5a5a5a)goto done;
 adapter32 *a=gm_ptr(buffer+4);if(strcmp(a->name,"cs16-wasm-sockets")||strcmp(a->ip.ip,"0.0.0.0")||a->address_length!=6||(a->address[0]&3)!=2)goto done;
 result=0;
 done:gm_free(gm_ptr(size));gm_free(gm_ptr(buffer));return result;
}
