from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import urllib.request

root=Path.cwd().resolve()
task='20261029-cpu30';old_task='102900614';port=18785
origin='https://dd1cacbcfa84c17d.q4d3dgcpeeoq.opentunnel.xyz'
surge='https://lume-102900614-gpt6-astra-pro.surge.sh'
old_state=root/('output/session-'+old_task+'/progress-state')
base=root/('output/session-'+task);state=base/'progress-state'
old_pid=12706
expected_start='Wed Oct 28 16:04:40 2026'
expected_args='node tools/lume-progress-10290244/server.mjs'
utc=lambda:datetime.now(timezone.utc).isoformat()
def sha(p):
    with Path(p).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def write(path,value):
    pending=path.with_suffix(path.suffix+'.tmp');pending.write_text(json.dumps(value,indent=2)+'\n');pending.chmod(0o600);pending.replace(path)
def command(args):return subprocess.check_output(args,text=True,stderr=subprocess.DEVNULL,timeout=5).strip()
def identity(pid):
    return {'pid':pid,'start':command(['ps','-p',str(pid),'-o','lstart=']),
        'args':command(['ps','-ww','-p',str(pid),'-o','args=']),
        'cwd':command(['lsof','-a','-p',str(pid),'-d','cwd','-Fn'])}
def get(base_url,endpoint='/api/health',timeout=8):
    with urllib.request.urlopen(base_url+endpoint,timeout=timeout) as response:return json.load(response)
def copy_inbox(source_state=old_state,target_state=state):
    old=source_state/'messages';new=target_state/'messages';new.mkdir(parents=True,exist_ok=True);new.chmod(0o700)
    rows=[];pending=0
    for p in sorted(old.glob('*.json')):
        if p.is_symlink() or p.stat().st_size>65536 or not re.fullmatch(r'[a-f0-9-]{36}\.json',p.name):raise RuntimeError('Unexpected existing task inbox file')
        payload=p.read_bytes();data=json.loads(payload)
        if not data.get('reply'):pending+=1
        target=new/p.name
        temporary=target.with_suffix('.copytmp');temporary.write_bytes(payload);temporary.chmod(0o600);temporary.replace(target)
        if sha(target)!=sha(p):raise RuntimeError('Copied message bytes differ')
        rows.append([p.name,sha(p)])
    return {'copiedMessages':len(rows),'pendingMessages':pending,'inventorySHA256':hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest()}
def launch(which_task,which_state,allowed_origins,log):
    env=dict(os.environ,PORT=str(port),LUME_TASK_ID=which_task,LUME_PROGRESS_STATE=str(which_state),
       LUME_SCORECARD=str(root/('agent/scorecard-'+which_task+'.json')),
       LUME_SOURCE_ARCHIVE=str(root/('output/session-'+which_task+'/public/src.tar.gz')),
       LUME_ALLOWED_ORIGINS=','.join(allowed_origins),LUME_MESSAGE_SESSION_ID=old_task)
    process=subprocess.Popen(['node','tools/lume-progress-10290244/server.mjs'],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
    began=time.monotonic()
    while time.monotonic()-began<12:
        if process.poll() is not None:raise RuntimeError('Task server exited before readiness')
        try:
            health=get('http://127.0.0.1:'+str(port),timeout=1)
            if health.get('ok') and health.get('taskId')==which_task:
                listener=command(['lsof','-nP','-a','-p',str(process.pid),'-iTCP:'+str(port),'-sTCP:LISTEN'])
                if '127.0.0.1:'+str(port) in listener:return process
        except Exception:pass
        time.sleep(.2)
    process.terminate();process.wait(timeout=8);raise RuntimeError('Task server readiness deadline exceeded')

receipt={'taskId':task,'startedUTC':utc(),'publicURL':origin,'surgeURL':surge,'oldTask':old_task,'oldPID':old_pid,
 'scope':'Reuse the existing task HTTPS route and private capability; preserve prior status/archive and all stored messages. Only the exact verified task server is replaced.',
 'switched':False,'rolledBack':False,'privateKeysOmitted':True}
old_stopped=False;new_server=None;token=None
state.mkdir(parents=True,exist_ok=True);state.chmod(0o700)
log=(state/'server-switch.log').open('a');os.chmod(state/'server-switch.log',0o600)
try:
    status=json.loads((state/'status.json').read_text())
    if status.get('taskId')!=task or status.get('fullOfficialWptPassPercent') is not None:raise RuntimeError('Current task status identity or WPT scope is invalid')
    current=status.get('currentWpt')
    if not isinstance(current,dict) or current.get('scheduled')!=1515 or current.get('unrun')!=1515-current.get('recorded',-1) or not 0<=current.get('fullyPassing',-1)<=current.get('recorded',-1)<=1515:
        raise RuntimeError('Current-artifact frozen WPT inventory is incomplete')
    if not re.fullmatch(r'[a-f0-9]{64}',current.get('binarySha256','')):raise RuntimeError('Current native artifact identity missing')
    if not re.fullmatch(r'[a-f0-9]{40}',current.get('sourceCommit','')):raise RuntimeError('Frozen native build source identity missing')
    score=json.loads((root/('agent/scorecard-'+task+'.json')).read_text())
    if score.get('task_id')!=task:raise RuntimeError('Scorecard task mismatch')
    manifest=json.loads((base/'public/manifest.json').read_text())
    head=command(['git','rev-parse','HEAD'])
    if manifest.get('sourceCommit')!=head or sha(base/'public/src.tar.gz')!=manifest.get('sha256'):raise RuntimeError('Package must match current committed source before live switch')
    receipt['sourceCommit']=head;receipt['sourceArchiveSHA256']=manifest['sha256']
    old_receipt=json.loads((old_state/'server-receipt.json').read_text())
    if old_receipt.get('pid')!=old_pid or old_receipt.get('port')!=port or old_receipt.get('publicURL')!=origin:raise RuntimeError('Prior task server receipt identity changed')
    observed=identity(old_pid)
    if observed['start']!=expected_start or observed['args']!=expected_args or 'n'+str(root) not in observed['cwd'].splitlines():raise RuntimeError('PID start/command/cwd identity changed; no process was stopped')
    listeners=command(['lsof','-nP','-a','-p',str(old_pid),'-iTCP:'+str(port),'-sTCP:LISTEN'])
    if '127.0.0.1:'+str(port) not in listeners:raise RuntimeError('Task PID is not the expected loopback listener')
    for url in ['http://127.0.0.1:'+str(port),origin]:
        health=get(url)
        if health.get('taskId')!=old_task or not health.get('ok'):raise RuntimeError('Prior task health identity differs')
    key=old_state/'message-access-key'
    if key.is_symlink():raise RuntimeError('Private capability file must be a regular task file')
    token=key.read_text().strip()
    if not re.fullmatch(r'[a-f0-9]{48}',token):raise RuntimeError('Unexpected private capability format')
    target=state/'message-access-key'
    if target.exists() and target.read_text().strip()!=token:raise RuntimeError('New task already has a different capability; refusing to overwrite it')
    target.write_text(token+'\n');target.chmod(0o600)
    receipt['inboxBefore']=copy_inbox()
    if identity(old_pid)!=observed:raise RuntimeError('Task server identity changed before switch')
    os.kill(old_pid,signal.SIGTERM)
    stopped=False;stop_deadline=time.monotonic()+12
    while time.monotonic()<stop_deadline:
        try:
            status_probe=subprocess.run(['ps','-p',str(old_pid),'-o','stat=,lstart='],capture_output=True,text=True,timeout=2)
            value=status_probe.stdout.strip()
            if status_probe.returncode or not value or value.startswith('Z') or expected_start not in value:
                stopped=True;break
        except subprocess.TimeoutExpired:
            continue
        time.sleep(.2)
    if not stopped:raise RuntimeError('Prior task server did not stop; no forced kill was attempted')
    old_stopped=True
    receipt['inboxAfterStop']=copy_inbox()
    allowed=[origin,surge]
    new_server=launch(task,state,allowed,log)
    health=get(origin)
    if health.get('taskId')!=task or not health.get('ok'):raise RuntimeError('Public route did not reach the new task server')
    public=get(origin,'/api/progress')
    if public.get('status',{}).get('taskId')!=task or public.get('status',{}).get('currentWpt')!=current:raise RuntimeError('Public current-artifact evidence differs')
    env=dict(os.environ,LUME_TASK_ID=task,LUME_PROGRESS_ORIGIN=origin,LUME_MESSAGE_SESSION_ID=old_task)
    verified=subprocess.run(['node','tools/apple-session-gpt6as/verify_progress_continuity.mjs'],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=65)
    verification=json.loads((state/'continuity-public-verification.json').read_text())
    if verified.returncode or not verification.get('passed'):raise RuntimeError('Public WebSocket/private message continuity verification failed')
    server_receipt={'taskId':task,'startedUTC':utc(),'pid':new_server.pid,'port':port,'stateDirectory':str(state.relative_to(root)),
        'sourceCommit':head,'localHealth':True,'publicURL':origin,'messageKeyPublished':False,'serverAlive':True,
        'publicProgressVerified':True,'allowedOrigins':allowed,'reusedRouteFromTask':old_task,'messageSessionId':old_task,'privateCapabilityReused':True,'finishedUTC':utc()}
    write(state/'server-receipt.json',server_receipt)
    receipt.update(switched=True,newPID=new_server.pid,newIdentity=identity(new_server.pid),verification=verification)
except Exception as exc:
    message=type(exc).__name__+': '+str(exc)
    if token:message=message.replace(token,'[private capability omitted]')
    receipt['error']=message[:600]
    if new_server is not None and new_server.poll() is None:
        new_server.terminate()
        try:new_server.wait(timeout=8)
        except subprocess.TimeoutExpired:receipt['newServerStopIssue']='New task server did not terminate promptly; no unrelated process was targeted'
    if old_stopped and (new_server is None or new_server.poll() is not None):
        try:
            receipt['rollbackInbox']=copy_inbox(state,old_state)
            rollback=launch(old_task,old_state,old_receipt.get('allowedOrigins',[origin,surge]),log)
            health=get(origin)
            receipt['rolledBack']=health.get('taskId')==old_task and bool(health.get('ok'))
            receipt['rollbackPID']=rollback.pid
            receipt['rollbackIdentity']=identity(rollback.pid)
        except Exception as recovery:receipt['rollbackError']=type(recovery).__name__+': recovery health check failed'
finally:
    receipt['finishedUTC']=utc();write(state/'server-switch-receipt.json',receipt);log.close()
    print(json.dumps(receipt,indent=2))
raise SystemExit(0 if receipt['switched'] else 1)
