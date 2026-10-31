#!/usr/bin/env python3
"""Accepted owned demo through the unchanged public marker-only fixture API. No capture or WPT claim."""
from pathlib import Path
import datetime,hashlib,json,os,re,signal,subprocess,sys,time
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=ROOT/'agent/validation/linux-wpt-20261031-session9/showcase'
SOURCE='206bb89d161d91278c56899a3f664a99480374f8';BINARY='253f563c6529af4d8bab38b12373b51aec9941eb475fa16cae50093ff4e2b282';FIXTURE='9b330a84b469beb564e615c7aeb18ce3bc93e1b55804f6e49aa5badd1fe11236';MANIFEST='bf2e5ca98d4234de88bacad4c8075c6c4f00ea633e26b2f6d472412472b7e5d2'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def require(v,m):
 if not v:raise AssertionError(m)
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def dump(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def messages(raw,prefix):return [json.loads(l.split(prefix,1)[1]) for l in raw.splitlines() if prefix in l]
def main():
 require(Path.cwd()==ROOT,'Project cwd required');out=BASE/'native234';require(not out.exists(),'Fresh output required')
 fixture=BASE/'gpt-6-astra-pro-builtin-rubywebterm-lumen-live-demo.html';manifest=BASE/'lumen-live-demo-manifest-final.json';binary=ROOT/'build/codex-native/lumen-astra-i234';build=ROOT/'agent/validation/iteration-234';state=read(build/'build-state.json');source=read(build/'source-manifest-after.json')
 require(sha(fixture)==FIXTURE and sha(manifest)==MANIFEST and sha(binary)==BINARY,'Pinned demo/binary changed');require(state['head']==SOURCE and state['binary_sha256']==BINARY and state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'],'Successful accepted build required');require(source==read(build/'source-manifest-before.json') and len(source['files'])==650 and all(sha(ROOT/p)==h for p,h in source['files'].items()),'Source inputs differ');head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip();require(head==SOURCE,'Keep source/Git frozen for demo validation')
 runner=ROOT/'agent/validation/run_guarded_fixture.py';paths=[Path(__file__).resolve(),fixture,manifest,binary,runner,ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py']+[build/n for n in ['build-state.json','source-manifest-before.json','source-manifest-after.json','build.log']];pins={str(p):sha(p) for p in paths};out.mkdir();dump(out/'pins-before.json',pins)
 env=dict(os.environ)
 for k in ['JSRUN_PRE','JSRUN_POST','LUMEN_FRAME_PRE','LUMEN_PRE','LUMEN_PRE_FILE','LUMEN_WPT_CTRL','LUMEN_WPT_PRE','LUMEN_SHOT','LUMEN_SHOT_MS']:env.pop(k,None)
 env.update({'TMPDIR':str(ROOT/'agent/t'),'LUMEN_ICU_DATA':str(ROOT/'build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat'),'LUMEN_CAPTURE_GUARD_LIBRARY':str(ROOT/'agent/validation/linux-wpt-20261028/capture-refusal/capture_guard.so'),'LUMEN_CAPTURE_GUARD_SHA256':'45643caa22bb659bc7b431acfc7b8af04d68f971c3610f4d56dcb2847d808900','LUMEN_CAPTURE_GUARD_AUDIT_DIR':str(out/'guard-audit')})
 command=['python3',str(ROOT/'tools/codex_compile.py'),'python3',str(runner),'--binary',str(binary),'--fixture',str(fixture),'--marker','lumen-live-demo','--output',str(out/'browser'),'--timeout','30','--cleanup-timeout','10'];started=now();start=time.monotonic();dump(out/'launch.json',{'candidate':234,'source_commit':SOURCE,'binary_sha256':BINARY,'fixture_sha256':FIXTURE,'manifest_sha256':MANIFEST,'started_utc':started,'command':command,'maximum_browser_lanes':1,'resource_interval_seconds':1,'not_wpt':True,'capture_requested':False,'design_canvas_from_manifest':read(manifest)['viewport'],'supported_fixture_viewport':'Adapter default; design canvas is not asserted as actual viewport. No screenshot or visual validation is claimed.'})
 handles=[];monitors=[];p=None;code=None;forced=[];errors=[];raw='';events=[];measurements=[];cleanup_ok=False;native_exit=None;profile_removed=False
 try:
  h=(out/'cgroup-monitor.log').open('w');handles.append(h);monitors.append(subprocess.Popen(['python3',str(ROOT/'agent/progress/cgroup_monitor.py'),'--output',str(out/'cgroup.csv')],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT))
  h=(out/'controller.log').open('w');handles.append(h);p=subprocess.Popen(command,cwd=ROOT,env=env,stdout=h,stderr=subprocess.STDOUT,start_new_session=True)
  h=(out/'tree-monitor.log').open('w');handles.append(h);monitors.append(subprocess.Popen(['python3',str(ROOT/'agent/progress/monitor.py'),'--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label','accepted234 owned marker-only demonstration'],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT))
  code=p.wait(timeout=75)
 except Exception as e:errors.append(repr(e))
 finally:
  if p is not None and p.poll() is None:
   os.killpg(p.pid,signal.SIGTERM);forced.append('owned controller group SIGTERM')
   try:p.wait(10)
   except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);forced.append('owned controller group SIGKILL');p.wait(5)
  if p is not None:code=p.returncode
  for i,m in enumerate(monitors):
   if i==1 and m.poll() is None:
    try:m.wait(6)
    except subprocess.TimeoutExpired:forced.append('tree sampler required stop')
   if m.poll() is None:m.terminate()
   try:m.wait(6)
   except subprocess.TimeoutExpired:m.kill();m.wait(3);forced.append('sampler kill')
  for h in handles:h.close()
 try:
  raw=(out/'browser/browser.log').read_text(errors='replace');events=messages(raw,'DEMO_EVENT ');measurements=messages(raw,'DEMO_MEASUREMENTS ')
  require(re.findall(r'RESULTS lumen-live-demo:(PASS|FAIL)',raw)==['PASS'] and 'DEMO_ERROR' not in raw,'Demo did not complete cleanly');require(len(events)==3 and [x['type'] for x in events]==['DOMContentLoaded','load','pageshow'],'Genuine lifecycle observations incomplete')
  require(len(measurements)==1,'Exactly one settled measurement required');v=measurements[0]
  require(v['containment']['width']==76 and v['containment']['height']==40 and v['containment']['contain']=='size' and v['containment']['writingMode']=='horizontal-tb','Unexpected real containment reading')
  require(v['scroll']['left']==80 and v['scroll']['top']==90 and v['scroll']['contentVisibility']=='visible','Retained scroll reading differs')
  require(v['visibility']['default'] is True and v['visibility']['opacity'] is False and v['visibility']['visibility'] is True and v['visibility']['ancestorOpacity']=='0' and v['visibility']['ownVisibility']=='visible','Actual visibility reading differs')
  for e in events:require(e['target']=='Document' and e['trusted'] is True and e['composed'] is False,'Genuine lifecycle metadata differs')
  require(events[0]['currentTarget']=='Document' and events[1]['currentTarget']==events[2]['currentTarget']=='Window' and events[2]['persisted'] is False,'Lifecycle current-target/persisted metadata differs')
  c=read(out/'browser/guard-cleanup.json');require(c['sources_unchanged'] is True and c['guard_events_complete_and_linked'] is True and c['runner_exception'] is None and not c['remaining_cleanup_threads'] and not c['remaining_recorded_pids'],'Guard cleanup incomplete');ap=Path(c['guard_audit']);require(ap.resolve()==(out/'guard-audit/launches.jsonl').resolve(),'Unexpected audit path');ae=[json.loads(x) for x in ap.read_text().splitlines() if x.strip()];require([x['event'] for x in ae]==['started_at_private_gate','constructor_verified_and_acknowledged','author_url_released','exited'],'Guard lifecycle differs');ex=ae[-1];native_exit=ex['exit_code'];profile_removed=ex['profile_removed'];cleanup_ok=native_exit==0 and profile_removed is True;require(cleanup_ok,'Native actual exit/profile cleanup failed')
  for f in ['resources.summary.json','cgroup.summary.json','browser/tree-resources.summary.json']:
   q=read(out/f);require(q['interval_seconds']==1 and q['samples']>=1,'Missing one-second sampler evidence')
 except Exception as e:errors.append(repr(e))
 after={p:sha(p) for p in pins};dump(out/'pins-after.json',after);stable=all(sha(ROOT/p)==h for p,h in source['files'].items());head_after=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip();ok=code==0 and not forced and not errors and cleanup_ok and pins==after and stable and head==head_after and len(monitors)==2 and all(m.returncode==0 for m in monitors)
 dump(out/'observations.json',{'events':events,'measurements':measurements,'not_WPT':True,'no_visual_capture':True});receipt={'candidate':234,'source_commit':SOURCE,'binary_sha256':BINARY,'fixture_sha256':FIXTURE,'manifest_sha256':MANIFEST,'started_utc':started,'finished_utc':now(),'duration_s':time.monotonic()-start,'actual_controller_returncode':code,'actual_native_exit_code':native_exit,'profile_removed':profile_removed,'monitor_returncodes':[m.returncode for m in monitors],'forced_cleanup':forced,'pins_unchanged':pins==after,'source_unchanged':stable,'git_unchanged':head==head_after,'cleanup_ok':cleanup_ok,'settled_marker_and_real_readings_valid':ok,'not_WPT':True,'no_screenshot_or_visual_validation':True,'errors':errors};dump(out/'receipt.json',receipt);print(json.dumps(receipt));sys.exit(0 if ok else 2)
if __name__=='__main__':main()
