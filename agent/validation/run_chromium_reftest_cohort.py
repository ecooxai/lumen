# Lifecycle-only adaptation: identical official reftest runner and pixel rules.
import os,sys,json,time,datetime,subprocess,hashlib,signal,shutil,argparse
from pathlib import Path
parser=argparse.ArgumentParser(description='Run an unchanged official Chromium cohort and drain owned processes/profiles.');parser.add_argument('--urls-file',required=True,type=Path);parser.add_argument('--output',required=True,type=Path);parser.add_argument('--jobs',type=int,default=6);args=parser.parse_args();assert 1<=args.jobs<=6
root=Path.cwd().resolve();run=args.output.resolve();run.mkdir(parents=True,exist_ok=False);out=run/'chromium';out.mkdir()
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def ident(pid):
 try:
  s=Path('/proc/%d/stat'%pid).read_text();a=s[s.rfind(')')+2:].split();return {'pid':pid,'start_ticks':int(a[19]),'ppid':int(a[1])}
 except (OSError,ValueError):return None
def live(i):
 n=ident(i['pid']);return n is not None and n['start_ticks']==i['start_ticks']
def snapshot():
 d={}
 for p in Path('/proc').iterdir():
  if p.name.isdigit():
   i=ident(int(p.name))
   if i:d[i['pid']]=i
 return d
urls=args.urls_file.resolve(strict=True);runner=root/'agent/validation/official_reftest.py';env=os.environ.copy()
for k in ('LUMEN_WPT_CTRL','LUMEN_PRE','LUMEN_PRE_FILE'):env.pop(k,None)
env['TMPDIR']=str(root/'agent/t');Path(env['TMPDIR']).mkdir(exist_ok=True)
cmd=[sys.executable,'tools/codex_compile.py',sys.executable,str(runner),'--urls-file',str(urls),'--output',str(out),'--jobs',str(args.jobs)]
started=time.monotonic();log=(run/'controller.log').open('w');p=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);ri=ident(p.pid);assert ri
launch={'pid':p.pid,'identity':ri,'coordinator_identity':ident(os.getpid()),'command':cmd,'started_utc':now(),'started_monotonic':started,'TMPDIR':env['TMPDIR'],'selection_sha256':hashlib.sha256(urls.read_bytes()).hexdigest(),'runner_sha256':hashlib.sha256(runner.read_bytes()).hexdigest(),'mode':'Fresh specified pinned variants; official Chromium WPT; fixed worker count; scheduling duration not performance','selected_variants':len(json.loads(urls.read_text())),'jobs':args.jobs,'coordinator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
(run/'chromium-launch.json').write_text(json.dumps(launch,indent=2));owned={p.pid:ri};profiles=set();m=subprocess.Popen([sys.executable,'agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(run/'resources.csv'),'--label','owned final Chromium WPT process tree'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
while p.poll() is None:
 allp=snapshot();front=[q for q,i in owned.items() if live(i)]
 seen=set(front)
 while front:
  parent=front.pop()
  for q,i in allp.items():
   if i['ppid']==parent and q not in seen:seen.add(q);front.append(q);owned[q]=i
 for q in seen:
  try:
   a=Path('/proc/%d/cmdline'%q).read_bytes().split(b'\0')
   for arg in a:
    if arg.startswith(b'--user-data-dir='):profiles.add(arg.split(b'=',1)[1].decode())
  except OSError:pass
 (run/'ownership.json').write_text(json.dumps({'identities':list(owned.values()),'observed_profiles':sorted(profiles),'updated_utc':now()},indent=2))
 time.sleep(1)
ended=time.monotonic();code=p.returncode
try:m.wait(timeout=5)
except subprocess.TimeoutExpired:m.terminate();m.wait(timeout=5)
remaining=[i for i in owned.values() if live(i)];actions=[]
for sig,wait in ((signal.SIGTERM,3),(signal.SIGKILL,2)):
 for i in remaining:
  if live(i):
   try:os.kill(i['pid'],sig);actions.append({'identity':i,'signal':sig})
   except ProcessLookupError:pass
 stop=time.monotonic()+wait
 while time.monotonic()<stop and any(live(i) for i in remaining):time.sleep(.1)
 remaining=[i for i in remaining if live(i)]
removed=[];retained=[];tmp=Path(env['TMPDIR']).resolve()
for name in profiles:
 f=Path(name)
 if not f.exists():continue
 try:
  resolved=f.resolve();safe=resolved.is_relative_to(tmp) and not f.is_symlink() and f.stat().st_uid==os.getuid()
  refs=[]
  for q in Path('/proc').iterdir():
   if q.name.isdigit():
    try:
     if str(f).encode() in (q/'cmdline').read_bytes():refs.append(int(q.name))
    except OSError:pass
  if safe and not refs and not remaining:shutil.rmtree(f);removed.append(name)
  else:retained.append({'profile':name,'safe':safe,'references':refs})
 except OSError as e:retained.append({'profile':name,'error':str(e)})
seal={'finished_utc':now(),'started_monotonic':started,'ended_monotonic':ended,'duration_s':ended-started,'exit_code':code,'owned_identities':len(owned),'remaining_same_identity':remaining,'cleanup_actions':actions,'observed_profiles':len(profiles),'removed_residual_profiles':removed,'remaining_profiles':retained,'runner_unchanged':hashlib.sha256(runner.read_bytes()).hexdigest()==launch['runner_sha256'],'selection_unchanged':hashlib.sha256(urls.read_bytes()).hexdigest()==launch['selection_sha256']}
(run/'completion.json').write_text(json.dumps(seal,indent=2));print(json.dumps(seal),flush=True)

if remaining or retained or not seal['runner_unchanged'] or not seal['selection_unchanged']:raise SystemExit('Owned-process/profile/provenance gate failed')
if not (out/'report.json').exists():raise SystemExit('Official WPT report missing')
