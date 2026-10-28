#!/usr/bin/env python3
"""Independently adjudicate fixed native scroll/navigation telemetry and receipts."""
import argparse, datetime, hashlib, importlib.util, json, math, statistics, sys
from pathlib import Path
from urllib.parse import parse_qs, urlsplit
RUNNER_SHA='61f919ee0be6e2a972237d7ed7cf9e71f827ff3d9f6d2a8336cebca9fbd9cfb7'
TELEMETRY_SHA='70a9c367e6b7649e5469167c4431f7dc0cb4ad428a9d89ceb291e7834aaa4ec2'
MIB=1048576
POLICY={'sampling_seconds':1,'max_sampling_gap_seconds':1.5,'initial_settle_seconds':15,'final_settle_seconds':15,'final_idle_seconds_minimum':45,'idle_cpu_p95_ceiling_percent':3,'scroll_cpu_p95_ceiling_percent':10,'settled_rss_growth_ceiling_mib':32,'settled_rss_growth_ceiling_fraction':.20,'initial_rss_window':'Last 5 samples of initial idle phase','final_rss_window':'Last 15 samples of final idle phase','native_live_nodes_growth_allowance':50,'native_live_styles_growth_allowance':50}
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def finite(value): return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value)
def stamp(value): return datetime.datetime.fromisoformat(value)
def percentile(values): return sorted(values)[max(0,math.ceil(len(values)*.95)-1)] if values else None

def adjudicate(run,binary_sha,runner_path,compare_run=None):
    run=Path(run); runner_path=Path(runner_path)
    files={name:run/name for name in ['receipt.json','process-summary.json','process-samples.jsonl']}
    receipt=json.loads(files['receipt.json'].read_text()); summary=json.loads(files['process-summary.json'].read_text())
    rows=[json.loads(line) for line in files['process-samples.jsonl'].read_text().splitlines() if line.strip()]
    scenario=receipt.get('scenario'); assert scenario in ('scroll','navigation'),'Unknown lifecycle scenario'
    result={'scenario':scenario,'source':str(run),'expected_binary_sha256':binary_sha,'binary_sha256':receipt.get('binary_sha256'),'source_sha256':{name:sha(path) for name,path in files.items()},'runner_sha256':sha(runner_path),'started_utc':receipt.get('started_utc'),'finished_utc':receipt.get('finished_utc'),'duration_seconds':receipt.get('duration_seconds'),'policy':POLICY,'issues':[],'phases':{},'receipt_issues':receipt.get('issues'),'scope':'Fixed native Mac process-tree workload; synthetic SDL scrolling or20 actual navigations across21 pages. No IPA/simulator, physical-input, same-page Document GC, or universal resource guarantee.'}
    issues=result['issues']
    def require(ok,description):
        if not ok: issues.append(description)
    if result['runner_sha256'] != RUNNER_SHA:
        raise ValueError('Reviewed lifecycle runner hash mismatch; refusing to import changed code')
    require(receipt.get('binary_sha256')==binary_sha and receipt.get('binary_sha256_after')==binary_sha,'Measured immutable binary identity mismatch')
    require(receipt.get('telemetry_sha256')==TELEMETRY_SHA,'Pinned telemetry source hash mismatch')
    require(receipt.get('policy')==POLICY,'Predeclared resource policy changed')
    require(receipt.get('samples_sha256')==result['source_sha256']['process-samples.jsonl'],'Raw samples hash differs from measurement receipt')
    require(receipt.get('summary_exit_code')==0,'Telemetry summary did not exit successfully')
    require(not summary.get('errors') and not summary.get('sampling_gaps_over_1_5_seconds'),'Telemetry summary contains errors or sampling gaps')
    require(not any(row.get('type')=='error' for row in rows),'Raw telemetry contains collector errors')
    require(receipt.get('natural_app_return_code')==0,'Application did not exit naturally with code0')
    for key in ['app_termination','sampler_termination']:
        record=receipt.get(key) or {}
        require(record.get('return_code')==0 and record.get('forced') is False,key+' is not a clean nonforced exit')
    require((receipt.get('app_termination') or {}).get('pid')==receipt.get('app_pid'),'Application cleanup PID mismatch')
    result['cleanup']={key:receipt.get(key) for key in ['app_pid','natural_app_return_code','app_termination','sampler_termination']}
    sample_rows=[row for row in rows if row.get('type')=='sample']
    groups={}; previous=None; cpu_checks=0; cpu_disagreements=[]; roots=set(); starts=set()
    for row in sample_rows:
        group=row.get('groups',{}).get('lumen',{}); phase=row.get('phase')
        groups.setdefault(phase,[]).append(row)
        if group.get('observed'):
            roots.update(group.get('roots',[]))
            for proc in group.get('processes',[]):
                if proc.get('pid')==receipt.get('app_pid'): starts.add(proc.get('start_time'))
        if previous is not None and group.get('observed') and not group.get('phase_boundary'):
            interval=row.get('interval_seconds'); old_group=previous.get('groups',{}).get('lumen',{})
            old={(p.get('pid'),p.get('start_time')):p for p in old_group.get('processes',[])}
            current=group.get('processes',[])
            if finite(interval) and interval>0 and current and all((p.get('pid'),p.get('start_time')) in old for p in current):
                deltas=[p['cpu_seconds']-old[(p.get('pid'),p.get('start_time'))]['cpu_seconds'] for p in current]
                if all(finite(v) and v>=0 for v in deltas):
                    calculated=100*sum(deltas)/interval; observed=group.get('cpu_percent'); cpu_checks+=1
                    if not finite(observed) or abs(calculated-observed)>.002:
                        cpu_disagreements.append({'utc':row.get('utc'),'calculated':calculated,'recorded':observed})
        previous=row
    result['cumulative_cpu_crosscheck']={'intervals_checked':cpu_checks,'disagreements':cpu_disagreements}
    require(cpu_checks>=10 and not cpu_disagreements,'Interval CPU disagrees with raw cumulative process CPU counters')
    require(roots=={receipt.get('app_pid')} and len(starts)==1 and None not in starts,'Observed process root/start identity is inconsistent')
    wanted={'scrolling':(30,10),'idle:after':(45,3)} if scenario=='scroll' else {'idle:before':(10,3),'navigation':(30,None),'idle:after':(45,3)}
    for phase,(minimum,ceiling) in wanted.items():
        all_rows=groups.get(phase,[]); observations=[r for r in all_rows if r.get('groups',{}).get('lumen',{}).get('observed')]
        usable=[r for r in observations if not r['groups']['lumen'].get('phase_boundary')]
        cpus=[r['groups']['lumen'].get('cpu_percent') for r in usable]; rss=[r['groups']['lumen'].get('rss_bytes') for r in observations]
        times=[stamp(r['utc']) for r in observations]; gaps=[(b-a).total_seconds() for a,b in zip(times,times[1:])]
        elapsed=[r.get('elapsed_seconds') for r in observations]
        valid=bool(times) and all(finite(v) and v>=0 for v in cpus+rss+elapsed) and len(observations)==len(all_rows)
        span=(times[-1]-times[0]).total_seconds() if len(times)>1 else 0
        clock_span=elapsed[-1]-elapsed[0] if len(elapsed)>1 and all(finite(v) for v in elapsed) else 0
        p95=percentile(cpus) if valid and cpus else None
        phase_issues=[]
        if not valid or len(observations)<max(10,minimum) or len(cpus)<10 or min(span,clock_span)<minimum: phase_issues.append('Incomplete observations, CPU intervals, or duration')
        if not gaps or min(gaps)<=0 or max(gaps)>1.5 or any(not finite(r.get('interval_seconds')) or r['interval_seconds']>1.5 for r in observations): phase_issues.append('Invalid or excessive sampling gap')
        if p95 is None or (ceiling is not None and p95>ceiling): phase_issues.append('CPU p95 unverified or above predeclared ceiling')
        receipt_phase=(receipt.get('phase_metrics') or {}).get(phase,{})
        summary_phase=summary.get('groups',{}).get('lumen',{}).get(phase,{})
        if receipt_phase!=summary_phase or p95 is None or round(p95,3)!=receipt_phase.get('cpu_p95_percent'): phase_issues.append('Raw CPU p95 does not agree with both summaries')
        if receipt_phase.get('samples')!=len(all_rows) or receipt_phase.get('observed_samples')!=len(observations): phase_issues.append('Summary sample counts do not match raw data')
        if receipt_phase.get('cpu_status') not in ['within_threshold','measured_no_threshold']: phase_issues.append('Telemetry phase verdict is unverified or failed')
        start_after=(times[0]-stamp(receipt['app_started_utc'])).total_seconds() if times else None
        if phase.startswith('idle:') and (start_after is None or start_after<15): phase_issues.append('Initial15-second settle not satisfied')
        result['phases'][phase]={'observations':len(observations),'cpu_intervals':len(cpus),'duration_seconds':round(span,3),'monotonic_span_seconds':round(clock_span,3),'first_after_launch_seconds':round(start_after,3) if start_after is not None else None,'maximum_gap_seconds':round(max(gaps),3) if gaps else None,'cpu_p95_percent':round(p95,3) if p95 is not None else None,'cpu_mean_percent':round(statistics.mean(cpus),3) if valid and cpus else None,'cpu_peak_percent':max(cpus) if valid and cpus else None,'cpu_ceiling_percent':ceiling,'rss_start_mib':rss[0]/MIB if valid and rss else None,'rss_end_mib':rss[-1]/MIB if valid and rss else None,'rss_peak_mib':max(rss)/MIB if valid and rss else None,'accepted':not phase_issues,'issues':phase_issues}
    # Only the known scrolling CPU failure is separable from the independent
    # final-idle verdict; every other runner issue remains disqualifying.
    known_scroll=[]
    if scenario=='scroll':
        p95=result['phases']['scrolling']['cpu_p95_percent']
        if p95 is not None and p95>10: known_scroll=[f'scrolling: CPU p95 {p95}% exceeds 10%']
    extra=[x for x in receipt.get('issues',[]) if x not in known_scroll]
    require(not extra,'Measurement runner reported other issues: '+repr(extra))
    require(receipt.get('accepted') is (not bool(receipt.get('issues'))),'Measurement receipt accepted flag is inconsistent')
    spec=importlib.util.spec_from_file_location('fixed_native_lifecycle_fixture',runner_path); module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    reports=[r.get('value',{}) for r in receipt.get('reports',[])]; proofs=[]
    selected=[r for r in reports if r.get('kind')==('ready' if scenario=='scroll' else 'loaded')]
    expected_steps=[0] if scenario=='scroll' else list(range(21))
    require([r.get('step') for r in selected]==expected_steps,'Fixture report sequence is incomplete or duplicated')
    for value in selected:
        step=value.get('step'); query=parse_qs(urlsplit(value.get('url','')).query); nonce=query.get('nonce',[])
        exact=False
        if len(nonce)==1:
            regenerated=module.fixture(scenario,step,nonce[0]); exact=hashlib.sha256(regenerated).hexdigest()==receipt.get('fixture_sha256_by_step',{}).get(str(step))
        proofs.append({'step':step,'exact_fixture_sha256_match':exact})
    require(all(p['exact_fixture_sha256_match'] for p in proofs) and len(proofs)==len(expected_steps),'Served fixture hashes differ from the reviewed workload')
    result['fixture_proofs']=proofs
    result['normalized_fixture_sha256']=hashlib.sha256(module.fixture(scenario,0,'REVIEW_NONCE')).hexdigest()
    transitions=receipt.get('phase_transitions',[]); phase_start={r['phase']:r.get('elapsed_seconds') for r in transitions}
    if scenario=='scroll':
        require(receipt.get('input')=={'source':'Existing native SDL wheel hook','parameters':'400,400,-0.08,15000,1875','scheduled_event_count':1875,'scheduled_duration_seconds':30},'Scheduled native SDL input changed')
        require(all(finite(phase_start.get(p)) and abs(phase_start[p]-t)<=.5 for p,t in [('scrolling',15),('settle:after',50),('idle:after',65)]),'Fixed scroll/settle/idle phase boundaries changed')
        completed=[r for r in reports if r.get('kind')=='scroll-complete']
        result['scroll_report']=completed[0] if len(completed)==1 else None
        require(len(completed)==1 and finite(completed[0].get('events')) and completed[0]['events']>=10 and finite(completed[0].get('maxY')) and completed[0]['maxY']>=500,'Native scrolling did not complete with verified movement')
    else:
        require(all(v.get('checks')==80 and v.get('documents')==16 for v in selected),'Navigation clone/Attr checks did not all complete')
        result['page_sequence']=[v.get('step') for v in selected]
        last_loaded=next((r for r in receipt.get('reports',[]) if r.get('value',{}).get('kind')=='loaded' and r['value'].get('step')==20),None)
        idle_start=next((r for r in transitions if r['phase']=='idle:after'),None)
        require(last_loaded is not None and idle_start is not None and (stamp(idle_start['utc'])-stamp(last_loaded['utc'])).total_seconds()>=15,'Final page did not settle for15seconds')
        initial=[r['groups']['lumen']['rss_bytes']/MIB for r in groups.get('idle:before',[]) if r.get('groups',{}).get('lumen',{}).get('observed')]
        final=[r['groups']['lumen']['rss_bytes']/MIB for r in groups.get('idle:after',[]) if r.get('groups',{}).get('lumen',{}).get('observed')]
        memory=None
        if len(initial)>=5 and len(final)>=15:
            before=statistics.median(initial[-5:]); after=statistics.median(final[-15:]); growth=after-before
            memory={'initial_median_mib':before,'final_median_mib':after,'growth_mib':growth,'growth_fraction':growth/before if before>0 else None}
            require(before>0 and growth<=32 and growth<=before*.20,'Settled RSS growth exceeds32MiB or20%')
        else: issues.append('Incomplete fixed RSS comparison windows')
        result['settled_rss_comparison']=memory
        require(memory==receipt.get('settled_rss_comparison'),'Fixed raw RSS medians disagree with receipt')
        counters=receipt.get('native_memory_counters',[])
        before=[c for c in counters if c.get('kind')=='lumen-mem2' and phase_start.get('idle:before',math.inf)<=c['elapsed_seconds']<phase_start.get('navigation',-1)-2]
        after=[c for c in counters if c.get('kind')=='lumen-mem2' and c['elapsed_seconds']>=phase_start.get('idle:after',math.inf)]
        native={'initial':before[-1]['values'],'final':after[-1]['values']} if before and after else None
        result['native_counter_comparison']=native
        require(native is not None and native==receipt.get('native_counter_comparison'),'Native counter windows are absent or inconsistent')
        if native:
            require(all(finite(native['initial'].get(k)) and finite(native['final'].get(k)) and native['final'][k]-native['initial'][k]<=50 for k in ['nodes','styles']) and native['final'].get('workers')==0,'Native live-node/style/worker budget failed')
    if compare_run:
        prior=json.loads((Path(compare_run)/'receipt.json').read_text())
        require(prior.get('scenario')==scenario and prior.get('policy')==POLICY,'Comparison scenario/policy differs')
        if scenario=='scroll': require(prior.get('input')==receipt.get('input'),'Comparison native input differs')
        result['comparison']={'source':str(compare_run),'receipt_sha256':sha(Path(compare_run)/'receipt.json'),'binary_sha256':prior.get('binary_sha256'),'same_policy':prior.get('policy')==receipt.get('policy'),'same_input':prior.get('input')==receipt.get('input')}
    result['idle_accepted']=not issues and result['phases']['idle:after']['accepted']
    result['interaction_accepted']=scenario=='scroll' and not issues and result['phases']['scrolling']['accepted']
    result['soak_accepted']=scenario=='navigation' and not issues and all(p['accepted'] for p in result['phases'].values())
    result['accepted']=result['idle_accepted'] and (result['interaction_accepted'] if scenario=='scroll' else result['soak_accepted'])
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--run',required=True,type=Path);parser.add_argument('--sha256',required=True);parser.add_argument('--runner',required=True,type=Path);parser.add_argument('--output',required=True,type=Path);parser.add_argument('--compare-run',type=Path);args=parser.parse_args()
    try: result=adjudicate(args.run,args.sha256,args.runner,args.compare_run)
    except Exception as exc: result={'accepted':False,'idle_accepted':False,'interaction_accepted':False,'soak_accepted':False,'issues':[type(exc).__name__+': '+str(exc)]}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result.get(k) for k in ['scenario','binary_sha256','accepted','idle_accepted','interaction_accepted','soak_accepted','issues']}))
    return 0 if result['accepted'] else 1
if __name__=='__main__':raise SystemExit(main())
