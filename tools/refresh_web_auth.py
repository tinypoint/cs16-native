#!/usr/bin/env python3
"""Refresh the existing Vercel CLI OAuth session without printing credentials."""
import base64,json,os,subprocess,tempfile,time,urllib.parse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 path=ROOT/'.local/web-access/vercel-auth.json';auth=json.loads(path.read_text());part=auth['id_token'].split('.')[1];claims=json.loads(base64.urlsafe_b64decode(part+'='*(-len(part)%4)))
 if claims.get('iss')!='https://vercel.com' or not isinstance(claims.get('aud'),str):raise SystemExit('Unexpected OAuth issuer/client')
 data=urllib.parse.urlencode({'grant_type':'refresh_token','client_id':claims['aud'],'refresh_token':auth['refresh_token']})
 with tempfile.NamedTemporaryFile() as body:
  body.write(data.encode());body.flush();response=subprocess.run(['curl','-sS','--connect-timeout','10','--max-time','30','-H','Content-Type: application/x-www-form-urlencoded','--data-binary','@'+body.name,'https://api.vercel.com/login/oauth/token'],capture_output=True,text=True,check=True)
 result=json.loads(response.stdout)
 if 'access_token' not in result:raise SystemExit('OAuth refresh failed: '+str(result.get('error','unknown')))
 result={**auth,**result,'obtained_at':time.time()};tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(result,indent=2)+'\n');tmp.chmod(0o600);os.replace(tmp,path);print('Existing Vercel session refreshed')
if __name__=='__main__':main()
