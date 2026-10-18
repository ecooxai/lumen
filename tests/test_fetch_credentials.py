#!/usr/bin/env python3
"""Run browser session regressions against two isolated loopback HTTP origins.

Usage: python3 tests/test_fetch_credentials.py --jsrun build/jsrun
No accounts or external network services are used. CPU/RSS samples and the
browser log are saved with the supplied --output prefix.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]


class Fixture:
    def __init__(self):
        self.records = {}
        self.preflights = {}
        self.release = {}
        self.lock = threading.Lock()
        self.servers = [ThreadingHTTPServer(('127.0.0.1', 0), Handler) for _ in range(2)]
        for server in self.servers:
            server.fixture = self
        self.origins = [f'http://127.0.0.1:{server.server_port}' for server in self.servers]

    def __enter__(self):
        for server in self.servers:
            threading.Thread(target=server.serve_forever, daemon=True).start()
        return self

    def __exit__(self, *args):
        for server in self.servers:
            server.shutdown()
            server.server_close()


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def response(self, body, status=200, headers=()):
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Access-Control-Allow-Origin', self.headers.get('Origin', self.server.fixture.origins[0]))
        self.send_header('Access-Control-Allow-Credentials', 'true')
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_OPTIONS(self):
        query = parse_qs(urlsplit(self.path).query)
        ticket = query.get('case', [''])[0]
        with self.server.fixture.lock:
            self.server.fixture.preflights[ticket] = self.headers.get('Cookie', '')
        self.response(b'', 204, [('Access-Control-Allow-Methods', 'GET, PUT'),
                                 ('Access-Control-Allow-Headers', 'X-Lumen-Test'),
                                 ('Set-Cookie', 'lumen_preflight_response=ignored; Path=/; SameSite=Lax')])

    def do_PUT(self):
        self.do_GET()

    def do_GET(self):
        fixture = self.server.fixture
        parts = urlsplit(self.path)
        query = parse_qs(parts.query)
        ticket = query.get('case', [''])[0]
        length = int(self.headers.get('Content-Length', '0'))
        if length:
            self.rfile.read(length)
        if parts.path == '/test':
            page = (ROOT / 'tests/js/fetch-credentials.html').read_text().replace('@OTHER_ORIGIN@', fixture.origins[1]).encode()
            return self.response(page, headers=[('Content-Type', 'text/html; charset=utf-8')])
        with fixture.lock:
            fixture.records.setdefault(ticket, []).append({'cookie': self.headers.get('Cookie', ''), 'path': parts.path})
        if parts.path == '/redirect':
            return self.response(b'', 302, [('Location', query['to'][0])])
        if parts.path == '/release':
            with fixture.lock:
                fixture.release.setdefault(ticket, threading.Event()).set()
            return self.response(b'ok')
        if parts.path == '/stream':
            with fixture.lock:
                release = fixture.release.setdefault(ticket, threading.Event())
            self.send_response(200)
            self.send_header('Content-Length', '8')
            self.send_header('Access-Control-Allow-Origin', fixture.origins[0])
            self.send_header('Access-Control-Allow-Credentials', 'true')
            self.end_headers()
            self.wfile.flush()
            return self.wfile.write(b'released' if release.wait(3) else b'timedout')
        if parts.path == '/echo':
            headers = [('Content-Type', 'application/json')]
            if 'set' in query:
                headers.append(('Set-Cookie', query['set'][0] + '; Path=/; SameSite=Lax'))
            with fixture.lock:
                data = {'cookie': self.headers.get('Cookie', ''), 'preflightCookie': fixture.preflights.get(ticket), 'hops': list(fixture.records[ticket])}
            return self.response(json.dumps(data).encode(), headers=headers)
        self.response(b'not found', 404)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jsrun', type=Path, default=ROOT / 'build/jsrun')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/apple-compat/fetch-credentials')
    args = parser.parse_args()
    jsrun = args.jsrun.resolve()
    if not jsrun.is_file():
        parser.error(f'jsrun executable not found: {jsrun}')
    args.output = args.output.resolve()
    environment = dict(os.environ, JSRUN_UNTIL_ATTR='data-fetch-done', LUMEN_CONSOLE='1')
    with Fixture() as fixture:
        result = subprocess.run([sys.executable, str(ROOT / 'tools/monitor_process.py'),
                                 '--output', str(args.output), '--duration', '30', '--settle', '0', '--',
                                 str(jsrun), fixture.origins[0] + '/test', '20000'], env=environment)
    log = args.output.with_suffix('.log').read_text(errors='replace')
    for line in log.splitlines():
        if 'FETCH-' in line:
            print(line)
    done = re.search(r'FETCH-DONE passed=(\d+) failed=(\d+)', log)
    if result.returncode or not done or int(done[1]) < 50 or int(done[2]):
        print(f'Fetch session regression failed; inspect {args.output.with_suffix(".log")}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
