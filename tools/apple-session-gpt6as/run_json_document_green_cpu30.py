"""Build one immutable native/jsrun snapshot and test the JSON parser change."""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,shutil,subprocess,sys,time

out=Path('out/lume-20261029-cpu30/native-json-1')
out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
began=time.monotonic()
patch_path=Path('out/lume-20261029-cpu30/json-document-patch-1/receipt.json')
patch=json.loads(patch_path.read_text())
red_path=Path('out/lume-20261029-cpu30/json-document-red-2/summary.json')
red=json.loads(red_path.read_text())
if patch['redReceipt']!=str(red_path) or patch['redReceiptSHA256']!=sha(red_path):raise SystemExit('Patch must reference measured RED2')
if not red.get('redConfirmed') or red.get('error') or not all(red['checks'][i].get('ok') is True for i in (14,15)):raise SystemExit('Valid legacy-carrier RED2 controls are required')
if patch['fixtureSHA256']!=red['fixtureSHA256']:raise SystemExit('JSON fixture does not match measured RED2')
if sha('src/html/html.c')!=patch['afterSHA256']:raise SystemExit('JSON patch source changed before build')
if sha('tests/test_json_document_encoding.py')!=patch['fixtureSHA256']:raise SystemExit('JSON regression changed after RED')
paths=subprocess.check_output(['git','ls-files','src','Makefile','tests/jsrun.c','tests/test_document_encoding.cc','tests/test_parser_input.c','tests/js/document-encoding-regression.js'],text=True).splitlines()
source={p:sha(p) for p in paths if Path(p).is_file()}
manifest=out/'compiled-source-sha256.json';manifest.write_text(json.dumps(source,sort_keys=True,indent=2)+'\n')
summary={'startedUTC':utc(),'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
         'compiledSourceManifest':str(manifest),'compiledSourceManifestSHA256':sha(manifest),
         'patchReceipt':str(patch_path),'patchReceiptSHA256':sha(patch_path),
         'stages':[],'artifacts':{},'passed':False}
def save():
    temp=out/'summary.json.tmp';temp.write_text(json.dumps(summary,indent=2)+'\n');temp.replace(out/'summary.json')
def stage(name,command,seconds,expected=None,marker=None):
    began=time.monotonic();prefix=out/name
    result=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(seconds),'--settle','0','--output',str(prefix),'--',*command],capture_output=True,text=True,timeout=seconds+30)
    (out/(name+'-driver.log')).write_text(result.stdout+result.stderr)
    meta=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
    row={'name':name,'command':command,'durationSeconds':round(time.monotonic()-began,3),
         'monitor':str(prefix.with_suffix('.json')),'monitorSHA256':sha(prefix.with_suffix('.json')),
         'logSHA256':sha(prefix.with_suffix('.log')),'exitCode':result.returncode,
         'naturalExitCode':meta.get('natural_exit_code'),'collectorError':meta.get('collector_error'),
         'endedByMonitor':meta.get('ended_by_monitor'),'cpuSamples':meta.get('interval_cpu_samples'),
         'cpuPeakPercent':meta.get('interval_cpu_peak_percent'),'rssPeakMiB':meta.get('rss_peak_mb'),
         'maximumSamplingGapSeconds':meta.get('max_sampling_gap_s')}
    row['passed']=result.returncode==0 and row['naturalExitCode']==0 and not row['collectorError'] and not row['endedByMonitor']
    if marker:
        receipts=[json.loads(line.split(marker,1)[1]) for line in log.splitlines() if marker in line]
        row['fixtureReceipts']=receipts
        row['passed']=row['passed'] and len(receipts)==1 and receipts[0].get('total')==expected and receipts[0].get('passed')==expected and receipts[0].get('failed')==0
    summary['stages'].append(row);save()
    if not row['passed']:raise RuntimeError(name+' failed; inspect retained monitored log')
    return log
save()
try:
    build='build-apple-load-20261029'
    names=['lumen','jsrun','test_document_encoding','test_parser_input']
    stage('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+name for name in names]],180)
    for name in names:
        source_binary=Path(build)/name;digest=sha(source_binary);dest=out/(name+'-'+digest[:16])
        shutil.copy2(source_binary,dest);dest.chmod(0o555)
        summary['artifacts'][name]={'path':str(dest),'sha256':digest,'bytes':dest.stat().st_size}
    links=subprocess.run(['otool','-L',summary['artifacts']['lumen']['path']],capture_output=True,text=True,check=True)
    (out/'native-linked-libraries.txt').write_text(links.stdout)
    summary['prebuiltV8Linked']=any('libv8' in line for line in links.stdout.splitlines())
    save()
    jsrun=summary['artifacts']['jsrun']['path']
    stage('json-document',[sys.executable,'tests/test_json_document_encoding.py','--binary',jsrun],40,16,'JSON_DOC_RECEIPT ')
    stage('html-meta',[sys.executable,'tests/test_html_meta_encoding.py','--binary',jsrun],50,29,'HTML_META_RECEIPT ')
    stage('document-encoding',[summary['artifacts']['test_document_encoding']['path']],60)
    stage('parser-input',[summary['artifacts']['test_parser_input']['path']],30)
    if source!={p:sha(p) for p in source}:raise RuntimeError('Compiled source changed during GREEN verification')
    if sha('tests/test_json_document_encoding.py')!=patch['fixtureSHA256']:raise RuntimeError('JSON fixture changed after RED')
    if not summary['prebuiltV8Linked']:raise RuntimeError('Expected prebuilt V8 linkage was not observed')
    summary['passed']=True
except Exception as error:summary['error']=type(error).__name__+': '+str(error)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3));save()
print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
