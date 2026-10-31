#!/usr/bin/env python3
"""Run the existing raster ASan/UBSan test against a frozen candidate source."""
from pathlib import Path
import argparse,subprocess,json,hashlib,datetime,time,sys,os
ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();root=Path.cwd();base=root/'agent/validation/linux-wpt-20261031-session9';build=root/f'agent/validation/iteration-{a.iteration}';out=base/f'raster{a.iteration}-sanitizer';assert not out.exists()
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def gitstate():
 return {'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'source_diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff','--binary','--','src','tests','CMakeLists.txt','tools/gen_css_props.py'])).hexdigest()}
state=json.loads((build/'build-state.json').read_text());manifest=json.loads((build/'source-manifest-before.json').read_text());after=json.loads((build/'source-manifest-after.json').read_text());assert state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'];assert manifest==after;assert all(sha(root/p)==h for p,h in manifest['files'].items());g=gitstate();assert g['head']==state['head'];assert g['source_diff_sha256']==hashlib.sha256(b'').hexdigest();assert sha(root/state['binary'])==state['binary_sha256']
extra=[Path(__file__).resolve(),build/'build-state.json',build/'source-manifest-before.json',build/'source-manifest-after.json',root/'tests/test_raster_layers.py',root/'tests/raster_layers.c',root/'tools/codex_compile.py'];pins={str(p):sha(p) for p in extra};out.mkdir();(out/'pins-before.json').write_text(json.dumps(pins,indent=2)+'\n');cmd=['python3','tools/codex_compile.py','python3','tests/test_raster_layers.py','-v'];(out/'plan.json').write_text(json.dumps({'command':cmd,'candidate':a.iteration,'source_commit':state['head'],'source_manifest_sha256':manifest['sha256'],'extra_inputs':pins,'sanitizers':['address','undefined']},indent=2)+'\n');start=datetime.datetime.now(datetime.timezone.utc).isoformat();t=time.monotonic();env=dict(os.environ);env['TMPDIR']=str(root/'agent/t')
with (out/'test.log').open('x') as log,(out/'cgroup-monitor.log').open('x') as cglog,(out/'tree-monitor.log').open('x') as tlog:
 cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],stdout=cglog,stderr=subprocess.STDOUT)
 p=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,env=env)
 tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label',f'candidate{a.iteration} real-source raster ASan UBSan'],stdout=tlog,stderr=subprocess.STDOUT)
 rc=p.wait()
 try:tm.wait(6)
 except subprocess.TimeoutExpired:tm.terminate();tm.wait(6)
 cg.terminate();cg.wait(6)
post={p:sha(p) for p in pins};stable=all(sha(root/p)==h for p,h in manifest['files'].items());git_after=gitstate();r={'candidate':a.iteration,'started_utc':start,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'duration_s':time.monotonic()-t,'returncode':rc,'monitor_returncodes':[cg.returncode,tm.returncode],'source_unchanged':stable,'extra_inputs_unchanged':pins==post,'git_unchanged':g==git_after,'source_commit':state['head'],'immutable_binary_sha256':state['binary_sha256'],'source_manifest_limit':'Recorded build inputs plus explicit sanitizer files, not a full transitive system-toolchain closure'};(out/'pins-after.json').write_text(json.dumps(post,indent=2)+'\n');(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));print((out/'test.log').read_text()[-3000:]);sys.exit(0 if rc==0 and stable and pins==post and g==git_after and cg.returncode==0 and tm.returncode==0 else 2)
