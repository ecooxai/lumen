#!/usr/bin/env python3
"""Observe the real requested benchmark on the immutable accepted Mac candidate."""
from pathlib import Path
import datetime as dt, hashlib, json, os, signal, struct, subprocess, sys, time
ROOT=Path('/Users/devin/repos/lumen');OUT=ROOT/'out/verification'
TAG='html5-live-candidate01';META='out/native-verification/mac-dev-bundle-webtransport-candidate.json';GATE='out/native-verification/webtransport-native-gate04.json'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(ok,why):
 if not ok:raise RuntimeError(why)
def quiet():
 m=OUT/'resource-monitor.jsonl';mtime=m.stat().st_mtime
 need(0<=time.time()-mtime<6,'Fresh one-second monitor required')
 fs=os.statvfs(ROOT);need(fs.f_bavail*fs.f_frsize>=1024**3,'Preserve1GiB free')
 rows=subprocess.check_output(['/bin/ps','-axo','comm='],text=True,timeout=5).splitlines()
 need(not any(Path(s.strip()).name in {'lumen','Lumen','clang','clang++','ld','cargo','rustc','ninja','xcodebuild','swift-frontend'} for s in rows),'Exclusive browser/compiler reservation required')
def png(p):
 if not p.is_file() or p.is_symlink():return None
 with p.open('rb') as h:b=h.read(24)
 if len(b)!=24 or b[:8]!=b'\x89PNG\r\n\x1a\n' or b[12:16]!=b'IHDR':return None
 w,h=struct.unpack('>II',b[16:24])
 return {'path':str(p.relative_to(ROOT)),'sha256':sha(p),'width':w,'height':h,'bytes':p.stat().st_size} if 0<w<=16384 and 0<h<=16384 else None

def main():
 need(sys.platform=='darwin' and Path.cwd().resolve()==ROOT,'Unexpected workspace')
 need(not list(OUT.glob(TAG+'*')),'Preserve prior live-run evidence')
 manifest=json.loads((ROOT/'out/native-verification/live-html5-observer01-stage.json').read_text())
 helpers=manifest['files']|{'verification/bundle_case.py':'5b6eb132f81123aeb4207406c8ee0d0fa6179922a09b70f8ec48fdb1fbcd4483','verification/native_case.py':'37a2b576464e714a6670dac6cf5112fa1af329fe15a5f626644387b7fe912112','verification/resource_monitor.py':'de325936e1b9c2923e34af82883d63c5f406b15c0104f15ae2f49fb558e05e36','verification/run_native_webtransport_browser_isolated.py':'394e720f5d0beed17667657ce453fd9a1e549b70930b07679f9702abd43ba666'}
 need(all(sha(ROOT/n)==v for n,v in helpers.items()),'Reviewed helper changed')
 quiet();sys.path.insert(0,str(ROOT/'verification'))
 from run_native_webtransport_browser_isolated import native_provenance,bundle_provenance,candidate_chain
 native,tracked=native_provenance(ROOT,ROOT/GATE);meta,exe=bundle_provenance(ROOT,ROOT/META)
 chain,gate_ref,relink_ref=candidate_chain(ROOT,meta,ROOT/GATE,native);tracked.update(chain);tracked[META]=sha(ROOT/META)
 need(sha(exe)=='13bd1735fe3fb24792e025674ec25efa779519f5778bdc67e9814c74af1ddd5a','Wrong current accepted candidate')
 before={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'build/lumen',exe,OUT/'capture-window']}
 result={'schemaVersion':1,'tag':TAG,'startedUTC':dt.datetime.now(dt.timezone.utc).isoformat(),'running':True,
  'source':'https://html5test.co/','suiteMode':'unmodified requested live public site','localMirrorUsed':False,
  'benchmarkDetectorOrScoringModified':False,'userAgentOverriddenByHarness':False,'capabilitiesStubbed':False,
  'originalProfilesAccessedOrChanged':False,'authenticationClaimed':False,'comparativePerformanceClaimed':False,
  'modelVisualReviewCompleted':False,'signedExecutableSha256':sha(exe),'nativeGate':gate_ref,'relinkReport':relink_ref,
  'bounds':{'seconds':60,'ownedRSSMiBSum':4096,'minimumFreeDiskMiB':1024},'measurementAccepted':False,'score':None,'maximum':None,'scoreTarget':550,'scoreTargetOperator':'>','scoreTargetMet':False}
 summary=OUT/(TAG+'-summary.json')
 def save():
  temp=summary.with_suffix('.writing');temp.write_text(json.dumps(result,indent=2)+'\n');temp.replace(summary)
 save()
 try:
  command=[sys.executable,'verification/live_html5_bundle_case01.py','--name',TAG,'--kind','html5-live','--url','https://html5test.co/','--probe','verification/html5-live-observer01.js','--metadata',META,'--duration','60']
  with (OUT/(TAG+'.driver.log')).open('x') as log:
   p=subprocess.Popen(command,cwd=ROOT,env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')},stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   try:p.wait(timeout=80)
   except subprocess.TimeoutExpired:
    result['outerDeadlineExceeded']=True
    p.terminate()
    try:p.wait(timeout=10)
    except subprocess.TimeoutExpired:
     os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
    journal=OUT/(TAG+'.owned-process.json')
    if journal.is_file():
     owner=json.loads(journal.read_text())
     from live_html5_bundle_case01 import process_identity
     need(owner.get('executable')==str(exe) and type(owner.get('pid')) is int and bool(owner.get('identity')),'Unexpected owned process journal')
     pid=owner['pid'];identity=owner['identity']
     if process_identity(pid)==identity:
      os.kill(pid,signal.SIGTERM);until=time.monotonic()+4
      while process_identity(pid)==identity and time.monotonic()<until:time.sleep(.1)
      if process_identity(pid)==identity:os.kill(pid,signal.SIGKILL)
    raise RuntimeError('Live benchmark helper exceeded80s outer deadline')
  result['driverExit']=p.returncode
  value=json.loads((OUT/(TAG+'.json')).read_text());records=value.get('html5_live_records',[])
  result['observations']=records
  completed=[v for v in records if v.get('stage')=='completed' and v.get('complete') is True and v.get('stableObservations',0)>=2 and v.get('url')=='https://html5test.co/' and v.get('visibleResults') is True and v.get('loaderHidden') is True and v.get('documentHidden') is False and type(v.get('score')) is int and type(v.get('maximum')) is int and 0<=v['score']<=v['maximum'] and v['maximum']>0 and v.get('observerChangedBenchmark') is False]
  last=completed[-1] if completed else None
  result['score']=last['score'] if last else None;result['maximum']=last['maximum'] if last else None
  result['actualUserAgent']=last.get('userAgent') if last else None
  result['actualPNG']=png(OUT/(TAG+'.overview.png'))
  vis=value.get('window_visibility',{}).get('final',{})
  result['finalWindowVisibility']=vis
  onscreen=all(vis.get(k) is True for k in ('active','frontmostMatchesTarget','ownedWindowFound','ownedWindowOnScreen')) and vis.get('applicationHidden') is False
  result['sampleCount']=value.get('sample_count');result['browserSampleCount']=value.get('observed_browser_samples');result['peakOwnedBrowserRSSMiB']=value.get('rss_peak_mib');result['steadyCPUPercentOfOneCore']=value.get('steady_cpu_mean_percent');result['samplesWithCompilers']=value.get('samples_with_active_compilers')
  result['measurementAccepted']=p.returncode==0 and bool(last) and result['actualPNG'] is not None and onscreen and value.get('binary_sha256')==sha(exe) and value.get('samples_with_active_compilers')==0 and value.get('observed_browser_samples',0)>=10
  result['scoreTargetMet']=result['measurementAccepted'] and result['score']>550
 except (Exception,KeyboardInterrupt) as e:result['error']=type(e).__name__+': '+str(e)[:300]
 finally:
  result['sourcesAndProvenanceUnchanged']=all((ROOT/n).is_file() and sha(ROOT/n)==v for n,v in tracked.items())
  result['binariesUnchanged']=all((ROOT/n).is_file() and sha(ROOT/n)==v for n,v in before.items())
  result['helpersUnchanged']=all((ROOT/n).is_file() and sha(ROOT/n)==v for n,v in helpers.items())
  from bundle_case import matching_pids
  result['ownedBrowserStopped']=not matching_pids(exe)
  result['measurementAccepted']=result['measurementAccepted'] and all(result[k] for k in ('sourcesAndProvenanceUnchanged','binariesUnchanged','helpersUnchanged','ownedBrowserStopped')) and not result.get('error')
  result['scoreTargetMet']=result['measurementAccepted'] and type(result['score']) is int and result['score']>550
  result['running']=False;result['finishedUTC']=dt.datetime.now(dt.timezone.utc).isoformat();save()
  print(json.dumps({k:v for k,v in result.items() if k!='observations'},indent=2))
 return 0 if result['measurementAccepted'] else 1
if __name__=='__main__':raise SystemExit(main())
