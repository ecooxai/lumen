"""Read-only original/frozen Chrome signature audit and official CfT metadata."""
import datetime,hashlib,json,os,pathlib,plistlib,re,subprocess,time,urllib.request
ROOT=pathlib.Path.cwd();BASE=ROOT/'output/session-10290244/chrome-reference-provenance';BASE.mkdir(parents=True,exist_ok=False)
FROZEN=ROOT/'output/wpt-10290244/reference/chrome-4f71e9dbe725fec1-mac-arm64-10290244/Google Chrome.app'
APPS={'installed':pathlib.Path('/Applications/Google Chrome.app'),'frozen':FROZEN}
META='https://googlechromelabs.github.io/chrome-for-testing/last-known-good-versions-with-downloads.json'
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
receipt={'schema':1,'startedUTC':utc(),'scope':'Read-only signature, bundle identity and official download metadata. No app replacement, installation, certificate/trust change or bypass. Signature validation does not make failing URL behavior conformant.','applications':{},'environmentVariablePresence':{name:bool(os.environ.get(name))for name in ('DYLD_INSERT_LIBRARIES','DYLD_LIBRARY_PATH','DYLD_FRAMEWORK_PATH','V8_FLAGS','CHROME_WRAPPER','CHROME_EXTRA_ARGS')}}
def command(label,args,timeout=25):
 began=time.monotonic();row={'command':args,'startedUTC':utc()}
 try:
  result=subprocess.run(args,capture_output=True,text=True,timeout=timeout)
  output=result.stdout+result.stderr;row.update(exitCode=result.returncode,output=output[:20000],outputTruncated=len(output)>20000)
 except subprocess.TimeoutExpired:row.update(timedOut=True,exitCode=None)
 row.update(wallSeconds=round(time.monotonic()-began,3),finishedUTC=utc())
 (BASE/(label+'.json')).write_text(json.dumps(row,indent=2)+'\n');return row
for name,app in APPS.items():
 binary=app/'Contents/MacOS/Google Chrome';info=plistlib.loads((app/'Contents/Info.plist').read_bytes())
 value={'path':str(app),'binarySHA256':sha(binary),'bundleIdentifier':info.get('CFBundleIdentifier'),'bundleVersion':info.get('CFBundleShortVersionString'),'bundleExecutable':info.get('CFBundleExecutable'),'bundleLSEnvironmentKeys':sorted(info.get('LSEnvironment',{}))}
 value['signatureDescription']=command(name+'-signature',['/usr/bin/codesign','-dv','--verbose=4',str(app)])
 value['designatedRequirement']=command(name+'-requirement',['/usr/bin/codesign','-dr','-',str(app)])
 value['strictDeepVerification']=command(name+'-verify',['/usr/bin/codesign','--verify','--deep','--strict','--verbose=2',str(app)],30)
 value['gatekeeperAssessment']=command(name+'-assessment',['/usr/sbin/spctl','--assess','--type','execute','--verbose=4',str(app)],20)
 description=value['signatureDescription'].get('output','')
 value['reportedAuthorities']=re.findall(r'^Authority=(.*)$',description,re.M)
 value['reportedTeamIdentifier']=next(iter(re.findall(r'^TeamIdentifier=(.*)$',description,re.M)),None)
 receipt['applications'][name]=value
try:
 with urllib.request.urlopen(META,timeout=20)as response:
  raw=response.read(1024*1024+1)
  if len(raw)>1024*1024:raise RuntimeError('Unexpected metadata size')
  metadata=json.loads(raw);receipt['metadataHTTPStatus']=response.status
 (BASE/'official-cft-channel-metadata.json').write_bytes(raw)
 stable=metadata['channels']['Stable'];downloads=[row for row in stable['downloads']['chrome']if row['platform']=='mac-arm64']
 if len(downloads)!=1 or not downloads[0]['url'].startswith('https://storage.googleapis.com/chrome-for-testing-public/'):raise RuntimeError('Official mac-arm64 Chrome download is ambiguous')
 receipt['officialChromeForTesting']={'metadataURL':META,'metadataSHA256':hashlib.sha256(raw).hexdigest(),'timestamp':metadata['timestamp'],'channel':stable['channel'],'version':stable['version'],'revision':stable['revision'],'download':downloads[0]}
 with urllib.request.urlopen(urllib.request.Request(downloads[0]['url'],method='HEAD'),timeout=20)as response:
  receipt['officialChromeForTesting']['downloadHead']={'status':response.status,'contentLength':response.headers.get('Content-Length'),'contentType':response.headers.get('Content-Type'),'etag':response.headers.get('ETag'),'googleHash':response.headers.get('x-goog-hash')}
except Exception as error:receipt['officialMetadataError']=type(error).__name__+': '+str(error)
receipt['finishedUTC']=utc();(BASE/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
