#!/usr/bin/env python3
"""Preserve the exact reference browser's connect-event metadata discrepancy."""
import argparse
import datetime
import hashlib
import http.server
import json
from pathlib import Path
import tempfile
import threading
import time

from test_worker_response_headers import chrome_control


WORKER = b"""let count=0;onconnect=e=>e.ports[0].postMessage({connection:++count,
data:e.data,dataType:typeof e.data,origin:e.origin,originType:typeof e.origin,
sourceIsPort:e.source===e.ports[0],portsLength:e.ports.length,trusted:e.isTrusted});"""
HTML = b"""<!doctype html><meta charset=utf-8><script>
const workers=[],rows=[];
function connect(){const w=new SharedWorker('/worker.js','connect-diagnostic');workers.push(w);
w.port.onmessage=e=>{rows.push(e.data);if(rows.length===1)connect();else{
console.log('SHARED_CONNECT_METADATA '+JSON.stringify(rows));for(const x of workers)x.port.close();}};}
connect();</script>"""


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body, mime = (WORKER, 'text/javascript') if self.path == '/worker.js' else (HTML, 'text/html')
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--chromium', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    started, start = datetime.datetime.now(datetime.timezone.utc).isoformat(), time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix='lumen-connect-control-') as profile:
            stdout, stderr, code, version = chrome_control(args.chromium.resolve(),
                'http://127.0.0.1:' + str(server.server_port) + '/', profile,
                completion_marker='SHARED_CONNECT_METADATA ', infrastructure_marker='SHARED_CONNECT_INFRA ')
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    data = [json.loads(line.split('SHARED_CONNECT_METADATA ', 1)[1]) for line in stdout.splitlines()
        if 'SHARED_CONNECT_METADATA ' in line]
    receipt = {'officialWPT': False, 'purpose': 'Diagnose actual reference behavior without changing the22-case oracle',
        'binary': str(args.chromium.resolve()), 'binarySHA256': hashlib.sha256(args.chromium.read_bytes()).hexdigest(),
        'browserVersion': version, 'fixtureSHA256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'normativeSource': 'https://html.spec.whatwg.org/multipage/workers.html#dom-sharedworker',
        'normativeExpectation': 'connect.data is the empty string; connect.origin defaults to the empty string',
        'started': started, 'durationSeconds': round(time.monotonic() - start, 6),
        'exitCode': code, 'metadata': data, 'stdout': stdout, 'stderr': stderr}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({key: value for key, value in receipt.items() if key not in ('stdout', 'stderr')}), flush=True)
    return 0 if code == 0 and len(data) == 1 and len(data[0]) == 2 else 1


if __name__ == '__main__':
    raise SystemExit(main())
