#!/usr/bin/env python3
import argparse,hashlib,json,os,pathlib,shutil,subprocess,time
from paired_wpt import monitor,endmonitor

def source_manifest():
 paths=subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '--', 'src', 'CMakeLists.txt', 'tests', 'assets/data', 'tools/generate_embedded.py'],text=True).splitlines()
 files={}
 for name in sorted(set(paths)):
  path=pathlib.Path(name)
  if path.is_file(): files[name]=hashlib.sha256(path.read_bytes()).hexdigest()
 return {'files':files,'sha256':hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()}
def reserve_candidate(iteration, expected_head):
 # Candidate identities are immutable, including failed attempts and tombstones.
 if not iteration.isascii() or not iteration.isdigit() or str(int(iteration)) != iteration or int(iteration) <= 0:
  raise SystemExit('iteration must be a canonical positive decimal integer')
 head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
 if expected_head != head:
  raise SystemExit('expected HEAD does not match the checked-out source')
 out=pathlib.Path('agent/validation/iteration-'+iteration)
 target=pathlib.Path('build/codex-native/lumen-astra-i'+iteration)
 reservation=pathlib.Path('agent/validation/runtime/candidate-reservations')/(iteration+'.json')
 if any(os.path.lexists(path) for path in (out,target,reservation)):
  raise SystemExit('candidate identity already exists or is reserved; choose a new iteration')
 reservation.parent.mkdir(parents=True,exist_ok=True)
 try:
  fd=os.open(reservation,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
 except FileExistsError:
  raise SystemExit('candidate identity was reserved concurrently; choose a new iteration')
 with os.fdopen(fd,'w') as f:
  json.dump({'iteration':iteration,'head':head,'reserved_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'reusable':False},f,indent=2)
 # Exclusive directory creation protects against an old, unguarded concurrent caller.
 out.mkdir(parents=True)
 return out,target,head

p=argparse.ArgumentParser();p.add_argument('--iteration',required=True);p.add_argument('--expected-head',required=True);p.add_argument('--startup',action='store_true');p.add_argument('--jobs',default='6');a=p.parse_args()
out,target,head=reserve_candidate(a.iteration,a.expected_head)
before=hashlib.sha256(subprocess.check_output(['git','diff','--','src','CMakeLists.txt','tests'])).hexdigest()
manifest_before=source_manifest()
json.dump(manifest_before,open(out/'source-manifest-before.json','w'),indent=2)
state={'source_manifest_before':manifest_before['sha256'],'source_diff_before':before,'head':head,'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'pid':os.getpid()}
with (out/'build.log').open('wb') as f:r=subprocess.run(['cmake','--build','build/codex-native','-j',a.jobs],stdout=f,stderr=f)
state['build_exit_code']=r.returncode
if r.returncode==0:
 with open('build/codex-native/lumen','rb') as src,target.open('xb') as dst:
  shutil.copyfileobj(src,dst)
 shutil.copystat('build/codex-native/lumen',target)
 state['binary']=str(target);state['binary_sha256']=hashlib.sha256(target.read_bytes()).hexdigest()
 state['diff_sha256']=hashlib.sha256(subprocess.check_output(['git','diff','--','src','CMakeLists.txt','tests'])).hexdigest()
 if a.startup:
  with (out/'startup.log').open('wb') as f:
   q=subprocess.Popen(['ctest','--test-dir','build/codex-native','-R','^js_performance_startup$','--output-on-failure'],stdout=f,stderr=f);m=monitor(q.pid,out/'startup-resources','performance startup ctest')
   state['startup_exit_code']=q.wait();endmonitor(m)
state['source_diff_after']=hashlib.sha256(subprocess.check_output(['git','diff','--','src','CMakeLists.txt','tests'])).hexdigest()
manifest_after=source_manifest()
json.dump(manifest_after,open(out/'source-manifest-after.json','w'),indent=2)
state['source_manifest_after']=manifest_after['sha256']
state['source_unchanged_during_build']=manifest_after['sha256']==manifest_before['sha256'] and state['source_diff_after']==before
state['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());json.dump(state,open(out/'build-state.json','w'),indent=2);print(json.dumps(state),flush=True)

# Propagate build, startup, and source-provenance failures to the caller.
if state.get("build_exit_code", 1):
 raise SystemExit(state.get("build_exit_code", 1))
if state.get("startup_exit_code", 0):
 raise SystemExit(state["startup_exit_code"])
if not state.get("source_unchanged_during_build", False):
 raise SystemExit(3)
