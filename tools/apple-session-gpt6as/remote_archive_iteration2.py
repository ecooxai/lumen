"""Exact-byte public evidence archive for the reviewed second native milestone."""
import hashlib,io,json,pathlib,subprocess,tarfile
from datetime import datetime,timezone
R=pathlib.Path.cwd().resolve();S=R/'output/session-20261029';K=S/'native-qa-kit-v1/source';SHA='37447be5593f90073e04afdc2166e77a757a71252a1c3ce92079e52e60205cb8';selected={};visuals={}
def add(p):
 if not p.is_file()or p.is_symlink():return
 rel=p.resolve().relative_to(R)
 if any(x in rel.parts for x in ['profile','profiles','node_modules'])or any('profile'in x for x in rel.parts):return
 if p.suffix in ['.png','.jpg','.jpeg','.webp']:
  visuals[str(rel)]={'path':str(rel),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'persistedInThisArchive':False,'purpose':'Intermediate visual QA; image retained in task workspace for inspection only'};return
 if p.suffix not in ['.json','.jsonl','.log','.csv','.patch']:return
 size=p.stat().st_size
 if size>8*1024*1024:raise RuntimeError('Evidence file exceeds8MiB cap: '+str(rel))
 selected[str(rel)]=p
for name in ['native-wasm-candidate-artifact.json','native-wasm-commit-receipt.json']:
 p=S/name
 if not p.is_file():raise RuntimeError('Missing reviewed artifact: '+name)
 add(p)
commit=json.loads((S/'native-wasm-commit-receipt.json').read_text())['commit']
patch=S/'native-wasm-reviewed-commit.patch'
patch.write_bytes(subprocess.check_output(['git','show','--format=fuller','--stat','--patch',commit,'--','src/js/js.cc','tests/test_wasm_async_deadline.cc','Makefile']))
add(patch)
found_jobs=[]
for directory in S.glob('native-wpt-*-job'):
 receipt=directory/'receipt.json'
 if not receipt.is_file():continue
 d=json.loads(receipt.read_text())
 if d.get('sourceSHA256')!=SHA or not d.get('finishedUTC'):continue
 found_jobs.append(directory.name)
 for p in directory.iterdir():add(p)
 for step in d.get('steps',[]):
  command=step.get('command',[])
  if '--output'in command:
   path=K/command[command.index('--output')+1]
   if path.is_dir():
    for p in path.rglob('*'):add(p)
   else:add(path)
if len(found_jobs)!=1:raise RuntimeError('Expected exactly one completed immutable WASM WPT job')
for prefix in ['native-wasm-red','native-wasm-green','native-wasm-idle','wasm-idle','native-xml-frame-red','native-xml-frame-green','native-wasm-app']:
 for directory in S.glob(prefix+'*'):
  if directory.is_dir():
   for p in directory.rglob('*'):add(p)
public=S/'mac-public-probe-install-1/kit/output/mac-public-delivery-1'
if not(public/'receipt.json').is_file():raise RuntimeError('Public Mac TLS probe receipt absent')
for p in public.iterdir():add(p)
files=[];total=0
for name,p in sorted(selected.items()):
 data=p.read_bytes();total+=len(data)
 files.append({'path':name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
if total>32*1024*1024:raise RuntimeError('Evidence archive expanded size exceeds32MiB')
manifest={'schema':1,'createdUTC':datetime.now(timezone.utc).isoformat(),'scope':'Reviewed native WASM RED/GREEN, fixed1850WPT comparison, settled idle, source commit, XML controls and trusted public MacChrome delivery; not completeAppleAPI/IPA acceptance','binarySHA256':SHA,'sourceCommit':commit,'wptJobs':found_jobs,'visualReviewReferences':list(visuals.values()),'excludes':['Intermediate screenshot image bodies','Browser profiles','Session/token stores','Certificates/private keys','Dependency caches','Native binaries'],'files':files}
output=S/'public-native/gpt6-astra-pro_qa-deploy_devinmac3d_iteration2-evidence.tar.gz';output.parent.mkdir(exist_ok=True)
if output.exists():raise RuntimeError('Preserve existing evidence archive; choose a new milestone filename')
with tarfile.open(output,'w:gz')as t:
 for name,p in sorted(selected.items()):t.add(p,arcname=name,recursive=False)
 data=(json.dumps(manifest,indent=2)+'\n').encode();info=tarfile.TarInfo('ITERATION2-EVIDENCE-MANIFEST.json');info.size=len(data);info.mode=0o644;t.addfile(info,io.BytesIO(data))
print(json.dumps({'archive':str(output.relative_to(R)),'bytes':output.stat().st_size,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'files':len(files),'expandedBytes':total,'wptJobs':found_jobs}))
