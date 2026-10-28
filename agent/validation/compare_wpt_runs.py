#!/usr/bin/env python3
"""Compare exact WPT URL/subtest identities without dropping missing results.

Accepts native JSONL or official wptreport JSON. Duplicate subtest names are
matched by occurrence. Reports lost passes even if Chromium also fails them.
No expected-failure metadata alters pass counts; no subset is full-suite parity.
"""
import argparse,collections,json,pathlib,statistics,time

def read(path):
    p=pathlib.Path(path)
    if p.suffix=='.jsonl': rows=[json.loads(s) for s in p.read_text().splitlines() if s.strip()]
    else:
        raw=json.loads(p.read_text());rows=raw.get('results',[raw]) if isinstance(raw,dict) else raw
    result={};duplicates=[]
    for r in rows:
        url=r.get('test',r.get('url'))
        if url is None:continue
        if url in result:duplicates.append(url)
        tests={};counts=collections.Counter()
        for s in r.get('subtests',[]):
            name,status,message=(s.get('name'),s.get('status'),s.get('message')) if isinstance(s,dict) else (*s[:2],s[2] if len(s)>2 else None)
            key=(name,counts[name]);counts[name]+=1;tests[key]={'status':status,'message':message}
        result[url]={'status':r.get('status'),'message':r.get('message'),'tests':tests,'duration_s':r.get('elapsed_s',r.get('ms',r.get('duration',0))/1000)}
    return result,duplicates

def states(r):return {k:s['status'] for k,s in r['tests'].items()} if r else {}
def passed(r):return {k for k,s in states(r).items() if s=='PASS'}
def fully(r):return bool(r and r['status']=='OK' and all(v=='PASS' for v in states(r).values()))
def identity(u,k):return [u,k[0],k[1]]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--urls',required=True);p.add_argument('--baseline',required=True);p.add_argument('--candidate');p.add_argument('--chromium');p.add_argument('--output',required=True);p.add_argument('--automated-total',type=int,default=71398);a=p.parse_args()
    urls=json.loads(pathlib.Path(a.urls).read_text())
    if len(set(urls))!=len(urls):raise ValueError('Selection contains duplicate URLs')
    baseline,bd=read(a.baseline);candidate,cd=read(a.candidate or a.baseline);chrome,rd=read(a.chromium) if a.chromium else ({},[])
    out=pathlib.Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
    summary=collections.Counter();statuses={x:collections.Counter() for x in ('baseline','candidate','chromium')};detail=[];gained=[];lost=[];harness_losses=[]
    for u in urls:
        b,c,r=baseline.get(u),candidate.get(u),chrome.get(u);bp,cp,rp=passed(b),passed(c),passed(r)
        for name,item in [('baseline',b),('candidate',c),('chromium',r)]:
            statuses[name][item['status'] if item else 'MISSING']+=1
            summary[name+'_observed_urls']+=item is not None
            summary[name+'_observed_subtests']+=len(states(item));summary[name+'_pass_subtests']+=len(passed(item))
            summary[name+'_fully_passing_urls']+=fully(item)
        gains=cp-bp;losses=bp-cp;gained.extend(identity(u,k) for k in sorted(gains));lost.extend(identity(u,k) for k in sorted(losses))
        if b and b['status']=='OK' and (not c or c['status']!='OK'):harness_losses.append([u,b['status'],c['status'] if c else 'MISSING'])
        summary['chromium_passing_subtests']+=len(rp);summary['baseline_matches_chromium_pass']+=len(bp&rp);summary['candidate_matches_chromium_pass']+=len(cp&rp)
        summary['candidate_missing_chromium_assertions']+=len(set(states(r))-set(states(c)))
        summary['candidate_fully_matches_chromium_passing_urls']+=bool(fully(r) and fully(c) and not (rp-cp))
        detail.append({'url':u,'baseline_status':b['status'] if b else 'MISSING','candidate_status':c['status'] if c else 'MISSING','chromium_status':r['status'] if r else 'MISSING','baseline_pass':len(bp),'candidate_pass':len(cp),'chromium_pass':len(rp),'baseline_total':len(states(b)),'candidate_total':len(states(c)),'chromium_total':len(states(r)),'gained':len(gains),'lost':len(losses),'unmatched_chromium_pass':len(rp-cp),'candidate_duration_s':c['duration_s'] if c else None,'candidate_message':c['message'] if c else 'No result','first_failures':[[k[0],v['status'],v['message']] for k,v in (c['tests'].items() if c else []) if v['status']!='PASS'][:3]})
    report={'created_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'selection':a.urls,'selected_urls':len(urls),'inputs':{'baseline':a.baseline,'candidate':a.candidate or a.baseline,'chromium':a.chromium},'summary':dict(summary),'statuses':{k:dict(v) for k,v in statuses.items()},'gained_passes':gained,'lost_passes':lost,'lost_harness_ok':harness_losses,'duplicate_url_records':{'baseline':bd,'candidate':cd,'chromium':rd},'details':detail,'caveats':['This is an exact selected-URL comparison, not full WPT parity.','Missing URL/subtest results are retained and never counted as passes.','Chromium failing or unsupported cases remain visible; not silently skipped.','Tests use custom Lumen driver with incomplete automation and offscreen software rendering.'],'full_automated_corpus':a.automated_total,'fresh_verified_url_coverage_pct':round(100*summary['candidate_fully_passing_urls']/a.automated_total,5)}
    out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'selected':len(urls),'summary':dict(summary),'statuses':report['statuses'],'gained':len(gained),'lost':len(lost),'harness_losses':len(harness_losses),'output':str(out)}))
if __name__=='__main__':main()
