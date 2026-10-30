"""Promote only the measured ArrayBuffer-transfer artifact and checkpoint its evidence."""
from pathlib import Path
import copy, datetime as dt, hashlib, json, os, re, subprocess

ROOT=Path.cwd();TASK='102900614';BRANCH='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614'
NATIVE='733943aa9496d1342dd9d90a76146d0433b24c9ef7848df5860881e57c2ac848'
JSRUN='70d4e740da4da3a7e9f3ec360b6f0761a9adaa77dcdc50a44f60d63a650c8c03'
os.environ.update(GIT_PAGER='cat',PAGER='cat')
def git(*args,raw=False):
    value=subprocess.check_output(['git','--no-pager',*args])
    return value if raw else value.decode().strip()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text())
def write(path,data):Path(path).write_text(json.dumps(data,indent=2)+'\n')
if git('branch','--show-current')!=BRANCH:raise RuntimeError('Wrong task branch')
scorepath=Path('agent/scorecard-'+TASK+'.json');score=read(scorepath)
if score['history'][-1]['iteration']!='102900614-002':raise RuntimeError('Iteration003 is already recorded or ordering changed')
source_paths=['src/js/worker.cc','src/js/prelude/07b-net.js','src/js/prelude/08-window.js']
allow=source_paths+['tests/test_message_transfer.py']+['tools/apple-session-gpt6as/'+name for name in (
'apply_message_transfer_102900614.py','build_message_transfer_102900614.py','verify_transfer_wpt_102900614.py','verify_transfer_wpt_v2_102900614.py','restore_wpt_fixture_102900614.py')]
if git('diff','--cached','--name-only'):raise RuntimeError('Other staged changes must be coordinated first')
patchpath=Path('output/session-102900614/message-transfer-candidate-1/patch-receipt.json');patch=read(patchpath)
for name in source_paths:
    if sha(name)!=patch['after'][name]:raise RuntimeError('Transfer source changed before isolated checkpoint: '+name)
if sha('tests/test_message_transfer.py')!=patch['fixtureSHA256']:raise RuntimeError('Transfer fixture changed after RED/GREEN')
greenpath=Path('output/session-102900614/message-transfer-green-1/summary.json');green=read(greenpath)
if not green['passed'] or green['artifacts']['lumen']['sha256']!=NATIVE or green['artifacts']['jsrun']['sha256']!=JSRUN:raise RuntimeError('Native regression evidence differs')
if len(green['stages'])!=8 or any(not row['passed'] or row['naturalExitCode']!=0 or row['endedByMonitor'] or row['durationSeconds']>300 for row in green['stages']):raise RuntimeError('Native stage gate incomplete')
for key in ('lumen','jsrun'):
    if sha(green['artifacts'][key]['snapshot'])!=green['artifacts'][key]['sha256']:raise RuntimeError('Frozen binary changed')
compiledpath=Path(green['compiledSourceManifest']);compiled=read(compiledpath)
upstreampath=Path('output/session-102900614/message-transfer-upstream-2/receipt.json');upstream=read(upstreampath)
original=upstream['originalReused'];target=upstream['stages'][0]
if not upstream['passed'] or upstream['binarySHA256']!=NATIVE or upstream['binarySHA256After']!=NATIVE:raise RuntimeError('Upstream artifact mismatch')
if original['assertions']!={'PASS':1850} or original['files']!=6 or original['fullyPassingFiles']!=6 or not original['passed']:raise RuntimeError('Original WPT gate incomplete')
if sha(original['report'])!=original['reportSHA256']:raise RuntimeError('Original WPT report changed')
if target['assertions']!={'PASS':222} or target['files']!=2 or target['fullyPassingFiles']!=2 or not target['passed'] or len(target['detachedBufferAssertions'])!=2 or any(row['status']!='PASS' for row in target['detachedBufferAssertions']):raise RuntimeError('Detached-output-buffer upstream gate incomplete')
redprefix=Path('output/session-102900614/message-transfer-red-1');red=read(str(redprefix)+'.json');redlog=Path(str(redprefix)+'.log').read_text()
if not re.search(r'"total"\s*:\s*68\s*,\s*"passed"\s*:\s*15\s*,\s*"failed"\s*:\s*53',redlog):raise RuntimeError('Expected regression-first RED15/68 absent')
scrollpath=Path('output/session-102900614/scroll-transfer-baseline/receipt.json');scroll=read(scrollpath)
if scroll['binary_sha256']!=NATIVE or scroll['binary_sha256_after']!=NATIVE or scroll['natural_app_return_code']!=0 or scroll['app_termination']['forced'] or scroll['sampler_termination']['forced']:raise RuntimeError('Current-artifact resource identity or cleanup differs')
idle=scroll['phase_metrics']['idle:after'];interaction=scroll['phase_metrics']['scrolling']
if idle['samples']<46 or idle['duration_seconds']<45 or idle['cpu_p95_percent']>3 or scroll['phase_metrics']['settle:after']['duration_seconds']<15:raise RuntimeError('Current idle window fails fixed policy')
if scroll['issues']!=['scrolling: CPU p95 23.054% exceeds 10%']:raise RuntimeError('Unreviewed resource issues appeared')

# Independently prove that the future index's production inputs equal the frozen build,
# even while an unrelated GPU candidate is present in the working tree.
for name,expected in compiled.items():
    actual=sha(name) if name in source_paths else hashlib.sha256(git('show','HEAD:'+name,raw=True)).hexdigest()
    if actual!=expected:raise RuntimeError('Transfer-only committed source would differ from compiled inputs: '+name)

now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
folder=Path('agent/evidence/102900614-003');folder.mkdir(exist_ok=False)
evidence_files=[]
for name,value in [('compiled-source-sha256.json',compiled),('native-regression-summary.json',green),('upstream-receipt.json',upstream)]:
    path=folder/name;write(path,value);evidence_files.append(str(path))
original_evidence=folder/'original-native-wpt.json'
write(original_evidence,{'format':'native_absolute_wpt','taskId':TASK,'binarySHA256':NATIVE,'report':original['report'],'reportSHA256':original['reportSHA256'],'files':6,'fullyPassingFiles':6,'assertions':{'PASS':1850},'passed':True,'sourceReceipt':str(upstreampath),'referenceStatus':'Quarantined native URL sanity; no Chrome parity claim','scope':'Six unchanged upstream WPT-source files only; full official WPT remains unmeasured'})
evidence_files.append(str(original_evidence))
resource_evidence=folder/'resource-review.json'
idle_review={'scenario':'scroll','binary_sha256':NATIVE,'source_receipt':str(scrollpath),'source_sha256':sha(scrollpath),'started_utc':scroll['started_utc'],'finished_utc':scroll['finished_utc'],'duration_seconds':scroll['duration_seconds'],'policy':scroll['policy'],'issues':scroll['issues'],'phases':copy.deepcopy(scroll['phase_metrics']),'idle_accepted':True,'interaction_accepted':False,'soak_accepted':False,'scope':'Current native Mac process-tree controlled scroll/final idle only; no IPA, physical-input, memory-soak or universal resource claim'}
for phase in idle_review['phases'].values():phase['cpu_intervals']=phase['samples']-phase.get('phase_boundary_samples_excluded_from_cpu_summary',1)
write(resource_evidence,idle_review);evidence_files.append(str(resource_evidence))
entry={'iteration':'102900614-003','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':40,'status':'ArrayBuffer transfer accepted on733; fresh soak and task delivery remain pending','binary_sha256':NATIVE,'source_base_commit':green['sourceBaseCommit'],'source_manifest':str(folder/'compiled-source-sha256.json'),'source_manifest_sha256':sha(compiledpath),'red':{'passed':15,'total':68,'failed':53,'log':str(redprefix)+'.log','log_sha256':sha(str(redprefix)+'.log'),'monitor':str(redprefix)+'.json','monitor_sha256':sha(str(redprefix)+'.json'),'duration_seconds':red.get('duration_s'),'valid_cpu_intervals':red.get('interval_cpu_samples')},'green':{'passed':68,'total':68,'regression_checks':826,'duration_seconds':green['durationSeconds'],'build_seconds':green['stages'][0]['durationSeconds'],'receipt':str(greenpath),'receipt_sha256':sha(greenpath)},'wpt':{'original_assertions':'1850/1850','original_files':'6/6','original_seconds':original['durationSeconds'],'target_assertions':'222/222','target_files':'2/2','target_seconds':target['durationSeconds'],'full_official_pass_rate':None},'resources':{'duration_seconds':scroll['duration_seconds'],'idle_p95_percent':idle['cpu_p95_percent'],'idle_rss_mib':idle['rss_end_mib'],'scroll_p95_percent':interaction['cpu_p95_percent'],'scroll_target_percent':10},'historical_score':50,'score_explanation':'Fresh executable acceptance resets artifact-specific soak and new-task delivery to pending. This is an evidence refresh, not a claimed functional regression. Criterion weights unchanged.','limits':['ArrayBuffer transfer is implemented; complete MessagePort shipping/entanglement lifecycle is not established.','Raw expanded priority counts remain historical158/207 observed,158/1515 scheduled.','Chrome reference remains quarantined; native absolute WPT results remain valid.','No identical signed IPA execution proof.'],'infrastructure_note':'Initial target runner stopped because the old official WPT fixture PID had exited. Restored owned fixture and reran only the two pending variants; original1850 accepted results were retained.','model_compute_duration':'Unavailable; parallel worker wall times are not summed'}
entrypath=Path('agent/evidence/102900614-003-message-transfer.json');write(entrypath,entry);evidence_files.append(str(entrypath))

# Preserve complete former gate states as history; do not copy their measurements forward.
current_gates=[]
for old in score['gates']:
    gate={key:old[key] for key in ('id','category','weight','name')}
    gate.update(status='unverified',earned=0,evidence=[],evidence_generation='current_task',historical_evidence=old.get('historical_evidence',[])+[{'binary_sha256':old.get('binary_sha256',score['current_baseline_binary_sha256']),'prior_status':old['status'],'prior_earned':old['earned'],'evidence':old.get('evidence',[])}])
    current_gates.append(gate)
gates={row['id']:row for row in current_gates}
def passed(name,evidence,**extras):gates[name].update(status='passed',earned=gates[name]['weight'],evidence=evidence,**extras)
passed('wpt_original',[str(original_evidence)],evidence_format='native_absolute_wpt',passed=1850,denominator=1850,binary_sha256=NATIVE,note='Six unchanged upstream files; absolute native assertions only. Chrome control quarantined.')
gates['wpt_expanded'].update(status='incomplete_reference_quarantined',evidence=next(row['evidence']for row in score['gates']if row['id']=='wpt_expanded'),note='All1515 variants remain frozen; historical158/207 cannot establish current build acceptance or full WPT conformance.')
passed('wpt_no_regression',[str(folder/'native-regression-summary.json'),str(folder/'compiled-source-sha256.json'),str(original_evidence),str(folder/'upstream-receipt.json')],binary_sha256=NATIVE,note='826 custom regression assertions plus unchanged original and targeted upstream assertions; source manifest matches this isolated commit.')
passed('perf_idle',[str(resource_evidence)],binary_sha256=NATIVE,review=idle_review,note='Fixed final idle after15s settle,49.010s window; zero p95 reflects sampler granularity.')
gates['perf_interaction'].update(status='failed',evidence=[str(resource_evidence)],binary_sha256=NATIVE,review=idle_review,scroll_cpu_p95_percent=interaction['cpu_p95_percent'],ceiling_percent=10,note='Fixed controlled fractional-scroll workload; interaction target not met.')
gates['perf_soak'].update(status='pending_current_artifact',binary_sha256=NATIVE,note='Previous executable soak remains historical; current executable needs fresh20-navigation evidence.')
passed('perf_budgets',[str(folder/'native-regression-summary.json')],build_seconds=green['stages'][0]['durationSeconds'],prebuilt_v8=True,note='Existing matched V8 binary reused; eight individually timed1Hz monitored stages, none above5min.')
passed('repro_tdd',[str(entrypath),str(folder/'native-regression-summary.json')],note='Unchanged native fixture15/68 RED to68/68 GREEN, then unchanged upstream detached-buffer checks pass.')
passed('repro_source',[str(folder/'compiled-source-sha256.json'),str(folder/'native-regression-summary.json')],binary_sha256=NATIVE,note='All compiled src/Makefile/ios inputs match isolated committed source; unrelated GPU working changes are excluded.')
gates['release_progress'].update(status='pending_current_task',note='Task-local identity and9/9 checks pass; public task route, websocket, archive and message-reply still require fresh verification.')
score.update(gates=current_gates,score=sum(gate['earned']for gate in current_gates),score_state='current_native_artifact_accepted_partial_gates_pending',accepted_artifact_status='current_native_partial_acceptance',current_baseline_binary_sha256=NATIVE,current_native_source_base_commit=green['sourceBaseCommit'],current_native_source_commit=None,current_native_source_manifest=str(folder/'compiled-source-sha256.json'),updated_utc=now.isoformat(),historical_baseline_score=50)
if score['score']!=40:raise RuntimeError('Fixed gate sum differs from reviewed40')
score['history'].append({key:entry[key]for key in ('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status','binary_sha256')});write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:
    out.write(f"\n## 102900614-003 — native ArrayBuffer transfer and unchanged upstream validation\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; iteration `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Parallel worker times are not summed; model compute duration unavailable.\n- Current acceptance **40/100**. Prior **50/100** remains historical. Current soak and external delivery are pending on the new executable; no functional regression is implied by the evidence reset.\n- Native regression **15/68 RED → 68/68 GREEN**; all **826/826** transfer, codec, clone, worker lifecycle/security and port regression checks pass. Build `{green['stages'][0]['durationSeconds']}s`; full regression stage `{green['durationSeconds']}s`. Existing prebuilt V8 reused.\n- Original upstream **1850/1850 assertions across6/6 files** in `{original['durationSeconds']}s`; targeted encodeInto window/worker **222/222 across2/2** in `{target['durationSeconds']}s`. Full WPT and Chrome parity remain unmeasured.\n- Current-artifact resource run `{scroll['duration_seconds']}s`: final idle p95 `{idle['cpu_p95_percent']}%` with stable `{idle['rss_end_mib']}MiB` RSS; fractional scrolling p95 `{interaction['cpu_p95_percent']}%` still exceeds10%. Subsecond regression runs do not establish steady-state performance.\n- The stale official fixture was repaired before rerunning the two pending variants; the accepted original six-file result was preserved. Source commit excludes unrelated GPU candidate work.\n- Evidence `{entrypath}`; immutable native SHA-256 `{NATIVE}`. MessagePort lifecycle, full Streams and same signed IPA execution remain outside this acceptance.\n")
with Path('agent/continue.md').open('a')as out:out.write(f"\n## Checkpoint003 — current native transfer artifact\n\nCurrent verified native `{green['artifacts']['lumen']['snapshot']}` SHA-256 `{NATIVE}`. Source manifest `{folder/'compiled-source-sha256.json'}`. Original1850/1850 and targeted222/222 upstream assertions pass;826 custom regression assertions pass. Current score40/100; prior50 remains historical, fresh soak and task delivery pending. Current final idle passes; scroll p95{interaction['cpu_p95_percent']}% fails10% target. GPU and stream candidates must be separately validated and never silently attributed to this snapshot. All broader158/207 priority results remain historical. See `{entrypath}`.\n")
with Path('AGENTS.md').open('a')as out:out.write(f"\n### Current checkpoint003\n\nVerified transfer native `{NATIVE}`; original WPT1850/1850 and targeted222/222 pass. Current artifact acceptance40/100, prior50 historical. Fresh soak and task public delivery pending; scroll p95{interaction['cpu_p95_percent']}% still fails10% ceiling. Keep GPU/stream candidates isolated. Details and exact sources in `agent/evidence/102900614-003-message-transfer.json`.\n")

# Teach the existing dashboard to read absolute native evidence without fabricating a Chrome numerator.
updater=Path('tools/apple-session-gpt6as/update_priority_progress_10290244.py');text=updater.read_text();begin=text.index("comparison=json.loads((ROOT/comparison_path).read_text())");end=text.index("interaction=gates['perf_interaction']",begin)
old=text[begin:end]
new="""comparison=json.loads((ROOT/comparison_path).read_text())
if gates['wpt_original'].get('evidence_format')=='native_absolute_wpt':
    native=comparison['binarySHA256']
    if comparison.get('format')!='native_absolute_wpt' or not comparison.get('passed') or native!=score['current_baseline_binary_sha256'] or sha(ROOT/comparison['report'])!=comparison['reportSHA256']:raise RuntimeError('Native absolute WPT evidence does not match current artifact')
    counts=comparison['assertions'];total=sum(counts.values());passed=counts.get('PASS',0)
    status['wpt']={'passed':passed,'total':total,'passPercent':round(100*passed/total,4) if total else None,
        'reportedFiles':comparison['files'],'fullyPassingFiles':comparison['fullyPassingFiles'],
        'chromePassed':None,'chromeTotal':None,'binarySha256':native,'scope':comparison['scope'],
        'referenceStatus':comparison['referenceStatus'],'evidence':comparison_path}
else:
"""+''.join('    '+line+'\n'for line in old.splitlines()[1:])
text=text[:begin]+new+text[end:]
needle="'scheduledFullyPassingPercent':round(100*passed/scheduled,4),'scope':'Raw frozen priority variants, not full WPT'}"
replacement="'scheduledFullyPassingPercent':round(100*passed/scheduled,4),'scope':'Raw frozen priority variants, not full WPT',\n    'binarySha256':value.get('lumenBinarySHA256'),'isCurrentAcceptedNativeArtifact':value['isCurrentAcceptedNativeArtifact']}"
if text.count(needle)!=1:raise RuntimeError('Priority freshness updater anchor changed')
text=text.replace(needle,replacement);compile(text,str(updater),'exec');updater.write_text(text)
app=Path('tools/lume-progress-10290244/public/app.js');text=app.read_text()
needle="`${priority.unrun.toLocaleString()} unrun variants remain. ${number(priority.scheduledFullyPassingPercent,2)}% of the frozen schedule is recorded fully passing. Full WPT rate is unmeasured.`"
replacement="`${priority.isCurrentAcceptedNativeArtifact===false?'Previous native artifact; current build needs this broader run. ':''}${priority.unrun.toLocaleString()} unrun variants remain. ${number(priority.scheduledFullyPassingPercent,2)}% of the frozen schedule is recorded fully passing. Full WPT rate is unmeasured.`"
if text.count(needle)!=1:raise RuntimeError('Priority freshness UI anchor changed')
text=text.replace(needle,replacement)
needle="wpt?`${wpt.chromePassed} / ${wpt.chromeTotal} assertions${wpt.referenceStatus?' · '+wpt.referenceStatus:''}`:'Unverified'"
replacement="wpt?(Number.isFinite(wpt.chromePassed)&&Number.isFinite(wpt.chromeTotal)?`${wpt.chromePassed} / ${wpt.chromeTotal} assertions${wpt.referenceStatus?' · '+wpt.referenceStatus:''}`:wpt.referenceStatus||'Unverified'):'Unverified'"
if text.count(needle)!=1:raise RuntimeError('Reference facts UI anchor changed')
app.write_text(text.replace(needle,replacement))
allow+=evidence_files+[str(scorepath),'agent/devsummary.md','agent/continue.md','AGENTS.md',str(updater),str(app)]
for name in allow:
    path=Path(name)
    if not path.is_file()or path.is_symlink():raise RuntimeError('Unexpected allowlist file: '+name)
    data=path.read_bytes()
    if re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data):raise RuntimeError('Private-key material in proposed source')
    for config in [Path('output/session-102900614/progress-state/config.json'),Path('output/session-10290244/progress-state/config.json')]:
        if config.is_file():
            settings=read(config)
            for key in ('messageKey','token','adminKey'):
                secret=settings.get(key)
                if isinstance(secret,str)and len(secret)>=16 and secret.encode()in data:raise RuntimeError('Private message credential in proposed source')
subprocess.run(['git','--no-pager','add','--',*allow],check=True)
if set(git('diff','--cached','--name-only').splitlines())!=set(allow):raise RuntimeError('Index contains unexpected paths or an expected path is unchanged')
for name,expected in compiled.items():
    if hashlib.sha256(git('show',':'+name,raw=True)).hexdigest()!=expected:raise RuntimeError('Staged production source does not match compiled manifest: '+name)
subprocess.run(['git','--no-pager','diff','--cached','--check'],check=True)
subprocess.run(['git','--no-pager','commit','-m','fix: transfer ArrayBuffers across window and worker messaging'],check=True)
commit=git('rev-parse','HEAD')
print(json.dumps({'iteration':entry['iteration'],'commit':commit,'score':40,'historicalScore':50,'binarySHA256':NATIVE,'files':allow,'compiledInputsMatchedIndex':len(compiled),'untouchedWorkingChanges':git('diff','--name-only').splitlines(),'totalSessionSeconds':entry['total_session_seconds']},indent=2))
