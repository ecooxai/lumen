#!/usr/bin/env python3
"""Independent public-API SharedWorker regression; not official WPT.

The same unmodified page/worker bytes run in Lumen jsrun and Chromium. No API
is injected or substituted. Run through tools/monitor_process.py for 1Hz data.
Normative oracles (HTML Standard, inspected 2026-10-29):
https://html.spec.whatwg.org/multipage/workers.html#dom-sharedworker
https://html.spec.whatwg.org/multipage/web-messaging.html#message-ports
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
    'SharedWorker exposes its own public interface and stable MessagePort',
    'connect carries a trusted source port and empty data',
    'worker has a genuine SharedWorkerGlobalScope and no document',
    'same URL and string or dictionary name share one global',
    'different names isolate globals',
    'different URLs with the same name isolate globals',
    'two clients receive only their own replies',
    'addEventListener does not start a port and start drains queued messages',
    'onmessage assignment starts a port automatically',
    'ArrayBuffer round trip transfers and detaches in both directions',
    'duplicate buffer transfer throws without detaching or breaking the port',
    'structured clone preserves cycles and typed arrays',
    'closing one client leaves the other client usable',
    'same-origin child frame joins the existing shared global',
    'removing a child owner does not terminate the remaining owner',
    'cross-origin pages do not share same-named worker globals',
    'matching URL and name with different type raises an asynchronous error',
    'matching URL and name with different credentials raises an asynchronous error',
    'invalid constructor enums and missing script argument throw TypeError',
    'failed script loading raises an asynchronous error',
    'module SharedWorker executes an imported module in its own global',
    'worker close permits a new independent global with the same identity',
]

WORKER = r'''
'use strict';
let connectionCount=0;
const instanceToken=Date.now().toString(36)+Math.random().toString(36);
self.addEventListener('connect', event=>{
  const port=event.ports[0], connection=++connectionCount;
  const meta={kind:'ready',connection,instanceToken,name:self.name,
    sourceIsPort:event.source===port,portCount:event.ports.length,
    connectData:event.data,connectOrigin:event.origin,trusted:event.isTrusted,
    shared:typeof SharedWorkerGlobalScope==='function'&&self instanceof SharedWorkerGlobalScope,
    worker:typeof WorkerGlobalScope==='function'&&self instanceof WorkerGlobalScope,
    dedicated:typeof DedicatedWorkerGlobalScope==='function'&&self instanceof DedicatedWorkerGlobalScope,
    documentType:typeof document,windowType:typeof window,globalPostMessage:typeof self.postMessage,
    moduleValue:self.moduleValue||null};
  port.postMessage(meta);
  port.onmessage=message=>{
    const data=message.data;
    if(data.command==='echo')port.postMessage({id:data.id,value:data.value,connection,instanceToken});
    else if(data.command==='buffer'){
      port.postMessage({id:data.id,phase:'buffer',buffer:data.buffer},[data.buffer]);
      port.postMessage({id:data.id,phase:'detached',byteLength:data.buffer.byteLength});
    }else if(data.command==='clone')port.postMessage({id:data.id,value:data.value});
    else if(data.command==='close'){
      port.postMessage({id:data.id,closing:true});
      self.close();
      setTimeout(()=>port.postMessage({kind:'unexpected-after-close'}),0);
    }
  };
});
'''

PAGE = r'''<!doctype html><meta charset=utf-8><title>SharedWorker independent regression</title>
<body><script>
'use strict';
const rows=[],clients=[],frames=[],crossOrigin=__CROSS_ORIGIN__;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const equal=(actual,expected)=>{if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));};
const assert=(value,message)=>{if(!value)throw new Error(message);};
let sequence=0;
async function until(predicate,description,ms=1600){const start=Date.now();while(!predicate()){if(Date.now()-start>ms)throw new Error('timeout: '+description);await delay(5);}}
function connect(name,url='/worker.js',options){
  const worker=new SharedWorker(url,options===undefined?name:options),events=[],errors=[];
  const client={worker,port:worker.port,events,errors,name};clients.push(client);
  worker.onerror=e=>{errors.push(e.message||e.type);if(e.preventDefault)e.preventDefault();};
  worker.port.onmessage=e=>events.push(e.data);
  return client;
}
async function ready(client){await until(()=>client.events.some(e=>e.kind==='ready')||client.errors.length,'worker ready');if(client.errors.length)throw new Error('worker error: '+client.errors.join(';'));return client.events.find(e=>e.kind==='ready');}
async function request(client,command,value,transfer){const id=++sequence;client.port.postMessage(Object.assign({id,command},value||{}),transfer||[]);await until(()=>client.events.some(e=>e.id===id),'reply '+command);return client.events.find(e=>e.id===id);}
async function check(name,callback){try{await callback();rows.push({i:rows.length,name,ok:true});}catch(error){rows.push({i:rows.length,name,ok:false,error:error.name+': '+error.message});}console.log('SHARED_WORKER_CHECK '+JSON.stringify(rows[rows.length-1]));}
async function asynchronousError(client){await until(()=>client.errors.length,'asynchronous worker error');equal(client.events.length,0);}
async function child(name,origin){
  const frame=document.createElement('iframe'), token='frame-'+(++sequence),events=[];
  const listener=e=>{if(e.data&&e.data.token===token&&e.source===frame.contentWindow)events.push(e.data);};
  window.addEventListener('message',listener);frames.push({frame,listener});
  frame.src=origin+'/frame.html?name='+encodeURIComponent(name)+'&token='+encodeURIComponent(token)+'&parent='+encodeURIComponent(location.origin);
  document.body.appendChild(frame);
  await until(()=>events.some(e=>e.kind==='frame-ready'||e.kind==='frame-error'),'child frame ready',2400);
  const result=events.find(e=>e.kind==='frame-ready'||e.kind==='frame-error');
  if(result.kind==='frame-error')throw new Error(result.error);
  return {frame,events,meta:result.meta};
}
async function main(){
  let first,second,firstMeta,secondMeta,frameClient;
  await check('SharedWorker exposes its own public interface and stable MessagePort',async()=>{
    assert(typeof SharedWorker==='function','SharedWorker is unavailable');first=connect('shared-main');firstMeta=await ready(first);
    assert(first.worker instanceof SharedWorker,'wrong SharedWorker brand');assert(first.worker instanceof EventTarget,'not EventTarget');
    assert(first.port instanceof MessagePort,'port is not MessagePort');equal(first.worker.port,first.port);
    equal(typeof first.worker.postMessage,'undefined');equal(typeof first.worker.terminate,'undefined');
  });
  await check('connect carries a trusted source port and empty data',async()=>{
    const c=connect('connect-shape'),m=await ready(c);equal(m.sourceIsPort,true);equal(m.portCount,1);equal(m.connectData,'');equal(m.connectOrigin,'');equal(m.trusted,true);
  });
  await check('worker has a genuine SharedWorkerGlobalScope and no document',async()=>{
    const c=connect('scope-shape'),m=await ready(c);equal(m.shared,true);equal(m.worker,true);equal(m.dedicated,false);equal(m.documentType,'undefined');equal(m.windowType,'undefined');equal(m.globalPostMessage,'undefined');
  });
  await check('same URL and string or dictionary name share one global',async()=>{
    if(!first){first=connect('shared-main');firstMeta=await ready(first);}second=connect('shared-main','/worker.js',{name:'shared-main'});secondMeta=await ready(second);
    equal(firstMeta.connection,1);equal(secondMeta.connection,2);equal(firstMeta.instanceToken,secondMeta.instanceToken);equal(secondMeta.name,'shared-main');
  });
  await check('different names isolate globals',async()=>{
    const a=connect('isolated-a'),b=connect('isolated-b');const [x,y]=await Promise.all([ready(a),ready(b)]);equal(x.connection,1);equal(y.connection,1);assert(x.instanceToken!==y.instanceToken,'different names reused one global');
  });
  await check('different URLs with the same name isolate globals',async()=>{
    const a=connect('same-name-two-urls','/worker.js?first'),b=connect('same-name-two-urls','/worker.js?second');const[x,y]=await Promise.all([ready(a),ready(b)]);equal(x.connection,1);equal(y.connection,1);assert(x.instanceToken!==y.instanceToken,'different URLs reused one global');
  });
  await check('two clients receive only their own replies',async()=>{
    const a=connect('routing'),b=connect('routing');await Promise.all([ready(a),ready(b)]);
    a.port.postMessage({id:777,command:'echo',value:'for-a'});b.port.postMessage({id:777,command:'echo',value:'for-b'});
    await until(()=>a.events.some(e=>e.id===777)&&b.events.some(e=>e.id===777),'independent routes');
    equal(a.events.filter(e=>e.id===777).length,1);equal(b.events.filter(e=>e.id===777).length,1);
    equal(a.events.find(e=>e.id===777).value,'for-a');equal(b.events.find(e=>e.id===777).value,'for-b');
  });
  await check('addEventListener does not start a port and start drains queued messages',async()=>{
    const guard=connect('queue-control');await ready(guard);const worker=new SharedWorker('/worker.js','queue-control'),events=[];clients.push({worker,port:worker.port});
    worker.port.addEventListener('message',e=>events.push(e.data));worker.port.postMessage({id:778,command:'echo',value:'queued'});
    await request(guard,'echo',{value:'guard'});await delay(100);equal(events.length,0);
    worker.port.start();await until(()=>events.some(e=>e.id===778),'queued reply after start');equal(events[0].kind,'ready');equal(events.find(e=>e.id===778).value,'queued');
  });
  await check('onmessage assignment starts a port automatically',async()=>{
    const c=connect('auto-start');await ready(c);equal((await request(c,'echo',{value:'automatic'})).value,'automatic');
  });
  await check('ArrayBuffer round trip transfers and detaches in both directions',async()=>{
    const c=connect('buffer-transfer');await ready(c);const bytes=new Uint8Array([7,11,23,41]),buffer=bytes.buffer,id=++sequence;
    c.port.postMessage({id,command:'buffer',buffer},[buffer]);equal(buffer.byteLength,0);
    await until(()=>c.events.filter(e=>e.id===id).length===2,'both buffer receipts');const answer=c.events.find(e=>e.id===id&&e.phase==='buffer');
    equal(Array.from(new Uint8Array(answer.buffer)).join(','),'7,11,23,41');equal(c.events.find(e=>e.id===id&&e.phase==='detached').byteLength,0);
  });
  await check('duplicate buffer transfer throws without detaching or breaking the port',async()=>{
    const c=connect('duplicate-transfer');await ready(c);const buffer=new ArrayBuffer(4);let error;try{c.port.postMessage({buffer},[buffer,buffer]);}catch(e){error=e;}
    assert(error,'duplicate transfer did not throw');equal(error.name,'DataCloneError');equal(buffer.byteLength,4);equal((await request(c,'echo',{value:'survives'})).value,'survives');
  });
  await check('structured clone preserves cycles and typed arrays',async()=>{
    const c=connect('clone');await ready(c);const value={typed:new Uint16Array([256,1025])};value.self=value;
    const reply=await request(c,'clone',{value});equal(reply.value.self,reply.value);assert(reply.value.typed instanceof Uint16Array,'typed array brand changed');equal(reply.value.typed[1],1025);
  });
  await check('closing one client leaves the other client usable',async()=>{
    const a=connect('close-one'),b=connect('close-one');await Promise.all([ready(a),ready(b)]);a.port.close();equal((await request(b,'echo',{value:'alive'})).value,'alive');
  });
  await check('same-origin child frame joins the existing shared global',async()=>{
    if(!first){first=connect('shared-main');firstMeta=await ready(first);}frameClient=await child('shared-main',location.origin);equal(frameClient.meta.instanceToken,firstMeta.instanceToken);assert(frameClient.meta.connection>firstMeta.connection,'child failed to join the existing global');
  });
  await check('removing a child owner does not terminate the remaining owner',async()=>{
    const a=connect('owner-lifetime');await ready(a);const nested=await child('owner-lifetime',location.origin);nested.frame.remove();await delay(50);equal((await request(a,'echo',{value:'after-child-removal'})).value,'after-child-removal');
  });
  await check('cross-origin pages do not share same-named worker globals',async()=>{
    const a=connect('origin-isolation'),m=await ready(a),nested=await child('origin-isolation',crossOrigin);equal(nested.meta.connection,1);assert(nested.meta.instanceToken!==m.instanceToken,'cross-origin shared worker collision');
  });
  await check('matching URL and name with different type raises an asynchronous error',async()=>{
    const a=connect('type-mismatch');await ready(a);const b=connect('type-mismatch','/worker.js',{name:'type-mismatch',type:'module'});await asynchronousError(b);equal((await request(a,'echo',{value:'original'})).value,'original');
  });
  await check('matching URL and name with different credentials raises an asynchronous error',async()=>{
    const a=connect('credentials-mismatch');await ready(a);const b=connect('credentials-mismatch','/worker.js',{name:'credentials-mismatch',credentials:'omit'});await asynchronousError(b);equal((await request(a,'echo',{value:'original'})).value,'original');
  });
  await check('invalid constructor enums and missing script argument throw TypeError',async()=>{
    for(const callback of [()=>new SharedWorker(),()=>new SharedWorker('/worker.js',{type:'bad'}),()=>new SharedWorker('/worker.js',{credentials:'bad'})]){let error;try{callback();}catch(e){error=e;}assert(error,'invalid constructor input did not throw');equal(error.name,'TypeError');}
  });
  await check('failed script loading raises an asynchronous error',async()=>{const c=connect('missing-script','/missing.js');await asynchronousError(c);});
  await check('module SharedWorker executes an imported module in its own global',async()=>{const c=connect('module-worker','/module-worker.js',{name:'module-worker',type:'module'}),m=await ready(c);equal(m.moduleValue,'imported-module');equal(m.shared,true);equal((await request(c,'echo',{value:'module'})).value,'module');});
  await check('worker close permits a new independent global with the same identity',async()=>{
    const a=connect('close-global'),m=await ready(a);await request(a,'close');await delay(50);const b=connect('close-global'),n=await ready(b);equal(n.connection,1);assert(n.instanceToken!==m.instanceToken,'closed global reused');equal(a.events.filter(e=>e.kind==='unexpected-after-close').length,0);
  });
  for(const client of clients)try{client.port.close();}catch(_){}
  for(const item of frames){window.removeEventListener('message',item.listener);item.frame.remove();}
  const passed=rows.filter(row=>row.ok).length;console.log('SHARED_WORKER_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));
  document.documentElement.setAttribute('data-shared-worker-done','1');
}
main().catch(error=>{console.log('SHARED_WORKER_INFRA '+JSON.stringify({error:error.name+': '+error.message}));document.documentElement.setAttribute('data-shared-worker-done','1');});
</script>'''

FRAME = r'''<!doctype html><meta charset=utf-8><script>
'use strict';
const parameters=new URLSearchParams(location.search),token=parameters.get('token'),target=parameters.get('parent');
try{const worker=new SharedWorker('/worker.js',parameters.get('name'));self.keepWorker=worker;
worker.port.onmessage=e=>{if(e.data.kind==='ready')parent.postMessage({kind:'frame-ready',token,meta:e.data},target);};
worker.onerror=e=>{parent.postMessage({kind:'frame-error',token,error:e.message||e.type},target);e.preventDefault();};
}catch(e){parent.postMessage({kind:'frame-error',token,error:e.name+': '+e.message},target);}
</script>'''


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        route = urllib.parse.urlsplit(self.path).path
        body, mime = None, 'text/javascript; charset=utf-8'
        if route == '/':
            body, mime = self.server.page, 'text/html; charset=utf-8'
        elif route == '/worker.js':
            body = WORKER.encode()
        elif route == '/frame.html':
            body, mime = FRAME.encode(), 'text/html; charset=utf-8'
        elif route == '/module-worker.js':
            body = ("import {value} from './module-value.js';self.moduleValue=value;\n" + WORKER).encode()
        elif route == '/module-value.js':
            body = b"export const value='imported-module';"
        if body is None:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Connection', 'close')
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser()
    engine = parser.add_mutually_exclusive_group(required=True)
    engine.add_argument('--binary', type=Path, help='actual Lumen jsrun executable')
    engine.add_argument('--chromium', type=Path, help='actual Chromium control')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binary = (args.binary or args.chromium).resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error('runner must be an actual executable; no browser mocks')
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
        with tempfile.TemporaryDirectory(prefix='lumen-shared-worker-') as profile:
            if args.chromium:
                stdout, stderr, code, version = chrome_control(binary, origins[0] + '/', profile,
                    completion_marker='SHARED_WORKER_RESULTS ', infrastructure_marker='SHARED_WORKER_INFRA ')
            else:
                env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
                env.update(LUMEN_PROFILE_DIR=profile, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-shared-worker-done')
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
        for marker, destination in [('SHARED_WORKER_CHECK ', rows), ('SHARED_WORKER_RESULTS ', summaries), ('SHARED_WORKER_INFRA ', infrastructure)]:
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
    receipt = {'kind': 'shared-worker-public-api-regression', 'officialWPT': False,
        'nativeLumenExecuted': args.binary is not None and code is not None,
        'chromiumControlExecuted': args.chromium is not None and code is not None,
        'binary': str(binary), 'binarySHA256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'browserVersion': version, 'fixtureSHA256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'pageTemplateSHA256': hashlib.sha256(PAGE.encode()).hexdigest(),
        'workerSHA256': hashlib.sha256(WORKER.encode()).hexdigest(),
        'caseManifestSHA256': hashlib.sha256(json.dumps(EXPECTED_NAMES).encode()).hexdigest(),
        'started': started, 'ended': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'durationSeconds': round(time.monotonic() - start, 6), **summary, 'rows': rows,
        'infrastructureErrors': infrastructure, 'monitor': 'External 1Hz monitor required',
        'stdoutSHA256': hashlib.sha256(stdout.encode()).hexdigest(),
        'stderrSHA256': hashlib.sha256(stderr.encode()).hexdigest()}
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
