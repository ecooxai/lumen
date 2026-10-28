"""Verify an isolated official Chrome copy; never disable Gatekeeper or quarantine."""
from pathlib import Path
import datetime as dt
import ctypes, ctypes.util, errno, hashlib, json, os, plistlib, signal, subprocess, threading, time

ROOT=Path.cwd()
OLD=Path('output/wpt-10290244/reference/official-signed-chrome-dmg-supportdoc-10290244')
BASE=Path('output/session-102900614/chrome-official-reference-v2')
BASE.mkdir(parents=True,exist_ok=False)
os.chmod(BASE,0o700)
TEAM='EQHXZ8M8AV'
receipt={'schema':1,'task_id':'102900614','startedUTC':dt.datetime.now(dt.timezone.utc).isoformat(),
         'scope':'Isolated official Chrome copy; original download, signed payload, quarantine and security policy preserved. No browser execution in this stage.',
         'readyForSanityProbe':False,'steps':[],
         'references':['https://developer.apple.com/library/archive/qa/qa1940/_index.html',
                       'https://support.google.com/chrome/answer/95346?co=GENIE.Platform%3DDesktop&hl=en',
                       'https://developer.apple.com/library/archive/technotes/tn2206/_index.html']}
start=time.monotonic()
stop=threading.Event()
samples=[]
owned_mount=None

# This Mac's Python omits os.*xattr. Use the documented native interfaces
# without shell output; xattr values are only hashed and never displayed.
native=ctypes.CDLL(ctypes.util.find_library('System'),use_errno=True)
native.listxattr.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int]
native.listxattr.restype=ctypes.c_ssize_t
native.getxattr.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint32,ctypes.c_int]
native.getxattr.restype=ctypes.c_ssize_t
native.removexattr.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int]
native.removexattr.restype=ctypes.c_int
def xattr_names(path):
    size=native.listxattr(os.fsencode(path),None,0,1)
    if size<0:raise OSError(ctypes.get_errno(),'listxattr failed',str(path))
    if not size:return []
    buffer=ctypes.create_string_buffer(size)
    result=native.listxattr(os.fsencode(path),buffer,size,1)
    if result<0:raise OSError(ctypes.get_errno(),'listxattr failed',str(path))
    return [os.fsdecode(name) for name in buffer.raw[:result].split(b'\0') if name]
def xattr_value(path,key):
    size=native.getxattr(os.fsencode(path),os.fsencode(key),None,0,0,1)
    if size<0:raise OSError(ctypes.get_errno(),'getxattr failed',str(path))
    if not size:return b''
    buffer=ctypes.create_string_buffer(size)
    result=native.getxattr(os.fsencode(path),os.fsencode(key),buffer,size,0,1)
    if result<0:raise OSError(ctypes.get_errno(),'getxattr failed',str(path))
    return buffer.raw[:result]
def remove_finder_info(path):
    if native.removexattr(os.fsencode(path),b'com.apple.FinderInfo',1)<0:
        raise OSError(ctypes.get_errno(),'removexattr FinderInfo failed',str(path))

def utc():return dt.datetime.now(dt.timezone.utc).isoformat()
def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1048576),b''):h.update(block)
    return h.hexdigest()
def monitor():
    target=time.monotonic()
    while not stop.is_set():
        t=time.monotonic()
        try:
            output=subprocess.check_output(['ps','-axo','pid=,ppid=,%cpu=,rss='],text=True,timeout=3)
            rows={}
            for line in output.splitlines():
                fields=line.split()
                if len(fields)==4:
                    rows[int(fields[0])]=(int(fields[1]),float(fields[2]),int(fields[3]))
            owned={os.getpid()}
            for _ in range(20):
                more={pid for pid,row in rows.items() if row[0] in owned}
                before=len(owned);owned.update(more)
                if len(owned)==before:break
            selected=[row for pid,row in rows.items() if pid in owned]
            samples.append({'utc':utc(),'elapsed_seconds':round(t-start,3),
                            'cpu_percent_sum':sum(row[1] for row in selected),
                            'rss_mib_sum':round(sum(row[2] for row in selected)/1024,3),
                            'process_count':len(selected)})
        except Exception as e:
            samples.append({'utc':utc(),'elapsed_seconds':round(t-start,3),'error':type(e).__name__})
        target+=1
        stop.wait(max(0,target-time.monotonic()))
thread=threading.Thread(target=monitor,daemon=True);thread.start()

def command(name,args,timeout=60,require=True):
    t=time.monotonic()
    result=subprocess.run(args,capture_output=True,text=True,timeout=timeout,stdin=subprocess.DEVNULL)
    row={'name':name,'command':args,'exitCode':result.returncode,'wallSeconds':round(time.monotonic()-t,3),
         'stdout':result.stdout[:12000],'stderr':result.stderr[:12000]}
    receipt['steps'].append(row)
    if require and result.returncode:raise RuntimeError(name+' failed; receipt retains diagnostics')
    return result

def payload(root):
    files=[]
    for p in sorted(root.rglob('*')):
        rel=p.relative_to(root).as_posix()
        if p.is_symlink():
            p.resolve().relative_to(root.resolve())
            files.append({'path':rel,'symlink':os.readlink(p)})
        elif p.is_file():files.append({'path':rel,'bytes':p.stat().st_size,'sha256':sha(p)})
    return files

def attributes(root):
    rows=[]
    for p in [root,*sorted(root.rglob('*'))]:
        if p.is_symlink():continue
        values={}
        for key in xattr_names(p):
            value=xattr_value(p,key)
            values[key]={'bytes':len(value),'sha256':hashlib.sha256(value).hexdigest()}
        if values:rows.append({'path':p.relative_to(root).as_posix(),'attributes':values})
    return rows

def terminate(signum,frame):raise KeyboardInterrupt('Owned verification interrupted')
signal.signal(signal.SIGTERM,terminate)
try:
    source_path=Path(__file__)
    if source_path.is_file():receipt['verifierSourceSHA256']=sha(source_path)
    else:receipt['verifierSourceSHA256']=None
    old=json.loads((OLD/'receipt.json').read_text())
    dmg=OLD/'googlechrome.dmg'
    if old.get('downloadFinalURL')!='https://dl.google.com/chrome/mac/universal/stable/GGRO/googlechrome.dmg':
        raise RuntimeError('Expected verified official Google download provenance')
    digest=sha(dmg)
    if digest!=old.get('archiveSHA256') or dmg.stat().st_size!=old.get('archiveBytes'):
        raise RuntimeError('Original DMG changed from provenance receipt')
    receipt.update(downloadURL=old['downloadFinalURL'],archiveSHA256=digest,archiveBytes=dmg.stat().st_size,
                   originalReceipt=str(OLD/'receipt.json'),originalReceiptSHA256=sha(OLD/'receipt.json'))
    mount=BASE/'mount';mount.mkdir()
    attached=command('read-only-mount',['hdiutil','attach','-readonly','-nobrowse','-noautoopen','-plist',
                                        '-mountpoint',str(mount.resolve()),str(dmg.resolve())])
    entities=plistlib.loads(attached.stdout.encode()).get('system-entities',[])
    row=next((e for e in entities if e.get('mount-point')==str(mount.resolve())),None)
    if not row or not row.get('dev-entry'):raise RuntimeError('Owned mount identity unavailable')
    owned_mount=row['dev-entry']
    source=mount/'Google Chrome.app'
    if not source.is_dir():raise RuntimeError('Official app missing')
    pub=command('mounted-publisher',['codesign','-dv','--verbose=4',str(source)])
    signature=pub.stdout+pub.stderr
    if ('TeamIdentifier='+TEAM not in signature or
        'Authority=Developer ID Application: Google LLC ('+TEAM+')' not in signature):
        raise RuntimeError('Official publisher identity mismatch')
    strict_original=command('mounted-deep-strict',['codesign','--verify','--deep','--strict','--verbose=2',str(source)],require=False)
    dest=BASE/'Google Chrome.app'
    command('normal-isolated-copy',['ditto',str(source),str(dest)],timeout=120)
    original_payload=payload(dest)
    original_attrs=attributes(dest)
    (BASE/'copied-payload-before.json').write_text(json.dumps(original_payload,indent=2)+'\n')
    (BASE/'copied-xattr-hashes-before.json').write_text(json.dumps(original_attrs,indent=2)+'\n')
    copied_strict=command('copied-deep-strict-before',['codesign','--verify','--deep','--strict','--verbose=2',str(dest)],require=False)
    if copied_strict.returncode:
        diagnostics=copied_strict.stdout+copied_strict.stderr
        if 'com.apple.FinderInfo' not in diagnostics or 'detritus not allowed' not in diagnostics:
            raise RuntimeError('Strict verification failure is not the known FinderInfo metadata problem; no repair attempted')
        affected=[row['path'] for row in original_attrs if 'com.apple.FinderInfo' in row['attributes']]
        if not affected:raise RuntimeError('No exact FinderInfo metadata found; no repair attempted')
        for name in affected:
            # QA1940 documents this non-code metadata. Preserve quarantine, resource forks,
            # provenance, signatures and every other extended attribute byte-for-byte.
            remove_finder_info(dest/name)
        receipt['finderInfoRepair']={'attribute':'com.apple.FinderInfo','copiedPaths':affected,
                                    'scope':'Only exact FinderInfo attributes on task-owned copy; no quarantine/security changes'}
    after_payload=payload(dest)
    after_attrs=attributes(dest)
    if after_payload!=original_payload:raise RuntimeError('Signed payload changed during isolated copy verification')
    expected=[]
    for row in original_attrs:
        kept={k:v for k,v in row['attributes'].items() if k!='com.apple.FinderInfo' or not copied_strict.returncode}
        if kept:expected.append({'path':row['path'],'attributes':kept})
    if after_attrs!=expected:raise RuntimeError('Unapproved extended-attribute change detected')
    receipt['payloadUnchanged']=True;receipt['quarantineAndOtherAttributesUnchanged']=True
    command('copied-deep-strict-final',['codesign','--verify','--deep','--strict','--verbose=2',str(dest)])
    pub=command('copied-publisher-final',['codesign','-dv','--verbose=4',str(dest)])
    signature=pub.stdout+pub.stderr
    if 'TeamIdentifier='+TEAM not in signature or 'Authority=Developer ID Application: Google LLC ('+TEAM+')' not in signature:
        raise RuntimeError('Final publisher identity mismatch')
    command('copied-gatekeeper-final',['spctl','--assess','--type','execute','--verbose=2',str(dest)],timeout=90)
    info=plistlib.loads((dest/'Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier')!='com.google.Chrome':raise RuntimeError('Chrome bundle identifier mismatch')
    binary=dest/'Contents/MacOS/Google Chrome'
    receipt.update(bundleVersion=info.get('CFBundleShortVersionString'),bundleBuild=info.get('CFBundleVersion'),
                   binary=str(binary),binarySHA256=sha(binary),publisherTeam=TEAM,
                   payloadFiles=len(after_payload),payloadManifestSHA256=sha(BASE/'copied-payload-before.json'),
                   readyForSanityProbe=True)
except BaseException as e:
    receipt['error']=type(e).__name__+': '+str(e)
finally:
    if owned_mount:
        try:
            result=subprocess.run(['hdiutil','detach',owned_mount],capture_output=True,text=True,timeout=30,stdin=subprocess.DEVNULL)
            receipt['mountCleanup']={'ownedDevice':owned_mount,'exitCode':result.returncode,'stderr':result.stderr[:1000]}
            if result.returncode:receipt['readyForSanityProbe']=False
        except Exception as e:receipt['mountCleanupError']=str(e);receipt['readyForSanityProbe']=False
    stop.set();thread.join(timeout=5)
    (BASE/'resources.json').write_text(json.dumps(samples,indent=2)+'\n')
    good=[s for s in samples if 'cpu_percent_sum' in s]
    gaps=[b['elapsed_seconds']-a['elapsed_seconds'] for a,b in zip(samples,samples[1:])]
    receipt['resources']={'sample_interval_target_seconds':1,'samples':len(samples),'valid_samples':len(good),
                          'cpu_peak_percent_sum':max((s['cpu_percent_sum'] for s in good),default=None),
                          'rss_peak_mib_sum':max((s['rss_mib_sum'] for s in good),default=None),
                          'max_gap_seconds':max(gaps,default=None),'scope':'Owned verifier process and descendants, including copy/signature checks'}
    receipt['finishedUTC']=utc();receipt['wallSeconds']=round(time.monotonic()-start,3)
    (BASE/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({k:v for k,v in receipt.items() if k!='steps'},indent=2))
raise SystemExit(0 if receipt['readyForSanityProbe'] else 1)
