"""Qualify the new signed Chrome control while retaining its real standards failures."""
from pathlib import Path
import datetime as dt, hashlib, json, os, re, subprocess
os.environ.update(GIT_PAGER='cat',PAGER='cat')
ROOT=Path.cwd();TASK='102900614';CHROME='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'
def git(*args):return subprocess.check_output(['git','--no-pager',*args],text=True).strip()
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text())
def write(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
if git('branch','--show-current')!='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614'or git('diff','--cached','--name-only'):raise RuntimeError('Branch/index coordination required')
scorepath=Path('agent/scorecard-102900614.json');score=read(scorepath)
if score['history'][-1]['iteration']!='102900614-005'or score['score']!=45:raise RuntimeError('Reference checkpoint006 ordering changed')
trustpath=Path('output/session-102900614/chrome-official-reference-v2/receipt.json');trust=read(trustpath)
required=('copied-deep-strict-final','copied-publisher-final','copied-gatekeeper-final');steps={row['name']:row for row in trust['steps']}
if not(trust['readyForSanityProbe']and trust['payloadUnchanged']and trust['quarantineAndOtherAttributesUnchanged'])or any(steps[name]['exitCode']!=0 for name in required)or trust['publisherTeam']!='EQHXZ8M8AV'or trust['binarySHA256']!=CHROME or sha(trust['binary'])!=CHROME:raise RuntimeError('Official Chrome payload/publisher/trust validation differs')
if trust['downloadURL']!='https://dl.google.com/chrome/mac/universal/stable/GGRO/googlechrome.dmg'or trust['mountCleanup']['exitCode']!=0:raise RuntimeError('Official provenance or owned mount cleanup differs')
cdppath=Path('output/session-102900614/chrome-native-sanity-1/receipt.json');cdp=read(cdppath)
directpath=Path('output/session-102900614/chrome-independent-diagnostic-1/receipt.json');direct=read(directpath)
if not cdp['probeCompleted']or cdp['binarySHA256']!=CHROME or cdp['binarySHA256After']!=CHROME or cdp['nativeSanityPassed']:raise RuntimeError('Raw CDP evidence changed; do not relabel standards failures')
if direct['binarySHA256']!=CHROME or direct['binarySHA256After']!=CHROME or direct['bothNativeSanityPassed']or len(direct['runs'])!=2:raise RuntimeError('Independent raw comparison identity differs')
left,right=direct['runs']
for row in (left,right):
    if row['exitCode']!=0 or row['forced']or not row['ownedProcessGroupGone']or row['parseError']is not None:raise RuntimeError('Direct browser execution/cleanup incomplete')
if left['mode']!='inherited-environment'or right['mode']!='minimal-environment'or left['domSHA256']!=right['domSHA256']or left['result']['checks']!=right['result']['checks']:raise RuntimeError('Direct native behavior is not independently reproducible')
if direct['environmentVariableNamesRelevantToLoaderOrChrome']or direct['managedChromePolicyNames']:raise RuntimeError('Unexpected loader/Chrome environment or policy needs independent review')
if sum(row['passed']for row in cdp['result']['checks'])!=3 or len(cdp['result']['checks'])!=12 or sum(row['passed']for row in left['result']['checks'])!=1 or len(left['result']['checks'])!=6:raise RuntimeError('Fixed standards probe counts differ')
sourcepath=Path('output/session-102900614/chromium-url-source-review/receipt.json');sources=read(sourcepath)
revision=cdp['browserVersion']['revision'].removeprefix('@')
if sources['revision']!=revision or sources['binarySHA256']!=CHROME:raise RuntimeError('Exact-revision implementation sources differ')
byname={row['path']:row for row in sources['sources']}
for name in ('url/url_features.cc','url/url_canon_fileurl.cc','third_party/blink/renderer/platform/weborigin/kurl.cc'):
    row=byname[name]
    if row.get('error')or sha(row['localPath'])!=row['sha256']:raise RuntimeError('Required public Chromium source is missing or changed: '+name)
features=Path(byname['url/url_features.cc']['localPath']).read_text()
fileurl=Path(byname['url/url_canon_fileurl.cc']['localPath']).read_text()
kurl=Path(byname['third_party/blink/renderer/platform/weborigin/kurl.cc']['localPath']).read_text()
if not re.search(r'BASE_FEATURE\(kDisallowSpaceCharacterInURLHostParsing,\s*base::FEATURE_DISABLED_BY_DEFAULT\)',features):raise RuntimeError('Spaced-host feature default differs from reviewed cause')
if '41299821'not in fileurl or 'SetHostAndPort'not in kurl or 'sloppy'not in kurl:raise RuntimeError('File/host compatibility explanation differs from reviewed source')
oldreview=Path('output/wpt-10290244/reference-review.json');oldsha=sha(oldreview)
now=dt.datetime.now(dt.timezone.utc);previous=dt.datetime.fromisoformat(score['history'][-1]['updated_utc'].replace('Z','+00:00'));start=dt.datetime.fromisoformat(score['session_start_utc'].replace('Z','+00:00'))
folder=Path('agent/evidence/102900614-006');folder.mkdir(exist_ok=False);allow=[]
trust_public={key:trust[key]for key in ('schema','task_id','startedUTC','scope','readyForSanityProbe','downloadURL','archiveSHA256','archiveBytes','originalReceiptSHA256','payloadUnchanged','quarantineAndOtherAttributesUnchanged','bundleVersion','bundleBuild','binary','binarySHA256','publisherTeam','payloadFiles','payloadManifestSHA256','mountCleanup','resources','finishedUTC','wallSeconds')}
trust_public['requiredTrustChecks']=[{key:steps[name][key]for key in ('name','exitCode','wallSeconds')}for name in required]
trust_public['finderInfoMetadataRepair']={'attribute':trust['finderInfoRepair']['attribute'],'copiedPathCount':len(trust['finderInfoRepair']['copiedPaths']),'copiedPathListSHA256':hashlib.sha256(json.dumps(trust['finderInfoRepair']['copiedPaths'],separators=(',',':')).encode()).hexdigest(),'scope':trust['finderInfoRepair']['scope']}
cdp_public={key:cdp[key]for key in ('schema','startedUTC','binarySHA256','probeSourceSHA256','runnerSHA256','scope','probeCompleted','nativeSanityPassed','browserVersion','target','result','binarySHA256After','finishedUTC','wallSeconds')}
source_public={'revision':revision,'browserVersion':sources['browserVersion'],'sources':[{key:row[key]for key in ('path','sourceURL','bytes','sha256','error')if key in row}for row in sources['sources']],'scope':'Only source identity/errors are copied; full public implementation files remain in ignored output. No unavailable source was invented.'}
for name,value in [('official-signature-trust.json',trust_public),('raw-cdp-standards-probe.json',cdp_public),('raw-independent-dump-dom.json',direct),('exact-chromium-source-identities.json',source_public)]:
    path=folder/name;write(path,value);allow.append(str(path))
qualification={'schema':1,'taskId':TASK,'createdUTC':now.isoformat(),'status':'qualified_known_standard_failures','qualifiedForPairedMeasurement':True,'binaryPath':trust['binary'],'binarySHA256':CHROME,'browserVersion':cdp['browserVersion'],'nativeStandardsSanityPassed':False,'standardsFailuresRetained':True,'assertionsModified':False,'qualificationBasis':['Official Google download provenance; strict deep signature, Google publisher team and Gatekeeper pass without security/quarantine changes.','Fresh task profiles reproduce the same native parsing through CDP/about:blank and direct dump-DOM in inherited/minimal environments.','Exact shipped Chromium revision explains the observed URL differences; native known failures do not imply an invalid browser identity.'],'rawOutcomeCounts':{'cdp':{'passed':3,'failed':9,'total':12},'eachDirectRun':{'passed':1,'failed':5,'total':6}},'knownDifferences':[{'case':'Spaced hostname is percent-encoded instead of rejected','standardsExpected':'TypeError for constructor; invalid anchor literal remains invalid','observedChrome':'http://example%20example.com/','cause':'Exact shipped source disables DisallowSpaceCharacterInURLHostParsing by default; Chromium issue416293488 records this known nonconformance.','sources':[byname['url/url_features.cc']['sourceURL'],'https://issues.chromium.org/issues/416293488','https://url.spec.whatwg.org/#forbidden-host-code-point']},{'case':'file://localhost/ keeps localhost instead of serializing empty host','standardsExpected':'file:///','observedChrome':'file://localhost/','cause':'Exact shipped file canonicalizer leaves platform/path-specific behavior and records TODO41299821 to generalize localhost canonicalization.','sources':[byname['url/url_canon_fileurl.cc']['sourceURL'],'https://url.spec.whatwg.org/#file-host-state']},{'case':'file host setter with x:123 mutates host to x while dropping port','standardsExpected':'file://y/ unchanged','observedChrome':'file://x/','cause':'Exact shipped KURL source documents deliberately permissive host/port parsing for compatibility. This is a source-supported explanation consistent with observed native behavior, not a changed WPT expectation.','inference':True,'sources':[byname['third_party/blink/renderer/platform/weborigin/kurl.cc']['sourceURL'],'https://url.spec.whatwg.org/#dom-url-host']}],'evidence':{'trust':str(folder/'official-signature-trust.json'),'cdp':str(folder/'raw-cdp-standards-probe.json'),'independent':str(folder/'raw-independent-dump-dom.json'),'exactSource':str(folder/'exact-chromium-source-identities.json')},'sourceReceiptSHA256':{str(trustpath):sha(trustpath),str(cdppath):sha(cdppath),str(directpath):sha(directpath),str(sourcepath):sha(sourcepath)},'historicalComparator':{'review':str(oldreview),'sha256':oldsha,'statusUnchanged':True,'policy':'Historical quarantined reports remain immutable. New paired runs must pin this exact qualified executable, upstream files, schedule and native artifact; no old comparison is retroactively accepted.'},'scope':'Reference provenance and measurement qualification only. Chrome failures remain FAIL and in all WPT denominators. This is not full conformance, expanded-suite acceptance, native performance or IPA proof.'}
qualified_path=Path('agent/evidence/102900614-006-chrome-reference.json');write(qualified_path,qualification);allow.append(str(qualified_path))
referenceout=Path('output/wpt-102900614/reference-review.json');referenceout.parent.mkdir(parents=True,exist_ok=True)
if referenceout.exists():raise RuntimeError('New task reference-review already exists; preserve it')
write(referenceout,{**qualification,'committedEvidence':str(qualified_path),'committedEvidenceSHA256':sha(qualified_path)})
if sha(oldreview)!=oldsha:raise RuntimeError('Historical reference review was changed')
entry={'iteration':'102900614-006','started_utc':previous.isoformat(),'updated_utc':now.isoformat(),'elapsed_seconds':round((now-previous).total_seconds(),3),'total_session_seconds':round((now-start).total_seconds(),3),'score':score['score'],'status':'Fresh signed Chrome qualified for new paired measurements; all native standards failures retained','reference_binary_sha256':CHROME,'native_artifact_promoted':False,'qualification':str(qualified_path),'stages':{'trust_seconds':trust['wallSeconds'],'native_cdp_seconds':cdp['wallSeconds'],'direct_dump_dom_seconds':[row['durationSeconds']for row in direct['runs']]},'resource_scope':'Signature/copy and browser startup/process-tree samples are diagnostic observations; no idle or general browser performance claim.','model_compute_duration':'Unavailable; parallel worker wall time is not summed'}
entrypath=Path('agent/evidence/102900614-006-reference-checkpoint.json');write(entrypath,entry);allow.append(str(entrypath))
score['reference_qualification']={'evidence':str(qualified_path),'sha256':sha(qualified_path),'binary_sha256':CHROME,'status':qualification['status'],'qualified_for_paired_measurement':True,'standards_failures_retained':True}
gate=next(row for row in score['gates']if row['id']=='wpt_expanded');gate.update(status='incomplete_new_reference_qualified',note='Fresh signed Chrome is qualified for new paired measurements with known standards failures retained. All1515 frozen variants still required; old quarantined comparator reports remain historical and no expanded acceptance points are awarded.')
score['history'].append({key:entry[key]for key in ('iteration','started_utc','updated_utc','elapsed_seconds','total_session_seconds','score','status')});score['updated_utc']=now.isoformat();write(scorepath,score)
with Path('agent/devsummary.md').open('a')as out:out.write(f"\n## 102900614-006 — independently qualified official Chrome reference\n\n- Start `{entry['started_utc']}`; end `{entry['updated_utc']}`; checkpoint `{entry['elapsed_seconds']}s`; total session `{entry['total_session_seconds']}s`. Score remains**{score['score']}/100**; no native or expanded-suite promotion.\n- Official signed Chrome `{trust['bundleVersion']}` SHA-256 `{CHROME}` passed strict signature, Google publisher and Gatekeeper after exact FinderInfo-only repair on an isolated copy. Payload/quarantine/all other attributes preserved; original DMG untouched. Verification `{trust['wallSeconds']}s`,1Hz observations.\n- Native CDP standards probe remains**3/12 pass,9fail**. Independent direct dump-DOM in two fresh profiles/environments remains**1/6 pass,5fail** per run, with identical DOM hash and natural group cleanup. Durations `{[row['durationSeconds']for row in direct['runs']]}`s. No loader/Chrome environment overrides or managed policy names observed.\n- Exact Chromium revision confirms spaced-host rejection is disabled bydefault and file-localhost canonicalization is an open implementation TODO. Host/port compatibility behavior is documented in KURL. Standards expectations and WPT assertions remain unchanged.\n- Reference identity and reproducibility are now qualified for fresh paired runs; standards failures stayFAIL. Historical quarantine/reports are preserved byte-for-byte, and no earlier comparison is retrospectively accepted. Sources and full rationale: `{qualified_path}`.\n")
with Path('agent/continue.md').open('a')as out:out.write(f"\n## Checkpoint006 — qualified fresh comparator\n\nUse exact Chrome `{trust['binary']}` SHA-256 `{CHROME}` with qualification `{qualified_path}` (also ignored `output/wpt-102900614/reference-review.json`). Its native URL failures are genuine known implementation differences, retained asFAIL; this qualification does not assert perfect Chrome conformance. Pin both artifacts and upstream schedule in new paired runs. Preserve all old reference/quarantine reports. Current score{score['score']}, native accepted artifact unchanged.\n")
with Path('AGENTS.md').open('a')as out:out.write(f"\n### Current reference qualification006\n\nThe fresh official signed Chrome `{CHROME}` is qualified for new paired measurements by provenance, native reproducibility and exact implementation-source review. Its real URL standards failures remainFAIL. Historical quarantined reports stay immutable. Use `agent/evidence/102900614-006-chrome-reference.json`; never equate reference qualification with WPT conformance.\n")
# Keep published reference wording aligned while preserving old comparator rows.
updater=Path('tools/apple-session-gpt6as/update_priority_progress_10290244.py');text=updater.read_text()
needle="review_path=BASE/'reference-review.json'"
replacement="""qualified_reference=None
if score.get('reference_qualification'):
    qualification=score['reference_qualification'];proof_path=ROOT/qualification['evidence']
    if sha(proof_path)!=qualification['sha256']:raise RuntimeError('Reference qualification changed')
    proof=json.loads(proof_path.read_text())
    if not proof.get('qualifiedForPairedMeasurement') or proof.get('binarySHA256')!=qualification['binary_sha256'] or not proof.get('standardsFailuresRetained'):raise RuntimeError('Reference qualification is incomplete')
    qualified_reference=proof
    status['chromeControl']={'qualifiedForPairedMeasurement':True,'version':proof['browserVersion']['product'],
        'binarySha256':proof['binarySHA256'],'knownStandardsFailuresRetained':True,'evidence':qualification['evidence'],
        'scope':'Fresh reference identity qualified; historical comparisons unchanged and new paired results pending.'}
else:status.pop('chromeControl',None)
review_path=BASE/'reference-review.json'"""
if text.count(needle)!=1:raise RuntimeError('Reference updater anchor changed')
text=text.replace(needle,replacement)
needle="""        status['wpt']['referenceStatus']='Installed control under review: fresh URL sanity checks failed.'
        status['phase']+=' The installed Chrome control failed independent URL sanity checks; comparative rates are quarantined while raw failures remain visible.'"""
replacement="""        if qualified_reference:
            status['wpt']['referenceStatus']='Fresh signed Chrome qualified for paired measurement; known URL failures retained. Historical comparisons unchanged.'
            status['phase']+=' A fresh official Chrome reference is qualified after independent reproducibility and exact-source review; its real standards failures remain visible. The old comparator matrix stays historical.'
        else:
            status['wpt']['referenceStatus']='Installed control under review: fresh URL sanity checks failed.'
            status['phase']+=' The installed Chrome control failed independent URL sanity checks; comparative rates are quarantined while raw failures remain visible.'"""
if text.count(needle)!=1:raise RuntimeError('Reference status anchor changed')
text=text.replace(needle,replacement);compile(text,str(updater),'exec');updater.write_text(text)
app=Path('tools/lume-progress-10290244/public/app.js');text=app.read_text();needle="fact('Installed control',wpt?(Number.isFinite(wpt.chromePassed)"
replacement="fact('Chrome reference',status.chromeControl?.qualifiedForPairedMeasurement?`${status.chromeControl.version} · known URL failures retained · fresh comparisons pending`:wpt?(Number.isFinite(wpt.chromePassed)"
if text.count(needle)!=1:raise RuntimeError('Reference UI anchor changed')
app.write_text(text.replace(needle,replacement))
allow += [str(scorepath),'agent/devsummary.md','agent/continue.md','AGENTS.md',str(updater),str(app)]
for name in ('verify_official_chrome_copy_102900614.py','run_verified_chrome_sanity_102900614.py','chrome_independent_diagnostic_102900614.py','fetch_chromium_url_sources_102900614.py','progress_inbox_102900614.py'):
    path=Path('tools/apple-session-gpt6as')/name
    if path.is_file():allow.append(str(path))
secrets=[path.read_bytes().strip()for path in Path('output').glob('session-*/progress-state/message-access-key')]
for name in allow:
    path=Path(name)
    if path.is_symlink():raise RuntimeError('Unexpected publication symlink')
    data=path.read_bytes()
    if any(len(secret)>=24 and secret in data for secret in secrets):raise RuntimeError('Private capability in proposed reference evidence')
subprocess.run(['git','--no-pager','add','--',*allow],check=True)
if set(git('diff','--cached','--name-only').splitlines())!=set(allow):raise RuntimeError('Reference staged allowlist differs')
subprocess.run(['git','--no-pager','diff','--cached','--check'],check=True)
subprocess.run(['git','--no-pager','commit','-m','test: qualify signed Chrome while retaining standards failures'],check=True)
print(json.dumps({'iteration':entry['iteration'],'commit':git('rev-parse','HEAD'),'score':score['score'],'referenceEvidence':str(qualified_path),'referenceReview':str(referenceout),'referenceBinarySHA256':CHROME,'rawStandardsFailuresUnchanged':True,'oldReferenceReviewSHA256':oldsha,'files':allow,'untouchedWorkingChanges':git('diff','--name-only').splitlines()},indent=2))
