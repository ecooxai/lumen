"""Verify explicit en-US policy; preserve original legacy expectations as raw deltas."""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,re,shutil,subprocess,sys,time

out=Path('out/lume-20261029-cpu30/native-dsd-1')
out.mkdir(parents=True,exist_ok=False)
utc=lambda:datetime.now(timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
began=time.monotonic()
dsd_path=Path('out/lume-20261029-cpu30/declarative-shadow-patch-1/receipt.json')
dsd=json.loads(dsd_path.read_text())
for name,digest in dsd['afterSHA256'].items():
    if sha(name)!=digest:raise SystemExit('DSD patch source changed before GREEN: '+name)
if sha('tests/test_declarative_shadow_documents_v2.py')!=dsd['fixtureSHA256']:raise SystemExit('Frozen37 DSD fixture changed')

patch_path=Path('out/lume-20261029-cpu30/textcodec-fatal-patch-1/receipt.json')
patch=json.loads(patch_path.read_text())
if sha(patch['source'])!=patch['afterSHA256']:raise SystemExit('Fatal codec patch source changed before build')
if sha('tests/test_text_decoder_fatal_recovery.py')!=patch['fixtureSHA256'] or sha('tests/test_text_codec.py')!=patch['baseFixtureSHA256']:raise SystemExit('Codec fixtures changed after RED')
policy_path=Path('out/lume-20261029-cpu30/html-default-patch-1/receipt.json')
policy=json.loads(policy_path.read_text())
for name,row in policy['sources'].items():
    if name not in dsd['afterSHA256'] and sha(name)!=row['afterSHA256']:raise SystemExit('Policy source changed before GREEN: '+name)
if sha('tests/test_html_default_policy.py')!=policy['fixtureSHA256'] or sha('tests/test_html_default_policy_network.c')!=policy['boundaryFixtureSHA256']:raise SystemExit('Frozen policy fixture changed')
if sha('tests/test_frame_encoding_fallback.py')!=policy['legacyFixtureSHA256']:raise SystemExit('Original15-case fixture changed')
bom_patch=json.loads(Path('out/lume-20261029-cpu30/html-bom-patch-1/receipt.json').read_text())
for name,row in bom_patch['sources'].items():
    if name!='src/html/html.c' and sha(name)!=row['afterSHA256']:raise SystemExit('Accepted parser source changed before policy build: '+name)
if sha('tests/test_html_bom_boundary.py')!=bom_patch['fixtureSHA256']:raise SystemExit('BOM fixture changed')
json_patch=json.loads(Path('out/lume-20261029-cpu30/json-document-patch-1/receipt.json').read_text())
json_red=Path('out/lume-20261029-cpu30/json-document-red-2/summary.json')
if json_patch['redReceipt']!=str(json_red) or json_patch['redReceiptSHA256']!=sha(json_red):raise SystemExit('JSON provenance changed')
if sha('tests/test_json_document_encoding.py')!=json_patch['fixtureSHA256']:raise SystemExit('JSON fixture changed')
paths=subprocess.check_output(['git','ls-files','src','Makefile','tests/jsrun.c','tests/test_document_encoding.cc','tests/test_parser_input.c','tests/js/document-encoding-regression.js','tests/test_render_overdraw.c','tests/test_gpu_present.c','tests/test_app_wheel.c'],text=True).splitlines()
paths.append('tests/test_html_default_policy_network.c')
source={p:sha(p) for p in paths if Path(p).is_file()}
manifest=out/'compiled-source-sha256.json';manifest.write_text(json.dumps(source,sort_keys=True,indent=2)+'\n')
summary={'startedUTC':utc(),'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
         'compiledSourceManifest':str(manifest),'compiledSourceManifestSHA256':sha(manifest),
         'patchReceipt':str(policy_path),'patchReceiptSHA256':sha(policy_path),'codecPatchReceipt':str(patch_path),'codecPatchReceiptSHA256':sha(patch_path),
         'currentPolicyCheckCount':755,'priorPolicyCheckCount':718,'declarativeShadowChecks':37,'declarativeShadowPatch':str(dsd_path),'declarativeShadowPatchSHA256':sha(dsd_path),'legacyPolicyChecks':{'total':15,'expectedPassing':10,'intentionalChangedOutcomes':5,'fixtureUnchanged':True},
         'driverConsistencyEvidence':{'fixture':'out/lume-20261029-cpu30/json-document-red-1/fixture-invalid-v1.py','reason':'Recheck exact observed top-level jsrun transport-charset defect; original RED1 remains invalid for JSON compatibility.'},
         'stages':[],'artifacts':{},'passed':False}
def save():
    temp=out/'summary.json.tmp';temp.write_text(json.dumps(summary,indent=2)+'\n');temp.replace(out/'summary.json')
def stage(name,command,seconds,expected=None,marker=None,legacy=False):
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
         'maximumSamplingGapSeconds':meta.get('max_sampling_gap_s'),
         'resultLines':[line for line in log.splitlines() if re.search(r'checks|failures|passed|PASS|FAIL',line)][-5:]}
    row['passed']=result.returncode==0 and row['naturalExitCode']==0 and not row['collectorError'] and not row['endedByMonitor']
    if marker:
        receipts=[json.loads(line.split(marker,1)[1]) for line in log.splitlines() if marker in line]
        row['fixtureReceipts']=receipts
        row['passed']=row['passed'] and len(receipts)==1 and receipts[0].get('total')==expected and receipts[0].get('passed')==expected and receipts[0].get('failed')==0
    elif expected is not None:
        pairs=re.findall(r'PASS \((\d+)/(\d+) checks\)',log)
        gpu=re.findall(r'GPU presentation: (\d+) checks, (\d+) failures',log)
        row['nativeCheckCount']=expected
        row['passed']=row['passed'] and (pairs==[(str(expected),str(expected))] or gpu==[(str(expected),'0')])
    if legacy:
        checks=[json.loads(line.split('FRAME_ENCODING_CHECK ',1)[1]) for line in log.splitlines() if 'FRAME_ENCODING_CHECK ' in line]
        changed={check['name'] for check in checks if not check['ok']}
        required=set(policy['intentionalLegacyDeltas'])
        row['acceptedIntentionalPolicyDelta']=result.returncode==1 and row['naturalExitCode']==1 and not row['collectorError'] and not row['endedByMonitor'] and len(checks)==15 and changed==required and len(receipts)==1 and receipts[0].get('total')==15 and receipts[0].get('passed')==10 and receipts[0].get('failed')==5 and all(check.get('error')=='Error: expected \"UTF-8\", got \"windows-1252\"' for check in checks if not check['ok'])
        row['rawLegacyChecks']=checks
        row['policyExplanation']='Original15-case fixture is unchanged. Five former UTF8-default expectations deliberately differ under explicit en-US1252 policy. These failures remain visible and are excluded from755 current-policy passing checks. The original36 DSD replay is a subset of37 and is not double-counted.'
    summary['stages'].append(row);save()
    if not row['passed'] and not row.get('acceptedIntentionalPolicyDelta'):raise RuntimeError(name+' failed; inspect retained monitored log')
    return log
save()
try:
    build='build-apple-load-20261029'
    names=['lumen','jsrun','test_document_encoding','test_parser_input','test_render_overdraw','test_gpu_present','test_app_wheel','test_html_default_policy_network']
    stage('build',['make','-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',*[build+'/'+name for name in names]],240)
    for name in names:
        source_binary=Path(build)/name;digest=sha(source_binary);dest=out/(name+'-'+digest[:16])
        shutil.copy2(source_binary,dest);dest.chmod(0o555)
        summary['artifacts'][name]={'path':str(dest),'sha256':digest,'bytes':dest.stat().st_size}
    links=subprocess.run(['otool','-L',summary['artifacts']['lumen']['path']],capture_output=True,text=True,check=True)
    (out/'native-linked-libraries.txt').write_text(links.stdout)
    summary['prebuiltV8Linked']=any('libv8' in line for line in links.stdout.splitlines())
    save()
    jsrun=summary['artifacts']['jsrun']['path']
    stage('declarative-shadow',[sys.executable,'tests/test_declarative_shadow_documents_v2.py','--binary',jsrun],40,37,'DSD_RECEIPT ')
    stage('declarative-shadow-original36',[sys.executable,'tests/test_declarative_shadow_documents.py','--binary',jsrun],40,36,'DSD_RECEIPT ')
    stage('fatal-codec',[sys.executable,'tests/test_text_decoder_fatal_recovery.py','--binary',jsrun],30,218,'TEXT_CODEC_RECEIPT ')
    stage('html-bom',[sys.executable,'tests/test_html_bom_boundary.py','--binary',jsrun],40,21,'HTML_BOM_RECEIPT ')
    stage('json-document',[sys.executable,'tests/test_json_document_encoding.py','--binary',jsrun],40,16,'JSON_DOC_RECEIPT ')
    stage('driver-parent-charset',[sys.executable,'out/lume-20261029-cpu30/json-document-red-1/fixture-invalid-v1.py','--binary',jsrun],40,16,'JSON_DOC_RECEIPT ')
    stage('legacy-frame-policy',[sys.executable,'tests/test_frame_encoding_fallback.py','--binary',jsrun],50,15,'FRAME_ENCODING_RECEIPT ',legacy=True)
    stage('xhr-encoding',[sys.executable,'tests/test_xhr_response_encoding.py','--binary',jsrun],50,42,'XHR_ENCODING_RECEIPT ')
    stage('html-meta',[sys.executable,'tests/test_html_meta_encoding.py','--binary',jsrun],50,29,'HTML_META_RECEIPT ')
    stage('document-encoding',[summary['artifacts']['test_document_encoding']['path']],60,78)
    stage('parser-input',[summary['artifacts']['test_parser_input']['path']],30,48)
    for name,count in [('test_render_overdraw',100),('test_gpu_present',68),('test_app_wheel',35)]:
        stage(name,[summary['artifacts'][name]['path']],30,count)
    stage('html-default-boundary',[summary['artifacts']['test_html_default_policy_network']['path'] ],30,22)
    stage('html-default-navigation',[sys.executable,'tests/test_html_default_policy.py','--binary',summary['artifacts']['lumen']['path'],'--output',str(out/'navigation-policy-run')],70,25,'HTML_DEFAULT_RESULTS ')
    if sha('tests/test_html_default_policy.py')!=policy['fixtureSHA256'] or sha('tests/test_html_default_policy_network.c')!=policy['boundaryFixtureSHA256']:raise RuntimeError('Frozen policy fixtures changed')
    if sha('tests/test_frame_encoding_fallback.py')!=policy['legacyFixtureSHA256']:raise RuntimeError('Original15-case fixture changed')
    if source!={p:sha(p) for p in source}:raise RuntimeError('Compiled source changed during GREEN verification')
    if sha('tests/test_html_bom_boundary.py')!=bom_patch['fixtureSHA256']:raise RuntimeError('BOM fixture changed after RED')
    if sha('tests/test_text_decoder_fatal_recovery.py')!=patch['fixtureSHA256'] or sha('tests/test_text_codec.py')!=patch['baseFixtureSHA256']:raise RuntimeError('Codec fixtures changed after RED')
    if sha('tests/test_json_document_encoding.py')!=json_patch['fixtureSHA256']:raise RuntimeError('JSON fixture changed after RED')
    if not summary['prebuiltV8Linked']:raise RuntimeError('Expected prebuilt V8 linkage was not observed')
    v8=Path('out/lume-20261029-cpu30/v8-prebuilt-identity.json')
    if not v8.exists():raise RuntimeError('Measured prebuilt V8 identity receipt missing')
    summary['prebuiltV8IdentityReceipt']={'path':str(v8),'sha256':sha(v8)}
    if sha('tests/test_declarative_shadow_documents_v2.py')!=dsd['fixtureSHA256']:raise RuntimeError('Frozen37 DSD fixture changed during GREEN')
    summary['passed']=True
except Exception as error:summary['error']=type(error).__name__+': '+str(error)
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3));save()
print(json.dumps(summary),flush=True)
raise SystemExit(0 if summary['passed'] else 1)
