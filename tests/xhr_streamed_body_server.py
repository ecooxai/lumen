#!/usr/bin/env python3
"""Run the real native JS host against deterministic loopback HTTP framing.

No WPT transport, expectation adjustment, physical capture, or browser profile
is used. The host must finish normal teardown with an exact PASS attribute and
exit zero; its real sanitizer exit status is never replaced by output matching.
"""
from __future__ import annotations
import argparse
import collections
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import urllib.parse


def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',required=True,type=Path)
    parser.add_argument('--fixture',type=Path,default=Path(__file__).with_name('js')/'xhr-streamed-body.html')
    args=parser.parse_args()
    fixture=args.fixture.read_bytes()
    requests: collections.Counter[str]=collections.Counter()
    lock=threading.Lock()
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version='HTTP/1.1'
        def log_message(self,format: str,*args: object) -> None: pass
        def do_HEAD(self) -> None: self.respond(head=True)
        def do_GET(self) -> None: self.respond(head=False)
        def respond(self,*,head: bool) -> None:
            path=urllib.parse.urlsplit(self.path).path
            with lock: requests[self.command+' '+path]+=1
            if path=='/fixture.html':
                self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(fixture)));self.send_header('Connection','close');self.end_headers()
                if not head: self.wfile.write(fixture)
                self.close_connection=True;return
            mode=path.removeprefix('/body/')
            bodies={'chunked':b'streamed text','length':b'streamed text','close':b'streamed text','status':b'unauthorized body','empty':b'','no-content':b'','bom':b'\xef\xbb\xbftext after BOM','utf16':b'\xff\xfeh\x00e\x00l\x00l\x00o\x00','binary':bytes([0,255,1,128]),'json':b'{"value":42,"message":"ok"}','xml':b'<root>ok</root>','slow':b'streamed text'}
            if mode not in bodies: self.send_error(404);return
            body=bodies[mode];status=401 if mode=='status' else 204 if mode=='no-content' else 200
            self.send_response(status)
            self.send_header('Content-Type',{'json':'application/json','xml':'application/xml','binary':'application/octet-stream'}.get(mode,'text/plain; charset=utf-8'))
            self.send_header('X-Mode',mode);self.send_header('Cache-Control','no-store');self.send_header('Connection','close')
            chunked=mode not in ['close','length','no-content']
            if mode=='length':self.send_header('Content-Length',str(len(body)))
            elif chunked:self.send_header('Transfer-Encoding','chunked')
            self.end_headers();self.wfile.flush();self.close_connection=True
            if head or status==204:return
            try:
                if mode=='slow':time.sleep(.15)
                if chunked:
                    # Each UTF BOM byte is a separately framed network chunk.
                    for byte in body:
                        self.wfile.write(b'1\r\n'+bytes([byte])+b'\r\n');self.wfile.flush()
                    self.wfile.write(b'0\r\n\r\n');self.wfile.flush()
                else:self.wfile.write(body);self.wfile.flush()
            except (BrokenPipeError,ConnectionResetError):
                if mode!='slow':raise
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    server.daemon_threads=False
    thread=threading.Thread(target=server.serve_forever,name='xhr-owned-http',daemon=False);thread.start()
    env=dict(os.environ)
    for key in ['LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_PRE_FILE']:env.pop(key,None)
    env.update(LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-xhr-streamed-body',JSRUN_EXPECT_ATTR_VALUE='PASS')
    code=1
    try:
        completed=subprocess.run([str(args.binary.resolve()),f'http://127.0.0.1:{server.server_port}/fixture.html','12000'],env=env,timeout=20)
        code=completed.returncode
    except subprocess.TimeoutExpired:
        print('xhr-streamed-body: native fixture exceeded its completion deadline',file=sys.stderr)
    finally:
        server.shutdown();thread.join();server.server_close()
    required={'GET /fixture.html','GET /body/chunked','GET /body/length','GET /body/close','GET /body/status','GET /body/empty','GET /body/no-content','GET /body/bom','GET /body/utf16','GET /body/binary','GET /body/json','GET /body/xml','GET /body/slow','HEAD /body/chunked'}
    missing=sorted(required-set(requests))
    print('XHR_HTTP_RECEIPT '+json.dumps({'actual_native_exit':code,'requests':dict(requests),'missing_required_requests':missing}),flush=True)
    return code if code else (1 if missing else 0)

if __name__=='__main__':raise SystemExit(main())
