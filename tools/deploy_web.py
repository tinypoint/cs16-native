#!/usr/bin/env python3
"""Deploy only the web/ directory to the independent CS16 Vercel project.

Credentials stay outside Git. Pass CS16_VERCEL_AUTH pointing to an existing
Vercel auth JSON, or use .local/web-access/vercel-auth.json. No game files or
local binary-translation outputs are uploaded by this tool.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.parse

ROOT=Path(__file__).resolve().parents[1];ACCESS=ROOT/'.local/web-access'

class VercelError(RuntimeError):
    def __init__(self,status,payload):
        self.status=int(status);self.payload=payload
        super().__init__('Vercel '+str(status)+': '+str(payload.get('error',{}).get('message','API error')))

def api(method,path,data=None):
    auth=Path(os.environ.get('CS16_VERCEL_AUTH',str(ACCESS/'vercel-auth.json')))
    token=json.loads(auth.read_text())['access_token']
    project=json.loads((ACCESS/'vercel-project.json').read_text())['project']
    query='?'+urllib.parse.urlencode({'teamId':project['orgId']})
    cfg='header = '+json.dumps('Authorization: Bearer '+token)+'\nheader = "Content-Type: application/json"\n'
    cmd=['curl','--config','-','-sS','--connect-timeout','10','--max-time','90','-X',method,'-w','\n%{http_code}','https://api.vercel.com'+path+query]
    with tempfile.NamedTemporaryFile() as f:
        if data is not None:
            f.write(json.dumps(data).encode());f.flush();cmd+=['--data-binary','@'+f.name]
        response=subprocess.run(cmd,input=cfg,text=True,capture_output=True,check=True)
    body,status=response.stdout.rsplit('\n',1)
    value=json.loads(body) if body else {}
    if not 200<=int(status)<300:
        raise VercelError(status,value)
    return value

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--status',action='store_true');args=ap.parse_args()
    state=ACCESS/'vercel-deployment.json'
    if args.status:
        previous=json.loads(state.read_text());result=api('GET','/v13/deployments/'+previous['id'])
        print(json.dumps({key:result.get(key) for key in ('id','readyState','url','alias')},ensure_ascii=False));return
    project=json.loads((ACCESS/'vercel-project.json').read_text())['project']
    site=ROOT/'web'
    if json.loads((site/'manifest.json').read_text()).get('preview',{}).get('ready'):raise RuntimeError('Use prepare_web_preview.py and publish_web_preview.py to include verified program/assets')
    config=json.loads((site/'vercel.json').read_text());files=[]
    for source in sorted(site.rglob('*')):
        relative=source.relative_to(site)
        if not source.is_file() or any(part.startswith('.') for part in relative.parts):continue
        if source.suffix.lower() in ('.exe','.dll','.zip','.mix','.bsp','.mdl'):
            raise RuntimeError('Original resource in web deployment directory')
        raw=source.read_bytes()
        if len(raw)>1024*1024:raise RuntimeError('Large runtime/assets require the separate versioned delivery path')
        files.append({'file':'web/'+relative.as_posix(),'data':base64.b64encode(raw).decode(),'encoding':'base64'})
    if project['projectName']!='cs16-web':raise RuntimeError('Unexpected project; preserve other deployments')
    body={'name':project['projectName'],'project':project['projectId'],'target':'production','files':files,
          'projectSettings':{key:config[key] for key in ('framework','buildCommand','installCommand','outputDirectory')}}
    body['projectSettings']['rootDirectory']='web'
    body.update({key:config[key] for key in ('headers','rewrites','redirects') if key in config})
    result=api('POST','/v13/deployments',body);state.write_text(json.dumps(result,indent=2)+'\n');state.chmod(0o600)
    print(json.dumps({key:result.get(key) for key in ('id','url','readyState')},ensure_ascii=False))

if __name__=='__main__':main()
