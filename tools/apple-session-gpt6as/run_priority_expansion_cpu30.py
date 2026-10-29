"""Expand an immutable native cohort to the frozen279 important variants."""
from pathlib import Path
from datetime import datetime,timezone
import argparse,hashlib,json,subprocess,sys,time
p=argparse.ArgumentParser();p.add_argument('--plan',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--budget-seconds',type=int,default=720);args=p.parse_args()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
utc=lambda:datetime.now(timezone.utc).isoformat()
plan=json.loads(args.plan.read_text());artifact=plan['binary']
if sha(artifact['path'])!=artifact['sha256']:raise SystemExit('Immutable native artifact hash mismatch')
groups=['dom','events','url','storage','microtasks','headers']
if sum(len(plan['groups'][g]) for g in groups)+len(plan['groups']['encoding-core'])+len(plan['groups']['encoding-streams'])!=279:raise SystemExit('Frozen important cohort denominator changed')
if not 120<=args.budget_seconds<=720:raise SystemExit('Expansion budget must be120..720seconds')
out=args.output;out.mkdir(parents=True,exist_ok=False);began=time.monotonic();deadline=began+args.budget_seconds
priority=args.plan.parent/'priority-summary.json'
before=json.loads(priority.read_text())
if any(before['groups'][g]['unrun'] for g in ['encoding-core','encoding-streams']):raise SystemExit('Complete unchanged103 core/stream prerequisite required')
(out/'cohort-before.json').write_bytes(priority.read_bytes())
summary={'startedUTC':utc(),'binary':artifact,'cohortPlan':str(args.plan),'cohortSHA256':sha(args.plan),
         'overallBudgetSeconds':args.budget_seconds,'frozenInventoryVariants':1515,'importantVariants':279,'scope':'Expand coverage on the same executable; failures/timeouts/errors and unrun remain visible. Raw103 comparison remains separately preserved.',
         'priorSummary':str(out/'cohort-before.json'),'priorSummarySHA256':sha(out/'cohort-before.json'),'stages':[],'complete':False}
def save():
    temp=out/'summary.json.tmp';temp.write_text(json.dumps(summary,indent=2)+'\n');temp.replace(out/'summary.json')
save()
try:
    for group in groups:
        for attempt in range(2):
            previous=json.loads(priority.read_text())['groups'][group]
            if previous['unrun']==0:break
            remaining=int(deadline-time.monotonic())
            if remaining<105:
                summary['budgetExhausted']=True
                raise RuntimeError('Overall expansion budget reached; all unrun variants remain visible')
            work_budget=min(240,remaining-100);monitor_budget=work_budget+85
            start=time.monotonic();name=group+'-'+str(attempt+1);prefix=out/name
            row={'group':group,'attempt':attempt+1,'startedUTC':utc(),'before':previous}
            summary['stages'].append(row);save()
            command=[sys.executable,'tools/apple-session-gpt6as/run_task_wpt_102900614.py','run','--plan',str(args.plan),'--group',group,'--max-seconds',str(work_budget)]
            run=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(monitor_budget),'--settle','0','--output',str(prefix),'--',*command],capture_output=True,text=True,timeout=monitor_budget+5)
            (out/(name+'-driver.log')).write_text(run.stdout+run.stderr)
            metric=json.loads(prefix.with_suffix('.json').read_text())
            current=json.loads(priority.read_text())['groups'][group]
            row.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-start,3),after=current,
                monitor=str(prefix.with_suffix('.json')),monitorSHA256=sha(prefix.with_suffix('.json')),
                naturalExitCode=metric.get('natural_exit_code'),collectorError=metric.get('collector_error'),endedByMonitor=metric.get('ended_by_monitor'),
                cpuSamples=metric.get('interval_cpu_samples'),cpuPeakPercent=metric.get('interval_cpu_peak_percent'),rssPeakMiB=metric.get('rss_peak_mb'),maximumSamplingGapSeconds=metric.get('max_sampling_gap_s'))
            row['executionCompleted']=run.returncode==0 and row['naturalExitCode']==0 and not row['collectorError'] and not row['endedByMonitor']
            result_path=args.plan.parent/group/'results.jsonl'
            if result_path.exists():
                cases=[json.loads(line) for line in result_path.read_text().splitlines() if line.strip()]
                row['slowestCases']=[{k:r.get(k) for k in ('test','status','duration_ms')} for r in sorted(cases,key=lambda r:r.get('duration_ms',0),reverse=True)[:5]]
            if row['durationSeconds']>300:row['longRunReview']='Measured slowestCases above identify the cost; missing service/shared-worker implementations retain their actual timeout/error outcomes.'
            save()
            if not row['executionCompleted']:raise RuntimeError(group+' execution failed; raw results/logs retained')
            if current['unrun']==0:break
            if current['run']<=previous['run']:raise RuntimeError(group+' made no progress')
        if json.loads(priority.read_text())['groups'][group]['unrun']:raise RuntimeError(group+' retains unrun variants after two bounded invocations')
    summary['cohortSummary']=json.loads(priority.read_text())
    summary['complete']=summary['cohortSummary']['run']==279 and all(summary['cohortSummary']['groups'][g]['unrun']==0 for g in groups)
except Exception as error:
    summary['error']=type(error).__name__+': '+str(error)
    if priority.exists():summary['cohortSummary']=json.loads(priority.read_text())
summary.update(finishedUTC=utc(),durationSeconds=round(time.monotonic()-began,3),binarySHA256After=sha(artifact['path']))
summary['sameArtifact']=summary['binarySHA256After']==artifact['sha256'];save()
print(json.dumps({k:v for k,v in summary.items() if k!='cohortSummary'}),flush=True)
raise SystemExit(0 if summary['complete'] and summary['sameArtifact'] and not summary.get('error') else 1)
