#!/usr/bin/env python3
"""Prepare a source-only checkpoint on separately reviewed public history.
No network, no publication, no credential or private operational-file export.
"""
import argparse,hashlib,json,os,pathlib,subprocess
ROOT=pathlib.Path('/mnt/p3/project/browser/lumen-android/lumen-android')
BASE=ROOT/'agent/ops/linux-wpt-20261030-session7/public-export205'
PUBLIC='0742c66b2c9b077c68006c9cb1b4413e281c328d'
def git(repo,*args):return subprocess.check_output(['git','-C',str(repo),*args])
def sha(b):return hashlib.sha256(b).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--candidate',type=int,required=True);p.add_argument('--source',required=True);p.add_argument('--out',type=pathlib.Path,required=True);a=p.parse_args()
manifest_path=ROOT/f'agent/validation/iteration-{a.candidate}/source-manifest-before.json'
before=json.loads(manifest_path.read_text());after=json.loads(manifest_path.with_name('source-manifest-after.json').read_text())
if before!=after:raise SystemExit('source build inputs changed')
source=git(ROOT,'rev-parse',a.source+'^{commit}').decode().strip()
if source!=a.source:raise SystemExit('full source hash required')
baseline=json.loads(git(BASE,'show',PUBLIC+':SOURCE-MANIFEST.json'))
allowed={x['path'] for x in baseline['files']}
inputs=before['files']
if not isinstance(inputs,dict) or not set(inputs)<=allowed:raise SystemExit('unreviewed new input path needs separate review')
for name in inputs:
 if pathlib.PurePosixPath(name).is_absolute() or '..' in pathlib.PurePosixPath(name).parts:raise SystemExit('unsafe source path')
 if name.startswith(('agent/','agents/','.git/','tools/promo/')) or name in ('AGENTS.md','devsummary.md'):raise SystemExit('private source boundary')
if a.out.exists():raise SystemExit('refuse to overwrite an existing export worktree')
subprocess.run(['git','clone','--quiet','--no-hardlinks','--no-checkout',str(BASE),str(a.out)],check=True)
subprocess.run(['git','-C',str(a.out),'checkout','--quiet','--detach',PUBLIC],check=True)
branch=f'gpt-6-astra-pro-builtin-integration-source{a.candidate}-20261030'
subprocess.run(['git','-C',str(a.out),'switch','--quiet','-c',branch],check=True)
for name,expected in inputs.items():
 data=git(ROOT,'show',source+':'+name)
 if sha(data)!=expected:raise SystemExit('recorded source mismatch: '+name)
 mode=git(ROOT,'ls-tree',source,'--',name).split()[0].decode()
 if mode not in ('100644','100755'):raise SystemExit('nonregular source member')
 target=a.out/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data);target.chmod(int(mode[-3:],8))
if git(a.out,'rev-parse','HEAD').decode().strip()!=PUBLIC:raise SystemExit('unexpected public parent')
private_object=subprocess.run(['git','-C',str(a.out),'cat-file','-e',source+'^{commit}'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
receipt={'candidate':a.candidate,'source_commit':source,'public_parent':PUBLIC,'proposed_public_branch':branch,'recorded_source_set_manifest_sha256':sha(manifest_path.read_bytes()),'recorded_source_set_entries':len(inputs),'all_recorded_inputs_included_and_exact':True,'private_source_commit_present_in_export':private_object.returncode==0,'public_export_commit_created':False,'qualification_pending':True,'export_rebuild_performed':False}
if receipt['private_source_commit_present_in_export']:raise SystemExit('private source ancestry contamination')
receipt_path=ROOT/f'agent/validation/linux-wpt-20261030-session7/public-source{a.candidate}-prepared.json';receipt_path.write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt))
