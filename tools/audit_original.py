#!/usr/bin/env python3
"""Read-only inventory of the uploaded Windows program and game modules."""
import argparse
import hashlib
import json
from pathlib import Path
import pefile

def text(value):
    return value.decode('utf-8', 'replace') if isinstance(value, bytes) else str(value)

def describe(path, root):
    with path.open('rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    record = {'path': str(path.relative_to(root)), 'bytes': path.stat().st_size, 'sha256': digest}
    try:
        pe = pefile.PE(str(path))
        record.update(machine=hex(pe.FILE_HEADER.Machine),
                      preferredBase=hex(pe.OPTIONAL_HEADER.ImageBase),
                      entry=hex(pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint),
                      imageBytes=pe.OPTIONAL_HEADER.SizeOfImage,
                      relocationsPresent=bool(getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', [])),
                      sections=[{'name': text(s.Name.rstrip(b'\0')), 'rva': hex(s.VirtualAddress),
                                 'bytes': s.Misc_VirtualSize, 'executable': bool(s.Characteristics & 0x20000000)} for s in pe.sections])
        for key, attribute in [('imports', 'DIRECTORY_ENTRY_IMPORT'), ('delayImports', 'DIRECTORY_ENTRY_DELAY_IMPORT')]:
            record[key] = {text(d.dll): [text(i.name) if i.name else '#' + str(i.ordinal) for i in d.imports]
                           for d in getattr(pe, attribute, [])}
        record['exports'] = [{'name': text(s.name) if s.name else None, 'ordinal': s.ordinal,
                              'address': hex(pe.OPTIONAL_HEADER.ImageBase + s.address),
                              'forwarder': text(s.forwarder) if s.forwarder else None}
                             for s in getattr(getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None), 'symbols', [])]
        record['versionStrings'] = {}
        for group in getattr(pe, 'FileInfo', []):
            for info in group:
                for table in getattr(info, 'StringTable', []):
                    record['versionStrings'].update({text(k): text(v) for k, v in table.entries.items()})
        pe.close()
    except pefile.PEFormatError as e:
        record['parseError'] = str(e)
    return record

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    ap.add_argument('--output', type=Path, default=Path('logs/original-program-audit.json'))
    args = ap.parse_args(); root = args.directory.resolve()
    if not root.is_dir():
        ap.error('An extracted game directory is required.')
    paths = sorted(p for p in root.rglob('*') if p.is_file() and p.suffix.casefold() in ('.exe', '.dll'))
    report = {'root': str(root), 'programsExecuted': False,
              'authenticityVerified': False, 'modules': [describe(p, root) for p in paths]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    for module in report['modules']:
        print(module['path'], module.get('machine', 'not PE'), module['bytes'],
              'imports=' + str(sum(len(v) for v in module.get('imports', {}).values())),
              'exports=' + str(len(module.get('exports', []))))

if __name__ == '__main__':
    main()
