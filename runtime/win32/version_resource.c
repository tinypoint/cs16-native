/* PE32 RT_VERSION reader. Original files supply all version data.
 * ANSI string queries currently support ASCII, failing explicitly for other
 * code points. No MUI merging is attempted by this diagnostic host. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "win32hle.h"
static uint16_t u16(const unsigned char *p){return p[0]|p[1]<<8;}
static uint32_t u32(const unsigned char *p){return u16(p)|(uint32_t)u16(p+2)<<16;}
static int span(size_t n,size_t at,size_t count){return at<=n && count<=n-at;}
static size_t align4(size_t x){return (x+3)&~(size_t)3;}
static size_t rva_offset(const unsigned char *p,size_t n,size_t table,unsigned count,uint32_t va,size_t len){
 for(unsigned i=0;i<count;i++){
  size_t s=table+i*40;uint32_t base=u32(p+s+12),raw=u32(p+s+20),bytes=u32(p+s+16);
  if(va>=base && va-base<=bytes && len<=bytes-(va-base) && span(n,(size_t)raw+va-base,len))return (size_t)raw+va-base;
 }
 return SIZE_MAX;
}
static unsigned char *read_version(const char *guest,uint32_t *length){
 char path[4096];*length=0;
 fprintf(stderr,"[version] reading %s\n",guest);
 if(!hle_host_path(guest,path,sizeof path)){hle_set_last_error(2);return NULL;}
 FILE *f=fopen(path,"rb");if(!f){hle_set_last_error(5);return NULL;}
 if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long size=ftell(f);
 if(size<64 || size>128*1024*1024){fclose(f);hle_set_last_error(1813);return NULL;}
 rewind(f);size_t n=(size_t)size;unsigned char *p=malloc(n),*out=NULL;
 if(!p){fclose(f);hle_set_last_error(8);return NULL;}
 if(fread(p,1,n,f)!=n){fclose(f);free(p);return NULL;}fclose(f);
 size_t pe=u32(p+60);
 if(u16(p)!=0x5a4d || !span(n,pe,24) || u32(p+pe)!=0x4550)goto done;
 unsigned sections=u16(p+pe+6),optional=u16(p+pe+20);size_t opt=pe+24,table=opt+optional;
 if(optional<120 || !span(n,opt,optional) || !span(n,table,sections*40) || u16(p+opt)!=0x10b || u32(p+opt+92)<3)goto done;
 uint32_t rootva=u32(p+opt+112),rootlen=u32(p+opt+116);
 size_t root=rva_offset(p,n,table,sections,rootva,rootlen);
 if(root==SIZE_MAX || rootlen<16)goto done;
 size_t rel=0;
 for(unsigned depth=0;depth<3;depth++){
  if(!span(rootlen,rel,16))goto done;
  unsigned count=u16(p+root+rel+12)+u16(p+root+rel+14);
  if(!span(rootlen,rel+16,(size_t)count*8))goto done;
  int found=0;
  for(unsigned i=0;i<count;i++){
   size_t e=root+rel+16+i*8;uint32_t id=u32(p+e),next=u32(p+e+4);
   if(depth==0 && id!=16)continue;
   if(depth<2){if(!(next&0x80000000u))continue;rel=next&0x7fffffffu;found=1;break;}
   if(next&0x80000000u || !span(rootlen,next,16))continue;
   uint32_t bytes=u32(p+root+next+4);size_t off=rva_offset(p,n,table,sections,u32(p+root+next),bytes);
   if(off==SIZE_MAX || bytes<6 || bytes>65535 || u16(p+off)<6 || u16(p+off)>bytes)goto done;
   bytes=u16(p+off);
   out=malloc(bytes);if(!out)goto done;memcpy(out,p+off,bytes);*length=bytes;goto done;
  }
  if(!found)goto done;
 }
done:free(p);if(!out)hle_set_last_error(1813);return out;
}
static void version_size(void){
 if(A32(1))MEM32(A32(1))=0;uint32_t n;unsigned char *p=read_version(ASTR(0),&n);free(p);
 /* Extra space holds converted ANSI strings while preserving raw UTF-16. */
 RET(n?n*2+16:0,2);
}
static void version_info(void){
 uint32_t n;unsigned char *p=read_version(ASTR(0),&n);
 if(!p)RET(0,4);
 uint32_t size=A32(2),dest=A32(3);
 if(!size || !gm_accessible(dest,size,1)){free(p);hle_set_last_error(87);RET(0,4);}
 memset(gm_ptr(dest),0,size);memcpy(gm_ptr(dest),p,n<size?n:size);free(p);
 if(size>=n+8){MEM32(dest+n)=0x31565343;MEM32(dest+n+4)=size;}
 RET(1,4);
}
static int block(const unsigned char *p,size_t end,size_t at,size_t *value,size_t *children){
 if(!span(end,at,6))return 0;size_t len=u16(p+at),vlen=u16(p+at+2),text=u16(p+at+4),k=at+6;
 if(len<6 || !span(end,at,len))return 0;
 while(span(at+len,k,2) && u16(p+k))k+=2;
 if(!span(at+len,k,2))return 0;*value=align4(k+2);
 size_t bytes=text?vlen*2:vlen;if(!span(at+len,*value,bytes))return 0;
 *children=align4(*value+bytes);return 1;
}
static int key_equal(const unsigned char *p,size_t end,size_t at,const char *key,size_t len){
 for(size_t i=0;i<len;i++)if(!span(end,at+i*2,2) || u16(p+at+i*2)!=(unsigned char)key[i])return 0;
 return span(end,at+len*2,2) && !u16(p+at+len*2);
}
static void version_query(void){
 uint32_t base=A32(0),result=A32(2),length=A32(3);
 if(length)MEM32(length)=0;
 if(!result || !length || !gm_accessible(base,6,0))RET(0,4);
 unsigned char *p=gm_ptr(base);size_t n=u16(p),at=0,value,children;
 if(!gm_accessible(base,(uint32_t)n,0) || !block(p,n,0,&value,&children))RET(0,4);
 const char *key=ASTR(1);while(*key=='\\')key++;
 while(*key){
  const char *slash=strchr(key,'\\');size_t len=slash?(size_t)(slash-key):strlen(key),end=at+u16(p+at);int found=0;
  for(size_t child=children;span(end,child,6);){
   size_t v,c;if(!block(p,end,child,&v,&c))break;
   if(key_equal(p,child+u16(p+child),child+6,key,len)){at=child;value=v;children=c;found=1;break;}
   child=align4(child+u16(p+child));
  }
  if(!found)RET(0,4);key=slash?slash+1:key+len;
 }
 uint32_t count=u16(p+at+2);
 if(u16(p+at+4) && count){
  /* Conversion storage belongs to the caller's GetFileVersionInfo buffer. */
  if(base>UINT32_MAX-n-8 || !gm_accessible(base+(uint32_t)n,8,0) || MEM32(base+n)!=0x31565343)RET(0,4);
  uint32_t capacity=MEM32(base+n+4);size_t ansi=n+8+value;
  if(ansi>capacity || count>capacity-ansi || !gm_accessible(base+(uint32_t)ansi,count,1))RET(0,4);
  for(uint32_t i=0;i<count;i++){uint16_t c=u16(p+value+i*2);if(c>127){hle_set_last_error(1113);RET(0,4);}p[ansi+i]=(unsigned char)c;}
  value=ansi;
 }
 MEM32(result)=base+(uint32_t)value;MEM32(length)=count;RET(1,4);
}
const win32hle_shim win32hle_version[]={
 {"GetFileVersionInfoSizeA",version_size},{"GetFileVersionInfoA",version_info},{"VerQueryValueA",version_query},{0,0}
};
