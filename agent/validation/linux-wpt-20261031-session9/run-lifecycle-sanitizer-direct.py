#!/usr/bin/env python3
"""Record real OS exit from the already sealed sanitized jsrun; no CTest inference."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,signal,subprocess,sys,time
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=ROOT/'agent/validation/linux-wpt-20261031-session9'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def require(v,msg):
 if not v:raise ValueError(msg)
def load(p):return json.loads(Path(p).read_text())
def git():return {'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff','--binary','--','src','tests','CMakeLists.txt','tools/gen_css_props.py'],cwd=ROOT)).hexdigest()}
ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();require(Path.cwd().resolve()==ROOT,'Project cwd required')
build=ROOT/f'agent/validation/iteration-{a.iteration}';prior=BASE/f'native{a.iteration}-lifecycle-sanitizer';out=BASE/f'native{a.iteration}-lifecycle-sanitizer-direct';require(not out.exists(),'Fresh output required')
state=load(build/'build-state.json');manifest=load(build/'source-manifest-before.json');require(manifest==load(build/'source-manifest-after.json'),'Sealed manifests differ');require(state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'],'Unchanged successful normal build required');require(all(sha(ROOT/n)==h for n,h in manifest['files'].items()),'Recorded source changed');before_git=git();require(before_git['head']==state['head'] and before_git['source_diff_sha256']==hashlib.sha256(b'').hexdigest(),'Frozen clean source required')
old=load(prior/'receipt.json');binary=Path(old['instrumented_binary']);require(binary==ROOT/f'build/codex-style-sanitize/jsrun-astra-i{a.iteration}-lifecycle-sanitized','Wrong immutable sanitized target');require(sha(binary)==old['instrumented_binary_sha256'],'Sealed sanitizer target changed');require(old['source_commit']==state['head'] and all(old[k] is True for k in ['source_unchanged','extra_inputs_unchanged','git_unchanged']),'Prior build correspondence invalid');execution=load(prior/'execution.json');require(any(r['label']=='build-jsrun' and r['returncode']==0 for r in execution),'Actual successful instrumented build absent')
fixture=ROOT/'tests/js/document-lifecycle.html';require(sha(fixture)=='cc4c6cfe1372b603cf395d9181a0e4bfaf133c4078ea10f99609c0b4c42c7d04','Original reviewed fixture changed')
contract=BASE/'qa-document-lifecycle/acceptance-contract-v1.json';require(sha(contract)=='7044502e5d3974e2ce7b67e30cf18907c17c28731f9b6f713c316940ac1f6c82','Reviewed contract changed')
paths=[Path(__file__).resolve(),binary,fixture,contract,prior/'receipt.json',prior/'execution.json',prior/'full-ctest-log.txt',ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py']+[build/n for n in ['build-state.json','source-manifest-before.json','source-manifest-after.json','build.log']];pins={str(p):sha(p) for p in paths};out.mkdir();write(out/'pins-before.json',pins)
env=dict(os.environ)
for key in ['JSRUN_PRE','JSRUN_POST','JSRUN_DUMP','LUMEN_FRAME_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_SHOT','LUMEN_SHOT_MS','JSRUN_EXPECT_ATTR_VALUE']:env.pop(key,None)
env.update({'TMPDIR':str(ROOT/'agent/t'),'JSRUN_LAYOUT':'1','JSRUN_UNTIL_ATTR':'data-document-lifecycle-complete','LUMEN_CONSOLE':'0','LUMEN_ICU_DATA':str(ROOT/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat'),'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1','LSAN_OPTIONS':'exitcode=23'})
command=[str(binary),str(fixture),'12000'];inner='import subprocess,pathlib,json,datetime,time,os,signal,sys\ncommand='+repr(command)+'\nout=pathlib.Path('+repr(str(out))+')\nstart=datetime.datetime.now(datetime.timezone.utc).isoformat();t=time.monotonic();signals=[];timeout=False\np=subprocess.Popen(command,start_new_session=True)\ntry:rc=p.wait(timeout=30)\nexcept subprocess.TimeoutExpired:\n timeout=True;os.killpg(p.pid,signal.SIGTERM);signals.append("SIGTERM")\n try:rc=p.wait(timeout=5)\n except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);signals.append("SIGKILL");rc=p.wait()\nr={"started_utc":start,"finished_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"duration_s":time.monotonic()-t,"pid":p.pid,"actual_jsrun_returncode":rc,"timed_out":timeout,"owned_cleanup_signals":signals,"command":command};(out/"actual-native-exit.json").write_text(json.dumps(r,indent=2)+"\\n");sys.exit(rc if rc>=0 else 128-rc)\n'
outer=['python3','tools/codex_compile.py',sys.executable,'-c',inner];started=now();clock=time.monotonic();write(out/'launch.json',{'candidate':a.iteration,'source_commit':state['head'],'immutable_binary_sha256':sha(binary),'fixture_sha256':sha(fixture),'actual_native_command':command,'quota_wrapper':'tools/codex_compile.py','native_environment':{k:env[k] for k in ['JSRUN_LAYOUT','JSRUN_UNTIL_ATTR','LUMEN_CONSOLE','ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']},'resource_interval_seconds':1,'maximum_browser_lanes':1,'diagnostic_reason':'Preserve actual native exit independently of CTest PASS_REGULAR_EXPRESSION; unchanged fixture and leak settings'})
handles=[];mon=[];p=None;wrapper_rc=None;forced=False;errors=[]
try:
 h=(out/'cgroup-monitor.log').open('x');handles.append(h);cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT);mon.append(cg)
 deadline=time.monotonic()+3
 while not (out/'cgroup.csv').exists() and cg.poll() is None and time.monotonic()<deadline:time.sleep(.05)
 require(cg.poll() is None and (out/'cgroup.csv').exists(),'Cgroup monitor startup failed')
 h=(out/'runtime.log').open('x');handles.append(h);p=subprocess.Popen(outer,cwd=ROOT,env=env,stdout=h,stderr=subprocess.STDOUT)
 h=(out/'tree-monitor.log').open('x');handles.append(h);tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label',f'candidate{a.iteration} actual immutable sanitizer exit'],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT);mon.append(tm);wrapper_rc=p.wait()
except Exception as e:errors.append(type(e).__name__+': '+str(e))
finally:
 if p is not None and p.poll() is None:wrapper_rc=p.wait()
 for i,m in enumerate(mon):
  if i==1 and m.poll() is None:
   try:m.wait(6)
   except subprocess.TimeoutExpired:forced=True
  if m.poll() is None:m.terminate()
  try:m.wait(6)
  except subprocess.TimeoutExpired:forced=True;m.kill();m.wait(3)
 for h in handles:h.close()
actual=load(out/'actual-native-exit.json') if (out/'actual-native-exit.json').exists() else {};raw=(out/'runtime.log').read_text();post={str(p):sha(p) for p in paths};source_stable=all(sha(ROOT/n)==h for n,h in manifest['files'].items());after_git=git();write(out/'pins-after.json',post)
ids=[c['id'] for c in load(BASE/'qa-document-lifecycle/acceptance-contract-v1.json')['cases']];marks=re.findall(r'LIFECYCLE_CONTRACT ([A-Za-z0-9-]+):(PASS|FAIL)',raw);behavior=marks==[(n,'PASS') for n in ids] and re.findall(r'RESULTS document-lifecycle:(PASS|FAIL)',raw)==['PASS'] and 'LIFECYCLE_FATAL ' not in raw
leaks=re.findall(r'SUMMARY: AddressSanitizer: (\d+) byte\(s\) leaked in (\d+) allocation\(s\)',raw);diagnostics=bool(re.search(r'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|SUMMARY: (AddressSanitizer|UndefinedBehaviorSanitizer)',raw));integrity=bool(actual) and wrapper_rc==(actual['actual_jsrun_returncode'] if actual['actual_jsrun_returncode']>=0 else 128-actual['actual_jsrun_returncode']) and not actual.get('timed_out') and actual.get('owned_cleanup_signals')==[] and source_stable and pins==post and before_git==after_git and [m.returncode for m in mon]==[0,0] and not forced and not errors
qualifies=integrity and actual.get('actual_jsrun_returncode')==0 and wrapper_rc==0 and behavior and not diagnostics
r={'candidate':a.iteration,'source_commit':state['head'],'started_utc':started,'finished_utc':now(),'duration_s':time.monotonic()-clock,'actual_quota_wrapper_returncode':wrapper_rc,'actual_jsrun_returncode':actual.get('actual_jsrun_returncode'),'actual_native_exit_receipt_sha256':sha(out/'actual-native-exit.json') if actual else None,'immutable_binary_sha256':sha(binary),'fixture_sha256':sha(fixture),'recorded_input_count':len(manifest['files']),'source_unchanged':source_stable,'pins_unchanged':pins==post,'git_unchanged':before_git==after_git,'monitor_returncodes':[m.returncode for m in mon],'monitor_forced_stop':forced,'behavioral12_cases_pass':behavior,'sanitizer_diagnostics_present':diagnostics,'leak_summaries':[{'bytes':int(b),'allocations':int(n)} for b,n in leaks],'measurement_integrity_valid':integrity,'sanitizer_qualification_pass':qualifies,'errors':errors,'runtime_log_sha256':sha(out/'runtime.log')};write(out/'receipt.json',r);print(json.dumps(r),flush=True);sys.exit(0 if qualifies else 2)
