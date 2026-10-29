from pathlib import Path
from datetime import datetime,timezone
import hashlib,json,subprocess,sys,time

root=Path.cwd().resolve();out=Path('out/lume-20261029-cpu30/apple-sdk-direct-2');out.mkdir(parents=True,exist_ok=False)
base=Path('/Applications/Xcode-27.0-RC.app/Contents/Developer')
clang=Path('/Library/Developer/CommandLineTools/usr/bin/clang')
source=out/'sdk_probe.m';source.write_text('#import <UIKit/UIKit.h>\nint main(void) { @autoreleasepool { return (int)UIDevice.currentDevice.userInterfaceIdiom; } }\n')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();began=time.monotonic()
report={'startedUTC':datetime.now(timezone.utc).isoformat(),'scope':'Tiny project-local UIKit SDK compile/link feasibility probe using the existing matching CLT compiler/resource directory and explicit installed iOS SDKs. No Lume app, IPA, simulator install, signing-identity use, SDK modification, or global Xcode selection.','compiler':str(clang),'compilerSelectionReason':'Xcode clang and CLT clang share build clang-2100.3.34.2; only CLT has the nonempty ptrcheck.h resource. No headers are copied or edited.','sourceSHA256':sha(source),'stages':[]}
def save():
 p=out/'summary.tmp';p.write_text(json.dumps(report,indent=2)+'\n');p.replace(out/'summary.json')
save()
for name,platform,target in [('sim','iPhoneSimulator.platform','arm64-apple-ios16.0-simulator'),('ios','iPhoneOS.platform','arm64-apple-ios16.0')]:
 sdk=base/'Platforms'/platform/'Developer/SDKs'/('iPhoneSimulator.sdk' if name=='sim' else 'iPhoneOS.sdk')
 artifact=out/('sdk-probe-'+name);prefix=out/name
 command=[str(clang),'-target',target,'-isysroot',str(sdk),'-fobjc-arc','-O0',str(source),'-framework','UIKit','-framework','Foundation','-o',str(artifact)]
 row={'platform':name,'target':target,'sdk':str(sdk),'startedUTC':datetime.now(timezone.utc).isoformat(),'command':command};started=time.monotonic()
 try:
  p=subprocess.run([sys.executable,'tools/monitor_process.py','--duration','20','--settle','0','--output',str(prefix),'--',*command],capture_output=True,text=True,timeout=35)
  (out/(name+'-driver.log')).write_text(p.stdout+p.stderr)
  meta=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
  passed=meta.get('natural_exit_code')==0 and not meta.get('ended_by_monitor') and not meta.get('collector_error') and artifact.is_file()
  row.update(passed=passed,monitor=str(prefix.with_suffix('.json')),monitorSHA256=sha(prefix.with_suffix('.json')),naturalExitCode=meta.get('natural_exit_code'),collectorError=meta.get('collector_error'),endedByMonitor=meta.get('ended_by_monitor'),intervalSamples=meta.get('interval_cpu_samples'),peakCPUPercent=meta.get('interval_cpu_peak_percent'),peakRSSMiB=meta.get('rss_peak_mb'),logTail=log[-3000:])
  if passed:
   row['artifact']={'path':str(artifact),'sha256':sha(artifact),'bytes':artifact.stat().st_size,'file':subprocess.check_output(['file',str(artifact)],text=True,timeout=5).strip()}
   result=subprocess.run(['otool','-l',str(artifact)],capture_output=True,text=True,timeout=5)
   rows=result.stdout.splitlines();contexts=[]
   for i,line in enumerate(rows):
    if 'LC_BUILD_VERSION' in line:contexts.append('\n'.join(rows[max(0,i-1):i+9]))
   row['platformLoadCommand']=contexts
 except Exception as error:row.update(passed=False,error=type(error).__name__+': '+str(error))
 row.update(finishedUTC=datetime.now(timezone.utc).isoformat(),durationSeconds=round(time.monotonic()-started,3));report['stages'].append(row);save()
report.update(finishedUTC=datetime.now(timezone.utc).isoformat(),durationSeconds=round(time.monotonic()-began,3),allPassed=all(row.get('passed') for row in report['stages']))
save();print(json.dumps(report,indent=2));raise SystemExit(0 if report['allPassed'] else 1)
