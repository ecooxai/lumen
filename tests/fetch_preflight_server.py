#!/usr/bin/env python3
"""Two local origins for fetch-preflight-stream.html. No external requests.

Run this server, reverse Android tcp:18765 and tcp:18766 with adb, then open
http://127.0.0.1:18765/. JSON events on stdout include the browser's report.
Responses deliberately pause three seconds between the first and last chunk.
"""
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
import time


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *_args):
        pass

    def event(self, **fields):
        print(json.dumps(dict(time=time.monotonic(), port=self.server.server_port,
                              method=self.command, path=self.path, **fields)), flush=True)

    def headers_for(self, size, cors=True, status=200):
        self.send_response(status)
        self.send_header('Content-Length', str(size))
        self.send_header('Content-Type', 'text/plain; charset=utf-8')
        self.send_header('Cache-Control', 'no-store')
        if cors:
            self.send_header('Access-Control-Allow-Origin', 'http://127.0.0.1:18765')
            self.send_header('Access-Control-Allow-Credentials', 'true')
            self.send_header('Access-Control-Allow-Headers', 'X-Stream')
            self.send_header('Access-Control-Allow-Methods', 'POST')
        self.end_headers()

    def do_GET(self):
        self.event()
        page = {'/': 'fetch-preflight-stream.html', '/base-uri-cache': 'base-uri-cache.html', '/request-state': 'request-state.html', '/video-frames': 'video-frame-callback.html', '/media-events': 'media-events.html', '/blob-parts': 'blob-parts.html', '/resize-observer': 'resize-observer.html', '/user-agent': 'user-agent.html', '/positioned-drawer': 'positioned-drawer.html', '/player-overlay': 'player-overlay.html'}.get(self.path)
        if self.path == '/user-agent-headers':
            data = json.dumps(dict(self.headers)).encode()
            self.headers_for(len(data))
            self.wfile.write(data)
            return
        if self.path == '/video.mp4':
            data = (Path(__file__).parent / 'media' / 'test.mp4').read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', 'video/mp4')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if not page:
            self.headers_for(0, status=404)
            return
        data = (Path(__file__).parent / 'js' / page).read_bytes()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def do_OPTIONS(self):
        self.event(request_headers=self.headers.get('Access-Control-Request-Headers'))
        self.headers_for(0, cors=self.path != '/deny-preflight', status=204)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        if self.path == '/report':
            self.event(results=json.loads(body))
            self.headers_for(0)
            return
        self.event(body=body.decode())
        if self.path != '/stream':
            self.headers_for(0, cors=False)
            return
        self.headers_for(11)
        self.wfile.write(b'first\n')
        self.wfile.flush()
        time.sleep(3)
        self.wfile.write(b'last\n')
        self.wfile.flush()
        self.event(complete=True)


if __name__ == '__main__':
    servers = [ThreadingHTTPServer(('127.0.0.1', port), Handler) for port in (18765, 18766)]
    for server in servers:
        threading.Thread(target=server.serve_forever, daemon=True).start()
    print(json.dumps({'ready': True}), flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
