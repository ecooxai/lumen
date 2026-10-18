#!/usr/bin/env python3
"""Loopback-only UI fixture and event oracle; observes only the selected app binary."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import signal
from socketserver import TCPServer
import subprocess
import sys
import threading
import time
from urllib.parse import parse_qs, urlparse


class LoopbackHTTPServer(ThreadingHTTPServer):
    def server_bind(self):
        # The fixture binds a numeric loopback address; reverse DNS has no role
        # and can stall startup on simulator hosts with an unavailable resolver.
        TCPServer.server_bind(self)
        self.server_name = 'localhost'
        self.server_port = self.server_address[1]


class EventStore:
    def __init__(self):
        self.runs = {}
        self.lock = threading.Lock()

    @staticmethod
    def validate(run):
        if not isinstance(run, str) or not re.fullmatch(r'[A-Za-z0-9-]{1,64}', run):
            raise ValueError('invalid run identifier')

    def record(self, event):
        run = event.get('run')
        self.validate(run)
        with self.lock:
            state = self.runs.setdefault(run, {'events': [], 'clicks': []})
            event = dict(event, received=time.time())
            state['events'].append(event)
            state['events'] = state['events'][-128:]
            for key in ('width', 'height', 'scrollY', 'value'):
                if key in event:
                    state[key] = event[key]
            if event.get('type') == 'click':
                state['clicks'].append(event.get('target'))
                state['clicks'] = state['clicks'][-128:]
            if event.get('type') == 'ready':
                state['ready'] = True

    def state(self, run):
        self.validate(run)
        with self.lock:
            return json.loads(json.dumps(self.runs.get(run, {})))


def match_pids(raw, binary):
    found = []
    for line in raw.splitlines():
        fields = line.split(None, 3)
        if len(fields) == 4 and fields[3] == binary:
            try:
                found.append(int(fields[0]))
            except ValueError:
                pass
    return found


def observe_app(binary, output, stop):
    """Attach the repository's 1 Hz CPU/RSS recorder to every test app launch."""
    tool = Path(__file__).resolve().parents[2] / 'tools' / 'monitor_process.py'
    observers = {}
    while not stop.is_set():
        raw = subprocess.check_output(['ps', '-Aww', '-o', 'pid=,%cpu=,rss=,comm='], text=True)
        for pid in match_pids(raw, binary):
            if pid not in observers:
                log = (output / f'resource-{pid}.monitor.log').open('w')
                process = subprocess.Popen([sys.executable, str(tool), '--pid', str(pid), '--duration', '3600',
                    '--settle', '15', '--output', str(output / f'resource-{pid}')], stdout=log, stderr=subprocess.STDOUT)
                observers[pid] = (process, log)
        stop.wait(1)
    for process, log in observers.values():
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=3)
        log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8789)
    parser.add_argument('--app-binary', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    store = EventStore()
    html = Path(__file__).with_name('fixture.html').read_bytes()

    class Handler(BaseHTTPRequestHandler):
        def reply(self, status, body, content_type='application/json'):
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == '/fixture':
                self.reply(200, html, 'text/html; charset=utf-8')
            elif parsed.path == '/health':
                self.reply(200, b'{"ok":true}')
            elif parsed.path == '/state':
                try:
                    run = parse_qs(parsed.query).get('run', [''])[0]
                    self.reply(200, json.dumps(store.state(run)).encode())
                except ValueError:
                    self.reply(400, b'{"error":"invalid run"}')
            else:
                self.reply(404, b'{}')

        def do_POST(self):
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if urlparse(self.path).path != '/event' or not 0 < length <= 8192:
                    self.reply(400, b'{}')
                    return
                event = json.loads(self.rfile.read(length))
                store.record(event)
                (args.output / 'events.json').write_text(json.dumps(store.runs, indent=2) + '\n')
                self.reply(200, b'{"ok":true}')
            except (ValueError, TypeError, AttributeError):
                self.reply(400, b'{"error":"invalid event"}')

        def log_message(self, *args):
            pass

    stop = threading.Event()
    server = LoopbackHTTPServer(('127.0.0.1', args.port), Handler)
    monitor = threading.Thread(target=observe_app, args=(args.app_binary, args.output, stop), daemon=True)
    monitor.start()

    def finish(*unused):
        stop.set()
        threading.Thread(target=server.shutdown, daemon=True).start()

    signal.signal(signal.SIGTERM, finish)
    signal.signal(signal.SIGINT, finish)
    print(f'UI fixture ready at http://127.0.0.1:{args.port}/fixture', flush=True)
    try:
        server.serve_forever()
    finally:
        stop.set()
        server.server_close()
        monitor.join(timeout=6)


if __name__ == '__main__':
    main()
