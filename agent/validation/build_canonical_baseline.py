import hashlib,json,os,pathlib,subprocess,time
out=pathlib.Path('agent/validation/canonical-baseline');out.mkdir(exist_ok=True,parents=True)
work='/mnt/p3/project/browser/lumen-android/lumen-astra-baseline-3495493';build=work+'/build/astra-canonical';t=time.time()
commands=[['cmake','-S',work,'-B',build,'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER=/usr/bin/clang','-DCMAKE_CXX_COMPILER=/usr/bin/clang++','-DLUMEN_DEPS_DIR=/mnt/p3/project/browser/lumen-android/lumen-android/build/codex-deps/v8/bottle/v8/15.6.75.8','-DLUMEN_V8_POINTER_COMPRESSION=ON','-DLUMEN_BUILD_BROWSER=ON','-DLUMEN_BUILD_TESTS=ON'],['cmake','--build',build,'--target','lumen','-j','3']]
extra=[]
for line in pathlib.Path('build/codex-native/CMakeCache.txt').read_text().splitlines():
 if not line or line.startswith(('#','//')) or ':' not in line or '=' not in line:continue
 key,rest=line.split(':',1);typ,val=rest.split('=',1)
 if typ in ('PATH','FILEPATH') and not key.startswith(('CMAKE_','Lumen_')) and val.startswith('/') and os.path.exists(val):extra.append('-D'+key+':'+typ+'='+val)
 if key in ('CMAKE_CXX_FLAGS','CMAKE_C_FLAGS','CMAKE_CXX_FLAGS_RELEASE','CMAKE_C_FLAGS_RELEASE','CMAKE_EXE_LINKER_FLAGS','CMAKE_BUILD_RPATH'):extra.append('-D'+key+'='+val)
commands[0].extend(extra)
r={'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(t)),'commit':subprocess.check_output(['git','-C',work,'rev-parse','HEAD'],text=True).strip(),'worktree':work,'commands':commands,'returncodes':[]}
with (out/'build.log').open('wb') as f:
 for cmd in commands:
  p=subprocess.run(cmd,stdout=f,stderr=f);r['returncodes'].append(p.returncode)
  if p.returncode:break
if r['returncodes']==[0,0]:
 r['binary']=build+'/lumen';r['binary_sha256']=hashlib.sha256(open(r['binary'],'rb').read()).hexdigest()
r['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());r['elapsed_s']=round(time.time()-t,3);json.dump(r,open(out/'build-state.json','w'),indent=2);print(json.dumps(r),flush=True)
