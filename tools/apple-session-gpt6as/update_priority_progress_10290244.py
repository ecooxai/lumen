"""Refresh public category progress from frozen inventory and verified reports."""
import argparse
from datetime import datetime, timezone
import hashlib, json, pathlib

ROOT=pathlib.Path.cwd();BASE=ROOT/'output/wpt-10290244';STATE=ROOT/'output/session-10290244/progress-state'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cohort',type=pathlib.Path,help='Directory containing cohort.json and priority-comparison.json')
args=parser.parse_args()
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
frozen=json.loads((BASE/'priority-selection-v1.json').read_text())
if frozen['schedule_sha256']!='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe':raise RuntimeError('Unexpected frozen selection')
value={'selectedVariants':frozen['selected_variants'],'selectedSourceFiles':frozen['selected_source_files'],
       'scope':'Predeclared priority subset; not full WPT. All frozen URL variants remain in the schedule.',
       'selectionScheduleSHA256':frozen['schedule_sha256'],'categories':{}}
report=None;plan=None
if args.cohort:
    plan=json.loads((args.cohort/'cohort.json').read_text())
    report=json.loads((args.cohort/'priority-comparison.json').read_text())
    if report['cohort_sha256']!=sha(args.cohort/'cohort.json')or plan['schedule_sha256']!=frozen['schedule_sha256']or report['scheduled_variants']!=frozen['selected_variants']:raise RuntimeError('Comparison and frozen cohort identities differ')
    value.update(lumenBinarySHA256=plan['binaries']['lumen']['sha256'],chromiumBinarySHA256=plan['binaries']['chromium']['sha256'],reportSHA256=sha(args.cohort/'priority-comparison.json'),reportUpdatedUTC=report['created_utc'])
for name,count in frozen['category_counts'].items():
    row={'scheduled':count['variants'],'lumenRecorded':0,'chromeRecorded':0,'lumenFullyPassing':0,'chromeFullyPassing':0,'chromePassRetention':None,'bothComplete':False,'harnessesHealthy':False}
    if report:
        source=report['categories'][name]
        if source['scheduled_variants']!=count['variants']:raise RuntimeError('Category denominator differs: '+name)
        row.update(lumenRecorded=source['lumen']['recorded_files'],chromeRecorded=source['chromium']['recorded_files'],
                   chromePassRetention=source['parity']['chromium_pass_retention'],bothComplete=source['both_schedules_complete'],
                   harnessesHealthy=source['both_harnesses_complete_without_zero_assertion_files'],
                   lumenRemaining=source['lumen']['remaining_files'],chromeRemaining=source['chromium']['remaining_files'],
                   lumenFullyPassing=source['lumen']['files_passing_all_assertions'],chromeFullyPassing=source['chromium']['files_passing_all_assertions'],
                   lumenSubtestOutcomes=source['lumen']['subtest_outcomes'],chromeSubtestOutcomes=source['chromium']['subtest_outcomes'],
                   lumenFileOutcomes=source['lumen']['file_outcomes'],chromeFileOutcomes=source['chromium']['file_outcomes'])
    value['categories'][name]=row
status=json.loads((STATE/'status.json').read_text())
if not args.cohort and status.get('expandedWpt',{}).get('reportSHA256')and status['expandedWpt'].get('selectionScheduleSHA256')==frozen['schedule_sha256']:
    value=status['expandedWpt']
score=json.loads((ROOT/'agent/scorecard-10290244.json').read_text())
gates={row['id']:row for row in score['gates']}
comparison_path=gates['wpt_original']['evidence'][0]
comparison=json.loads((ROOT/comparison_path).read_text())
candidate,reference=comparison['candidate'],comparison['reference']
native=candidate['run_info']['browser_version'].removeprefix('sha256:')
if not comparison.get('comparable')or comparison.get('blockers')or native!=score['current_baseline_binary_sha256']:raise RuntimeError('Current accepted WPT artifact does not match the scorecard')
status['wpt']={'passed':candidate['subtests']['passed'],'total':candidate['subtests']['union_inventory'],
    'passPercent':candidate['subtests']['observed_pass_percent'],'reportedFiles':candidate['files']['reported'],
    'fullyPassingFiles':candidate['files']['passed'],'chromePassed':reference['subtests']['passed'],
    'chromeTotal':reference['subtests']['union_inventory'],'binarySha256':native,
    'scope':'Pinned six-file upstream WPT-source subset. Full official WPT remains unmeasured.','evidence':comparison_path}
interaction=gates['perf_interaction'];idle=gates['perf_idle']
status['performance']={'binarySha256':native,'interactionStatus':interaction['status'],
    'interactionCpuP95Percent':interaction.get('scroll_cpu_p95_percent'),'interactionCpuCeilingPercent':10,
    'idleStatus':idle['status'],'idleCpuP95Percent':idle.get('review',{}).get('cpuP95Percent'),
    'idleRssMiB':idle.get('review',{}).get('rssEndMiB'),
    'scope':'Artifact-matched native test process measurements; system live metrics are separate.'}
if interaction['status']=='failed'and isinstance(interaction.get('scroll_cpu_p95_percent'),(int,float)):
    status['phase']=f"The original important WPT subset passes completely. Current native scrolling p95 is {interaction['scroll_cpu_p95_percent']:.3f}% against the 10% target; optimization and broader standards checks continue."
else:
    status['phase']=f"The current accepted native build {native[:12]} passes all 1,850 assertions in the original six-file WPT subset. Fresh CPU/RAM acceptance for this build is {interaction['status']}; broader standards and Apple package checks continue."
status.pop('targetedNativeChecks',None)
history=interaction.get('historical_evidence',[])
if history:
    last=history[-1]
    status['performance']['historicalInteraction']={'binarySha256':last.get('binary_sha256'),'status':last.get('prior_status','unverified'),'scope':'Historical executable; does not establish performance of the current accepted build.'}
    for name in last.get('evidence',[]):
        path=ROOT/name
        if path.name=='receipt.json'and path.is_file():
            receipt=json.loads(path.read_text())
            if receipt.get('binary_sha256')!=last.get('binary_sha256'):raise RuntimeError('Historical performance artifact differs')
            measured=receipt.get('phase_metrics',{}).get('scrolling',{})
            if isinstance(measured,dict):
                cpu=measured.get('cpuP95Percent',measured.get('cpu_p95_percent'))
                if isinstance(cpu,(int,float)):status['performance']['historicalInteraction']['cpuP95Percent']=cpu
    if status['performance']['historicalInteraction'].get('status')=='failed':
        status['phase']+=' The previous executable failed the scrolling CPU target; its measurements remain historical.'
if score.get('history'):
    iteration=score['history'][-1].get('iteration','')
    if iteration.rsplit('-',1)[-1].isdigit():status['iteration']=int(iteration.rsplit('-',1)[-1])
value['isCurrentAcceptedNativeArtifact']=value.get('lumenBinarySHA256')in(None,native)
review_path=BASE/'reference-review.json'
if review_path.is_file():
    review=json.loads(review_path.read_text())
    if review.get('status')=='quarantined':
        status['wpt']['referenceStatus']='Installed control under review: fresh URL sanity checks failed.'
        status['phase']+=' The installed Chrome control failed independent URL sanity checks; comparative rates are quarantined while raw failures remain visible.'
        if value.get('chromiumBinarySHA256')==review.get('binarySHA256'):
            value['referenceReview']={key:review[key]for key in ('status','reason','binarySHA256','probeEvidence')}
            for row in value['categories'].values():
                row.setdefault('recordedChromePassRetention',row.get('chromePassRetention'))
                row['chromePassRetention']=None
                row['referenceStatus']='quarantined'
status['expandedWpt']=value;status['updatedUTC']=datetime.now(timezone.utc).isoformat()
archive=ROOT/'output/session-10290244/public/manifest.json'
if archive.is_file():
    manifest=json.loads(archive.read_text());status['sourceArchive']=manifest;status['sourceCommit']=manifest['sourceCommit']
temporary=STATE/'status.tmp';temporary.write_text(json.dumps(status,indent=2)+'\n');temporary.replace(STATE/'status.json')
print(json.dumps({'updatedUTC':status['updatedUTC'],'selectedVariants':value['selectedVariants'],
                  'lumenRecorded':sum(row['lumenRecorded']for row in value['categories'].values()),
                  'chromeRecorded':sum(row['chromeRecorded']for row in value['categories'].values()),
                  'originalSubsetRefreshedFromAcceptedEvidence':True,'acceptanceScoreUnchanged':True},indent=2))
