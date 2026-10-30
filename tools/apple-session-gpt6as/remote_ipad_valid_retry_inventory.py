"""Bounded valid simulator utilities and declared project dependency inventory."""
import datetime,hashlib,json,os,pathlib,plistlib,subprocess,sys
ROOT=pathlib.Path.cwd().resolve();SIM='/Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/Resources/bin/simctl';DEVICE='3DD498EB-4464-4ED7-9CD3-7667889DD1F6';OUT=ROOT/'output/session-20261029/ipad-valid-retry-inventory'
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def run(args,timeout=8,cap=2400):
 start=utc()
 try:
  r=subprocess.run(args,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=timeout)
  return {'startedUTC':start,'finishedUTC':utc(),'exitCode':r.returncode,'stdout':r.stdout[:cap],'stderr':r.stderr[:cap],'stdoutBytes':len(r.stdout.encode()),'stdoutTruncated':len(r.stdout)>cap}
 except subprocess.TimeoutExpired as error:
  def text(value):return value.decode(errors='replace')if isinstance(value,bytes)else value or''
  return {'startedUTC':start,'finishedUTC':utc(),'timedOutSeconds':timeout,'stdout':text(error.stdout)[:cap],'stderr':text(error.stderr)[:cap]}
def stat(path):
 row={'path':str(path),'exists':path.exists(),'kind':'directory'if path.is_dir()else'file'if path.is_file()else'missing'}
 if path.is_file():row['bytes']=path.stat().st_size
 return row
OUT.mkdir(parents=True,exist_ok=False)
result={'startedUTC':utc(),'taskDevice':DEVICE,'scope':'Existing authorized task iPad only; valid utilities and read-only metadata. No reset, erase, app-data read, runtime repair or synthetic native success.','utility':run([SIM,'spawn',DEVICE,'launchctl','help']),
 'listApps':run([SIM,'listapps',DEVICE],12,1000),'lumenContainer':run([SIM,'get_app_container',DEVICE,'com.ecooxai.lumen','app'],10),'projectDependencyPaths':[],'artifactCandidates':[],'runtimeAppMetadata':[]}
# These are declared in Makefile, ios/build-v8.py and ios/build-deps.sh, not an unrestricted home search.
for relative in ['build-apple-toolchain','build-apple-toolchain/v8','build-apple-toolchain/v8/include/v8.h','build-apple-toolchain/v8/out/ios-simulator/obj/libv8_monolith.a','build-apple-toolchain/v8/out/ios-device/obj/libv8_monolith.a','build-apple-toolchain/sim','build-apple-toolchain/ios','build-apple-toolchain/wgpu','build-apple-toolchain/archives','build-sim','build-sim-jit','build-ios','build-ios-jit','build-ios-jitless','build-sim-jitless']:
 result['projectDependencyPaths'].append(stat(ROOT/relative))
for base in [ROOT/'build-apple-toolchain',ROOT/'build-sim',ROOT/'build-sim-jit',ROOT/'build-ios',ROOT/'build-ios-jit',ROOT/'build-ios-jitless',ROOT/'build-sim-jitless',ROOT/'packages',ROOT/'artifacts']:
 if not base.is_dir():continue
 visited=0
 for directory,dirs,files in os.walk(base,followlinks=False):
  visited+=1
  if visited>120:break
  dirs[:]=[d for d in dirs if d not in('.git','node_modules','obj')and not(pathlib.Path(directory)/d).is_symlink()]
  for name in files:
   if name.endswith(('.ipa','.xcframework','.zip','.tar.gz'))or name=='libv8_monolith.a':
    p=pathlib.Path(directory)/name;row=stat(p)
    if name=='libv8_monolith.a':row['fileType']=run(['/usr/bin/file',str(p)],5,500)
    result['artifactCandidates'].append(row)
runtime=pathlib.Path('/Library/Developer/CoreSimulator/Profiles/Runtimes/iOS 27.0.simruntime/Contents/Resources/RuntimeRoot')
for relative in ['Applications/MobileSafari.app','System/Library/CoreServices/SpringBoard.app','Applications/DiagnosticsReporter.app']:
 app=runtime/relative;info=app/'Info.plist';row={'app':str(app),'info':stat(info)}
 if info.is_file():
  try:
   data=plistlib.loads(info.read_bytes());row['bundleIdentifier']=data.get('CFBundleIdentifier');row['executable']=data.get('CFBundleExecutable');row['plistValid']=True
   if row['executable']:row['executableMetadata']=stat(app/row['executable'])
  except Exception as error:row['plistValid']=False;row['parseError']=type(error).__name__+': '+str(error)
 result['runtimeAppMetadata'].append(row)
result['finishedUTC']=utc();(OUT/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
