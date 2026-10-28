#!/usr/bin/env python3
"""Retry only two pinned transfer WPT variants with a renewed official fixture.

The immutable successful original1850 report is revalidated and reused. The
failed first orchestration and all historical fixture files remain untouched.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import time
import traceback

root=Path.cwd().resolve()
out=root/'output/session-102900614/message-transfer-upstream-2'
out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
binary=root/'build-apple-load-20261029/lumen-message-transfer-733943aa9496d134'
expected='733943aa9496d1342dd9d90a76146d0433b24c9ef7848df5860881e57c2ac848'
began=time.monotonic()
receipt={'startedUTC':utc(),'binary':str(binary.relative_to(root)),'binarySHA256':expected,
         'scope':'Two unchanged upstream transfer variants plus revalidated original six-file1850 receipt; never full WPT',
         'stages':[],'passed':False}
def save(): (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
save()
try:
    if sha(binary)!=expected:raise ValueError('Native binary changed')
    prior_path=root/'output/session-102900614/message-transfer-upstream-1/receipt.json'
    prior=json.loads(prior_path.read_text())
    if prior.get('binarySHA256')!=expected:raise ValueError('Original report belongs to another binary')
    originals=[r for r in prior['stages'] if r['name']=='original']
    if len(originals)!=1:raise ValueError('Original report is missing or ambiguous')
    original=originals[0]
    if not original.get('passed') or original['files']!=6 or original['fullyPassingFiles']!=6 or original['assertions']!={'PASS':1850}:raise ValueError('Original six-file gate is not valid')
    if sha(root/original['report'])!=original['reportSHA256']:raise ValueError('Original report changed')
    copied=root/'output/session-20261029/native-qa-kit-v1/source/build/lumen-session102900614-message-transfer-1'
    if sha(copied)!=expected:raise ValueError('Original tested kit binary changed')
    receipt['originalReused']={**original,'receipt':str(prior_path.relative_to(root)),'receiptSHA256':sha(prior_path)}
    save()
    priority=root/'tools/apple-session-gpt6as/run_priority_wpt_10290244.py'
    spec=importlib.util.spec_from_file_location('unchanged_priority',priority);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.BASE = root/'output/session-102900614/wpt-fixture-1'
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
    receipt.update(passed=original['passed'] and targeted['passed'],binarySHA256After=sha(binary),kitBinarySHA256After=sha(copied))
    if receipt['binarySHA256After']!=expected or receipt['kitBinarySHA256After']!=expected:receipt['passed']=False

except BaseException as error:
    receipt['error']=type(error).__name__+': '+str(error)
    (out/'exception.log').write_text(traceback.format_exc())
finally:
    receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3))
    save()
    print(json.dumps(receipt),flush=True)
raise SystemExit(0 if receipt['passed'] else 1)
