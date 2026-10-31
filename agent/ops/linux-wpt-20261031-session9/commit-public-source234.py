#!/usr/bin/env python3
"""Commit only the qualified isolated public tree and verify its self-contained bundle offline. No push."""
from pathlib import Path
import argparse,datetime,hashlib,json,subprocess
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');OPS=ROOT/'agent/ops/linux-wpt-20261031-session9'
PUBLIC='2287d7fc602954c85a51c36f45ab5faa2473decc';SOURCE='206bb89d161d91278c56899a3f664a99480374f8'
BRANCH='gpt-6-astra-pro-builtin-rubywebterm-linux-containment-lifecycle-source234-20261031'
BUNDLE='gpt-6-astra-pro-builtin-rubywebterm-lumen-linux-source-i234.bundle'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def dump(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def need(v,m):
 if not v:raise AssertionError(m)
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.STDOUT)
def mode(p):return '100755' if p.stat().st_mode&0o111 else '100644'
def blob(p):
 b=p.read_bytes();return hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--qualification-sha256',required=True);a=ap.parse_args()
 qpath=OPS/'qualification234-precommit.json';need(sha(qpath)==a.qualification_sha256,'Qualification receipt changed');q=read(qpath)
 need(q['qualification_accepted'] is True and q['candidate']==234 and q['tested_source_commit']==SOURCE,'Qualified final source required')
 repo=OPS/'public-export234';out=OPS/'public-source234-recovery';restore=OPS/'offline-source234-recovered'
 need(not out.exists() and not restore.exists(),'Fresh public recovery outputs required');need(git(repo,'rev-parse','HEAD').decode().strip()==PUBLIC and git(repo,'branch','--show-current').decode().strip()==BRANCH,'Public stage base/branch differs')
 need(git(ROOT,'rev-parse','HEAD').decode().strip()==q['private_handoff_commit'],'Private handoff advanced after qualification')
 inv=q['public_file_inventory'];actual={str(p.relative_to(repo)):p for p in repo.rglob('*') if '.git' not in p.relative_to(repo).parts and p.is_file()}
 need(set(actual)==set(inv),'Unexpected/missing public file')
 for rel,p in actual.items():need(not p.is_symlink() and sha(p)==inv[rel]['sha256'] and mode(p)==inv[rel]['mode'],'Qualified public bytes/mode changed: '+rel)
 out.mkdir();dump(out/'precommit.json',{'candidate':234,'qualification_sha256':a.qualification_sha256,'controller_sha256':sha(__file__),'public_parent':PUBLIC,'branch':BRANCH,'file_count':len(inv),'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
 paths=sorted(inv)
 for i in range(0,len(paths),200):git(repo,'add','--force','--',*paths[i:i+200])
 rows=git(repo,'ls-files','--stage','-z').decode().split('\0');index={}
 for row in rows:
  if not row:continue
  left,rel=row.split('\t',1);md,oid,stage=left.split();need(stage=='0','Unmerged public index');index[rel]={'mode':md,'oid':oid}
 need(set(index)==set(inv),'Public index file set differs')
 for rel,p in actual.items():need(index[rel]=={'mode':inv[rel]['mode'],'oid':blob(p)},'Public staged blob mismatch: '+rel)
 msg=out/'commit-message.txt';msg.write_text('Implement Linux containment, visibility and document lifecycle\n\nAdd native logical size containment, hidden-content state handling, grid auto-repeat and image interpolation corrections. Implement visibility option conversion and genuine document load/pageshow metadata with bounded lifetime controls.\n\nPreserve the exact tested234 source inputs. Selected containment39 passes286 assertions and28 reftests; the fixed640 sample passes417 files and9249/10540 assertions, with all prior passes retained. Full browser conformance remains incomplete. Include reviewed build/recovery documentation and explicit external prerequisites.\n')
 commit_log=git(repo,'commit','--file',str(msg));(out/'commit.log').write_bytes(commit_log)
 commit=git(repo,'rev-parse','HEAD').decode().strip();tree=git(repo,'rev-parse','HEAD^{tree}').decode().strip();need(git(repo,'rev-parse','HEAD^').decode().strip()==PUBLIC,'Unexpected public parent')
 need(int(git(repo,'rev-list','--count','HEAD'))==216,'Public ancestry count differs');need(not git(repo,'status','--porcelain').strip(),'Public worktree/index dirty after commit')
 for private in [SOURCE,q['private_handoff_commit']]:need(subprocess.run(['git','-C',str(repo),'cat-file','-e',private+'^{commit}'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0,'Private object leaked')
 dump(out/'commit.json',{'candidate':234,'public_commit':commit,'public_tree':tree,'public_parent':PUBLIC,'public_branch':BRANCH,'public_ancestors':216,'qualification_sha256':a.qualification_sha256,'all_public_inventory_staged_exactly':True,'private_source_history_absent':True})
 bundle=out/BUNDLE;git(repo,'-c','pack.threads=2','bundle','create',str(bundle),'refs/heads/'+BRANCH)
 (out/'bundle-verify.log').write_bytes(git(repo,'bundle','verify',str(bundle)))
 heads=git(repo,'bundle','list-heads',str(bundle)).decode().splitlines();need(heads==[commit+' refs/heads/'+BRANCH],'Bundle has unexpected refs')
 (out/'clone.log').write_bytes(subprocess.check_output(['git','clone','--branch',BRANCH,str(bundle),str(restore)],stderr=subprocess.STDOUT))
 need(git(restore,'rev-parse','HEAD').decode().strip()==commit and git(restore,'rev-parse','HEAD^{tree}').decode().strip()==tree,'Offline restored identity differs')
 need(not git(restore,'status','--porcelain').strip(),'Offline restored worktree dirty');need(int(git(restore,'rev-list','--count','HEAD'))==216,'Offline history differs')
 (out/'offline-fsck.log').write_bytes(git(restore,'fsck','--full'))
 restored={str(p.relative_to(restore)):p for p in restore.rglob('*') if '.git' not in p.relative_to(restore).parts and p.is_file()};need(set(restored)==set(inv),'Offline restored file set differs')
 for rel,p in restored.items():need(not p.is_symlink() and sha(p)==inv[rel]['sha256'] and mode(p)==inv[rel]['mode'],'Offline blob/mode differs: '+rel)
 source=read(ROOT/'agent/validation/iteration-234/source-manifest-after.json');need(len(source['files'])==650 and all(sha(restore/p)==h for p,h in source['files'].items()),'Offline measured input differs')
 need(git(ROOT,'rev-parse','HEAD').decode().strip()==q['private_handoff_commit'],'Main handoff changed during public recovery')
 recovery={'candidate':234,'tested_source_commit':SOURCE,'tested_binary_sha256':q['tested_binary_sha256'],'public_commit':commit,'public_tree':tree,'public_parent':PUBLIC,'public_branch':BRANCH,'public_ancestry_count':216,'bundle_file':BUNDLE,'bundle_sha256':sha(bundle),'bundle_bytes':bundle.stat().st_size,'all_650_recorded_inputs_exact':True,'all_public_files_and_modes_exact':True,'public_file_count':len(inv),'offline_clone_verified':True,'offline_fsck_passed':True,'private_history_included':False,'independent_rebuild_performed':False,'qualification_receipt_sha256':a.qualification_sha256,'completed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 dump(out/'PUBLIC-GIT-RECOVERY.json',recovery);dump(out/'receipt.json',{**recovery,'controller_sha256':sha(__file__),'bundle_path':str(bundle),'restored_path':str(restore),'no_push_performed':True});print(json.dumps(recovery))
if __name__=='__main__':main()
