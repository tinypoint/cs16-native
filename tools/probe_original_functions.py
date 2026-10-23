#!/usr/bin/env python3
"""Compare actual uploaded CS server functions on ARM64/Wasm against x86.

Controlled states only: not a game startup or complete engine compatibility test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, x86_const as X

ROOT=Path(__file__).resolve().parents[1]
KIT=ROOT/'vendor/pcrecomp';OUT=ROOT/'.local/original-probe'
sys.path.insert(0,str(KIT/'tools/lift'))
from generate import linear_disassemble_function, lift_function_linear
from lift32 import Lifter

EXPECTED_MODULE_SHA='0e7f1764f7ff9705153247b5a282acd16b57f488323f85c38457c3b73f2fa677'
BASE=0x10000000;SCRATCH=0x11000000;OBJECT=SCRATCH+0x1000
OUTPUT=SCRATCH+0x3000;VERSION=SCRATCH+0x5000;STACK=SCRATCH+0xe000;STOP=SCRATCH+0xff00
REGS=('eax','ecx','edx','ebx','esp','ebp','esi','edi')
REG_IDS=[getattr(X,'UC_X86_REG_'+r.upper()) for r in REGS]
FUNCTIONS={
 'GetNewDLLFunctions':(0x10059b20,0x10059b5a),
 '?SparkStop@CEnvSpark@@QAEXPAVCBaseEntity@@0W4USE_TYPE@@M@Z':(0x10058c50,0x10058c61),
 '?DetonateUse@CGrenade@@QAEXPAVCBaseEntity@@0W4USE_TYPE@@M@Z':(0x10077820,0x1007783b),
}

def command(args,log,env=None):
    p=subprocess.run(list(map(str,args)),cwd=ROOT,env=env,capture_output=True,text=True,timeout=180)
    (ROOT/'logs'/log).write_text(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(log+': '+p.stderr[-1200:])

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--module',type=Path,required=True)
    ap.add_argument('--emsdk',type=Path)
    args=ap.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    assert hashlib.sha256(args.module.read_bytes()).hexdigest()==EXPECTED_MODULE_SHA,'Audit input before changing function addresses'
    pe=pefile.PE(str(args.module));assert pe.OPTIONAL_HEADER.ImageBase==BASE
    image=bytearray(pe.get_memory_mapped_image());md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
    # A controlled pointer used by DetonateUse; not a change to the input DLL.
    struct.pack_into('<I',image,0x1016a71c-BASE,VERSION)
    exports={s.name.decode():BASE+s.address for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
    bodies=[];selected=[]
    for name,(start,end) in FUNCTIONS.items():
        assert exports[name]==start
        # Validate bounded instruction extent against the uploaded module.
        ins=list(md.disasm(image[start-BASE:end-BASE],start))
        assert ins and ins[-1].mnemonic=='ret' and ins[-1].address+ins[-1].size==end,(name,[(i.mnemonic,i.op_str) for i in ins])
        decoded,leaders=linear_disassemble_function(md,image,BASE,start,end)
        body=lift_function_linear(Lifter(iat_map={},lifted={x[0] for x in FUNCTIONS.values()}),f'sub_{start:08X}',decoded,leaders,start)
        assert not any(marker in body for marker in ('RECOMP_TODO','UNIMPLEMENTED'))
        bodies.append(body);selected.append({'name':name,'start':hex(start),'end':hex(end),'codeSha256':hashlib.sha256(image[start-BASE:end-BASE]).hexdigest()})
    header=['#pragma once','#include "recomp_types.h"']+[f'void sub_{a:08X}(void);' for a,_ in FUNCTIONS.values()]
    header+=['static const recomp_dispatch_entry_t game_functions[] = {']+[f'{{0x{a:08X},sub_{a:08X}}},' for a,_ in FUNCTIONS.values()]+['};','#define GAME_FUNCTION_COUNT (sizeof(game_functions)/sizeof(game_functions[0]))']
    (OUT/'game_functions.h').write_text('\n'.join(header)+'\n')
    (OUT/'functions.c').write_text('#define RECOMP_GENERATED_CODE\n#include "game_functions.h"\n'+'\n'.join(bodies))
    (OUT/'image.bin').write_bytes(image)
    common=['-O2','-fno-strict-aliasing','-fwrapv','-ffp-contract=off','-DRECOMP_LOCAL_REGS','-I',KIT/'runtime/recomp32','-I',OUT,OUT/'functions.c',ROOT/'tools/original_probe_host.c']
    command(['/usr/bin/clang','-arch','arm64',*common,'-o',OUT/'probe-arm64'],'probe-arm64-compile.log')
    targets=[('arm64',[OUT/'probe-arm64'])]
    if args.emsdk:
        sdk=args.emsdk.resolve();node=next((sdk/'node').glob('*/bin/node'));env={**os.environ,'EMSDK_PYTHON':sys.executable,'PATH':str(node.parent)+':'+os.environ['PATH']}
        command([sdk/'upstream/emscripten/emcc',*common,'-sENVIRONMENT=node','-sNODERAWFS=1','-sINITIAL_MEMORY=67108864','-sALLOW_MEMORY_GROWTH=1','-sMAXIMUM_MEMORY=134217728','-o',OUT/'probe-wasm.js'],'probe-wasm-compile.log',env)
        targets.append(('wasm',[node,OUT/'probe-wasm.js']))
    cases=[]
    for name,(entry,_) in FUNCTIONS.items():
        for value in (0,1,2,3,255,0x7fffffff,0x80000000,0xffffffff):
            for null_output in ((False,True) if name=='GetNewDLLFunctions' else (False,)):
                memory=bytearray([0xa5])*0x10000
                def put(va,v):struct.pack_into('<I',memory,va-SCRATCH,v)
                put(OBJECT+4,OUTPUT);put(VERSION,value);put(STACK,STOP)
                put(STACK+4,0 if null_output else OUTPUT);put(STACK+8,VERSION)
                regs=[value,OBJECT,~value&0xffffffff,0x10203040,STACK,0x60708090,0x13579bdf,0x2468ace0]
                uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(BASE,(len(image)+4095)&~4095);uc.mem_write(BASE,bytes(image));uc.mem_map(SCRATCH,0x10000);uc.mem_write(SCRATCH,bytes(memory))
                for reg,val in zip(REG_IDS,regs):uc.reg_write(reg,val)
                uc.reg_write(X.UC_X86_REG_EFLAGS,0x202);uc.emu_start(entry,STOP,count=1000)
                assert uc.reg_read(X.UC_X86_REG_EIP)==STOP
                expected_regs=[uc.reg_read(reg)&0xffffffff for reg in REG_IDS];expected_memory=bytes(uc.mem_read(SCRATCH,0x10000));expected_flags=uc.reg_read(X.UC_X86_REG_EFLAGS)
                unchanged=bytes(uc.mem_read(BASE,len(image)))==bytes(image);assert unchanged
                (OUT/'input.bin').write_bytes(struct.pack('<8I',*regs)+memory)
                for target,launcher in targets:
                    command([*launcher,OUT/'image.bin',OUT/'input.bin',hex(entry),OUT/'result.bin'],'probe-'+target+'-last-run.log')
                    result=(OUT/'result.bin').read_bytes();assert len(result)==40+0x10000
                    fields=struct.unpack_from('<10I',result);mask=0x8c5 # CF, PF, ZF, SF, OF; AF undefined after test/xor
                    passed=list(fields[:8])==expected_regs and (fields[8]^expected_flags)&mask==0 and fields[9]==1 and result[40:]==expected_memory
                    cases.append({'name':name,'value':hex(value),'nullOutput':null_output,'target':target,'passed':passed})
    report={'moduleSha256':hashlib.sha256(args.module.read_bytes()).hexdigest(),'scope':'Isolated original DLL functions with controlled memory; game not launched. Unicorn is an x86 reference, not a hardware validation.','controlledImageSetup':{'0x1016a71c':hex(VERSION)},'functions':selected,'total':len(cases),'passed':sum(c['passed'] for c in cases),'cases':cases}
    (ROOT/'logs/original-function-results.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Original functions:',len(selected),'cases:',report['passed'],'/',report['total'])
    return int(report['passed']!=report['total'])

if __name__=='__main__':sys.exit(main())
