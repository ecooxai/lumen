#!/usr/bin/env python3
import argparse,hashlib,json,os,pathlib,shutil,subprocess,time
from paired_wpt import monitor,endmonitor

def source_manifest():
 paths=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','--','src','CMakeLists.txt','tests'],text=True).splitlines()
 files={}
 for name in sorted(set(paths)):
  path=pathlib.Path(name)
  if path.is_file(): files[name]=hashlib.sha256(path.read_bytes()).hexdigest()
 return {'files':files,'sha256':hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()}
p=argparse.ArgumentParser();p.add_argument('--iteration',required=True);p.add_argument('--startup',action='store_true');p.add_argument('--jobs',default='6');a=p.parse_args()
out=pathlib.Path('agent/validation/iteration-'+a.iteration);out.mkdir(exist_ok=True,parents=True)
before=hashlib.sha256(subprocess.check_output(['git','diff','--','src','CMakeLists.txt','tests'])).hexdigest()
manifest_before=source_manifest()
json.dump(manifest_before,open(out/'source-manifest-before.json','w'),indent=2)
state={'source_manifest_before':manifest_before['sha256'],'source_diff_before':before,'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'pid':os.getpid()}
with (out/'build.log').open('wb') as f:r=subprocess.run(['cmake','--build','build/codex-native','-j',a.jobs],stdout=f,stderr=f)
state['build_exit_code']=r.returncode
if r.returncode==0:
 target='build/codex-native/lumen-astra-i'+str(int(a.iteration));shutil.copy2('build/codex-native/lumen',target);state['binary']=target;state['binary_sha256']=hashlib.sha256(open(target,'rb').read()).hexdigest()
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
