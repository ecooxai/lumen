"""Checkpoint legacy incremental decoding with exact103-variant diagnostic evidence."""
from pathlib import Path
from collections import Counter
import datetime as dt,hashlib,json,os,re,shutil,subprocess
os.environ.update(GIT_PAGER='cat',PAGER='cat')
ROOT=Path.cwd();BASE=Path('output/session-102900614');NATIVE='050b0160e0869c4ac9e840a1835403c7dbb678c90a49c3619e0acf3275d007b6';JSRUN='9dbf792698c8e504d9f3d93df6b28de9e6c95ee180b1e089d6e33b4a5d6e10a8'
EXPECTED={'src/base/encoding.c':'fc928f380ee0357e6f307bdf04058a6462e91c8b3674ee9768e37395ee690339','src/base/encoding.h':'fa2a85fa515c861c047f63587d0e4c00113366e10ce54d79cd3e86ed34dd3920','src/js/text_codec.h':'9b64d6725aa4c0f8a58a8ed457f8f5898f3c929b3c9f415b9f0b798f397891cf','src/js/prelude/06u-textcodec.js':'7cade02452e3346de75880f5a2d7ebfafa7f1beb448d95af17124643c34a99a4'}
FIXTURES={'tests/test_legacy_codecs.py':'b562ebb8d62f5679e7eefffaec90e5a46d591116d83e0273423e9aad74d04d2c','tests/test_encoding_stream.c':'aa9c910563d625047e52a6d17b504f326bf2173898146ed7fafd07a6d58cf986'}
def git(*args,raw=False):
    value=subprocess.check_output(['git','--no-pager',*args]);return value if raw else value.decode().strip()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text())
def write(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614' or git('diff','--cached','--name-only'):raise RuntimeError('Branch/index coordination required')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['history'][-1]['iteration']!='102900614-008' or score['score']!=45:raise RuntimeError('Checkpoint009 order or score changed')
for name,digest in {**EXPECTED,**FIXTURES}.items():
    if sha(name)!=digest:raise RuntimeError('Reviewed legacy source/fixture changed: '+name)
patchpath=BASE/'legacy-codecs-candidate-1/patch-receipt.json';patch=read(patchpath)
if patch['after']!=EXPECTED or patch['checks']!=178 or not patch['applied']:raise RuntimeError('Legacy patch receipt differs')
greenpath=BASE/'legacy-codecs-green-1/summary.json';green=read(greenpath)
if not green['passed'] or len(green['stages'])!=11 or green['artifacts']['lumen']['sha256']!=NATIVE or green['artifacts']['jsrun']['sha256']!=JSRUN:raise RuntimeError('Native legacy green identity differs')
for artifact in green['artifacts'].values():
    if sha(artifact.get('snapshot',artifact['path']))!=artifact['sha256']:raise RuntimeError('Frozen native artifact changed')
for stage in green['stages']:
    monitor=read(stage['monitor'])
    if not stage['passed'] or stage['naturalExitCode']!=0 or stage['endedByMonitor'] or monitor.get('collector_error') or monitor.get('natural_exit_code')!=0 or monitor.get('ended_by_monitor'):raise RuntimeError('Legacy stage telemetry/cleanup incomplete')
if 'RESULTS legacy-native PASS cases=459119 persistentStateBytes=44' not in green['stages'][-1]['resultLines']:raise RuntimeError('Exact ASan/UBSan bounds gate absent')
compiledpath=Path(green['compiledSourceManifest']);compiled=read(compiledpath)
for name,digest in compiled.items():
    actual=sha(name) if name in EXPECTED else hashlib.sha256(git('show','HEAD:'+name,raw=True)).hexdigest()
    if actual!=digest:raise RuntimeError('Legacy-only prospective index differs: '+name)
redpath=BASE/'legacy-codecs-red-1.json';red=read(redpath);redlog=redpath.with_suffix('.log').read_text()
if red.get('natural_exit_code')!=1 or red.get('collector_error') or red.get('ended_by_monitor') or not re.search(r'"total"\s*:\s*178\s*,\s*"passed"\s*:\s*14\s*,\s*"failed"\s*:\s*164',redlog):raise RuntimeError('Frozen14/178 RED missing')
cohort=BASE/'wpt-legacy-native-1';planpath=cohort/'cohort.json';plan=read(planpath);originalpath=cohort/'original-six-receipt.json';original=read(originalpath)
if plan['binary']['sha256']!=NATIVE or plan['fullFrozenVariantCount']!=1515 or plan['fullFrozenScheduleSHA256']!='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe':raise RuntimeError('Frozen1515 legacy cohort differs')
if not original['passed'] or original['binarySHA256']!=NATIVE or original['files']!=6 or original['fullyPassing']!=6 or original['assertions']!={'PASS':1850} or original['reportSHA256']!='baf0869f97f61390d7e10a0da697f3d10e869e98666e9e7be96a63616a4d0c7f' or sha(original['report'])!=original['reportSHA256']:raise RuntimeError('Original upstream gate differs')
fully=lambda row:row.get('status')in('OK','PASS') and bool(row.get('subtests')) and all(s['status']=='PASS'for s in row['subtests'])
groups={};allrows=[];group_paths=[]
for group,scheduled,expected_pass in [('encoding-streams',26,25),('encoding-core',77,65)]:
    directory=cohort/group;meta=read(directory/'run.json');schedule=read(directory/'schedule.json');rows=[json.loads(line)for line in(directory/'results.jsonl').read_text().splitlines()]
    if schedule!=plan['groups'][group] or len(schedule)!=scheduled or len(rows)!=scheduled or len({r['test']for r in rows})!=scheduled or {r['test']for r in rows}!={r['test']for r in schedule} or meta['signature']['binary_sha256']!=NATIVE or meta['signature']['schedule_sha256']!=plan['groupScheduleSHA256'][group]:raise RuntimeError('Diagnostic group identity differs: '+group)
    for row in rows:
        if (row.get('metrics')or{}).get('errors') or row.get('telemetry_errors') or (row.get('cleanup')or{}).get('error') or (row.get('cleanup')or{}).get('profile_error'):raise RuntimeError('Diagnostic telemetry/cleanup error')
    count=sum(bool(fully(row))for row in rows)
    if count!=expected_pass:raise RuntimeError('Reviewed diagnostic result changed: '+group)
    groups[group]={'scheduled':scheduled,'recorded':len(rows),'fullyPassing':count,'assertions':dict(Counter(s['status']for r in rows for s in r.get('subtests',[]))),'harnessStatuses':dict(Counter(r['status']for r in rows)),'failures':[{'test':r['test'],'status':r['status'],'assertions':[s for s in r.get('subtests',[])if s['status']!='PASS']}for r in rows if not fully(r)]};allrows+=rows;group_paths += [directory/name for name in('run.json','schedule.json','results.jsonl','summary.json')]
counts=Counter(s['status']for r in allrows for s in r.get('subtests',[]))
if counts!=Counter(PASS=23408,FAIL=235) or Counter(r['status']for r in allrows)!=Counter(OK=101,TIMEOUT=2):raise RuntimeError('Reviewed103-row raw outcomes differ')
tools=['tools/apple-session-gpt6as/'+name for name in('apply_legacy_codecs_102900614.py','build_legacy_codecs_102900614.py','run_codec_wpt_102900614.py')]
allow=[*EXPECTED,*FIXTURES,*tools];tracked=set(git('ls-files','-z',raw=True).decode().split('\0'))
for name in allow:
    if not Path(name).is_file():raise RuntimeError('Required authored source/helper missing: '+name)
    if name not in tracked and any(line.rstrip(b' \t')!=line for line in Path(name).read_bytes().splitlines()):raise RuntimeError('Authored whitespace requires review: '+name)
subprocess.run(['git','--no-pager','diff','--check','--',*allow],check=True)
folder=Path('agent/evidence/102900614-009');folder.mkdir(exist_ok=False)
copy_paths=[patchpath,greenpath,compiledpath,redpath,redpath.with_suffix('.log'),redpath.with_suffix('.csv'),planpath,originalpath,cohort.parent/(cohort.name+'-invocation.json'),*group_paths]
for stage in green['stages']:copy_paths += [Path(stage['monitor']),Path(stage['monitor']).with_suffix('.log')]
evidence=[];raw=[]
for index,path in enumerate(dict.fromkeys(copy_paths)):
    destination=folder/('%02d-'%index+path.parent.name+'-'+path.name);shutil.copyfile(path,destination);allow.append(str(destination));evidence.append({'source':str(path),'committed':str(destination),'sha256':sha(path),'bytes':path.stat().st_size})
    if path.suffix in('.log','.csv'):raw.append(str(destination))
manifestpath=folder/'compiled-source-sha256.json';write(manifestpath,compiled);allow.append(str(manifestpath))
review={'schema':1,'binarySHA256':NATIVE,'scheduled':1515,'run':103,'fullyPassing':90,'unrun':1412,'passingPercentOfRun':87.379,'fullyPassingPercentOfFrozenSchedule':round(9000/1515,3),'assertions':dict(counts),'harnessStatuses':dict(Counter(r['status']for r in allrows)),'groups':groups,'fullOfficialWptPassRate':None,'note':'Only this immutable103-row native cohort. Historical207 and80a results are not combined.'};reviewpath=folder/'wpt-review.json';write(reviewpath,review);allow.append(str(reviewpath))
stream_review={'schema':1,'binarySHA256':NATIVE,'scope':'Unchanged26 Encoding Streams/window/dedicated-worker variants, including IDL; subset of frozen1515 inventory','recorded':26,'fullyPassing':25,'frozenScheduledVariants':1515,'assertions':groups['encoding-streams']['assertions'],'knownLimits':['Cross-realm iframe completion still times out in this exact050b artifact; frame fix is a separate later candidate.']};streampath=folder/'stream-wpt-review.json';write(streampath,stream_review);allow.append(str(streampath))
now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
entry={'iteration':'102900614-009','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':45,'status':'Legacy incremental codecs candidate:178/178 and459119 sanitizer cases;90/103 fresh diagnostic variants pass','native_artifact_promoted':False,'accepted_native_unchanged':score['current_baseline_binary_sha256'],'candidate_binary_sha256':NATIVE,'candidate_jsrun_sha256':JSRUN,'candidate_source_manifest':str(manifestpath),'red':{'passed':14,'total':178},'green':{'passed':178,'total':178,'text_codec':170,'text_streams':74,'transfer':68,'document_encoding':78,'asan_ubsan_cases':459119,'persistent_state_bytes':44,'build_seconds':9.106,'all_build_test_seconds':20.3},'original_wpt':original,'diagnostic_wpt':review,'evidence':evidence,'raw_byte_preservation':raw,'implementation':'Native finite-state incremental decoders reuse immutable encoding tables with44-byte private state, at most3 restored bytes and one native call per chunk; only a fatal call retains its unconsumed input tail.','limits':['This artifact still has iframe/body-handler/meta/XHR gaps and two fatal UTF8 streaming failures. Raw failures remain unchanged.','Current accepted733/score45 unchanged; combined current-artifact resources and navigation soak remain pending.','Same signed IPA execution remains unproved.'],'model_compute_duration':'Unavailable; parallel worker wall times are not summed'}
entrypath=Path('agent/evidence/102900614-009-legacy-codecs.json');write(entrypath,entry);allow.append(str(entrypath))
score.setdefault('candidate_artifacts',[]).append({'iteration':entry['iteration'],'binary_sha256':NATIVE,'source_manifest':str(manifestpath),'evidence':str(entrypath),'accepted':False,'note':'Native legacy streaming validated;90/103 diagnostic variants pass,1412unrun; fresh combined performance/soak pending.'})
score['latest_functional_candidate']={'binary_sha256':NATIVE,'evidence':str(entrypath),'evidence_sha256':sha(entrypath),'stream_wpt':str(streampath),'stream_wpt_sha256':sha(streampath)}
score['history'].append({key:entry[key]for key in('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:out.write(f"\n## 102900614-009 — native incremental legacy decoding\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; checkpoint `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Accepted **45/100**, artifact733 unchanged.\n- Frozen legacy fixture **14/178 RED →178/178 GREEN**; codec170,streams74,transfer68,document78 and existing native encoding/encoder/URL gates remain green. AddressSanitizer/UBSan **459,119 cases** pass with44-byte persistent decoder state. Build9.106s; all build/test stages20.3s under1Hz monitoring, reusing prebuilt V8.\n- Exact050b original upstream **6/6 files,1850/1850 assertions** pass in9.234s. Fresh pinned diagnostic **90/103 variants fully pass (87.379%)**: streams25/26,core65/77;23,408 passed assertions,235 failed assertions,2timeouts,**1,412 unrun of1,515**. Full official WPT rate remains unknown.\n- Known iframe/body/meta/XHR failures and fatalUTF8 streaming cases remain visible. No historical207/80a measurements or later frame improvements are mixed into050b. Source/receipts `{entrypath}`. Current performance/soak and same-IPA gates remain pending.\n")
with Path('agent/continue.md').open('a')as out:out.write(f"\n## Checkpoint009 — legacy decoder candidate\n\nNative050b `{NATIVE}` passes178legacy checks,459119sanitizer cases, original1850 and fresh90/103 variants (25/26streams,65/77core).1412 of frozen1515 remain unrun. Cross-realm load still times out in this exact artifact; later frame fix must retain separate identity. Accepted733/score45 unchanged. See `{entrypath}`.\n")
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md']
secrets=[p.read_bytes().strip()for p in Path('output').glob('session-*/progress-state/message-access-key')]
for name in allow:
    data=Path(name).read_bytes()
    if Path(name).is_symlink() or any(len(secret)>=24 and secret in data for secret in secrets) or re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data):raise RuntimeError('Private/unexpected publication material: '+name)
subprocess.run(['git','--no-pager','add','--',*allow],check=True)
if set(git('diff','--cached','--name-only').splitlines())!=set(allow):raise RuntimeError('Legacy staging allowlist differs')
for name,digest in compiled.items():
    if hashlib.sha256(git('show',':'+name,raw=True)).hexdigest()!=digest:raise RuntimeError('Staged legacy production source differs: '+name)
for row in evidence:
    if hashlib.sha256(git('show',':'+row['committed'],raw=True)).hexdigest()!=row['sha256']:raise RuntimeError('Staged evidence bytes differ')
subprocess.run(['git','--no-pager','diff','--cached','--check','--',*[name for name in allow if name not in raw]],check=True)
subprocess.run(['git','--no-pager','commit','-m','feat: stream legacy text decoders with bounded native state'],check=True)
print(json.dumps({'iteration':entry['iteration'],'commit':git('rev-parse','HEAD'),'score':45,'nativePromoted':False,'candidateBinarySHA256':NATIVE,'compiledInputsMatched':len(compiled),'files':allow,'untouchedWorkingChanges':git('diff','--name-only').splitlines(),'totalSessionSeconds':entry['total_session_seconds']},indent=2))
