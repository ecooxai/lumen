#!/usr/bin/env python3
"""Record real lifecycle observations; transport completion is never a behavioral pass."""
from pathlib import Path
import argparse,collections,datetime,hashlib,json,os,re,shutil,signal,subprocess,sys,time
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=Path(__file__).resolve().parent
FIXTURE_SHA='15ba1babfbbde45a7571e3f2d9c2abce5bcbaeaa75c3dfa2c337cea3e80a53d0'
MANIFEST_SHA='66b539c4479d821eaf8ca94979b544b38f8d8f492c4f2b5e20387c56550bffb5'
CHROME_SHA='1bcad71ad1bff85955db13c6d8dd9f32374934d5a5cb079b526c46fb51f9bd97';DRIVER_SHA='d7f1682dcf9e4087bae6a52ca03c9913390a79ab86e09796562db7ed6f534587';ADAPTER_SHA='07648d03d652201057f91bdd514f8a90ab62d9b0e4b5ba6e80a8bc32ca98bf27'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def write(p,value):Path(p).write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def require(ok,message):
 if not ok:raise ValueError(message)

ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--kind',choices=['lumen','chromium156'],required=True);ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();os.chdir(ROOT)
manifest=BASE/'matrix-manifest-v3.json';fixture=BASE/'document-lifecycle-matrix-v3.html';require(sha(manifest)==MANIFEST_SHA and sha(fixture)==FIXTURE_SHA,'Reviewed matrix changed');plan=read(manifest);cases=plan['cases'];require(plan['case_count']==len(cases)==len(set(cases))==11,'Expected eleven cases')
build=ROOT/f'agent/validation/iteration-{a.iteration}';state=read(build/'build-state.json');source=read(build/'source-manifest-before.json');require(source==read(build/'source-manifest-after.json') and state['source_unchanged_during_build'] and state['build_exit_code']==state['startup_exit_code']==0,'Successful unchanged build required');require(state['source_manifest_before']==state['source_manifest_after']==source['sha256'],'Sealed build input digest differs');require(all(sha(ROOT/p)==h for p,h in source['files'].items()),'Current inputs differ from sealed candidate');head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();require(head==state['head'],'Source/Git correspondence failed')
out=BASE/(f'jsrun{a.iteration}-v3' if a.kind=='lumen' else 'chromium156-v3');require(not out.exists(),'Fresh output required')
paths=[Path(__file__),manifest,fixture,build/'build-state.json',build/'source-manifest-before.json',build/'source-manifest-after.json',build/'build.log',ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py']
env=dict(os.environ)
for key in ['LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_SHOT','LUMEN_SHOT_MS','JSRUN_PRE','JSRUN_POST','JSRUN_DUMP']:env.pop(key,None)
env.update({'TMPDIR':str(ROOT/'agent/t'),'LUMEN_ICU_DATA':str(ROOT/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat')})
if a.kind=='lumen':
 require('Linking CXX executable jsrun' in (build/'build.log').read_text(),'Current jsrun link not recorded')
 current=ROOT/'build/codex-native/jsrun';binary=ROOT/f'build/codex-native/jsrun-astra-i{a.iteration}'
 if not binary.exists():
  with current.open('rb') as src,binary.open('xb') as dst:shutil.copyfileobj(src,dst)
  binary.chmod(0o555)
 require(sha(current)==sha(binary),'Immutable jsrun differs from current linked target')
 if a.iteration==231:require(sha(binary)=='00435d64f63d928ceac27691ab1f28f533e4002a80ee728e0787df5b9c4d35fe','Recorded immutable231 jsrun changed')
 paths.extend([current,binary]);env.update({'JSRUN_LAYOUT':'1','JSRUN_UNTIL_ATTR':plan['completion_attribute'],'LUMEN_CONSOLE':'0'});inner=[str(binary),str(fixture),'10000']
else:
 cache=ROOT/'build/references/chrome-for-testing-156.0.8078.80';binary=cache/'chrome-linux64/chrome';driver=cache/'chromedriver-linux64/chromedriver';adapter=ROOT/'agent/validation/linux-wpt-20261030-session7/qa-script-scheduling/authored/run-cft156-fixture.py'
 require(sha(binary)==CHROME_SHA and sha(driver)==DRIVER_SHA and sha(adapter)==ADAPTER_SHA,'Pinned reference/adapter changed');paths.extend([binary,driver,adapter]);inner=['python3',str(adapter),'--fixture',str(fixture),'--marker',plan['bare_adapter_marker'],'--output',str(out/'browser'),'--timeout','20']
command=['python3',str(ROOT/'tools/codex_compile.py')]+inner;pins={str(p.resolve()):sha(p) for p in paths};out.mkdir();write(out/'pins-before.json',pins);started=now();clock=time.monotonic();write(out/'launch.json',{'kind':a.kind,'workspace_candidate':a.iteration,'source_commit':head,'binary_sha256':sha(binary),'command':command,'fixture_sha256':FIXTURE_SHA,'manifest_sha256':MANIFEST_SHA,'resource_interval_seconds':1,'maximum_browser_lanes':1,'native_environment':{k:env[k] for k in ['JSRUN_LAYOUT','JSRUN_UNTIL_ATTR','LUMEN_CONSOLE']} if a.kind=='lumen' else None,'transport_only':True,'behavioral_PASS_is_not_implied':True})
p=None;tm=None;cg=None;rc=None;timed_out=False;forced_monitor=False;outer_signals=[];errors=[]
with (out/'controller.log').open('x') as log,(out/'tree-monitor.log').open('x') as tl,(out/'cgroup-monitor.log').open('x') as cl:
 try:
  cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],stdout=cl,stderr=subprocess.STDOUT)
  p=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,env=env,start_new_session=True)
  tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label',a.kind+' actual lifecycle matrix'],stdout=tl,stderr=subprocess.STDOUT)
  try:rc=p.wait(45)
  except subprocess.TimeoutExpired:
   timed_out=True;outer_signals.append('SIGTERM');os.killpg(p.pid,signal.SIGTERM)
   try:rc=p.wait(5)
   except subprocess.TimeoutExpired:outer_signals.append('SIGKILL');os.killpg(p.pid,signal.SIGKILL);rc=p.wait(3)
 except Exception as error:errors.append('execution: '+repr(error))
 finally:
  if p is not None and p.poll() is None:
   outer_signals.append('SIGTERM')
   try:
    os.killpg(p.pid,signal.SIGTERM)
    try:rc=p.wait(5)
    except subprocess.TimeoutExpired:outer_signals.append('SIGKILL');os.killpg(p.pid,signal.SIGKILL);rc=p.wait(3)
   except Exception as error:errors.append('owned process cleanup: '+repr(error))
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
raw=(out/'controller.log').read_text();cleanup=a.kind=='lumen' and rc==0;exit_evidence={'native_jsrun_actual_returncode':rc};case_rows=[];observations=[];summaries=[];complete=False;result=None
try:
 if a.kind=='chromium156':
  result=read(out/'browser/result.json');raw='\n'.join(result.get('console',[]));owned=result.get('owned_process_cleanup',{});cleanup=result.get('profile_removed') is True and result.get('fixture_unchanged') is True and bool(owned.get('recorded_identities')) and owned.get('remaining_same_identity')==[] and owned.get('signals')==[] and not result.get('driver_cleanup_error') and not result.get('error');exit_evidence={'adapter_actual_returncode':rc,'chromium_os_exit_status':'Not exposed by unchanged supported public adapter','profile_removed':result.get('profile_removed'),'remaining_owned_identities':owned.get('remaining_same_identity'),'fallback_signals':owned.get('signals'),'driver_cleanup_error':result.get('driver_cleanup_error')}
 for line in raw.splitlines():
  for prefix,target in [('LIFECYCLE_CASE ',case_rows),('LIFECYCLE_ROW ',observations),('MEASUREMENTS document-lifecycle ',summaries)]:
   if prefix in line:
    message=line[line.index(prefix):];require(len(message.encode('utf-8'))<1900,'Lifecycle row exceeds safe console budget');target.append(json.loads(message[len(prefix):]))
 require('LIFECYCLE_FATAL ' not in raw,'Fatal matrix error retained')
 require(len(case_rows)==11 and [v.get('id') for v in case_rows]==cases,'Exact ordered eleven case declarations required')
 for i,c in enumerate(case_rows):
  require(set(c)=={'index','id','finished','eventCount','actionCount','errorCount'} and type(c['index']) is int and c['index']==i and c['finished'] is True,'Incomplete or malformed case')
  require(all(type(c[k]) is int and c[k]>=0 for k in ['eventCount','actionCount','errorCount']),'Invalid declared row counts')
 for row in observations:
  require(set(row)=={'caseIndex','caseId','kind','index','value'} and type(row['caseIndex']) is int and 0<=row['caseIndex']<11 and row['caseId']==cases[row['caseIndex']],'Unknown/malformed observation case')
  require(row['kind'] in ['events','actions','errors'] and type(row['index']) is int and row['index']>=0 and isinstance(row['value'],dict),'Malformed observation')
  require(type(row['value'].get('sequence')) is int and row['value']['sequence']>=0,'Missing chronological sequence')
 for i,c in enumerate(case_rows):
  selected=[r for r in observations if r['caseIndex']==i];expected_total=c['eventCount']+c['actionCount']+c['errorCount'];require(len(selected)==expected_total,'Case row count differs')
  require(sorted(r['value']['sequence'] for r in selected)==list(range(expected_total)),'Chronological sequence missing or duplicated')
  for kind,count in [('events',c['eventCount']),('actions',c['actionCount']),('errors',c['errorCount'])]:require([r['index'] for r in selected if r['kind']==kind]==list(range(count)),'Per-kind observation index/order differs')
 require(len(summaries)==1 and set(summaries[0])=={'cases','finished','complete'} and type(summaries[0]['cases']) is int and type(summaries[0]['finished']) is int and type(summaries[0]['complete']) is bool,'Malformed lifecycle summary')
 require(summaries==[{'cases':11,'finished':11,'complete':True}],'Complete eleven-case summary required')
 require(re.findall(r'RESULTS document-lifecycle:(PASS|FAIL)',raw)==['PASS'],'Exactly one transport completion marker required')
 if result is not None:require(result.get('status')=='PASS' and result.get('marker')=='RESULTS document-lifecycle:PASS','Reference adapter completion mismatch')
 complete=True
except Exception as error:errors.append('observation audit: '+repr(error))
post={};pins_same=None;source_same=None;git_same=None
try:post={p:sha(p) for p in pins};pins_same=post==pins
except Exception as error:errors.append('postflight pins: '+repr(error))
try:source_same=all(sha(ROOT/p)==h for p,h in source['files'].items())
except Exception as error:errors.append('postflight source: '+repr(error))
try:git_same=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head
except Exception as error:errors.append('postflight Git: '+repr(error))
monitors=[cg.returncode if cg else None,tm.returncode if tm else None];valid=rc==0 and not timed_out and not outer_signals and not forced_monitor and monitors==[0,0] and cleanup and complete and pins_same is True and source_same is True and git_same is True and not errors
write(out/'pins-after.json',post);write(out/'observations.json',{'cases':case_rows,'rows':observations,'summary':summaries})
receipt={'kind':a.kind,'workspace_candidate':a.iteration,'source_commit':head,'binary_sha256':sha(binary),'fixture_sha256':FIXTURE_SHA,'manifest_sha256':MANIFEST_SHA,'started_utc':started,'finished_utc':now(),'duration_s':time.monotonic()-clock,'actual_inner_returncode':rc,'exit_evidence':exit_evidence,'timed_out':timed_out,'outer_cleanup_signals':outer_signals,'monitor_returncodes':monitors,'monitor_forced_stop':forced_monitor,'cleanup_valid':cleanup,'transport_complete':complete,'case_count':len(case_rows),'observation_row_count':len(observations),'recorded_case_errors':sum(c.get('errorCount',0) for c in case_rows),'pins_unchanged':pins_same,'source_unchanged':source_same,'git_unchanged':git_same,'recorded_input_count':len(source['files']),'measurement_integrity_valid':valid,'behavioral_conformance':'Unclassified until independent reference contract review; transport PASS contributes zero WPT passes. Real case errors remain in observations.','not_WPT':True,'errors':errors,'resource_interpretation':'One-second owned execution tree/cgroup; Chromium includes driver. No general performance claim.'};write(out/'receipt.json',receipt);print(json.dumps({k:receipt[k] for k in ['kind','actual_inner_returncode','monitor_returncodes','cleanup_valid','transport_complete','case_count','observation_row_count','recorded_case_errors','pins_unchanged','source_unchanged','git_unchanged','measurement_integrity_valid','errors']}));sys.exit(0 if valid else 2)
