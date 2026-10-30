#!/usr/bin/env python3
"""Bounded read-only inventory of this project's Apple artifacts and tool paths."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import plistlib
import struct
import subprocess
import time
import zipfile

root=Path.cwd().resolve()
if root!=Path('/Users/devin/repos/lumen'):
    raise SystemExit('Run only in the authorized Lumen project')
out=root/'output/session-102900614/apple-artifact-inventory'
out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat()
start=time.monotonic()
receipt={'startedUTC':utc(),'scope':'Read-only project IPA/app and declared Apple developer/dependency paths. No signing, imports, installation, runtime reset, security change, or substitute platform claim.',
         'commands':[],'toolchains':[],'sdks':[],'dependencyPrefixes':[],'ipaCandidates':[],'appCandidates':[],
         'nativeMacProven':True,'sameSignedDeviceIPAProven':False,'currentSimulatorAppProven':False}
def run(command,timeout=8,limit=12000):
    t0=time.monotonic();row={'command':command,'startedUTC':utc()}
    try:
        p=subprocess.run(command,capture_output=True,text=True,timeout=timeout,stdin=subprocess.DEVNULL)
        row.update(exitCode=p.returncode,stdout=p.stdout[:limit],stderr=p.stderr[:limit],stdoutTruncated=len(p.stdout)>limit)
    except (OSError,subprocess.TimeoutExpired) as error:
        row.update(error=type(error).__name__,detail=str(error)[:300])
    row.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-t0,3));receipt['commands'].append(row)
    return row
def metadata(path):
    result={'path':str(path),'exists':path.exists(),'directory':path.is_dir()}
    if path.is_file():result['bytes']=path.stat().st_size
    return result
def app_info(path):
    row=metadata(path)
    try:
        data=plistlib.loads(path.read_bytes());row.update(bundleIdentifier=data.get('CFBundleIdentifier'),
          platforms=data.get('CFBundleSupportedPlatforms'),families=data.get('UIDeviceFamily'),minimumOS=data.get('MinimumOSVersion'))
        executable=data.get('CFBundleExecutable')
        if isinstance(executable,str) and '/' not in executable:
            binary=path.parent/executable
            row['executable']=metadata(binary)
            if binary.is_file():
                with binary.open('rb') as stream:row['executable']['sha256']=hashlib.file_digest(stream,'sha256').hexdigest()
        row['embeddedProvisioningPresent']=(path.parent/'embedded.mobileprovision').is_file()
        row['signatureResourcesPresent']=(path.parent/'_CodeSignature/CodeResources').is_file()
    except (OSError,ValueError,plistlib.InvalidFileException) as error:row['inspectionError']=type(error).__name__+': '+str(error)
    return row

run(['/usr/bin/xcode-select','-p'])
run(['/usr/bin/security','find-identity','-v','-p','codesigning'],limit=2000)
toolchains=[Path('/Library/Developer/CommandLineTools')]
toolchains.extend(sorted(Path('/Applications').glob('Xcode*.app/Contents/Developer')))
for developer in toolchains:
    row=metadata(developer);row['clang']=metadata(developer/'usr/bin/clang');row['xcodebuild']=metadata(developer/'usr/bin/xcodebuild')
    receipt['toolchains'].append(row)
    candidates=sorted((developer/'SDKs').glob('*.sdk'))+sorted(developer.glob('Platforms/*/Developer/SDKs/*.sdk'))
    for sdk in candidates:
        info=metadata(sdk);settings=sdk/'SDKSettings.plist'
        if settings.is_file():
            try:
                data=plistlib.loads(settings.read_bytes());info['canonicalName']=data.get('CanonicalName');info['version']=data.get('Version')
            except (OSError,ValueError,plistlib.InvalidFileException) as error:info['settingsError']=type(error).__name__
        receipt['sdks'].append(info)
    if (developer/'usr/bin/xcodebuild').is_file():run([str(developer/'usr/bin/xcodebuild'),'-version'],timeout=6,limit=1200)

prefixes=[root/'build-apple-toolchain'/name for name in ('sim','ios','cat')]
prefixes += [Path('/Users/devin/ios-deps')/name for name in ('sim','ios','cat')]
required=['libv8_monolith.a','libwgpu_native.a','libSDL3.a','libfreetype.a','libharfbuzz.a','libssl.a','libcrypto.a',
          'libcurl.a','libxml2.a','libz.a','libavcodec.a','libavformat.a','libavutil.a','libswscale.a','libswresample.a',
          'libpng.a','libjpeg.a','libwebp.a','libwebpdemux.a','libsharpyuv.a','libgif.a','libdav1d.a']
for prefix in prefixes:
    row=metadata(prefix);row['archives']={name:(prefix/'lib'/name).is_file() for name in required};receipt['dependencyPrefixes'].append(row)
for suffix in ('ios-simulator','ios-device','catalyst','ios-simulator-jit','ios-device-jit','catalyst-jit'):
    path=root/'build-apple-toolchain/v8/out'/suffix/'obj/libv8_monolith.a'
    receipt.setdefault('prebuiltV8Candidates',[]).append(metadata(path))
run(['/usr/bin/file','/opt/homebrew/opt/v8/lib/libv8.dylib'],timeout=5,limit=1200)

command=['rg','--files','--hidden','--no-ignore','-g','*.ipa','-g','*.mobileprovision',
         '-g','!.git/**','-g','!node_modules/**','-g','!build-apple-toolchain/v8/third_party/**',
         '-g','!build-apple-toolchain/depot_tools/**','-g','!.cache/**','.']
found=run(command,timeout=20,limit=16000)
for name in found.get('stdout','').splitlines()[:20]:
    path=(root/name).resolve()
    if not path.is_relative_to(root) or not path.is_file():continue
    row=metadata(path)
    if path.suffix=='.ipa':
        if path.stat().st_size<=512*1024*1024:
            with path.open('rb') as stream:row['sha256']=hashlib.file_digest(stream,'sha256').hexdigest()
            try:
                with zipfile.ZipFile(path) as archive:
                    names=archive.namelist();infos=[name for name in names if name.startswith('Payload/') and name.endswith('.app/Info.plist') and name.count('/')==2]
                    row['appInfoCount']=len(infos)
                    if len(infos)==1 and archive.getinfo(infos[0]).file_size<1024*1024:
                        data=plistlib.loads(archive.read(infos[0]));base=infos[0].rsplit('/',1)[0]
                        row.update(bundleIdentifier=data.get('CFBundleIdentifier'),platforms=data.get('CFBundleSupportedPlatforms'),families=data.get('UIDeviceFamily'),
                                   embeddedProvisioningPresent=base+'/embedded.mobileprovision' in names,signatureResourcesPresent=base+'/_CodeSignature/CodeResources' in names)
                        executable=data.get('CFBundleExecutable')
                        if isinstance(executable,str) and '/' not in executable and base+'/'+executable in names:
                            with archive.open(base+'/'+executable) as stream:row['machOHeaderHex']=stream.read(32).hex()
                        row['signatureValidity']='Not verified by ZIP metadata; OS/codesign validation still required before launch'
            except (OSError,ValueError,zipfile.BadZipFile,plistlib.InvalidFileException) as error:row['inspectionError']=type(error).__name__+': '+str(error)
        else:row['inspectionLimit']='Archive larger than bounded512MiB hash/metadata scope'
        receipt['ipaCandidates'].append(row)
    else:receipt.setdefault('profileCandidatePaths',[]).append(row)

app_paths=set(root.glob('build*/Lumen.app/Info.plist'))
device=Path('/Users/devin/Library/Developer/CoreSimulator/Devices/3DD498EB-4464-4ED7-9CD3-7667889DD1F6')
container=device/'data/Containers/Bundle/Application'
if container.is_dir():
    for folder in container.iterdir():
        if folder.is_dir():
            for app in folder.iterdir():
                if app.is_dir() and app.suffix=='.app' and 'lume' in app.stem.lower():app_paths.add(app/'Info.plist')
receipt['appCandidates']=[app_info(path) for path in sorted(app_paths)]
receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3))
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt))
