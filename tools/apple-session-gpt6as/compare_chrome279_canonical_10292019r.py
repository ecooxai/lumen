from pathlib import Path
from datetime import datetime,timezone
import hashlib,importlib.util,json,time
root=Path.cwd().resolve()
if str(root)!='/Users/devin/repos/lumen':raise ValueError('Unexpected workspace')
spec=importlib.util.spec_from_file_location('independent_raw',root/'tools/apple-session-gpt6as/audit_wpt_candidate.py');A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)
base=root/'out/10292019r-mac';out=base/'independent-chrome279-002';out.mkdir(parents=True,exist_ok=True);started=time.monotonic()
result={'createdUTC':datetime.now(timezone.utc).isoformat(),'acceptedReferenceComparison':False,'scope':'Exact pinned279 variant/assertion identities, Linux Chrome156.0.8078.47 versus Mac Lumen d5; platform and V8 versions differ, so this is a qualified cross-platform reference comparison, not same-platform Chrome parity.','fullOfficialWptPassPercent':None}
try:
 reference_path=root/'out/10292019r-chrome-reference/chrome279-reference.json'
 if A.sha(reference_path)!='55e2c570757611a9d691ff46cfbfe8268af3ba5dd315dc4df81dff05533656ce':raise ValueError('Transferred reference bytes differ')
 reference=A.read(reference_path);chrome=reference['rows'];assert len(chrome)==279
 native_plan=A.read(base/'wpt-candidate-001/cohort/cohort.json');fixed=A.read(base/'independent-worker-acceptance-001/receipt.json');old=A.read(base/'independent-worker-acceptance-001/wpt-audit.json')
 if not fixed['accepted']or A.sha(native_plan['binary']['path'])!=fixed['binary']['sha256']:raise ValueError('Native accepted artifact changed')
 rp=reference['protocol'];native=[];protocols={}
 proof_dir=base/'fixture-comparison-diagnostic-002';proof=A.read(proof_dir/'receipt.json');expected=A.read(proof_dir/'expected-reference.json')
 if not proof['accepted'] or not proof['canonicalSelectedScheduleEqual'] or proof['sourceMismatches'] or proof['sourceCount']!=136:raise ValueError('Canonical selected fixture proof not qualified')
 if expected['protocol']['wpt_revision']!=rp['wpt_revision']:raise ValueError('Reference pin changed')
 upstream=root/'output/wpt-10290244/upstream'
 if any(A.sha(upstream/name)!=digest for name,digest in expected['sourceSHA256'].items()):raise ValueError('Pinned selected source/helper changed since canonical audit')
 selected=sorted([v for name,values in native_plan['groups'].items() if name!='encoding-rest' for v in values],key=lambda r:(r['type'],r['test']))
 if selected!=expected['schedule']:raise ValueError('Canonical selected schedule metadata changed')
 result['canonicalFixtureQualification']={'selectedVariants':279,'selectedSourceAndHelperHashesVerified':136,'canonicalSelectedScheduleEqual':True,'wholeManifestBytesEqual':False,'wholeManifestDifferenceScope':'Generated sparse-checkout manifest includes different unselected tests/support files; selected full variant metadata and actual pinned source/helper bytes are equal. This qualifies only the selected279 comparison.','manifestTopDifferences':proof['manifestTopDifferences'],'proofSHA256':A.sha(proof_dir/'receipt.json'),'expectedReferenceSHA256':A.sha(proof_dir/'expected-reference.json'),'referenceWholeManifestSHA256':rp['manifest_sha256']}

 for name,schedule in native_plan['groups'].items():
  if name=='encoding-rest':continue
  meta_path,schedule_path,rows_path=A.group_paths(base/'wpt-candidate-001/cohort',name)
  if A.sha(rows_path)!=old['candidate']['evidence'][name]['resultsSHA256']:raise ValueError('Accepted native rows changed: '+name)
  meta=A.read(meta_path);sp=meta['signature'];rows=[json.loads(x)for x in rows_path.read_text().splitlines()if x.strip()]
  A.validate_recorded_count(meta,rows,schedule,name)
  for k in ['wpt_revision','reporter_sha256','monitor_sha256','origin','secure_origin','h2_origin','timeout_s','long_timeout_s']:
   if sp.get(k)!=rp.get(k):raise ValueError('Reference fixture/protocol differs: '+name+':'+k)
  if sp['runner_sha256']!=rp['collector_sha256']:raise ValueError('Shared result collector differs')
  if any(sp['source_fixture'].get(k)!=rp['source_fixture'].get(k) for k in ['wpt_revision','tracked_upstream_sources_clean']):raise ValueError('Pinned source fixture identity/cleanliness differs')
  native.extend(rows);protocols[name]={'nativeWholeManifestSHA256':sp['manifest_sha256'],'resultsSHA256':A.sha(rows_path),'scheduleSHA256':A.sha(schedule_path)}
 if set(A.indexed(native))!=set(A.indexed(chrome)):raise ValueError('Reference selected variant identities differ')
 comparison=A.audit_rows(chrome,native);ns=A.summarize_rows(native,279);cs=A.summarize_rows(chrome,279)
 if(ns['fullyPassing'],cs['fullyPassing'],len(native))!=(248,234,279):raise ValueError('Selected native/reference counts changed')
 missing=comparison['lostPassingAssertions'];retained=cs['qualifiedPassingAssertions']-len(missing)
 result.update(acceptedReferenceComparison=True,sourceCommit=fixed['sourceCommit'],nativeBinary=fixed['binary'],referenceBinary=reference['binary'],referenceVersion=reference['version'],native=ns,chrome=cs,chromePassingAssertionsRetained=retained,chromePassingAssertionsTotal=cs['qualifiedPassingAssertions'],chromePassingAssertionsMissing=len(missing),chromePassingVariantsRetained=cs['fullyPassing']-len(comparison['lostFullyPassingVariants']),nativeMissingAssertionsMustNotCountAsPass=True,referenceSHA256=A.sha(reference_path),protocols=protocols,comparison=comparison)
except Exception as error:result['error']=type(error).__name__+': '+str(error)
result['durationSeconds']=round(time.monotonic()-started,3);target=out/'receipt.json';target.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'accepted':result['acceptedReferenceComparison'],'nativeFullyPassing':result.get('native',{}).get('fullyPassing'),'chromeFullyPassing':result.get('chrome',{}).get('fullyPassing'),'retainedAssertions':result.get('chromePassingAssertionsRetained'),'chromePassingAssertions':result.get('chromePassingAssertionsTotal'),'retainedVariants':result.get('chromePassingVariantsRetained'),'platformQualifier':'Linux Chrome vs Mac Lumen; same pinned279','error':result.get('error'),'receipt':str(target.relative_to(root))},separators=(',',':')))
