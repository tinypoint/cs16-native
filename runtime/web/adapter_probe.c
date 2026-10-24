#include "guest_memory.h"
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <emscripten.h>
int cs16_probe_adapters(void);
int cs16_web_socket_buffer(int,int,int);
int cs16_web_socket_flag(int,int,int);
int cs16_web_socket_option(int,int,int *);
int cs16_web_socket_can_send(int,unsigned);
int main(void){
 if(!gm_init()||cs16_probe_adapters())return 1;
 int fd=socket(AF_INET,SOCK_DGRAM,0);assert(fd>=0);
 assert(!cs16_web_socket_buffer(fd,0x1001,64));assert(!cs16_web_socket_buffer(fd,0x1002,32));
 assert(cs16_web_socket_can_send(fd,64)&&!cs16_web_socket_can_send(fd,65));
 assert(cs16_web_socket_buffer(-1,0x1001,64)!=0);
 int bounded=EM_ASM_INT({return(function(){const sock=SOCKFS.getSocket($0);sock.recv_queue.push({data:new Uint8Array(24)},{data:new Uint8Array(24)});return sock.recv_queue.length===1&&sock.recv_queue[0].data.byteLength===24?1:0;})();},fd);assert(bounded);close(fd);
 int sender=socket(AF_INET,SOCK_DGRAM,0),receiver=socket(AF_INET,SOCK_DGRAM,0);assert(sender>=0&&receiver>=0);
 assert(!cs16_web_socket_flag(sender,0,0)&&!cs16_web_socket_flag(receiver,0,0));
 struct sockaddr_in destination={0},source={0},actual={0};destination.sin_family=AF_INET;destination.sin_addr.s_addr=htonl(0x7f000001);destination.sin_port=htons(27015);source.sin_family=AF_INET;source.sin_port=htons(27005);
 assert(!bind(receiver,(struct sockaddr *)&destination,sizeof destination));assert(!bind(sender,(struct sockaddr *)&source,sizeof source));
 const char packet[]="CS16-local";char received[32]={0};socklen_t length=sizeof actual;
 assert(sendto(sender,packet,sizeof packet,0,(struct sockaddr *)&destination,sizeof destination)==sizeof packet);
 assert(recvfrom(receiver,received,sizeof received,MSG_DONTWAIT,(struct sockaddr *)&actual,&length)==sizeof packet&&!memcmp(packet,received,sizeof packet)&&ntohs(actual.sin_port)==27005);
 destination.sin_addr.s_addr=htonl(0xffffffff);assert(sendto(sender,packet,sizeof packet,0,(struct sockaddr *)&destination,sizeof destination)<0&&errno==EACCES);
 assert(!cs16_web_socket_flag(sender,0x20,1));int enabled=0;assert(!cs16_web_socket_option(sender,0x20,&enabled)&&enabled==1);
 assert(sendto(sender,packet,sizeof packet,0,(struct sockaddr *)&destination,sizeof destination)==sizeof packet);
 assert(recvfrom(receiver,received,sizeof received,MSG_DONTWAIT,NULL,NULL)==sizeof packet&&!memcmp(packet,received,sizeof packet));close(sender);close(receiver);
 puts("{\"softwareAdapterProbe\":true,\"instanceLocalDatagramRoundtrip\":true,\"broadcastPermissionAndDelivery\":true,\"bufferBounds\":true,\"sizeNegotiation\":true,\"unconfiguredAddress\":true,\"localUnicastIdentity\":true,\"physicalAdapterExposed\":false,\"actualSocketSendQueueLimit\":true,\"actualDatagramReceiveQueueLimit\":true}");return 0;
}
