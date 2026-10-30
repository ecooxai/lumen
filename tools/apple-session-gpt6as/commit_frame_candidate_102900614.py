"""Commit the verified queued child completion delta without staging later candidates."""
from pathlib import Path
from collections import Counter
import datetime as dt, hashlib, json, os, re, shutil, subprocess
os.environ.update(GIT_PAGER='cat', PAGER='cat')
BASE=Path('output/session-102900614')
NATIVE='4d81029fec2383cdb1e25bfe7b91fba65640f244e4564c19a344686f7ee5b5c5'
JSRUN='2f3170840352af15bbe42d5222f8e021013d385f96f3e80a293dc54f6201d0f0'
EXPECTED={'src/js/js.cc':'ae8deb196729291c038dca13533808eb06e5c3ad236bf2d1ef815e8abe5f878b'}
FIXTURE='tests/test_frame_load_tasks.py'; FIXTURE_SHA='64edfda6febcfe6d4c634faeae0587024a4b5df57fdbd79746e2ed08f2694ed8'
def git(*args,raw=False):
    data=subprocess.check_output(['git','--no-pager',*args]);return data if raw else data.decode().strip()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text())
def write(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614' or git('diff','--cached','--name-only'):raise RuntimeError('Branch/index coordination required')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['history'][-1]['iteration']!='102900614-009' or score['score']!=45:raise RuntimeError('Checkpoint010 order or score differs')
for name,digest in {**EXPECTED,FIXTURE:FIXTURE_SHA}.items():
    if sha(name)!=digest:raise RuntimeError('Reviewed frame source/fixture differs: '+name)
patchpath=BASE/'frame-completion-candidate-1/patch-receipt.json';patch=read(patchpath)
if patch['after']!=EXPECTED or patch['fixtureSHA256']!=FIXTURE_SHA:raise RuntimeError('Frame patch identity differs')
greenpath=BASE/'frame-completion-green-1/summary.json';green=read(greenpath)
if not green['passed'] or len(green['stages'])!=9 or green['changedCompiledSources']!=['src/js/js.cc'] or green['artifacts']['lumen']['sha256']!=NATIVE or green['artifacts']['jsrun']['sha256']!=JSRUN:raise RuntimeError('Frame green identity differs')
for artifact in green['artifacts'].values():
    if sha(artifact['snapshot'])!=artifact['sha256']:raise RuntimeError('Frozen frame artifact differs')
for stage in green['stages']:
    monitor=read(stage['monitor'])
    if not stage['passed'] or stage['naturalExitCode']!=0 or stage['endedByMonitor'] or monitor.get('collector_error') or monitor.get('natural_exit_code')!=0 or monitor.get('ended_by_monitor'):raise RuntimeError('Frame build/test telemetry incomplete')
    if sha(Path(stage['monitor']).with_suffix('.log'))!=stage['logSHA256']:raise RuntimeError('Frame stage raw log changed')
compiledpath=Path(green['compiledSourceManifest']);compiled=read(compiledpath)
for name,digest in compiled.items():
    actual=sha(name) if name in EXPECTED else hashlib.sha256(git('show','HEAD:'+name,raw=True)).hexdigest()
    if actual!=digest:raise RuntimeError('Frame-only prospective index differs: '+name)
redpath=BASE/'frame-load-tasks-red-1.json';red=read(redpath)
if red.get('natural_exit_code')!=1 or red.get('collector_error') or red.get('ended_by_monitor') or 'RESULTS frame-load-tasks FAIL (2/7 checks)'not in redpath.with_suffix('.log').read_text():raise RuntimeError('Frozen2/7 frame RED missing')
cohort=BASE/'wpt-frame-native-1';planpath=cohort/'cohort.json';plan=read(planpath);group=cohort/'encoding-streams';meta=read(group/'run.json');schedule=read(group/'schedule.json');rows=[json.loads(line)for line in(group/'results.jsonl').read_text().splitlines()]
if plan['binary']['sha256']!=NATIVE or plan['fullFrozenVariantCount']!=1515 or plan['fullFrozenScheduleSHA256']!='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe':raise RuntimeError('Frame frozen1515 cohort differs')
if len(schedule)!=26 or schedule!=plan['groups']['encoding-streams'] or len(rows)!=26 or len({r['test']for r in rows})!=26 or {r['test']for r in rows}!={r['test']for r in schedule} or meta['signature']['binary_sha256']!=NATIVE or meta['signature']['schedule_sha256']!=plan['groupScheduleSHA256']['encoding-streams']:raise RuntimeError('Frame upstream group identity differs')
fully=lambda r:r.get('status')in('OK','PASS') and bool(r.get('subtests')) and all(s['status']=='PASS'for s in r['subtests'])
if not all(fully(r)for r in rows) or Counter(s['status']for r in rows for s in r['subtests'])!=Counter(PASS=358):raise RuntimeError('Reviewed26/26 upstream result differs')
for row in rows:
    if (row.get('metrics')or{}).get('errors') or row.get('telemetry_errors') or (row.get('cleanup')or{}).get('error') or (row.get('cleanup')or{}).get('profile_error'):raise RuntimeError('Frame upstream infrastructure incomplete')
tools=['tools/apple-session-gpt6as/'+name for name in('apply_frame_completion_102900614.py','build_frame_completion_102900614.py')]
allow=[*EXPECTED,FIXTURE,*tools];tracked=set(git('ls-files','-z',raw=True).decode().split('\0'))
for name in allow:
    if not Path(name).is_file():raise RuntimeError('Required authored source/helper missing: '+name)
    if name not in tracked and any(line.rstrip(b' \t')!=line for line in Path(name).read_bytes().splitlines()):raise RuntimeError('Authored whitespace requires correction: '+name)
subprocess.run(['git','--no-pager','diff','--check','--',*allow],check=True)
folder=Path('agent/evidence/102900614-010');folder.mkdir(exist_ok=False)
copy_paths=[patchpath,greenpath,compiledpath,redpath,redpath.with_suffix('.log'),redpath.with_suffix('.csv'),planpath,*[group/n for n in('run.json','schedule.json','results.jsonl','summary.json')]]
for stage in green['stages']:copy_paths += [Path(stage['monitor']),Path(stage['monitor']).with_suffix('.log')]
evidence=[];raw=[]
for index,path in enumerate(dict.fromkeys(copy_paths)):
    destination=folder/('%02d-'%index+path.parent.name+'-'+path.name);shutil.copyfile(path,destination);allow.append(str(destination));evidence.append({'source':str(path),'committed':str(destination),'sha256':sha(path),'bytes':path.stat().st_size})
    if path.suffix in('.log','.csv'):raw.append(str(destination))
manifestpath=folder/'compiled-source-sha256.json';write(manifestpath,compiled);allow.append(str(manifestpath))
review={'schema':1,'binarySHA256':NATIVE,'scope':'Unchanged26 Encoding Streams/window/dedicated-worker variants, including IDL; subset of frozen1515 inventory','recorded':26,'fullyPassing':26,'frozenScheduledVariants':1515,'passingPercentOfRun':100.0,'assertions':{'PASS':358},'fullOfficialWptPassRate':None,'knownLimits':['This reviewed snapshot contains only the26 Encoding Streams variants. Other groups are running separately and are not combined here.','No original1850 rerun is claimed for this exact frame artifact by this checkpoint.','Not complete asynchronous parsing/navigation or comprehensive frame conformance.']}
reviewpath=folder/'stream-wpt-review.json';write(reviewpath,review);allow.append(str(reviewpath))
now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
entry={'iteration':'102900614-010','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':45,'status':'Queued child completion candidate:7/7 regression checks and26/26 fresh upstream stream variants','native_artifact_promoted':False,'accepted_native_unchanged':score['current_baseline_binary_sha256'],'candidate_binary_sha256':NATIVE,'candidate_jsrun_sha256':JSRUN,'candidate_source_manifest':str(manifestpath),'red':{'passed':2,'total':7},'green':{'passed':7,'total':7,'existing_load_order':14,'microtask_realm':35,'native_frame_removal':True,'native_window_lifecycle':True,'text_streams':74,'transfer':68,'legacy_codecs':178,'build_seconds':4.106,'all_build_test_seconds':16.046},'diagnostic_wpt':review,'evidence':evidence,'raw_byte_preservation':raw,'implementation':'Defer non-initial-blank child completion to the event loop; preserve parent blockers, synchronous initial about:blank, removal/replacement cancellation, and avoid immediate deadlines for dead or closed realms.','limits':review['knownLimits']+['Fresh combined resources/navigation soak and same signed IPA execution remain pending.'],'model_compute_duration':'Unavailable; parallel worker wall times are not summed'}
entrypath=Path('agent/evidence/102900614-010-frame-completion.json');write(entrypath,entry);allow.append(str(entrypath))
score.setdefault('candidate_artifacts',[]).append({'iteration':entry['iteration'],'binary_sha256':NATIVE,'source_manifest':str(manifestpath),'evidence':str(entrypath),'accepted':False,'note':'Queued child completion validated;26/26 upstream stream variants pass; fresh combined performance/soak pending.'})
score['latest_functional_candidate']={'binary_sha256':NATIVE,'evidence':str(entrypath),'evidence_sha256':sha(entrypath),'stream_wpt':str(reviewpath),'stream_wpt_sha256':sha(reviewpath)}
score['history'].append({key:entry[key]for key in('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:out.write(f"\n## 102900614-010 — queued child-document completion\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; checkpoint `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Accepted **45/100**, artifact733 unchanged.\n- Frozen frame regression **2/7 RED →7/7 GREEN**; existing load order14, microtask realm35, native frame-removal/window-lifecycle gates, streams74, transfer68 and legacy178 pass. Build4.106s; nine build/test stages16.046s under1Hz monitoring, with prebuilt V8 reused. Short tests do not establish performance.\n- Exact4d81029 upstream **26/26 Encoding Streams variants and358/358 assertions** pass in13.355s, including the formerly timed-out iframe realm cases. This subset does not establish a full WPT passrate; other groups are recorded separately. No original1850 rerun on4d is claimed here.\n- Initial about:blank remains synchronous; child completion now waits for its task and retains parent blocking/removal/replacement guards. Dead or closed realms do not schedule immediate completion loops. This is not comprehensive asynchronous frame parsing/navigation. Evidence `{entrypath}`.\n")
with Path('agent/continue.md').open('a')as out:out.write(f"\n## Checkpoint010 — queued frame completion\n\nNative4d81029 `{NATIVE}` passes7frame checks, existing lifecycle gates and26/26 pinned Encoding Streams variants (358 assertions). Other expanded groups are separate. Source is only src/js/js.cc; meta/body/XHR working changes are excluded. Accepted733/score45 unchanged; combined current-artifact resources, original suite and navigation soak remain required. See `{entrypath}`.\n")
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md']
secrets=[p.read_bytes().strip()for p in Path('output').glob('session-*/progress-state/message-access-key')]
for name in allow:
    data=Path(name).read_bytes()
    if Path(name).is_symlink() or any(len(secret)>=24 and secret in data for secret in secrets) or re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data):raise RuntimeError('Private/unexpected publication material: '+name)
subprocess.run(['git','--no-pager','add','--',*allow],check=True)
if set(git('diff','--cached','--name-only').splitlines())!=set(allow):raise RuntimeError('Frame staging allowlist differs')
for name,digest in compiled.items():
    if hashlib.sha256(git('show',':'+name,raw=True)).hexdigest()!=digest:raise RuntimeError('Staged frame source differs: '+name)
for row in evidence:
    if hashlib.sha256(git('show',':'+row['committed'],raw=True)).hexdigest()!=row['sha256']:raise RuntimeError('Staged raw evidence differs')
subprocess.run(['git','--no-pager','diff','--cached','--check','--',*[n for n in allow if n not in raw]],check=True)
subprocess.run(['git','--no-pager','commit','-m','fix: queue child document completion after insertion'],check=True)
print(json.dumps({'iteration':entry['iteration'],'commit':git('rev-parse','HEAD'),'score':45,'nativePromoted':False,'candidateBinarySHA256':NATIVE,'compiledInputsMatched':len(compiled),'files':allow,'untouchedWorkingChanges':git('diff','--name-only').splitlines(),'totalSessionSeconds':entry['total_session_seconds']},indent=2))
