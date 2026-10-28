#!/usr/bin/env python3
"""Rerun exactly two fixture-blocked Headers variants without editing raw results."""
import argparse,hashlib,importlib.util,json,os,re,subprocess,sys,time,urllib.parse,urllib.request
from datetime import datetime,timezone
from pathlib import Path
PIN='7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
WRAPPER_SHA='b2395c8e7b00e4081924025d765cdb5386de19cd6eddb0f4178c4a8c6d6e2987'
BINARY_SHA='1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9'
HELPER='fetch/api/cors/resources/not-cors-safelisted.json'
URLS=['/fetch/api/headers/headers-no-cors.any.html','/fetch/api/headers/headers-no-cors.any.worker.html']
def utc():return datetime.now(timezone.utc).isoformat()
def digest(data):return hashlib.sha256(data).hexdigest()
def sha(path):return digest(Path(path).read_bytes())
def save(path,value):
    temp=path.with_suffix('.tmp');temp.write_text(json.dumps(value,indent=2)+'\n');temp.replace(path)
def write_rows(path,rows):path.write_text(''.join(json.dumps(row,ensure_ascii=False,separators=(',',':'))+'\n' for row in rows))
def running_wpt():
    raw=subprocess.check_output(['ps','-A','-o','pid=,command='],text=True,timeout=5)
    found=[]
    for line in raw.splitlines():
        parts=line.strip().split(None,1)
        if len(parts)==2 and 'run_priority_wpt_10290244.py' in parts[1] and re.search(r'(?:^|\s)run(?:\s|$)',parts[1]):found.append(int(parts[0]))
    return found

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plan',type=Path,default=Path('output/wpt-10290244/priority-1b9d-v1/cohort.json'))
    p.add_argument('--output',type=Path,default=Path('output/wpt-10290244/priority-1b9d-v1-headers-supplemental-1'))
    args=p.parse_args();root=Path.cwd().resolve()
    if str(root)!='/Users/devin/repos/lumen':raise RuntimeError('Unexpected repository')
    if running_wpt():raise RuntimeError('WPT category runner is active; launch supplemental only after the native batch')
    wrapper=root/'tools/apple-session-gpt6as/run_priority_wpt_10290244.py'
    if sha(wrapper)!=WRAPPER_SHA:raise RuntimeError('Reviewed wrapper changed')
    spec=importlib.util.spec_from_file_location('lume_priority_supplemental_base',wrapper)
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    plan,categories,environment,runner=base.validate_plan(args.plan)
    if plan['binaries']['lumen']['sha256']!=BINARY_SHA:raise RuntimeError('Wrong immutable Lume artifact')
    inventory=json.loads(Path(plan['inputs']['inventory']['path']).read_text())
    selected,_=runner.select_schedule(inventory,'testharness',URLS)
    if [row['test'] for row in selected]!=URLS or len(categories['headers'])!=52:raise RuntimeError('Supplemental selection differs from exact two frozen variants')
    upstream=Path(inventory['wpt_root']);blob=subprocess.check_output(['git','-C',str(upstream),'show',PIN+':'+HELPER],timeout=30)
    helper_path=upstream/HELPER
    if helper_path.read_bytes()!=blob:raise RuntimeError('Helper differs from pinned upstream bytes')
    value=json.loads(blob)
    if not isinstance(value,list) or len(value)!=11:raise RuntimeError('Pinned helper is not expected11-entry array')
    origin=urllib.parse.urlsplit(plan['origins']['http'])
    endpoint='http://127.0.0.1:'+str(origin.port or 80)+'/'+HELPER
    request=urllib.request.Request(endpoint,headers={'Host':origin.netloc})
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request,timeout=10) as response:
        status=response.status;content_type=response.headers.get('Content-Type');body=response.read(1024*1024)
    if status!=200 or body!=blob:raise RuntimeError('Official fixture HTTP bytes do not match pinned JSON helper')
    original_path=args.plan.parent/'headers/lumen/results.jsonl';original_hash=sha(original_path)
    original=runner.read_jsonl(original_path)
    if len(original)!=52 or len({(row['type'],row['test']) for row in original})!=52:raise RuntimeError('Original Headers schedule is incomplete or duplicated')
    original_by_url={row['test']:row for row in original}
    original_cases=[]
    for url in URLS:
        row=original_by_url[url]
        failed=[s for s in row.get('subtests',[]) if s.get('status')!='PASS']
        if row.get('status')!='OK' or len(failed)!=1 or not failed[0]['name'].startswith('Loading data') or 'concat is not a function' not in (failed[0].get('message') or ''):
            raise RuntimeError('Original row is not the reviewed missing-helper failure: '+url)
        original_cases.append(row)
    args.output.mkdir(parents=True,exist_ok=False)
    write_rows(args.output/'original-two-rows.jsonl',original_cases)
    receipt={'schema':'lume-headers-fixture-supplemental-10290244-v1','startedUTC':utc(),'parentPlan':str(args.plan),'parentPlanSHA256':sha(args.plan),'binarySHA256':BINARY_SHA,'upstreamRevision':PIN,'frozenHeaderVariants':52,'supplementalVariants':2,'urls':URLS,'originalResults':str(original_path),'originalResultsSHA256':original_hash,'helper':{'path':HELPER,'sha256':digest(blob),'httpStatus':status,'contentType':content_type,'httpSHA256':digest(body),'jsonType':'array','entries':len(value)},'policy':'Preserve original52 rows. Replace exactly two URLs only in a separately labeled adjusted view; never add repeated variants or their subtests to the original denominator.','complete':False}
    receipt_path=args.output/'receipt.json';save(receipt_path,receipt)
    for key in tuple(os.environ):
        if key.startswith('LUMEN_'):del os.environ[key]
    os.environ.update(environment['nativeEnvironment'])
    options=argparse.Namespace(inventory=Path(plan['inputs']['inventory']['path']),engine='lumen',binary=Path(plan['binaries']['lumen']['path']),output=args.output/'lumen',types='testharness',prefix=URLS,offset=0,limit=None,max_seconds=60,server_config=Path(plan['inputs']['server_config']['path']),fixture_manifest=Path(plan['inputs']['fixture_readiness']['path']),strict_infrastructure=plan['strict_infrastructure'],timeout=plan['timeout_s'],long_timeout=plan['long_timeout_s'],origin=plan['origins']['http'],secure_origin=plan['origins']['https'],h2_origin=plan['origins']['h2'],chromium_arg=environment['chromiumArguments'],chromium_headful=plan['chromium_headful'],resume=False)
    started=time.monotonic()
    try:
        runner.run_command(options)
        repeated=runner.read_jsonl(args.output/'lumen/results.jsonl')
        if len(repeated)!=2 or [row['test'] for row in repeated]!=URLS:raise RuntimeError('Supplemental did not record both exact variants')
        failures={row['test']:runner.measurement_failure(row) for row in repeated if runner.measurement_failure(row)}
        if failures:raise RuntimeError('Supplemental telemetry/cleanup failure: '+json.dumps(failures))
        if sha(original_path)!=original_hash:raise RuntimeError('Original raw results changed during supplemental run')
        if helper_path.read_bytes()!=blob:raise RuntimeError('Pinned helper changed during supplemental run')
        replacements={row['test']:row for row in repeated}
        adjusted=[replacements.get(row['test'],row) for row in original]
        write_rows(args.output/'adjusted-headers-results.jsonl',adjusted)
        receipt['replacementLedger']=[{'test':url,'originalRowSHA256':digest(json.dumps(original_by_url[url],sort_keys=True).encode()),'supplementalRowSHA256':digest(json.dumps(replacements[url],sort_keys=True).encode()),'reason':'Pinned helper restored after original HTTP404 fixture error'} for url in URLS]
        receipt['rawOriginalSummary']=runner.summarize(original,categories['headers'])
        receipt['supplementalSummary']=runner.summarize(repeated,selected)
        receipt['adjustedSummary']=runner.summarize(adjusted,categories['headers'])
        receipt['allSupplementalAssertionsPass']=all(runner.row_passes(row) for row in repeated)
        receipt['complete']=True
    except BaseException as exc:
        receipt['error']=type(exc).__name__+': '+str(exc);raise
    finally:
        receipt['finishedUTC']=utc();receipt['wallSeconds']=round(time.monotonic()-started,3);save(receipt_path,receipt)
    print(json.dumps({'receipt':str(receipt_path),'complete':receipt['complete'],'allSupplementalAssertionsPass':receipt['allSupplementalAssertionsPass'],'rawOriginalSummary':receipt['rawOriginalSummary'],'supplementalSummary':receipt['supplementalSummary'],'adjustedSummary':receipt['adjustedSummary']}))
    # Completion/provenance determines process success; assertion outcomes stay
    # explicit above and must not be silently converted into a conformance pass.
    return 0
if __name__=='__main__':raise SystemExit(main())
