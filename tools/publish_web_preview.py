#!/usr/bin/env python3
"""Publish only an asset-bound verified original-Wasm experiment to CS Vercel."""
import base64,hashlib,json,os,re,subprocess,tempfile,urllib.parse
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from deploy_web import api,VercelError
ROOT=Path(__file__).resolve().parents[1]
def upload(path,sha):
 auth=json.loads((ROOT/'.local/web-access/vercel-auth.json').read_text());project=json.loads((ROOT/'.local/web-access/vercel-project.json').read_text())['project']
 query='?'+urllib.parse.urlencode({'teamId':project['orgId']})
 config='header = '+json.dumps('Authorization: Bearer '+auth['access_token'])+'\nheader = '+json.dumps('x-vercel-digest: '+sha)+'\nheader = "Content-Type: application/octet-stream"\n'
 p=subprocess.run(['curl','--config','-','-sS','--connect-timeout','10','--max-time','600','--retry','2','--retry-delay','1','--data-binary','@'+str(path),'-w','\n%{http_code}','https://api.vercel.com/v2/files'+query],input=config,capture_output=True,text=True,check=True)
 body,status=p.stdout.rsplit('\n',1)
 if int(status) not in (200,201):
  try:message=json.loads(body).get('error',{}).get('message','Upload failed')
  except ValueError:message='Upload failed'
  raise RuntimeError('Vercel upload '+status+': '+message)
 return path.name,path.stat().st_size

def main():
 site=ROOT/'.local/web-publish';verification=json.loads((site/'verification.json').read_text());assert verification['verified'] and verification['mode'] in ('first-frame-only','offline-map-interaction')
 proof=ROOT/('logs/web-browser-gameplay.json' if verification['mode']=='offline-map-interaction' else 'logs/web-browser-first.json');assert hashlib.sha256(proof.read_bytes()).hexdigest()==verification['proofSha256'],'Proof changed after packaging'
 project=json.loads((ROOT/'.local/web-access/vercel-project.json').read_text())['project'];assert project['projectName']=='cs16-web'
 manifest=json.loads((site/'manifest.json').read_text());preview=manifest['preview']
 allowed=['index.html','app.js','style.css','manifest.json','vercel.json',preview['base'].lstrip('/')+'cs16-web.js',preview['runtime']['url'].lstrip('/'),preview['resources']['url'].lstrip('/')]
 allowed += [part['url'].lstrip('/') for part in preview.get('gameplay',[])]
 files=[];large=[]
 for relative_name in sorted(allowed):
  path=site/relative_name
  assert path.resolve().is_relative_to(site.resolve()),'Asset outside staging directory'
  relative=path.relative_to(site)
  if not path.is_file() or relative.name=='verification.json' or any(part.startswith('.') for part in relative.parts):continue
  raw=path.read_bytes()
  if len(raw)<1024*1024:files.append({'file':'web/'+relative.as_posix(),'data':base64.b64encode(raw).decode(),'encoding':'base64'})
  else:
   sha=hashlib.sha1(raw).hexdigest();files.append({'file':'web/'+relative.as_posix(),'sha':sha,'size':len(raw)});large.append((path,sha))
 config=json.loads((site/'vercel.json').read_text());body={'name':project['projectName'],'project':project['projectId'],'target':'production','files':files,'projectSettings':{key:config[key] for key in ('framework','buildCommand','installCommand','outputDirectory')}}
 body['projectSettings']['rootDirectory']='web';body.update({key:config[key] for key in ('headers','rewrites','redirects') if key in config})
 try:
  result=api('POST','/v13/deployments',body)
 except VercelError as error:
  details=error.payload.get('error',{});code=details.get('code','')
  if code!='missing_files':raise
  hashes=set(re.findall(r'\b[0-9a-f]{40}\b',json.dumps(details)))
  needed=[item for item in large if not hashes or item[1] in hashes]
  print('Missing content-addressed uploads:',len(needed),flush=True)
  with ThreadPoolExecutor(max_workers=4) as pool:
   for name,size in pool.map(lambda item:upload(*item),needed):print('Uploaded',name,size,'bytes',flush=True)
  result=api('POST','/v13/deployments',body)
 path=ROOT/'.local/web-access/vercel-deployment.json';path.write_text(json.dumps(result,indent=2)+'\n');path.chmod(0o600)
 print(json.dumps({key:result.get(key) for key in ('id','url','readyState')},ensure_ascii=False))
if __name__=='__main__':main()
