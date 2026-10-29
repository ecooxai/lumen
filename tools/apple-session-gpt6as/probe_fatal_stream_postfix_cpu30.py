#!/usr/bin/env python3
"""Observe identical TextDecoder calls in qualified Chrome and native Lume.

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
OUT = ROOT / 'out/lume-20261029-cpu30/fatal-stream-interop-3'
BUILD = ROOT / 'out/lume-20261029-cpu30/native-codec-1/summary.json'
NATIVE_SHA = '9137de2fb8cba151b7a4ca4f21aa02771061068cd41aa4327de4e3e20d709553'
CHROME = ROOT / 'output/session-102900614/chrome-official-reference-v2/Google Chrome.app/Contents/MacOS/Google Chrome'
CHROME_SHA = '0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'

CORE = r'''
function observeFatalStreams() {
 const u=a=>Uint8Array.from(a);
 const invoke=(d,input,stream)=>{
   try {
     const value=input===null ? d.decode() : stream===null ? d.decode(u(input)) : d.decode(u(input),{stream});
     return {kind:'return',value,codepoints:Array.from(value,c=>c.codePointAt(0))};
   } catch(e) {return {kind:'throw',name:e.name};}
 };
 const cases=[
  ['fd_ef_then_flush','utf-8',true,[[[0xfd,0xef],true],[null,null],[null,null]]],
  ['fd_ascii_then_flush','utf-8',true,[[[0xfd,65],true],[null,null],[[66],null]]],
  ['fd_ef_then_bom_completion','utf-8',true,[[[0xfd,0xef],true],[[0xbb,0xbf,65],false],[null,null]]],
  ['fd_bom_then_fresh_bom','utf-8',true,[[[0xfd,0xef,0xbb,0xbf],true],[[0xef,0xbb,0xbf,65],false]]],
  ['invalid_continuation_reconsume','utf-8',true,[[[0xe2,65],true],[null,null]]],
  ['fatal_without_tail_then_success','utf-8',true,[[[0xfd],true],[[0xc2],true],[[0xa2],false]]],
  ['bom_before_fatal_then_bom','utf-8',true,[[[0xef,0xbb,0xbf,0xff],true],[[0xef,0xbb,0xbf,65],false]]],
  ['replacement_fd_ef','utf-8',false,[[[0xfd,0xef],true],[null,null]]],
  ['successful_scalar_continuation','utf-8',true,[[[0xe2,0x82],true],[[0xac],false]]],
  ['iso2022jp_roman_state','iso-2022-jp',true,[[[0x1b,0x28,0x4a,0xff],true],[[0x7e],null]]],
  ['partial_e2_bad_then_valid_continuations','utf-8',true,[[[0xe2,65],true],[[0x82,0xac],false]]],
  ['partial_e2_82_bad_then_last_continuation','utf-8',true,[[[0xe2,0x82,65],true],[[0xac],false]]],
  ['restricted_e0_bad_then_valid_continuations','utf-8',true,[[[0xe0,0x80,65],true],[[0xa0,0x80],false]]],
  ['prior_e2_then_bad_call_then_valid_continuations','utf-8',true,[[[0xe2],true],[[65,66],true],[[0x82,0xac],false]]],
 ];
 return cases.map(([name,encoding,fatal,calls])=>{
   const d=new TextDecoder(encoding,{fatal});
   return {name,encoding,fatal,calls,observations:calls.map(([bytes,stream])=>invoke(d,bytes,stream))};
 });
}
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def stop_owned(process):
    # Use the already reviewed PID/start-identity group cleanup helper so
    # sandbox helpers cannot be abandoned after their root exits.
    spec = importlib.util.spec_from_file_location('fatal_probe_owned_monitor', ROOT / 'tools/monitor_process.py')
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
    worker = CORE + '\npostMessage(observeFatalStreams());\n'
    page = ('<!doctype html><meta charset="utf-8"><title>Owned TextDecoder interoperability probe</title>'
            '<h1>TextDecoder interoperability diagnostic</h1><script>' + CORE + r'''
const rows=observeFatalStreams();
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
        command = [str(binary), '--headless=new', '--no-first-run', '--no-default-browser-check',
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
        if payload.get('workerIssue') or any(len(payload.get(scope, [])) != 14 for scope in ['window', 'worker']):
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
    if hashlib.sha256(CORE.encode()).hexdigest() != '99fd8f80444872fe0a97400f91eed72d840db57ff5033809402903f28af5d19e':
        raise RuntimeError('Independent14-sequence fixture changed')
    OUT.mkdir(parents=True, exist_ok=False)
    build = json.loads(BUILD.read_text())
    if not build.get('passed') or build.get('compiledSourceManifestSHA256') != 'ffb74d059ed1ae954c665d78bcd4ad08fe2c2e3f5d6ec4e77d40e61032befb06' or build.get('sourceBaseCommit') != '963d4ddd72620b4978e950cc75298319615911a2':
        raise RuntimeError('Current native build/source manifest is not the reviewed candidate')
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
                                   'lume': a['observations'], 'chrome': b['observations']})
    prior_path=ROOT/'out/lume-20261029-cpu30/fatal-stream-interop-2/summary.json'
    prior=json.loads(prior_path.read_text())
    if complete:
        for scope in ['window','worker']:
            prior_cases=prior['browsers'][1]['actual'][scope]
            current_cases=results[1]['actual'][scope]
            if prior_cases!=current_cases:
                raise RuntimeError('Qualified Chrome observations changed despite unchanged14 inputs; investigate before parity interpretation')
    receipt = {'taskId': '20261029-cpu30', 'complete': complete, 'browsers': results,
               'fixtureCoreSHA256': hashlib.sha256(CORE.encode()).hexdigest(),
               'priorObservationReceipt':str(prior_path.relative_to(ROOT)),'priorObservationSHA256':sha(prior_path),
               'unchanged14InputsAndChromeObservations':complete,
               'comparisons': comparison,
               'scope': 'Fourteen observational call sequences (four additional partial-scalar recovery controls) in window and worker for exact Chrome/Lume executables. This does not modify or replace pinned WPT assertions or award conformance.',
               'spec': 'https://encoding.spec.whatwg.org/#dom-textdecoder-decode',
               'relatedUnsettledIssue': 'https://github.com/whatwg/encoding/issues/359'}
    (OUT / 'summary.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'complete': complete, 'receipt': str((OUT/'summary.json').relative_to(ROOT)),
                      'same': sum(row['same'] for row in comparison), 'total': len(comparison),
                      'comparisons': comparison}, indent=2))
    return 0 if complete else 1


if __name__ == '__main__':
    raise SystemExit(main())
