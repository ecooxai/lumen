"""Link proven UIKit objects with CLT and inventory exact Lume Apple dependencies."""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,os,struct,subprocess,time
root=Path('/Users/devin/repos/lumen')
if Path.cwd().resolve()!=root:raise RuntimeError('Unexpected workspace')
out=root/'output/session-10290244/apple-sdk-link-dependencies';out.mkdir(exist_ok=False)
receipt={'startedUTC':datetime.now(timezone.utc).isoformat(),'scope':'Unmodified CLT compiler plus installed iOS SDKs; task-local diagnostic links only. No app installation, code-signing identity import, SDK metadata edits or Mac archive substitution.','links':[],'dependencyPrefixes':[],'v8Inputs':[]}
compiler='/Library/Developer/CommandLineTools/usr/bin/clang'
xcode=Path('/Applications/Xcode-27.0-RC.app/Contents/Developer')
def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def macho(path):
 with path.open('rb') as f:
  header=f.read(32)
  if len(header)!=32:raise ValueError('Truncated Mach-O header')
  magic,cpu,subtype,kind,count,size,flags,reserved=struct.unpack('<8I',header)
  if magic!=0xfeedfacf or size>path.stat().st_size-32 or count>size//8:raise ValueError('Invalid thin Mach-O bounds')
  row={'cpuType':cpu,'fileType':kind,'loadCommandCount':count,'buildVersions':[]}
  remaining=size
  for _ in range(count):
   cmd,cmdsize=struct.unpack('<2I',f.read(8))
   if cmdsize<8 or cmdsize>remaining:raise ValueError('Invalid command length')
   body=f.read(cmdsize-8);remaining-=cmdsize
   if cmd==0x32:
    platform,minos,sdk,ntools=struct.unpack('<4I',body[:16]);row['buildVersions'].append({'platform':platform,'minimumOSPacked':minos,'sdkPacked':sdk,'toolCount':ntools})
  return row
for name,triple,sdk,expected in [('device','arm64-apple-ios16.0','iPhoneOS.platform/Developer/SDKs/iPhoneOS27.0.sdk',2),('simulator','arm64-apple-ios16.0-simulator','iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator27.0.sdk',7)]:
 obj=root/'output/session-10290244/apple-sdk-header-diagnostics'/('clt-'+name+'.o');binary=out/('uikit-'+name+'-link-probe')
 command=[compiler,'-target',triple,'-isysroot',str(xcode/'Platforms'/sdk),'-fobjc-arc','-fblocks',str(obj),'-framework','UIKit','-framework','Foundation','-o',str(binary)]
 started=time.monotonic();row={'target':name,'command':command,'objectExists':obj.exists()}
 if obj.exists():
  row['objectSHA256']=sha(obj)
  try:
   p=subprocess.run(command,capture_output=True,text=True,timeout=20);row['exitCode']=p.returncode
   (out/(name+'.stdout.txt')).write_text(p.stdout);(out/(name+'.stderr.txt')).write_text(p.stderr)
   row['stderrHead']=p.stderr[:2400]
   if p.returncode==0:
    row['binarySHA256']=sha(binary);row['binaryBytes']=binary.stat().st_size;row['macho']=macho(binary)
    row['expectedPlatformVerified']=row['macho']['cpuType']==0x0100000c and row['macho']['fileType']==2 and len(row['macho']['buildVersions'])==1 and row['macho']['buildVersions'][0]['platform']==expected
  except subprocess.TimeoutExpired:row['exitCode']='timeout'
 row['durationSeconds']=round(time.monotonic()-started,3);receipt['links'].append(row)
# These prefixes and required libraries come directly from the checked-in Makefile.
libraries=['ssl','crypto','brotlidec','brotlicommon','zstd','freetype','harfbuzz','SDL3','wgpu_native','avcodec','avformat','avutil','swscale','swresample','png','jpeg','webp','webpdemux','sharpyuv','gif','dav1d']
headers=['openssl/ssl.h','brotli/decode.h','zstd.h','freetype2/ft2build.h','harfbuzz/hb.h','SDL3/SDL.h','webgpu/webgpu.h','libavcodec/avcodec.h','png.h','jpeglib.h','webp/decode.h','gif_lib.h','dav1d/dav1d.h']
for platform in ['sim','ios','cat']:
 for prefix in [root/'build-apple-toolchain'/platform,Path('/Users/devin/ios-deps')/platform]:
  row={'platform':platform,'path':str(prefix),'exists':prefix.exists(),'archives':{},'missingLibraries':[],'missingHeaders':[]}
  for library in libraries:
   path=prefix/'lib'/('lib'+library+'.a')
   if path.is_file():row['archives'][library]={'bytes':path.stat().st_size,'sha256':sha(path)}
   else:row['missingLibraries'].append(library)
  row['missingHeaders']=[h for h in headers if not(prefix/'include'/h).is_file()]
  receipt['dependencyPrefixes'].append(row)
for base in [root/'build-apple-toolchain/v8',Path('/Users/devin/src/v8ios/v8')]:
 for platform in ['ios-device','ios-simulator','catalyst']:
  for suffix in ['', '-jit']:
   output=base/'out'/(platform+suffix)
   candidates=[base/'include/v8.h',output/'obj/libv8_monolith.a',output/'obj/v8_monolith.ninja',output/'args.gn',output/'icudtl.dat']
   receipt['v8Inputs'].append({'root':str(base),'output':str(output),'files':[{'path':str(p),'exists':p.exists(),'bytes':p.stat().st_size if p.is_file() else None} for p in candidates]})
receipt['finishedUTC']=datetime.now(timezone.utc).isoformat();(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
