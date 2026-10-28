#!/usr/bin/env python3
"""Native document load waits for connected descendant frame navigations.

The delayed fixture exercises application and jsrun loading paths independently
of the fixed upstream WPT corpus. Removed frames must not retain a load blocker.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

PAGE = r'''<!doctype html><html><head><script>
const order={dcl:null,complete:0,parentLoads:0,xmlLoads:0,xhtmlLoads:0,nestedLoads:0,nestedWindowLoads:0,removedLoads:0};
const checks=[];
function check(name,fn){try{checks.push({name,passed:fn()===true});}catch(e){checks.push({name,passed:false,error:e.name+': '+e.message});}}
function finish(){
 for(let index=0;index<checks.length;index++)console.log('FRAME_LOAD_CHECK '+JSON.stringify({index,...checks[index]}));
 console.log('FRAME_LOAD_RESULTS '+JSON.stringify({passed:checks.filter(x=>x.passed).length,total:checks.length,order}));
 document.documentElement.setAttribute('data-load-fixture-done','1');
}
document.addEventListener('readystatechange',()=>{if(document.readyState==='complete')order.complete++;});
document.addEventListener('DOMContentLoaded',()=>{order.dcl=document.readyState;document.getElementById('removed').remove();});
window.addEventListener('load',()=>{
 order.parentLoads++;
 if(order.parentLoads!==1)return;
 const x=document.getElementById('xml'),h=document.getElementById('xhtml'),n=document.getElementById('nested');
 check('DOMContentLoaded stays interactive before delayed frames complete',()=>order.dcl==='interactive');
 check('parent load exposes the complete readyState',()=>document.readyState==='complete');
 check('parent load observes the fully navigated XML document',()=>x.contentDocument.documentElement.textContent==='Dummy XML document');
 check('parent load observes the fully navigated XHTML document',()=>h.contentDocument.documentElement.textContent==='Dummy XHTML document');
 check('XML element load precedes parent window load exactly once',()=>order.xmlLoads===1);
 check('XHTML element load precedes parent window load exactly once',()=>order.xhtmlLoads===1);
 check('parent load waits through a nested frame navigation',()=>n.contentDocument.getElementById('grand').contentDocument.documentElement.textContent==='Grandchild');
 check('nested element load waits for its descendant and fires once',()=>order.nestedLoads===1);
 check('nested window load precedes its parent window load',()=>order.nestedWindowLoads===1);
 check('removed pending frames have no browsing context association',()=>removedFrame.contentWindow===null&&removedFrame.contentDocument===null);
 const dynamic=document.createElement('iframe');
 dynamic.onload=()=>{
  check('a frame added after parent load still completes normally',()=>dynamic.contentDocument.documentElement.textContent==='Dynamic');
  setTimeout(()=>{
   check('dynamic frame completion never redispatches parent window load',()=>order.parentLoads===1);
   check('complete readystatechange fires only once for this document',()=>order.complete===1);
   check('a retired navigation never dispatches an iframe load',()=>order.removedLoads===0);
   clearTimeout(watchdog);finish();
  },50);
 };
 dynamic.src='/dynamic.xml';document.body.appendChild(dynamic);
});
const watchdog=setTimeout(()=>{console.log('FRAME_LOAD_FATAL '+JSON.stringify(order));document.documentElement.setAttribute('data-load-fixture-done','1');},7000);
</script></head><body>
<iframe id="xml" src="/delayed.xml"></iframe>
<iframe id="xhtml" src="/delayed.xhtml"></iframe>
<iframe id="nested" src="/nested.html"></iframe>
<iframe id="removed" src="/retired.xml"></iframe>
<script>
const removedFrame=document.getElementById('removed');
document.getElementById('xml').onload=()=>order.xmlLoads++;
document.getElementById('xhtml').onload=()=>order.xhtmlLoads++;
document.getElementById('nested').onload=()=>order.nestedLoads++;
removedFrame.onload=()=>order.removedLoads++;
void removedFrame.contentWindow;
</script></body></html>'''

NESTED = b'''<!doctype html><html><body><iframe id="grand" src="/grand.xml"></iframe><script>window.addEventListener('load',()=>parent.order.nestedWindowLoads++);</script></body></html>'''
# Use a real global property for cross-realm access, not a lexical binding.
PAGE = PAGE.replace('const order={', 'globalThis.order={')
RESPONSES = {
    '/': ('text/html; charset=utf-8', PAGE.encode(), 0),
    '/delayed.xml': ('application/xml', b'<root>Dummy XML document</root>', .18),
    '/delayed.xhtml': ('application/xhtml+xml', b'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Dummy XHTML document</title></head><body/></html>', .24),
    '/nested.html': ('text/html; charset=utf-8', NESTED, .1),
    '/grand.xml': ('application/xml', b'<root>Grandchild</root>', .35),
    '/retired.xml': ('application/xml', b'<root>Retired</root>', 3),
    '/dynamic.xml': ('application/xml', b'<root>Dynamic</root>', .05),
}

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        response = RESPONSES.get(self.path)
        if response is None:
            self.send_error(404)
            return
        content_type, body, delay = response
        time.sleep(delay)
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, _format, *_args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--runtime', choices=['jsrun', 'application'], default='jsrun')
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lumen-frame-load-') as directory:
            env = dict(os.environ, LUMEN_PROFILE_DIR=directory, LUMEN_CONSOLE='1',
                       JSRUN_UNTIL_ATTR='data-load-fixture-done')
            command = [str(binary), f'http://127.0.0.1:{server.server_port}/']
            if args.runtime == 'jsrun':
                command.append('9000')
            else:
                env.update(LUMEN_SHOT=str(Path(directory) / 'load-order.png'),
                           LUMEN_SHOT_MS='8000', LUMEN_WINDOW='800x600')
            result = subprocess.run(command, text=True, capture_output=True, env=env, timeout=15)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        transcript = result.stdout + result.stderr
        summaries = [json.loads(line.split('FRAME_LOAD_RESULTS ', 1)[1])
                     for line in transcript.splitlines() if 'FRAME_LOAD_RESULTS ' in line]
        checks = [json.loads(line.split('FRAME_LOAD_CHECK ', 1)[1])
                  for line in transcript.splitlines() if 'FRAME_LOAD_CHECK ' in line]
        if result.returncode or len(summaries) != 1 or len(checks) != 14 or sorted(x['index'] for x in checks) != list(range(14)):
            print('FRAME_LOAD_INFRASTRUCTURE missing, duplicate, or failed process results')
            return 2
        report = summaries[0]
        if report['total'] != 14 or report['passed'] != sum(x['passed'] is True for x in checks):
            print('FRAME_LOAD_INFRASTRUCTURE inconsistent summary')
            return 2
        print(f"RESULTS frame-load-order {args.runtime} {'PASS' if report['passed']==14 else 'FAIL'} ({report['passed']}/14 checks)")
        return 0 if report['passed'] == 14 else 1
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)

if __name__ == '__main__':
    raise SystemExit(main())
