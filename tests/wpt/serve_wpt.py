#!/usr/bin/env python3
"""wptserve-backed server for Lumen's headless WPT runner.

Like serve.py, but runs WPT's own Python handlers (*.py) through wptserve, so
XHR/fetch tests get real server behaviour. Needs tools/ in the WPT checkout.
usage: serve_wpt.py [WPT_ROOT] [PORT]
"""
import os, sys, time

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/src/wpt'))
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8799
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import localpaths  # noqa: F401  (adds tools/third_party to sys.path)
from wptserve import server, handlers, routes as wroutes
from wptserve.handlers import handler
from wptserve.stash import StashServer
import importlib.util
_spec = importlib.util.spec_from_file_location('lumen_simple_serve', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'serve.py'))
simple = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(simple)

@handler
def report(request, response):
    response.headers.set('Content-Type', 'text/javascript')
    response.headers.set('Content-Length', str(len(simple.REPORT)))
    return simple.REPORT

@handler
def wrapped(request, response):
    rel = request.url_parts.path[:-len('.html')] + '.js'
    path = ROOT + rel
    if not os.path.exists(path):
        response.status = 404
        return 'not found'
    body = simple.wrap(path, rel)
    response.headers.set('Content-Type', 'text/html; charset=utf-8')
    response.headers.set('Content-Length', str(len(body)))
    return body

routes = [('GET', '/resources/testharnessreport.js', report),
          ('GET', '*.any.html', wrapped), ('GET', '*.window.html', wrapped)] + wroutes.routes
config = {'browser_host': '127.0.0.1', 'domains': {'': {'': '127.0.0.1', 'www': '127.0.0.1', 'www1': '127.0.0.1', 'www2': '127.0.0.1'}},
          'ports': {'http': [PORT, PORT + 1], 'https': [PORT + 2]}, 'server_host': '127.0.0.1'}
if __name__ == '__main__':
    with StashServer(('127.0.0.1', 0), authkey=b'lumen'):
        httpd = server.WebTestHttpd(host='127.0.0.1', port=PORT, doc_root=ROOT, routes=routes, config=config)
        httpd.start()
        while True:
            time.sleep(3600)
