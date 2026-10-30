#!/usr/bin/env python3
"""Create minimized public qualification from already reviewed source and counts."""
import argparse,hashlib,json,pathlib,subprocess
ROOT=pathlib.Path('/mnt/p3/project/browser/lumen-android/lumen-android')
PUBLIC='0742c66b2c9b077c68006c9cb1b4413e281c328d'
def git(repo,*args,input=None):return subprocess.check_output(['git','-C',str(repo),*args],input=input)
def sha(b):return hashlib.sha256(b).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--state',type=pathlib.Path,required=True);p.add_argument('--repo',type=pathlib.Path,required=True);p.add_argument('--draft',action='store_true');a=p.parse_args()
r=json.loads(a.state.read_text());candidate=r['candidate'];repo=a.repo.resolve()
if git(repo,'rev-parse','HEAD').decode().strip()!=PUBLIC:raise SystemExit('Require the reviewed public parent before qualification')
if not a.draft and r['release_acceptance']!='qualified_frozen_scopes_and_latest_nonurl640':raise SystemExit('Final validation is not qualified')
before=ROOT/f'agent/validation/iteration-{candidate}/source-manifest-before.json'
source_map=json.loads(before.read_text())['files']
for path,pin in source_map.items():
 q=repo/path
 if not q.is_file() or sha(q.read_bytes())!=pin:raise SystemExit('Source changed after snapshot preparation')
report_name=f'docs/lumen_gpt-6-astra-pro_builtin-rubywebterm_checkpoint{candidate}.json'
markdown_name=f'docs/lumen_gpt-6-astra-pro_builtin-rubywebterm_report{candidate}.md'
(repo/report_name).write_text(json.dumps(r,indent=2,ensure_ascii=False)+'\n')
def metric(x):return f"{x['assertions_pass']:,}/{x['assertions_total']:,} assertions; {x['files_pass']}/{x['files_total']} whole files"
labels={'parser_options':'Parser options and related Trusted Types','ordinary_scripts':'Ordinary script scheduling','document_streaming':'Document input and document.write','custom_element_parser':'Parser custom elements','document_creation':'Document and element creation','template_ownership':'Template ownership and adoption','cloning':'Node and Document cloning','css_variables':'CSS custom properties','iframe_source_changes':'Iframe source changes','domparser_xml':'DOMParser XML metadata'}
b=r.get('broad_current')
broad=metric(b) if b else 'Latest candidate 640 replay pending; this draft is not a final release qualification.'
lines=[f'# Lumen Linux — candidate {candidate}','',
'Work by **GPT-6 Astra Pro · Built-in agents · RubyWebterm**.','',
f"Qualification: **{r['release_acceptance']}**.",'',
'## Fixed non-URL comparison','',
'The starting and final browsers use the same 640-file selection. Reported assertion denominators remain distinct. This is a sample, not the entire WPT suite.','',
'| Browser | Fully passing files | Passing reported assertions |','| --- | ---: | ---: |',
'| Lumen starting candidate 200 | 380/640 · 59.38% | 8,324/10,540 |']
if b:lines.append(f"| Lumen candidate {candidate} | {b['files_pass']}/640 · {b['files_pass']/640*100:.2f}% | {b['assertions_pass']:,}/{b['assertions_total']:,} |")
else:lines.append(f'| Lumen candidate {candidate} | Pending final replay | Pending final replay |')
lines+=['| Chromium 156.0.8078.80 | 560/640 · 87.50% | 9,551/10,552 |','']
if b:
 delta=b.get('comparison_to200',{});h=b.get('harness',{})
 lines+=['',f"Exact starting-identity comparison: {delta.get('common_identity_pass_gains',0)} gained passes; {delta.get('prior_pass_losses',0)} prior-pass losses; {delta.get('missing_previous_identities',0)} missing prior identities; {delta.get('restored_passing_registrations',0)} restored passing registrations. Whole-file gains/losses: {delta.get('whole_file_gains',0)}/{delta.get('whole_file_losses',0)}.",'',f"Final harness outcomes: {h.get('OK',0)} OK, {h.get('TIMEOUT',0)} TIMEOUT, {h.get('ERROR',0)} ERROR. Timeouts and errors remain in the denominator.",'']
lines+=['',
r['whole_browser_goal'],'',
'## Focused scopes','',
'These scopes overlap each other and the broader sample; their counts must not be added. Each row names the candidate actually measured. Lower starting reported denominators reflect missing registrations after timeouts; restored registrations remain explicit. The CSS reference comparison also retains two encoding/name identity variants in each direction.','',
'| Frozen whole-file scope | Latest measured Lumen | Starting Lumen | Current Chromium 156 |',
'| --- | --- | --- | --- |']
for key,v in r['focused'].items():
 ref=v.get('chromium')
 reftext=metric(ref) if ref else 'Separate control modes' if key=='parser_options' else f"{v.get('chromium_assertions_pass','—')}/{v.get('chromium_assertions_total','—')} assertions"
 lines.append(f"| {labels[key]} | candidate {v['candidate']}: {metric(v)} | {metric(v['baseline']) if 'baseline' in v else 'Existing 20/20 regressed to 4/20 in 211, then recovered'} | {reftext} |")
lines+=['',f"Native gate: **{r['native']['pass']}/{r['native']['total']}**, actual exit 0, with the three dedicated URL tests deferred outside that denominator.",'',
'The CSS comparison gains 64 passing results among existing native identities and restores one after-refresh assertion, with no prior-pass loss or missing prior identity. Total passing reported assertions rise 154→219. The six iframe files now report and pass all 10 assertions. The XML file restores all 16 earlier losses and passes 20/20.','',
'The parser-options scope includes newer draft features. Chromium 156 normal mode reports 433/1961 and 1/18 whole files; its separately labeled experimental mode reports 1218/1961 and 3/18. These feature-coverage differences do not establish overall browser superiority.','',
'## What changed','']
lines += ['- '+x for x in r['changes']]
lines+=['','## Remaining limitations','']
lines += ['- '+x for x in r['limitations']]
lines+=['','## Reproduction and evidence','',
f"- Pinned WPT revision: {r['wpt_revision']}.",
f"- Fixed 640 manifest SHA256: {r['fixed640_manifest_sha256']}.",
f"- Tested working source commit: {r['tested_source_commit']}.",
f"- Immutable tested binary SHA256: {r['tested_binary_sha256']}.",
f"- Recorded source-input manifest SHA256: {r['recorded_source_manifest_sha256']}.",
'- Build jobs 2, native gate jobs 1, at most two browser workers, shared 4.8 CPU budget and 1-second resource monitoring.',
'- Exact assertion identities, harness outcomes, process exit status, source pins and owned-process/profile cleanup are retained in the local validation workspace.',
'- Public source includes the reviewed public Git ancestry and source mapping; private task text, operational records, profiles, inbox data, credentials and protected guard implementation are excluded.',
'- The public source snapshot was not independently rebuilt. It does not include prebuilt V8/wgpu dependencies and is not a hermetic or portable binary distribution.','']
(repo/markdown_name).write_text('\n'.join(lines))
readme=repo/'README.md';text=readme.read_text();start=text.index('## Linux source checkpoint');end=text.index('## Install on macOS',start)
intro=f"""## Linux source checkpoint {candidate} — 30 October 2026

This reviewed Linux engine checkpoint includes parser/script lifecycle, custom-element/document ownership and CSS-variable improvements. {broad}

The selected parser-options, ordinary-script, creation and template/adoption scopes reach complete passes in their recorded runs. Whole-browser 98/100 and full WPT conformance remain uncertified; URL conformance is deferred. Exact candidates, denominators, reference differences and remaining limitations are documented in [the full measured report]({markdown_name}) and [checkpoint JSON]({report_name}).

EXPORT-QUALIFICATION.json maps this public source to the tested working inputs. The export has not been rebuilt independently and makes no portable-binary claim. Existing platform build/dependency instructions follow.

"""
readme.write_text(text[:start]+intro+text[end:])
tracked=git(repo,'ls-files','-z').decode().split('\0');paths=sorted(set(x for x in tracked if x and x not in ['SOURCE-MANIFEST.json','EXPORT-QUALIFICATION.json'])|{report_name,markdown_name})
entries=[]
for path in paths:
 q=repo/path
 if q.is_symlink() or not q.is_file():raise SystemExit('Nonregular public member')
 data=q.read_bytes();mode='100755' if q.stat().st_mode&0o111 else '100644'
 entries.append({'path':path,'git_blob':git(repo,'hash-object','--stdin',input=data).decode().strip(),'sha256':sha(data),'bytes':len(data),'mode':mode})
manifest={'schema':2,'source_candidate':candidate,'original_working_commit':r['tested_source_commit'],'already_public_parent':PUBLIC,'previous_reviewed_export_commit':PUBLIC,'manifest_scope':'Every regular exported file except SOURCE-MANIFEST.json and EXPORT-QUALIFICATION.json; public historical files are retained.','source_file_count':len(entries),'source_bytes':sum(x['bytes'] for x in entries),'files':entries}
(repo/'SOURCE-MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
qualification={'schema':2,'candidate':candidate,'source_commit':r['tested_source_commit'],'already_public_parent':PUBLIC,'previous_reviewed_export_commit':PUBLIC,'proposed_public_branch':r['publication']['source_branch'],'scope':'Reviewed source-only Linux progress checkpoint; no full WPT, whole-browser 98, hermetic-build or portable-binary claim','release_acceptance':r['release_acceptance'],'source_file_count':len(entries),'all_recorded_inputs_included_and_exact':True,'original_binary_sha256':r['tested_binary_sha256'],'export_rebuild_performed':False,'whole_browser_assessment':r['whole_browser_goal'],'additive_metadata':['SOURCE-MANIFEST.json','EXPORT-QUALIFICATION.json',report_name,markdown_name,'README.md checkpoint qualification'],'excluded':['private working Git ancestry','agent/','agents/','AGENTS.md','devsummary.md','private historical docs/','tools/promo/','raw validation/operational receipts','protected guard/auth implementations','profiles/inbox/capabilities/credentials','build artifacts and dependency caches'],'recorded_source_set_manifest_sha256':sha(before.read_bytes()),'recorded_source_set_entries':len(source_map),'validation':{'native':r['native'],'broad_current':r['broad_current'],'focused':r['focused']},'current_limits':r['limitations']}
(repo/'EXPORT-QUALIFICATION.json').write_text(json.dumps(qualification,indent=2,ensure_ascii=False)+'\n')
print(json.dumps({'candidate':candidate,'draft':a.draft,'source_inputs_exact':len(source_map),'manifest_members':len(entries),'source_manifest_sha256':sha((repo/'SOURCE-MANIFEST.json').read_bytes()),'qualification_sha256':sha((repo/'EXPORT-QUALIFICATION.json').read_bytes()),'publication_performed':False}))
