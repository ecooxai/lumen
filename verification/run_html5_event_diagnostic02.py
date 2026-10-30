#!/usr/bin/env python3
"""Owned loopback async-event diagnosis on the new immutable Mac candidate."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import urlparse,parse_qs
import hashlib,json,os,re,shutil,signal,struct,subprocess,sys,threading,time,uuid
ROOT=Path('/Users/devin/repos/lumen');OUT=ROOT/'out/verification'
META='out/native-verification/mac-dev-bundle-media-dialog-candidate02.json'
GATE='out/native-verification/webtransport-native-gate05.json'
TAG='html5-events-candidate02-02'
BODY=b'<!doctype html><html><head><title>&amp;&amp;&lt;</title></head><body>typed diagnostic</body></html>'
HTML=b'''<!doctype html><html><head><meta charset="utf-8"><title>Lume async event diagnostic</title>
<style>*{box-sizing:border-box}body{margin:0;padding:24px;font:16px system-ui;background:#eaf3ff;color:#14243b}h1{font-size:25px;margin:0 0 8px}p{margin:8px 0}table{width:100%;border-collapse:collapse;background:white}td,th{padding:6px 10px;border:1px solid #bdcce0;text-align:left;font-size:14px}#images{height:28px}#images img{margin-right:8px}</style></head><body>
<h1>Async browser events</h1><p id="status">Waiting for actual success, error or timeout callbacks.</p><p>Independent diagnostic. This page does not run or score HTML5test.</p><div id="images"></div>
<table><thead><tr><th>Family</th><th>Case</th><th>Observed terminal event</th><th>Upstream would settle</th></tr></thead><tbody id="rows"></tbody></table></body></html>'''
def need(value,why):
 if not value:raise RuntimeError(why)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def png(path):
 if not path.is_file() or path.is_symlink() or path.stat().st_size>20*1024*1024:return None
 with path.open('rb') as stream:header=stream.read(24)
 if len(header)!=24 or header[:8]!=b'\x89PNG\r\n\x1a\n' or header[12:16]!=b'IHDR':return None
 w,h=struct.unpack('>II',header[16:24])
 return {'path':str(path.relative_to(ROOT)),'sha256':sha(path),'width':w,'height':h} if 0<w<5000 and 0<h<5000 else None
def quiet():
 monitor=OUT/'resource-monitor.jsonl';mtime=monitor.stat().st_mtime
 need(0<=time.time()-mtime<6,'Fresh one-second monitor required')
 need(shutil.disk_usage(ROOT).free>=1024**3,'Preserve1GiB disk floor')
 rows=subprocess.check_output(['/bin/ps','-axo','comm='],text=True,timeout=5).splitlines()
 need(not any(Path(row.strip()).name in {'lumen','Lumen','clang','clang++','ld','ninja','cargo','rustc','xcodebuild','swift-frontend','xctest'} for row in rows),'Exclusive compiler/GUI slot required')
def assess(value,requests):
 rows=value.get('records',[]);images={r.get('id'):r for r in rows if r.get('family')=='image'}
 xhr={r.get('id'):r for r in rows if r.get('family')=='xhr'};idb=[r for r in rows if r.get('family')=='idb']
 image_ids={kind+'-'+mode for kind in ('png','webp','jxl','avif','heic','invalid') for mode in ('attached','detached')}
 shape=(len(rows)==17 and set(images)==image_ids and len(images)==12 and set(xhr)=={'text','document','arraybuffer','blob'} and len(idb)==1)
 image_summary={name:{k:row.get(k) for k in ('terminal','naturalWidth','naturalHeight','decoderAccepted','msUntilTerminal')} for name,row in images.items()}
 settled_images={name:row.get('terminal') in ('load','error') for name,row in images.items()}
 typed={}
 for kind,row in xhr.items():
  r=row.get('response') or {};correct=requests.get(kind,0)==1 and r.get('status')==200 and r.get('responseType')==kind
  if kind=='text':correct=correct and r.get('bodyMatched') is True and r.get('textLength')==len(BODY)
  if kind=='document':correct=correct and r.get('documentPresent') is True and r.get('title')=='&&<'
  if kind=='arraybuffer':correct=correct and r.get('arrayBuffer') is True and r.get('byteLength')==len(BODY)
  if kind=='blob':correct=correct and r.get('blob') is True and r.get('byteLength')==len(BODY) and r.get('mimeType','').startswith('text/html')
  typed[kind]={'readyState4Observed':row.get('readyState4Observed') is True,'actualServerRequests':requests.get(kind,0),'typedResponseCorrect':bool(correct),'terminal':row.get('terminal')}
 blocking=[]
 if any(not settled_images.get(kind+'-detached',False) for kind in ('webp','jxl','avif','heic')):blocking.append('detached-image-events')
 if any(not xhr.get(kind,{}).get('upstreamWouldSettle') for kind in ('text','document','arraybuffer','blob')):blocking.append('xhr-completion-events')
 if not idb or idb[0].get('upstreamWouldSettle') is not True:blocking.append('indexeddb-request-events')
 return {'allExpectedCasesObserved':shape,'imageCases':image_summary,'imageEventSettlement':settled_images,'xhrCases':typed,
  'indexedDB':idb[0] if idb else None,'blockingFamilies':blocking,'allUpstreamBackgroundFamiliesWouldSettle':shape and not blocking,
  'validPNGControlDecoded':all(images.get('png-'+mode,{}).get('terminal')=='load' and images['png-'+mode].get('naturalWidth')==1 and images['png-'+mode].get('naturalHeight')==1 for mode in ('attached','detached')),
  'invalidImageErrorObserved':all(images.get('invalid-'+mode,{}).get('terminal')=='error' for mode in ('attached','detached')),
  'indexedDBCleanupConfirmed':bool(idb and idb[0].get('connectionClosed') is True and idb[0].get('cleanupDeleteSucceeded') is True),
  'benchmarkScore':None,'html5TargetAcceptanceClaimed':False}
def main():
 need(sys.platform=='darwin' and Path.cwd().resolve()==ROOT,'Unexpected diagnostic workspace')
 stage=ROOT/'out/native-verification/html5-event-diagnostic02-stage.json';manifest=json.loads(stage.read_text())
 need(manifest.get('passed') is True,'Exact staged diagnostic required')
 for name,want in manifest['files'].items():need(sha(ROOT/name)==want,'Reviewed diagnostic changed: '+name)
 prior_attempt=ROOT/'out/native-verification/html5-event-diagnostic01/result.json'
 need(sha(prior_attempt)=='f022625c83cf2ae24c27bd0e3b8c9a21dec455c199e7722582b618c40749ffc0','Preserve the exact failed observation01')
 quiet()
 need(sha(ROOT/META)=='3206f700eb8b65dd5fad36ae3a73b71a52b4b5775d52181245a0dd522a77434b' and sha(ROOT/GATE)=='5270832cb360d010784bd5938914df5584a569cdb1f874af96256fa6eca3f034','The independently accepted candidate02 chain changed')
 sys.path.insert(0,str(ROOT/'verification'))
 from run_native_webtransport_browser_isolated import native_provenance,bundle_provenance,candidate_chain
 from html5_event_bundle_case02 import matching_pids,process_identity,late_owned_process
 native,tracked=native_provenance(ROOT,ROOT/GATE);metadata,exe=bundle_provenance(ROOT,ROOT/META)
 chain,gate_ref,relink_ref=candidate_chain(ROOT,metadata,ROOT/GATE,native);tracked.update(chain)
 need(gate_ref.get('evidenceTag')=='native-gate05' and str(exe.relative_to(ROOT))=='out/native-verification/mac-media-dialog-candidate02/Lumen.app/Contents/MacOS/lumen','Wrong selected current candidate')
 case=ROOT/'out/native-verification/html5-event-diagnostic02';need(not case.exists() and not list(OUT.glob(TAG+'*')),'Preserve prior diagnostic evidence')
 os.umask(0o077);case.mkdir(mode=0o700);nonce=uuid.uuid4().hex[:24];lock=threading.Lock();state={'requests':{},'documentRequests':0,'result':None,'resultBytesSHA256':None}
 report=case/'result.json';source_before=sha(ROOT/'build/lumen');signed_before=sha(exe);capture_before=sha(OUT/'capture-window')
 tracked[META]=sha(ROOT/META);tracked[str(stage.relative_to(ROOT))]=sha(stage)
 result={'schemaVersion':1,'consoleTransport':'17 ordered bounded rows plus header; exactly reconstructed against independent POST','priorAttemptPreserved':True,'tag':TAG,'running':True,'measurementAccepted':False,'startUnix':time.time(),'nonce':nonce,
  'scope':'Actual independent loopback image/XHR/IndexedDB event diagnosis; no live benchmark execution',
  'benchmarkExecuted':False,'benchmarkScore':None,'detectorOrScoreChanged':False,'capabilitiesStubbed':False,'originalProfilesAccessed':False,
  'comparativePerformanceClaimed':False,'modelVisualReviewCompleted':False,'nativeGate':gate_ref,'relinkReport':relink_ref,
  'signedExecutableSHA256':signed_before,'sourceBinarySHA256':source_before,'sourceHashes':manifest['files'],
  'bounds':{'browserSeconds':40,'outerHelperSeconds':60,'maximumOwnedRSSMiBSum':4096,'minimumFreeDiskMiB':1024},'cleanupErrors':[]}
 def save():
  temp=report.with_suffix('.writing');temp.write_text(json.dumps(result,indent=2)+'\n');temp.replace(report)
 class Handler(BaseHTTPRequestHandler):
  def reply(self,status,body,content_type):
   self.send_response(status);self.send_header('Content-Type',content_type);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  def do_GET(self):
   url=urlparse(self.path);query=parse_qs(url.query)
   if query.get('nonce')!=[nonce]:self.reply(404,b'','text/plain');return
   if url.path=='/event-diagnostic':
    with lock:state['documentRequests']+=1
    self.reply(200,HTML,'text/html; charset=utf-8')
   elif url.path=='/detect.html' and query.get('case',[None])[0] in ('text','document','arraybuffer','blob'):
    with lock:
     kind=query['case'][0];state['requests'][kind]=state['requests'].get(kind,0)+1
    self.reply(200,BODY,'text/html; charset=utf-8')
   else:self.reply(404,b'','text/plain')
  def do_POST(self):
   try:
    url=urlparse(self.path);size=int(self.headers.get('Content-Length','0'))
    if url.path!='/result' or parse_qs(url.query).get('nonce')!=[nonce] or not 0<size<=65536:self.reply(400,b'','text/plain');return
    raw=self.rfile.read(size);value=json.loads(raw)
    if value.get('nonce')!=nonce or value.get('schemaVersion')!=1 or value.get('benchmarkExecuted') is not False:self.reply(400,b'','text/plain');return
    with lock:
     if state['result'] is not None:self.reply(409,b'','text/plain');return
     state['result']=value;state['resultBytesSHA256']=hashlib.sha256(raw).hexdigest()
    self.reply(200,b'{"ok":true}','application/json')
   except (ValueError,TypeError,AttributeError):self.reply(400,b'','text/plain')
  def log_message(self,*args):pass
 server=ThreadingHTTPServer(('127.0.0.1',0),Handler);server.daemon_threads=True
 thread=threading.Thread(target=lambda:server.serve_forever(poll_interval=.1),daemon=True);thread.start();helper=None
 def cleanup_browser():
  journal=OUT/(TAG+'.owned-process.json')
  if not journal.is_file():return
  owner=json.loads(journal.read_text());need(owner.get('executable')==str(exe) and owner.get('uid')==os.getuid() and owner.get('absentBeforeLaunch') is True and type(owner.get('launchedSince')) in (int,float),'Unexpected exact owned browser journal')
  pid=owner.get('pid');identity=owner.get('identity')
  if pid is None:pid,identity=late_owned_process(exe,owner['launchedSince'])
  if pid is None:return
  need(type(pid) is int and isinstance(identity,str),'Unexpected exact owned browser identity')
  if process_identity(pid)==identity:
   os.kill(pid,signal.SIGTERM);deadline=time.monotonic()+4
   while process_identity(pid)==identity and time.monotonic()<deadline:time.sleep(.1)
   if process_identity(pid)==identity:os.kill(pid,signal.SIGKILL)
 def interrupted(signum,frame):raise KeyboardInterrupt('Owned event diagnostic interrupted')
 for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,interrupted)
 save()
 try:
  url='http://127.0.0.1:'+str(server.server_port)+'/event-diagnostic?nonce='+nonce
  result['server']={'host':'127.0.0.1','port':server.server_port,'documentSHA256':hashlib.sha256(HTML).hexdigest(),'xhrBodySHA256':hashlib.sha256(BODY).hexdigest(),'xhrBodyBytes':len(BODY)};save()
  command=[sys.executable,'verification/html5_event_bundle_case02.py','--name',TAG,'--kind','html5-events','--url',url,'--probe','verification/html5-event-probe02.js','--metadata',META,'--duration','40']
  with (case/'driver.log').open('xb') as log:
   helper=subprocess.Popen(command,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   helper.wait(timeout=60)
  result['driverExit']=helper.returncode
  casepath=OUT/(TAG+'.json');need(casepath.is_file() and casepath.stat().st_size<2*1024*1024,'Bounded actual browser report missing')
  browser=json.loads(casepath.read_text());records=browser.get('html5_event_records',[])
  with lock:observed=json.loads(json.dumps(state))
  result['serverObservation']=observed;result['consoleObservations']=records
  value=observed.get('result');need(isinstance(value,dict) and records==[value],'Exact console/independent server evidence correlation missing')
  result['assessment']=assess(value,observed['requests'])
  vis=browser.get('window_visibility',{}).get('final',{});result['finalWindowVisibility']=vis
  result['actualPNG']=png(OUT/(TAG+'.overview.png'));result['browserSamples']=browser.get('observed_browser_samples');result['peakBrowserRSSMiB']=browser.get('rss_peak_mib');result['samplesWithCompilers']=browser.get('samples_with_active_compilers')
  result['measurementAccepted']=bool(helper.returncode==0 and not browser.get('cleanupErrors') and browser.get('ownedBrowserStopped') is True and result['assessment']['allExpectedCasesObserved'] and observed['documentRequests']==1 and result['actualPNG'] and result['browserSamples']>=10 and result['samplesWithCompilers']==0 and all(vis.get(k) is True for k in ('active','frontmostMatchesTarget','ownedWindowFound','ownedWindowOnScreen')) and vis.get('applicationHidden') is False)
 except (Exception,KeyboardInterrupt) as error:result['error']=type(error).__name__+': '+str(error)[:400]
 finally:
  for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,signal.SIG_IGN)
  if helper is not None and helper.poll() is None:
   try:
    helper.terminate()
    try:helper.wait(timeout=8)
    except subprocess.TimeoutExpired:os.killpg(helper.pid,signal.SIGKILL);helper.wait(timeout=3)
   except (OSError,subprocess.TimeoutExpired) as error:result['cleanupErrors'].append('helper:'+type(error).__name__)
  try:cleanup_browser()
  except Exception as error:result['cleanupErrors'].append('browser:'+type(error).__name__)
  server.shutdown();server.server_close();thread.join(timeout=2)
  try:
   result['binariesUnchanged']=sha(ROOT/'build/lumen')==source_before and sha(exe)==signed_before and sha(OUT/'capture-window')==capture_before
   result['sourcesAndProvenanceUnchanged']=all((ROOT/name).is_file() and sha(ROOT/name)==want for name,want in {**tracked,**manifest['files']}.items())
   result['ownedBrowserStopped']=not matching_pids(exe);result['serverStopped']=not thread.is_alive()
  except Exception as error:result['cleanupErrors'].append('final-integrity:'+type(error).__name__)
  result['measurementAccepted']=bool(result['measurementAccepted'] and not result.get('error') and not result['cleanupErrors'] and all(result.get(k) is True for k in ('binariesUnchanged','sourcesAndProvenanceUnchanged','ownedBrowserStopped','serverStopped')))
  result['running']=False;result['endUnix']=time.time();save()
 print(json.dumps({k:v for k,v in result.items() if k not in ('sourceHashes','consoleObservations','serverObservation')},indent=2))
 return 0 if result['measurementAccepted'] else 1
if __name__=='__main__':raise SystemExit(main())
