#!/usr/bin/env python3
"""Real dedicated-worker HTTP header visibility checks through public APIs.

This is a focused Lumen regression fixture, not an upstream WPT selection.
Two loopback origins distinguish basic and CORS filtered responses. No page
or worker API is polyfilled. Run under tools/monitor_process.py for 1Hz data.
Spec: https://fetch.spec.whatwg.org/#concept-filtered-response-cors
"""
import argparse
import asyncio
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


CASES = [
    {'name': 'same-origin Set-Cookie is hidden', 'origin': 'same', 'path': 'default', 'header': 'Set-Cookie', 'expected': None},
    {'name': 'same-origin Set-Cookie2 is hidden', 'origin': 'same', 'path': 'default', 'header': 'Set-Cookie2', 'expected': None},
    {'name': 'same-origin custom header is visible', 'origin': 'same', 'path': 'default', 'header': 'X-Private', 'expected': 'private'},
    {'name': 'cross-origin private header is hidden', 'origin': 'cross', 'path': 'default', 'header': 'X-Private', 'expected': None},
    {'name': 'cross-origin safelisted Content-Type is visible', 'origin': 'cross', 'path': 'default', 'header': 'Content-Type', 'expected': 'text/plain; charset=utf-8'},
    {'name': 'cross-origin CORS control header is hidden', 'origin': 'cross', 'path': 'default', 'header': 'Access-Control-Allow-Origin', 'expected': None},
    {'name': 'explicitly exposed custom header is visible', 'origin': 'cross', 'path': 'expose', 'header': 'X-Visible', 'expected': 'visible'},
    {'name': 'explicit exposure keeps other custom header hidden', 'origin': 'cross', 'path': 'expose', 'header': 'X-Private', 'expected': None},
    {'name': 'explicit exposure cannot reveal Set-Cookie', 'origin': 'cross', 'path': 'expose', 'header': 'Set-Cookie', 'expected': None},
    {'name': 'explicit exposure cannot reveal Set-Cookie2', 'origin': 'cross', 'path': 'expose', 'header': 'Set-Cookie2', 'expected': None},
    {'name': 'credentialed wildcard keeps private header hidden', 'origin': 'cross', 'path': 'wildcard', 'credentials': 'include', 'header': 'X-Private', 'expected': None},
    {'name': 'credentialed wildcard retains safelisted header', 'origin': 'cross', 'path': 'wildcard', 'credentials': 'include', 'header': 'Content-Type', 'expected': 'text/plain; charset=utf-8'},
    {'name': 'uncredentialed wildcard exposes custom header', 'origin': 'cross', 'path': 'wildcard', 'credentials': 'omit', 'header': 'X-Private', 'expected': 'private'},
    {'name': 'uncredentialed wildcard never exposes cookie', 'origin': 'cross', 'path': 'wildcard', 'credentials': 'omit', 'header': 'Set-Cookie', 'expected': None},
    {'name': 'credentialed explicit exposure remains effective', 'origin': 'cross', 'path': 'expose', 'credentials': 'include', 'header': 'X-Visible', 'expected': 'visible'},
    {'name': 'credentialed wildcard plus explicit name exposes only explicit name', 'origin': 'cross', 'path': 'wildcard-explicit', 'credentials': 'include', 'header': 'X-Visible', 'expected': 'visible'},
    {'name': 'cross-origin redirect does not reveal private headers', 'origin': 'same', 'path': 'redirect', 'header': 'X-Private', 'expected': None},
    {'name': 'missing CORS permission rejects response', 'origin': 'cross', 'path': 'denied', 'reject': True},
]
EXPECTED_NAMES = [api + ': ' + case['name'] for api in ('fetch', 'xhr') for case in CASES]

WORKER_TEMPLATE = r'''
'use strict';
const crossOrigin=__CROSS_ORIGIN__, cases=__CASES__, rows=[];
const equal=(actual,expected)=>{if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));};
function xhrRequest(url, credentials){return new Promise((resolve,reject)=>{const xhr=new XMLHttpRequest();xhr.open('GET',url);xhr.withCredentials=credentials==='include';xhr.timeout=1200;xhr.onload=()=>resolve({status:xhr.status,get:name=>xhr.getResponseHeader(name),all:xhr.getAllResponseHeaders()});xhr.onerror=()=>reject(new TypeError('network error'));xhr.ontimeout=()=>reject(new Error('request timed out'));xhr.send();});}
async function fetchRequest(url,credentials){const response=await fetch(url,{credentials});return{status:response.status,get:name=>response.headers.get(name),all:Array.from(response.headers).map(([name,value])=>name+': '+value+'\r\n').join('')};}
async function check(api,test){const name=api+': '+test.name;let timer;try{
  await Promise.race([(async()=>{
    const url=(test.origin==='cross'?crossOrigin:location.origin)+'/headers/'+test.path;
    let result,error;try{result=await(api==='fetch'?fetchRequest:xhrRequest)(url,test.credentials||'same-origin');}catch(e){error=e;}
    if(test.reject){if(!error)throw new Error('CORS-denied response unexpectedly readable');equal(error.name,'TypeError');return;}
    if(error)throw error;equal(result.status,200);equal(result.get(test.header),test.expected);
    if(test.expected===null){const escaped=test.header.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');if(new RegExp('(^|\\r?\\n)'+escaped+':','i').test(result.all))throw new Error('hidden header appeared in full header enumeration');}
    if(/(^|\r?\n)set-cookie2?:/i.test(result.all))throw new Error('forbidden cookie appeared in full header enumeration');
  })(),new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('check timeout')),1500);})]);
  rows.push({i:rows.length,name,ok:true});
}catch(e){rows.push({i:rows.length,name,ok:false,error:e.name+': '+e.message});}finally{clearTimeout(timer);}postMessage({kind:'check',row:rows[rows.length-1]});}
(async()=>{for(const api of ['fetch','xhr'])for(const test of cases)await check(api,test);const passed=rows.filter(row=>row.ok).length;postMessage({kind:'results',summary:{total:rows.length,passed,failed:rows.length-passed}});})().catch(e=>postMessage({kind:'infrastructure',error:e.name+': '+e.message}));
'''

HTML = r'''<!doctype html><meta charset=utf-8><title>Worker response headers</title><script>
'use strict';
try {
const worker=new Worker('/worker.js');
worker.onmessage=event=>{const message=event.data;if(message.kind==='check')console.log('WORKER_HEADERS_CHECK '+JSON.stringify(message.row));else if(message.kind==='results'){console.log('WORKER_HEADERS_RESULTS '+JSON.stringify(message.summary));worker.terminate();document.documentElement.setAttribute('data-worker-headers-done','1');}else console.log('WORKER_HEADERS_INFRA '+JSON.stringify(message));};
worker.onerror=event=>console.log('WORKER_HEADERS_INFRA '+JSON.stringify({error:event.message||'worker error'}));
}catch(error){console.log('WORKER_HEADERS_INFRA '+JSON.stringify({error:error.name+': '+error.message}));}
</script>'''


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def do_GET(self):
        route = urllib.parse.urlsplit(self.path).path
        if route == '/':
            body, content_type = HTML.encode(), 'text/html; charset=utf-8'
        elif route == '/worker.js':
            body, content_type = self.server.worker_bytes, 'text/javascript; charset=utf-8'
        elif route == '/headers/redirect':
            self.send_response(302)
            self.send_header('Location', self.server.cross_origin + '/headers/default')
            self.send_header('Content-Length', '0')
            self.send_header('Connection', 'close')
            self.end_headers()
            return
        elif route.startswith('/headers/'):
            body, content_type = b'headers-ok', 'text/plain; charset=utf-8'
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Connection', 'close')
        if route.startswith('/headers/'):
            self.send_header('Set-Cookie', 'worker_header_probe=one; Max-Age=0; Path=/')
            self.send_header('Set-Cookie2', 'worker_header_probe=two; Max-Age=0; Path=/')
            self.send_header('X-Private', 'private')
            self.send_header('X-Visible', 'visible')
            if route != '/headers/denied':
                self.send_header('Access-Control-Allow-Origin', self.server.owner_origin)
                self.send_header('Access-Control-Allow-Credentials', 'true')
            exposed = {'/headers/expose': 'X-Visible, Set-Cookie, Set-Cookie2', '/headers/wildcard': '*', '/headers/wildcard-explicit': '*, X-Visible'}.get(route)
            if exposed is not None:
                self.send_header('Access-Control-Expose-Headers', exposed)
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def chrome_control(binary, url, profile, completion_marker='WORKER_HEADERS_RESULTS ',
                   infrastructure_marker='WORKER_HEADERS_INFRA '):
    """Drive an actual isolated Chrome page; never substitute it for Lumen."""
    import websockets
    log = tempfile.TemporaryFile(mode='w+t')
    process = subprocess.Popen([
        str(binary), '--headless=new', '--no-sandbox', '--disable-gpu',
        '--disable-background-networking', '--no-first-run', '--no-default-browser-check',
        '--remote-debugging-port=0', '--remote-debugging-address=127.0.0.1',
        '--user-data-dir=' + profile, 'about:blank',
    ], stdout=log, stderr=log)
    lines = []
    version = None
    try:
        active = Path(profile) / 'DevToolsActivePort'
        deadline = time.monotonic() + 10
        while not active.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        if not active.exists():
            raise RuntimeError('Chrome did not create a local DevTools endpoint')
        port, endpoint = active.read_text().splitlines()[:2]

        async def execute():
            sequence = 0
            finished = False
            async with websockets.connect('ws://127.0.0.1:' + port + endpoint, max_size=4 * 1024 * 1024, proxy=None) as socket:
                def record(message):
                    nonlocal finished
                    if message.get('method') == 'Runtime.consoleAPICalled':
                        text = ' '.join(str(arg.get('value', arg.get('description', ''))) for arg in message['params']['args'])
                        lines.append(text)
                        if completion_marker in text:
                            finished = True
                    if message.get('method') == 'Runtime.exceptionThrown':
                        detail = message['params'].get('exceptionDetails', {})
                        lines.append(infrastructure_marker + json.dumps({'error': detail.get('text', 'uncaught page exception')}))

                async def call(method, params=None, session=None):
                    nonlocal sequence
                    sequence += 1
                    identity = sequence
                    command = {'id': identity, 'method': method, 'params': params or {}}
                    if session:
                        command['sessionId'] = session
                    await socket.send(json.dumps(command))
                    while True:
                        message = json.loads(await asyncio.wait_for(socket.recv(), 65))
                        if message.get('id') == identity:
                            if 'error' in message:
                                raise RuntimeError(message['error'])
                            return message.get('result', {})
                        record(message)

                build = await call('Browser.getVersion')
                target = await call('Target.createTarget', {'url': 'about:blank'})
                attached = await call('Target.attachToTarget', {'targetId': target['targetId'], 'flatten': True})
                session = attached['sessionId']
                await call('Runtime.enable', session=session)
                await call('Page.enable', session=session)
                await call('Page.navigate', {'url': url}, session=session)
                deadline = time.monotonic() + 65
                while not finished and time.monotonic() < deadline:
                    message = json.loads(await asyncio.wait_for(socket.recv(), max(0.1, deadline - time.monotonic())))
                    record(message)
                if not finished:
                    raise TimeoutError('Chrome fixture did not report all expected results')
                await call('Target.closeTarget', {'targetId': target['targetId']})
                return build

        version = asyncio.run(execute())
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        log.seek(0)
        diagnostics = log.read()
        log.close()
    return '\n'.join(lines) + '\n', diagnostics, 0, version


def main():
    parser = argparse.ArgumentParser()
    runner = parser.add_mutually_exclusive_group(required=True)
    runner.add_argument('--binary', type=Path, help='actual Lumen jsrun executable')
    runner.add_argument('--chromium', type=Path, help='actual Chromium control; requires Python websockets')
    parser.add_argument('--output', type=Path, help='structured regression receipt')
    args = parser.parse_args()
    binary = (args.binary or args.chromium).resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error('runner must be an existing executable; this test does not mock a browser')
    servers = [http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) for _ in range(2)]
    owner_origin, cross_origin = ['http://127.0.0.1:' + str(server.server_port) for server in servers]
    worker = WORKER_TEMPLATE.replace('__CROSS_ORIGIN__', json.dumps(cross_origin)).replace('__CASES__', json.dumps(CASES, separators=(',', ':'))).encode()
    threads = []
    for server in servers:
        server.daemon_threads = True
        server.owner_origin, server.cross_origin, server.worker_bytes = owner_origin, cross_origin, worker
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        threads.append(thread)
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    start = time.monotonic()
    stdout = stderr = ''
    exit_code = None
    version = None
    infrastructure = []
    try:
        with tempfile.TemporaryDirectory(prefix='lume-worker-headers-') as profile:
            if args.chromium:
                stdout, stderr, exit_code, version = chrome_control(binary, owner_origin + '/', profile)
            else:
                env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
                env.update(LUMEN_PROFILE_DIR=profile, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-worker-headers-done')
                run = subprocess.run([str(binary), owner_origin + '/', '60000'], capture_output=True, text=True, env=env, timeout=65)
                stdout, stderr, exit_code = run.stdout, run.stderr, run.returncode
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
    duration = time.monotonic() - start
    print(stdout, end='')
    print(stderr, end='')
    rows, summaries = [], []
    for line in (stdout + '\n' + stderr).splitlines():
        for marker, destination in [('WORKER_HEADERS_CHECK ', rows), ('WORKER_HEADERS_RESULTS ', summaries), ('WORKER_HEADERS_INFRA ', infrastructure)]:
            index = line.find(marker)
            if index >= 0:
                try:
                    destination.append(json.loads(line[index + len(marker):]))
                except json.JSONDecodeError as error:
                    infrastructure.append({'error': 'invalid result JSON', 'marker': marker, 'detail': str(error)})
    if exit_code != 0:
        infrastructure.append({'error': 'nonzero or missing runner exit', 'exit': exit_code})
    if [row.get('i') for row in rows] != list(range(len(EXPECTED_NAMES))) or [row.get('name') for row in rows] != EXPECTED_NAMES:
        infrastructure.append({'error': 'missing, duplicate, changed or reordered fixture cases', 'observed': len(rows), 'expected': len(EXPECTED_NAMES)})
    passed = sum(row.get('ok') is True for row in rows)
    summary = {'total': len(EXPECTED_NAMES), 'passed': passed, 'failed': len(EXPECTED_NAMES) - passed}
    if summaries != [summary]:
        infrastructure.append({'error': 'missing or inconsistent summary', 'summaries': summaries})
    receipt = {
        'kind': 'dedicated-worker-public-api-http-regression', 'officialWPT': False,
        'nativeLumenExecuted': args.binary is not None and exit_code is not None,
        'chromiumControlExecuted': args.chromium is not None and exit_code is not None,
        'browserVersion': version, 'binary': str(binary),
        'binarySHA256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'fixtureSHA256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'htmlSHA256': hashlib.sha256(HTML.encode()).hexdigest(),
        'workerTemplateSHA256': hashlib.sha256(WORKER_TEMPLATE.encode()).hexdigest(),
        'caseManifestSHA256': hashlib.sha256(json.dumps(CASES, sort_keys=True).encode()).hexdigest(),
        'started': started, 'ended': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'durationSeconds': duration, **summary, 'rows': rows, 'infrastructureErrors': infrastructure,
        'monitor': 'Run with the external 1Hz monitor; no embedded resource claim',
        'stdoutSHA256': hashlib.sha256(stdout.encode()).hexdigest(), 'stderrSHA256': hashlib.sha256(stderr.encode()).hexdigest(),
    }
    if args.output:
        args.output.write_text(json.dumps(receipt, indent=2) + '\n')
    print('WORKER_HEADERS_RECEIPT ' + json.dumps({key: value for key, value in receipt.items() if key != 'rows'}))
    return 2 if infrastructure else 0 if passed == len(EXPECTED_NAMES) else 1


if __name__ == '__main__':
    raise SystemExit(main())
