#!/usr/bin/env python3
"""Compare fixed scrolling workload position and every final page pixel.

The 44 CSS-pixel compact toolbar contains a random fixture port/nonce and is
excluded explicitly. All page rows remain in the comparison. CPU-frame pixel
comparison supplements separate real-Metal window capture and unit tests.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import time
import zlib

p=argparse.ArgumentParser()
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--candidate',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=Path.cwd().resolve()
for value in (a.baseline,a.candidate,a.output):
    if not value.resolve().is_relative_to(root):raise SystemExit('All evidence paths must stay inside the task repository')
utc=lambda:datetime.now(timezone.utc).isoformat()
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def decode(path):
    blob=path.read_bytes()
    if blob[:8]!=b'\x89PNG\r\n\x1a\n':raise ValueError('Invalid PNG signature')
    pos=8;data=bytearray();size=None;ended=False
    while pos+12<=len(blob):
        length=struct.unpack('>I',blob[pos:pos+4])[0];tag=blob[pos+4:pos+8]
        block=blob[pos+8:pos+8+length];tail=blob[pos+8+length:pos+12+length]
        if len(block)!=length or len(tail)!=4:raise ValueError('Truncated PNG chunk')
        if zlib.crc32(tag+block)&0xffffffff!=struct.unpack('>I',tail)[0]:raise ValueError('PNG checksum mismatch')
        if tag==b'IHDR':
            if length!=13:raise ValueError('Invalid PNG header')
            w,h,depth,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',block)
            if (depth,color,compression,filtering,interlace)!=(8,6,0,0,0) or not 0<w<=4096 or not 0<h<=4096:raise ValueError('Unexpected native screenshot format or size')
            size=(w,h)
        elif tag==b'IDAT':data.extend(block)
        elif tag==b'IEND':ended=True;break
        pos+=12+length
    if not ended or size is None:raise ValueError('Incomplete native screenshot')
    w,h=size;raw=zlib.decompress(data)
    if len(raw)!=h*(w*4+1):raise ValueError('Native screenshot data length mismatch')
    rows=[]
    for y in range(h):
        offset=y*(w*4+1)
        if raw[offset]!=0:raise ValueError('Expected the native raster writer\'s unfiltered rows')
        rows.append(raw[offset+1:offset+1+w*4])
    return w,h,rows

def evidence(directory):
    receipt_path=directory/'receipt.json';r=json.loads(receipt_path.read_text())
    if r.get('scenario')!='scroll' or r.get('natural_app_return_code')!=0:raise ValueError('Scroll run did not complete naturally')
    if r.get('binary_sha256')!=r.get('binary_sha256_after'):raise ValueError('Executable changed during observation')
    if r.get('app_termination',{}).get('forced') or r.get('sampler_termination',{}).get('forced'):raise ValueError('Forced termination invalidates the comparison')
    binary=root/r['binary']
    if not binary.resolve().is_relative_to(root) or sha(binary)!=r['binary_sha256']:raise ValueError('Immutable executable no longer matches')
    expected_input={'source':'Existing native SDL wheel hook','parameters':'400,400,-0.08,15000,1875','scheduled_event_count':1875,'scheduled_duration_seconds':30}
    if r.get('input')!=expected_input:raise ValueError('Fixed input workload changed')
    reports=[row['value'] for row in r.get('reports',[]) if row.get('value',{}).get('kind')=='scroll-complete']
    if len(reports)!=1:raise ValueError('Expected exactly one scroll completion report')
    report=reports[0]
    f32=lambda value:struct.unpack('f',struct.pack('f',value))[0]
    delta=f32(-f32(-.08)*40);expected_y=0
    for _ in range(1875):expected_y=f32(expected_y+delta)
    if report.get('finalY')!=expected_y or report.get('maxY')!=expected_y:raise ValueError('The 1875 fractional wheel inputs did not retain their exact accumulated position')
    if not isinstance(report.get('events'),int) or report['events']<10:raise ValueError('No actual DOM scrolling events observed')
    image=directory/'gpt6-astra-pro_builtin-agents_devinmac3d_scroll.png'
    result={'directory':str(directory),'receiptSHA256':sha(receipt_path),'binarySHA256':r['binary_sha256'],'imageSHA256':sha(image),'scroll':{key:report[key] for key in ('events','maxY','finalY')},'input':expected_input}
    return result,decode(image)

start=time.monotonic();result={'startedUTC':utc(),'scope':'Exact final CPU-frame page pixels and fractional DOM position; dynamic compact toolbar excluded. Actual native window capture is separate.','passed':False}
try:
    before,left=evidence(a.baseline);after,right=evidence(a.candidate)
    result.update(baseline=before,candidate=after)
    if left[:2]!=right[:2]:raise ValueError('Viewport screenshot dimensions changed')
    w,h=left[:2]
    if w%1000 or h*1000!=w*700:raise ValueError('The fixed 1000x700 logical viewport changed')
    top=44*(w//1000)
    differing=0;examples=[]
    for y in range(top,h):
        old,new=left[2][y],right[2][y]
        if old==new:continue
        for x in range(w):
            at=x*4
            if old[at:at+4]!=new[at:at+4]:
                differing+=1
                if len(examples)<12:examples.append({'x':x,'y':y,'baselineRGBA':old[at:at+4].hex(),'candidateRGBA':new[at:at+4].hex()})
    result.update(width=w,height=h,toolbarPixelsExcluded=top,pagePixelsCompared=w*(h-top),differingPagePixels=differing,examples=examples,passed=differing==0)
except Exception as error:result['issue']=type(error).__name__+': '+str(error)
result.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3))
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
raise SystemExit(0 if result['passed'] else 1)
