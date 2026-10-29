from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,re,shutil,subprocess,sys,time
root=Path.cwd().resolve();out=Path('out/lume-20261029-cpu30/json-document-red-1');out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
start=time.monotonic();fixture=Path('tests/test_json_document_encoding.py')
summary={'startedUTC':utc(),'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
         'fixtureSHA256':sha(fixture),'htmlSourceSHA256':sha('src/html/html.c'),'stages':[],'redConfirmed':False}
source={p:sha(p) for p in subprocess.check_output(['git','ls-files','src','Makefile'],text=True).splitlines() if Path(p).is_file()}
(out/'compiled-source-sha256.json').write_text(json.dumps(source,indent=2)+'\n')
def save():(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
def stage(name,command,seconds):
    before=time.monotonic();prefix=out/name
    run=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(seconds),'--settle','0','--output',str(prefix),'--',*command],capture_output=True,text=True,timeout=seconds+30)
    (out/(name+'-driver.log')).write_text(run.stdout+run.stderr)
    meta=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    row={'name':name,'command':command,'durationSeconds':round(time.monotonic()-before,3),'naturalExitCode':meta.get('natural_exit_code'),
         'collectorError':meta.get('collector_error'),'endedByMonitor':meta.get('ended_by_monitor'),'monitor':str(prefix.with_suffix('.json')),
         'cpuSamples':meta.get('interval_cpu_samples'),'cpuPeakPercent':meta.get('interval_cpu_peak_percent'),'rssPeakMiB':meta.get('rss_peak_mb')}
    summary['stages'].append(row);save();return row,log
save()
try:
    build='build-apple-load-20261029'
    row,_=stage('build-jsrun',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',build+'/jsrun'],120)
    if row['naturalExitCode']!=0 or row['collectorError'] or row['endedByMonitor']:raise RuntimeError('RED reference build failed')
    binary=Path(build)/'jsrun';digest=sha(binary);snapshot=out/('jsrun-'+digest[:16]);shutil.copy2(binary,snapshot);snapshot.chmod(0o555)
    summary['artifact']={'path':str(snapshot),'sha256':digest};save()
    row,log=stage('json-document',[sys.executable,str(fixture),'--binary',str(snapshot)],35)
    receipts=[json.loads(line.split('JSON_DOC_RECEIPT ',1)[1]) for line in log.splitlines() if 'JSON_DOC_RECEIPT ' in line]
    summary['checks']=[json.loads(line.split('JSON_DOC_CHECK ',1)[1]) for line in log.splitlines() if 'JSON_DOC_CHECK ' in line]
    if len(receipts)==1:summary['regression']=receipts[0]
    summary['redConfirmed']=len(receipts)==1 and receipts[0]['total']==16 and receipts[0]['failed']>0 and row['naturalExitCode']==1 and not row['collectorError'] and not row['endedByMonitor']
    if source!={p:sha(p) for p in source}:raise RuntimeError('Compiled source changed during RED run')
    if sha(fixture)!=summary['fixtureSHA256']:raise RuntimeError('Fixture changed during RED run')
except Exception as error:summary['error']=type(error).__name__+': '+str(error)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3));save();print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['redConfirmed'] and not summary.get('error') else 1)
