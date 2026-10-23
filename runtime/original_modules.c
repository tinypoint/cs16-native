/* Fixed AOT mappings for original DLLs. No game implementation is substituted. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "win32hle.h"
#include "pe_loader.h"

extern int (*hle_load_translated_module)(const char *,pe_image *);
extern int (*hle_attach_translated_module)(const pe_image *);

static int map_module(const char *path,pe_image *image){
 const char *name=strrchr(path,'/');name=name?name+1:path;
 if(!strcasecmp(name,"filesystem_stdio.dll"))return recomp_pe_map_fixed(path,image,0x40000000);
 if(!strcasecmp(name,"vgui.dll"))return recomp_pe_map_fixed(path,image,0x40100000);
 if(!strcasecmp(name,"Steam.dll"))return recomp_pe_map_fixed(path,image,0x20000000);
 if(!strcasecmp(name,"Mss32.dll"))return recomp_pe_map_fixed(path,image,0x21100000);
 if(!strcasecmp(name,"dbg.dll"))return recomp_pe_map_fixed(path,image,0x40200000);
 if(!strcasecmp(name,"particleman.dll"))return recomp_pe_map_fixed(path,image,0x40300000);
 if(!strcasecmp(name,"vgui2.dll"))return recomp_pe_map_fixed(path,image,0x41000000);
 if(!strcasecmp(name,"GameUI.dll"))return recomp_pe_map_fixed(path,image,0x43000000);
 if(!strcasecmp(name,"DemoPlayer.dll"))return recomp_pe_map_fixed(path,image,0x45000000);
 hle_fatal("original DLL has no translated module registered yet: %s",name);
 return 1;
}
static int is_system(const char *dll){
 static const char *names[]={"kernel32.dll","user32.dll","gdi32.dll","advapi32.dll","shell32.dll","ole32.dll","oleaut32.dll","winmm.dll","wsock32.dll","ws2_32.dll","ddraw.dll","dsound.dll","dinput.dll","version.dll","comctl32.dll","comdlg32.dll","ntdll.dll","msvcrt.dll","imm32.dll","mpr.dll","rpcrt4.dll","uuid.dll","dplayx.dll","d3dim.dll","wininet.dll","shlwapi.dll","mswsock.dll"};
 for(size_t i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcasecmp(names[i],dll))return 1;
 return 0;
}
uint32_t cs16_resolve_import(const char *dll,const char *name,uint32_t ordinal){
 uint32_t module_name=gm_va(gm_strdup(dll));
 uint32_t module=hle_call_guest(hle_resolve("LoadLibraryA"),1,&module_name);
 if(!module)hle_fatal("Required original import module unavailable: %s",dll);
 uint32_t query[]={module,name?gm_va(gm_strdup(name)):ordinal};
 uint32_t result=hle_call_guest(hle_resolve("GetProcAddress"),2,query);
 if(!result && is_system(dll)){
  char key[256];
  if(name)snprintf(key,sizeof key,"%s",name);
  else {
   snprintf(key,sizeof key,"%s#%u",dll,ordinal);
   for(char *p=key;*p;p++)if(*p>='A'&&*p<='Z')*p+=32;
  }
  /* A required real import gets an explicit failure trap, never a no-op.
     Feature probes through GetProcAddress retain ordinary missing-export behavior. */
  result=hle_resolve_or_stub(strdup(key));
 }
 if(!result)hle_fatal("Required original import unresolved: %s!%s ordinal=%u",dll,name?name:"",ordinal);
 return result;
}
static int attach_module(const pe_image *image){
 if(recomp_pe_bind_modules(image,cs16_resolve_import))return 0;
 if(!image->entry)return 1;
 uint32_t args[]={image->base,1,0};
 fprintf(stderr,"[cs16-module] entering original DllMain %08x\n",image->entry);
 uint32_t result=hle_call_guest(image->entry,3,args);
 fprintf(stderr,"[cs16-module] original DllMain returned %u\n",result);
 return result!=0;
}
void cs16_original_modules_install(void){
 hle_load_translated_module=map_module;hle_attach_translated_module=attach_module;
}
