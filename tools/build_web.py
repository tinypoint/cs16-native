#!/usr/bin/env python3
"""Experimental original-program Wasm build; never substitutes game logic.

Platform materializations remain in .local. The native build is unchanged.
"""
import argparse,hashlib,json,os,re,subprocess,time
from concurrent.futures import ThreadPoolExecutor,as_completed
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MODULES=('hl','filesystem','vgui','steam','mss32','dbg','hw','cstrike-client','particleman','vgui2','gameui','demoplayer','mp3dec','mssv29','mssv12','voice-miles','core','swda','trackerui','serverbrowser','trackernet','steamclient','vstdlib','tier0')
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--emsdk',type=Path,required=True);ap.add_argument('--memory-probe',action='store_true');ap.add_argument('--adapter-probe',action='store_true');ap.add_argument('--profile-roots',action='store_true');ap.add_argument('--entry-selection',type=Path);args=ap.parse_args()
 sdk=args.emsdk.resolve();node=next((sdk/'node').glob('*/bin/node'));emcc=sdk/'upstream/emscripten/emcc';out=ROOT/'.local/web-build';inc=out/'include';obj=out/'objects';host=out/'host'
 for p in [out,inc,obj,host]:p.mkdir(parents=True,exist_ok=True)
 env={**os.environ,'EM_CACHE':str(ROOT/'.local/emscripten-cache'),'EMSDK_PYTHON':str(ROOT/'.venv/bin/python'),'PATH':str(node.parent)+':'+os.environ['PATH']}
 if args.memory_probe:
  command=[str(emcc),'-O1','-I',str(ROOT/'runtime/guest'),str(ROOT/'runtime/web/guest_memory.c'),str(ROOT/'runtime/web/memory_probe.c'),'-sENVIRONMENT=node','-sINITIAL_MEMORY=67108864','-sALLOW_MEMORY_GROWTH=1','-sMAXIMUM_MEMORY=268435456','-o',str(out/'memory-probe.js')]
  subprocess.run(command,env=env,check=True);p=subprocess.run([str(node),str(out/'memory-probe.js')],capture_output=True,text=True,check=True);(ROOT/'logs/web-memory-probe.log').write_text(p.stdout+p.stderr);print(p.stdout);return
 # Shadow the upstream address macro before its inline REP/SIMD helpers.
 header=(ROOT/'vendor/pcrecomp/runtime/recomp32/recomp_types.h').read_text();needle='#define ADDR(va)     ((uintptr_t)(uint32_t)(va) + g_mem_base)';assert header.count(needle)==1
 (inc/'recomp_types.h').write_text(header.replace(needle,'void *gm_ptr(uint32_t va);\n#define ADDR(va) ((uintptr_t)gm_ptr((uint32_t)(va)))'))
 if args.adapter_probe:
  target=out/'adapter-probe.js'
  subprocess.run([str(emcc),'-O1','-I',str(inc),'-I',str(ROOT/'runtime/win32'),'-I',str(ROOT/'runtime/guest'),'-I',str(ROOT/'vendor/pcrecomp/runtime/recomp32'),str(ROOT/'runtime/web/guest_memory.c'),str(ROOT/'runtime/web/iphlpapi.c'),str(ROOT/'runtime/web/adapter_probe.c'),str(ROOT/'runtime/web/socket_options.c'),'-sENVIRONMENT=node','-sINITIAL_MEMORY=67108864','-sALLOW_MEMORY_GROWTH=1','-o',str(target)],env=env,check=True)
  p=subprocess.run([str(node),str(target)],capture_output=True,text=True,check=True);(ROOT/'logs/web-adapter-probe.log').write_text(p.stdout+p.stderr);print(p.stdout);return
 sources=[];entries=[];selected={int(a,0) for a in json.loads(args.entry_selection.read_text())['roots']} if args.entry_selection else None
 for name in MODULES:
  folder=ROOT/'.local/lift'/name;result=json.loads((folder/'result.json').read_text());assert not result['generationErrors'],name
  gen=out/'generated'/name;gen.mkdir(parents=True,exist_ok=True);(gen/'recomp_funcs.h').write_bytes((folder/'recomp_funcs.h').read_bytes())
  entries += [(int(a,16),symbol) for a,symbol in re.findall(r'\{0x([0-9A-Fa-f]+),\s*(sub_[0-9A-Fa-f]+)\}',(folder/'recomp_dispatch.c').read_text())]
  for p in sorted(folder.glob('recomp_*.c')):
   if p.name=='recomp_dispatch.c':continue
   s=p.read_text()
   if selected is not None:
    functions=list(re.finditer(r'^void sub_([0-9A-Fa-f]+)\(void\)\s*\{',s,re.M));parts=[s[:functions[0].start()]+'\nvoid cs16_web_unloaded(uint32_t);\n']
    for i,m in enumerate(functions):
     end=functions[i+1].start() if i+1<len(functions) else len(s)
     parts.append(s[m.start():end] if int(m[1],16) in selected else 'void sub_'+m[1]+'(void){cs16_web_unloaded(0x'+m[1]+'u); }\n')
    s=''.join(parts)
   s=s.replace('#include "recomp_types.h"','#include "recomp_types.h"\n#include "cs16_dispatch.h"');target=gen/p.name;target.write_text(s);sources.append(target)
 entries.sort();assert len(entries)==len({a for a,_ in entries})
 if selected is not None and (unknown:=selected-{a for a,_ in entries}):raise SystemExit('Selected original entries are absent from these modules: '+str(sorted(unknown)))
 dispatch=host/'combined_dispatch.c';dispatch.write_text('#include "recomp_types.h"\n'+''.join('void '+n+'(void);\n' for _,n in entries)+'const recomp_dispatch_entry_t recomp_dispatch_table[]={\n'+''.join(f'{{0x{a:08X},{n}}},\n' for a,n in entries)+'};\nconst uint32_t recomp_dispatch_count='+str(len(entries))+';\n')
 for p in sorted((ROOT/'runtime/win32').glob('*.c')):
  if p.name=='iphlpapi.c':continue
  s=p.read_text().replace('st_mtimespec','st_mtim').replace('st_atimespec','st_atim').replace('#include <SDL2_ttf/SDL_ttf.h>','#include <SDL2/SDL_ttf.h>').replace('h->sin_len=sizeof *h;','/* No Darwin sin_len. */')
  if p.name=='crypto_random.c':
   s=s.replace('#include <stdint.h>','#include <stdint.h>\n#include <unistd.h>')
   s=s.replace('arc4random_buf(gm_ptr(out),size);','for(uint32_t i=0;i<size;){uint32_t n=size-i>256?256:size-i;if(getentropy(gm_ptr(out+i),n)){hle_set_last_error(0x80090020);RET(0,3);}i+=n;}')
  if p.name=='kernel32_crt.c':
   s=s.replace('#include <execinfo.h>','').replace('void *frames[24];int frame_count=backtrace(frames,24);','/* Native diagnostic backtraces unavailable in browser. */').replace('backtrace_symbols_fd(frames,frame_count,2);','')
  if p.name=='kernel32_ext.c':
   s=s.replace('#include <sys/sysctl.h>','');a=s.index('static void k_IsDebuggerPresent(void)');b=s.index('const win32hle_shim win32hle_kernel32_ext',a);s=s[:a]+'/* Browser guest has no attached Windows process debugger. */\nstatic void k_IsDebuggerPresent(void){RET(0,0);}\n'+s[b:]
  if p.name=='kernel32_ext.c':
   marker='const win32hle_shim win32hle_kernel32_ext[] = {'
   s=s.replace(marker,'/* Browser does not expose process CPU accounting. */\nstatic void web_GetProcessTimes(void){hle_set_last_error(120);RET(0,5); }\n'+marker+'\n    {"GetProcessTimes",web_GetProcessTimes},')
  if p.name=='opengl.c':
   from web_gl import patch,bindings
   library='\n'.join(p.read_text() for p in (sdk/'upstream/emscripten/src/lib').glob('lib*gl*.js'));available=set(re.findall(r'\b(gl\w+)\s*:',library))
   for table in re.findall(r'var glPassthroughFuncs\s*=\s*(\[[\s\S]*?\n\];)',library):
    for group in re.findall(r"'([^']+)'",table):
     for name in group.split():
      name=name.replace('[','').replace(']','').rstrip('*');available.add('gl'+name[0].upper()+name[1:])
   s=patch(s);(host/'opengl_bindings.inc').write_text(bindings((ROOT/'runtime/win32/opengl_bindings.inc').read_text(),available))
  if p.name=='native_window.c':s=s.replace('SDL_GL_CONTEXT_MINOR_VERSION,1','SDL_GL_CONTEXT_MINOR_VERSION,0')
  if p.name=='gdidc.c':
   a=s.index('static const char *font_file(');b=s.index('static font_t *FONT',a);s=s[:a]+'static const char *font_file(const char *face,int bold,int italic){(void)face;(void)bold;(void)italic;return "/game/fonts/LiberationSans-Regular.ttf";}\n'+s[b:]
  if p.name=='wsock32.c':
   s=s.replace('#define FAIL(n) do {', '#define FAIL(n) do { if(errno!=EWOULDBLOCK)fprintf(stderr,"[web-socket-error] %s errno=%d from=%08x\\n",__func__,errno,g_cur_func);')
   s=s.replace('#define SOCK_BASE', 'extern int cs16_web_socket_buffer(int,int,int);\nextern int cs16_web_socket_can_send(int,unsigned);\n#define SOCK_BASE')
   s=s.replace('if (setsockopt(fd_of(A32(0)), hl, ho, &v, sizeof v) != 0) FAIL(5);','if(A32(1)==0xffffu&&(A32(2)==0x1001||A32(2)==0x1002)){int error=cs16_web_socket_buffer(fd_of(A32(0)),A32(2),v);if(error){errno=error;FAIL(5);}RET(0,5); }if (setsockopt(fd_of(A32(0)), hl, ho, &v, sizeof v) != 0) FAIL(5);')
   s=s.replace('ssize_t n;\n    if (A32(4))', 'ssize_t n;\n    if(!cs16_web_socket_can_send(fd_of(A32(0)),A32(2))){g_wsa_error=WSAEWOULDBLOCK;RET(SOCKET_ERROR,6); }\n    if (A32(4))')
   s=s.replace('static void w_send(void) { ssize_t n', 'static void w_send(void) { if(!cs16_web_socket_can_send(fd_of(A32(0)),A32(2))){g_wsa_error=WSAEWOULDBLOCK;RET(SOCKET_ERROR,4);} ssize_t n')
  if p.name=='webbrowser.c':s=s.replace('#include "../platform/browser_native.h"','#include "browser_native.h"')
  target=host/p.name;target.write_text(s);sources.append(target)
 sources += [p for p in sorted((ROOT/'runtime/guest').glob('*.c')) if p.name!='guest_memory.c']
 sources += [ROOT/'runtime/web/guest_memory.c',ROOT/'runtime/web/browser_backend.c',ROOT/'runtime/web/iphlpapi.c',ROOT/'runtime/web/present.c',ROOT/'runtime/web/demand.c',ROOT/'runtime/web/socket_options.c',ROOT/'runtime/original_modules.c',dispatch]
 s=(ROOT/'runtime/launcher_host.c').read_text().replace('#include <execinfo.h>','');a=s.index('static void fault(');b=s.index('void recomp_not_lifted(',a);s=s[:a]+s[b:];a=s.index(' stack_t alternate');b=s.index(' win32hle_register(',a);s=s[:a]+' chdir("/game");setenv("HLE_FILETRACE","1",1);\n'+s[b:];s=s.replace('chdir("/game");','chdir("/game");setenv("CS16_PROFILE_TARGETS","1",1);') if args.profile_roots else s;target=host/'web_boot.c';target.write_text(s.replace('target=ARM64','target=WebAssembly'));sources.append(target)
 flags=['-O1','-g1','-pthread','-mtail-call','-DRECOMP_LOCAL_REGS','-fno-strict-aliasing','-fwrapv','-ffp-contract=off','-include',str(ROOT/'runtime/guest/guest_cpu.h'),'-I',str(inc),'-I',str(ROOT/'runtime/win32'),'-I',str(ROOT/'runtime/guest'),'-I',str(ROOT/'runtime/platform'),'-I',str(ROOT/'vendor/pcrecomp/runtime/recomp32'),'--use-port=sdl2','--use-port=sdl2_ttf']
 # Original headers can include one another; retain the generated headers' path.
 signature=hashlib.sha256((' '.join(flags)).encode()+b''.join(p.read_bytes() for folder in [inc,ROOT/'runtime/win32',ROOT/'runtime/guest',ROOT/'runtime/platform'] for p in sorted(folder.glob('*.h')))).digest()
 def compile_one(source):
  name=source.stem+'-'+hashlib.sha256(str(source).encode()).hexdigest()[:10];dest=obj/(name+'.o');stamp=dest.with_suffix('.sha');key=hashlib.sha256(signature+source.read_bytes()+((host/'opengl_bindings.inc').read_bytes() if source.name=='opengl.c' else b'')).hexdigest()
  if dest.exists() and stamp.exists() and stamp.read_text()==key:return dest
  p=subprocess.run([str(emcc),*flags,'-I',str(source.parent),'-c',str(source),'-o',str(dest)],env=env,capture_output=True,text=True,timeout=600);(out/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:raise RuntimeError(source.name+': '+p.stderr[-2200:])
  stamp.write_text(key);return dest
 # Build shared port variants before concurrent compiler invocations.
 compile_one(ROOT/'runtime/web/guest_memory.c');start=time.monotonic();objects=[];errors=[]
 with ThreadPoolExecutor(max_workers=3) as pool:
  jobs=[pool.submit(compile_one,s) for s in sources]
  for n,f in enumerate(as_completed(jobs),1):
   try:objects.append(f.result())
   except Exception as e:errors.append(str(e));print('FAIL',e,flush=True)
   if n%15==0:print('Compiled',n,'/',len(sources),'errors',len(errors),flush=True)
 if errors:(ROOT/'logs/web-build-errors.json').write_text(json.dumps(errors,indent=2)+'\n');raise SystemExit(1)
 objects.sort(key=str)
 link=[str(emcc),'-O1','-g1','-pthread','-mtail-call',*map(str,objects),'--use-port=sdl2','--use-port=sdl2_ttf','-sCASE_INSENSITIVE_FS=1','-sLEGACY_GL_EMULATION=1','-sGL_FFP_ONLY=1','-sGL_ASSERTIONS=1','-sOFFSCREEN_FRAMEBUFFER=1','-sOFFSCREENCANVAS_SUPPORT=1','-sPROXY_TO_PTHREAD=1','-sPTHREAD_POOL_SIZE=6','-sMODULARIZE=1','-sEXPORT_NAME=createCS16Web','-sENVIRONMENT=web,worker','-sINITIAL_MEMORY=67108864','-sALLOW_MEMORY_GROWTH=1','-sMAXIMUM_MEMORY=1073741824','-sSTACK_SIZE=4194304','-sINCOMING_MODULE_JS_API=canvas,noInitialRun,onAbort,print,printErr,locateFile,wasmBinary,wasm,instantiateWasm,noExitRuntime,onExit,onRuntimeInitialized,preRun,postRun,arguments,thisProgram','-sEXPORTED_RUNTIME_METHODS=FS,callMain','-sEXPORTED_FUNCTIONS=_main','-sEXIT_RUNTIME=1','-sASSERTIONS=1','-sERROR_ON_UNDEFINED_SYMBOLS=1','-Wl,--error-limit=0','-o',str(out/'cs16-web.js')]
 p=subprocess.run(link,env=env,capture_output=True,text=True,timeout=600);(ROOT/'logs/web-link.log').write_text(p.stdout+p.stderr)
 if p.returncode:raise SystemExit(p.stderr[-3500:])
 print('Wasm built',round(time.monotonic()-start,1),'seconds; original game/browser execution unverified',flush=True)
if __name__=='__main__':main()
