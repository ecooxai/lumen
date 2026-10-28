#!/usr/bin/env python3
"""Bind a fresh frozen1515 cohort to one successful native codec build."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

parser=argparse.ArgumentParser()
parser.add_argument('--summary',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
summary=json.loads(args.summary.read_text())
if summary.get('passed') is not True:raise SystemExit('Native regression summary has not passed')
artifact=summary['artifacts']['lumen']
binary=Path(artifact['snapshot'])
if sha(binary)!=artifact['sha256']:raise SystemExit('Frozen native artifact changed')
tool=Path('tools/apple-session-gpt6as/run_task_wpt_102900614.py')
if sha(tool)!='2259c29f2d3d05545e310739131256d7bd73e319bc522096926779fa3dff51f3':
    raise SystemExit('Frozen upstream cohort runner changed')
if args.output.exists():raise SystemExit('Fresh codec cohort output must not already exist')
utc=lambda:datetime.now(timezone.utc).isoformat()
start=time.monotonic()
receipt={'startedUTC':utc(),'buildSummary':str(args.summary),'buildSummarySHA256':sha(args.summary),
         'binarySHA256':artifact['sha256'],'stages':[],'completed':False,'error':None}
stages=[('prepare',['prepare','--binary',str(binary),'--sha256',artifact['sha256'],'--output',str(args.output)],45),
        ('original-six',['original','--plan',str(args.output/'cohort.json')],280),
        ('encoding-streams',['run','--plan',str(args.output/'cohort.json'),'--group','encoding-streams','--max-seconds','180'],270),
        ('encoding-core',['run','--plan',str(args.output/'cohort.json'),'--group','encoding-core','--max-seconds','180'],270)]
try:
    for name,arguments,timeout in stages:
        began=time.monotonic();row={'stage':name,'startedUTC':utc()}
        command=[sys.executable,str(tool),*arguments]
        row['command']=command
        run=subprocess.run(command,timeout=timeout)
        row.update(finishedUTC=utc(),seconds=round(time.monotonic()-began,3),exitCode=run.returncode)
        receipt['stages'].append(row)
        print(json.dumps(row),flush=True)
        if run.returncode:raise RuntimeError('Execution failed at '+name)
    receipt['completed']=True
except BaseException as error:
    receipt['error']=type(error).__name__+': '+str(error)
    raise
finally:
    receipt.update(finishedUTC=utc(),seconds=round(time.monotonic()-start,3))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    destination=args.output.parent/(args.output.name+'-invocation.json')
    destination.write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'receipt':str(destination),'completed':receipt['completed'],'seconds':receipt['seconds']}),flush=True)
