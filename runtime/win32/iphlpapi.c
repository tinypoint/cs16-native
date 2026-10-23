/* IPv4 adapter snapshots from macOS, serialized using the 32-bit XP ABI.
 * No network configuration is modified. WINS is unavailable on this host. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <SystemConfiguration/SCDynamicStoreCopyDHCPInfo.h>
#include "win32hle.h"

typedef struct { uint32_t next;char ip[16],mask[16];uint32_t context; } ip_string32;
typedef struct {
 uint32_t next,combo;char name[260],description[132];uint32_t address_length;
 uint8_t address[8];uint32_t index,type,dhcp,current_ip;
 ip_string32 ip,gateway,server;uint32_t have_wins;ip_string32 primary_wins,secondary_wins;
 int32_t lease_start,lease_end;
} adapter32;
_Static_assert(sizeof(ip_string32)==40,"32-bit IP_ADDR_STRING");
_Static_assert(sizeof(adapter32)==640 && offsetof(adapter32,address)==404 && offsetof(adapter32,ip)==428 && offsetof(adapter32,lease_start)==632,"32-bit IP_ADAPTER_INFO");
typedef struct { const char *name;unsigned index,count;adapter32 value; } host_adapter;
static void address_text(char text[16],const void *address){if(!inet_ntop(AF_INET,address,text,16))snprintf(text,16,"0.0.0.0");}
static void empty_ip(ip_string32 *p){snprintf(p->ip,16,"0.0.0.0");snprintf(p->mask,16,"0.0.0.0");}
static int32_t lease_time(CFDateRef date){if(!date)return 0;double t=CFDateGetAbsoluteTime(date)+kCFAbsoluteTimeIntervalSince1970;return t>=INT32_MAX?INT32_MAX:t<=0?0:(int32_t)t;}
static int dictionary(CFTypeRef value){return value&&CFGetTypeID(value)==CFDictionaryGetTypeID();}
static int text_equal(CFTypeRef value,const char *text){char utf8[IF_NAMESIZE];return value&&CFGetTypeID(value)==CFStringGetTypeID()&&CFStringGetCString(value,utf8,sizeof utf8,kCFStringEncodingUTF8)&&!strcmp(utf8,text);}
static void service_info(host_adapter *a,CFArrayRef keys){
 if(!keys)return;
 for(CFIndex i=0;i<CFArrayGetCount(keys);i++){
  CFStringRef key=CFArrayGetValueAtIndex(keys,i);CFPropertyListRef state=SCDynamicStoreCopyValue(NULL,key);
  if(!dictionary(state)){if(state)CFRelease(state);continue;}
  if(!text_equal(CFDictionaryGetValue(state,CFSTR("InterfaceName")),a->name)){CFRelease(state);continue;}
  CFTypeRef router=CFDictionaryGetValue(state,CFSTR("Router"));char ip[16];struct in_addr parsed;
  if(router&&CFGetTypeID(router)==CFStringGetTypeID()&&CFStringGetCString(router,ip,sizeof ip,kCFStringEncodingASCII)&&inet_pton(AF_INET,ip,&parsed)==1)snprintf(a->value.gateway.ip,16,"%s",ip);
  CFRelease(state);
  /* State:/Network/Service/<serviceID>/IPv4 */
  CFArrayRef parts=CFStringCreateArrayBySeparatingStrings(NULL,key,CFSTR("/"));
  if(parts&&CFArrayGetCount(parts)==5){
   CFDictionaryRef dhcp=SCDynamicStoreCopyDHCPInfo(NULL,CFArrayGetValueAtIndex(parts,3));
   if(dhcp){CFDateRef start=DHCPInfoGetLeaseStartTime(dhcp);a->value.dhcp=start!=NULL;a->value.lease_start=lease_time(start);CFDateRef end=DHCPInfoGetLeaseExpirationTime(dhcp);a->value.lease_end=start&&!end?INT32_MAX:lease_time(end);CFDataRef server=DHCPInfoGetOptionData(dhcp,54);if(server&&CFDataGetLength(server)==4)address_text(a->value.server.ip,CFDataGetBytePtr(server));CFRelease(dhcp);}
  }
  if(parts)CFRelease(parts);break;
 }
}
static uint32_t adapters(uint32_t output,uint32_t size_pointer){
 if(!gm_accessible(size_pointer,4,1))return 87;
 struct ifaddrs *interfaces=NULL;if(getifaddrs(&interfaces))return 13;
 size_t capacity=0;for(struct ifaddrs *p=interfaces;p;p=p->ifa_next)capacity++;
 host_adapter *list=calloc(capacity?capacity:1,sizeof *list);if(!list){freeifaddrs(interfaces);return 8;}
 unsigned count=0,total_ips=0;
 for(struct ifaddrs *p=interfaces;p;p=p->ifa_next){
  if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_INET||(p->ifa_flags&IFF_LOOPBACK))continue;
  unsigned i=0;while(i<count&&strcmp(list[i].name,p->ifa_name))i++;
  if(i==count){host_adapter *a=&list[count++];a->name=p->ifa_name;a->index=if_nametoindex(p->ifa_name);a->value.index=a->index;a->value.type=1;snprintf(a->value.name,sizeof a->value.name,"%s",p->ifa_name);snprintf(a->value.description,sizeof a->value.description,"macOS %s",p->ifa_name);empty_ip(&a->value.ip);empty_ip(&a->value.gateway);empty_ip(&a->value.server);empty_ip(&a->value.primary_wins);empty_ip(&a->value.secondary_wins);}
  list[i].count++;total_ips++;
 }
 uint32_t result=0;uint64_t required=(uint64_t)count*sizeof(adapter32)+(uint64_t)(total_ips-count)*sizeof(ip_string32);
 if(!count){result=232;goto done;}
 if(required>UINT32_MAX){result=8;goto done;}
 uint32_t supplied=MEM32(size_pointer);MEM32(size_pointer)=(uint32_t)required;
 if(!output||supplied<required){result=111;goto done;}
 if(!gm_accessible(output,(uint32_t)required,1)||(uint64_t)output+required>UINT32_MAX){result=87;goto done;}
 CFArrayRef keys=SCDynamicStoreCopyKeyList(NULL,CFSTR("State:/Network/Service/[^/]+/IPv4"));
 uint32_t extra=output+count*sizeof(adapter32);
 for(unsigned i=0;i<count;i++){
  host_adapter *a=&list[i];a->value.next=i+1<count?output+(i+1)*sizeof(adapter32):0;
  for(struct ifaddrs *p=interfaces;p;p=p->ifa_next){
   if(!p->ifa_addr||strcmp(p->ifa_name,a->name)||p->ifa_addr->sa_family!=AF_LINK)continue;
   const struct sockaddr_dl *link=(const void *)p->ifa_addr;
   if(link->sdl_len<offsetof(struct sockaddr_dl,sdl_data)||link->sdl_nlen+link->sdl_alen>link->sdl_len - offsetof(struct sockaddr_dl,sdl_data))continue;
   a->value.type=link->sdl_type?link->sdl_type:1;a->value.address_length=link->sdl_alen>8?8:link->sdl_alen;memcpy(a->value.address,LLADDR(link),a->value.address_length);break;
  }
  service_info(a,keys);ip_string32 *last=NULL;
  for(struct ifaddrs *p=interfaces;p;p=p->ifa_next){
   if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_INET||strcmp(p->ifa_name,a->name))continue;
   ip_string32 *ip;if(!last)ip=&a->value.ip;else{last->next=extra;ip=gm_ptr(extra);memset(ip,0,sizeof *ip);extra+=sizeof *ip;}
   address_text(ip->ip,&((struct sockaddr_in *)p->ifa_addr)->sin_addr);if(p->ifa_netmask)address_text(ip->mask,&((struct sockaddr_in *)p->ifa_netmask)->sin_addr);else snprintf(ip->mask,16,"0.0.0.0");last=ip;
  }
  memcpy(gm_ptr(output+i*sizeof(adapter32)),&a->value,sizeof a->value);
 }
 if(keys)CFRelease(keys);
 done:free(list);freeifaddrs(interfaces);return result;
}
static void get_adapters(void){RET(adapters(A32(0),A32(1)),2);}
const win32hle_shim win32hle_iphlpapi[]={{"GetAdaptersInfo",get_adapters},{0,0}};

#define I_CHECK(c) do{if(!(c)){fprintf(stderr,"[adapter-probe] failed line %d: %s\n",__LINE__,#c);return 16;}}while(0)
int cs16_probe_adapters(void){
 uint32_t length=gm_va(gm_calloc(1,4));I_CHECK(adapters(0,0)==87);uint32_t status=adapters(0,length);I_CHECK(status==111||status==232);if(status==232){gm_free(gm_ptr(length));return 0;}
 uint32_t required=MEM32(length);I_CHECK(required>=640);uint32_t buf=gm_va(gm_alloc(required+8));memset(gm_ptr(buf),0x5a,required+8);MEM32(length)=required-1;I_CHECK(adapters(buf+4,length)==111&&MEM32(buf+4)==0x5a5a5a5a);MEM32(length)=required;I_CHECK(!adapters(buf+4,length));I_CHECK(MEM32(buf)==0x5a5a5a5a&&MEM32(buf+4+required)==0x5a5a5a5a);
 struct ifaddrs *native=NULL;I_CHECK(!getifaddrs(&native));unsigned seen=0;
 for(uint32_t p=buf+4;p;){
  I_CHECK(p>=buf+4&&(uint64_t)p+640<=buf+4+required&&seen++<=required/640);adapter32 *a=gm_ptr(p);I_CHECK(memchr(a->name,0,sizeof a->name)&&a->index==if_nametoindex(a->name)&&a->address_length<=8);int matched=0,mac_matched=a->address_length==0;
  for(struct ifaddrs *n=native;n;n=n->ifa_next)if(n->ifa_addr&&n->ifa_addr->sa_family==AF_INET&&!strcmp(n->ifa_name,a->name)&&!(n->ifa_flags&IFF_LOOPBACK)){struct in_addr ip;I_CHECK(inet_pton(AF_INET,a->ip.ip,&ip)==1);if(ip.s_addr==((struct sockaddr_in *)n->ifa_addr)->sin_addr.s_addr)matched=1;}I_CHECK(matched);
  for(struct ifaddrs *n=native;n;n=n->ifa_next)if(n->ifa_addr&&n->ifa_addr->sa_family==AF_LINK&&!strcmp(n->ifa_name,a->name)){struct sockaddr_dl *link=(void *)n->ifa_addr;if(link->sdl_alen==a->address_length&&!memcmp(a->address,LLADDR(link),a->address_length))mac_matched=1;}I_CHECK(mac_matched);
  unsigned ips=0;for(ip_string32 *ip=&a->ip;ip;){I_CHECK(ips++<required/40&&memchr(ip->ip,0,16)&&memchr(ip->mask,0,16));struct in_addr address,mask;I_CHECK(inet_pton(AF_INET,ip->ip,&address)==1&&inet_pton(AF_INET,ip->mask,&mask)==1);int found=0;for(struct ifaddrs *n=native;n;n=n->ifa_next)if(n->ifa_addr&&n->ifa_netmask&&n->ifa_addr->sa_family==AF_INET&&!strcmp(n->ifa_name,a->name)&&address.s_addr==((struct sockaddr_in *)n->ifa_addr)->sin_addr.s_addr&&mask.s_addr==((struct sockaddr_in *)n->ifa_netmask)->sin_addr.s_addr)found=1;I_CHECK(found);if(!ip->next)break;I_CHECK(ip->next>=buf+4&&(uint64_t)ip->next+40<=buf+4+required);ip=gm_ptr(ip->next);}p=a->next;
 }
 freeifaddrs(native);gm_free(gm_ptr(buf));gm_free(gm_ptr(length));return 0;
}
