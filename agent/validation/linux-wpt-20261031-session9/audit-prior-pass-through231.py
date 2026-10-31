#!/usr/bin/env python3
"""Ordinary read-only preservation audit; no test, runner, deadline or expectation changes."""
from pathlib import Path
import argparse,collections,datetime,hashlib,json,sys
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android')
S9=ROOT/'agent/validation/linux-wpt-20261031-session9'
BASE=ROOT/'agent/validation/linux-wpt-20261030-session8/qa-frozen640'

def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def require(value,message):
 if not value:raise ValueError(message)
def rows(path):
 result={}
 for line in Path(path).read_text().splitlines():
  if not line.strip():continue
  row=json.loads(line);url=row['url'];require(url not in result,'Duplicate file URL: '+url);result[url]=row
 return result
def identities(data):
 result={}
 for url,row in data.items():
  seen=collections.Counter()
  for sub in row['subtests']:
   name,status=sub[:2];key=(url,name,seen[name]);seen[name]+=1;require(key not in result,'Duplicate identity');result[key]=sub
 return result
def full(row):return row['status']=='OK' and bool(row['subtests']) and all(sub[1]=='PASS' for sub in row['subtests'])
def summary(data):
 c=collections.Counter(sub[1] for row in data.values() for sub in row['subtests'])
 return {'files':len(data),'fully_passing_files':sum(full(row) for row in data.values()),'assertions':dict(c),'total_assertions':sum(c.values()),'harness_statuses':dict(collections.Counter(row['status'] for row in data.values()))}

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--candidate',type=int,required=True);a=ap.parse_args();require(a.candidate>231,'This gate is for a subsequent candidate')
 pinfile=S9/'prior-pass-through231-pins.json';require(sha(pinfile)==PINFILE_SHA256,'Accepted231 baseline pins changed');pins=read(pinfile)
 for rel,h in pins['files'].items():require(sha(ROOT/rel)==h,'Pinned accepted231 evidence changed: '+rel)
 require(sha(BASE/'manifest.json')==pins['manifest_sha256'] and sha(BASE/'two-shard-execution-freeze-v2.json')==pins['schedule_sha256'],'Frozen scope changed')
 before=rows(ROOT/pins['baseline_results_path']);before_ids=identities(before);baseline=summary(before)
 require(baseline==pins['baseline_summary'],'Accepted231 counts changed')
 required={key for key,sub in before_ids.items() if sub[1]=='PASS'};whole={url for url,row in before.items() if full(row)}
 require(len(required)==9246 and len(whole)==416,'Accepted231 required pass scope changed')
 current_path=BASE/f'lumen{a.candidate}/results.jsonl';after=rows(current_path);require(len(after)==640 and after.keys()==before.keys(),'Exact640 URL scope changed');after_ids=identities(after)
 union_path=BASE/f'lumen{a.candidate}/prior-pass-union-audit.json';union=read(union_path)
 require(int(union['candidate'])==a.candidate and union['result_sha256']==sha(current_path),'Original226 union result correspondence failed')
 require(union['required_prior_pass_identities']==9162 and union['required_prior_fully_passing_urls']==411,'Original226 union weakened')
 require(union['preservation_and_lifecycle_accepted'] is True and union['lifecycle_integrity'] is True and union['actual_browser_pids']==640,'Original historical union/lifecycle gate failed')
 binding_path=S9/f'candidate{a.candidate}-frozen640-source-binding.json';binding=read(binding_path)
 require(binding_path.with_suffix('.json.sha256').read_text().split()[0]==sha(binding_path),'Current source-binding sidecar mismatch')
 require(binding['execution_and_source_binding_valid'] is True and binding['actual_outer_runner_returncode']==0,'Current execution/source binding failed')
 require(binding['candidate']==a.candidate and binding['source_commit']==union['source_commit'] and binding['binary_sha256']==union['binary_sha256'],'Current source/binary correspondence failed')
 losses=[{'identity':list(k),'before':before_ids[k],'after':after_ids.get(k)} for k in sorted(required) if k not in after_ids or after_ids[k][1]!='PASS']
 whole_losses=[u for u in sorted(whole) if not full(after[u])]
 harness_changes=[{'url':u,'before':before[u]['status'],'after':after[u]['status']} for u in sorted(before) if before[u]['status']!=after[u]['status']]
 harness_regressions=[x for x in harness_changes if x['after']!='OK']
 shared=before_ids.keys()&after_ids.keys();gains=[{'identity':list(k),'before':before_ids[k],'after':after_ids[k]} for k in sorted(shared) if before_ids[k][1]!='PASS' and after_ids[k][1]=='PASS']
 missing=[{'identity':list(k),'before':before_ids[k]} for k in sorted(before_ids.keys()-after_ids.keys())]
 added=[{'identity':list(k),'after':after_ids[k]} for k in sorted(after_ids.keys()-before_ids.keys())]
 accepted=not losses and not whole_losses and not harness_regressions and not missing
 report={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'candidate':a.candidate,'source_commit':binding['source_commit'],'binary_sha256':binding['binary_sha256'],'scope':'Same frozen640 non-URL files; preserve accepted226 and every newly demonstrated231 pass. Added registration is disclosed as new coverage, not a recovered before failure.','comparison_candidate':231,'baseline231':baseline,'current':summary(after),'required_prior_pass_identities':len(required),'required_prior_fully_passing_urls':len(whole),'prior_pass_losses':losses,'prior_fully_passing_file_losses':whole_losses,'prior_harness_regressions':harness_regressions,'harness_changes':harness_changes,'common_nonpass_to_pass':gains,'missing_identities':missing,'added_identities':added,'whole_file_gains':[u for u in sorted(after) if full(after[u]) and not full(before[u])],'original226_union_preservation_accepted':True,'source_binding_and_lifecycle_accepted':True,'preservation_through231_accepted':accepted,'result_sha256':sha(current_path),'baseline_result_sha256':sha(ROOT/pins['baseline_results_path']),'pins_sha256':sha(pinfile),'original_union_sha256':sha(union_path),'current_source_binding_sha256':sha(binding_path),'auditor_sha256':sha(Path(__file__))}
 out=S9/f'candidate{a.candidate}-prior-pass-through231-audit.json';require(not out.exists(),'Fresh audit output required');out.write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({'candidate':a.candidate,'comparison_candidate':231,'required_passes':len(required),'required_whole_files':len(whole),'prior_pass_losses':len(losses),'whole_file_losses':len(whole_losses),'new_nonOK_harness_results':len(harness_regressions),'gains':len(gains),'missing_identities':len(missing),'added_identities':len(added),'preservation_through231_accepted':accepted,'path':str(out),'sha256':sha(out)}))
 return 0 if accepted else 2

PINFILE_SHA256='93c09f58afbfb44a81b1d204b6f3fd5c63a1e7190b51074823cee4355d2fe406'
if __name__=='__main__':sys.exit(main())
