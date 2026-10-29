from pathlib import Path
from datetime import datetime,timezone
from collections import Counter
import hashlib,json,os,urllib.request
root=Path.cwd().resolve()
if str(root)!='/Users/devin/repos/lumen':raise ValueError('Unexpected workspace')
base=root/'out/10292019r-mac';read=lambda p:json.loads(Path(p).read_text());sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
finalpath=base/'independent-final-003/receipt.json';final=read(finalpath);reference=read(base/'independent-final-chrome279-003/receipt.json')
if not final['acceptedEvidence']or not final['noRegression']or not final['fixed279NoRegression']or not reference['acceptedReferenceComparison']:raise ValueError('Final functional evidence is not independently qualified')
if reference['nativeBinary']!=final['binary']or reference['sourceCommit']!=final['sourceCommit']or reference['nativeAuditSHA256']!=sha(finalpath):raise ValueError('Final reference identity changed')
if sha(final['binary']['path'])!=final['binary']['sha256']:raise ValueError('Final native binary changed')
summary=final['summary'];fixed=final['fixed279'];state=root/'output/session-20261029-cpu30/progress-state';path=state/'status.json';status=read(path)
if status['taskId']!='20261029-cpu30':raise ValueError('Existing transport namespace changed')
backup=base/'status-before-final003-wpt.private.json'
if not backup.exists():backup.write_bytes(path.read_bytes());backup.chmod(0o600)
prior=status.get('priorityObserved',status.get('currentWpt'));history=status.setdefault('acceptedWptHistory',[])
if prior and not any(row.get('binarySha256')==prior.get('binarySha256')for row in history):history.append(prior)
status['humanTaskId']='10.292019r';status['activeTaskId']='10292019r';status['iteration']=13;status['updatedUTC']=datetime.now(timezone.utc).isoformat();status['fullOfficialWptPassPercent']=None
current={**summary,'binarySha256':final['binary']['sha256'],'sourceCommit':final['sourceCommit'],'observedVariantPassPercent':summary['passPercentOfRecorded'],'scope':'Latest independently audited source-bound native Mac frozen priority inventory; full official WPT unknown'}
status['priorityObserved']=current;status['currentWpt']=current
status['fixedComparisonWpt']={**fixed,'binarySha256':final['binary']['sha256'],'sourceCommit':final['sourceCommit'],'baselineFullyPassing':246,'baselineRecorded':279,'intermediateD5FullyPassing':248,'scope':'Same fixed279 selection; overlaps full priority inventory'}
status['currentNativeEvidence']={'sourceCommit':final['sourceCommit'],'binarySHA256':final['binary']['sha256'],'compiledSourceFiles':final['compiledSourceFiles'],'sourceManifestSHA256':final['sourceManifestSHA256'],'independentReceipt':str(finalpath.relative_to(root)),'independentReceiptSHA256':sha(finalpath),'noRegression':final['noRegression'],'ISO2022FullyPassing':final['iso2022']['candidateFullyPassing']}
status['qualifiedChrome279Reference']={k:reference[k]for k in ['native','chrome','chromePassingAssertionsRetained','chromePassingAssertionsTotal','chromePassingAssertionsMissing','chromePassingVariantsRetained','scope']}
reference_text='Qualified selected279: current Mac Lumen '+str(fixed['fullyPassing'])+'/279; Linux Chrome234/279. Retains '+str(reference['chromePassingAssertionsRetained'])+'/'+str(reference['chromePassingAssertionsTotal'])+' Chrome-passing assertion identities, '+str(reference['chromePassingAssertionsMissing'])+' missing/nonpassing; '+str(reference['chromePassingVariantsRetained'])+'/234 Chrome-fully-passing variants retained. Different platform/V8; no general Chrome parity.'
original_path=base/'wpt-final-003/cohort/original-six-receipt.json';original_accepted=False
if original_path.is_file():
 original=read(original_path)
 if original['passed']and original['binarySHA256']==final['binary']['sha256']and sha(original['report'])==original['reportSHA256']:
  rows=read(original['report'])['results'];counts=Counter(x['status']for row in rows for x in row['subtests'])
  if len(rows)==6 and counts==Counter(PASS=1850)and all(row['status']in('OK','PASS')for row in rows):
   original_accepted=True;status['wpt']={'passed':1850,'total':1850,'passPercent':100,'reportedFiles':6,'fullyPassingFiles':6,'binarySha256':final['binary']['sha256'],'nativeSourceCommit':final['sourceCommit'],'scope':'Exact final003 original six-file subset; separate from1515','chromePassed':None,'chromeTotal':None,'referenceStatus':reference_text}
if not original_accepted:
 status.setdefault('wpt',{})['referenceStatus']=reference_text;status['originalSixCurrentArtifactStatus']='Unverified on final003; existing original-six figures remain explicitly bound to earlier source'
status['phase']='Current native Mac source '+final['sourceCommit'][:12]+': '+str(summary['fullyPassing'])+'/'+str(summary['recorded'])+' recorded priority variants fully pass; '+str(summary['unrun'])+'/'+str(summary['scheduled'])+' unrun. '+str(summary['assertions'])+' assertions; '+str(summary['harnessStatuses'])+' harness outcomes. Same fixed279='+str(fixed['fullyPassing'])+'/279 versus246 baseline and248 d5. Original six-file result is separate. No lost prior passing assertions on the unchanged protocol. Full official WPT and shared signed IPA/iPad execution remain unverified. Resource/package acceptance is separately artifact-bound; earlier Mac002 results are historical until final003 qualifies.'
status['sourceCommit']=final['sourceCommit'];status.setdefault('apple',{})['macNative']='Final003 source'+final['sourceCommit'][:12]+' binary'+final['binary']['sha256'][:16]+' has independent priorityWPT; exact-artifact resource/package qualification recorded separately.'
tmp=path.with_suffix('.final003.tmp');tmp.write_text(json.dumps(status,indent=2)+'\n');tmp.chmod(0o600);os.replace(tmp,path)
with urllib.request.urlopen('http://127.0.0.1:18785/api/progress',timeout=5)as response:observed=json.load(response)
if observed['status']['currentWpt']['binarySha256']!=final['binary']['sha256']:raise ValueError('Live final artifact differs')
receipt={'updatedUTC':status['updatedUTC'],'sourceCommit':final['sourceCommit'],'binarySHA256':final['binary']['sha256'],'fullyPassing':summary['fullyPassing'],'recorded':summary['recorded'],'unrun':summary['unrun'],'fixed279FullyPassing':fixed['fullyPassing'],'originalSixCurrentArtifactVerified':original_accepted,'visibleScorecardScore':observed.get('scorecard',{}).get('score'),'sourceArchiveCommit':status.get('sourceArchive',{}).get('sourceCommit'),'statusSHA256':sha(path)};target=base/'delivery-final003-wpt.json';target.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({**receipt,'receipt':str(target.relative_to(root))},separators=(',',':')))
