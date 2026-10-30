#!/usr/bin/env python3
"""Observe shared-buffer codec and ISO-2022-JP reset behavior independently of WPT.

This is a diagnostic, not a replacement for upstream WPT expectations. A fresh
loopback page and dedicated worker report actual results to their owned server.
"""
from datetime import datetime, timezone
import hashlib
import importlib.util
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import sys
import threading
import time
from urllib.parse import urlsplit

ROOT = Path.cwd().resolve()
OUT = ROOT / 'out/lume-20261029-cpu30/codec-boundary-control-1'
BUILD = ROOT / 'out/lume-20261029-cpu30/native-bom-1/summary.json'
NATIVE_SHA = '4b26d5b59edf9f6866b1317e6ffa29aabe7d8681cdee26a2b56a670d23536d54'
CHROME = ROOT / 'output/session-102900614/chrome-official-reference-v2/Google Chrome.app/Contents/MacOS/Google Chrome'
CHROME_SHA = '0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'

CORE = r"""
function observeCodecBoundary() {
 const capture=fn=>{try {const value=fn();return {kind:'return',value,codepoints:typeof value==='string'?Array.from(value,c=>c.codePointAt(0)):undefined};}catch(e){return {kind:'throw',name:e.name,message:String(e.message)};}};
 const context={isSecureContext,crossOriginIsolated,sharedArrayBufferType:typeof SharedArrayBuffer};
 let buffer,view;
 const allocation=capture(()=>{buffer=new SharedArrayBuffer(4);view=new Uint8Array(buffer);view[0]=65;return view.byteLength;});
 const shared={name:'shared_buffer_codec_boundary',context,calls:[['allocate',4],['decode','utf-8',[65]],['encodeInto','B',4]],observations:[allocation]};
 if(view){shared.observations.push(capture(()=>new TextDecoder().decode(view.subarray(0,1))));shared.observations.push(capture(()=>{const result=new TextEncoder().encodeInto('B',view);return {read:result.read,written:result.written,bytes:Array.from(view)};}));}
 else {shared.observations.push({kind:'unavailable'},{kind:'unavailable'});}
 const d=new TextDecoder('iso-2022-jp');
 const reset={name:'iso2022jp_reset_after_final_chunk',context,calls:[[27,40,73,33],[33]],observations:[capture(()=>d.decode(Uint8Array.from([27,40,73,33]))),capture(()=>d.decode(Uint8Array.from([33])))]};
 return [shared,reset];
}
"""


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def stop_owned(process):
    # Use the already reviewed PID/start-identity group cleanup helper so
    # sandbox helpers cannot be abandoned after their root exits.
    spec = importlib.util.spec_from_file_location('codec_boundary_owned_monitor', ROOT / 'tools/monitor_process.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    cleanup = module.cleanup_owned_group(process, {}, grace=5)
    until = time.monotonic() + 2
    while module.owned_group_members(process.pid) and time.monotonic() < until:
        time.sleep(.2)
    if cleanup.get('error') or module.owned_group_members(process.pid):
        raise RuntimeError('Owned diagnostic process-group cleanup is incomplete')
    return process.poll()


def observe(label, binary, digest, chrome):
    if sha(binary) != digest:
        raise RuntimeError(label + ' executable hash differs')
    directory = OUT / label
    directory.mkdir()
    profile = directory / 'profile'
    profile.mkdir()
    nonce = secrets.token_hex(16)
    event = threading.Event()
    reports = []
    worker = CORE + '\npostMessage(observeCodecBoundary());\n'
    page = ('<!doctype html><meta charset="utf-8"><title>Owned Codec boundary interoperability probe</title>'
            '<h1>Codec boundary interoperability diagnostic</h1><script>' + CORE + r'''
const rows=observeCodecBoundary();
const worker=new Worker('/worker.js');
let done=false;
const finish=(workerRows,workerIssue)=>{
 if(done)return;done=true;worker.terminate();
 fetch('/report',{method:'POST',headers:{'Content-Type':'application/json'},
 body:JSON.stringify({nonce:NONCE,window:rows,worker:workerRows,workerIssue,userAgent:navigator.userAgent})});
};
worker.onmessage=e=>finish(e.data,null);
worker.onerror=e=>finish(null,String(e.message||'worker error'));
setTimeout(()=>finish(null,'worker timeout'),5000);
</script>'''.replace('NONCE', json.dumps(nonce))).encode()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            is_worker = urlsplit(self.path).path == '/worker.js'
            data = worker.encode() if is_worker else page
            self.send_response(200)
            self.send_header('Content-Type', 'application/javascript; charset=utf-8' if is_worker else 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self):
            size = int(self.headers.get('Content-Length', '0'))
            if self.path != '/report' or size < 1 or size > 100000:
                self.send_error(400)
                return
            value = json.loads(self.rfile.read(size))
            if value.pop('nonce', None) != nonce:
                self.send_error(403)
                return
            reports.append(value)
            self.send_response(204)
            self.end_headers()
            event.set()

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = 'http://127.0.0.1:' + str(server.server_port) + '/'
    environment = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
    environment.update(LUMEN_PROFILE_DIR=str(profile), LUMEN_CONSOLE='1', LUMEN_WINDOW='900x700')
    command = [str(binary), url]
    if chrome:
        command = [str(binary), '--no-first-run', '--no-default-browser-check',
                   '--disable-background-networking', '--disable-component-update',
                   '--disable-sync', '--user-data-dir=' + str(profile), url]
    began = time.monotonic()
    log = (directory / 'browser.log').open('x')
    app = None
    monitor = None
    monitor_log = None
    row = {'browser': label, 'binarySHA256': digest, 'startedUTC': utc(),
           'fixtureCoreSHA256': hashlib.sha256(CORE.encode()).hexdigest(), 'complete': False}
    try:
        app = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT,
                               stdin=subprocess.DEVNULL, start_new_session=True)
        monitor_log = (directory / 'monitor.log').open('x')
        prefix = directory / 'resources'
        monitor = subprocess.Popen([sys.executable, 'tools/monitor_process.py', '--pid', str(app.pid),
                                    '--duration', '25', '--settle', '0', '--output', str(prefix)],
                                   stdout=monitor_log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                   start_new_session=True)
        if not event.wait(18):
            raise RuntimeError(label + ' did not report the fresh fixture')
        time.sleep(max(0, 1.3 - (time.monotonic() - began)))
        if len(reports) != 1:
            raise RuntimeError(label + ' produced duplicate fixture reports')
        payload = reports[0]
        if payload.get('workerIssue') or any(len(payload.get(scope, [])) != 2 for scope in ['window', 'worker']):
            raise RuntimeError(label + ' fixture context incomplete')
        row['actual'] = payload
        row['complete'] = True
    except Exception as error:
        row['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        if app is not None:
            try:
                row['ownedBrowserExitCode'] = stop_owned(app)
                row['ownedProcessGroupGone'] = True
            except Exception as cleanup_error:
                row['ownedCleanupError'] = str(cleanup_error)
                row['complete'] = False
        if monitor is not None:
            try:
                row['monitorExitCode'] = monitor.wait(timeout=6)
            except subprocess.TimeoutExpired:
                row['monitorForcedStop'] = True
                stop_owned(monitor)
        log.close()
        if monitor_log:
            monitor_log.close()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
        resources = directory / 'resources.json'
        if resources.is_file():
            telemetry = json.loads(resources.read_text())
            row['resources'] = {'path': str(resources.relative_to(ROOT)), 'sha256': sha(resources),
                                'samples': telemetry.get('samples'), 'intervalSamples': telemetry.get('interval_cpu_samples'),
                                'collectorError': telemetry.get('collector_error'), 'violations': telemetry.get('violations')}
            if telemetry.get('collector_error') or not telemetry.get('samples'):
                row['complete'] = False
                row['resourceError'] = 'Incomplete or failed one-second collection'
        else:
            row['complete'] = False
            row['resourceError'] = 'Missing one-second collection'
        row.update(finishedUTC=utc(),wallSeconds=round(time.monotonic() - began, 3),binarySHA256After=sha(binary))
        row['complete'] = row['complete'] and row['binarySHA256After'] == digest
        (directory / 'receipt.json').write_text(json.dumps(row, indent=2) + '\n')
    return row


def main():
    OUT.mkdir(parents=True, exist_ok=False)
    build = json.loads(BUILD.read_text())
    if not build.get('passed'):
        raise RuntimeError('Current native build is not verified')
    native = build['artifacts']['lumen']
    if native['sha256'] != NATIVE_SHA:
        raise RuntimeError('Reviewed current native artifact differs')
    if sha(ROOT / 'tools/monitor_process.py') != 'e31aa4fc949b149ac9c0e8c6170ce72ec3def971dbab4d6b026635f262d9cd79':
        raise RuntimeError('Reviewed process cleanup helper changed')
    results = [observe('lume', ROOT / native['path'], native['sha256'], False),
               observe('qualified-chrome', CHROME, CHROME_SHA, True)]
    complete = all(row['complete'] for row in results)
    comparison = []
    if complete:
        for scope in ['window', 'worker']:
            for a, b in zip(results[0]['actual'][scope], results[1]['actual'][scope]):
                if a['name'] != b['name'] or a['calls'] != b['calls']:
                    raise RuntimeError('The paired browser inputs differ')
                comparison.append({'scope': scope, 'name': a['name'], 'same': a['observations'] == b['observations'],
                                   'lume': a['observations'], 'chrome': b['observations'], 'lumeContext': a['context'], 'chromeContext': b['context']})
    receipt = {'taskId': '20261029-cpu30', 'complete': complete, 'browsers': results,
               'comparisons': comparison,
               'scope': 'Two observational codec boundary cases in a fresh loopback origin with COOP/COEP, in window and worker, using exact headful Chrome/Lume executables. No WPT fixtures/reporters or API substitutions; no conformance score is awarded.',
               'spec': 'https://encoding.spec.whatwg.org/#dom-textdecoder-decode',
               'isolationHeaders': {'Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp'}}
    (OUT / 'summary.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'complete': complete, 'receipt': str((OUT/'summary.json').relative_to(ROOT)),
                      'same': sum(row['same'] for row in comparison), 'total': len(comparison),
                      'comparisons': comparison}, indent=2))
    return 0 if complete else 1


if __name__ == '__main__':
    raise SystemExit(main())
