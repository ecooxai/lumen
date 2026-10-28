"""Create a private, read-only snapshot of the existing Chrome application.

No browser profile, account data or updater state is copied. Source payloads
must remain unchanged across the copy, and every snapshot file is hashed.
"""
from datetime import datetime, timezone
import hashlib, json, os, pathlib, plistlib, shutil, subprocess, tempfile, time

ROOT=pathlib.Path.cwd();SOURCE=pathlib.Path('/Applications/Google Chrome.app')
BASE=ROOT/'output/wpt-10290244/reference';BASE.mkdir(parents=True,exist_ok=True)
def sha(path):
    value=hashlib.sha256()
    with path.open('rb')as source:
        for chunk in iter(lambda:source.read(1024*1024),b''):value.update(chunk)
    return value.hexdigest()
def tree(root):
    result=[]
    for directory,dirs,files in os.walk(root,followlinks=False):
        for name in sorted(dirs+files):
            path=pathlib.Path(directory)/name;relative=str(path.relative_to(root))
            if path.is_symlink():
                target=os.readlink(path)
                if not path.resolve().is_relative_to(root.resolve()):raise RuntimeError('Application symlink leaves its bundle: '+relative)
                result.append({'path':relative,'type':'symlink','target':target})
            elif path.is_file():result.append({'path':relative,'type':'file','bytes':path.stat().st_size,'sha256':sha(path)})
    return sorted(result,key=lambda row:row['path'])
def digest(value):return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()

started=datetime.now(timezone.utc).isoformat();begin=time.monotonic()
source_binary=SOURCE/'Contents/MacOS/Google Chrome'
binary_sha=sha(source_binary)
target=BASE/('chrome-'+binary_sha[:16]+'-mac-arm64-10290244')
if target.exists():raise RuntimeError('Reference snapshot already exists; inspect its retained receipt instead of overwriting it')
before=tree(SOURCE)
temporary=pathlib.Path(tempfile.mkdtemp(prefix='chrome-snapshot-',dir=BASE))
try:
    copied=temporary/'Google Chrome.app'
    result=subprocess.run(['/bin/cp','-cR',str(SOURCE),str(copied)],capture_output=True,text=True,timeout=120)
    method='APFS copy-on-write clone'
    if result.returncode:
        if copied.exists():shutil.rmtree(copied)
        subprocess.run(['/usr/bin/ditto',str(SOURCE),str(copied)],check=True,capture_output=True,timeout=180)
        method='ditto full bundle copy; APFS clone unavailable'
    after=tree(SOURCE);snapshot=tree(copied)
    if before!=after or before!=snapshot:raise RuntimeError('Chrome payload changed during snapshot or copied payload differs')
    if sha(copied/'Contents/MacOS/Google Chrome')!=binary_sha:raise RuntimeError('Chrome executable snapshot differs')
    # Preserve executable bits, remove write bits only inside this owned copy.
    for directory,dirs,files in os.walk(copied,followlinks=False):
        for name in dirs+files:
            path=pathlib.Path(directory)/name
            if not path.is_symlink():path.chmod(path.stat().st_mode&~0o222)
    copied.chmod(copied.stat().st_mode&~0o222)
    info=plistlib.loads((copied/'Contents/Info.plist').read_bytes())
    receipt={'schema':1,'startedUTC':started,'finishedUTC':datetime.now(timezone.utc).isoformat(),
      'wallSeconds':round(time.monotonic()-begin,3),'copyMethod':method,'sourceApplication':str(SOURCE),
      'browserVersion':info.get('CFBundleShortVersionString'),'binarySHA256':binary_sha,
      'binary':str(target/'Google Chrome.app/Contents/MacOS/Google Chrome'),
      'treeSHA256':digest(snapshot),'files':len(snapshot),'payloadBytes':sum(row.get('bytes',0)for row in snapshot),
      'sourceStableAcrossCopy':True,'allCopiedPayloadHashesMatch':True,'writeBitsRemoved':True,
      'scope':'Application payload only. No user browser profile, cookies, credentials or updater state.'}
    (temporary/'snapshot-tree-manifest.json').write_text(json.dumps(snapshot,indent=2)+'\n')
    (temporary/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    temporary.rename(target)
except BaseException:
    # Retain the failed task-owned candidate for diagnosis; do not remove any
    # original application file or attempt to repair/update the user's browser.
    (temporary/'FAILED').write_text('Snapshot validation did not complete. Do not use this reference.\n')
    raise
print(json.dumps(receipt,indent=2))
