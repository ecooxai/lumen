#!/usr/bin/env python3
"""Exact whole-file API comparison, without expectation changes."""
import argparse,collections,datetime,hashlib,json,pathlib
ROOT=pathlib.Path("/mnt/p3/project/browser/lumen-android/lumen-android");BASE=ROOT/"agent/validation/linux-wpt-20261031-session9"
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def rows(p):
 r=[json.loads(s) for s in p.read_text().splitlines() if s];assert len({x['url'] for x in r})==len(r);return {x['url']:x for x in r}

def ids(rs):
 d={}
 for u,r in rs.items():
  c=collections.Counter()
  for x in r['subtests']:
   k=(u,x[0],c[x[0]]);c[x[0]]+=1;d[k]=x
 return d

def full(r):return r['status']=='OK' and bool(r['subtests']) and all(x[1]=='PASS' for x in r['subtests'])

def summary(rs):
 c=collections.Counter(x[1] for r in rs.values() for x in r.get('subtests',[]))
 return {'files':len(rs),'fully_passing_files':sum(full(r) for r in rs.values()),'assertions':dict(c),'total_assertions':sum(c.values()),'harness_statuses':dict(collections.Counter(r['status'] for r in rs.values()))}

def lifecycle(d,expected,mode,all_pass=False):
 p=d/'guard-audit/launches.jsonl';events=[json.loads(s) for s in p.read_text().splitlines() if s];names=['started_at_private_gate','constructor_verified_and_acknowledged','author_url_released','exited'];sets={n:{e['pid'] for e in events if e['event']==n} for n in names};counts=collections.Counter(e['event'] for e in events);exits=[e for e in events if e['event']=='exited'];r=json.loads((d/'receipt.json').read_text());cleanup=json.loads((d/'cleanup.json').read_text())
 rc=r['returncode'];exc=cleanup.get('runner_exception')
 if mode=='assertions':exit_valid=rc==0 and exc is None
 elif all_pass:exit_valid=rc==0 and exc in (None,'SystemExit(0)')
 else:exit_valid=rc==1 and exc=='SystemExit(1)'
 ok=exit_valid and all(counts[n]==expected and sets[n]==sets['exited'] for n in names) and all(e['exit_code']==0 and e['profile_removed'] for e in exits) and cleanup.get('remaining_cleanup_threads')==[] and cleanup.get('sources_unchanged') and r['pins_unchanged'] and r['source_unchanged'] and r['git_unchanged'] and all(c==0 for c in r['monitor_returncodes'])
 return {'runner_exit_valid':exit_valid,'inner_returncode':rc,'guard_counts':dict(counts),'actual_exit_codes':dict(collections.Counter(e['exit_code'] for e in exits)),'all_profiles_removed':all(e['profile_removed'] for e in exits),'cleanup_threads_remaining':cleanup.get('remaining_cleanup_threads'),'runner_exception':cleanup.get('runner_exception'),'pins_unchanged':r['pins_unchanged'],'source_unchanged':r['source_unchanged'],'git_unchanged':r['git_unchanged'],'monitor_returncodes':r['monitor_returncodes'],'lifecycle_and_provenance_valid':bool(ok),'receipt_sha256':sha(d/'receipt.json'),'audit_sha256':sha(p)}

ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();base=BASE/'qa-hidden-observer-visibility';baseline=json.loads((BASE/'hidden-api-baseline-summary.json').read_text());assert sha(base/'manifest.json')==baseline['manifest_sha256'];expected=set(json.loads((base/'urls.json').read_text()));cd=base/f'lumen{a.iteration}-assertions';cp=cd/'native/results.jsonl';cr=rows(cp);assert set(cr)==expected;ci=ids(cr)
out={'candidate':a.iteration,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'manifest_sha256':baseline['manifest_sha256'],'scope':'All three unchanged upstream files and all23 registered assertions; auto-related residuals remain visible','current':summary(cr),'comparisons':{},'result_sha256':sha(cp),'auditor_sha256':sha(pathlib.Path(__file__))}
for name,b in baseline['results'].items():
 bp=ROOT/b['result_path'];assert sha(bp)==b['result_sha256'];br=rows(bp);assert set(br)==expected;bi=ids(br);common=bi.keys()&ci.keys();loss=[{'identity':k,'before':bi[k],'after':ci.get(k)} for k in sorted(bi) if bi[k][1]=='PASS' and (k not in ci or ci[k][1]!='PASS')]
 out['comparisons'][name]={'baseline':summary(br),'gains':[{'identity':k,'before':bi[k],'after':ci[k]} for k in sorted(common) if bi[k][1]!='PASS' and ci[k][1]=='PASS'],'prior_pass_losses':loss,'missing_identities':[list(k) for k in sorted(bi.keys()-ci.keys())],'added_identities':[list(k) for k in sorted(ci.keys()-bi.keys())],'prior_whole_file_losses':[u for u in sorted(expected) if full(br[u]) and not full(cr[u])],'harness_changes':[{'url':u,'before':br[u]['status'],'after':cr[u]['status']} for u in sorted(expected) if br[u]['status']!=cr[u]['status']]}
 if name=='chrome156':assert summary(br)['assertions']=={'PASS':23}
out['remaining_failures']=[{'identity':k,'result':ci[k]} for k in sorted(ci) if ci[k][1]!='PASS'];out['lifecycle']=lifecycle(cd,len(expected),'assertions');ref=out['comparisons']['chrome156'];out['exact_reference_identities']=not ref['missing_identities'] and not ref['added_identities'];out['preservation_valid']=all(not b['prior_pass_losses'] and not b['missing_identities'] and not b['prior_whole_file_losses'] and not any(h['before']=='OK' and h['after']!='OK' for h in b['harness_changes']) for n,b in out['comparisons'].items() if n.startswith('lumen'));out['all_three_files_complete']=out['exact_reference_identities'] and out['current']['assertions']=={'PASS':23} and out['current']['harness_statuses']=={'OK':3};p=BASE/f'candidate{a.iteration}-hidden-api-audit.json';assert not p.exists();p.write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({'path':str(p),'current':out['current'],'preservation_valid':out['preservation_valid'],'exact_reference_identities':out['exact_reference_identities'],'lifecycle_valid':out['lifecycle']['lifecycle_and_provenance_valid'],'all_three_files_complete':out['all_three_files_complete'],'remaining_failures':out['remaining_failures']}))
