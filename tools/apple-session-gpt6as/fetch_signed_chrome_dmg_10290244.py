"""Isolated official Chrome DMG reference; no app replacement or trust changes."""
import datetime,hashlib,html,json,os,pathlib,plistlib,re,signal,subprocess,time,urllib.parse,urllib.request
ROOT=pathlib.Path.cwd();BASE=ROOT/'output/wpt-10290244/reference/official-signed-chrome-dmg-10290244'
PAGE='https://www.google.com/chrome/';TEAM='EQHXZ8M8AV'
receipt={'schema':1,'startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'One isolated official signed Chrome reference download; no installed browser/profile modification, trust change, quarantine removal, re-signing or browser execution.','readyForSanityProbe':False,'steps':[]}
owned_mount=None;created=False;started=time.monotonic()
def need(value,message):
 if not value:raise RuntimeError(message)
def sha(path):
 h=hashlib.sha256()
 with pathlib.Path(path).open('rb')as f:
  for part in iter(lambda:f.read(1048576),b''):h.update(part)
 return h.hexdigest()
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
class GoogleRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,request,fp,code,msg,headers,url):
  parsed=urllib.parse.urlsplit(url)
  need(parsed.scheme=='https'and parsed.hostname in ('www.google.com','google.com','dl.google.com')and parsed.username is None,'Redirect outside official Google download hosts')
  return super().redirect_request(request,fp,code,msg,headers,url)
opener=urllib.request.build_opener(GoogleRedirect)
UA='Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Safari/537.36'
def command(name,args,timeout=40):
 t=time.monotonic();run=subprocess.run(args,capture_output=True,text=True,timeout=timeout,stdin=subprocess.DEVNULL)
 row={'name':name,'command':args,'exitCode':run.returncode,'durationSeconds':round(time.monotonic()-t,3),'stdout':run.stdout[:12000],'stderr':run.stderr[:12000]};receipt['steps'].append(row);need(run.returncode==0,name+' failed; exact result retained');return run

def terminated(signum,frame):raise KeyboardInterrupt('Owned download task terminated')
signal.signal(signal.SIGTERM,terminated)
try:
 BASE.mkdir(parents=True,exist_ok=False);created=True;os.chmod(BASE,0o700)
 with opener.open(urllib.request.Request(PAGE,headers={'User-Agent':UA}),timeout=20)as r:
  raw=r.read(4*1024*1024+1);need(len(raw)<=4*1024*1024,'Official page unexpectedly large')
 text=html.unescape(raw.decode()).replace('\\/','/')
 urls=sorted(set(re.findall(r'https://dl\.google\.com/[^\s\"<>\\]+?googlechrome\.dmg',text)))
 urls=[u for u in urls if '/mac/universal/stable/'in u and not urllib.parse.urlsplit(u).fragment]
 need(urls,'Official Chrome page did not expose a stable universal DMG link; no guessed download attempted')
 # Prefer the public GGRO stable route when multiple official experiment links exist.
 url=next((u for u in urls if '/GGRO/'in u),urls[0])
 need(urllib.parse.urlsplit(url).hostname=='dl.google.com','Official download host mismatch')
 (BASE/'official-download-page.html').write_bytes(raw)
 receipt.update(officialPage=PAGE,officialPageSHA256=hashlib.sha256(raw).hexdigest(),downloadURL=url)
 target=BASE/'googlechrome.dmg';size=0;digest=hashlib.sha256();began=time.monotonic()
 with opener.open(urllib.request.Request(url,headers={'User-Agent':UA}),timeout=30)as response,target.open('xb')as f:
  need(urllib.parse.urlsplit(response.url).hostname=='dl.google.com','Download final origin mismatch')
  declared=response.headers.get('Content-Length');declared=int(declared)if declared is not None else None
  need(declared is None or 10*1024*1024<=declared<=700*1024*1024,'Unexpected DMG declared length')
  receipt['downloadFinalURL']=response.url;receipt['downloadContentType']=response.headers.get('Content-Type');receipt['declaredBytes']=declared
  while True:
   need(time.monotonic()-began<180,'Download exceeded180seconds')
   chunk=response.read(1048576)
   if not chunk:break
   size+=len(chunk);need(size<=700*1024*1024,'DMG exceeds700MiB safety bound');digest.update(chunk);f.write(chunk)
 need(size>=10*1024*1024 and(declared is None or size==declared),'Truncated/unexpected DMG download')
 receipt.update(archiveSHA256=digest.hexdigest(),archiveBytes=size,downloadSeconds=round(time.monotonic()-began,3))
 command('disk-image-integrity',['hdiutil','verify',str(target)],timeout=90)
 mount=BASE/'mount';mount.mkdir()
 attached=command('read-only-mount',['hdiutil','attach','-readonly','-nobrowse','-noautoopen','-plist','-mountpoint',str(mount),str(target)],timeout=60)
 entities=plistlib.loads(attached.stdout.encode()).get('system-entities',[])
 row=next((e for e in entities if e.get('mount-point')==str(mount)),None);need(row and row.get('dev-entry'),'Owned mount identity was not returned');owned_mount=row['dev-entry']
 app=mount/'Google Chrome.app';need(app.is_dir(),'Official DMG has no expected Google Chrome app')
 command('mounted-app-deep-strict',['codesign','--verify','--deep','--strict','--verbose=2',str(app)])
 details=command('mounted-app-publisher',['codesign','-dv','--verbose=4',str(app)])
 signature=details.stdout+details.stderr;need('TeamIdentifier='+TEAM in signature and 'Authority=Developer ID Application: Google LLC ('+TEAM+')'in signature,'Google publisher identity did not match')
 command('mounted-app-gatekeeper',['spctl','--assess','--type','execute','--verbose=2',str(app)],timeout=60)
 dest=BASE/'Google Chrome.app';need(not dest.exists(),'Owned destination already exists')
 command('copy-isolated-app',['ditto',str(app),str(dest)],timeout=90)
 command('copied-app-deep-strict',['codesign','--verify','--deep','--strict','--verbose=2',str(dest)])
 command('copied-app-gatekeeper',['spctl','--assess','--type','execute','--verbose=2',str(dest)],timeout=60)
 info=plistlib.loads((dest/'Contents/Info.plist').read_bytes());need(info.get('CFBundleIdentifier')=='com.google.Chrome','Copied bundle identifier differs')
 files=[];total=0
 for p in sorted(dest.rglob('*')):
  relative=p.relative_to(dest).as_posix()
  if p.is_symlink():
   p.resolve().relative_to(dest.resolve());files.append({'path':relative,'symlink':os.readlink(p)})
  elif p.is_file():
   total+=p.stat().st_size;need(total<=4*1024**3,'Copied app exceeds4GiB inventory bound');files.append({'path':relative,'bytes':p.stat().st_size,'sha256':sha(p)})
 payload=json.dumps(files,sort_keys=True,separators=(',',':')).encode();(BASE/'app-payload-manifest.json').write_bytes(payload+b'\n')
 binary=dest/'Contents/MacOS/Google Chrome';need(binary.is_file(),'Verified app executable missing')
 receipt.update(bundleVersion=info.get('CFBundleShortVersionString'),bundleBuild=info.get('CFBundleVersion'),binary=str(binary.relative_to(ROOT)),binarySHA256=sha(binary),payloadFiles=len(files),payloadBytes=total,payloadSHA256=hashlib.sha256(payload).hexdigest(),publisherTeam=TEAM,readyForSanityProbe=True)
except BaseException as error:
 receipt['error']=type(error).__name__+': '+str(error)
finally:
 if owned_mount:
  try:
   result=subprocess.run(['hdiutil','detach',owned_mount],capture_output=True,text=True,timeout=30,stdin=subprocess.DEVNULL)
   receipt['mountCleanup']={'ownedDevice':owned_mount,'exitCode':result.returncode,'stderr':result.stderr[:2000]}
   if result.returncode:receipt['readyForSanityProbe']=False;receipt['cleanupIssue']='Owned read-only mount did not detach; no force-detach attempted'
  except BaseException as error:receipt['readyForSanityProbe']=False;receipt['cleanupIssue']=type(error).__name__+': '+str(error)
 receipt['finishedUTC']=utc();receipt['totalSeconds']=round(time.monotonic()-started,3)
 if created:(BASE/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2));raise SystemExit(0 if receipt['readyForSanityProbe']else 1)
