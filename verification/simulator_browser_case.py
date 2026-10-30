#!/usr/bin/env python3
"""Actual installed simulator app with fresh profile and exact process identity."""
import argparse,datetime as dt,hashlib,json,os
from pathlib import Path
import plistlib,signal,statistics,subprocess,tempfile,time
from bundle_case import matching_pids,structured_evidence
from native_case import descendants
from resource_monitor import add_cpu_deltas,sample_processes
from simulator_idle_case import DEVICES

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--device',choices=DEVICES,required=True);ap.add_argument('--name',required=True);ap.add_argument('--url',required=True);ap.add_argument('--duration',type=float,default=35);ap.add_argument('--probe');ap.add_argument('--capture-markers',action='store_true');args=ap.parse_args()
    if not args.url.startswith(('http://127.0.0.1:','http://localhost:')):raise SystemExit('Only the neutral task-owned loopback lab is accepted')
    root=Path.cwd().resolve();out=root/'out/verification';out.mkdir(exist_ok=True,parents=True);prefix=out/args.name
    udid,label=DEVICES[args.device];bundle_id='com.ecooxai.lumen'
    def container(kind):return Path(subprocess.check_output(['xcrun','simctl','get_app_container',udid,bundle_id,kind],text=True).strip()).resolve()
    app,data=container('app'),container('data');exe=app/plistlib.loads((app/'Info.plist').read_bytes())['CFBundleExecutable']
    binary_hash=hashlib.sha256(exe.read_bytes()).hexdigest();built=root/'build-sim/Lumen.app'/exe.name
    if not built.is_file() or hashlib.sha256(built.read_bytes()).hexdigest()!=binary_hash:raise SystemExit('Installed simulator binary does not match the current build-sim artifact')
    if matching_pids(exe):raise SystemExit('The target simulator Lume is already running; ownership would be ambiguous')
    profile_root=data/'tmp/lume-verification';profile_root.mkdir(exist_ok=True,parents=True);profile=Path(tempfile.mkdtemp(prefix=args.name+'-',dir=profile_root));profile.chmod(0o700)
    env={key:value for key,value in os.environ.items() if not key.startswith('SIMCTL_CHILD_LUMEN_')}
    env.update(SIMCTL_CHILD_LUMEN_PROFILE_DIR=str(profile),SIMCTL_CHILD_LUMEN_NO_REPLAYKIT='1',SIMCTL_CHILD_LUMEN_CONSOLE='1')
    if args.probe:
        probe=profile/'preload.js';probe.write_text(Path(args.probe).read_text());env['SIMCTL_CHILD_LUMEN_PRE_FILE']=str(probe)
    log_path=Path(str(prefix)+'.log');log=log_path.open('w');command=['xcrun','simctl','launch','--console',udid,bundle_id,args.url]
    started=dt.datetime.now(dt.timezone.utc).isoformat();start=time.monotonic();due=start;previous={};prior=None;pid=None;observed=False;samples=[];captures={};failure=None
    (out/'monitor-scenario.txt').write_text(args.name+'\n');launcher=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    try:
        while time.monotonic()-start<args.duration:
            rows=sample_processes();now=time.monotonic();add_cpu_deltas(rows,previous,now-prior if prior else 0);previous={row['pid']:row for row in rows};prior=now
            if pid is None:
                found=matching_pids(exe)
                if len(found)>1:failure='ambiguous-owned-process';break
                if found:pid=found[0]
                elif now-start>10:failure='simctl-launch-produced-no-owned-process';break
            selected=descendants(rows,pid) if pid else [];cpu=[row['cpu_interval_percent'] for row in selected if row['cpu_interval_percent'] is not None]
            compilers=sum(1 for row in rows if row['name'] in ('clang','clang++','ld','swift-frontend','rustc','cargo','ninja','cc1') and (row.get('cpu_interval_percent') or row['cpu_percent'])>10)
            samples.append({'elapsed_s':round(now-start,3),'cpu_interval_percent':sum(cpu) if cpu else None,'rss_mib':sum(row['rss_kib'] for row in selected)/1024,'active_compilers':compilers,'owned_process_observed':bool(selected)})
            if selected:observed=True
            elif observed:failure='owned-process-exited-before-deadline';break
            if args.capture_markers and pid:
                content=log_path.read_text(errors='replace')
                for stage in ['overview','gpu','network']:
                    if stage not in captures and 'LUME_CAPTURE_REQUEST '+stage in content:
                        captures[stage]=subprocess.Popen(['xcrun','simctl','io',udid,'screenshot',str(prefix)+'.'+stage+'.png'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            due+=1;time.sleep(max(0,due-time.monotonic()))
    finally:
        for capture in captures.values():
            try:capture.wait(timeout=3)
            except subprocess.TimeoutExpired:capture.terminate();capture.wait(timeout=3)
        if pid and pid in matching_pids(exe):subprocess.run(['xcrun','simctl','terminate',udid,bundle_id],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=10,check=False)
        if launcher.poll() is None:
            os.killpg(launcher.pid,signal.SIGTERM)
            try:launcher.wait(timeout=3)
            except subprocess.TimeoutExpired:os.killpg(launcher.pid,signal.SIGKILL);launcher.wait()
        log.close();(out/'monitor-scenario.txt').write_text('development-uncontrolled\n')
    records=structured_evidence(log_path.read_text(errors='replace').splitlines(),'LUME_LAB_EVIDENCE ')
    results={row['payload']['id']:row['payload'] for row in records if row.get('kind')=='result' and 'id' in row.get('payload',{})}
    stops=[row.get('payload',{}) for row in records if row.get('kind')=='post-stop'];cleanup=bool(stops) and all(stops[-1].get('resources',{}).get(key)==0 for key in ['activeScopes','registeredCleanups','activeMediaTracks'])
    summary={'name':args.name,'engine':'lumen','platform':label,'device_udid':udid,'process_pid':pid,'binary_fingerprint':{'sha256':binary_hash},'installedMatchesCurrentBuildSim':True,'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'started_utc':started,'profile_policy':'fresh isolated empty simulator profile','sample_count':sum(row['owned_process_observed'] for row in samples),'failure':failure,'samples_with_active_compilers':sum(bool(row['active_compilers']) for row in samples),'rss_peak_mib':max((row['rss_mib'] for row in samples),default=0),'cpu_peak_percent':max((row['cpu_interval_percent'] for row in samples if row['cpu_interval_percent'] is not None),default=None),'lab_results':results,'lab_cleanup_verified':cleanup,'post_stop_evidence':stops[-1] if stops else None,'scope':'Actual simulator browser process; not a physical-device, iOS-on-Mac, or comparative-performance claim','owned_simulator_captures':{stage:{'exit_code':job.returncode,'path':str(prefix)+'.'+stage+'.png'} for stage,job in captures.items()}}
    Path(str(prefix)+'.json').write_text(json.dumps(dict(summary,samples=samples),indent=2));print(json.dumps(summary,indent=2))
    return 1 if failure else 0

if __name__=='__main__':raise SystemExit(main())
