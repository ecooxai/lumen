#!/usr/bin/env python3
import argparse,datetime,hashlib,http.server,importlib.util,json,sys,threading,time,urllib.request
from pathlib import Path

def load_module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);mod=importlib.util.module_from_spec(spec);sys.modules[name]=mod;spec.loader.exec_module(mod);return mod

def main():
 p=argparse.ArgumentParser();p.add_argument('--binary',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
 root=Path.cwd();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);binary=a.binary.resolve();digest=hashlib.sha256(binary.read_bytes()).hexdigest()
 assert digest=='4f71e9dbe725fec1110977b6a6dd0708fb32fbde9c905b4b461cef880ac08288',digest
 base=root/'output/session-10290244/frame-microtask-candidate'
 frame=load_module('lume_frame_fixture',base/'test_frame_microtask_realm.py');storage=load_module('lume_storage_fixture',base/'test_storage_key_unsigned_long.py')
 fixtures={'/frame.html':(frame.HTML,35,'data-frame-microtask-done'),'/storage.html':(storage.HTML,66,'data-storage-key-done')}
 class Handler(http.server.BaseHTTPRequestHandler):
  def do_GET(self):
   item=fixtures.get(self.path.split('?',1)[0])
   if item is None:self.send_error(404);return
   data=item[0].encode();self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
  def log_message(self,*_):pass
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 runner=load_module('lume_cdp_runner',root/'tests/wpt/full_runner.py');browser=None;started=time.monotonic()
 receipt={'started_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'reference_role':'narrow_diagnostic_only_general_reference_quarantined','binary':str(binary),'binary_sha256':digest,'fixtures':[],'issues':[]}
 try:
  browser=runner.Chromium(binary,out/'chrome',[],headless=True);receipt['version']=browser.version;receipt['pid']=browser.process.pid;receipt['argv']=browser.process.args
  for path,(html,expected,attr) in fixtures.items():
   context=None;page=None;entry={'path':path,'expected':expected,'fixture_sha256':hashlib.sha256(html.encode()).hexdigest()};receipt['fixtures'].append(entry)
   try:
    context=browser.browser.call('Target.createBrowserContext')['browserContextId'];target=browser.browser.call('Target.createTarget',{'url':'about:blank','browserContextId':context})['targetId']
    info=None;deadline=time.monotonic()+5
    while time.monotonic()<deadline and info is None:
     with urllib.request.urlopen('http://127.0.0.1:%d/json/list'%browser.port,timeout=2) as response:targets=json.load(response)
     info=next((x for x in targets if x.get('id')==target),None)
     if info is None:time.sleep(.05)
    if info is None:raise RuntimeError('created page absent from target list')
    page=runner.CDP(info['webSocketDebuggerUrl']);page.call('Runtime.enable');page.call('Page.enable')
    page.call('Page.navigate',{'url':'http://127.0.0.1:%d%s'%(server.server_port,path)})
    deadline=time.monotonic()+10;complete=False
    while time.monotonic()<deadline:
     r=page.call('Runtime.evaluate',{'expression':'!!document.documentElement && document.documentElement.getAttribute('+json.dumps(attr)+') === "1"','returnByValue':True},timeout=3)
     if r.get('result',{}).get('value') is True:complete=True;break
     time.sleep(.05)
    entry['complete']=complete
    r=page.call('Runtime.evaluate',{'expression':'({rows:typeof results==="undefined"?[]:results,url:location.href,readyState:document.readyState})','returnByValue':True},timeout=3)
    if 'exceptionDetails' in r:entry['evaluation_exception']=r['exceptionDetails']
    entry.update(r.get('result',{}).get('value') or {});rows=entry.get('rows',[]);entry['passed']=sum(x.get('ok') is True for x in rows);entry['total']=len(rows)
    entry['fixture_pass']=complete and len(rows)==expected and [x.get('i') for x in rows]==list(range(expected)) and entry['passed']==expected
   except Exception as exc:entry['error']=repr(exc);entry['fixture_pass']=False
   finally:
    if page is not None:page.close()
    if context is not None:
     try:browser.browser.call('Target.disposeBrowserContext',{'browserContextId':context})
     except Exception as exc:receipt['issues'].append('context cleanup: '+repr(exc))
 except Exception as exc:receipt['issues'].append(repr(exc))
 finally:
  if browser is not None:
   try:
    cleanup=browser.close();receipt['browser_cleanup']=cleanup
    if cleanup.get('errors'):receipt['issues'].append({'browser_cleanup':cleanup})
   except Exception as exc:receipt['issues'].append('browser cleanup: '+repr(exc))
  server.shutdown();server.server_close();thread.join(timeout=2)
 receipt['ended_at']=datetime.datetime.now(datetime.timezone.utc).isoformat();receipt['duration_seconds']=time.monotonic()-started;receipt['fixture_assertions_passed']=len(receipt['fixtures'])==2 and all(x.get('fixture_pass') for x in receipt['fixtures']) and not receipt['issues']
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
 return 0 if receipt['fixture_assertions_passed'] else 1
if __name__=='__main__':raise SystemExit(main())
