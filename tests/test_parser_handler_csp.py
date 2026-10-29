#!/usr/bin/env python3
"""Independent real-browser parser-handler policy/timing oracle (36 checks).

HTML attribute-change steps check CSP before assigning/activating an event
handler; a blocked update must not revoke an earlier permitted callback:
https://html.spec.whatwg.org/multipage/webappapis.html#event-handler-content-attributes
The exact original nine-case parser/meta fixture remains unchanged elsewhere.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import html
import http.server
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
MARKER = 'PARSER_HANDLER_DONE '
CASE_MARKER = 'PARSER_HANDLER_CASE '

def say(case, value):
    data = json.dumps({'kind':'parser-handler','case':case,'value':value},separators=(',',':'))
    return 'parent.postMessage('+data+',"*");'

def child(case, policy, setup='', body_handler=None, tag='body', nonce=False, extra=''):
    # Emit the diagnostic directly; no proxy, DOM, fetch or event API is mocked.
    ready = 'let denied=false;try{void parent.document}catch(e){denied=e.name==="SecurityError"};parent.postMessage({kind:"parser-handler",case:'+json.dumps(case)+',value:"ready",parentDenied:denied},"*");'
    initial = '<script>'+ready+setup+'</script>'
    meta = '<meta http-equiv="Content-Security-Policy" content="'+html.escape(policy,quote=True)+'">'
    attributes = (' nonce="allowed"' if nonce else '')
    if body_handler is not None:
        attributes += ' onload="'+html.escape(body_handler,quote=True)+'"'
    return '<!doctype html><html><head>'+initial+meta+extra+'</head><'+tag+attributes+'></'+tag+'></html>'

CASES = []
def add(name, policy, expected, *, setup='', tag='body', body=True, nonce=False, extra=''):
    handler = say(name,'handler') if body else None
    CASES.append({'name':name,'html':child(name,policy,setup,handler,tag,nonce,extra),'expected':['ready']+expected})

add('body unsafe-inline positive',"script-src 'unsafe-inline'",['handler'])
name='body nonce does not authorize attribute'
add(name,"script-src 'nonce-allowed'",['nonce-script'],nonce=True,
    extra='<script nonce="allowed">'+say(name,'nonce-script')+'</script>')
name='body unsafe-hashes exact positive'
digest=base64.b64encode(hashlib.sha256(say(name,'handler').encode()).digest()).decode()
add(name,"script-src 'unsafe-hashes' 'sha256-"+digest+"'",['handler'])
name='body hash without unsafe-hashes denied'
digest=base64.b64encode(hashlib.sha256(say(name,'handler').encode()).digest()).decode()
add(name,"script-src 'sha256-"+digest+"'",[])
name='body unsafe-hashes mismatching hash denied'
digest=base64.b64encode(hashlib.sha256(b'wrong-handler').digest()).decode()
add(name,"script-src 'unsafe-hashes' 'sha256-"+digest+"'",[])
for name, body in [('earlier IDL survives blocked parsed attribute',True),('earlier IDL survives body without attribute',False)]:
    setup='addEventListener("load",()=>{'+say(name,'head-listener')+'});window.onload=()=>{'+say(name,'prior-idl')+'};'
    add(name,"script-src 'none'",['head-listener','prior-idl'],setup=setup,body=body)
name='getter compiled content handler survives later policy'
code=say(name,'prior-content')
setup='const detached=document.createElement("body");detached.setAttribute("onload",'+json.dumps(code)+');const saved=detached.onload;if(typeof saved==="function"){'+say(name,'getter-observed')+'}else{'+say(name,'getter-missing')+'}addEventListener("load",()=>{'+say(name,'later-listener')+'});'
add(name,"script-src 'none'",['getter-observed','prior-content','later-listener'],setup=setup,body=False)
name='head listener precedes parsed body handler'
add(name,"script-src 'unsafe-inline'",['head-listener','handler'],setup='addEventListener("load",()=>{'+say(name,'head-listener')+'});')
name='dynamic blocked attribute preserves earlier IDL'
bad=say(name,'forbidden-dynamic')
setup='const detached=document.createElement("body");const prior=()=>{'+say(name,'prior-idl')+'};window.onload=prior;addEventListener("load",()=>{detached.setAttribute("onload",'+json.dumps(bad)+');if(detached.onload===prior){'+say(name,'prior-retained')+'}else{'+say(name,'prior-lost')+'}});'
add(name,"script-src 'none'",['prior-idl','prior-retained'],setup=setup,body=False)
add('frameset restrictive meta denied',"script-src 'none'",[],tag='frameset')
add('frameset unsafe-inline positive',"script-src 'unsafe-inline'",['handler'],tag='frameset')

PAGE = ('<!doctype html><meta charset="utf-8"><title>Parser handler CSP timing</title><body><script>const cases='+json.dumps(CASES,separators=(',',':')).replace('</','<' + chr(92) + '/')+r''';
(async()=>{const rows=[];for(const spec of cases){
 const frame=document.createElement('iframe');const seen=[];let handler;
 const result=await new Promise(resolve=>{let done=false;const finish=loaded=>{if(done)return;done=true;clearTimeout(timer);resolve({loaded,seen:[...seen]})};
  handler=event=>{if(event.source===frame.contentWindow&&event.data&&event.data.kind==='parser-handler'&&event.data.case===spec.name)seen.push({value:event.data.value,origin:event.origin,parentDenied:event.data.parentDenied})};addEventListener('message',handler);
  frame.onload=()=>{try{void frame.contentWindow.document;return}catch(e){if(e.name!=='SecurityError')return}setTimeout(()=>finish(true),50)};
  const timer=setTimeout(()=>finish(false),3500);frame.src='data:text/html,'+encodeURIComponent(spec.html);document.body.appendChild(frame);
 });removeEventListener('message',handler);frame.remove();
 const values=result.seen.map(item=>item.value);const ready=result.seen.find(item=>item.value==='ready');
 const checks=[['data iframe completed',result.loaded],['opaque origin and parent DOM denied',!!ready&&ready.parentDenied===true&&result.seen.every(item=>item.origin==='null')],['exact callback policy and registration order',JSON.stringify(values)===JSON.stringify(spec.expected)]];
 for(const [name,ok]of checks)rows.push({index:rows.length,name:spec.name+' / '+name,ok,observed:values,expected:spec.expected});
 }for(const row of rows)console.log('PARSER_HANDLER_CASE '+JSON.stringify(row));console.log('PARSER_HANDLER_DONE '+JSON.stringify({count:rows.length}));})();
</script>''').encode()
NAMES=[case['name']+' / '+label for case in CASES for label in ['data iframe completed','opaque origin and parent DOM denied','exact callback policy and registration order']]
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != '/':
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type','text/html; charset=utf-8')
        self.send_header('Content-Length',str(len(PAGE)))
        self.send_header('Cache-Control','no-store')
        self.end_headers()
        self.wfile.write(PAGE)
    def log_message(self,*args):
        pass

def run_native(common,binary,url,prefix,runtime,native_gui):
    assert set(runtime) <= {'LUMEN_ICU_DATA','LD_LIBRARY_PATH'}
    environment={key:value for key,value in os.environ.items() if not key.startswith(('JSRUN_','LUMEN_'))}
    environment.update(runtime)
    profile=Path(tempfile.mkdtemp(prefix='owned-parser-handler-',dir=prefix.parent))
    (profile/'downloads').mkdir()
    environment.update(LUMEN_CONSOLE='1',LUMEN_PROFILE_DIR=str(profile),LUMEN_DOWNLOAD_DIR=str(profile/'downloads'))
    process=sampler=None
    metrics=cleanup=None
    text=''
    try:
        with prefix.with_suffix('.stdout.log').open('wb')as log:
            command=[str(binary),url]+([]if native_gui else['65000'])
            process=subprocess.Popen(command,cwd=ROOT,env=environment,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            sampler=common.Sampler(process.pid,prefix.with_suffix('.metrics.jsonl'))
            until=time.monotonic()+60
            while time.monotonic()<until:
                text=prefix.with_suffix('.stdout.log').read_text(errors='replace')
                if MARKER in text or process.poll()is not None:break
                time.sleep(.025)
            natural=process.poll()
    finally:
        if sampler:metrics=sampler.stop()
        if process:cleanup=common.terminate_owned(process,sampler)
        if cleanup and not cleanup.get('error')and cleanup.get('process_group_gone')is True:shutil.rmtree(profile)
    return {'stdout':text,'metrics':metrics,'cleanup':cleanup,'naturalExitCode':natural}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    choice=parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('--binary',type=Path);choice.add_argument('--chromium',type=Path)
    parser.add_argument('--native-gui',action='store_true')
    parser.add_argument('--runtime-environment',type=Path)
    parser.add_argument('--source-manifest',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():raise ValueError('Preserve previous receipt')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    transport_path=ROOT/'tests/test_blob_range_whitespace.py'
    spec=importlib.util.spec_from_file_location('handler_policy_control_transport',transport_path)
    transport=importlib.util.module_from_spec(spec);spec.loader.exec_module(transport);transport.MARKER=MARKER
    common=transport.common_module()
    error=common.monitor_module().pid_namespace_error()
    if error:raise RuntimeError(error)
    binary=(args.binary or args.chromium).resolve(strict=True)
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    began=time.monotonic()
    record={'startedUTC':datetime.now(timezone.utc).isoformat(),'officialWPT':False,
            'scope':'Actual direct data iframe content handlers, CSP positives/negatives, prior callback preservation and parser registration order',
            'binarySHA256':sha(binary),'fixtureSHA256':sha(__file__),'pageSHA256':hashlib.sha256(PAGE).hexdigest(),
            'nativeGui':args.native_gui,'nativeLumenExecuted':bool(args.binary),'chromiumControlExecuted':bool(args.chromium),
            'total':len(NAMES),'rows':[],'infrastructureErrors':[]}
    if args.source_manifest:record['sourceManifestSHA256']=sha(args.source_manifest)
    try:
        url='http://127.0.0.1:'+str(server.server_port)+'/'
        runtime=json.loads(args.runtime_environment.read_text())if args.runtime_environment else{}
        result=run_native(common,binary,url,args.output.with_suffix(''),runtime,args.native_gui)if args.binary else transport.run_chromium(common,binary,url,args.output.with_suffix(''))
        for key in ('metrics','cleanup','naturalExitCode','browserVersion'):
            if key in result:record[key]=result[key]
        rows=[json.loads(line.split(CASE_MARKER,1)[1])for line in result['stdout'].splitlines()if CASE_MARKER in line]
        done=[json.loads(line.split(MARKER,1)[1])for line in result['stdout'].splitlines()if MARKER in line]
        record['rows']=rows
        if len(done)!=1 or done[0].get('count')!=len(NAMES)or len(rows)!=len(NAMES)or[r['index']for r in rows]!=list(range(len(NAMES)))or[r['name']for r in rows]!=NAMES:raise RuntimeError('Frozen assertion inventory incomplete or differs')
        if record.get('metrics',{}).get('errors')or record.get('cleanup',{}).get('error')or record.get('cleanup',{}).get('errors'):raise RuntimeError('Unqualified measurement or owned cleanup')
        if sha(binary)!=record['binarySHA256']or sha(__file__)!=record['fixtureSHA256']:raise RuntimeError('Runtime/fixture identity changed')
    except Exception as error:record['infrastructureErrors'].append(type(error).__name__+': '+str(error))
    finally:server.shutdown();server.server_close();thread.join(timeout=5)
    record.update(finishedUTC=datetime.now(timezone.utc).isoformat(),durationSeconds=round(time.monotonic()-began,3),
                  recorded=len(record['rows']),passed=sum(row.get('ok')is True for row in record['rows']),
                  failed=sum(row.get('ok')is False for row in record['rows']),unrun=len(NAMES)-len(record['rows']))
    args.output.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({key:record[key]for key in ('binarySHA256','fixtureSHA256','total','recorded','passed','failed','unrun','infrastructureErrors','durationSeconds')}),flush=True)
    raise SystemExit(0 if record['passed']==len(NAMES)and not record['infrastructureErrors'] else 1)

if __name__=='__main__':main()
