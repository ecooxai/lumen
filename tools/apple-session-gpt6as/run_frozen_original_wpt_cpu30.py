#!/usr/bin/env python3
"""Fresh task-scoped invocation of the unchanged original six-file native WPT gate."""
import argparse
from collections import Counter
from datetime import datetime,timezone
import hashlib,json,re,shutil,subprocess,sys,time
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--sha256',required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--label',required=True)
a=p.parse_args()
root=Path.cwd().resolve()
if not re.fullmatch(r'[a-z0-9-]+',a.label):raise SystemExit('Unsafe gate label')
if not a.output.resolve().is_relative_to(root):raise SystemExit('Receipt must stay within project')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if sha(a.binary)!=a.sha256:raise SystemExit('Executable changed')
kit=root/'output/session-20261029/native-qa-kit-v1/source'
label='task20261029-cpu30-'+a.label+'-'+a.sha256[:16]
copy=kit/'build'/('lumen-'+label)
if copy.exists():
    if sha(copy)!=a.sha256:raise SystemExit('Immutable QA copy differs')
else:shutil.copy2(a.binary,copy);copy.chmod(0o555)
native_out='output/native-wpt-'+label
if (kit/native_out).exists() or a.output.exists():raise SystemExit('Fresh gate output already exists')
a.output.parent.mkdir(parents=True,exist_ok=True)
utc=lambda:datetime.now(timezone.utc).isoformat()
began=time.monotonic()
command=[sys.executable,'tools/apple-session-gpt6as/probe_native_wpt.py','--binary',str(copy.relative_to(kit)),
         '--expected-sha256',a.sha256,'--output',native_out,'--seconds-per-test','25']
receipt={'task':'20261029-cpu30','startedUTC':utc(),'binarySHA256':a.sha256,'command':command,
         'scope':'Unmodified original six-file upstream gate,1850 assertions; distinct from frozen1515-variant priority cohort.','passed':False}
try:
    run=subprocess.run(command,cwd=kit,stdout=sys.stdout,stderr=sys.stderr,timeout=240)
    report=kit/native_out/'wpt-report.json'
    rows=json.loads(report.read_text()).get('results',[])
    counts=Counter(s['status'] for r in rows for s in r.get('subtests',[]))
    full=sum(r.get('status') in ('OK','PASS') and bool(r.get('subtests')) and all(s['status']=='PASS' for s in r['subtests']) for r in rows)
    receipt.update(exitCode=run.returncode,report=str(report),reportSHA256=sha(report),files=len(rows),fullyPassing=full,
                   assertions=dict(counts),passed=run.returncode==0 and len(rows)==6 and full==6 and counts==Counter(PASS=1850))
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
finally:
    receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3),binarySHA256After=sha(a.binary))
    receipt['passed']=receipt['passed'] and receipt['binarySHA256After']==a.sha256
    a.output.write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt),flush=True)
raise SystemExit(0 if receipt['passed'] else 1)
