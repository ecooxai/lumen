"""Fresh artifact-bound full1515 priority ledger, with explicit unrun retention."""
from pathlib import Path
from datetime import datetime,timezone
import argparse,hashlib,json,os,subprocess,sys,tempfile,time
parser=argparse.ArgumentParser();parser.add_argument('--native-summary',default='out/10292019r-mac/native-worker-004/summary.json');parser.add_argument('--output',default='out/10292019r-mac/wpt-final-004');parser.add_argument('--deadline',default='2026-10-29T16:08:00+00:00');args=parser.parse_args()
root=Path.cwd().resolve();out=(root/args.output).resolve();sp=(root/args.native_summary).resolve();deadline=datetime.fromisoformat(args.deadline).timestamp()
if str(root)!='/Users/devin/repos/lumen' or not out.is_relative_to(root/'out') or not sp.is_relative_to(root/'out'):raise SystemExit('Unexpected workspace/output')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();source=json.loads(sp.read_text())
if not source.get('passed'):raise SystemExit('Native source has not passed its acceptance gates')
binary=root/source['artifacts']['lumen']['path'];expected=source['artifacts']['lumen']['sha256'];mp=root/source['compiledSourceManifest'];inputs=json.loads(mp.read_text())
if sha(mp)!=source['sourceManifestSHA256']:raise SystemExit('Compiled manifest differs')
def guard():
 if sha(binary)!=expected or any(sha(root/p)!=h for p,h in inputs.items()):raise RuntimeError('Native artifact/compiled input changed')
def atomic(path,value):
 fd,temp=tempfile.mkstemp(prefix='.receipt-',dir=path.parent)
 with os.fdopen(fd,'w')as f:json.dump(value,f,indent=2);f.write('\n');f.flush();os.fsync(f.fileno())
 os.replace(temp,path)
guard();out.mkdir(parents=True,exist_ok=False);began=time.monotonic();stages=[];plan=out/'cohort/cohort.json';runner='tools/apple-session-gpt6as/run_task_wpt_102900614.py'
receipt={'source':source['sourceBaseCommit'],'sourceManifestSHA256':source['sourceManifestSHA256'],'binarySHA256':expected,'nativeSummary':str(sp.relative_to(root)),'nativeSummarySHA256':sha(sp),'scope':'Every1515 frozen priority variant retained on one current native Mac artifact; not full official WPT','startedUTC':datetime.now(timezone.utc).isoformat(),'complete':False,'stages':stages}
def run(name,cmd,limit):
 guard();start=time.monotonic();row={'name':name,'startedUTC':datetime.now(timezone.utc).isoformat(),'command':cmd};stages.append(row);atomic(out/'receipt.json',receipt)
 with (out/(name+'.log')).open('w')as log:p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=limit)
 row.update(exitCode=p.returncode,seconds=round(time.monotonic()-start,3),log=str((out/(name+'.log')).relative_to(root)));guard();atomic(out/'receipt.json',receipt)
 if p.returncode:raise RuntimeError(name+' execution/measurement failure')
try:
 run('prepare',[sys.executable,runner,'prepare','--binary',str(binary),'--sha256',expected,'--output',str(plan.parent)],60)
 for group,budget in [('encoding-streams',240),('dom',240),('events',240),('url',240),('storage',240),('microtasks',240),('headers',420),('encoding-core',240),('encoding-rest',1200)]:
  remaining=deadline-time.time()-75
  if remaining<80:receipt['timeBudgetExhausted']=True;break
  limit=min(budget,int(remaining));run(group,[sys.executable,runner,'run','--plan',str(plan),'--group',group,'--max-seconds',str(limit)],limit+75)
 report=json.loads((plan.parent/'priority-summary.json').read_text());receipt.update({k:report[k]for k in ['scheduled','run','fullyPassing','unrun','passingPercentOfRun','assertions','harnessStatuses','groups']});receipt['complete']=report['run']==1515 and report['unrun']==0
 receipt.update(summary=str((plan.parent/'priority-summary.json').relative_to(root)),summarySHA256=sha(plan.parent/'priority-summary.json'),cohortSHA256=sha(plan))
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
finally:
 receipt.update(finishedUTC=datetime.now(timezone.utc).isoformat(),seconds=round(time.monotonic()-began,3));atomic(out/'receipt.json',receipt)
 print(json.dumps({k:receipt.get(k)for k in ['complete','fullyPassing','run','unrun','passingPercentOfRun','assertions','harnessStatuses','binarySHA256','source','seconds','error']}|{'receipt':str((out/'receipt.json').relative_to(root))}),flush=True)
raise SystemExit(0 if receipt['complete'] and not receipt.get('error')else 1)
