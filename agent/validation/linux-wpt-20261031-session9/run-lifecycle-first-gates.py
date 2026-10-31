#!/usr/bin/env python3
"""Two owned lanes: unchanged one-file WPT plus unchanged observational matrix, strict separate audits."""
from pathlib import Path
import argparse,collections,datetime,hashlib,json,re,subprocess,sys
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');BASE=ROOT/'agent/validation/linux-wpt-20261031-session9';QA=BASE/'qa-document-lifecycle';OPS=ROOT/'agent/ops/linux-wpt-20261031-session9'
CONTROLLER_SHA='9cd4091e07f5eced384ec7d35d5ca646784cb3b11d28d660d2f84286506ae4a2'
AUDITOR_SHA='9c1a3969d72cee29ed9b5119c1d3d3310ed5b8a9428258800b1890494c16a1dd'
URL='/html/syntax/parsing/the-end.html'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def require(v,msg):
 if not v:raise ValueError(msg)
def load(p):return json.loads(Path(p).read_text())
def row(path):
 rows=[json.loads(x) for x in Path(path).read_text().splitlines() if x];selected=[r for r in rows if r['url']==URL];require(len(selected)==1,'Missing/duplicate lifecycle WPT file');return selected[0]
def identities(r):
 seen=collections.Counter();out={}
 for item in r['subtests']:
  k=(r['url'],item[0],seen[item[0]]);seen[item[0]]+=1;out[k]=item[1]
 return out
ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();require(Path.cwd().resolve()==ROOT,'Project cwd required')
out=BASE/f'candidate{a.iteration}-lifecycle-first-execution.json';require(not out.exists(),'Fresh output required')
require(sha(QA/'run-matrix-candidate-v5.py')==CONTROLLER_SHA and sha(OPS/'audit-document-lifecycle.py')==AUDITOR_SHA,'Reviewed controller/auditor changed')
require(sha(BASE/'qa-document-lifecycle-upstream/manifest.json')=='471d089dbcb8fb9c3212ffdd3b36bb7f94524ebafa68fb27b1caf63229251db0','Whole-file manifest changed')
require(sha(BASE/'qa-document-lifecycle-upstream/urls.json')=='35a0247479be7218f5ec7c0148d1385ec0fa0aadd4ce6816f3a4df20e7fe2837','Whole-file selection changed')
commands=[['python3',str(BASE/'run-cohort-v3.py'),'--scope','qa-document-lifecycle-upstream','--kind','lumen','--type','assertions','--iteration',str(a.iteration)],['python3',str(QA/'run-matrix-candidate-v5.py'),'--kind','lumen','--iteration',str(a.iteration)]]
started=now();jobs=[]
for i,cmd in enumerate(commands):
 log=BASE/f'candidate{a.iteration}-lifecycle-first-{i}.log';stream=log.open('x');p=subprocess.Popen(cmd,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT);jobs.append((p,stream,cmd,str(log)))
execution=[]
for p,stream,cmd,log in jobs:
 rc=p.wait();stream.close();execution.append({'command':cmd,'actual_returncode':rc,'log':log})
audit_path=BASE/f'candidate{a.iteration}-document-lifecycle-contract-audit.json';auditcmd=['python3',str(OPS/'audit-document-lifecycle.py'),'--candidate',str(a.iteration),'--run',str(QA/f'jsrun{a.iteration}-v4'),'--controller-sha256',CONTROLLER_SHA,'--output',str(audit_path),'--check-current-source']
with (BASE/f'candidate{a.iteration}-lifecycle-contract-audit.log').open('x') as f:audit_rc=subprocess.run(auditcmd,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT).returncode
wpt={};errors=[]
try:
 d=BASE/f'qa-document-lifecycle-upstream/lumen{a.iteration}-assertions';result=d/'native/results.jsonl';current=row(result);curids=identities(current)
 baseline=ROOT/'agent/validation/linux-wpt-20261030-session8/qa-frozen640/lumen231/results.jsonl';reference=ROOT/'agent/validation/linux-wpt-20261030-session7/qa-frozen640/chrome156-normal/results.jsonl'
 require(sha(baseline)=='2f5b8445e81e545006c26db3ee4418537f26feed2b1d1f8bbf5f098315332d38' and sha(reference)=='cb62cc6e1414e04c9303928f42140f8ac939253498549b1d58bc32f33e7dbce8','Preserved baseline/reference changed')
 oldids=identities(row(baseline));refids=identities(row(reference));exact=set(curids)==set(oldids)==set(refids) and len(curids)==4
 require(all(v=='PASS' for v in refids.values()) and row(reference)['status']=='OK','Pinned reference control incomplete')
 events=[json.loads(x) for x in (d/'guard-audit/launches.jsonl').read_text().splitlines() if x];names=['started_at_private_gate','constructor_verified_and_acknowledged','author_url_released','exited'];require([e['event'] for e in events]==names,'Exactly one four-event owned lifecycle required')
 identity_keys=['pid','profile','nonce_sha256','binary','binary_sha256','target_url'];require(all(all(e[k]==events[0][k] for k in identity_keys) for e in events),'Owned lifecycle identity mismatch')
 state=load(ROOT/f'agent/validation/iteration-{a.iteration}/build-state.json');require(events[0]['binary']==str(ROOT/f'build/codex-native/lumen-astra-i{a.iteration}') and events[0]['binary_sha256']==state['binary_sha256'],'Measured immutable binary mismatch')
 require(events[0]['target_url']=='http://web-platform.test:8000'+URL,'Wrong actual author URL')
 require(all(e['author_navigation_gated'] and e['session_restore_disabled'] for e in events),'Owned navigation setup differs')
 require(events[-1]['exit_code']==0 and events[-1]['profile_removed'] is True and not Path(events[-1]['profile']).exists(),'Actual owned exit/profile cleanup failed')
 receipt=load(d/'receipt.json');cleanup=load(d/'cleanup.json');life=receipt['returncode']==0 and receipt['monitor_returncodes']==[0,0] and all(receipt[k] is True for k in ['pins_unchanged','source_unchanged','git_unchanged']) and cleanup.get('runner_exception') is None and cleanup.get('remaining_cleanup_threads')==[] and cleanup.get('sources_unchanged') is True
 wpt={'url':URL,'exact4_baseline_and_reference_identities':exact,'harness_status':current['status'],'assertions':dict(collections.Counter(curids.values())),'prior231_pass_losses':[list(k) for k,v in oldids.items() if v=='PASS' and curids.get(k)!='PASS'],'whole_file_pass':exact and current['status']=='OK' and all(v=='PASS' for v in curids.values()),'lifecycle_and_provenance_valid':life,'actual_owned_exit_code':events[-1]['exit_code'],'profile_removed':events[-1]['profile_removed'],'result_sha256':sha(result),'receipt_sha256':sha(d/'receipt.json'),'guard_audit_sha256':sha(d/'guard-audit/launches.jsonl'),'baseline231_assertions':dict(collections.Counter(oldids.values())),'chromium156_assertions':dict(collections.Counter(refids.values())),'raw_results':current}
except Exception as e:errors.append(type(e).__name__+': '+str(e))
audit=load(audit_path) if audit_path.exists() else {};valid=not any(x['actual_returncode'] for x in execution) and audit_rc==0 and audit.get('evidence_integrity_valid') is True and audit.get('observable_bounded_contract_passed') is True and wpt.get('whole_file_pass') is True and wpt.get('lifecycle_and_provenance_valid') is True and not errors
report={'candidate':a.iteration,'started_utc':started,'finished_utc':now(),'maximum_concurrent_browser_lanes':2,'actual_execution':execution,'contract_audit_actual_returncode':audit_rc,'contract_audit_path':str(audit_path),'contract_audit_sha256':sha(audit_path) if audit_path.exists() else None,'upstream_WPT':wpt,'errors':errors,'qualification_pass':valid,'scope':'Whole unchanged the-end4 plus paired12-case exact observable lifecycle contract; durable finish boundary and sanitizer are separate mandatory gates'};write(out,report);print(json.dumps(report),flush=True);sys.exit(0 if valid else 2)
