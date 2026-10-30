"""Run qualified Chrome against one exact native cohort group, keeping all real failures."""
from pathlib import Path
import argparse, datetime, hashlib, importlib.util, json, os, time, traceback
p=argparse.ArgumentParser();p.add_argument('--plan',type=Path,required=True);p.add_argument('--group',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--max-seconds',type=float,default=180);p.add_argument('--resume',action='store_true');a=p.parse_args()
root=Path.cwd().resolve();sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest();read=lambda path:json.loads(Path(path).read_text())
def save(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);temp=path.with_suffix(path.suffix+'.tmp');temp.write_text(json.dumps(value,indent=2)+'\n');temp.replace(path)
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
plan=read(a.plan);qualification_path=root/'agent/evidence/102900614-006-chrome-reference.json';qualification=read(qualification_path)
expected='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'
if not qualification.get('qualifiedForPairedMeasurement') or not qualification.get('standardsFailuresRetained') or qualification['binarySHA256']!=expected:raise RuntimeError('Fresh signed reference qualification differs')
binary=Path(qualification['binaryPath'])
if sha(binary)!=expected or sha(plan['binary']['path'])!=plan['binary']['sha256']:raise RuntimeError('One of the immutable executables changed')
if plan['task']!='102900614' or plan['fullFrozenVariantCount']!=1515 or plan['fullFrozenScheduleSHA256']!='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe' or a.group not in plan['groups']:raise RuntimeError('Native cohort or frozen schedule changed')
module=load('qualified_reference_inputs',root/'tools/apple-session-gpt6as/run_priority_wpt_10290244.py');module.BASE=Path(plan['fixture'])
frozen,categories,environment,hashes,runner=module.current_inputs()
if hashes!=plan['inputs'] or frozen['schedule']!=plan['frozenSchedule']:raise RuntimeError('Current fixture/runner differs from the native cohort')
for row in plan['helperInputs']:
    if sha(row['path'])!=row['sha256']:raise RuntimeError('Pinned helper changed: '+row['source'])
schedule=plan['groups'][a.group];native=a.plan.parent/a.group
if not (native/'run.json').exists() or not (native/'results.jsonl').exists():raise RuntimeError('Run the native group before paired measurement')
native_meta=read(native/'run.json')
if native_meta['signature']['binary_sha256']!=plan['binary']['sha256'] or runner.verified_schedule(native,native_meta['signature'])!=schedule:raise RuntimeError('Native group does not match its artifact and exact schedule')
selected,_=runner.select_schedule(read(plan['inputs']['inventory']['path']),'testharness',[row['test'] for row in schedule])
if selected!=schedule:raise RuntimeError('Exact URL selection changed')
out=a.output.resolve()
if not out.is_relative_to((root/'output/session-102900614').resolve()):raise RuntimeError('Reference output must be task scoped')
identity={'taskId':'102900614','nativeCohort':str(a.plan),'nativeCohortSHA256':sha(a.plan),'nativeBinarySHA256':plan['binary']['sha256'],'referenceBinarySHA256':expected,'referenceQualificationSHA256':sha(qualification_path),'group':a.group,'groupScheduleSHA256':plan['groupScheduleSHA256'][a.group],'frozenScheduledVariants':1515,'helperSHA256':sha(__file__),'scope':'Exact pinned group comparison; all failed and unrun cases remain visible. No full WPT or performance claim.'}
if (out/'paired-identity.json').exists():
    if not a.resume or read(out/'paired-identity.json')!=identity:raise RuntimeError('Existing output cannot be reused without an identical explicit resume')
else:
    out.mkdir(parents=True,exist_ok=False);save(out/'paired-identity.json',identity)
for name in tuple(os.environ):
    if name.startswith('LUMEN_'):del os.environ[name]
options=argparse.Namespace(inventory=Path(plan['inputs']['inventory']['path']),engine='chromium',binary=binary,output=out,
    types='testharness',prefix=[row['test'] for row in schedule],offset=0,limit=None,max_seconds=a.max_seconds,
    server_config=Path(plan['inputs']['server_config']['path']),fixture_manifest=Path(plan['inputs']['fixture_readiness']['path']),
    strict_infrastructure=False,timeout=15,long_timeout=65,origin=plan['origins']['http'],secure_origin=plan['origins']['https'],h2_origin=plan['origins']['h2'],
    chromium_arg=environment['chromiumArguments'],chromium_headful=True,resume=a.resume)
start=time.monotonic();receipt={'startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),**identity,'invocationBudgetSeconds':a.max_seconds};error=None
try:
    runner.run_command(options)
    if sha(binary)!=expected:raise RuntimeError('Reference executable changed during execution')
    meta=read(out/'run.json');rows=runner.read_jsonl(out/'results.jsonl')
    resource_errors=[row['test'] for row in rows if runner.measurement_failure(row) or (row.get('cleanup')or{}).get('error') or (row.get('cleanup')or{}).get('profile_error')]
    stops={key:value for key,value in meta.items() if key.startswith('stopped_for_')}
    disqualifying={key:value for key,value in stops.items() if key!='stopped_for_time_budget'}
    receipt.update(recorded=len(rows),scheduled=len(schedule),unrun=len(schedule)-len(rows),rawFullyPassing=sum(bool(runner.row_passes(row)) for row in rows),resourceErrorCases=resource_errors,stops=stops,cleanup=meta.get('chromium_cleanup'),measurementQualified=not resource_errors and not disqualifying)
    if not receipt['measurementQualified']:raise RuntimeError('Reference execution/telemetry cleanup incomplete; retain raw reports without accepted parity')
    comparison=out/'paired-comparison.json';runner.compare_command(argparse.Namespace(lumen=native,chromium=out,output=comparison))
    receipt.update(comparison=str(comparison.relative_to(root)),comparisonSHA256=sha(comparison),rawResultsSHA256=sha(out/'results.jsonl'),runSHA256=sha(out/'run.json'),passed=True)
except BaseException as exc:
    error=exc;receipt.update(passed=False,error=type(exc).__name__+': '+str(exc),traceback=traceback.format_exc())
finally:
    receipt.update(finishedUTC=datetime.datetime.now(datetime.timezone.utc).isoformat(),durationSeconds=round(time.monotonic()-start,3))
    save(out/('paired-invocation-'+str(time.time_ns())+'.json'),receipt)
    print(json.dumps({key:receipt[key] for key in ('group','nativeBinarySHA256','referenceBinarySHA256','recorded','scheduled','unrun','rawFullyPassing','measurementQualified','passed','error','durationSeconds') if key in receipt},indent=2),flush=True)
if error:raise error
