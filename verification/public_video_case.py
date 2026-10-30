#!/usr/bin/env python3
"""Actual public YouTube playback observation in a new empty browser profile.

Only typed counters/booleans and owned public-window images are retained. No
cookies are imported, no account controls are clicked, and the play request is muted.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import tempfile
import threading
import time
from auth_bundle_case import RedactedPipe
from bundle_case import matching_pids
from native_case import descendants
from resource_monitor import sample_processes, add_cpu_deltas

URL='https://www.youtube.com/watch?v=YE7VzlLtp-4'
OBSERVATION_LIMIT_SECONDS=70
BOOLS={'targetPageMatched','videoPresent','videoElementChanged','adPlaybackObserved','playAttempted','playPromiseResolved','playRejected','paused','muted','documentHidden','resourcesReleased','actualCompositorFramesVerified','accountActionsPerformed'}
NUMBERS={'schema','probeRevision','elapsedMs','readyState','mediaErrorCode','videoWidth','videoHeight','currentTimeDeltaSeconds','playbackProgressSamples','firstProgressElapsedMs','lastProgressElapsedMs','playRequestElapsedMs','readyStateAtPlayRequest','muteWriteCount','muteWriteFailureCount','unmutedBeforeEnforcementSamples','totalFrameCountDelta','droppedFrameCountDelta','nonDroppedFrameCountDelta','viewportWidth','viewportHeight','devicePixelRatio'}
ENUMS={'state':{'waiting-for-media-data','mute-not-applied','waiting-for-video','unexpected-page','play-requested','no-video','play-rejected','video-element-replaced','ad-playback-observed','video-frame-progress-observed','playback-not-verified'},'rejectionName':{None,'NotAllowedError','NotSupportedError','AbortError','SecurityError','OtherError'},'videoFrameCounter':{None,'getVideoPlaybackQuality','webkitDecodedFrameCount'},'videoSourceKind':{'none','blob','http','data','provider-object','other','unavailable'}}


def sanitize_video(line):
    marker='LUME_PUBLIC_VIDEO '
    if len(line)>4096:return None
    if line.startswith('[js log] '):line=line[len('[js log] '):]
    if not line.startswith(marker):return None
    try: source=json.loads(line[len(marker):])
    except (ValueError,TypeError):return None
    if not isinstance(source,dict) or type(source.get('schema')) is not int or source.get('schema')!=1:return None
    result={}
    for key in BOOLS:
        value=source.get(key)
        if type(value) is bool or value is None:result[key]=value
    for key in NUMBERS:
        value=source.get(key)
        if value is None or type(value) in (int,float) and -1e7<=value<=1e12:result[key]=value
    for key,allowed in ENUMS.items():
        value=source.get(key)
        if isinstance(value,(str,type(None))) and value in allowed:result[key]=value
    return result


def assess_video(evidence):
    finals=[row for row in evidence if row.get('resourcesReleased') is True]
    if not finals:return {'videoFrameProgressVerified':False,'reason':'no verified cleanup and completed observation'}
    row=finals[-1]
    passed=(row.get('state')=='video-frame-progress-observed' and row.get('targetPageMatched') is True
      and row.get('videoElementChanged') is False and row.get('accountActionsPerformed') is False
      and row.get('adPlaybackObserved') is False
      and row.get('playRejected') is False and row.get('paused') is True and row.get('muted') is True
      and (row.get('probeRevision') != 3 or (type(row.get('muteWriteFailureCount')) is int and row['muteWriteFailureCount'] == 0
        and type(row.get('readyStateAtPlayRequest')) is int and row['readyStateAtPlayRequest'] >= 2))
      and isinstance(row.get('currentTimeDeltaSeconds'),(int,float)) and row['currentTimeDeltaSeconds']>=3
      and isinstance(row.get('nonDroppedFrameCountDelta'),(int,float)) and row['nonDroppedFrameCountDelta']>=30
      and isinstance(row.get('totalFrameCountDelta'),(int,float)) and isinstance(row.get('droppedFrameCountDelta'),(int,float))
      and row['totalFrameCountDelta']>=30 and row['droppedFrameCountDelta']>=0
      and row['totalFrameCountDelta']-row['droppedFrameCountDelta']==row['nonDroppedFrameCountDelta']
      and (row.get('playbackProgressSamples') or 0)>=3 and (row.get('videoWidth') or 0)>0 and (row.get('videoHeight') or 0)>0
      and row.get('mediaErrorCode') is None and row.get('videoFrameCounter') in {'getVideoPlaybackQuality','webkitDecodedFrameCount'})
    return {'videoFrameProgressVerified':passed,'state':row.get('state'),'reason':'time and non-dropped frame-counter progress with pause/cleanup' if passed else 'requested real playback criteria were not all observed','actualCompositorFramesVerified':False,'audioOutputVerified':False,'physicalInputVerified':False}


class VideoPipe(RedactedPipe):
    def drain(self,pipe):
        continued=False
        for raw in iter(lambda:pipe.readline(8193),b''):
            complete=raw.endswith(b'\n')
            if not continued and complete and len(raw)<=4096:
                record=sanitize_video(raw.decode('utf8',errors='replace').strip())
                if record is not None and len(self.evidence)<24:self.evidence.append(record)
            continued=not complete
        pipe.close()


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--engine',choices=['lumen','chrome'],required=True);ap.add_argument('--metadata',default='out/native-verification/mac-dev-bundle-stable.json');ap.add_argument('--tag',default='');args=ap.parse_args()
    if args.tag and not re.fullmatch(r'[A-Za-z0-9_-]{1,40}',args.tag):raise SystemExit('Invalid public observation tag')
    os.umask(0o077);root=Path.cwd().resolve();out=root/'out/verification';out.mkdir(exist_ok=True,parents=True)
    prefix=out/('public-video-'+args.engine+('-'+args.tag if args.tag else ''))
    if list(out.glob(prefix.name+'.*')):raise SystemExit('Existing public video evidence must be preserved; choose a new tag')
    profile=Path(tempfile.mkdtemp(prefix='public-video-empty-'+args.engine+'-',dir=out))
    evidence=[];pipe=VideoPipe(out,evidence);workers=[];jobs=[];captures={};visibility={};pid=None;source_hash=None;failure=None
    if args.engine=='lumen':
        manifest=json.loads(Path(args.metadata).read_text());app=Path(manifest['app']).resolve();exe=Path(manifest['executable']).resolve()
        if not app.is_relative_to(root/'out') or exe!=app/'Contents/MacOS/lumen':raise SystemExit('Unexpected development bundle')
        source_hash=hashlib.sha256((root/'build/lumen').read_bytes()).hexdigest()
        if source_hash!=manifest['binaryBeforeBundleSigningSha256']:raise SystemExit('Development bundle is stale')
        if matching_pids(exe):raise SystemExit('Owned development executable is already running')
        command=['/usr/bin/open','-n',str(app),'--stdout',str(pipe.path),'--stderr',str(pipe.path)]
        envs={'LUMEN_PROFILE_DIR':str(profile),'LUMEN_PRE_FILE':str(root/'verification/public-video-probe.js'),'LUMEN_CONSOLE':'1','LUMEN_NO_SIDEBAR':'1','LUMEN_WINDOW':'1024x768','LUMEN_MAC_VISIBILITY':'0'}
        for key,value in envs.items():command+=['--env',key+'='+value]
        command+=['--args',URL]
    else:
        exe=Path('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
        command=[str(exe),'--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','--remote-debugging-port=0','--user-data-dir='+str(profile),'--window-size=1024,768','about:blank']
    def process(command):
        proc=subprocess.Popen(command,env={key:value for key,value in os.environ.items() if not key.startswith('LUMEN_')},stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
        thread=threading.Thread(target=pipe.drain,args=(proc.stdout,),daemon=True);thread.start();workers.append((proc,thread));return proc
    def inspect(label,operation):
        target=Path(str(prefix)+'.visibility-'+label+'.json');handle=target.open('w');job=subprocess.Popen([str(out/'capture-window'),operation,str(pid)],stdout=handle,stderr=subprocess.STDOUT);jobs.append((job,handle));visibility[label]=target
    started=dt.datetime.now(dt.timezone.utc).isoformat();start=time.monotonic();due=start;previous={};prior=None;samples=[];observed=False
    (out/'monitor-scenario.txt').write_text('public-video-'+args.engine+'\n')
    browser=process(command)
    if args.engine=='chrome':pid=browser.pid;process(['node','verification/public_video_chrome.mjs',str(profile)])
    try:
        # The probe observes 45 seconds after it is injected. Allow bounded app
        # startup/navigation time before that observation, then end on cleanup.
        while time.monotonic()-start<OBSERVATION_LIMIT_SECONDS:
            processes=sample_processes();now=time.monotonic();add_cpu_deltas(processes,previous,now-prior if prior else 0);previous={p['pid']:p for p in processes};prior=now
            if pid is None:
                found=matching_pids(exe)
                if len(found)>1:failure='ambiguous-owned-process';break
                if found:pid=found[0]
                elif now-start>10:failure='no-owned-process';break
            if pid and 'activation' not in visibility:inspect('activation','--activate-owned')
            owned=descendants(processes,pid) if pid else [];cpu=[p['cpu_interval_percent'] for p in owned if p['cpu_interval_percent'] is not None]
            samples.append({'elapsed_s':round(now-start,3),'cpu_interval_percent':sum(cpu) if cpu else None,'rss_mib':sum(p['rss_kib'] for p in owned)/1024,'owned_process_observed':bool(owned),'active_compilers':sum(1 for p in processes if p['name'] in ('clang','clang++','ld','swift-frontend','rustc','cargo','ninja','cc1') and (p.get('cpu_interval_percent') or p['cpu_percent'])>10)})
            if owned:observed=True
            elif observed:break
            for phase,deadline in [('earlier',18),('later',36)]:
                if pid and phase not in captures and now-start>=deadline:
                    image_path=Path(str(prefix)+'.'+phase+'.png');handle=Path(str(prefix)+'.'+phase+'.capture.json').open('w');job=subprocess.Popen([str(out/'capture-window'),str(pid),str(image_path)],stdout=handle,stderr=subprocess.STDOUT);jobs.append((job,handle));captures[phase]=image_path
            if pid and 'steady' not in visibility and now-start>=35:inspect('steady','--inspect-owned')
            if now-start>=36 and any(row.get('resourcesReleased') is True for row in evidence):break
            due+=1;time.sleep(max(0,due-time.monotonic()))
    finally:
        if args.engine=='lumen' and pid and pid in matching_pids(exe):
            os.kill(pid,signal.SIGTERM);until=time.monotonic()+4
            while pid in matching_pids(exe) and time.monotonic()<until:time.sleep(.1)
            if pid in matching_pids(exe):os.kill(pid,signal.SIGKILL)
        for proc,thread in workers:
            if proc.poll() is None:
                os.killpg(proc.pid,signal.SIGTERM)
                try:proc.wait(timeout=4)
                except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait(timeout=3)
            thread.join(timeout=3)
        for job,handle in jobs:
            try:job.wait(timeout=3)
            except subprocess.TimeoutExpired:job.terminate();job.wait(timeout=3)
            handle.close()
        pipe.close();(out/'monitor-scenario.txt').write_text('development-uncontrolled\n')
    actual_visibility={}
    for label,path in visibility.items():
        try:actual_visibility[label]=json.loads(path.read_text())
        except (OSError,ValueError):actual_visibility[label]={'unavailable':True}
    steady=[row['cpu_interval_percent'] for row in samples if 30<=row['elapsed_s']<44 and row['cpu_interval_percent'] is not None]
    result={'schema':1,'engine':args.engine,'started_utc':started,'actual_browser_pid':pid,'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'source_binary_sha256':source_hash,'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'profile_policy':'fresh empty isolated; no cookies imported','public_url':URL,'scope':'muted programmatic public video playback observation; no account actions or real input','raw_logs_saved':False,'sample_count':len(samples),'samples_with_active_compilers':sum(bool(row['active_compilers']) for row in samples),'window_visibility':actual_visibility,'captures':{key:str(value) for key,value in captures.items() if value.is_file()},'steady_cpu_mean_percent':statistics.mean(steady) if steady else None,'steady_scope':'30 to44 seconds after process launch; playback status must be interpreted separately','rss_peak_mib':max((row['rss_mib'] for row in samples),default=None),'launch_failure':failure,'assessment':assess_video(evidence),'evidence':evidence,'samples':samples}
    result.update(tag=args.tag,observation_bound_seconds=OBSERVATION_LIMIT_SECONDS,
        observation_completed=any(row.get('resourcesReleased') is True for row in evidence))
    Path(str(prefix)+'.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({key:value for key,value in result.items() if key!='samples'},indent=2))


if __name__=='__main__':main()
