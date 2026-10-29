from pathlib import Path
import os,subprocess,json,time
root=Path.cwd();out=root/'out/10292019r-linux-worker';deps=root/'out/10.292019r-linux-deps/root';v8=root/'out/10292019r-v8-prebuilt/extracted/v8/15.7.112.12';sdl=root/'out/10292019r-prebuilt-deps/sdl3/sdl3/3.4.18';wgpu=root/'out/10292019r-prebuilt-deps/wgpu'
build='out/10292019r-linux-worker/build-baseline'
includes=['src',str(deps/'usr/include'),str(deps/'usr/include/x86_64-linux-gnu'),str(deps/'usr/include/freetype2'),str(deps/'usr/include/harfbuzz'),str(v8/'include'),str(sdl/'include'),str(wgpu/'include/webgpu')]
paths=[deps/'usr/lib/x86_64-linux-gnu',v8/'libexec',sdl/'lib',wgpu/'lib']
libdirs=' '.join('-L'+str(p)+' -Wl,-rpath,'+str(p) for p in paths)
netlibs=libdirs+' -l:libicuuc.so.74 -lssl -lcrypto -lz -lbrotlidec -lzstd -lm'
libs=netlibs+' '+str(v8/'libexec/libicuuc.so')+' -Wl,-rpath-link,'+str(v8/'libexec')+' -lfreetype -lharfbuzz -lSDL3 -lv8 -lv8_libplatform -lv8_libbase -lwgpu_native -lavcodec -lavformat -lavutil -lswscale -lswresample -lpng -ljpeg -lwebp -lwebpdemux -lsharpyuv -lgif -lfontconfig -lX11 -lpthread -ldl'
command=['make','-s','-j3','-f','Makefile','-f',str(out/'baseline-tests.mk'),'BUILD='+build,'CC=gcc','CXX=g++','OPT=-O1 -g0 -DNDEBUG','INC='+' '.join('-I'+p for p in includes),'LIBDIRS='+libdirs,'BASELIBS='+libdirs+' -l:libicuuc.so.74 -lm','NETLIBS='+netlibs,'LIBS='+libs,'RTC=0','WT=0',build+'/jsrun',build+'/test_worker_lifecycle',build+'/test_worker_async_lifecycle']
(out/'baseline-command.json').write_text(json.dumps(command,indent=2)+'\n')
env=dict(os.environ);env['LUMEN_ICU_DATA']=str(v8/'libexec/icudtl.dat');env['LD_LIBRARY_PATH']=':'.join(str(p) for p in paths)
(out/'runtime-env.json').write_text(json.dumps({k:env[k] for k in ['LUMEN_ICU_DATA','LD_LIBRARY_PATH']},indent=2)+'\n')
started=time.monotonic();process=subprocess.run(command,env=env)
print(json.dumps({'stage':'baseline-build','exitCode':process.returncode,'durationSeconds':time.monotonic()-started,'build':build,'v8':'15.7.112.12','rtc':False,'webtransport':False}),flush=True)
raise SystemExit(process.returncode)
