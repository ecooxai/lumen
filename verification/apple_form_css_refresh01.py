#!/usr/bin/env python3
"""Incremental Apple app refresh; retain V8 archives and prior artifacts/data."""
from pathlib import Path
import hashlib,json,os,plistlib,re,shutil,signal,subprocess,sys,time,urllib.request,zipfile
ROOT=Path('/Users/devin/repos/lumen')
sys.path.insert(0,str(ROOT/'verification'))
from resource_monitor import sample_processes,add_cpu_deltas
from native_case import descendants
from bundle_case import matching_pids,structured_evidence
TARGETS={'iphone':'EDD9B083-63F1-44E6-9A7B-0A6964CC2561','ipad':'590ACCA9-0BA2-4803-970F-EF3DAC515255'}
OLD_SIM='b7e581006bd0afc77aabddfcf0348478c3baa59c8d996101cd5c9609efd20b73'
MAC='707e3c21ffac64c1583741ae2a40474372341cae1da947a21e41f2d4ee43fd24'
BUNDLE='com.ecooxai.lumen'

def need(ok,why):
 if not ok:raise RuntimeError(why)
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def command(args,timeout=15):
 p=subprocess.run(args,cwd=ROOT,stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=timeout)
 need(p.returncode==0,'Read/lifecycle command failed: '+str(args[:3])+': '+p.stderr[-250:])
 need(len(p.stdout)<2*1024*1024,'Bounded command output exceeded')
 return p.stdout

def states():
 d=json.loads(command(['xcrun','simctl','list','devices','--json']))
 return {v['udid']:v['state'] for group in d['devices'].values() for v in group}
def container(udid,kind):
 return Path(command(['xcrun','simctl','get_app_container',udid,BUNDLE,kind]).strip()).resolve()
def identity(p):
 s=p.stat();return {'path':str(p.resolve()),'device':s.st_dev,'inode':s.st_ino}
def tree(p):
 return {str(f.relative_to(p)):sha(f) for f in p.rglob('*') if f.is_file()}
def identity_pass(r):
 checks=r.get('checks',[])
 return (r.get('status')=='pass' and r.get('providerAuthenticationTested') is False and r.get('resourcesReleased') is True
   and len(checks)==4 and {(x.get('provider'),x.get('choice')) for x in checks}=={(p,c) for p in ('google','apple') for c in ('accepted','cancel')}
   and all(x.get('passed') is True and x.get('dialogOpened') is True and x.get('dialogClosed') is True and
       x.get('simulation') is True and x.get('authenticated') is False and x.get('providerContacted') is False and
       x.get('syntheticClick') is True and x.get('returnedChoice')==x.get('choice') and x.get('state')=='configuration' for x in checks)
   and all(r.get('resources',{}).get(k)==0 for k in ('activeScopes','registeredCleanups','activeMediaTracks')))

def main():
 need(sys.platform=='darwin' and Path.cwd().resolve()==ROOT,'Unexpected Apple refresh workspace')
 out=ROOT/'out/native-verification';case=out/'apple-form-css-refresh01';report=case/'result.json'
 need(not case.exists(),'Preserve existing Apple refresh evidence')
 basis=json.loads((out/'apple-form-css-refresh01-basis.json').read_text())
 monitor=ROOT/'out/verification/resource-monitor.jsonl'
 need(monitor.is_file() and 0<=time.time()-monitor.stat().st_mtime<6,'Fresh one-second monitor required')
 need(shutil.disk_usage(ROOT).free>=3*1024**3,'Keep3GiB before verified artifact backup')
 names=command(['ps','-axo','comm='])
 need(not any(Path(s.strip()).name in {'clang','clang++','ld','ninja','cargo','rustc','xcodebuild','swift-frontend','xctest','lumen','Lumen'} for s in names.splitlines()),'Compiler/GUI slot is busy')
 gate=json.loads((out/'webtransport-native-gate04.json').read_text())
 need(gate.get('passed') is True and gate.get('running') is False,'Require completed current native gate')
 source_hashes={**gate['sourceHashes'],**basis['sources']}
 for name,want in source_hashes.items():need(sha(ROOT/name)==want,'Frozen engine/helper source changed: '+name)
 for name,want in basis['archives'].items():need(sha(ROOT/name)==want,'Existing Apple archive/cache changed: '+name)
 for name,want in basis['artifacts'].items():need(sha(ROOT/name)==want,'Prior Apple artifact changed: '+name)
 need(sha(ROOT/'build/lumen')==MAC,'Accepted Mac candidate changed')
 initial_states=states();initial_targets={}
 for label,udid in TARGETS.items():
  need(initial_states.get(udid)=='Booted','Only already-booted designated targets may be updated')
  app=container(udid,'app');data=container(udid,'data')
  need(sha(app/'Lumen')==OLD_SIM and not matching_pids(app/'Lumen'),'Prior target app differs or is active')
  initial_targets[label]={'udid':udid,'dataIdentity':identity(data),'priorAppPath':str(app),'priorBinarySHA256':OLD_SIM}
 port=(ROOT/'out/verification/lab-server-port.txt').read_text().strip();need(port.isdigit() and 0<int(port)<65536,'Invalid lab port')
 for relative,want in basis['servedLabSources'].items():
  with urllib.request.urlopen('http://127.0.0.1:'+port+'/'+relative,timeout=3) as response:
   data=response.read(2*1024*1024+1);need(len(data)<=2*1024*1024 and hashlib.sha256(data).hexdigest()==want,'Served lab source differs: '+relative)
 os.umask(0o077);case.mkdir(mode=0o700)
 result={'schemaVersion':1,'scope':'Incremental simulator/device packaging plus actual two-simulator form/CSS/WASM checks',
  'running':True,'passed':False,'phase':'backup','startUnixSeconds':time.time(),'commands':[],'bundles':{},'cases':[],
  'sourceHashes':source_hashes,'v8ArchiveHashes':basis['archives'],'priorArtifactHashes':basis['artifacts'],
  'initialBootStates':initial_states,'targets':initial_targets,'bounds':{'jobs':2,'maximumOwnedRSSMiB':4096,'minimumFreeDiskMiB':1024},
  'physicalDeviceRuntimeVerified':False,'sameIpaMacRuntimeVerified':False,'realProviderAuthenticationVerified':False,
  'compositorPixelsReviewed':False,'performanceComparisonClaimed':False,'bootOrPreferencesChangedByRunner':False,
  'newV8BuildRequested':False,'webTransportEnabledOnApple':False}
 active=None;installed=[];owned_sim={};built=False
 def record():
  tmp=report.with_suffix('.tmp');tmp.write_text(json.dumps(result,indent=2)+'\n');tmp.replace(report)
 def stop():
  nonlocal active
  if active is not None and active.poll() is None:
   try:os.killpg(active.pid,signal.SIGINT)
   except ProcessLookupError:pass
   try:active.wait(timeout=12)
   except subprocess.TimeoutExpired:
    try:os.killpg(active.pid,signal.SIGTERM)
    except ProcessLookupError:pass
    try:active.wait(timeout=5)
    except subprocess.TimeoutExpired:
     try:os.killpg(active.pid,signal.SIGKILL)
     except ProcessLookupError:pass
     active.wait(timeout=3)
 def interrupt(signum,frame):raise KeyboardInterrupt('Owned Apple refresh interrupted')
 for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,interrupt)
 def run(label,args,limit,sim=None,require_exit=True):
  nonlocal active
  log=case/(label+'.log');entry={'label':label,'command':args,'log':str(log.relative_to(ROOT)),'samples':[]};result['commands'].append(entry)
  begin=time.monotonic();due=begin;prior=None;previous={}
  with log.open('xb') as f:
   active=subprocess.Popen(args,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
   entry['ownedProcessGroup']=active.pid;record()
   try:
    while active.poll() is None:
     rows=sample_processes();now=time.monotonic();add_cpu_deltas(rows,previous,now-prior if prior else 0)
     previous,prior={r['pid']:r for r in rows},now;selected={r['pid']:r for r in descendants(rows,active.pid)}
     if sim:
      udid,exe=sim
      for pid in matching_pids(exe):
       token=command(['/bin/ps','-p',str(pid),'-o','lstart=','-o','comm='],3).strip()
       need(pid not in owned_sim or owned_sim[pid]['identity']==token,'Target PID was reused')
       owned_sim[pid]={'udid':udid,'exe':str(exe),'identity':token}
       selected.update({r['pid']:r for r in descendants(rows,pid)})
     owned=list(selected.values());cpu=[r['cpu_interval_percent'] for r in owned if r['cpu_interval_percent'] is not None]
     sample={'elapsedSeconds':round(now-begin,3),'rssMiBSum':sum(r['rss_kib'] for r in owned)/1024,'cpuOneCorePercent':sum(cpu) if cpu else None,
             'freeDiskMiB':shutil.disk_usage(ROOT).free/1024**2,'monitorAgeSeconds':time.time()-monitor.stat().st_mtime if monitor.exists() else None}
     entry['samples'].append(sample)
     need(sample['monitorAgeSeconds'] is not None and 0<=sample['monitorAgeSeconds']<6,'Required monitor became stale')
     need(now-begin<limit and sample['rssMiBSum']<4096 and sample['freeDiskMiB']>=1024,'Owned job exceeded time/memory/disk bound')
     need(not any(r['name'] in ('ninja','gn','cargo','rustc') for r in owned),'Unexpected dependency compilation')
     record();due+=1;time.sleep(max(0,due-time.monotonic()))
   finally:stop();entry['exitCode']=active.wait();active=None
  entry['seconds']=round(time.monotonic()-begin,3);entry['logSha256']=sha(log);entry['peakRSSMiBSum']=max((r['rssMiBSum'] for r in entry['samples']),default=0);record()
  need(log.stat().st_size<=8*1024*1024,'Owned log exceeded bound')
  if require_exit:need(entry['exitCode']==0,'Owned command failed: '+label)
  return entry
 def check_target(label,newhash):
  udid=TARGETS[label];app=container(udid,'app');data=container(udid,'data')
  need(identity(data)==initial_targets[label]['dataIdentity'],'Simulator data container identity changed')
  need(sha(app/'Lumen')==newhash,'Installed binary differs from new artifact')
  return app/'Lumen'
 def read_records(path,marker):
  need(path.is_file() and path.stat().st_size<2*1024*1024,'Missing/oversized simulator evidence')
  return structured_evidence(path.read_text(errors='replace').splitlines(),marker)
 record()
 try:
  (ROOT/'out/verification/monitor-scenario.txt').write_text('apple-form-css-artifact-refresh\n')
  previous=case/'previous';previous.mkdir()
  for platform in ('sim','ios'):
   dest=previous/platform;dest.mkdir();source=ROOT/('build-'+platform)
   prior_tree=tree(source/'Lumen.app');shutil.copytree(source/'Lumen.app',dest/'Lumen.app',symlinks=True)
   need(tree(dest/'Lumen.app')==prior_tree,'Prior bundle backup differs')
   shutil.copy2(source/'lumen',dest/'lumen')
   ipa=source/'Lumen-jitless.ipa'
   if ipa.is_file():shutil.copy2(ipa,dest/ipa.name)
   need(sha(dest/'lumen')==basis['artifacts']['build-'+platform+'/lumen'],'Prior binary backup differs')
   if ipa.is_file():need(sha(dest/ipa.name)==basis['artifacts']['build-'+platform+'/Lumen-jitless.ipa'],'Prior IPA backup differs')
  result['priorArtifactsBackedUp']=True;record()
  result['phase']='incremental-build';record()
  for platform,target in (('sim','app'),('ios','ipa')):
   start=time.time();run('build-'+platform,['make','-j2','PLAT='+platform,'RTC=1','WT=0',target],360)
   build=ROOT/('build-'+platform);app=build/'Lumen.app';exe=app/'Lumen';info=plistlib.loads((app/'Info.plist').read_bytes())
   need(exe.stat().st_mtime>=start,'App was not freshly repackaged')
   need((build/'rtc-config.h').read_text().strip()=='#define LUMEN_RTC_NATIVE 1','Apple RTC must stay enabled')
   need((build/'webtransport-config.h').read_text().strip()=='#define LUMEN_WEBTRANSPORT_NATIVE 0','Apple WT must stay honestly disabled')
   need(info.get('CFBundleIdentifier')==BUNDLE and info.get('UIDeviceFamily')==[1,2],'Shared Apple bundle identity/families changed')
   arch=command(['lipo','-archs',str(exe)]).strip();need(arch=='arm64','Wrong Apple architecture')
   description=command(['xcrun','vtool','-show-build',str(exe)]);m=re.search(r'platform\s+(\S+)',description)
   actual=m.group(1) if m else None;need(actual==('IOSSIMULATOR' if platform=='sim' else 'IOS'),'Wrong real Mach-O platform')
   command(['codesign','--verify','--strict',str(app)],20)
   result['bundles'][platform]={'path':str(app.relative_to(ROOT)),'binarySHA256':sha(exe),'buildBinarySHA256':sha(build/'lumen'),
     'bytes':exe.stat().st_size,'architecture':arch,'machoPlatform':actual,'info':{k:info.get(k) for k in ('CFBundleIdentifier','CFBundleSupportedPlatforms','UIDeviceFamily','MinimumOSVersion')},
     'provisioningProfilePresent':(app/'embedded.mobileprovision').is_file()}
   record()
  ipa=ROOT/'build-ios/Lumen-jitless.ipa'
  with zipfile.ZipFile(ipa) as z:
   body=z.read('Payload/Lumen.app/Lumen');result['ipa']={'path':str(ipa.relative_to(ROOT)),'sha256':sha(ipa),'bytes':ipa.stat().st_size,
    'containedBinarySHA256':hashlib.sha256(body).hexdigest(),'provisioningProfilePresent':'Payload/Lumen.app/embedded.mobileprovision' in z.namelist()}
  need(result['ipa']['containedBinarySHA256']==result['bundles']['ios']['binarySHA256'],'Packaged iOS binary differs')
  need(all(sha(ROOT/p)==h for p,h in basis['archives'].items()),'V8 archive/cache bytes changed during incremental build')
  built=True;result['buildsPassed']=True;result['phase']='install-and-focused-checks';record()
  newhash=result['bundles']['sim']['binarySHA256'];need(newhash!=OLD_SIM,'Simulator app did not incorporate changed source')
  for label,udid in TARGETS.items():
   need(states()==initial_states,'Boot states changed before target update')
   need(not matching_pids(container(udid,'app')/'Lumen'),'Target app became active before update')
   installed.append(label)
   run('install-'+label,['xcrun','simctl','install',udid,str(ROOT/'build-sim/Lumen.app')],40)
   exe=check_target(label,newhash)
   for kind,probe,duration in (('form-wasm','apple_form_wasm_probe.js','24'),('responsive','responsive_layout_probe_v1.js','27')):
    name='apple-form-css-refresh01-'+label+'-'+kind;prefix=ROOT/'out/verification'/name
    need(not list(prefix.parent.glob(name+'.*')),'Preserve prior actual simulator case')
    args=[sys.executable,'verification/simulator_browser_case.py','--device',label,'--name',name,'--url','http://127.0.0.1:'+port+'/',
          '--probe','verification/'+probe,'--duration',duration]
    if kind=='responsive':args.append('--capture-markers')
    run('check-'+label+'-'+kind,args,48,sim=(udid,exe))
    summary=json.loads(Path(str(prefix)+'.json').read_text())
    need(summary.get('binary_fingerprint',{}).get('sha256')==newhash and summary.get('installedMatchesCurrentBuildSim') is True,'Case does not match new Apple binary')
    need(not summary.get('failure') and summary.get('sample_count',0)>=15 and summary.get('samples_with_active_compilers')==0,'Case lifetime/compiler gate failed')
    item={'device':label,'kind':kind,'binarySHA256':newhash,'samples':summary['sample_count'],'peakRSSMiB':summary.get('rss_peak_mib'),
          'peakCPUOneCorePercent':summary.get('cpu_peak_percent'),'evidencePath':str(Path(str(prefix)+'.json').relative_to(ROOT))}
    if kind=='form-wasm':
     identities=read_records(Path(str(prefix)+'.log'),'LUME_IDENTITY_EVIDENCE ');wasm=read_records(Path(str(prefix)+'.log'),'LUME_APPLE_WASM_REUSE_EVIDENCE ')
     item['identity']=identities[-1] if identities else None;item['wasm']=wasm[-1] if wasm else None
     item['passed']=bool(item['identity'] and identity_pass(item['identity']) and item['wasm'] and item['wasm'].get('passed') is True and item['wasm'].get('actual')==42 and item['wasm'].get('moduleBytes')==41)
    else:
     records=read_records(Path(str(prefix)+'.log'),'LUME_LAB_EVIDENCE ')
     geometry={r['payload']['id']:r['payload'] for r in records if r.get('kind')=='responsive-geometry'}
     errors=[r.get('payload') for r in records if r.get('kind')=='responsive-error']
     canvas=all(geometry.get(k,{}).get('canvas',{}).get('fullCanvasInsideViewport') is True and geometry[k]['canvas'].get('intrinsicAspectPreserved') is True and geometry[k]['canvas'].get('objectFit')=='contain' for k in ('webgl','webgpu'))
     frame=geometry.get('iframe',{}).get('iframe',{})
     frame_ok=frame.get('fullFrameInsideViewport') is True and frame.get('allButtonsInsideChildViewport') is True and frame.get('flexWrap')=='wrap'
     phone=label!='iphone' or all(r.get('singlePhoneColumnObserved') is True for r in geometry.values())
     pngs={}
     for stage in ('overview','gpu','network'):
      entry=summary.get('owned_simulator_captures',{}).get(stage,{});p=Path(str(prefix)+'.'+stage+'.png')
      valid=entry.get('exit_code')==0 and entry.get('path')==str(p) and p.is_file() and p.stat().st_size>100
      if valid:
       with p.open('rb') as f:valid=f.read(8)==b'\x89PNG\r\n\x1a\n'
      pngs[stage]={'path':str(p.relative_to(ROOT)),'validPNG':valid,'sha256':sha(p) if valid else None}
     item.update(geometry=geometry,errors=errors,captures=pngs,cleanupVerified=summary.get('lab_cleanup_verified'))
     item['passed']=bool(len(geometry)==3 and canvas and frame_ok and phone and not errors and
       any(r.get('kind')=='responsive-completed' for r in records) and summary.get('lab_cleanup_verified') is True and
       all(summary.get('lab_results',{}).get(k,{}).get('state')=='pass' for k in ('webgl','webgpu','iframe')) and all(x['validPNG'] for x in pngs.values()))
    result['cases'].append(item);record();need(item['passed'],'Actual affected Apple flow failed: '+label+'/'+kind)
   check_target(label,newhash)
  result['passed']=True
 except (Exception,KeyboardInterrupt) as error:result['error']=type(error).__name__+': '+str(error)[:500]
 finally:
  stop();cleanup=[]
  for pid,entry in owned_sim.items():
   try:
    exe=Path(entry['exe'])
    if pid in matching_pids(exe):
     token=command(['/bin/ps','-p',str(pid),'-o','lstart=','-o','comm='],3).strip()
     if token==entry['identity']:command(['xcrun','simctl','terminate',entry['udid'],BUNDLE],15)
     else:cleanup.append('Owned simulator identity changed; no signal sent')
   except Exception as error:cleanup.append(type(error).__name__+': '+str(error)[:200])
  try:
   result['sourcesUnchanged']=all(sha(ROOT/p)==h for p,h in source_hashes.items())
   result['v8ArchivesAndCachesUnchanged']=all(sha(ROOT/p)==h for p,h in basis['archives'].items())
   result['macCandidateUnchanged']=sha(ROOT/'build/lumen')==MAC
   result['bootStatesPreserved']=states()==initial_states
   result['dataContainersPreserved']=all(identity(container(udid,'data'))==initial_targets[label]['dataIdentity'] for label,udid in TARGETS.items())
   result['passed']=bool(result['passed'] and result['sourcesUnchanged'] and result['v8ArchivesAndCachesUnchanged'] and result['macCandidateUnchanged'] and result['bootStatesPreserved'] and result['dataContainersPreserved'] and not cleanup)
  except Exception as error:result['passed']=False;cleanup.append(type(error).__name__+': '+str(error)[:200])
  if not result['passed'] and result.get('priorArtifactsBackedUp'):
   restoration={}
   for label in reversed(installed):
    try:
     udid=TARGETS[label];need(identity(container(udid,'data'))==initial_targets[label]['dataIdentity'],'Do not alter unexpected data container')
     command(['xcrun','simctl','install',udid,str(case/'previous/sim/Lumen.app')],40)
     restoration[label]=sha(container(udid,'app')/'Lumen')==OLD_SIM and identity(container(udid,'data'))==initial_targets[label]['dataIdentity']
    except Exception as error:restoration[label]=type(error).__name__+': '+str(error)[:200]
   result['priorInstalledAppsRestored']=restoration
   for platform in ('sim','ios'):
    try:
     build=ROOT/('build-'+platform);prior=case/'previous'/platform;failed=case/('unaccepted-'+platform);failed.mkdir()
     for name in ('Lumen.app','lumen','Lumen-jitless.ipa'):
      current=build/name;backup=prior/name
      if current.exists():current.rename(failed/name)
      if backup.is_dir():shutil.copytree(backup,current,symlinks=True)
      elif backup.is_file():shutil.copy2(backup,current)
     need(sha(build/'Lumen.app/Lumen')==basis['artifacts']['build-'+platform+'/Lumen.app/Lumen'],'Prior artifact restoration differs')
    except Exception as error:cleanup.append('Artifact restore '+platform+': '+type(error).__name__+': '+str(error)[:150])
  result['cleanupErrors']=cleanup;result['running']=False;result['phase']='complete';result['endUnixSeconds']=time.time();record()
  (ROOT/'out/verification/monitor-scenario.txt').write_text('idle-between-scenarios\n')
 print(json.dumps({k:v for k,v in result.items() if k not in ('commands','sourceHashes','v8ArchiveHashes','priorArtifactHashes','initialBootStates')}))
 return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
