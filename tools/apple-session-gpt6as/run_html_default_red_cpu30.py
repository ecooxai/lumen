from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,subprocess,sys,time
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
out=Path('out/lume-20261029-cpu30/html-default-red-1');out.mkdir(parents=True,exist_ok=False)
build=json.loads(Path('out/lume-20261029-cpu30/native-codec-1/summary.json').read_text());artifact=build['artifacts']['lumen']
if not build['passed'] or artifact['sha256']!='9137de2fb8cba151b7a4ca4f21aa02771061068cd41aa4327de4e3e20d709553' or sha(artifact['path'])!=artifact['sha256']:raise RuntimeError('Frozen native9137 differs')
began=time.monotonic();started=datetime.now(timezone.utc).isoformat()
run=subprocess.run([sys.executable,'tests/test_html_default_policy.py','--binary',artifact['path'],'--output',str(out/'run')],capture_output=True,text=True,timeout=70)
(out/'fixture.log').write_text(run.stdout+run.stderr)
report=json.loads((out/'run/summary.json').read_text())
expected={'Top-level unlabelled HTML','cross-origin child does not inherit','sandbox opaque-origin child does not inherit','CSP sandbox opaque-origin child does not inherit'}
failures={r['name'] for r in report['checks'] if not r['ok']}
confirmed=run.returncode==1 and report['result']=={'total':25,'passed':21,'failed':4,'unresolvedLegacyControls':['UTF16LE parent encoding is not inherited','UTF16BE parent encoding is not inherited']} and failures==expected
summary={'startedUTC':started,'finishedUTC':datetime.now(timezone.utc).isoformat(),'durationSeconds':round(time.monotonic()-began,3),'redConfirmed':confirmed,'exitCode':run.returncode,'fixtureSHA256':sha('tests/test_html_default_policy.py'),'legacyFixtureSHA256':sha('tests/test_frame_encoding_fallback.py'),'artifact':artifact,'result':report['result'],'checks':report['checks'],'nativeReceipt':report['nativeReceipt'],'compiledSourceManifest':build['compiledSourceManifest'],'compiledSourceManifestSHA256':sha(build['compiledSourceManifest'])}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));raise SystemExit(0 if confirmed else 1)
