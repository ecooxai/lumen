#!/usr/bin/env python3
"""Unchanged original WPT selection plus two targeted upstream transfer failures.

The diagnostic two-file run supplements the fixed1515 inventory; it does not
replace, reduce, or mutate that inventory. No Chrome control is executed here.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

root=Path.cwd().resolve()
out=root/'output/session-102900614/message-transfer-upstream-1'
out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
binary=root/'build-apple-load-20261029/lumen-message-transfer-733943aa9496d134'
expected='733943aa9496d1342dd9d90a76146d0433b24c9ef7848df5860881e57c2ac848'
if sha(binary)!=expected:raise SystemExit('Native binary changed')
began=time.monotonic()
receipt={'startedUTC':utc(),'binary':str(binary.relative_to(root)),'binarySHA256':expected,'scope':'Unchanged six-file1850-assertion selection plus two unchanged pinned encoding variants; not full WPT','stages':[],'passed':False}
def save(): (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
def stage(name,command,cwd,timeout):
    started=time.monotonic();row={'name':name,'startedUTC':utc(),'command':command};receipt['stages'].append(row);save()
    with (out/(name+'.log')).open('w') as log:
        run=subprocess.run(command,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    row.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-started,3),exitCode=run.returncode);save()
    return row

kit=root/'output/session-20261029/native-qa-kit-v1/source'
label='session102900614-message-transfer-1'
copied=kit/'build'/('lumen-'+label)
if copied.exists():raise SystemExit('Native kit copy already exists')
shutil.copy2(binary,copied);copied.chmod(0o555)
if sha(copied)!=expected:raise SystemExit('Native kit copy mismatch')
native_output='output/native-wpt-'+label
original=stage('original',[sys.executable,'tools/apple-session-gpt6as/probe_native_wpt.py','--binary',str(copied.relative_to(kit)),'--expected-sha256',expected,'--output',native_output,'--seconds-per-test','25'],kit,240)
original_report=kit/native_output/'wpt-report.json'
document=json.loads(original_report.read_text());rows=document.get('results',[])
assertions=Counter(subtest.get('status') for row in rows for subtest in row.get('subtests',[]))
original.update(report=str(original_report.relative_to(root)),reportSHA256=sha(original_report),files=len(rows),assertions=dict(assertions),fullyPassingFiles=sum(row.get('status') in ('OK','PASS') and bool(row.get('subtests')) and all(s.get('status')=='PASS' for s in row['subtests']) for row in rows))
original['passed']=original['exitCode']==0 and original['files']==6 and original['fullyPassingFiles']==6 and assertions==Counter(PASS=1850)
save()

priority=root/'tools/apple-session-gpt6as/run_priority_wpt_10290244.py'
spec=importlib.util.spec_from_file_location('unchanged_priority',priority);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
frozen,categories,environment,hashes,runner=module.current_inputs()
paths=['/encoding/encodeInto.any.html','/encoding/encodeInto.any.worker.html']
selected=[row for row in categories['encoding'] if row['test'] in paths]
if len(selected)!=2 or {row['test'] for row in selected}!=set(paths):raise SystemExit('Pinned diagnostic variants missing')
for name in tuple(os.environ):
    if name.startswith('LUMEN_'):del os.environ[name]
os.environ.update(environment['nativeEnvironment'])
options=argparse.Namespace(inventory=Path(hashes['inventory']['path']),engine='lumen',binary=binary,output=out/'encoding-transfer',types='testharness',prefix=paths,offset=0,limit=None,max_seconds=120,server_config=Path(hashes['server_config']['path']),fixture_manifest=Path(hashes['fixture_readiness']['path']),strict_infrastructure=False,timeout=15,long_timeout=65,origin=environment['origins']['http'],secure_origin=environment['origins']['https'],h2_origin=environment['origins']['h2'],chromium_arg=environment['chromiumArguments'],chromium_headful=True,resume=False)
start=time.monotonic();targeted={'name':'encoding-transfer','startedUTC':utc(),'upstreamRevision':frozen['wpt_revision'],'fullFrozenScheduleSHA256':frozen['schedule_sha256'],'fullFrozenVariantCount':1515,'diagnosticVariants':paths,'selectedScheduleSHA256':module.digest(selected),'runnerSHA256':hashes['runner']['sha256']};receipt['stages'].append(targeted);save()
runner.run_command(options)
records=[json.loads(line) for line in (out/'encoding-transfer/results.jsonl').read_text().splitlines() if line.strip()]
counts=Counter(sub.get('status') for row in records for sub in row.get('subtests',[]))
targeted.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3),files=len(records),assertions=dict(counts),fullyPassingFiles=sum(row.get('status') in ('OK','PASS') and bool(row.get('subtests')) and all(s.get('status')=='PASS' for s in row['subtests']) for row in records),resultsSHA256=sha(out/'encoding-transfer/results.jsonl'))
named=[sub for row in records for sub in row.get('subtests',[]) if sub.get('name')=='encodeInto() and a detached output buffer']
targeted['detachedBufferAssertions']=named
targeted['passed']=len(records)==2 and targeted['fullyPassingFiles']==2 and len(named)==2 and all(sub.get('status')=='PASS' for sub in named)
receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3),passed=original['passed'] and targeted['passed'],binarySHA256After=sha(binary),kitBinarySHA256After=sha(copied))
if receipt['binarySHA256After']!=expected or receipt['kitBinarySHA256After']!=expected:receipt['passed']=False
save();print(json.dumps(receipt),flush=True)
raise SystemExit(0 if receipt['passed'] else 1)
