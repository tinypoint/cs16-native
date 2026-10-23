"""Compare the actual original masking helper to Unicorn x86, not a rewrite."""
import struct
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_FPCW,UC_X86_REG_FPSW,UC_X86_REG_FPTAG,UC_X86_REG_FIP,UC_X86_REG_FDP,UC_X86_REG_FCS,UC_X86_REG_FDS,UC_X86_REG_FOP
from read_goldsrc_blob import decode
ROOT=Path(__file__).resolve().parents[1]
def verify(result):
    image,meta=decode(ROOT/'game/upload-531009895/CounterStrike1.6/hw.dll')
    fields=((0,2),(4,2),(8,2),(12,4),(16,4),(20,4),(24,2))
    for case in result['cases']:
        cpu=Uc(UC_ARCH_X86,UC_MODE_32)
        cpu.mem_map(meta['base'],(len(image)+4095)&~4095);cpu.mem_write(meta['base'],bytes(image))
        cpu.mem_map(0x70000000,0x2000);cpu.reg_write(UC_X86_REG_ESP,0x70001000)
        cpu.mem_write(0x70001000,struct.pack('<I',0x70000000))
        for reg,value in ((UC_X86_REG_FPCW,case['inputControl']),(UC_X86_REG_FPSW,0),(UC_X86_REG_FPTAG,0xffff),(UC_X86_REG_FIP,0),(UC_X86_REG_FDP,0),(UC_X86_REG_FCS,0),(UC_X86_REG_FDS,0),(UC_X86_REG_FOP,0)):
            cpu.reg_write(reg,value)
        cpu.emu_start(0x1dd0824,0x70000000,count=20)
        expected=bytes(cpu.mem_read(0x1ee0004,28));actual=bytes.fromhex(case['environmentHex'])
        for at,n in fields:
            assert actual[at:at+n]==expected[at:at+n],(case['inputControl'],at,actual.hex(),expected.hex())
        assert case['controlAfter']==cpu.reg_read(UC_X86_REG_FPCW)
    return {'passedCases':len(result['cases']),'environmentDefinedFieldsMatch':True,'controlWordMatches':True,'scope':'Original empty-stack control masking helper; not complete x87 arithmetic or exception conformance'}
