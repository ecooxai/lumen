from pathlib import Path
import hashlib,json,subprocess,sys,time
root=Path.cwd().resolve();base=root/'out/10292019r-mac/wpt-final-004';plan=base/'cohort/cohort.json';native=json.loads((root/'out/10292019r-mac/native-worker-004/summary.json').read_text())
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if str(root)!='/Users/devin/repos/lumen':raise SystemExit('Wrong workspace')
p=json.loads(plan.read_text());expected=native['artifacts']['lumen']['sha256']
if p['binary']['sha256']!=expected or sha(p['binary']['path'])!=expected or not native['passed']:raise SystemExit('Native artifact not qualified')
manifest=json.loads((root/native['compiledSourceManifest']).read_text())
if any(sha(root/k)!=v for k,v in manifest.items()):raise SystemExit('Compiled source changed')
receipt=plan.parent/'original-six-receipt.json'
if receipt.exists():raise SystemExit('Original-six receipt already exists; inspect it instead of overwriting')
start=time.monotonic()
with (base/'original-six-driver.log').open('w')as log:
 result=subprocess.run([sys.executable,'tools/monitor_process.py','--duration','260','--settle','0','--output',str(base/'original-six-driver'),'--',sys.executable,'tools/apple-session-gpt6as/run_task_wpt_102900614.py','original','--plan',str(plan)],stdout=log,stderr=subprocess.STDOUT,timeout=295)
d=json.loads(receipt.read_text())if receipt.exists()else{};m=json.loads((base/'original-six-driver.json').read_text());valid=d.get('passed')and d.get('files')==6 and d.get('fullyPassing')==6 and d.get('assertions')=={'PASS':1850}and m.get('natural_exit_code')==0 and not m.get('collector_error')and not m.get('ended_by_monitor')
print(json.dumps({'passed':bool(valid),'files':d.get('files'),'fullyPassing':d.get('fullyPassing'),'assertions':d.get('assertions'),'binarySHA256':expected,'seconds':round(time.monotonic()-start,3),'receipt':str(receipt.relative_to(root))}),flush=True)
raise SystemExit(0 if valid else 1)
