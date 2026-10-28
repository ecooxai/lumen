"""Build and verify only the queued-frame completion delta against a frozen combined base."""
from pathlib import Path
import argparse, datetime, hashlib, json, os, shutil, subprocess, sys, time
p=argparse.ArgumentParser();p.add_argument('--base-manifest',type=Path,required=True);p.add_argument('--output',type=Path,default=Path('output/session-102900614/frame-completion-green-1'));a=p.parse_args()
root=Path.cwd();sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest();utc=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat()
patch_path=root/'output/session-102900614/frame-completion-candidate-1/patch-receipt.json';patch=json.loads(patch_path.read_text());before=json.loads(a.base_manifest.read_text())
if set(patch['after'])!={'src/js/js.cc'}:raise RuntimeError('Frame source delta differs')
if before['src/js/js.cc']!=patch['before']['src/js/js.cc'] or sha(patch['fixture'])!=patch['fixtureSHA256']:raise RuntimeError('Base completion source or frozen fixture differs')
existing_fixtures={'tests/test_frame_load_order.py':'9d825427b005e0ff6ffcee6c6261b01564f7506c2296f896b8b493d702a428db','tests/test_frame_microtask_realm.py':'aeb86f23ff1c3d49fb3d10842db96504b0be2d342241fdfea1df06594e580238'}
for name,digest in existing_fixtures.items():
    if sha(root/name)!=digest:raise RuntimeError('Existing frame regression fixture differs: '+name)
source=dict(before);source.update(patch['after'])
for name,digest in source.items():
    if sha(root/name)!=digest:raise RuntimeError('Only the reviewed frame delta may differ from the frozen combined base: '+name)
a.output.mkdir(parents=True,exist_ok=False);manifest=a.output/'compiled-source-sha256.json';manifest.write_text(json.dumps(source,indent=2)+'\n')
summary={'schema':1,'taskId':'102900614','startedUTC':utc(),'sourceBaseCommit':subprocess.check_output(['git','--no-pager','rev-parse','HEAD'],text=True).strip(),'baseCompiledSourceManifest':str(a.base_manifest),'baseCompiledSourceManifestSHA256':sha(a.base_manifest),'compiledSourceManifest':str(manifest),'changedCompiledSources':['src/js/js.cc'],'framePatchReceipt':str(patch_path.relative_to(root)),'framePatchReceiptSHA256':sha(patch_path),'fixtureSHA256':patch['fixtureSHA256'],'prebuiltV8':'Existing matched V8 reused; no V8 compilation.','stages':[],'artifacts':{},'passed':False}
started=time.monotonic();build='build-apple-load-20261029'
def measured(name,command,duration,marker=None):
    prefix=a.output/name;begin=time.monotonic();start=utc()
    run=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(duration),'--settle','0','--output',str(prefix),'--',*command],text=True,capture_output=True,timeout=duration+40)
    (a.output/(name+'-collector.log')).write_text(run.stdout+run.stderr)
    monitor=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    passed=monitor.get('natural_exit_code')==0 and not monitor.get('collector_error') and not monitor.get('ended_by_monitor') and (marker is None or marker in log)
    row={'stage':name,'startedUTC':start,'finishedUTC':utc(),'durationSeconds':round(time.monotonic()-begin,3),'command':command,'passed':passed,'monitor':str(prefix.with_suffix('.json')),'logSHA256':sha(prefix.with_suffix('.log')),'naturalExitCode':monitor.get('natural_exit_code'),'endedByMonitor':monitor.get('ended_by_monitor'),'intervalCpuSamples':monitor.get('interval_cpu_samples'),'rssPeakMiB':monitor.get('rss_peak_mb'),'resultLines':[line for line in log.splitlines()if 'RESULTS'in line or '_RECEIPT 'in line]}
    summary['stages'].append(row);(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({'stage':name,'passed':passed,'durationSeconds':row['durationSeconds'],'results':row['resultLines']}),flush=True);return passed
targets=['lumen','jsrun','test_frame_removal','test_window_lifecycle']
built=measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+name for name in targets]],300)
if built:
    for name in targets:
        path=root/build/name;digest=sha(path);snapshot=root/build/(name+'-frame-completion-'+digest[:16]);shutil.copy2(path,snapshot)
        if sha(snapshot)!=digest:raise RuntimeError('Frozen artifact copy differs')
        summary['artifacts'][name]={'snapshot':str(snapshot.relative_to(root)),'sha256':digest,'bytes':snapshot.stat().st_size}
    js=summary['artifacts']['jsrun']['snapshot']
    measured('frame-load-tasks',[sys.executable,'tests/test_frame_load_tasks.py','--binary',js],25,'RESULTS frame-load-tasks PASS (7/7 checks)')
    measured('frame-load-order',[sys.executable,'tests/test_frame_load_order.py','--binary',js,'--runtime','jsrun'],30,'RESULTS frame-load-order jsrun PASS (14/14 checks)')
    measured('frame-microtask-realm',[sys.executable,'tests/test_frame_microtask_realm.py','--binary',js],30,'FRAME_MICROTASK_RECEIPT ')
    for name in targets[2:]:
        measured(name,['env','LUMEN_PROFILE_DIR='+str((a.output/(name+'-profile')).resolve()),str(root/summary['artifacts'][name]['snapshot'])],90)
    measured('text-streams',[sys.executable,'tests/test_text_streams.py','--binary',js],45,'RESULTS text-streams PASS (74/74 checks)')
    measured('message-transfer',[sys.executable,'tests/test_message_transfer.py','--binary',js],35,'RESULTS message-transfer PASS (68/68 checks)')
    if (root/'tests/test_legacy_codecs.py').is_file():
        measured('legacy-codecs',[sys.executable,'tests/test_legacy_codecs.py','--binary',js],45,'RESULTS legacy-codecs PASS (178/178 checks)')
for name,digest in source.items():
    if sha(root/name)!=digest:raise RuntimeError('Source changed during the frozen frame build/test gate: '+name)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-started,3),passed=built and all(row['passed']for row in summary['stages']),scope='Queued child-document completion and native frame/window lifecycle regression. Original upstream WPT, cross-realm Encoding Streams and final combined resources/soak remain separate acceptance gates.')
(a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True);raise SystemExit(0 if summary['passed']else 1)
