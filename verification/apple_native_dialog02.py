#!/usr/bin/env python3
"""One targeted XCTest per installed Apple simulator; no Lume build or install."""
from pathlib import Path
import hashlib,json,os,signal,stat,subprocess,sys,time,urllib.request
ROOT=Path('/Users/devin/repos/lumen');sys.path.insert(0,str(ROOT/'verification'))
from apple_form_css_refresh02 import TARGETS,BUNDLE,NEW_SIM,MAC,sha,command,states,container,identity,scoped_data
from resource_monitor import sample_processes,add_cpu_deltas
from native_case import descendants
from bundle_case import matching_pids

REPORT02='8f5b4e829569e7aa4e99ce045eea3d7e57b83a5fe82a08599975045839551b5b'
def need(value,message):
 if not value:raise RuntimeError(message)
def heartbeat_age(path):
 # Snapshot the concurrently written file before reading the later wall clock.
 mtime=path.stat().st_mtime
 return time.time()-mtime

def stop(proc,grace=12):
 if proc is None or proc.poll() is not None:return
 try:os.killpg(proc.pid,signal.SIGINT)
 except ProcessLookupError:return
 try:proc.wait(timeout=grace)
 except subprocess.TimeoutExpired:
  try:os.killpg(proc.pid,signal.SIGTERM)
  except ProcessLookupError:pass
  try:proc.wait(timeout=5)
  except subprocess.TimeoutExpired:
   try:os.killpg(proc.pid,signal.SIGKILL)
   except ProcessLookupError:pass
   proc.wait(timeout=3)

def main():
 need(sys.platform=='darwin' and Path.cwd().resolve()==ROOT,'Unexpected native dialog workspace')
 import shutil
 prior_attempt=ROOT/'out/native-verification/apple-native-dialog01/result.json'
 need(sha(prior_attempt)=='fd2a305011c35f93037b52beebf3cfe0aa55dac09d372607676979674a808b44','Preserve interrupted prior UI attempt')
 acceptance=ROOT/'out/native-verification/apple-form-css-refresh02/result.json'
 need(sha(acceptance)==REPORT02,'Require exact accepted Apple02 report')
 accepted=json.loads(acceptance.read_text());need(accepted.get('passed') is True and accepted.get('running') is False and accepted['bundles']['sim']['binarySHA256']==NEW_SIM,'Apple02 did not pass')
 manifest_path=ROOT/'out/native-verification/apple-native-dialog02-source-manifest.json';manifest=json.loads(manifest_path.read_text())
 for name,want in manifest['after'].items():need(sha(ROOT/name)==want,'Native dialog test source changed: '+name)
 need(sha(ROOT/'build-sim/Lumen.app/Lumen')==NEW_SIM and sha(ROOT/'build/lumen')==MAC,'Accepted binary changed')
 monitor=ROOT/'out/verification/resource-monitor.jsonl'
 need(monitor.is_file() and 0<=heartbeat_age(monitor)<6,'Fresh one-second monitoring required')
 need(shutil.disk_usage(ROOT).free>=2*1024**3,'Keep2GiB before bounded UI harness work')
 processes=command(['ps','-axo','comm='])
 need(not any(Path(s.strip()).name in ('lumen','Lumen','xcodebuild','swift-frontend','xctest','ninja','cargo','rustc','clang','clang++','ld') for s in processes.splitlines()),'GUI/compiler reservation is busy')
 initial_states=states();target_state={};fds={}
 for label,udid in TARGETS.items():
  need(initial_states.get(udid)=='Booted','Only the already-booted designated targets may run')
  app=container(udid,'app');data,manager=scoped_data(udid)
  need(sha(app/'Lumen')==NEW_SIM and not matching_pids(app/'Lumen'),'Installed target differs or is active')
  before=identity(data);original=accepted['targets'][label]['originalDataIdentity']
  need((before['device'],before['inode'])==(original['device'],original['inode']),'Original data object differs')
  fds[label]=os.open(data,os.O_RDONLY|os.O_DIRECTORY)
  target_state[label]={'udid':udid,'dataIdentity':before,'manager':manager,'executable':str(app/'Lumen')}
 case=ROOT/'out/native-verification/apple-native-dialog02';need(not case.exists(),'Preserve earlier native dialog attempt')
 os.umask(0o077);case.mkdir(mode=0o700);report=case/'result.json'
 result={'schemaVersion':1,'scope':'Actual native simulator window pixels and UIKit input for local HTML dialog; no provider authentication',
  'running':True,'passed':False,'startUnix':time.time(),'caseResults':[],'sourceHashes':manifest['after'],'sourceManifestSha256':sha(manifest_path),
  'priorInterruptedAttempt':{'path':str(prior_attempt.relative_to(ROOT)),'sha256':sha(prior_attempt)},'heartbeatReadOrder':'file-mtime-before-wall-clock',
  'acceptedApple02Sha256':REPORT02,'binarySha256':NEW_SIM,'targets':target_state,'initialBootStates':initial_states,
  'lumeBuildOrInstallRequested':False,'newV8BuildRequested':False,'userDataRead':False,'privateDataBytewiseVerified':False,
  'realAuthenticationVerified':False,'providerContacted':False,'physicalDeviceVerified':False,'modalFocusAndInertnessConformanceVerified':False,
  'independentVisualReviewCompleted':False,'performanceComparisonClaimed':False,'bounds':{'jobs':2,'caseDeadlineSeconds':180,'maximumOwnedRSSMiB':4096,'minimumFreeDiskMiB':1024}}
 active=None;server=None;owned_apps={};cleanup=[]
 def record():
  tmp=report.with_suffix('.tmp');tmp.write_text(json.dumps(result,indent=2)+'\n');tmp.replace(report)
 def check_data(label):
  current,manager=scoped_data(TARGETS[label]);observed=identity(current);before=target_state[label]['dataIdentity'];held=os.fstat(fds[label])
  need(stat.S_ISDIR(held.st_mode) and held.st_nlink>0 and (held.st_dev,held.st_ino)==(observed['device'],observed['inode'])==(before['device'],before['inode']),'Original held data object changed during native input check')
  return {'identity':observed,'sameHeldFilesystemObject':True,'pathRelocated':observed['path']!=before['path'],'manager':manager}
 def interrupt(signum,frame):raise KeyboardInterrupt('Owned native dialog check interrupted')
 for signum in (signal.SIGINT,signal.SIGTERM):signal.signal(signum,interrupt)
 record()
 try:
  for label,udid in TARGETS.items():
   need(states()==initial_states,'Simulator boot state changed before native input case')
   check_data(label);entry={'device':label,'udid':udid,'samples':[]};result['caseResults'].append(entry)
   directory=case/label;directory.mkdir();exe=Path(target_state[label]['executable'])
   need(sha(exe)==NEW_SIM and not matching_pids(exe),'Target executable changed or is active')
   serverlog=(directory/'fixture.log').open('xb')
   server=subprocess.Popen([sys.executable,'ios/ui-tests/dialog_fixture_server.py','--output',str(directory)],cwd=ROOT,stdin=subprocess.DEVNULL,stdout=serverlog,stderr=subprocess.STDOUT,start_new_session=True)
   entry['serverPID']=server.pid;record()
   ready_path=directory/'server-ready.json';deadline=time.monotonic()+8
   while not ready_path.is_file() and server.poll() is None and time.monotonic()<deadline:time.sleep(0.1)
   need(ready_path.is_file() and server.poll() is None,'Owned local dialog fixture did not become ready')
   ready=json.loads(ready_path.read_text());need(ready['pid']==server.pid and ready['host']=='127.0.0.1' and 0<ready['port']<65536 and ready['fixtureSha256']==manifest['after']['ios/ui-tests/dialog_fixture.html'],'Fixture identity differs')
   base='http://127.0.0.1:'+str(ready['port'])
   with urllib.request.urlopen(base+'/health',timeout=3) as response:need(response.status==200,'Owned dialog fixture health failed')
   data=Path(target_state[label]['dataIdentity']['path']);profile=data/'tmp/lume-native-dialog02'
   xcresult=directory/'LumenDialog.xcresult'
   args=['xcodebuild','test','-project','ios/ui-tests/LumenUITests.xcodeproj','-scheme','LumenUITests','-sdk','iphonesimulator',
    '-destination','platform=iOS Simulator,id='+udid,'-derivedDataPath','build-ui-tests/DerivedData','-resultBundlePath',str(xcresult),
    '-parallel-testing-enabled','NO','-maximum-concurrent-test-simulator-destinations','1','-jobs','2','-collect-test-diagnostics','never',
    '-only-testing:LumenUITests/LumenUITests/testNativeDialogVisibleTapAndClose','CODE_SIGNING_ALLOWED=NO',
    'LUMEN_UI_TEST_BASE='+base,'LUMEN_UI_TEST_PROFILE_ROOT='+str(profile)]
   entry['command']=args;log_path=directory/'xcodebuild.log'
   (ROOT/'out/verification/monitor-scenario.txt').write_text('native-dialog-'+label+'\n')
   start=time.monotonic();due=start;previous={};prior_time=None
   with log_path.open('xb') as log:
    active=subprocess.Popen(args,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    entry['ownedProcessGroup']=active.pid;record()
    try:
     while active.poll() is None:
      rows=sample_processes();now=time.monotonic();add_cpu_deltas(rows,previous,now-prior_time if prior_time else 0);previous={r['pid']:r for r in rows};prior_time=now
      selected={r['pid']:r for r in descendants(rows,active.pid)};selected.update({r['pid']:r for r in descendants(rows,server.pid)});browser={}
      found=matching_pids(exe);need(len(found)<=1,'Ambiguous simulator app process ownership')
      for pid in found:
       token=command(['/bin/ps','-p',str(pid),'-o','lstart=','-o','uid=','-o','comm='],3).strip()
       need(pid not in owned_apps or owned_apps[pid]['identity']==token,'Owned app PID identity changed')
       owned_apps[pid]={'device':label,'udid':udid,'executable':str(exe),'identity':token}
       browser.update({r['pid']:r for r in descendants(rows,pid)})
      selected.update(browser);owned=list(selected.values());browsers=list(browser.values())
      def cpu(items):
       values=[r['cpu_interval_percent'] for r in items if r['cpu_interval_percent'] is not None]
       return sum(values) if values else None
      sample={'elapsedSeconds':round(now-start,3),'ownedRSSMiBSum':sum(r['rss_kib'] for r in owned)/1024,'ownedCPUOneCorePercent':cpu(owned),
       'browserRSSMiBSum':sum(r['rss_kib'] for r in browsers)/1024,'browserCPUOneCorePercent':cpu(browsers),'browserObserved':bool(browser),
       'freeDiskMiB':shutil.disk_usage(ROOT).free/1024**2,'monitorAgeSeconds':heartbeat_age(monitor)}
      entry['samples'].append(sample)
      need(0<=sample['monitorAgeSeconds']<6 and now-start<180 and sample['ownedRSSMiBSum']<4096 and sample['freeDiskMiB']>=1024,'Native input job exceeded monitoring/time/memory/disk bound')
      need(not any(r['name'] in ('ninja','gn','cargo','rustc') for r in owned),'Unexpected dependency build during XCTest')
      record();due+=1;time.sleep(max(0,due-time.monotonic()))
    finally:
     stop(active);entry['xcodebuildExit']=active.wait();active=None;entry['seconds']=round(time.monotonic()-start,3);entry['xcodebuildLogSha256']=sha(log_path);record()
   stop(server);entry['serverExit']=server.wait();server=None;serverlog.close()
   need(log_path.stat().st_size<8*1024*1024 and xcresult.is_dir(),'Missing bounded real XCTest result')
   summary=json.loads(command(['xcrun','xcresulttool','get','test-results','summary','--path',str(xcresult)],20));entry['xctestSummary']=summary
   attachments=ROOT/'out/verification'/('apple-native-dialog02-'+label+'-attachments');need(not attachments.exists(),'Preserve prior native dialog screenshots')
   command(['xcrun','xcresulttool','export','attachments','--path',str(xcresult),'--output-path',str(attachments)],30)
   exported=json.loads((attachments/'manifest.json').read_text());entry['attachments']=[]
   for test in exported:
    if 'testNativeDialogVisibleTapAndClose' not in test.get('testIdentifier',''):continue
    for item in test.get('attachments',[]):
     name=item.get('suggestedHumanReadableName','')
     if not name.startswith('native-dialog-'):continue
     path=(attachments/item['exportedFileName']).resolve();need(path.is_relative_to(attachments.resolve()) and path.is_file(),'Unexpected XCTest attachment path')
     with path.open('rb') as f:magic=f.read(8)
     entry['attachments'].append({'name':name,'path':str(path.relative_to(ROOT)),'sha256':sha(path),'bytes':path.stat().st_size,'validPNG':magic==b'\x89PNG\r\n\x1a\n'})
   event_path=directory/'events.json';requests_path=directory/'document-requests.json'
   need(event_path.is_file() and event_path.stat().st_size<65536 and requests_path.is_file(),'Missing bounded independent dialog event evidence')
   runs=json.loads(event_path.read_text());requests=json.loads(requests_path.read_text());need(len(runs)==1 and len(requests)==1,'Expected exactly one fixture document/run')
   run_id,next_state=next(iter(runs.items()));events=next_state.get('events',[]);entry['independentEvents']=events;entry['documentRequests']=requests.get(run_id);entry['eventSha256']=sha(event_path)
   expected_types=['ready','opened','click','submit','closed','opened','click','submit','closed']
   nonce=events[0].get('pageNonce') if events else None
   event_pass=(len(events)==9 and [e.get('type') for e in events]==expected_types and isinstance(nonce,str) and bool(nonce) and requests.get(run_id)==1 and
    all(e.get('pageNonce')==nonce and e.get('simulation') is True and e.get('authenticated') is False and e.get('providerContacted') is False for e in events) and
    events[0].get('dialogOpen') is False and events[0].get('returnValue')=='' and
    all(events[i].get('trusted') is True and events[i].get('target')=='open' and events[i].get('dialogOpen') is True for i in (1,5)) and
    all(events[i].get('trusted') is True and events[i].get('target')==target for i,target in ((2,'accept'),(6,'cancel'))) and
    all(events[i].get('submitter')==target for i,target in ((3,'accept'),(7,'cancel'))) and
    all(events[i].get('dialogOpen') is False and events[i].get('returnValue')==value and events[i].get('submits')==count and events[i].get('closes')==count for i,value,count in ((4,'accepted',1),(8,'cancel',2))))
   required_images=('native-dialog-initially-closed','native-dialog-accepted-open','native-dialog-accepted-closed','native-dialog-cancel-open','native-dialog-cancel-closed')
   image_pass=all(any(a['name'].startswith(name) and a['validPNG'] for a in entry['attachments']) for name in required_images)
   entry['dataAfter']=check_data(label);entry['installedBinaryUnchanged']=sha(container(udid,'app')/'Lumen')==NEW_SIM
   entry['passed']=bool(entry['xcodebuildExit']==0 and entry['serverExit']==0 and summary.get('passedTests')==1 and summary.get('failedTests')==0 and summary.get('skippedTests')==0 and summary.get('totalTestCount')==1 and event_pass and image_pass and entry['installedBinaryUnchanged'] and not matching_pids(exe) and sum(s['browserObserved'] for s in entry['samples'])>=5)
   entry['nativeEventsAccepted']=event_pass;entry['requiredActualImagesPresent']=image_pass;record();need(entry['passed'],'Actual native dialog case failed: '+label)
  result['passed']=len(result['caseResults'])==2 and all(e.get('passed') for e in result['caseResults'])
 except (Exception,KeyboardInterrupt) as error:result['error']=type(error).__name__+': '+str(error)[:500]
 finally:
  stop(active);stop(server)
  for pid,own in owned_apps.items():
   try:
    if pid in matching_pids(Path(own['executable'])):
     token=command(['/bin/ps','-p',str(pid),'-o','lstart=','-o','uid=','-o','comm='],3).strip()
     need(token==own['identity'],'Owned app identity changed; no signal sent')
     command(['xcrun','simctl','terminate',own['udid'],BUNDLE],12)
   except Exception as error:cleanup.append(type(error).__name__+': '+str(error)[:200])
  try:
   result['bootStatesPreserved']=states()==initial_states
   result['dataObjectsPreserved']=all(check_data(label)['sameHeldFilesystemObject'] for label in TARGETS)
   result['installedBinariesUnchanged']=all(sha(container(udid,'app')/'Lumen')==NEW_SIM for udid in TARGETS.values())
   result['buildBinariesUnchanged']=sha(ROOT/'build-sim/Lumen.app/Lumen')==NEW_SIM and sha(ROOT/'build/lumen')==MAC
   result['testSourcesUnchanged']=all(sha(ROOT/name)==want for name,want in manifest['after'].items())
   result['acceptedApple02Unchanged']=sha(acceptance)==REPORT02
   result['priorInterruptedAttemptUnchanged']=sha(prior_attempt)==result['priorInterruptedAttempt']['sha256']
   result['passed']=bool(result['passed'] and all(result[k] for k in ('bootStatesPreserved','dataObjectsPreserved','installedBinariesUnchanged','buildBinariesUnchanged','testSourcesUnchanged','acceptedApple02Unchanged','priorInterruptedAttemptUnchanged')) and not cleanup)
  except Exception as error:result['passed']=False;cleanup.append(type(error).__name__+': '+str(error)[:200])
  for fd in fds.values():os.close(fd)
  result['cleanupErrors']=cleanup;result['running']=False;result['finishedUnix']=time.time();record()
  (ROOT/'out/verification/monitor-scenario.txt').write_text('idle-between-scenarios\n')
 print(json.dumps({'passed':result['passed'],'running':False,'error':result.get('error'),'report':str(report.relative_to(ROOT)),'caseCount':len(result['caseResults']),'cleanupErrors':cleanup}))
 return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
