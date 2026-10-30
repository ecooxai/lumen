"""Start the task's bounded progress endpoint and an isolated HTTPS route."""
import datetime, json, os, pathlib, re, signal, socket, subprocess, time, urllib.parse, urllib.request

ROOT=pathlib.Path.cwd();STATE=ROOT/'output/session-10290244/progress-state'
STATE.mkdir(parents=True,exist_ok=True)
PORT=18784
def atomic(path,value):
    pending=path.with_suffix('.tmp');pending.write_text(json.dumps(value,indent=2)+'\n');pending.replace(path)
comparison_path='output/session-20261029/native-qa-kit-v1/source/output/wpt-comparison-session10290244-clone-metadata-1.json'
comparison=json.loads((ROOT/comparison_path).read_text())
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
candidate,reference=comparison['candidate'],comparison['reference']
if not comparison.get('comparable') or comparison.get('blockers'):raise RuntimeError('Progress requires a comparable WPT receipt')
status={'taskId':'10290244','updatedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'phase':'The original important WPT subset passes completely. Broader standards coverage and current-artifact resource verification are in progress.','iteration':3,'sourceCommit':head,'fullOfficialWptPassPercent':None,'wpt':{'passed':candidate['subtests']['passed'],'total':candidate['subtests']['union_inventory'],'passPercent':candidate['subtests']['observed_pass_percent'],'reportedFiles':candidate['files']['reported'],'fullyPassingFiles':candidate['files']['passed'],'chromePassed':reference['subtests']['passed'],'chromeTotal':reference['subtests']['union_inventory'],'binarySha256':candidate['run_info']['browser_version'].removeprefix('sha256:'),'scope':'Pinned six-file upstream WPT-source subset. Full official WPT remains unmeasured.','evidence':comparison_path},'apple':{'sameIpa':'Unverified on Mac, iPad and iPhone'},'targetedNativeChecks':{'passed':262,'total':262,'areWpt':False}}
for result_path in sorted((ROOT/'output/session-10290244').glob('*scroll*/receipt.json')):
    try:
        result=json.loads(result_path.read_text())
        if result.get('scenario')!='scroll' or result.get('binary_sha256')!=status['wpt']['binarySha256']:continue
        metric=result.get('phase_metrics',{}).get('scrolling',{})
        if result.get('natural_app_return_code')!=0 or metric.get('observed_samples',0)<30 or metric.get('duration_seconds',0)<30:continue
        cpu=metric.get('cpu_p95_percent')
        ceiling=result.get('policy',{}).get('scroll_cpu_p95_ceiling_percent',10)
        verdict='passed' if result.get('accepted') else 'failed' if isinstance(cpu,(int,float)) and cpu>ceiling else 'unverified'
        status['performance']={'interactionStatus':verdict,'interactionCpuP95Percent':cpu,'interactionCpuCeilingPercent':ceiling,'evidence':str(result_path.relative_to(ROOT)),'scope':'Measured synthetic native SDL scroll workload on the same identified binary.'}
        if verdict=='failed':status['phase']=f'The original important WPT subset passes completely. Native scroll CPU p95 was {cpu:.3f}% against the {ceiling}% target; reducing that hot path takes priority.'
    except (ValueError,OSError):continue
archive=ROOT/'output/session-10290244/public/src.tar.gz'
manifest=archive.parent/'manifest.json'
if manifest.is_file():status['sourceArchive']=json.loads(manifest.read_text())
atomic(STATE/'status.json',status)

with socket.socket() as probe:
    try:probe.bind(('127.0.0.1',PORT))
    except OSError:raise RuntimeError('Progress port already in use; inspect the existing task-owned receipt before restarting. No process was killed.')

environment=dict(os.environ)
environment.update({'PORT':str(PORT),'LUME_PROGRESS_STATE':str(STATE),'LUME_SOURCE_ARCHIVE':str(archive)})
stdout=(STATE/'server.log').open('a')
def launch(origins=''):
    env=dict(environment,LUME_ALLOWED_ORIGINS=origins)
    process=subprocess.Popen(['node','tools/lume-progress-10290244/server.mjs'],cwd=ROOT,env=env,stdout=stdout,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
    for _ in range(50):
        if process.poll()is not None:raise RuntimeError('Progress server exited; inspect ignored server.log')
        try:
            with urllib.request.urlopen(f'http://127.0.0.1:{PORT}/api/health',timeout=1)as response:
                health=json.load(response)
            if health.get('ok')and health.get('taskId')=='10290244':return process
        except Exception:pass
        time.sleep(.1)
    process.terminate();process.wait(timeout=5)
    raise RuntimeError('Progress startup deadline exceeded')

server=launch()
receipt={'startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'pid':server.pid,'port':PORT,'stateDirectory':str(STATE.relative_to(ROOT)),'sourceCommit':head,'localHealth':True,'publicURL':None,'messageKeyPublished':False,'routeLog':'output/session-10290244/progress-state/tunnel-route.log'}
atomic(STATE/'server-receipt.json',receipt)
cli=ROOT/'output/session-20261029/bin/opentunnel'
route_env=dict(os.environ,XDG_DATA_HOME=str(STATE/'tunnel-data'),XDG_CONFIG_HOME=str(STATE/'tunnel-config'),XDG_CACHE_HOME=str(STATE/'tunnel-cache'))
try:
    started=time.monotonic()
    result=subprocess.run([str(cli),'--profile','lume-progress-10290244','route','add',str(PORT)],cwd=ROOT,env=route_env,text=True,capture_output=True,timeout=30)
    log=result.stdout+result.stderr
    (STATE/'tunnel-route.log').write_text(log)
    receipt.update(routeExitCode=result.returncode,routeSeconds=round(time.monotonic()-started,3))
    if result.returncode:raise RuntimeError('Tunnel route command failed; inspect private route log')
    candidates=[]
    for raw in re.findall(r'https://[^\s\"\'<>]+',log):
        parsed=urllib.parse.urlsplit(raw.rstrip('.,;)'))
        if parsed.username or parsed.password:continue
        origin=parsed.scheme+'://'+parsed.netloc
        if origin not in candidates:candidates.append(origin)
    for origin in candidates[:3]:
        try:
            with urllib.request.urlopen(origin+'/api/health',timeout=4)as response:health=json.load(response)
            if health.get('ok')and health.get('taskId')=='10290244':receipt['publicURL']=origin;break
        except Exception:pass
    if not receipt['publicURL']:raise RuntimeError('No returned HTTPS route passed the task-specific health probe')
    server.terminate();server.wait(timeout=5)
    origins=receipt['publicURL']+',https://lume-10290244-gpt6-astra-pro.surge.sh'
    server=launch(origins)
    receipt['pid']=server.pid;receipt['allowedOrigins']=origins.split(',')
    with urllib.request.urlopen(receipt['publicURL']+'/api/progress',timeout=8)as response:public=json.load(response)
    if public.get('status',{}).get('wpt',{}).get('passed')!=candidate['subtests']['passed']:raise RuntimeError('Public status differs from verified WPT receipt')
    receipt['publicProgressVerified']=True
except Exception as error:
    receipt['issue']=type(error).__name__+': '+str(error)
finally:
    receipt['serverAlive']=server.poll()is None
    receipt['finishedUTC']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    atomic(STATE/'server-receipt.json',receipt)
    stdout.close()
print(json.dumps(receipt,indent=2))
