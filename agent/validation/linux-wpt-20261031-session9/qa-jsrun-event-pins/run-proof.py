#!/usr/bin/env python3
"""One ownership-only real jsrun host proof; frozen engine and unchanged behavioral rows."""
from pathlib import Path
import datetime,hashlib,json,os,re,shlex,subprocess,sys,time
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=ROOT/'agent/validation/linux-wpt-20261031-session9';QA=Path(__file__).resolve().parent;SAN=ROOT/'build/codex-style-sanitize'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def require(v,msg):
 if not v:raise ValueError(msg)
def load(p):return json.loads(Path(p).read_text())
def git():return {'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff','--binary','--','src','tests','CMakeLists.txt','tools/gen_css_props.py'],cwd=ROOT)).hexdigest()}
require(Path.cwd().resolve()==ROOT,'Project cwd required');out=QA/'proof232';require(not out.exists(),'Fresh proof output required')
plan=load(QA/'proof-plan.json');require(sha(QA/'proof-plan.json')=='f1b960b520150a9d6f0973faedcb8a7a774dda318b24283ccd634b824bf43f3a','Reviewed plan changed')
source=ROOT/plan['proof_source'];base=ROOT/plan['base_source'];fixture=ROOT/plan['unchanged_fixture'];require(sha(source)==plan['proof_sha256'] and sha(base)==plan['base_sha256'] and sha(fixture)==plan['fixture_sha256'],'Reviewed input changed');require(sha(QA/'exact-three-call-delta.patch')==plan['delta_sha256'],'Reviewed delta changed')
lines=source.read_text().splitlines(keepends=True);require(sum(l.strip()=='js_release_pins();' for l in lines)==3 and ''.join(l for l in lines if l.strip()!='js_release_pins();')==base.read_text(),'Proof is not exactly three added calls')
build=ROOT/'agent/validation/iteration-232';state=load(build/'build-state.json');manifest=load(build/'source-manifest-before.json');require(manifest==load(build/'source-manifest-after.json') and state['head']==plan['base_commit'] and state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'],'Original source/build mismatch');require(all(sha(ROOT/n)==h for n,h in manifest['files'].items()),'Current recorded source changed');before_git=git();require(before_git['head']==state['head'] and before_git['source_diff_sha256']==hashlib.sha256(b'').hexdigest(),'Frozen clean source required')
control=BASE/'native232-lifecycle-sanitizer-direct';cr=load(control/'receipt.json');require(cr['measurement_integrity_valid'] is True and cr['actual_jsrun_returncode']==23 and cr['behavioral12_cases_pass'] is True and cr['leak_summaries']==[{'bytes':54207,'allocations':48}],'Exact failed baseline control required');immutable=SAN/'jsrun-astra-i232-lifecycle-sanitized';require(sha(immutable)==cr['immutable_binary_sha256']==sha(SAN/'jsrun'),'Frozen sanitized runtime correspondence differs')
flags_path=SAN/'CMakeFiles/jsrun.dir/flags.make';link_path=SAN/'CMakeFiles/jsrun.dir/link.txt';fields={l.split(' = ',1)[0]:l.split(' = ',1)[1] for l in flags_path.read_text().splitlines() if ' = ' in l};require('-fsanitize=address,undefined' in fields['C_FLAGS'],'Sanitizer C flags absent');link=shlex.split(link_path.read_text());require('-fsanitize=address,undefined' in link,'Sanitizer link flags absent')
out.mkdir();obj=out/'jsrun-event-pins.o';binary=out/'jsrun-event-pins-sanitized';compile_cmd=['/usr/bin/clang']+shlex.split(fields['C_DEFINES'])+shlex.split(fields['C_INCLUDES'])+shlex.split(fields['C_FLAGS'])+['-iquote',str(ROOT/'tests'),'-o',str(obj),'-c',str(source)]
original_link=list(link);require(link.count('CMakeFiles/jsrun.dir/tests/jsrun.c.o')==1,'Unexpected original host object');link[link.index('CMakeFiles/jsrun.dir/tests/jsrun.c.o')]=str(obj);oi=link.index('-o');require(link[oi+1]=='jsrun','Unexpected original binary');link[oi+1]=str(binary)
for i,v in enumerate(link):
 if v.startswith('--dependency-file='):link[i]='--dependency-file='+str(out/'link.d')
explicit=[]
for token in original_link:
 p=Path(token);p=p if p.is_absolute() else SAN/p
 if not token.startswith('-') and p.is_file() and p not in explicit:explicit.append(p)
require(SAN/'liblumen_engine.a' in explicit,'Frozen actual engine library absent');paths=[Path(__file__).resolve(),QA/'proof-plan.json',QA/'exact-three-call-delta.patch',source,base,fixture,immutable,SAN/'jsrun',flags_path,link_path,control/'receipt.json',control/'runtime.log',ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py']+explicit+[build/n for n in ['build-state.json','source-manifest-before.json','source-manifest-after.json','build.log']];pins={str(p):sha(p) for p in paths};write(out/'pins-before.json',pins)
env=dict(os.environ)
for key in ['JSRUN_PRE','JSRUN_POST','JSRUN_DUMP','LUMEN_FRAME_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_SHOT','LUMEN_SHOT_MS','JSRUN_EXPECT_ATTR_VALUE']:env.pop(key,None)
env.update({'TMPDIR':str(ROOT/'agent/t'),'JSRUN_LAYOUT':'1','JSRUN_UNTIL_ATTR':'data-document-lifecycle-complete','LUMEN_CONSOLE':'0','LUMEN_ICU_DATA':str(ROOT/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat'),'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1','LSAN_OPTIONS':'exitcode=23'})
runtime=[str(binary),str(fixture),'12000'];runtime_inner='import subprocess,pathlib,json,datetime,time,os,signal,sys,hashlib\ncommand='+repr(runtime)+'\nout=pathlib.Path('+repr(str(out))+')\nstart=datetime.datetime.now(datetime.timezone.utc).isoformat();clock=time.monotonic();signals=[];timeout=False\nbefore=hashlib.sha256(pathlib.Path(command[0]).read_bytes()).hexdigest();p=subprocess.Popen(command,start_new_session=True)\ntry:rc=p.wait(timeout=30)\nexcept subprocess.TimeoutExpired:\n timeout=True;os.killpg(p.pid,signal.SIGTERM);signals.append("SIGTERM")\n try:rc=p.wait(timeout=5)\n except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);signals.append("SIGKILL");rc=p.wait()\nr={"started_utc":start,"finished_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"duration_s":time.monotonic()-clock,"pid":p.pid,"actual_jsrun_returncode":rc,"timed_out":timeout,"owned_cleanup_signals":signals,"binary_sha256":before,"binary_unchanged":hashlib.sha256(pathlib.Path(command[0]).read_bytes()).hexdigest()==before,"command":command};(out/"actual-native-exit.json").write_text(json.dumps(r,indent=2)+"\\n");sys.exit(rc if rc>=0 else 128-rc)\n'
commands=[{'label':'compile-host','command':['python3',str(ROOT/'tools/codex_compile.py')]+compile_cmd},{'label':'link-real-runtime','command':['python3',str(ROOT/'tools/codex_compile.py')]+link},{'label':'runtime','command':['python3',str(ROOT/'tools/codex_compile.py'),sys.executable,'-c',runtime_inner]}]
write(out/'plan.json',{'source_commit':state['head'],'original_source_sha256':sha(base),'proof_source_sha256':sha(source),'fixture_sha256':sha(fixture),'exact_three_call_delta':True,'frozen_engine_libraries':{str(p):sha(p) for p in explicit if p.suffix=='.a'},'compile_command':compile_cmd,'original_link_command':original_link,'proof_link_command':link,'runtime_command':runtime,'sanitizer_options':{k:env[k] for k in ['ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']},'resource_interval_seconds':1,'maximum_browser_lanes':1,'source_manifest_limit':'All recorded source plus explicit frozen link inputs; -l-resolved system dependencies are not a full transitive closure'})
inner='import subprocess,pathlib,json,datetime,sys\ncommands='+repr(commands)+'\nout=pathlib.Path('+repr(str(out))+')\nrows=[]\nfor item in commands:\n start=datetime.datetime.now(datetime.timezone.utc).isoformat()\n with (out/(item["label"]+".log")).open("x") as f:p=subprocess.run(item["command"],stdout=f,stderr=subprocess.STDOUT)\n rows.append({"label":item["label"],"started_utc":start,"finished_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"actual_returncode":p.returncode});(out/"execution.json").write_text(json.dumps(rows,indent=2)+"\\n")\n if p.returncode:sys.exit(p.returncode)\n'
started=now();clock=time.monotonic();mon=[];handles=[];p=None;rc=None;forced=False;errors=[]
try:
 h=(out/'cgroup-monitor.log').open('x');handles.append(h);cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT);mon.append(cg)
 end=time.monotonic()+3
 while not (out/'cgroup.csv').exists() and cg.poll() is None and time.monotonic()<end:time.sleep(.05)
 require(cg.poll() is None and (out/'cgroup.csv').exists(),'Cgroup monitor startup failed')
 h=(out/'controller.log').open('x');handles.append(h);p=subprocess.Popen([sys.executable,'-c',inner],cwd=SAN,env=env,stdout=h,stderr=subprocess.STDOUT)
 h=(out/'tree-monitor.log').open('x');handles.append(h);tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label','frozen232 actual host pin-release proof'],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT);mon.append(tm);rc=p.wait()
except Exception as e:errors.append(type(e).__name__+': '+str(e))
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
actual=load(out/'actual-native-exit.json') if (out/'actual-native-exit.json').exists() else {};raw=(out/'runtime.log').read_text() if (out/'runtime.log').exists() else '';baseline_raw=(control/'runtime.log').read_text()
def observations(t):
 result={}
 for prefix in ['LIFECYCLE_CASE ','LIFECYCLE_ROW ','LIFECYCLE_CONTRACT_SUMMARY ','MEASUREMENTS document-lifecycle ']:result[prefix]=[json.loads(l.split(prefix,1)[1]) for l in t.splitlines() if prefix in l]
 return result
try:identical=observations(raw)==observations(baseline_raw) and len(observations(raw)['LIFECYCLE_CASE '])==12
except Exception as e:identical=False;errors.append(type(e).__name__+': '+str(e))
behavior=re.findall(r'LIFECYCLE_CONTRACT ([A-Za-z0-9-]+):(PASS|FAIL)',raw)==re.findall(r'LIFECYCLE_CONTRACT ([A-Za-z0-9-]+):(PASS|FAIL)',baseline_raw) and re.findall(r'RESULTS document-lifecycle:(PASS|FAIL)',raw)==['PASS'] and 'LIFECYCLE_FATAL ' not in raw
diagnostics=bool(re.search(r'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|SUMMARY: (AddressSanitizer|UndefinedBehaviorSanitizer)',raw));post={str(p):sha(p) for p in paths};source_stable=all(sha(ROOT/n)==h for n,h in manifest['files'].items());after_git=git();write(out/'pins-after.json',post)
valid=rc==0 and actual.get('actual_jsrun_returncode')==0 and actual.get('timed_out') is False and actual.get('owned_cleanup_signals')==[] and actual.get('binary_unchanged') is True and identical and behavior and not diagnostics and source_stable and pins==post and before_git==after_git and [m.returncode for m in mon]==[0,0] and not forced and not errors
r={'created_utc':now(),'started_utc':started,'duration_s':time.monotonic()-clock,'source_commit':state['head'],'proof_source_sha256':sha(source),'actual_outer_sequence_returncode':rc,'actual_jsrun_returncode':actual.get('actual_jsrun_returncode'),'proof_binary_sha256':sha(binary) if binary.exists() else None,'exact_three_call_delta':True,'all_structured_behavioral_rows_unchanged':identical,'all12_behavior_cases_pass':behavior,'sanitizer_diagnostics_present':diagnostics,'source_unchanged':source_stable,'all_explicit_link_inputs_and_pins_unchanged':pins==post,'git_unchanged':before_git==after_git,'monitor_returncodes':[m.returncode for m in mon],'monitor_forced_stop':forced,'errors':errors,'proof_pass':valid,'scope':'Actual copied jsrun host with only3 real pin-release boundary calls; engine, fixture, expectations and leak detection unchanged'};write(out/'receipt.json',r);print(json.dumps(r),flush=True);sys.exit(0 if valid else 2)
