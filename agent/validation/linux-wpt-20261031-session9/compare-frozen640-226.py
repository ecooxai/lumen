#!/usr/bin/env python3
"""Explicit accepted226 comparison, separate from unchanged frozen execution."""
from pathlib import Path
import argparse,collections,datetime,hashlib,json
ROOT=Path("/mnt/p3/project/browser/lumen-android/lumen-android");SESSION=ROOT/"agent/validation/linux-wpt-20261031-session9";BASE=ROOT/"agent/validation/linux-wpt-20261030-session8/qa-frozen640";REF=ROOT/"agent/validation/linux-wpt-20261030-session7/qa-frozen640/chrome156-normal/results.jsonl"
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def read(p):
 result={}
 for line in p.read_text().splitlines():
  row=json.loads(line);assert row['url'] not in result;result[row['url']]=row
 return result

def ids(rows):
 result={}
 for url,row in rows.items():
  seen=collections.Counter()
  for sub in row['subtests']:
   key=(url,sub[0],seen[sub[0]]);seen[sub[0]]+=1;result[key]=sub
 return result

def summary(rows):
 c=collections.Counter(s[1] for r in rows.values() for s in r['subtests'])
 return {'files':len(rows),'fully_passing_files':sum(r['status']=='OK' and bool(r['subtests']) and all(s[1]=='PASS' for s in r['subtests']) for r in rows.values()),'assertions':dict(c),'total_assertions':sum(c.values()),'harness_statuses':dict(collections.Counter(r['status'] for r in rows.values()))}

def full(row):return row['status']=='OK' and bool(row['subtests']) and all(s[1]=='PASS' for s in row['subtests'])

ap=argparse.ArgumentParser();ap.add_argument('--candidate',type=int,required=True);a=ap.parse_args();pins=json.loads((SESSION/'frozen640-comparison-pins.json').read_text());bp=BASE/'lumen226/results.jsonl';cp=BASE/f'lumen{a.candidate}/results.jsonl';assert sha(BASE/'manifest.json')==pins['manifest_sha256'];assert sha(bp)==pins['baseline226_results_sha256'];assert sha(REF)==pins['chromium156_results_sha256'];current,baseline,reference=read(cp),read(bp),read(REF);urls=json.loads((BASE/'urls.json').read_text());assert len(urls)==640 and set(current)==set(baseline)==set(reference)==set(urls);ci,bi,ri=ids(current),ids(baseline),ids(reference);assert summary(baseline)['fully_passing_files']==411 and summary(baseline)['assertions']['PASS']==9162 and summary(baseline)['total_assertions']==10540;assert summary(reference)['assertions']['PASS']==9551 and summary(reference)['total_assertions']==10552
common=ci.keys()&bi.keys();losses=[{'identity':k,'before':bi[k],'after':ci.get(k)} for k in sorted(bi) if bi[k][1]=='PASS' and (k not in ci or ci[k][1]!='PASS')];gains=[{'identity':k,'before':bi[k],'after':ci[k]} for k in sorted(common) if bi[k][1]!='PASS' and ci[k][1]=='PASS'];harness=[{'url':u,'before':baseline[u]['status'],'after':current[u]['status']} for u in urls if baseline[u]['status']!=current[u]['status']];shared=ci.keys()&ri.keys();strict=[{'identity':k,'native':ci[k],'chromium':ri[k]} for k in sorted(shared) if ri[k][1]=='PASS' and ci[k][1]!='PASS'];native_adv=[{'identity':k,'native':ci[k],'chromium':ri[k]} for k in sorted(shared) if ci[k][1]=='PASS' and ri[k][1]!='PASS'];audit={'identity_definition':'Exact URL, assertion name, occurrence index; missing priorPASS remains a loss','native_prior_pass_losses':losses,'native_gains':gains,'native_missing_identities':[{'identity':k,'before':bi[k]} for k in sorted(bi.keys()-ci.keys())],'native_added_identities':[{'identity':k,'after':ci[k]} for k in sorted(ci.keys()-bi.keys())],'harness_changes':harness,'reference_shared_pass_native_nonpass':strict,'shared_native_pass_reference_nonpass':native_adv,'reference_only':[{'identity':k,'reference':ri[k]} for k in sorted(ri.keys()-ci.keys())],'native_only':[{'identity':k,'native':ci[k]} for k in sorted(ci.keys()-ri.keys())]};apath=SESSION/f'candidate{a.candidate}-frozen640-identity-audit.json';assert not apath.exists();apath.write_text(json.dumps(audit,indent=2)+'\n')
r={'candidate':a.candidate,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'Unchanged non-URL640 assertion-file sample, not full WPT','pins':pins,'native':summary(current),'baseline226':summary(baseline),'chromium156':summary(reference),'native_results_sha256':sha(cp),'auditor_sha256':sha(Path(__file__)),'identity_audit_sha256':sha(apath),'vs226':{'gains':len(gains),'prior_pass_losses':len(losses),'missing_identities':len(bi.keys()-ci.keys()),'added_identities':len(ci.keys()-bi.keys()),'whole_file_gains':[u for u in sorted(urls) if full(current[u]) and not full(baseline[u])],'whole_file_losses':[u for u in sorted(urls) if full(baseline[u]) and not full(current[u])],'harness_changes':harness},'reference_identity_comparison':{'shared':len(shared),'shared_reference_pass_native_nonpass':len(strict),'shared_native_pass_reference_nonpass':len(native_adv),'reference_only':len(ri.keys()-ci.keys()),'native_only':len(ci.keys()-ri.keys())},'categories':{k:summary({u:row for u,row in current.items() if u.lstrip('/').split('/')[0]==k}) for k in sorted({u.lstrip('/').split('/')[0] for u in urls})},'qualification':'Actual controller/guard exits, cleanup, resources and source pins remain separately required; no overall98 readiness claim'};r['preserves226']=not losses and not r['vs226']['whole_file_losses'] and not any(h['before']=='OK' and h['after']!='OK' for h in harness);p=SESSION/f'candidate{a.candidate}-frozen640-comparison.json';assert not p.exists();p.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:r[k] for k in ['candidate','native','baseline226','chromium156','vs226','reference_identity_comparison','preserves226']}))
