from pathlib import Path
from datetime import datetime,timezone
import hashlib,importlib.util,json,os,tempfile,time
root=Path.cwd().resolve()
if str(root)!='/Users/devin/repos/lumen':raise ValueError('Unexpected workspace')
base=root/'out/10292019r-mac';out=base/'independent-final-chrome279-003';out.mkdir(parents=True,exist_ok=True);began=time.monotonic()
spec=importlib.util.spec_from_file_location('independent_raw',root/'tools/apple-session-gpt6as/audit_wpt_candidate.py');A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)
receipt={'startedUTC':datetime.now(timezone.utc).isoformat(),'acceptedReferenceComparison':False,'scope':'Selected279 exact pinned metadata/source/helper/collector reference comparison: native Mac b816 Lumen versus Linux Chrome156.0.8078.47. Different platforms/V8 versions; not full official WPT or general Chrome parity.','fullOfficialWptPassPercent':None}
try:
 auditpath=base/'independent-final-003/receipt.json';final=A.read(auditpath)
 if not final['acceptedEvidence']or not final['noRegression']or not final['fixed279NoRegression']:raise ValueError('Final native independent acceptance incomplete')
 referencepath=root/'out/10292019r-chrome-reference/chrome279-reference.json'
 if A.sha(referencepath)!='55e2c570757611a9d691ff46cfbfe8268af3ba5dd315dc4df81dff05533656ce':raise ValueError('Reference artifact changed')
 reference=A.read(referencepath);rp=reference['protocol'];chrome=reference['rows'];current=A.load_cohort(base/'wpt-final-003/cohort/cohort.json')
 if current['summary']!=final['summary']or current['evidence']!=final['evidence']or current['plan']['binary']!=final['binary']:raise ValueError('Final native results changed since independent acceptance')
 proofdir=base/'fixture-comparison-diagnostic-002';proof=A.read(proofdir/'receipt.json');expected=A.read(proofdir/'expected-reference.json');upstream=root/'output/wpt-10290244/upstream'
 if not proof['accepted']or not proof['canonicalSelectedScheduleEqual']or proof['sourceMismatches']or proof['sourceCount']!=136:raise ValueError('Canonical selected fixture proof not qualified')
 if any(A.sha(upstream/name)!=digest for name,digest in expected['sourceSHA256'].items()):raise ValueError('Actual selected source/helper bytes changed')
 schedule=sorted([v for name,values in current['plan']['groups'].items()if name!='encoding-rest'for v in values],key=lambda r:(r['type'],r['test']))
 if schedule!=expected['schedule']:raise ValueError('Complete selected variant metadata differs')
 keys={A.key(r)for r in schedule};native=[r for r in current['rows']if A.key(r)in keys]
 if len(native)!=279 or set(A.indexed(native))!=set(A.indexed(chrome)):raise ValueError('Exact279 rows required')
 protocols={}
 for name,sp in current['signatures'].items():
  if name=='encoding-rest':continue
  for k in ['wpt_revision','reporter_sha256','monitor_sha256','origin','secure_origin','h2_origin','timeout_s','long_timeout_s']:
   if sp.get(k)!=rp.get(k):raise ValueError('Reference protocol differs: '+name+':'+k)
  if sp['runner_sha256']!=rp['collector_sha256']:raise ValueError('Shared collector changed')
  if any(sp['source_fixture'].get(k)!=rp['source_fixture'].get(k)for k in ['wpt_revision','tracked_upstream_sources_clean']):raise ValueError('Pinned source identity/cleanliness differs')
  protocols[name]={'nativeManifestSHA256':sp['manifest_sha256'],**current['evidence'][name]}
 comparison=A.audit_rows(chrome,native);ns=A.summarize_rows(native,279);cs=A.summarize_rows(chrome,279);missing=len(comparison['lostPassingAssertions'])
 receipt.update(acceptedReferenceComparison=True,sourceCommit=final['sourceCommit'],nativeBinary=final['binary'],nativeAuditSHA256=A.sha(auditpath),referenceBinary=reference['binary'],referenceVersion=reference['version'],native=ns,chrome=cs,chromePassingAssertionsRetained=cs['qualifiedPassingAssertions']-missing,chromePassingAssertionsTotal=cs['qualifiedPassingAssertions'],chromePassingAssertionsMissing=missing,chromePassingVariantsRetained=cs['fullyPassing']-len(comparison['lostFullyPassingVariants']),nativeMissingAssertionsMustNotCountAsPass=True,canonicalFixtureQualification={'completeSelectedMetadataEqual':True,'selectedSourceAndHelperCount':136,'wholeGeneratedManifestBytesEqual':False,'scope':'Unselected/support manifest entries differ; no blanket equivalence claim outside selected279','diagnosticSHA256':A.sha(proofdir/'receipt.json'),'referenceSHA256':A.sha(referencepath)},protocols=protocols,comparison=comparison)
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
receipt['finishedUTC']=datetime.now(timezone.utc).isoformat();receipt['durationSeconds']=round(time.monotonic()-began,3);target=out/'receipt.json';data=(json.dumps(receipt,indent=2)+'\n').encode();fd,name=tempfile.mkstemp(dir=out,prefix='receipt.',suffix='.tmp')
with os.fdopen(fd,'wb')as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
os.replace(name,target)
print(json.dumps({'accepted':receipt['acceptedReferenceComparison'],'nativeFullyPassing':receipt.get('native',{}).get('fullyPassing'),'chromeFullyPassing':receipt.get('chrome',{}).get('fullyPassing'),'retainedAssertions':receipt.get('chromePassingAssertionsRetained'),'chromePassingAssertions':receipt.get('chromePassingAssertionsTotal'),'missingAssertions':receipt.get('chromePassingAssertionsMissing'),'retainedVariants':receipt.get('chromePassingVariantsRetained'),'source':receipt.get('sourceCommit'),'error':receipt.get('error'),'receipt':str(target.relative_to(root))},separators=(',',':')))
