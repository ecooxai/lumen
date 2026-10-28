#!/usr/bin/env python3
"""Measured incremental native build and transfer/codec/worker regression gates."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

root = Path.cwd()
out = root/'output/session-102900614/message-transfer-green-1'
out.mkdir(parents=True, exist_ok=False)
started = time.monotonic()
utc = lambda: datetime.datetime.now(datetime.timezone.utc).isoformat()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
patch = json.loads((root/'output/session-102900614/message-transfer-candidate-1/patch-receipt.json').read_text())
for name, expected in patch['after'].items():
    if sha(root/name) != expected: raise SystemExit('Candidate source changed: '+name)
if sha(root/'tests/test_message_transfer.py') != patch['fixtureSHA256']:
    raise SystemExit('The measured RED fixture changed')
tracked = subprocess.check_output(['git','ls-files','-z','--','src','Makefile','ios'],text=True).split('\0')
source = {name:sha(root/name) for name in tracked if name and (root/name).is_file()}
(out/'compiled-source-sha256.json').write_text(json.dumps(source,indent=2)+'\n')
summary = {'startedUTC':utc(),'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
           'compiledSourceManifest':str((out/'compiled-source-sha256.json').relative_to(root)),
           'build':'build-apple-load-20261029','rtcEnabled':True,'webtransportEnabled':True,
           'prebuiltV8':'Reuse the existing platform-matched Makefile dependency; no V8 build requested.',
           'stages':[],'artifacts':{},'passed':False}

def measured(name, command, duration, marker=None):
    t0=time.monotonic();prefix=out/name;start=utc()
    run=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(duration),'--settle','0','--output',str(prefix.relative_to(root)),'--',*command],text=True,capture_output=True,timeout=duration+40)
    (out/(name+'-collector.log')).write_text(run.stdout+run.stderr)
    report=json.loads(prefix.with_suffix('.json').read_text())
    log=prefix.with_suffix('.log').read_text(errors='replace')
    passed=report.get('natural_exit_code')==0 and not report.get('collector_error') and not report.get('ended_by_monitor') and (marker is None or marker in log)
    row={'stage':name,'passed':passed,'startedUTC':start,'finishedUTC':utc(),'durationSeconds':round(time.monotonic()-t0,3),'command':command,
         'monitor':str(prefix.with_suffix('.json').relative_to(root)),'naturalExitCode':report.get('natural_exit_code'),
         'endedByMonitor':report.get('ended_by_monitor'),'intervalCpuSamples':report.get('interval_cpu_samples'),
         'intervalCpuPeakPercent':report.get('interval_cpu_peak_percent'),'rssPeakMiB':report.get('rss_peak_mb'),
         'resultLines':[line for line in log.splitlines() if 'RESULTS' in line or '_RECEIPT ' in line],
         'performanceNote':'Short assertion runs do not establish an idle/performance verdict.'}
    summary['stages'].append(row)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({'stage':name,'passed':passed,'seconds':row['durationSeconds'],'results':row['resultLines']}),flush=True)
    return passed

build=summary['build']
targets=['lumen','jsrun','test_clone_brand','test_worker_lifecycle','test_worker_security']
built=measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+name for name in targets]],900)
if built:
    for name in targets:
        binary=root/build/name;digest=sha(binary)
        snapshot=root/build/(name+'-message-transfer-'+digest[:16])
        shutil.copy2(binary,snapshot)
        if sha(snapshot)!=digest: raise SystemExit('Immutable artifact copy mismatch')
        summary['artifacts'][name]={'path':str(binary.relative_to(root)),'snapshot':str(snapshot.relative_to(root)),'sha256':digest,'bytes':binary.stat().st_size}
    js=summary['artifacts']['jsrun']
    measured('message-transfer',[sys.executable,'tests/test_message_transfer.py','--binary',js['snapshot']],35,'RESULTS message-transfer PASS (68/68 checks)')
    measured('text-codec',[sys.executable,'tests/test_text_codec.py','--binary',js['snapshot']],35,'TEXT_CODEC_RECEIPT ')
    measured('html-clone-brands',[sys.executable,'tests/test_html_interface_clone_brands.py','--binary',js['snapshot'],'--expected-sha256',js['sha256']],35,'RESULTS html-interface-clone-brands PASS')
    for name in targets[2:]:
        profile=out/(name+'-profile');profile.mkdir()
        measured(name,['env','LUMEN_PROFILE_DIR='+str(profile),str(root/summary['artifacts'][name]['snapshot'])],90)
    measured('worker-ports',['env','LUMEN_CONSOLE=1','LUMEN_PROFILE_DIR='+str(out/'worker-ports-profile'),str(root/js['snapshot']),str(root/'tests/js/worker-ports.html'),'1500'],15,'RESULTS worker-ports blobWorker:PASS portsDelivered:PASS portRoundTrip:PASS')
for name, expected in source.items():
    if sha(root/name)!=expected: raise SystemExit('Compiled source changed during gate execution: '+name)
summary['passed']=built and all(row['passed'] for row in summary['stages'])
summary['finishedUTC']=utc();summary['durationSeconds']=round(time.monotonic()-started,3)
summary['scope']='Native transfer regression and existing clone/worker/codec regressions. Unchanged upstream WPT and final native GUI resource acceptance remain separate gates.'
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
