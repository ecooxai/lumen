#!/usr/bin/env python3
"""Normal three-file documentation child, then both exact offline recovery paths. No push."""
from pathlib import Path
import argparse,datetime,hashlib,json,subprocess
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');OPS=ROOT/'agent/ops/linux-wpt-20261031-session9';REPO=OPS/'public-export234'
PARENT='d72c9d8225a733dd959e1b81389bb3dd699ebc86';SOURCE='206bb89d161d91278c56899a3f664a99480374f8';BRANCH='gpt-6-astra-pro-builtin-rubywebterm-linux-containment-lifecycle-source234-20261031';BUNDLE='gpt-6-astra-pro-builtin-rubywebterm-lumen-linux-source-i234.bundle'
ALLOWED={'docs/gpt-6-astra-pro-builtin-rubywebterm-linux-build.md','SOURCE-MANIFEST.json','EXPORT-QUALIFICATION.json'}
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
def inventory_check(repo,inv):
 actual={str(p.relative_to(repo)):p for p in repo.rglob('*') if '.git' not in p.relative_to(repo).parts and p.is_file()};need(set(actual)==set(inv),'Public file set differs')
 for rel,p in actual.items():need(not p.is_symlink() and sha(p)==inv[rel]['sha256'] and mode(p)==inv[rel]['mode'],'Public bytes/mode differ: '+rel)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--qualification-sha256',required=True);a=ap.parse_args();qp=OPS/'qualification234-docfix-precommit.json';need(sha(qp)==a.qualification_sha256,'Correction qualification changed');q=read(qp)
 need(q['qualification_accepted'] is True and q['documentation_correction_only'] is True and q['tested_source_commit']==SOURCE and q['all_650_recorded_inputs_unchanged'] is True and q['report_and_16_gates_unchanged'] is True and set(q['approved_changed_paths'])==ALLOWED,'Approved narrow correction required')
 need(git(ROOT,'rev-parse','HEAD').decode().strip()==q['private_handoff_commit'],'Main handoff changed');need(git(REPO,'rev-parse','HEAD').decode().strip()==PARENT and git(REPO,'branch','--show-current').decode().strip()==BRANCH,'Public correction parent/branch differs');need(set(git(REPO,'diff','--name-only').decode().splitlines())==ALLOWED and not git(REPO,'diff','--cached','--name-only').strip(),'Unexpected staged/public changes')
 inv=q['public_file_inventory'];need(len(inv)==749,'Public inventory count differs');inventory_check(REPO,inv)
 out=OPS/'public-source234-docfix-recovery';need(not out.exists(),'Fresh corrected recovery directory required');out.mkdir();dump(out/'precommit.json',{'qualification_sha256':sha(qp),'parent':PARENT,'changed_paths':sorted(ALLOWED),'controller_sha256':sha(__file__)})
 git(REPO,'add','--',*sorted(ALLOWED));need(set(git(REPO,'diff','--cached','--name-only').decode().splitlines())==ALLOWED,'Staged correction paths differ')
 for rel in ALLOWED:need(git(REPO,'rev-parse',':'+rel).decode().strip()==blob(REPO/rel),'Staged correction bytes differ')
 msg=out/'commit-message.txt';msg.write_text('Correct local demo file URLs in Linux delivery guide\n\nUse an explicit file:// URL for the authored local demonstration and refresh the two generated public provenance records. All650 tested engine inputs, executable bytes, report and16 technical gate hashes are unchanged. Preserve the initial published delivery and its documented-command failure evidence.\n')
 (out/'commit.log').write_bytes(git(REPO,'commit','--file',str(msg)))
 commit=git(REPO,'rev-parse','HEAD').decode().strip();tree=git(REPO,'rev-parse','HEAD^{tree}').decode().strip();need(git(REPO,'rev-parse','HEAD^').decode().strip()==PARENT and int(git(REPO,'rev-list','--count','HEAD'))==217,'Corrected public ancestry differs');need(not git(REPO,'status','--porcelain').strip(),'Public correction worktree dirty');need(set(git(REPO,'diff','--name-only',PARENT,'HEAD').decode().splitlines())==ALLOWED,'Public commit exceeded exact three-file correction')
 bundle=out/BUNDLE;git(REPO,'-c','pack.threads=2','bundle','create',str(bundle),'refs/heads/'+BRANCH,'HEAD');heads=git(REPO,'bundle','list-heads',str(bundle)).decode().splitlines();need(set(heads)=={commit+' HEAD',commit+' refs/heads/'+BRANCH} and len(heads)==2,'Bundle ref set differs');(out/'bundle-verify.log').write_bytes(git(REPO,'bundle','verify',str(bundle)))
 m=read(ROOT/'agent/validation/iteration-234/source-manifest-after.json');checks=[]
 for kind,args in [('documented-default',[]),('explicit-branch',['--branch',BRANCH])]:
  dst=OPS/('offline-source234-docfix-'+kind);need(not dst.exists(),'Fresh corrected clone required');r=subprocess.run(['git','clone',*args,str(bundle),str(dst)],capture_output=True,text=True);(out/(kind+'-clone.log')).write_text(r.stdout+r.stderr);need(r.returncode==0 and 'unable to checkout' not in r.stderr,'Offline corrected clone failed')
  need(git(dst,'rev-parse','HEAD').decode().strip()==commit and git(dst,'rev-parse','HEAD^{tree}').decode().strip()==tree and git(dst,'branch','--show-current').decode().strip()==BRANCH,'Recovered identity differs');need(int(git(dst,'rev-list','--count','HEAD'))==217 and not git(dst,'status','--porcelain').strip(),'Recovered ancestry/state differs');(out/(kind+'-fsck.log')).write_bytes(git(dst,'fsck','--full'));inventory_check(dst,inv)
  need(len(m['files'])==650 and all(sha(dst/p)==h for p,h in m['files'].items()),'Recovered measured input differs')
  for private in [SOURCE,q['private_handoff_commit']]:need(subprocess.run(['git','-C',str(dst),'cat-file','-e',private+'^{commit}'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0,'Private history in recovered bundle')
  checks.append({'kind':kind,'actual_clone_exit':r.returncode,'actual_fsck_exit':0,'all749_files_and_modes_exact':True,'all650_inputs_exact':True})
 need(git(ROOT,'rev-parse','HEAD').decode().strip()==q['private_handoff_commit'] and all(sha(ROOT/p)==h for p,h in m['files'].items()),'Main measured state changed')
 recovery={'candidate':234,'tested_source_commit':SOURCE,'tested_binary_sha256':q['tested_binary_sha256'],'public_commit':commit,'public_tree':tree,'public_parent':PARENT,'original_engine_export_parent':'2287d7fc602954c85a51c36f45ab5faa2473decc','documentation_only_child_of_engine_export':True,'public_branch':BRANCH,'public_ancestry_count':217,'bundle_file':BUNDLE,'bundle_sha256':sha(bundle),'bundle_bytes':bundle.stat().st_size,'bundle_ref_names':['HEAD','refs/heads/'+BRANCH],'both_refs_point_to_exact_public_commit':True,'all_650_recorded_inputs_exact':True,'all_public_files_and_modes_exact':True,'public_file_count':749,'offline_clone_verified':True,'default_documented_clone_verified':True,'explicit_branch_clone_verified':True,'offline_fsck_passed':True,'private_history_included':False,'independent_rebuild_performed':False,'qualification_receipt_sha256':sha(qp),'completed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 dump(out/'PUBLIC-GIT-RECOVERY.json',recovery);dump(out/'receipt.json',{**recovery,'controller_sha256':sha(__file__),'clone_checks':checks,'qualified_source_and_technical_report_unchanged':True,'actual_public_changed_paths':sorted(ALLOWED),'no_push_performed':True});print(json.dumps({'public_commit':commit,'public_tree':tree,'bundle_sha256':recovery['bundle_sha256'],'bundle_bytes':recovery['bundle_bytes'],'recovery_sha256':sha(out/'PUBLIC-GIT-RECOVERY.json'),'receipt_sha256':sha(out/'receipt.json')}))
if __name__=='__main__':main()
