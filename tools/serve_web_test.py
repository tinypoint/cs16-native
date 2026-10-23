#!/usr/bin/env python3
"""Loopback-only development server for the original-program Wasm experiment."""
from http.server import ThreadingHTTPServer,SimpleHTTPRequestHandler
import argparse,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PUBLISH=False
class Handler(SimpleHTTPRequestHandler):
 def __init__(self,*a,**kw):super().__init__(*a,directory=str(ROOT/('.local/web-publish' if PUBLISH else '.local/web-test')),**kw)
 def translate_path(self,path):
  clean=path.split('?',1)[0]
  if PUBLISH:return super().translate_path(path)
  if clean.startswith('/build/'):
   name=clean.removeprefix('/build/')
   if Path(name).name!=name:return '/nonexistent'
   return str(ROOT/'.local/web-build'/name)
  if clean=='/pack-info.json':return str(ROOT/'.local/web-startup/manifest.json')
  if clean=='/pack.gz':return json.loads((ROOT/'.local/web-startup/manifest.json').read_text())['path']
  return super().translate_path(path)
 def end_headers(self):
  for key,value in [('Cross-Origin-Opener-Policy','same-origin'),('Cross-Origin-Embedder-Policy','require-corp'),('Cross-Origin-Resource-Policy','same-origin'),('Cache-Control','no-store')]:self.send_header(key,value)
  if PUBLISH:
   config=json.loads((ROOT/'web/vercel.json').read_text())
   for header in config['headers'][0]['headers']:
    if header['key']=='Content-Security-Policy':self.send_header(header['key'],header['value'])
  super().end_headers()
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--publish',action='store_true');ap.add_argument('--port',type=int,default=8896);args=ap.parse_args();PUBLISH=args.publish
 ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
