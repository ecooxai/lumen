"""Build the reviewed tag-registry candidate and validate it in real native V8."""
import datetime,hashlib,json,os,pathlib,shutil,subprocess,sys
ROOT=pathlib.Path.cwd().resolve();OUT=ROOT/'output/session-20261029/native-clone-brands-green';BUILD='build-apple-load-20261029'
SOURCE='src/js/prelude/04-tags.js';SOURCE_SHA='b0549b35e1feab47aa977f0aad14907275c6432884ea749b87e19d52ea1ad9bf'
FILES={SOURCE:SOURCE_SHA,'tests/js/html-interface-clone-brands.html':'b2f8fcfc554cd5625d3a5999e981d9ba967571cfc0afd3bac9466e50ce715232','tests/test_html_interface_clone_brands.py':'7f8f6c0dd74ab9c751cee3292ffb3755fa2630c9ae6b84b940ad399242cc7583'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
if '--worker' not in sys.argv:
 subprocess.run(['python3','tools/apple-session-gpt6as/launch_job.py','--output','output/session-20261029/native-clone-brands-green-job','--timeout','420','--','python3',str(pathlib.Path(__file__).resolve()),'--worker'],check=True);raise SystemExit(0)
for name,expected in FILES.items():
 if sha(ROOT/name)!=expected:raise RuntimeError('Reviewed constructor candidate changed: '+name)
dirty=subprocess.check_output(['git','diff','--name-only','--','src','Makefile','ios','tests/jsrun.c'],text=True).splitlines()
if dirty!=[SOURCE]:raise RuntimeError('Isolate constructor build from other production edits: '+repr(dirty))
OUT.mkdir(parents=True,exist_ok=False)
tracked=subprocess.check_output(['git','ls-files','-z','--','src','Makefile','ios','tests/jsrun.c'],text=True).split('\0');hashes={p:sha(ROOT/p)for p in tracked if p and(ROOT/p).is_file()}
(OUT/'compiled-source-sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
rows=[];summary={'startedUTC':utc(),'sourceBaseCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'candidateFiles':FILES,'build':BUILD,'rtcEnabled':True,'webtransportEnabled':True,'stages':rows,'artifacts':{},'scope':'18 native HTML element mappings to16 canonical interface prototypes, clone independence and foreign namespace controls. No complete-IDL claim.'}
def save():(OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
def measured(name,cmd,duration,marker=None):
 prefix=OUT/name;r=subprocess.run(['python3','tools/monitor_process.py','--duration',str(duration),'--settle','0','--output',str(prefix.relative_to(ROOT)),'--',*cmd],text=True,capture_output=True,timeout=duration+30)
 (OUT/(name+'-collector.log')).write_text(r.stdout+r.stderr)
 report=json.loads(prefix.with_suffix('.json').read_text());log=prefix.with_suffix('.log').read_text(errors='replace')
 passed=report.get('natural_exit_code')==0 and not report.get('collector_error')and not report.get('ended_by_monitor')and(marker is None or marker in log)
 rows.append({'stage':name,'passed':passed,'naturalExitCode':report.get('natural_exit_code'),'resultLines':[line for line in log.splitlines()if line.startswith('RESULTS')or line.startswith('CLONE_BRANDS_RECEIPT')],'intervalCpuSamples':report.get('interval_cpu_samples'),'rssPeakMiB':report.get('rss_peak_mb')});save();return passed
targets=['lumen','jsrun','test_frame_removal','test_wasm_async_deadline']
built=measured('build',['make','-j3','BUILD='+BUILD,'MODE=release','RTC=1','WT=1',*[BUILD+'/'+name for name in targets]],180)
if built:
 for name in targets:
  binary=ROOT/BUILD/name;digest=sha(binary);row={'path':str(binary.relative_to(ROOT)),'sha256':digest};summary['artifacts'][name]=row
  if name in('lumen','jsrun'):
   copy=ROOT/BUILD/(name+'-clone-brands-'+digest[:16]);shutil.copy2(binary,copy)
   if sha(copy)!=digest:raise RuntimeError('Native constructor snapshot copy mismatch')
   row['snapshot']=str(copy.relative_to(ROOT))
 artifact=summary['artifacts']['jsrun'];measured('clone-brands',['python3','tests/test_html_interface_clone_brands.py','--binary',artifact['snapshot'],'--expected-sha256',artifact['sha256']],30,'RESULTS html-interface-clone-brands PASS (132/132 checks)')
 measured('xml-documents',['python3','tests/test_xml_frame_documents.py','--binary',artifact['snapshot']],40,'RESULTS xml-frame-documents PASS (24/24 checks)')
 for runtime,name in [('jsrun','jsrun'),('application','lumen')]:
  measured('frame-load-'+runtime,['python3','tests/test_frame_load_order.py','--binary',summary['artifacts'][name]['snapshot'],'--runtime',runtime],30,'RESULTS frame-load-order '+runtime+' PASS (14/14 checks)')
 for name,marker in [('test_frame_removal','RESULTS frame-removal PASS'),('test_wasm_async_deadline','RESULTS wasm-async-deadline PASS')]:
  profile=OUT/(name+'-profile');profile.mkdir();measured(name,['env','LUMEN_PROFILE_DIR='+str(profile),str(ROOT/BUILD/name)],60,marker)
for name,expected in hashes.items():
 if sha(ROOT/name)!=expected:raise RuntimeError('Compiled source changed before constructor validation completed: '+name)
summary['passed']=built and all(row['passed']for row in rows);summary['finishedUTC']=utc();save();print(json.dumps(summary));sys.exit(not summary['passed'])
