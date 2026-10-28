"""Enable the independently verified exact task route and the planned Surge view."""
from datetime import datetime, timezone
import argparse, json, os, pathlib, signal, subprocess, time, urllib.request
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--restart',action='store_true');args=parser.parse_args()
ROOT=pathlib.Path.cwd();STATE=ROOT/'output/session-10290244/progress-state';PORT=18784
ORIGIN='https://4d3ee194969f5f51.czd5qzludboe.opentunnel.xyz'
SURGE='https://lume-10290244-gpt6-astra-pro.surge.sh'
def get(url,timeout=5):
    with urllib.request.urlopen(url,timeout=timeout)as response:return json.load(response)
def identity(pid):
    result=subprocess.run(['ps','-p',str(pid),'-o','lstart='],capture_output=True,text=True)
    return result.stdout.strip()if result.returncode==0 else None
def atomic(path,value):
    temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)
public=get(ORIGIN+'/api/health')
if not public.get('ok')or public.get('taskId')!='10290244':raise RuntimeError('Exact public route did not pass the task health check')
receipt=json.loads((STATE/'server-receipt.json').read_text())
local=get(f'http://127.0.0.1:{PORT}/api/health')
if not local.get('ok')or local.get('taskId')!='10290244':raise RuntimeError('Local task backend differs')
if not args.restart and receipt.get('publicURL')==ORIGIN and receipt.get('publicProgressVerified')and set(receipt.get('allowedOrigins',[]))=={ORIGIN,SURGE}:
    print(json.dumps({'alreadyConfigured':True,'publicURL':ORIGIN,'publicHealthVerified':True}));raise SystemExit(0)
old_pid=receipt['pid']
if receipt.get('startIdentity')and identity(old_pid)!=receipt['startIdentity']:raise RuntimeError('Recorded Node process start identity differs; no process was stopped')
command=subprocess.run(['ps','-p',str(old_pid),'-o','command='],capture_output=True,text=True).stdout
if 'tools/lume-progress-10290244/server.mjs'not in command:raise RuntimeError('Recorded Node PID identity differs; no process was stopped')
listener=subprocess.run(['lsof','-nP','-a','-p',str(old_pid),'-iTCP:'+str(PORT),'-sTCP:LISTEN'],capture_output=True,text=True)
if listener.returncode:raise RuntimeError('Recorded Node PID does not own the task port')
os.kill(old_pid,signal.SIGTERM)
until=time.monotonic()+5
while identity(old_pid)and time.monotonic()<until:time.sleep(.1)
listener=subprocess.run(['lsof','-nP','-iTCP:'+str(PORT),'-sTCP:LISTEN'],capture_output=True,text=True)
if listener.returncode==0:raise RuntimeError('Task port did not close; no unrelated listener was stopped')
env=dict(os.environ,PORT=str(PORT),LUME_PROGRESS_STATE=str(STATE),LUME_SOURCE_ARCHIVE=str(ROOT/'output/session-10290244/public/src.tar.gz'),LUME_ALLOWED_ORIGINS=ORIGIN+','+SURGE)
with (STATE/'server.log').open('a')as log:
    server=subprocess.Popen(['node','tools/lume-progress-10290244/server.mjs'],cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
receipt.update(pid=server.pid,startIdentity=identity(server.pid),publicURL=ORIGIN,allowedOrigins=[ORIGIN,SURGE],publicProgressVerified=False,serverAlive=True,tunnelProvider='opentunnel',recoveredUTC=datetime.now(timezone.utc).isoformat())
atomic(STATE/'server-receipt.json',receipt)
until=time.monotonic()+8
while time.monotonic()<until:
    try:
        local=get(f'http://127.0.0.1:{PORT}/api/health',1)
        if local.get('ok')and local.get('taskId')=='10290244':break
    except Exception:pass
    if server.poll()is not None:raise RuntimeError('Owned backend exited; inspect task server.log')
    time.sleep(.1)
progress=get(ORIGIN+'/api/progress')
if progress.get('status',{}).get('taskId')!='10290244':raise RuntimeError('Public progress identity differs after origin configuration')
receipt.update(publicProgressVerified=True,serverAlive=server.poll()is None,routeLog='output/session-10290244/progress-state/tunnel-serve-attempt-1.log',routeServicePID=31096)
receipt.pop('issue',None);atomic(STATE/'server-receipt.json',receipt)
print(json.dumps({'publicURL':ORIGIN,'pid':server.pid,'allowedOrigins':receipt['allowedOrigins'],'publicProgressVerified':True,'opentunnelServiceUnchanged':True},indent=2))
