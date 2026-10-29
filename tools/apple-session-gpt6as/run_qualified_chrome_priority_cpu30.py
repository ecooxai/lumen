#!/usr/bin/env python3
"""Run exactly the additional176 frozen priority variants in qualified Chrome.

Keeps every raw Chrome failure and the complete1515-row frozen denominator. Earlier qualified103 results remain immutable and separate until their disjoint union is explicitly audited.
This control does not claim full WPT, nor promote any native candidate.
"""
from pathlib import Path
from datetime import datetime, timezone
from collections import Counter
import argparse,hashlib,importlib.util,json,os,signal,subprocess,sys,time

TASK='20261029-cpu30'
CHROME_SHA='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'
QUALIFICATION_SHA='1e34a36c89d61da6314e975fb8fe8296e0bd2e07ac73cc3836861785fcac5452'
GROUPS=['dom','events','url','storage','microtasks','headers']
ROOT=Path.cwd().resolve()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_text())
utc=lambda:datetime.now(timezone.utc).isoformat()
def save(path,value):
 path=Path(path);temporary=path.with_suffix(path.suffix+'.tmp');temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)
def require(condition,message):
 if not condition:raise RuntimeError(message)
def module_at(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def inputs(native_plan):
 plan=read(native_plan)
 require(plan['wptRevision']=='7f8d49b5a82606efc771f43d10aad5bce5ccbfeb','Wrong frozen WPT revision')
 require(plan['fullFrozenVariantCount']==1515 and plan['fullFrozenScheduleSHA256']=='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe','Frozen denominator differs')
 require([len(plan['groups'][g]) for g in GROUPS]==[14,16,77,7,10,52],'Exact additional176-variant group sizes differ')
 for row in plan['inputs'].values():require(sha(row['path'])==row['sha256'],'Frozen input changed: '+Path(row['path']).name)
 tool=module_at('chrome_control_inputs',plan['inputs']['orchestrator']['path'])
 tool.BASE=Path(plan['fixture'])
 frozen,categories,environment,hashes,runner=tool.current_inputs()
 require(hashes==plan['inputs'] and frozen['schedule']==plan['frozenSchedule'],'Control input signatures differ from native cohort')
 qualification_path=ROOT/'agent/evidence/102900614-006-chrome-reference.json'
 require(sha(qualification_path)==QUALIFICATION_SHA,'Qualified comparator evidence changed')
 qualification=read(qualification_path)
 require(qualification['qualifiedForPairedMeasurement'] and qualification['standardsFailuresRetained'] and qualification['binarySHA256']==CHROME_SHA,'Comparator is not qualified')
 chrome=Path(qualification['binaryPath']);require(sha(chrome)==CHROME_SHA,'Qualified Chrome bytes changed')
 return plan,environment,runner,chrome,qualification

def execute_group(args):
 plan,environment,runner,chrome,qualification=inputs(args.native_plan)
 schedule=plan['groups'][args.group]
 selected,_=runner.select_schedule(read(plan['inputs']['inventory']['path']),'testharness',[r['test'] for r in schedule])
 require(selected==schedule,'Exact prefixes do not reproduce the native group')
 for name in tuple(os.environ):
  if name.startswith('LUMEN_'):del os.environ[name]
 options=argparse.Namespace(inventory=Path(plan['inputs']['inventory']['path']),engine='chromium',binary=chrome,
  output=args.output/args.group,types='testharness',prefix=[r['test'] for r in schedule],offset=0,limit=None,max_seconds=240,
  server_config=Path(plan['inputs']['server_config']['path']),fixture_manifest=Path(plan['inputs']['fixture_readiness']['path']),
  strict_infrastructure=False,timeout=15,long_timeout=65,origin=plan['origins']['http'],secure_origin=plan['origins']['https'],
  h2_origin=plan['origins']['h2'],chromium_arg=environment['chromiumArguments'],chromium_headful=True,resume=False)
 runner.run_command(options)
 require(sha(chrome)==CHROME_SHA,'Comparator changed during execution')

def evidence_error(runner,row):
 cleanup=row.get('cleanup') or {}
 return runner.measurement_failure(row) or cleanup.get('error') or cleanup.get('profile_error')

def audit_group(plan,runner,out,name):
 directory=out/name;meta=read(directory/'run.json');signature=meta['signature'];native=read(Path(plan['_path']).parent/name/'run.json')['signature']
 require(signature['engine']=='chromium' and signature['binary_sha256']==CHROME_SHA,'Recorded control artifact differs')
 require(signature['schedule_sha256']==plan['groupScheduleSHA256'][name],'Recorded control schedule differs')
 for key in ('wpt_revision','manifest_sha256','inventory_sha256','source_fixture','server_config_sha256','fixture_manifest_sha256','runner_sha256','reporter_sha256','monitor_sha256','origin','secure_origin','h2_origin','timeout_s','long_timeout_s','strict_infrastructure','verified_http_reporter_sha256'):
  require(signature[key]==native[key],'Native/control signature mismatch: '+key)
 require(runner.verified_schedule(directory,signature)==plan['groups'][name],'Control schedule file differs')
 rows=[json.loads(line) for line in (directory/'results.jsonl').read_text().splitlines()]
 require(len(rows)==len(plan['groups'][name]) and len({r['test'] for r in rows})==len(rows),'Control did not complete exact group once')
 require({r['test'] for r in rows}=={r['test'] for r in plan['groups'][name]},'Control result test identities differ')
 native_rows=[json.loads(line) for line in (Path(plan['_path']).parent/name/'results.jsonl').read_text().splitlines()]
 require({r['test']:r['url'] for r in rows}=={r['test']:r['url'] for r in native_rows},'Native/control actual document URLs differ')
 stops=[key for key,value in meta.items() if key.startswith('stopped_for_') and value]
 require(not stops and meta.get('finished_schedule'),'Control infrastructure/cleanup stopped the schedule: '+str(stops))
 require(not any(evidence_error(runner,row) for row in rows),'Control contains invalid telemetry or cleanup evidence')
 cleanup=meta.get('chromium_cleanup') or {}
 require(not cleanup.get('errors') and not (cleanup.get('process') or {}).get('error'),'Control process cleanup failed')
 passed=lambda row:row.get('status') in ('OK','PASS') and bool(row.get('subtests')) and all(s['status']=='PASS' for s in row['subtests'])
 return {'scheduled':len(rows),'recorded':len(rows),'fullyPassing':sum(passed(r) for r in rows),'harnessStatuses':dict(Counter(r['status'] for r in rows)),
  'assertions':dict(Counter(s['status'] for r in rows for s in r.get('subtests',[]))),
  'failures':[{'test':r['test'],'status':r['status'],'message':r.get('message'),'assertions':[s for s in r.get('subtests',[]) if s['status']!='PASS']} for r in rows if not passed(r)],
  'rawResults':str(directory/'results.jsonl'),'rawResultsSHA256':sha(directory/'results.jsonl'),'runMetadataSHA256':sha(directory/'run.json'),
  'browserVersion':meta.get('browser_version'),'signatureComparedWithNative':True,'actualDocumentUrlsMatchNative':True,
  'workerWrapperUrls':[{'test':r['test'],'url':r['url'],'status':r['status']} for r in rows if '.serviceworker.' in r['test'] or '.sharedworker.' in r['test']]}

def run(args):
 out=args.output.resolve();out.relative_to(ROOT);out.mkdir(parents=True,exist_ok=False)
 began=time.monotonic();summary={'taskId':TASK,'startedUTC':utc(),'scope':'Exact additional176 frozen priority variants on qualified Chrome; not full WPT or native artifact acceptance. Actual Chrome failures are preserved.',
  'scheduledFrozenVariants':1515,'scheduledControlVariants':176,'nativePlan':str(args.native_plan),'nativePlanSHA256':sha(args.native_plan),'chromeBinarySHA256':CHROME_SHA,'groups':{},'stages':[],'complete':False}
 def persist():save(out/'summary.json',summary)
 persist()
 try:
  plan,environment,runner,chrome,qualification=inputs(args.native_plan);plan['_path']=str(args.native_plan)
  summary.update(chromeBinary=str(chrome),nativeBinaryAtScheduling=plan['binary'],wptRevision=plan['wptRevision'],frozenScheduleSHA256=plan['fullFrozenScheduleSHA256'],qualificationEvidence='agent/evidence/102900614-006-chrome-reference.json',qualificationSHA256=QUALIFICATION_SHA)
  save(out/'cohort.json',{'schema':1,'taskId':TASK,'createdUTC':utc(),'engine':'chromium','binary':{'path':str(chrome),'sha256':CHROME_SHA},'groups':{g:plan['groups'][g] for g in GROUPS},'fullFrozenVariantCount':1515,'fullFrozenScheduleSHA256':plan['fullFrozenScheduleSHA256'],'groupScheduleSHA256':{g:plan['groupScheduleSHA256'][g] for g in GROUPS},'inputs':plan['inputs'],'nativePlan':str(args.native_plan),'nativePlanSHA256':sha(args.native_plan),'scope':summary['scope']})
  for name in GROUPS:
   started=time.monotonic();row={'name':name,'startedUTC':utc()};summary['stages'].append(row);persist()
   command=[sys.executable,'tools/monitor_process.py','--duration','270','--settle','0','--output',str(out/(name+'-resources')),'--',sys.executable,str(Path(__file__).resolve()),'--native-plan',str(args.native_plan),'--output',str(out),'--group',name]
   with (out/(name+'-driver.log')).open('x') as log:
    process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    try:row['exitCode']=process.wait(timeout=290)
    except subprocess.TimeoutExpired:
     row['outerTimeout']=True;os.killpg(process.pid,signal.SIGTERM)
     try:row['exitCode']=process.wait(timeout=10)
     except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);row['exitCode']=process.wait(timeout=5)
   metric=read(out/(name+'-resources.json'))
   row.update(finishedUTC=utc(),wallSeconds=round(time.monotonic()-started,3),monitorReceipt=str(out/(name+'-resources.json')),monitorSHA256=sha(out/(name+'-resources.json')),
    naturalExitCode=metric.get('natural_exit_code'),collectorError=metric.get('collector_error'),endedByMonitor=metric.get('ended_by_monitor'),intervalCpuSamples=metric.get('interval_cpu_samples'),cpuPeakPercent=metric.get('interval_cpu_peak_percent'),rssPeakMiB=metric.get('rss_peak_mb'),maximumSamplingGapSeconds=metric.get('max_sampling_gap_s'))
   persist()
   require(row['exitCode']==0 and row['naturalExitCode']==0 and not row['collectorError'] and not row['endedByMonitor'] and not row.get('outerTimeout'),'Control stage infrastructure failed: '+name)
   summary['groups'][name]=audit_group(plan,runner,out,name);persist()
  summary['recorded']=sum(x['recorded'] for x in summary['groups'].values());summary['fullyPassing']=sum(x['fullyPassing'] for x in summary['groups'].values())
  summary['unrunOfFrozenSchedule']=1515-summary['recorded'];summary['passingPercentOfObserved']=100*summary['fullyPassing']/summary['recorded']
  summary['assertions']=dict(sum((Counter(x['assertions']) for x in summary['groups'].values()),Counter()))
  summary['harnessStatuses']=dict(sum((Counter(x['harnessStatuses']) for x in summary['groups'].values()),Counter()))
  summary['chromeBinarySHA256After']=sha(chrome);require(summary['chromeBinarySHA256After']==CHROME_SHA,'Control bytes changed')
  summary['complete']=summary['recorded']==176
 except BaseException as error:summary['error']=type(error).__name__+': '+str(error)
 finally:
  summary.update(finishedUTC=utc(),wallSeconds=round(time.monotonic()-began,3));persist()
  print(json.dumps({k:v for k,v in summary.items() if k!='groups'},indent=2),flush=True)
 raise SystemExit(0 if summary['complete'] and not summary.get('error') else 1)

parser=argparse.ArgumentParser()
parser.add_argument('--native-plan',type=Path,default=Path('out/lume-20261029-cpu30/compat-parser-1/cohort/cohort.json'))
parser.add_argument('--output',type=Path,default=Path('out/lume-20261029-cpu30/chrome-control-2'))
parser.add_argument('--group',choices=GROUPS)
args=parser.parse_args()
if args.group:execute_group(args)
else:run(args)
