#!/usr/bin/env python3
"""Capture an authorized, unchanged 49-check baseline; a complete FAIL is a valid measurement."""
from pathlib import Path
import argparse,subprocess,json,hashlib,datetime,time,sys,os,signal,re
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=Path(__file__).resolve().parent
FIXTURE_SHA='aeeb79df0c100533387835070801079a695dff5b0e64819757b73db14eddca06'
MANIFEST_SHA='bab6f648f9b74225dfb880bcf4590d444d23d3ed4ce889febf957b4990b2703c'
EXPECTED_SHA='a76f6d7611e87643a2fdf34ca2d72bf80a7a789c3d66d82c59234855cc797506'
SOURCE='554d59472954585bfd39c579ee122f9f21194847';JSRUN_SHA='00435d64f63d928ceac27691ab1f28f533e4002a80ee728e0787df5b9c4d35fe'
CHROME_SHA='1bcad71ad1bff85955db13c6d8dd9f32374934d5a5cb079b526c46fb51f9bd97';DRIVER_SHA='d7f1682dcf9e4087bae6a52ca03c9913390a79ab86e09796562db7ed6f534587'
ADAPTER_SHA='07648d03d652201057f91bdd514f8a90ab62d9b0e4b5ba6e80a8bc32ca98bf27'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(p,v):Path(p).write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def require(v,message):
 if not v:raise ValueError(message)
def equal_json(a,b):
 if type(a) is not type(b):return False
 if isinstance(a,dict):return a.keys()==b.keys() and all(equal_json(a[k],b[k]) for k in a)
 if isinstance(a,list):return len(a)==len(b) and all(equal_json(x,y) for x,y in zip(a,b))
 return a==b
ap=argparse.ArgumentParser();ap.add_argument('--kind',choices=['lumen'],required=True);ap.add_argument('--iteration',type=int,choices=[231],required=True);a=ap.parse_args();os.chdir(ROOT)
fixture=BASE/'checkvisibility-options-v1.html';manifest=BASE/'fixture-manifest-v1.json';oracle=BASE/'expected-checks-v1.json'
require(sha(fixture)==FIXTURE_SHA and sha(manifest)==MANIFEST_SHA and sha(oracle)==EXPECTED_SHA,'fixture/manifest/oracle changed')
plan=read(manifest);expected=read(oracle)['checks'];require(len(expected)==49 and plan['expected_checks']==49,'scope changed')
build=ROOT/'agent/validation/iteration-231';state=read(build/'build-state.json');source=read(build/'source-manifest-after.json')
require(source==read(build/'source-manifest-before.json') and state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'],'candidate build did not qualify')
require(state['head']==SOURCE and state['source_manifest_before']==state['source_manifest_after']==source['sha256'],'candidate source identity changed')
require(all(sha(ROOT/p)==h for p,h in source['files'].items()),'workspace differs from candidate inputs');head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();require(head==SOURCE,'workspace Git differs from candidate')
output=BASE/'jsrun231-v3';require(not output.exists(),'fresh output required')
paths=[Path(__file__),fixture,manifest,oracle,build/'build-state.json',build/'source-manifest-before.json',build/'source-manifest-after.json',build/'build.log',ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py']
env=dict(os.environ)
for key in ['LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_SHOT','LUMEN_SHOT_MS']:env.pop(key,None)
post=BASE/'serialize-completed-results-v3.js';require(sha(post)=='fc11a22c4bac7f5738fe35c486a0a1a856cd902082df3f872684ddb1ddee07cf','read-only transport hook changed');paths.append(post);env['JSRUN_POST']=post.read_text()
env['TMPDIR']=str(ROOT/'agent/t');env['LUMEN_ICU_DATA']=str(ROOT/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat')
if a.kind=='lumen':
 binary=ROOT/'build/codex-native/jsrun-astra-i231';current=ROOT/'build/codex-native/jsrun'
 require(sha(binary)==sha(current)==JSRUN_SHA and 'Linking CXX executable jsrun' in (build/'build.log').read_text(),'immutable native jsrun/build mismatch')
 paths.extend([binary,current]);env.update({'JSRUN_LAYOUT':'1','JSRUN_UNTIL_ATTR':plan['completion_attribute'],'LUMEN_CONSOLE':'0'});inner=[str(binary),str(fixture),'10000']
else:
 cache=ROOT/'build/references/chrome-for-testing-156.0.8078.80';binary=cache/'chrome-linux64/chrome';driver=cache/'chromedriver-linux64/chromedriver';adapter=ROOT/'agent/validation/linux-wpt-20261030-session7/qa-script-scheduling/authored/run-cft156-fixture.py'
 require(sha(binary)==CHROME_SHA and sha(driver)==DRIVER_SHA and sha(adapter)==ADAPTER_SHA,'reference executable/adapter identity changed');paths.extend([binary,driver,adapter]);inner=['python3',str(adapter),'--fixture',str(fixture),'--marker',plan['bare_adapter_marker'],'--output',str(output/'browser'),'--timeout','20']
command=['python3',str(ROOT/'tools/codex_compile.py')]+inner;pins={str(p):sha(p) for p in paths};output.mkdir();start=now();t=time.monotonic();write(output/'pins-before.json',pins)
write(output/'launch.json',{'kind':a.kind,'candidate':231,'source_commit':SOURCE,'binary_sha256':sha(binary),'command':command,'native_environment':{k:env[k] for k in ['JSRUN_LAYOUT','JSRUN_UNTIL_ATTR','LUMEN_CONSOLE']},'read_only_post_hook_sha256':sha(post),'post_hook_does_not_rerun_checks':True,'fixture_sha256':FIXTURE_SHA,'expected_checks':49,'resource_interval_seconds':1,'browser_lane_count':1,'not_WPT':True,'complete_feature_FAIL_is_valid_measurement':True})
timed_out=False;forced_monitor=False;rc=None;errors=[];tm=None;cg=None;p=None;outer_cleanup_signals=[]
with (output/'controller.log').open('x') as log,(output/'tree-monitor.log').open('x') as tl,(output/'cgroup-monitor.log').open('x') as cl:
 try:
  cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(output/'cgroup.csv')],stdout=cl,stderr=subprocess.STDOUT)
  p=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
  tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(output/'resources.csv'),'--label',a.kind+' options baseline'],stdout=tl,stderr=subprocess.STDOUT)
  try:rc=p.wait(45)
  except subprocess.TimeoutExpired:
   timed_out=True;os.killpg(p.pid,signal.SIGTERM)
   try:rc=p.wait(5)
   except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);rc=p.wait(3)
 except Exception as error:errors.append('execution: '+repr(error))
 finally:
  if p is not None and p.poll() is None:
   outer_cleanup_signals.append('SIGTERM')
   try:
    os.killpg(p.pid,signal.SIGTERM)
    try:rc=p.wait(5)
    except subprocess.TimeoutExpired:outer_cleanup_signals.append('SIGKILL');os.killpg(p.pid,signal.SIGKILL);rc=p.wait(3)
   except Exception as error:errors.append('owned controller cleanup: '+repr(error))
  if tm:
   try:tm.wait(6)
   except subprocess.TimeoutExpired:
    forced_monitor=True
    try:tm.terminate();tm.wait(6)
    except Exception as error:errors.append('tree monitor cleanup: '+repr(error))
   except Exception as error:errors.append('tree monitor wait: '+repr(error))
  if cg:
   try:cg.terminate();cg.wait(6)
   except Exception as error:errors.append('cgroup monitor cleanup: '+repr(error))
raw=(output/'controller.log').read_text();result=None;measurement=None;cleanup_ok=a.kind=='lumen' and rc==0;execution_exit_valid=cleanup_ok;exit_evidence={'native_jsrun_actual_returncode':rc};after={};pins_unchanged=None;source_unchanged=None;git_unchanged=None;complete=False;behavior=None
try:
 if a.kind=='chromium156':
  result=read(output/'browser/result.json');raw='\n'.join(result.get('console',[]));owned=result.get('owned_process_cleanup',{})
  cleanup_ok=result.get('profile_removed') is True and result.get('fixture_unchanged') is True and owned.get('remaining_same_identity')==[] and bool(owned.get('recorded_identities')) and owned.get('signals')==[] and not result.get('driver_cleanup_error') and not result.get('error')
  execution_exit_valid=(result.get('status')=='PASS' and rc==0) or (result.get('status')=='FAIL' and rc==1)
  exit_evidence={'adapter_actual_returncode':rc,'public_adapter_status':result.get('status'),'chromium_os_exit_status':'Not exposed by unchanged supported public adapter','profile_removed':result.get('profile_removed'),'remaining_owned_identities':owned.get('remaining_same_identity'),'fallback_signals':owned.get('signals'),'driver_cleanup_error':result.get('driver_cleanup_error')}
 indexed=[];summaries=[];original_headers=[]
 for line in raw.splitlines():
  for prefix,target in [('TRANSPORT_ROW checkvisibility-options ',indexed),('TRANSPORT_SUMMARY checkvisibility-options ',summaries)]:
   if prefix in line:
    message=line[line.index(prefix):];require(len(message.encode('utf-8'))<1900,'transport log exceeded cap');target.append(json.loads(message[len(prefix):]))
  prefix=plan['required_measurement_label']+' '
  if prefix in line:
   body=line[line.index(prefix)+len(prefix):];match=re.match(r'^\{"total":(\d+),"passed":(\d+),"failed":(\d+),"results":\[',body)
   require(match is not None,'original aggregate header missing');original_headers.append(tuple(map(int,match.groups())))
 require(len(indexed)==49 and all(set(v)=={'index','result'} and type(v['index']) is int and v['index']==i for i,v in enumerate(indexed)),'exact49 ordered indexed transport rows required')
 require(len(summaries)==1 and set(summaries[0])=={'total','passed','failed','completion'},'one complete transport summary required')
 summary=summaries[0];require(all(type(summary[k]) is int for k in ['total','passed','failed']),'transport counts must be integers')
 require(original_headers==[(summary['total'],summary['passed'],summary['failed'])],'original aggregate counts differ from transport')
 measurement={k:summary[k] for k in ['total','passed','failed']};measurement['results']=[v['result'] for v in indexed]
 rows=measurement['results'];require(isinstance(rows,list) and len(rows)==49,'expected49 rows');require([x.get('name') for x in rows]==[x['name'] for x in expected],'names/order differ from pinned expectations')
 for row,exp in zip(rows,expected):
  require(set(row)=={'name','expected','actual','pass','error'} and equal_json(row['expected'],exp['expected']) and type(row['pass']) is bool,'row structure or expected value changed')
  err=row['error'];require(err is None or (isinstance(err,dict) and set(err)=={'name','message'} and all(type(v) is str for v in err.values())),'malformed semantic error')
  require(row['pass'] is (err is None and equal_json(row['actual'],row['expected'])),'row pass contradicts recorded values')
 passed=sum(x['pass'] for x in rows);failed=49-passed
 require(all(type(measurement[k]) is int for k in ['total','passed','failed']) and measurement['total']==49 and measurement['passed']==passed and measurement['failed']==failed,'inconsistent counts')
 behavior='PASS' if failed==0 else 'FAIL';require(summary['completion']==behavior,'original completion attribute disagrees');markers=re.findall(r'RESULTS checkvisibility-options:(PASS|FAIL)',raw);require(markers==[behavior],'final behavioral marker missing/duplicated/inconsistent')
 if result is not None:require(result.get('status')==behavior and result.get('marker')=='RESULTS checkvisibility-options:'+behavior,'public adapter marker differs')
 complete=True;write(output/'observations.json',measurement)
except Exception as error:errors.append('result audit: '+repr(error))
# Postflight evidence is independent of behavioral-output parsing. None means a check could not run.
try:
 after={path:sha(path) for path in pins};pins_unchanged=after==pins
except Exception as error:errors.append('postflight pins: '+repr(error))
try:source_unchanged=all(sha(ROOT/path)==value for path,value in source['files'].items())
except Exception as error:errors.append('postflight source: '+repr(error))
try:git_unchanged=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head
except Exception as error:errors.append('postflight Git: '+repr(error))
monitor_codes=[cg.returncode if cg else None,tm.returncode if tm else None]
valid=execution_exit_valid and not outer_cleanup_signals and not timed_out and not forced_monitor and monitor_codes==[0,0] and cleanup_ok and complete and pins_unchanged is True and source_unchanged is True and git_unchanged is True and not errors
receipt={'kind':a.kind,'candidate':231,'source_commit':SOURCE,'binary_sha256':sha(binary),'fixture_sha256':FIXTURE_SHA,'read_only_post_hook_sha256':sha(post),'transport_only_serialization':True,'started_utc':start,'finished_utc':now(),'duration_s':time.monotonic()-t,'actual_inner_returncode':rc,'execution_exit_valid':execution_exit_valid,'exit_evidence':exit_evidence,'outer_cleanup_signals':outer_cleanup_signals,'timed_out':timed_out,'monitor_returncodes':monitor_codes,'monitor_forced_stop':forced_monitor,'source_input_count':len(source['files']),'pins_unchanged':pins_unchanged,'source_unchanged':source_unchanged,'git_unchanged':git_unchanged,'cleanup_ok':cleanup_ok,'measurement_complete':complete,'behavioral_result':behavior,'counts':{k:measurement.get(k) for k in ['total','passed','failed']} if measurement else None,'measurement_integrity_valid':valid,'complete_feature_FAIL_is_valid_measurement':True,'not_WPT':True,'errors':errors,'resource_scope':'One-second owned process tree/cgroup. Chromium includes ChromeDriver; not a browser memory benchmark. No Chromium OS-exit claim.'}
write(output/'pins-after.json',after);write(output/'receipt.json',receipt);print(json.dumps({k:receipt[k] for k in ['kind','actual_inner_returncode','execution_exit_valid','monitor_returncodes','cleanup_ok','measurement_complete','behavioral_result','counts','measurement_integrity_valid','errors']}));sys.exit(0 if valid else 2)
