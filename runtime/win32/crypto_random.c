/* Ephemeral CryptoAPI provider handles and OS-backed random bytes only.
 * Key containers, signing and encryption are not implemented. */
#include <stdlib.h>
#include <stdint.h>
#include "win32hle.h"
static uint32_t providers[64],next=0xC1700001u;
static int provider(uint32_t h){for(int i=0;i<64;i++)if(h && providers[i]==h)return i;return -1;}
static void acquire(void){
 uint32_t out=A32(0),flags=A32(4),type=A32(3);
 if(!out || !gm_accessible(out,4,1)){hle_set_last_error(87);RET(0,5);}
 if(type!=1 || (flags!=0 && flags!=0xF0000000u) || (A32(1)&&*ASTR(1))){hle_set_last_error(120);RET(0,5);}
 for(int i=0;i<64;i++)if(!providers[i]){providers[i]=next++;MEM32(out)=providers[i];RET(1,5);}
 hle_set_last_error(8);RET(0,5);
}
static void random_bytes(void){
 if(provider(A32(0))<0){hle_set_last_error(6);RET(0,3);}
 uint32_t size=A32(1),out=A32(2);if(!gm_accessible(out,size,1)){hle_set_last_error(87);RET(0,3);}
 arc4random_buf(gm_ptr(out),size);RET(1,3);
}
static void release(void){int i=provider(A32(0));if(i<0 || A32(1)){hle_set_last_error(i<0?6:87);RET(0,2);}providers[i]=0;RET(1,2);}
const win32hle_shim win32hle_crypto_random[]={{"CryptAcquireContextA",acquire},{"CryptGenRandom",random_bytes},{"CryptReleaseContext",release},{0,0}};
