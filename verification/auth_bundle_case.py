#!/usr/bin/env python3
"""Read-only private-site observations through a normal owned app bundle.

Native stdout/stderr flow through a 0600 FIFO into the typed redactor. They are
never written as a regular log file. Private comparisons never capture a window.
An explicit public-fresh mode creates a new empty profile before any capture.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import signal
import statistics
import subprocess
import tempfile
import threading
import time
from auth_case import sanitize, URLS
from bundle_case import matching_pids
from native_case import descendants
from resource_monitor import sample_processes, add_cpu_deltas


class RedactedPipe:
    def __init__(self, root, evidence):
        self.directory = tempfile.TemporaryDirectory(prefix='private-native-pipe-',dir=root)
        self.path = Path(self.directory.name)/'stream.fifo'
        os.mkfifo(self.path,0o600)
        self.keeper = os.open(self.path,os.O_RDWR)
        self.reader = self.path.open('rb',buffering=0)
        self.evidence = evidence
        self.thread = threading.Thread(target=self.drain,args=(self.reader,),daemon=True)
        self.thread.start()

    def drain(self, pipe):
        # Entire overlong lines are discarded; a valid-looking suffix cannot
        # be promoted into evidence after a truncated raw line.
        continued = False
        for raw in iter(lambda:pipe.readline(8193),b''):
            complete = raw.endswith(b'\n')
            if not continued and complete and len(raw)<=2048:
                record = sanitize(raw.decode('utf8',errors='replace').strip())
                if record is not None and len(self.evidence)<20:self.evidence.append(record)
            continued = not complete
        pipe.close()

    def close(self):
        os.close(self.keeper)
        self.thread.join(timeout=3)
        if self.thread.is_alive():
            raise RuntimeError('Private FIFO still has a writer after owned process teardown')
        self.directory.cleanup()


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--profile')
    ap.add_argument('--public-fresh',action='store_true',help='Create a new empty profile and allow one public-window capture')
    ap.add_argument('--site',choices=URLS,required=True)
    ap.add_argument('--metadata',default='out/native-verification/mac-dev-bundle-stable.json')
    ap.add_argument('--duration',type=float,default=48)
    ap.add_argument('--tag',default='stable-bundle')
    args=ap.parse_args()
    import re
    if not re.fullmatch('[a-z0-9-]{1,32}',args.tag):raise SystemExit('Invalid run tag')
    os.umask(0o077)
    root=Path.cwd().resolve();out=root/'out/verification';out.mkdir(exist_ok=True,parents=True)
    if args.public_fresh:
        if args.profile:raise SystemExit('Public capture cannot use any supplied existing profile')
        profile=Path(tempfile.mkdtemp(prefix='public-native-empty-',dir=out))
    else:
        if not args.profile:raise SystemExit('Private comparison profile is required')
        profile=Path(args.profile).resolve();private_root=(Path.home()/'Library/Application Support/lumen/Lumen-session-tests').resolve()
        if not profile.is_dir() or profile.parent!=private_root or not profile.name.startswith('GPT6-AstraPro-'):
            raise SystemExit('Profile must be the existing authorized isolated comparison profile')
    manifest=json.loads(Path(args.metadata).read_text());app=Path(manifest['app']).resolve();exe=Path(manifest['executable']).resolve()
    if not app.is_relative_to(root/'out') or exe!=app/'Contents/MacOS/lumen':raise SystemExit('Unexpected development bundle path')
    source_hash=hashlib.sha256((root/'build/lumen').read_bytes()).hexdigest()
    if source_hash!=manifest['binaryBeforeBundleSigningSha256']:raise SystemExit('Private bundle is stale relative to current build')
    if matching_pids(exe):raise SystemExit('Owned development executable is already running')
    evidence=[];pipe=RedactedPipe(out,evidence);prefix=out/(('public' if args.public_fresh else 'private')+'-lumen-'+args.site+'-'+args.tag)
    envs={'LUMEN_PROFILE_DIR':str(profile),'LUMEN_PRE_FILE':str(root/'verification/auth-status-probe.js'),'LUMEN_CONSOLE':'1','LUMEN_NO_SIDEBAR':'1','LUMEN_WINDOW':'1024x768','LUMEN_MAC_VISIBILITY':'0'}
    command=['/usr/bin/open','-n',str(app),'--stdout',str(pipe.path),'--stderr',str(pipe.path)]
    for key,value in envs.items():command+=['--env',key+'='+value]
    command+=['--args',URLS[args.site]]
    launch_env={key:value for key,value in os.environ.items() if not key.startswith('LUMEN_')}
    launcher=subprocess.Popen(command,env=launch_env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    launcher_reader=threading.Thread(target=pipe.drain,args=(launcher.stdout,),daemon=True);launcher_reader.start()
    jobs=[];visibility_paths={};pid=None;observed=False;failure=None
    def inspect(label,operation):
        path=Path(str(prefix)+'.visibility-'+label+'.json');handle=path.open('w')
        job=subprocess.Popen([str(out/'capture-window'),operation,str(pid)],stdout=handle,stderr=subprocess.STDOUT)
        jobs.append((job,handle));visibility_paths[label]=path
    started_utc=dt.datetime.now(dt.timezone.utc).isoformat();start=time.monotonic();due=start;previous={};prior=None;samples=[];steady_visibility=False;capture_requested=False;capture_path=Path(str(prefix)+'.public-window.png')
    (out/'monitor-scenario.txt').write_text('private-'+args.site+'-normal-bundle-navigation\n')
    try:
        while time.monotonic()-start<args.duration:
            processes=sample_processes();now=time.monotonic();add_cpu_deltas(processes,previous,now-prior if prior else 0)
            previous={p['pid']:p for p in processes};prior=now
            if pid is None:
                found=matching_pids(exe)
                if len(found)>1:failure='ambiguous-owned-process';break
                if found:pid=found[0];inspect('activation','--activate-owned')
                elif now-start>10:failure='normal-launch-produced-no-owned-process';break
            owned=descendants(processes,pid) if pid else []
            cpu=[p['cpu_interval_percent'] for p in owned if p['cpu_interval_percent'] is not None]
            compilers=sum(1 for p in processes if p['name'] in ('clang','clang++','ld','swift-frontend','rustc','cargo','ninja','cc1') and (p.get('cpu_interval_percent') or p['cpu_percent'])>10)
            samples.append({'elapsed_s':round(now-start,3),'cpu_interval_percent':sum(cpu) if cpu else None,'rss_mib':sum(p['rss_kib'] for p in owned)/1024,'active_compilers':compilers,'owned_process_observed':bool(owned)})
            if owned:observed=True
            elif observed:failure='owned-process-exited-before-deadline';break
            if args.public_fresh and pid and not capture_requested and now-start>=32:
                handle=Path(str(prefix)+'.public-window-capture.json').open('w')
                job=subprocess.Popen([str(out/'capture-window'),str(pid),str(capture_path)],stdout=handle,stderr=subprocess.STDOUT);jobs.append((job,handle));capture_requested=True
            if pid and not steady_visibility and now-start>=35:inspect('steady','--inspect-owned');steady_visibility=True
            due+=1;time.sleep(max(0,due-time.monotonic()))
    finally:
        if pid and pid in matching_pids(exe):
            os.kill(pid,signal.SIGTERM);until=time.monotonic()+4
            while pid in matching_pids(exe) and time.monotonic()<until:time.sleep(.1)
            if pid in matching_pids(exe):os.kill(pid,signal.SIGKILL)
        if launcher.poll() is None:launcher.terminate();launcher.wait(timeout=3)
        launcher_reader.join(timeout=3)
        for job,handle in jobs:
            try:job.wait(timeout=3)
            except subprocess.TimeoutExpired:job.terminate();job.wait(timeout=3)
            handle.close()
        pipe.close()
        (out/'monitor-scenario.txt').write_text('development-uncontrolled\n')
    visibility={}
    for label,path in visibility_paths.items():
        try:visibility[label]=json.loads(path.read_text())
        except (OSError,ValueError):visibility[label]={'unavailable':True}
    steady=[row['cpu_interval_percent'] for row in samples if row['elapsed_s']>=30 and row['cpu_interval_percent'] is not None]
    result={'schema':1,'site':args.site,'engine':'lumen','run_tag':args.tag,'launcher':'normal LaunchServices open -n','actual_browser_pid':pid,'launcher_exit':launcher.returncode,'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'source_binary_sha256':source_hash,'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'headless':False,'window_visibility':visibility,'started_utc':started_utc,'tested_utc':dt.datetime.now(dt.timezone.utc).isoformat(),'scope':'fresh signed-out public-page rendering and typed readiness observations' if args.public_fresh else 'private profile read-only UI readiness; DOM indicators alone are not provider authentication proof','raw_logs_saved':False,'transport':'0600 FIFO redacted in memory','screenshots_taken':args.public_fresh and capture_path.is_file(),'capture_path':str(capture_path) if args.public_fresh and capture_path.is_file() else None,'profile_policy':'fresh empty isolated profile, no imported sessions' if args.public_fresh else 'existing isolated matched-cookie comparison profile','account_actions_performed':False,'launch_failure':failure,'sample_count':len(samples),'samples_with_active_compilers':sum(bool(row['active_compilers']) for row in samples),'steady_cpu_mean_percent':statistics.mean(steady) if steady else None,'rss_peak_mib':max((row['rss_mib'] for row in samples),default=None),'evidence':evidence,'samples':samples}
    Path(str(prefix)+'-status.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:value for key,value in result.items() if key!='samples'},indent=2))


if __name__=='__main__':main()
