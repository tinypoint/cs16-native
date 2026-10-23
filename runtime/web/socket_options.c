/* Buffer limits for the real Emscripten WebSocket socket queues. These do
 * not create raw TCP/UDP connectivity or turn failed sends into successes. */
#include <emscripten.h>
#include <errno.h>
int cs16_web_socket_buffer(int fd,int option,int value){
 if(value<1||value>16777216)return EINVAL;
 return MAIN_THREAD_EM_ASM_INT({return(function(){
  const sock=SOCKFS.getSocket($0);if(!sock)return 8;
  if($1===0x1001){sock.cs16SendLimit=$2;return 0;}
  if($1!==0x1002||sock.type!==2)return 50;
  sock.cs16ReceiveLimit=$2;
  if(!sock.cs16BoundedReceive){
   sock.recv_queue.push=function(...items){let bytes=this.reduce((n,item)=>n+item.data.byteLength,0);for(const item of items){if(bytes+item.data.byteLength>sock.cs16ReceiveLimit)continue;Array.prototype.push.call(this,item);bytes+=item.data.byteLength;}return this.length;};
   sock.cs16BoundedReceive=true;
  }
  let bytes=sock.recv_queue.reduce((n,item)=>n+item.data.byteLength,0);
  while(bytes>sock.cs16ReceiveLimit&&sock.recv_queue.length)bytes-=sock.recv_queue.pop().data.byteLength;
  return 0;
 })();},fd,option,value);
}
int cs16_web_socket_can_send(int fd,unsigned bytes){
 return MAIN_THREAD_EM_ASM_INT({return(function(){
  const sock=SOCKFS.getSocket($0);if(!sock)return 0;
  if(!sock.cs16SendLimit)return 1;
  let queued=0;for(const peer of Object.values(sock.peers)){queued+=peer.socket?.bufferedAmount||0;for(const item of peer.msg_send_queue||[])queued+=item.byteLength;}
  return queued+$1<=sock.cs16SendLimit?1:0;
 })();},fd,bytes);
}
