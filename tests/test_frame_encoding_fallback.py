#!/usr/bin/env python3
"""Same-origin parent encoding fallback with precedence and sandbox controls."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

CASES=[
 {'name':'Windows1252 parent fallback','parent':'windows-1252','bytes':[128],'encoding':'windows-1252','text':'€'},
 {'name':'unsupported transport label uses parent fallback','parent':'windows-1252','header':'utf-32','bytes':[128],'encoding':'windows-1252','text':'€'},
 {'name':'Big5 parent fallback','parent':'Big5','bytes':[164,244],'encoding':'Big5','text':'水'},
 {'name':'UTF16LE parent encoding is not inherited','parent':'UTF-16LE','bytes':[195,169],'encoding':'UTF-8','text':'é'},
 {'name':'supported transport label overrides parent','parent':'windows-1252','header':'utf-8','bytes':[195,169],'encoding':'UTF-8','text':'é'},
 {'name':'HTML metadata overrides parent','parent':'windows-1252','meta':'utf-8','bytes':[195,169],'encoding':'UTF-8','text':'é'},
 {'name':'UTF8 BOM overrides parent','parent':'windows-1252','bom':True,'bytes':[195,169],'encoding':'UTF-8','text':'é'},
 {'name':'cross-origin child does not inherit','parent':'windows-1252','cross':True,'bytes':[128],'encoding':'UTF-8','text':'�'},
 {'name':'sandbox opaque-origin child does not inherit','parent':'windows-1252','sandbox':True,'bytes':[128],'encoding':'UTF-8','text':'�'},
 {'name':'CSP sandbox opaque-origin child does not inherit','parent':'windows-1252','csp':True,'bytes':[128],'encoding':'UTF-8','text':'�'},
 {'name':'UTF8 parent fallback stays UTF8','parent':'UTF-8','bytes':[195,169],'encoding':'UTF-8','text':'é'},
 {'name':'UTF16BE parent encoding is not inherited','parent':'UTF-16BE','bytes':[195,169],'encoding':'UTF-8','text':'é'},
 {'name':'UTF16 child transport remains authoritative','parent':'windows-1252','header':'utf-16le','childCodec':'utf-16le','bytes':[233],'encoding':'UTF-16LE','text':'é'},
 {'name':'Big5 meta overrides a Windows1252 parent','parent':'windows-1252','meta':'big5','bytes':[164,244],'encoding':'Big5','text':'水'},
 {'name':'unsupported meta label permits Big5 parent fallback','parent':'Big5','meta':'bogus','bytes':[164,244],'encoding':'Big5','text':'水'},
]
EXPECTED=len(CASES)
HTML=r'''<!doctype html><meta charset=utf-8><title>Frame encoding fallback</title><body><script>
const cases=__CASES__,rows=[];const eq=(a,b)=>{if(a!==b)throw Error('expected '+JSON.stringify(b)+', got '+JSON.stringify(a));};
(async()=>{for(let i=0;i<cases.length;i++){const f=document.createElement('iframe');let timer,listener;try{
 const result=new Promise((resolve,reject)=>{listener=e=>{if(e.source===f.contentWindow&&e.data&&e.data.kind==='parent-result'&&e.data.index===i)resolve(e.data);};window.addEventListener('message',listener);timer=setTimeout(()=>reject(Error('frame result timeout')),1200);});
 f.src='/parent/'+i;document.body.appendChild(f);const row=await result;eq(row.parentEncoding,cases[i].parent);eq(row.encoding,cases[i].encoding);eq(row.text,cases[i].text);rows.push({i,name:cases[i].name,ok:true});
 }catch(e){rows.push({i,name:cases[i].name,ok:false,error:e.name+': '+e.message});}finally{clearTimeout(timer);window.removeEventListener('message',listener);f.remove();}console.log('FRAME_ENCODING_CHECK '+JSON.stringify(rows[rows.length-1]));}
 const passed=rows.filter(r=>r.ok).length;console.log('FRAME_ENCODING_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));document.documentElement.setAttribute('data-frame-encoding-done','1');
})().catch(e=>console.log('FRAME_ENCODING_INFRA '+e.name+': '+e.message));
</script>'''.replace('__CASES__',json.dumps(CASES,ensure_ascii=True))

def parent_bytes(index,other_port):
    row=CASES[index];target=('http://127.0.0.1:'+str(other_port) if row.get('cross') else '')+'/child/'+str(index)
    page='''<!doctype html><body><script>
const child=document.createElement('iframe');
window.addEventListener('message',e=>{if(e.source===child.contentWindow&&e.data&&e.data.kind==='child-result')parent.postMessage({kind:'parent-result',index:INDEX,parentEncoding:document.characterSet,encoding:e.data.encoding,text:e.data.text},'*');});
SANDBOX
child.src=TARGET;document.body.appendChild(child);
</script>'''.replace('INDEX',str(index)).replace('TARGET',json.dumps(target)).replace('SANDBOX',"child.setAttribute('sandbox','allow-scripts');" if row.get('sandbox') else '')
    codec={'UTF-16LE':'utf-16le','UTF-16BE':'utf-16be'}.get(row['parent'],'ascii')
    return page.encode(codec)

def child_bytes(index):
    row=CASES[index];prefix=('<meta charset='+row['meta']+'>' if row.get('meta') else '')
    suffix="</p><script>parent.postMessage({kind:'child-result',encoding:document.characterSet,text:document.getElementById('payload').textContent},'*');</script>"
    if row.get('childCodec'):return (prefix+'<p id=payload>'+bytes(row['bytes']).decode('latin-1')+suffix).encode(row['childCodec'])
    return (b'\xef\xbb\xbf' if row.get('bom') else b'')+prefix.encode()+b'<p id=payload>'+bytes(row['bytes'])+suffix.encode()

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path=self.path.split('?',1)[0];csp=False
        if path.startswith('/parent/'):
            index=int(path.removeprefix('/parent/'));content_type='text/html;charset='+CASES[index]['parent'];data=parent_bytes(index,self.server.other_port)
        elif path.startswith('/child/'):
            index=int(path.removeprefix('/child/'));row=CASES[index];content_type='text/html'+(';charset='+row['header'] if row.get('header') else '');data=child_bytes(index);csp=row.get('csp',False)
        else:content_type,data='text/html;charset=utf-8',HTML.encode()
        self.send_response(200);self.send_header('Content-Type',content_type);self.send_header('Content-Length',str(len(data)))
        if csp:self.send_header('Content-Security-Policy','sandbox allow-scripts')
        self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass

def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);a=p.parse_args();binary=a.binary.resolve(strict=True)
    servers=[http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)for _ in range(2)];threads=[]
    for server in servers:
        server.other_port=servers[1].server_port;thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();threads.append(thread)
    try:
        with tempfile.TemporaryDirectory(prefix='lume-frame-encoding-')as profile:
            env={k:v for k,v in os.environ.items()if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-frame-encoding-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%servers[0].server_port,'25000'],capture_output=True,text=True,env=env,timeout=30)
    except subprocess.TimeoutExpired as e:print('FRAME_ENCODING_INFRA '+json.dumps({'error':'timeout','seconds':e.timeout}));return 2
    finally:
        for server in servers:server.shutdown();server.server_close()
        for thread in threads:thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];results=[]
    for line in(run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in[('FRAME_ENCODING_CHECK ',rows),('FRAME_ENCODING_RESULTS ',results)]:
            at=line.find(marker)
            if at>=0:target.append(json.loads(line[at+len(marker):]))
    if run.returncode or len(rows)!=EXPECTED or len(results)!=1 or[r['i']for r in rows]!=list(range(EXPECTED)):
        print('FRAME_ENCODING_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'results':results}));return 2
    passed=sum(row['ok']is True for row in rows)
    if results[0]!={'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}:raise RuntimeError('Inconsistent fixture summary')
    print('FRAME_ENCODING_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
    print('RESULTS frame-encoding-fallback %s (%d/%d checks)'%('PASS'if passed==EXPECTED else'FAIL',passed,EXPECTED));return 0 if passed==EXPECTED else 1

if __name__=='__main__':raise SystemExit(main())
