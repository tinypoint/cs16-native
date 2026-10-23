#!/usr/bin/env python3
"""Inspect an extracted installation, without launching its EXE or installer."""
import argparse
import json
from pathlib import Path

def inspect(root):
    folders = {p.name.casefold(): p for p in root.iterdir() if p.is_dir()}
    result = {'root': str(root), 'directories': {}, 'readyForIntegration': True}
    for name in ('valve', 'cstrike'):
        directory = folders.get(name)
        if directory is None:
            result['directories'][name] = {'present': False}
            result['readyForIntegration'] = False
            continue
        files = [p for p in directory.rglob('*') if p.is_file()]
        suffixes = {}
        for p in files:
            ext = p.suffix.casefold()
            suffixes[ext] = suffixes.get(ext, 0) + 1
        result['directories'][name] = {
            'present': True, 'path': str(directory), 'files': len(files),
            'bytes': sum(p.stat().st_size for p in files),
            'bspMaps': suffixes.get('.bsp', 0), 'models': suffixes.get('.mdl', 0),
            'archives': sum(suffixes.get(e, 0) for e in ('.pak', '.pk3')),
            'caseCollisions': [],
        }
        seen = {}
        for p in files:
            rel = str(p.relative_to(directory)); key = rel.casefold()
            if key in seen:
                result['directories'][name]['caseCollisions'].append([seen[key], rel])
            seen[key] = rel
        if not files:
            result['readyForIntegration'] = False
    result['note'] = 'Directory presence is a prerequisite, not proof of completeness, authenticity or playability. Archive contents are not expanded by this tool.'
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = args.directory.expanduser().resolve()
    if not root.is_dir():
        parser.error('Provide the extracted game directory containing valve/ and cstrike/.')
    report = inspect(root)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    raise SystemExit(0 if report['readyForIntegration'] else 1)
