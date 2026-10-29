"""Run JSON RED through a genuine legacy-encoded HTTP parent iframe."""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,subprocess,sys,time
utc=lambda:datetime.now(timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
previous=Path('out/lume-20261029-cpu30/json-document-red-1/summary.json')
old=json.loads(previous.read_text());artifact=old['artifact'];binary=Path(artifact['path'])
if sha(binary)!=artifact['sha256']:raise SystemExit('Immutable pre-patch jsrun changed')
source={p:sha(p) for p in subprocess.check_output(['git','ls-files','src','Makefile','tests/jsrun.c'],text=True).splitlines() if Path(p).is_file()}
if source['src/html/html.c']!='5deaa7cee34b71b928ee37aa2c7125c5b68273eb88c31ae47a6e21b64ac0e5aa':raise SystemExit('JSON engine source changed before valid RED')
out=Path('out/lume-20261029-cpu30/json-document-red-2');out.mkdir(parents=True,exist_ok=False)
manifest=out/'compiled-source-sha256.json';manifest.write_text(json.dumps(source,indent=2)+'\n')
fixture=Path('tests/test_json_document_encoding.py');start=time.monotonic()
summary={'startedUTC':utc(),'artifact':artifact,'fixtureSHA256':sha(fixture),'htmlSourceSHA256':source['src/html/html.c'],
         'sourceManifest':str(manifest),'sourceManifestSHA256':sha(manifest),'supersedesInvalidRun':str(previous),
         'invalidRunReason':'jsrun top-level document ignores transport charset, so v1 never reached child cases. v2 uses a genuine HTTP iframe with an explicit windows-1251 transport charset.',
         'redConfirmed':False}
def save():(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
save()
try:
    prefix=out/'json-document'
    result=subprocess.run([sys.executable,'tools/monitor_process.py','--duration','40','--settle','0','--output',str(prefix),'--',sys.executable,str(fixture),'--binary',str(binary)],capture_output=True,text=True,timeout=65)
    (out/'driver.log').write_text(result.stdout+result.stderr)
    metric=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    summary['monitor']={'path':str(prefix.with_suffix('.json')),'sha256':sha(prefix.with_suffix('.json')),'naturalExitCode':metric.get('natural_exit_code'),
                        'collectorError':metric.get('collector_error'),'endedByMonitor':metric.get('ended_by_monitor'),
                        'cpuSamples':metric.get('interval_cpu_samples'),'cpuPeakPercent':metric.get('interval_cpu_peak_percent'),'rssPeakMiB':metric.get('rss_peak_mb')}
    receipts=[json.loads(line.split('JSON_DOC_RECEIPT ',1)[1]) for line in log.splitlines() if 'JSON_DOC_RECEIPT ' in line]
    summary['checks']=[json.loads(line.split('JSON_DOC_CHECK ',1)[1]) for line in log.splitlines() if 'JSON_DOC_CHECK ' in line]
    if len(receipts)==1:summary['regression']=receipts[0]
    controls=len(summary['checks'])==16 and all(summary['checks'][i].get('ok') is True for i in (14,15))
    child_failures=len(summary['checks'])==16 and all(summary['checks'][i].get('ok') is False and 'expected \"UTF-8\", got \"windows-1251\"' in summary['checks'][i].get('error','') for i in (0,1,2))
    summary['redConfirmed']=len(receipts)==1 and receipts[0]['total']==16 and controls and child_failures and metric.get('natural_exit_code')==1 and not metric.get('collector_error') and not metric.get('ended_by_monitor')
    if source!={p:sha(p) for p in source}:raise RuntimeError('Source changed during RED')
    if sha(fixture)!=summary['fixtureSHA256']:raise RuntimeError('Fixture changed during RED')
except Exception as error:summary['error']=type(error).__name__+': '+str(error)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3));save();print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['redConfirmed'] and not summary.get('error') else 1)
