"""Start a separate task identity using the existing Lume MVC progress server."""
import argparse, datetime, json, os, pathlib, re, socket, subprocess, time, urllib.parse, urllib.request

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--task-id',required=True)
parser.add_argument('--port',type=int,default=18785)
args=parser.parse_args()
TASK=args.task_id
if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',TASK):raise RuntimeError('Invalid task identifier')
ROOT=pathlib.Path.cwd();BASE=ROOT/('output/session-'+TASK);STATE=BASE/'progress-state'
STATE.mkdir(parents=True,exist_ok=True);os.chmod(STATE,0o700)
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
status=json.loads((STATE/'status.json').read_text())
if status.get('taskId')!=TASK:raise RuntimeError('Write a matching measured task status before launch')
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def atomic(path,value):
    temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)
def health(url,timeout=3):
    with urllib.request.urlopen(url+'/api/health',timeout=timeout)as response:return json.load(response)
with socket.socket()as probe:
    try:probe.bind(('127.0.0.1',args.port))
    except OSError:raise RuntimeError('Requested progress port is in use; no existing process was changed')
environment=dict(os.environ)
environment.update(PORT=str(args.port),LUME_TASK_ID=TASK,LUME_PROGRESS_STATE=str(STATE),
                   LUME_SCORECARD=str(ROOT/('agent/scorecard-'+TASK+'.json')),
                   LUME_SOURCE_ARCHIVE=str(BASE/'public/src.tar.gz'))
log=(STATE/'server.log').open('a')
def launch(origins=''):
    env=dict(environment,LUME_ALLOWED_ORIGINS=origins)
    process=subprocess.Popen(['node','tools/lume-progress-10290244/server.mjs'],cwd=ROOT,env=env,
                             stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
    for _ in range(50):
        if process.poll()is not None:raise RuntimeError('Progress server exited; inspect private server.log')
        try:
            value=health('http://127.0.0.1:'+str(args.port),1)
            if value.get('ok')and value.get('taskId')==TASK:return process
        except Exception:pass
        time.sleep(.1)
    process.terminate();process.wait(timeout=5)
    raise RuntimeError('Task progress server startup deadline exceeded')
server=launch()
receipt={'taskId':TASK,'startedUTC':utc(),'pid':server.pid,'port':args.port,'stateDirectory':str(STATE.relative_to(ROOT)),
         'sourceCommit':head,'localHealth':True,'publicURL':None,'messageKeyPublished':False,
         'routeLog':str((STATE/'tunnel-route.log').relative_to(ROOT))}
atomic(STATE/'server-receipt.json',receipt)
cli=ROOT/'output/session-20261029/bin/opentunnel'
route_env=dict(os.environ,XDG_DATA_HOME=str(STATE/'tunnel-data'),XDG_CONFIG_HOME=str(STATE/'tunnel-config'),XDG_CACHE_HOME=str(STATE/'tunnel-cache'))
try:
    began=time.monotonic()
    result=subprocess.run([str(cli),'--profile','lume-progress-'+TASK,'route','add',str(args.port)],cwd=ROOT,
                          env=route_env,text=True,capture_output=True,timeout=30)
    route_log=result.stdout+result.stderr
    (STATE/'tunnel-route.log').write_text(route_log)
    receipt.update(routeExitCode=result.returncode,routeSeconds=round(time.monotonic()-began,3))
    if result.returncode:raise RuntimeError('HTTPS route command failed; private log retained')
    candidates=[]
    for raw in re.findall(r'https://[^\s\"\'<>]+',route_log):
        parsed=urllib.parse.urlsplit(raw.rstrip('.,;)'))
        if parsed.username or parsed.password:continue
        origin=parsed.scheme+'://'+parsed.netloc
        if origin not in candidates:candidates.append(origin)
    for origin in candidates[:3]:
        try:
            value=health(origin,4)
            if value.get('ok')and value.get('taskId')==TASK:receipt['publicURL']=origin;break
        except Exception:pass
    if not receipt['publicURL']:raise RuntimeError('No returned HTTPS route passed matching task health')
    server.terminate();server.wait(timeout=5)
    origins=receipt['publicURL']+',https://lume-'+TASK+'-gpt6-astra-pro.surge.sh'
    server=launch(origins)
    receipt.update(pid=server.pid,allowedOrigins=origins.split(','))
    with urllib.request.urlopen(receipt['publicURL']+'/api/progress',timeout=8)as response:public=json.load(response)
    if public.get('status',{}).get('taskId')!=TASK or public.get('status',{}).get('wpt')!=status.get('wpt'):
        raise RuntimeError('Public task evidence differs from the measured local receipt')
    receipt['publicProgressVerified']=True
except Exception as error:
    receipt['issue']=type(error).__name__+': '+str(error)
finally:
    receipt['serverAlive']=server.poll()is None;receipt['finishedUTC']=utc()
    atomic(STATE/'server-receipt.json',receipt);log.close()
print(json.dumps(receipt,indent=2))
raise SystemExit(0 if receipt.get('publicProgressVerified') else 1)
