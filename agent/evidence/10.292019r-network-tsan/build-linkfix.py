from pathlib import Path
import json,subprocess,time
out=Path('out/10292019r-network-tsan');start=time.monotonic()
r=subprocess.run(json.loads((out/'command-linkfix.json').read_text()))
print(json.dumps({'stage':'TSan native C network build','durationSeconds':time.monotonic()-start,'exitCode':r.returncode}),flush=True)
raise SystemExit(r.returncode)
