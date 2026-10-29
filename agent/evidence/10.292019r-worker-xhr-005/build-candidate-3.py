from pathlib import Path
import os,subprocess,json,time,hashlib,shutil
root=Path.cwd();out=root/'out/10292019r-linux-worker';deps=root/'out/10.292019r-linux-deps/root';v8=root/'out/10292019r-v8-prebuilt/extracted/v8/15.7.112.12';sdl=root/'out/10292019r-prebuilt-deps/sdl3/sdl3/3.4.18';wgpu=root/'out/10292019r-prebuilt-deps/wgpu'
build='out/10292019r-linux-worker/build-candidate-3'
includes=['src',str(deps/'usr/include'),str(deps/'usr/include/x86_64-linux-gnu'),str(deps/'usr/include/freetype2'),str(deps/'usr/include/harfbuzz'),str(v8/'include'),str(sdl/'include'),str(wgpu/'include/webgpu')]
paths=[deps/'usr/lib/x86_64-linux-gnu',v8/'libexec',sdl/'lib',wgpu/'lib']
libdirs=' '.join('-L'+str(p)+' -Wl,-rpath,'+str(p) for p in paths)
netlibs=libdirs+' -l:libicuuc.so.74 -lssl -lcrypto -lz -lbrotlidec -lzstd -lm'
libs=netlibs+' '+str(v8/'libexec/libicuuc.so')+' -Wl,-rpath-link,'+str(v8/'libexec')+' -lfreetype -lharfbuzz -lSDL3 -lv8 -lv8_libplatform -lv8_libbase -lwgpu_native -lavcodec -lavformat -lavutil -lswscale -lswresample -lpng -ljpeg -lwebp -lwebpdemux -lsharpyuv -lgif -lfontconfig -lX11 -lpthread -ldl'
command=['make','-s','-j2','-f','Makefile','BUILD='+build,'CC=gcc','CXX=g++','OPT=-O1 -g0 -DNDEBUG','INC='+' '.join('-I'+p for p in includes),'LIBDIRS='+libdirs,'BASELIBS='+libdirs+' -l:libicuuc.so.74 -lm','NETLIBS='+netlibs,'LIBS='+libs,'RTC=0','WT=0',build+'/jsrun',build+'/test_worker_lifecycle',build+'/test_worker_async_lifecycle']
(out/'candidate-3-command.json').write_text(json.dumps(command,indent=2)+'\n')
env=dict(os.environ);env['LUMEN_ICU_DATA']=str(v8/'libexec/icudtl.dat');env['LD_LIBRARY_PATH']=':'.join(str(p) for p in [paths[1],paths[0],*paths[2:]])
(out/'runtime-env.json').write_text(json.dumps({k:env[k] for k in ['LUMEN_ICU_DATA','LD_LIBRARY_PATH']},indent=2)+'\n')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
inputs=json.loads((out/'candidate-2-source-manifest.json').read_text())
manifest={name:sha(root/name)for name in inputs}
(out/'candidate-3-source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if (root/build).exists():raise RuntimeError('Fresh build directory already exists')
started=time.monotonic();process=subprocess.run(command,env=env)
if process.returncode==0:
    files={}
    for name in ['jsrun','test_worker_lifecycle','test_worker_async_lifecycle']:
        p=root/build/name
        with p.open('rb')as stream:magic=stream.read(4)
        st=p.stat()
        if st.st_size<1024 or magic!=b'\x7fELF' or not os.access(p,os.X_OK):
            raise RuntimeError('Invalid native artifact '+name+' size='+str(st.st_size)+' mode='+oct(st.st_mode))
        files[str(p.relative_to(root))]={'bytes':st.st_size,'sha256':sha(p),'executable':True}
    icu=root/build/'icudtl.dat';shutil.copy2(v8/'libexec/icudtl.dat',icu)
    files[str(icu.relative_to(root))]={'bytes':icu.stat().st_size,'sha256':sha(icu)}
    if any(sha(root/name)!=digest for name,digest in manifest.items()):raise RuntimeError('Source changed during build')
    (out/'candidate-3-binaries.json').write_text(json.dumps({'stage':'native-linux-worker-candidate-3','sourceManifest':'candidate-3-source-manifest.json','buildCommand':'candidate-3-command.json','v8':'15.7.112.12','platform':'linux-x86_64','rtc':False,'webtransport':False,'files':files},indent=2)+'\n')

print(json.dumps({'stage':'candidate-3-build','exitCode':process.returncode,'durationSeconds':time.monotonic()-started,'build':build,'v8':'15.7.112.12','rtc':False,'webtransport':False}),flush=True)
raise SystemExit(process.returncode)
