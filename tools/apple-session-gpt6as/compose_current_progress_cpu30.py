#!/usr/bin/env python3
"""Compose public current-artifact evidence without pooling historical runs."""
from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,subprocess
ROOT=Path.cwd().resolve();TASK='20261029-cpu30';OUT=Path('out/lume-'+TASK)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_text())
def require(condition,message):
 if not condition:raise RuntimeError(message)
def save(path,value):
 path.parent.mkdir(parents=True,exist_ok=True);tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(value,indent=2)+'\n');tmp.replace(path)
score=read('agent/scorecard-'+TASK+'.json');native=score['current_baseline_binary_sha256'];source=score['current_native_source_commit']
require(score['task_id']==TASK and score['new_session_native_validation'],'Task accepted evidence is not ready')
# This compositor describes the explicitly accepted f52 checkpoint. A changed
# executable requires a new reviewed compositor/input set, never a mutable alias.
require(native=='f52a08ca9a1bc6cd79eb082d8bedd7b5851e23341ee7427ff01cddfb038a8a9c' and source=='df4ae3bc8d52a06778e0d8a1f706138201dd043d','Accepted artifact changed; review new evidence inputs before updating public status')
build=read(OUT/'native-combined-1/summary.json');resources=read(OUT/'native-resources-1/summary.json');cohort=OUT/'compat-baseline-2/cohort';priority=read(cohort/'priority-summary.json');original=read(cohort/'original-six-receipt.json')
require(build['passed'] and build['artifacts']['lumen']['sha256']==native and sha(build['artifacts']['lumen']['path'])==native,'Native build identity differs')
require(resources['accepted'] and resources['artifact']['sha256']==native and priority['binarySHA256']==native and original['binarySHA256']==native and original['passed'],'Accepted evidence identity differs')
require((priority['scheduled'],priority['run'],priority['fullyPassing'],priority['unrun'])==(1515,103,96,1412),'Scoped WPT evidence changed')
status=read('output/session-102900614/progress-state/status.json')
status.update(taskId=TASK,updatedUTC=datetime.now(timezone.utc).isoformat(),iteration=2,fullOfficialWptPassPercent=None,
 phase='Current accepted native f52: 96/103 selected Encoding variants fully pass (93.20%); 1,412 of 1,515 frozen variants remain unrun. The original six-file subset passes 1,850/1,850 assertions. Controlled scrolling uses 11.085% CPU at p95 and peaks at 11.137%, meeting the revised 30% limit. Memory remained stable through 20 navigation cycles. The JSON/BOM candidate now passes 98/103 (95.146%) on the same selection, pending its own resource acceptance. The qualified Chrome control recorded 89/103 under investigation; this is not an overall browser ranking. Device IPA and iPad app execution remain unverified.')
status['currentWpt']={'binarySha256':native,'sourceCommit':source,'sourceManifestSHA256':build['sourceManifestSHA256'],'scheduled':1515,'recorded':103,'fullyPassing':96,'unrun':1412,'assertions':priority['assertions'],'groups':priority['groups'],'cohortSHA256':priority['cohortSHA256'],'scope':'Fresh selected upstream Encoding core and stream tests on this exact artifact; the original six-file, 1,850-assertion subset is separate. Every unrun test, error and failure remains visible; full official WPT is unknown.'}
status['wpt']={'passed':1850,'total':1850,'passPercent':100,'reportedFiles':6,'fullyPassingFiles':6,'chromePassed':None,'chromeTotal':None,'binarySha256':native,'nativeSourceCommit':source,'scope':'Six unchanged upstream WPT-source files only; separate from the fresh 103-variant Encoding cohort and not full official WPT.','referenceStatus':'Separate qualified Chrome 103-variant control recorded 89/103; differences are under investigation and this six-file result is not that comparison.'}
scenarios={row['name']:row for row in resources['scenarios']};scroll=scenarios['scroll']['phaseMetrics']['scrolling'];idle=scenarios['scroll']['phaseMetrics']['idle:after'];soak=scenarios['navigation']['settledRSSComparison']
oldperformance=status.get('performance',{})
status['performance']={'binarySha256':native,'sourceCommit':source,'idleStatus':'Passed controlled native idle','idleCpuP95Percent':idle['cpu_p95_percent'],'idleRssMiB':idle['rss_end_mib'],'interactionStatus':'Passed controlled scrolling','interactionCpuP95Percent':scroll['cpu_p95_percent'],'interactionCpuPeakPercent':scroll['cpu_peak_percent'],'interactionCpuCeilingPercent':30,'scrollSamples':scroll['samples'],'scrollDurationSeconds':scroll['duration_seconds'],'typingStatus':'Not freshly measured','memorySoakStatus':'Passed 20 navigation cycles','settledRssGrowthMiB':soak['growth_mib'],'scope':'Synthetic SDL wheel input and actual native page navigations;1Hz process-tree CPU (100%=one logical core) and RSS. Not whole-host, iPad or IPA measurements.'}
if oldperformance.get('binarySha256'):
 status['performance']['historicalInteraction']={'binarySha256':oldperformance['binarySha256'],'status':oldperformance.get('interactionStatus','Historical'),'cpuP95Percent':oldperformance.get('interactionCpuP95Percent')}
status['apple']={'macNative':'Verified f52 visible native app, scroll/idle/navigation checks','ipadSimulator':'Simulator OS reachable; Lume app execution unverified','sameIpa':'Unverified: no signed device IPA or signing identity; platform V8 unavailable and Xcode SDK resolution fails','iphoneExecutionRequired':False}
candidate=read(OUT/'native-bom-1/summary.json');candidate_wpt=read(OUT/'compat-parser-1/summary.json')
candidate_hash='4b26d5b59edf9f6866b1317e6ffa29aabe7d8681cdee26a2b56a670d23536d54'
require(candidate['passed'] and candidate['artifacts']['lumen']['sha256']==candidate_hash and sha(candidate['artifacts']['lumen']['path'])==candidate_hash,'Functional candidate identity differs')
require(candidate_wpt['complete'] and candidate_wpt['sameArtifact'] and candidate_wpt['binarySHA256']==candidate_hash,'Functional WPT identity differs')
candidate_counts=candidate_wpt['cohortSummary'];require((candidate_counts['scheduled'],candidate_counts['run'],candidate_counts['fullyPassing'],candidate_counts['unrun'])==(1515,103,98,1412),'Functional candidate scope differs')
status['functionalCandidate']={'binarySha256':candidate_hash,'fullyPassingVariants':98,'recordedVariants':103,'scheduledFrozenVariants':1515,'unrunVariants':1412,'historicalRun':False,'scopeLabel':'Encoding core + streams','sourceBaseCommit':candidate['sourceBaseCommit'],'sourceManifestSHA256':candidate['compiledSourceManifestSHA256'],'resourceAcceptance':'Pending on this exact artifact','assertions':candidate_counts['assertions']}
status['currentChromeDiagnostic']={'recorded':103,'fullyPassing':89,'assertions':{'PASS':23477,'FAIL':181},'harnessOK':103,'scope':'Qualified Chrome on the identical frozen 103 variants. Differences are being investigated; no overall ranking claim. Raw failures remain retained.'}

catalog=status.setdefault('candidateCatalog',{})
if catalog.get('qualifiedChrome'):catalog['qualifiedChrome']['historicalRun']=True
if status.get('chromeControl'):status['chromeControl']['observationScope']='Earlier 26-variant control; separate fresh 103 control recorded 89/103 and is under investigation'
for row in catalog.get('cohorts',[]):
 row['historicalRun']=True;row['acceptedArtifact']=row.get('binarySha256')==native
fresh={'binarySha256':native,'scheduled':1515,'recorded':103,'fullyPassing':96,'unrun':1412,'passingPercentOfRun':100*96/103,'passingCoveragePercent':100*96/1515,'assertions':priority['assertions'],'acceptedArtifact':True,'historicalRun':False,'cohortSHA256':priority['cohortSHA256'],'scope':'Fresh 20261029-cpu30 baseline run; earlier runs retained separately.'}
catalog['cohorts']=[fresh,*[row for row in catalog.get('cohorts',[]) if row.get('cohortSHA256')!=fresh['cohortSHA256']]];catalog['latestMeasuredCohort']=fresh
if status.get('prioritySummary'):status['prioritySummary']['isCurrentAcceptedNativeArtifact']=status['prioritySummary'].get('binarySha256')==native
if status.get('expandedWpt'):status['expandedWpt']['isCurrentAcceptedNativeArtifact']=status['expandedWpt'].get('lumenBinarySHA256')==native
head=subprocess.check_output(['git','--no-pager','rev-parse','HEAD'],text=True).strip();status['sourceCommit']=head
manifestpath=Path('output/session-'+TASK+'/public/manifest.json')
if manifestpath.exists():
 archive=read(manifestpath);require(archive.get('sourceCommit')==head,'Source archive is stale; package current committed HEAD first')
 require(sha(manifestpath.parent/'src.tar.gz')==archive['sha256'],'Public source archive bytes differ')
 status['sourceArchive']={key:archive[key] for key in ['sourceCommit','sha256','bytes'] if key in archive}
else:status.pop('sourceArchive',None)
state=Path('output/session-'+TASK+'/progress-state');save(state/'status.json',status)
print(json.dumps({'passed':True,'taskId':TASK,'state':str(state/'status.json'),'sourceCommit':head,'nativeSourceCommit':source,'nativeBinarySHA256':native,'currentWpt':{k:status['currentWpt'][k] for k in ['recorded','fullyPassing','unrun','scheduled']},'score':score['score'],'historicalCohortsKeptSeparate':True,'sourceArchivePresent':bool(status.get('sourceArchive'))},indent=2))
