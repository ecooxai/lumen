#!/usr/bin/env python3
"""Artifact-bound native WPT cohorts retaining the complete frozen1515 inventory.

Execution groups prioritize Encoding Streams and ordinary window/worker tests.
They partition, rather than shrink, the frozen inventory. Historical results
and fixture identities are never overwritten or mixed into this artifact's rate.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import http.client
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import traceback
from urllib.parse import urlsplit

ROOT=Path.cwd().resolve()
FIXTURE=ROOT/'output/session-102900614/wpt-fixture-1'
PIN='7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
SCHEDULE='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe'
HELPERS=['resources/webidl2/lib/webidl2.js','resources/idlharness.js',
         'encoding/streams/resources/readable-stream-from-array.js',
         'encoding/streams/resources/readable-stream-to-array.js','common/sab.js',
         'interfaces/encoding.idl','interfaces/streams.idl','tools/serve/serve.py']
utc=lambda:datetime.now(timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_text())
def save(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_suffix(path.suffix+'.tmp')
    temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)
def load_inputs():
    path=ROOT/'tools/apple-session-gpt6as/run_priority_wpt_10290244.py'
    spec=importlib.util.spec_from_file_location('task_priority_inputs',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.BASE=FIXTURE
    return module, module.current_inputs()
def is_supported_realm(row):
    return not any(marker in row['test'] for marker in ('.serviceworker.','.sharedworker.'))
def groups_for(categories):
    streams=[r for r in categories['encoding'] if is_supported_realm(r) and
             (r['source']=='encoding/idlharness.any.js' or r['source'].startswith('encoding/streams/'))]
    if len(streams)!=26:raise ValueError('The expected26 pinned stream variants changed')
    stream_tests={r['test'] for r in streams}
    core=[r for r in categories['encoding'] if r['test'] not in stream_tests and is_supported_realm(r) and Path(r['source']).parent.as_posix()=='encoding']
    core_tests={r['test'] for r in core}
    remaining=[r for r in categories['encoding'] if r['test'] not in stream_tests|core_tests]
    groups={'encoding-streams':streams}
    groups.update({name:categories[name] for name in ['dom','events','url','storage','microtasks','headers']})
    groups['encoding-core']=core;groups['encoding-rest']=remaining
    return groups
def prepare(args):
    binary=args.binary.resolve()
    if sha(binary)!=args.sha256:raise ValueError('Native artifact SHA mismatch')
    module,(frozen,categories,environment,hashes,runner)=load_inputs()
    if frozen['schedule_sha256']!=SCHEDULE or frozen['wpt_revision']!=PIN:raise ValueError('Frozen identity changed')
    groups=groups_for(categories)
    union=sorted((row for rows in groups.values() for row in rows),key=lambda r:(r['type'],r['test']))
    if union!=frozen['schedule'] or len(union)!=1515:raise ValueError('Execution groups changed the frozen denominator')
    inventory=read(hashes['inventory']['path']);upstream=Path(inventory['wpt_root'])
    helper_rows=[]
    for name in HELPERS:
        expected=subprocess.check_output(['git','show',PIN+':'+name],cwd=upstream)
        path=upstream/name;restored=False
        if not path.exists():
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(expected);restored=True
        if path.read_bytes()!=expected:raise ValueError('Pinned upstream helper differs: '+name)
        helper_rows.append({'path':str(path),'source':name,'sha256':sha(path),'restoredMissingPinnedHelper':restored})
    # The official pinned WPT server rewrites this public URL to webidl2;
    # WebIDLParser.js is not a missing source file and must not be fabricated.
    routing=(upstream/'tools/serve/serve.py').read_text()
    if '("GET", "/resources/WebIDLParser.js", "/resources/webidl2/lib/webidl2.js")' not in routing:
        raise ValueError('Pinned WPT WebIDLParser rewrite is absent')
    origin=urlsplit(environment['origins']['http'])
    connection=http.client.HTTPConnection('127.0.0.1',origin.port or 80,timeout=10)
    try:
        connection.request('GET','/resources/WebIDLParser.js',headers={'Host':origin.netloc})
        response=connection.getresponse();served=response.read()
        if response.status!=200 or served!=(upstream/'resources/webidl2/lib/webidl2.js').read_bytes():
            raise ValueError('Official WebIDLParser route does not serve pinned parser bytes')
    finally:connection.close()
    route={'url':environment['origins']['http']+'/resources/WebIDLParser.js',
           'source':'resources/webidl2/lib/webidl2.js','sha256':hashlib.sha256(served).hexdigest(),
           'routingSource':'tools/serve/serve.py','status':200,'checkedUTC':utc()}
    runner.source_fixture_signature(inventory)
    args.output.mkdir(parents=True,exist_ok=False)
    plan={'schema':1,'task':'102900614','createdUTC':utc(),'scope':'Native-only frozen priority cohort; never full WPT or Chrome parity',
          'binary':{'path':str(binary),'sha256':args.sha256},'wptRevision':PIN,'fullFrozenScheduleSHA256':SCHEDULE,
          'fullFrozenVariantCount':1515,'frozenSchedule':frozen['schedule'],'categoryCounts':frozen['category_counts'],
          'groups':groups,'groupScheduleSHA256':{name:module.digest(rows) for name,rows in groups.items()},
          'inputs':hashes,'helperInputs':helper_rows,'helperRoutes':[route],'fixture':str(FIXTURE),'origins':environment['origins'],
          'timeoutSeconds':15,'longTimeoutSeconds':65,'runnerToolSHA256':sha(__file__),
          'executionOrder':list(groups),'referenceStatus':'No Chromium result is incorporated into this native cohort.',
          'limitations':['Every unexecuted variant stays explicitly unrun in the1515 denominator.',
                         'No WebDriver/testdriver, reference, manual or protocol-test acceptance is implied.',
                         'Shared/service workers and long legacy tables remain scheduled in encoding-rest.',
                         'Subsecond case telemetry cannot establish peak memory or an idle performance verdict.']}
    save(args.output/'cohort.json',plan)
    print(json.dumps({'cohort':str(args.output/'cohort.json'),'scheduled':1515,'groups':{k:len(v) for k,v in groups.items()},'binarySHA256':args.sha256}),flush=True)
def validate(plan_path):
    plan=read(plan_path)
    if sha(plan['binary']['path'])!=plan['binary']['sha256']:raise ValueError('Cohort artifact changed')
    if sha(__file__)!=plan['runnerToolSHA256']:raise ValueError('Cohort orchestration code changed')
    module,(frozen,categories,environment,hashes,runner)=load_inputs()
    if plan['inputs']!=hashes or plan['frozenSchedule']!=frozen['schedule']:raise ValueError('Cohort inputs changed; create a new cohort')
    if plan['groups']!=groups_for(categories):raise ValueError('Cohort execution partition changed')
    for row in plan['helperInputs']:
        if sha(row['path'])!=row['sha256']:raise ValueError('Pinned helper changed: '+row['source'])
    return plan,module,environment,runner
def report(plan_path):
    plan,module,environment,runner=validate(plan_path);out=plan_path.parent
    rows=[];invocations=[];group_summary={}
    inventory=read(plan['inputs']['inventory']['path'])
    expected_signature={'engine':'lumen','wpt_revision':PIN,'manifest_sha256':inventory['manifest_sha256'],
        'inventory_sha256':plan['inputs']['inventory']['sha256'],'runner_sha256':plan['inputs']['runner']['sha256'],
        'reporter_sha256':plan['inputs']['reporter']['sha256'],'monitor_sha256':plan['inputs']['monitor']['sha256'],
        'server_config_sha256':plan['inputs']['server_config']['sha256'],
        'fixture_manifest_sha256':plan['inputs']['fixture_readiness']['sha256'],
        'origin':plan['origins']['http'],'secure_origin':plan['origins']['https'],'h2_origin':plan['origins']['h2'],
        'timeout_s':15,'long_timeout_s':65,'strict_infrastructure':False,
        'native_fixture':{'host_map':environment['nativeEnvironment']['LUMEN_HOST_MAP'],
                          'host_map_mode':'exact','ssl_cert_sha256':sha(environment['nativeEnvironment']['SSL_CERT_FILE'])}}
    def evidence_error(row):
        if runner.measurement_failure(row):return runner.measurement_failure(row)
        cleanup=row.get('cleanup') or {}
        return cleanup.get('error') or cleanup.get('profile_error')
    def passed_row(row):
        return row.get('status') in ('OK','PASS') and bool(row.get('subtests')) and all(s['status']=='PASS' for s in row['subtests']) and not evidence_error(row)
    for name,schedule in plan['groups'].items():
        directory=out/name
        if not (directory/'results.jsonl').exists():group_summary[name]={'scheduled':len(schedule),'run':0,'fullyPassing':0,'unrun':len(schedule)};continue
        meta=read(directory/'run.json');signature=meta['signature']
        if signature.get('binary_sha256')!=plan['binary']['sha256'] or signature.get('schedule_sha256')!=plan['groupScheduleSHA256'][name]:raise ValueError('Results belong to a different artifact or group')
        for key,value in expected_signature.items():
            if signature.get(key)!=value:raise ValueError('Recorded result input differs: '+key)
        if runner.verified_schedule(directory,signature)!=schedule:raise ValueError('Recorded group schedule differs')
        seen=set();current=[]
        for line in (directory/'results.jsonl').read_text().splitlines():
            row=json.loads(line)
            if row['test'] in seen or not any(row['test']==r['test'] for r in schedule):raise ValueError('Duplicate or foreign case result')
            seen.add(row['test']);current.append(row)
        rows.extend(current)
        passed=sum(bool(passed_row(r)) for r in current)
        group_summary[name]={'scheduled':len(schedule),'run':len(current),'fullyPassing':passed,'unrun':len(schedule)-len(current)}
        invocations.extend(read(path) for path in directory.glob('invocation-*.json'))
    all_keys=[r['test'] for r in rows]
    if len(set(all_keys))!=len(all_keys):raise ValueError('Group results overlap; do not double-count')
    categories={}
    for name,count in plan['categoryCounts'].items():
        names=[g for g in plan['groups'] if (g.startswith('encoding-') if name=='encoding' else g==name)]
        run=sum(group_summary[g]['run'] for g in names);passing=sum(group_summary[g]['fullyPassing'] for g in names)
        categories[name]={'scheduled':count['variants'],'run':run,'fullyPassing':passing,'unrun':count['variants']-run,'passingPercentOfRun':round(100*passing/run,3) if run else None}
    passing=sum(g['fullyPassing'] for g in group_summary.values())
    failures=[{'test':r['test'],'status':r['status'],'message':r.get('message'),'evidenceError':evidence_error(r),
               'assertions':[s for s in r.get('subtests',[]) if s['status']!='PASS']} for r in rows
              if not passed_row(r)]
    summary={'schema':1,'updatedUTC':utc(),'binarySHA256':plan['binary']['sha256'],'cohortSHA256':sha(plan_path),
             'scope':plan['scope'],'scheduled':1515,'run':len(rows),'fullyPassing':passing,'unrun':1515-len(rows),
             'passingPercentOfRun':round(100*passing/len(rows),3) if rows else None,
             'verifiedPassingCoveragePercent':round(100*passing/1515,3),
             'harnessStatuses':dict(Counter(r['status'] for r in rows)),
             'assertions':dict(Counter(s['status'] for r in rows for s in r.get('subtests',[]))),
             'categories':categories,'groups':group_summary,'failures':failures,
             'invocationWallSeconds':round(sum(i.get('durationSeconds',0) for i in invocations),3)}
    save(out/'priority-summary.json',summary)
    print(json.dumps({k:summary[k] for k in ['binarySHA256','scheduled','run','fullyPassing','unrun','passingPercentOfRun','categories']}),flush=True)
    return summary
def run_group(args):
    plan,module,environment,runner=validate(args.plan)
    if args.group not in plan['groups']:raise ValueError('Unknown execution group')
    schedule=plan['groups'][args.group];out=args.plan.parent/args.group
    for name in tuple(os.environ):
        if name.startswith('LUMEN_'):del os.environ[name]
    os.environ.update(environment['nativeEnvironment'])
    selected,_=runner.select_schedule(read(plan['inputs']['inventory']['path']),'testharness',[r['test'] for r in schedule])
    if selected!=schedule:raise ValueError('Exact URL prefixes do not reproduce this group')
    options=argparse.Namespace(inventory=Path(plan['inputs']['inventory']['path']),engine='lumen',binary=Path(plan['binary']['path']),
        output=out,types='testharness',prefix=[r['test'] for r in schedule],offset=0,limit=None,max_seconds=args.max_seconds,
        server_config=Path(plan['inputs']['server_config']['path']),fixture_manifest=Path(plan['inputs']['fixture_readiness']['path']),
        strict_infrastructure=False,timeout=15,long_timeout=65,origin=plan['origins']['http'],secure_origin=plan['origins']['https'],
        h2_origin=plan['origins']['h2'],chromium_arg=environment['chromiumArguments'],chromium_headful=True,resume=(out/'run.json').exists())
    began=time.monotonic();receipt={'startedUTC':utc(),'group':args.group,'cohortSHA256':sha(args.plan),'budgetSeconds':args.max_seconds}
    error=None
    try:runner.run_command(options)
    except BaseException as exc:error=exc;receipt['error']=type(exc).__name__+': '+str(exc);receipt['traceback']=traceback.format_exc()
    finally:
        receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3))
        save(out/('invocation-'+str(time.time_ns())+'.json'),receipt)
        report(args.plan)
    if error:raise error
def original(args):
    plan,module,environment,runner=validate(args.plan);out=args.plan.parent
    kit=ROOT/'output/session-20261029/native-qa-kit-v1/source'
    label='task102900614-'+plan['binary']['sha256'][:16];copy=kit/'build'/('lumen-'+label)
    if copy.exists():
        if sha(copy)!=plan['binary']['sha256']:raise ValueError('QA artifact copy changed')
    else:shutil.copy2(plan['binary']['path'],copy);copy.chmod(0o555)
    native_out='output/native-wpt-'+label
    if (kit/native_out).exists():raise ValueError('An original gate already exists for this artifact; inspect its receipt')
    started=time.monotonic();command=[sys.executable,'tools/apple-session-gpt6as/probe_native_wpt.py','--binary',str(copy.relative_to(kit)),
        '--expected-sha256',plan['binary']['sha256'],'--output',native_out,'--seconds-per-test','25']
    receipt={'startedUTC':utc(),'binarySHA256':plan['binary']['sha256'],'command':command,'passed':False}
    with (out/'original-six.log').open('w') as log:run=subprocess.run(command,cwd=kit,stdout=log,stderr=subprocess.STDOUT,timeout=240)
    path=kit/native_out/'wpt-report.json';rows=read(path).get('results',[])
    counts=Counter(s['status'] for r in rows for s in r.get('subtests',[]))
    full=sum(r.get('status') in ('OK','PASS') and bool(r.get('subtests')) and all(s['status']=='PASS' for s in r['subtests']) for r in rows)
    receipt.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-started,3),exitCode=run.returncode,
        report=str(path),reportSHA256=sha(path),files=len(rows),fullyPassing=full,assertions=dict(counts),
        passed=run.returncode==0 and len(rows)==6 and full==6 and counts==Counter(PASS=1850))
    save(out/'original-six-receipt.json',receipt);print(json.dumps(receipt),flush=True)
    if not receipt['passed']:raise SystemExit(1)

parser=argparse.ArgumentParser();sub=parser.add_subparsers(dest='action',required=True)
p=sub.add_parser('prepare');p.add_argument('--binary',type=Path,required=True);p.add_argument('--sha256',required=True);p.add_argument('--output',type=Path,required=True)
for name in ['run','report','original']:
    p=sub.add_parser(name);p.add_argument('--plan',type=Path,required=True)
    if name=='run':p.add_argument('--group',required=True);p.add_argument('--max-seconds',type=float,default=180)
args=parser.parse_args()
if args.action=='prepare':prepare(args)
elif args.action=='run':run_group(args)
elif args.action=='report':report(args.plan)
else:original(args)
