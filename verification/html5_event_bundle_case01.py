#!/usr/bin/env python3
"""Separate owned-window independent async-event diagnostic.
This does not execute or score the HTML5 benchmark.
"""
import re
from urllib.parse import urlparse,parse_qs
import argparse,datetime as dt,hashlib,json,os,signal,statistics,subprocess,tempfile,time
from pathlib import Path
from native_case import descendants
from resource_monitor import sample_processes,add_cpu_deltas


def matching_pids(executable):
    result=[]
    for line in subprocess.check_output(['ps','-axo','pid=,comm='],text=True).splitlines():
        item=line.strip().split(None,1)
        if len(item)==2 and item[1]==str(executable):result.append(int(item[0]))
    return result


def process_identity(pid):
    try:
        p=subprocess.run(['ps','-p',str(pid),'-o','uid=,lstart=,comm='],capture_output=True,text=True,timeout=3)
        return p.stdout.strip() if p.returncode==0 and p.stdout.strip() else None
    except (OSError,subprocess.TimeoutExpired):return None


def claimed_identity(pid, executable, launched_since):
    value=process_identity(pid)
    if not value:return None
    fields=value.split(None,6)
    if len(fields)!=7 or fields[0]!=str(os.getuid()) or fields[6]!=str(executable):return None
    try:started=time.mktime(time.strptime(' '.join(fields[1:6]),'%a %b %d %H:%M:%S %Y'))
    except ValueError:return None
    return value if started>=int(launched_since)-1 else None


def late_owned_process(executable, launched_since):
    until=time.monotonic()+3
    while time.monotonic()<until:
        found=matching_pids(executable)
        if len(found)>1:raise RuntimeError('Ambiguous late exact executable identities; no signal sent')
        if found:
            value=claimed_identity(found[0],executable,launched_since)
            if not value:raise RuntimeError('Late executable identity differs; no signal sent')
            return found[0],value
        time.sleep(.1)
    return None,None


def structured_evidence(lines, marker):
    records=[]
    for line in lines:
        if marker not in line:continue
        try:records.append(json.loads(line.split(marker,1)[1]))
        except ValueError:pass
    return records


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--name',default='gpu-challenge-native-bundle');ap.add_argument('--probe',default='verification/gpu-challenge-probe.js');ap.add_argument('--url',required=True);ap.add_argument('--duration',type=float,default=30);ap.add_argument('--metadata',default='out/native-verification/mac-dev-bundle.json');ap.add_argument('--kind',choices=['html5-events'],default='html5-events');ap.add_argument('--mac-visibility',action='store_true');ap.add_argument('--gpu-stats',action='store_true');ap.add_argument('--hide-cycle',action='store_true');ap.add_argument('--reactivate-after-unhide',action='store_true');ap.add_argument('--settle',type=float,default=20);args=ap.parse_args()
    if args.reactivate_after_unhide and not args.hide_cycle:raise SystemExit('Reactivation control requires the hide cycle')
    if args.hide_cycle and (args.kind!='gpu-challenge' or args.duration<28):raise SystemExit('Hide/recover control requires the extended GPU probe and at least28 seconds')
    parsed=urlparse(args.url);nonce=parse_qs(parsed.query).get('nonce',[''])[0]
    if parsed.scheme!='http' or parsed.hostname!='127.0.0.1' or not parsed.port or parsed.path!='/event-diagnostic' or re.fullmatch('[a-f0-9]{24}',nonce) is None:raise SystemExit('Only the owned loopback event diagnostic is permitted')
    if not 40<=args.duration<=65:raise SystemExit('Bounded independent diagnostic duration required')
    def interrupted(signum, frame):raise KeyboardInterrupt('Owned event diagnostic interrupted')
    signal.signal(signal.SIGTERM, interrupted);signal.signal(signal.SIGINT, interrupted)
    root=Path.cwd().resolve();out=root/'out/verification';out.mkdir(parents=True,exist_ok=True);prefix=out/args.name
    manifest=json.loads(Path(args.metadata).read_text());app=Path(manifest['app']).resolve();exe=Path(manifest['executable']).resolve()
    if not app.is_relative_to(root/'out') or exe!=app/'Contents/MacOS/lumen':raise SystemExit('Bundle must be the isolated development artifact')
    if matching_pids(exe):raise SystemExit('Owned development executable is already running; refusing ambiguous ownership')
    source_hash=hashlib.sha256((root/'build/lumen').read_bytes()).hexdigest()
    if source_hash!=manifest['binaryBeforeBundleSigningSha256']:raise SystemExit('Development bundle is stale relative to build/lumen')
    profile=Path(tempfile.mkdtemp(prefix='bundle-public-profile-',dir=out));stdout_path=Path(str(prefix)+'.stdout.log');stderr_path=Path(str(prefix)+'.stderr.log')
    envs={'LUMEN_PROFILE_DIR':str(profile),'LUMEN_PRE_FILE':str((root/args.probe).resolve()),'LUMEN_MAC_VISIBILITY':'1' if args.mac_visibility else '0','LUMEN_CONSOLE':'1','LUMEN_NO_SIDEBAR':'1','LUMEN_WINDOW':'1024x768','LUMEN_SHOT':str(prefix)+'.internal.png','LUMEN_SHOT_MS':'60000'}
    if args.gpu_stats:envs['LUMEN_GPU_STATS']='1'
    command=['/usr/bin/open','-n',str(app),'--stdout',str(stdout_path),'--stderr',str(stderr_path)]
    for key,value in envs.items():command+=['--env',key+'='+value]
    command+=['--args',args.url]
    mode=out/'monitor-scenario.txt';mode.write_text(args.name+'\n');started_utc=dt.datetime.now(dt.timezone.utc).isoformat();start=time.monotonic();due=start;previous={};prior=None;rows=[];pid=None;owned_identity=None;pid_observed=False;captures={};activation=None;activation_log=None;actions={}
    launcher_log=open(str(prefix)+'.launcher.log','w');launcher=None;cleanup_errors=[];launched_since=time.time()
    journal=Path(str(prefix)+'.owned-process.json')
    journal.write_text(json.dumps({'pid':None,'identity':None,'executable':str(exe),'uid':os.getuid(),'launchedSince':launched_since,'absentBeforeLaunch':True}))
    try:
        launcher=subprocess.Popen(command,stdout=launcher_log,stderr=subprocess.STDOUT,start_new_session=True)
        while time.monotonic()-start<args.duration:
            processes=sample_processes();now=time.monotonic();add_cpu_deltas(processes,previous,now-prior if prior else 0);previous={p['pid']:p for p in processes};prior=now
            if pid is None:
                found=matching_pids(exe)
                if len(found)>1:raise RuntimeError('More than one exact owned executable appeared')
                if found:
                    pid=found[0];owned_identity=claimed_identity(pid,exe,launched_since)
                    if not owned_identity:raise RuntimeError('Owned browser identity unavailable')
                    journal.write_text(json.dumps({'pid':pid,'identity':owned_identity,'executable':str(exe),'uid':os.getuid(),'launchedSince':launched_since,'absentBeforeLaunch':True}))
                    activation_log=open(str(prefix)+'.activation.log','w');activation=subprocess.Popen([str(out/'capture-window'),'--activate-owned',str(pid)],stdout=activation_log,stderr=subprocess.STDOUT)
                elif now-start>10:raise RuntimeError('Normal LaunchServices launch produced no exact executable PID')
            selected=descendants(processes,pid) if pid else []
            cpu=[p['cpu_interval_percent'] for p in selected if p['cpu_interval_percent'] is not None]
            rows.append({'elapsed_s':round(now-start,3),'pid_discovered':pid is not None,'cpu_interval_percent':sum(cpu) if cpu else None,'rss_kib':sum(p['rss_kib'] for p in selected),'members':selected,'active_compilers':sum(1 for p in processes if p['name'] in ('clang','clang++','ld','swift-frontend','rustc','cargo','ninja','cc1') and (p.get('cpu_interval_percent') or p['cpu_percent'])>10)})
            monitor=root/'out/verification/resource-monitor.jsonl'
            monitor_mtime=monitor.stat().st_mtime
            if not 0<=time.time()-monitor_mtime<6:raise RuntimeError('One-second monitor stale')
            fs=os.statvfs(root)
            if fs.f_bavail*fs.f_frsize<1024**3 or rows[-1]['rss_kib']>4*1024*1024:raise RuntimeError('Owned memory/disk bound')
            text='\n'.join(p.read_text(errors='replace') if p.exists() else '' for p in [stdout_path,stderr_path])
            if pid:
                for stage in ('overview','gpu','network'):
                    if stage not in captures and 'LUME_CAPTURE_REQUEST '+stage in text:
                        handle=open(str(prefix)+'.'+stage+'.capture.log','w');job=subprocess.Popen([str(out/'capture-window'),str(pid),str(prefix)+'.'+stage+'.png'],stdout=handle,stderr=subprocess.STDOUT);captures[stage]=(job,handle)
                if args.hide_cycle:
                    for action,deadline in [('hide',6),('unhide',18)]+([('reactivate',20)] if args.reactivate_after_unhide else []):
                        if action not in actions and now-start>=deadline:
                            if action=='reactivate' and ('unhide' not in actions or actions['unhide'][0].poll() is None):continue
                            operation='--activate-owned' if action=='reactivate' else '--'+action+'-owned'
                            handle=open(str(prefix)+'.'+action+'.json','w');job=subprocess.Popen([str(out/'capture-window'),operation,str(pid)],stdout=handle,stderr=subprocess.STDOUT);actions[action]=(job,handle,round(now-start,3))
                    recovery_ready=('reactivate' in actions and actions['reactivate'][0].poll() is not None and now-start>=23) if args.reactivate_after_unhide else 'unhide' in actions and now-start>=20
                    if recovery_ready and 'recovered' not in captures:
                        handle=open(str(prefix)+'.recovered.capture.log','w');job=subprocess.Popen([str(out/'capture-window'),str(pid),str(prefix)+'.recovered.png'],stdout=handle,stderr=subprocess.STDOUT);captures['recovered']=(job,handle)
                if selected:pid_observed=True
                elif pid_observed:break
            due+=1;time.sleep(max(0,due-time.monotonic()))
    finally:
        for signum in (signal.SIGINT,signal.SIGTERM):signal.signal(signum,signal.SIG_IGN)
        try:
            # A normal launch can precede the first sample. The exact executable
            # was absent before launch; adopt only this UID/new start/executable.
            if pid is None and launcher is not None:
                pid,owned_identity=late_owned_process(exe,launched_since)
                if pid:journal.write_text(json.dumps({'pid':pid,'identity':owned_identity,'executable':str(exe),'uid':os.getuid(),'launchedSince':launched_since,'absentBeforeLaunch':True}))
            if pid and owned_identity and process_identity(pid)==owned_identity:
                try:
                    with open(str(prefix)+'.visibility-final.json','w') as handle:
                        try:subprocess.run([str(out/'capture-window'),'--inspect-owned',str(pid)],stdout=handle,stderr=subprocess.STDOUT,timeout=3,check=False)
                        except (OSError,subprocess.TimeoutExpired) as error:
                            handle.seek(0);handle.truncate();handle.write(json.dumps({'error':'visibility-inspection-'+type(error).__name__}))
                except Exception as error:cleanup_errors.append('visibility:'+type(error).__name__)
                if process_identity(pid)==owned_identity:
                    os.kill(pid,signal.SIGTERM);until=time.monotonic()+4
                    while process_identity(pid)==owned_identity and time.monotonic()<until:time.sleep(.1)
                    if process_identity(pid)==owned_identity:
                        os.kill(pid,signal.SIGKILL);until=time.monotonic()+3
                        while process_identity(pid)==owned_identity and time.monotonic()<until:time.sleep(.1)
        except Exception as error:cleanup_errors.append('browser:'+type(error).__name__)
        owned_jobs=[(launcher,launcher_log)]+([(activation,activation_log)] if activation else [])+list(captures.values())+[(job,handle) for job,handle,_ in actions.values()]
        for job,handle in owned_jobs:
            try:
                if job and job.poll() is None:
                    try:job.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        job.terminate()
                        try:job.wait(timeout=3)
                        except subprocess.TimeoutExpired:job.kill();job.wait(timeout=3)
            except Exception as error:cleanup_errors.append('owned-child:'+type(error).__name__)
            finally:
                if handle:handle.close()
        try:mode.write_text('development-uncontrolled\n')
        except Exception as error:cleanup_errors.append('scenario:'+type(error).__name__)
    combined='\n'.join(p.read_text(errors='replace') if p.exists() else '' for p in [stdout_path,stderr_path]);Path(str(prefix)+'.log').write_text(combined)
    evidence=[line for line in combined.splitlines() if 'LUME_GPU_EVIDENCE ' in line or 'LUME_IDENTITY_EVIDENCE ' in line or 'LUME_LAB_EVIDENCE ' in line or 'LUME_HTML5_EVENTS ' in line]
    stats=[line for line in combined.splitlines() if 'gpu: stats ' in line]
    native_visibility=[line for line in combined.splitlines() if 'mac: visibility ' in line]
    gpu_records=structured_evidence(evidence,'LUME_GPU_EVIDENCE ')
    lab_records=structured_evidence(evidence,'LUME_LAB_EVIDENCE ')
    identity_records=structured_evidence(evidence,'LUME_IDENTITY_EVIDENCE ')
    readback_count=sum(row.get('stage')=='readback' and row.get('value',{}).get('passed') is True for row in gpu_records)
    cleanup_verified=any(row.get('stage')=='cleanup' and row.get('value',{}).get('resourcesReleased') is True for row in gpu_records)
    observed_samples=sum(bool(row['members']) for row in rows)
    post_stops=[row.get('payload',{}) for row in lab_records if row.get('kind')=='post-stop']
    lab_cleanup=bool(post_stops) and all(post_stops[-1].get('resources',{}).get(key)==0 for key in ['activeScopes','registeredCleanups','activeMediaTracks'])
    lab_completed=any(row.get('kind')=='completed' for row in lab_records)
    lab_results={row['payload']['id']:row['payload'] for row in lab_records if row.get('kind')=='result' and 'id' in row.get('payload',{})}
    common_passed=all(lab_results.get(name,{}).get('state')=='pass' for name in ['wasm','webgl','webgpu','iframe','websocket'])
    identity_complete=bool(identity_records) and identity_records[-1].get('status')=='pass' and identity_records[-1].get('resourcesReleased') is True and len(identity_records[-1].get('checks',[]))==4 and all(check.get('passed') is True for check in identity_records[-1]['checks'])
    live_records=structured_evidence(evidence,'LUME_HTML5_EVENTS ')
    live_complete=any(v.get('stage')=='completed' and v.get('benchmarkExecuted') is False and v.get('nonce')==nonce and len(v.get('records',[]))==17 for v in live_records)
    kind_complete={'html5-events':live_complete,'gpu-challenge':readback_count==2 and cleanup_verified,'common':lab_completed and lab_cleanup and common_passed,'showcase':lab_completed and lab_cleanup,'identity-simulation':identity_complete}
    workload_complete=kind_complete[args.kind] and observed_samples>=10 and not cleanup_errors and not matching_pids(exe)
    visibility={}
    for name,suffix in [('activation','.activation.log'),('final','.visibility-final.json')]:
        try:visibility[name]=json.loads(Path(str(prefix)+suffix).read_text())
        except (OSError,ValueError):pass
    steady=[r['cpu_interval_percent'] for r in rows if r['elapsed_s']>=args.settle and r['cpu_interval_percent'] is not None]
    result={'name':args.name,'started_utc':started_utc,'launcher':'normal LaunchServices open -n','launcher_exit':launcher.returncode,'actual_browser_pid':pid,'application_exit_status':'not available for a LaunchServices child','binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'source_binary_sha256':source_hash,'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'window_visibility':visibility,'sample_count':len(rows),'observed_browser_samples':observed_samples,'gpu_readback_count':readback_count,'cleanup_verified':cleanup_verified,'gpu_workload_complete':workload_complete,'steady_cpu_mean_percent':statistics.mean(steady) if steady else None,'rss_peak_mib':max((r['rss_kib'] for r in rows),default=0)/1024,'samples_with_active_compilers':sum(bool(r['active_compilers']) for r in rows),'scope':'independent actual async event diagnostic; not an HTML5 score or performance comparison','evidence':evidence,'gpu_stats_tail':stats[-3:],'native_visibility_first':native_visibility[:1],'native_visibility_tail':native_visibility[-3:],'samples':rows}
    result.update(kind=args.kind,workload_complete=workload_complete,gpu_workload_complete=kind_complete['gpu-challenge'] and observed_samples>=10,lab_cleanup_verified=lab_cleanup,common_probes_passed=common_passed,identity_simulation_passed=identity_complete,settle_s=args.settle,steady_samples=len(steady),mac_visibility_diagnostics=args.mac_visibility,gpu_stats_diagnostics=args.gpu_stats)
    action_evidence={}
    for action,(_,_,elapsed) in actions.items():
        try:action_evidence[action]={'requested_elapsed_s':elapsed,'result':json.loads(Path(str(prefix)+'.'+action+'.json').read_text())}
        except (OSError,ValueError):action_evidence[action]={'requested_elapsed_s':elapsed,'unavailable':True}
    reactivated=action_evidence.get('reactivate',{}).get('result',{})
    normal_reactivation=reactivated.get('activeAfter') is True and reactivated.get('frontmostMatchesTarget') is True
    result['hide_recover_control']={'requested':args.hide_cycle,'scope':'normal owned-app hide/unhide then normal activation; not minimization' if args.reactivate_after_unhide else 'normal owned-app hide/unhide; not minimization','actions':action_evidence,'normal_reactivation_requested':args.reactivate_after_unhide,'normal_reactivation_verified':normal_reactivation if args.reactivate_after_unhide else None,'recovered_pixels_visually_verified':False}
    if args.hide_cycle:result['native_visibility_lifecycle']=native_visibility;result['gpu_stats_lifecycle']=stats
    result['rss_final_mib']=rows[-1]['rss_kib']/1024 if rows else None
    result['html5_event_records']=live_records
    result['cleanupErrors']=cleanup_errors
    result['ownedBrowserStopped']=not matching_pids(exe)
    result['lab_results']=lab_results
    result['post_stop_evidence']=post_stops[-1] if post_stops else None
    Path(str(prefix)+'.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))
    for stage in captures:
        p=Path(str(prefix)+'.'+stage+'.capture.log');print(stage+': '+p.read_text().strip())
    if not workload_complete:raise SystemExit('Bundle control did not complete the requested actual workload')
    if args.reactivate_after_unhide and not normal_reactivation:raise SystemExit('Normal post-unhide foreground activation was not observed')

if __name__=='__main__':main()
