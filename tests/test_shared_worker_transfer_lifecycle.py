#!/usr/bin/env python3
"""Independent public SharedWorker connection-port ownership oracle.

These additional cases preserve the frozen22 and13 suites. The same fixture
bytes run in native Lumen and qualified Chromium; this is not official WPT.
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
    'duplicate connection-port transfer fails without detaching the sender',
    'uncloneable payload fails without detaching the connection port',
    'transferred connection preserves its shared-global and connection identity',
    'detached sender cannot receive send or transfer the connection again',
    'messages queued before transfer arrive in original FIFO order',
    'terminating the recipient preserves another shared-worker client',
    'closing the transferred endpoint preserves another shared-worker client',
    'a connection endpoint can return and transfer to another worker',
    'an independent channel survives connection transfer and recipient termination',
]

WORKER = r'''
'use strict';
let count=0,badMessages=0;const token=Date.now().toString(36)+Math.random().toString(36);
onconnect=event=>{
  const port=event.ports[0],connection=++count;
  port.postMessage({kind:'ready',connection,token});
  port.postMessage({kind:'queued',number:1});port.postMessage({kind:'queued',number:2});
  port.onmessage=event=>{
    const data=event.data;
    if(data.command==='echo')port.postMessage({id:data.id,kind:'echo',value:data.value,connection,token});
    else if(data.command==='bad')++badMessages;
    else if(data.command==='stats')port.postMessage({id:data.id,badMessages,token});
    else if(data.command==='give'){
      const channel=new MessageChannel();
      channel.port1.onmessage=message=>channel.port1.postMessage({value:message.data,token});
      port.postMessage({id:data.id,port:channel.port2},[channel.port2]);
    }
  };
};
'''

DEDICATED = r'''
'use strict';let endpoint;
onmessage=event=>{
  const data=event.data;
  if(data.command==='adopt'){
    endpoint=event.ports[0];
    endpoint.onmessage=message=>postMessage({kind:'relay',value:message.data});
    postMessage({kind:'adopted',same:endpoint===data.port,isPort:endpoint instanceof MessagePort});
  }else if(data.command==='send')endpoint.postMessage(data.value);
  else if(data.command==='return'){
    endpoint.onmessage=null;postMessage({kind:'returned',port:endpoint},[endpoint]);endpoint=null;
  }else if(data.command==='close'){
    endpoint.close();endpoint=null;postMessage({kind:'closed'});
  }
};
'''

PAGE = r'''<!doctype html><meta charset=utf-8><body><script>
'use strict';
const clients=[],workers=[],rows=[];let sequence=0;
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function equal(actual,expected){if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));}
function assert(value,message){if(!value)throw new Error(message);}
async function until(predicate,label,ms=1900){const start=Date.now();while(!predicate()){if(Date.now()-start>ms)throw new Error('timeout: '+label);await delay(5);}}
function connect(name,start=true){
  const worker=new SharedWorker('/worker.js',name),events=[],errors=[],client={worker,port:worker.port,events,errors};
  clients.push(client);if(start)client.port.onmessage=event=>events.push(event);
  worker.onerror=event=>{errors.push(event.message||event.type);event.preventDefault();};return client;
}
async function ready(client){await until(()=>client.events.some(e=>e.data.kind==='ready')||client.errors.length,'shared worker ready');if(client.errors.length)throw new Error(client.errors.join(';'));return client.events.find(e=>e.data.kind==='ready').data;}
async function request(client,command,value,transfer){const id=++sequence;client.port.postMessage(Object.assign({id,command},value||{}),transfer||[]);await until(()=>client.events.some(e=>e.data.id===id),'shared worker reply');return client.events.find(e=>e.data.id===id);}
function dedicated(){const worker=new Worker('/dedicated.js'),events=[],errors=[],value={worker,events,errors};workers.push(value);worker.onmessage=event=>events.push(event);worker.onerror=event=>{errors.push(event.message||event.type);event.preventDefault();};return value;}
async function adopt(target,port){target.worker.postMessage({command:'adopt',port},[port]);await until(()=>target.events.some(e=>e.data.kind==='adopted')||target.errors.length,'recipient adoption');if(target.errors.length)throw new Error(target.errors.join(';'));const result=target.events.find(e=>e.data.kind==='adopted').data;equal(result.same,true);equal(result.isPort,true);}
async function remoteEcho(target,value){const id=++sequence;target.worker.postMessage({command:'send',value:{command:'echo',id,value}});await until(()=>target.events.some(e=>e.data.kind==='relay'&&e.data.value.id===id),'transferred connection echo');return target.events.find(e=>e.data.kind==='relay'&&e.data.value.id===id).data.value;}
function cloneError(callback){let error;try{callback();}catch(caught){error=caught;}assert(error,'transfer must throw');equal(error.name,'DataCloneError');}
async function check(name,callback){try{await callback();rows.push({i:rows.length,name,ok:true});}catch(error){rows.push({i:rows.length,name,ok:false,error:error.name+': '+error.message});}console.log('SHARED_TRANSFER_CHECK '+JSON.stringify(rows[rows.length-1]));}
async function main(){
  await check('duplicate connection-port transfer fails without detaching the sender',async()=>{
    const source=connect('transfer-duplicate'),target=dedicated();await ready(source);
    cloneError(()=>target.worker.postMessage({command:'adopt',port:source.port},[source.port,source.port]));
    equal((await request(source,'echo',{value:'still-entangled'})).data.value,'still-entangled');
  });
  await check('uncloneable payload fails without detaching the connection port',async()=>{
    const source=connect('transfer-uncloneable'),target=dedicated();await ready(source);
    cloneError(()=>target.worker.postMessage({command:'adopt',port:source.port,notCloneable:function(){}},[source.port]));
    equal((await request(source,'echo',{value:'clone-rollback'})).data.value,'clone-rollback');
  });
  await check('transferred connection preserves its shared-global and connection identity',async()=>{
    const source=connect('transfer-identity'),target=dedicated(),initial=await ready(source),original=source.port;
    await adopt(target,original);equal(source.worker.port,original);
    const reply=await remoteEcho(target,'same-global');equal(reply.value,'same-global');equal(reply.token,initial.token);equal(reply.connection,initial.connection);
  });
  await check('detached sender cannot receive send or transfer the connection again',async()=>{
    const source=connect('transfer-detach'),other=connect('transfer-detach'),target=dedicated();await ready(source);await ready(other);
    await adopt(target,source.port);let afterTransferEvents=0;source.port.onmessage=()=>++afterTransferEvents;
    source.port.postMessage({command:'bad'});cloneError(()=>target.worker.postMessage({port:source.port},[source.port]));
    equal((await remoteEcho(target,'new-owner-only')).value,'new-owner-only');await delay(40);
    equal(afterTransferEvents,0);equal((await request(other,'stats')).data.badMessages,0);
  });
  await check('messages queued before transfer arrive in original FIFO order',async()=>{
    const source=connect('transfer-queue',false),control=connect('transfer-queue'),target=dedicated();await ready(control);await delay(40);
    await adopt(target,source.port);await remoteEcho(target,'queue-tail');
    const received=target.events.filter(e=>e.data.kind==='relay').map(e=>e.data.value);
    equal(received.length,4);equal(received[0].kind,'ready');equal(received[1].number,1);equal(received[2].number,2);equal(received[3].value,'queue-tail');
  });
  await check('terminating the recipient preserves another shared-worker client',async()=>{
    const source=connect('transfer-terminate'),other=connect('transfer-terminate'),target=dedicated();const initial=await ready(other);await ready(source);
    await adopt(target,source.port);await remoteEcho(target,'before-terminate');target.worker.terminate();
    const reply=(await request(other,'echo',{value:'surviving-client'})).data;equal(reply.value,'surviving-client');equal(reply.token,initial.token);
  });
  await check('closing the transferred endpoint preserves another shared-worker client',async()=>{
    const source=connect('transfer-close'),other=connect('transfer-close'),target=dedicated();await ready(source);await ready(other);
    await adopt(target,source.port);await remoteEcho(target,'before-close');target.worker.postMessage({command:'close'});
    await until(()=>target.events.some(e=>e.data.kind==='closed'),'endpoint close acknowledgement');
    equal((await request(other,'echo',{value:'other-port-alive'})).data.value,'other-port-alive');
  });
  await check('a connection endpoint can return and transfer to another worker',async()=>{
    const source=connect('transfer-return'),first=dedicated(),second=dedicated();const initial=await ready(source);
    await adopt(first,source.port);await remoteEcho(first,'first-recipient');first.worker.postMessage({command:'return'});
    await until(()=>first.events.some(e=>e.data.kind==='returned'),'returned connection endpoint');
    const returned=first.events.find(e=>e.data.kind==='returned');equal(returned.ports.length,1);equal(returned.ports[0],returned.data.port);assert(returned.ports[0]!==source.port,'transfer should create a new owner endpoint');
    await adopt(second,returned.ports[0]);const reply=await remoteEcho(second,'second-recipient');equal(reply.value,'second-recipient');equal(reply.token,initial.token);equal(reply.connection,initial.connection);
  });
  await check('an independent channel survives connection transfer and recipient termination',async()=>{
    const source=connect('transfer-independent-channel'),target=dedicated();await ready(source);
    const reply=await request(source,'give'),port=reply.ports[0],messages=[];equal(reply.data.port,port);port.onmessage=e=>messages.push(e.data);
    await adopt(target,source.port);await remoteEcho(target,'root-transferred');port.postMessage('while-transferred');
    await until(()=>messages.some(e=>e.value==='while-transferred'),'independent channel while root transferred');target.worker.terminate();port.postMessage('after-recipient-terminated');
    await until(()=>messages.some(e=>e.value==='after-recipient-terminated'),'independent channel after recipient terminated');port.close();
  });
  for(const value of workers)try{value.worker.terminate();}catch(_){}
  for(const value of clients)try{value.port.close();}catch(_){}
  const passed=rows.filter(row=>row.ok).length;console.log('SHARED_TRANSFER_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));
  document.documentElement.setAttribute('data-shared-transfer-done','1');
}
main().catch(error=>{console.log('SHARED_TRANSFER_INFRA '+JSON.stringify({error:error.name+': '+error.message}));document.documentElement.setAttribute('data-shared-transfer-done','1');});
</script>'''


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        if path == '/':
            body, mime = self.server.page, 'text/html; charset=utf-8'
        elif path == '/worker.js':
            body, mime = WORKER.encode(), 'text/javascript; charset=utf-8'
        elif path == '/dedicated.js':
            body, mime = DEDICATED.encode(), 'text/javascript; charset=utf-8'
        else:
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
    parser = argparse.ArgumentParser(description=__doc__)
    engine = parser.add_mutually_exclusive_group(required=True)
    engine.add_argument('--binary', type=Path)
    engine.add_argument('--chromium', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise RuntimeError('Preserve evidence: use a fresh output')
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
        with tempfile.TemporaryDirectory(prefix='lumen-shared-transfer-') as profile:
            if args.chromium:
                stdout, stderr, code, version = chrome_control(binary, origins[0] + '/', profile,
                    completion_marker='SHARED_TRANSFER_RESULTS ', infrastructure_marker='SHARED_TRANSFER_INFRA ')
            else:
                env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
                env.update(LUMEN_PROFILE_DIR=profile, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-shared-transfer-done')
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
        for marker, destination in [('SHARED_TRANSFER_CHECK ', rows), ('SHARED_TRANSFER_RESULTS ', summaries), ('SHARED_TRANSFER_INFRA ', infrastructure)]:
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
    receipt = {'kind': 'shared-worker-connection-transfer-lifecycle', 'officialWPT': False,
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
