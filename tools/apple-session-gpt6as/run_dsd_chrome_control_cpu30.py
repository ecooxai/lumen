#!/usr/bin/env python3
"""Qualify the unchanged36-case DSD regression using the existing signed Chrome/CDP adapter."""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,http.server,importlib.util,json,os,socket,sys,threading,time,urllib.request
ROOT=Path.cwd().resolve();OUT=Path('out/lume-20261029-cpu30/dsd-chrome-control-1');FIXTURE=Path('tests/test_declarative_shadow_documents.py')
FIXTURE_SHA='60c78f66aabcaed704aaa0635c4beb4412d9acc7f8469b98c8de0cab20a8b941';CHROME_SHA='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4';QUAL_SHA='1e34a36c89d61da6314e975fb8fe8296e0bd2e07ac73cc3836861785fcac5452'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();read=lambda p:json.loads(Path(p).read_text());utc=lambda:datetime.now(timezone.utc).isoformat()
def require(value,message):
 if not value:raise RuntimeError(message)
def module_at(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);sys.modules[name]=m;spec.loader.exec_module(m);return m
require(sha(FIXTURE)==FIXTURE_SHA,'Frozen36-case fixture differs')
fixture=module_at('dsd_frozen36_fixture',FIXTURE);require(fixture.EXPECTED==36,'Frozen denominator differs');pagebytes=fixture.HTML.encode();htmlsha=hashlib.sha256(pagebytes).hexdigest()
plan=read('out/lume-20261029-cpu30/compat-default-1/cohort/cohort.json')
for v in plan['inputs'].values():require(sha(v['path'])==v['sha256'],'Qualified adapter input changed')
tool=module_at('dsd_control_inputs',plan['inputs']['orchestrator']['path']);tool.BASE=Path(plan['fixture']);frozen,categories,environment,hashes,runner=tool.current_inputs();require(hashes==plan['inputs'],'Qualified adapter fingerprints differ')
qualification_path=Path('agent/evidence/102900614-006-chrome-reference.json');require(sha(qualification_path)==QUAL_SHA,'Comparator qualification changed');qualification=read(qualification_path);require(qualification['qualifiedForPairedMeasurement'] and qualification['binarySHA256']==CHROME_SHA,'Comparator not qualified');chrome=Path(qualification['binaryPath']);require(sha(chrome)==CHROME_SHA,'Chrome executable changed')
OUT.mkdir(parents=True,exist_ok=False);(OUT/'fixture.html').write_bytes(pagebytes);began=time.monotonic();receipt={'taskId':'20261029-cpu30','startedUTC':utc(),'scope':'Exact unchanged36 project regression cases in fresh qualified Chrome; not upstream WPT or a native acceptance gate. No Web API injection.','fixture':str(FIXTURE),'fixtureSHA256':FIXTURE_SHA,'htmlSHA256':htmlsha,'chromeBinarySHA256':CHROME_SHA,'qualificationSHA256':QUAL_SHA,'complete':False,'allPassed':False,'checks':[],'summaryMarkers':[],'uncaughtExceptions':[],'cleanupErrors':[]}
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),fixture.Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();browser=None;page=None;context=None
try:
 for key in list(os.environ):
  if key.startswith('LUMEN_'):os.environ.pop(key)
 browser=runner.Chromium(chrome,OUT/'browser',environment['chromiumArguments'],headless=False);receipt['browserVersion']=browser.version
 context=browser.browser.call('Target.createBrowserContext')['browserContextId'];target=browser.browser.call('Target.createTarget',{'url':'about:blank','browserContextId':context})['targetId']
 with urllib.request.urlopen('http://127.0.0.1:'+str(browser.port)+'/json/list',timeout=10) as response:targets=json.load(response)
 info=next(x for x in targets if x['id']==target);page=runner.CDP(info['webSocketDebuggerUrl']);page.call('Page.enable');page.call('Runtime.enable');url='http://127.0.0.1:'+str(server.server_port)+'/';receipt['actualURL']=url;nav=page.call('Page.navigate',{'url':url});require(not nav.get('errorText'),'Chrome navigation failed')
 until=time.monotonic()+20
 with (OUT/'console.log').open('x') as log:
  while time.monotonic()<until and not receipt['summaryMarkers']:
   try:event=page.event(min(1,max(.01,until-time.monotonic())))
   except socket.timeout:continue
   method=event.get('method')
   if method=='Runtime.consoleAPICalled':
    text=' '.join(str(x.get('value',x.get('description',''))) for x in event.get('params',{}).get('args',[]));log.write(text+'\n')
    for marker,key in [('DSD_CHECK ','checks'),('DSD_RESULTS ','summaryMarkers')]:
     if text.startswith(marker):receipt[key].append(json.loads(text[len(marker):]))
   elif method=='Runtime.exceptionThrown':receipt['uncaughtExceptions'].append(event.get('params'))
   elif method in ['Inspector.targetCrashed','Target.targetCrashed']:raise RuntimeError('Chrome target crashed')
 require(len(receipt['checks'])==36 and [r['i'] for r in receipt['checks']]==list(range(36)) and len({r['name'] for r in receipt['checks']})==36 and len(receipt['summaryMarkers'])==1,'Exact36 checks did not complete once')
 passed=sum(r['ok'] is True for r in receipt['checks']);require(receipt['summaryMarkers'][0]=={'total':36,'passed':passed,'failed':36-passed},'Inconsistent fixture total');receipt.update(complete=True,total=36,passed=passed,failed=36-passed,allPassed=passed==36)
 # Obtain at least two useful1Hz process-tree observations without claiming an idle baseline.
 time.sleep(max(0,1.5-(time.monotonic()-began)))
 require(sha(FIXTURE)==FIXTURE_SHA and sha(chrome)==CHROME_SHA,'Control input changed during execution')
except Exception as e:receipt['error']=type(e).__name__+': '+str(e)
finally:
 if page:
  try:page.close()
  except Exception as e:receipt['cleanupErrors'].append('page: '+str(e))
 if browser and context:
  try:browser.browser.call('Target.disposeBrowserContext',{'browserContextId':context})
  except Exception as e:receipt['cleanupErrors'].append('context: '+str(e))
 if browser:
  receipt['browserCleanup']=browser.close();metrics=OUT/'browser/chromium.metrics-summary.json'
  if metrics.exists():receipt['resources']={'path':str(metrics),'sha256':sha(metrics),'summary':read(metrics)}
 server.shutdown();server.server_close();thread.join(timeout=2)
 cleanup=receipt.get('browserCleanup',{});proc=cleanup.get('process',{})
 receipt['normalCleanup']=not receipt['cleanupErrors'] and not cleanup.get('errors') and not proc.get('error') and proc.get('root_exit_code')==0
 if not receipt['normalCleanup']:receipt['complete']=False
 receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3));(OUT/'summary.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
raise SystemExit(0 if receipt['complete'] else 1)
