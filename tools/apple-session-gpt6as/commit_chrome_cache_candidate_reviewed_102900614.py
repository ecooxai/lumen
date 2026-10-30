"""Commit exact measured browser-chrome reuse and refresh the active handoff."""
from pathlib import Path
from collections import Counter
import datetime as dt,hashlib,json,os,re,shutil,subprocess
os.environ.update(GIT_PAGER='cat',PAGER='cat');BASE=Path('output/session-102900614')
NATIVE='f52a08ca9a1bc6cd79eb082d8bedd7b5851e23341ee7427ff01cddfb038a8a9c';REFERENCE='07cca318e49e830d25280b9a8b5e345fe9ea7bf57b6515e80d12946f767068e8'
SOURCE={'src/app/main.c':'d76a2f3bd2c22d52e66ca2a4b32a851332aacbbd92b2d684babe475a6d8852fd','src/app/chrome_paint.h':'1a85870834b5282ca6b0e53ad3a327073ff0343cdc8964a5d697cc86168eba68','tests/test_render_overdraw.c':'32515b62aece48fd75fe0246866461c7438a195ba196921faaa64ab6c040feb0'}
TOOLS={'tools/apple-session-gpt6as/apply_chrome_paint_cache.py': '5e267c07173e69684f4d10a348f0aad3e5bea0f7824d0f66c0902134e0e69756', 'tools/apple-session-gpt6as/install_chrome_cache_tests.py': '3505e0d9c8caa8e94f2b97ad9c148408397028cb70d6f4e5aaad77fa5ce9d8bf', 'tools/apple-session-gpt6as/run_chrome_reference_102900614.py': '9efb527005acd9aa151b64f02a398d2130b710d8796cee526117061592b4fee1', 'tools/apple-session-gpt6as/run_chrome_cache_gate_102900614.py': 'd369302dec679ccea12afc13a5ba637183c0706b16f59283e23a71e29aba5be1'}
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(*args,raw=False):
    value=subprocess.check_output(['git','--no-pager',*args]);return value if raw else value.decode().strip()
def read(p):return json.loads(Path(p).read_text())
def write(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614'or git('diff','--cached','--name-only'):raise RuntimeError('Branch/index coordination required')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['score']!=45 or score['history'][-1]['iteration']!='102900614-016':raise RuntimeError('Checkpoint017 order or score differs')
for name,digest in {**SOURCE,**TOOLS}.items():
    if sha(name)!=digest:raise RuntimeError('Reviewed source/helper differs: '+name)
redpath=BASE/'chrome-cache-reference-1/summary.json';greenpath=BASE/'chrome-cache-candidate-1/summary.json';red=read(redpath);green=read(greenpath)
if red['passed']or not green['passed']or red['fixtureSHA256']!=SOURCE['tests/test_render_overdraw.c']or green['fixtureSHA256']!=red['fixtureSHA256']or red['artifacts']['lumen']['sha256']!=REFERENCE or green['artifacts']['lumen']['sha256']!=NATIVE:raise RuntimeError('Frozen toolbar red/green differs')
if 'RESULTS render-overdraw FAIL (99/100 checks)'not in red['stages'][1]['resultLines']or 'RESULTS render-overdraw PASS (100/100 checks)'not in green['stages'][1]['resultLines']:raise RuntimeError('Toolbar assertion count differs')
for build in(red,green):
    for artifact in build['artifacts'].values():
        if sha(artifact['path'])!=artifact['sha256']:raise RuntimeError('Frozen executable differs')
    for stage in build['stages']:
        monitor=read(stage['monitor']);expected=1 if build is red and stage['name']=='pixel-equivalence'else 0
        if monitor.get('natural_exit_code')!=expected or monitor.get('collector_error')or monitor.get('ended_by_monitor')or sha(stage['monitor'])!=stage['monitorSHA256']or sha(stage['log'])!=stage['logSHA256']:raise RuntimeError('Build/test evidence differs')
compiled=read(green['compiledSourceManifest']);tracked=set(git('ls-files','-z',raw=True).decode().split('\0'));tracked_compiled={k:v for k,v in compiled.items()if k in tracked}
for name,digest in compiled.items():
    if name in tracked:value=sha(name)if name in SOURCE else hashlib.sha256(git('show','HEAD:'+name,raw=True)).hexdigest()
    else:
        if name.startswith('src/'):
            if not name.startswith('src/net/webtransport-native/output/') or name in read(green['compiledDependencyManifest']) or subprocess.run(['git','check-ignore','--quiet','--',name]).returncode != 0:
                raise RuntimeError('Untracked production input needs review: '+name)
        value=sha(name)
    if value!=digest:raise RuntimeError('Prospective compiled input differs: '+name)
controlpath=BASE/'scroll-chrome-cache-control/summary.json';control=read(controlpath);resourcepath=Path(control['resourceReceipt']);resource=read(resourcepath);referencepath=BASE/'scroll-chrome-reference/receipt.json';reference=read(referencepath)
changed=['src/app/chrome_paint.h','src/app/main.c']
if control['binary']['sha256']!=NATIVE or control['changedCompiledSources']!=changed or control['changedCompiledDependencies']!=changed or not control['pixelIdentical']or not control['nativeWindowCaptured']or not control['gpuClean']or control['gpuErrors']:raise RuntimeError('Source/pixel/native/GPU proof incomplete')
identity=control['baselineByteIdentity']
if identity['sha256']!=REFERENCE or sha(identity['measuredBinary'])!=REFERENCE or sha(identity['referenceBuildBinary'])!=REFERENCE:raise RuntimeError('Reference byte identity differs')
for receipt,expected in[(reference,REFERENCE),(resource,NATIVE)]:
    if receipt['binary_sha256']!=expected or receipt['binary_sha256_after']!=expected or receipt['natural_app_return_code']!=0 or receipt['app_termination']['forced']or receipt['sampler_termination']['forced']:raise RuntimeError('Resource identity/cleanup differs')
if sha(resourcepath)!='ab8352381773f52b44e6b012069d8cb9803304050c1b00d5b7eee32fa28c7701'or sha(resourcepath)!=control['resourceReceiptSHA256']:raise RuntimeError('Reviewed resource receipt differs')
old=reference['phase_metrics']['scrolling'];new=resource['phase_metrics']['scrolling'];idle=resource['phase_metrics']['idle:after'];pixels=read(control['pixelComparison'])
if (old['cpu_p95_percent'],new['cpu_p95_percent'])!=(15.998,15.09)or resource['accepted']or control['resourceAccepted']or not pixels['passed']or pixels['differingPagePixels']!=0:raise RuntimeError('Reviewed CPU failure or pixels differ')
cohort=BASE/'wpt-combined-native-1';plan=read(cohort/'cohort.json');originalpath=cohort/'original-six-receipt.json';original=read(originalpath)
if plan['binary']['sha256']!=NATIVE or plan['fullFrozenVariantCount']!=1515 or not original['passed']or original['binarySHA256']!=NATIVE or original['files']!=6 or original['fullyPassing']!=6 or original['assertions']!={'PASS':1850}or sha(original['report'])!=original['reportSHA256']:raise RuntimeError('Fresh original WPT identity differs')
group=cohort/'encoding-streams';schedule=read(group/'schedule.json');meta=read(group/'run.json');rows=[json.loads(line)for line in(group/'results.jsonl').read_text().splitlines()]
if schedule!=plan['groups']['encoding-streams']or len(schedule)!=26 or len(rows)!=26 or len({r['test']for r in rows})!=26 or {r['test']for r in rows}!={r['test']for r in schedule}or meta['signature']['binary_sha256']!=NATIVE or meta['signature']['schedule_sha256']!=plan['groupScheduleSHA256']['encoding-streams']:raise RuntimeError('Fresh streams identity differs')
if any(r.get('status')not in('OK','PASS')or not r.get('subtests')or any(s['status']!='PASS'for s in r['subtests'])for r in rows)or Counter(s['status']for r in rows for s in r['subtests'])!=Counter(PASS=358):raise RuntimeError('Fresh26/358 stream result differs')
for row in rows:
    if(row.get('metrics')or{}).get('errors')or row.get('telemetry_errors')or(row.get('cleanup')or{}).get('error')or(row.get('cleanup')or{}).get('profile_error'):raise RuntimeError('Fresh WPT infrastructure incomplete')
review=Path('agent/gpt6-astra-pro_builtin-agents_devinmac3d_chrome_cache_review_102900614.md')
if not review.is_file():raise RuntimeError('Updated independent toolbar review missing')
allow=[*SOURCE,*TOOLS,str(review)];subprocess.run(['git','--no-pager','diff','--check','--',*allow],check=True)
for name in allow:
    if name not in tracked and any(line.rstrip(b' \t')!=line for line in Path(name).read_bytes().splitlines()):raise RuntimeError('Authored whitespace requires correction: '+name)
folder=Path('agent/evidence/102900614-017');folder.mkdir(exist_ok=False)
paths=[redpath,greenpath,controlpath,resourcepath,referencepath,Path(control['pixelComparison']),Path(control['windowCaptureReceipt']),cohort/'cohort.json',originalpath,*[group/n for n in('run.json','schedule.json','results.jsonl','summary.json')]]
for build in(red,green):
    paths += [Path(build['compiledSourceManifest']),Path(build['compiledDependencyManifest'])]
    for stage in build['stages']:paths += [Path(stage['monitor']),Path(stage['log'])]
evidence=[];raw=[]
for index,path in enumerate(dict.fromkeys(paths)):
    destination=folder/('%02d-'%index+path.parent.name+'-'+path.name);shutil.copyfile(path,destination);allow.append(str(destination));evidence.append({'source':str(path),'committed':str(destination),'sha256':sha(path),'bytes':path.stat().st_size})
    if path.suffix in('.log','.csv'):raw.append(str(destination))
manifestpath=folder/'tracked-compiled-source-sha256.json';write(manifestpath,tracked_compiled);allow.append(str(manifestpath))
now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
entry={'iteration':'102900614-017','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':45,'status':'Retain unchanged browser chrome pixels; paired15.998→15.09% scrolling still fails10%','native_artifact_promoted':False,'accepted_native_unchanged':score['current_baseline_binary_sha256'],'candidate_binary_sha256':NATIVE,'candidate_source_manifest':str(manifestpath),'source_sha256':SOURCE,'red':{'passed':99,'total':100},'green':{'passed':100,'total':100},'build_test_seconds':{'reference':red['durationSeconds'],'candidate':green['durationSeconds']},'resources':{'reference_binary_sha256':REFERENCE,'reference_scroll_p95':old['cpu_p95_percent'],'candidate_scroll_p95':new['cpu_p95_percent'],'candidate_scroll_mean':new['cpu_mean_percent'],'fixed_ceiling_percent':10,'accepted':False,'idle_p95':idle['cpu_p95_percent'],'idle_rss_start_mib':idle['rss_start_mib'],'idle_rss_end_mib':idle['rss_end_mib'],'duration_seconds':control['durationSeconds'],'pixels_identical':True,'native_window_captured':True,'gpu_clean':True},'original_wpt':original,'streams_wpt':{'variants':26,'fullyPassing':26,'assertions':{'PASS':358},'fullFrozenScheduledVariants':1515},'evidence':evidence,'raw_byte_preservation':raw,'implementation':'Rebuild and hash chrome normally; retain toolbar/sidebar pixels only while backing, geometry, scale, clear color, display hash and overlay invalidation match. Page pixels, input cadence and GPU submission remain unchanged.','limits':['Fixed10% scrolling gate still fails; older12.06% glyph numbers belong to another standards artifact.','Accepted733 remains unchanged; final current-binary navigation soak, broader WPT and identical signed IPA gates remain pending.'],'model_compute_duration':'Unavailable; parallel worker elapsed times are not summed'}
entrypath=Path('agent/evidence/102900614-017-chrome-cache.json');write(entrypath,entry);allow.append(str(entrypath));score.setdefault('candidate_artifacts',[]).append({'iteration':entry['iteration'],'binary_sha256':NATIVE,'source_manifest':str(manifestpath),'evidence':str(entrypath),'accepted':False,'note':'Fresh1850original+26/358streams pass; exact pixels/native capture/GPU pass;15.09% scrolling still exceeds10%.'});score['history'].append({k:entry[k]for k in('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:out.write(f"\n## 102900614-017 — retained browser chrome\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; elapsed `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Accepted **45/100**, artifact733 unchanged.\n- Frozen renderer **99/100 RED→100/100 GREEN**. Paired07cca→f52 scroll p95 **15.998%→15.09%**, mean11.679%; fixed10% gate still fails. Idlep95.995%, settledRSS150.312MiB stable. Workload117.482s; exact pixels, scoped native capture, GPU errors and two-file source/dependency delta verified. Existing V8 reused; all build/test1Hz receipts retained.\n- Freshf52 original6/6files,1850/1850assertions and streams26/26variants,358/358assertions pass. Broader cohort remains separate. Evidence `{entrypath}`.\n")
stamp=now.strftime('%Y%m%dT%H%M%SZ');archives=[]
for name in('AGENTS.md','agent/continue.md'):
    destination=Path('agent/archive')/(Path(name).name+'.before-102900614-017-'+stamp);shutil.copyfile(name,destination);allow.append(str(destination));archives.append(str(destination))
summary=f'''## Active checkpoint017

Accepted score45/100 and native733943aa remain unchanged; historical50 belongs to the previous task. Latest reviewed candidate `{NATIVE}` passes fresh original6/1850 and26/358 stream WPT assertions. Full official WPT remains unknown; all1515 priority variants stay scheduled and each native hash has its own live catalog row. Candidate CPU15.09% p95 still fails10%, despite paired reference15.998% improvement; idle.995%, stable150.312MiB, exact pixels/native capture/GPU clean. Final same-binary navigation soak and identical signed IPA execution remain unproved.

Completed fixes: ArrayBuffer transfer, text streams, native legacy decoding, queued child completion, body/frameset Window handlers with owner CSP, HTML metadata/XHR/dataURL decoding, tokenizer EOF underflow, eligible parent encoding, GPU/raster/glyph reuse and retained browser chrome. The failed tokenizer/GPU receipts remain preserved. Focused55945parse/57semantic/3contract and24case/172split sanitizer checks pass after the EOF repair; this is not universal safety or full platform conformance.

Fresh signed Chrome156 is independently qualified; actual23/26 stream variants and344PASS/14FAIL remain raw outcomes. Frame4d retains344/344 Chrome-passing assertions only on that26-case pair. Old installed comparator remains quarantined. Keep assertions, trust controls and denominators unchanged.

Next: finish bounded artifact-specific WPT discovery and actionable fixes, separately verify the pending row-copy optimization, then original/regression/resource/navigation-soak gates on the final chosen binary. Root brokers Mac I/O and serializes expensive work. Integrator alone commits exact allowlists; preserve later working edits. Use existing compatible prebuiltV8. Original deadline2026-10-29T01:14:41.459Z remains fixed.

Evidence `{entrypath}` and `agent/devsummary.md`. Live https://dd1cacbcfa84c17d.q4d3dgcpeeoq.opentunnel.xyz ; static https://lume-102900614-gpt6-astra-pro.surge.sh . Private access capability remains outside Git. Poll `python3 tools/apple-session-gpt6as/progress_inbox_102900614.py poll` between jobs. Update with `LUME_TASK_ID=102900614 python3 tools/apple-session-gpt6as/update_priority_progress_10290244.py`; push/pack/publish reviewed committed source after important progress and every10iterations. Never publish output/profile/key state or bypass prior FileHarbor403.
'''
agents=Path('AGENTS.md').read_text();heading='## Current evidence and next work'
if agents.count(heading)!=1:raise RuntimeError('AGENTS summary anchor differs')
agents=agents.split(heading)[0].replace('- The prior Chrome control is quarantined after native URL/anchor sanity failures. Restore a verified official reference without bypassing signature or trust checks; keep raw anomalous evidence.','- The historical installed comparator remains quarantined. Fresh signed Chrome156 is qualified, with real failures retained. Do not bypass trust checks or alter upstream assertions.')
Path('AGENTS.md').write_text(agents+summary)
Path('agent/continue.md').write_text('# Lume102900614 — active continuation\n\nRepo `/Users/devin/repos/lumen`, alias `~/project/lumen`; branch `gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614`. Read AGENTS.md and byte-exact agent/task.md first. Started2026-10-28T22:14:41.459Z, goal98/100, deadline01:14:41.459Z. This checkpoint is not goal completion.\n\n'+summary+'\nPrior detailed chronology remains in devsummary and archived handoff `'+archives[1]+'`. The current full candidate manifest and public release receipt are authoritative; do not infer source identity from mutable build paths.\n')
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md','AGENTS.md']
secrets=[p.read_bytes().strip()for p in Path('output').glob('session-*/progress-state/message-access-key')]
for name in allow:
    data=Path(name).read_bytes()
    if Path(name).is_symlink()or any(len(s)>=24 and s in data for s in secrets)or re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data):raise RuntimeError('Unexpected publication material: '+name)
subprocess.run(['git','--no-pager','add','--',*allow],check=True);expected={n for n in allow if(git('show','HEAD:'+n,raw=True)if n in tracked else None)!=Path(n).read_bytes()}
if set(git('diff','--cached','--name-only').splitlines())!=expected:raise RuntimeError('Toolbar staging allowlist differs')
for name,digest in tracked_compiled.items():
    if hashlib.sha256(git('show',':'+name,raw=True)).hexdigest()!=digest:raise RuntimeError('Staged compiled source differs: '+name)
for row in evidence:
    if hashlib.sha256(git('show',':'+row['committed'],raw=True)).hexdigest()!=row['sha256']:raise RuntimeError('Evidence bytes differ')
subprocess.run(['git','--no-pager','diff','--cached','--check','--',*[n for n in allow if n not in raw]],check=True)
subprocess.run(['git','--no-pager','commit','-m','perf: retain unchanged browser chrome raster output'],check=True)
print(json.dumps({'iteration':entry['iteration'],'commit':git('rev-parse','HEAD'),'score':45,'nativePromoted':False,'candidateBinarySHA256':NATIVE,'trackedCompiledInputsMatched':len(tracked_compiled),'files':sorted(expected),'untouchedWorkingChanges':git('diff','--name-only').splitlines()},indent=2))
