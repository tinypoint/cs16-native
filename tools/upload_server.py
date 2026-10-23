#!/usr/bin/env python3
"""Private LAN file receiver. Streams files to disk without executing them."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit, parse_qs, unquote
import argparse, json, os, secrets, socket, time

HTML = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CS 1.6 资源上传</title><style>body{font:17px system-ui;background:#14171c;color:#eee;max-width:600px;margin:48px auto;padding:20px}button,input{font:inherit;margin:16px 0}button{padding:12px 22px}progress{width:100%}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style><h1>CS 1.6 资源上传</h1><p>选择安装包或压缩包，上传到电脑。支持多个文件。</p><input id="files" type="file" multiple><br><button id="send">上传</button><progress id="progress" max="100" value="0"></progress><pre id="status">等待选择文件</pre><script>
const files=document.querySelector('#files'),button=document.querySelector('#send'),bar=document.querySelector('#progress'),status=document.querySelector('#status');
button.onclick=async()=>{if(!files.files.length){status.textContent='请先选择文件';return;}button.disabled=true;files.disabled=true;try{let finished=0;const total=Array.from(files.files).reduce((n,f)=>n+f.size,0);for(const file of files.files){await new Promise((resolve,reject)=>{const xhr=new XMLHttpRequest();xhr.open('PUT',location.pathname+'upload?name='+encodeURIComponent(file.name));xhr.upload.onprogress=e=>{bar.value=total?(finished+e.loaded)/total*100:0;status.textContent=file.name+'：'+(e.loaded/1048576).toFixed(1)+' / '+(file.size/1048576).toFixed(1)+' MB';};xhr.onload=()=>{if(xhr.status===201){finished+=file.size;resolve();}else reject(new Error(xhr.responseText||'上传失败'));};xhr.onerror=()=>reject(new Error('连接中断，请重试'));xhr.send(file);});}bar.value=100;status.textContent='全部上传成功，文件已保存到电脑。';}catch(e){status.textContent=String(e);}finally{button.disabled=false;files.disabled=false;}};
</script></html>'''

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--port',type=int,default=8878);ap.add_argument('--directory',type=Path,required=True);ap.add_argument('--state',type=Path,required=True);args=ap.parse_args()
    dest=args.directory.resolve();dest.mkdir(parents=True,exist_ok=True);token=secrets.token_urlsafe(18);prefix='/'+token+'/'
    class Handler(BaseHTTPRequestHandler):
        def reply(self,code,body,kind='text/plain; charset=utf-8'):
            data=body.encode();self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(data)
        def do_GET(self):
            if urlsplit(self.path).path!=prefix:self.reply(404,'Not found');return
            self.reply(200,HTML,'text/html; charset=utf-8')
        def do_PUT(self):
            req=urlsplit(self.path)
            if req.path!=prefix+'upload':self.reply(404,'Not found');return
            name=parse_qs(req.query).get('name',[''])[0].replace('\\','/').split('/')[-1]
            if not name or name in ('.','..') or any(ord(c)<32 for c in name):self.reply(400,'无效文件名');return
            try:length=int(self.headers.get('Content-Length','-1'))
            except ValueError:length=-1
            if length<0 or length>8*1024**3:self.reply(413,'文件大小无效，单文件上限 8 GB');return
            # Reserve a new destination; never overwrite a previous upload.
            target=dest/name
            for n in range(10000):
                candidate=target if n==0 else target.with_name(target.stem+'-'+str(n)+target.suffix)
                try:fd=os.open(candidate,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600);break
                except FileExistsError:continue
            else:self.reply(409,'重名文件过多');return
            temp=dest/('.upload-'+secrets.token_hex(12)+'.part');os.close(fd)
            try:
                remaining=length;self.connection.settimeout(300)
                with temp.open('xb') as out:
                    while remaining:
                        chunk=self.rfile.read(min(1024*1024,remaining))
                        if not chunk:raise ConnectionError('Incomplete upload')
                        out.write(chunk);remaining-=len(chunk)
                    out.flush();os.fsync(out.fileno())
                os.replace(temp,candidate)
                with (dest/'.received.jsonl').open('a') as receipt:receipt.write(json.dumps({'time':time.time(),'file':candidate.name,'bytes':length},ensure_ascii=False)+'\n')
                self.reply(201,json.dumps({'saved':candidate.name,'bytes':length},ensure_ascii=False),'application/json; charset=utf-8')
            except Exception as error:
                temp.unlink(missing_ok=True);candidate.unlink(missing_ok=True)
                self.log_error('upload failed: %s',error)
                try:self.reply(500,'上传未完成，请重试')
                except (BrokenPipeError,ConnectionResetError):pass
    server=ThreadingHTTPServer(('0.0.0.0',args.port),Handler);server.daemon_threads=True
    args.state.parent.mkdir(parents=True,exist_ok=True);args.state.write_text(json.dumps({'pid':os.getpid(),'port':args.port,'path':prefix,'directory':str(dest)}));args.state.chmod(0o600)
    print('LAN receiver ready on port',args.port,flush=True)
    server.serve_forever()

if __name__=='__main__':main()
