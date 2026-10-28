#!/usr/bin/env python3
"""Measured native build and deterministic byte-preservation regression gate."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--source-sha256', required=True)
parser.add_argument('--fixture-sha256', required=True)
parser.add_argument('--name', default='gpu-upload-green-1')
parser.add_argument('--variant', default='gpu-upload')
parser.add_argument('--asan', action='store_true')
args = parser.parse_args()
root = Path.cwd().resolve()
if not re.fullmatch(r'[a-z0-9-]+', args.name) or not re.fullmatch(r'[a-z0-9-]+', args.variant):
    raise SystemExit('Receipt and variant names must contain only lowercase letters, digits, and hyphens')
out = root/'output/session-102900614'/args.name
out.mkdir(parents=True, exist_ok=False)
utc = lambda: datetime.now(timezone.utc).isoformat()
def sha(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()
if sha(root/'src/gpu/gpu.c') != args.source_sha256:
    raise SystemExit('GPU source does not match the reviewed candidate')
if sha(root/'tests/test_gpu_present.c') != args.fixture_sha256:
    raise SystemExit('GPU test fixture changed after the RED run')
tracked = subprocess.check_output(['git','ls-files','-z','--','src','Makefile','ios'],text=True).split('\0')
source = {name:sha(root/name) for name in tracked if name and (root/name).is_file()}
(out/'compiled-source-sha256.json').write_text(json.dumps(source,indent=2)+'\n')
start = time.monotonic()
summary = {'startedUTC':utc(),'scope':'Native Metal upload-buffer candidate and deterministic GPU presenter regressions; CPU/idle/scroll/Apple-device acceptance is separate.',
           'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
           'compiledSourceManifest':str((out/'compiled-source-sha256.json').relative_to(root)),
           'gpuSourceSHA256':args.source_sha256,'fixtureSHA256':args.fixture_sha256,
           'build':'build-apple-load-20261029','prebuiltV8':'Reuses the same existing native Homebrew V8 as the paired baseline; no V8 rebuild.',
           'stages':[],'artifacts':{},'passed':False}
def save(): (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
def measured(name, command, maximum):
    prefix=out/name;t0=time.monotonic();started=utc()
    process=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(maximum),'--settle','0','--output',str(prefix.relative_to(root)),'--',*command],capture_output=True,text=True,timeout=maximum+35)
    (out/(name+'-collector.log')).write_text(process.stdout+process.stderr)
    monitor=json.loads(prefix.with_suffix('.json').read_text())
    log=prefix.with_suffix('.log').read_text(errors='replace')
    passed=monitor.get('natural_exit_code')==0 and not monitor.get('collector_error') and not monitor.get('ended_by_monitor')
    row={'name':name,'startedUTC':started,'finishedUTC':utc(),'durationSeconds':round(time.monotonic()-t0,3),'passed':passed,'command':command,
         'monitor':str(prefix.with_suffix('.json').relative_to(root)),'monitorSHA256':sha(prefix.with_suffix('.json')),
         'log':str(prefix.with_suffix('.log').relative_to(root)),'logSHA256':sha(prefix.with_suffix('.log')),
         'intervalCpuSamples':monitor.get('interval_cpu_samples'),'intervalCpuPeakPercent':monitor.get('interval_cpu_peak_percent'),'rssPeakMiB':monitor.get('rss_peak_mb'),
         'maximumSamplingGapSeconds':monitor.get('max_sampling_gap_s'),'resultLines':[line for line in log.splitlines() if 'GPU presentation:' in line],
         'measurementScope':'Short build/test observations; these are not idle or scrolling resource gates.'}
    summary['stages'].append(row);save();print(json.dumps(row),flush=True)
    return passed, log
build=summary['build']
built,_=measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',build+'/test_gpu_present',build+'/lumen'],120)
passed=False
if built:
    for name in ['lumen','test_gpu_present']:
        artifact=root/build/name;digest=sha(artifact)
        snapshot=root/build/(name+'-'+args.variant+'-'+digest[:16])
        if snapshot.exists(): raise SystemExit('Candidate snapshot name already exists')
        shutil.copy2(artifact,snapshot);snapshot.chmod(0o555)
        if sha(snapshot)!=digest: raise SystemExit('Immutable artifact copy changed')
        summary['artifacts'][name]={'path':str(snapshot.relative_to(root)),'sha256':digest,'bytes':snapshot.stat().st_size}
    save()
    ok,log=measured('gpu-presentation',[summary['artifacts']['test_gpu_present']['path']],20)
    passed=ok and bool(re.search(r'GPU presentation: \d+ checks, 0 failures',log))
    if passed and args.asan:
        asan_build='build-apple-gpu-asan-102900614'
        asan_ok,_=measured('asan-build',['make','-j2','BUILD='+asan_build,'MODE=debug','RTC=1','WT=1',
                              'CC=clang -fsanitize=address -fno-omit-frame-pointer',asan_build+'/test_gpu_present'],120)
        if asan_ok:
            asan_artifact=root/asan_build/'test_gpu_present';asan_digest=sha(asan_artifact)
            summary['artifacts']['test_gpu_present_asan']={'path':str(asan_artifact.relative_to(root)),
                                                         'sha256':asan_digest,'bytes':asan_artifact.stat().st_size}
            asan_ok,asan_log=measured('asan-callback-lifetime',[str(asan_artifact.relative_to(root))],30)
            asan_ok=asan_ok and bool(re.search(r'GPU presentation: \d+ checks, 0 failures',asan_log))
            if sha(asan_artifact)!=asan_digest: raise SystemExit('ASan fixture changed during execution')
        passed=passed and asan_ok
for name,expected in source.items():
    if sha(root/name)!=expected: raise SystemExit('Native source changed during compilation/verification: '+name)
if sha(root/'tests/test_gpu_present.c') != args.fixture_sha256: raise SystemExit('GPU fixture changed during verification')
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3),passed=built and passed)
save();print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
