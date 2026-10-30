"""Download an isolated official Chrome for Testing reference without installing it."""
import base64,datetime,hashlib,json,os,pathlib,plistlib,re,stat,subprocess,time,urllib.parse,urllib.request,zipfile
ROOT=pathlib.Path.cwd();BASE=ROOT/'output/wpt-10290244/reference'
metadata=json.loads((ROOT/'output/session-10290244/chrome-reference-provenance/receipt.json').read_text())['officialChromeForTesting']
version=metadata['version'];url=metadata['download']['url'];parts=urllib.parse.urlsplit(url)
if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+',version)or parts.scheme!='https'or parts.netloc!='storage.googleapis.com'or parts.path!='/chrome-for-testing-public/'+version+'/mac-arm64/chrome-mac-arm64.zip'or parts.query or parts.fragment:raise RuntimeError('Official download metadata is not the expected mac-arm64 Chrome artifact')
OUT=BASE/('chrome-for-testing-'+version+'-mac-arm64-10290244');OUT.mkdir(parents=True,exist_ok=False)
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(path):
 value=hashlib.sha256()
 with path.open('rb')as stream:
  for chunk in iter(lambda:stream.read(1024*1024),b''):value.update(chunk)
 return value.hexdigest()
receipt={'schema':1,'startedUTC':utc(),'metadata':metadata,'installedIntoApplications':False,'source':'Google Chrome for Testing official channel JSON and HTTPS download','scope':'Isolated task-owned reference app; existing installed/frozen apps remain unchanged. No quarantine, trust or access-control bypass.','readyForSanityProbe':False}
began=time.monotonic()
try:
 archive=OUT/'chrome-mac-arm64.zip';temporary=OUT/'download.partial';digest=hashlib.sha256();md5=hashlib.md5();count=0
 with urllib.request.urlopen(url,timeout=20)as response,temporary.open('xb')as output:
  length=int(response.headers.get('Content-Length','0'));server_hash=response.headers.get('x-goog-hash','')
  if not 10*1024*1024<=length<=500*1024*1024:raise RuntimeError('Unexpected official archive length')
  while True:
   if time.monotonic()-began>180:raise TimeoutError('Official reference download exceeded180seconds')
   chunk=response.read(1024*1024)
   if not chunk:break
   count+=len(chunk)
   if count>length:raise RuntimeError('Download exceeded declared archive length')
   digest.update(chunk);md5.update(chunk);output.write(chunk)
 if count!=length:raise RuntimeError('Truncated official archive')
 reported_md5=next((item.strip()[4:]for item in server_hash.split(',')if item.strip().startswith('md5=')),None)
 if reported_md5 and base64.b64encode(md5.digest()).decode()!=reported_md5:raise RuntimeError('Download differs from Google storage MD5 integrity header')
 temporary.rename(archive);receipt['archive']={'path':str(archive.relative_to(ROOT)),'bytes':count,'sha256':digest.hexdigest(),'googleStorageMD5Verified':bool(reported_md5)}
 with zipfile.ZipFile(archive)as zipped:
  entries=zipped.infolist()
  if len(entries)>15000 or sum(entry.file_size for entry in entries)>4*1024**3:raise RuntimeError('Unexpected archive expansion size')
  for entry in entries:
   path=pathlib.PurePosixPath(entry.filename)
   if path.is_absolute()or '..'in path.parts or not path.parts or path.parts[0]!='chrome-mac-arm64':raise RuntimeError('Unsafe archive member path')
   if stat.S_ISLNK(entry.external_attr>>16):
    if entry.file_size>4096:raise RuntimeError('Oversized archive symlink')
    target=zipped.read(entry).decode();resolved=(OUT/path.parent/target).resolve()
    if pathlib.PurePosixPath(target).is_absolute()or not resolved.is_relative_to(OUT/'chrome-mac-arm64'):raise RuntimeError('Archive symlink leaves its payload')
 unzip=subprocess.run(['/usr/bin/ditto','-x','-k',str(archive),str(OUT)],capture_output=True,text=True,timeout=90)
 if unzip.returncode:raise RuntimeError('Official archive extraction failed: '+unzip.stderr[:500])
 apps=list((OUT/'chrome-mac-arm64').glob('*.app'))
 if len(apps)!=1:raise RuntimeError('Official app bundle is ambiguous')
 app=apps[0];info=plistlib.loads((app/'Contents/Info.plist').read_bytes());binary=app/'Contents/MacOS'/info['CFBundleExecutable']
 if info.get('CFBundleShortVersionString')!=version:raise RuntimeError('Extracted app version differs from official metadata')
 signature=subprocess.run(['/usr/bin/codesign','-dv','--verbose=4',str(app)],capture_output=True,text=True,timeout=20)
 verification=subprocess.run(['/usr/bin/codesign','--verify','--deep','--strict','--verbose=2',str(app)],capture_output=True,text=True,timeout=45)
 description=signature.stdout+signature.stderr
 receipt['signature']={'description':description,'verifyOutput':verification.stdout+verification.stderr,'verifyExitCode':verification.returncode}
 if signature.returncode or verification.returncode or not re.search(r'^Authority=Developer ID Application: Google (?:LLC|Inc\.) \(',description,re.M):raise RuntimeError('Official app signature did not validate with Google publisher identity')
 payload=[]
 for directory,dirs,files in os.walk(app,followlinks=False):
  for name in sorted(dirs+files):
   path=pathlib.Path(directory)/name;relative=str(path.relative_to(app))
   if path.is_symlink():
    if not path.resolve().is_relative_to(app.resolve()):raise RuntimeError('Extracted app symlink leaves its bundle')
    payload.append({'path':relative,'type':'symlink','target':os.readlink(path)})
   elif path.is_file():payload.append({'path':relative,'bytes':path.stat().st_size,'sha256':sha(path)})
   if not path.is_symlink():path.chmod(path.stat().st_mode&~0o222)
 app.chmod(app.stat().st_mode&~0o222)
 payload.sort(key=lambda row:row['path']);(OUT/'snapshot-tree-manifest.json').write_text(json.dumps(payload,indent=2)+'\n')
 receipt.update(readyForSanityProbe=True,application=str(app),binary=str(binary),binarySHA256=sha(binary),bundleIdentifier=info.get('CFBundleIdentifier'),browserVersion=version,files=len(payload),treeSHA256=hashlib.sha256(json.dumps(payload,sort_keys=True,separators=(',',':')).encode()).hexdigest(),writeBitsRemoved=True)
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
receipt['finishedUTC']=utc();receipt['wallSeconds']=round(time.monotonic()-began,3);(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
raise SystemExit(0 if receipt['readyForSanityProbe']else 1)
