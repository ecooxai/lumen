#!/usr/bin/env python3
"""Public SharedWorker policy and transferable-port boundaries; not upstream WPT.

Run the identical fixture in Lumen jsrun or the qualified Chromium reference.
No implementation helpers, browser globals, or policies are replaced.
"""
import argparse
import datetime
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import urllib.parse

from test_worker_response_headers import chrome_control


EXPECTED_NAMES = [
    'worker-src self permits a second document to join the same worker',
    'worker-src none rejects a fresh worker in a restricted document',
    'worker-src none rejects joining an already-running worker',
    'child-src none fallback rejects joining an already-running worker',
    'explicit worker-src self overrides child-src none for a worker join',
    'native cross-origin script rejection raises SecurityError',
    'malformed SharedWorker script URL raises SyntaxError',
    'setting MessagePort onmessage to null enables its message queue',
    'shared global does not expose DedicatedWorkerGlobalScope',
    'owner transfers a MessageChannel endpoint into SharedWorker',
    'SharedWorker transfers a MessageChannel endpoint to its owner',
    'a SharedWorker connection port can transfer into a dedicated worker',
    'data SharedWorker reports an opaque origin',
]

POLICIES = {
    'allow': "script-src 'self' 'unsafe-inline'; worker-src 'self'",
    'deny': "script-src 'self' 'unsafe-inline'; worker-src 'none'",
    'fallback': "script-src 'self' 'unsafe-inline'; child-src 'none'",
    'override': "script-src 'self' 'unsafe-inline'; child-src 'none'; worker-src 'self'",
}

WORKER = r'''
let connectionCount=0;const token=Date.now().toString(36)+Math.random().toString(36);
onconnect=event=>{
  const port=event.ports[0],connection=++connectionCount;
  port.postMessage({kind:'ready',token,connection,origin:self.origin,dedicatedConstructor:typeof DedicatedWorkerGlobalScope});
  port.onmessage=event=>{
    const data=event.data;
    if(data.command==='echo')port.postMessage({id:data.id,value:data.value});
    else if(data.command==='adopt'){
      const endpoint=event.ports[0];
      endpoint.onmessage=message=>endpoint.postMessage({kind:'port-reply',value:message.data});
      endpoint.postMessage({kind:'port-ready'});
      port.postMessage({id:data.id,portIdentity:endpoint===data.port});
    }else if(data.command==='give'){
      const channel=new MessageChannel();
      channel.port1.onmessage=message=>channel.port1.postMessage({kind:'returned-port-reply',value:message.data});
      port.postMessage({id:data.id,port:channel.port2},[channel.port2]);
    }
  };
};
'''

DEDICATED = r'''
onmessage=event=>{
  const port=event.ports[0];
  port.onmessage=message=>postMessage({kind:'relay',value:message.data});
  port.postMessage({id:90001,command:'echo',value:'through-transferred-connection'});
};
'''

FRAME = r'''<!doctype html><meta charset=utf-8><script>
'use strict';
const params=new URLSearchParams(location.search),tag=params.get('tag'),target=params.get('parent');
let sent=false;function send(result){if(!sent){sent=true;parent.postMessage(Object.assign({tag,kind:'policy-result',constructorType:typeof SharedWorker},result),target);}}
try{const worker=new SharedWorker('/worker.js',params.get('name'));self.keepWorker=worker;
worker.port.onmessage=e=>{if(e.data.kind==='ready')send({disposition:'ready',constructed:true,meta:e.data});};
worker.onerror=e=>{send({disposition:'error',constructed:true,error:e.message||e.type});e.preventDefault();};
}catch(e){send({disposition:'throw',constructed:false,error:e.name});}
</script>'''

PAGE = r'''<!doctype html><meta charset=utf-8><body><script>
'use strict';
const clients=[],frames=[],rows=[],crossOrigin=__CROSS_ORIGIN__;let sequence=0;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function equal(actual,expected){if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));}
function assert(value,message){if(!value)throw new Error(message);}
async function until(predicate,label,ms=1600){const start=Date.now();while(!predicate()){if(Date.now()-start>ms)throw new Error('timeout: '+label);await delay(5);}}
function connect(name,url='/worker.js'){
  const worker=new SharedWorker(url,name),events=[],errors=[],client={worker,port:worker.port,events,errors};clients.push(client);
  worker.port.onmessage=event=>events.push(event);
  worker.onerror=event=>{errors.push(event.message||event.type);event.preventDefault();};return client;
}
async function ready(client){await until(()=>client.events.some(e=>e.data.kind==='ready')||client.errors.length,'worker ready');if(client.errors.length)throw new Error(client.errors.join(';'));return client.events.find(e=>e.data.kind==='ready').data;}
async function request(client,command,value,transfer){const id=++sequence;client.port.postMessage(Object.assign({id,command},value||{}),transfer||[]);await until(()=>client.events.some(e=>e.data.id===id),'worker reply');return client.events.find(e=>e.data.id===id);}
async function policyFrame(name,policy){
  const frame=document.createElement('iframe'),tag='policy-'+(++sequence),messages=[];
  const listener=event=>{if(event.source===frame.contentWindow&&event.data&&event.data.tag===tag)messages.push(event.data);};
  addEventListener('message',listener);frames.push({frame,listener});
  frame.src='/policy-frame.html?policy='+policy+'&name='+encodeURIComponent(name)+'&tag='+tag+'&parent='+encodeURIComponent(location.origin);
  document.body.appendChild(frame);await until(()=>messages.length,'policy frame',2200);equal(messages[0].constructorType,'function');return messages[0];
}
async function check(name,callback){try{await callback();rows.push({i:rows.length,name,ok:true});}catch(error){rows.push({i:rows.length,name,ok:false,error:error.name+': '+error.message});}console.log('SHARED_BOUNDARY_CHECK '+JSON.stringify(rows[rows.length-1]));}
async function main(){
  await check('worker-src self permits a second document to join the same worker',async()=>{const c=connect('policy-positive'),m=await ready(c),r=await policyFrame('policy-positive','allow');equal(r.disposition,'ready');equal(r.meta.token,m.token);equal(r.meta.connection,2);});
  await check('worker-src none rejects a fresh worker in a restricted document',async()=>{assert(typeof SharedWorker==='function','SharedWorker missing');const r=await policyFrame('policy-fresh-deny','deny');equal(r.disposition,'error');equal(r.constructed,true);});
  await check('worker-src none rejects joining an already-running worker',async()=>{const c=connect('policy-join-deny');await ready(c);const r=await policyFrame('policy-join-deny','deny');equal(r.disposition,'error');equal(r.constructed,true);equal((await request(c,'echo',{value:'owner-alive'})).data.value,'owner-alive');});
  await check('child-src none fallback rejects joining an already-running worker',async()=>{const c=connect('policy-fallback');await ready(c);const r=await policyFrame('policy-fallback','fallback');equal(r.disposition,'error');equal(r.constructed,true);});
  await check('explicit worker-src self overrides child-src none for a worker join',async()=>{const c=connect('policy-override'),m=await ready(c),r=await policyFrame('policy-override','override');equal(r.disposition,'ready');equal(r.meta.token,m.token);});
  await check('native cross-origin script rejection raises SecurityError',async()=>{let error;try{new SharedWorker(crossOrigin+'/worker.js','cross-origin-script');}catch(e){error=e;}assert(error,'cross-origin script did not throw');equal(error.name,'SecurityError');});
  await check('malformed SharedWorker script URL raises SyntaxError',async()=>{let error;try{new SharedWorker('http://[broken','malformed-url');}catch(e){error=e;}assert(error,'malformed script URL did not throw');equal(error.name,'SyntaxError');});
  await check('setting MessagePort onmessage to null enables its message queue',async()=>{
    const worker=new SharedWorker('/worker.js','null-handler'),events=[];clients.push({port:worker.port});
    worker.port.addEventListener('message',e=>events.push(e.data));worker.port.onmessage=null;
    await until(()=>events.some(e=>e.kind==='ready'),'onmessage=null queued ready');
  });
  await check('shared global does not expose DedicatedWorkerGlobalScope',async()=>{const c=connect('global-exposure'),m=await ready(c);equal(m.dedicatedConstructor,'undefined');});
  await check('owner transfers a MessageChannel endpoint into SharedWorker',async()=>{
    const c=connect('port-in');await ready(c);const channel=new MessageChannel(),messages=[];
    channel.port1.onmessage=e=>messages.push(e.data);
    const reply=await request(c,'adopt',{port:channel.port2},[channel.port2]);equal(reply.data.portIdentity,true);
    await until(()=>messages.some(e=>e.kind==='port-ready'),'adopted port ready');channel.port1.postMessage('from-owner');
    await until(()=>messages.some(e=>e.kind==='port-reply'),'adopted port reply');equal(messages.find(e=>e.kind==='port-reply').value,'from-owner');channel.port1.close();
  });
  await check('SharedWorker transfers a MessageChannel endpoint to its owner',async()=>{
    const c=connect('port-out');await ready(c);const reply=await request(c,'give'),port=reply.ports[0],messages=[];
    equal(reply.ports.length,1);equal(reply.data.port,port);port.onmessage=e=>messages.push(e.data);port.postMessage('from-returned-port');
    await until(()=>messages.length,'returned port reply');equal(messages[0].kind,'returned-port-reply');equal(messages[0].value,'from-returned-port');port.close();
  });
  await check('a SharedWorker connection port can transfer into a dedicated worker',async()=>{
    const c=connect('connection-transfer');await ready(c);const worker=new Worker('/dedicated.js'),messages=[];
    try{worker.onmessage=e=>messages.push(e.data);worker.postMessage({port:c.port},[c.port]);
      await until(()=>messages.some(e=>e.kind==='relay'&&e.value.id===90001),'transferred connection reply');
      equal(messages.find(e=>e.kind==='relay'&&e.value.id===90001).value.value,'through-transferred-connection');
    }finally{worker.terminate();}
  });
  await check('data SharedWorker reports an opaque origin',async()=>{
    const script="onconnect=e=>e.ports[0].postMessage({kind:'ready',origin:self.origin});";
    const c=connect('opaque-data','data:text/javascript,'+encodeURIComponent(script)),m=await ready(c);equal(m.origin,'null');
  });
  for(const c of clients)try{c.port.close();}catch(_){}
  for(const item of frames){removeEventListener('message',item.listener);item.frame.remove();}
  const passed=rows.filter(row=>row.ok).length;console.log('SHARED_BOUNDARY_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));
  document.documentElement.setAttribute('data-shared-boundaries-done','1');
}
main().catch(error=>{console.log('SHARED_BOUNDARY_INFRA '+JSON.stringify({error:error.name+': '+error.message}));document.documentElement.setAttribute('data-shared-boundaries-done','1');});
</script>'''


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        body, mime, policy = None, 'text/javascript; charset=utf-8', None
        if parsed.path == '/':
            body, mime = self.server.page, 'text/html; charset=utf-8'
        elif parsed.path == '/worker.js':
            body = WORKER.encode()
        elif parsed.path == '/dedicated.js':
            body = DEDICATED.encode()
        elif parsed.path == '/policy-frame.html':
            body, mime = FRAME.encode(), 'text/html; charset=utf-8'
            policy = POLICIES[urllib.parse.parse_qs(parsed.query)['policy'][0]]
        if body is None:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Connection', 'close')
        if policy:
            self.send_header('Content-Security-Policy', policy)
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    engine = parser.add_mutually_exclusive_group(required=True)
    engine.add_argument('--binary', type=Path)
    engine.add_argument('--chromium', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary = (args.binary or args.chromium).resolve()
    servers = [http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) for _ in range(2)]
    origins = ['http://127.0.0.1:' + str(server.server_port) for server in servers]
    page = PAGE.replace('__CROSS_ORIGIN__', json.dumps(origins[1])).encode()
    threads = []
    for server in servers:
        server.page, server.daemon_threads = page, True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        threads.append(thread)
    started, start = datetime.datetime.now(datetime.timezone.utc).isoformat(), time.monotonic()
    stdout = stderr = ''
    code = version = None
    infrastructure = []
    try:
        with tempfile.TemporaryDirectory(prefix='lumen-shared-boundaries-') as profile:
            if args.chromium:
                stdout, stderr, code, version = chrome_control(binary, origins[0] + '/', profile,
                    completion_marker='SHARED_BOUNDARY_RESULTS ', infrastructure_marker='SHARED_BOUNDARY_INFRA ')
            else:
                env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
                env.update(LUMEN_PROFILE_DIR=profile, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-shared-boundaries-done')
                result = subprocess.run([str(binary), origins[0] + '/', '60000'], capture_output=True,
                    text=True, env=env, timeout=65)
                stdout, stderr, code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as error:
        infrastructure.append({'error': 'process timeout', 'seconds': error.timeout})
        stdout = error.stdout.decode(errors='replace') if isinstance(error.stdout, bytes) else error.stdout or ''
        stderr = error.stderr.decode(errors='replace') if isinstance(error.stderr, bytes) else error.stderr or ''
    except Exception as error:
        infrastructure.append({'error': type(error).__name__, 'detail': str(error)})
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=2)
    rows, summaries = [], []
    for line in (stdout + '\n' + stderr).splitlines():
        for marker, destination in [('SHARED_BOUNDARY_CHECK ', rows), ('SHARED_BOUNDARY_RESULTS ', summaries), ('SHARED_BOUNDARY_INFRA ', infrastructure)]:
            index = line.find(marker)
            if index >= 0:
                try:
                    destination.append(json.loads(line[index + len(marker):]))
                except json.JSONDecodeError as error:
                    infrastructure.append({'error': 'invalid JSON', 'marker': marker, 'detail': str(error)})
    if code != 0:
        infrastructure.append({'error': 'nonzero or missing runner exit', 'exit': code})
    if [row.get('i') for row in rows] != list(range(len(EXPECTED_NAMES))) or [row.get('name') for row in rows] != EXPECTED_NAMES:
        infrastructure.append({'error': 'missing, duplicate, changed or reordered cases', 'observed': len(rows), 'expected': len(EXPECTED_NAMES)})
    passed = sum(row.get('ok') is True for row in rows)
    summary = {'total': len(EXPECTED_NAMES), 'passed': passed, 'failed': len(EXPECTED_NAMES) - passed}
    if summaries != [summary]:
        infrastructure.append({'error': 'missing or inconsistent summary', 'summaries': summaries})
    receipt = {'kind': 'shared-worker-policy-port-boundaries', 'officialWPT': False,
        'nativeLumenExecuted': args.binary is not None and code is not None,
        'chromiumControlExecuted': args.chromium is not None and code is not None,
        'binary': str(binary), 'binarySHA256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'browserVersion': version, 'fixtureSHA256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'pageTemplateSHA256': hashlib.sha256(PAGE.encode()).hexdigest(),
        'workerSHA256': hashlib.sha256(WORKER.encode()).hexdigest(),
        'caseManifestSHA256': hashlib.sha256(json.dumps(EXPECTED_NAMES).encode()).hexdigest(),
        'started': started, 'ended': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'durationSeconds': round(time.monotonic() - start, 6), **summary, 'rows': rows,
        'infrastructureErrors': infrastructure, 'monitor': 'External one-second monitor required',
        'stdoutSHA256': hashlib.sha256(stdout.encode()).hexdigest(), 'stderrSHA256': hashlib.sha256(stderr.encode()).hexdigest()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix('.stdout.log').write_text(stdout)
    args.output.with_suffix('.stderr.log').write_text(stderr)
    temporary = args.output.with_suffix(args.output.suffix + '.tmp')
    with temporary.open('w') as stream:
        stream.write(json.dumps(receipt, indent=2) + '\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(args.output)
    print(json.dumps({key: value for key, value in receipt.items() if key != 'rows'}), flush=True)
    return 2 if infrastructure else 0 if passed == len(EXPECTED_NAMES) else 1


if __name__ == '__main__':
    raise SystemExit(main())
