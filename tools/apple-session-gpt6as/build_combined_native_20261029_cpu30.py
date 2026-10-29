from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time

root=Path.cwd().resolve()
out=root/'out/lume-20261029-cpu30/native-combined-1'
out.mkdir(parents=True,exist_ok=False)
build='build-apple-load-20261029'
utc=lambda:datetime.now(timezone.utc).isoformat()
def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def save():
    pending=out/'summary.tmp';pending.write_text(json.dumps(summary,indent=2)+'\n');pending.replace(out/'summary.json')
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
branch=subprocess.check_output(['git','branch','--show-current'],text=True).strip()
if branch!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-20261029-cpu30':raise SystemExit('Wrong task branch')
tracked=subprocess.check_output(['git','ls-files','-z','--','src','Makefile','ios'],text=True).split('\0')
sources={name:sha(root/name) for name in tracked if name and (root/name).is_file()}
fixtures={name:sha(root/name) for name in ['tests/test_render_overdraw.c','tests/test_gpu_present.c','tests/test_app_wheel.c']}
(out/'compiled-source-sha256.json').write_text(json.dumps(sources,indent=2)+'\n')
started=time.monotonic()
summary={'startedUTC':utc(),'sourceBaseCommit':head,'branch':branch,'compiledSourceManifest':str((out/'compiled-source-sha256.json').relative_to(root)),
 'sourceManifestSHA256':sha(out/'compiled-source-sha256.json'),'fixtures':fixtures,'buildCache':build,'artifactOutput':str(out.relative_to(root)),
 'prebuiltV8':'Reusing existing native Homebrew V8; no V8 source build. All immutable deliverable binaries are written under out/.',
 'scope':'Combined current browser source and inherited chrome-cache regression verification; WPT and native resource gates remain separate.',
 'stages':[],'artifacts':{},'passed':False}
save()

def measured(name,command,maximum):
    prefix=out/name;began=time.monotonic()
    p=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(maximum),'--settle','0','--output',str(prefix.relative_to(root)),'--',*command],capture_output=True,text=True,timeout=maximum+35)
    (out/(name+'-collector.log')).write_text(p.stdout+p.stderr)
    monitor=json.loads(prefix.with_suffix('.json').read_text())
    log=prefix.with_suffix('.log').read_text(errors='replace')
    ok=monitor.get('natural_exit_code')==0 and not monitor.get('collector_error') and not monitor.get('ended_by_monitor')
    row={'name':name,'durationSeconds':round(time.monotonic()-began,3),'passed':ok,'command':command,'monitor':str(prefix.with_suffix('.json').relative_to(root)),
      'monitorSHA256':sha(prefix.with_suffix('.json')),'log':str(prefix.with_suffix('.log').relative_to(root)),'logSHA256':sha(prefix.with_suffix('.log')),
      'intervalCpuSamples':monitor.get('interval_cpu_samples'),'intervalCpuPeakPercent':monitor.get('interval_cpu_peak_percent'),'rssPeakMiB':monitor.get('rss_peak_mb'),
      'maximumSamplingGapSeconds':monitor.get('max_sampling_gap_s'),'resultLines':[line for line in log.splitlines() if re.search(r'checks|failures|passed|PASS|FAIL',line)][-12:]}
    summary['stages'].append(row);save();print(json.dumps(row),flush=True)
    return ok

try:
    targets=['lumen','test_render_overdraw','test_gpu_present','test_app_wheel']
    if not measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+name for name in targets]],180):
        raise RuntimeError('Combined build failed; inspected build log required')
    for name in targets:
        artifact=root/build/name;digest=sha(artifact)
        snapshot=out/(name+'-'+digest[:16])
        shutil.copy2(artifact,snapshot);snapshot.chmod(0o555)
        if sha(snapshot)!=digest:raise RuntimeError('Immutable snapshot copy changed')
        summary['artifacts'][name]={'path':str(snapshot.relative_to(root)),'sha256':digest,'bytes':snapshot.stat().st_size}
    save()
    for name in targets[1:]:
        if not measured(name,[str(root/summary['artifacts'][name]['path'])],30):raise RuntimeError(name+' failed')
    for name,expected in {**sources,**fixtures}.items():
        if sha(root/name)!=expected:raise RuntimeError('Source changed during build/regressions: '+name)
    binary=root/summary['artifacts']['lumen']['path']
    linked=subprocess.check_output(['otool','-L',str(binary)],text=True)
    (out/'linked-libraries.txt').write_text(linked)
    summary['linkedV8']=[line.strip() for line in linked.splitlines() if 'v8' in line.lower()]
    summary['passed']=True
except Exception as exc:
    summary['error']=str(exc)
finally:
    summary['finishedUTC']=utc();summary['durationSeconds']=round(time.monotonic()-started,3);save()
    print(json.dumps({'passed':summary['passed'],'artifacts':summary['artifacts'],'summary':str((out/'summary.json').relative_to(root)),'durationSeconds':summary['durationSeconds'],'error':summary.get('error')}),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
