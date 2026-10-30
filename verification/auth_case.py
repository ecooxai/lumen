#!/usr/bin/env python3
"""Private-profile navigation with allowlisted, secret-free evidence only.

Raw browser stdout/stderr is discarded in memory. Private comparisons never
capture screenshots. Public-fresh mode captures only its new empty-profile window.
No cookie read, account action, submission, or full URL/DOM dump is performed. Indicators
are UI observations and must not be promoted to confirmed provider auth alone.
"""
import argparse
import datetime as dt
import hashlib
import re
import json
import os
from pathlib import Path
import signal
import statistics
import subprocess
import threading
import tempfile
import time
from native_case import descendants
from resource_monitor import sample_processes, add_cpu_deltas

URLS = {'google':'https://www.google.com/', 'chatgpt':'https://chatgpt.com/', 'youtube':'https://www.youtube.com/', 'x':'https://x.com/'}
BOOL_FIELDS = ('uiAccountIndicator','uiSignInIndicator','composerPresent','composerEditable','videoPresent','videoReady','playbackProgressed','pageHasContent','documentHidden')

def sanitize(line):
    marker = 'LUME_AUTH_STATUS '
    if line.startswith('[js log] '):
        line = line[len('[js log] '):]
    if not line.startswith(marker) or len(line) > 2000:
        return None
    try:
        value = json.loads(line[len(marker):])
    except (ValueError, TypeError):
        return None
    if not isinstance(value,dict) or value.get('schema') != 1 or value.get('site') not in (*URLS,'other'):
        return None
    if any(type(value.get(k)) is not bool for k in BOOL_FIELDS):
        return None
    if value.get('providerLoggedInFlag') is not None and type(value.get('providerLoggedInFlag')) is not bool:
        return None
    if value.get('readyState') not in ('loading','interactive','complete') or type(value.get('elapsedMs')) is not int or not 0 <= value['elapsedMs'] <= 600000:
        return None
    clean={k:value[k] for k in ('schema','site','elapsedMs','readyState','providerLoggedInFlag',*BOOL_FIELDS)}
    for name in ('viewportWidth','viewportHeight'):
        if name in value:
            if type(value[name]) is not int or not 0 <= value[name] <= 20000:return None
            clean[name]=value[name]
    if 'devicePixelRatio' in value:
        if type(value['devicePixelRatio']) not in (int,float) or not 0.1 <= value['devicePixelRatio'] <= 8:return None
        clean['devicePixelRatio']=value['devicePixelRatio']
    return clean


def sanitize_probe_diagnostic(line):
    marker='LUME_AUTH_PROBE '
    if not line.startswith(marker) or len(line)>1024:return None
    try:value=json.loads(line[len(marker):])
    except (ValueError,TypeError):return None
    if not isinstance(value,dict) or value.get('schema')!=1 or value.get('stage') not in {'connect','page-enabled','observer-registered','navigated','sampled'}:return None
    clean={'schema':1,'stage':value['stage']}
    for key in ['pageEnabled','observerPresent','evaluationException','helperFailed']:
        if key in value:
            if type(value[key]) is not bool:return None
            clean[key]=value[key]
    if 'sampleDelayMs' in value:
        if value['sampleDelayMs'] not in [5000,15000,30000,45000]:return None
        clean['sampleDelayMs']=value['sampleDelayMs']
    return clean

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--profile')
    parser.add_argument('--public-fresh',action='store_true',help='Create a new empty profile and allow one public-window capture')
    parser.add_argument('--site',choices=URLS,required=True)
    parser.add_argument('--engine',choices=('lumen','chrome'),default='lumen')
    parser.add_argument('--duration',type=float,default=48)
    parser.add_argument('--headful',action='store_true')
    parser.add_argument('--activate-owned',action='store_true')
    parser.add_argument('--binary')
    parser.add_argument('--tag',default='first')
    args=parser.parse_args()
    if not re.fullmatch('[a-z0-9-]{1,32}',args.tag):raise SystemExit('Invalid run tag')
    os.umask(0o077)
    out=Path('out/verification');out.mkdir(parents=True,exist_ok=True)
    if args.public_fresh:
        if args.profile:raise SystemExit('Public capture cannot use any supplied existing profile')
        profile=Path(tempfile.mkdtemp(prefix='public-'+args.engine+'-empty-',dir=out.resolve()))
    else:
        if not args.profile:raise SystemExit('Private comparison profile is required')
        profile=Path(args.profile).resolve()
        if not profile.is_dir():raise SystemExit('Private profile directory is missing')
        private_root=(Path.home()/'Library/Application Support/lumen/Lumen-session-tests').resolve()
        if profile.parent!=private_root or not profile.name.startswith('GPT6-AstraPro-'):
            raise SystemExit('Profile is outside the authorized isolated test-profile directory')
    if args.engine=='chrome':
        marker=profile/'LumePrivateProfile.json'
        if not args.public_fresh and (not marker.is_file() or json.loads(marker.read_text()).get('kind')!='scoped-cookie-comparison'):
            raise SystemExit('Private Chrome profile marker missing or mismatched')
        lock=profile/'SingletonLock'
        if lock.exists() or lock.is_symlink():
            raise SystemExit('Private Chrome profile is already locked; no browser launched')
        (profile/'DevToolsActivePort').unlink(missing_ok=True)
    out=Path('out/verification');out.mkdir(parents=True,exist_ok=True)
    prefix=out/(('public' if args.public_fresh else 'private')+'-'+args.engine+'-'+args.site+'-'+args.tag)
    destination=Path(str(prefix)+'-status.json')
    binary=args.binary or ('./build/lumen' if args.engine=='lumen' else '../../../../Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
    fingerprint=hashlib.sha256(Path(binary).read_bytes()).hexdigest()
    source_revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
    env=os.environ.copy()
    for key in ('LUMEN_SHOT','LUMEN_SHOT_MS','LUMEN_TYPE','LUMEN_TAB_TOUR'):
        env.pop(key,None)
    env.update(LUMEN_PROFILE_DIR=str(profile),LUMEN_PRE_FILE='verification/auth-status-probe.js',LUMEN_CONSOLE='1',LUMEN_NO_SIDEBAR='1',LUMEN_WINDOW='1024x768')
    evidence=[];driver_diagnostics=[]
    def drain(pipe):
        continued=False
        for raw in iter(lambda:pipe.readline(8193),b''):
            # Cap bounded retained data; raw process logs are never stored.
            complete=raw.endswith(b'\n')
            if not continued and complete and len(raw)<=2048:
                line=raw.decode('utf8',errors='replace').strip();item=sanitize(line)
                if item is not None and len(evidence)<20:evidence.append(item)
                diagnostic=sanitize_probe_diagnostic(line)
                if diagnostic is not None and len(driver_diagnostics)<20:driver_diagnostics.append(diagnostic)
            continued=not complete
        pipe.close()
    (out/'monitor-scenario.txt').write_text('private-'+args.site+'-navigation\n')
    command=[binary,URLS[args.site]] if args.engine=='lumen' else [binary]+([] if args.headful else ['--headless=new'])+['--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync']+([] if args.public_fresh else ['--restore-last-session'])+['--remote-debugging-port=0','--user-data-dir='+str(profile),'--window-size=1024,768','about:blank']
    proc=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
    thread=threading.Thread(target=drain,args=(proc.stdout,),daemon=True);thread.start()
    visibility_jobs=[];visibility_paths={}
    def inspect(label,operation):
        path=Path(str(prefix)+'.visibility-'+label+'.json');handle=path.open('w')
        job=subprocess.Popen(['out/verification/capture-window',operation,str(proc.pid)],stdout=handle,stderr=subprocess.STDOUT)
        visibility_jobs.append((job,handle));visibility_paths[label]=path
    if args.activate_owned:inspect('activation','--activate-owned')
    driver=None;driver_thread=None
    if args.engine=='chrome':
        driver=subprocess.Popen(['node','verification/private_chrome_probe.mjs',str(profile),args.site,'headful' if args.headful else 'headless'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
        driver_thread=threading.Thread(target=drain,args=(driver.stdout,),daemon=True);driver_thread.start()
    start=time.monotonic();due=start;previous={};prior=None;samples=[];steady_visibility=False;capture_requested=False;capture_path=Path(str(prefix)+'.public-window.png')
    try:
        while proc.poll() is None and time.monotonic()-start<args.duration:
            processes=sample_processes();now=time.monotonic()
            add_cpu_deltas(processes,previous,now-prior if prior else 0)
            previous={p['pid']:p for p in processes};prior=now
            owned=descendants(processes,proc.pid)
            cpu=[p['cpu_interval_percent'] for p in owned if p['cpu_interval_percent'] is not None]
            compilers=sum(1 for p in processes if p['name'] in ('clang','clang++','ld','swift-frontend','rustc','cargo','ninja','cc1') and (p.get('cpu_interval_percent') or p['cpu_percent'])>10)
            samples.append({'elapsed_s':round(now-start,3),'cpu_interval_percent':sum(cpu) if cpu else None,'rss_mib':sum(p['rss_kib'] for p in owned)/1024,'active_compilers':compilers})
            if args.public_fresh and not capture_requested and now-start>=32:
                handle=Path(str(prefix)+'.public-window-capture.json').open('w')
                job=subprocess.Popen(['out/verification/capture-window',str(proc.pid),str(capture_path)],stdout=handle,stderr=subprocess.STDOUT);visibility_jobs.append((job,handle));capture_requested=True
            if args.activate_owned and not steady_visibility and now-start>=35:
                inspect('steady','--inspect-owned');steady_visibility=True
            due+=1;time.sleep(max(0,due-time.monotonic()))
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid,signal.SIGTERM)
            try:proc.wait(timeout=4)
            except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
        thread.join(timeout=3)
        if driver is not None and driver.poll() is None:
            os.killpg(driver.pid,signal.SIGTERM);driver.wait(timeout=3)
        if driver_thread is not None:driver_thread.join(timeout=3)
        for job,handle in visibility_jobs:
            try:job.wait(timeout=3)
            except subprocess.TimeoutExpired:job.terminate();job.wait(timeout=3)
            handle.close()
        (out/'monitor-scenario.txt').write_text('development-uncontrolled\n')
    steady=[row['cpu_interval_percent'] for row in samples if row['elapsed_s']>=30 and row['cpu_interval_percent'] is not None]
    visibility={}
    for name,path in visibility_paths.items():
        try:visibility[name]=json.loads(path.read_text())
        except (OSError,ValueError):visibility[name]={'unavailable':True}
    result={'schema':1,'site':args.site,'engine':args.engine,'run_tag':args.tag,'binary_sha256':fingerprint,'source_revision':source_revision,'headless':args.engine=='chrome' and not args.headful,'window_visibility':visibility,'samples_with_active_compilers':sum(bool(row['active_compilers']) for row in samples),'tested_utc':dt.datetime.now(dt.timezone.utc).isoformat(),'scope':'fresh signed-out public-page rendering and typed readiness observations' if args.public_fresh else 'private profile, read-only UI readiness; no provider authentication guarantee from DOM indicators alone','raw_logs_saved':False,'screenshots_taken':args.public_fresh and capture_path.is_file(),'capture_path':str(capture_path) if args.public_fresh and capture_path.is_file() else None,'profile_policy':'fresh empty isolated profile, no imported sessions' if args.public_fresh else 'existing isolated matched-cookie comparison profile','account_actions_performed':False,'sample_count':len(samples),'steady_cpu_mean_percent':statistics.mean(steady) if steady else None,'rss_peak_mib':max((p['rss_mib'] for p in samples),default=None),'evidence':evidence,'driver_exit_code':driver.returncode if driver else None,'driver_protocol_diagnostics':driver_diagnostics,'samples':samples}
    destination.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:value for key,value in result.items() if key!='samples'},indent=2))

if __name__=='__main__':main()
