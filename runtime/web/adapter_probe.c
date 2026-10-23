#include "guest_memory.h"
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <emscripten.h>
int cs16_probe_adapters(void);
int cs16_web_socket_buffer(int,int,int);
int cs16_web_socket_can_send(int,unsigned);
int main(void){
 if(!gm_init()||cs16_probe_adapters())return 1;
 int fd=socket(AF_INET,SOCK_DGRAM,0);assert(fd>=0);
 assert(!cs16_web_socket_buffer(fd,0x1001,64));assert(!cs16_web_socket_buffer(fd,0x1002,32));
 assert(cs16_web_socket_can_send(fd,64)&&!cs16_web_socket_can_send(fd,65));
 assert(cs16_web_socket_buffer(-1,0x1001,64)!=0);
 int bounded=EM_ASM_INT({return(function(){const sock=SOCKFS.getSocket($0);sock.recv_queue.push({data:new Uint8Array(24)},{data:new Uint8Array(24)});return sock.recv_queue.length===1&&sock.recv_queue[0].data.byteLength===24?1:0;})();},fd);assert(bounded);close(fd);
 puts("{\"softwareAdapterProbe\":true,\"bufferBounds\":true,\"sizeNegotiation\":true,\"unconfiguredAddress\":true,\"localUnicastIdentity\":true,\"physicalAdapterExposed\":false,\"actualSocketSendQueueLimit\":true,\"actualDatagramReceiveQueueLimit\":true}");return 0;
}
