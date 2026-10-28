#!/usr/bin/env python3
import argparse, hashlib, http.server, json, os, subprocess, tempfile, threading
from pathlib import Path
HTML = r'''<!doctype html><meta charset="utf-8"><title>Frame and microtask realm regression</title><iframe></iframe><iframe></iframe><iframe></iframe><script>
'use strict';
const results=[], reports=[], handlerReports=[], diagnostics={}, wins=[];
let loadCount=0, rejectionCount=0;
function check(name,ok,detail){const row={i:results.length,name,ok:!!ok,detail:detail===undefined?null:detail};results.push(row);console.log('FRAME_MICROTASK_CHECK '+JSON.stringify(row));}
function tick(){return new Promise(resolve=>setTimeout(resolve,8));}
const loaded=new Promise(resolve=>addEventListener('load',()=>{loadCount++;resolve();}));
const eager=[frames[0],frames[1],frames[2]];
for(let i=0;i<3;i++)check('static index '+i+' ready before length/contentWindow',!!eager[i]);
const els=document.querySelectorAll('iframe');
for(let i=0;i<3;i++)wins.push(els[i].contentWindow);
for(let i=0;i<3;i++)check('indexed frame '+i+' identity',frames[i]===wins[i]);
check('frame count',length===3,length);
check('out-of-range frame',frames[3]===undefined);
check('blank frames synchronously accessible',wins.every(w=>w.document&&w.document.body));
const scopes=[window,...wins],names=['top','frame0','frame1','frame2'];
for(let i=0;i<scopes.length;i++){
 scopes[i].addEventListener('error',e=>{reports.push({realm:names[i],error:e.error});e.preventDefault();});
 scopes[i].onerror=function(){handlerReports.push(names[i]);return true;};
 scopes[i].addEventListener('unhandledrejection',e=>{rejectionCount++;e.preventDefault();});
}
window.__fmCalls=0;window.__fmExpected=null;
function thrower(w){return new w.Function('parent.__fmCalls++; throw parent.__fmExpected;');}
function realmNames(){return reports.map(r=>r.realm).join(',');}
async function routed(label,scheduler,callback,expectedRealm,error){
 reports.length=0;handlerReports.length=0;window.__fmCalls=0;window.__fmExpected=error;
 scheduler.queueMicrotask(callback);await tick();
 check(label+' report realm',realmNames()===expectedRealm,realmNames());
 check(label+' original error',reports.length===1&&reports[0].error===error);
}
(async()=>{
 await loaded;await tick();check('parent load once',loadCount===1,loadCount);
 await routed('three-realm callback',wins[0],thrower(wins[1]),'frame1',new wins[2].Error('frame2 error'));
 check('three-realm onerror property realm',handlerReports.join(',')==='frame1',handlerReports);
 check('three-realm callback runs once',window.__fmCalls===1,window.__fmCalls);
 await routed('top callback',wins[0],()=>{window.__fmCalls++;throw window.__fmExpected;},'top',new Error('top error'));
 await routed('top scheduler',window,thrower(wins[1]),'frame1',17);
 await routed('same frame',wins[0],thrower(wins[0]),'frame0',new wins[0].Error('same frame'));
 await routed('bound callback',wins[0],thrower(wins[1]).bind(null),'frame1',new Error('bound error'));
 window.__fmStrict=null;window.__fmArgc=-1;window.__fmThenReads=0;
 wins[0].queueMicrotask(new wins[1].Function('"use strict";parent.__fmStrict=this===undefined;parent.__fmArgc=arguments.length;return {get then(){parent.__fmThenReads++;throw Error("must not assimilate");}};'));
 await tick();
 check('strict callback this undefined',window.__fmStrict===true);
 check('callback receives no arguments',window.__fmArgc===0,window.__fmArgc);
 check('callback return ignored',window.__fmThenReads===0,window.__fmThenReads);
 const fifo=[];queueMicrotask(()=>{fifo.push('a');queueMicrotask(()=>fifo.push('c'));});Promise.resolve().then(()=>fifo.push('b'));await tick();
 check('microtask FIFO preserved',fifo.join(',')==='a,b,c',fifo);
 const old=wins[1].onerror,during=[];
 wins[1].onerror=function(){during.push('handler');queueMicrotask(()=>during.push('from-handler'));return true;};
 window.__fmExpected=new Error('reentrant reporting');wins[0].queueMicrotask(thrower(wins[1]));Promise.resolve().then(()=>during.push('tail'));await tick();wins[1].onerror=old;
 check('report does not recursively settle',during.join(',')==='handler,tail,from-handler',during);
 for(const value of [undefined,null,0,'callback',{}]){let e=null;try{queueMicrotask(value);}catch(x){e=x;}check('invalid callback '+String(value)+' throws synchronously',e instanceof TypeError);}
 let reached=0;check('queueMicrotask returns undefined',queueMicrotask(()=>{reached++;})===undefined);await tick();
 check('queue remains live after reports',reached===1,reached);
 check('callback exceptions are not promise rejections',rejectionCount===0,rejectionCount);
 diagnostics.loadCount=loadCount;diagnostics.rejectionCount=rejectionCount;
})().catch(e=>check('fixture completed',false,String(e&&e.stack||e))).finally(()=>{
 const passed=results.filter(r=>r.ok).length;console.log('FRAME_MICROTASK_RESULTS '+JSON.stringify({total:results.length,passed,failed:results.length-passed,diagnostics}));document.documentElement.setAttribute('data-frame-microtask-done','1');
});
</script>'''
EXPECTED=35

def main():
 p=argparse.ArgumentParser();p.add_argument('--binary',required=True,type=Path);args=p.parse_args();binary=args.binary.resolve()
 if not binary.is_file():p.error('binary does not exist')
 class Handler(http.server.BaseHTTPRequestHandler):
  def do_GET(self):
   data=HTML.encode();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
  def log_message(self,*_):pass
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 try:
  with tempfile.TemporaryDirectory(prefix='lume-frame-microtask-') as profile:
   env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-frame-microtask-done')
   result=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'7000'],capture_output=True,text=True,env=env,timeout=12)
 except subprocess.TimeoutExpired as exc:
  print('FRAME_MICROTASK_INFRA '+json.dumps({'error':'timeout','seconds':exc.timeout}));return 2
 finally:
  server.shutdown();server.server_close();thread.join(timeout=2)
 print(result.stdout,end='');print(result.stderr,end='');rows=[];summaries=[]
 for line in (result.stdout+'\n'+result.stderr).splitlines():
  for marker,target in [('FRAME_MICROTASK_CHECK ',rows),('FRAME_MICROTASK_RESULTS ',summaries)]:
   at=line.find(marker)
   if at>=0:
    try:target.append(json.loads(line[at+len(marker):]))
    except json.JSONDecodeError:print('FRAME_MICROTASK_INFRA malformed JSON');return 2
 if result.returncode!=0 or len(rows)!=EXPECTED or len(summaries)!=1:
  print('FRAME_MICROTASK_INFRA '+json.dumps({'exit':result.returncode,'checks':len(rows),'summaries':len(summaries)}));return 2
 if [r.get('i') for r in rows]!=list(range(EXPECTED)):print('FRAME_MICROTASK_INFRA duplicate or missing check index');return 2
 passed=sum(r.get('ok') is True for r in rows);summary=summaries[0]
 if summary.get('total')!=EXPECTED or summary.get('passed')!=passed or summary.get('failed')!=EXPECTED-passed:print('FRAME_MICROTASK_INFRA inconsistent summary');return 2
 print('FRAME_MICROTASK_RECEIPT '+json.dumps({'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(HTML.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
 return 0 if passed==EXPECTED else 1
if __name__=='__main__':raise SystemExit(main())
