from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,subprocess,sys,time
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
utc=lambda:datetime.now(timezone.utc).isoformat()
build=json.loads(Path('out/lume-20261029-cpu30/native-bom-1/summary.json').read_text())
if not build.get('passed'):raise SystemExit('Verified parser candidate required')
artifact=build['artifacts']['jsrun'];binary=Path(artifact['path'])
if sha(binary)!=artifact['sha256']:raise SystemExit('Immutable RED jsrun changed')
source=json.loads(Path(build['compiledSourceManifest']).read_text())
if source!={p:sha(p) for p in source}:raise SystemExit('Compiled sources changed before RED')
probe_path=Path('out/lume-20261029-cpu30/fatal-stream-interop-2/summary.json')
probe=json.loads(probe_path.read_text())
if not probe.get('complete') or len(probe.get('comparisons',[]))!=28:raise SystemExit('Independent qualified28-sequence control required')
fixture=Path('tests/test_text_decoder_fatal_recovery.py');base=Path('tests/test_text_codec.py')
out=Path('out/lume-20261029-cpu30/textcodec-fatal-red-1');out.mkdir(parents=True,exist_ok=False);began=time.monotonic()
summary={'startedUTC':utc(),'artifact':artifact,'compiledSourceManifest':build['compiledSourceManifest'],'compiledSourceManifestSHA256':build['compiledSourceManifestSHA256'],
         'fixtureSHA256':sha(fixture),'baseFixtureSHA256':sha(base),'independentProbe':str(probe_path),'independentProbeSHA256':sha(probe_path),'redConfirmed':False}
def save():(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
save()
try:
    prefix=out/'codec'
    result=subprocess.run([sys.executable,'tools/monitor_process.py','--duration','30','--settle','0','--output',str(prefix),'--',sys.executable,str(fixture),'--binary',str(binary)],capture_output=True,text=True,timeout=50)
    (out/'driver.log').write_text(result.stdout+result.stderr)
    metric=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    summary['monitor']={'path':str(prefix.with_suffix('.json')),'sha256':sha(prefix.with_suffix('.json')),
        'naturalExitCode':metric.get('natural_exit_code'),'collectorError':metric.get('collector_error'),'endedByMonitor':metric.get('ended_by_monitor'),
        'cpuSamples':metric.get('interval_cpu_samples'),'cpuPeakPercent':metric.get('interval_cpu_peak_percent'),'rssPeakMiB':metric.get('rss_peak_mb')}
    rows=[json.loads(line.split('TEXT_CODEC_CHECK ',1)[1]) for line in log.splitlines() if 'TEXT_CODEC_CHECK ' in line]
    receipts=[json.loads(line.split('TEXT_CODEC_RECEIPT ',1)[1]) for line in log.splitlines() if 'TEXT_CODEC_RECEIPT ' in line]
    failures=[r for r in rows if not r.get('ok')]
    summary['failedChecks']=failures
    if len(receipts)==1:summary['regression']=receipts[0]
    names={'fatal UTF8 invalid leading byte 0x'+format(b,'x')+' discards rejected chunk tail' for b in [0x80,0xbf,0xc0,0xc1,0xf5,0xfd,0xfe,0xff]}
    names.update(['fatal UTF8 discards ASCII following an invalid leading byte','discarded UTF8 prefix cannot complete a later BOM',
                  'fatal UTF8 never consumes a later BOM in the rejected chunk','invalid UTF8 leading byte after valid text still discards its chunk tail'])
    expected={(scope,name) for scope in ['window','worker'] for name in names}
    actual={(r.get('scope'),r.get('name')) for r in failures}
    summary['expectedFailureIdentities']=sorted([list(x) for x in expected])
    summary['redConfirmed']=len(rows)==218 and len(receipts)==1 and receipts[0]['total']==218 and receipts[0]['passed']==194 and receipts[0]['failed']==24 and len(failures)==24 and actual==expected and metric.get('natural_exit_code')==1 and not metric.get('collector_error') and not metric.get('ended_by_monitor')
    if source!={p:sha(p) for p in source}:raise RuntimeError('Compiled sources changed during RED')
    if sha(fixture)!=summary['fixtureSHA256'] or sha(base)!=summary['baseFixtureSHA256']:raise RuntimeError('Codec fixtures changed during RED')
except Exception as error:summary['error']=type(error).__name__+': '+str(error)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3));save();print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['redConfirmed'] and not summary.get('error') else 1)
