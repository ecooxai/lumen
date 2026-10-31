#!/usr/bin/env python3
"""Real native-layout diagnostic; separate from upstream WPT qualification."""
from pathlib import Path
import argparse,subprocess,json,hashlib,datetime,time,sys,os,shutil,signal
ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();root=Path.cwd();base=Path(__file__).resolve().parent;out=base/f'jsrun{a.iteration}-v2';assert not out.exists()
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
plan=json.loads((base/'manifest-v2.json').read_text());fixture=root/plan['diagnostic_path'];assert sha(fixture)==plan['diagnostic_sha256'];original=subprocess.check_output(['git','show','30c48fcaed638e17d9aae4292bdb86a83fc1cce2:'+plan['source_path']]);assert hashlib.sha256(original).hexdigest()==plan['source_sha256'];build=root/f'agent/validation/iteration-{a.iteration}';state=json.loads((build/'build-state.json').read_text());source=json.loads((build/'source-manifest-after.json').read_text());assert source==json.loads((build/'source-manifest-before.json').read_text());assert state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'];assert all(sha(root/p)==h for p,h in source['files'].items());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==state['head'];log=(build/'build.log').read_text();assert 'Linking CXX executable jsrun' in log
current=root/'build/codex-native/jsrun';binary=root/f'build/codex-native/jsrun-astra-i{a.iteration}';current_sha=sha(current)
if not binary.exists():
 with current.open('rb') as src,binary.open('xb') as dst:shutil.copyfileobj(src,dst)
 binary.chmod(0o555)
assert sha(binary)==sha(current)==current_sha
out.mkdir();extra=[Path(__file__),base/'manifest-v2.json',fixture,binary,current,build/'build-state.json',build/'source-manifest-after.json',build/'build.log',root/'tools/codex_compile.py'];pins={str(p.resolve()):sha(p) for p in extra};(out/'pins-before.json').write_text(json.dumps(pins,indent=2)+'\n')
cmd=['python3','tools/codex_compile.py',str(binary),str(fixture),str(plan['jsrun_duration_ms'])];env=dict(os.environ)
for k in ['LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_SHOT','LUMEN_SHOT_MS']:env.pop(k,None)
env.update(plan['environment']);env['TMPDIR']=str(root/'agent/t');env['LUMEN_ICU_DATA']=str(root/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat');start=datetime.datetime.now(datetime.timezone.utc).isoformat();t=time.monotonic();(out/'launch.json').write_text(json.dumps({'command':cmd,'source_commit':head,'jsrun_sha256':current_sha,'fixture_sha256':plan['diagnostic_sha256'],'completion_attribute':plan['completion_attribute'],'resource_interval_seconds':1,'not_wpt':True,'fixed_root_viewport':True,'geometry_instrumentation':'Delegates original getBoundingClientRect unchanged; counts reads only'},indent=2)+'\n');timed_out=False
with (out/'diagnostic.log').open('x') as stdout,(out/'cgroup-monitor.log').open('x') as cglog,(out/'tree-monitor.log').open('x') as tlog:
 cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],stdout=cglog,stderr=subprocess.STDOUT)
 p=subprocess.Popen(cmd,stdout=stdout,stderr=subprocess.STDOUT,env=env,start_new_session=True)
 tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label',f'candidate{a.iteration} actual native-layout IO timing diagnostic'],stdout=tlog,stderr=subprocess.STDOUT)
 try:rc=p.wait(20)
 except subprocess.TimeoutExpired:
  timed_out=True;os.killpg(p.pid,signal.SIGTERM)
  try:rc=p.wait(5)
  except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);rc=p.wait(3)
 try:tm.wait(6)
 except subprocess.TimeoutExpired:tm.terminate();tm.wait(6)
 cg.terminate();cg.wait(6)
raw=(out/'diagnostic.log').read_text();events=[]
for line in raw.splitlines():
 marker='DIAGNOSTIC io-frame-cadence '
 if marker in line:events.append(json.loads(line[line.index(marker)+len(marker):]))
completion=[e for e in events if e.get('phase')=='complete'];idle=[e for e in events if e.get('phase')=='idle-end'];post={p:sha(p) for p in pins};legacy=plan['legacy_marker'] in raw and 'RESULTS intersection-geometry:FAIL' not in raw;cadence=plan['probe_marker'] in raw and 'RESULTS io-frame-cadence:FAIL' not in raw;stable=all(sha(root/p)==h for p,h in source['files'].items());git_same=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head
r={'candidate':a.iteration,'started_utc':start,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'duration_s':time.monotonic()-t,'returncode':rc,'timed_out':timed_out,'monitor_returncodes':[cg.returncode,tm.returncode],'pins_unchanged':pins==post,'source_unchanged':stable,'git_unchanged':git_same,'source_commit':head,'jsrun_sha256':current_sha,'legacy_geometry_pass':legacy,'cadence_pass':cadence,'all_phases_completed':len(completion)==1 and len(idle)==1,'quiescent_idle':idle,'failures':completion[0]['failures'] if completion else None,'events':events,'not_wpt':True,'qualification':'Both original geometry and final cadence PASS required for a repaired candidate. Baseline failure remains a real diagnostic result; fixed-root jsrun does not replace WPT root-scroll validation.'};(out/'pins-after.json').write_text(json.dumps(post,indent=2)+'\n');(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:r[k] for k in ['candidate','duration_s','returncode','monitor_returncodes','pins_unchanged','source_unchanged','git_unchanged','legacy_geometry_pass','cadence_pass','all_phases_completed','quiescent_idle','failures']}));print(raw[-10000:]);sys.exit(0 if rc==0 and not timed_out and legacy and len(completion)==1 and len(idle)==1 and pins==post and stable and git_same and cg.returncode==0 and tm.returncode==0 else 2)
