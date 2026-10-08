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
HOST = os.environ.get('WPT_HOST', 'web-platform.test')  # resolve via LUMEN_HOST_MAP=web-platform.test=127.0.0.1
if __name__ == '__main__':
    import logging
    from wptserve.config import ConfigBuilder
    from collections import defaultdict

    class Builder(ConfigBuilder):
        def _get_ports(self, data):  # keep https/wss listed for templates even though only http is served
            return defaultdict(list, {k: list(v) for k, v in data['ports'].items()})

    config = Builder(logging.getLogger('wpt'), subdomains={'www', 'www1', 'www2', '\u5929\u6c17\u306e\u826f\u3044\u65e5', '\u00e9l\u00e8ve'},
                     not_subdomains={'nonexistent'}, browser_host=HOST, alternate_hosts={'alt': 'not-' + HOST}, server_host='127.0.0.1',
                     ports={'http': [PORT, PORT + 1], 'https': [PORT + 2, PORT + 3], 'http-private': [PORT + 4], 'http-public': [PORT + 5], 'https-private': [PORT + 6], 'https-public': [PORT + 7], 'ws': [PORT + 8], 'wss': [PORT + 9], 'h2': [PORT + 10], 'webtransport-h3': [PORT + 11]}, doc_root=ROOT).__enter__()
    with StashServer(('127.0.0.1', 0), authkey=b'lumen'):
        for port in (PORT, PORT + 1):
            server.WebTestHttpd(host='127.0.0.1', port=port, doc_root=ROOT, routes=routes, config=config).start()
        while True:
            time.sleep(3600)
