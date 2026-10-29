#!/usr/bin/env python3
"""Bounded native-input-queue migration qualifier, separate from frozen9 and2.

A recipient holds one120ms script task while an independently running shared
worker sends25 messages. Its owner observes the producer acknowledgement before
the endpoint returns; all queued messages must follow that endpoint even when
the former recipient is terminated. The deliberate short task is a test barrier,
not a browser performance workload.
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

import test_shared_worker_transfer_lifecycle as frozen
from test_worker_response_headers import chrome_control

INHERITED_SHA = '2c66901eb9a5a12bf8947c3ce5f217d88da45f2a7795295a5a6e88a58f020dae'
if hashlib.sha256(Path(frozen.__file__).read_bytes()).hexdigest() != INHERITED_SHA:
    raise RuntimeError('Frozen transfer9 helper source changed')
EXPECTED_NAMES = ['queued native messages survive port return, round ' + str(i) for i in range(1, 4)]
WORKER = r'''
'use strict';let count=0;const ports=new Map(),token=Date.now().toString(36)+Math.random().toString(36);
onconnect=event=>{const port=event.ports[0],connection=++count;ports.set(connection,port);port.postMessage({kind:'ready',connection,token});
port.onmessage=event=>{const data=event.data;
  if(data.command==='echo')port.postMessage({kind:'echo',id:data.id,value:data.value,token,connection});
  else if(data.command==='burst'){
    const target=ports.get(data.target);for(let number=0;number<data.count;number++)target.postMessage({kind:'burst',round:data.round,number});
    port.postMessage({kind:'producer-complete',round:data.round,count:data.count});
  }
};};
'''
DEDICATED = frozen.DEDICATED.replace("}else if(data.command==='return'){", r'''}else if(data.command==='returnBusy'){
    postMessage({kind:'busy'});const end=performance.now()+120;while(performance.now()<end){}
    endpoint.onmessage=null;postMessage({kind:'returned',port:endpoint},[endpoint]);endpoint=null;
  }else if(data.command==='return'){''')
PAGE = frozen.PAGE.split('async function main(){', 1)[0].replace('SHARED_TRANSFER_CHECK ', 'SHARED_BURST_CHECK ') + r'''
async function main(){
  for(let round=1;round<=3;round++)await check('queued native messages survive port return, round '+round,async()=>{
    const source=connect('return-burst-'+round),control=connect('return-burst-'+round),recipient=dedicated();
    const initial=await ready(source);await ready(control);await adopt(recipient,source.port);await remoteEcho(recipient,'ready-to-migrate');
    recipient.worker.postMessage({command:'returnBusy'});await until(()=>recipient.events.some(e=>e.data.kind==='busy'),'recipient script barrier');
    const count=25;control.port.postMessage({command:'burst',round,count,target:initial.connection});
    await until(()=>control.events.some(e=>e.data.kind==='producer-complete'&&e.data.round===round),'producer acknowledgement');
    const barrierEstablished=!recipient.events.some(e=>e.data.kind==='returned');
    console.log('SHARED_BURST_BARRIER '+JSON.stringify({round,barrierEstablished,count}));
    assert(barrierEstablished,'setup barrier was not established; this run cannot classify migration semantics');
    await until(()=>recipient.events.some(e=>e.data.kind==='returned'),'returned endpoint');
    const event=recipient.events.find(e=>e.data.kind==='returned'),port=event.ports[0],received=[];
    equal(port,event.data.port);port.onmessage=message=>received.push(message.data);recipient.worker.terminate();
    const all=()=>recipient.events.filter(e=>e.data.kind==='relay'&&e.data.value.kind==='burst').map(e=>e.data.value).concat(received.filter(data=>data.kind==='burst'));
    try{await until(()=>all().length>=count,'all25 queued messages follow endpoint');const messages=all();equal(messages.length,count);equal(messages.map(data=>data.number).join(','),Array.from({length:count},(_,i)=>i).join(','));assert(messages.every(data=>data.round===round),'foreign burst payload');}
    finally{console.log('SHARED_BURST_DELIVERY '+JSON.stringify({round,received:all().map(data=>data.number)}));port.close();}
  });
  for(const value of workers)try{value.worker.terminate();}catch(_){}
  for(const value of clients)try{value.port.close();}catch(_){}
  const passed=rows.filter(row=>row.ok).length;console.log('SHARED_BURST_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));
  document.documentElement.setAttribute('data-shared-burst-done','1');
}
main().catch(error=>{console.log('SHARED_BURST_INFRA '+JSON.stringify({error:error.name+': '+error.message}));document.documentElement.setAttribute('data-shared-burst-done','1');});
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
        with tempfile.TemporaryDirectory(prefix='lumen-shared-burst-') as profile:
            if args.chromium:
                stdout, stderr, code, version = chrome_control(binary, origins[0] + '/', profile,
                    completion_marker='SHARED_BURST_RESULTS ', infrastructure_marker='SHARED_BURST_INFRA ')
            else:
                env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
                env.update(LUMEN_PROFILE_DIR=profile, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-shared-burst-done')
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
        for marker, destination in [('SHARED_BURST_CHECK ', rows), ('SHARED_BURST_RESULTS ', summaries), ('SHARED_BURST_INFRA ', infrastructure)]:
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
    receipt = {'kind': 'shared-worker-native-input-queue-migration', 'officialWPT': False,
        'nativeLumenExecuted': args.binary is not None and code is not None,
        'chromiumControlExecuted': args.chromium is not None and code is not None,
        'binary': str(binary), 'binarySHA256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'browserVersion': version, 'fixtureSHA256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'pageTemplateSHA256': hashlib.sha256(PAGE.encode()).hexdigest(),
        'workerSHA256': hashlib.sha256(WORKER.encode()).hexdigest(),
        'inheritedFixtureSHA256': INHERITED_SHA,
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
