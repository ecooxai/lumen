#!/usr/bin/env python3
"""Versioned bundle metadata correction only; qualified source/report remain unchanged."""
from pathlib import Path
import datetime,hashlib,json,subprocess
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');OPS=ROOT/'agent/ops/linux-wpt-20261031-session9'
COMMIT='d72c9d8225a733dd959e1b81389bb3dd699ebc86';TREE='40016234061f6494d7519e15e9546c35169f4f0a'
BRANCH='gpt-6-astra-pro-builtin-rubywebterm-linux-containment-lifecycle-source234-20261031';BUNDLE='gpt-6-astra-pro-builtin-rubywebterm-lumen-linux-source-i234.bundle'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def dump(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def need(v,m):
 if not v:raise AssertionError(m)
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.STDOUT)
def mode(p):return '100755' if p.stat().st_mode&0o111 else '100644'
def main():
 qpath=OPS/'qualification234-precommit.json';need(sha(qpath)=='27e156638d45671a9015c9a06841ef178ad83c70adf2ae1326f621fc684bcd40','Qualified inventory changed');q=read(qpath)
 old=OPS/'public-source234-recovery';need(sha(old/BUNDLE)=='6cda7cf52d56ac97cb98441af7a983aad13ecabbf957231d404c1db9d321f2ed','Old bundle must remain exact');prior=read(old/'documentation-command-replay.json');need(prior['head_returncode']==128 and prior['all_650_files_match'] is False,'Retain genuine failed default-clone control')
 repo=OPS/'public-export234';need(git(repo,'rev-parse','HEAD').decode().strip()==COMMIT and git(repo,'rev-parse','HEAD^{tree}').decode().strip()==TREE,'Qualified public source changed');need(not git(repo,'status','--porcelain').strip(),'Public source dirty')
 out=OPS/'public-source234-recovery-v2';need(not out.exists(),'Fresh correction output required');out.mkdir();bundle=out/BUNDLE
 git(repo,'-c','pack.threads=2','bundle','create',str(bundle),'refs/heads/'+BRANCH,'HEAD')
 heads=git(repo,'bundle','list-heads',str(bundle)).decode().splitlines();need(set(heads)=={COMMIT+' refs/heads/'+BRANCH,COMMIT+' HEAD'} and len(heads)==2,'Bundle refs differ from exact two approved public names')
 (out/'bundle-verify.log').write_bytes(git(repo,'bundle','verify',str(bundle)));inv=q['public_file_inventory'];m=read(ROOT/'agent/validation/iteration-234/source-manifest-after.json');records=[]
 for kind,args in [('documented-default',[]),('explicit-branch',['--branch',BRANCH])]:
  dest=OPS/('offline-source234-'+kind+'-v2');need(not dest.exists(),'Fresh clone output required')
  p=subprocess.run(['git','clone',*args,str(bundle),str(dest)],capture_output=True,text=True);(out/(kind+'-clone.log')).write_text(p.stdout+p.stderr);need(p.returncode==0,'Clone failed');need('unable to checkout' not in p.stderr and 'nonexistent ref' not in p.stderr,'Default clone did not select real HEAD')
  need(git(dest,'rev-parse','HEAD').decode().strip()==COMMIT and git(dest,'rev-parse','HEAD^{tree}').decode().strip()==TREE,'Restored commit/tree differs');need(git(dest,'branch','--show-current').decode().strip()==BRANCH,'Recovered branch differs');need(not git(dest,'status','--porcelain').strip(),'Recovered checkout dirty');need(int(git(dest,'rev-list','--count','HEAD'))==216,'Public ancestry differs')
  (out/(kind+'-fsck.log')).write_bytes(git(dest,'fsck','--full'));files={str(p.relative_to(dest)):p for p in dest.rglob('*') if '.git' not in p.relative_to(dest).parts and p.is_file()};need(set(files)==set(inv),'Recovered file inventory differs')
  for rel,pth in files.items():need(not pth.is_symlink() and sha(pth)==inv[rel]['sha256'] and mode(pth)==inv[rel]['mode'],'Recovered bytes/mode differs: '+rel)
  need(len(m['files'])==650 and all(sha(dest/rel)==h for rel,h in m['files'].items()),'Recovered input hash differs')
  for private in [q['tested_source_commit'],q['private_handoff_commit']]:need(subprocess.run(['git','-C',str(dest),'cat-file','-e',private+'^{commit}'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0,'Private commit entered recovered history')
  records.append({'kind':kind,'actual_clone_exit':p.returncode,'actual_fsck_exit':0,'commit':COMMIT,'tree':TREE,'branch':BRANCH,'all_749_files_and_modes_exact':True,'all_650_inputs_exact':True})
 need(sha(old/BUNDLE)=='6cda7cf52d56ac97cb98441af7a983aad13ecabbf957231d404c1db9d321f2ed','Original bundle changed');need(git(ROOT,'rev-parse','HEAD').decode().strip()==q['private_handoff_commit'],'Main handoff changed');need(git(repo,'rev-parse','HEAD').decode().strip()==COMMIT and not git(repo,'status','--porcelain').strip(),'Public source changed')
 base=read(old/'PUBLIC-GIT-RECOVERY.json');r={**base,'bundle_sha256':sha(bundle),'bundle_bytes':bundle.stat().st_size,'bundle_ref_names':['HEAD','refs/heads/'+BRANCH],'both_refs_point_to_exact_public_commit':True,'default_documented_clone_verified':True,'explicit_branch_clone_verified':True,'offline_clone_verified':True,'offline_fsck_passed':True,'all_650_recorded_inputs_exact':True,'all_public_files_and_modes_exact':True,'completed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};dump(out/'PUBLIC-GIT-RECOVERY.json',r)
 rec={**r,'controller_sha256':sha(__file__),'qualification_sha256':sha(qpath),'bundle_metadata_only_correction':True,'qualified_source_and_report_unchanged':True,'original_bundle_preserved_sha256':sha(old/BUNDLE),'failed_documentation_replay_sha256':sha(old/'documentation-command-replay.json'),'clone_checks':records,'no_new_source_commit_or_push':True};dump(out/'receipt.json',rec);print(json.dumps({'bundle_sha256':r['bundle_sha256'],'bundle_bytes':r['bundle_bytes'],'default_clone':True,'explicit_clone':True,'public_commit':COMMIT,'public_tree':TREE,'recovery_sha256':sha(out/'PUBLIC-GIT-RECOVERY.json'),'receipt_sha256':sha(out/'receipt.json')}))
if __name__=='__main__':main()
