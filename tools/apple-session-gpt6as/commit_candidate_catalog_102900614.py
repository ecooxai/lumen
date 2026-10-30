"""Commit artifact-specific public progress and verified raw reference evidence."""
from pathlib import Path
import datetime as dt,hashlib,importlib.util,json,os,re,shutil,subprocess,sys
os.environ.update(GIT_PAGER='cat',PAGER='cat')
BASE=Path('output/session-102900614');sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
EXPECTED={'tools/apple-session-gpt6as/update_priority_progress_10290244.py': '3f38c3dbd69d701f638a946f2cca6790461a89b9cebdd9d7d2e18732cd6adea1', 'tools/lume-progress-10290244/public/app.js': '88462246a89a448d812c8bf1374dae4bd0e83ba234e084d51d9139dfe81bceea', 'tools/apple-session-gpt6as/candidate_catalog_102900614.py': '518af88ecb25b8e1baa696e95ab55ea8762728fa1f303166c28585289d6d3f81', 'tests/test_candidate_catalog_102900614.py': '7fe9de31f3bb37f17f05b31f9405148c571fcb6dc244618ec36b52a35e40c97c'}

def git(*args,raw=False):
    value=subprocess.check_output(['git','--no-pager',*args]);return value if raw else value.decode().strip()
def read(p):return json.loads(Path(p).read_text())
def write(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614'or git('diff','--cached','--name-only'):raise RuntimeError('Branch/index coordination required')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['score']!=45 or score['history'][-1]['iteration']!='102900614-015':raise RuntimeError('Checkpoint016 order or score differs')
for name,digest in EXPECTED.items():
    if sha(name)!=digest:raise RuntimeError('Verified dashboard source differs: '+name)
installers=[Path('tools/apple-session-gpt6as')/n for n in('install_candidate_catalog_102900614.py','install_candidate_catalog.py')]
installer=next((p for p in installers if p.is_file()and sha(p)=='b61c52152182f5d09f0f7ea1e679126a3b997a98f9c84767f00678b9e72ad194'),None)
if installer is None:raise RuntimeError('Executed dashboard reproduction helper missing')
patchpath=BASE/'progress-candidate-catalog-1/patch-receipt.json';patch=read(patchpath)
if not patch['applied']or patch['after']!=EXPECTED:raise RuntimeError('Executed dashboard patch receipt differs')
monitorpath=BASE/'progress-catalog-tests-1.json';monitor=read(monitorpath);testlog=monitorpath.with_suffix('.log').read_text()
if monitor.get('natural_exit_code')!=0 or monitor.get('ended_by_monitor')or monitor.get('collector_error')or 'Ran 6 tests in'not in testlog or not re.search(r'\bOK\b',testlog):raise RuntimeError('Six integrity tests did not finish cleanly')
module_path=Path('tools/apple-session-gpt6as/candidate_catalog_102900614.py');spec=importlib.util.spec_from_file_location('catalog_checkpoint',module_path);module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
q=score['reference_qualification'];qualified={'binarySHA256':q['binary_sha256'],'evidenceSHA256':q['sha256']}
catalog=module.build_candidate_catalog(Path.cwd(),'102900614',score['current_baseline_binary_sha256'],qualified)
reference=catalog['qualifiedChrome']
if catalog['validationErrors']or len(catalog['cohorts'])<3 or not reference or reference['fullyPassing']!=23 or reference['recorded']!=26 or reference['assertions']!={'PASS':344,'FAIL':14}:raise RuntimeError('Real candidate/reference ledger verification incomplete')
pairpath=BASE/'chrome-qualified-streams-1/frame-paired-comparison.json';pair=read(pairpath)
if pair['scheduled_files']!=26 or pair['lumen_binary_sha256']!='4d81029fec2383cdb1e25bfe7b91fba65640f244e4564c19a344686f7ee5b5c5'or pair['chromium_binary_sha256']!=reference['binarySha256']or pair['parity']['counts'].get('chromium_passing_assertions')!=344 or pair['parity']['counts'].get('also_passed_by_lumen')!=344:raise RuntimeError('Exact26-case paired reference differs')
allow=[*EXPECTED,str(installer)];tracked=set(git('ls-files','-z',raw=True).decode().split('\0'))
for name in allow:
    if name not in tracked and any(line.rstrip(b' \t')!=line for line in Path(name).read_bytes().splitlines()):raise RuntimeError('Authored whitespace requires correction: '+name)
subprocess.run(['git','--no-pager','diff','--check','--',*allow],check=True)
folder=Path('agent/evidence/102900614-016');folder.mkdir(exist_ok=False)
paths=[patchpath,monitorpath,monitorpath.with_suffix('.log'),monitorpath.with_suffix('.csv'),pairpath]
reference_dir=BASE/'chrome-qualified-streams-1'
paths += [reference_dir/n for n in('paired-identity.json','run.json','schedule.json','results.jsonl','summary.json','paired-comparison.json')]
paths += sorted(reference_dir.glob('paired-invocation-*.json'))
evidence=[];raw=[]
for index,path in enumerate(dict.fromkeys(paths)):
    destination=folder/('%02d-'%index+path.parent.name+'-'+path.name);shutil.copyfile(path,destination);allow.append(str(destination));evidence.append({'source':str(path),'committed':str(destination),'sha256':sha(path),'bytes':path.stat().st_size})
    if path.suffix in('.log','.csv'):raw.append(str(destination))
catalogpath=folder/'candidate-catalog.json';write(catalogpath,catalog);allow.append(str(catalogpath))
now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
entry={'iteration':'102900614-016','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':45,'status':'Live progress reports verified separate native cohorts and raw qualified Chrome outcomes','native_artifact_promoted':False,'accepted_native_unchanged':score['current_baseline_binary_sha256'],'tests':{'passed':6,'total':6,'scope':'Synthetic ledger integrity, not browser assertions; subsecond execution is not performance evidence.'},'catalog':str(catalogpath),'catalogSHA256':sha(catalogpath),'qualified_chrome':reference,'paired_frame_streams':{'variants':26,'lumen_fully_passing':26,'chrome_fully_passing':23,'chrome_assertions':{'PASS':344,'FAIL':14},'chrome_passing_assertions_retained':344,'retention':1.0,'full_platform_parity_claimed':False},'evidence':evidence,'raw_byte_preservation':raw,'implementation':'Validate frozen1515 denominators, artifact/cohort hashes and raw stable-prefix ledgers. Display latest measured cohort prominently; keep accepted artifact, each candidate, historical207 and actual reference failures distinct. Existing2-second telemetry and private inbox remain unchanged.','model_compute_duration':'Unavailable; parallel worker wall times are not summed'}
entrypath=Path('agent/evidence/102900614-016-progress-catalog.json');write(entrypath,entry);allow.append(str(entrypath));score['history'].append({k:entry[k]for k in('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();score['candidate_catalog_evidence']={'path':str(entrypath),'sha256':sha(entrypath)};write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:out.write(f"\n## 102900614-016 — verified live candidate catalog\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; checkpoint `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Accepted **45/100**, artifact733 unchanged.\n- Six synthetic integrity tests pass. Real local refresh independently checks raw ledgers against each cohort and frozen1515 schedule, with no validation errors. The hero shows latest measured results; separate rows retain other artifact identities, unrun variants and historical207.\n- Qualified Chrome actually passes **23/26** Encoding Streams variants, **344 PASS/14 FAIL** assertions. Exact paired frame4d streams pass26/26 and retain344/344 Chrome-passing assertions. This is one26-case subset, not full browser parity; all Chrome failures remain raw failures.\n- Existing2-second CPU/RAM/disk/latency telemetry and private message path remain intact. Evidence `{entrypath}`.\n")
with Path('agent/continue.md').open('a')as out:out.write(f"\n## Checkpoint016 — progress catalog\n\nThe updater now verifies and displays separate artifact-specific native WPT cohorts, keeps all1515 scheduled variants and real unrun/failures, and shows qualified Chrome23/26 with344PASS/14FAIL. Frame4d retains344/344 Chrome-passing assertions on the same26 cases only. Public rate is a measured cohort rate; full official WPT remains unknown. Run LUME_TASK_ID=102900614 update_priority_progress_10290244.py after each group/checkpoint. Accepted733/score45 unchanged. Evidence `{entrypath}`.\n")
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md']
secrets=[p.read_bytes().strip()for p in Path('output').glob('session-*/progress-state/message-access-key')]
for name in allow:
    data=Path(name).read_bytes()
    if Path(name).is_symlink()or any(len(secret)>=24 and secret in data for secret in secrets)or re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data):raise RuntimeError('Unexpected publication material: '+name)
subprocess.run(['git','--no-pager','add','--',*allow],check=True)
expected={name for name in allow if(git('show','HEAD:'+name,raw=True)if name in tracked else None)!=Path(name).read_bytes()}
if set(git('diff','--cached','--name-only').splitlines())!=expected:raise RuntimeError('Catalog staging allowlist differs')
for row in evidence:
    if hashlib.sha256(git('show',':'+row['committed'],raw=True)).hexdigest()!=row['sha256']:raise RuntimeError('Raw reference evidence differs')
subprocess.run(['git','--no-pager','diff','--cached','--check','--',*[n for n in allow if n not in raw]],check=True)
subprocess.run(['git','--no-pager','commit','-m','docs: report verified WPT cohorts by native artifact'],check=True)
print(json.dumps({'iteration':entry['iteration'],'commit':git('rev-parse','HEAD'),'score':45,'nativePromoted':False,'catalogCohorts':[{'binary':r['binarySha256'],'run':r['recorded'],'fullyPassing':r['fullyPassing'],'unrun':r['unrun']}for r in catalog['cohorts']],'referenceFullyPassing':23,'referenceRun':26,'files':sorted(expected),'untouchedWorkingChanges':git('diff','--name-only').splitlines()},indent=2))
