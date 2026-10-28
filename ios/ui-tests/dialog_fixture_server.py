#!/usr/bin/env python3
"""One loopback HTML dialog fixture; native XCTest supplies every UI action."""
from pathlib import Path
import argparse,hashlib,json,os,signal,threading
from http.server import BaseHTTPRequestHandler
from urllib.parse import parse_qs,urlparse
from fixture_server import EventStore,LoopbackHTTPServer

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
 assert args.output.is_dir()
 html=Path(__file__).with_name('dialog_fixture.html').read_bytes();store=EventStore();requests={};lock=threading.Lock()
 def save():
  with store.lock: data=json.loads(json.dumps(store.runs))
  with lock:
   temp=args.output/'events.tmp';temp.write_text(json.dumps(data,indent=2)+'\n');temp.replace(args.output/'events.json')
 class Handler(BaseHTTPRequestHandler):
  def reply(self,status,body,content_type='application/json'):
   self.send_response(status);self.send_header('Content-Type',content_type);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  def do_GET(self):
   parsed=urlparse(self.path)
   try:
    run=parse_qs(parsed.query).get('run',[''])[0]
    if parsed.path=='/health':self.reply(200,b'{"ok":true}');return
    store.validate(run)
    if parsed.path=='/dialog':
     with lock:requests[run]=requests.get(run,0)+1
     self.reply(200,html,'text/html; charset=utf-8')
    elif parsed.path=='/state':
     data=store.state(run)
     with lock:data['documentRequests']=requests.get(run,0)
     self.reply(200,json.dumps(data).encode())
    else:self.reply(404,b'{}')
   except ValueError:self.reply(400,b'{"error":"invalid run"}')
  def do_POST(self):
   try:
    length=int(self.headers.get('Content-Length','0'))
    if urlparse(self.path).path!='/event' or not 0<length<=8192:self.reply(400,b'{}');return
    event=json.loads(self.rfile.read(length));store.record(event);save();self.reply(200,b'{"ok":true}')
   except (ValueError,TypeError,AttributeError):self.reply(400,b'{"error":"invalid event"}')
  def log_message(self,*args):pass
 server=LoopbackHTTPServer(('127.0.0.1',0),Handler)
 def finish(*unused):threading.Thread(target=server.shutdown,daemon=True).start()
 signal.signal(signal.SIGTERM,finish);signal.signal(signal.SIGINT,finish)
 ready={'pid':os.getpid(),'host':'127.0.0.1','port':server.server_port,'fixtureSha256':hashlib.sha256(html).hexdigest()}
 temp=args.output/'server-ready.tmp';temp.write_text(json.dumps(ready)+'\n');temp.replace(args.output/'server-ready.json')
 try:server.serve_forever()
 finally:
  server.server_close();save()
  (args.output/'document-requests.json').write_text(json.dumps(requests)+'\n')
if __name__=='__main__':main()
