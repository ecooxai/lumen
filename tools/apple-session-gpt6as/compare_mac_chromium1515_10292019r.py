from pathlib import Path
from datetime import datetime,timezone
import hashlib,importlib.util,json,os,tempfile,time
root=Path.cwd().resolve()
if str(root)!='/Users/devin/repos/lumen':raise ValueError('Unexpected canonical workspace')
base=root/'out/10292019r-mac';out=base/'independent-final-chrome1515-004'
if (out/'receipt.json').exists(): raise SystemExit('Existing comparison receipt preserved; use a separately named attempt')
out.mkdir(parents=True,exist_ok=True);started=time.monotonic()
spec=importlib.util.spec_from_file_location('independent_raw',root/'tools/apple-session-gpt6as/audit_wpt_candidate.py');A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)
REFERENCE_SHA256='3480ef53ee582e96fa7ef0a2fc5eb662f136cfa7268fad635712653013bedbb6'
SOURCE='dc7a137d79551df3f5d3dc5cc914d06f0b7be468';BINARY='f906769431c7a25b28ba53a364ffb5027ca0dd04fa0f8accc1161407203d3b95';FROZEN='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe'
digest=lambda v:hashlib.sha256(json.dumps(v,sort_keys=True,separators=(',',':')).encode()).hexdigest()
receipt={'startedUTC':datetime.now(timezone.utc).isoformat(),'acceptedReferenceComparison':False,'scope':'Every1515 frozen priority variant: actual nativeMac004 versus fresh LinuxChrome156.0.8078.47, identical canonical selected metadata and457pinned source/dependency files. Different platform/V8; not full officialWPT or generalChrome parity.','fullOfficialWptPassPercent':None,'scoreAwarded':None}
try:
 auditpath=base/'independent-final-004/receipt.json';final=A.read(auditpath)
 if not final['acceptedEvidence']or not final['noRegression']or not final['fixed279NoRegression']or not final['fullFrozenInventoryExecuted']or final['sourceCommit']!=SOURCE or final['binary']['sha256']!=BINARY:raise ValueError('Exact complete004 independent measurement missing')
 referencepath=root/'out/10292019r-chrome1515-reference/reference.json'
 if A.sha(referencepath)!=REFERENCE_SHA256:raise ValueError('Full reference immutable bytes differ')
 reference=A.read(referencepath);rp=reference['protocol'];chrome=reference['rows'];current=A.load_cohort(base/'wpt-final-004/cohort/cohort.json')
 if not reference['acceptedReference']or reference['fullFrozenVariantCount']!=1515 or reference['all1515ImmutableCaseCount']!=1515 or reference['frozenScheduleSHA256']!=FROZEN:raise ValueError('Complete fresh reference is not qualified')
 if current['summary']!=final['summary']or current['evidence']!=final['evidence']or current['plan']['binary']!=final['binary']:raise ValueError('Current native rows changed since raw audit')
 proofdir=base/'fixture-comparison1515-004';proof=A.read(proofdir/'receipt.json');expected=A.read(proofdir/'expected-reference.json')
 if not proof['accepted']or not proof['selectedScheduleEqual']or proof['sourceMismatches']or proof['errors']or proof['sourceCount']!=457 or proof['selectedSourceCount']!=235 or proof['dependencyCount']!=222:raise ValueError('Full selected dependency proof unqualified')
 if A.sha(proofdir/'expected-reference.json')!=proof['expectedReferenceSHA256']or A.sha(proofdir/'expected-reference.json')!=reference['sourceProofSHA256']or expected!=reference['sourceProof']:raise ValueError('Expected complete source proof differs from fresh reference')
 up=root/'output/wpt-10290244/upstream'
 if any(not(up/n).is_file()or(up/n).is_symlink()or A.sha(up/n)!=h for n,h in expected['sourceSHA256'].items()):raise ValueError('Actual selected source/dependency bytes changed')
 if digest(current['plan']['frozenSchedule'])!=FROZEN or current['plan']['fullFrozenScheduleSHA256']!=FROZEN or current['plan']['groups']!=reference['groups']or current['plan']['groupScheduleSHA256']!=reference['groupScheduleSHA256']:raise ValueError('Complete variant metadata or group schedule differs')
 native=current['rows'];keys=set(A.indexed(current['plan']['frozenSchedule']))
 if len(native)!=1515 or len(chrome)!=1515 or set(A.indexed(native))!=keys or set(A.indexed(chrome))!=keys:raise ValueError('Exact complete1515 result identity required on both browsers')
 protocols={}
 for name,sp in current['signatures'].items():
  for key in ['wpt_revision','reporter_sha256','monitor_sha256','origin','secure_origin','h2_origin','timeout_s','long_timeout_s']:
   if sp.get(key)!=rp.get(key):raise ValueError('Reference protocol differs:'+name+':'+key)
  if sp['runner_sha256']!=rp['collector_sha256']:raise ValueError('Shared collector differs')
  if any(sp['source_fixture'].get(key)!=rp['source_fixture'].get(key)for key in ['wpt_revision','tracked_upstream_sources_clean']):raise ValueError('Pinned source or cleanliness differs')
  if sp['schedule_sha256']!=reference['groupScheduleSHA256'][name]:raise ValueError('Group schedule signature differs:'+name)
  protocols[name]={'nativeManifestSHA256':sp['manifest_sha256'],**current['evidence'][name]}
 comparison=A.audit_rows(chrome,native);ns=A.summarize_rows(native,1515);cs=A.summarize_rows(chrome,1515)
 for key,value in reference['summary'].items():
  if cs.get(key)!=value:raise ValueError('Fresh reference summary differs from its ledger:'+key)
 missing=len(comparison['lostPassingAssertions']);categories={};mapping={'dom':['dom'],'events':['events'],'url':['url'],'encoding':['encoding-streams','encoding-core','encoding-rest'],'headers':['headers'],'microtasks':['microtasks'],'storage':['storage']}
 for category,groups in mapping.items():
  scheduled=[v for group in groups for v in current['plan']['groups'][group]];wanted={A.key(v)for v in scheduled};n=[r for r in native if A.key(r)in wanted];c=[r for r in chrome if A.key(r)in wanted];nmap=A.indexed(n);cmap=A.indexed(c);nsummary=A.summarize_rows(n,len(scheduled));csummary=A.summarize_rows(c,len(scheduled));delta=A.audit_rows(c,n);union=0;candidate_pass=0
  for identity in wanted:
   a=nmap.get(identity);b=cmap.get(identity);am=A.assertions(a);bm=A.assertions(b)
   for name in set(am)|set(bm):
    union+=1;candidate_pass+=A.assertion_status(a,name,am)=='PASS'
  healthy=all(A.qualified(r)and bool(A.assertions(r))for r in n+c)
  categories[category]={'scheduled':len(scheduled),'lumenRecorded':len(n),'lumenFullyPassing':nsummary['fullyPassing'],'chromeRecorded':len(c),'chromeFullyPassing':csummary['fullyPassing'],'lumenSubtestOutcomes':nsummary['assertions'],'chromeSubtestOutcomes':csummary['assertions'],'lumenHarnessStatuses':nsummary['harnessStatuses'],'chromeHarnessStatuses':csummary['harnessStatuses'],'bothComplete':len(n)==len(c)==len(scheduled),'harnessesHealthy':healthy,'referenceStatus':'qualified','chromePassingAssertionsRetained':csummary['qualifiedPassingAssertions']-len(delta['lostPassingAssertions']),'chromePassingAssertionsTotal':csummary['qualifiedPassingAssertions'],'chromePassingAssertionsMissing':len(delta['lostPassingAssertions']),'chromePassRetention':(csummary['qualifiedPassingAssertions']-len(delta['lostPassingAssertions']))/csummary['qualifiedPassingAssertions']if csummary['qualifiedPassingAssertions']else None,'unionAssertionIdentities':union,'candidateQualifiedPassInUnion':candidate_pass,'fixedCategoryWeight':1/7,'proportionalPassRatio':candidate_pass/union if union else None,'eligibleForExpandedScoreReview':healthy and len(n)==len(c)==len(scheduled),'scoreReviewNote':'Existing rubric requires unsupported/incomplete categories remain unearned; no score assigned by this measurement.'}
 receipt.update(acceptedReferenceComparison=True,sourceCommit=SOURCE,nativeBinary=final['binary'],nativeAuditSHA256=A.sha(auditpath),referenceBinary=reference['binary'],referenceVersion=reference['version'],referenceSHA256=REFERENCE_SHA256,native=ns,chrome=cs,chromePassingAssertionsRetained=cs['qualifiedPassingAssertions']-missing,chromePassingAssertionsTotal=cs['qualifiedPassingAssertions'],chromePassingAssertionsMissing=missing,chromePassingVariantsRetained=cs['fullyPassing']-len(comparison['lostFullyPassingVariants']),nativeMissingAssertionsMustNotCountAsPass=True,categories=categories,canonicalFixtureQualification={'completeSelectedMetadataEqual':True,'selectedSourceCount':235,'dependencyCount':222,'actualFiles':457,'wholeGeneratedManifestBytesEqual':False,'scope':'Complete frozen1515 selected metadata and selected dependency closure proved; unrelated/unselected sparse manifest entries may differ.','proofSHA256':A.sha(proofdir/'receipt.json'),'expectedReferenceSHA256':proof['expectedReferenceSHA256']},protocols=protocols,comparison=comparison)
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
receipt['finishedUTC']=datetime.now(timezone.utc).isoformat();receipt['durationSeconds']=round(time.monotonic()-started,3);target=out/'receipt.json';data=(json.dumps(receipt,indent=2)+'\n').encode();fd,name=tempfile.mkstemp(prefix='receipt.',suffix='.tmp',dir=out)
with os.fdopen(fd,'wb')as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
os.replace(name,target)
if A.sha(target)!=hashlib.sha256(data).hexdigest():raise ValueError('Full comparison receipt readback differs')
print(json.dumps({'accepted':receipt['acceptedReferenceComparison'],'nativeFullyPassing':receipt.get('native',{}).get('fullyPassing'),'chromeFullyPassing':receipt.get('chrome',{}).get('fullyPassing'),'recorded':receipt.get('native',{}).get('recorded'),'retainedAssertions':receipt.get('chromePassingAssertionsRetained'),'chromePassingAssertions':receipt.get('chromePassingAssertionsTotal'),'missingAssertions':receipt.get('chromePassingAssertionsMissing'),'retainedVariants':receipt.get('chromePassingVariantsRetained'),'source':SOURCE[:12],'error':receipt.get('error'),'receipt':str(target.relative_to(root))},separators=(',',':')))
