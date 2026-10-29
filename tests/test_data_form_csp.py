#!/usr/bin/env python3
"""Twenty-one real-browser data form navigation inherited-policy checks; separate from upstream WPT.

The native/Chromium process transport and1Hz sampler are reused from the frozen
Blob Range fixture. Only this local completion marker changes; no browser API,
realm or document object is mocked.

Policy-list self-origin follows CSP3 WD2026-10-27 section2.2 and actual
Chrome156: https://www.w3.org/TR/2026/WD-CSP3-20261027/#framework-policy.
The initial14-case per-policy-origin assumption is archived as a rejected
oracle; the explicit restrictive meta scenario proves meta enforcement.
"""
import argparse
from datetime import datetime,timezone
import hashlib,http.server,importlib.util,json
from pathlib import Path
import threading,time

ROOT=Path(__file__).resolve().parents[1]
MARKER='DATA_POLICY_RESULTS '
CASE_MARKER='DATA_POLICY_CASE '
SCRIPT=r'''
const own=location.origin,other='http://127.0.0.1:'+OTHER_PORT;
const rows=[];
const checks=['form data child completed','child has opaque origin','parent document access denied','original-origin script follows policy','original-origin fetch follows policy','other-origin script blocked','other-origin fetch blocked'];
async function runCase(mode){
 const frame=document.createElement('iframe');frame.name='data-policy-target-'+mode;document.body.appendChild(frame);
 const result=await new Promise(resolve=>{
  let finished=false;
  function finish(data,origin){if(finished)return;finished=true;clearTimeout(timer);removeEventListener('message',onMessage);resolve({data,origin});}
  function onMessage(event){if(event.source===frame.contentWindow&&event.data?.kind==='data-policy')finish(event.data,event.origin)}
  addEventListener('message',onMessage);const timer=setTimeout(()=>finish(null,'timeout'),7000);
  const body=`(async()=>{let parentDenied=false,ownFetch=false,foreignFetchDenied=false,foreignScriptDenied=false;try{void parent.document}catch(e){parentDenied=e.name==='SecurityError'};try{ownFetch=(await(await fetch(${JSON.stringify(own+'/data')})).text())==='ok'}catch(e){};try{await fetch(${JSON.stringify(other+'/data')})}catch(e){foreignFetchDenied=true};foreignScriptDenied=await new Promise(resolve=>{const element=document.createElement('script');element.src=${JSON.stringify(other+'/foreign.js')};element.onload=()=>resolve(false);element.onerror=()=>resolve(true);document.head.appendChild(element);setTimeout(()=>resolve(false),1500)});parent.postMessage({kind:'data-policy',parentDenied,ownScript:globalThis.ownScriptLoaded===true,ownFetch,foreignFetchDenied,foreignScriptDenied},'*')})()`;
  const policy=mode===1?"script-src &apos;self&apos; &apos;nonce-data-policy-probe&apos;; connect-src &apos;self&apos;":"script-src &apos;nonce-data-policy-probe&apos;; connect-src &apos;none&apos;";
  const meta=mode?'<meta http-equiv="Content-Security-Policy" content="'+policy+'">':'';
  const child='<!doctype html>'+meta+'<body><script src="'+own+'/self.js"></'+'script><script nonce="data-policy-probe">'+body+'</'+'script>';
  const form=document.createElement('form');form.action='data:text/html,'+encodeURIComponent(child);form.target=frame.name;document.body.appendChild(form);form.submit();
 });
 const d=result.data,allowOwn=mode!==2;
 const okay=[!!d,result.origin==='null',!!d?.parentDenied,!!d&&d.ownScript===allowOwn,!!d&&d.ownFetch===allowOwn,!!d?.foreignScriptDenied,!!d?.foreignFetchDenied];
 const label=['inherited source self','child meta uses CSP-list self','child restrictive meta intersection'][mode];
 for(let i=0;i<checks.length;i++)rows.push({index:rows.length,name:label+' / '+checks[i],ok:okay[i],error:d?.error||null});
 frame.remove();
}
(async()=>{await runCase(0);await runCase(1);await runCase(2);for(const row of rows)console.log('DATA_POLICY_CASE '+JSON.stringify(row));console.log('DATA_POLICY_RESULTS '+JSON.stringify({count:rows.length}));})();
'''
PAGE_TEMPLATE='<!doctype html><meta charset="utf-8"><title>Data navigation inherited CSP</title><body><script nonce="data-policy-probe">'+SCRIPT+'</script>'
PAGE=PAGE_TEMPLATE.encode()
NAMES=[label+' / '+name for label in ['inherited source self','child meta uses CSP-list self','child restrictive meta intersection']for name in ['form data child completed','child has opaque origin','parent document access denied','original-origin script follows policy','original-origin fetch follows policy','other-origin script blocked','other-origin fetch blocked']]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
utc=lambda:datetime.now(timezone.utc).isoformat()

class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  route=self.path.split('?',1)[0]
  if route=='/':body=PAGE_TEMPLATE.replace('OTHER_PORT',str(self.server.other_port)).encode();kind='text/html; charset=utf-8'
  elif route=='/self.js':body=b'globalThis.ownScriptLoaded=true;';kind='application/javascript'
  elif route=='/foreign.js':body=b'globalThis.foreignScriptLoaded=true;';kind='application/javascript'
  elif route=='/data':body=b'ok';kind='text/plain'
  else:self.send_error(404);return
  self.send_response(200);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(body)));self.send_header('Access-Control-Allow-Origin','*')
  if route=='/':self.send_header('Content-Security-Policy',"script-src 'self' 'nonce-data-policy-probe'; connect-src 'self'; frame-src data:; form-action data:")
  self.end_headers();self.wfile.write(body)
 def log_message(self,*_):pass

def main():
 parser=argparse.ArgumentParser(description=__doc__);choice=parser.add_mutually_exclusive_group(required=True);choice.add_argument('--binary',type=Path);choice.add_argument('--chromium',type=Path);parser.add_argument('--runtime-environment',type=Path);parser.add_argument('--source-manifest',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
 if args.output.exists():raise ValueError('Preserve prior receipt')
 args.output.parent.mkdir(parents=True,exist_ok=True)
 path=ROOT/'tests/test_blob_range_whitespace.py';spec=importlib.util.spec_from_file_location('realm_fixture_transport',path);transport=importlib.util.module_from_spec(spec);spec.loader.exec_module(transport);transport.MARKER=MARKER
 common=transport.common_module();error=common.monitor_module().pid_namespace_error()
 if error:raise RuntimeError(error)
 binary=(args.binary or args.chromium).resolve(strict=True);server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);other=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);server.other_port=other.server_port;other.other_port=server.server_port;thread=threading.Thread(target=server.serve_forever,daemon=True);other_thread=threading.Thread(target=other.serve_forever,daemon=True);thread.start();other_thread.start();began=time.monotonic()
 record={'kind':'actual form data child policy self-origin inheritance, opaque principal and cross-origin denial','officialWPT':False,'nativeLumenExecuted':bool(args.binary),'chromiumControlExecuted':bool(args.chromium),'binary':str(binary),'binarySHA256':sha(binary),'fixtureSHA256':sha(__file__),'transportSHA256':sha(path),'htmlSHA256':hashlib.sha256(PAGE).hexdigest(),'startedUTC':utc(),'rows':[],'infrastructureErrors':[]}
 if args.source_manifest:record['sourceManifestSHA256']=sha(args.source_manifest)
 try:
  url='http://127.0.0.1:'+str(server.server_port)+'/'
  runtime=json.loads(args.runtime_environment.read_text())if args.runtime_environment else{}
  result=transport.run_native(common,binary,url,args.output.with_suffix(''),runtime)if args.binary else transport.run_chromium(common,binary,url,args.output.with_suffix(''))
  for name in['metrics','cleanup','naturalExitCode','browserVersion']:
   if name in result:record[name]=result[name]
  summaries=[json.loads(line.split(MARKER,1)[1])for line in result['stdout'].splitlines()if MARKER in line]
  rows=[json.loads(line.split(CASE_MARKER,1)[1])for line in result['stdout'].splitlines()if CASE_MARKER in line];record['rows']=rows
  if len(summaries)!=1 or summaries[0].get('count')!=len(NAMES) or len(rows)!=len(NAMES) or [r['index']for r in rows]!=list(range(len(NAMES)))or[r['name']for r in rows]!=NAMES:raise RuntimeError('Fixture assertion inventory differs or runtime did not complete')
  if record.get('metrics',{}).get('errors')or record.get('cleanup',{}).get('error')or record.get('cleanup',{}).get('errors'):raise RuntimeError('Unqualified measurement or cleanup')
  if sha(binary)!=record['binarySHA256']or sha(path)!=record['transportSHA256']:raise RuntimeError('Fixture execution identity changed')
 except Exception as error:record['infrastructureErrors'].append(type(error).__name__+': '+str(error))
 finally:server.shutdown();server.server_close();thread.join(timeout=5);other.shutdown();other.server_close();other_thread.join(timeout=5)
 record.update(finishedUTC=utc(),durationSeconds=time.monotonic()-began,total=len(NAMES),recorded=len(record['rows']),passed=sum(r['ok']for r in record['rows']),failed=sum(not r['ok']for r in record['rows']),unrun=len(NAMES)-len(record['rows']))
 args.output.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({k:record[k]for k in['kind','binarySHA256','total','recorded','passed','failed','unrun','infrastructureErrors','durationSeconds']}));raise SystemExit(0 if record['passed']==len(NAMES) and not record['infrastructureErrors']else 1)
if __name__=='__main__':main()
