from pathlib import Path
from datetime import datetime, timezone
import hashlib,json,os,re,shutil,subprocess,sys,time
root=Path.cwd().resolve()
if str(root)!='/Users/devin/repos/lumen':raise SystemExit('Unexpected workspace')
branch=subprocess.check_output(['git','branch','--show-current'],text=True).strip()
if branch!='gpt6-astra-pro_builtin-agents_lumen-10.292019r-worker-xhr':raise SystemExit('Wrong task branch')
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
if head!='dc7a137d79551df3f5d3dc5cc914d06f0b7be468':raise SystemExit('Unexpected compiled source commit '+head)
out=root/'out/10292019r-mac/native-worker-004';out.mkdir(parents=True,exist_ok=False)
build='build-apple-load-20261029'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def utc():return datetime.now(timezone.utc).isoformat()
accepted=root/'out/lume-20261029-cpu30/native-dsd-2/lumen-c259da2597a6e064'
if not accepted.is_file() or sha(accepted)!='c259da2597a6e064764d7cec8c64edb5c935a99edad9c259f08dac7dbc5a3849':raise SystemExit('Known accepted Mac baseline missing or changed; preserve discovery before build')
base=out.parent/'baseline/lumen-accepted-c259da2597a6e064';base.parent.mkdir(parents=True,exist_ok=True)
if base.exists() and sha(base)!=sha(accepted):raise SystemExit('Existing accepted baseline differs')
if not base.exists():shutil.copy2(accepted,base);base.chmod(0o555)
names=subprocess.check_output(['git','ls-files','-z','--','src','Makefile','ios','tests/jsrun.c'],text=True).split('\0')
sources={n:sha(root/n) for n in names if n and (root/n).is_file()}
fixtures={n:sha(root/n)for n in ['tests/test_worker_async_lifecycle.cc','tests/test_worker_lifecycle.cc','tests/test_net_background.c','tests/test_net_cancel.c','tests/test_worker_response_headers.py','tests/test_worker_progress_event.html','tests/test_worker_progress_event.py','tests/test_blob_range_whitespace.py','tests/test_sync_xhr_watchdog.py','tests/test_formdata_realm.py','tests/test_form_navigation.cc','tests/test_form_data_navigation.cc','tests/test_csp_inherit.c','tests/test_data_form_csp.py','tests/test_parser_handler_policy.py','tests/fixtures/parser-handler-policy.html','tests/test_body_window_handlers.py','tests/test_borrowed_window_handlers.py','tests/test_parser_handler_csp.py','tests/jsrun.c']}
(out/'compiled-source-sha256.json').write_text(json.dumps(sources,indent=2)+'\n')
summary={'startedUTC':utc(),'sourceBaseCommit':head,'branch':branch,'platform':'Darwin arm64','compiledSourceManifest':str((out/'compiled-source-sha256.json').relative_to(root)),'sourceManifestSHA256':sha(out/'compiled-source-sha256.json'),'fixtures':fixtures,'buildCache':build,'acceptedBaseline':{'path':str(base.relative_to(root)),'sha256':sha(base)},'prebuiltV8':'Existing /opt/homebrew/opt/v8; no V8 source build','rtc':True,'webtransport':True,'stages':[],'artifacts':{},'passed':False,'scope':'Native Mac worker regressions; WPT and GUI/IPA acceptance are separate.'}
started=time.monotonic()
def save():(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
def measured(name,command,limit):
 prefix=out/name;begun=time.monotonic()
 p=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(limit),'--settle','0','--output',str(prefix.relative_to(root)),'--',*command],capture_output=True,text=True,timeout=limit+35)
 (out/(name+'-collector.log')).write_text(p.stdout+p.stderr)
 report=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
 ok=report.get('natural_exit_code')==0 and not report.get('collector_error') and not report.get('ended_by_monitor') and not report.get('owned_group_cleanup',{}).get('error')
 row={'name':name,'startedUTC':report.get('started_utc'),'finishedUTC':report.get('finished_utc'),'durationSeconds':round(time.monotonic()-begun,3),'passed':ok,'command':command,'monitor':str(prefix.with_suffix('.json').relative_to(root)),'monitorSHA256':sha(prefix.with_suffix('.json')),'log':str(prefix.with_suffix('.log').relative_to(root)),'logSHA256':sha(prefix.with_suffix('.log')),'resultLines':[line for line in log.splitlines()if line.startswith(('RESULTS','FAIL'))][-20:]}
 summary['stages'].append(row);save();return ok
save()
try:
 targets=['lumen','jsrun','test_worker_lifecycle','test_worker_async_lifecycle','test_net_cancel','test_net_background','test_form_navigation','test_form_data_navigation','test_csp_inherit']
 if not measured('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+n for n in targets]],240):raise RuntimeError('Native build failed; inspect concise compiler errors')
 for n in targets:
  source=root/build/n
  if source.stat().st_size<1024 or not os.access(source,os.X_OK) or source.read_bytes()[:4]!=b'\xcf\xfa\xed\xfe':raise RuntimeError('Native Mach-O artifact invalid '+n)
  digest=sha(source);target=out/(n+'-'+digest[:16]);shutil.copy2(source,target);target.chmod(0o555)
  if sha(target)!=digest:raise RuntimeError('Frozen artifact copy changed '+n)
  summary['artifacts'][n]={'path':str(target.relative_to(root)),'sha256':digest,'bytes':target.stat().st_size}
 save()
 for n in targets[2:]:
  if not measured(n,[str(root/summary['artifacts'][n]['path'])],100):raise RuntimeError(n+' failed')
 if not measured('worker-headers',[sys.executable,'tests/test_worker_response_headers.py','--binary',str(root/summary['artifacts']['jsrun']['path']),'--output',str(out/'worker-headers-receipt.json')],80):raise RuntimeError('Native worker header checks failed')
 for label,driver in [('progress-event','tests/test_worker_progress_event.py'),('blob-range','tests/test_blob_range_whitespace.py'),('sync-watchdog','tests/test_sync_xhr_watchdog.py'),('cross-realm','tests/test_formdata_realm.py'),('data-policy','tests/test_data_form_csp.py'),('parser-handler-policy','tests/test_parser_handler_policy.py'),('parser-handler-controls','tests/test_parser_handler_csp.py')]:
  if not measured(label,[sys.executable,driver,'--binary',str(root/summary['artifacts']['jsrun']['path']),'--output',str(out/(label+'-receipt.json'))],100):raise RuntimeError('Native '+label+' regression failed')
 for label,driver in [('body-window-handlers','tests/test_body_window_handlers.py'),('borrowed-window-handlers','tests/test_borrowed_window_handlers.py')]:
  if not measured(label,[sys.executable,driver,'--binary',str(root/summary['artifacts']['jsrun']['path'])],50):raise RuntimeError('Native '+label+' regression failed')
 if not measured('parser-handler-gui',[sys.executable,'tests/test_parser_handler_csp.py','--binary',str(root/summary['artifacts']['lumen']['path']),'--native-gui','--output',str(out/'parser-handler-gui-receipt.json')],100):raise RuntimeError('Native GUI parser-handler policy regression failed')
 for n,h in {**sources,**fixtures}.items():
  if sha(root/n)!=h:raise RuntimeError('Source changed during native validation '+n)
 linked=subprocess.check_output(['otool','-L',str(root/summary['artifacts']['lumen']['path'])],text=True);(out/'linked-libraries.txt').write_text(linked)
 summary['linkedV8']=[s.strip()for s in linked.splitlines()if 'v8' in s.lower()];summary['passed']=True
except Exception as error:summary['error']=type(error).__name__+': '+str(error)
finally:
 summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-started,3));save();print(json.dumps({'passed':summary['passed'],'source':head,'summary':str((out/'summary.json').relative_to(root)),'lumenSHA256':summary['artifacts'].get('lumen',{}).get('sha256'),'jsrunSHA256':summary['artifacts'].get('jsrun',{}).get('sha256'),'stagesPassed':sum(row['passed']for row in summary['stages']),'stagesTotal':len(summary['stages']),'error':summary.get('error'),'durationSeconds':summary['durationSeconds']}),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
