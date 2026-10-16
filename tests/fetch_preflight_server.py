#!/usr/bin/env python3
"""Two local origins for fetch-preflight-stream.html. No external requests.

Run this server, reverse Android tcp:18765 and tcp:18766 with adb, then open
http://127.0.0.1:18765/. JSON events on stdout include the browser's report.
Responses deliberately pause three seconds between the first and last chunk.
"""
import json
import struct
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
import time


def thumbnail_png():
    """Small deterministic RGB thumbnail for scaling/scrolling comparisons."""
    width, height = 160, 90
    raw = b''.join(b'\0' + bytes(channel for x in range(width) for channel in
                   (x * 255 // (width - 1), y * 255 // (height - 1),
                    40 if (x // 10 + y // 10) % 2 else 210)) for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


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
        if self.path in ('/wasm-browser', '/wasm-worker', '/wasm-streaming', '/wasm-page-wakeup', '/wasm-clone'):
            data = (Path(__file__).parent / 'js' / (self.path[1:] + '.html')).read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        if self.path in ('/add.wasm', '/slow.wasm', '/truncated.wasm'):
            data = bytes([0,97,115,109,1,0,0,0,1,7,1,96,2,127,127,1,127,
                          3,2,1,0,7,7,1,3,97,100,100,0,0,10,9,1,7,0,32,0,32,1,106,11])
            self.send_response(200)
            self.send_header('Content-Type', 'application/wasm')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            if self.path == '/add.wasm':
                self.wfile.write(data)
            else:
                self.wfile.write(data[:30])
                self.wfile.flush()
                self.event(first_chunk=True)
                if self.path == '/truncated.wasm':
                    self.close_connection = True
                    return
                time.sleep(0.8)
                try:
                    self.wfile.write(data[30:])
                    self.wfile.flush()
                    self.event(last_chunk=True)
                except (BrokenPipeError, ConnectionResetError):
                    self.event(canceled=True)
            return
        page = {'/': 'fetch-preflight-stream.html', '/base-uri-cache': 'base-uri-cache.html', '/request-state': 'request-state.html', '/video-frames': 'video-frame-callback.html', '/media-events': 'media-events.html', '/blob-parts': 'blob-parts.html', '/resize-observer': 'resize-observer.html', '/user-agent': 'user-agent.html', '/positioned-drawer': 'positioned-drawer.html', '/player-overlay': 'player-overlay.html', '/intersection-observer': 'intersection-observer.html', '/event-wait': 'event-wait.html', '/opacity-scroll': 'opacity-scroll.html', '/thumbnail-scroll': 'thumbnail-scroll.html', '/native-touch': 'native-touch.html'}.get(self.path)
        page = {'/touch-overlay-removal': 'touch-overlay-removal.html', '/page-zoom': 'page-zoom.html', '/root-scroll': 'root-scroll.html',
                '/root-scroll-frame.html': 'root-scroll-frame.html'}.get(self.path, page)
        if self.path == '/user-agent-headers':
            data = json.dumps(dict(self.headers)).encode()
            self.headers_for(len(data))
            self.wfile.write(data)
            return
        if self.path == '/thumbnail.png':
            data = thumbnail_png()
            self.send_response(200)
            self.send_header('Content-Type', 'image/png')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
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
