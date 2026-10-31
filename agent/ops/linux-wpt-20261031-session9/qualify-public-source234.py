#!/usr/bin/env python3
"""Qualify the isolated234 public tree after explicit final review. Never commit, push, archive, or run tests."""
from pathlib import Path,PurePosixPath
import argparse,datetime,hashlib,json,os,subprocess
ROOT=Path('/mnt/p3/project/browser/lumen-android/lumen-android');OPS=ROOT/'agent/ops/linux-wpt-20261031-session9'
PUBLIC='2287d7fc602954c85a51c36f45ab5faa2473decc';SOURCE='206bb89d161d91278c56899a3f664a99480374f8';BINARY='253f563c6529af4d8bab38b12373b51aec9941eb475fa16cae50093ff4e2b282'
BRANCH='gpt-6-astra-pro-builtin-rubywebterm-linux-containment-lifecycle-source234-20261031';BUNDLE='gpt-6-astra-pro-builtin-rubywebterm-lumen-linux-source-i234.bundle'
GATES={'native147','visibility49_accessors2','lifecycle12_upstream4','containment39','hidden_api3','hidden_scroll_contract','hidden_scroll_upstream','cadence_v1','actual_cpp_sanitizer','native_math_sanitizer','native_grid_containment_sanitizer','raster_sanitizer','frozen640','prior_through231','render40','resources'}
GENERATED={'SOURCE-MANIFEST.json','EXPORT-QUALIFICATION.json','OFFLINE-RECOVERY.md'}
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def require(ok,msg):
 if not ok:raise AssertionError(msg)
def git(p,*args):return subprocess.check_output(['git','-C',str(p),*args])
def valid(rel):
 p=PurePosixPath(rel);require(str(p)==rel and not p.is_absolute() and '..' not in p.parts and '.git' not in p.parts and p.parts and p.parts[0] not in ['agent','agents','build','node_modules'],'Unsafe/private public path '+rel);return p
def mode(p):return '100755' if p.stat().st_mode&0o111 else '100644'
def oid(p):
 data=p.read_bytes();return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
def dump(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def pinned(item):
 rel=PurePosixPath(item['path']);require(not rel.is_absolute() and '..' not in rel.parts and rel.parts[:2] in [('agent','validation'),('agent','ops')],'Receipt outside task evidence')
 p=ROOT/rel;require(p.is_file() and not p.is_symlink() and sha(p)==item['sha256'],'Pinned task receipt changed '+str(rel));return read(p)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--spec',type=Path,required=True);ap.add_argument('--spec-sha256',required=True);a=ap.parse_args();require(sha(a.spec)==a.spec_sha256,'Final specification changed');s=read(a.spec)
 require(s['candidate']==234 and s['source_commit']==SOURCE and s['binary_sha256']==BINARY,'Specification identity differs')
 repo=OPS/'public-export234';receipt=OPS/'qualification234-precommit.json';require(not receipt.exists(),'Fresh qualification receipt required')
 prep=pinned(s['preparation']);require(prep['candidate']==234 and prep['source_commit']==SOURCE and prep['tested_binary_sha256']==BINARY and prep['all_inputs_exact'] is True and prep['proposed_public_branch']==BRANCH,'Preparation mismatch')
 decision=pinned(s['root_decision']);review=pinned(s['independent_review']);handoff=pinned(s['handoff']);extra=pinned(s['final_allowlist']);report=pinned(s['public_report'])
 require(decision['decision_owner']=='root' and decision['publication_accepted'] is True and decision['candidate']==234 and decision['source_commit']==SOURCE and decision['binary_sha256']==BINARY and decision['decision_utc'] and decision['decision_reference'],'Explicit final root acceptance missing')
 require(decision['independent_review_sha256']==s['independent_review']['sha256'] and decision['handoff_receipt_sha256']==s['handoff']['sha256'] and decision['public_report_sha256']==s['public_report']['sha256'] and decision['final_allowlist_sha256']==s['final_allowlist']['sha256'],'Root decision does not bind exact final evidence/files')
 require(review['candidate']==234 and review['source_commit']==SOURCE and review['binary_sha256']==BINARY and review['independent_checkpoint_accepted'] is True and review['public_report_sha256']==s['public_report']['sha256'],'Independent final acceptance mismatch')
 gates=review['required_gates'];require(set(gates)==GATES,'Required final gate set differs')
 for name,item in gates.items():
  require(item['accepted'] is True,'Rejected final gate '+name);pinned(item)
 build=ROOT/'agent/validation/iteration-234';state=read(build/'build-state.json');mp=build/'source-manifest-after.json';m=read(mp);require(m==read(build/'source-manifest-before.json') and state['head']==SOURCE and state['binary_sha256']==BINARY and state['source_unchanged_during_build'] is True,'Sealed final build mismatch')
 require(m['sha256']==state['source_manifest_before']==state['source_manifest_after']==prep['source_manifest_digest'] and len(m['files'])==prep['recorded_input_count']==650,'Input digest/count differs')
 require(state['build_exit_code']==state['startup_exit_code']==0 and sha(mp)==prep['source_manifest_file_sha256'],'Build/input receipt changed')
 require(sha(ROOT/'build/codex-native/lumen')==sha(ROOT/'build/codex-native/lumen-astra-i234')==BINARY,'Actual accepted executable differs')
 hc=git(ROOT,'rev-parse','HEAD').decode().strip();require(hc==handoff['handoff_commit']==decision['final_handoff_commit'],'Final private handoff commit differs')
 require(handoff['candidate']==234 and handoff['tested_source_commit']==SOURCE and handoff['binary_sha256']==BINARY and handoff['equivalence_accepted'] is True and handoff['all_recorded_inputs_unchanged'] is True,'Handoff equivalence missing')
 require(subprocess.run(['git','-C',str(ROOT),'merge-base','--is-ancestor',SOURCE,hc]).returncode==0,'Tested source is not handoff ancestor')
 changed=set(git(ROOT,'diff','--name-only',SOURCE,hc).decode().splitlines());require(changed==set(handoff['approved_changed_paths']) and not(changed&set(m['files'])),'Handoff changed unapproved/measured inputs')
 require(subprocess.run(['git','-C',str(ROOT),'diff','--quiet']).returncode==0 and subprocess.run(['git','-C',str(ROOT),'diff','--cached','--quiet']).returncode==0,'Tracked main handoff state is dirty')
 require(all((ROOT/p).is_file() and not (ROOT/p).is_symlink() and sha(ROOT/p)==h for p,h in m['files'].items()),'Current measured bytes changed after handoff')
 require(git(repo,'rev-parse','HEAD').decode().strip()==PUBLIC and git(repo,'branch','--show-current').decode().strip()==BRANCH,'Unexpected public staging branch/base')
 base=ROOT/'agent/ops/linux-wpt-20261030-session8/public-export226';approved=set(git(base,'rev-list',PUBLIC).decode().splitlines());require(len(approved)==215 and set(git(repo,'rev-list','--all').decode().splitlines())==approved,'Public ancestry changed')
 for row in git(repo,'for-each-ref','--format=%(refname) %(objectname) %(objecttype)').decode().splitlines():
  ref,obj,kind=row.split();require(kind=='commit' and obj in approved,'Nonpublic exported ref '+ref)
 for private in [SOURCE,hc]:require(subprocess.run(['git','-C',str(repo),'cat-file','-e',private+'^{commit}'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0,'Private history entered export')
 require(extra['current_candidate']==234 and extra['current_source_commit']==SOURCE and extra['recorded_input_count']==650 and extra['source_manifest_sha256']==m['sha256'] and extra['private_files_allowed'] is False,'Final allowlist source mismatch')
 require(report['candidate']==234 and report['tested_source_commit']==SOURCE and report['binary_sha256']==BINARY and report['source_manifest_sha256']==m['sha256'],'Public report provenance differs')
 require(all(k in report for k in ['fixed640','scopes','sanitizers','resources','limitations']),'Required public report sections missing')
 # Curated documentation/control overlays are exact reviewed bytes, never raw private receipts.
 overlays={}
 for item in extra['documentation']+extra['staged_authored_controls']:
  rel=item['export_path'];valid(rel);require(rel not in m['files'] and rel not in GENERATED,'Public extras overwrite measured/generated file '+rel)
  p=ROOT/item['staged_source'];require(p.resolve().is_relative_to((OPS/'public-docs-preparation').resolve()),'Final public overlay outside reviewed staging');require(p.is_file() and not p.is_symlink() and sha(p)==item['sha256'],'Reviewed final public file changed '+rel);overlays[rel]={'sha256':item['sha256'],'mode':'100644','source':p}
 for rel,item in overlays.items():
  p=repo/rel;require(not p.is_symlink(),'Public destination symlink');p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(item['source'].read_bytes());p.chmod(0o644)
 dependencies={item['export_path']:item['sha256'] for item in extra['additional_dependencies']}
 initial_extra=OPS/'public-extra-dependencies-allowlist-prepared-v3.json';require(sha(initial_extra)==prep['reviewed_extra_allowlist_sha256'],'Initial reviewed dependency allowlist changed');require(dependencies=={item['export_path']:item['sha256'] for item in read(initial_extra)['additional_dependencies']},'Fixed tested/generator dependency set changed')
 require(dependencies=={rel:prep['overlaid_paths'][rel]['sha256'] for rel in dependencies},'Reviewed generator/dependency set changed')
 for rel,h in {**m['files'],**dependencies}.items():
  valid(rel);p=repo/rel;require(p.is_file() and not p.is_symlink() and sha(p)==h and mode(p)==prep['overlaid_paths'][rel]['mode'],'Exact tested input/dependency/mode mismatch '+rel)
 recovery=f'''# Offline recovery of the reviewed Lumen source\n\nThis package contains the tested source plus a self-contained public-history Git bundle. Private task history, profiles, credentials, dependency caches and the private guarded WPT machinery are excluded.\n\nAfter extracting `src.tar.gz`, enter `lumen-linux-source234` and recover the repository:\n\n```sh\ngit clone ./{BUNDLE} ../lumen-source234-recovered\ngit -C ../lumen-source234-recovered fsck --full\ngit -C ../lumen-source234-recovered status --porcelain\ngit -C ../lumen-source234-recovered rev-parse HEAD\ngit -C ../lumen-source234-recovered rev-parse HEAD^{{tree}}\n```\n\nCompare the final two values and bundle checksum with the separately packaged `PUBLIC-GIT-RECOVERY.json` and `SHA256SUMS`. The checked-out branch is `{BRANCH}`. The delivery receipt records an actual fresh offline restore and exact tree/blob/mode checks.\n\nThe public Git commit is a curated export of tested working source `{SOURCE}`; those commit identifiers are intentionally different. `SOURCE-MANIFEST.json` identifies all 650 recorded build inputs and additional reviewed files. This is not an independent or hermetic rebuild.\n\nBuild and runtime prerequisites, the 147 selected native tests, the three deferred URL names and the exact development-executable limitations are documented in `docs/gpt-6-astra-pro-builtin-rubywebterm-linux-build.md`. External libraries are not bundled. The unchanged WPT selection is reported with its revision and denominators; the private capture/guard environment is not part of this source package.\n'''
 (repo/'OFFLINE-RECOVERY.md').write_text(recovery);(repo/'OFFLINE-RECOVERY.md').chmod(0o644)
 source_manifest={'candidate':234,'tested_source_commit':SOURCE,'tested_binary_sha256':BINARY,'recorded_input_digest':m['sha256'],'recorded_input_count':650,'source_manifest_file_sha256':sha(mp),'public_parent':PUBLIC,'public_branch':BRANCH,'recorded_inputs':{p:{'sha256':h,'mode':prep['overlaid_paths'][p]['mode']} for p,h in m['files'].items()},'reviewed_additional_files':{p:{'sha256':h,'mode':prep['overlaid_paths'][p]['mode']} for p,h in dependencies.items() if p not in m['files']},'reviewed_public_documentation_controls':{p:{'sha256':v['sha256'],'mode':v['mode']} for p,v in overlays.items()},'independent_rebuild_performed':False,'external_dependencies_included':False,'private_history_included':False}
 dump(repo/'SOURCE-MANIFEST.json',source_manifest)
 public_qualification={'candidate':234,'qualification_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'tested_source_commit':SOURCE,'tested_binary_sha256':BINARY,'reviewed_public_parent':PUBLIC,'proposed_public_branch':BRANCH,'recorded_inputs':650,'all_recorded_inputs_and_modes_exact':True,'public_source_manifest_file_sha256':sha(repo/'SOURCE-MANIFEST.json'),'root_decision_sha256':s['root_decision']['sha256'],'independent_final_review_sha256':s['independent_review']['sha256'],'public_report_sha256':s['public_report']['sha256'],'accepted_gate_audit_hashes':{k:v['sha256'] for k,v in gates.items()},'partial_scopes_remain_partial':True,'whole_browser_or_full_WPT_parity_claimed':False,'private_history_or_raw_evidence_exported':False,'independent_rebuild_performed':False,'portable_binary_distribution':False,'publication_performed_by_this_qualifier':False}
 dump(repo/'EXPORT-QUALIFICATION.json',public_qualification)
 allowed=set(prep['base_paths'])|set(prep['overlaid_paths'])|set(overlays)|GENERATED;actual={}
 for p in repo.rglob('*'):
  rel=p.relative_to(repo)
  if '.git' in rel.parts:continue
  require(not p.is_symlink(),'Special public symlink '+str(rel))
  if p.is_file():actual[rel.as_posix()]={'sha256':sha(p),'mode':mode(p)}
 require(set(actual)==allowed,'Unlisted/missing public path')
 for rel,entry in prep['base_paths'].items():
  if rel not in prep['overlaid_paths'] and rel not in overlays and rel not in GENERATED:require(oid(repo/rel)==entry['oid'] and mode(repo/rel)==entry['mode'],'Unapproved historical public file change '+rel)
 require(all(sha(repo/p)==h and sha(ROOT/p)==h for p,h in m['files'].items()),'Final measured input changed')
 require(git(ROOT,'rev-parse','HEAD').decode().strip()==hc,'Main handoff changed during qualification')
 result={**public_qualification,'private_handoff_commit':hc,'spec_sha256':a.spec_sha256,'qualifier_sha256':sha(__file__),'final_allowlist_sha256':s['final_allowlist']['sha256'],'preparation_sha256':s['preparation']['sha256'],'handoff_receipt_sha256':s['handoff']['sha256'],'public_file_inventory':actual,'public_file_count':len(actual),'public_ancestry_count_before_commit':215,'qualification_accepted':True}
 dump(receipt,result);print(json.dumps({'candidate':234,'qualification_accepted':True,'public_file_count':len(actual),'recorded_inputs':650,'receipt_sha256':sha(receipt),'no_commit_push_archive_or_browser_launch':True}))
if __name__=='__main__':main()
