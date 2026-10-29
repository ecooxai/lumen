from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,subprocess,sys,time
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
green=json.loads(Path('out/lume-20261029-cpu30/native-json-1/summary.json').read_text())
if not green.get('passed'):raise SystemExit('A passing JSON GREEN build is required')
artifact=green['artifacts']['jsrun'];binary=Path(artifact['path'])
if sha(binary)!=artifact['sha256']:raise SystemExit('Immutable JSON artifact hash changed')
fixture=Path('tests/test_html_bom_boundary.py')
source_manifest=json.loads(Path(green['compiledSourceManifest']).read_text())
if source_manifest!={p:sha(p) for p in source_manifest}:raise SystemExit('Compiled source changed since JSON GREEN')
out=Path('out/lume-20261029-cpu30/html-bom-red-1');out.mkdir(parents=True,exist_ok=False)
start=time.monotonic();summary={'startedUTC':datetime.now(timezone.utc).isoformat(),'artifact':artifact,
    'fixtureSHA256':sha(fixture),'compiledSourceManifest':green['compiledSourceManifest'],'redConfirmed':False}
def save():(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
save()
try:
    prefix=out/'html-bom'
    command=[sys.executable,'tools/monitor_process.py','--duration','40','--settle','0','--output',str(prefix),'--',sys.executable,str(fixture),'--binary',str(binary)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=65)
    (out/'driver.log').write_text(result.stdout+result.stderr)
    metric=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    summary['monitor']={'path':str(prefix.with_suffix('.json')),'sha256':sha(prefix.with_suffix('.json')),
        'naturalExitCode':metric.get('natural_exit_code'),'collectorError':metric.get('collector_error'),
        'endedByMonitor':metric.get('ended_by_monitor'),'cpuSamples':metric.get('interval_cpu_samples'),
        'cpuPeakPercent':metric.get('interval_cpu_peak_percent'),'rssPeakMiB':metric.get('rss_peak_mb')}
    receipts=[json.loads(line.split('HTML_BOM_RECEIPT ',1)[1]) for line in log.splitlines() if 'HTML_BOM_RECEIPT ' in line]
    summary['checks']=[json.loads(line.split('HTML_BOM_CHECK ',1)[1]) for line in log.splitlines() if 'HTML_BOM_CHECK ' in line]
    if len(receipts)==1:summary['regression']=receipts[0]
    summary['redConfirmed']=len(receipts)==1 and receipts[0]['total']==21 and receipts[0]['failed']>0 and metric.get('natural_exit_code')==1 and not metric.get('collector_error') and not metric.get('ended_by_monitor')
    if source_manifest!={p:sha(p) for p in source_manifest}:raise RuntimeError('Source changed during BOM RED')
    if sha(fixture)!=summary['fixtureSHA256']:raise RuntimeError('BOM fixture changed during RED')
except Exception as error:summary['error']=type(error).__name__+': '+str(error)
summary.update(finishedUTC=datetime.now(timezone.utc).isoformat(),durationSeconds=round(time.monotonic()-start,3));save();print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['redConfirmed'] and not summary.get('error') else 1)
