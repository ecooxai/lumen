"""Exact historical source checkpoints; preserve later working-tree candidates."""
from pathlib import Path
import argparse, ast, datetime as dt, hashlib, json, os, re, shutil, subprocess
os.environ.update(GIT_PAGER='cat', PAGER='cat')
parser=argparse.ArgumentParser();parser.add_argument('iteration',choices=['011','012','013']);args=parser.parse_args()
BASE=Path('output/session-102900614')
CONFIGS = {'011': {'kind': 'meta', 'prefix': 'html-meta', 'installer': 'tools/apple-session-gpt6as/apply_html_meta.py', 'production': {'src/html/html.c': '99218b33f6190d865619f233164c7bb81627637b7d021ff39899ee8fa20e9a07'}, 'authored': {'tests/test_html_meta_encoding.py': 'f466ea43bdb133c418df706162e4fab1f4d1b3717e9a2ab116d7745a89630468', 'tools/apple-session-gpt6as/apply_html_meta.py': '0f8c8e5fcc5971ace1a5dff535cbab54402ab2734b66b787732ca46c0b925242', 'tools/apple-session-gpt6as/build_response_decoding.py': '1bafcb64ebed361692ca24e57a6ae8a9d46c659a9c2cb2584183b176ef6d52ce', 'tests/test_html_encoding_bounds.c': 'ce828546e389a1af1bdc25e534c79d28189af4f8927086a703f5658895fd0c50', 'tools/apple-session-gpt6as/run_html_encoding_bounds_102900614.py': '01d06623ceceda0b677e9cbc81944cebf7024af77cd9b568e742f4d8eaba59fa', 'tools/apple-session-gpt6as/run_html_encoding_bounds_v2_102900614.py': 'd5c7c8a2c42e645140116ced7fb16635f288afa474b9d4615a3357359b322e9e'}, 'native': '9e5d9bd983bc7525cfad92c07e14049ef941c2a56796148906e31c4c63c2c7dd', 'jsrun': '7ba1ea30e120cf8fee7d854a37757d3ebbd322e6e74a0222c7507b203e8550df', 'stages': 10, 'markers': ['RESULTS html-meta-encoding PASS (29/29 checks)'], 'redPrefix': 'html-meta-red-1', 'redMarker': 'RESULTS html-meta-encoding FAIL (9/29 checks)', 'red': {'passed': 9, 'total': 29}, 'green': {'passed': 29, 'total': 29, 'build_seconds': 1.128, 'all_build_test_seconds': 12.175, 'legacy': 178, 'streams': 74, 'transfer': 68, 'codec': 170, 'document_encoding': 78}, 'title': 'Bounded HTML encoding metadata candidate', 'summary': 'Frozen real-HTTP metadata checks **9/29 RED →29/29 GREEN**; legacy178, streams74, transfer68, codec170 and native document78 remain green. Build1.128s; ten build/test stages12.175s under1Hz sampling with existing V8 reused. Supplemental ASan exposed a pre-existing tokenizer EOF underflow; its failure remains explicit.', 'implementation': 'Bounded1024-byte HTML prescan handles metadata attribute ordering/duplicates, pragma rules, labels, comments and XML declaration fallback. The parser restart and parent encoding policy remain separate.', 'limits': ['No parser restart or parent encoding fallback conformance is claimed.'], 'commitMessage': 'fix: prescan HTML encoding metadata within bounded input'}, '012': {'kind': 'body', 'prefix': 'body-window-handlers', 'installer': 'tools/apple-session-gpt6as/apply_body_handlers.py', 'production': {'src/js/prelude/01-events.js': 'ef927c0e9281ce9d22d267b09ff81d8ebdbbe4db83eaf1f91e597f5c36c6311d', 'src/js/prelude/03-element.js': '48b7ac01190fcb2c732560fc7705d3d1d793228ad508a62ba1313b469dd1e7dd', 'src/js/prelude/08-window.js': '56c1e0bbbd58e1ece29e9060787c46bd9651fd8488f7135d21c33dca5cab0186', 'src/js/jsnative.cc': 'dbf1b47500e0803c78903cee2da2e75c6e789134a7ca88910b94f986eef9931a'}, 'authored': {'tests/test_body_window_handlers.py': '04bbd4c2d5044b0d402f4c6354d98e6fab1cc70f94ccfa0d7c4963da4f8948e9', 'tests/test_borrowed_window_handlers.py': '3cf3649d288b9cd810b727b33c3af500030d143187115aa1045b54458c590e6c', 'tools/apple-session-gpt6as/apply_body_handlers.py': 'b39e75692b30af15b77eb680310a589215436a59f2c6c213af0f40c2ad399cb6', 'tools/apple-session-gpt6as/build_body_handlers.py': 'f6a9f7a638142444fa6614a6a6b886bd62750b5c3e5cf19d2b5e7c7e7cfac7fc', 'tools/apple-session-gpt6as/run_body_handler_reference.py': '69cc7564ec89a158ddd6f518ed7ac3d18dcca9e4fc0fa6f2cdd238f9934bdcbb'}, 'native': 'c2a47e90b142feb848e9dce21296075fa8cbec5ae01b8d973e80608794338a2e', 'jsrun': '8c5a7e7f3de1001b44fd6850f070baa31abfd2f5b6b558c53c3f0478811402d7', 'stages': 10, 'markers': ['RESULTS body-window-handlers PASS (45/45 checks)', 'RESULTS borrowed-window-handlers PASS (4/4 checks)', 'RESULTS window-lifecycle PASS (445/445 checks)'], 'redPrefix': 'body-window-handlers-red-1', 'redMarker': 'RESULTS body-window-handlers FAIL (5/45 checks)', 'red': {'passed': 5, 'total': 45, 'borrowed_passed': 3, 'borrowed_total': 4}, 'green': {'passed': 45, 'total': 45, 'borrowed_passed': 4, 'borrowed_total': 4, 'build_seconds': 3.091, 'all_build_test_seconds': 16.126, 'frame_tasks': 7, 'frame_order': 14, 'microtask_realm': 35, 'window_lifecycle': 445, 'transfer': 68, 'streams': 74, 'legacy': 178}, 'title': 'Body and frameset Window handler aliases', 'summary': 'Frozen body/frameset checks **5/45 RED →45/45 GREEN**; separate borrowed-Window regression **3/4 →4/4**. Existing frame7/14/35, native window445, transfer68, streams74 and legacy178 pass. Build3.091s; all ten stages16.126s. Qualified Chrome actually passes43/45; its pageswap/pagereveal alias failures remain visible.', 'implementation': 'Use private shared Window records for cross-realm handler identity, existing event-listener activation order, private special-event brands and the owner document’s existing inline-handler CSP/sandbox compiler. Blocked attributes preserve valid prior handlers; syntax failure clears them. Parsed body aliases hydrate before author scripts.', 'limits': ['General parser/script interleaving and all DOM event semantics are not claimed.'], 'commitMessage': 'fix: alias body window handlers with owner realm policy'}, '013': {'kind': 'xhr', 'prefix': 'xhr-encoding', 'installer': 'tools/apple-session-gpt6as/apply_xhr_encoding.py', 'production': {'src/base/encoding.c': '21788c99d1ebd0559d63b987efaaf4bfa42eda8df2d91a759413d47af32186ab', 'src/base/encoding.h': 'b205d19524e9a5227f4cb7cb0ed6172ff5154a610c6e29d86df1360ff5b6aa38', 'src/js/text_codec.h': 'cc4176360e07324e171a55abcfe487031281089fea60184f449786cd4d778950', 'src/js/jsnative.cc': '4d3b3e9106451873c8d1fe29823966edbc5e4e81fb453efe2a3412a646fdb220', 'src/js/prelude/07b-net.js': '1c379284e978bcc4c5ec135575e017997558d3762606e1548a3e19a68ff18d39'}, 'authored': {'tests/test_xhr_response_encoding.py': '0af8307a1ff9e24721eedc7298ee0ccdbc84372e67c5573dab17510b86793add', 'tools/apple-session-gpt6as/apply_xhr_encoding.py': 'b42dc2a7093f1b8167161029b67974aa48bacd11a23d30b25402938406c3c7d9'}, 'native': '50eb65330c1dc48e13afb42e7543d38294c2841ebcf154898cb536deaf582fe9', 'jsrun': '668280f7be5249213dc1b68e71dd72ad564e62b1ed5a38a913713f93674be011', 'stages': 11, 'markers': ['RESULTS xhr-response-encoding PASS (42/42 checks)', 'RESULTS html-meta-encoding PASS (29/29 checks)'], 'redPrefix': 'xhr-encoding-red-1', 'redMarker': 'RESULTS xhr-response-encoding FAIL (14/42 checks)', 'red': {'passed': 14, 'total': 42}, 'green': {'passed': 42, 'total': 42, 'build_seconds': 9.11, 'all_build_test_seconds': 21.265, 'meta': 29, 'legacy': 178, 'streams': 74, 'transfer': 68, 'codec': 170, 'document_encoding': 78}, 'title': 'XHR response encoding and data URL bytes', 'summary': 'Frozen XHR/data-URL checks **14/42 RED →42/42 GREEN**; meta29, legacy178, streams74, transfer68, codec170 and native document78 remain green. Build9.110s; eleven stages21.265s under1Hz sampling, reusing prebuilt V8.', 'implementation': 'Decode XHR response bytes with MIME/override charset and BOM precedence, preserve JSON UTF8 behavior and override state, and parse data URLs through percent-byte decoding and forgiving base64 with network errors for invalid payloads.', 'limits': ['Dedicated-worker XMLHttpRequest remains separate work.'], 'commitMessage': 'fix: decode XHR response bytes and data URL payloads'}}
cfg=CONFIGS[args.iteration];number=args.iteration;tag='102900614-'+number
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(*args,raw=False,input=None):
    data=subprocess.check_output(['git','--no-pager',*args],input=input);return data if raw else data.decode().strip()
def read(p):return json.loads(Path(p).read_text())
def write(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
def literal(path,name):
    for stmt in ast.parse(Path(path).read_text()).body:
        if isinstance(stmt,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in stmt.targets):return ast.literal_eval(stmt.value)
    raise RuntimeError('Reviewed installer literal missing: '+name)
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614' or git('diff','--cached','--name-only'):raise RuntimeError('Branch/index coordination required')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['score']!=45 or score['history'][-1]['iteration']!='102900614-'+str(int(number)-1).zfill(3):raise RuntimeError('Checkpoint order or acceptance score differs')
installer=cfg['installer']
for name,digest in cfg['authored'].items():
    if sha(name)!=digest:raise RuntimeError('Reviewed fixture/helper changed: '+name)
if cfg['kind']=='body':
    payload=literal(installer,'manifest');operations=literal(installer,'operations')
else:payload=literal(installer,'PAYLOAD')
if payload['after']!=cfg['production']:raise RuntimeError('Reviewed production hashes differ')
source={}
for name,digest in payload['before'].items():
    before=git('show','HEAD:'+name,raw=True)
    if hashlib.sha256(before).hexdigest()!=digest:raise RuntimeError('Prospective source-chain preimage differs: '+name)
    source[name]=before.decode()
if cfg['kind']=='body':
    for name,old,new in operations:
        if source[name].count(old)!=1:raise RuntimeError('Reconstruction anchor differs: '+name)
        source[name]=source[name].replace(old,new,1)
elif 'files'in payload:source=dict(payload['files'])
else:
    for name,edits in payload['edits'].items():
        lines=source[name].splitlines(True)
        for start,end,replacement in reversed(edits):
            if not(0<=start<=end<=len(lines)):raise RuntimeError('Historical source line range differs: '+name)
            lines[start:end]=[replacement]
        source[name]=''.join(lines)
for name,text in source.items():
    if hashlib.sha256(text.encode()).hexdigest()!=cfg['production'][name]:raise RuntimeError('Reconstructed historical source differs: '+name)
patchpath=BASE/(cfg['prefix']+'-candidate-1/patch-receipt.json');patch=read(patchpath)
if patch['after']!=cfg['production'] or patch['before']!=payload['before']:raise RuntimeError('Executed patch receipt differs')
greenpath=BASE/(cfg['prefix']+'-green-1/summary.json');green=read(greenpath)
if not green['passed'] or len(green['stages'])!=cfg['stages'] or green['artifacts']['lumen']['sha256']!=cfg['native'] or green['artifacts']['jsrun']['sha256']!=cfg['jsrun']:raise RuntimeError('Executed green artifact differs')
for artifact in green['artifacts'].values():
    if sha(artifact.get('snapshot')or artifact.get('path'))!=artifact['sha256']:raise RuntimeError('Frozen candidate executable changed')
for stage in green['stages']:
    monitor=read(stage['monitor'])
    if not stage['passed'] or stage['naturalExitCode']!=0 or stage['endedByMonitor'] or monitor.get('collector_error') or monitor.get('natural_exit_code')!=0 or monitor.get('ended_by_monitor'):raise RuntimeError('Candidate stage infrastructure failed')
    log=Path(stage['monitor']).with_suffix('.log')
    if stage.get('logSHA256') and sha(log)!=stage['logSHA256']:raise RuntimeError('Green raw log differs')
joined='\n'.join('\n'.join(row.get('resultLines',[]))for row in green['stages'])
for marker in cfg['markers']:
    if marker not in joined:raise RuntimeError('Required green assertion group missing: '+marker)
compiledpath=Path(green['compiledSourceManifest']);compiled=read(compiledpath)
for name,digest in compiled.items():
    data=source[name].encode() if name in source else git('show','HEAD:'+name,raw=True)
    if hashlib.sha256(data).hexdigest()!=digest:raise RuntimeError('Exact historical prospective index differs: '+name)
redprefix=BASE/cfg['redPrefix'];red=read(redprefix.with_suffix('.json'));redlog=redprefix.with_suffix('.log').read_text()
if red.get('natural_exit_code')!=1 or red.get('collector_error') or red.get('ended_by_monitor') or cfg['redMarker']not in redlog:raise RuntimeError('Frozen candidate RED differs')
extra=[];qualification={}
if cfg['kind']=='meta':
    bounds=BASE/'html-meta-bounds-2';failure=read(bounds/'summary.json');log=(bounds/'bounds.log').read_text()
    if failure.get('passed')is True or 'AddressSanitizer'not in log or not('negative-size-param'in log or 'SIZE_MAX'in log or 'memcpy-param-overlap'in log):raise RuntimeError('Expected preserved tokenizer sanitizer failure differs')
    for d in [BASE/'html-meta-bounds-1',bounds]:
        if d.is_dir():extra += [p for p in d.iterdir()if p.is_file()and p.suffix in('.json','.log','.csv')]
    qualification={'sanitizerAccepted':False,'knownFailure':'Pre-existing bogus-comment EOF pointer underflow for exact <! causes SIZE_MAX append. Frozen prescan29 passes, but this artifact is not sanitizer-clean; a separate tokenizer correction is required.','failedReceipt':str(bounds/'summary.json')}
elif cfg['kind']=='body':
    borrowed=BASE/'borrowed-window-handlers-red-1';bm=read(borrowed.with_suffix('.json'))
    if bm.get('natural_exit_code')!=1 or bm.get('collector_error') or bm.get('ended_by_monitor') or 'RESULTS borrowed-window-handlers FAIL (3/4 checks)'not in borrowed.with_suffix('.log').read_text():raise RuntimeError('Borrowed cross-realm3/4 RED differs')
    referencepath=BASE/'body-window-handlers-chrome-1/receipt.json';reference=read(referencepath);rows=reference['rows']
    expected={'body and frameset reflect Window onpageswap','body and frameset reflect Window onpagereveal'}
    if len(rows)!=45 or sum(bool(r['ok'])for r in rows)!=43 or {r['name']for r in rows if not r['ok']}!=expected:raise RuntimeError('Qualified Chrome raw43/45 result differs')
    extra += [borrowed.with_suffix(ext)for ext in('.json','.log','.csv')]+[referencepath]
    qualification={'qualifiedChromeBodyFixture':{'recorded':45,'passed':43,'failed':2,'failures':sorted(expected),'policy':'Actual reference failures retained; current HTML WindowEventHandlers includes both event-handler attributes.'},'primarySpecification':['https://html.spec.whatwg.org/multipage/webappapis.html#windoweventhandlers','https://html.spec.whatwg.org/multipage/webappapis.html#event-handler-content-attributes'],'inheritedTokenizerSanitizerFailureUnresolvedInThisArtifact':True}
else:qualification={'inheritedTokenizerSanitizerFailureUnresolvedInThisArtifact':True,'scope':'Window XHR; dedicated-worker XMLHttpRequest is not claimed.'}
allow=[*cfg['production'],*cfg['authored']]
tracked=set(git('ls-files','-z',raw=True).decode().split('\0'))
for name in cfg['authored']:
    if name not in tracked and any(line.rstrip(b' \t')!=line for line in Path(name).read_bytes().splitlines()):raise RuntimeError('Authored whitespace requires correction: '+name)
subprocess.run(['git','--no-pager','diff','--check','--',*cfg['authored']],check=True)
folder=Path('agent/evidence')/tag;folder.mkdir(exist_ok=False)
paths=[patchpath,greenpath,compiledpath,*[redprefix.with_suffix(ext)for ext in('.json','.log','.csv')],*extra]
for stage in green['stages']:paths += [Path(stage['monitor']),Path(stage['monitor']).with_suffix('.log')]
evidence=[];raw=[]
for index,path in enumerate(dict.fromkeys(paths)):
    destination=folder/('%02d-'%index+path.parent.name+'-'+path.name);shutil.copyfile(path,destination);allow.append(str(destination));evidence.append({'source':str(path),'committed':str(destination),'sha256':sha(path),'bytes':path.stat().st_size})
    if path.suffix in('.log','.csv'):raw.append(str(destination))
manifestpath=folder/'compiled-source-sha256.json';write(manifestpath,compiled);allow.append(str(manifestpath))
now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
entry={'iteration':tag,'started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':45,'status':cfg['title'],'native_artifact_promoted':False,'accepted_native_unchanged':score['current_baseline_binary_sha256'],'candidate_binary_sha256':cfg['native'],'candidate_jsrun_sha256':cfg['jsrun'],'candidate_source_manifest':str(manifestpath),'red':cfg['red'],'green':cfg['green'],'qualification':qualification,'evidence':evidence,'raw_byte_preservation':raw,'implementation':cfg['implementation'],'limits':cfg['limits']+['The inherited tokenizer sanitizer failure is unresolved in this exact historical binary.','No fresh original/full-WPT, GUI resource, navigation soak or same signed IPA acceptance is claimed by this checkpoint.'],'model_compute_duration':'Unavailable; parallel worker wall times are not summed','sourceStaging':'Reviewed historical source was reconstructed from exact installer/preimage hashes and staged directly in the index. Later working-tree candidates were preserved.'}
entrypath=Path('agent/evidence')/(tag+'-'+cfg['prefix']+'.json');write(entrypath,entry);allow.append(str(entrypath))
score.setdefault('candidate_artifacts',[]).append({'iteration':tag,'binary_sha256':cfg['native'],'source_manifest':str(manifestpath),'evidence':str(entrypath),'accepted':False,'note':cfg['title']+'; unresolved inherited tokenizer sanitizer failure, fresh combined gates pending.'})
score['history'].append({k:entry[k]for k in('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:out.write(f"\n## {tag} — {cfg['title']}\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; checkpoint `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Accepted **45/100**, artifact733 unchanged.\n- {cfg['summary']}\n- {qualification.get('knownFailure','This exact historical artifact still contains the tokenizer bounds failure recorded in011.')} Source and all relevant RED/GREEN/failed receipts are preserved in `{entrypath}`. No safety, GUI resource or full-WPT promotion follows from these functional gates.\n- {cfg['implementation']}\n")
with Path('agent/continue.md').open('a')as out:out.write(f"\n## Checkpoint{number} — {cfg['title']}\n\nCandidate `{cfg['native']}`: {cfg['summary']} Exact historical source staged without overwriting later working changes. Accepted733/score45 unchanged. The inherited tokenizer sanitizer failure remains unresolved in this binary and requires the separately tested correction. See `{entrypath}`.\n")
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md']
secrets=[p.read_bytes().strip()for p in Path('output').glob('session-*/progress-state/message-access-key')]
for name in allow:
    data=source[name].encode() if name in source else Path(name).read_bytes()
    if Path(name).is_symlink() or any(len(secret)>=24 and secret in data for secret in secrets) or re.search(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data):raise RuntimeError('Unexpected publication material: '+name)
for name,text in source.items():
    oid=git('hash-object','-w','--stdin',input=text.encode());mode=git('ls-files','--stage','--',name).split()[0]
    subprocess.run(['git','--no-pager','update-index','--cacheinfo',mode,oid,name],check=True)
subprocess.run(['git','--no-pager','add','--',*[n for n in allow if n not in source]],check=True)
expected_changed=set()
for name in allow:
    old=git('show','HEAD:'+name,raw=True) if name in tracked else None
    new=source[name].encode() if name in source else Path(name).read_bytes()
    if old!=new:expected_changed.add(name)
if set(git('diff','--cached','--name-only').splitlines())!=expected_changed:raise RuntimeError('Historical candidate staging allowlist differs')
for name,digest in compiled.items():
    if hashlib.sha256(git('show',':'+name,raw=True)).hexdigest()!=digest:raise RuntimeError('Staged historical production manifest differs: '+name)
for row in evidence:
    if hashlib.sha256(git('show',':'+row['committed'],raw=True)).hexdigest()!=row['sha256']:raise RuntimeError('Staged raw evidence differs')
subprocess.run(['git','--no-pager','diff','--cached','--check','--',*[n for n in allow if n not in raw]],check=True)
subprocess.run(['git','--no-pager','commit','-m',cfg['commitMessage']],check=True)
print(json.dumps({'iteration':tag,'commit':git('rev-parse','HEAD'),'score':45,'nativePromoted':False,'candidateBinarySHA256':cfg['native'],'knownTokenizerSanitizerFailureRetained':True,'compiledInputsMatched':len(compiled),'files':sorted(expected_changed),'untouchedWorkingChanges':git('diff','--name-only').splitlines(),'totalSessionSeconds':entry['total_session_seconds']},indent=2))
