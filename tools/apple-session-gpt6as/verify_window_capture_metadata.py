#!/usr/bin/env python3
"""Add file metadata evidence without rewriting an earlier capture failure."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import time
import zlib

parser=argparse.ArgumentParser()
parser.add_argument('--capture-dir',type=Path,required=True)
args=parser.parse_args();root=Path.cwd().resolve();directory=args.capture_dir.resolve()
if not directory.is_relative_to(root):raise SystemExit('Capture directory must be project owned')
original=directory/'receipt.json';output=directory/'metadata-verification.json'
if output.exists():raise SystemExit('Supplemental receipt already exists')
utc=lambda:datetime.now(timezone.utc).isoformat();start=time.monotonic()
sha=lambda data:hashlib.sha256(data).hexdigest()
receipt={'startedUTC':utc(),'scope':'Supplemental verification of the existing PID-scoped screenshot file and recorded pre-capture metadata. The original failed receipt is preserved; no after-capture PID identity is inferred or fabricated.',
         'captureFileVerified':False,'postCaptureProcessReceiptPresent':False}
try:
    raw=original.read_bytes();data=json.loads(raw)
    receipt.update(originalReceipt=str(original.relative_to(root)),originalReceiptSHA256=sha(raw),originalCaptured=data.get('captured'),originalIssue=data.get('issue'),
                   recordedCaptureExitCode=data.get('captureExitCode'),recordedProcessBefore=data.get('process'),recordedWindow=data.get('selectedWindow'))
    if data.get('captureExitCode')!=0:raise RuntimeError('Original capture command did not report success')
    before=data.get('process',{});inventory=data.get('windowInventory',{});selected=data.get('selectedWindow',{})
    if not inventory.get('screenCapturePermission') or inventory.get('pid')!=before.get('pid') or selected not in inventory.get('windows',[]):
        raise RuntimeError('Recorded pre-capture PID/window authorization is incomplete')
    binary=(root/data['binary']).resolve()
    if not binary.is_relative_to(root):raise RuntimeError('Binary path is not project owned')
    with binary.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    if digest!=data['binarySHA256']:raise RuntimeError('Immutable application hash changed')
    screenshot=directory/'gpt6-astra-pro_builtin-agents_devinmac3d_lumen-window.png'
    png=screenshot.read_bytes()
    if png[:8]!=b'\x89PNG\r\n\x1a\n':raise RuntimeError('Screenshot PNG signature is invalid')
    offset=8;chunks=[];dimensions=None;ended=False
    while offset<len(png):
        if offset+12>len(png):raise RuntimeError('Truncated PNG chunk header')
        length=struct.unpack('>I',png[offset:offset+4])[0];kind=png[offset+4:offset+8];end=offset+12+length
        if end>len(png):raise RuntimeError('Truncated PNG chunk payload')
        payload=png[offset+8:offset+8+length];expected=struct.unpack('>I',png[offset+8+length:end])[0]
        if zlib.crc32(kind+payload)&0xffffffff!=expected:raise RuntimeError('PNG chunk CRC mismatch')
        chunks.append(kind.decode('ascii'))
        if kind==b'IHDR':
            if dimensions is not None or len(chunks)!=1 or length!=13:raise RuntimeError('Invalid PNG dimensions record')
            dimensions=struct.unpack('>II',payload[:8])
        if kind==b'IEND':
            if length or end!=len(png):raise RuntimeError('Invalid PNG end record')
            ended=True
        offset=end
    if not ended or dimensions is None or 'IDAT' not in chunks:raise RuntimeError('Incomplete PNG image')
    if dimensions[0]<200 or dimensions[1]<100:raise RuntimeError('Unexpectedly small capture')
    receipt.update(captureFileVerified=True,screenshot=str(screenshot.relative_to(root)),screenshotSHA256=sha(png),screenshotBytes=len(png),
                   width=dimensions[0],height=dimensions[1],pngChunks=chunks,binarySHA256=digest,
                   postCaptureProcessReceiptPresent=isinstance(data.get('processAfterCapture'),dict))
    if sha(original.read_bytes())!=receipt['originalReceiptSHA256']:raise RuntimeError('Original capture receipt changed')
except Exception as error:
    receipt['captureFileVerified']=False
    receipt['issue']=type(error).__name__+': '+str(error)
receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3))
output.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
raise SystemExit(0 if receipt['captureFileVerified'] else 1)
