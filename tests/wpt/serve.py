#!/usr/bin/env python3
"""Minimal web-platform-tests server for Lumen's headless runner.

Serves a WPT checkout, swaps in a testharnessreport.js that prints results to
the console and wraps *.any.js / *.window.js tests into HTML like wptserve.
"""
import http.server, os, re, sys

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/src/wpt'))
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8765

REPORT = b"""
add_result_callback(function (t) {
  console.log('WPT-SUB ' + ['PASS', 'FAIL', 'TIMEOUT', 'NOTRUN', 'PRECONDITION_FAILED'][t.status] + ' ' + JSON.stringify(t.name) + (t.message ? ' ' + JSON.stringify(String(t.message).slice(0, 200)) : ''));
});
add_completion_callback(function (tests, st) {
  console.log('WPT-DONE harness=' + ['OK', 'ERROR', 'TIMEOUT', 'PRECONDITION_FAILED'][st.status] + (st.message ? ' ' + JSON.stringify(String(st.message).slice(0, 200)) : ''));
  document.documentElement.setAttribute('data-wpt-done', '1');
});
"""

def wrap(path, rel):
    src = open(path, encoding='utf-8', errors='replace').read()
    scripts = re.findall(r'^//\s*META:\s*script=(\S+)', src, re.M)
    title = re.findall(r'^//\s*META:\s*title=(.+)$', src, re.M)
    tags = ''.join('<script src="%s"></script>\n' % s for s in scripts)
    name = os.path.basename(rel)
    return ('<!doctype html><meta charset=utf-8><title>%s</title>\n'
            '<script src="/resources/testharness.js"></script>\n'
            '<script src="/resources/testharnessreport.js"></script>\n%s'
            '<div id=log></div>\n<script src="%s"></script>\n' % (title[0] if title else name, tags, name)).encode()

class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=ROOT, **k)
    def log_message(self, *a): pass
    def send_bytes(self, body, ctype):
        self.send_response(200); self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        p = self.path.split('?')[0]
        if p == '/resources/testharnessreport.js': return self.send_bytes(REPORT, 'text/javascript')
        m = re.match(r'(.*\.(?:any|window))\.html$', p)
        if m and os.path.exists(ROOT + m.group(1) + '.js'):
            return self.send_bytes(wrap(ROOT + m.group(1) + '.js', m.group(1) + '.js'), 'text/html; charset=utf-8')
        return super().do_GET()

http.server.ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
