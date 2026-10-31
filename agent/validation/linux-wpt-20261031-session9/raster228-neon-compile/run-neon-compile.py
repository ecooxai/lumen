from pathlib import Path
import subprocess,json,hashlib,datetime,time,sys,os
root=Path.cwd();out=Path(__file__).resolve().parent;proof=root/'agent/validation/linux-wpt-20261031-session9/raster228-independent-proof';plan=json.loads((proof/'plan.json').read_text());sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();pins=json.loads((proof/'pins-after.json').read_text());assert all(sha(p)==h for p,h in pins.items())
compiler=root/'build/codex-android-toolchain/sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang';assert compiler.is_file()
commands=[{'label':'compiler-version','command':[str(compiler),'--version']},{'label':'compile-neon','command':['python3','tools/codex_compile.py',str(compiler),'-std=gnu11','-O2','-Wall','-Wextra','-Wno-misleading-indentation','-I'+plan['frozen_include_dir'],'-c',str(proof/'actual-raster.c'),'-o',str(out/'raster-aarch64.o')]},{'label':'target-macros','command':['python3','tools/codex_compile.py',str(compiler),'-dM','-E','-x','c','/dev/null']}]
(out/'plan.json').write_text(json.dumps({'commands':commands,'source_sha256':sha(proof/'actual-raster.c'),'scope':'Compilation only; ARM runtime untested on x86 host'},indent=2)+'\n')
inner='import subprocess,json,pathlib,sys,datetime\ncommands='+repr(commands)+'\nout=pathlib.Path('+repr(str(out))+')\nrows=[]\nfor item in commands:\n started=datetime.datetime.now(datetime.timezone.utc).isoformat()\n with (out/(item["label"]+".log")).open("x") as f:p=subprocess.run(item["command"],stdout=f,stderr=subprocess.STDOUT)\n rows.append({**item,"started_utc":started,"finished_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"returncode":p.returncode});(out/"execution.json").write_text(json.dumps(rows,indent=2)+"\\n")\n if p.returncode:sys.exit(p.returncode)\n'
start=datetime.datetime.now(datetime.timezone.utc).isoformat();t=time.monotonic();env=dict(os.environ);env['TMPDIR']=str(root/'agent/t')
with (out/'controller.log').open('x') as log,(out/'cgroup-monitor.log').open('x') as cglog,(out/'tree-monitor.log').open('x') as tlog:
 cg=subprocess.Popen(['python3','agent/progress/cgroup_monitor.py','--output',str(out/'cgroup.csv')],stdout=cglog,stderr=subprocess.STDOUT)
 p=subprocess.Popen([sys.executable,'-c',inner],stdout=log,stderr=subprocess.STDOUT,env=env)
 tm=subprocess.Popen(['python3','agent/progress/monitor.py','--root-pid',str(p.pid),'--output',str(out/'resources.csv'),'--label','frozen raster ARM64 NEON compile only'],stdout=tlog,stderr=subprocess.STDOUT)
 rc=p.wait()
 try:tm.wait(6)
 except subprocess.TimeoutExpired:tm.terminate();tm.wait(6)
 cg.terminate();cg.wait(6)
macros=(out/'target-macros.log').read_text() if (out/'target-macros.log').exists() else '';neon='#define __ARM_NEON 1' in macros and '#define __aarch64__ 1' in macros
r={'started_utc':start,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'duration_s':time.monotonic()-t,'returncode':rc,'monitor_returncodes':[cg.returncode,tm.returncode],'frozen_inputs_unchanged':all(sha(p)==h for p,h in pins.items()),'aarch64_and_neon_macros_verified':neon,'compiler':str(compiler),'compiler_version':(out/'compiler-version.log').read_text(),'object_sha256':sha(out/'raster-aarch64.o') if (out/'raster-aarch64.o').exists() else None,'runtime_coverage':'Not run; x86 host. Compilation validates ARM64 intrinsics only.'};(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));sys.exit(0 if rc==0 and neon and r['frozen_inputs_unchanged'] and cg.returncode==0 and tm.returncode==0 else 2)
