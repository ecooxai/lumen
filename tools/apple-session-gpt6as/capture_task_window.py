#!/usr/bin/env python3
"""Capture one verified Lumen process window, without changing TCC permissions.

The inventory helper must already be compiled. Capture is supplementary visual
proof: fixed unprofiled lifecycle telemetry remains the performance gate.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time

p=argparse.ArgumentParser()
p.add_argument('--targets',type=Path,required=True)
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--sha256',required=True)
p.add_argument('--helper',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--phase',default='scrolling')
p.add_argument('--wait',type=float,default=45)
p.add_argument('--phase-delay',type=float,default=10)
a=p.parse_args();root=Path.cwd().resolve()
for path in (a.targets,a.binary,a.helper,a.output):
    if not path.resolve().is_relative_to(root): raise SystemExit('Every path must stay inside the task repository')
a.targets=a.targets.resolve();a.binary=a.binary.resolve();a.helper=a.helper.resolve();a.output=a.output.resolve()
if not 0 <= a.wait <= 90 or not 0 <= a.phase_delay <= 20: raise SystemExit('Invalid bounded wait')
if a.output.exists(): raise SystemExit('Capture output already exists')
utc=lambda:datetime.now(timezone.utc).isoformat()
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
receipt={'startedUTC':utc(),'scope':'One PID-verified native Lumen window only; no whole-desktop capture, permission request, or security change.','binary':str(a.binary),'binarySHA256':a.sha256,'captured':False}
began=time.monotonic()
try:
    if sha(a.binary)!=a.sha256: raise RuntimeError('Application hash mismatch')
    if not a.helper.is_file(): raise RuntimeError('Precompiled window inventory helper missing')
    receipt['helperSHA256']=sha(a.helper)
    target=None;deadline=time.monotonic()+a.wait
    while time.monotonic()<deadline:
        if a.targets.is_file():
            data=json.loads(a.targets.read_text())
            groups=[g for g in data.get('groups',[]) if g.get('name')=='lumen' and len(g.get('pids',[]))==1]
            if data.get('phase')==a.phase and len(groups)==1:
                target=int(groups[0]['pids'][0]);break
        time.sleep(.2)
    if target is None: raise RuntimeError('Owned application never entered requested phase before deadline')
    time.sleep(a.phase_delay)
    def identity():
        command=subprocess.check_output(['/bin/ps','-ww','-p',str(target),'-o','comm='],text=True,timeout=5).strip()
        started=subprocess.check_output(['/bin/ps','-p',str(target),'-o','lstart='],text=True,timeout=5).strip()
        if Path(command).resolve()!=a.binary.resolve(): raise RuntimeError('PID executable no longer matches immutable task application')
        return {'pid':target,'executable':command,'startIdentity':started}
    before=identity();receipt['process']=before
    inventory=subprocess.run([str(a.helper.resolve()),str(target)],capture_output=True,text=True,timeout=5,check=True)
    windows=json.loads(inventory.stdout);receipt['windowInventory']=windows
    if not windows.get('screenCapturePermission'):
        raise RuntimeError('Existing Screen Recording permission does not authorize capture; no permission request was made')
    visible=windows.get('windows',[])
    if not visible: raise RuntimeError('The verified process has no visible ordinary window')
    selected=max(visible,key=lambda row:row['width']*row['height']);receipt['selectedWindow']=selected
    a.output.mkdir(parents=True,exist_ok=False)
    screenshot=a.output/'gpt6-astra-pro_builtin-agents_devinmac3d_lumen-window.png'
    captured=subprocess.run(['/usr/sbin/screencapture','-x','-o','-l',str(selected['id']),str(screenshot.resolve())],capture_output=True,text=True,timeout=8)
    receipt['captureExitCode']=captured.returncode;receipt['captureStderr']=captured.stderr[:1500]
    if captured.returncode: raise RuntimeError('Owned-window screen capture failed')
    after=identity();receipt['processAfterCapture']=after
    if after!=before: raise RuntimeError('Application identity changed while capturing')
    with screenshot.open('rb') as stream: header=stream.read(33)
    if len(header)!=33 or header[:8]!=b'\x89PNG\r\n\x1a\n' or header[12:16]!=b'IHDR': raise RuntimeError('Capture is not a complete PNG header')
    width,height=struct.unpack('>II',header[16:24])
    if width<200 or height<100: raise RuntimeError('Captured window is unexpectedly small')
    receipt.update(captured=True,screenshot=str(screenshot.relative_to(root)),screenshotSHA256=sha(screenshot),width=width,height=height)
except Exception as error:
    receipt['issue']=type(error).__name__+': '+str(error)
receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3))
a.output.mkdir(parents=True,exist_ok=True)
(a.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt))
raise SystemExit(0 if receipt['captured'] else 1)
