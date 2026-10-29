from pathlib import Path
from collections import Counter
from datetime import datetime,timezone
import hashlib,json,os,re,tempfile
root=Path.cwd().resolve()
if str(root)!='/Users/devin/repos/lumen':raise ValueError('Unexpected workspace')
base=root/'out/10292019r-mac';cp=base/'independent-final-chrome1515-004/receipt.json';ap=base/'independent-final-004/receipt.json';read=lambda p:json.loads(p.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();c=read(cp);a=read(ap)
if not c['acceptedReferenceComparison']or not a['acceptedEvidence']or c['nativeAuditSHA256']!=sha(ap):raise ValueError('Independent comparison identities differ')
nonpasses={r['test']:r for r in a['nonPassingVariants']};lost=c['comparison']['lostPassingAssertions'];classification=Counter();harness=Counter();variants={};unexpected=[]
for item in lost:
 test=item['test'];row=nonpasses.get(test);match=re.search(r'\.(serviceworker|sharedworker)\.',test);context=match.group(1)if match else'other';classification[context]+=1;harness[row['status']if row else'MISSING_NATIVE_ROW']+=1;variants.setdefault(context,set()).add(test)
 if not row or row['status']not in('ERROR','TIMEOUT')or context=='other':unexpected.append(item)
if len(lost)!=c['chromePassingAssertionsMissing']:raise ValueError('Missing assertion total differs')
both=c['chromePassingVariantsRetained'];nv=c['native']['fullyPassing'];cv=c['chrome']['fullyPassing'];n=1515
categories={name:{k:value[k]for k in ['scheduled','lumenRecorded','lumenFullyPassing','chromeRecorded','chromeFullyPassing','lumenSubtestOutcomes','chromeSubtestOutcomes','lumenHarnessStatuses','chromeHarnessStatuses','chromePassingAssertionsRetained','chromePassingAssertionsTotal','chromePassingAssertionsMissing','unionAssertionIdentities','candidateQualifiedPassInUnion','proportionalPassRatio','eligibleForExpandedScoreReview']}for name,value in c['categories'].items()}
result={'schema':1,'createdUTC':datetime.now(timezone.utc).isoformat(),'sourceCommit':c['sourceCommit'],'nativeBinary':c['nativeBinary'],'comparisonSHA256':sha(cp),'nativeAuditSHA256':sha(ap),'allMissingChromePASSInUnsupportedContexts':not unexpected,'missingChromePASS':len(lost),'missingChromePASSByContext':dict(classification),'missingChromePASSByNativeHarness':dict(harness),'affectedVariantsByContext':{k:len(v)for k,v in variants.items()},'unexpectedMissing':unexpected,'variantContingency':{'bothFullyPassing':both,'onlyNativeFullyPassing':nv-both,'onlyChromeFullyPassing':cv-both,'neitherFullyPassing':n-(nv+cv-both)},'categories':categories,'scoreAssigned':None,'scope':'Same complete frozen1515 schedule. All unsupported rows and every missing assertion stay counted. Exact available Chrome156 binary comparison, not full officialWPT or general Chrome parity.'}
out=base/'independent-final-chrome1515-004/classification.json'
if out.exists():raise ValueError('Preserve existing classification receipt')
data=(json.dumps(result,indent=2)+'\n').encode();fd,name=tempfile.mkstemp(prefix='classification.',suffix='.tmp',dir=out.parent)
with os.fdopen(fd,'wb')as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
os.replace(name,out)
if out.read_bytes()!=data:raise ValueError('Classification persistence differs')
print(json.dumps({'allMissingInUnsupported':result['allMissingChromePASSInUnsupportedContexts'],'missingByContext':result['missingChromePASSByContext'],'nativeHarness':result['missingChromePASSByNativeHarness'],'affectedVariants':result['affectedVariantsByContext'],'variantContingency':result['variantContingency'],'eligibleCategories':[k for k,v in categories.items()if v['eligibleForExpandedScoreReview']],'receipt':str(out.relative_to(root))},separators=(',',':')))
