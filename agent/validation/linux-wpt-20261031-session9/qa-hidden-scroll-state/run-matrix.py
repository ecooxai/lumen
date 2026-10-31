#!/usr/bin/env python3
"""Transport-complete state measurements; this controller never declares behavioral conformance."""
from pathlib import Path
import argparse,subprocess,json,hashlib,datetime,time,sys,os,shutil,signal,collections
ap=argparse.ArgumentParser();ap.add_argument('--kind',choices=['lumen','chromium156'],required=True);ap.add_argument('--iteration',type=int,required=True);ap.add_argument('--manifest',default='manifest-v3.json');a=ap.parse_args();root=Path.cwd();base=Path(__file__).resolve().parent;manifest=(base/a.manifest).resolve();assert manifest.parent==base
sha=lambda p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
now=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat()
plan=json.loads(manifest.read_text());fixture=root/plan['fixture'];assert sha(fixture)==plan['sha256'];assert plan['expected_case_count']==7 and plan['expected_snapshot_count']==30
out=base/(f'jsrun{a.iteration}-v3' if a.kind=='lumen' else 'chromium156-v3');assert not out.exists()
build=root/f'agent/validation/iteration-{a.iteration}';state=json.loads((build/'build-state.json').read_text());source=json.loads((build/'source-manifest-after.json').read_text());assert source==json.loads((build/'source-manifest-before.json').read_text());assert state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'];assert all(sha(root/p)==h for p,h in source['files'].items());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==state['head']
paths=[Path(__file__),manifest,fixture,build/'build-state.json',build/'source-manifest-before.json',build/'source-manifest-after.json',root/'tools/codex_compile.py',root/'agent/progress/monitor.py',root/'agent/progress/cgroup_monitor.py']
env=dict(os.environ)
for k in ['LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_SHOT','LUMEN_SHOT_MS']:env.pop(k,None)
env['TMPDIR']=str(root/'agent/t');env['LUMEN_ICU_DATA']=str(root/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat')
if a.kind=='lumen':
 assert 'Linking CXX executable jsrun' in (build/'build.log').read_text()
 current=root/'build/codex-native/jsrun';binary=root/f'build/codex-native/jsrun-astra-i{a.iteration}'
 if not binary.exists():
  with current.open('rb') as src,binary.open('xb') as dst:shutil.copyfileobj(src,dst)
  binary.chmod(0o555)
 assert sha(binary)==sha(current);paths.extend([binary,current,build/'build.log']);env.update(plan['environment']);cmd=[str(binary),str(fixture),str(plan['jsrun_duration_ms'])]
else:
 cache=root/'build/references/chrome-for-testing-156.0.8078.80';binary=cache/'chrome-linux64/chrome';driver=cache/'chromedriver-linux64/chromedriver';adapter=root/'agent/validation/linux-wpt-20261030-session7/qa-script-scheduling/authored/run-cft156-fixture.py';assert sha(binary)=='1bcad71ad1bff85955db13c6d8dd9f32374934d5a5cb079b526c46fb51f9bd97';assert sha(driver)=='d7f1682dcf9e4087bae6a52ca03c9913390a79ab86e09796562db7ed6f534587';paths.extend([binary,driver,adapter]);cmd=['python3',str(adapter),'--fixture',str(fixture),'--marker',plan['adapter_marker'],'--output',str(out/'browser'),'--timeout','20']
cmd=['python3',str(root/'tools/codex_compile.py')]+cmd;pins={str(p.resolve()):sha(p) for p in paths};out.mkdir();(out/'pins-before.json').write_text(json.dumps(pins,indent=2)+'\n');start=now();t=time.monotonic();timed_out=False
(out/'launch.json').write_text(json.dumps({'kind':a.kind,'candidate_workspace':a.iteration,'source_commit':head,'command':cmd,'fixture_sha256':plan['sha256'],'binary_sha256':sha(binary),'resource_interval_seconds':1,'measurement_only':True,'transport_PASS_does_not_imply_behavior_PASS':True},indent=2)+'\n')
with (out/'controller.log').open('x') as stdout,(out/'cgroup-monitor.log').open('x') as cglog,(out/'tree-monitor.log').open('x') as tlog:
 cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],stdout=cglog,stderr=subprocess.STDOUT)
 p=subprocess.Popen(cmd,stdout=stdout,stderr=subprocess.STDOUT,env=env,start_new_session=True)
 tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label',a.kind+' owned hidden scroll matrix measurement'],stdout=tlog,stderr=subprocess.STDOUT)
 try:rc=p.wait(45)
 except subprocess.TimeoutExpired:
  timed_out=True;os.killpg(p.pid,signal.SIGTERM)
  try:rc=p.wait(5)
  except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);rc=p.wait(3)
 try:tm.wait(6)
 except subprocess.TimeoutExpired:tm.terminate();tm.wait(6)
 cg.terminate();cg.wait(6)
raw=(out/'controller.log').read_text();cleanup=True;exit_evidence={'native_jsrun_actual_returncode':rc}
if a.kind=='chromium156':
 result=json.loads((out/'browser/result.json').read_text());raw='\n'.join(result.get('console',[]));owned=result.get('owned_process_cleanup',{});cleanup=result.get('status')=='PASS' and result.get('profile_removed') is True and result.get('fixture_unchanged') is True and owned.get('remaining_same_identity')==[] and bool(owned.get('recorded_identities')) and owned.get('signals')==[] and not result.get('driver_cleanup_error') and not result.get('error');exit_evidence={'adapter_actual_returncode':rc,'chromium_os_exit_status':'Not exposed by unchanged supported public adapter','profile_removed':result.get('profile_removed'),'remaining_owned_identities':owned.get('remaining_same_identity'),'fallback_signals':owned.get('signals'),'driver_cleanup_error':result.get('driver_cleanup_error')}
records=[];completion=[]
for line in raw.splitlines():
 prefix='DIAGNOSTIC hidden-scroll-state '
 if prefix in line:records.append(json.loads(line[line.index(prefix)+len(prefix):]))
 prefix=plan['completion_marker']+' '
 if prefix in line:completion.append(json.loads(line[line.index(prefix)+len(prefix):]))
expected={x['id']:x['snapshots'] for x in plan['cases']};counts=dict(collections.Counter(x['id'] for x in records));complete=len(completion)==1 and completion[0].get('completed')==list(expected) and completion[0].get('records')==30 and len(records)==30 and counts==expected and len({(x['id'],x['phase']) for x in records})==30 and plan['transport_success_marker'] in raw and plan['transport_failure_marker'] not in raw and plan['error_marker'] not in raw
post={p:sha(p) for p in pins};stable=all(sha(root/p)==h for p,h in source['files'].items());git_same=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head
valid=rc==0 and not timed_out and cg.returncode==tm.returncode==0 and pins==post and stable and git_same and cleanup and complete
r={'kind':a.kind,'workspace_candidate':a.iteration,'started_utc':start,'finished_utc':now(),'duration_s':time.monotonic()-t,'returncode':rc,'timed_out':timed_out,'monitor_returncodes':[cg.returncode,tm.returncode],'pins_unchanged':pins==post,'source_unchanged':stable,'git_unchanged':git_same,'source_commit':head,'binary_sha256':sha(binary),'fixture_sha256':plan['sha256'],'transport_complete':complete,'all7_cases30_snapshots':counts==expected and len(records)==30,'cleanup_valid':cleanup,'exit_evidence':exit_evidence,'measurement_integrity_valid':valid,'behavioral_conformance':'Unclassified pending independent reference observation review; transport PASS is not a behavioral result.','not_WPT':True,'records':records,'completion':completion};(out/'pins-after.json').write_text(json.dumps(post,indent=2)+'\n');(out/'observations.json').write_text(json.dumps(records,indent=2)+'\n');(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:r[k] for k in ['kind','duration_s','returncode','monitor_returncodes','pins_unchanged','source_unchanged','git_unchanged','transport_complete','all7_cases30_snapshots','cleanup_valid','measurement_integrity_valid']}));sys.exit(0 if valid else 2)
