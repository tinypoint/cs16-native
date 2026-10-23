/* Runs translated original launcher code. Missing interfaces stop explicitly. */
#include <signal.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "win32hle.h"
#include "pe_loader.h"

static void fault(int sig,siginfo_t *info,void *context){
 (void)context;
 fprintf(stderr,"[cs16-boot] signal=%d address=%p guest=%08x import=%s esp=%08x\n",sig,info?info->si_addr:NULL,g_cur_func,g_cur_import?g_cur_import:"none",g_esp);
 if(getenv("CS16_CALLBACK_TRACE")){
  void *frames[32];int count=backtrace(frames,32);backtrace_symbols_fd(frames,count,2);
  fprintf(stderr,"[guest-fault] eax=%08x ecx=%08x edx=%08x ebp=%08x\n",g_eax,g_ecx,g_edx,g_ebp);
  if(gm_accessible(g_esp,64,0)){fprintf(stderr,"[guest-fault] stack:");for(unsigned i=0;i<16;i++)fprintf(stderr," %08x",MEM32(g_esp+i*4));fputc('\n',stderr);}
 }
 _exit(128+sig);
}
static void timeout(int sig){(void)sig;fprintf(stderr,"[cs16-boot] watchdog guest=%08x import=%s\n",g_cur_func,g_cur_import?g_cur_import:"none");_exit(124);}
void recomp_not_lifted(uint32_t va){hle_fatal("untranslated function %08x",va);}
extern const win32hle_shim win32hle_path_extra[];
extern const win32hle_shim win32hle_version[];
extern void cs16_original_modules_install(void);
extern void cs16_native_window_install(void);
extern int cs16_probe_ui(void);
extern int cs16_probe_x87(void);
extern int cs16_probe_exceptions(void);
extern int cs16_probe_browser(void);
extern int cs16_probe_kernel(void);
extern int cs16_probe_audio(void);
extern int cs16_probe_network(void);
extern void cs16_opengl_register(void);
extern void cs16_webbrowser_install(void);
extern const win32hle_shim win32hle_crypto_random[];
static void messagebox(void){
 fprintf(stderr,"[cs16-boot] original dialog title=%s text=%s flags=%08x\n",ASTR(2)?ASTR(2):"",ASTR(1)?ASTR(1):"",A32(3));
 hle_fatal("Original program requested a dialog; diagnostic host stops here");
}
static int probe_filesystem(void){
 uint32_t arg=gm_va(gm_strdup("filesystem_stdio.dll"));
 uint32_t module=hle_call_guest(hle_resolve("LoadLibraryA"),1,&arg);
 if(module!=0x40000000u)return 8;
 uint32_t name=gm_va(gm_strdup("CreateInterface")),query[]={module,name};
 uint32_t factory=hle_call_guest(hle_resolve("GetProcAddress"),2,query);
 if(factory!=0x40005570u)return 8;
 query[1]=1;if(hle_call_guest(hle_resolve("GetProcAddress"),2,query)!=factory)return 8;
 query[1]=2;if(hle_call_guest(hle_resolve("GetProcAddress"),2,query))return 8;
 query[1]=gm_va(gm_strdup("NoSuchExport"));
 if(hle_call_guest(hle_resolve("GetProcAddress"),2,query))return 8;
 uint32_t result=gm_va(gm_calloc(1,4));
 uint32_t args[]={gm_va(gm_strdup("VFileSystem009")),result};
 MEM32(result)=99;uint32_t object=hle_call_guest(factory,2,args);
 if(!object || MEM32(result)!=0 || !gm_accessible(object,4,0))return 8;
 uint32_t vtable=MEM32(object);
 if(!gm_accessible(vtable,4,0) || !recomp_lookup(MEM32(vtable)))return 8;
 args[0]=gm_va(gm_strdup("NoSuchFileSystem"));MEM32(result)=99;
 if(hle_call_guest(factory,2,args) || MEM32(result)!=1)return 8;
 if(hle_call_guest(hle_resolve("LoadLibraryA"),1,&arg)!=module)return 8;
 printf("{\"originalFilesystemProbe\":true,\"module\":%u,\"factory\":%u,\"object\":%u,\"vtable\":%u,\"validInterface\":true,\"invalidInterfaceRejected\":true,\"missingExportRejected\":true,\"ordinalLookup\":true,\"gameLaunched\":false}\n",module,factory,object,vtable);
 return 0;
}
static int probe_version(void){
 uint32_t name=gm_va(gm_strdup("C:\\Steam.dll")),zero=gm_va(gm_calloc(1,4));
 uint32_t sizeargs[]={name,zero};uint32_t size=hle_call_guest(hle_resolve("GetFileVersionInfoSizeA"),2,sizeargs);
 if(!size || MEM32(zero))return 9;
 uint32_t buffer=gm_va(gm_alloc(size)),args[]={name,0,size,buffer};
 if(!hle_call_guest(hle_resolve("GetFileVersionInfoA"),4,args))return 9;
 uint32_t pointer=gm_va(gm_calloc(1,4)),length=gm_va(gm_calloc(1,4));
 uint32_t query[]={buffer,gm_va(gm_strdup("\\")),pointer,length};
 if(!hle_call_guest(hle_resolve("VerQueryValueA"),4,query) || MEM32(length)!=52)return 9;
 uint32_t fixed=MEM32(pointer);if(MEM32(fixed)!=0xfeef04bd)return 9;
 printf("{\"versionProbe\":true,\"fileVersionMS\":%u,\"fileVersionLS\":%u",MEM32(fixed+8),MEM32(fixed+12));
 query[1]=gm_va(gm_strdup("\\StringFileInfo\\040904b0\\FileVersion"));
 if(!hle_call_guest(hle_resolve("VerQueryValueA"),4,query))return 9;
 printf(",\"fileVersionText\":\"%s\"",(char *)gm_ptr(MEM32(pointer)));
 query[1]=gm_va(gm_strdup("\\NoSuchBlock"));
 if(hle_call_guest(hle_resolve("VerQueryValueA"),4,query) || MEM32(length))return 9;
 name=gm_va(gm_strdup("C:\\missing-version-file.dll"));sizeargs[0]=name;
 if(hle_call_guest(hle_resolve("GetFileVersionInfoSizeA"),2,sizeargs))return 9;
 printf(",\"missingQueryRejected\":true,\"missingFileRejected\":true}\n");return 0;
}
int main(int argc,char **argv){
 if((argc!=2 && argc!=3)||!gm_init())return 2;
 stack_t alternate={0};alternate.ss_sp=malloc(64*1024);alternate.ss_size=64*1024;
 if(!alternate.ss_sp || sigaltstack(&alternate,NULL))return 2;
 struct sigaction sa={0};sa.sa_sigaction=fault;sa.sa_flags=SA_SIGINFO|SA_ONSTACK;
 sigaction(SIGSEGV,&sa,NULL);sigaction(SIGBUS,&sa,NULL);sigaction(SIGABRT,&sa,NULL);
 signal(SIGALRM,timeout);alarm(15);
 win32hle_register(win32hle_kernel32);win32hle_register(win32hle_kernel32_crt);
 win32hle_register(win32hle_kernel32_ext);win32hle_register(win32hle_module);
 win32hle_register(win32hle_winmm);win32hle_register(win32hle_wsock32);win32hle_register(win32hle_advapi32);
 win32hle_register(win32hle_user32);
 win32hle_register(win32hle_dsound);
 win32hle_register(win32hle_gdi32);win32hle_register(win32hle_gdidc);
 win32hle_register(win32hle_ole32);win32hle_register(win32hle_ddraw);
 win32hle_register(win32hle_path_extra);
 win32hle_register(win32hle_version);
 cs16_original_modules_install();
 cs16_native_window_install();
 cs16_opengl_register();
 cs16_webbrowser_install();
 win32hle_register(win32hle_crypto_random);
 static const win32hle_shim diagnostics[]={{"MessageBoxA",messagebox},{0,0}};win32hle_register(diagnostics);
 char directory[4096];if(!getcwd(directory,sizeof directory))return 2;
 hle_set_drive('C',directory);hle_set_command_line("hl.exe -game cstrike -console -windowed");
 pe_image image={0};if(recomp_pe_map(argv[1],&image))return 3;
 char guest[4096];hle_guest_path(argv[1],guest,sizeof guest);
 hle_module_add(guest,image.base,image.resource_rva,image.resource_size,1);
 if(recomp_pe_bind(&image,hle_resolve_or_stub))return 3;
 win32hle_trace=getenv("CS16_TRACE")!=NULL;
 if(argc==3 && !strcmp(argv[2],"--probe-filesystem"))return probe_filesystem();
 if(argc==3 && !strcmp(argv[2],"--probe-version"))return probe_version();
 if(argc==3 && !strcmp(argv[2],"--probe-ui"))return cs16_probe_ui();
 if(argc==3 && !strcmp(argv[2],"--probe-x87"))return cs16_probe_x87();
 if(argc==3 && !strcmp(argv[2],"--probe-network"))return cs16_probe_network();
 if(argc==3 && !strcmp(argv[2],"--probe-audio"))return cs16_probe_audio();
 if(argc==3 && !strcmp(argv[2],"--probe-kernel"))return cs16_probe_kernel();
 if(argc==3 && !strcmp(argv[2],"--probe-browser"))return cs16_probe_browser();
 if(argc==3 && !strcmp(argv[2],"--probe-exception"))return cs16_probe_exceptions();
 fprintf(stderr,"[cs16-boot] original launcher CRT entry=%08x target=ARM64\n",image.entry);
 uint32_t result=hle_call_guest(image.entry,0,NULL);
 fprintf(stderr,"[cs16-boot] original launcher returned %08x\n",result);
 return 0;
}
