from pathlib import Path
from datetime import datetime,timezone
import gzip,hashlib,importlib.util,json,os,tarfile,tempfile,time
R=Path('/workspace/scratch/2b90c4e9e7b5/project/lumen');os.chdir(R)
spec=importlib.util.spec_from_file_location('independent_audit',R/'tools/apple-session-gpt6as/audit_wpt_candidate.py');A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)
def digest(value):return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def atomic_bytes(path,data):
 fd,name=tempfile.mkstemp(prefix=path.name+'.',suffix='.tmp',dir=path.parent)
 with os.fdopen(fd,'wb')as f:f.write(data);f.flush();os.fsync(f.fileno())
 os.replace(name,path);directory=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY)
 try:os.fsync(directory)
 finally:os.close(directory)
 if path.stat().st_size!=len(data)or A.sha(path)!=hashlib.sha256(data).hexdigest():raise ValueError('Final output atomic readback differs:'+str(path))
def atomic_json(path,value):atomic_bytes(path,(json.dumps(value,indent=2)+'\n').encode())
def check_summary(stored,computed,label):
 for key in ['scheduled','recorded','fullyPassing','unrun','passPercentOfRecorded','assertions','harnessStatuses']:
  if stored.get(key)!=computed[key]:raise ValueError('Stored summary differs from raw '+label+':'+key)
 if stored.get('unqualifiedEvidenceVariants',0)!=0:raise ValueError('Stored summary has unqualified evidence:'+label)
p=R/'out/lumen-10292019r/chromium-1515-atomic-005';out=R/'out/lumen-10292019r/chromium-1515-accepted';out.mkdir(exist_ok=False);start=time.monotonic();plan=A.read(p/'cohort.json');receipt=A.read(p/'finalized-receipt.json');final=A.read(p/'finalized-results.json');rows=final['rows']
if plan['wptRevision']!='7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'or plan['fullFrozenScheduleSHA256']!='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe'or digest(plan['frozenSchedule'])!=plan['fullFrozenScheduleSHA256']:raise ValueError('Pinned schedule identity differs')
if any(digest(v)!=plan['groupScheduleSHA256'][k]for k,v in plan['groups'].items()):raise ValueError('Frozen group schedule digest differs')
for name,key in [('tests/wpt/full_runner.py','collector_sha256'),('tests/wpt/testharnessreport.js','reporter_sha256'),('tools/monitor_process.py','monitor_sha256'),('tests/wpt/chromium_local_fixture.py','chromium_routing_helper_sha256')]:
 if A.sha(R/name)!=plan['protocol'][key]:raise ValueError('Execution helper changed:'+name)
if final['count']!=1515 or len(rows)!=1515:raise ValueError('Complete1515 immutable evidence required; partial attempt preserved')
if A.sha(p/'finalized-results.json')!=receipt['finalizedResultsSHA256']:raise ValueError('Final aggregate hash differs')
if A.sha(plan['binary']['path'])!=plan['binary']['sha256']or A.sha(R/'out/lumen-10292019r/chromium_control_atomic_fixture.py')!=plan['protocol']['runner_sha256']:raise ValueError('Frozen browser or driver changed')
actual=A.summarize_rows(rows,1515)
for label,value in [('receipt',receipt['summary']),('finalaggregate',final['summary']),('summaryfile',A.read(p/'summary.json'))]:check_summary(value,actual,label)
if actual['measurementErrorVariants']or receipt.get('error')or receipt.get('chromiumCleanupError')or receipt['chromiumCleanup'].get('errors')or not receipt['chromiumCleanup']['process'].get('process_group_gone'):raise ValueError('Reference infrastructure or cleanup unqualified')
if set(A.indexed(rows))!=set(A.indexed(plan['frozenSchedule']))or len(A.indexed(plan['frozenSchedule']))!=1515:raise ValueError('Full schedule identity differs')
merged=[];groups={};file_set=set();case_files=[]
for group in receipt['requestedGroups']:
 d=p/group;meta=A.read(d/'run.json');scheduled=A.read(d/'schedule.json');j=list(A.iter_jsonl(d/'results.jsonl'))if hasattr(A,'iter_jsonl')else[json.loads(x)for x in(d/'results.jsonl').read_text().splitlines()if x.strip()];agg=A.read(d/'finalized-results.json');cases=sorted(d.glob('case-*.result.json'));per=[A.read(x)for x in cases]
 if j!=agg['rows']or j!=per or len(j)!=len(scheduled)or len(j)!=agg['count']or not meta['finished_schedule']:raise ValueError('Immutable per-case/group evidence reconciliation differs:'+group)
 if meta['persistence']['finalizedResultsSHA256']!=A.sha(d/'finalized-results.json')or meta['persistence']['resultsJSONLSHA256']!=A.sha(d/'results.jsonl'):raise ValueError('Group aggregate hashes differ:'+group)
 if scheduled!=plan['groups'][group]or set(A.indexed(j))!=set(A.indexed(scheduled)):raise ValueError('Frozen group schedule differs:'+group)
 if any(meta['signature'][k]!=plan['protocol'][k]for k in plan['protocol']):raise ValueError('Group protocol differs:'+group)
 group_summary=A.summarize_rows(j,len(scheduled));check_summary(meta['summary'],group_summary,group)
 if meta['persistence']['count']!=len(j)or meta['persistence']['perCaseFiles']!=len(cases):raise ValueError('Case persistence metadata differs:'+group)
 merged.extend(j);groups[group]={'summary':group_summary,'persistence':meta['persistence']};case_files.extend(cases)
 for pat in ['case-*.result.json','case-*.console.log','case-*.metrics.jsonl']:
  file_set.update(d.glob(pat))
 file_set.update([d/'run.json',d/'schedule.json'])
if merged!=rows:raise ValueError('Global rows differ from every immutable case')
proof=A.read(R/'out/lumen-10292019r/chromium-1515-proof/expected-reference-transport.json');up=R/'output/wpt-10290244/upstream'
if proof['wptRevision']!=plan['wptRevision']or proof['frozenScheduleSHA256']!=plan['fullFrozenScheduleSHA256']or proof['selectedSourceCount']!=235 or len(proof['sourceSHA256'])!=457 or any(plan['protocol'][k]!=v for k,v in proof['protocol'].items()):raise ValueError('Transport source proof does not bind this reference')
if any(A.sha(up/n)!=h for n,h in proof['sourceSHA256'].items()):raise ValueError('Pinned dependency changed after reference')
file_set.update(p/name for name in ['cohort.json','finalized-receipt.json','summary.json','active-invocation.json'])
file_set.update((p/'chromium-session').glob('chromium.*'))
file_set.update((p/'chromium-session').glob('chromium-process.log'))
for name in ['out/lumen-10292019r/chromium_control_atomic_fixture.py','out/lumen-10292019r/chromium-1515-proof/expected-reference.json','out/lumen-10292019r/chromium-1515-proof/expected-reference-transport.json','tests/wpt/chromium_local_fixture.py','tests/wpt/full_runner.py','tests/wpt/testharnessreport.js','tools/monitor_process.py']:
 file_set.add(R/name)
manifest=[];raw=out/'GPT6-Astra-Pro_builtin-agents_localvm_Chromium156_Lumen-10.292019r_full1515_raw.tar.gz';temp=raw.with_suffix('.tmp')
with tarfile.open(temp,'w:gz')as tar:
 for f in sorted(file_set):
  if f.is_symlink()or not f.is_file()or any(part.startswith('lumen-wpt-chrome-')for part in f.parts):raise ValueError('Raw evidence allowlist contains a profile or nonfile')
  identity={'path':str(f.relative_to(R)),'bytes':f.stat().st_size,'sha256':A.sha(f)};manifest.append(identity);tar.add(f,arcname=identity['path'],recursive=False)
with temp.open('rb')as f:os.fsync(f.fileno())
os.replace(temp,raw)
if any(A.sha(R/x['path'])!=x['sha256']for x in manifest):raise ValueError('Reference evidence changed during archive')
ledger=[{k:r.get(k)for k in ['test','type','url','status','subtest_count','subtests']}for r in rows]
reference={'schema':2,'acceptedReference':True,'scope':'Actual Chromium156 Linux complete frozen1515 priority reference. Same pinned test bodies and protocol; different platform from nativeMac. Not full officialWPT or general Chrome parity.','summary':actual,'binary':plan['binary'],'version':receipt['chromiumVersion'],'protocol':plan['protocol'],'frozenScheduleSHA256':plan['fullFrozenScheduleSHA256'],'fullFrozenVariantCount':1515,'groups':plan['groups'],'groupScheduleSHA256':plan['groupScheduleSHA256'],'sourceProof':proof,'sourceProofSHA256':A.sha(R/'out/lumen-10292019r/chromium-1515-proof/expected-reference-transport.json'),'startedUTC':receipt['startedUTC'],'finishedUTC':receipt['finishedUTC'],'durationSeconds':receipt['durationSeconds'],'globalAggregateSHA256':receipt['finalizedResultsSHA256'],'all1515ImmutableCaseCount':len(case_files),'rawArchiveSHA256':A.sha(raw),'rows':ledger}
reference_path=out/'GPT6-Astra-Pro_builtin-agents_localvm_Chromium156_Lumen-10.292019r_full1515_reference.json';data=(json.dumps(reference,ensure_ascii=True,separators=(',',':'))+'\n').encode();atomic_bytes(reference_path,data)
compressed=reference_path.with_suffix('.json.gz')
fd,name=tempfile.mkstemp(prefix='reference.',suffix='.tmp',dir=out)
with os.fdopen(fd,'wb')as stream:
 with gzip.GzipFile(fileobj=stream,mode='wb',mtime=0)as archive:archive.write(data)
 stream.flush();os.fsync(stream.fileno())
os.replace(name,compressed);decompressed_hash=hashlib.sha256();decompressed_count=0
with gzip.open(compressed,'rb')as stream:
 while block:=stream.read(1024*1024):decompressed_hash.update(block);decompressed_count+=len(block)
if decompressed_count!=len(data)or decompressed_hash.hexdigest()!=hashlib.sha256(data).hexdigest():raise ValueError('Compressed reference readback differs')
acceptance={'schema':2,'accepted':True,'createdUTC':datetime.now(timezone.utc).isoformat(),'scope':reference['scope'],'fullOfficialWptPassPercent':None,'summary':actual,'binary':plan['binary'],'version':receipt['chromiumVersion'],'startedUTC':receipt['startedUTC'],'finishedUTC':receipt['finishedUTC'],'durationSeconds':receipt['durationSeconds'],'auditSeconds':time.monotonic()-start,'groups':groups,'protocol':plan['protocol'],'completeAggregateSHA256':receipt['finalizedResultsSHA256'],'sourceProofSHA256':reference['sourceProofSHA256'],'sourceProofFiles':len(proof['sourceSHA256']),'rawArchive':{'path':str(raw.relative_to(R)),'bytes':raw.stat().st_size,'sha256':A.sha(raw),'files':manifest},'reference':{'path':str(reference_path.relative_to(R)),'bytes':len(data),'sha256':A.sha(reference_path),'compressedPath':str(compressed.relative_to(R)),'compressedBytes':compressed.stat().st_size,'compressedSHA256':A.sha(compressed)},'excludedPriorControls':'All earlier incomplete, routing-invalid and sparse-missing-dependency controls remain excluded. No historical cases were reused.','rawArchiveScope':'All1515 immutable case result JSON and available console/1Hz telemetry; metadata preserves hashes of redundant final group/global JSONL/aggregates reconciled before archival. Browser profiles excluded.'}
atomic_json(out/'acceptance.json',acceptance)
compact={k:v for k,v in acceptance.items()if k!='rawArchive'};compact['rawArchive']={k:v for k,v in acceptance['rawArchive'].items()if k!='files'};compact['rawManifestFiles']=len(manifest);atomic_json(out/'raw-manifest.json',manifest);atomic_json(out/'compact-acceptance.json',compact)
print(json.dumps({'accepted':True,'summary':actual,'auditSeconds':acceptance['auditSeconds'],'reference':acceptance['reference'],'rawArchiveBytes':raw.stat().st_size,'rawArchiveFiles':len(manifest),'receipt':str((out/'acceptance.json').relative_to(R))},separators=(',',':')))
