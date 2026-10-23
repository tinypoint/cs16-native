#!/usr/bin/env python3
"""Compile the original launcher with the initial diagnostic Win32 host."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import subprocess
import time
import re
import argparse
from migration_patches import materialize

ROOT=Path(__file__).resolve().parents[1];GEN=ROOT/'.local/lift/hl';OUT=ROOT/'.local/launcher-native'

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--diagnostic',action='store_true',help='Allow modules with reported semantic gaps; unsupported instructions still abort');ap.add_argument('--profile-entries',action='store_true',help='Record every reached original function using the optional runtime profiler');args=ap.parse_args()
    modules=[GEN,*[ROOT/'.local/lift'/name for name in ('filesystem','vgui','steam','mss32','dbg','hw','cstrike-client','particleman','vgui2','gameui','demoplayer','mp3dec','mssv29','mssv12','voice-miles','core','swda','trackerui','serverbrowser','trackernet','steamclient','vstdlib','tier0')]]
    for module in modules:
        result=json.loads((module/'result.json').read_text())
        if result['generationErrors'] or (not args.diagnostic and any(result[k] for k in ('unimplementedInstructions','uncleanExtents'))):
            raise SystemExit(module.name+' translation has unresolved generation diagnostics')
        if args.diagnostic:print(module.name,'diagnostic-only build, unsupported functions',len(result['unimplementedInstructions']),'unclean extents',len(result['uncleanExtents']),flush=True)
    obj=OUT/'objects';obj.mkdir(parents=True,exist_ok=True)
    entries=[]
    for module in modules:
        entries += [(int(a,16),name) for a,name in re.findall(r'\{0x([0-9A-Fa-f]+),\s*(sub_[0-9A-Fa-f]+)\}',(module/'recomp_dispatch.c').read_text())]
    entries.sort();assert len({a for a,_ in entries})==len(entries),'Overlapping AOT module addresses'
    dispatch=OUT/'combined_dispatch.c'
    dispatch.write_text('#include "recomp_types.h"\n'+''.join('void '+name+'(void);\n' for _,name in entries)+'const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n'+''.join(f'{{0x{a:08X},{name}}},\n' for a,name in entries)+'};\nconst uint32_t recomp_dispatch_count = '+str(len(entries))+';\n')
    lifted=[]
    for module in modules:
        build=OUT/'generated'/module.name;build.mkdir(parents=True,exist_ok=True)
        (build/'recomp_funcs.h').write_bytes((module/'recomp_funcs.h').read_bytes())
        for p in sorted(module.glob('recomp_*.c')):
            if p.name=='recomp_dispatch.c':continue
            source=materialize(module.name,p.name,p.read_text(),json.loads((module/'result.json').read_text())['inputSha256']);needle='#include "recomp_types.h"';assert source.count(needle)==1
            if args.profile_entries:
                source=re.sub(r'(RECOMP_ENTER\((0x[0-9A-Fa-f]+)u\);)',lambda m:m[1]+' cs16_profile_target('+m[2]+');',source)
                source=source.replace('#include "recomp_funcs.h"','#include "recomp_funcs.h"\nvoid cs16_profile_target(uint32_t);')
            materialized=build/p.name
            materialized.write_text(source.replace(needle,needle+'\n#include "cs16_dispatch.h"'))
            lifted.append(materialized)
    sources=sorted((ROOT/'runtime/platform').glob('*.m'))+lifted+sorted((ROOT/'runtime/win32').glob('*.c'))+sorted((ROOT/'runtime/guest').glob('*.c'))+[ROOT/'runtime/launcher_host.c',ROOT/'runtime/original_modules.c',dispatch]
    flags=['/usr/bin/clang','-arch','arm64','-O0','-g','-DRECOMP_LOCAL_REGS','-fno-strict-aliasing','-fwrapv','-ffp-contract=off','-include',str(ROOT/'runtime/guest/guest_cpu.h'),'-I',str(GEN),'-I',str(ROOT/'runtime/win32'),'-I',str(ROOT/'runtime/guest'),'-I',str(ROOT/'vendor/pcrecomp/runtime/recomp32')]
    frameworks=ROOT/'.local/Frameworks'
    if not all((frameworks/(name+'.framework/Headers')).exists() for name in ('SDL2','SDL2_ttf')):
        raise SystemExit('Install native display dependencies with tools/setup_native_display.py first')
    flags+=['-F',str(frameworks),'-I',str(frameworks/'SDL2.framework/Headers')]
    signature=hashlib.sha256((' '.join(flags)).encode()+b''.join(p.read_bytes() for folder in [ROOT/'runtime/win32',ROOT/'runtime/guest',ROOT/'runtime/platform',ROOT/'vendor/pcrecomp/runtime/recomp32'] for p in sorted(folder.glob('*.h')))).digest()
    def build(source):
        name=source.stem+'-'+hashlib.sha256(str(source.relative_to(ROOT)).encode()).hexdigest()[:12]
        headers=b''.join(p.read_bytes() for pattern in ('*.h','*.inc') for p in sorted(source.parent.glob(pattern)))
        target=obj/(name+'.o');stamp=target.with_suffix('.sha');digest=hashlib.sha256(signature+headers+source.read_bytes()).hexdigest()
        if target.exists() and stamp.exists() and stamp.read_text()==digest:return target
        p=subprocess.run([*flags,*(['-fobjc-arc','-fblocks'] if source.suffix=='.m' else []),'-c',str(source),'-o',str(target)],capture_output=True,text=True,timeout=300)
        (OUT/(name+'.log')).write_text(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError(source.name+': '+p.stderr[-1800:])
        stamp.write_text(digest);return target
    start=time.monotonic();objects=[];errors=[]
    with ThreadPoolExecutor(max_workers=3) as pool:
        for f in as_completed([pool.submit(build,s) for s in sources]):
            try:objects.append(f.result())
            except Exception as e:errors.append(str(e));print(str(e),flush=True)
    if errors:raise SystemExit(1)
    p=subprocess.run(['/usr/bin/clang','-arch','arm64',*map(str,objects),'-F',str(frameworks),'-framework','SDL2','-framework','SDL2_ttf','-framework','OpenGL','-framework','Cocoa','-framework','WebKit','-framework','SystemConfiguration','-framework','CoreFoundation','-Wl,-rpath,'+str(frameworks),'-o',str(OUT/'cs16-original-launcher')],capture_output=True,text=True,timeout=60)
    (OUT/'link.log').write_text(p.stdout+p.stderr)
    if p.returncode:raise SystemExit(p.stderr[-3000:])
    print('Original launcher ARM64 host built:',round(time.monotonic()-start,2),'seconds')

if __name__=='__main__':main()
