#!/usr/bin/env python3
"""Pack byte-identical files actually observed during native original startup.

A starter resource set only; this does not claim all maps/gameplay assets exist.
The original binaries and pack remain outside Git.
"""
import argparse,gzip,hashlib,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--file-list',type=Path,help='Reproducible original file inventory instead of the current native trace');args=ap.parse_args()
 source=ROOT/'game/upload-531009895/CounterStrike1.6';stage=(ROOT/'.local/run/game').resolve();files=set()
 for line in ([] if args.file_list else (ROOT/'logs/launcher-run.log').read_text(errors='replace').splitlines()):
  m=re.search(r'^\[file\] CreateFileA .* -> (.*) ok$',line)
  if m:
   p=Path(m[1]);
   if p.is_file() and p.is_relative_to(stage):files.add(p.relative_to(stage).as_posix())
 files.update(json.loads((ROOT/'docs/evidence/first-frame.json').read_text())['startup']['inputModulesVerified'])
 if args.file_list:
  inventory=json.loads(args.file_list.read_text());files.update(e['path'] for e in inventory['files'] if not e['path'].startswith('fonts/'))
  for entry in inventory['files']:
   assert entry['path'] and not entry['path'].startswith('/') and '\\' not in entry['path'] and '..' not in Path(entry['path']).parts,'Invalid original input path'
   if not entry['path'].startswith('fonts/'):
    assert hashlib.sha256((source/entry['path']).read_bytes()).hexdigest()==entry['sha256'],'Original input changed: '+entry['path']
 # Modal layouts and normal menu assets may be opened after the first native
 # buffer swap. Include their original files, not just the loading backgrounds.
 for folder in ['cstrike/Resource','valve/resource','platform/Resource']:
  files.update(p.relative_to(source).as_posix() for p in (source/folder).rglob('*') if p.is_file())
 entries=[];payload=bytearray()
 inputs=[(name,source/name) for name in sorted(files)]
 inputs += [('fonts/LiberationSans-Regular.ttf',ROOT/'.local/fonts/LiberationSans-Regular.ttf'),('fonts/LICENSE',ROOT/'.local/fonts/LICENSE')]
 for name,path in inputs:
  assert not name.startswith('/') and '..' not in Path(name).parts
  raw=path.read_bytes()
  entries.append({'path':name,'offset':len(payload),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()});payload.extend(raw)
 table=json.dumps({'schema':1,'scope':'Original startup subset; not full game resources','files':entries},separators=(',',':')).encode()
 raw=b'CS16PK01'+struct.pack('<I',len(table))+table+payload;compressed=gzip.compress(raw,compresslevel=6,mtime=0);digest=hashlib.sha256(compressed).hexdigest()
 out=ROOT/'.local/web-startup';out.mkdir(exist_ok=True);target=out/(digest+'.cs16pack.gz');target.write_bytes(compressed)
 info={'schema':1,'path':str(target),'sha256':digest,'bytes':len(compressed),'decodedBytes':len(raw),'files':len(entries),'scope':'original-startup-subset','fontSource':'https://github.com/liberationfonts/liberation-fonts/releases/tag/2.1.5'}
 (out/'manifest.json').write_text(json.dumps(info,indent=2)+'\n');print(json.dumps(info))
if __name__=='__main__':main()
