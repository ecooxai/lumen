#!/usr/bin/env python3
"""Run the unchanged fixed scroll gate, owned-window capture and pixel comparison."""
from datetime import datetime, timezone
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

parser=argparse.ArgumentParser()
parser.add_argument('--name', default='scroll-gpu-upload')
parser.add_argument('--build-name', default='gpu-upload-green-1')
args=parser.parse_args()
if any(not name or any(char not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for char in name) for name in (args.name,args.build_name)):
    raise SystemExit('Receipt names must contain only lowercase letters, digits, and hyphens')
root=Path.cwd().resolve()
control=root/'output/session-102900614'/(args.name+'-control')
control.mkdir(parents=True,exist_ok=False)
out=root/'output/session-102900614'/args.name
utc=lambda:datetime.now(timezone.utc).isoformat()
def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
build=json.loads((root/'output/session-102900614'/args.build_name/'summary.json').read_text())
if not build.get('passed'):raise SystemExit('GPU build and deterministic tests are not green')
artifact=build['artifacts']['lumen'];binary=root/artifact['path']
if sha(binary)!=artifact['sha256']:raise SystemExit('GPU candidate hash changed')
baseline_sources=json.loads((root/'output/session-102900614/message-transfer-green-1/compiled-source-sha256.json').read_text())
candidate_sources=json.loads((root/build['compiledSourceManifest']).read_text())
changes=[name for name in sorted(baseline_sources.keys()|candidate_sources.keys()) if baseline_sources.get(name)!=candidate_sources.get(name)]
if changes!=['src/gpu/gpu.c']:raise SystemExit('GPU-only source comparison changed: '+repr(changes))
if not (root/'output/session-102900614/task-window-inventory').is_file(): raise SystemExit('Precompiled owned-window helper is missing')
runner=root/'tools/apple-session-gpt6as/remote_native_lifecycle.py'
runner_sha='61f919ee0be6e2a972237d7ed7cf9e71f827ff3d9f6d2a8336cebca9fbd9cfb7'
if sha(runner)!=runner_sha:raise SystemExit('The fixed resource workload changed')
start=time.monotonic();summary={'startedUTC':utc(),'binary':artifact,'changedCompiledSources':changes,'runnerSHA256':runner_sha,
 'scope':'The original 115s native scroll workload, 1875 inputs, 1Hz telemetry and thresholds are unchanged. An external PID-scoped window capture may perturb one candidate interval; it never alters application input/render cadence.',
 'resourceAccepted':False,'pixelIdentical':False,'nativeWindowCaptured':False}
def save():(control/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
save();capture=None
with (control/'runner.log').open('w') as runlog,(control/'window-capture.log').open('w') as capturelog:
    run=subprocess.Popen([sys.executable,str(runner),'--scenario','scroll','--binary',artifact['path'],'--sha256',artifact['sha256'],'--output',str(out.relative_to(root))],stdout=runlog,stderr=subprocess.STDOUT)
    capture=subprocess.Popen([sys.executable,'tools/apple-session-gpt6as/capture_task_window.py','--targets',str((out/'targets.json').relative_to(root)),
                             '--binary',artifact['path'],'--sha256',artifact['sha256'],'--helper','output/session-102900614/task-window-inventory',
                             '--output',str((out/'native-window-capture').relative_to(root))],stdout=capturelog,stderr=subprocess.STDOUT)
    try:summary['runnerExitCode']=run.wait(timeout=150)
    except subprocess.TimeoutExpired:
        run.terminate();summary['runnerTimedOut']=True;summary['runnerExitCode']=run.wait(timeout=15)
    try:summary['captureExitCode']=capture.wait(timeout=15)
    except subprocess.TimeoutExpired:
        capture.terminate();summary['captureTimedOut']=True;summary['captureExitCode']=capture.wait(timeout=10)
receipt_path=out/'receipt.json'
if receipt_path.is_file():
    receipt=json.loads(receipt_path.read_text());summary['resourceReceipt']=str(receipt_path.relative_to(root));summary['resourceReceiptSHA256']=sha(receipt_path)
    summary['resourceAccepted']=receipt.get('accepted',False);summary['resourceIssues']=receipt.get('issues',[]);summary['phaseMetrics']=receipt.get('phase_metrics',{})
window_receipt=out/'native-window-capture/receipt.json'
if window_receipt.is_file():
    window=json.loads(window_receipt.read_text());summary['nativeWindowCaptured']=window.get('captured',False);summary['windowCaptureReceipt']=str(window_receipt.relative_to(root));summary['windowCaptureIssue']=window.get('issue')
command=[sys.executable,'tools/monitor_process.py','--duration','30','--settle','0','--output',str((control/'pixel-monitor').relative_to(root)),'--',
         sys.executable,'tools/apple-session-gpt6as/compare_scroll_pixels.py','--baseline','output/session-102900614/scroll-transfer-baseline','--candidate',str(out.relative_to(root)),
         '--output',str((out/'pixel-comparison.json').relative_to(root))]
with (control/'pixel-command.log').open('w') as log:
    compared=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=60)
summary['pixelCommandExitCode']=compared.returncode
pixels=out/'pixel-comparison.json'
if pixels.is_file():summary['pixelIdentical']=json.loads(pixels.read_text()).get('passed',False);summary['pixelComparison']=str(pixels.relative_to(root))
lumen_log=out/'lumen.log'
if lumen_log.is_file():
    error_terms=('gpu: error','Validation Error','fatal signal','panicked at','panic in a function')
    summary['gpuErrors']=[line for line in lumen_log.read_text(errors='replace').splitlines() if any(term in line for term in error_terms)]
else:summary['gpuErrors']=['Native application log is missing']
summary['gpuClean']=not summary['gpuErrors']
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3))
save();print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['resourceAccepted'] and summary['pixelIdentical'] and summary['gpuClean'] else 1)
