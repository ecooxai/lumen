#!/usr/bin/env python3
from pathlib import Path
import datetime,hashlib,json,subprocess,time,os,signal,sys
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');OPS=ROOT/'agent/ops/linux-wpt-20261031-session9'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def dump(p,v):p.write_text(json.dumps(v,indent=2)+'\n')
def main():
 runner=OPS/'repair-public-bundle234-v2.py';q=OPS/'qualification234-precommit.json';out=OPS/'public-source234-recovery-v2-execution'
 assert not out.exists();assert sha(runner)=='1e60ae81e60c528017254207dac3ec9cd9860160531b2a5d54aaf39915ae0886';assert json.loads(q.read_text())['qualification_accepted'] is True
 out.mkdir();pins={str(p):sha(p) for p in [runner,q,Path(__file__).resolve(),ROOT/'tools/codex_compile.py',ROOT/'agent/progress/monitor.py',ROOT/'agent/progress/cgroup_monitor.py']};dump(out/'pins-before.json',pins)
 command=['python3',str(ROOT/'tools/codex_compile.py'),'python3',str(runner)];start=time.monotonic();started=datetime.datetime.now(datetime.timezone.utc).isoformat();handles=[];mons=[];p=None;errors=[];forced=[]
 try:
  h=(out/'cgroup-monitor.log').open('w');handles.append(h);mons.append(subprocess.Popen(['python3',str(ROOT/'agent/progress/cgroup_monitor.py'),'--output',str(out/'cgroup.csv')],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT))
  h=(out/'command.log').open('w');handles.append(h);p=subprocess.Popen(command,cwd=ROOT,stdout=h,stderr=subprocess.STDOUT,start_new_session=True)
  h=(out/'tree-monitor.log').open('w');handles.append(h);mons.append(subprocess.Popen(['python3',str(ROOT/'agent/progress/monitor.py'),'--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label','public source234 commit bundle offline recovery'],cwd=ROOT,stdout=h,stderr=subprocess.STDOUT));p.wait(180)
 except Exception as e:errors.append(repr(e))
 finally:
  if p is not None and p.poll() is None:
   os.killpg(p.pid,signal.SIGTERM);forced.append('owned recovery group SIGTERM')
   try:p.wait(10)
   except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(5);forced.append('owned recovery group SIGKILL')
  for i,m in enumerate(mons):
   if i==1 and m.poll() is None:
    try:m.wait(6)
    except subprocess.TimeoutExpired:forced.append('tree sampler forced stop')
   if m.poll() is None:m.terminate()
   try:m.wait(6)
   except subprocess.TimeoutExpired:m.kill();m.wait(3);forced.append('sampler killed')
  for h in handles:h.close()
 after={p:sha(p) for p in pins};dump(out/'pins-after.json',after);code=p.returncode if p else None;ok=code==0 and not errors and not forced and pins==after and len(mons)==2 and all(m.returncode==0 for m in mons)
 rec={'candidate':234,'started_utc':started,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'duration_s':time.monotonic()-start,'command':command,'actual_returncode':code,'monitor_returncodes':[m.returncode for m in mons],'errors':errors,'forced_cleanup':forced,'pins_unchanged':pins==after,'execution_accepted':ok,'no_push_performed':True};dump(out/'receipt.json',rec);print(json.dumps(rec));sys.exit(0 if ok else 2)
if __name__=='__main__':main()
