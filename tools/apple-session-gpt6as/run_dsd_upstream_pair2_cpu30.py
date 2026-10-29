#!/usr/bin/env python3
"""Separate byte-pinned six-file DSD diagnostic; no frozen cohort mutation."""
from pathlib import Path
from datetime import datetime,timezone
from collections import Counter
import hashlib,http.server,importlib.util,json,os,subprocess,sys,threading,time,urllib.parse,urllib.request
ROOT=Path.cwd().resolve();OUT=Path('out/lume-20261029-cpu30/dsd-upstream-pair-2')
PIN='7f8d49b5a82606efc771f43d10aad5bce5ccbfeb';UP=Path('output/wpt-10290244/upstream')
SELECTED={
 'shadow-dom/declarative/declarative-shadow-dom-basic.html':'f4eb6d3e2a5459e08453f1ff25ffcbc9da0cfe696c76221d194f646532274bca',
 'shadow-dom/declarative/declarative-after-attachshadow.html':'567de552e82a32f8b7d698dd5e2b45b1cbbe474210f47e1397ff19a6e7319227',
 'shadow-dom/declarative/innerhtml-on-ordinary-template.html':'1693cd57a8b99e80e6600fc423dd40b97cf699120f995fd08a68bf90587bc116',
 'shadow-dom/declarative/declarative-shadow-dom-repeats.html':'3b5120ec28b5d94f51679a7c9e99711771fc41774c1f0ef6dff0757bcaa550ed',
 'shadow-dom/declarative/declarative-shadow-dom-write-to-iframe.html':'cd0ba36bbce56f8fcd800007e0df73451db6c38a46a76edea769ade1d8710ce2',
 'shadow-dom/declarative/declarative-parser-interaction.html':'957c6660ae31f816a2d4c2e3e0b6db853ce4d55a9d22e1c8d1691cbdd652d64d'}
BINARIES={
 'baseline':{'path':'out/lume-20261029-cpu30/native-default-1/lumen-3ad884713a4aa896','sha256':'3ad884713a4aa896dec1d26e20c5a85af5543499467edf9b7ecf424196c63bce'},
 'candidate':{'path':'out/lume-20261029-cpu30/native-dsd-2/lumen-c259da2597a6e064','sha256':'c259da2597a6e064764d7cec8c64edb5c935a99edad9c259f08dac7dbc5a3849'}}
CHROME_SHA='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4';QUAL_SHA='1e34a36c89d61da6314e975fb8fe8296e0bd2e07ac73cc3836861785fcac5452'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();read=lambda p:json.loads(Path(p).read_text());utc=lambda:datetime.now(timezone.utc).isoformat()
def require(ok,msg):
 if not ok:raise RuntimeError(msg)
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);sys.modules[name]=m;spec.loader.exec_module(m);return m
plan=read('out/lume-20261029-cpu30/compat-default-1/cohort/cohort.json')
for row in plan['inputs'].values():require(sha(row['path'])==row['sha256'],'Frozen adapter input differs: '+row['path'])
orchestrator=module('dsd_upstream_inputs',plan['inputs']['orchestrator']['path']);orchestrator.BASE=Path(plan['fixture']);frozen,categories,environment,hashes,runner=orchestrator.current_inputs();require(hashes==plan['inputs'],'Adapter fingerprint differs')
qualpath=Path('agent/evidence/102900614-006-chrome-reference.json');require(sha(qualpath)==QUAL_SHA,'Chrome qualification differs');qual=read(qualpath);require(qual['qualifiedForPairedMeasurement'] and qual['binarySHA256']==CHROME_SHA,'Chrome not qualified');chrome_path=Path(qual['binaryPath']);require(sha(chrome_path)==CHROME_SHA,'Chrome binary changed')
build=read('out/lume-20261029-cpu30/native-dsd-2/summary.json');require(build['passed'] and build['prior718ChecksPassed'] and build['supportedTargetsPass'] and build['allOriginalEightControlsPass'] and build['full37']['passed']==26 and build['full37']['failed']==11,'Candidate functional gate not passed')
for row in BINARIES.values():require(sha(row['path'])==row['sha256'],'Native executable differs')
reporter=Path(plan['inputs']['reporter']['path']).read_bytes();require(hashlib.sha256(reporter).hexdigest()==plan['inputs']['reporter']['sha256'],'Reporter changed')
require(reporter.decode()==runner.REPORTER,'Adapter reporter bytes differ')
OUT.mkdir(parents=True,exist_ok=False);(OUT/'reporter.js').write_bytes(reporter);began=time.monotonic();receipt={'startedUTC':utc(),'scope':'Independent six whole upstream DSD files at the existing official pin, outside the frozen 1515-variant schedule and outside its 279 recorded variants. No assertions filtered or rewritten. Basic file deliberately retains unsupported API/slot/IDL failures.','pin':PIN,'upstream':str(UP),'schedule':[{'path':p,'sha256':h} for p,h in SELECTED.items()],'signature':{'reporterSHA256':hashlib.sha256(reporter).hexdigest(),'reporterDelivery':'Unchanged project completion reporter at /resources/testharnessreport.js, identical for both native artifacts and qualified Chrome.','adapterInputs':hashes,'timeoutPerFileSeconds':10,'overallBudgetSeconds':105,'qualificationSHA256':QUAL_SHA},'binaries':BINARIES,'chromeBinarySHA256':CHROME_SHA,'complete':False,'engines':{},'requests':[],'servedInputs':{},'errors':[]}
cache={'resources/testharnessreport.js':reporter};lock=threading.Lock()
def pinned_file(path):
 with lock:
  if path in cache:return cache[path]
  proc=subprocess.run(['git','--no-pager','-C',str(UP),'show',PIN+':'+path],capture_output=True,timeout=8)
  if proc.returncode:cache[path]=None;return None
  require(len(proc.stdout)<2000000,'Unexpectedly large pinned dependency')
  cache[path]=proc.stdout;dst=OUT/'upstream-fixture'/path;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(proc.stdout)
  receipt['servedInputs'][path]={'sha256':hashlib.sha256(proc.stdout).hexdigest(),'bytes':len(proc.stdout),'source':'git show '+PIN+':'+path}
  return proc.stdout
for path,digest in SELECTED.items():require(hashlib.sha256(pinned_file(path)).hexdigest()==digest,'Pinned selected fixture mismatch: '+path)
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  path=urllib.parse.unquote(urllib.parse.urlsplit(self.path).path).lstrip('/')
  if not path or any(part in ('..','.') for part in path.split('/')) or '\\' in path or '\x00' in path:self.send_error(404);return
  try:body=pinned_file(path)
  except Exception as exc:receipt['errors'].append('serve '+path+': '+str(exc));self.send_error(500);return
  receipt['requests'].append({'path':path,'status':200 if body is not None else 404})
  if body is None:self.send_error(404);return
  content='application/javascript' if path.endswith('.js') else 'text/css' if path.endswith('.css') else 'text/html'
  self.send_response(200);self.send_header('Content-Type',content+'; charset=utf-8');self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();origin='http://127.0.0.1:'+str(server.server_port);receipt['origin']=origin
chrome=None
def summarize(rows):
 counts=Counter(s['status'] for row in rows for s in row.get('subtests',[]));statuses=Counter(row['status'] for row in rows)
 return {'recorded':len(rows),'scheduled':len(SELECTED),'fullyPassing':sum(row['status']=='OK' and bool(row.get('subtests')) and all(s['status']=='PASS' for s in row['subtests']) for row in rows),'assertions':dict(counts),'harness':dict(statuses)}
try:
 require(runner.verify_reporter_route(origin)==hashlib.sha256(reporter).hexdigest(),'Reporter HTTP bytes differ')
 for key in list(os.environ):
  if key.startswith('LUMEN_'):os.environ.pop(key)
 for engine in ['baseline','candidate','chrome']:
  directory=OUT/engine;directory.mkdir();rows=[];receipt['engines'][engine]={'rows':rows}
  if engine=='chrome':chrome=runner.Chromium(chrome_path,directory/'browser',environment['chromiumArguments'],headless=False);receipt['chromeVersion']=chrome.version
  for i,path in enumerate(SELECTED):
   require(time.monotonic()-began<105,'Overall diagnostic budget exhausted; raw completed rows retained')
   url=origin+'/'+path;prefix=directory/('case-'+str(i));start=utc()
   row=chrome.run(url,10,prefix) if chrome else runner.run_lumen(Path(BINARIES[engine]['path']).resolve(),url,10,prefix,(OUT/'reporter.js').resolve())
   row.update(test=path,actualURL=url,startedUTC=start);rows.append(row)
   (directory/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
   require(not row.get('cleanup',{}).get('error') and not row.get('cleanup',{}).get('profile_error'),'Owned native cleanup failed')
   require(not runner.measurement_failure(row),'Resource measurement failed: '+str(runner.measurement_failure(row)))
  receipt['engines'][engine]['summary']=summarize(rows)
  if chrome:receipt['chromeCleanup']=chrome.close();chrome=None
 for row in BINARIES.values():require(sha(row['path'])==row['sha256'],'Native executable changed during paired diagnostic')
 require(sha(chrome_path)==CHROME_SHA,'Chrome changed during paired diagnostic')
 for row in plan['inputs'].values():require(sha(row['path'])==row['sha256'],'Frozen1515 inputs changed')
 baseline=receipt['engines']['baseline']['rows'];candidate=receipt['engines']['candidate']['rows'];comparison=[]
 for old,new in zip(baseline,candidate):
  oldpass={s['name'] for s in old.get('subtests',[]) if s['status']=='PASS'};newpass={s['name'] for s in new.get('subtests',[]) if s['status']=='PASS'}
  comparison.append({'test':old['test'],'oldHarness':old['status'],'newHarness':new['status'],'fixedAssertions':sorted(newpass-oldpass),'lostPreviouslyPassingAssertions':sorted(oldpass-newpass)})
 receipt['nativeDelta']=comparison;receipt['complete']=all(len(x['rows'])==len(SELECTED) for x in receipt['engines'].values()) and not receipt['errors']
except Exception as exc:receipt['errors'].append(type(exc).__name__+': '+str(exc))
finally:
 if chrome:
  try:receipt['chromeCleanup']=chrome.close()
  except Exception as exc:receipt['errors'].append('Chrome cleanup: '+str(exc))
 server.shutdown();server.server_close();thread.join(timeout=2)
 cleanup=receipt.get('chromeCleanup',{})
 if cleanup.get('errors') or cleanup.get('process',{}).get('error') or cleanup.get('process',{}).get('root_exit_code')!=0:receipt['complete']=False;receipt['errors'].append('Chrome did not exit cleanly')
 receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3));(OUT/'summary.json').write_text(json.dumps(receipt,indent=2)+'\n')
 compact={k:v for k,v in receipt.items() if k not in ['engines','requests']};compact['engines']={k:{'summary':v.get('summary'),'failures':[{'test':r['test'],'status':r['status'],'message':r.get('message'),'failed':[s for s in r.get('subtests',[]) if s['status']!='PASS']} for r in v['rows'] if r['status']!='OK' or any(s['status']!='PASS' for s in r.get('subtests',[]))]} for k,v in receipt['engines'].items()};compact['fullSummary']=str(OUT/'summary.json');compact['fullSummarySHA256']=sha(OUT/'summary.json');print(json.dumps(compact,indent=2))
raise SystemExit(0 if receipt['complete'] else 1)
