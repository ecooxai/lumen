#!/usr/bin/env python3
"""Apply only the approved file-URL documentation correction; preserve all measured inputs/report."""
from pathlib import Path
import datetime,hashlib,json,subprocess
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');OPS=ROOT/'agent/ops/linux-wpt-20261031-session9';REPO=OPS/'public-export234'
PARENT='d72c9d8225a733dd959e1b81389bb3dd699ebc86';TREE='40016234061f6494d7519e15e9546c35169f4f0a';GUIDE='docs/gpt-6-astra-pro-builtin-rubywebterm-linux-build.md'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def dump(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def need(v,m):
 if not v:raise AssertionError(m)
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.STDOUT)
def mode(p):return '100755' if p.stat().st_mode&0o111 else '100644'
def main():
 oldpath=OPS/'qualification234-precommit.json';need(sha(oldpath)=='27e156638d45671a9015c9a06841ef178ad83c70adf2ae1326f621fc684bcd40','Old qualification changed');old=read(oldpath);decisionpath=OPS/'root-docfix234-acceptance.json';decision=read(decisionpath)
 need(decision['decision_owner']=='root' and decision['accepted'] is True and decision['public_parent']==PARENT and decision['public_parent_tree']==TREE,'Root correction acceptance mismatch')
 reviewpath=OPS/'candidate234-independent-final-review-v3.json';allowpath=OPS/'public-extra-dependencies-allowlist-final-v5.json';guidepath=OPS/'public-docs-preparation-v5'/GUIDE
 for p,key in [(reviewpath,'independent_review_sha256'),(allowpath,'allowlist_sha256'),(guidepath,'guide_sha256')]:need(sha(p)==decision[key],'Correction input changed: '+str(p))
 need(decision['guide_sha256']=='72b42ab31800634f508e14f14d15153ce7ac06582479e2dc52ec562aa9407c5c' and decision['independent_review_sha256']=='63bbbe783fb2dbf70c0fddb052631ae2ffea788e8f2bd30c83447320742d5c65' and decision['allowlist_sha256']=='b3a46c146d068d42276ec8478ca8d9a41a42dff3dfb6c55934b6de17c352fca3','Unexpected correction hashes')
 review=read(reviewpath);priorreview=read(OPS/'candidate234-independent-final-review-v2.json');need(review['required_gates']==priorreview['required_gates'] and review['independent_checkpoint_accepted'] is True and review['public_report_sha256']==decision['public_report_sha256'],'Technical gate/report changed')
 need(sha(OPS/'public-docs-preparation/report.json')==decision['public_report_sha256']=='7b1d017329c350b6de0ff18f573da66f02c305d328215aba486aebe60aa548ff','Report changed')
 oldallow=read(OPS/'public-extra-dependencies-allowlist-final-v4.json');allow=read(allowpath)
 for key in ['additional_dependencies','staged_authored_controls']:need(allow[key]==oldallow[key],'Non-guide allowlist change')
 def entries(v):return {x['export_path']:x for x in v['documentation']}
 a,b=entries(allow),entries(oldallow);need(set(a)==set(b) and all(a[k]==b[k] for k in a if k!=GUIDE) and a[GUIDE]['sha256']==decision['guide_sha256'],'Only guide documentation may change')
 need(git(REPO,'rev-parse','HEAD').decode().strip()==PARENT and git(REPO,'rev-parse','HEAD^{tree}').decode().strip()==TREE and not git(REPO,'status','--porcelain').strip(),'Public parent not pristine')
 need(git(ROOT,'rev-parse','HEAD').decode().strip()==old['private_handoff_commit'] and not git(ROOT,'diff','--name-only').strip() and not git(ROOT,'diff','--cached','--name-only').strip(),'Main handoff changed')
 inv=old['public_file_inventory'];need(len(inv)==749,'Public input count changed')
 for rel,x in inv.items():p=REPO/rel;need(p.is_file() and not p.is_symlink() and sha(p)==x['sha256'] and mode(p)==x['mode'],'Old public bytes/mode differ: '+rel)
 m=read(ROOT/'agent/validation/iteration-234/source-manifest-after.json');need(len(m['files'])==650 and m['sha256']==decision['source_manifest_digest'] and all(sha(ROOT/p)==sha(REPO/p)==h for p,h in m['files'].items()),'Measured input differs')
 need(sha(ROOT/'build/codex-native/lumen-astra-i234')==sha(ROOT/'build/codex-native/lumen')==decision['binary_sha256']==old['tested_binary_sha256'],'Accepted app differs')
 before=(REPO/GUIDE).read_bytes();after=guidepath.read_bytes();oldcmd=b'./build/codex-native/lumen "$PWD/examples/gpt-6-astra-pro-builtin-rubywebterm-lumen-live-demo.html"';newcmd=b'./build/codex-native/lumen "file://$PWD/examples/gpt-6-astra-pro-builtin-rubywebterm-lumen-live-demo.html"'
 need(before.count(oldcmd)==1 and before.replace(oldcmd,newcmd,1)==after,'Guide is not the exact approved one-line correction')
 receipt=OPS/'qualification234-docfix-precommit.json';need(not receipt.exists(),'Fresh correction receipt required')
 (REPO/GUIDE).write_bytes(after)
 sm=read(REPO/'SOURCE-MANIFEST.json');sm['public_parent']=PARENT;sm['reviewed_public_documentation_controls'][GUIDE]['sha256']=decision['guide_sha256'];dump(REPO/'SOURCE-MANIFEST.json',sm)
 eq=read(REPO/'EXPORT-QUALIFICATION.json');eq.update({'qualification_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'reviewed_public_parent':PARENT,'public_source_manifest_file_sha256':sha(REPO/'SOURCE-MANIFEST.json'),'root_decision_sha256':sha(decisionpath),'independent_final_review_sha256':sha(reviewpath),'documentation_only_correction':{'prior_public_commit':PARENT,'prior_public_tree':TREE,'original_engine_export_parent':'2287d7fc602954c85a51c36f45ab5faa2473decc','original_qualification_sha256':sha(oldpath),'guide_old_sha256':inv[GUIDE]['sha256'],'guide_new_sha256':decision['guide_sha256'],'all_650_measured_inputs_unchanged':True,'technical_report_and_all_16_gate_hashes_unchanged':True}});dump(REPO/'EXPORT-QUALIFICATION.json',eq)
 allowed=set(decision['allowed_public_paths']);need(set(git(REPO,'diff','--name-only').decode().splitlines())==allowed,'Unexpected public correction path')
 actual={}
 for p in REPO.rglob('*'):
  if '.git' in p.relative_to(REPO).parts:continue
  need(not p.is_symlink(),'Unexpected public symlink')
  if p.is_file():actual[str(p.relative_to(REPO))]={'sha256':sha(p),'mode':mode(p)}
 need(set(actual)==set(inv) and all(actual[k]==inv[k] for k in inv if k not in allowed),'Non-approved public path changed');need(all(sha(REPO/p)==h for p,h in m['files'].items()),'Measured exported input changed')
 result={**old,'qualification_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'reviewed_public_parent':PARENT,'public_source_manifest_file_sha256':sha(REPO/'SOURCE-MANIFEST.json'),'root_decision_sha256':sha(decisionpath),'independent_final_review_sha256':sha(reviewpath),'final_allowlist_sha256':sha(allowpath),'qualifier_sha256':sha(__file__),'public_file_inventory':actual,'public_file_count':len(actual),'public_ancestry_count_before_commit':216,'qualification_accepted':True,'documentation_correction_only':True,'original_qualification_sha256':sha(oldpath),'approved_changed_paths':sorted(allowed),'exact_approved_file_url_insertion':True,'all_650_recorded_inputs_unchanged':True,'report_and_16_gates_unchanged':True}
 dump(receipt,result);print(json.dumps({'qualification_accepted':True,'public_files':len(actual),'changed_paths':sorted(allowed),'receipt_sha256':sha(receipt)}))
if __name__=='__main__':main()
