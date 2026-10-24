/* Buffer limits for the real Emscripten WebSocket socket queues. These do
 * not create raw TCP/UDP connectivity or turn failed sends into successes. */
#include <emscripten.h>
#include <errno.h>
int cs16_web_socket_flag(int,int,int);
int cs16_web_socket_buffer(int fd,int option,int value){
 int installed=cs16_web_socket_flag(fd,0,0);if(installed)return installed;
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
/* A real datagram transport confined to this Wasm instance's virtual host.
 * It does not expose LAN UDP or manufacture an external server response. */
int cs16_web_socket_flag(int fd,int option,int value){
 return MAIN_THREAD_EM_ASM_INT({return(function(){
  const sock=SOCKFS.getSocket($0);if(!sock)return 8;
  if(sock.type!==2)return $1===0?0:50;
  const sockets=Module.cs16Datagrams ||= new Map();
  if(!sock.cs16LocalDatagram){
   sock.cs16LocalDatagram=true;sock.cs16Broadcast=false;sock.cs16Reuse=false;sockets.set($0,sock);
   const original=sock.sock_ops,ops=Object.create(original);
   const freePort=()=>{for(let p=49152;p<65536;p++)if(!Array.from(sockets.values()).some(s=>s.sport===p))return p;throw new FS.ErrnoError(138)};
   ops.bind=function(s,addr,port){
    if(s.saddr!==undefined||s.sport!==undefined)throw new FS.ErrnoError(28);
    if(port===0)port=freePort();
    for(const peer of sockets.values())if(peer!==s&&peer.sport===port&&!(peer.cs16Reuse&&s.cs16Reuse)&&(addr==='0.0.0.0'||peer.saddr==='0.0.0.0'||addr===peer.saddr))throw new FS.ErrnoError(3);
    s.saddr=addr;s.sport=port;
   };
   ops.sendmsg=function(s,buffer,offset,length,addr,port){
    addr ??= s.daddr;port ??= s.dport;
    const broadcast=addr==='255.255.255.255',local=broadcast||addr==='localhost'||addr==='0.0.0.0'||addr?.startsWith('127.');
    if(!local)return original.sendmsg(s,buffer,offset,length,addr,port);
    if(broadcast&&!s.cs16Broadcast)throw new FS.ErrnoError(2);
    if(s.sport===undefined)ops.bind(s,'0.0.0.0',0);
    const source=ArrayBuffer.isView(buffer)?new Uint8Array(buffer.buffer,buffer.byteOffset+offset,length):new Uint8Array(buffer,offset,length);
    for(const receiver of sockets.values())if(receiver.sport===port&&(broadcast||receiver.saddr==='0.0.0.0'||receiver.saddr===addr||addr==='localhost'||addr==='127.0.0.1')){
     receiver.recv_queue.push({addr:'127.0.0.1',port:s.sport,data:source.slice()});if(!broadcast)break;
    }
    return length;
   };
   ops.poll=function(s){return 4|(s.recv_queue.length?65:0)};
   ops.close=function(s){sockets.delete(s.stream.fd);return original.close(s)};
   sock.sock_ops=ops;
  }
  if($1===0)return 0;
  if($1===0x20){sock.cs16Broadcast=!!$2;return 0;}
  if($1===4){sock.cs16Reuse=!!$2;return 0;}
  return 50;
 })();},fd,option,value);
}
int cs16_web_socket_option(int fd,int option,int *out){
 return MAIN_THREAD_EM_ASM_INT({return(function(){
  const s=SOCKFS.getSocket($0);if(!s)return 8;let value;
  if($1===0x20)value=s.cs16Broadcast?1:0;
  else if($1===4)value=s.cs16Reuse?1:0;
  else if($1===0x1001)value=s.cs16SendLimit||65536;
  else if($1===0x1002)value=s.cs16ReceiveLimit||65536;
  else if($1===0x1008)value=s.type;
  else return 50;
  HEAP32[$2>>2]=value;return 0;
 })();},fd,option,out);
}
