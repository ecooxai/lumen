#!/usr/bin/env python3
"""Observe srcdoc completion tasks, parent blocking and frame cancellation."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

HTML = r'''<!doctype html><meta charset=utf-8><title>Iframe completion tasks</title><body><script>
'use strict';
const rows=[];
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const eq=(actual,expected)=>{if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));};
async function check(name,fn){let timer;try{await Promise.race([Promise.resolve().then(fn),new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('check timeout')),600);})]);rows.push({i:rows.length,name,ok:true});}catch(e){rows.push({i:rows.length,name,ok:false,error:e.name+': '+e.message});}finally{clearTimeout(timer);for(const f of [...document.querySelectorAll('iframe')])f.remove();}console.log('FRAME_LOAD_CHECK '+JSON.stringify(rows[rows.length-1]));}
function iframe(srcdoc){const f=document.createElement('iframe');if(srcdoc!==undefined)f.srcdoc=srcdoc;return f;}
(async()=>{
 await check('srcdoc load reaches a handler installed after append',async()=>{
  const f=iframe('<p>A</p>');document.body.appendChild(f);
  await new Promise(resolve=>{f.onload=resolve;});eq(f.contentDocument.body.textContent,'A');
 });
 await check('srcdoc load runs after append and microtask checkpoint',async()=>{
  const events=[],f=iframe('<p>A</p>');const loaded=new Promise(resolve=>{f.onload=()=>{events.push('load');resolve();};});
  document.body.appendChild(f);events.push('append');Promise.resolve().then(()=>events.push('microtask'));
  await loaded;eq(events.join(','),'append,microtask,load');
 });
 await check('removed srcdoc has no obsolete load event',async()=>{
  let loads=0;const f=iframe('<p>A</p>');f.onload=()=>++loads;document.body.appendChild(f);f.remove();
  await delay(25);eq(loads,0);
 });
 await check('replaced srcdoc dispatches only current document load',async()=>{
  const values=[],f=iframe('<p>A</p>');f.onload=()=>values.push(f.contentDocument.body.textContent);
  document.body.appendChild(f);f.srcdoc='<p>B</p>';await delay(40);eq(values.join(','),'B');
 });
 await check('initial blank insertion preserves synchronous iframe load',()=>{
  let loads=0;const f=iframe();f.onload=()=>++loads;document.body.appendChild(f);
  eq(loads,1);eq(f.contentDocument.readyState,'complete');
 });
 await check('late blank handler does not redispatch completed initial load',async()=>{
  let loads=0;const f=iframe();document.body.appendChild(f);f.onload=()=>++loads;await delay(25);eq(loads,0);
 });
 await check('parent completion waits for srcdoc child completion task',async()=>{
  const f=iframe();f.src='/nested.html';
  const result=new Promise(resolve=>{window.addEventListener('message',function listener(e){if(e.source!==f.contentWindow)return;window.removeEventListener('message',listener);resolve(e.data);});});
  document.body.appendChild(f);eq((await result).join(','),'append,child,parent');
 });
 const passed=rows.filter(r=>r.ok).length;console.log('FRAME_LOAD_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));document.documentElement.setAttribute('data-frame-load-done','1');
})().catch(e=>console.log('FRAME_LOAD_INFRA '+e.name+': '+e.message));
</script>'''
NESTED = r'''<!doctype html><meta charset=utf-8><body><script>
const events=[];window.onload=()=>{events.push('parent');parent.postMessage(events,'*');};
const f=document.createElement('iframe');f.srcdoc='<p>child</p>';f.onload=()=>events.push('child');document.body.appendChild(f);events.push('append');
</script>'''

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        data=(NESTED if self.path.split('?',1)[0]=='/nested.html' else HTML).encode()
        self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);args=parser.parse_args();binary=args.binary.resolve()
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-frame-load-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')}
            env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-frame-load-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'10000'],capture_output=True,text=True,env=env,timeout=15)
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];results=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,destination in [('FRAME_LOAD_CHECK ',rows),('FRAME_LOAD_RESULTS ',results)]:
            index=line.find(marker)
            if index>=0:destination.append(json.loads(line[index+len(marker):]))
    if run.returncode or len(rows)!=7 or len(results)!=1 or [row['i'] for row in rows]!=list(range(7)):
        print('FRAME_LOAD_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'results':results}));return 2
    passed=sum(row['ok'] is True for row in rows)
    if results[0]!={'total':7,'passed':passed,'failed':7-passed}:raise RuntimeError('Inconsistent fixture result')
    print('FRAME_LOAD_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),'nestedSHA256':hashlib.sha256(NESTED.encode()).hexdigest(),
        'total':7,'passed':passed,'failed':7-passed}))
    print('RESULTS frame-load-tasks %s (%d/7 checks)'%('PASS' if passed==7 else 'FAIL',passed))
    return 0 if passed==7 else 1

if __name__=='__main__':raise SystemExit(main())
