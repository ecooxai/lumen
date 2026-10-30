"""Checkpoint measured rectangle spans and glyph reuse; keep resource acceptance pending."""
from pathlib import Path
import datetime as dt, hashlib, json, os, re, shutil, subprocess
os.environ.update(GIT_PAGER='cat', PAGER='cat')
ROOT=Path.cwd(); BASE=Path('output/session-102900614')
NATIVE='4773b85d28d3b1a7c17a1beee5225abc5141bb4cec4b713d409464ebe42582e9'
SPAN='970e6cafc95133ca62ec02f7c9deeaffe568f276dbafda39d117c4f04fc9217b'
SOURCE={'src/paint/raster.c':'ab8490489c5c3f670d903ee0959ded347464b5b3bcc577007db164d0012cedf1','tests/test_render_overdraw.c':'c46fb31407ef24b9fa050b1aba964fb34d6cb707eb4be48c02f54a5806fe924f'}
def git(*args,raw=False):
    value=subprocess.check_output(['git','--no-pager',*args]);return value if raw else value.decode().strip()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text())
def write(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614' or git('diff','--cached','--name-only'):raise RuntimeError('Branch/index coordination required')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['history'][-1]['iteration']!='102900614-007' or score['score']!=45:raise RuntimeError('Checkpoint008 order/score changed')
for name,digest in SOURCE.items():
    if sha(name)!=digest:raise RuntimeError('Reviewed renderer candidate changed: '+name)
names=('raster-span-reference-1','raster-span-candidate-1','glyph-cache-reference-1','glyph-cache-candidate-1')
builds={name:read(BASE/name/'summary.json') for name in names};green=builds['glyph-cache-candidate-1'];red=builds['glyph-cache-reference-1'];span=builds['raster-span-candidate-1']
if not green['passed'] or green['rasterSHA256']!=SOURCE['src/paint/raster.c'] or green['fixtureSHA256']!=SOURCE['tests/test_render_overdraw.c'] or green['artifacts']['lumen']['sha256']!=NATIVE:raise RuntimeError('Glyph candidate identity differs')
if red['passed'] or red['fixtureSHA256']!=green['fixtureSHA256'] or red['artifacts']['lumen']['sha256']!=SPAN:raise RuntimeError('Frozen glyph RED differs')
if 'RESULTS render-overdraw FAIL (95/97 checks)' not in red['stages'][1]['resultLines'] or 'RESULTS render-overdraw PASS (97/97 checks)' not in green['stages'][1]['resultLines']:raise RuntimeError('Exact glyph red/green checks differ')
if not span['passed'] or span['artifacts']['lumen']['sha256']!=SPAN or any('RESULTS render-overdraw PASS (93/93 checks)' not in builds[name]['stages'][1]['resultLines'] for name in names[:2]):raise RuntimeError('Span oracle reference/candidate differ')
for name,build in builds.items():
    for artifact in build['artifacts'].values():
        if sha(artifact['path'])!=artifact['sha256']:raise RuntimeError('Immutable renderer artifact changed: '+name)
    for stage in build['stages']:
        monitor=read(stage['monitor'])
        expected_exit=1 if name=='glyph-cache-reference-1' and stage['name']=='pixel-equivalence' else 0
        if monitor.get('natural_exit_code')!=expected_exit or monitor.get('collector_error') or monitor.get('ended_by_monitor'):raise RuntimeError('Renderer build/test telemetry differs: '+name)
        if sha(stage['monitor'])!=stage['monitorSHA256'] or sha(stage['log'])!=stage['logSHA256']:raise RuntimeError('Renderer stage receipt changed')
compiledpath=Path(green['compiledSourceManifest']);compiled=read(compiledpath)
tracked=set(git('ls-files','-z',raw=True).decode().split('\0'));tracked_compiled={k:v for k,v in compiled.items() if k in tracked}
for name,digest in tracked_compiled.items():
    actual=sha(name) if name in SOURCE else hashlib.sha256(git('show','HEAD:'+name,raw=True)).hexdigest()
    if actual!=digest:raise RuntimeError('Prospective renderer-only index differs from compiled source: '+name)
for name,digest in compiled.items():
    if name in tracked:continue
    if '/output/' not in name and name.startswith('src/'):raise RuntimeError('Untracked production input needs explicit review: '+name)
    if sha(name)!=digest:raise RuntimeError('Recorded ignored dependency/source inventory changed: '+name)
controls={name:read(BASE/(name+'-control')/'summary.json') for name in ('scroll-raster-span','scroll-glyph-cache')}
resources={name:read(controls[name]['resourceReceipt']) for name in controls}
for name,control in controls.items():
    expected=SPAN if name=='scroll-raster-span' else NATIVE
    receipt=resources[name]
    if control['binary']['sha256']!=expected or control['changedCompiledSources']!=['src/paint/raster.c'] or control['changedCompiledDependencies']!=['src/paint/raster.c'] or not control['pixelIdentical'] or not control['nativeWindowCaptured'] or not control['gpuClean'] or control['gpuErrors']:raise RuntimeError('Native pixel/source/GPU proof incomplete: '+name)
    if receipt['binary_sha256']!=expected or receipt['binary_sha256_after']!=expected or receipt['natural_app_return_code']!=0 or receipt['app_termination']['forced'] or receipt['sampler_termination']['forced']:raise RuntimeError('Resource identity/natural cleanup differs')
    if receipt['accepted'] or receipt['phase_metrics']['scrolling']['cpu_p95_percent']<=10:raise RuntimeError('Expected failed fixed10% resource gate changed')
    pixels=read(control['pixelComparison'])
    if not pixels['passed'] or pixels['differingPagePixels']!=0:raise RuntimeError('Exact page pixels differ')
    if sha(control['resourceReceipt'])!=control['resourceReceiptSHA256']:raise RuntimeError('Resource receipt hash differs')
if sha(controls['scroll-glyph-cache']['resourceReceipt'])!='29b2175a2f2a3fae08dcf53b813d9e317a782e5728fe77b4365b368e19592b45':raise RuntimeError('Reviewed final glyph resource receipt changed')
reference=read(BASE/'scroll-raster-reference/receipt.json');old=reference['phase_metrics']['scrolling'];mid=resources['scroll-raster-span']['phase_metrics']['scrolling'];new=resources['scroll-glyph-cache']['phase_metrics']['scrolling'];idle=resources['scroll-glyph-cache']['phase_metrics']['idle:after']
if (old['cpu_p95_percent'],mid['cpu_p95_percent'],new['cpu_p95_percent'])!=(20.175,16.935,12.06):raise RuntimeError('Reviewed paired CPU results differ')
if not controls['scroll-glyph-cache']['baselineByteIdentity']['sameBytesAtDistinctSnapshotPaths']:raise RuntimeError('Glyph baseline byte identity absent')
source_tools=['tools/apple-session-gpt6as/'+name for name in ('apply_raster_span.py','install_raster_span_tests.py','apply_glyph_cache.py','install_glyph_cache_tests.py','build_raster_span_candidate.py','run_glyph_cache_gate_102900614.py')]
for name in source_tools:
    if not Path(name).is_file():raise RuntimeError('Required reproduction helper missing: '+name)
review=Path('agent/gpt6-astra-pro_builtin-agents_devinmac3d_raster_review_102900614.md')
if not review.is_file():raise RuntimeError('Independent renderer review is not installed')
allow=[*SOURCE,*source_tools,str(review)]
subprocess.run(['git','--no-pager','diff','--check','--',*allow],check=True)
for name in allow:
    if name not in tracked and any(line.rstrip(b' \t')!=line for line in Path(name).read_bytes().splitlines()):raise RuntimeError('Authored helper whitespace requires correction: '+name)
# All validation precedes document/index mutation. Exact raw logs/CSV are preserved,
# and only those verified copies are exempted from authored-text whitespace lint.
folder=Path('agent/evidence/102900614-008');folder.mkdir(exist_ok=False)
copy_paths=[]
for name,build in builds.items():
    copy_paths += [BASE/name/'summary.json',Path(build['compiledSourceManifest']),Path(build['compiledDependencyManifest'])]
    for stage in build['stages']:copy_paths += [Path(stage['monitor']),Path(stage['log'])]
for name,control in controls.items():
    copy_paths += [BASE/(name+'-control')/'summary.json',Path(control['resourceReceipt']),Path(control['pixelComparison']),Path(control['windowCaptureReceipt'])]
copy_paths += [BASE/'scroll-raster-reference/receipt.json']
evidence=[];raw_copies=[]
for index,path in enumerate(dict.fromkeys(copy_paths)):
    destination=folder/('%02d-'%index+path.parent.name+'-'+path.name);shutil.copyfile(path,destination)
    if sha(path)!=sha(destination):raise RuntimeError('Evidence copy differs')
    allow.append(str(destination));evidence.append({'source':str(path),'committed':str(destination),'sha256':sha(path),'bytes':path.stat().st_size})
    if path.suffix in ('.log','.csv'):raw_copies.append(str(destination))
manifestpath=folder/'tracked-compiled-source-sha256.json';write(manifestpath,tracked_compiled);allow.append(str(manifestpath))
now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
entry={'iteration':'102900614-008','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':45,'status':'Bounded span and glyph reuse improve fixed scrolling CPU;12.060% still exceeds10% gate','native_artifact_promoted':False,'accepted_native_unchanged':score['current_baseline_binary_sha256'],'candidate_binary_sha256':NATIVE,'candidate_source_manifest':str(manifestpath),'compiled_tracked_inputs':len(tracked_compiled),'ignored_inventory_inputs':len(compiled)-len(tracked_compiled),'source_sha256':SOURCE,'implementation':'Whole-span fractional rectangle blending preserves exact coverage; a bounded glyph lookup cache reuses persistent font-owned glyph pointers. No event/input/frame throttling or acceptance threshold changes.','unit_checks':{'span_reference':93,'span_candidate':93,'span_independent_cases':12000,'glyph_red_passed':95,'glyph_red_total':97,'glyph_green_passed':97,'glyph_green_total':97,'glyph_independent_text_cases':7168},'build_and_test_seconds':{name:build['durationSeconds'] for name,build in builds.items()},'resources':{'reference_binary_sha256':reference['binary_sha256'],'span_binary_sha256':SPAN,'glyph_binary_sha256':NATIVE,'reference_scroll_p95_percent':old['cpu_p95_percent'],'span_scroll_p95_percent':mid['cpu_p95_percent'],'glyph_scroll_p95_percent':new['cpu_p95_percent'],'glyph_scroll_mean_percent':new['cpu_mean_percent'],'p95_reduction_from_same_stream_baseline_percent':round(100*(old['cpu_p95_percent']-new['cpu_p95_percent'])/old['cpu_p95_percent'],3),'fixed_scroll_p95_ceiling_percent':10,'accepted':False,'idle_p95_percent':idle['cpu_p95_percent'],'idle_rss_start_mib':idle['rss_start_mib'],'idle_rss_end_mib':idle['rss_end_mib'],'glyph_duration_seconds':controls['scroll-glyph-cache']['durationSeconds'],'page_pixels_identical':True,'native_window_captured':True,'gpu_clean':True},'evidence':evidence,'raw_byte_preservation':raw_copies,'limits':['Current accepted733 and score45 remain unchanged; fresh combined standards and navigation soak gates remain pending.','Fixed10% scrollingCPU acceptance still fails; idle/CPU results belong only to the exact measured binaries.','The earlier b62 GPU abort and all previous raw failures remain preserved in checkpoint004.','Same signed IPA execution on Mac/iOS/iPad remains unproved.'],'model_compute_duration':'Unavailable; parallel worker wall times are not summed'}
entrypath=Path('agent/evidence/102900614-008-raster-glyph.json');write(entrypath,entry);allow.append(str(entrypath))
score.setdefault('candidate_artifacts',[]).append({'iteration':entry['iteration'],'binary_sha256':NATIVE,'source_manifest':str(manifestpath),'evidence':str(entrypath),'accepted':False,'note':'Paired span/glyph resource improvement with exact pixels and native capture;12.060% still exceeds fixed10% gate.'})
score['history'].append({key:entry[key] for key in ('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();write(scorepath,score)
with Path('agent/devsummary.md').open('a') as out:out.write(f"\n## 102900614-008 — exact span blending and bounded glyph reuse\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; checkpoint `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Accepted score **45/100** and native733 remain unchanged.\n- Fractional rectangle oracle **93/93**, including12,000 independent coverage/alpha/clipping cases. Glyph reuse frozen fixture **95/97 RED →97/97 GREEN**, including7,168 independent text cases. Existing V8 binaries reused; each build/test stage monitored at1Hz. Short checks provide no steady-state CPU credit.\n- Same-stream paired native scroll p95 **20.175% →16.935% →12.060%**; glyph mean **9.912%**, final idle p95 **0.991%**, settledRSS **153.203MiB**, stable across the measured idle phase. Fixed **10% scrolling gate still fails**. Final workload117.419s; exact page pixels, scoped native-window capture and GPU errors all pass.\n- Source and compiler dependency comparisons differ only in raster.c; glyph reference bytes exactly equal the prior measured span binary. Persistent font-owned glyph allocations were independently reviewed; the bounded lookup cache does not own/free them.\n- Combined candidate4773 remains unpromoted pending fresh combined standards/resource/soak checks. Prior failedGPU receipts remain preserved; no historical performance numbers are transferred to a new binary. Evidence `{entrypath}`. Parallel worker wall times are not summed.\n")
with Path('agent/continue.md').open('a') as out:out.write(f"\n## Checkpoint008 — measured span/glyph candidate\n\nNative `{NATIVE}` passes97/97 raster checks, exact paired page pixels and scoped native capture. Scroll p95 is12.060%, improved from same-stream20.175%, but still fails fixed10%. Idle p95.991%, stable153.203MiB belong only to this exact binary. Current accepted733/score45 unchanged. Source manifest, byte-identical reference and all timing/evidence are in `{entrypath}`.\n")
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md']
secrets=[path.read_bytes().strip() for path in Path('output').glob('session-*/progress-state/message-access-key')]
for name in allow:
    path=Path(name);data=path.read_bytes()
    if path.is_symlink() or any(len(secret)>=24 and secret in data for secret in secrets) or re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data):raise RuntimeError('Private or unexpected publication material: '+name)
subprocess.run(['git','--no-pager','add','--',*allow],check=True)
if set(git('diff','--cached','--name-only').splitlines())!=set(allow):raise RuntimeError('Renderer exact staging allowlist differs')
for name,digest in tracked_compiled.items():
    if hashlib.sha256(git('show',':'+name,raw=True)).hexdigest()!=digest:raise RuntimeError('Renderer-only staged production source differs: '+name)
for row in evidence:
    if hashlib.sha256(git('show',':'+row['committed'],raw=True)).hexdigest()!=row['sha256']:raise RuntimeError('Staged raw evidence differs')
subprocess.run(['git','--no-pager','diff','--cached','--check','--',*[name for name in allow if name not in raw_copies]],check=True)
subprocess.run(['git','--no-pager','commit','-m','perf: reuse glyph lookups and blend fractional rectangle spans'],check=True)
print(json.dumps({'iteration':entry['iteration'],'commit':git('rev-parse','HEAD'),'score':45,'nativePromoted':False,'candidateBinarySHA256':NATIVE,'trackedCompiledInputsMatched':len(tracked_compiled),'files':allow,'untouchedWorkingChanges':git('diff','--name-only').splitlines(),'totalSessionSeconds':entry['total_session_seconds']},indent=2))
