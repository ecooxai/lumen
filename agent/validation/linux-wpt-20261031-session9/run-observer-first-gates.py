#!/usr/bin/env python3
"""Run supported unchanged API cohort and authored v2, then enforce both contracts."""
from pathlib import Path
import argparse,subprocess,json,datetime,sys,collections
ap=argparse.ArgumentParser();ap.add_argument('--iteration',type=int,required=True);a=ap.parse_args();s=Path(__file__).resolve().parent;root=Path.cwd();out=s/f'candidate{a.iteration}-observer-first-execution.json';assert not out.exists()
now=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat()
commands=[['python3',str(s/'run-cohort-v3.py'),'--scope','qa-hidden-observer-visibility','--kind','lumen','--type','assertions','--iteration',str(a.iteration)],['python3',str(s/'qa-io-frame-cadence/run-v2.py'),'--iteration',str(a.iteration)]]
started=now();jobs=[]
for i,cmd in enumerate(commands):
 log=s/f'candidate{a.iteration}-observer-first-{i}.log';stream=log.open('x');p=subprocess.Popen(cmd,stdout=stream,stderr=subprocess.STDOUT);jobs.append((p,stream,cmd,str(log)))
results=[]
for p,stream,cmd,log in jobs:
 code=p.wait();stream.close();results.append({'command':cmd,'returncode':code,'log':log})
with (s/f'candidate{a.iteration}-observer-first-audit.log').open('x') as stream:
 audit_code=subprocess.run(['python3',str(s/'audit-hidden-api.py'),'--iteration',str(a.iteration)],stdout=stream,stderr=subprocess.STDOUT).returncode
base=s/'qa-hidden-observer-visibility';before=[json.loads(x) for x in (base/'lumen229-assertions/native/results.jsonl').read_text().splitlines()];after=[json.loads(x) for x in (base/f'lumen{a.iteration}-assertions/native/results.jsonl').read_text().splitlines()]
def identities(rows):
 result={}
 for r in rows:
  seen=collections.Counter()
  for sub in r['subtests']:
   identity=(r['url'],sub[0],seen[sub[0]]);seen[sub[0]]+=1;result[identity]=sub[1]
 return result
old=identities(before);new=identities(after);losses=[list(k) for k,v in old.items() if v=='PASS' and new.get(k)!='PASS'];exact=set(old)==set(new);byurl={r['url']:r for r in after};required=['/css/css-contain/content-visibility/content-visibility-030.html','/css/css-contain/content-visibility/content-visibility-031.html'];observers_pass=all(u in byurl and byurl[u]['status']=='OK' and len(byurl[u]['subtests'])==1 and byurl[u]['subtests'][0][1]=='PASS' for u in required)
diagnostic=json.loads((s/f'qa-io-frame-cadence/jsrun{a.iteration}-v2/receipt.json').read_text());idle=diagnostic.get('quiescent_idle',[]);strict=all(diagnostic.get(k) is True for k in ['legacy_geometry_pass','cadence_pass','all_phases_completed','pins_unchanged','source_unchanged','git_unchanged']) and diagnostic.get('returncode')==0 and diagnostic.get('timed_out') is False and diagnostic.get('monitor_returncodes')==[0,0] and diagnostic.get('failures')==[] and len(idle)==1 and idle[0]['elapsed']>=3300 and idle[0]['geometryReads']==0 and idle[0]['notifications']==0
valid=not any(x['returncode'] for x in results) and audit_code==0 and exact and len(new)==23 and not losses and observers_pass and strict and all(r['status']=='OK' for r in after)
r={'candidate':a.iteration,'started_utc':started,'finished_utc':now(),'maximum_concurrent_browser_lanes':2,'execution':results,'api_audit_returncode':audit_code,'exact23_identities_preserved':exact and len(new)==23,'api_pass_count':sum(v=='PASS' for v in new.values()),'api_total':len(new),'prior229_pass_losses':losses,'IO030_and_RO031_pass':observers_pass,'strict_authored_v2_pass':strict,'quiescent_idle':idle,'qualification_pass':valid,'authored_fixture_not_WPT':True,'remaining_checkVisibility_failures':[list(k) for k,v in new.items() if v!='PASS']};out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r));sys.exit(0 if valid else 2)
