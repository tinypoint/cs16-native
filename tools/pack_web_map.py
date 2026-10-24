#!/usr/bin/env python3
"""Build a separate cached original offline-map resource pack. No addons."""
import argparse,gzip,hashlib,json,re,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--map',default='de_dust2');args=ap.parse_args()
 assert re.fullmatch('[a-zA-Z0-9_]+',args.map)
 source=ROOT/'game/upload-531009895/CounterStrike1.6';files=set()
 def add(p):
  if p.is_file():files.add(p.relative_to(source).as_posix())
 for mod in ['cstrike','valve']:
  for folder in ['models','sound','sprites','events','classes','gfx/vgui','gfx/shell']:
   for p in (source/mod/folder).rglob('*'):add(p)
  for p in (source/mod).iterdir():
   if p.suffix.lower() in ['.cfg','.rc','.gam','.txt','.lst','.scr','.db','.vdf']:add(p)
 for mod in ['cstrike','valve']:
  for p in (source/mod/'gfx/env').glob('des*'):add(p)
 for p in (source/'cstrike/maps').glob(args.map+'.*'):add(p)
 add(source/'cstrike/dlls/mp.dll')
 bsp=(source/'cstrike/maps'/(args.map+'.bsp')).read_bytes();offset,size=struct.unpack_from('<ii',bsp,4)
 entities=bsp[offset:offset+size].decode('latin1');wad=re.search(r'"wad"\s+"([^\"]+)"',entities)
 names={p.lower().split('\\')[-1] for p in wad[1].split(';') if p} if wad else set()
 names.update(['decals.wad','tempdecal.wad','fonts.wad','cached.wad'])
 for mod in ['cstrike','valve']:
  for p in (source/mod).glob('*.wad'):
   if p.name.lower() in names:add(p)
 entries=[];payload=bytearray()
 startup=json.loads((ROOT/'.local/web-startup/manifest.json').read_text());raw=gzip.decompress(Path(startup['path']).read_bytes());n=struct.unpack_from('<I',raw,8)[0];existing={e['path'].lower() for e in json.loads(raw[12:12+n])['files']}
 for name in sorted(files):
  if name.lower() in existing:continue
  data=(source/name).read_bytes();entries.append({'path':name,'offset':len(payload),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()});payload.extend(data)
 out=ROOT/'.local/web-gameplay';out.mkdir(exist_ok=True);parts=[];group=[];size=0
 def flush():
  nonlocal group,size
  if not group:return
  data=bytearray();index=[]
  for entry in group:
   e=dict(entry);e['offset']=len(data);index.append(e);data.extend(payload[entry['offset']:entry['offset']+entry['bytes']])
  table=json.dumps({'schema':1,'scope':'original-offline-map-'+args.map,'files':index},separators=(',',':')).encode()
  raw=b'CS16PK01'+struct.pack('<I',len(table))+table+data;zipped=gzip.compress(raw,compresslevel=6,mtime=0);digest=hashlib.sha256(zipped).hexdigest();target=out/(digest+'.cs16pack.gz');target.write_bytes(zipped)
  parts.append({'path':str(target),'sha256':digest,'bytes':len(zipped),'decodedBytes':len(raw),'files':len(index),'scope':'original-offline-map-'+args.map});group=[];size=0
 for entry in entries:
  if size+entry['bytes']>8*1024*1024:flush()
  group.append(entry);size+=entry['bytes']
 flush();info={'schema':1,'map':args.map,'parts':parts};(out/'manifest.json').write_text(json.dumps(info,indent=2)+'\n');print('Map packs:',len(parts),'compressed bytes',sum(p['bytes'] for p in parts))
if __name__=='__main__':main()
