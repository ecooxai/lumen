#!/usr/bin/env python3
"""Frozen nine-case real data-frame parser-order and inline-handler CSP test.

Original HTML bytes are unchanged from Chrome9/native7 security evidence.
This independent regression is not counted as upstream WPT.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, http.server, importlib.util, json, os, tempfile, threading, time
ROOT=Path(__file__).resolve().parents[1]
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
utc=lambda:datetime.now(timezone.utc).isoformat()
PAGE=(ROOT/'tests/fixtures/parser-handler-policy.html').read_bytes()
if hashlib.sha256(PAGE).hexdigest()!='c20ae4a5082eb6afb1053e986f03d081f04eefd087b266ccab46493b288e84e0':raise RuntimeError('Original nine-case fixture changed')
MARKER='PARSER_POLICY_DONE '
CASE_MARKER='PARSER_POLICY_CASE '
NAMES=[mode+' / '+name for mode in ['none','nonce']for name in ['actual opaque data document loaded','script before meta executed exactly once','untrusted script after meta blocked','later inline event handler blocked']+(['nonce-authorized script after meta executed']if mode=='nonce'else[])]
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(PAGE)));self.end_headers();self.wfile.write(PAGE)
 def log_message(self,*_):pass

def main():
 parser=argparse.ArgumentParser(description=__doc__);choice=parser.add_mutually_exclusive_group(required=True);choice.add_argument('--binary',type=Path);choice.add_argument('--chromium',type=Path);parser.add_argument('--runtime-environment',type=Path);parser.add_argument('--source-manifest',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
 if args.output.exists():raise ValueError('Preserve prior receipt')
 args.output.parent.mkdir(parents=True,exist_ok=True)
 path=ROOT/'tests/test_blob_range_whitespace.py';spec=importlib.util.spec_from_file_location('realm_fixture_transport',path);transport=importlib.util.module_from_spec(spec);spec.loader.exec_module(transport);transport.MARKER=MARKER
 common=transport.common_module();error=common.monitor_module().pid_namespace_error()
 if error:raise RuntimeError(error)
 binary=(args.binary or args.chromium).resolve(strict=True);server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);other=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);server.other_port=other.server_port;other.other_port=server.server_port;thread=threading.Thread(target=server.serve_forever,daemon=True);other_thread=threading.Thread(target=other.serve_forever,daemon=True);thread.start();other_thread.start();began=time.monotonic()
 record={'kind':'frozen real parser-order inline-handler CSP regression','officialWPT':False,'nativeLumenExecuted':bool(args.binary),'chromiumControlExecuted':bool(args.chromium),'binary':str(binary),'binarySHA256':sha(binary),'fixtureSHA256':sha(__file__),'transportSHA256':sha(path),'htmlSHA256':hashlib.sha256(PAGE).hexdigest(),'startedUTC':utc(),'rows':[],'infrastructureErrors':[]}
 if args.source_manifest:record['sourceManifestSHA256']=sha(args.source_manifest)
 try:
  url='http://127.0.0.1:'+str(server.server_port)+'/'
  runtime=json.loads(args.runtime_environment.read_text())if args.runtime_environment else{}
  result=transport.run_native(common,binary,url,args.output.with_suffix(''),runtime)if args.binary else transport.run_chromium(common,binary,url,args.output.with_suffix(''))
  for name in['metrics','cleanup','naturalExitCode','browserVersion']:
   if name in result:record[name]=result[name]
  summaries=[json.loads(line.split(MARKER,1)[1])for line in result['stdout'].splitlines()if MARKER in line]
  rows=[json.loads(line.split(CASE_MARKER,1)[1])for line in result['stdout'].splitlines()if CASE_MARKER in line];record['rows']=rows
  if len(summaries)!=1 or summaries[0].get('count')!=len(NAMES) or len(rows)!=len(NAMES) or [r['index']for r in rows]!=list(range(len(NAMES)))or[r['name']for r in rows]!=NAMES:raise RuntimeError('Fixture assertion inventory differs or runtime did not complete')
  if record.get('metrics',{}).get('errors')or record.get('cleanup',{}).get('error')or record.get('cleanup',{}).get('errors'):raise RuntimeError('Unqualified measurement or cleanup')
  if sha(binary)!=record['binarySHA256']or sha(path)!=record['transportSHA256']:raise RuntimeError('Fixture execution identity changed')
 except Exception as error:record['infrastructureErrors'].append(type(error).__name__+': '+str(error))
 finally:server.shutdown();server.server_close();thread.join(timeout=5);other.shutdown();other.server_close();other_thread.join(timeout=5)
 record.update(finishedUTC=utc(),durationSeconds=time.monotonic()-began,total=len(NAMES),recorded=len(record['rows']),passed=sum(r['ok']for r in record['rows']),failed=sum(not r['ok']for r in record['rows']),unrun=len(NAMES)-len(record['rows']))
 fd,temporary=tempfile.mkstemp(prefix='.parser-policy-',dir=args.output.parent)
 with os.fdopen(fd,'w')as stream:json.dump(record,stream,indent=2);stream.write('\n');stream.flush();os.fsync(stream.fileno())
 os.replace(temporary,args.output)
 print(json.dumps({k:record[k]for k in['kind','binarySHA256','total','recorded','passed','failed','unrun','infrastructureErrors','durationSeconds']}));raise SystemExit(0 if record['passed']==len(NAMES) and not record['infrastructureErrors']else 1)
if __name__=='__main__':main()
