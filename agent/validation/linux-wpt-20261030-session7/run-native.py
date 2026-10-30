#!/usr/bin/env python3
import pathlib,subprocess,sys,datetime,time,json,hashlib,xml.etree.ElementTree as ET
ROOT=pathlib.Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'agent/validation'))
from paired_wpt import monitor,endmonitor
candidate=int(sys.argv[1]);out=ROOT/'agent/validation/linux-wpt-20261030-session7'/('native'+str(candidate));out.mkdir(exist_ok=False)
state=json.loads((ROOT/'agent/validation'/('iteration-'+str(candidate))/'build-state.json').read_text());binary=ROOT/state['binary'];expected=state['binary_sha256']
if hashlib.sha256(binary.read_bytes()).hexdigest()!=expected:raise SystemExit('Candidate identity mismatch')
command=[sys.executable,str(ROOT/'tools/codex_compile.py'),'ctest','--test-dir',str(ROOT/'build/codex-native'),'-j','1','--exclude-regex','^(url|url_host_nul|data_url)$','--output-on-failure','--output-junit',str(out/'results.xml')]
start=datetime.datetime.now(datetime.timezone.utc).isoformat();clock=time.monotonic()
with (out/'ctest.log').open('wb') as log,(out/'cgroup.log').open('wb') as cglog:
 cg=subprocess.Popen([sys.executable,str(ROOT/'agent/progress/cgroup_monitor.py'),'--output',str(out/'cgroup.csv'),'--duration','1800'],cwd=ROOT,stdin=subprocess.DEVNULL,stdout=cglog,stderr=cglog)
 p=subprocess.Popen(command,cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=log)
 (out/'running.json').write_text(json.dumps({'pid':p.pid,'candidate':candidate,'source':state['head'],'started_utc':start,'command':command},indent=2)+'\n')
 mon=monitor(p.pid,out/'resources','native non-URL candidate '+str(candidate));code=p.wait();endmonitor(mon);cg.terminate();cg.wait(timeout=5)
counts={}
if (out/'results.xml').is_file():
 t=ET.parse(out/'results.xml').getroot();entries=list(t.iter('testcase'));counts={'entries':len(entries),'failures':sum(e.find('failure') is not None for e in entries),'skipped':sum(e.find('skipped') is not None for e in entries),'deferred_present':[e.attrib.get('name') for e in entries if e.attrib.get('name') in ['url','url_host_nul','data_url']]}
report={'candidate':candidate,'source_commit':state['head'],'binary_sha256':expected,'started_utc':start,'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'elapsed_s':time.monotonic()-clock,'exit_code':code,'counts':counts,'actual_owned_pid':p.pid,'owned_pid_remaining':pathlib.Path('/proc/'+str(p.pid)).exists(),'monitor_remaining':pathlib.Path('/proc/'+str(cg.pid)).exists(),'canonical_binary_unchanged':hashlib.sha256((ROOT/'build/codex-native/lumen').read_bytes()).hexdigest()==expected,'test_build_directory_not_modified_by_new_compilation':True}
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
raise SystemExit(code if code else (0 if counts.get('entries')==140 and not counts.get('failures') and not counts.get('skipped') and not counts.get('deferred_present') else 2))
