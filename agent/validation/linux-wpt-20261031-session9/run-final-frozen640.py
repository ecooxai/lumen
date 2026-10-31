#!/usr/bin/env python3
"""Bind sealed compiled source around the unchanged frozen640 runner; retain failures too."""
from pathlib import Path
import argparse,subprocess,json,hashlib,datetime,sys,time

def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def head():return subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();root=Path.cwd();s=Path(__file__).resolve().parent;base=root/'agent/validation/linux-wpt-20261030-session8/qa-frozen640';out=s/f'candidate{a.iteration}-frozen640-source-binding.json';assert not out.exists()
 started=now();t=time.monotonic();phase='preflight';errors=[];state={};source={};observed_before={};observed_after={};pins_before={};pins_after={};source_commit=None;after_head=None;binary_same=False;p=None;code=None;command=None
 try:
  build=root/f'agent/validation/iteration-{a.iteration}';state=json.loads((build/'build-state.json').read_text());source=json.loads((build/'source-manifest-before.json').read_text());assert source==json.loads((build/'source-manifest-after.json').read_text());assert state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'];assert state['source_manifest_before']==state['source_manifest_after']==source['sha256'];source_commit=state['head'];assert head()==source_commit
  binary=root/f'build/codex-native/lumen-astra-i{a.iteration}';assert sha(binary)==state['binary_sha256'];observed_before={p:sha(root/p) for p in source['files']};assert observed_before==source['files']
  runner=base/'run-two-native-v2.py';schedule=base/'two-shard-execution-freeze-v2.json';assert sha(runner)=='9200b0af2c314955267077c456c76cc9b02423d88fed55ad1f87b092587091c6';schedule_sha='7e17aeaf30a0d05982a5d69a11fe0bd062561fe9375b2fcad72fb1eba41d1c32';assert sha(schedule)==schedule_sha
  paths=[Path(__file__),runner,schedule,build/'build-state.json',build/'source-manifest-before.json',build/'source-manifest-after.json',root/'tools/codex_compile.py'];pins_before={str(p):sha(p) for p in paths};command=['python3','tools/codex_compile.py','python3',str(runner),'--iteration',str(a.iteration),'--binary-sha256',state['binary_sha256'],'--source',source_commit,'--schedule-sha256',schedule_sha]
  phase='execution'
  with (s/f'candidate{a.iteration}-frozen640-controller.log').open('x') as f:
   p=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT)
   try:
    (s/f'candidate{a.iteration}-frozen640-source-binding-launch.json').write_text(json.dumps({'started_utc':started,'command':command,'pid':p.pid,'source_commit':source_commit,'binary_sha256':state['binary_sha256'],'source_files_before':observed_before,'pins_before':pins_before,'frozen_schedule_unchanged':True,'maximum_browser_lanes':2,'monitoring':'Unchanged runner cgroup and each actual browser process tree at1second'},indent=2)+'\n')
   finally:code=p.wait()
  phase='postflight';observed_after={p:sha(root/p) for p in source['files']};pins_after={p:sha(p) for p in pins_before};after_head=head();binary_same=sha(binary)==state['binary_sha256']
 except Exception as e:errors.append({'phase':phase,'error':type(e).__name__+': '+str(e)})
 stable=bool(source) and source.get('files')==observed_before==observed_after;git_same=source_commit is not None and after_head==source_commit;metadata_same=bool(pins_before) and pins_before==pins_after;valid=not errors and code==0 and stable and git_same and binary_same and metadata_same
 r={'candidate':a.iteration,'started_utc':started,'finished_utc':now(),'duration_s':time.monotonic()-t,'actual_outer_runner_returncode':code,'actual_owned_controller_pid':p.pid if p else None,'command':command,'source_commit':source_commit,'git_head_after':after_head,'git_unchanged':git_same,'binary_sha256':state.get('binary_sha256'),'binary_unchanged':binary_same,'recorded_build_input_count':len(source.get('files',{})),'source_files_before':observed_before,'source_files_after':observed_after,'compiled_source_correspondence_valid':stable,'pins_before':pins_before,'pins_after':pins_after,'wrapper_and_build_metadata_unchanged':metadata_same,'errors':errors,'execution_and_source_binding_valid':valid,'scope_limit':'Recorded build input set, not full transitive dependency closure. Execution/provenance only; raw WPT outcomes and prior-pass preservation require separate audits.','unchanged_frozen640_controller_receipt':str(base/f'lumen{a.iteration}-controller-receipt.json')}
 out.write_text(json.dumps(r,indent=2)+'\n');digest=sha(out);out.with_suffix(out.suffix+'.sha256').write_text(digest+'  '+out.name+'\n');print(json.dumps({**{k:r[k] for k in ['candidate','duration_s','actual_outer_runner_returncode','git_unchanged','binary_unchanged','recorded_build_input_count','compiled_source_correspondence_valid','execution_and_source_binding_valid','errors']},'receipt_sha256':digest}));return 0 if valid else 2
if __name__=='__main__':sys.exit(main())
