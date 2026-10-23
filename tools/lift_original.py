#!/usr/bin/env python3
"""Generate local C from an uploaded PE/BLOB module, retaining guest addresses.

Discovery is heuristic; counts do not establish complete callback coverage.
Missing instructions and failed extents are reported, never silently replaced
by successful empty functions. Generated code remains in ignored .local/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from read_goldsrc_blob import decode

ROOT=Path(__file__).resolve().parents[1];KIT=ROOT/'vendor/pcrecomp'
for sub in ('pe','disasm','lift'):
    sys.path.insert(0,str(KIT/'tools'/sub))
from disasm32 import Disassembler
from pe_analyze import Section, analyze_pe, build_iat_map
from generate import true_extent, linear_disassemble_function, lift_function_linear, write_chunk, EXTENT_REACH
from lift32 import Lifter
from eh32 import resume_points,funcinfo,catch_funclets
import eh32 as _eh_metadata
from exception_metadata import funclet_returns
_eh_metadata.funclet_returns=funclet_returns
from capstone.x86 import X86_OP_MEM

# Extend the pinned converter locally; upstream sources remain unchanged.
_upstream_instruction=Lifter.lift_instruction
def lift_with_x87_environment(self,insn):
    m=insn.mnemonic
    prefixes=[]
    if m in ('fnstenv','fstenv','fldenv'):
        for byte in insn.bytes:
            if byte not in (0x26,0x2e,0x36,0x3e,0x64,0x65,0x66,0x67,0xf0,0xf2,0xf3):break
            prefixes.append(byte)
    if m in ('finit','fninit'):return ['cs16_x87_init(); _fpu_cmp=1;']
    if m in ('fclex','fnclex'):return ['cs16_x87_clear();']
    if m in ('fnstenv','fstenv','fldenv') and 0x66 not in prefixes and len(insn.operands)==1 and insn.operands[0].type==X86_OP_MEM:
        address=self._fmt_mem_addr(insn.operands[0].mem)
        operation=f'cs16_x87_store_env({address});' if m!='fldenv' else f'_fpu_cmp=cs16_x87_load_env({address});'
        return [operation+f' /* 0x{insn.address:08X}: {m} {insn.op_str} */']
    lines=_upstream_instruction(self,insn)
    controls=('fnstcw','fstcw','fldcw','fnstsw','fstsw','fnclex','fclex','fninit','finit','fwait','wait')
    if m.startswith('f') and m not in controls and not any('UNIMPLEMENTED' in s for s in lines):
        memory=next((o for o in insn.operands if o.type==X86_OP_MEM),None)
        address=self._fmt_mem_addr(memory.mem) if memory else '0'
        raw=bytes(insn.bytes);op=((raw[0]&7)<<8)|raw[1] if len(raw)>1 else 0
        compared=int((m.startswith(('fcom','fucom')) and 'i' not in m) or m in ('ftst','fxam','fprem','fprem1','fsin','fcos','fsincos','fptan'))
        lines.append(f'cs16_x87_note(0x{insn.address:08X}u,0x{op:X}u,{address},{int(memory is not None)},_fpu_cmp,{compared});')
    return lines
Lifter.lift_instruction=lift_with_x87_environment

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--entry',action='append',type=lambda v:int(v,0),default=[],help='Additional original executable entry observed at runtime; input hash remains checked')
    ap.add_argument('module',type=Path);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--image-base',type=lambda value:int(value,0),help='Apply PE HIGHLOW relocations before lifting; original file is preserved')
    args=ap.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'.local'):
        ap.error('Generated code must remain in this project .local directory.')
    out.mkdir(parents=True,exist_ok=True);raw=args.module.read_bytes();digest=hashlib.sha256(raw).hexdigest()
    started=time.monotonic()
    analysis_path=args.module
    if raw[:2]==b'MZ':
        if args.image_base is not None:
            relocated=pefile.PE(data=raw)
            if not getattr(relocated,'DIRECTORY_ENTRY_BASERELOC',[]):
                raise ValueError('Cannot relocate a module without relocation records')
            relocated.relocate_image(args.image_base)
            analysis_path=out/'relocated-input.dll'
            analysis_path.write_bytes(relocated.write());relocated.close()
        info=analyze_pe(str(analysis_path));pe=pefile.PE(str(analysis_path));image=bytes(pe.get_memory_mapped_image());pe.close()
        base=info.image_base;code_start=info.code_start;code_end=info.code_end
        sections=[Section(s.name,s.virtual_address,s.virtual_size,s.virtual_address,min(max(s.virtual_size,s.raw_size),len(image)-s.virtual_address),s.characteristics) for s in info.sections]
        iat=build_iat_map(info);seeds={base+info.entry_point_rva}|{base+e.rva for e in info.exports if e.rva and code_start<=base+e.rva<code_end}
        kind='PE'
    else:
        image,meta=decode(args.module);image=bytes(image);base=meta['base'];kind=meta['format']
        # BLOB has no section flags. Validate the known code-bearing section by
        # requiring both loader-supplied code roots inside its file-backed data.
        first=meta['sections'][0];code_start=first['address'];code_end=code_start+first['fileBytes']
        seeds={meta['entry'],meta['interfaceAddress']}
        assert all(code_start<=a<code_end for a in seeds),'Unrecognized BLOB code layout'
        sections=[Section('blob'+str(i),s['address']-base,s['memoryBytes'],s['address']-base,s['fileBytes'],0x60000020 if i==0 else 0x40000040) for i,s in enumerate(meta['sections'])]
        iat={i['iatAddress']:(i['dll'],i['name']) for i in meta['imports']}
    if any(not code_start<=entry<code_end for entry in args.entry):raise ValueError('Additional entry outside original code section')
    seeds.update(args.entry)
    # MSVC takes addresses of import jump thunks as callbacks/vtable slots.
    # Validate the complete FF 25 absolute operand against the real IAT;
    # ordinary prologue discovery can miss these six-byte function bodies.
    import_thunks=set();position=code_start-base
    while True:
        position=image.find(b'\xff\x25',position,code_end-base)
        if position<0:break
        if position+6<=len(image) and int.from_bytes(image[position+2:position+6],'little') in iat:
            import_thunks.add(base+position)
        position+=1
    seeds|=import_thunks
    cache=out/'catalog.json';meta_path=out/'catalog-source.json'
    cache_key={'sha256':digest,'imageBase':base}
    if cache.exists() and meta_path.exists() and all(json.loads(meta_path.read_text()).get(k)==v for k,v in cache_key.items()):
        starts={a for a in json.loads(cache.read_text()) if code_start<=a<code_end}
        starts|=seeds
    else:
        funcs=Disassembler(image,base,sections).find_functions(code_start,code_end,iat,seeds=seeds,release_operands=True)
        starts={a for a in set(funcs)|seeds if code_start<=a<code_end}
        cache.write_text(json.dumps(sorted(starts)));meta_path.write_text(json.dumps({**cache_key,'kind':kind,'discoveryIsHeuristic':True}))
        del funcs
    md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
    def read(va,size):
        offset=va-base
        return bytes(image[offset:offset+size]) if 0<=offset<=len(image) and 0<=size<=len(image)-offset else None
    lifter=Lifter(iat_map=iat,lifted=starts)
    todo=sorted(starts);done=set();chunk=[];chunk_index=0;errors=[];dirty=[];unsupported=[];exception_owners=[]
    while todo:
        outside=set()
        for addr in todo:
            reached=set();behind=set();called=set();name=f'sub_{addr:08X}'
            try:
                end,clean=true_extent(md,image,base,addr,min(addr+EXTENT_REACH,code_end),starts,reached=reached,behind=behind,called=called)
                if not clean:dirty.append(hex(addr))
                outside|={t for t in behind if t not in reached}|called
                if not reached:raise ValueError('No reachable instructions')
                ins,leaders=linear_disassemble_function(md,image,base,min(reached),end,reached=reached);leaders.add(addr)
                if not ins:raise ValueError('No decoded instructions')
                eh=resume_points(md,read,ins)
                if eh:
                    for resume in eh:
                        if not code_start<=resume<code_end:raise ValueError('EH continuation outside code')
                        if resume not in reached:
                            more,ok=true_extent(md,image,base,resume,min(resume+EXTENT_REACH,code_end),starts,reached=reached,behind=behind,called=called)
                            end=max(end,more)
                    ins,leaders=linear_disassemble_function(md,image,base,min(reached),end,reached=reached);leaders.add(addr)
                    exception_owners.append({'function':hex(addr),'resume':list(map(hex,sorted(eh)))})
                # Include the original catch / cleanup funclets, even when
                # their addresses occur only in exception metadata.
                for instruction in ins[:12]:
                    if instruction.mnemonic=='push' and instruction.op_str.startswith('0x'):
                        fi=funcinfo(read,int(instruction.op_str,16))
                        if fi:
                            outside.update(catch_funclets(read,fi))
                            maximum=int.from_bytes(read(fi+4,4),'little');unwind_map=int.from_bytes(read(fi+8,4),'little')
                            if maximum>4096:raise ValueError('EH unwind map too large')
                            for state in range(maximum):
                                action=read(unwind_map+state*8+4,4)
                                if not action:raise ValueError('EH unwind map unreadable')
                                target=int.from_bytes(action,'little')
                                if target:outside.add(target)
                outside|={t for t in behind if t not in reached}|called
                body=lift_function_linear(lifter,name,ins,leaders,addr,eh_resume=eh)
                if eh:body=body.replace('RECOMP_ITAIL(','RECOMP_EH_ITAIL(')
                missing=[line.strip() for line in body.splitlines() if 'UNIMPLEMENTED:' in line]
                if missing:
                    unsupported.append({'function':hex(addr),'instructions':missing})
                    # Never execute through a skipped unsupported instruction.
                    body=body.replace('; /* UNIMPLEMENTED:', 'abort(); /* UNIMPLEMENTED:')
                chunk.append((body,addr,name));done.add(addr)
                if len(chunk)>=200:
                    write_chunk(str(out),chunk_index,chunk);chunk=[];chunk_index+=1
                    print('Generated',len(done),'functions',flush=True)
            except Exception as e:
                errors.append({'function':hex(addr),'error':str(e)})
        todo=sorted(t for t in outside if code_start<=t<code_end and t not in starts)
        starts.update(todo)
    if chunk:write_chunk(str(out),chunk_index,chunk);chunk_index+=1
    header=['#pragma once','#include "recomp_types.h"']+[f'void sub_{a:08X}(void);' for a in sorted(done)]
    (out/'recomp_funcs.h').write_text('\n'.join(header)+'\n')
    table='#include "recomp_funcs.h"\nconst recomp_dispatch_entry_t recomp_dispatch_table[] = {\n'+''.join(f'{{0x{a:08X},sub_{a:08X}}},\n' for a in sorted(done))+'};\nconst uint32_t recomp_dispatch_count = '+str(len(done))+';\n'
    (out/'recomp_dispatch.c').write_text(table)
    report={'inputSha256':digest,'format':kind,'imageBase':base,'codeStart':code_start,'codeEnd':code_end,'requestedEntries':list(map(hex,args.entry)),'verifiedImportThunkSeeds':len(import_thunks),'generatedFunctions':len(done),'files':chunk_index,
            'generationErrors':errors,'unimplementedInstructions':unsupported,'uncleanExtents':dirty,'exceptionLandingPads':exception_owners,
            'elapsedSeconds':round(time.monotonic()-started,2),'compiled':False,'gameLaunched':False,
            'scope':'Static module translation experiment; discovery is heuristic and callbacks, imports, relocations, execution and behavior remain unverified.'}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('generatedFunctions','files','elapsedSeconds')}),'errors',len(errors),'unsupported functions',len(unsupported),'unclean extents',len(dirty),flush=True)
    return int(bool(errors or unsupported or dirty))

if __name__=='__main__':sys.exit(main())
