#!/usr/bin/env python3
"""Reuse the established WPT navigation harness with isolated outputs and frozen exact selection."""
import argparse,hashlib,json,os,pathlib,signal,subprocess,sys,threading,time,urllib.request
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]/'tests/wpt'))
import lumen_wpt as W
from paired_wpt import monitor,endmonitor
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--urls-file',required=True);p.add_argument('--output',required=True);p.add_argument('--jobs',type=int,default=4);a=p.parse_args()
out=pathlib.Path(a.output).resolve();out.mkdir(exist_ok=True,parents=True);runtime=out/'runtime';runtime.mkdir(exist_ok=True)
W.OUT=str(out);W.TMP=str(runtime)
urls=json.load(open(a.urls_file));items=W.load_json('tests.json')['items'];items={'/'+u.lstrip('/'):v for u,v in items.items()}
def islong(u):
 v=items.get(u);return isinstance(v,list) and len(v)>1 and isinstance(v[1],dict) and v[1].get('timeout')=='long'
ca=W.setup_wpt();runner=W.Runner([(u,islong(u)) for u in urls],a.jobs,100,os.path.realpath(a.binary),ca);server=W.serve_controller(runner)
meta={'binary':os.path.realpath(a.binary),'binary_sha256':hashlib.sha256(open(a.binary,'rb').read()).hexdigest(),'urls':urls,'jobs':a.jobs,'restart_every':100,'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'timeout_policy':'Unmodified established harness:normal40s/long100s harddeadlines plus testharness watchdog; localmanifest timeout metadata normalized for leading slash','wpt_revision':'0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb','reporting':'Same testharness assertions, custom native testdriver; no filter/expected-failure rewrites.'}
json.dump(meta,open(out/'metadata.json','w'),indent=2)
m=monitor(os.getpid(),out/'resources','Lumen batch process tree inclsmallcontroller')
def interrupt(*_):
 runner.stop=True
 with runner.lock:runner.cv.notify_all()
 for inst in runner.insts:runner.kill(inst)
signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt)
try:
 threading.Thread(target=runner.progress,daemon=True).start();threads=[threading.Thread(target=runner.drive,args=(i,)) for i in runner.insts]
 for t in threads:t.start()
 for t in threads:t.join()
finally:
 runner.stop=True
 for inst in runner.insts:runner.kill(inst)
 runner.out.close();server.shutdown();server.server_close();endmonitor(m)
meta.update(finished_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),completed=runner.ndone,statuses=dict(runner.counts));json.dump(meta,open(out/'metadata.json','w'),indent=2);print(json.dumps({k:v for k,v in meta.items() if k!='urls'}),flush=True)
