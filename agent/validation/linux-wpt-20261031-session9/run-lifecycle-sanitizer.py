#!/usr/bin/env python3
"""Build and exercise actual instrumented lifecycle C++ through the existing jsrun host."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,subprocess,sys,time,xml.etree.ElementTree as ET
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android')
BASE=ROOT/'agent/validation/linux-wpt-20261031-session9'
SAN=ROOT/'build/codex-style-sanitize'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def gitstate():
 return {'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff','--binary','--','src','tests','CMakeLists.txt','tools/gen_css_props.py'],cwd=ROOT)).hexdigest()}
def require(v,msg):
 if not v:raise ValueError(msg)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);ap.add_argument('--fixture-sha256',required=True);a=ap.parse_args()
 require(Path.cwd().resolve()==ROOT,'Run from the project root');require(re.fullmatch('[0-9a-f]{64}',a.fixture_sha256),'Invalid fixture hash')
 build=ROOT/f'agent/validation/iteration-{a.iteration}';out=BASE/f'native{a.iteration}-lifecycle-sanitizer';require(not out.exists(),'Fresh output required')
 state=json.loads((build/'build-state.json').read_text());manifest=json.loads((build/'source-manifest-before.json').read_text())
 require(manifest==json.loads((build/'source-manifest-after.json').read_text()),'Build manifests differ')
 require(state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'],'Successful unchanged build required')
 require(state['source_manifest_before']==state['source_manifest_after']==manifest['sha256'],'Manifest binding differs')
 require(all(sha(ROOT/n)==h for n,h in manifest['files'].items()),'Current recorded source differs')
 before_git=gitstate();require(before_git['head']==state['head'] and before_git['source_diff_sha256']==hashlib.sha256(b'').hexdigest(),'Frozen clean source required')
 fixture=ROOT/'tests/js/document-lifecycle.html';require(sha(fixture)==a.fixture_sha256,'Reviewed fixture differs')
 cache=(SAN/'CMakeCache.txt').read_text();flag_keys=('CMAKE_C_FLAGS:','CMAKE_CXX_FLAGS:','CMAKE_EXE_LINKER_FLAGS:');flags={line.split(':',1)[0]:line.split('=',1)[1] for line in cache.splitlines() if line.startswith(flag_keys)}
 require(len(flags)==3 and all('-fsanitize=address,undefined' in v for v in flags.values()),'Existing C/C++/link sanitizer flags required')
 extras=[Path(__file__).resolve(),fixture,ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py',BASE/'qa-document-lifecycle/acceptance-contract-v1.json']+[build/n for n in ['build-state.json','source-manifest-before.json','source-manifest-after.json','build.log']]
 pins={str(p):sha(p) for p in extras};out.mkdir();write(out/'pins-before.json',pins);(out/'cache-before.txt').write_text(cache)
 commands=[{'label':'configure','command':['python3','tools/codex_compile.py','cmake','-S',str(ROOT),'-B',str(SAN)]},{'label':'build-jsrun','command':['python3','tools/codex_compile.py','cmake','--build',str(SAN),'--target','jsrun','-j2']},{'label':'ctest','command':['python3','tools/codex_compile.py','ctest','--test-dir',str(SAN),'-j1','--output-on-failure','-R','^js_document_lifecycle$','--output-junit',str(out/'ctest.xml')]}]
 write(out/'plan.json',{'candidate':a.iteration,'source_commit':state['head'],'source_manifest_sha256':manifest['sha256'],'recorded_input_count':len(manifest['files']),'fixture_sha256':a.fixture_sha256,'commands':commands,'flags':flags,'actual_cpp_paths':['src/js/frames.cc','src/js/js.cc'],'runtime_host':'actual instrumented jsrun including iframe creation, document completion, removal and reentry','maximum_browser_lanes':1,'resource_interval_seconds':1,'prebuilt_V8_itself_instrumented':False,'scope':'Existing target and sanitizer configuration; no suppressions, fake host or changed behavioral oracle'})
 inner='import subprocess,json,pathlib,sys,datetime\ncommands='+repr(commands)+'\nout=pathlib.Path('+repr(str(out))+')\nrows=[]\nfor item in commands:\n started=datetime.datetime.now(datetime.timezone.utc).isoformat()\n with (out/(item["label"]+".log")).open("x") as f:p=subprocess.run(item["command"],stdout=f,stderr=subprocess.STDOUT)\n rows.append({**item,"started_utc":started,"finished_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"returncode":p.returncode});(out/"execution.json").write_text(json.dumps(rows,indent=2)+"\\n")\n print(json.dumps(rows[-1]),flush=True)\n if p.returncode:sys.exit(p.returncode)\n'
 env=dict(os.environ)
 for key in ['JSRUN_PRE','JSRUN_POST','LUMEN_FRAME_PRE','JSRUN_DUMP','JSRUN_UNTIL_ATTR','JSRUN_LAYOUT','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_SHOT','LUMEN_SHOT_MS']:env.pop(key,None)
 env.update({'TMPDIR':str(ROOT/'agent/t'),'LUMEN_ICU_DATA':str(ROOT/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat'),'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1','LSAN_OPTIONS':'exitcode=23'})
 started=now();t=time.monotonic();mon=[];handles=[];p=None;rc=None;forced=False;errors=[]
 try:
  h=(out/'cgroup-monitor.log').open('x');handles.append(h);cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT);mon.append(cg)
  deadline=time.monotonic()+3
  while not (out/'cgroup.csv').exists() and cg.poll() is None and time.monotonic()<deadline:time.sleep(.05)
  require(cg.poll() is None and (out/'cgroup.csv').exists(),'Cgroup monitor startup failed')
  h=(out/'controller.log').open('x');handles.append(h);p=subprocess.Popen([sys.executable,'-c',inner],cwd=ROOT,env=env,stdout=h,stderr=subprocess.STDOUT)
  h=(out/'tree-monitor.log').open('x');handles.append(h);tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label',f'candidate{a.iteration} actual lifecycle C++ ASan UBSan'],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT);mon.append(tm)
  write(out/'launch.json',{'started_utc':started,'source_commit':state['head'],'controller_pid':p.pid,'command':[sys.executable,'-c','recorded command sequence in plan.json'],'sanitizer_options':{k:env[k] for k in ['ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']},'resource_interval_seconds':1})
  rc=p.wait()
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
 evidence={};strict=False;immutable=None;immutable_sha=None
 try:
  execution=json.loads((out/'execution.json').read_text())
  if any(r['label']=='build-jsrun' and r['returncode']==0 for r in execution):
   immutable=SAN/f'jsrun-astra-i{a.iteration}-lifecycle-sanitized';require(not immutable.exists(),'Immutable sanitizer target already exists');immutable.write_bytes((SAN/'jsrun').read_bytes());immutable.chmod((SAN/'jsrun').stat().st_mode & 0o777);immutable_sha=sha(immutable)
  require(any(r['label']=='ctest' for r in execution),'Targeted CTest was not executed')
  log=SAN/'Testing/Temporary/LastTest.log';require(log.is_file(),'Missing full CTest output');full_raw=log.read_text();(out/'full-ctest-log.txt').write_text(full_raw)
  require(full_raw.count('Output:\n')==1 and full_raw.count('<end of output>')==1,'Expected one actual CTest output section');raw=full_raw.split('Output:\n',1)[1].split('<end of output>',1)[0]
  jt=ET.parse(out/'ctest.xml').getroot();cases=jt.findall('.//testcase');require(len(cases)==1 and cases[0].attrib.get('name')=='js_document_lifecycle' and cases[0].find('failure') is None and cases[0].find('error') is None,'Actual targeted CTest failed or missing')
  expected=json.loads((BASE/'qa-document-lifecycle/acceptance-contract-v1.json').read_text());ids=[x['id'] for x in expected['cases']]
  marks=re.findall(r'LIFECYCLE_CONTRACT ([A-Za-z0-9-]+):(PASS|FAIL)',raw);require(marks==[(n,'PASS') for n in ids],'All12 exact durable contract markers required')
  summaries=[json.loads(l.split('LIFECYCLE_CONTRACT_SUMMARY ',1)[1]) for l in raw.splitlines() if 'LIFECYCLE_CONTRACT_SUMMARY ' in l];require(summaries==[{'cases':12,'passed':12,'failed':0}],'Strict lifecycle summary differs')
  require(re.findall(r'RESULTS document-lifecycle:(PASS|FAIL)',raw)==['PASS'] and 'LIFECYCLE_FATAL ' not in raw,'Final lifecycle marker invalid')
  require(not re.search(r'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|SUMMARY: (AddressSanitizer|UndefinedBehaviorSanitizer)',raw),'Sanitizer diagnostics found')
  generated={}
  for rel in ['CMakeFiles/lumen_engine.dir/flags.make','CMakeFiles/jsrun.dir/flags.make','CMakeFiles/jsrun.dir/link.txt']:
   path=SAN/rel;text=path.read_text();require('-fsanitize=address,undefined' in text,'Instrumented compile/link flag absent: '+rel);generated[rel]={'sha256':sha(path),'text':text}
  objects={}
  for rel in ['CMakeFiles/lumen_engine.dir/src/js/frames.cc.o','CMakeFiles/lumen_engine.dir/src/js/js.cc.o','CMakeFiles/jsrun.dir/tests/jsrun.c.o']:
   path=SAN/rel;symbols=subprocess.check_output(['nm','-u',str(path)],text=True);require('__asan' in symbols and '__ubsan' in symbols,'Actual object instrumentation missing: '+rel);objects[rel]={'sha256':sha(path),'address_and_undefined_instrumentation_present':True}
  require(immutable is not None and immutable_sha==sha(SAN/'jsrun'),'Instrumented runtime was not sealed from this successful build')
  evidence={'actual_native_test_count':1,'all12_durable_contracts_pass':True,'durable_finish_sequence_and_zero_late_records_asserted':True,'fixture_sha256':sha(fixture),'instrumented_binary':str(immutable),'instrumented_binary_sha256':sha(immutable),'instrumented_objects':objects,'generated_compile_link_inputs':generated,'full_log_sha256':sha(out/'full-ctest-log.txt'),'ctest_xml_sha256':sha(out/'ctest.xml')};write(out/'instrumentation-and-behavior.json',evidence);strict=True
 except Exception as e:errors.append(type(e).__name__+': '+str(e))
 post={str(p):sha(p) for p in extras};source_stable=all(sha(ROOT/n)==h for n,h in manifest['files'].items());after_git=gitstate();write(out/'pins-after.json',post);(out/'cache-after.txt').write_text((SAN/'CMakeCache.txt').read_text())
 valid=rc==0 and strict and not errors and not forced and len(mon)==2 and [m.returncode for m in mon]==[0,0] and source_stable and pins==post and before_git==after_git
 receipt={'candidate':a.iteration,'source_commit':state['head'],'started_utc':started,'finished_utc':now(),'duration_s':time.monotonic()-t,'actual_inner_returncode':rc,'instrumented_binary':str(immutable) if immutable else None,'instrumented_binary_sha256':immutable_sha,'monitor_returncodes':[m.returncode for m in mon],'monitor_forced_stop':forced,'recorded_input_count':len(manifest['files']),'source_unchanged':source_stable,'extra_inputs_unchanged':pins==post,'git_unchanged':before_git==after_git,'strict_durable_contract_pass':strict,'sanitizer_options':{k:env[k] for k in ['ASAN_OPTIONS','UBSAN_OPTIONS','LSAN_OPTIONS']},'errors':errors,'qualification_pass':valid,'source_manifest_limit':'Recorded own build inputs plus explicit generated compiler/link inputs; not a full transitive toolchain/system closure','prebuilt_V8_itself_instrumented':False};write(out/'receipt.json',receipt);print(json.dumps(receipt),flush=True);return 0 if valid else 2
if __name__=='__main__':sys.exit(main())
