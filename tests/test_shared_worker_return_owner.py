#!/usr/bin/env python3
"""Two additional public ownership qualifiers after a SharedWorker port returns.

The frozen9 transfer suite is unchanged. These cases verify that an intermediate
worker can exit after relinquishing an endpoint, including an in-flight reply.
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


INHERITED = Path(frozen.__file__)
INHERITED_SHA = '2c66901eb9a5a12bf8947c3ce5f217d88da45f2a7795295a5a6e88a58f020dae'
if hashlib.sha256(INHERITED.read_bytes()).hexdigest() != INHERITED_SHA:
    raise RuntimeError('Frozen transfer9 helper source changed')

EXPECTED_NAMES = [
    'returned connection survives termination of its former worker before retransfer',
    'an in-flight shared-worker reply follows the returned endpoint after former owner exits',
]
WORKER = frozen.WORKER.replace("if(data.command==='echo')",
    "if(data.command==='deferred')setTimeout(()=>port.postMessage({id:data.id,kind:'echo',value:data.value,connection,token}),80);else if(data.command==='echo')")
DEDICATED = frozen.DEDICATED
PAGE = frozen.PAGE.split('async function main(){', 1)[0].replace('SHARED_TRANSFER_CHECK ', 'SHARED_RETURN_CHECK ') + r'''
async function returned(target){
  target.worker.postMessage({command:'return'});await until(()=>target.events.some(e=>e.data.kind==='returned'),'returned endpoint');
  const event=target.events.find(e=>e.data.kind==='returned');equal(event.ports.length,1);equal(event.ports[0],event.data.port);return event.ports[0];
}
async function main(){
  await check('returned connection survives termination of its former worker before retransfer',async()=>{
    const source=connect('return-exit-retransfer'),first=dedicated(),second=dedicated(),initial=await ready(source);
    await adopt(first,source.port);await remoteEcho(first,'before-return');const port=await returned(first);first.worker.terminate();
    await adopt(second,port);const reply=await remoteEcho(second,'after-former-owner-exit');
    equal(reply.value,'after-former-owner-exit');equal(reply.token,initial.token);equal(reply.connection,initial.connection);
  });
  await check('an in-flight shared-worker reply follows the returned endpoint after former owner exits',async()=>{
    const source=connect('return-in-flight'),first=dedicated(),initial=await ready(source);await adopt(first,source.port);
    const id=++sequence;first.worker.postMessage({command:'send',value:{command:'deferred',id,value:'in-flight-reply'}});
    const port=await returned(first);first.worker.terminate();const messages=[];port.onmessage=event=>messages.push(event.data);
    await until(()=>messages.some(data=>data.id===id),'reply after former owner exit');const reply=messages.find(data=>data.id===id);
    equal(reply.value,'in-flight-reply');equal(reply.token,initial.token);equal(reply.connection,initial.connection);port.close();
  });
  for(const value of workers)try{value.worker.terminate();}catch(_){}
  for(const value of clients)try{value.port.close();}catch(_){}
  const passed=rows.filter(row=>row.ok).length;console.log('SHARED_RETURN_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));
  document.documentElement.setAttribute('data-shared-return-done','1');
}
main().catch(error=>{console.log('SHARED_RETURN_INFRA '+JSON.stringify({error:error.name+': '+error.message}));document.documentElement.setAttribute('data-shared-return-done','1');});
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
        with tempfile.TemporaryDirectory(prefix='lumen-shared-return-') as profile:
            if args.chromium:
                stdout, stderr, code, version = chrome_control(binary, origins[0] + '/', profile,
                    completion_marker='SHARED_RETURN_RESULTS ', infrastructure_marker='SHARED_RETURN_INFRA ')
            else:
                env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
                env.update(LUMEN_PROFILE_DIR=profile, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-shared-return-done')
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
        for marker, destination in [('SHARED_RETURN_CHECK ', rows), ('SHARED_RETURN_RESULTS ', summaries), ('SHARED_RETURN_INFRA ', infrastructure)]:
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
    receipt = {'kind': 'shared-worker-returned-endpoint-ownership', 'officialWPT': False,
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
