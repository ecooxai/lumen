#!/usr/bin/env python3
"""Serialize bounded unchanged priority WPT categories on the frozen1b9d artifact."""
import argparse, hashlib, json, os, re, signal, subprocess, sys, time
from datetime import datetime, timezone
from pathlib import Path
RUNNER_SHA='b2395c8e7b00e4081924025d765cdb5386de19cd6eddb0f4178c4a8c6d6e2987'
BINARY_SHA='1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9'
CATEGORIES=[('encoding',180),('dom',20),('events',20),('microtasks',45),('storage',20),('url',60)]
def utc():return datetime.now(timezone.utc).isoformat()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,value):
    pending=path.with_suffix('.tmp');pending.write_text(json.dumps(value,indent=2)+'\n');pending.replace(path)
def active_runners():
    data=subprocess.check_output(['ps','-A','-o','pid=,command='],text=True,timeout=5)
    pids=[]
    for line in data.splitlines():
        pieces=line.strip().split(None,1)
        if len(pieces)==2 and 'run_priority_wpt_10290244.py' in pieces[1] and re.search(r'(?:^|\s)run(?:\s|$)',pieces[1]):
            pids.append(int(pieces[0]))
    return [pid for pid in pids if pid!=os.getpid()]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,default=Path('output/wpt-10290244/priority-1b9d-v1/cohort.json'))
    parser.add_argument('--wait-job',type=Path,default=Path('output/session-10290244/final-headers-wpt-job'))
    parser.add_argument('--wait-timeout',type=float,default=300)
    parser.add_argument('--budget',type=float,default=400)
    parser.add_argument('--output',type=Path,default=Path('output/session-10290244/final-priority-batch-1'))
    args=parser.parse_args();root=Path.cwd().resolve()
    if str(root)!='/Users/devin/repos/lumen':raise RuntimeError('Unexpected repository')
    runner=root/'tools/apple-session-gpt6as/run_priority_wpt_10290244.py'
    if sha(runner)!=RUNNER_SHA:raise RuntimeError('Immutable priority wrapper hash changed')
    plan=json.loads(args.plan.read_text())
    if plan['binaries']['lumen']['sha256']!=BINARY_SHA or sha(plan['binaries']['lumen']['path'])!=BINARY_SHA:raise RuntimeError('Wrong immutable candidate')
    if plan['selected_variants']!=1515 or plan['timeout_s']!=15 or plan['long_timeout_s']!=65:raise RuntimeError('Frozen schedule/timeout changed')
    args.output.mkdir(parents=True,exist_ok=False)
    record={'schema':1,'startedUTC':utc(),'plan':str(args.plan),'planSHA256':sha(args.plan),'binarySHA256':BINARY_SHA,'runnerSHA256':RUNNER_SHA,'budgetAfterWaitSeconds':args.budget,'stages':[],'notes':['Each unchanged runner case records CPU/RSS every1s; no concurrent category runs.','Wall budgets pause the frozen schedule and leave all unrun variants explicit.','No Chrome parity inference is made from this native-only batch.']}
    receipt=args.output/'receipt.json';save(receipt,record)
    began=time.monotonic();quiet=0
    while True:
        result_path=args.wait_job/'result.json'
        result=json.loads(result_path.read_text()) if result_path.is_file() else None
        running=active_runners()
        if result is not None and result.get('exit_code') is not None and not running:
            if result.get('timed_out') or result.get('exit_code')!=0:raise RuntimeError('Previous Headers job ended abnormally; inspect its preserved receipt')
            quiet+=1
            if quiet>=2:break
        else:quiet=0
        if time.monotonic()-began>args.wait_timeout:raise RuntimeError('Previous Headers runner did not clear before wait deadline')
        time.sleep(1)
    record['waitSeconds']=round(time.monotonic()-began,3);record['categoriesStartedUTC']=utc();save(receipt,record)
    deadline=time.monotonic()+args.budget
    child=None
    def terminate_child():
        nonlocal child
        if child is None or child.poll() is not None:return
        try:os.killpg(child.pid,signal.SIGTERM)
        except ProcessLookupError:return
        try:child.wait(timeout=15)
        except subprocess.TimeoutExpired:
            try:os.killpg(child.pid,signal.SIGKILL)
            except ProcessLookupError:pass
            child.wait(timeout=10)
    def report(tag):
        log=args.output/(tag+'-report.log')
        with log.open('w') as stream:
            result=subprocess.run([sys.executable,str(runner),'report','--plan',str(args.plan)],stdout=stream,stderr=subprocess.STDOUT,timeout=30)
        if result.returncode:raise RuntimeError('Provenance report failed: '+str(log))
        comparison=json.loads((args.plan.parent/'priority-comparison.json').read_text())
        compact={k:{'scheduled':v['scheduled_variants'],'native':v['lumen']} for k,v in comparison['categories'].items()}
        save(args.output/(tag+'-matrix.json'),compact)
        return compact
    try:
        record['initialMatrix']=report('initial');save(receipt,record)
        for category,preferred in CATEGORIES:
            remaining=deadline-time.monotonic()
            # Reserve the unchanged65s longest case plus cleanup/report time.
            budget=min(float(preferred),max(0,remaining-80))
            if budget<5:
                record['stages'].append({'category':category,'started':False,'reason':'Batch time budget exhausted; full frozen inventory retained'});save(receipt,record);continue
            if active_runners():raise RuntimeError('Another WPT category began; refusing overlapping browser workloads')
            command=[sys.executable,str(runner),'run','--plan',str(args.plan),'--category',category,'--engine','lumen','--max-seconds',str(round(budget,3))]
            row={'category':category,'started':True,'startedUTC':utc(),'budgetSeconds':budget,'command':command};start=time.monotonic()
            logfile=args.output/(category+'.log');row['log']=str(logfile)
            with logfile.open('w') as stream:
                child=subprocess.Popen(command,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
                try:
                    row['exitCode']=child.wait(timeout=min(budget+100,max(1,deadline-time.monotonic())))
                    row['terminatedForBudget']=False
                except subprocess.TimeoutExpired:
                    row['terminatedForBudget']=True;terminate_child();row['exitCode']=child.returncode
            child=None;row['finishedUTC']=utc();row['wallSeconds']=round(time.monotonic()-start,3)
            row['matrix']=report(category);record['stages'].append(row);save(receipt,record)
            print(json.dumps({'category':category,'exitCode':row['exitCode'],'wallSeconds':row['wallSeconds'],'matrix':row['matrix'][category]}),flush=True)
            if row['exitCode']!=0 or row['terminatedForBudget']:
                record['stoppedForRunnerError']=category;break
    finally:
        terminate_child();record['finishedUTC']=utc();record['wallSeconds']=round(time.monotonic()-began,3);save(receipt,record)
    print(json.dumps({'receipt':str(receipt),'finishedUTC':record['finishedUTC'],'stoppedForRunnerError':record.get('stoppedForRunnerError')}),flush=True)
    return 1 if record.get('stoppedForRunnerError') else 0
if __name__=='__main__':raise SystemExit(main())
