"""Commit shared stream implementation with artifact-bound evidence, without resource promotion."""
from pathlib import Path
from collections import Counter
import datetime as dt, hashlib, importlib.util, json, os, re, shutil, subprocess
os.environ.update(GIT_PAGER='cat',PAGER='cat')
ROOT=Path.cwd();TASK='102900614';NATIVE='80a60752a025a3b1c0bfda4485c9e37c9da2e203eaab63b5cc884ccaec218129';JSRUN='582aefb38f35e97167654b550a6afbe69c93b8998f990e3622adae0ad7bafbfa'
def git(*args,raw=False):
    value=subprocess.check_output(['git','--no-pager',*args]);return value if raw else value.decode().strip()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text())
def write(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614' or git('diff','--cached','--name-only'):raise RuntimeError('Branch/index coordination required')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['history'][-1]['iteration']!='102900614-006' or score['score']!=45:raise RuntimeError('Checkpoint007 ordering or accepted score changed')
source=['Makefile','src/js/prelude/07-web.js','src/js/prelude/06t-streams.js','src/js/worker.cc','src/js/idl_state.h','src/js/clone_brand.h','tests/test_writable_streams.mjs']
base=Path('output/session-102900614');patchpath=base/'text-streams-candidate-1/patch-receipt.json';patch=read(patchpath)
if set(patch['after'])!=set(source):raise RuntimeError('Reviewed stream source allowlist differs')
for name in source:
    if sha(name)!=patch['after'][name]:raise RuntimeError('Stream candidate changed before checkpoint: '+name)
for name,expected in patch['testFixtures'].items():
    if sha(name)!=expected:raise RuntimeError('Frozen regression fixture changed: '+name)
greenpath=base/'text-streams-green-1/summary.json';green=read(greenpath)
if not green['passed'] or len(green['stages'])!=12 or green['artifacts']['lumen']['sha256']!=NATIVE or green['artifacts']['jsrun']['sha256']!=JSRUN:raise RuntimeError('Stream artifact/native regression summary differs')
for artifact in green['artifacts'].values():
    if sha(artifact['snapshot'])!=artifact['sha256']:raise RuntimeError('Frozen regression executable changed')
for stage in green['stages']:
    if not stage['passed'] or stage['naturalExitCode']!=0 or stage['endedByMonitor'] or stage['durationSeconds']>300:raise RuntimeError('Measured regression gate incomplete')
    monitor=read(stage['monitor'])
    if monitor.get('collector_error') or monitor.get('ended_by_monitor') or monitor.get('natural_exit_code')!=0:raise RuntimeError('Regression telemetry or cleanup failure')
compiledpath=Path(green['compiledSourceManifest']);compiled=read(compiledpath)
for name,expected in compiled.items():
    actual=sha(name) if name in source else hashlib.sha256(git('show','HEAD:'+name,raw=True)).hexdigest()
    if actual!=expected:raise RuntimeError('Stream-only committed source differs from its build manifest: '+name)
redpaths=[Path(patch['red']['encodingStreams']['report']),Path(patch['red']['transformLifecycle']['report'])]
red_encoding=redpaths[0].read_text();red_transform=redpaths[1].read_text()
if not re.search(r'"total"\s*:\s*74\s*,\s*"passed"\s*:\s*10\s*,\s*"failed"\s*:\s*64',red_encoding):raise RuntimeError('Unchanged text stream10/74 RED absent')
if not all(marker in red_transform for marker in ('# tests 13','# pass 5','# fail 6','# cancelled 2')):raise RuntimeError('Expected Transform lifecycle RED absent')
planpath=base/'wpt-streams-native-1/cohort.json';plan=read(planpath);originalpath=planpath.parent/'original-six-receipt.json';original=read(originalpath)
if plan['binary']['sha256']!=NATIVE or plan['fullFrozenVariantCount']!=1515 or plan['fullFrozenScheduleSHA256']!='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe':raise RuntimeError('Pinned native cohort differs')
if not original['passed'] or original['binarySHA256']!=NATIVE or original['files']!=6 or original['fullyPassing']!=6 or original['assertions']!={'PASS':1850} or sha(original['report'])!=original['reportSHA256']:raise RuntimeError('Original six-file native WPT gate incomplete')
group=planpath.parent/'encoding-streams';meta=read(group/'run.json');schedule=read(group/'schedule.json');rows=[json.loads(line) for line in (group/'results.jsonl').read_text().splitlines()]
if schedule!=plan['groups']['encoding-streams'] or len(schedule)!=26 or meta['signature']['binary_sha256']!=NATIVE or meta['signature']['schedule_sha256']!=plan['groupScheduleSHA256']['encoding-streams']:raise RuntimeError('Exact upstream stream group identity differs')
if len(rows)!=26 or len({r['test'] for r in rows})!=26 or {r['test'] for r in rows}!={r['test'] for r in schedule}:raise RuntimeError('Missing, duplicate or foreign upstream stream row')
for row in rows:
    if (row.get('metrics')or{}).get('errors') or row.get('telemetry_errors') or (row.get('cleanup')or{}).get('error') or (row.get('cleanup')or{}).get('profile_error'):raise RuntimeError('Upstream stream telemetry or cleanup failure')
fully=lambda row:row.get('status')in('OK','PASS') and bool(row.get('subtests')) and all(s['status']=='PASS'for s in row['subtests'])
passing=sum(bool(fully(row))for row in rows);counts=Counter(s['status']for row in rows for s in row.get('subtests',[]));failed=[row for row in rows if not fully(row)]
if passing!=23 or counts!=Counter(PASS=338,FAIL=8) or Counter(r['status']for r in rows)!=Counter(OK=25,TIMEOUT=1):raise RuntimeError('Reviewed23/26 raw upstream outcomes changed')
expected_failed={'/encoding/streams/decode-non-utf8.any.html','/encoding/streams/decode-non-utf8.any.worker.html','/encoding/streams/realms.window.html'}
if {row['test']for row in failed}!=expected_failed:raise RuntimeError('Unreviewed upstream failure appeared')

# Preflight authored files before producing checkpoint documents. No whitespace exemption.
tools=[]
for name in ('apply_text_streams_102900614.py','build_text_streams_102900614.py','run_task_wpt_102900614.py','run_remaining_wpt_102900614.py'):
    path=Path('tools/apple-session-gpt6as')/name
    if not path.is_file():raise RuntimeError('Reproduction helper missing: '+str(path))
    tools.append(str(path))
allow=source+['tests/test_text_streams.py','tests/test_transform_streams.mjs']+tools
subprocess.run(['git','--no-pager','diff','--check','--',*allow],check=True)
tracked_names=set(git('ls-files','--',*allow).splitlines())
for name in allow:
    if name in tracked_names:continue
    data=Path(name).read_text()
    if any(line.endswith((' ','\t'))for line in data.splitlines()):raise RuntimeError('New authored trailing whitespace must be resolved before checkpoint: '+name)

now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
folder=Path('agent/evidence/102900614-007');folder.mkdir(exist_ok=False)
copy_paths=[patchpath,greenpath,compiledpath,planpath,originalpath,group/'run.json',group/'schedule.json',group/'results.jsonl',group/'summary.json']
for stage in green['stages']:copy_paths.append(Path(stage['monitor']))
for path in redpaths:
    copy_paths.extend([path,path.with_suffix('.json'),path.with_suffix('.csv')])
evidence=[]
for index,path in enumerate(copy_paths):
    destination=folder/('%02d-'%index+path.parent.name+'-'+path.name)
    shutil.copyfile(path,destination);allow.append(str(destination));evidence.append({'source':str(path),'committed':str(destination),'sha256':sha(path),'bytes':path.stat().st_size})
results_summary={'schema':1,'binarySHA256':NATIVE,'scope':'Unchanged26 Encoding Streams/window/dedicated-worker variants, including two IDL variants; diagnostic subset of fixed1515 inventory','scheduled':26,'recorded':26,'fullyPassing':23,'passingPercentOfRun':round(2300/26,3),'frozenScheduledVariants':1515,'frozenUnrunOutsideThisGroup':1489,'assertions':dict(counts),'harnessStatuses':dict(Counter(r['status']for r in rows)),'failures':[{'test':r['test'],'status':r['status'],'assertions':[s for s in r.get('subtests',[])if s['status']!='PASS']}for r in failed],'inputEvidence':evidence[3:9],'knownLimits':['Legacy multibyte incremental decoding is still missing in this exact artifact.','The cross-realm case times out before assertions because srcdoc iframe load currently dispatches synchronously; no realm conformance credit.','Other groups may run separately in the same immutable cohort; these26 counts are not combined with historical207 results.']}
resultpath=folder/'stream-wpt-review.json';write(resultpath,results_summary);allow.append(str(resultpath))
entry={'iteration':'102900614-007','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':45,'status':'Shared stream candidate validated; original WPT retained;23/26 fresh upstream stream variants pass','native_artifact_promoted':False,'accepted_native_unchanged':score['current_baseline_binary_sha256'],'candidate_binary_sha256':NATIVE,'candidate_jsrun_sha256':JSRUN,'candidate_source_manifest':str(folder/('02-'+compiledpath.parent.name+'-'+compiledpath.name)),'compiled_inputs':len(compiled),'source_manifest_sha256':sha(compiledpath),'red':{'encoding_streams':patch['red']['encodingStreams'],'transform_lifecycle':patch['red']['transformLifecycle']},'green':{'custom_checks_passed':946,'custom_checks_total':946,'text_streams_checks':74,'transform_checks':13,'writable_checks':26,'duration_seconds':green['durationSeconds'],'build_seconds':green['stages'][0]['durationSeconds']},'original_wpt':{'files':6,'fully_passing':6,'assertions_passed':1850,'assertions_total':1850,'duration_seconds':original['durationSeconds'],'report_sha256':original['reportSHA256']},'stream_wpt':results_summary,'evidence':evidence,'resource_scope':'Every native/build regression stage uses1Hz process-tree monitoring. Short assertion runs do not establish idle, scrolling or soak acceptance.733 remains the accepted resource artifact; no2dac or733 idle numbers are transferred to80a.','standards_scope':'UTF streaming, backpressure/error propagation and private nonserializable Encoding Stream brands in windows and dedicated workers. Broader Streams, legacy streaming and cross-realm behavior are not fully conformant.','model_compute_duration':'Unavailable; parallel worker wall times are not summed'}
entrypath=Path('agent/evidence/102900614-007-shared-streams.json');write(entrypath,entry);allow.append(str(entrypath))
score.setdefault('candidate_artifacts',[]).append({'iteration':entry['iteration'],'binary_sha256':NATIVE,'source_manifest':entry['candidate_source_manifest'],'evidence':str(entrypath),'accepted':False,'note':'Verified functional shared-stream candidate with original6/6 and fresh23/26 upstream variants; combined resource/soak acceptance pending.'})
score['latest_functional_candidate']={'binary_sha256':NATIVE,'evidence':str(entrypath),'evidence_sha256':sha(entrypath),'stream_wpt':str(resultpath),'stream_wpt_sha256':sha(resultpath)}
score['history'].append({key:entry[key]for key in('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:out.write(f"\n## 102900614-007 — shared Encoding Streams candidate\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; checkpoint `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Current accepted score **45/100** and733 artifact remain unchanged; this is a verified functional candidate.\n- Encoding Stream fixture **10/74 RED →74/74 GREEN**. Transform lifecycle fixture originally5 Node-reference checks pass,6 Lumen assertions fail and2 Lumen cases time out; unchanged fixture now **13/13** passes. Unchanged writable checks **26/26** and all custom gates **946/946** pass. Build `{green['stages'][0]['durationSeconds']}s`; full build/regression `{green['durationSeconds']}s`;1Hz telemetry, existing matched prebuilt V8 reused.\n- Original upstream **6/6 files,1850/1850 assertions** remain passing on80a in `{original['durationSeconds']}s`. The fresh pinned stream diagnostic is **23/26 fully passing variants**,338 passed assertions,8 failed assertions and1 zero-assertion timeout. Both IDL and backpressure files pass.\n- The two non-UTF8 failures need legacy incremental decoding; realms.window times out at synchronous srcdoc frame loading. These failures remain unchanged and visible. All1515 variants remain frozen; this26-variant subset is not a full WPT rate or a replacement for the historical207-row cohort.\n- Source/fixture/compiled-manifest identities are checked against this isolated commit. Raster, legacy-codec and frame-lifecycle working edits are excluded. No733/2dac resource numbers are attributed to80a. Evidence `{entrypath}`.\n")
with Path('agent/continue.md').open('a')as out:out.write(f"\n## Checkpoint007 — verified shared-stream functional candidate\n\nCandidate `{green['artifacts']['lumen']['snapshot']}` SHA-256 `{NATIVE}` passes946 custom assertions and original6/6 files,1850/1850 assertions. Fresh upstream Encoding Streams group23/26 fully passes; non-UTF8 window/worker and cross-realm timeout remain. Accepted resource artifact733 and score45 unchanged until combined current-artifact gates pass. All1515 inventory rows retained. Exact source/evidence: `{entrypath}`. Do not merge historical207 results into this candidate's rates.\n")
with Path('AGENTS.md').open('a')as out:out.write(f"\n### Current functional candidate007\n\nShared-stream native `{NATIVE}` has946/946 custom checks, original1850/1850 and fresh23/26 stream variants. Legacy decoding and iframe realm timing remain known failures. Current accepted artifact733/score45 is unchanged pending combined resources. Exact candidate source manifest and evidence are in `agent/evidence/102900614-007-shared-streams.json`.\n")
# Publish the separate candidate evidence without changing accepted WPT/performance facts.
updater=Path('tools/apple-session-gpt6as/update_priority_progress_10290244.py');text=updater.read_text();needle="status['expandedWpt']=value;status['updatedUTC']=datetime.now(timezone.utc).isoformat()"
replacement="""candidate=score.get('latest_functional_candidate')
if candidate:
    candidate_path=ROOT/candidate['stream_wpt']
    if sha(candidate_path)!=candidate['stream_wpt_sha256']:raise RuntimeError('Candidate WPT evidence changed')
    candidate_wpt=json.loads(candidate_path.read_text())
    if candidate_wpt['binarySHA256']!=candidate['binary_sha256']:raise RuntimeError('Candidate WPT artifact differs')
    status['functionalCandidate']={'binarySha256':candidate['binary_sha256'],'accepted':False,
        'fullyPassingVariants':candidate_wpt['fullyPassing'],'recordedVariants':candidate_wpt['recorded'],
        'scheduledFrozenVariants':candidate_wpt['frozenScheduledVariants'],'assertions':candidate_wpt['assertions'],
        'scope':candidate_wpt['scope'],'evidence':candidate['stream_wpt']}
else:status.pop('functionalCandidate',None)
status['expandedWpt']=value;status['updatedUTC']=datetime.now(timezone.utc).isoformat()"""
if text.count(needle)!=1:raise RuntimeError('Candidate progress anchor changed')
text=text.replace(needle,replacement);compile(text,str(updater),'exec');updater.write_text(text)
app=Path('tools/lume-progress-10290244/public/app.js');text=app.read_text();needle="  $('compatibility').replaceChildren(...facts);"
replacement="""  const candidate=status.functionalCandidate;
  if(candidate)facts.push(fact('Latest functional candidate',`${candidate.binarySha256.slice(0,12)}… · ${candidate.fullyPassingVariants}/${candidate.recordedVariants} Encoding Stream variants fully pass · ${candidate.scheduledFrozenVariants} frozen total · resource acceptance pending`));
  $('compatibility').replaceChildren(...facts);"""
if text.count(needle)!=1:raise RuntimeError('Candidate facts UI anchor changed')
app.write_text(text.replace(needle,replacement));subprocess.run(['node','--check',str(app)],check=True)
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md','AGENTS.md',str(updater),str(app)]
secrets=[path.read_bytes().strip()for path in Path('output').glob('session-*/progress-state/message-access-key')]
for name in allow:
    path=Path(name)
    if not path.is_file()or path.is_symlink():raise RuntimeError('Unexpected publication file: '+name)
    data=path.read_bytes()
    if any(len(secret)>=24 and secret in data for secret in secrets):raise RuntimeError('Private capability found in proposed source')
subprocess.run(['git','--no-pager','add','--',*allow],check=True)
if set(git('diff','--cached','--name-only').splitlines())!=set(allow):raise RuntimeError('Candidate staged allowlist differs')
for name,expected in compiled.items():
    if hashlib.sha256(git('show',':'+name,raw=True)).hexdigest()!=expected:raise RuntimeError('Isolated staged production source differs: '+name)
subprocess.run(['git','--no-pager','diff','--cached','--check'],check=True)
subprocess.run(['git','--no-pager','commit','-m','feat: share incremental Encoding Streams across windows and workers'],check=True)
print(json.dumps({'iteration':entry['iteration'],'commit':git('rev-parse','HEAD'),'score':45,'nativePromoted':False,'acceptedNativeUnchanged':score['current_baseline_binary_sha256'],'candidateBinarySHA256':NATIVE,'files':allow,'compiledInputsMatchedIndex':len(compiled),'untouchedWorkingChanges':git('diff','--name-only').splitlines(),'totalSessionSeconds':entry['total_session_seconds']},indent=2))
