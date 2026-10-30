"""Launch the unchanged six-file native/Chrome comparison for one immutable candidate."""
import argparse,hashlib,json,os,pathlib,re,shutil,signal,subprocess,sys
from datetime import datetime,timezone
ROOT=pathlib.Path.cwd().resolve();KIT=ROOT/'output/session-20261029/native-qa-kit-v1/source'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb')as f:
  for data in iter(lambda:f.read(1048576),b''):h.update(data)
 return h.hexdigest()
def utc():return datetime.now(timezone.utc).isoformat()
def save(p,d):
 tmp=p.with_suffix('.tmp');tmp.write_text(json.dumps(d,indent=2)+'\n');tmp.replace(p)
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--label',required=True);p.add_argument('--binary',required=True);p.add_argument('--sha256',required=True);p.add_argument('--worker',action='store_true');a=p.parse_args()
if not re.fullmatch('[a-z0-9-]{1,60}',a.label)or not re.fullmatch('[0-9a-f]{64}',a.sha256):raise RuntimeError('Invalid candidate label/hash')
source=(ROOT/a.binary).resolve();source.relative_to(ROOT)
if not source.is_file()or sha(source)!=a.sha256:raise RuntimeError('Immutable candidate hash mismatch')
if not KIT.is_dir():raise RuntimeError('Verified isolated native QA kit absent')
JOB=ROOT/f'output/session-20261029/native-wpt-{a.label}-job';copied=KIT/f'build/lumen-{a.label}'
nativeout=f'output/native-wpt-{a.label}';chromeout=f'output/mac-chrome-wpt-{a.label}';comparison=f'output/wpt-comparison-{a.label}.json'
if not a.worker:
 JOB.mkdir(parents=True,exist_ok=False)
 if copied.exists():raise RuntimeError('Candidate kit copy already exists; choose a new immutable label')
 copied.parent.mkdir(exist_ok=True);shutil.copy2(source,copied);copied.chmod(0o555)
 if sha(copied)!=a.sha256:raise RuntimeError('Candidate copy hash mismatch')
 with(JOB/'job.log').open('x')as log:
  child=subprocess.Popen([sys.executable,str(pathlib.Path(__file__).resolve()),'--label',a.label,'--binary',a.binary,'--sha256',a.sha256,'--worker'],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 print(json.dumps({'workerPID':child.pid,'output':str(JOB),'binarySHA256':a.sha256,'sequence':['native unchanged1850assertion selection','same-Mac Chrome matched reference','exact-inventory comparison']}));raise SystemExit(0)
receipt={'startedUTC':utc(),'workerPID':os.getpid(),'label':a.label,'scope':'Unchanged six-file pinned WPT selection, fixed1850assertions; not fullWPT','sourceBinary':str(source),'sourceSHA256':a.sha256,'steps':[],'issues':[]}
commands=[('native',[sys.executable,'tools/apple-session-gpt6as/probe_native_wpt.py','--binary',str(copied.relative_to(KIT)),'--expected-sha256',a.sha256,'--output',nativeout,'--seconds-per-test','25'],240),('chrome',['node','tools/wpt-reference-gpt6as/run-bounded-chrome.cjs','--output',chromeout],240),('compare',[sys.executable,'scripts/qa-apple-gpt6/wpt_compare.py','--selection','tools/wpt-reference-gpt6as/selection.json','--reference',chromeout+'/wptreport.json','--candidate',nativeout+'/wpt-report.json','--output',comparison],20)]
active=None
def interrupted(*_):raise KeyboardInterrupt
signal.signal(signal.SIGTERM,interrupted)
try:
 for name,command,timeout in commands:
  step={'name':name,'command':command,'startedUTC':utc()};receipt['steps'].append(step);save(JOB/'receipt.json',receipt)
  with(JOB/(name+'.log')).open('x')as log:
   active=subprocess.Popen(command,cwd=KIT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);step['pid']=active.pid
   try:step['exitCode']=active.wait(timeout=timeout)
   except subprocess.TimeoutExpired:
    step['timedOut']=True;os.killpg(active.pid,signal.SIGTERM)
    try:active.wait(timeout=8)
    except subprocess.TimeoutExpired:os.killpg(active.pid,signal.SIGKILL);active.wait(timeout=3)
    step['exitCode']=active.returncode
  step['finishedUTC']=utc();active=None;save(JOB/'receipt.json',receipt)
  if step.get('timedOut'):receipt['issues'].append(name+' exceeded bounded walltime')
 for path in [nativeout,chromeout]:
  samples=KIT/path/'process-samples.jsonl'
  if samples.is_file():subprocess.run([sys.executable,str(KIT/'scripts/qa-apple-gpt6/telemetry.py'),'summarize',str(samples),'--output',str(samples.with_name('process-summary.json'))],cwd=KIT,check=True,timeout=15)
except (Exception,KeyboardInterrupt)as error:receipt['issues'].append(type(error).__name__+': '+str(error))
finally:
 if active and active.poll()is None:
  os.killpg(active.pid,signal.SIGTERM)
  try:active.wait(timeout=8)
  except subprocess.TimeoutExpired:os.killpg(active.pid,signal.SIGKILL);active.wait(timeout=3)
 receipt['finishedUTC']=utc();receipt['sourceHashAfter']=sha(source);receipt['copiedHashAfter']=sha(copied)
 if receipt['sourceHashAfter']!=a.sha256 or receipt['copiedHashAfter']!=a.sha256:receipt['issues'].append('Native candidate changed during comparison')
 save(JOB/'receipt.json',receipt)
 print(json.dumps({'output':str(JOB),'comparison':str(KIT/comparison),'issues':receipt['issues'],'steps':[{'name':s['name'],'exit':s.get('exitCode')}for s in receipt['steps']]}))
