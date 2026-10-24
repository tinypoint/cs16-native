"""Direct original CALL/JMP closure for a newly observed missing Wasm body."""
import hashlib,json,re
from pathlib import Path
def load(root):
 paths=[p for folder in sorted((root/'.local/lift').iterdir()) for p in sorted(folder.glob('recomp_*.c')) if p.name!='recomp_dispatch.c'];key=hashlib.sha256(b'closure-v2-static-tails'+json.dumps([(str(p),p.stat().st_size,p.stat().st_mtime_ns) for p in paths]).encode()).hexdigest();cache=root/'.local/web-direct-calls.json'
 if cache.exists() and (saved:=json.loads(cache.read_text())).get('key')==key:return {int(a):set(b) for a,b in saved['edges'].items()}
 known={int(a,16) for folder in (root/'.local/lift').iterdir() if (folder/'recomp_dispatch.c').exists() for a in re.findall(r'\{0x([0-9A-Fa-f]+),',(folder/'recomp_dispatch.c').read_text())};graph={}
 for path in paths:
  s=path.read_text();methods=list(re.finditer(r'^void sub_([0-9A-Fa-f]+)\(void\)',s,re.M))
  for i,m in enumerate(methods):
   body=s[m.end():methods[i+1].start() if i+1<len(methods) else len(s)];refs={int(a,16) for a in re.findall(r'RECOMP_CALL\(sub_([0-9A-Fa-f]+)\)',body)}
   refs|={int(a,16) for a in re.findall(r'_itail_tgt\s*=\s*(?:\(uint32_t\))?\(?\s*0x([0-9A-Fa-f]+)',body)};refs|={int(a,16) for a in re.findall(r'RECOMP_(?:EH_)?ITAIL\(0x([0-9A-Fa-f]+)',body)};graph[int(m[1],16)]=refs&known
 cache.write_text(json.dumps({'key':key,'edges':{str(a):sorted(b) for a,b in graph.items()}},separators=(',',':')));return graph
def descendants(graph,address):
 seen={address};todo=[address]
 while todo:
  for child in graph.get(todo.pop(),()):
   if child not in seen:seen.add(child);todo.append(child)
 return seen
