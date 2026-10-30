#!/usr/bin/env python3
"""Actual compact modal geometry on the new immutable Mac candidate."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from urllib.parse import urlparse,parse_qs
import hashlib,json,math,os,re,shutil,signal,struct,subprocess,sys,threading,time,uuid
ROOT=Path('/Users/devin/repos/lumen');OUT=ROOT/'out/verification'
META='out/native-verification/mac-dev-bundle-media-dialog-candidate02.json'
GATE='out/native-verification/webtransport-native-gate05.json'
TAG='compact-dialog-candidate02-01'
HTML=b'<!doctype html>\n<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">\n<title>Lume native dialog input fixture</title>\n<style>\n* { box-sizing:border-box; }\nhtml,body { margin:0; font:18px system-ui; background:#eaf3ff; color:#111827; }\nbody { padding:112px 24px 24px; }\nbutton { font:18px system-ui; border:0; border-radius:6px; color:#ffffff; }\n#open { position:fixed; top:24px; left:20px; width:240px; height:56px; background:#0055cc; }\ndialog { width:280px; max-width:calc(100vw - 48px); padding:18px; border:3px solid #1e3a8a; border-radius:12px; background:#dbeafe; color:#111827; }\ndialog h2 { margin:0 0 12px; font-size:22px; }\ndialog p { margin:0 0 16px; line-height:1.3; }\ndialog button { display:block; width:100%; height:48px; margin:10px 0 0; }\n#accept { background:#006b4f; }\n#cancel { background:#b42318; }\n</style></head><body>\n<button id="open" type="button">Open local consent</button>\n<h1>Actual compact dialog</h1><p id="fixture-status">Opening the owned local modal.</p><p>This fixture measures actual modal size. No account or authentication provider is contacted.</p>\n<dialog id="consent"><form method="dialog"><h2>Local consent simulation</h2><p>Choose Continue or Cancel. This does not sign in to an account.</p>\n<button id="accept" value="accepted">Continue</button><button id="cancel" value="cancel">Cancel</button>\n</form></dialog>\n</body></html>\n'
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
def assess(value):
 def rectangle(row):return isinstance(row,dict) and all(type(row.get(k)) in (int,float) and math.isfinite(row[k]) for k in ('x','y','width','height'))
 viewport=value.get('viewport',{});dialog=value.get('dialog',{});form=value.get('form',{});controls=value.get('controls',{})
 shape=rectangle(dialog) and rectangle(form) and all(rectangle(controls.get(k)) for k in ('accept','cancel')) and all(type(viewport.get(k)) in (int,float) and viewport[k]>0 for k in ('width','height'))
 if not shape:return {'passed':False,'reason':'Missing finite actual geometry'}
 natural=abs(dialog['height']-(form['height']+42))<=2
 centered=abs(dialog['y']-(viewport['height']-dialog['height'])/2)<=2 and abs(dialog['x']-(viewport['width']-dialog['width'])/2)<=2
 contained=all(r['width']>0 and r['height']>0 and r['x']>=dialog['x']+20 and r['y']>=dialog['y']+20 and r['x']+r['width']<=dialog['x']+dialog['width']-20 and r['y']+r['height']<=dialog['y']+dialog['height']-20 for r in controls.values())
 initial=value.get('initial',{});initial_closed=initial.get('open') is False and initial.get('rect',{}).get('width')==0 and initial.get('rect',{}).get('height')==0
 visible=value.get('open') is True and 0<dialog['width']<=280.5 and 0<dialog['height']<viewport['height']*.85 and dialog['y']>=0 and dialog['y']+dialog['height']<=viewport['height']
 passed=initial_closed and natural and centered and contained and visible and all(value.get(k) is False for k in ('authenticated','providerContacted','physicalInputClaim','focusOrInertnessConformanceClaim'))
 return {'passed':bool(passed),'initialClosed':initial_closed,'heightFollowsActualFormPlus42Pixels':natural,'centeredInViewport':centered,'bothControlsInsidePadding':contained,'compactAndInViewport':visible,'dialogHeight':dialog['height'],'formHeight':form['height'],'viewportHeight':viewport['height'],'nativeInputExercised':False,'generalModalConformanceClaimed':False}
def main():
 need(sys.platform=='darwin' and Path.cwd().resolve()==ROOT,'Unexpected diagnostic workspace')
 stage=ROOT/'out/native-verification/compact-dialog-runtime01-stage.json';manifest=json.loads(stage.read_text())
 need(manifest.get('passed') is True,'Exact staged diagnostic required')
 for name,want in manifest['files'].items():need(sha(ROOT/name)==want,'Reviewed diagnostic changed: '+name)
 quiet()
 need(sha(ROOT/META)=='3206f700eb8b65dd5fad36ae3a73b71a52b4b5775d52181245a0dd522a77434b' and sha(ROOT/GATE)=='5270832cb360d010784bd5938914df5584a569cdb1f874af96256fa6eca3f034','The independently accepted candidate02 chain changed')
 sys.path.insert(0,str(ROOT/'verification'))
 from run_native_webtransport_browser_isolated import native_provenance,bundle_provenance,candidate_chain
 from compact_dialog_bundle_case01 import matching_pids,process_identity,late_owned_process
 native,tracked=native_provenance(ROOT,ROOT/GATE);metadata,exe=bundle_provenance(ROOT,ROOT/META)
 chain,gate_ref,relink_ref=candidate_chain(ROOT,metadata,ROOT/GATE,native);tracked.update(chain)
 need(gate_ref.get('evidenceTag')=='native-gate05' and str(exe.relative_to(ROOT))=='out/native-verification/mac-media-dialog-candidate02/Lumen.app/Contents/MacOS/lumen','Wrong selected current candidate')
 case=ROOT/'out/native-verification/compact-dialog-runtime01';need(not case.exists() and not list(OUT.glob(TAG+'*')),'Preserve prior diagnostic evidence')
 os.umask(0o077);case.mkdir(mode=0o700);nonce=uuid.uuid4().hex[:24];lock=threading.Lock();state={'requests':{},'documentRequests':0,'result':None,'resultBytesSHA256':None}
 report=case/'result.json';source_before=sha(ROOT/'build/lumen');signed_before=sha(exe);capture_before=sha(OUT/'capture-window')
 tracked[META]=sha(ROOT/META);tracked[str(stage.relative_to(ROOT))]=sha(stage)
 result={'schemaVersion':1,'tag':TAG,'running':True,'measurementAccepted':False,'startUnix':time.time(),'nonce':nonce,
  'scope':'Actual owned script-opened compact modal; physical input and general focus/inertness are not tested',
  'benchmarkExecuted':False,'benchmarkScore':None,'detectorOrScoreChanged':False,'capabilitiesStubbed':False,'originalProfilesAccessed':False,
  'comparativePerformanceClaimed':False,'modelVisualReviewCompleted':False,'nativeGate':gate_ref,'relinkReport':relink_ref,
  'signedExecutableSHA256':signed_before,'sourceBinarySHA256':source_before,'sourceHashes':manifest['files'],
  'bounds':{'browserSeconds':18,'outerHelperSeconds':40,'maximumOwnedRSSMiBSum':4096,'minimumFreeDiskMiB':1024},'cleanupErrors':[]}
 def save():
  temp=report.with_suffix('.writing');temp.write_text(json.dumps(result,indent=2)+'\n');temp.replace(report)
 class Handler(BaseHTTPRequestHandler):
  def reply(self,status,body,content_type):
   self.send_response(status);self.send_header('Content-Type',content_type);self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  def do_GET(self):
   url=urlparse(self.path);query=parse_qs(url.query)
   if query.get('nonce')!=[nonce]:self.reply(404,b'','text/plain');return
   if url.path=='/compact-dialog':
    with lock:state['documentRequests']+=1
    self.reply(200,HTML,'text/html; charset=utf-8')
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
  url='http://127.0.0.1:'+str(server.server_port)+'/compact-dialog?nonce='+nonce
  result['server']={'host':'127.0.0.1','port':server.server_port,'documentSHA256':hashlib.sha256(HTML).hexdigest(),'fixtureIsOwned':True};save()
  command=[sys.executable,'verification/compact_dialog_bundle_case01.py','--name',TAG,'--kind','compact-dialog','--url',url,'--probe','verification/compact-dialog-probe01.js','--metadata',META,'--duration','18']
  with (case/'driver.log').open('xb') as log:
   helper=subprocess.Popen(command,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   helper.wait(timeout=40)
  result['driverExit']=helper.returncode
  casepath=OUT/(TAG+'.json');need(casepath.is_file() and casepath.stat().st_size<2*1024*1024,'Bounded actual browser report missing')
  browser=json.loads(casepath.read_text());records=browser.get('compact_dialog_records',[])
  with lock:observed=json.loads(json.dumps(state))
  result['serverObservation']=observed;result['consoleObservations']=records
  value=observed.get('result');need(isinstance(value,dict) and records==[value],'Exact console/independent server evidence correlation missing')
  result['assessment']=assess(value)
  vis=browser.get('window_visibility',{}).get('final',{});result['finalWindowVisibility']=vis
  result['actualPNG']=png(OUT/(TAG+'.overview.png'));result['browserSamples']=browser.get('observed_browser_samples');result['peakBrowserRSSMiB']=browser.get('rss_peak_mib');result['samplesWithCompilers']=browser.get('samples_with_active_compilers')
  result['measurementAccepted']=bool(helper.returncode==0 and not browser.get('cleanupErrors') and browser.get('ownedBrowserStopped') is True and observed['documentRequests']==1 and result['actualPNG'] and result['browserSamples']>=10 and result['samplesWithCompilers']==0 and all(vis.get(k) is True for k in ('active','frontmostMatchesTarget','ownedWindowFound','ownedWindowOnScreen')) and vis.get('applicationHidden') is False)
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
  result['passed']=bool(result['measurementAccepted'] and result.get('assessment',{}).get('passed') is True)
  result['running']=False;result['endUnix']=time.time();save()
 print(json.dumps({k:v for k,v in result.items() if k not in ('sourceHashes','consoleObservations','serverObservation')},indent=2))
 return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
