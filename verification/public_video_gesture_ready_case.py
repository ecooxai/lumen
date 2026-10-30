#!/usr/bin/env python3
"""One fixed public Play-control click in an owned empty Chrome profile.

No original profile is opened. Only bounded typed evidence and owned public
window captures survive; raw browser/driver output is drained and discarded.
"""
import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import tempfile
import threading
import time
from native_case import descendants
from resource_monitor import sample_processes, add_cpu_deltas

URL='https://www.youtube.com/watch?v=YE7VzlLtp-4'
LIMIT=80
BOOLS={'targetPageMatched','videoPresent','videoElementChanged','adPlaybackObserved','clickArmed','playingAfterTrustedClick','programmaticPlayCalled','paused','muted','documentHidden','resourcesReleased','actualCompositorFramesVerified','accountActionsPerformed','physicalInputVerified'}
NUMBERS={'schema','probeRevision','elapsedMs','trustedControlClickCount','untrustedControlClickCount','trustedClickElapsedMs','readyState','mediaErrorCode','videoWidth','videoHeight','muteWriteCount','muteWriteFailureCount','unmutedBeforeEnforcementSamples','currentTimeDeltaSeconds','playbackProgressSamples','firstProgressElapsedMs','lastProgressElapsedMs','totalFrameCountDelta','droppedFrameCountDelta','nonDroppedFrameCountDelta','viewportWidth','viewportHeight','devicePixelRatio','eventsOmitted'}
ENUMS={'state':{'waiting-for-video','waiting-for-play-control','unexpected-page','input-armed','trusted-control-clicked','video-frame-progress-observed','playback-not-verified'},'selectedControl':{None,'large-play','transport-play'},'videoFrameCounter':{None,'getVideoPlaybackQuality'},'videoSourceKind':{'none','provider-object','blob','http','other','unavailable'}}
EVENTS={'bound','control-click','play','playing','pause','waiting','stalled','ended','error','loadeddata','cleanup'}
REASONS={'unsafe-or-unavailable-page','already-armed','already-playing','media-not-ready','no-player','play-control-unavailable','visible-unobscured-play-control','no-probe'}

def finite(value):
    return type(value) in (int,float) and math.isfinite(value) and -1e7<=value<=1e12

def sanitize_line(line):
    if len(line)>8192:return None
    marker=next((p for p in ('LUME_PUBLIC_VIDEO_GESTURE ','LUME_PUBLIC_VIDEO_INPUT ','LUME_PUBLIC_VIDEO_DIAGNOSTIC ') if line.startswith(p)),None)
    if marker is None:return None
    try:source=json.loads(line[len(marker):])
    except (ValueError,TypeError):return None
    if not isinstance(source,dict) or type(source.get('schema')) is not int or source['schema']!=1:return None
    result={'schema':1}
    if marker=='LUME_PUBLIC_VIDEO_DIAGNOSTIC ':
        for key in ('failed','contextTransition','observerCompleted','cleanupFailed','navigationSelected','navigationDOMContentLoadedObserved','navigationLoadObserved'):
            if type(source.get(key)) is bool:result[key]=source[key]
        for key in ('navigationReadyWaitMs','navigationReceiptCount'):
            value=source.get(key)
            if key in source and (value is None or type(value) in (int,float) and math.isfinite(value) and 0<=value<=55000):result[key]=value
        code=source.get('cdpErrorCode')
        if code is None or type(code) is int and abs(code)<1e9:result['cdpErrorCode']=code
        allowed={'phase':{'profile-preflight','debug-port','owned-target','debug-connect','page-enable','lifecycle-enable','wait-navigation-ready','viewport','foreground','observer-install','navigate','observe','measure-control','arm-control','dispatch-control','finish-observation'},
            'errorKind':{'cdp-error','cdp-timeout','script-exception','local-guard','none'},
            'cdpMethod':{None,'Page.enable','Page.setLifecycleEventsEnabled','Browser.getWindowForTarget','Runtime.evaluate','Browser.getWindowBounds','Browser.setWindowBounds','Page.bringToFront','Page.addScriptToEvaluateOnNewDocument','Page.navigate','Input.dispatchMouseEvent','Browser.close'},
            'exceptionClass':{None,'Error','TypeError','ReferenceError','SyntaxError','RangeError','OtherError'}}
        for key,values in allowed.items():
            value=source.get(key)
            if type(value) in (str,type(None)) and value in values:result[key]=value
        return 'diagnostic',result
    if marker=='LUME_PUBLIC_VIDEO_INPUT ':
        for key in ('pressRequested','releaseRequested','pressAcknowledged','releaseAcknowledged','programmaticPlayCalled','physicalInputVerified'):
            if type(source.get(key)) is bool:result[key]=source[key]
        for key in ('clicksRequested','elapsedMs'):
            if finite(source.get(key)):result[key]=source[key]
        for key,values in {'method':{'Input.dispatchMouseEvent','none'},'control':{None,'large-play','transport-play'},'unavailableReason':REASONS}.items():
            value=source.get(key)
            if type(value) in (str,type(None)) and value in values:result[key]=value
        return 'input',result
    for key in BOOLS:
        value=source.get(key)
        if type(value) is bool or value is None:result[key]=value
    for key in NUMBERS:
        value=source.get(key)
        if value is None or finite(value):result[key]=value
    for key,values in ENUMS.items():
        value=source.get(key)
        if type(value) in (str,type(None)) and value in values:result[key]=value
    timeline=source.get('eventTimeline')
    if isinstance(timeline,list) and len(timeline)<=24:
        result['eventTimeline']=[]
        for event in timeline:
            if not isinstance(event,dict) or event.get('type') not in EVENTS:continue
            row={'type':event['type']}
            for key in ('isTrusted','cleanup','paused','muted'):
                if type(event.get(key)) is bool:row[key]=event[key]
            for key in ('elapsedMs','readyState','currentTime'):
                if event.get(key) is None or finite(event.get(key)):row[key]=event.get(key)
            result['eventTimeline'].append(row)
    return 'video',result

def assess(evidence,inputs):
    finals=[r for r in evidence if r.get('resourcesReleased') is True]
    row=finals[-1] if finals else {}
    item=inputs[0] if len(inputs)==1 else {}
    timeline=row.get('eventTimeline',[])
    click=row.get('trustedClickElapsedMs')
    input_ok=(len(inputs)==1 and item.get('method')=='Input.dispatchMouseEvent' and item.get('clicksRequested')==1 and
        all(item.get(k) is True for k in ('pressRequested','releaseRequested','pressAcknowledged','releaseAcknowledged')) and
        item.get('programmaticPlayCalled') is False and item.get('physicalInputVerified') is False and
        item.get('control') in {'large-play','transport-play'} and item.get('control')==row.get('selectedControl'))
    click_ok=(type(row.get('trustedControlClickCount')) is int and row['trustedControlClickCount']==1 and
        row.get('untrustedControlClickCount')==0 and finite(click) and row.get('playingAfterTrustedClick') is True and
        any(e.get('type')=='control-click' and e.get('isTrusted') is True and e.get('cleanup') is False and e.get('elapsedMs')==click for e in timeline) and
        any(e.get('type')=='playing' and e.get('isTrusted') is True and e.get('cleanup') is False and finite(e.get('elapsedMs')) and e['elapsedMs']>=click for e in timeline))
    frame_ok=(all(finite(row.get(k)) for k in ('totalFrameCountDelta','droppedFrameCountDelta','nonDroppedFrameCountDelta')) and
        row['droppedFrameCountDelta']>=0 and row['nonDroppedFrameCountDelta']>=30 and
        row['totalFrameCountDelta']-row['droppedFrameCountDelta']==row['nonDroppedFrameCountDelta'])
    passed=(row.get('probeRevision')==1 and row.get('state')=='video-frame-progress-observed' and input_ok and click_ok and frame_ok and
        all(row.get(k) is True for k in ('targetPageMatched','videoPresent','clickArmed','paused','muted','resourcesReleased')) and
        all(row.get(k) is False for k in ('videoElementChanged','adPlaybackObserved','programmaticPlayCalled','documentHidden','accountActionsPerformed','physicalInputVerified')) and
        type(row.get('muteWriteFailureCount')) is int and row['muteWriteFailureCount']==0 and row.get('mediaErrorCode') is None and
        finite(row.get('currentTimeDeltaSeconds')) and row['currentTimeDeltaSeconds']>=3 and
        type(row.get('playbackProgressSamples')) is int and row['playbackProgressSamples']>=3 and
        finite(row.get('videoWidth')) and row['videoWidth']>0 and finite(row.get('videoHeight')) and row['videoHeight']>0 and
        row.get('videoFrameCounter')=='getVideoPlaybackQuality')
    return {'videoFrameProgressVerified':passed,'singleCDPClickAcknowledged':input_ok,'trustedClickAndPlayingObserved':click_ok,
        'frameArithmeticAndThresholdPassed':frame_ok,'state':row.get('state'),'physicalInputVerified':False,
        'actualCompositorFramesVerified':False,'audioOutputVerified':False,'performanceComparisonClaim':False}

class TypedOutput:
    def __init__(self):self.video=[];self.inputs=[];self.diagnostics=[]
    def drain(self,pipe):
        continued=False
        for raw in iter(lambda:pipe.readline(8193),b''):
            complete=raw.endswith(b'\n')
            if not continued and complete and len(raw)<=8192:
                item=sanitize_line(raw.decode('utf8',errors='replace').strip())
                if item:
                    target=self.video if item[0]=='video' else self.inputs if item[0]=='input' else self.diagnostics
                    if len(target)<16:target.append(item[1])
            continued=not complete
        pipe.close()

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def png(path):
    if not path.is_file() or not 100<path.stat().st_size<20*1024*1024:return None
    with path.open('rb') as stream:header=stream.read(24)
    if header[:8]!=b'\x89PNG\r\n\x1a\n' or header[12:16]!=b'IHDR':return None
    w,h=int.from_bytes(header[16:20],'big'),int.from_bytes(header[20:24],'big')
    if not 400<=w<=5000 or not 300<=h<=5000:return None
    return {'path':str(path),'sha256':sha(path),'width':w,'height':h}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--tag',required=True);args=parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,40}',args.tag):raise SystemExit('Invalid evidence tag')
    os.umask(0o077);root=Path.cwd().resolve();out=root/'out/verification';prefix=out/('public-video-chrome-'+args.tag)
    if list(out.glob(prefix.name+'.*')):raise SystemExit('Existing evidence must be preserved')
    exe=Path('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');before=sha(exe)
    profile=Path(tempfile.mkdtemp(prefix='public-video-gesture-empty-',dir=out))
    typed=TypedOutput();workers=[];jobs=[];captures={};visibility={};samples=[];failure=None;cleanup=[]
    def spawn(command):
        p=subprocess.Popen(command,env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
        thread=threading.Thread(target=typed.drain,args=(p.stdout,),daemon=True);thread.start();workers.append((p,thread));return p
    def capture(label,operation=None):
        target=Path(str(prefix)+('.visibility-'+label+'.json' if operation else '.'+label+'.capture.json'));handle=target.open('x')
        command=[str(out/'capture-window'),operation,str(browser.pid)] if operation else [str(out/'capture-window'),str(browser.pid),str(Path(str(prefix)+'.'+label+'.png'))]
        job=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=handle,stderr=subprocess.STDOUT);jobs.append((job,handle))
        if operation:visibility[label]=target
        else:captures[label]=Path(str(prefix)+'.'+label+'.png')
    started=dt.datetime.now(dt.timezone.utc).isoformat();start=time.monotonic();due=start;previous={};prior=None
    browser=driver=None
    try:
        (out/'monitor-scenario.txt').write_text('public-video-chrome-trusted-input\n')
        browser=spawn([str(exe),'--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-sync','--remote-debugging-port=0','--user-data-dir='+str(profile),'--window-size=1024,768','about:blank'])
        driver=spawn(['node','verification/public_video_gesture_chrome_ready.mjs',str(profile)])
        while time.monotonic()-start<LIMIT:
            processes=sample_processes();now=time.monotonic();add_cpu_deltas(processes,previous,now-prior if prior else 0);previous={p['pid']:p for p in processes};prior=now
            owned={p['pid']:p for pid in (browser.pid,driver.pid) for p in descendants(processes,pid)}
            cpu=[p['cpu_interval_percent'] for p in owned.values() if p['cpu_interval_percent'] is not None]
            monitor_mtime=(out/'resource-monitor.jsonl').stat().st_mtime;monitor_age=time.time()-monitor_mtime
            st=os.statvfs(root);free=st.f_bavail*st.f_frsize
            compilers=sum(1 for p in processes if p['name'] in ('clang','clang++','ld','swift-frontend','rustc','cargo','ninja','xcodebuild','cc1') and (p.get('cpu_interval_percent') or p['cpu_percent'])>10)
            row={'elapsed_s':round(now-start,3),'cpu_interval_percent':sum(cpu) if cpu else None,'rss_mib':sum(p['rss_kib'] for p in owned.values())/1024,'owned_process_count':len(owned),'active_compilers':compilers,'free_bytes':free,'monitor_age_s':monitor_age};samples.append(row)
            if not 0<=monitor_age<6 or free<1024**3 or row['rss_mib']>4096 or compilers:
                failure='resource-or-exclusive-slot-bound';break
            if 'activation' not in visibility:capture('activation','--activate-owned')
            for phase,deadline in [('earlier',18),('later',36)]:
                if phase not in captures and now-start>=deadline and browser.poll() is None:capture(phase)
            if 'steady' not in visibility and now-start>=35 and browser.poll() is None:capture('steady','--inspect-owned')
            if driver.poll() is not None and browser.poll() is not None:break
            due+=1;time.sleep(max(0,due-time.monotonic()))
        else:failure='observation-time-bound'
    except (Exception,KeyboardInterrupt) as error:
        failure=type(error).__name__
    finally:
        # Popen handles and session-leading owned children, never broad names.
        natural={str(p.pid):p.poll() for p,_ in workers}
        for process,thread in reversed(workers):
            try:
                if process.poll() is None:
                    os.killpg(process.pid,signal.SIGTERM)
                    try:process.wait(timeout=4)
                    except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait(timeout=3)
                thread.join(timeout=3)
                if thread.is_alive():cleanup.append('typed-drain-not-closed')
            except (OSError,subprocess.TimeoutExpired):cleanup.append('owned-process-cleanup-failed')
        for job,handle in jobs:
            try:
                try:job.wait(timeout=3)
                except subprocess.TimeoutExpired:job.terminate();job.wait(timeout=3)
                if job.returncode!=0:cleanup.append('owned-capture-operation-failed')
            except (OSError,subprocess.TimeoutExpired):cleanup.append('owned-capture-cleanup-failed')
            handle.close()
        (out/'monitor-scenario.txt').write_text('development-uncontrolled\n')
    view={}
    for label,path in visibility.items():
        try:
            raw=json.loads(path.read_text());view[label]={k:raw[k] for k in ('accepted','activeAfter','active','applicationHidden','frontmostMatchesTarget','ownedWindowFound','ownedWindowOnScreen','pid','inputEventsPosted','permissionsChanged') if k in raw}
        except (OSError,ValueError):view[label]={'unavailable':True}
    images={label:png(path) for label,path in captures.items()}
    steady=[r['cpu_interval_percent'] for r in samples if 30<=r['elapsed_s']<44 and r['cpu_interval_percent'] is not None]
    result={'schemaVersion':1,'engine':'chrome','tag':args.tag,'startedUTC':started,'endedUTC':dt.datetime.now(dt.timezone.utc).isoformat(),
        'scope':'one CDP click on a measured visible fixed public Play control, followed by typed event/counter observation',
        'publicURL':URL,'profilePolicy':'fresh empty isolated; no cookies imported','originalProfilesAccessedOrChanged':False,
        'rawLogsSaved':False,'providerAuthenticationVerified':False,'accountActionsPerformed':False,'physicalInputVerified':False,
        'performanceComparisonClaim':False,'actualRenderedPixelsRequireSeparateReview':True,'binarySHA256':before,'binaryUnchanged':sha(exe)==before,
        'browserNaturalExit':natural.get(str(browser.pid)) if browser else None,'observerNaturalExit':natural.get(str(driver.pid)) if driver else None,
        'failure':failure,'cleanupErrors':cleanup,'observationBoundSeconds':LIMIT,'sampleCount':len(samples),
        'samplesWithCompilers':sum(bool(r['active_compilers']) for r in samples),'peakRSSMiB':max((r['rss_mib'] for r in samples),default=None),
        'minimumFreeBytes':min((r['free_bytes'] for r in samples),default=None),'steadyCPUPercentOfOneCore':statistics.mean(steady) if steady else None,
        'steadyScope':'descriptive30to44seconds after process start, not matched performance','windowVisibility':view,'captures':images,
        'inputs':typed.inputs,'diagnostics':typed.diagnostics,'evidence':typed.video,'assessment':assess(typed.video,typed.inputs),'samples':samples}
    result['navigationReadinessVerified']=(len(typed.diagnostics)==1 and typed.diagnostics[0].get('navigationSelected') is True and typed.diagnostics[0].get('navigationDOMContentLoadedObserved') is True)
    result['passed']=(result['navigationReadinessVerified'] and failure is None and not cleanup and result['binaryUnchanged'] and result['browserNaturalExit']==0 and result['observerNaturalExit']==0 and
        result['sampleCount']>=30 and result['samplesWithCompilers']==0 and result['assessment']['videoFrameProgressVerified'] and
        set(images)=={'earlier','later'} and all(images.values()) and view.get('steady',{}).get('ownedWindowOnScreen') is True and
        view.get('steady',{}).get('frontmostMatchesTarget') is True)
    with Path(str(prefix)+'.json').open('x') as stream:json.dump(result,stream,indent=2);stream.write('\n')
    print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))
    return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
