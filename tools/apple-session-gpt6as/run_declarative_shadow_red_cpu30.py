from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,subprocess,sys,time
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
utc=lambda:datetime.now(timezone.utc).isoformat()
build=json.loads(Path('out/lume-20261029-cpu30/native-default-1/summary.json').read_text())
if not build.get('passed'):raise SystemExit('Verified accepted3ad required')
artifact=build['artifacts']['jsrun'];binary=Path(artifact['path'])
if sha(binary)!=artifact['sha256']:raise SystemExit('Immutable RED jsrun changed')
source=json.loads(Path(build['compiledSourceManifest']).read_text())
if source!={p:sha(p) for p in source}:raise SystemExit('Compiled sources changed before RED')
fixture=Path('tests/test_declarative_shadow_documents.py')
out=Path('out/lume-20261029-cpu30/declarative-shadow-red-1');out.mkdir(parents=True,exist_ok=False);began=time.monotonic()
summary={'startedUTC':utc(),'artifact':artifact,'compiledSourceManifest':build['compiledSourceManifest'],'compiledSourceManifestSHA256':build['compiledSourceManifestSHA256'],'fixtureSHA256':sha(fixture),'redConfirmed':False}
def save():(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
save()
try:
    prefix=out/'dsd'
    result=subprocess.run([sys.executable,'tools/monitor_process.py','--duration','30','--settle','0','--output',str(prefix),'--',sys.executable,str(fixture),'--binary',str(binary)],capture_output=True,text=True,timeout=50)
    (out/'driver.log').write_text(result.stdout+result.stderr)
    metric=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    summary['monitor']={'path':str(prefix.with_suffix('.json')),'sha256':sha(prefix.with_suffix('.json')),'naturalExitCode':metric.get('natural_exit_code'),'collectorError':metric.get('collector_error'),'endedByMonitor':metric.get('ended_by_monitor'),'cpuSamples':metric.get('interval_cpu_samples'),'cpuPeakPercent':metric.get('interval_cpu_peak_percent'),'rssPeakMiB':metric.get('rss_peak_mb')}
    rows=[json.loads(line.split('DSD_CHECK ',1)[1]) for line in log.splitlines() if 'DSD_CHECK ' in line]
    receipts=[json.loads(line.split('DSD_RECEIPT ',1)[1]) for line in log.splitlines() if 'DSD_RECEIPT ' in line]
    failures=[r for r in rows if not r.get('ok')]
    summary['checks']=rows;summary['failedChecks']=failures
    if len(receipts)==1:summary['regression']=receipts[0]
    controls={'invalid mode remains ordinary inert template','invalid host remains ordinary inert template','shadow mode whitespace is not stripped','ordinary template stays inert with a separate owner','DOMParser keeps declarative shadow parsing disabled','innerHTML keeps declarative shadow parsing disabled','fragment parsing does not attach a root to context','ordinary imperative open and closed roots remain supported'}
    targets={'network open root content and native ShadowRoot brand','declarative template is absent from light DOM','shallow document clone preserves allow-declarative policy','detached writes do not insert into active document','split writes retain tokenizer state across tags','two detached parsers can interleave independently'}
    passed={r['name'] for r in rows if r.get('ok')};failed={r['name'] for r in failures}
    summary['requiredPassingControls']=sorted(controls);summary['requiredFailingTargets']=sorted(targets)
    summary['redConfirmed']=len(rows)==36 and len(receipts)==1 and receipts[0]['total']==36 and controls<=passed and targets<=failed and metric.get('natural_exit_code')==1 and not metric.get('collector_error') and not metric.get('ended_by_monitor')
    if source!={p:sha(p) for p in source}:raise RuntimeError('Compiled sources changed during RED')
    if sha(fixture)!=summary['fixtureSHA256']:raise RuntimeError('DSD fixture changed during RED')
except Exception as error:summary['error']=type(error).__name__+': '+str(error)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3));save();print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['redConfirmed'] and not summary.get('error') else 1)
