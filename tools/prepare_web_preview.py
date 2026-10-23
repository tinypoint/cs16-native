#!/usr/bin/env python3
"""Prepare a separate static first-frame experiment, never a playable manifest."""
import argparse,gzip,hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--local-unverified',action='store_true');args=ap.parse_args()
 proof=ROOT/'logs/web-browser-first.json'
 if not args.local_unverified:
  result=json.loads(proof.read_text());assert result.get('frame') and not result.get('error') and any('original pixels presented from this Wasm instance' in line for line in result['logs']),'Original rendered pixels have not been verified'
 out=ROOT/'.local/web-publish';out.mkdir(exist_ok=True)
 (out/'verification.json').write_text(json.dumps({'verified':False})+'\n')
 for name in ['index.html','app.js','style.css','vercel.json']:shutil.copyfile(ROOT/'web'/name,out/name)
 build=ROOT/'.local/web-build';js=(build/'cs16-web.js').read_bytes();wasm=(build/'cs16-web.wasm').read_bytes();binary=gzip.compress(wasm,compresslevel=6,mtime=0);digest=hashlib.sha256(js+binary).hexdigest();base='artifact/'+digest
 folder=out/base;folder.mkdir(parents=True,exist_ok=True);(folder/'cs16-web.js').write_bytes(js);(folder/'cs16-web.wasm.gz').write_bytes(binary)
 info=json.loads((ROOT/'.local/web-startup/manifest.json').read_text());pack=Path(info['path']);destination='resources/'+info['sha256']+'/startup.cs16pack.gz';target=out/destination;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(pack,target)
 if not args.local_unverified:
  expected={'loaderSha256':hashlib.sha256(js).hexdigest(),'runtimeSha256':hashlib.sha256(binary).hexdigest(),'resourceSha256':info['sha256']}
  assert result.get('build')==expected,'Browser evidence does not match these exact program and resource bytes'
 manifest={'schema':1,'version':'0.0.3','ready':False,'runtime':None,'resources':None,'message':'原程序 Wasm 首帧实验，暂不可操作。','preview':{'ready':True,'mode':'original-wasm-first-frame','base':'/'+base+'/','runtime':{'url':'/'+base+'/cs16-web.wasm.gz','bytes':len(binary),'decodedBytes':len(wasm),'sha256':hashlib.sha256(binary).hexdigest()},'resources':{'url':'/'+destination,'bytes':info['bytes'],'decodedBytes':info['decodedBytes'],'sha256':info['sha256'],'scope':'original-startup-subset'}}}
 (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n');(ROOT/'logs/web-preview-package.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
 (out/'verification.json').write_text(json.dumps({'verified':not args.local_unverified,'mode':'first-frame-only','proofSha256':hashlib.sha256(proof.read_bytes()).hexdigest() if proof.exists() else None})+'\n')
 print('First-frame package:',len(binary)//1024,'KiB compressed program;',info['bytes']//1024,'KiB resources; verification',not args.local_unverified)
if __name__=='__main__':main()
