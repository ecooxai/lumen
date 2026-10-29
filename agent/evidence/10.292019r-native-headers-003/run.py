from pathlib import Path
import datetime,hashlib,json,os,subprocess,time
root=Path(__file__).resolve().parents[3]
out=Path(__file__).resolve().parent
env=dict(os.environ);env.update(json.loads((root/'out/10292019r-linux-worker/runtime-env.json').read_text()))
rows=[]
for kind in ['baseline','candidate']:
 manifest=json.loads((root/f'out/10292019r-linux-worker/{kind}-binaries.json').read_text());binary=root/f'out/10292019r-linux-worker/build-{kind}/jsrun';expected=manifest['files'][str(binary.relative_to(root))]['sha256'];assert hashlib.sha256(binary.read_bytes()).hexdigest()==expected
 began=time.monotonic();start=datetime.datetime.now(datetime.timezone.utc).isoformat()
 command=['python3',str(out/'monitor_process.frozen.py'),'--duration','80','--settle','0','--output',str(out/kind),'--','python3',str(out/'test_worker_response_headers.frozen.py'),'--binary',str(binary),'--output',str(out/(kind+'-receipt.json'))]
 with (out/(kind+'-driver.log')).open('w') as log:run=subprocess.run(command,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90)
 row={'stage':kind,'started':start,'ended':datetime.datetime.now(datetime.timezone.utc).isoformat(),'durationSeconds':time.monotonic()-began,'exitCode':run.returncode,'binarySHA256':expected,'sourceManifestSHA256':hashlib.sha256((root/f'out/10292019r-linux-worker/{kind}-source-manifest.json').read_bytes()).hexdigest(),'icuSHA256':hashlib.sha256((binary.parent/'icudtl.dat').read_bytes()).hexdigest()};rows.append(row);(out/'pair-receipt.json').write_text(json.dumps({'kind':'actual-linux-lumen-headless-regression','notAppleRuntime':True,'officialWPT':False,'fixtureSHA256':hashlib.sha256((out/'test_worker_response_headers.frozen.py').read_bytes()).hexdigest(),'monitorSHA256':hashlib.sha256((out/'monitor_process.frozen.py').read_bytes()).hexdigest(),'stages':rows},indent=2)+'\n');print(json.dumps(row),flush=True)
