#!/usr/bin/env python3
"""Native two-accessor control with sealed host-build identity; reference v1 remains immutable."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,signal,subprocess,sys,time
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=Path(__file__).resolve().parent
FIXTURE_SHA='e23c504b9684614b74ddf4cb57aed465dd7f4b96e646b49b0718514f760ca8db';MANIFEST_SHA='1bd60a7160e1f2110ebda39c35f7f19c3d4a5ec8bcb44804110b08c5850a8f4a'
CHROME_SHA='1bcad71ad1bff85955db13c6d8dd9f32374934d5a5cb079b526c46fb51f9bd97';DRIVER_SHA='d7f1682dcf9e4087bae6a52ca03c9913390a79ab86e09796562db7ed6f534587';ADAPTER_SHA='07648d03d652201057f91bdd514f8a90ab62d9b0e4b5ba6e80a8bc32ca98bf27'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def write(p,v):Path(p).write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def require(v,m):
 if not v:raise ValueError(m)
def same(a,b):
 if type(a) is not type(b):return False
 if isinstance(a,dict):return a.keys()==b.keys() and all(same(a[k],b[k]) for k in a)
 if isinstance(a,list):return len(a)==len(b) and all(same(x,y) for x,y in zip(a,b))
 return a==b
def git():return {'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff','--binary','--','src','tests','CMakeLists.txt','tools/gen_css_props.py'],cwd=ROOT)).hexdigest()}
ap=argparse.ArgumentParser();ap.add_argument('--kind',choices=['lumen'],required=True);ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();require(Path.cwd().resolve()==ROOT,'Project cwd required');require(a.iteration>=234,'Requires new visibility candidate')
fixture=BASE/'checkvisibility-slot-accessors-v1.html';manifest=BASE/'manifest-v1.json';require(sha(fixture)==FIXTURE_SHA and sha(manifest)==MANIFEST_SHA,'Pinned fixture/manifest changed');plan=read(manifest)
require(plan['case_count']==2 and plan['ordered_cases']==['localName','namespaceURI'] and same(plan['expected_each'],{'assigned':True,'hasBox':True,'actual':False,'reads':0,'error':None,'pass':True}),'Predeclared oracle differs')
build=ROOT/f'agent/validation/iteration-{a.iteration}';state=read(build/'build-state.json');source=read(build/'source-manifest-after.json')
require(source==read(build/'source-manifest-before.json') and state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'],'Successful stable build required')
require(state['source_manifest_before']==state['source_manifest_after']==source['sha256'] and all(sha(ROOT/p)==h for p,h in source['files'].items()),'Source binding differs')
before_git=git();require(before_git['head']==state['head'] and before_git['source_diff_sha256']==hashlib.sha256(b'').hexdigest(),'Frozen clean source required')
output=BASE/(f'jsrun{a.iteration}-v2' if a.kind=='lumen' else f'chromium156-i{a.iteration}-v1');require(not output.exists(),'Fresh output required')
paths=[Path(__file__).resolve(),fixture,manifest,ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py']+[build/n for n in ['build-state.json','source-manifest-before.json','source-manifest-after.json','build.log']]
env=dict(os.environ)
for k in ['JSRUN_PRE','JSRUN_POST','JSRUN_DUMP','JSRUN_UNTIL_ATTR','JSRUN_EXPECT_ATTR_VALUE','LUMEN_FRAME_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_SHOT','LUMEN_SHOT_MS']:env.pop(k,None)
env.update({'TMPDIR':str(ROOT/'agent/t'),'LUMEN_ICU_DATA':str(ROOT/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat')})
if a.kind=='lumen':
 native_proof_path=BASE.parent/f'native{a.iteration}/strict-control-verification.json';native_proof=read(native_proof_path);require(native_proof['source_commit']==state['head'],'Native strict proof source mismatch')
 binary=ROOT/f'build/codex-native/jsrun-astra-i{a.iteration}';current=ROOT/'build/codex-native/jsrun';require(sha(binary)==sha(current)==native_proof['immutable_jsrun_sha256'] and 'Linking CXX executable jsrun' in (build/'build.log').read_text(),'Immutable host/native strict proof/link mismatch');paths.extend([binary,current,native_proof_path]);command=[str(binary),str(fixture),'10000'];env.update({'JSRUN_LAYOUT':'1','JSRUN_UNTIL_ATTR':plan['completion_attribute'],'JSRUN_EXPECT_ATTR_VALUE':'PASS','LUMEN_CONSOLE':'0'})
else:
 cache=ROOT/'build/references/chrome-for-testing-156.0.8078.80';binary=cache/'chrome-linux64/chrome';driver=cache/'chromedriver-linux64/chromedriver';adapter=ROOT/'agent/validation/linux-wpt-20261030-session7/qa-script-scheduling/authored/run-cft156-fixture.py'
 require(sha(binary)==CHROME_SHA and sha(driver)==DRIVER_SHA and sha(adapter)==ADAPTER_SHA,'Pinned reference identity changed');paths.extend([binary,driver,adapter]);command=['python3',str(adapter),'--fixture',str(fixture),'--marker',plan['marker'],'--output',str(output/'browser'),'--timeout','20']
pins={str(p):sha(p) for p in paths};output.mkdir();write(output/'pins-before.json',pins)
worker='import subprocess,pathlib,json,datetime,time,signal,os,sys\ncommand='+repr(command)+'\nout=pathlib.Path('+repr(str(output))+')\nstarted=datetime.datetime.now(datetime.timezone.utc).isoformat();clock=time.monotonic();timeout=False;signals=[]\np=subprocess.Popen(command,start_new_session=True)\ntry:rc=p.wait(timeout=40)\nexcept subprocess.TimeoutExpired:\n timeout=True;os.killpg(p.pid,signal.SIGTERM);signals.append("SIGTERM")\n try:rc=p.wait(timeout=5)\n except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);signals.append("SIGKILL");rc=p.wait()\nr={"started_utc":started,"finished_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"duration_s":time.monotonic()-clock,"pid":p.pid,"actual_returncode":rc,"timed_out":timeout,"owned_cleanup_signals":signals,"command":command};(out/"actual-exit.json").write_text(json.dumps(r,indent=2)+"\\n");sys.exit(rc if rc>=0 else 128-rc)\n'
outer=['python3',str(ROOT/'tools/codex_compile.py'),sys.executable,'-c',worker];start=now();clock=time.monotonic();write(output/'launch.json',{'kind':a.kind,'candidate_source_binding':a.iteration,'source_commit':state['head'],'binary_sha256':sha(binary),'fixture_sha256':FIXTURE_SHA,'manifest_sha256':MANIFEST_SHA,'actual_command':command,'environment':{k:env[k] for k in ['JSRUN_LAYOUT','JSRUN_UNTIL_ATTR','JSRUN_EXPECT_ATTR_VALUE','LUMEN_CONSOLE'] if k in env},'resource_interval_seconds':1,'browser_lane_count':1,'not_WPT':True,'not_part_of_frozen49':True})
mon=[];handles=[];p=None;rc=None;forced=False;errors=[]
try:
 h=(output/'cgroup-monitor.log').open('x');handles.append(h);cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(output/'cgroup.csv')],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT);mon.append(cg)
 deadline=time.monotonic()+3
 while not (output/'cgroup.csv').exists() and cg.poll() is None and time.monotonic()<deadline:time.sleep(.05)
 require(cg.poll() is None and (output/'cgroup.csv').exists(),'Cgroup monitor startup failed')
 h=(output/'controller.log').open('x');handles.append(h);p=subprocess.Popen(outer,cwd=ROOT,env=env,stdout=h,stderr=subprocess.STDOUT)
 h=(output/'tree-monitor.log').open('x');handles.append(h);tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(output/'resources.csv'),'--label',a.kind+' private slot accessor control'],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT);mon.append(tm);rc=p.wait()
except Exception as e:errors.append('execution: '+repr(e))
finally:
 if p is not None and p.poll() is None:rc=p.wait()
 for i,m in enumerate(mon):
  if i==1 and m.poll() is None:
   try:m.wait(6)
   except subprocess.TimeoutExpired:forced=True
  if m.poll() is None:m.terminate()
  try:m.wait(6)
  except subprocess.TimeoutExpired:forced=True;m.kill();m.wait(3)
 for h in handles:h.close()
actual=read(output/'actual-exit.json') if (output/'actual-exit.json').exists() else {};rows=[];complete=False;behavior=None;cleanup_ok=False;exit_valid=False;exit_evidence={};raw=(output/'controller.log').read_text();result=None
try:
 require(bool(actual) and actual['timed_out'] is False and actual['owned_cleanup_signals']==[],'Actual execution incomplete or forced');native_rc=actual['actual_returncode'];require(rc==(native_rc if native_rc>=0 else 128-native_rc),'Quota wrapper/actual exit differs')
 if a.kind=='chromium156':
  result=read(output/'browser/result.json');raw='\n'.join(result.get('console',[]));owned=result.get('owned_process_cleanup',{})
  cleanup_ok=result.get('profile_removed') is True and result.get('fixture_unchanged') is True and bool(owned.get('recorded_identities')) and owned.get('remaining_same_identity')==[] and owned.get('signals')==[] and not result.get('driver_cleanup_error') and not result.get('error')
  exit_evidence={'actual_adapter_exit':native_rc,'chromium_OS_exit':'Not exposed by unchanged supported public adapter','profile_removed':result.get('profile_removed'),'owned_identities_remaining':owned.get('remaining_same_identity'),'cleanup_signals':owned.get('signals')}
 else:
  cleanup_ok=not Path('/proc/'+str(actual['pid'])).exists();exit_evidence={'actual_native_jsrun_exit':native_rc,'owned_PID_gone':cleanup_ok,'completion_attribute_value_gate':'JSRUN_EXPECT_ATTR_VALUE=PASS, after full fixture execution and before ordinary teardown'}
 require('SLOT_ACCESSOR_ERROR ' not in raw,'Fixture setup exception')
 for line in raw.splitlines():
  prefix='SLOT_ACCESSOR_ROW '
  if prefix in line:
   message=line[line.index(prefix):];require(len(message.encode())<1900,'Short row exceeded native log cap');rows.append(json.loads(message[len(prefix):]))
 require(len(rows)==2 and [r.get('case') for r in rows]==plan['ordered_cases'],'Exactly two ordered cases required')
 for row in rows:
  require(set(row)=={'case','assigned','hasBox','actual','reads','error','pass'},'Row field set differs')
  require(type(row['assigned']) is bool and type(row['hasBox']) is bool and (type(row['actual']) is bool or row['actual'] is None) and type(row['reads']) is int and row['reads']>=0 and (row['error'] is None or type(row['error']) is str) and type(row['pass']) is bool,'Row type differs')
  expected_pass=row['assigned'] and row['hasBox'] and row['actual'] is False and row['reads']==0 and row['error'] is None
  require(row['pass'] is expected_pass,'Row pass flag contradicts values')
 passed=sum(r['pass'] for r in rows);behavior='PASS' if passed==2 else 'FAIL';require(re.findall(r'RESULTS checkvisibility-slot-accessors:(PASS|FAIL)',raw)==[behavior],'Final marker differs or duplicated')
 if result is not None:require(result.get('status')==behavior and result.get('marker')=='RESULTS '+plan['marker']+':'+behavior,'Public adapter status/marker mismatch')
 exit_valid=native_rc==(0 if behavior=='PASS' else 1);complete=True;write(output/'observations.json',{'total':2,'passed':passed,'failed':2-passed,'rows':rows,'behavior':behavior})
except Exception as e:errors.append('result audit: '+repr(e))
after={};pins_ok=None;source_ok=None;git_ok=None
try:after={p:sha(p) for p in pins};pins_ok=after==pins
except Exception as e:errors.append('postflight pins: '+repr(e))
try:source_ok=all(sha(ROOT/p)==h for p,h in source['files'].items())
except Exception as e:errors.append('postflight source: '+repr(e))
try:git_ok=git()==before_git
except Exception as e:errors.append('postflight Git: '+repr(e))
integrity=bool(actual) and exit_valid and cleanup_ok and complete and pins_ok is True and source_ok is True and git_ok is True and [m.returncode for m in mon]==[0,0] and not forced and not errors
qualification=integrity and behavior=='PASS' and all(same({k:r[k] for k in plan['expected_each']},plan['expected_each']) for r in rows)
receipt={'kind':a.kind,'candidate_source_binding':a.iteration,'source_commit':state['head'],'binary_sha256':sha(binary),'fixture_sha256':FIXTURE_SHA,'manifest_sha256':MANIFEST_SHA,'controller_sha256':sha(Path(__file__)),'started_utc':start,'finished_utc':now(),'duration_s':time.monotonic()-clock,'actual_quota_returncode':rc,'actual_command_returncode':actual.get('actual_returncode'),'actual_exit_receipt_sha256':sha(output/'actual-exit.json') if actual else None,'exit_evidence':exit_evidence,'monitor_returncodes':[m.returncode for m in mon],'monitor_forced_stop':forced,'source_input_count':len(source['files']),'pins_unchanged':pins_ok,'source_unchanged':source_ok,'git_unchanged':git_ok,'cleanup_ok':cleanup_ok,'measurement_complete':complete,'behavioral_result':behavior,'measurement_integrity_valid':integrity,'qualification_pass':qualification,'not_WPT':True,'separate_from_frozen49':True,'complete_FAIL_is_preserved_measurement':True,'errors':errors,'resource_scope':'One-second execution tree and shared cgroup. Reference includes ChromeDriver; no browser-memory or Chromium OS-exit claim.'}
write(output/'pins-after.json',after);write(output/'receipt.json',receipt);print(json.dumps(receipt),flush=True);sys.exit(0 if qualification else 1 if integrity else 2)

