/* Kernel handle lifetime and profile boundary checks through guest ABI. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "win32hle.h"
static uint32_t call(const char *name,unsigned n,uint32_t *a){return hle_call_guest(hle_resolve(name),(int)n,a);}
#define K_CHECK(c) do {if(!(c)){fprintf(stderr,"[kernel-probe] failed line %d: %s\n",__LINE__,#c);return 13;}}while(0)
static uint32_t worker_thread_handle;
static void worker(void){
 uint32_t out=gm_va(gm_calloc(1,4)),a[]={UINT32_MAX,0xfffffffeu,UINT32_MAX,out,0,0,2};
 if(!call("DuplicateHandle",7,a))hle_fatal("worker thread pseudo-handle duplicate failed");worker_thread_handle=MEM32(out);gm_free(gm_ptr(out));RET(0xc516,1);
}
static void import_identity(void){
 if(!g_cur_import||strcmp(g_cur_import,"KernelProbe.ImportIdentity"))hle_fatal("Wrong entry import identity");
 call("GetTickCount",0,NULL);
 if(!g_cur_import||strcmp(g_cur_import,"KernelProbe.ImportIdentity"))hle_fatal("Nested import lost outer identity");
 RET(1,0);
}
static void early_exit(void){uint32_t code=0xE817;call("ExitThread",1,&code);hle_fatal("ExitThread returned");}
static const win32hle_shim methods[]={{"KernelProbe.ExitWorker",early_exit},{"KernelProbe.Worker",worker},{"KernelProbe.ImportIdentity",import_identity},{0,0}};
int cs16_probe_kernel(void){
 uint32_t out=gm_va(gm_calloc(1,4)),a[8]={0};
 a[1]=1;uint32_t event=call("CreateEventA",4,a);K_CHECK(event);
 uint32_t dup[]={UINT32_MAX,event,UINT32_MAX,out,0,0,2};K_CHECK(call("DuplicateHandle",7,dup));uint32_t copy=MEM32(out);K_CHECK(copy&&copy!=event);
 K_CHECK(call("CloseHandle",1,&event));K_CHECK(!call("CloseHandle",1,&event));K_CHECK(call("SetEvent",1,&copy));a[0]=copy;a[1]=0;K_CHECK(call("WaitForSingleObject",2,a)==0);K_CHECK(call("CloseHandle",1,&copy));
 memset(a,0,sizeof a);event=call("CreateEventA",4,a);dup[1]=event;dup[6]=3;K_CHECK(call("DuplicateHandle",7,dup));copy=MEM32(out);K_CHECK(!call("SetEvent",1,&event));K_CHECK(call("CloseHandle",1,&copy));
 dup[1]=0xfffffffeu;dup[6]=2;K_CHECK(call("DuplicateHandle",7,dup));copy=MEM32(out);a[0]=copy;a[1]=0;K_CHECK(call("WaitForSingleObject",2,a)==258);K_CHECK(call("CloseHandle",1,&copy));
 dup[1]=0xdeadbeef;K_CHECK(!call("DuplicateHandle",7,dup));K_CHECK(call("GetLastError",0,NULL)==6);
 win32hle_register(methods);K_CHECK(call("KernelProbe.ImportIdentity",0,NULL));memset(a,0,sizeof a);a[2]=hle_resolve("KernelProbe.Worker");a[4]=4;uint32_t thread=call("CreateThread",6,a);K_CHECK(thread);
 dup[1]=thread;K_CHECK(call("DuplicateHandle",7,dup));copy=MEM32(out);K_CHECK(call("CloseHandle",1,&thread));K_CHECK(call("ResumeThread",1,&copy)==1);
 a[0]=copy;a[1]=3000;K_CHECK(call("WaitForSingleObject",2,a)==0);a[1]=out;K_CHECK(call("GetExitCodeThread",2,a)&&MEM32(out)==0xc516);
 a[0]=worker_thread_handle;a[1]=out;K_CHECK(call("GetExitCodeThread",2,a)&&MEM32(out)==0xc516);K_CHECK(call("CloseHandle",1,&copy));K_CHECK(call("CloseHandle",1,&worker_thread_handle));
 memset(a,0,sizeof a);a[2]=hle_resolve("KernelProbe.ExitWorker");thread=call("CreateThread",6,a);K_CHECK(thread);a[0]=thread;a[1]=3000;K_CHECK(call("WaitForSingleObject",2,a)==0);a[1]=out;K_CHECK(call("GetExitCodeThread",2,a)&&MEM32(out)==0xE817);K_CHECK(call("CloseHandle",1,&thread));call("GetTickCount",0,NULL);
 char file[]="cs16-profile-probe-XXXXXX";int fd=mkstemp(file);K_CHECK(fd>=0);FILE *f=fdopen(fd,"w");K_CHECK(f);fputs("[Audio]\n device = \"quoted\"\nRate=44100\n[Other]\nKey=yes\n",f);K_CHECK(!fclose(f));
 /* File-backed views survive file/mapping handle closure; rejected offsets
  * and output guards cover the ABI used by original engine DLL inspection. */
 uint32_t info=gm_va(gm_calloc(1,144));MEM32(info)=MEM32(info+140)=0xcafebabe;
 uint32_t open_args[]={gm_va(gm_strdup(file)),info+4,0};uint32_t opened=call("OpenFile",3,open_args);K_CHECK(opened&&opened!=UINT32_MAX);
 K_CHECK(MEM32(info)==0xcafebabe&&MEM32(info+140)==0xcafebabe&&MEM8(info+4)==136);
 uint32_t map_args[]={opened,0,2,0,0,gm_va(gm_strdup("CS16-KernelProbe-ReadOnly"))};uint32_t mapping=call("CreateFileMappingA",6,map_args);K_CHECK(mapping);
 uint32_t named_args[]={4,0,map_args[5]};uint32_t named=call("OpenFileMappingA",3,named_args);K_CHECK(named&&named!=mapping);
 uint32_t view_args[]={mapping,4,0,1,0};K_CHECK(!call("MapViewOfFile",5,view_args)&&call("GetLastError",0,NULL)==87);view_args[3]=0;
 uint32_t view=call("MapViewOfFile",5,view_args);K_CHECK(view&&!memcmp(gm_ptr(view),"[Audio]",7));K_CHECK(!gm_accessible(view,7,1));
 K_CHECK(call("CloseHandle",1,&mapping));K_CHECK(call("CloseHandle",1,&opened));K_CHECK(!memcmp(gm_ptr(view),"[Audio]",7));K_CHECK(call("CloseHandle",1,&named));
 K_CHECK(!call("OpenFileMappingA",3,named_args)&&call("GetLastError",0,NULL)==2);
 K_CHECK(call("UnmapViewOfFile",1,&view)&&!gm_accessible(view,7,0));K_CHECK(!call("UnmapViewOfFile",1,&view)&&call("GetLastError",0,NULL)==487);
 gm_free(gm_ptr(info));
 uint32_t registry[48]={0};for(unsigned i=0;i<48;i++){char text[64];snprintf(text,sizeof text,"C:\\KernelProbe%u.dll",i);int index=hle_module_add(text,0x61000000+i*0x10000,0,0,0);K_CHECK(index>=0);uint32_t name=gm_va(gm_strdup(text));registry[i]=call("GetModuleHandleA",1,&name);K_CHECK(registry[i]==0x61000000+i*0x10000);gm_free(gm_ptr(name));}
 uint32_t wide=gm_va(gm_calloc(64,2));const char *known="KeRnEl32.DlL";for(unsigned i=0;known[i];i++)MEM16(wide+i*2)=(uint8_t)known[i];a[0]=wide;uint32_t module=call("LoadLibraryW",1,a);K_CHECK(module&&call("GetModuleHandleW",1,a)==module);MEM16(wide)=0xd800;MEM16(wide+2)=0;K_CHECK(!call("GetModuleHandleW",1,a)&&call("GetLastError",0,NULL)==1113);
 uint32_t cs=gm_va(gm_calloc(1,32));MEM32(cs)=MEM32(cs+28)=0xcafebabe;a[0]=cs+4;a[1]=4000;K_CHECK(call("InitializeCriticalSectionAndSpinCount",2,a));K_CHECK(MEM32(cs+8)==UINT32_MAX&&MEM32(cs+24)==4000);call("EnterCriticalSection",1,a);call("EnterCriticalSection",1,a);K_CHECK(MEM32(cs+12)==2);call("LeaveCriticalSection",1,a);call("LeaveCriticalSection",1,a);K_CHECK(!MEM32(cs+12)&&MEM32(cs)==0xcafebabe&&MEM32(cs+28)==0xcafebabe);
 uint32_t buf=gm_va(gm_calloc(1,80));MEM32(buf)=MEM32(buf+76)=0xcafebabe;uint32_t profile[]={gm_va(gm_strdup("audio")),gm_va(gm_strdup("rate")),gm_va(gm_strdup("fallback  ")),buf+4,72,gm_va(gm_strdup(file))};
 K_CHECK(call("GetPrivateProfileStringA",6,profile)==5&&!strcmp(gm_ptr(buf+4),"44100"));
 profile[1]=gm_va(gm_strdup("device"));K_CHECK(call("GetPrivateProfileStringA",6,profile)==6&&!strcmp(gm_ptr(buf+4),"quoted"));
 profile[1]=gm_va(gm_strdup("missing"));K_CHECK(call("GetPrivateProfileStringA",6,profile)==8&&!strcmp(gm_ptr(buf+4),"fallback"));
 profile[1]=0;uint32_t n=call("GetPrivateProfileStringA",6,profile);K_CHECK(n==12&&!memcmp(gm_ptr(buf+4),"device\0Rate\0\0",13));
 profile[0]=0;n=call("GetPrivateProfileStringA",6,profile);K_CHECK(n==12&&!memcmp(gm_ptr(buf+4),"Audio\0Other\0\0",13));
 profile[4]=5;K_CHECK(call("GetPrivateProfileStringA",6,profile)==3&&!memcmp(gm_ptr(buf+4),"Aud\0\0",5));
 profile[0]=gm_va(gm_strdup("NoSuchSection-CS16-Probe"));profile[1]=gm_va(gm_strdup("missing"));profile[4]=1;K_CHECK(!call("GetProfileStringA",5,profile)&&!MEM8(buf+4));
 profile[4]=0;MEM8(buf+4)=0x51;K_CHECK(!call("GetProfileStringA",5,profile)&&MEM8(buf+4)==0x51);K_CHECK(MEM32(buf)==0xcafebabe&&MEM32(buf+76)==0xcafebabe);unlink(file);
 printf("{\"kernelProbe\":true,\"exitThreadCodeAndMachineRelease\":true,\"readOnlyFileMappingLifetime\":true,\"fileMappingBounds\":true,\"nestedImportIdentity\":true,\"duplicateSharesObject\":true,\"independentClose\":true,\"closeSource\":true,\"pseudoThreadConverted\":true,\"workerThreadIdentityAndExit\":true,\"invalidHandleRejected\":true,\"profileCaseInsensitive\":true,\"profileEnumeration\":true,\"profileTruncationBounds\":true,\"zeroLengthOutputUntouched\":true,\"moduleRegistryBeyond32\":true,\"unicodeModuleNames\":true,\"recursiveCriticalSection\":true}\n");return 0;
}
