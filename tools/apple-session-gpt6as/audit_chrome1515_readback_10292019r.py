"""Independent bounded-memory readback of accepted full Chrome priority evidence."""
from pathlib import Path
from collections import Counter
from datetime import datetime,timezone
import argparse,gzip,hashlib,json,os,tarfile,tempfile,time
p=argparse.ArgumentParser();p.add_argument('--acceptance',type=Path,required=True);p.add_argument('--sha256',required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args();root=Path.cwd().resolve();started=time.monotonic()
def file_hash(path):
 with Path(path).open('rb')as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
if args.output.exists():raise RuntimeError('Preserve prior independent review')
if file_hash(args.acceptance)!=args.sha256:raise RuntimeError('Acceptance identity differs')
a=json.loads(args.acceptance.read_text())
if not a.get('accepted')or a['summary']['scheduled']!=1515 or a['summary']['recorded']!=1515:raise RuntimeError('Complete accepted1515 required')
manifest={v['path']:v for v in a['rawArchive']['files']}
if len(manifest)!=len(a['rawArchive']['files']):raise RuntimeError('Duplicate archive identity')
cases=[entry for name,entry in manifest.items()if name.endswith('.result.json')];assert len(cases)==1515
statuses=Counter();substatuses=Counter();variants=set();fully=0;qualified_pass=0;errors=0
for item in cases:
 f=root/item['path']
 if f.is_symlink()or not f.resolve().is_relative_to(root):raise RuntimeError('Unexpected case path')
 data=f.read_bytes()
 if len(data)!=item['bytes']or hashlib.sha256(data).hexdigest()!=item['sha256']:raise RuntimeError('Case byte identity changed')
 row=json.loads(data);key=(row['type'],row['test'])
 if key in variants:raise RuntimeError('Duplicate variant')
 variants.add(key);checks=row.get('subtests')or[]
 if len({r['name']for r in checks})!=len(checks):raise RuntimeError('Duplicate assertion name')
 if row.get('subtest_count',len(checks))!=len(checks):raise RuntimeError('Declared assertion count differs')
 c=row.get('cleanup')or{};bad=bool((row.get('metrics')or{}).get('errors')or row.get('telemetry_errors')or c.get('error')or c.get('profile_error'));errors+=bad;healthy=row['status']=='OK'and not bad
 statuses[row['status']]+=1;substatuses.update(r['status']for r in checks);fully+=bool(healthy and checks and all(r['status']=='PASS'for r in checks));qualified_pass+=sum(r['status']=='PASS'for r in checks)if healthy else 0
summary={'scheduled':1515,'recorded':len(variants),'fullyPassing':fully,'unrun':1515-len(variants),'passPercentOfRecorded':100*fully/len(variants),'harnessStatuses':dict(statuses),'assertions':dict(substatuses),'qualifiedPassingAssertions':qualified_pass,'measurementErrorVariants':errors}
if summary!=a['summary']:raise RuntimeError('Independent raw summary differs')
cohort_path=next(root/name for name in manifest if name.endswith('/cohort.json'));cohort=json.loads(cohort_path.read_text());schedule=cohort['frozenSchedule'];digest=hashlib.sha256(json.dumps(schedule,sort_keys=True,separators=(',',':')).encode()).hexdigest()
if digest!='adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe'or{(r['type'],r['test'])for r in schedule}!=variants:raise RuntimeError('Frozen schedule differs')
archive_path=root/a['rawArchive']['path']
if archive_path.stat().st_size!=a['rawArchive']['bytes']or file_hash(archive_path)!=a['rawArchive']['sha256']:raise RuntimeError('Raw archive identity differs')
seen=set()
with tarfile.open(archive_path,'r|gz')as archive:
 for member in archive:
  if not member.isfile()or member.name not in manifest or member.name in seen:raise RuntimeError('Unexpected archive member')
  if any(part.startswith('lumen-wpt-chrome-')for part in Path(member.name).parts):raise RuntimeError('Profile in evidence archive')
  entry=manifest[member.name];stream=archive.extractfile(member);digest=hashlib.sha256();size=0
  while block:=stream.read(1024*1024):digest.update(block);size+=len(block)
  if size!=entry['bytes']or digest.hexdigest()!=entry['sha256']:raise RuntimeError('Archived bytes differ:'+member.name)
  seen.add(member.name)
if seen!=set(manifest):raise RuntimeError('Archive member missing')
ref=a['reference'];compressed=root/ref['compressedPath']
if compressed.stat().st_size!=ref['compressedBytes']or file_hash(compressed)!=ref['compressedSHA256']:raise RuntimeError('Compressed reference differs')
digest=hashlib.sha256();size=0
with gzip.open(compressed,'rb')as stream:
 while block:=stream.read(1024*1024):digest.update(block);size+=len(block)
if size!=ref['bytes']or digest.hexdigest()!=ref['sha256']or file_hash(root/ref['path'])!=ref['sha256']:raise RuntimeError('Reference decompression identity differs')
if file_hash(args.acceptance)!=args.sha256:raise RuntimeError('Acceptance changed during review')
record={'accepted':True,'reviewer':'lumen_wpt_engineering','reviewedUTC':datetime.now(timezone.utc).isoformat(),'seconds':time.monotonic()-started,'scope':'Independent readback of all1515 actual case bytes, exact frozen schedule, raw archive contents and compressed reference identity. No new browser execution or full officialWPT claim.','acceptanceSHA256':args.sha256,'summary':summary,'caseCount':len(cases),'archiveMemberCount':len(seen),'rawArchiveSHA256':a['rawArchive']['sha256'],'referenceSHA256':ref['sha256'],'referenceCompressedSHA256':ref['compressedSHA256']}
args.output.parent.mkdir(parents=True,exist_ok=True);data=(json.dumps(record,indent=2)+'\n').encode();fd,name=tempfile.mkstemp(prefix='.independent-',dir=args.output.parent)
with os.fdopen(fd,'wb')as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
os.replace(name,args.output)
if args.output.read_bytes()!=data:raise RuntimeError('Review receipt readback differs')
print(json.dumps({'accepted':True,'cases':len(cases),'fullyPassing':fully,'assertions':dict(substatuses),'archiveMembers':len(seen),'seconds':record['seconds'],'receipt':str(args.output)},separators=(',',':')))
