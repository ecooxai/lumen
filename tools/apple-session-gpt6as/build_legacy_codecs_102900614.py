#!/usr/bin/env python3
"""Build the exact legacy decoder candidate and measure its regression gates."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=Path('output/session-102900614/legacy-codecs-green-1'))
args = parser.parse_args()
root = Path.cwd()
out = args.output.resolve()
patch = json.loads((root/'output/session-102900614/legacy-codecs-candidate-1/patch-receipt.json').read_text())
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
for name, expected in patch['after'].items():
    if sha(root/name) != expected: raise SystemExit('Candidate source changed: '+name)
if sha(root/'tests/test_legacy_codecs.py') != patch['fixtureSHA256']:
    raise SystemExit('The frozen178-check RED fixture changed')
out.mkdir(parents=True, exist_ok=False)
started = time.monotonic()
utc = lambda: datetime.datetime.now(datetime.timezone.utc).isoformat()
tracked = subprocess.check_output(['git','ls-files','-z','--','src','Makefile','ios'],text=True).split('\0')
source = {name:sha(root/name) for name in tracked if name and (root/name).is_file()}
for name in [*patch['after'], 'src/js/prelude/06t-streams.js']:
    source[name] = sha(root/name)
(out/'compiled-source-sha256.json').write_text(json.dumps(source,indent=2)+'\n')
summary = {'startedUTC':utc(),'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
           'compiledSourceManifest':str((out/'compiled-source-sha256.json').relative_to(root)),
           'build':'build-apple-load-20261029','rtcEnabled':True,'webtransportEnabled':True,
           'prebuiltV8':'Existing platform-matched binary reused; no V8 source build.',
           'stages':[],'artifacts':{},'passed':False}

def measured(name, command, duration, marker=None):
    began=time.monotonic();prefix=out/name;start=utc()
    run=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(duration),'--settle','0',
                        '--output',str(prefix.relative_to(root)),'--',*command],
                       text=True,capture_output=True,timeout=duration+40)
    (out/(name+'-collector.log')).write_text(run.stdout+run.stderr)
    report=json.loads(prefix.with_suffix('.json').read_text())
    log=prefix.with_suffix('.log').read_text(errors='replace')
    passed=report.get('natural_exit_code')==0 and not report.get('collector_error') and not report.get('ended_by_monitor') and (marker is None or marker in log)
    row={'stage':name,'passed':passed,'startedUTC':start,'finishedUTC':utc(),
         'durationSeconds':round(time.monotonic()-began,3),'command':command,
         'monitor':str(prefix.with_suffix('.json').relative_to(root)),
         'naturalExitCode':report.get('natural_exit_code'),'endedByMonitor':report.get('ended_by_monitor'),
         'intervalCpuSamples':report.get('interval_cpu_samples'),
         'intervalCpuPeakPercent':report.get('interval_cpu_peak_percent'),'rssPeakMiB':report.get('rss_peak_mb'),
         'resultLines':[line for line in log.splitlines() if 'RESULTS' in line or '_RECEIPT ' in line],
         'performanceNote':'One-second samples are retained; short assertion tests do not establish idle or GUI performance.'}
    summary['stages'].append(row)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({'stage':name,'passed':passed,'seconds':row['durationSeconds'],'results':row['resultLines']}),flush=True)
    return passed

build=summary['build']
targets=['lumen','jsrun','test_encoding','test_encoding_encoder','test_url_encoding','test_document_encoding']
built=measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+name for name in targets]],300)
if built:
    for name in targets:
        binary=root/build/name;digest=sha(binary)
        snapshot=root/build/(name+'-legacy-codecs-'+digest[:16])
        if snapshot.exists() and sha(snapshot)!=digest: raise SystemExit('Immutable snapshot collision')
        shutil.copy2(binary,snapshot)
        if sha(snapshot)!=digest: raise SystemExit('Immutable artifact copy mismatch')
        summary['artifacts'][name]={'path':str(binary.relative_to(root)), 'snapshot':str(snapshot.relative_to(root)),
                                    'sha256':digest,'bytes':binary.stat().st_size}
    js=summary['artifacts']['jsrun']
    measured('legacy-codecs',[sys.executable,'tests/test_legacy_codecs.py','--binary',js['snapshot']],45,'RESULTS legacy-codecs PASS (178/178 checks)')
    measured('text-codec',[sys.executable,'tests/test_text_codec.py','--binary',js['snapshot']],35,'TEXT_CODEC_RECEIPT ')
    measured('text-streams',[sys.executable,'tests/test_text_streams.py','--binary',js['snapshot']],45,'RESULTS text-streams PASS (74/74 checks)')
    measured('message-transfer',[sys.executable,'tests/test_message_transfer.py','--binary',js['snapshot']],35,'RESULTS message-transfer PASS (68/68 checks)')
    for name in targets[2:]:
        profile=out/(name+'-profile');profile.mkdir()
        measured(name,['env','LUMEN_PROFILE_DIR='+str(profile),str(root/summary['artifacts'][name]['snapshot'])],90)
    sanitizer_binary=out/'test_encoding_stream_asan'
    sanitized=measured('asan-build',['clang','-std=c11','-D_DARWIN_C_SOURCE','-g','-O1','-UNDEBUG',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
        'tests/test_encoding_stream.c','src/base/encoding.c','src/base/util.c','-lm','-o',str(sanitizer_binary)],90)
    if sanitized:
        summary['artifacts']['test_encoding_stream_asan']={'path':str(sanitizer_binary.relative_to(root)),
            'sha256':sha(sanitizer_binary),'bytes':sanitizer_binary.stat().st_size}
        measured('asan-legacy-bounds',['env','ASAN_OPTIONS=halt_on_error=1','UBSAN_OPTIONS=halt_on_error=1',
            str(sanitizer_binary)],60,'RESULTS legacy-native PASS cases=459119 persistentStateBytes=44')
for name, expected in source.items():
    if sha(root/name)!=expected: raise SystemExit('Compiled source changed during gate execution: '+name)
summary['passed']=built and all(row['passed'] for row in summary['stages'])
summary['finishedUTC']=utc();summary['durationSeconds']=round(time.monotonic()-started,3)
summary['scope']='Native incremental legacy codec regressions; exact upstream WPT and GUI resource acceptance remain separate gates.'
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
