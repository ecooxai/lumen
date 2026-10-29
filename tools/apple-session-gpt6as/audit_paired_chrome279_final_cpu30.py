"""Audit disjoint same-binary Chrome103+176 against native279; keep missing contexts in denominator."""
from pathlib import Path
from collections import Counter
from datetime import datetime,timezone
import hashlib,json
OUT=Path('out/lume-20261029-cpu30');NATIVE='3ad884713a4aa896dec1d26e20c5a85af5543499467edf9b7ecf424196c63bce';CHROME='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'
read=lambda p:json.loads(Path(p).read_text());sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def require(ok,message):
 if not ok:raise RuntimeError(message)
controls={n:read(OUT/n/'summary.json') for n in ['chrome-control-1','chrome-control-2']};native=read(OUT/'priority-expansion-default-1/summary.json')
require(native['complete'] and native['sameArtifact'] and native['binary']['sha256']==NATIVE and native['cohortSummary']['run']==279,'Native279 identity differs')
require(all(c['complete'] and c['chromeBinarySHA256']==CHROME and c['chromeBinarySHA256After']==CHROME for c in controls.values()),'Qualified controls incomplete or artifact changed')
require(controls['chrome-control-1']['recorded']==103 and controls['chrome-control-2']['recorded']==176,'Control scope differs')
require(set(controls['chrome-control-1']['groups']).isdisjoint(controls['chrome-control-2']['groups']),'Control groups overlap')
summary={'taskId':'20261029-cpu30','createdUTC':datetime.now(timezone.utc).isoformat(),'nativeBinarySHA256':NATIVE,'chromeBinarySHA256':CHROME,'scope':'Same frozen279 variants. QualifiedChrome103+176 are explicitly disjoint executions on one artifact/protocol. Native missing/failed contexts remain in the denominator; no overall engine ranking.','groups':{},'frozenVariants':1515,'recorded':279,'unrun':1236,'assertionClasses':{},'variantClasses':{},'harnessClasses':{},'missingNativeAssertions':[],'nativeFailedAssertionsOnChromePass':[]}
ac=Counter();vc=Counter();hc=Counter();expected_sizes={'encoding-core':77,'encoding-streams':26,'dom':14,'events':16,'url':77,'storage':7,'microtasks':10,'headers':52};seen=set()
for group,total in expected_sizes.items():
 control_name='chrome-control-1' if group.startswith('encoding-') else 'chrome-control-2';control=controls[control_name]
 npath=OUT/'compat-default-1/cohort'/group/'results.jsonl';cpath=OUT/control_name/group/'results.jsonl';nmeta=read(npath.parent/'run.json');cmeta=read(cpath.parent/'run.json')
 require(sha(cpath)==control['groups'][group]['rawResultsSHA256'],'Chrome raw result changed')
 require(nmeta['signature']['binary_sha256']==NATIVE and cmeta['signature']['binary_sha256']==CHROME,'Raw artifact identity differs')
 for key in ['schedule_sha256','wpt_revision','inventory_sha256','source_fixture','server_config_sha256','fixture_manifest_sha256','runner_sha256','reporter_sha256','monitor_sha256','origin','secure_origin','h2_origin','timeout_s','long_timeout_s','strict_infrastructure']:
  require(nmeta['signature'][key]==cmeta['signature'][key],'Paired signature differs: '+key)
 nr=[json.loads(s) for s in npath.read_text().splitlines()];cr=[json.loads(s) for s in cpath.read_text().splitlines()]
 require(len(nr)==len(cr)==total,'Group result count differs');keys=[(r['type'],r['test']) for r in nr]
 require(keys==[(r['type'],r['test']) for r in cr] and len(set(keys))==total and not(set(keys)&seen),'Paired identity/order/duplicate differs');seen.update(keys)
 ag=Counter();vg=Counter();hg=Counter()
 for a,b in zip(nr,cr):
  require(a['url']==b['url'],'Actual document URLs differ')
  for r in [a,b]:require(not(r.get('metrics') or {}).get('errors') and not r.get('telemetry_errors') and not(r.get('cleanup') or {}).get('error'),'Raw measurement invalid')
  require(b['status']=='OK','Chrome control harness not OK')
  hg['native_'+a['status']+'__chrome_'+b['status']]+=1
  aa=a.get('subtests',[]);bb=b.get('subtests',[]);am={r['name']:r for r in aa};bm={r['name']:r for r in bb}
  require(len(am)==len(aa) and len(bm)==len(bb) and bb,'Duplicate assertion names or empty Chrome assertions')
  a_full=a['status']=='OK' and bool(aa) and all(r['status']=='PASS' for r in aa);b_full=all(r['status']=='PASS' for r in bb)
  vg[('native_PASS' if a_full else 'native_not_PASS')+'__'+('chrome_PASS' if b_full else 'chrome_not_PASS')]+=1
  missing=sum(name not in am for name in bm)
  if missing:summary['missingNativeAssertions'].append({'test':a['test'],'nativeHarness':a['status'],'missing':missing,'chromeAssertions':len(bb)})
  for name in set(am)|set(bm):
   av=am.get(name);bv=bm.get(name);astatus='MISSING' if av is None else av['status'];bstatus='MISSING' if bv is None else bv['status']
   if av is not None and a['status']!='OK':astatus='UNQUALIFIED_HARNESS_'+a['status']
   ag['native_'+astatus+'__chrome_'+bstatus]+=1
   if bstatus=='PASS' and av is not None and astatus!='PASS':summary['nativeFailedAssertionsOnChromePass'].append({'test':a['test'],'name':name,'nativeStatus':astatus})
 ac.update(ag);vc.update(vg);hc.update(hg);summary['groups'][group]={'variants':total,'assertionClasses':dict(ag),'variantClasses':dict(vg),'harnessClasses':dict(hg),'nativeRawSHA256':sha(npath),'chromeRawSHA256':sha(cpath),'scheduleSHA256':nmeta['signature']['schedule_sha256']}
require(len(seen)==279,'Frozen279 group union differs');summary.update(assertionClasses=dict(ac),variantClasses=dict(vc),harnessClasses=dict(hc),chromePassingAssertions=sum(v for k,v in ac.items() if k.endswith('__chrome_PASS')),nativePassesAmongChromePassingAssertions=ac['native_PASS__chrome_PASS'],chromePassingVariants=sum(v for k,v in vc.items() if k.endswith('__chrome_PASS')),nativeFullyPassesAmongChromePassingVariants=vc['native_PASS__chrome_PASS'],complete=True)
path=OUT/'paired-chrome279-default-audit.json';require(not path.exists(),'Preserve previous audit');path.write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
