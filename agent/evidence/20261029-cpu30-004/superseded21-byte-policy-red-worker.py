from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,re,shutil,subprocess,sys,time
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
out=Path('out/lume-20261029-cpu30/html-default-boundary-red-1');out.mkdir(parents=True,exist_ok=False)
began=time.monotonic();started=datetime.now(timezone.utc).isoformat();stages=[]
build='build-apple-load-20261029';extra=out/'policy-tests.mk'
extra.write_text('$(BUILD)/test_html_default_policy: $(call OBJ,tests/test_html_default_policy.c $(BASE_SRC) $(NET_SRC) $(wildcard src/dom/*.c) src/html/html.c src/html/entities.c)\n\t$(CC) $(OPT) $(filter %.o,$^) -o $@ $(NETLIBS)\n')
def stage(name,command,seconds):
 start=time.monotonic();prefix=out/name
 p=subprocess.run([sys.executable,'tools/monitor_process.py','--duration',str(seconds),'--settle','0','--output',str(prefix),'--',*command],capture_output=True,text=True,timeout=seconds+20)
 (out/(name+'-driver.log')).write_text(p.stdout+p.stderr);meta=json.loads(prefix.with_suffix('.json').read_text());text=prefix.with_suffix('.log').read_text()
 stages.append({'name':name,'durationSeconds':round(time.monotonic()-start,3),'exitCode':p.returncode,'naturalExitCode':meta.get('natural_exit_code'),'collectorError':meta.get('collector_error'),'endedByMonitor':meta.get('ended_by_monitor'),'monitor':str(prefix.with_suffix('.json')),'monitorSHA256':sha(prefix.with_suffix('.json')),'logSHA256':sha(prefix.with_suffix('.log'))})
 if meta.get('collector_error') or meta.get('ended_by_monitor'):raise RuntimeError('Monitor failure')
 return meta.get('natural_exit_code'),text
code,text=stage('build',['make','-f','Makefile','-f',str(extra),'-j3','BUILD='+build,'MODE=release','RTC=1','WT=1',build+'/test_html_default_policy'],120)
if code:raise RuntimeError('Boundary RED build failed')
source=Path(build)/'test_html_default_policy';binary=out/('test_html_default_policy-'+sha(source)[:16]);shutil.copy2(source,binary);binary.chmod(0o555)
code,text=stage('fixture',[str(binary)],20)
expected={'en-US unlabelled HTML defaults Windows1252','absent response MIME retains HTML default','unlabelled plaintext uses text-document default','unsupported header permits HTML default'}
failures={line[5:] for line in text.splitlines() if line.startswith('FAIL ')}
confirmed=code==1 and failures==expected and 'RESULTS html-default-boundary FAIL (17/21 checks)' in text
summary={'startedUTC':started,'finishedUTC':datetime.now(timezone.utc).isoformat(),'durationSeconds':round(time.monotonic()-began,3),'redConfirmed':confirmed,'fixtureSHA256':sha('tests/test_html_default_policy.c'),'binary':{'path':str(binary),'sha256':sha(binary)},'stages':stages,'failures':sorted(failures),'result':{'total':21,'passed':17 if confirmed else None,'failed':4 if confirmed else None},'sourcePreimages':{p:sha(p) for p in ['src/html/html.c','src/js/windows.cc','Makefile']}}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));raise SystemExit(0 if confirmed else 1)
