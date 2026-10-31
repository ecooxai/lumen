#!/usr/bin/env python3
"""Audit existing paired state measurements; no execution or conformance-score synthesis."""
import argparse,datetime,hashlib,json,math,pathlib,sys
ROOT=pathlib.Path('/mnt/p3/project/browser/lumen-android/lumen-android')
BASE=ROOT/'agent/validation/linux-wpt-20261031-session9/qa-hidden-scroll-state'
CONTRACT_SHA='f99db574f100b60107c55f62181b72681892ec4767b2e7cb46b73db7f7872d60'
REFERENCE_SHA='4e13f504121ceedec9e65a5056b0f1052551d1e6bb861c428dedf50999aae094'
BASELINE_SHA='182dd27b5e7b4d461dd6215386aa932be23508e07959e22367cb2b5f2a04aeaf'
FIXTURE_SHA='f51b1799025c08d900c8b31393f3606a16b104db00709db76eab4c48856de375'
REFERENCE_BINARY_SHA='1bcad71ad1bff85955db13c6d8dd9f32374934d5a5cb079b526c46fb51f9bd97'
EXCEPTION=('range-shrink-and-regrow','hidden-shrunk')
MISSING=object()
def sha(path):
 with pathlib.Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(path):return json.loads(pathlib.Path(path).read_text())
def require(condition,message):
 if not condition:raise ValueError(message)
def number(value):return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value)
def json_type(value):
 if value is MISSING:return 'missing'
 if value is None:return 'null'
 if isinstance(value,bool):return 'boolean'
 if isinstance(value,(int,float)):return 'number' if math.isfinite(value) else 'nonfinite number'
 if isinstance(value,str):return 'string'
 if isinstance(value,list):return 'array'
 if isinstance(value,dict):return 'object'
 return type(value).__name__
def different(native,reference,path=''):
 nt,rt=json_type(native),json_type(reference)
 if nt==rt=='object':
  out={}
  for key in sorted(set(native)|set(reference)):
   if not path and key=='time':continue
   out.update(different(native.get(key,MISSING),reference.get(key,MISSING),path+'.'+key if path else key))
  return out
 if nt==rt=='array':
  if len(native)!=len(reference):return {path:{'native':native,'reference':reference,'reason':'array length'}}
  out={}
  for i,(n,r) in enumerate(zip(native,reference)):out.update(different(n,r,path+'['+str(i)+']'))
  return out
 if nt==rt and nt!='nonfinite number' and native==reference:return {}
 return {path:{'native':'<missing>' if native is MISSING else native,'reference':'<missing>' if reference is MISSING else reference,'native_type':nt,'reference_type':rt}}
def verified_measurement(directory,kind):
 directory=pathlib.Path(directory);r=read(directory/'receipt.json');observations=read(directory/'observations.json');launch=read(directory/'launch.json')
 for key in ['transport_complete','all7_cases30_snapshots','cleanup_valid','measurement_integrity_valid','pins_unchanged','source_unchanged','git_unchanged']:
  require(r.get(key) is True,'measurement failed '+key)
 require(r['kind']==kind and r['returncode']==0 and not r['timed_out'] and r['monitor_returncodes']==[0,0],'process/monitor failure')
 require(r['records']==observations and len(observations)==30,'receipt and observations differ')
 require(r['fixture_sha256']==FIXTURE_SHA and launch['fixture_sha256']==FIXTURE_SHA,'fixture differs')
 require(launch['measurement_only'] is True and launch['transport_PASS_does_not_imply_behavior_PASS'] is True,'measurement classification changed')
 require(launch['source_commit']==r['source_commit'] and launch['binary_sha256']==r['binary_sha256'],'launch/receipt identity mismatch')
 before=read(directory/'pins-before.json');after=read(directory/'pins-after.json');require(before==after,'recorded pins differ')
 require(before[str(BASE/'hidden-scroll-state-matrix-v3.html')]==FIXTURE_SHA,'fixture pin absent')
 require(before[str(BASE/'run-matrix.py')]==sha(BASE/'run-matrix.py'),'ordinary measurement controller changed')
 build=ROOT/('agent/validation/iteration-'+str(r['workspace_candidate']))
 for name in ['build-state.json','source-manifest-before.json','source-manifest-after.json','build.log']:
  path=build/name; key=str(path)
  if name!='build.log':require(key in before,'archived build metadata pin missing: '+name)
  if key in before:require(sha(path)==before[key],'archived build metadata changed: '+name)
 manifest=read(build/'source-manifest-before.json'); manifest_after=read(build/'source-manifest-after.json'); state=read(build/'build-state.json')
 require(manifest==manifest_after and state['source_manifest_before']==manifest['sha256'] and state['source_manifest_after']==manifest['sha256'],'archived build input manifests differ')
 require(state['head']==r['source_commit'] and state['build_exit_code']==0 and state['startup_exit_code']==0 and state['source_unchanged_during_build'] is True,'archived build receipt mismatch')
 # Archived metadata is verified; current workspace files/default jsrun are deliberately not rehashed.
 require(number(observations[0]['time']) and all(number(x['time']) for x in observations),'invalid measurement timestamps')
 require([x['time'] for x in observations]==sorted(x['time'] for x in observations),'unordered measurement timestamps')
 require(len({(x['id'],x['phase']) for x in observations})==30,'duplicate phase identities')
 if kind=='chromium156':
  require(r['binary_sha256']==REFERENCE_BINARY_SHA,'reference binary differs')
  exit_info=r['exit_evidence'];require(exit_info['adapter_actual_returncode']==0 and exit_info['profile_removed'] is True and exit_info['remaining_owned_identities']==[] and exit_info['fallback_signals']==[] and exit_info['driver_cleanup_error'] is None,'reference owned cleanup failed')
 else:
  require(r['exit_evidence'].get('native_jsrun_actual_returncode')==0,'native actual exit failed')
  binary=ROOT/('build/codex-native/jsrun-astra-i'+str(r['workspace_candidate']))
  require(sha(binary)==r['binary_sha256'] and before[str(binary)]==r['binary_sha256'],'immutable native jsrun differs')
  state=read(ROOT/('agent/validation/iteration-'+str(r['workspace_candidate']))/'build-state.json')
  require(state['head']==r['source_commit'] and state['build_exit_code']==0 and state['startup_exit_code']==0 and state['source_unchanged_during_build'] is True,'native build identity mismatch')
 return r,observations

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--iteration',type=int,required=True);ap.add_argument('--source',required=True);ap.add_argument('--output',type=pathlib.Path,required=True);a=ap.parse_args()
 report={'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'iteration':a.iteration,'source_commit':a.source,'auditor_sha256':sha(__file__),'not_WPT':True,'passed_contract':False,'full_snapshot_equality_ignoring_time':False,'errors':[],'phase_results':[],'resource_scope':'Existing one-second process-tree/cgroup receipts; this matrix is not a performance comparison. Chromium OS exit status remains unexposed by the unchanged public adapter; its owned cleanup evidence is retained.'}
 try:
  contract_path=BASE/'acceptance-contract-v2.json';require(sha(contract_path)==CONTRACT_SHA,'acceptance contract changed');contract=read(contract_path)
  require(contract['version']==2 and contract['fixture_sha256']==FIXTURE_SHA and contract['required_cases']==7 and contract['required_snapshots']==30,'contract scope mismatch')
  require(sha(BASE/'hidden-scroll-state-matrix-v3.html')==FIXTURE_SHA,'fixture changed')
  refpath=ROOT/contract['reference_observations'];oldpath=ROOT/contract['native_baseline_observations']
  require(sha(refpath)==REFERENCE_SHA==contract['reference_sha256'],'reference observations changed')
  require(sha(oldpath)==BASELINE_SHA==contract['native_baseline_sha256'],'baseline observations changed')
  reference_receipt,reference=verified_measurement(refpath.parent,'chromium156');baseline_receipt,baseline=verified_measurement(oldpath.parent,'lumen')
  directory=BASE/('jsrun'+str(a.iteration)+'-v3');native_receipt,native=verified_measurement(directory,'lumen')
  require(native_receipt['workspace_candidate']==a.iteration and native_receipt['source_commit']==a.source,'candidate identity mismatch')
  identities=[(x['id'],x['phase']) for x in reference]
  require([(x['id'],x['phase']) for x in baseline]==identities==[(x['id'],x['phase']) for x in native],'missing/reordered/mutated phase identity')
  require(len({x[0] for x in identities})==7,'case count mismatch')
  exception=contract['only_recorded_exception'];require((exception['case'],exception['phase'])==EXCEPTION and exception['fields']==['leafBounds.width','leafBounds.height'] and exception['reference_pair']==[400,400] and exception['allowed_eager_native_pair']==[150,130],'exception broadened')
  for n,r in zip(native,reference):
   identity=(n['id'],n['phase']);raw=different(n,r);strict=dict(raw);accepted=None
   if identity==EXCEPTION:
    npair=[n['leafBounds']['width'],n['leafBounds']['height']];rpair=[r['leafBounds']['width'],r['leafBounds']['height']]
    require(all(number(v) for v in npair+rpair) and rpair==[400,400] and npair in ([150,130],[400,400]),'hidden-shrunk pair is arbitrary, mixed or nonnumeric')
    if npair==[150,130]:
     strict.pop('leafBounds.width',None);strict.pop('leafBounds.height',None)
     accepted={'native':npair,'reference':rpair,'classification':'Recorded eager retained layout versus Chromium cached hidden layout; no full snapshot equivalence claim.'}
   report['phase_results'].append({'case':n['id'],'phase':n['phase'],'strict_fields_pass':not strict,'raw_differences':raw,'unaccepted_differences':strict,'accepted_recorded_exception':accepted})
  report['passed_contract']=all(x['strict_fields_pass'] for x in report['phase_results']) and len(report['phase_results'])==30
  report['full_snapshot_equality_ignoring_time']=all(not x['raw_differences'] for x in report['phase_results'])
  report['case_results']={case:all(x['strict_fields_pass'] for x in report['phase_results'] if x['case']==case) for case in dict(identities)}
  report['claim']=contract['pass_language'];report['contract_sha256']=CONTRACT_SHA
  report['identities']={'native_jsrun_sha256':native_receipt['binary_sha256'],'reference_chromium_sha256':reference_receipt['binary_sha256'],'fixture_sha256':FIXTURE_SHA}
  report['input_sha256']={str(p):sha(p) for p in [contract_path,refpath,oldpath,directory/'receipt.json',directory/'observations.json',refpath.parent/'receipt.json',oldpath.parent/'receipt.json']}
 except Exception as e:report['errors'].append(type(e).__name__+': '+str(e));report['passed_contract']=False
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
 print(json.dumps({k:report[k] for k in ['iteration','passed_contract','full_snapshot_equality_ignoring_time','errors']}));return 0 if report['passed_contract'] else 1
if __name__=='__main__':sys.exit(main())
