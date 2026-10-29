#!/usr/bin/env python3
"""Build and measure the guarded tokenizer EOF endpoint fix."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args();args.kind='html-eof'
root=Path.cwd();out=(args.output or Path('output/session-102900614')/(args.kind+'-green-1')).resolve()
patch_path=root/'output/session-102900614'/(args.kind+'-candidate-1')/'patch-receipt.json'
patch=json.loads(patch_path.read_text());sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if patch.get('applied') is not True:raise SystemExit('Candidate was not applied')
for name,expected in patch['after'].items():
    if sha(root/name)!=expected:raise SystemExit('Candidate source changed: '+name)
primary=('tests/test_html_meta_encoding.py','f466ea43bdb133c418df706162e4fab1f4d1b3717e9a2ab116d7745a89630468','RESULTS html-meta-encoding PASS (29/29 checks)')
if sha(root/primary[0])!=primary[1]:raise SystemExit('Frozen observable fixture changed')
if patch['fixtureSHA256']!='ce828546e389a1af1bdc25e534c79d28189af4f8927086a703f5658895fd0c50':raise SystemExit('Frozen sanitizer RED fixture changed')
bounds=json.loads((root/'output/session-102900614/html-meta-bounds-3/summary.json').read_text())
if bounds.get('passed') is not True:raise SystemExit('Tokenizer sanitizer supplement has not passed')
out.mkdir(parents=True,exist_ok=False);start=time.monotonic();utc=lambda:datetime.now(timezone.utc).isoformat()
tracked=subprocess.check_output(['git','ls-files','-z','--','src','Makefile','ios'],text=True).split('\0')
source={name:sha(root/name) for name in tracked if name and (root/name).is_file()}
for name in [*patch['after'],'src/js/prelude/06t-streams.js']:source[name]=sha(root/name)
(out/'compiled-source-sha256.json').write_text(json.dumps(source,indent=2)+'\n')
summary={'startedUTC':utc(),'kind':args.kind,'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
         'patchReceipt':str(patch_path.relative_to(root)),'patchReceiptSHA256':sha(patch_path),
         'compiledSourceManifest':str((out/'compiled-source-sha256.json').relative_to(root)),
         'build':'build-apple-load-20261029','rtcEnabled':True,'webtransportEnabled':True,
         'prebuiltV8':'Existing platform-matched archive reused; no V8 source build.',
         'artifacts':{},'stages':[],'passed':False}
def measured(name,command,duration,marker=None):
    began=time.monotonic();prefix=out/name;started=utc()
    run=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(duration),'--settle','0','--output',str(prefix.relative_to(root)),'--',*command],capture_output=True,text=True,timeout=duration+40)
    (out/(name+'-collector.log')).write_text(run.stdout+run.stderr)
    report=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    passed=report.get('natural_exit_code')==0 and not report.get('ended_by_monitor') and not report.get('collector_error') and (marker is None or marker in log)
    row={'stage':name,'startedUTC':started,'finishedUTC':utc(),'durationSeconds':round(time.monotonic()-began,3),'passed':passed,'command':command,
         'monitor':str(prefix.with_suffix('.json').relative_to(root)),'naturalExitCode':report.get('natural_exit_code'),'endedByMonitor':report.get('ended_by_monitor'),
         'intervalCpuSamples':report.get('interval_cpu_samples'),'intervalCpuPeakPercent':report.get('interval_cpu_peak_percent'),'rssPeakMiB':report.get('rss_peak_mb'),
         'resultLines':[line for line in log.splitlines() if 'RESULTS' in line or '_RECEIPT ' in line],
         'performanceNote':'One-second samples retained; short regression tests do not establish GUI or idle performance.'}
    summary['stages'].append(row);(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(row),flush=True);return passed
build=summary['build'];targets=['lumen','jsrun','test_encoding','test_encoding_encoder','test_url_encoding','test_document_encoding','test_parser_input']
built=measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+name for name in targets]],300)
if built:
    for name in targets:
        binary=root/build/name;digest=sha(binary);snapshot=root/build/(name+'-'+args.kind+'-'+digest[:16])
        if snapshot.exists() and sha(snapshot)!=digest:raise SystemExit('Immutable snapshot collision')
        shutil.copy2(binary,snapshot)
        if sha(snapshot)!=digest:raise SystemExit('Immutable snapshot mismatch')
        summary['artifacts'][name]={'path':str(binary.relative_to(root)),'snapshot':str(snapshot.relative_to(root)),'sha256':digest,'bytes':binary.stat().st_size}
    js=summary['artifacts']['jsrun']['snapshot']
    measured(args.kind,[sys.executable,primary[0],'--binary',js],45,primary[2])
    measured('xhr-encoding',[sys.executable,'tests/test_xhr_response_encoding.py','--binary',js],45,'RESULTS xhr-response-encoding PASS (42/42 checks)')
    measured('frame-load-tasks',[sys.executable,'tests/test_frame_load_tasks.py','--binary',js],35,'RESULTS frame-load-tasks PASS (7/7 checks)')
    for name,path,count,duration in [('legacy-codecs','tests/test_legacy_codecs.py',178,45),('text-streams','tests/test_text_streams.py',74,45),('message-transfer','tests/test_message_transfer.py',68,35)]:
        measured(name,[sys.executable,path,'--binary',js],duration,'RESULTS '+name+' PASS ('+str(count)+'/'+str(count)+' checks)')
    measured('text-codec',[sys.executable,'tests/test_text_codec.py','--binary',js],35,'TEXT_CODEC_RECEIPT ')
    for name in targets[2:]:
        profile=out/(name+'-profile');profile.mkdir()
        measured(name,['env','LUMEN_PROFILE_DIR='+str(profile),str(root/summary['artifacts'][name]['snapshot'])],90)
for name,expected in source.items():
    if sha(root/name)!=expected:raise SystemExit('Compiled source changed during gates: '+name)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3),passed=built and all(stage['passed'] for stage in summary['stages']),
               scope='Native regression gates only; unchanged upstream WPT, browser UI resource acceptance and Apple device packaging are separate gates.')
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
