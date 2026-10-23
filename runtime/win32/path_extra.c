/* Long-path lookup in the guest filesystem, not the user's Windows registry. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "win32hle.h"
static void get_long_path(void){
 const char *input=ASTR(0);char host[4096],resolved[4096],output[4096];
 if(!input || !hle_host_path(input,host,sizeof host) || !realpath(host,resolved)){
  hle_set_last_error(2);RET(0,3);
 }
 if(input[0]=='/' || input[0]=='\\' || (input[0] && input[1]==':'))hle_guest_path(resolved,output,sizeof output);
 else {
  char cwd[4096];if(!getcwd(cwd,sizeof cwd)){hle_set_last_error(3);RET(0,3);}
  size_t n=strlen(cwd);const char *path=resolved;
  if(!strncmp(path,cwd,n) && path[n]=='/')path+=n+1;
  if(strlen(path)>=sizeof output){hle_set_last_error(206);RET(0,3);}
  strcpy(output,path);for(char *p=output;*p;p++)if(*p=='/')*p='\\';
 }
 size_t length=strlen(output);uint32_t size=A32(2);
 if(!size || size<=length)RET(length+1,3);
 if(!A32(1) || !gm_accessible(A32(1),(uint32_t)length+1,1)){hle_set_last_error(87);RET(0,3);}
 memcpy(APTR(1),output,length+1);RET(length,3);
}
const win32hle_shim win32hle_path_extra[]={{"GetLongPathNameA",get_long_path},{0,0}};
