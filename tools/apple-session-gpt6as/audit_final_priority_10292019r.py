from pathlib import Path
from datetime import datetime,timezone
import hashlib,importlib.util,json,os,subprocess,tarfile,tempfile,time
root=Path.cwd().resolve()
if str(root)!='/Users/devin/repos/lumen':raise ValueError('Unexpected canonical Mac workspace')
base=root/'out/10292019r-mac';out=base/'independent-final-003';out.mkdir(parents=True,exist_ok=True);began=time.monotonic()
spec=importlib.util.spec_from_file_location('independent_raw',root/'tools/apple-session-gpt6as/audit_wpt_candidate.py');A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)
SOURCE='b816d32f2a486e28fa2cb6846c603a40e7e8ca02';BINARY='5c16cbd5db05ddf0b8f7b22e54fb86dce6797771db80402e171ac8581955a2f6'
receipt={'startedUTC':datetime.now(timezone.utc).isoformat(),'acceptedEvidence':False,'noRegression':False,'sourceCommit':SOURCE,'scope':'Independent source-bound native Mac frozen1515 inventory audit; not full official WPT, mobile/IPA acceptance, or project quality score.','fullOfficialWptPassPercent':None}
def atomic(path,value):
 data=(json.dumps(value,ensure_ascii=True,separators=(',',':'))+'\n').encode();fd,name=tempfile.mkstemp(prefix=path.name+'.',suffix='.tmp',dir=path.parent)
 with os.fdopen(fd,'wb')as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
 os.replace(name,path)
 if path.read_bytes()!=data:raise ValueError('Audit output readback differs')
def protocol_same(before,after):
 for field in ['wptRevision','fullFrozenScheduleSHA256','fullFrozenVariantCount','frozenSchedule','groups']:
  if before['plan'][field]!=after['plan'][field]:raise ValueError('Frozen inventory changed: '+field)
 for group in set(before['signatures'])&set(after['signatures']):
  for field in (*A.SIGNATURE_FIELDS,'execution_adapter','collector_sha256'):
   if before['signatures'][group].get(field)!=after['signatures'][group].get(field):raise ValueError('Paired execution protocol differs: '+group+':'+field)
try:
 target_plan=base/'wpt-final-003/cohort/cohort.json';current=A.load_cohort(target_plan);before=A.load_cohort(base/'wpt-candidate-001/cohort/cohort.json');prior=A.read(base/'independent-expanded-001/receipt.json')
 if not prior['accepted']or before['summary']!=prior['summary']or before['evidence']!=prior['evidence']:raise ValueError('Accepted expanded baseline changed')
 if current['plan']['fullFrozenVariantCount']!=1515 or current['plan']['binary']['sha256']!=BINARY:raise ValueError('Final inventory/artifact identity differs')
 if current['summary']['measurementErrorVariants']:raise ValueError('Final runtime measurement or cleanup errors require review')
 protocol_same(before,current)
 builddir=base/'native-worker-003';build=A.read(builddir/'summary.json');manifestpath=builddir/'compiled-source-sha256.json'
 if not build['passed']or build['sourceBaseCommit']!=SOURCE or build['artifacts']['lumen']['sha256']!=BINARY:raise ValueError('Final native build not qualified')
 if A.sha(manifestpath)!=build['sourceManifestSHA256']:raise ValueError('Compiled source manifest changed')
 manifest=A.read(manifestpath)
 for name,digest in manifest.items():
  blob=subprocess.check_output(['git','show',SOURCE+':'+name],cwd=root,stderr=subprocess.DEVNULL)
  if hashlib.sha256(blob).hexdigest()!=digest:raise ValueError('Compiled source differs: '+name)
 for row in build['stages']:
  if not row.get('passed')or A.sha(root/row['monitor'])!=row['monitorSHA256']or A.sha(root/row['log'])!=row['logSHA256']:raise ValueError('Native stage bytes/acceptance changed: '+row['name'])
  metrics=A.read(root/row['monitor'])
  if metrics.get('natural_exit_code')!=0 or metrics.get('collector_error')or metrics.get('ended_by_monitor')or metrics.get('owned_group_cleanup',{}).get('error'):raise ValueError('Native stage telemetry unqualified: '+row['name'])
 keys={A.key(r)for r in before['rows']};common=[r for r in current['rows']if A.key(r)in keys];new=[r for r in current['rows']if A.key(r)not in keys];regression=A.audit_rows(before['rows'],common)
 fixedkeys={A.key(r)for name,group in before['plan']['groups'].items()if name!='encoding-rest'for r in group};fixed_before=[r for r in before['rows']if A.key(r)in fixedkeys];fixed=[r for r in current['rows']if A.key(r)in fixedkeys];fixedsummary=A.summarize_rows(fixed,279);fixedcomparison=A.audit_rows(fixed_before,fixed)
 iso='/encoding/legacy-mb-japanese/iso-2022-jp/iso2022jp-encode-form-errors-stateful.html';old_iso=[r for r in before['rows']if r['test']==iso];new_iso=[r for r in current['rows']if r['test']==iso]
 if len(old_iso)!=1:raise ValueError('Recorded baseline ISO2022 case identity differs')
 iso_status={'test':iso,'baselineStatus':old_iso[0]['status'],'baselineFullyPassing':A.fully_passes(old_iso[0]),'candidateStatus':new_iso[0]['status']if new_iso else'UNRUN','candidateFullyPassing':A.fully_passes(new_iso[0])if new_iso else False,'candidateAssertionCount':len(new_iso[0].get('subtests',[]))if new_iso else 0}
 observed=out/'observed-results.json';atomic(observed,{'count':len(current['rows']),'rows':current['rows']});atomic(out/'baseline-regression.json',regression);atomic(out/'fixed279-regression.json',fixedcomparison)
 files=[target_plan,manifestpath,builddir/'summary.json',base/'independent-expanded-001/receipt.json',base/'fixture-comparison-diagnostic-002/receipt.json',out/'baseline-regression.json',out/'fixed279-regression.json']
 for name in current['evidence']:files.extend(A.group_paths(target_plan.parent,name))
 for row in build['stages']:files.extend([root/row['log'],root/row['monitor']])
 files=list(dict.fromkeys(files));file_manifest=[{'path':str(p.relative_to(root)),'bytes':p.stat().st_size,'sha256':A.sha(p)}for p in files];raw=out/'raw-evidence.tar.gz'
 with tarfile.open(raw,'w:gz')as archive:
  for p in files:archive.add(p,arcname=str(p.relative_to(root)),recursive=False)
 if any(A.sha(root/entry['path'])!=entry['sha256']for entry in file_manifest):raise ValueError('Evidence changed during final audit; do not audit active writer')
 nonpasses=[{'test':r['test'],'status':r['status'],'assertions':[t for t in r.get('subtests',[])if t['status']!='PASS']}for r in current['rows']if not A.fully_passes(r)]
 receipt.update(acceptedEvidence=True,noRegression=regression['noRegression'],compiledSourcesMatchCommit=True,compiledSourceFiles=len(manifest),sourceManifestSHA256=A.sha(manifestpath),binary=current['plan']['binary'],summary=current['summary'],fullFrozenInventoryExecuted=len(current['rows'])==1515,baselineSummary=before['summary'],priorPassingAssertionsLost=len(regression['lostPassingAssertions']),priorPassingVariantsLost=len(regression['lostFullyPassingVariants']),priorRecordedVariantsMissing=len(regression['missingCandidateVariants']),priorAssertionsGained=len(regression['gainedPassingAssertions']),newlyObserved=A.summarize_rows(new,1515-len(before['rows'])),fixed279=fixedsummary,fixed279NoRegression=fixedcomparison['noRegression'],iso2022=iso_status,nonPassingVariants=nonpasses,planSHA256=current['planSHA256'],evidence=current['evidence'],observedResultsSHA256=A.sha(observed),rawArchive={'path':str(raw.relative_to(root)),'bytes':raw.stat().st_size,'sha256':A.sha(raw)},rawFiles=file_manifest)
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
receipt['finishedUTC']=datetime.now(timezone.utc).isoformat();receipt['durationSeconds']=round(time.monotonic()-began,3);atomic(out/'receipt.json',receipt)
print(json.dumps({'accepted':receipt['acceptedEvidence'],'noRegression':receipt['noRegression'],'summary':receipt.get('summary'),'lostAssertions':receipt.get('priorPassingAssertionsLost'),'fixed279':receipt.get('fixed279',{}).get('fullyPassing'),'iso2022Pass':receipt.get('iso2022',{}).get('candidateFullyPassing'),'error':receipt.get('error'),'receipt':str((out/'receipt.json').relative_to(root))},separators=(',',':')))
