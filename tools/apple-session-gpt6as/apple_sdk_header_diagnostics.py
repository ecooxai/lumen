"""Read-only Apple compiler/header diagnosis; writes only task-local receipts."""
from pathlib import Path
import hashlib,json,os,subprocess,time
from datetime import datetime,timezone
root=Path('/Users/devin/repos/lumen')
if Path.cwd().resolve()!=root:raise RuntimeError('Unexpected workspace')
out=root/'output/session-10290244/apple-sdk-header-diagnostics';out.mkdir(exist_ok=False)
receipt={'startedUTC':datetime.now(timezone.utc).isoformat(),'commands':[],'files':[]}
def run(name,args,timeout=12):
 started=time.monotonic()
 try:
  p=subprocess.run(args,text=True,capture_output=True,timeout=timeout)
  text=p.stdout;error=p.stderr;code=p.returncode
 except subprocess.TimeoutExpired as e:
  text=e.stdout or '';error=e.stderr or ''
  if isinstance(text,bytes):text=text.decode(errors='replace')
  if isinstance(error,bytes):error=error.decode(errors='replace')
  code='timeout'
 (out/(name+'.stdout.txt')).write_text(text);(out/(name+'.stderr.txt')).write_text(error)
 receipt['commands'].append({'name':name,'args':args,'exitCode':code,'durationSeconds':round(time.monotonic()-started,3),'stdoutHead':text[:1600],'stderrHead':error[:2000]})
 return text.strip(),code
xcode=Path('/Applications/Xcode-27.0-RC.app/Contents/Developer')
clang=xcode/'Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'
clt=Path('/Library/Developer/CommandLineTools/usr/bin/clang')
device=xcode/'Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS27.0.sdk'
simulator=xcode/'Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator27.0.sdk'
resources=[]
for name,compiler in [('xcode',clang),('clt',clt)]:
 if compiler.exists():
  run(name+'-version',[str(compiler),'--version'])
  resource,code=run(name+'-resource-dir',[str(compiler),'-print-resource-dir'])
  if code==0:resources.append((name,Path(resource)))
paths=[(name+'-ptrcheck',base/'include/ptrcheck.h') for name,base in resources]
paths += [('device-cdefs',device/'usr/include/sys/cdefs.h'),('device-ptrcheck',device/'usr/include/ptrcheck.h'),('simulator-ptrcheck',simulator/'usr/include/ptrcheck.h'),('uikit-umbrella',device/'System/Library/Frameworks/UIKit.framework/Headers/UIKit.h')]
for label,path in paths:
 row={'label':label,'path':str(path),'exists':path.exists(),'resolvedPath':str(path.resolve())}
 if path.is_file():
  data=path.read_bytes();row.update(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
  text=data.decode(errors='replace')
  if 'ptrcheck' in label or label=='uikit-umbrella':row['textHead']=text[:5000]
  else:row['pointerMacroLines']=[line for line in text.splitlines() if any(word in line for word in ['ptrcheck','sized_by','counted_by','unsafe_indexable'])][:30]
 receipt['files'].append(row)
source=root/'output/session-10290244/apple-direct-sdk-probe/toolchain-smoke.m'
if not source.exists():
 source=out/'probe.m';source.write_text('#import <UIKit/UIKit.h>\nint main(int argc,char **argv){@autoreleasepool{return UIApplicationMain(argc,argv,nil,nil);}}\n')
# An intact existing CLT compiler is a legitimate independent cross-compiler
# probe, with its own resource headers and the unchanged installed iOS SDK.
if clt.exists():
 for label,sdk,triple in [('clt-device',device,'arm64-apple-ios16.0'),('clt-simulator',simulator,'arm64-apple-ios16.0-simulator')]:
  run(label,[str(clt),'-target',triple,'-isysroot',str(sdk),'-fobjc-arc','-fblocks','-ferror-limit=3','-c',str(source),'-o',str(out/(label+'.o'))],15)
receipt['finishedUTC']=datetime.now(timezone.utc).isoformat()
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
