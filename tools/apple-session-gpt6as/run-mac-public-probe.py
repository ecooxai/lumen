#!/usr/bin/env python3
"""Fetch reviewed source only and detach one bounded, isolated Mac Chrome probe."""
import hashlib, io, json, shutil, subprocess, sys, tarfile, urllib.request
from pathlib import Path
ROOT=Path('/Users/devin/repos/lumen').resolve()
DEST=ROOT/'output/session-20261029/mac-public-probe-install-1'
URL='https://f6c8d1446c510279.j6oksipj7wog.opentunnel.xyz/gpt6-astra-pro_web-demos_builtin-subagents_mac-public-probe.tar.gz'
SHA='b253ace3f0cd277477dbe3636489f97de71a87ca18ee9e135e2dda188978e72e'
FILES={
 'tools/apple-web-lab-gpt6as/tests/public-chrome-cdp.cjs':'933bcbc874dff99be2250f2501647c9f52ec83bad23ce6a16484906a5489088b',
 'tools/wpt-reference-gpt6as/run-bounded-chrome.cjs':'0ad4111a2edf043482504760aa50795570d023210320bc514a44b0a36671fa39',
 'tools/wpt-reference-gpt6as/run-reference.cjs':'c2f9dbf00c86d799200d6a84815665d27c188587b222955928083f81a4cf95d9',
 'scripts/qa-apple-gpt6/telemetry.py':'8b03ab0aab566080ee56b6ec66b1a964c5fd62b6b822a4be7a16e12c7aad9b07',
}
if sys.platform!='darwin': raise SystemExit('This launcher requires the authorized Mac')
launcher=ROOT/'tools/apple-session-gpt6as/launch_job.py'
chrome=Path('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
if not launcher.is_file() or not chrome.is_file() or not shutil.which('node'): raise SystemExit('Existing launcher, Chrome and Node required; nothing will be installed')
if shutil.disk_usage(ROOT).free<128*1024*1024: raise SystemExit('At least128MiB free required')
if DEST.exists(): raise SystemExit('New task destination required; never replace prior evidence')
request=urllib.request.Request(URL,headers={'User-Agent':'Lumen-authorized-Mac-probe/1'})
with urllib.request.urlopen(request,timeout=15) as response:
 if response.status!=200: raise SystemExit('Expected HTTP200 source kit')
 data=response.read(20001)
if len(data)!=17507 or hashlib.sha256(data).hexdigest()!=SHA: raise SystemExit('Reviewed archive size/SHA mismatch')
with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
 members=archive.getmembers()
 if len(members)!=len(FILES) or {m.name for m in members}!=set(FILES): raise SystemExit('Unexpected archive members')
 verified={}
 for member in members:
  if not member.isfile() or member.issym() or member.islnk() or member.size>100000: raise SystemExit('Unsafe archive member')
  content=archive.extractfile(member).read()
  if hashlib.sha256(content).hexdigest()!=FILES[member.name]: raise SystemExit('Source SHA mismatch: '+member.name)
  verified[member.name]=content
DEST.mkdir(parents=True,exist_ok=False)
(DEST/'source-kit.tar.gz').write_bytes(data)
kit=DEST/'kit'
for name,content in verified.items():
 target=kit/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(content)
receipt={'archive_sha256':SHA,'bytes':len(data),'files':FILES,'tls_validation_disabled':False,'scope':'Reviewed source-only isolated public HTTPS/WSS probe kit; no modification to native engine or existing profiles'}
(DEST/'install-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
probe_output=kit/'output/mac-public-delivery-1'
command=[shutil.which('node'),str(kit/'tools/apple-web-lab-gpt6as/tests/public-chrome-cdp.cjs'),'--chrome',str(chrome),'--url','https://f6c8d1446c510279.j6oksipj7wog.opentunnel.xyz/','--output',str(probe_output),'--expect-reply']
subprocess.run([sys.executable,str(launcher),'--output',str(DEST/'job'),'--timeout','150','--',*command],cwd=kit,check=True,timeout=8)
print(json.dumps({'receipt':str(probe_output/'receipt.json'),'job':str(DEST/'job'),'feedback_reply_wait_seconds':45,'normal_tls':True}))
