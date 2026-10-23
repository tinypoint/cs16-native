"""Bounded control-flow analysis of original MSVC catch funclets.

Never scan through a return into an adjacent, unrelated catch function.
"""
from capstone.x86 import X86_OP_IMM,X86_REG_EAX,X86_REG_AX,X86_REG_AL,X86_REG_AH
from capstone import CS_GRP_JUMP
def funclet_returns(md,read,va):
    low=max(0,va-0x100);high=va+0x800
    work=[(va,None)];seen=set();result=set()
    while work:
        pc,constant=work.pop()
        while low<=pc<high and (pc,constant) not in seen:
            if len(seen)>4096:raise ValueError('Catch funclet control-flow limit exceeded')
            seen.add((pc,constant));raw=read(pc,15)
            if not raw:break
            ins=next(iter(md.disasm(raw,pc,count=1)),None)
            if ins is None:break
            name=ins.mnemonic
            if name in ('ret','retn'):
                if constant is not None:result.add(constant)
                break
            if name in ('int3','hlt','ud2'):break
            target=ins.operands[0].imm&0xffffffff if ins.operands and ins.operands[0].type==X86_OP_IMM else None
            if name=='jmp':
                if target is not None:work.append((target,constant))
                break
            if ins.group(CS_GRP_JUMP):
                if target is not None:work.append((target,constant))
            if name=='mov' and len(ins.operands)==2 and ins.op_str.startswith('eax, ') and ins.operands[1].type==X86_OP_IMM:
                constant=ins.operands[1].imm&0xffffffff
            elif name=='call' or any(r in (X86_REG_EAX,X86_REG_AX,X86_REG_AL,X86_REG_AH) for r in ins.regs_access()[1]):
                constant=None
            pc+=ins.size
    return result

def verify_fixtures():
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
    cases=[('b850100000c3b890100000c3',{0x1050}),
           ('85c07406b850100000c3b860100000c3',{0x1050,0x1060}),
           ('b85010000031c0c3',set())]
    for encoded,expected in cases:
        raw=bytes.fromhex(encoded)+b'\xcc'*4096
        def read(va,size):return raw[va-0x1000:va-0x1000+size] if 0x1000<=va<=0x1000+len(raw)-size else None
        actual=funclet_returns(md,read,0x1000)
        assert actual==expected,(encoded,actual,expected)
    return {'passedCases':len(cases),'adjacentReturnsExcluded':True,'branchReturnsIncluded':True,'clobberedReturnValueExcluded':True}
