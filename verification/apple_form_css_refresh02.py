#!/usr/bin/env python3
"""Continue retained Apple artifacts after independently proven container rename."""
from pathlib import Path
import hashlib,json,os,plistlib,re,shutil,signal,stat,subprocess,sys,time,urllib.request,zipfile
ROOT=Path('/Users/devin/repos/lumen')
sys.path.insert(0,str(ROOT/'verification'))
from resource_monitor import sample_processes,add_cpu_deltas
from native_case import descendants
from bundle_case import matching_pids,structured_evidence
TARGETS={'iphone':'EDD9B083-63F1-44E6-9A7B-0A6964CC2561','ipad':'590ACCA9-0BA2-4803-970F-EF3DAC515255'}
NEW_SIM='276823c0ce514a2609f86195035fa3dfdc96b4fd965f15ee9e531d0eb886362b'
FAILED_REPORT_SHA='2e627ff476c21ed3f42ca5b955a034942852fa7832277f5c5c5a03cacb1a9c51'
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
def scoped_data(udid):
 p=container(udid,'data')
 base=(Path('/Users/devin/Library/Developer/CoreSimulator/Devices')/udid/'data/Containers/Data/Application').resolve()
 relative=p.relative_to(base)
 need(len(relative.parts)==1 and re.fullmatch(r'[0-9A-Fa-f-]{36}',relative.name) is not None and not p.is_symlink(),'Unexpected canonical designated data-container scope')
 need(p.is_dir(),'Expected actual data directory')
 marker=p/'.com.apple.mobile_container_manager.metadata.plist'
 need(marker.is_file() and not marker.is_symlink() and marker.stat().st_size<65536,'Missing bounded container-manager identity')
 metadata=plistlib.loads(marker.read_bytes())
 need(metadata.get('MCMMetadataIdentifier')==BUNDLE and metadata.get('MCMMetadataContentClass')==2,'Unexpected container-manager owner/class')
 return p,{k:metadata.get(k) for k in ('MCMMetadataIdentifier','MCMMetadataUUID','MCMMetadataContentClass')}

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
 out=ROOT/'out/native-verification';case=out/'apple-form-css-refresh02';report=case/'result.json'
 previous_case=out/'apple-form-css-refresh01';failed_report=previous_case/'result.json'
 need(sha(failed_report)==FAILED_REPORT_SHA,'Preserve exact original failed01 report')
 prior=json.loads(failed_report.read_text())
 need(prior.get('running') is False and prior.get('passed') is False and prior.get('buildsPassed') is True and prior.get('cases')==[],'Unexpected prior Apple evidence')
 need(prior['bundles']['sim']['binarySHA256']==NEW_SIM,'Unexpected retained simulator artifact')
 need(sha(previous_case/'previous/sim/Lumen.app/Lumen')==OLD_SIM,'Original prior simulator backup changed')
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
 initial_states=states();initial_targets={};held_data={}
 for label,udid in TARGETS.items():
  need(initial_states.get(udid)=='Booted','Only already-booted designated targets may be updated')
  app=container(udid,'app');data,manager=scoped_data(udid)
  expected=NEW_SIM if label=='iphone' else OLD_SIM
  need(sha(app/'Lumen')==expected and not matching_pids(app/'Lumen'),'Current target app differs or is active')
  original=prior['targets'][label]['dataIdentity'];observed=identity(data)
  need((observed['device'],observed['inode'])==(original['device'],original['inode']),'Original data filesystem object is not preserved')
  fd=os.open(data,os.O_RDONLY|os.O_DIRECTORY);held=os.fstat(fd)
  need(stat.S_ISDIR(held.st_mode) and held.st_nlink>0 and (held.st_dev,held.st_ino)==(observed['device'],observed['inode']),'Cannot hold original data directory')
  held_data[label]=fd
  initial_targets[label]={'udid':udid,'dataIdentity':observed,'originalDataIdentity':original,'containerManagerIdentity':manager,'priorAppPath':str(app),'priorBinarySHA256':expected,'priorPathWasRelocated':observed['path']!=original['path']}
 port=(ROOT/'out/verification/lab-server-port.txt').read_text().strip();need(port.isdigit() and 0<int(port)<65536,'Invalid lab port')
 for relative,want in basis['servedLabSources'].items():
  with urllib.request.urlopen('http://127.0.0.1:'+port+'/'+relative,timeout=3) as response:
   data=response.read(2*1024*1024+1);need(len(data)<=2*1024*1024 and hashlib.sha256(data).hexdigest()==want,'Served lab source differs: '+relative)
 os.umask(0o077);case.mkdir(mode=0o700)
 result={'schemaVersion':1,'scope':'No-rebuild continuation using retained Apple artifacts; actual two-simulator form/CSS/WASM checks',
  'running':True,'passed':False,'phase':'adopt-retained-artifacts','startUnixSeconds':time.time(),'commands':[],'bundles':{},'cases':[],
  'sourceHashes':source_hashes,'v8ArchiveHashes':basis['archives'],'priorArtifactHashes':basis['artifacts'],
  'initialBootStates':initial_states,'targets':initial_targets,'bounds':{'jobs':2,'maximumOwnedRSSMiB':4096,'minimumFreeDiskMiB':1024},
  'physicalDeviceRuntimeVerified':False,'sameIpaMacRuntimeVerified':False,'realProviderAuthenticationVerified':False,
  'compositorPixelsReviewed':False,'performanceComparisonClaimed':False,'bootOrPreferencesChangedByRunner':False,
  'newV8BuildRequested':False,'newProjectBuildRequested':False,'webTransportEnabledOnApple':False,
  'reusedBuildReport':{'path':str(failed_report.relative_to(ROOT)),'sha256':FAILED_REPORT_SHA},
  'userDataContentRead':False,'privateDataBytewiseVerified':False,'containerManagerUUIDIsObservedNotAssumedImmutable':True,'dataContainerObservations':[]}
 active=None;installed=[];owned_sim={};adopted=[]
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
 def check_data(label):
  udid=TARGETS[label];data,manager=scoped_data(udid);observed=identity(data);original=initial_targets[label]['dataIdentity'];held=os.fstat(held_data[label])
  need(stat.S_ISDIR(held.st_mode) and held.st_nlink>0 and (held.st_dev,held.st_ino)==(original['device'],original['inode'])== (observed['device'],observed['inode']),'Held original data filesystem object changed')
  row={'device':label,'currentIdentity':observed,'initialPath':original['path'],'pathRelocated':observed['path']!=original['path'],
       'sameHeldFilesystemObject':True,'containerManagerIdentity':manager,'managerUUIDChanged':manager.get('MCMMetadataUUID')!=initial_targets[label]['containerManagerIdentity'].get('MCMMetadataUUID')}
  result['dataContainerObservations'].append(row)
  return row
 def check_target(label,newhash):
  udid=TARGETS[label];app=container(udid,'app');check_data(label)
  need(sha(app/'Lumen')==newhash,'Installed binary differs from retained artifact')
  return app/'Lumen'
 def read_records(path,marker):
  need(path.is_file() and path.stat().st_size<2*1024*1024,'Missing/oversized simulator evidence')
  return structured_evidence(path.read_text(errors='replace').splitlines(),marker)
 record()
 try:
  (ROOT/'out/verification/monitor-scenario.txt').write_text('apple-form-css-artifact-refresh\n')
  previous=case/'previous';previous.mkdir();result['priorArtifactsBackedUp']=True;record()
  for platform in ('sim','ios'):
   build=ROOT/('build-'+platform);retained=previous_case/('unaccepted-'+platform);backup=previous/platform;backup.mkdir()
   need(sha(retained/'Lumen.app/Lumen')==prior['bundles'][platform]['binarySHA256'] and sha(retained/'lumen')==prior['bundles'][platform]['buildBinarySHA256'],'Retained artifact differs from successful build')
   command(['codesign','--verify','--strict',str(retained/'Lumen.app')],20)
   for name in ('Lumen.app','lumen','Lumen-jitless.ipa'):
    current=build/name;replacement=retained/name;saved=backup/name
    if not replacement.exists():
     need(not current.exists(),'Unexpected prior-only artifact');continue
    need(current.exists() and not saved.exists(),'Artifact adoption precondition changed')
    current.rename(saved);adopted.append((platform,name));result['adoptedArtifacts']=[{'platform':p,'name':n} for p,n in adopted];record()
    if replacement.is_dir():
     fingerprint=tree(replacement);shutil.copytree(replacement,current,symlinks=True);need(tree(current)==fingerprint,'Copied candidate bundle differs')
    else:shutil.copy2(replacement,current);need(sha(current)==sha(replacement),'Copied candidate file differs')
    need(shutil.disk_usage(ROOT).free>=1024**3,'Keep1GiB during retained artifact adoption')
   app=build/'Lumen.app';exe=app/'Lumen';info=plistlib.loads((app/'Info.plist').read_bytes())
   need(info.get('CFBundleIdentifier')==BUNDLE and info.get('UIDeviceFamily')==[1,2],'Retained Apple identity/families changed')
   need((build/'rtc-config.h').read_text().strip()=='#define LUMEN_RTC_NATIVE 1' and (build/'webtransport-config.h').read_text().strip()=='#define LUMEN_WEBTRANSPORT_NATIVE 0','Retained Apple backend configuration changed')
   need(command(['lipo','-archs',str(exe)]).strip()=='arm64','Wrong retained Apple architecture')
   description=command(['xcrun','vtool','-show-build',str(exe)]);m=re.search(r'platform\s+(\S+)',description)
   need(m is not None and m.group(1)==('IOSSIMULATOR' if platform=='sim' else 'IOS'),'Wrong retained real Mach-O platform')
   command(['codesign','--verify','--strict',str(app)],20)
   need(sha(exe)==prior['bundles'][platform]['binarySHA256'] and sha(build/'lumen')==prior['bundles'][platform]['buildBinarySHA256'],'Active candidate differs from retained build')
   result['bundles'][platform]={**prior['bundles'][platform],'retainedSourcePath':str(retained.relative_to(ROOT))};record()
  ipa=ROOT/'build-ios/Lumen-jitless.ipa';need(sha(ipa)==prior['ipa']['sha256'],'Retained IPA differs')
  with zipfile.ZipFile(ipa) as z:need(hashlib.sha256(z.read('Payload/Lumen.app/Lumen')).hexdigest()==prior['bundles']['ios']['binarySHA256'],'Retained IPA binary differs')
  result['ipa']=prior['ipa'];result['retainedBuildsReused']=True;result['phase']='focused-checks';record()
  newhash=NEW_SIM
  for label,udid in TARGETS.items():
   need(states()==initial_states,'Boot states changed before target update')
   need(not matching_pids(container(udid,'app')/'Lumen'),'Target app became active before update')
   check_data(label)
   if label=='ipad':
    installed.append(label)
    run('install-'+label,['xcrun','simctl','install',udid,str(ROOT/'build-sim/Lumen.app')],40)
   else:
    result['iphoneReinstallSkippedBecauseCandidateAlreadyInstalled']=True;record()
   exe=check_target(label,newhash)
   for kind,probe,duration in (('form-wasm','apple_form_wasm_probe.js','24'),('responsive','responsive_layout_probe_v1.js','27')):
    name='apple-form-css-refresh02-'+label+'-'+kind;prefix=ROOT/'out/verification'/name
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
   result['dataFilesystemObjectsPreserved']=all(check_data(label)['sameHeldFilesystemObject'] for label in TARGETS)
   result['passed']=bool(result['passed'] and result['sourcesUnchanged'] and result['v8ArchivesAndCachesUnchanged'] and result['macCandidateUnchanged'] and result['bootStatesPreserved'] and result['dataFilesystemObjectsPreserved'] and not cleanup)
  except Exception as error:result['passed']=False;cleanup.append(type(error).__name__+': '+str(error)[:200])
  if not result['passed'] and result.get('priorArtifactsBackedUp'):
   restoration={}
   for label in reversed(installed):
    try:
     udid=TARGETS[label];check_data(label)
     need(initial_targets[label]['priorBinarySHA256']==OLD_SIM,'Rollback only the prior old iPad app')
     command(['xcrun','simctl','install',udid,str(previous_case/'previous/sim/Lumen.app')],40)
     restoration[label]=sha(container(udid,'app')/'Lumen')==OLD_SIM and check_data(label)['sameHeldFilesystemObject']
    except Exception as error:restoration[label]=type(error).__name__+': '+str(error)[:200]
   result['priorInstalledAppsRestored']=restoration
   for platform,name in reversed(adopted):
    try:
     build=ROOT/('build-'+platform);saved=case/'previous'/platform/name;unaccepted=case/('unaccepted-'+platform);unaccepted.mkdir(exist_ok=True)
     current=build/name
     if current.exists():current.rename(unaccepted/name)
     need(saved.exists(),'Prior artifact was not preserved');saved.rename(current)
     key='build-'+platform+'/'+name+('/Lumen' if name=='Lumen.app' else '')
     need(sha(current/'Lumen' if name=='Lumen.app' else current)==basis['artifacts'][key],'Prior artifact restoration differs')
    except Exception as error:cleanup.append('Artifact restore '+platform+'/'+name+': '+type(error).__name__+': '+str(error)[:150])
  result['originalFailedReportUnchanged']=sha(failed_report)==FAILED_REPORT_SHA
  result['passed']=bool(result['passed'] and result['originalFailedReportUnchanged'])
  for fd in held_data.values():os.close(fd)
  result['cleanupErrors']=cleanup;result['running']=False;result['phase']='complete';result['endUnixSeconds']=time.time();record()
  (ROOT/'out/verification/monitor-scenario.txt').write_text('idle-between-scenarios\n')
 print(json.dumps({k:v for k,v in result.items() if k not in ('commands','sourceHashes','v8ArchiveHashes','priorArtifactHashes','initialBootStates')}))
 return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
