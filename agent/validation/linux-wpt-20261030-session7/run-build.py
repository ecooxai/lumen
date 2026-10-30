#!/usr/bin/env python3
"""Record owned one-second resources around the existing frozen candidate builder."""
import argparse,datetime,json,pathlib,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'agent/validation'))
from paired_wpt import monitor,endmonitor
ap=argparse.ArgumentParser();ap.add_argument('candidate',type=int);args=ap.parse_args()
out=ROOT/'agent/validation'/('iteration-'+str(args.candidate))
binary=ROOT/'build/codex-native'/('lumen-astra-i'+str(args.candidate))
if out.exists() or binary.exists():raise SystemExit('Refuse to overwrite immutable candidate/output')
out.mkdir()
started=datetime.datetime.now(datetime.timezone.utc).isoformat();clock=time.monotonic()
command=[sys.executable,str(ROOT/'tools/codex_compile.py'),sys.executable,str(ROOT/'agent/validation/build_candidate.py'),'--iteration',str(args.candidate),'--jobs','2','--startup']
with (out/'driver.log').open('wb') as log,(out/'cgroup-monitor.log').open('wb') as cglog:
 cg=subprocess.Popen([sys.executable,str(ROOT/'agent/progress/cgroup_monitor.py'),'--output',str(out/'cgroup-resources.csv'),'--duration','1200'],cwd=ROOT,stdin=subprocess.DEVNULL,stdout=cglog,stderr=cglog)
 p=subprocess.Popen(command,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=log)
 state={'candidate':args.candidate,'started_utc':started,'owned_pid':p.pid,'monitor_pid':cg.pid,'command':command,'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()}
 (out/'driver-running.json').write_text(json.dumps(state,indent=2)+'\n')
 mon=monitor(p.pid,out/'build-resources','candidate '+str(args.candidate)+' owned compilation and startup')
 code=p.wait();endmonitor(mon)
 cg.terminate();cg.wait(timeout=5)
 state.update(finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),elapsed_s=time.monotonic()-clock,exit_code=code,remaining_build_pid=pathlib.Path('/proc/'+str(p.pid)).exists(),remaining_cgroup_monitor_pid=pathlib.Path('/proc/'+str(cg.pid)).exists())
 (out/'driver-final.json').write_text(json.dumps(state,indent=2)+'\n');print(json.dumps(state))
raise SystemExit(code)
