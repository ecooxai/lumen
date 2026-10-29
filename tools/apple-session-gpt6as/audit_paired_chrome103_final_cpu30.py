"""Compute exact named-assertion parity from already measured same103 cases; no browser execution."""
from pathlib import Path
from collections import Counter
from datetime import datetime,timezone
import hashlib,json
OUT=Path('out/lume-20261029-cpu30');NATIVE='3ad884713a4aa896dec1d26e20c5a85af5543499467edf9b7ecf424196c63bce';CHROME='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'
read=lambda p:json.loads(Path(p).read_text());sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def require(ok,message):
 if not ok:raise RuntimeError(message)
chrome=read(OUT/'chrome-control-1/summary.json');native=read(OUT/'compat-default-1/summary.json')
require(chrome['complete'] and chrome['recorded']==103 and chrome['chromeBinarySHA256']==CHROME,'Chrome103 identity differs')
require(native['complete'] and native['sameArtifact'] and native['binarySHA256']==NATIVE and native['cohortSummary']['run']==103,'Native103 identity differs')
summary={'taskId':'20261029-cpu30','createdUTC':datetime.now(timezone.utc).isoformat(),'nativeBinarySHA256':NATIVE,'chromeBinarySHA256':CHROME,'scope':'Named-assertion comparison of the same103 pinned upstream variants. Qualified Chrome has real failures, retained separately. This is not full WPT or an overall engine ranking.','groups':{},'fullFrozenVariants':1515,'unrun':1412,'harnessStatuses':{},'assertionClasses':{},'variantClasses':{},'nativeFailuresOnChromePassingAssertions':[]}
all_assertions=Counter();all_variants=Counter();harness=Counter()
for group,total in [('encoding-core',77),('encoding-streams',26)]:
 npath=OUT/'compat-default-1/cohort'/group/'results.jsonl';cpath=OUT/'chrome-control-1'/group/'results.jsonl';nmeta=read(npath.parent/'run.json');cmeta=read(cpath.parent/'run.json')
 require(sha(cpath)==chrome['groups'][group]['rawResultsSHA256'],'Chrome results changed')
 require(nmeta['signature']['binary_sha256']==NATIVE and cmeta['signature']['binary_sha256']==CHROME,'Recorded artifact identity differs')
 require(nmeta['signature']['schedule_sha256']==cmeta['signature']['schedule_sha256'],'Paired schedule differs')
 nrows=[json.loads(x) for x in npath.read_text().splitlines()];crows=[json.loads(x) for x in cpath.read_text().splitlines()]
 require(len(nrows)==len(crows)==total,'Case count differs');nkeys=[(r['type'],r['test']) for r in nrows];ckeys=[(r['type'],r['test']) for r in crows]
 require(nkeys==ckeys and len(set(nkeys))==total,'Case ordering or duplicate identity differs')
 classes=Counter();variants=Counter();issues=[]
 for nr,cr in zip(nrows,crows):
  require(nr['status']=='OK' and cr['status']=='OK','Non-OK harness must be separately reviewed')
  harness['both_OK']+=1
  for r in [nr,cr]:require(not (r.get('metrics') or {}).get('errors') and not r.get('telemetry_errors') and not (r.get('cleanup') or {}).get('error'),'Invalid raw evidence')
  ns=nr.get('subtests',[]);cs=cr.get('subtests',[]);nmap={r['name']:r for r in ns};cmap={r['name']:r for r in cs}
  require(ns and len(nmap)==len(ns) and len(cmap)==len(cs) and set(nmap)==set(cmap),'Missing, duplicate or unequal assertion names in '+nr['test'])
  nfull=all(s['status']=='PASS' for s in ns);cfull=all(s['status']=='PASS' for s in cs)
  variants[('native_PASS' if nfull else 'native_not_PASS')+'__'+('chrome_PASS' if cfull else 'chrome_not_PASS')]+=1
  for name,nassert in nmap.items():
   ca=cmap[name];key='native_'+nassert['status']+'__chrome_'+ca['status'];classes[key]+=1
   if ca['status']=='PASS' and nassert['status']!='PASS':issues.append({'test':nr['test'],'name':name,'nativeStatus':nassert['status'],'nativeMessage':nassert.get('message')})
 all_assertions.update(classes);all_variants.update(variants);summary['nativeFailuresOnChromePassingAssertions'].extend(issues)
 summary['groups'][group]={'variants':total,'assertionClasses':dict(classes),'variantClasses':dict(variants),'nativeRawSHA256':sha(npath),'chromeRawSHA256':sha(cpath),'scheduleSHA256':nmeta['signature']['schedule_sha256']}
summary['assertionClasses']=dict(all_assertions);summary['variantClasses']=dict(all_variants);summary['harnessStatuses']=dict(harness)
summary['chromePassingAssertions']=sum(v for k,v in all_assertions.items() if k.endswith('__chrome_PASS'));summary['nativePassesAmongChromePassingAssertions']=all_assertions['native_PASS__chrome_PASS']
summary['chromePassingVariants']=sum(v for k,v in all_variants.items() if k.endswith('__chrome_PASS'));summary['nativeFullyPassesAmongChromePassingVariants']=all_variants['native_PASS__chrome_PASS'];summary['complete']=True
path=OUT/'paired-chrome103-default-audit.json';require(not path.exists(),'Preserve previous audit');path.write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
