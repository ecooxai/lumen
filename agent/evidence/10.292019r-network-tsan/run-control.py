from pathlib import Path
import subprocess,time,json,os
out=Path('out/10292019r-network-tsan').resolve();start=time.monotonic()
command=['gcc','-O1','-g','-fsanitize=thread','-fno-omit-frame-pointer','-pthread',str(out/'control.c'),'-o',str(out/'control')]
(out/'control-command.json').write_text(json.dumps(command,indent=2)+'\n')
r=subprocess.run(command)
if r.returncode:raise SystemExit(r.returncode)
r=subprocess.run([str(out/'control')],env=dict(os.environ,TSAN_OPTIONS='halt_on_error=1'))
print(json.dumps({'stage':'TSan runtime control','durationSeconds':time.monotonic()-start,'exitCode':r.returncode}),flush=True)
raise SystemExit(r.returncode)
