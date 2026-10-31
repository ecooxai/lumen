#!/usr/bin/env python3
"""Independent read-only outcome/provenance review. This never launches or alters a browser test."""
from pathlib import Path
import argparse,json,hashlib,collections,datetime,subprocess,sys
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=ROOT/'agent/validation/linux-wpt-20261030-session8/qa-frozen640';S9=ROOT/'agent/validation/linux-wpt-20261031-session9'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def lines(p):return [json.loads(x) for x in Path(p).read_text().splitlines() if x.strip()]
def require(v,m):
 if not v:raise ValueError(m)
def index(data):
 out={}
 for r in data:require(r['url'] not in out,'duplicate file URL');out[r['url']]=r
 return out
def identities(data):
 out={}
 for u,r in data.items():
  occurrences=collections.Counter()
  for sub in r['subtests']:
   key=(u,sub[0],occurrences[sub[0]]);occurrences[sub[0]]+=1;require(key not in out,'duplicate identity');out[key]=sub
 return out
def full(r):return r['status']=='OK' and bool(r['subtests']) and all(s[1]=='PASS' for s in r['subtests'])
def counts(data):
 sub=collections.Counter(s[1] for r in data.values() for s in r['subtests'])
 return {'files':len(data),'whole_files_passed':sum(full(r) for r in data.values()),'whole_file_pass_percent':100*sum(full(r) for r in data.values())/len(data),'harness_statuses':dict(collections.Counter(r['status'] for r in data.values())),'assertion_statuses':dict(sub),'assertions_total':sum(sub.values())}
def compare(a,b):
 ai,bi=identities(a),identities(b);shared=ai.keys()&bi.keys()
 return {'shared_identities':len(shared),'candidate_only_identities':[list(k) for k in sorted(ai.keys()-bi.keys())],'other_only_identities':[list(k) for k in sorted(bi.keys()-ai.keys())],'candidate_PASS_other_nonPASS':sum(ai[k][1]=='PASS' and bi[k][1]!='PASS' for k in shared),'other_PASS_candidate_nonPASS':sum(bi[k][1]=='PASS' and ai[k][1]!='PASS' for k in shared),'prior_pass_losses':[{'identity':list(k),'before':bi[k],'after':ai.get(k)} for k in sorted(bi) if bi[k][1]=='PASS' and (k not in ai or ai[k][1]!='PASS')],'prior_whole_file_losses':[u for u,r in b.items() if full(r) and not full(a[u])],'harness_changes':[{'url':u,'before':b[u]['status'],'after':r['status']} for u,r in a.items() if r['status']!=b[u]['status']]}
ap=argparse.ArgumentParser();ap.add_argument('--candidate',type=int,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();require(not a.output.exists(),'fresh output required')
required=BASE/'required-prior-pass-identities-through226.json';require(sha(required)=='bc25a54ee620c02e7d962a2befefd2c7ae7174de044a7cfa7d4a405b755541d4','prior identity pins changed');prior=read(required)
manifest=BASE/'manifest.json';schedule=BASE/'two-shard-execution-freeze-v2.json';require(sha(manifest)=='8b5d0d491ce8b14e6ee8107d1213c10b90127a0193207d06057216bd54f3243f','manifest changed');require(sha(schedule)=='7e17aeaf30a0d05982a5d69a11fe0bd062561fe9375b2fcad72fb1eba41d1c32','schedule changed');plan=read(schedule);urls=read(BASE/'urls.json');require(len(urls)==len(set(urls))==640,'URL scope changed');require([r['url'] for r in read(manifest)['rows']]==urls,'manifest/order differs')
out=BASE/('lumen'+str(a.candidate));result_path=out/'results.jsonl';baseline_path=BASE/'lumen226/results.jsonl';reference_path=ROOT/'agent/validation/linux-wpt-20261030-session7/qa-frozen640/chrome156-normal/results.jsonl'
require(sha(baseline_path)=='ec00b3f4eaba4388f72fe6fb843798342a3fef5f146b1917d7a9d894f55cb9e9','accepted baseline changed');require(sha(reference_path)=='cb62cc6e1414e04c9303928f42140f8ac939253498549b1d58bc32f33e7dbce8','reference changed')
current=index(lines(result_path));baseline=index(lines(baseline_path));reference=index(lines(reference_path));require(current.keys()==baseline.keys()==reference.keys()==set(urls),'exact640 required')
cc,bc,rc=counts(current),counts(baseline),counts(reference);require((bc['whole_files_passed'],bc['assertion_statuses'].get('PASS'),bc['assertions_total'])==(411,9162,10540),'baseline denominator/counts differ');require((rc['whole_files_passed'],rc['assertion_statuses'].get('PASS'),rc['assertions_total'])==(560,9551,10552),'reference denominator/counts differ')
ci=identities(current);required_ids={tuple(r['identity']) for r in prior['pass_identities']};required_files={r['url'] for r in prior['whole_files']};require(len(required_ids)==9162 and len(required_files)==411,'prior scope changed')
losses=[{'identity':list(k),'after':ci.get(k)} for k in sorted(required_ids) if k not in ci or ci[k][1]!='PASS'];wholeloss=[u for u in sorted(required_files) if not full(current[u])];bcmp=compare(current,baseline);rcmp=compare(current,reference)
harness_regressions=[x for x in bcmp['harness_changes'] if x['after']!='OK'];preserved=not losses and not wholeloss and not harness_regressions
controller_path=BASE/('lumen'+str(a.candidate)+'-controller-receipt.json');controller=read(controller_path);cleanup_path=out/'cleanup-aggregate.json';cleanup=read(cleanup_path);binding_path=S9/('candidate'+str(a.candidate)+'-frozen640-source-binding.json');binding=read(binding_path);require(binding_path.with_suffix('.json.sha256').read_text().split()[0]==sha(binding_path),'binding sidecar mismatch')
require(binding['execution_and_source_binding_valid'] and binding['actual_outer_runner_returncode']==0,'source/actual execution binding failed');require(controller['returncode']==0 and controller['complete_sample'] and controller['reported_files']==640 and controller['duplicate_urls']==[],'controller incomplete');require(controller['binary_unchanged'] and controller['wpt_sources_unchanged'] and controller['runners_unchanged'] and controller['cgroup_monitor_returncode']==0 and all(w['returncode']==0 for w in controller['workers']),'controller provenance/monitor failure')
require(controller['actual_workers_per_browser']==2 and controller['deadline_seconds']==15 and controller['schedule_sha256']==sha(schedule) and controller['manifest_sha256']==sha(manifest),'schedule/concurrency changed')
for rel,pin in plan['runner_pins'].items():require(sha(BASE/rel)==pin,'frozen runner/static input changed: '+rel)
require(controller['sources_before']==controller['sources_after'],'WPT source pins differ');wpt=Path('/mnt/p3/project/browser/wpt')
for rel,pin in controller['sources_after'].items():require(sha(wpt/rel)==pin,'upstream source changed: '+rel)
require(cleanup['cleanup_complete'] and cleanup['all_native_exit_codes_zero'] and cleanup['actual_browser_exit_codes']=={'0':640} and not cleanup['remaining_recorded_pids'] and not cleanup['remaining_profiles'],'cleanup failed')
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip();require(head==controller['source_commit']==binding['source_commit'],'source commit differs')
build=ROOT/('agent/validation/iteration-'+str(a.candidate));state=read(build/'build-state.json');source=read(build/'source-manifest-after.json');require(state['head']==head and source==read(build/'source-manifest-before.json'),'immutable build correspondence failed');require(state['build_exit_code']==state['startup_exit_code']==0 and state['source_unchanged_during_build'],'build failed');require(all(sha(ROOT/p)==h for p,h in source['files'].items()),'compiled input changed');binary=ROOT/('build/codex-native/lumen-astra-i'+str(a.candidate));require(sha(binary)==controller['binary_sha256']==binding['binary_sha256']==state['binary_sha256'],'binary identity differs')
allpids=set();alltargets=set();guard_summary=[];evidence_hashes={str(p.relative_to(ROOT)):sha(p) for p in [required,manifest,schedule,result_path,baseline_path,reference_path,controller_path,cleanup_path,binding_path]}
order=['started_at_private_gate','constructor_verified_and_acknowledged','author_url_released','exited']
for i in range(2):
 path=out/('guard-shard'+str(i))/'launches.jsonl';events=lines(path);groups=collections.defaultdict(list)
 for e in events:groups[(e['pid'],e['nonce_sha256'],e['profile'])].append(e)
 require(len(groups)==320 and len(events)==1280,'guard group count differs');targets=set();pids=set()
 for key,ev in groups.items():
  require([e['event'] for e in ev]==order,'guard order/link differs');require(all(e['binary_sha256']==state['binary_sha256'] and e['author_navigation_gated'] and e['session_restore_disabled'] for e in ev),'guard identity/gating differs');require(len({e['target_url'] for e in ev})==len({e['guard_library_sha256'] for e in ev})==len({str(e['guard_library_seals']) for e in ev})==1,'guard target/seal chain differs');require(ev[-1]['exit_code']==0 and ev[-1]['profile_removed'] is True and not Path(key[2]).exists(),'actual exit/profile cleanup differs');targets.add(ev[0]['target_url'].removeprefix('http://web-platform.test:8000'));pids.add(key[0])
 require(targets==set(read(BASE/('urls-shard'+str(i)+'.json'))) and not(allpids&pids) and not(alltargets&targets),'shard target/pid partition differs');allpids|=pids;alltargets|=targets
 cp=out/('cleanup-shard'+str(i)+'.json');c=read(cp);require(c['sources_unchanged'] and c['source_hashes_before']==c['source_hashes_after'] and c['remaining_cleanup_threads']==[] and c['runner_exception'] is None,'actual worker cleanup failed');guard_summary.append({'shard':i,'actual_processes':320,'linked_event_count':1280,'exit0':320,'profiles_removed':320,'actual_worker_exception':None});evidence_hashes[str(path.relative_to(ROOT))]=sha(path);evidence_hashes[str(cp.relative_to(ROOT))]=sha(cp)
require(len(allpids)==len(alltargets)==640,'global lifecycle count differs')
report={'reviewed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'candidate':a.candidate,'source_commit':head,'binary_sha256':state['binary_sha256'],'recorded_input_count':len(source['files']),'scope':'Exact fixed640 non-URL sample, not full WPT; every TIMEOUT/ERROR remains in its file denominator.','candidate_counts':cc,'accepted226_counts':bc,'chromium156_counts':rc,'required_prior_assertion_passes':9162,'required_prior_whole_files':411,'prior_pass_losses':losses,'prior_whole_file_losses':wholeloss,'new_or_changed_nonOK_harness_results':harness_regressions,'comparison_with226':bcmp,'comparison_with_chromium156':rcmp,'source_execution_lifecycle_valid':True,'prior_pass_and_harness_preservation_valid':preserved,'regression_audit_pass':preserved,'guard_lifecycles':guard_summary,'one_second_monitor_configuration':True,'resource_interpretation':'Two native browser lanes and15-second per-file deadlines match the frozen schedule. Differing failure/timeout workloads cannot establish a general speed or memory advantage. Matched control results are separate.','actual_outer_terminal_exit':'Coordinator must separately retain actual terminal completion; inner actual wait returncode0 is verified here.','evidence_sha256':evidence_hashes,'auditor_sha256':sha(Path(__file__))}
a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['candidate','candidate_counts','accepted226_counts','chromium156_counts','prior_pass_losses','prior_whole_file_losses','new_or_changed_nonOK_harness_results','source_execution_lifecycle_valid','regression_audit_pass']}));sys.exit(0 if preserved else 2)
