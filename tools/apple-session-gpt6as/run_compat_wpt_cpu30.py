#!/usr/bin/env python3
"""Run the unchanged frozen WPT groups on one immutable native executable."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time

parser=argparse.ArgumentParser()
parser.add_argument('--binary',type=Path,required=True)
parser.add_argument('--sha256',required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--groups',nargs='+',default=['encoding-core','encoding-streams'])
parser.add_argument('--skip-original',action='store_true')
args=parser.parse_args()
root=Path.cwd().resolve()
if root.name!='lumen':raise SystemExit('Run from the Lumen repository')
if not args.output.resolve().is_relative_to(root):raise SystemExit('Output escapes project')
if hashlib.sha256(args.binary.read_bytes()).hexdigest()!=args.sha256:raise SystemExit('Artifact hash mismatch')
out=args.output
out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat()
began=time.monotonic()
receipt={'task':'20261029-cpu30','startedUTC':utc(),'binary':str(args.binary),'binarySHA256':args.sha256,
         'scope':'Frozen selected upstream WPT, not full conformance. All1515 variants remain in the cohort ledger.',
         'stages':[],'complete':False,'sameArtifact':True}
def save():
    temp=out/'summary.json.tmp';temp.write_text(json.dumps(receipt,indent=2)+'\n');temp.replace(out/'summary.json')
save()
runner='tools/apple-session-gpt6as/run_task_wpt_102900614.py'
plan=out/'cohort'/'cohort.json'
def run(name,command,timeout):
    start=time.monotonic();row={'name':name,'command':command,'startedUTC':utc()}
    receipt['stages'].append(row);save()
    monitor=[sys.executable,'tools/monitor_process.py','--duration',str(timeout),'--settle','0','--output',str(out/name),'--',*command]
    with (out/(name+'-driver.log')).open('w') as log:
        process=subprocess.Popen(monitor,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:row['exitCode']=process.wait(timeout=timeout+20)
        except subprocess.TimeoutExpired:
            process.terminate();row['outerTimeout']=True
            try:row['exitCode']=process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();row['exitCode']=process.wait()
    row.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3))
    if (out/(name+'.json')).exists():
        row['monitorReceipt']=str(out/(name+'.json'))
        metric=json.loads((out/(name+'.json')).read_text())
        row['naturalExitCode']=metric.get('natural_exit_code')
        row['collectorError']=metric.get('collector_error')
        row['endedByMonitor']=metric.get('ended_by_monitor')
        row['intervalCpuSamples']=metric.get('interval_cpu_samples')
        row['intervalCpuPeakPercent']=metric.get('interval_cpu_peak_percent')
        row['rssPeakMiB']=metric.get('rss_peak_mb')
        row['maximumSamplingGapSeconds']=metric.get('max_sampling_gap_s')
    row['passed']=row.get('naturalExitCode')==0 and not row.get('collectorError') and not row.get('endedByMonitor') and not row.get('outerTimeout')
    save()
    if row['exitCode']!=0 or not row['passed']:raise RuntimeError(name+' failed; inspect its driver and monitor logs')
try:
    run('prepare',[sys.executable,runner,'prepare','--binary',str(args.binary),'--sha256',args.sha256,'--output',str(plan.parent)],45)
    if not args.skip_original:
        run('original-six',[sys.executable,'tools/apple-session-gpt6as/run_frozen_original_wpt_cpu30.py',
            '--binary',str(args.binary),'--sha256',args.sha256,
            '--output',str(plan.parent/'original-six-receipt.json'),'--label',out.name],260)
    for group in args.groups:
        run(group,[sys.executable,runner,'run','--plan',str(plan),'--group',group,'--max-seconds','240'],280)
    native=json.loads((plan.parent/'priority-summary.json').read_text())
    receipt['cohortSummary']={k:native[k] for k in ['scheduled','run','fullyPassing','unrun','passingPercentOfRun','assertions','harnessStatuses','groups']}
    receipt['failures']=native['failures']
    receipt['complete']=all(native['groups'][g]['unrun']==0 for g in args.groups)
except Exception as error:
    receipt['error']=type(error).__name__+': '+str(error)
finally:
    receipt['binarySHA256After']=hashlib.sha256(args.binary.read_bytes()).hexdigest()
    receipt['sameArtifact']=receipt['binarySHA256After']==args.sha256
    receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3));save()
print(json.dumps({k:v for k,v in receipt.items() if k!='failures'}),flush=True)
raise SystemExit(0 if receipt['complete'] and receipt['sameArtifact'] and not receipt.get('error') else 1)
