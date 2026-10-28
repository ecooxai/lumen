#!/usr/bin/env python3
"""Real-HTTP regression for HTML body/frameset Window event-handler targeting."""
import argparse, hashlib, http.server, json, os, subprocess, tempfile, threading
from pathlib import Path

HTML=r'''<!doctype html><meta charset=utf-8><title>Body WindowEventHandlers</title>
<body onload="window.__naturalBodyLoad=(window.__naturalBodyLoad||0)+1;window.__naturalBodyThis=this===window;"><script>
'use strict';
const rows=[], reflected='blur error focus load resize scroll afterprint beforeprint beforeunload hashchange languagechange message messageerror offline online pageswap pagehide pagereveal pageshow popstate rejectionhandled storage unhandledrejection unload'.split(' ');
const eq=(a,b)=>{if(a!==b)throw Error('expected '+String(b)+', got '+String(a));};
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function check(name,fn){try{await fn();rows.push({i:rows.length,name,ok:true});}catch(e){rows.push({i:rows.length,name,ok:false,error:e.name+': '+e.message});}console.log('BODY_WINDOW_CHECK '+JSON.stringify(rows[rows.length-1]));}
const loaded=new Promise(resolve=>window.addEventListener('load',()=>setTimeout(resolve,0),{once:true}));
(async()=>{
 await loaded;
 await check('parsed body onload runs once with Window receiver',()=>{eq(window.__naturalBodyLoad,1);eq(window.__naturalBodyThis,true);});
 const body=document.body, frameset=document.createElement('frameset');
 for(const type of reflected) await check('body and frameset reflect Window on'+type,()=>{
  const key='on'+type, first=function(){},second=function(){};
  body[key]=first;eq(window[key],first);eq(frameset[key],first);
  frameset[key]=second;eq(window[key],second);eq(body[key],second);window[key]=null;
 });
 await check('Window assignment is visible through body',()=>{const f=function(){};window.onload=f;eq(body.onload,f);window.onload=null;});
 await check('disconnected body still targets its active document Window',()=>{const other=document.createElement('body'),f=function(){};other.onload=f;eq(window.onload,f);eq(body.onload,f);window.onload=null;});
 await check('inactive document body handler getter is null and setter ignored',()=>{const inert=document.implementation.createHTMLDocument('inert'),f=function(){};inert.body.onload=f;eq(inert.body.onload,null);eq(window.onload,null);});
 await check('foreign body targets its own Window through a borrowed setter',()=>{
  const frame=document.createElement('iframe');body.appendChild(frame);
  try{const child=frame.contentWindow,foreign=child.document.body,f=function(){};
   const descriptor=Object.getOwnPropertyDescriptor(HTMLBodyElement.prototype,'onload');
   descriptor.set.call(foreign,f);eq(child.onload,f);eq(foreign.onload,f);eq(window.onload,null);
  }finally{frame.remove();}
 });
 await check('ordinary element load handler remains element local',()=>{const div=document.createElement('div'),f=function(){};div.onload=f;eq(div.onload,f);eq(window.onload,null);});
 await check('body click is not in the reflecting set',()=>{const f=function(){};body.onclick=f;eq(body.onclick,f);eq(window.onclick,null);body.onclick=null;});
 await check('body dispatch does not invoke aliased Window handler',()=>{let count=0,receiver=null;body.onresize=function(){count++;receiver=this;};body.dispatchEvent(new Event('resize'));eq(count,0);window.dispatchEvent(new Event('resize'));eq(count,1);eq(receiver,window);window.onresize=null;});
 await check('body addEventListener remains element local',()=>{let count=0;const f=()=>count++;body.addEventListener('resize',f);window.dispatchEvent(new Event('resize'));eq(count,0);body.dispatchEvent(new Event('resize'));eq(count,1);body.removeEventListener('resize',f);});
 await check('handler activation respects registration order and replacement position',()=>{
  const order=[],a=()=>order.push('a'),b=()=>order.push('b');window.onresize=null;window.addEventListener('resize',a);body.onresize=()=>order.push('old');window.addEventListener('resize',b);body.onresize=()=>order.push('new');window.dispatchEvent(new Event('resize'));
  window.removeEventListener('resize',a);window.removeEventListener('resize',b);window.onresize=null;eq(order.join(','),'a,new,b');
 });
 await check('null deactivation and reactivation moves listener to the end',()=>{
  const order=[],a=()=>order.push('a'),b=()=>order.push('b');window.addEventListener('resize',a);body.onresize=()=>order.push('old');window.addEventListener('resize',b);body.onresize=null;body.onresize=()=>order.push('new');window.dispatchEvent(new Event('resize'));
  window.removeEventListener('resize',a);window.removeEventListener('resize',b);window.onresize=null;eq(order.join(','),'a,b,new');
 });
 await check('body content attribute compiles through the Window handler',()=>{window.__inlineAlias=0;body.setAttribute('onresize','window.__inlineAlias++;window.__inlineAliasThis=this===window;');eq(body.onresize,window.onresize);eq(typeof window.onresize,'function');window.dispatchEvent(new Event('resize'));eq(window.__inlineAlias,1);eq(window.__inlineAliasThis,true);body.removeAttribute('onresize');});
 await check('setting the same content attribute replaces an intervening IDL assignment',()=>{const source='window.__sameAlias=(window.__sameAlias||0)+1;';body.setAttribute('onresize',source);body.onresize=()=>{};body.setAttribute('onresize',source);window.__sameAlias=0;window.dispatchEvent(new Event('resize'));eq(window.__sameAlias,1);body.removeAttribute('onresize');});
 await check('removing a present content attribute clears an intervening Window assignment',()=>{body.setAttribute('onresize','');window.onresize=function(){};body.removeAttribute('onresize');eq(window.onresize,null);});
 await check('Attr.value updates the Window handler',()=>{body.setAttribute('onresize','window.__attrAlias=1;');body.getAttributeNode('onresize').value='window.__attrAlias=2;';window.__attrAlias=0;window.dispatchEvent(new Event('resize'));eq(window.__attrAlias,2);body.removeAttribute('onresize');});
 await check('setAttributeNode updates the Window handler',()=>{const attr=document.createAttribute('onresize');attr.value='window.__nodeAlias=3;';body.setAttributeNode(attr);window.__nodeAlias=0;window.dispatchEvent(new Event('resize'));eq(window.__nodeAlias,3);body.removeAttribute('onresize');});
 await check('Window error alias preserves five arguments and true cancellation',()=>{const error={},calls=[];body.onerror=function(...args){calls.push({receiver:this,args});return true;};const event=new ErrorEvent('error',{message:'message',filename:'fixture.js',lineno:7,colno:9,error,cancelable:true});const result=window.dispatchEvent(event);window.onerror=null;eq(calls.length,1);eq(calls[0].receiver,window);eq(calls[0].args.length,5);eq(calls[0].args[0],'message');eq(calls[0].args[4],error);eq(result,false);});
 await check('syntax failure replaces a prior valid Window handler with null',()=>{const prior=function(){};window.onresize=prior;const ignore=e=>e.preventDefault();window.addEventListener('error',ignore);try{body.setAttribute('onresize','if (');eq(body.onresize,null);eq(window.onresize,null);}finally{window.removeEventListener('error',ignore);body.removeAttribute('onresize');}});
 let cspFrame=null;
 await check('CSP blocks body alias and ordinary inline handlers through the same compiler',async()=>{
  const frame=cspFrame=document.createElement('iframe');frame.src='/csp.html';body.appendChild(frame);
  try{let result=null;for(let i=0;i<100;i++){try{result=frame.contentWindow.__cspAliasReport;}catch(e){}if(result)break;await wait(10);}if(!result)throw Error('CSP fixture timeout');eq(result.bodyGetterNull,true);eq(result.dynamicGetterNull,true);eq(result.ordinaryGetterNull,true);eq(result.ran,false);eq(result.allowedScript,true);
  }finally{}
 });
 await check('CSP-blocked attribute update preserves prior JS-installed Window callback',()=>{const r=cspFrame.contentWindow.__cspAliasReport;eq(r.priorPreserved,true);eq(r.priorCalls,1);});
 await check('borrowed parent attribute method uses foreign owner document CSP',()=>{const child=cspFrame.contentWindow,foreign=child.document.body,prior=child.onresize;Element.prototype.setAttribute.call(foreign,'onresize','window.__foreignCSPBypass=true;');eq(child.onresize,prior);child.dispatchEvent(new child.Event('resize'));eq(child.__foreignCSPBypass,undefined);eq(window.__foreignCSPBypass,undefined);});
 if(cspFrame)cspFrame.remove();
 const passed=rows.filter(row=>row.ok).length;console.log('BODY_WINDOW_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));document.documentElement.setAttribute('data-body-window-done','1');
})().catch(e=>console.log('BODY_WINDOW_INFRA '+e.name+': '+e.message));
</script>'''
CSP_HTML='<!doctype html><meta charset=utf-8><body onload="window.__cspAliasRan=true;"><div id=ordinary onclick="window.__cspAliasRan=true;"></div><script src="/csp.js"></script>'
CSP_JS="window.addEventListener('load',function(){const initial=document.body.onload===null;document.body.setAttribute('onresize','window.__cspAliasRan=true;');const dynamic=document.body.onresize===null;let calls=0;const prior=function(){calls++;};window.onresize=prior;document.body.setAttribute('onresize','window.__cspAliasRan=true;');const preserved=window.onresize===prior;window.dispatchEvent(new Event('resize'));window.__cspAliasReport={bodyGetterNull:initial,dynamicGetterNull:dynamic,ordinaryGetterNull:document.getElementById('ordinary').onclick===null,ran:window.__cspAliasRan===true,allowedScript:true,priorPreserved:preserved,priorCalls:calls};});"
EXPECTED=45
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
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-body-window-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'12000'],text=True,capture_output=True,env=env,timeout=18)
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];results=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in [('BODY_WINDOW_CHECK ',rows),('BODY_WINDOW_RESULTS ',results)]:
            at=line.find(marker)
            if at>=0:target.append(json.loads(line[at+len(marker):]))
    if run.returncode or len(rows)!=EXPECTED or len(results)!=1 or [r['i'] for r in rows]!=list(range(EXPECTED)):
        print('BODY_WINDOW_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'expected':EXPECTED,'results':results}));return 2
    passed=sum(row['ok'] is True for row in rows)
    if results[0]!={'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}:raise RuntimeError('Inconsistent fixture summary')
    print('BODY_WINDOW_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),'cspHTMLSHA256':hashlib.sha256(CSP_HTML.encode()).hexdigest(),'cspJSSHA256':hashlib.sha256(CSP_JS.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
    print('RESULTS body-window-handlers %s (%d/%d checks)'%('PASS' if passed==EXPECTED else 'FAIL',passed,EXPECTED));return 0 if passed==EXPECTED else 1
if __name__=='__main__':raise SystemExit(main())
