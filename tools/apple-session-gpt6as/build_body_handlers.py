"""Build a frozen body-handler delta and run scoped DOM/realm/codec regressions."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,shutil,subprocess,sys,time
p=argparse.ArgumentParser();p.add_argument('--base-manifest',type=Path,required=True);p.add_argument('--output',type=Path,default=Path('output/session-102900614/body-window-handlers-green-1'));a=p.parse_args()
ROOT=Path.cwd();sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest();utc=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat()
patchpath=ROOT/'output/session-102900614/body-window-handlers-candidate-1/patch-receipt.json';patch=json.loads(patchpath.read_text());base=json.loads(a.base_manifest.read_text());source=dict(base)
if set(patch['after'])!={'src/js/prelude/01-events.js','src/js/prelude/03-element.js','src/js/prelude/08-window.js','src/js/jsnative.cc'}:raise RuntimeError('Unexpected body handler source scope')
for name,digest in patch['before'].items():
    if base[name]!=digest:raise RuntimeError('Frozen combined base differs: '+name)
source.update(patch['after'])
for name,digest in source.items():
    if sha(ROOT/name)!=digest:raise RuntimeError('Only reviewed body delta may differ: '+name)
fixtures={'tests/test_borrowed_window_handlers.py':'3cf3649d288b9cd810b727b33c3af500030d143187115aa1045b54458c590e6c','tests/test_body_window_handlers.py':'04bbd4c2d5044b0d402f4c6354d98e6fab1cc70f94ccfa0d7c4963da4f8948e9','tests/test_frame_load_tasks.py':'64edfda6febcfe6d4c634faeae0587024a4b5df57fdbd79746e2ed08f2694ed8','tests/test_frame_load_order.py':'9d825427b005e0ff6ffcee6c6261b01564f7506c2296f896b8b493d702a428db','tests/test_frame_microtask_realm.py':'aeb86f23ff1c3d49fb3d10842db96504b0be2d342241fdfea1df06594e580238'}
for name,digest in fixtures.items():
    if sha(ROOT/name)!=digest:raise RuntimeError('Frozen body/frame fixture changed: '+name)
a.output.mkdir(parents=True,exist_ok=False);manifest=a.output/'compiled-source-sha256.json';manifest.write_text(json.dumps(source,indent=2)+'\n')
summary={'schema':1,'taskId':'102900614','startedUTC':utc(),'sourceBaseCommit':subprocess.check_output(['git','--no-pager','rev-parse','HEAD'],text=True).strip(),'compiledSourceManifest':str(manifest),'baseCompiledSourceManifest':str(a.base_manifest),'baseCompiledSourceManifestSHA256':sha(a.base_manifest),'changedCompiledSources':list(patch['after']),'patchReceipt':str(patchpath.relative_to(ROOT)),'patchReceiptSHA256':sha(patchpath),'fixtureSHA256':fixtures,'prebuiltV8':'Existing matched V8 reused; no V8 compilation.','stages':[],'artifacts':{},'passed':False}
started=time.monotonic();build='build-apple-load-20261029'
def measured(name,command,duration,marker=None):
    prefix=a.output/name;begin=time.monotonic();start=utc()
    run=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(duration),'--settle','0','--output',str(prefix),'--',*command],text=True,capture_output=True,timeout=duration+40)
    (a.output/(name+'-collector.log')).write_text(run.stdout+run.stderr)
    monitor=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    passed=monitor.get('natural_exit_code')==0 and not monitor.get('collector_error') and not monitor.get('ended_by_monitor') and (marker is None or marker in log)
    row={'stage':name,'startedUTC':start,'finishedUTC':utc(),'durationSeconds':round(time.monotonic()-begin,3),'command':command,'passed':passed,'monitor':str(prefix.with_suffix('.json')),'logSHA256':sha(prefix.with_suffix('.log')),'naturalExitCode':monitor.get('natural_exit_code'),'endedByMonitor':monitor.get('ended_by_monitor'),'intervalCpuSamples':monitor.get('interval_cpu_samples'),'rssPeakMiB':monitor.get('rss_peak_mb'),'resultLines':[line for line in log.splitlines()if 'RESULTS'in line or '_RECEIPT 'in line]}
    summary['stages'].append(row);(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({'stage':name,'passed':passed,'durationSeconds':row['durationSeconds'],'results':row['resultLines']}),flush=True);return passed
targets=['lumen','jsrun','test_window_lifecycle']
built=measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+name for name in targets]],300)
if built:
    for name in targets:
        path=ROOT/build/name;digest=sha(path);snapshot=ROOT/build/(name+'-body-handlers-'+digest[:16]);shutil.copy2(path,snapshot)
        if sha(snapshot)!=digest:raise RuntimeError('Immutable artifact copy differs')
        summary['artifacts'][name]={'snapshot':str(snapshot.relative_to(ROOT)),'sha256':digest,'bytes':snapshot.stat().st_size}
    js=summary['artifacts']['jsrun']['snapshot']
    measured('body-window-handlers',[sys.executable,'tests/test_body_window_handlers.py','--binary',js],35,'RESULTS body-window-handlers PASS (45/45 checks)')
    measured('borrowed-window-handlers',[sys.executable,'tests/test_borrowed_window_handlers.py','--binary',js],25,'RESULTS borrowed-window-handlers PASS (4/4 checks)')
    measured('frame-load-tasks',[sys.executable,'tests/test_frame_load_tasks.py','--binary',js],25,'RESULTS frame-load-tasks PASS (7/7 checks)')
    measured('frame-load-order',[sys.executable,'tests/test_frame_load_order.py','--binary',js,'--runtime','jsrun'],30,'RESULTS frame-load-order jsrun PASS (14/14 checks)')
    measured('frame-microtask-realm',[sys.executable,'tests/test_frame_microtask_realm.py','--binary',js],30,'FRAME_MICROTASK_RECEIPT ')
    measured('window-lifecycle',['env','LUMEN_PROFILE_DIR='+str((a.output/'window-lifecycle-profile').resolve()),str(ROOT/summary['artifacts']['test_window_lifecycle']['snapshot'])],90)
    measured('message-transfer',[sys.executable,'tests/test_message_transfer.py','--binary',js],35,'RESULTS message-transfer PASS (68/68 checks)')
    measured('text-streams',[sys.executable,'tests/test_text_streams.py','--binary',js],45,'RESULTS text-streams PASS (74/74 checks)')
    measured('legacy-codecs',[sys.executable,'tests/test_legacy_codecs.py','--binary',js],45,'RESULTS legacy-codecs PASS (178/178 checks)')
for name,digest in source.items():
    if sha(ROOT/name)!=digest:raise RuntimeError('Source changed during frozen body build/regression: '+name)
for name,digest in fixtures.items():
    if sha(ROOT/name)!=digest:raise RuntimeError('Frozen fixture changed during verification: '+name)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-started,3),passed=built and all(row['passed']for row in summary['stages']),scope='Body/frameset handler alias and listener activation with unchanged owner CSP, native lifecycle, frame realm and codec regressions. Original/upstream WPT and final same-binary resource/soak gates remain separate.')
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True);raise SystemExit(0 if summary['passed']else 1)
