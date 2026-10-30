#!/usr/bin/env python3
"""Actual bounded red/green layout regression; no browser or V8 relink."""
from pathlib import Path
import hashlib,json,os,re,shutil,signal,stat,subprocess,sys,time
ROOT=Path('/Users/devin/repos/lumen');OUT=ROOT/'out/native-verification'
sys.path.insert(0,str(ROOT/'verification'))
from resource_monitor import sample_processes,add_cpu_deltas
from native_case import descendants
BEFORE='8e681d7e1738caf635d715250ae2c8c1cbc8847153529b960fed270c3aa3d7a6'
AFTER='62b887bfa76257d2120ae925168ac5556009c904a894626ff6711aacd56a2ff4'
FIXTURE='30dd22d93055162f305b4d6e8e621693e3bb586b1b1e56bd643c9d606dfa801b'
SUPPLEMENTAL='8ccaa32b91ad6fd63d95e8558542759b2623e5f98bcce8ccc82060e336612cf7'
APP='707e3c21ffac64c1583741ae2a40474372341cae1da947a21e41f2d4ee43fd24'
ANCHOR='        if (!len_def(s->height, ph) && !tA && !bA) fh = ph - t - bt - a->m[0] - a->m[2];'
PATCH='''        /* Only auto height stretches between opposing insets. Intrinsic sizes
           (including modal dialog fit-content) must keep their content height. */
        if (s->height.kind == LK_AUTO && !tA && !bA) fh = ph - t - bt - a->m[0] - a->m[2];'''
def need(ok,why):
 if not ok:raise RuntimeError(why)
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def heartbeat():
 p=ROOT/'out/verification/resource-monitor.jsonl';mtime=p.stat().st_mtime
 return time.time()-mtime

def main():
 need(sys.platform=='darwin' and Path.cwd().resolve()==ROOT,'Unexpected layout regression workspace')
 case=OUT/'dialog-intrinsic-height01';need(not case.exists(),'Preserve earlier layout evidence')
 source=ROOT/'src/layout/block.c';test=ROOT/'tests/test_dialog_intrinsic_height.c'
 staged=OUT/'dialog-intrinsic-height01-input/test_dialog_intrinsic_height.c'
 supplemental=OUT/'dialog-intrinsic-height01-input/layout.mk'
 need(not test.exists(),'Preserve existing test source')
 need(sha(source)==BEFORE and sha(staged)==FIXTURE and sha(supplemental)==SUPPLEMENTAL,'Reviewed source/fixture/supplemental target changed')
 need(sha(ROOT/'Makefile')=='b16e593d4304a34d4d0b16879d1bde3ecf7f2c1f2c46453dc3428acfbb5c71c1','Shared Makefile changed')
 need(sha(ROOT/'build/lumen')==APP,'Accepted browser changed')
 need(0<=heartbeat()<6,'Required one-second monitor is stale')
 need(shutil.disk_usage(ROOT).free>=2*1024**3,'Need2GiB before bounded incremental regression')
 rows=subprocess.check_output(['ps','-axo','comm='],text=True,timeout=5).splitlines()
 need(not any(Path(x.strip()).name in {'lumen','Lumen','clang','clang++','ld','ninja','cargo','rustc','xcodebuild','swift-frontend','xctest'} for x in rows),'Wait for reserved compiler/GUI slot')
 original=source.read_text();need(original.count(ANCHOR)==1,'Exact layout anchor changed')
 corrected=original.replace(ANCHOR,PATCH,1);need(hashlib.sha256(corrected.encode()).hexdigest()==AFTER,'Correction fingerprint differs')
 paths=[p for p in (ROOT/'src').rglob('*') if p.is_file() and p.suffix in ('.c','.cc','.h','.m','.mm','.js') and p!=source]
 paths += [ROOT/'Makefile',supplemental,staged,Path(__file__),ROOT/'verification/resource_monitor.py',ROOT/'verification/native_case.py']
 tracked={str(p.relative_to(ROOT)):sha(p) for p in paths}
 binaries={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'build/lumen',ROOT/'out/native-verification/mac-webtransport-candidate01/Lumen.app/Contents/MacOS/lumen',ROOT/'build-sim/Lumen.app/Lumen',ROOT/'build-ios/Lumen.app/Lumen']}
 os.umask(0o077);case.mkdir(mode=0o700);backup=case/'block.c.before';shutil.copy2(source,backup);need(sha(backup)==BEFORE,'Original backup differs')
 report=case/'result.json';active=None;patched=False;created_test=False
 result={'schemaVersion':1,'scope':'Actual HTML, UA/author cascade and positioned intrinsic-height layout; no modal focus/authentication claim','running':True,'passed':False,'startUnix':time.time(),'sourceHashes':{'before':{'src/layout/block.c':BEFORE},'after':{'src/layout/block.c':AFTER,'tests/test_dialog_intrinsic_height.c':FIXTURE}},'commands':[],'browserRelinked':False,'appleArtifactsRebuilt':False,'v8CompilationRequested':False,'sharedMakefileModified':False,'bounds':{'jobs':2,'buildSeconds':120,'testSeconds':12,'maximumOwnedRSSMiB':2048,'minimumFreeDiskMiB':1024},'cleanupErrors':[]}
 def record():
  p=report.with_suffix('.writing');p.write_text(json.dumps(result,indent=2)+'\n');p.replace(report)
 def stop():
  if active is not None and active.poll() is None:
   os.killpg(active.pid,signal.SIGTERM)
   try:active.wait(timeout=4)
   except subprocess.TimeoutExpired:os.killpg(active.pid,signal.SIGKILL);active.wait(timeout=3)
 def interrupted(signum,frame):raise KeyboardInterrupt('Owned layout regression interrupted')
 for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,interrupted)
 def run(label,args,bound,expected):
  nonlocal active
  begin=time.monotonic();due=begin;prior=None;previous={};log=case/(label+'.log')
  entry={'label':label,'command':args,'log':str(log.relative_to(ROOT)),'samples':[],'expectedExit':expected};result['commands'].append(entry)
  with log.open('xb') as h:
   active=subprocess.Popen(args,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=h,stderr=subprocess.STDOUT,start_new_session=True)
   entry['ownedProcessGroup']=active.pid;record()
   try:
    while active.poll() is None:
     allrows=sample_processes();now=time.monotonic();add_cpu_deltas(allrows,previous,now-prior if prior else 0)
     previous={x['pid']:x for x in allrows};prior=now;owned=descendants(allrows,active.pid)
     cpus=[x['cpu_interval_percent'] for x in owned if x['cpu_interval_percent'] is not None]
     row={'elapsedSeconds':round(now-begin,3),'rssMiBSum':sum(x['rss_kib'] for x in owned)/1024,'cpuOneCorePercent':sum(cpus) if cpus else None,'freeDiskMiB':shutil.disk_usage(ROOT).free/1024**2,'monitorAgeSeconds':heartbeat()};entry['samples'].append(row)
     if not 0<=row['monitorAgeSeconds']<6 or row['rssMiBSum']>2048 or row['freeDiskMiB']<1024 or now-begin>bound:
      entry['stoppedForBound']=True;break
     if log.stat().st_size>2*1024*1024:entry['stoppedForBound']='log';break
     record();due+=1;time.sleep(max(0,due-time.monotonic()))
   finally:stop();entry['exit']=active.wait();active=None
  entry['seconds']=round(time.monotonic()-begin,3);entry['logSHA256']=sha(log);entry['peakRSSMiBSum']=max((x['rssMiBSum'] for x in entry['samples']),default=0)
  need(log.stat().st_size<=2*1024*1024,'Regression log exceeded bound')
  text=log.read_text(errors='replace');entry['passedExpectedOutcome']=entry['exit']==expected and not entry.get('stoppedForBound');record()
  need(entry['passedExpectedOutcome'],'Unexpected '+label+' outcome')
  return text
 record();scenario=ROOT/'out/verification/monitor-scenario.txt'
 try:
  scenario.write_text('dialog-intrinsic-height-regression\n')
  with test.open('xb') as h:
   created_test=True;h.write(staged.read_bytes())
  need(sha(test)==FIXTURE,'Installed fixture differs')
  cmd=['make','-f','Makefile','-f',str(supplemental.relative_to(ROOT)),'-j2','RTC=1','WT=1','build/test_dialog_intrinsic_height']
  red_build=run('red-build',cmd,120,0);need(not any(x in red_build for x in ('tools/build-v8','ninja -C','gn gen')),'Unexpected dependency compilation')
  red=run('red-layout',['./build/test_dialog_intrinsic_height'],12,1)
  m=re.search(r'DIALOG_INTRINSIC_HEIGHT_RESULT checks=(\d+) passed=(\d+) failed=(\d+)',red)
  need(m and int(m[1])==40 and int(m[3])>0,'Baseline did not execute all40 checks')
  need('default modal height is content plus padding and border' in red and 'ordinary fixed fit-content retains intrinsic height' in red,'Observed defect was not reproduced')
  result['red']={'checks':int(m[1]),'passed':int(m[2]),'failed':int(m[3]),'testBinarySHA256':sha(ROOT/'build/test_dialog_intrinsic_height')}
  need(sha(source)==BEFORE,'Layout source changed before correction')
  temp=source.with_name('.block.c.dialog-intrinsic-height01');need(not temp.exists(),'Preserve unexpected patch temporary')
  with temp.open('x') as h:h.write(corrected)
  os.chmod(temp,stat.S_IMODE(source.stat().st_mode));patched=True;temp.replace(source)
  need(sha(source)==AFTER,'Corrected layout hash differs')
  run('green-build',cmd,120,0);green=run('green-layout',['./build/test_dialog_intrinsic_height'],12,0)
  need('DIALOG_INTRINSIC_HEIGHT_RESULT checks=40 passed=40 failed=0' in green,'Corrected real layout regression did not pass40/40')
  result['green']={'checks':40,'passed':40,'failed':0,'testBinarySHA256':sha(ROOT/'build/test_dialog_intrinsic_height')};result['functionalRedGreenPassed']=True
 except (Exception,KeyboardInterrupt) as e:result['error']=type(e).__name__+': '+str(e)[:400]
 finally:
  try:stop()
  except Exception as e:result['cleanupErrors'].append('owned process: '+str(e)[:200])
  def matches(path,expected):
   try:return path.is_file() and sha(path)==expected
   except OSError as error:
    result['cleanupErrors'].append('integrity read: '+str(path.relative_to(ROOT))+': '+type(error).__name__);return False
  result['otherSourcesUnchanged']=all(matches(ROOT/n,v) for n,v in tracked.items())
  result['acceptedBinariesUnchanged']=all(matches(ROOT/n,v) for n,v in binaries.items())
  result['passed']=bool(result.get('functionalRedGreenPassed') and not result.get('error') and not result['cleanupErrors'] and result['otherSourcesUnchanged'] and result['acceptedBinariesUnchanged'] and matches(source,AFTER) and matches(test,FIXTURE))
  if not result['passed']:
   if patched:
    try:
     if matches(source,AFTER) and matches(backup,BEFORE):shutil.copy2(backup,source);os.utime(source,None)
     result['failedCorrectionRestored']=matches(source,BEFORE)
    except OSError as error:result['cleanupErrors'].append('source restoration: '+type(error).__name__);result['failedCorrectionRestored']=False
   if created_test and matches(test,FIXTURE):
    try:test.unlink();result['failedFixtureSourceRemoved']=True
    except OSError as error:result['cleanupErrors'].append('fixture cleanup: '+type(error).__name__)
  try:result['finalLayoutSHA256']=sha(source)
  except OSError:result['finalLayoutSHA256']=None
  result['finishedUnix']=time.time();result['running']=False
  try:scenario.write_text('idle-between-scenarios\n')
  except OSError as error:result['cleanupErrors'].append('scenario reset: '+type(error).__name__)
  record()
 print(json.dumps({k:v for k,v in result.items() if k!='commands'},indent=2))
 return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
