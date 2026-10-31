#!/usr/bin/env python3
"""One supported Chromium reference execution of the frozen durable retention fixture."""
from pathlib import Path
import argparse,subprocess,json,hashlib,datetime,time,sys,os,signal,re
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=Path(__file__).resolve().parent
FIXTURE_SHA='66d53129c5d4f8e46dea8bf194b3c2e9266ba565e99215d1f34e3cdb47ff6dff'
CHROME_SHA='1bcad71ad1bff85955db13c6d8dd9f32374934d5a5cb079b526c46fb51f9bd97';DRIVER_SHA='d7f1682dcf9e4087bae6a52ca03c9913390a79ab86e09796562db7ed6f534587'
ADAPTER_SHA='07648d03d652201057f91bdd514f8a90ab62d9b0e4b5ba6e80a8bc32ca98bf27'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
def require(v,message):
 if not v:raise ValueError(message)
def prefixed(raw,prefix):return [json.loads(line[line.index(prefix)+len(prefix):]) for line in raw.splitlines() if prefix in line]
def leaf_count(v):return sum(map(leaf_count,v.values())) if isinstance(v,dict) else sum(map(leaf_count,v)) if isinstance(v,list) else 1
ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();os.chdir(ROOT)
fixture=ROOT/'tests/js/hidden-scroll-retention.html';require(sha(fixture)==FIXTURE_SHA,'durable fixture changed')
build=ROOT/('agent/validation/iteration-'+str(a.iteration));state=read(build/'build-state.json');source=read(build/'source-manifest-after.json')
require(source==read(build/'source-manifest-before.json') and state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'],'build did not qualify')
require(all(sha(ROOT/p)==h for p,h in source['files'].items()),'workspace differs from immutable candidate inputs');head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();require(head==state['head'],'workspace Git differs from candidate source')
cache=ROOT/'build/references/chrome-for-testing-156.0.8078.80';binary=cache/'chrome-linux64/chrome';driver=cache/'chromedriver-linux64/chromedriver';adapter=ROOT/'agent/validation/linux-wpt-20261030-session7/qa-script-scheduling/authored/run-cft156-fixture.py'
require(sha(binary)==CHROME_SHA and sha(driver)==DRIVER_SHA and sha(adapter)==ADAPTER_SHA,'reference executable/adapter identity changed')
manifest=BASE/'manifest-v3.json';require(sha(manifest)=='055cde00bbf2e19ed2819d56915dfe6c3819f9b27de2008cd490d6e0a74ca0bb','reference manifest changed');plan=read(manifest);require(plan['expected_case_count']==7 and plan['expected_snapshot_count']==30,'reference matrix scope changed')
contract=BASE/'acceptance-contract-v2.json';require(sha(contract)=='f99db574f100b60107c55f62181b72681892ec4767b2e7cb46b73db7f7872d60','contract changed')
reference=BASE/'chromium156-v3/observations.json';require(sha(reference)=='4e13f504121ceedec9e65a5056b0f1052551d1e6bb861c428dedf50999aae094','captured reference observations changed')
reference_rows=[{k:v for k,v in row.items() if k!='time'} for row in read(reference)];expected_fields=leaf_count(reference_rows)
source_text=fixture.read_text();embedded,_=json.JSONDecoder().raw_decode(source_text.split('const expected = ',1)[1]);require(embedded==reference_rows,'embedded oracle differs from captured reference')
output=BASE/('chromium156-durable-control-i'+str(a.iteration));require(not output.exists(),'fresh output required');output.mkdir()
paths=[Path(__file__),fixture,binary,driver,adapter,manifest,contract,reference,build/'build-state.json',build/'source-manifest-before.json',build/'source-manifest-after.json',build/'build.log',ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py'];pins={str(p):sha(p) for p in paths}
command=['python3',str(ROOT/'tools/codex_compile.py'),'python3',str(adapter),'--fixture',str(fixture),'--marker','hidden-scroll-retention','--output',str(output/'browser'),'--timeout','20']
env=dict(os.environ)
for key in ['LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_SHOT','LUMEN_SHOT_MS']:env.pop(key,None)
env['TMPDIR']=str(ROOT/'agent/t')
start=now();t=time.monotonic();write(output/'pins-before.json',pins);write(output/'launch.json',{'kind':'chromium156','candidate_workspace':a.iteration,'workspace_source_commit':head,'command':command,'fixture_sha256':FIXTURE_SHA,'chromium_binary_sha256':CHROME_SHA,'driver_sha256':DRIVER_SHA,'resource_interval_seconds':1,'authored_control_not_WPT':True,'browser_lane_count':1})
timed_out=False;forced_monitor=False;rc=None;errors=[];tm=None;cg=None;p=None;outer_cleanup_signals=[]
with (output/'controller.log').open('x') as log,(output/'tree-monitor.log').open('x') as tl,(output/'cgroup-monitor.log').open('x') as cl:
 try:
  cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(output/'cgroup.csv')],stdout=cl,stderr=subprocess.STDOUT)
  p=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
  tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(output/'resources.csv'),'--label','Chromium156 durable hidden-scroll control'],stdout=tl,stderr=subprocess.STDOUT)
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
    except subprocess.TimeoutExpired:
     outer_cleanup_signals.append('SIGKILL');os.killpg(p.pid,signal.SIGKILL);rc=p.wait(3)
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
raw='';parent=[];completion=[];summary=[];cleanup_ok=False;checks_ok=False;exit_evidence={};after={};source_unchanged=False;git_unchanged=False
try:
 result=read(output/'browser/result.json');raw='\n'.join(result.get('console',[]));owned=result.get('owned_process_cleanup',{})
 cleanup_ok=result.get('status')=='PASS' and result.get('profile_removed') is True and result.get('fixture_unchanged') is True and owned.get('remaining_same_identity')==[] and bool(owned.get('recorded_identities')) and owned.get('signals')==[] and not result.get('driver_cleanup_error') and not result.get('error')
 exit_evidence={'adapter_actual_returncode':rc,'chromium_os_exit_status':'Not exposed by the unchanged supported public adapter','profile_removed':result.get('profile_removed'),'remaining_owned_identities':owned.get('remaining_same_identity'),'fallback_signals':owned.get('signals'),'driver_cleanup_error':result.get('driver_cleanup_error')}
 parent=prefixed(raw,'DIAGNOSTIC hidden-scroll-parent-overflow ');completion=prefixed(raw,'MEASUREMENTS hidden-scroll-state:COMPLETE ')
 summary=re.findall(r'hidden-scroll-retention: 7 cases, 30 phases, (\d+) fields, documented eager hidden size differences ([01])',raw)
 parent_expected=[{'phase':'hidden','width':160,'height':140},{'phase':'revealed','width':400,'height':400},{'phase':'hidden-again','width':160,'height':140}]
 checks_ok=('RESULTS hidden-scroll-retention:PASS' in raw and not re.search(r'RESULTS [^\n]*:FAIL',raw) and 'MEASUREMENTS hidden-scroll-state:ERROR' not in raw and len(parent)==1 and parent[0]==parent_expected and len(completion)==1 and completion[0]=={'completed':[x['id'] for x in plan['cases']],'records':30} and len(summary)==1 and int(summary[0][0])==expected_fields)
 after={path:sha(path) for path in pins};source_unchanged=all(sha(ROOT/path)==value for path,value in source['files'].items());git_unchanged=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head
except Exception as error:errors.append('result audit: '+repr(error))
monitor_codes=[cg.returncode if cg else None,tm.returncode if tm else None]
qualified=rc==0 and not outer_cleanup_signals and not timed_out and not forced_monitor and monitor_codes==[0,0] and cleanup_ok and checks_ok and after==pins and source_unchanged and git_unchanged and not errors
receipt={'kind':'chromium156','workspace_candidate':a.iteration,'workspace_source_commit':head,'started_utc':start,'finished_utc':now(),'duration_s':time.monotonic()-t,'fixture_sha256':FIXTURE_SHA,'chromium_binary_sha256':CHROME_SHA,'driver_sha256':DRIVER_SHA,'adapter_returncode':rc,'outer_cleanup_signals':outer_cleanup_signals,'source_input_count':len(source['files']),'timed_out':timed_out,'monitor_returncodes':monitor_codes,'monitor_forced_stop':forced_monitor,'pins_unchanged':after==pins,'source_unchanged':source_unchanged,'git_unchanged':git_unchanged,'cleanup_ok':cleanup_ok,'exit_evidence':exit_evidence,'retention_and_parent_controls_pass':checks_ok,'parent_overflow_observations':parent,'completion':completion,'field_summary':summary,'expected_reference_field_count':expected_fields,'qualified_reference':qualified,'authored_control_not_WPT':True,'errors':errors,'resource_scope':'One-second owned adapter process-tree/cgroup observations; not browser-only benchmarking. Chromium OS exit status is unavailable; supported owned cleanup is retained.'}
write(output/'pins-after.json',after);write(output/'receipt.json',receipt);print(json.dumps({k:receipt[k] for k in ['qualified_reference','adapter_returncode','monitor_returncodes','cleanup_ok','retention_and_parent_controls_pass','expected_reference_field_count','errors']}));sys.exit(0 if qualified else 2)
