#!/usr/bin/env python3
"""Real-HTTP regression for HTML body/frameset Window event-handler targeting."""
import argparse, hashlib, http.server, json, os, subprocess, tempfile, threading
from pathlib import Path

HTML=r'''<!doctype html><meta charset=utf-8><body><script>
'use strict';const rows=[];const eq=(a,b)=>{if(a!==b)throw Error('expected '+String(b)+', got '+String(a));};
function check(name,fn){try{fn();rows.push({i:rows.length,name,ok:true});}catch(e){rows.push({i:rows.length,name,ok:false,error:e.name+': '+e.message});}console.log('BORROWED_WINDOW_CHECK '+JSON.stringify(rows[rows.length-1]));}
const frame=document.createElement('iframe');document.body.appendChild(frame);const child=frame.contentWindow;
const load=Object.getOwnPropertyDescriptor(Window.prototype,'onload'),error=Object.getOwnPropertyDescriptor(Window.prototype,'onerror');
check('borrowed Window setter is visible to child and borrowed getter',()=>{const f=function(){};load.set.call(child,f);eq(child.onload,f);eq(load.get.call(child),f);});
check('child replacement after borrowed setter has one active listener',()=>{let old=0,next=0,receiver=null;load.set.call(child,()=>old++);child.onload=function(){next++;receiver=this;};child.dispatchEvent(new child.Event('load'));eq(old,0);eq(next,1);eq(receiver,child);});
check('borrowed null setter deactivates the child listener before reactivation',()=>{let count=0;load.set.call(child,null);eq(child.onload,null);child.dispatchEvent(new child.Event('load'));child.onload=()=>count++;child.dispatchEvent(new child.Event('load'));eq(count,1);load.set.call(child,null);});
check('borrowed Window onerror accepts cross-realm ErrorEvent five arguments',()=>{const calls=[],reason={};error.set.call(child,function(...args){calls.push({receiver:this,args});return true;});const event=new ErrorEvent('error',{message:'foreign',filename:'parent.js',lineno:2,colno:3,error:reason,cancelable:true});const result=child.dispatchEvent(event);error.set.call(child,null);eq(calls.length,1);eq(calls[0].receiver,child);eq(calls[0].args.length,5);eq(calls[0].args[0],'foreign');eq(calls[0].args[4],reason);eq(result,false);});
frame.remove();const passed=rows.filter(r=>r.ok).length;console.log('BORROWED_WINDOW_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));document.documentElement.setAttribute('data-borrowed-window-done','1');
</script>'''
CSP_HTML='<!doctype html><meta charset=utf-8><body onload="window.__cspAliasRan=true;"><div id=ordinary onclick="window.__cspAliasRan=true;"></div><script src="/csp.js"></script>'
CSP_JS="window.addEventListener('load',function(){const initial=document.body.onload===null;document.body.setAttribute('onresize','window.__cspAliasRan=true;');const dynamic=document.body.onresize===null;let calls=0;const prior=function(){calls++;};window.onresize=prior;document.body.setAttribute('onresize','window.__cspAliasRan=true;');const preserved=window.onresize===prior;window.dispatchEvent(new Event('resize'));window.__cspAliasReport={bodyGetterNull:initial,dynamicGetterNull:dynamic,ordinaryGetterNull:document.getElementById('ordinary').onclick===null,ran:window.__cspAliasRan===true,allowedScript:true,priorPreserved:preserved,priorCalls:calls};});"
EXPECTED=4
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path=self.path.split('?',1)[0]
        value=CSP_HTML if path=='/csp.html' else CSP_JS if path=='/csp.js' else HTML
        data=value.encode();self.send_response(200);self.send_header('Content-Type','text/javascript; charset=utf-8' if path=='/csp.js' else 'text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)))
        if path=='/csp.html':self.send_header('Content-Security-Policy',"script-src 'self'; object-src 'none'")
        self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass

def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);a=p.parse_args();binary=a.binary.resolve(strict=True)
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-body-window-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-borrowed-window-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'12000'],text=True,capture_output=True,env=env,timeout=18)
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];results=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in [('BORROWED_WINDOW_CHECK ',rows),('BORROWED_WINDOW_RESULTS ',results)]:
            at=line.find(marker)
            if at>=0:target.append(json.loads(line[at+len(marker):]))
    if run.returncode or len(rows)!=EXPECTED or len(results)!=1 or [r['i'] for r in rows]!=list(range(EXPECTED)):
        print('BORROWED_WINDOW_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'expected':EXPECTED,'results':results}));return 2
    passed=sum(row['ok'] is True for row in rows)
    if results[0]!={'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}:raise RuntimeError('Inconsistent fixture summary')
    print('BORROWED_WINDOW_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),'cspHTMLSHA256':hashlib.sha256(CSP_HTML.encode()).hexdigest(),'cspJSSHA256':hashlib.sha256(CSP_JS.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
    print('RESULTS borrowed-window-handlers %s (%d/%d checks)'%('PASS' if passed==EXPECTED else 'FAIL',passed,EXPECTED));return 0 if passed==EXPECTED else 1
if __name__=='__main__':raise SystemExit(main())
