"""Checkpoint the measured GPU upload improvement without promoting unverified artifact gates."""
from pathlib import Path
import datetime as dt, hashlib, json, os, re, subprocess
os.environ.update(GIT_PAGER='cat',PAGER='cat')
ROOT=Path.cwd();TASK='102900614';NATIVE='2dacdbcc36c5be7edeaf8027533be38d85fbefe06c717e172ee324c499f2ec03'
GPU='93f7e48a28806f6ac57e0e3e9c9b3b8f9d5cfa57c20b1328abe57d43e983888a';FIXTURE='5094bb02ce7f1c792ef1aa0964394b5a276c7181bc296befc9105e8a1e35eb7f'
def git(*args,raw=False):
    v=subprocess.check_output(['git','--no-pager',*args]);return v if raw else v.decode().strip()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text())
def write(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614':raise RuntimeError('Wrong branch')
if git('diff','--cached','--name-only'):raise RuntimeError('Other staged changes require coordination')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['history'][-1]['iteration']!='102900614-003' or score['score']!=40:raise RuntimeError('Global checkpoint sequence or accepted score changed')
for path,expected in [('src/gpu/gpu.c',GPU),('tests/test_gpu_present.c',FIXTURE)]:
    if sha(path)!=expected:raise RuntimeError('Reviewed GPU candidate changed: '+path)
greenpath=Path('output/session-102900614/gpu-callback-green-1/summary.json');green=read(greenpath)
if not green['passed'] or green['gpuSourceSHA256']!=GPU or green['fixtureSHA256']!=FIXTURE or green['artifacts']['lumen']['sha256']!=NATIVE or sha(green['artifacts']['lumen']['path'])!=NATIVE:raise RuntimeError('GPU candidate build evidence differs')
if len(green['stages'])!=4 or any(not row['passed']or row['durationSeconds']>300 for row in green['stages']):raise RuntimeError('GPU measured stages incomplete')
for name in ('gpu-presentation','asan-callback-lifetime'):
    row=next(row for row in green['stages']if row['name']==name)
    if 'GPU presentation: 68 checks, 0 failures'not in row['resultLines']:raise RuntimeError('Exact normal/ASan68-check gate absent')
compiled=read(green['compiledSourceManifest'])
for name,expected in compiled.items():
    actual=sha(name)if name=='src/gpu/gpu.c'else hashlib.sha256(git('show','HEAD:'+name,raw=True)).hexdigest()
    if actual!=expected:raise RuntimeError('Prospective GPU-only index differs from its compiled snapshot: '+name)
controlpath=Path('output/session-102900614/scroll-gpu-callback-control/summary.json');control=read(controlpath)
resourcepath=Path(control['resourceReceipt']);resource=read(resourcepath)
if control['binary']['sha256']!=NATIVE or control['changedCompiledSources']!=['src/gpu/gpu.c'] or not control['gpuClean']or control['gpuErrors']or not control['pixelIdentical']:raise RuntimeError('Real Metal/pixel/source isolation evidence is incomplete')
if sha(resourcepath)!='c4c6dcc41ce854a73ae2015978de083ffa29862acd4e83f1e8fe6be88c962012' or resource['binary_sha256']!=NATIVE or resource['binary_sha256_after']!=NATIVE:raise RuntimeError('Resource receipt identity changed')
if resource['natural_app_return_code']!=0 or resource['app_termination']['forced']or resource['sampler_termination']['forced']:raise RuntimeError('Resource run did not end naturally')
pixelpath=Path(control['pixelComparison']);pixels=read(pixelpath)
if not pixels['passed']or pixels['differingPagePixels']!=0 or pixels['candidate']['binarySHA256']!=NATIVE:raise RuntimeError('Exact page-pixel evidence differs')
baselinepath=Path('output/session-102900614/scroll-transfer-baseline/receipt.json');baseline=read(baselinepath)
old=baseline['phase_metrics']['scrolling'];new=resource['phase_metrics']['scrolling'];idle=resource['phase_metrics']['idle:after']
if new['cpu_p95_percent']!=18.997 or old['cpu_p95_percent']!=23.054 or new['cpu_p95_percent']>=old['cpu_p95_percent']or resource['accepted']:raise RuntimeError('Measured improvement or still-failing fixed threshold differs')
failedpath=Path('output/session-102900614/scroll-gpu-upload/receipt.json');failed=read(failedpath)
if failed['binary_sha256']!='b62a1720e52d91600fda327418dc3c733965c88550e5f4590f5d423d374efdee' or failed['natural_app_return_code']!=-6:raise RuntimeError('Failed first GPU candidate provenance differs')

names=('apply_gpu_upload_pool.py','install_gpu_upload_tests.py','repair_gpu_upload_callback.py','build_gpu_upload_candidate.py','run_gpu_scroll_candidate.py','run_gpu_scroll_candidate_v2_102900614.py','capture_task_window.py','task_window_inventory.c','compare_scroll_pixels.py','verify_window_capture_metadata.py','inventory_apple_artifacts_102900614.py')
tools=['tools/apple-session-gpt6as/'+name for name in names if Path('tools/apple-session-gpt6as/'+name).is_file()]
allow=['src/gpu/gpu.c','tests/test_gpu_present.c',*tools]
for path in allow:
    p=Path(path)
    if p.is_symlink():raise RuntimeError('Unexpected symlink in reviewed source')
    bad=[i+1 for i,line in enumerate(p.read_bytes().splitlines())if line.rstrip(b' \t')!=line]
    if bad:raise RuntimeError('Authored-source whitespace requires narrow review before recording checkpoint: '+path+' lines '+','.join(map(str,bad[:12])))
now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
folder=Path('agent/evidence/102900614-004');folder.mkdir(exist_ok=False)
for name,value in [('compiled-source-sha256.json',compiled),('gpu-build-regressions.json',green),('gpu-scroll-comparison.json',control),('pixel-comparison.json',pixels)]:
    path=folder/name;write(path,value);allow.append(str(path))
red_rows=[]
for path in sorted(Path('output/session-102900614').glob('gpu-callback-red*.json')):
    data=read(path);red_rows.append({'path':str(path),'sha256':sha(path),'started_utc':data.get('started_utc'),'finished_utc':data.get('finished_utc'),'duration_seconds':data.get('duration_s'),'natural_exit_code':data.get('natural_exit_code'),'interval_cpu_samples':data.get('interval_cpu_samples'),'collector_error':data.get('collector_error')})
if not any(row['natural_exit_code']==-6 for row in red_rows):raise RuntimeError('Strict pinned-backend crash RED monitor missing')
entry={'iteration':'102900614-004','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':40,'status':'GPU upload candidate reduces CPU with exact CPU-frame pixels;10% interaction gate remains failed','accepted_native_unchanged':score['current_baseline_binary_sha256'],'candidate_binary_sha256':NATIVE,'candidate_source_manifest':str(folder/'compiled-source-sha256.json'),'native_artifact_promoted':False,'build':{'source_receipt':str(greenpath),'sha256':sha(greenpath),'duration_seconds':green['durationSeconds'],'normal_checks':68,'asan_checks':68},'strict_backend_red':red_rows,'first_failed_candidate':{'binary_sha256':failed['binary_sha256'],'receipt':str(failedpath),'sha256':sha(failedpath),'duration_seconds':failed['duration_seconds'],'natural_exit_code':-6,'cause':'Installed wgpu-native exports wgpuBufferGetMapState but aborts when invoked; strict mock now reproduces that behavior.'},'resources':{'resource_receipt':str(resourcepath),'sha256':sha(resourcepath),'duration_seconds':resource['duration_seconds'],'baseline_scroll_cpu_mean_percent':old['cpu_mean_percent'],'candidate_scroll_cpu_mean_percent':new['cpu_mean_percent'],'mean_reduction_percent':round(100*(old['cpu_mean_percent']-new['cpu_mean_percent'])/old['cpu_mean_percent'],3),'baseline_scroll_cpu_p95_percent':old['cpu_p95_percent'],'candidate_scroll_cpu_p95_percent':new['cpu_p95_percent'],'p95_reduction_percent':round(100*(old['cpu_p95_percent']-new['cpu_p95_percent'])/old['cpu_p95_percent'],3),'fixed_scroll_p95_ceiling_percent':10,'interaction_accepted':False,'candidate_idle_p95_percent':idle['cpu_p95_percent'],'candidate_final_idle_rss_mib':idle['rss_end_mib'],'page_pixels_compared':pixels['pagePixelsCompared'],'differing_page_pixels':0,'pixel_scope':pixels['scope'],'native_window_capture_receipt_passed':control['nativeWindowCaptured'],'native_window_capture_metadata_issue':control.get('windowCaptureIssue')},'implementation':'Three bounded reusable MapWrite slots with reference-counted atomic callback tokens; no polling unsupported GetMapState, no frame/input throttling; fallback upload preserves presentation when slots unavailable.','source_tool_sha256':{name:sha(name)for name in tools},'limits':['Current accepted native remains733 until combined standards/resource acceptance is verified.','Zero CPU-frame pixel differences do not alone prove GPU scanout equivalence; window capture metadata failure remains recorded.','Fresh navigation soak and same signed IPA execution remain pending.'],'optional_local_preflight':{'started_utc':'2026-10-28T22:45:20.172Z','finished_utc':'2026-10-28T22:45:21.184Z','duration_seconds':1.012,'result':'Scratch-only compile lacked layout/layout.h and local ps failed; no Mac mutation, test pass or telemetry credit.'},'model_compute_duration':'Unavailable; parallel worker wall times are not summed'}
entrypath=Path('agent/evidence/102900614-004-gpu-upload.json');write(entrypath,entry);allow.append(str(entrypath))
score.setdefault('candidate_artifacts',[]).append({'iteration':entry['iteration'],'binary_sha256':NATIVE,'source_manifest':str(folder/'compiled-source-sha256.json'),'evidence':str(entrypath),'accepted':False,'note':'Measured GPU-only improvement; combined standards/fresh soak gates pending; fixed scrollingCPU gate stillfails.'})
score['history'].append({key:entry[key]for key in ('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:out.write(f"\n## 102900614-004 — bounded GPU upload reuse and callback lifetime repair\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; iteration `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Current acceptance **40/100**, prior50 historical; accepted native remains733.\n- First b62 candidate aborted after `{failed['duration_seconds']}s` because this pinned wgpu backend exports an unimplemented map-state query. A stricter regression reproduced the abort; reference-counted atomic completion tokens replace that query. The failed candidate and original receipts remain preserved.\n- Repaired candidate2dac: normal **68/68** and AddressSanitizer **68/68** pass; build/test stages total `{green['durationSeconds']}s`. Short1Hz-monitored checks do not establish steady-state CPU usage.\n- Unchanged controlled scroll run `{resource['duration_seconds']}s`: meanCPU `{old['cpu_mean_percent']}→{new['cpu_mean_percent']}%`; p95 `{old['cpu_p95_percent']}→{new['cpu_p95_percent']}%` (**{entry['resources']['p95_reduction_percent']}% lower**), still above fixed10%. Final idle p95 `{idle['cpu_p95_percent']}%`, settledRSS `{idle['rss_end_mib']}MiB`.\n- `{pixels['pagePixelsCompared']}` CPU-frame page pixels compare identically and all1875 fractional wheel inputs retain exact final position. Dynamic toolbar is explicitly excluded. The native-window capture metadata error remains separate; no invented scanout/PID proof.\n- Optional scratch-only preflight22:45:20.172–22:45:21.184Z failed from missing mirror header/ps, with no Mac mutation or performance credit.\n- Only GPU source/test and explicit reproduction helpers are committed; stream/raster working edits are excluded. Evidence `{entrypath}`. Parallel wall time is not summed; model compute duration unavailable.\n")
with Path('agent/continue.md').open('a')as out:out.write(f"\n## Checkpoint004 — measured GPU candidate\n\nCandidate `{green['artifacts']['lumen']['path']}` SHA-256 `{NATIVE}` has68/68 normal and68/68 ASan checks, no actual Metal errors, identical CPU-frame page pixels and scrollp95 `{new['cpu_p95_percent']}%` versus23.054%. Fixed10% still fails. Current accepted native733 and score40 remain unchanged until fresh combined gates. Originalb62 crash is retained. See `{entrypath}`.\n")
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md']
secrets=[p.read_bytes().strip()for p in Path('output').glob('session-*/progress-state/message-access-key')if p.is_file()]
for name in allow:
    data=Path(name).read_bytes()
    if any(len(secret)>=24 and secret in data for secret in secrets)or re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data):raise RuntimeError('Private material in proposed source: '+name)
subprocess.run(['git','--no-pager','add','--',*allow],check=True)
if set(git('diff','--cached','--name-only').splitlines())!=set(allow):raise RuntimeError('GPU checkpoint index allowlist differs')
for name,expected in compiled.items():
    if hashlib.sha256(git('show',':'+name,raw=True)).hexdigest()!=expected:raise RuntimeError('GPU index changed after review: '+name)
subprocess.run(['git','--no-pager','diff','--cached','--check'],check=True)
subprocess.run(['git','--no-pager','commit','-m','perf: reuse bounded Metal upload buffers with safe callbacks'],check=True)
print(json.dumps({'iteration':entry['iteration'],'commit':git('rev-parse','HEAD'),'score':score['score'],'nativePromoted':False,'candidateBinarySHA256':NATIVE,'files':allow,'untouchedWorkingChanges':git('diff','--name-only').splitlines(),'totalSessionSeconds':entry['total_session_seconds']},indent=2))
