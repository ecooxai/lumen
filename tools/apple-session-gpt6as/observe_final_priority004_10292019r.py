"""Incrementally observe complete raw rows without altering the running cohort."""
from pathlib import Path
from datetime import datetime,timezone
from collections import Counter
import json,hashlib,subprocess,tempfile,os,time
root=Path.cwd().resolve();base=root/'out/10292019r-mac/wpt-final-004';sp=root/'out/10292019r-mac/native-worker-004/summary.json'
if str(root)!='/Users/devin/repos/lumen':raise SystemExit('Wrong workspace')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();meta=json.loads(sp.read_text());manifest=json.loads((root/meta['compiledSourceManifest']).read_text());changes=[n for n,h in manifest.items()if sha(root/n)!=h]
statepath=base/'progress-state.json';state=json.loads(statepath.read_text())if statepath.exists()else{'groups':{}}
for group in ['encoding-streams','dom','events','url','storage','microtasks','headers','encoding-core','encoding-rest']:
 p=base/'cohort'/group/'results.jsonl'
 if not p.exists():continue
 g=state['groups'].setdefault(group,{'offset':0,'run':0,'fullyPassing':0,'statuses':{},'assertions':{},'failures':[],'seen':[]})
 if p.stat().st_size<g['offset']:raise SystemExit('Observed raw file was truncated; preserve prior progress')
 with p.open('rb')as f:
  f.seek(g['offset'])
  while True:
   position=f.tell();line=f.readline()
   if not line or not line.endswith(b'\n'):break
   row=json.loads(line);g['offset']=f.tell();name=row.get('test')
   if name in g['seen']:raise SystemExit('Duplicate raw variant observed')
   g['seen'].append(name);g['run']+=1;status=row.get('status','UNKNOWN');g['statuses'][status]=g['statuses'].get(status,0)+1
   sub=row.get('subtests')or[];okay=status in ['OK','PASS']and bool(sub)and all(s.get('status')=='PASS'for s in sub)and not row.get('metrics',{}).get('errors')and not row.get('cleanup',{}).get('error')and not row.get('cleanup',{}).get('profile_error')
   g['fullyPassing']+=int(okay);g['lastTest']=name
   for s in sub:g['assertions'][s.get('status','UNKNOWN')]=g['assertions'].get(s.get('status','UNKNOWN'),0)+1
   if not okay:g['failures'].append({'test':name,'status':status,'assertionFailures':sum(s.get('status')!='PASS'for s in sub),'message':str(row.get('message')or'')[:140]})
 g['rawBytes']=p.stat().st_size;g['lastWriteUTC']=datetime.fromtimestamp(p.stat().st_mtime,timezone.utc).isoformat()
receipt=json.loads((base/'receipt.json').read_text());stage=receipt.get('stages',[{}])[-1];processes=[]
for line in subprocess.check_output(['ps','-axo','pid=,ppid=,etime=,%cpu=,rss=,args='],text=True).splitlines():
 fields=line.strip().split(None,5)
 if len(fields)==6 and ('run_task_wpt_102900614.py' in fields[5] or str(root/meta['artifacts']['lumen']['path']) in fields[5]):processes.append({'pid':fields[0],'elapsed':fields[2],'cpu':fields[3],'rssMiB':round(int(fields[4])/1024,2),'command':fields[5][:220]})
state.update(utc=datetime.now(timezone.utc).isoformat(),stage=stage.get('name'),source=meta['sourceBaseCommit'],binarySHA256=meta['artifacts']['lumen']['sha256'],sourceChanges=changes,processes=processes,observationOnly=True)
fd,tmp=tempfile.mkstemp(prefix='.progress-',dir=base)
with os.fdopen(fd,'w')as f:json.dump(state,f,indent=2);f.write('\n');f.flush();os.fsync(f.fileno())
os.replace(tmp,statepath)
last=state['groups'].get(stage.get('name'),{});failures=[x for g in state['groups'].values()for x in g['failures']]
compact={'utc':state['utc'],'stage':state['stage'],'fullyPassing':sum(g['fullyPassing']for g in state['groups'].values()),'recorded':sum(g['run']for g in state['groups'].values()),'scheduled':1515,'lastTest':str(last.get('lastTest',''))[-120:],'lastWriteUTC':last.get('lastWriteUTC'),'nonpassing':len(failures),'latestFailures':[{k:x[k]for k in ['test','status']}for x in failures[-2:]],'sourceChanges':len(changes),'active':[{'pid':p['pid'],'elapsed':p['elapsed'],'cpu':p['cpu']}for p in processes[-2:]],'detail':str(statepath.relative_to(root))}
print(json.dumps(compact,separators=(',',':')),flush=True)
