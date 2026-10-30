#!/usr/bin/env python3
"""Validate observed Xcode tool paths using a child-only developer directory."""
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import re
import subprocess
import time

root=Path.cwd().resolve()
if root!=Path('/Users/devin/repos/lumen'):raise SystemExit('Unexpected project root')
developer=Path('/Applications/Xcode-27.0-RC.app/Contents/Developer')
if not developer.is_dir():raise SystemExit('Observed Xcode installation changed')
out=root/'output/session-102900614/apple-toolchain-supplement'
out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat();started=time.monotonic()
environment=dict(os.environ);environment['DEVELOPER_DIR']=str(developer)
receipt={'startedUTC':utc(),'scope':'Read-only existing Xcode/declared V8 tool paths. DEVELOPER_DIR is set only for these child commands. Global xcode-select, signing identities, trust settings and runtimes are untouched.',
         'developerDirectory':str(developer),'commands':[],'declaredV8Paths':[]}
def run(command,timeout=7):
    begin=time.monotonic();row={'command':command,'startedUTC':utc()}
    try:
        process=subprocess.run(command,capture_output=True,text=True,timeout=timeout,env=environment,stdin=subprocess.DEVNULL)
        row.update(exitCode=process.returncode,stdout=process.stdout[:3500],stderr=process.stderr[:1500],stdoutTruncated=len(process.stdout)>3500)
    except (OSError,subprocess.TimeoutExpired) as error:row.update(error=type(error).__name__,detail=str(error)[:300])
    row.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-begin,3));receipt['commands'].append(row);return row
for sdk in ('iphoneos','iphonesimulator'):
    compiler=run(['/usr/bin/xcrun','--sdk',sdk,'--find','clang'])
    run(['/usr/bin/xcrun','--sdk',sdk,'--show-sdk-path'])
    path=Path(compiler.get('stdout','').strip())
    if compiler.get('exitCode')==0 and path.is_absolute() and path.is_file():run([str(path),'--version'],5)
for utility in ('simctl','devicectl'):run(['/usr/bin/xcrun','--find',utility])
canonical=developer/'Toolchains/XcodeDefault.xctoolchain/usr/bin/clang'
receipt['canonicalXcodeCompiler']={'path':str(canonical),'exists':canonical.exists(),'bytes':canonical.stat().st_size if canonical.is_file() else None}
for base in (root/'build-apple-toolchain/v8',Path('/Users/devin/src/v8ios/v8')):
    for variant in ('ios-simulator','ios-device','catalyst','ios-simulator-jit','ios-device-jit','catalyst-jit'):
        archive=base/'out'/variant/'obj/libv8_monolith.a';args=base/'out'/variant/'args.gn';header=base/'include/v8-version.h'
        row={'root':str(base),'variant':variant,'archive':str(archive),'exists':archive.is_file(),
             'archiveBytes':archive.stat().st_size if archive.is_file() else None,'versionHeaderPresent':header.is_file()}
        if header.is_file():
            version=header.read_text(errors='replace')
            row['v8VersionMacros']={key:value for key,value in re.findall(r'^#define\s+(V8_(?:MAJOR_VERSION|MINOR_VERSION|BUILD_NUMBER|PATCH_LEVEL))\s+(\d+)\s*$',version,re.M)}
        if args.is_file():
            row['platformConfiguration']=[line.strip() for line in args.read_text(errors='replace').splitlines()
              if re.match(r'^\s*(target_os|target_cpu|target_environment|v8_jitless|v8_enable_pointer_compression|v8_enable_sandbox|v8_enable_drumbrake)\s*=',line)]
        receipt['declaredV8Paths'].append(row)
receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-started,3))
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
