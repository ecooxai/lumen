#!/usr/bin/env python3
"""Measure a fractional-span renderer build without changing the fixed workload."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

parser=argparse.ArgumentParser()
parser.add_argument('--stage',choices=['reference','candidate'],required=True)
parser.add_argument('--raster-sha256',required=True)
parser.add_argument('--fixture-sha256',required=True)
parser.add_argument('--name-prefix',default='raster-span')
args=parser.parse_args()
if not re.fullmatch(r'[a-z0-9-]+',args.name_prefix):raise SystemExit('Invalid receipt prefix')
root=Path.cwd().resolve();build='build-apple-load-20261029'
out=root/'output/session-102900614'/(args.name_prefix+'-'+args.stage+'-1')
out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat()
def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
for name,expected in [('src/paint/raster.c',args.raster_sha256),('tests/test_render_overdraw.c',args.fixture_sha256)]:
    if sha(root/name)!=expected:raise SystemExit('Reviewed source/fixture hash changed: '+name)
def source_paths():
    # Make embeds every prelude wildcard, including a newly created untracked file.
    # Git's index is not an inventory of the compiler's actual inputs.
    paths={root/'Makefile',root/'tools/prepare-webtransport-link.py',root/'assets/icon/lumen-256.png'}
    paths.update(path for path in (root/'src').rglob('*') if path.is_file())
    paths.update(path for path in (root/'ios').rglob('*') if path.is_file() and path.suffix in ('.py','.sh','.plist','.entitlements','.patch','.h','.c','.m','.cc'))
    return sorted(path for path in paths if path.is_file())
manifest={str(path.relative_to(root)):sha(path) for path in source_paths()}
(out/'compiled-source-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
start=time.monotonic();summary={'startedUTC':utc(),'scope':'Fractional rectangle-span byte equivalence and native build; fixed resource acceptance is separate.',
 'stage':args.stage,'rasterSHA256':args.raster_sha256,'fixtureSHA256':args.fixture_sha256,
 'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
 'compiledSourceManifest':str((out/'compiled-source-sha256.json').relative_to(root)),
 'prebuiltV8':'Existing pinned native Homebrew V8 reused; no dependency rebuild.', 'stages':[],'artifacts':{},'passed':False}
def save():(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
def measured(name,command,limit):
    t0=time.monotonic();begin=utc();prefix=out/name
    process=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(limit),'--settle','0','--output',str(prefix.relative_to(root)),'--',*command],
                           capture_output=True,text=True,timeout=limit+35)
    (out/(name+'-collector.log')).write_text(process.stdout+process.stderr)
    monitor=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    passed=monitor.get('natural_exit_code')==0 and not monitor.get('collector_error') and not monitor.get('ended_by_monitor')
    row={'name':name,'command':command,'startedUTC':begin,'finishedUTC':utc(),'durationSeconds':round(time.monotonic()-t0,3),'passed':passed,
         'monitor':str(prefix.with_suffix('.json').relative_to(root)),'log':str(prefix.with_suffix('.log').relative_to(root)),
         'monitorSHA256':sha(prefix.with_suffix('.json')),'logSHA256':sha(prefix.with_suffix('.log')),
         'intervalCpuSamples':monitor.get('interval_cpu_samples'),'rssPeakMiB':monitor.get('rss_peak_mb'),
         'maximumSamplingGapSeconds':monitor.get('max_sampling_gap_s'),
         'resultLines':[line for line in log.splitlines() if 'RESULTS render-overdraw' in line or 'Fractional span mismatch' in line]}
    summary['stages'].append(row);save();print(json.dumps(row),flush=True);return passed,log
targets=[build+'/test_render_overdraw',build+'/lumen']
built,_=measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*targets],180)
passed=False
if built:
    for target in targets:
        path=root/target;digest=sha(path);snapshot=path.with_name(path.name+'-'+args.name_prefix+'-'+args.stage+'-'+digest[:16])
        if snapshot.exists():raise SystemExit('Immutable raster artifact already exists')
        shutil.copy2(path,snapshot);snapshot.chmod(0o555)
        if sha(snapshot)!=digest:raise SystemExit('Copied raster artifact hash mismatch')
        summary['artifacts'][path.name]={'path':str(snapshot.relative_to(root)),'sha256':digest,'bytes':snapshot.stat().st_size}
    # Record the real compiler dependency closure after Make has regenerated all
    # embedded inputs. The system SDK is identified by the toolchain; project and
    # non-system header dependencies, generated .inc files and linked dylibs are hashed.
    dependencies=set(source_paths())
    depfiles=list((root/build/'src').rglob('*.d'))
    fixture_dep=root/build/'tests/test_render_overdraw.c.d'
    if fixture_dep.is_file():depfiles.append(fixture_dep)
    for depfile in depfiles:
        value=depfile.read_text().replace('\\\n',' ')
        if ':' not in value:raise SystemExit('Malformed compiler dependency file: '+str(depfile))
        for name in shlex.split(value.split(':',1)[1]):
            path=(root/name).resolve()
            if path.is_file():dependencies.add(path)
    linked=subprocess.check_output(['/usr/bin/otool','-L',summary['artifacts']['lumen']['path']],text=True)
    (out/'linked-libraries.txt').write_text(linked)
    for line in linked.splitlines()[1:]:
        name=line.strip().split(' (',1)[0]
        if name.startswith('/') and Path(name).is_file():dependencies.add(Path(name).resolve())
    for prefix,names in [('build-deps/rtc/macos',('lib/libdatachannel.a','lib/libjuice.a','lib/libusrsctp.a','include/rtc/rtc.h')),
                         ('build-deps/webtransport/macos',('manifest.json','lib/liblumen_webtransport_native.a','include/lumen_wt.h'))]:
        for name in names:
            path=root/prefix/name
            if path.is_file():dependencies.add(path.resolve())
    closure={str(path.relative_to(root)) if path.is_relative_to(root) else str(path):sha(path) for path in sorted(dependencies)}
    (out/'compiled-dependency-sha256.json').write_text(json.dumps(closure,indent=2)+'\n')
    summary['compiledDependencyManifest']=str((out/'compiled-dependency-sha256.json').relative_to(root))
    tracked_names=set(subprocess.check_output(['git','ls-files','-z','--','src'],text=True).split('\0'))
    summary['untrackedProductionInputs']=[name for name in manifest if name.startswith('src/') and name not in tracked_names]
    save()
    ok,log=measured('pixel-equivalence',[summary['artifacts']['test_render_overdraw']['path']],30)
    passed=ok and bool(re.search(r'RESULTS render-overdraw PASS \(\d+/\d+ checks\)',log))
for name,expected in manifest.items():
    if sha(root/name)!=expected:raise SystemExit('Source changed during compilation: '+name)
if set(manifest)!=set(str(path.relative_to(root)) for path in source_paths()):raise SystemExit('Production source files were added or removed during compilation')
if built:
    for name,expected in closure.items():
        if sha(root/name)!=expected:raise SystemExit('Compiler/linker input changed during verification: '+name)
if sha(root/'tests/test_render_overdraw.c')!=args.fixture_sha256:raise SystemExit('Fractional span fixture changed')
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3),passed=built and passed)
save();print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
