from pathlib import Path
import subprocess,json,hashlib,datetime,time,sys,os,tarfile,io
root=Path.cwd();out=root/'agent/validation/linux-wpt-20261031-session9/raster228-independent-proof';plan=json.loads((out/'plan.json').read_text());sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();pins={str(out/'plan.json'):sha(out/'plan.json'),str(Path(__file__).resolve()):sha(__file__)}
for label,item in plan['inputs'].items():
 for key in ['raster','test']:
  p=root/item[key];assert sha(p)==item[key+'_sha256'];pins[str(p)]=sha(p)
frozen=Path(plan['frozen_include_dir']).parents[1];archive=subprocess.check_output(['git','archive','55248ac10c99ffae7f0b6ea29b2f332b8f1d3e4d','src'])
with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
 for item in tf.getmembers():
  if item.isfile() and (item.name.endswith(('.h','.inc')) or item.name=='src/base/util.c'):
   p=frozen/item.name;want=hashlib.sha256(tf.extractfile(item).read()).hexdigest();assert sha(p)==want,str(p);pins[str(p)]=want
commands=[]
for label,item in plan['inputs'].items():
 exe=out/(label+'-asan');cmd=['cc',*plan['compiler_flags'],'-I'+plan['frozen_include_dir'],str(root/item['test']),plan['frozen_util_c'],*plan['link_flags'],'-o',str(exe)]
 commands.append({'label':label+'-compile','command':['python3',str(root/'tools/codex_compile.py'),*cmd]})
 commands.append({'label':label+'-run','command':['python3',str(root/'tools/codex_compile.py'),str(exe),'--print-golden',str(out/(label+'-frames.bin'))]})
inner='import subprocess,json,pathlib,sys,datetime\nplan='+repr(commands)+'\nout=pathlib.Path('+repr(str(out))+')\nrows=[]\nfor item in plan:\n started=datetime.datetime.now(datetime.timezone.utc).isoformat()\n with (out/(item["label"]+".log")).open("x") as f:p=subprocess.run(item["command"],stdout=f,stderr=subprocess.STDOUT)\n r={**item,"started_utc":started,"finished_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"returncode":p.returncode};rows.append(r);print(json.dumps(r),flush=True);(out/"execution.json").write_text(json.dumps(rows,indent=2)+"\\n")\n if p.returncode:sys.exit(p.returncode)\n'
start=datetime.datetime.now(datetime.timezone.utc).isoformat();t=time.monotonic();env=dict(os.environ);env['TMPDIR']=str(root/'agent/t');(out/'pins-before.json').write_text(json.dumps(pins,indent=2)+'\n')
with (out/'controller.log').open('x') as log,(out/'cgroup-monitor.log').open('x') as cglog,(out/'tree-monitor.log').open('x') as tlog:
 cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],stdout=cglog,stderr=subprocess.STDOUT)
 p=subprocess.Popen([sys.executable,'-c',inner],stdout=log,stderr=subprocess.STDOUT,env=env)
 tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label','frozen actual reference and prior raster sanitizer proof'],stdout=tlog,stderr=subprocess.STDOUT)
 rc=p.wait()
 try:tm.wait(6)
 except subprocess.TimeoutExpired:tm.terminate();tm.wait(6)
 cg.terminate();cg.wait(6)
after={p:sha(p) for p in pins};r={'started_utc':start,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'duration_s':time.monotonic()-t,'returncode':rc,'monitor_returncodes':[cg.returncode,tm.returncode],'frozen_inputs_unchanged':pins==after,'source_header_commit':'55248ac10c99ffae7f0b6ea29b2f332b8f1d3e4d','source_archive_sha256':hashlib.sha256(archive).hexdigest(),'production_raster_unchanged_during_proof':sha(root/'src/paint/raster.c')==plan['current_production_sha256'],'source_scope':'Immutable raster/test snapshots and exact227headers/util; concurrent unrelated228CSSedits not used'};(out/'pins-after.json').write_text(json.dumps(after,indent=2)+'\n');(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
for label in plan['inputs']:
 p=out/(label+'-run.log')
 if p.exists():print(label,p.read_text()[-2000:])
sys.exit(0 if rc==0 and pins==after and cg.returncode==0 and tm.returncode==0 else 2)
