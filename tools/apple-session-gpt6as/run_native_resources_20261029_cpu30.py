from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import subprocess
import sys
import time

parser=argparse.ArgumentParser()
parser.add_argument('--build-summary',default='out/lume-20261029-cpu30/native-combined-1/summary.json')
parser.add_argument('--output',default='out/lume-20261029-cpu30/native-resources-1')
args=parser.parse_args()
root=Path.cwd().resolve()
out=(root/args.output).resolve();out.relative_to(root)
out.mkdir(parents=True,exist_ok=False)
build=json.loads((root/args.build_summary).read_text())
if not build.get('passed'):raise SystemExit('Combined build/regression receipt is not passing')
artifact=build['artifacts']['lumen']
binary=(root/artifact['path']).resolve();binary.relative_to(root)
def sha(p):
    with Path(p).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
if sha(binary)!=artifact['sha256']:raise SystemExit('Immutable candidate bytes changed')
runner=Path('tools/apple-session-gpt6as/remote_native_lifecycle.py')
if sha(runner)!='b957f810f6f49a9133b57165af9e369e12b7514da4fab3c8d8d921bb01f08f1a':raise SystemExit('Reviewed workload/policy source changed')
started=time.monotonic();utc=lambda:datetime.now(timezone.utc).isoformat()
result={'startedUTC':utc(),'artifact':artifact,'buildSummary':args.build_summary,'buildSummarySHA256':sha(root/args.build_summary),
 'runnerSHA256':sha(runner),'userScrollCPUCeilingPercent':30,
 'cpuDefinition':'One-second cumulative process-tree CPU time deltas; 100% is one logical core. RSS sums resident process sets and can double-count shared pages.',
 'scope':'Visible native Mac browser with synthetic SDL wheel events and actual page navigations; isolated profile. This does not claim OS-input, iPad, or IPA validation.',
 'scenarios':[],'accepted':False}
def save():
    p=out/'summary.tmp';p.write_text(json.dumps(result,indent=2)+'\n');p.replace(out/'summary.json')
save()
try:
    for scenario,duration in [('scroll',155),('navigation',205)]:
        destination=out/scenario
        row={'name':scenario,'startedUTC':utc(),'output':str(destination.relative_to(root))};result['scenarios'].append(row);save()
        command=[sys.executable,str(runner),'--scenario',scenario,'--binary',artifact['path'],'--sha256',artifact['sha256'],
                 '--output',str(destination.relative_to(root)),'--scroll-cpu-ceiling','30']
        capture=None
        with (out/(scenario+'-runner.log')).open('x') as log:
            process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
            row['runnerPID']=process.pid;save()
            if scenario=='scroll':
                capture_log=(out/'window-capture.log').open('x')
                capture=subprocess.Popen([sys.executable,'tools/apple-session-gpt6as/capture_task_window.py','--targets',str((destination/'targets.json').relative_to(root)),
                    '--binary',artifact['path'],'--sha256',artifact['sha256'],'--helper','output/session-102900614/task-window-inventory',
                    '--output',str((destination/'native-window').relative_to(root))],stdout=capture_log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL)
            try:row['exitCode']=process.wait(timeout=duration)
            except subprocess.TimeoutExpired:
                process.terminate();row['timedOut']=True;row['exitCode']=process.wait(timeout=15)
            if capture is not None:
                try:row['captureExitCode']=capture.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    capture.terminate();row['captureTimedOut']=True;row['captureExitCode']=capture.wait(timeout=10)
                capture_log.close()
        receipt_path=destination/'receipt.json'
        if not receipt_path.is_file():raise RuntimeError('Native workload receipt missing: '+scenario)
        receipt=json.loads(receipt_path.read_text())
        row.update(finishedUTC=utc(),durationSeconds=receipt.get('duration_seconds'),accepted=receipt.get('accepted',False),
            receipt=str(receipt_path.relative_to(root)),receiptSHA256=sha(receipt_path),issues=receipt.get('issues',[]),
            phaseMetrics=receipt.get('phase_metrics'),settledRSSComparison=receipt.get('settled_rss_comparison'),nativeCounterComparison=receipt.get('native_counter_comparison'))
        capture_receipt=destination/'native-window/receipt.json'
        if capture_receipt.is_file():
            window=json.loads(capture_receipt.read_text());row['visibleWindow']={k:window.get(k) for k in ['captured','screenshot','screenshotSHA256','width','height','issue']}
        logs=(destination/'lumen.log').read_text(errors='replace')
        row['gpuErrors']=[line for line in logs.splitlines() if any(term in line for term in ['gpu: error','Validation Error','fatal signal','panicked at','panic in a function'])]
        save();print(json.dumps(row),flush=True)
        if not row['accepted'] or row['gpuErrors']:raise RuntimeError('Native resource gate failed: '+scenario)
    result['accepted']=all(r['accepted'] for r in result['scenarios']) and bool(result['scenarios'][0].get('visibleWindow',{}).get('captured'))
    if not result['accepted']:result['error']='Resource gates passed but visible native-window proof was not captured'
except Exception as exc:
    result['error']=str(exc)
finally:
    result['finishedUTC']=utc();result['durationSeconds']=round(time.monotonic()-started,3);save()
    print(json.dumps({'accepted':result['accepted'],'summary':str((out/'summary.json').relative_to(root)),'durationSeconds':result['durationSeconds'],'error':result.get('error')}),flush=True)
raise SystemExit(0 if result['accepted'] else 1)
