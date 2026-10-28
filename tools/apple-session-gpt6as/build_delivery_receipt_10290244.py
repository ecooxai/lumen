"""Write a standalone, secret-free delivery receipt from verified current evidence."""
import datetime,hashlib,json,pathlib,subprocess
ROOT=pathlib.Path.cwd();OUT=ROOT/'output/session-10290244'
NAME='GPT-6-Astra-Pro_release-integrator_builtin-subagents_devinmac3d_Lume-10290244_delivery-receipt.json'
def read(p):return json.loads(pathlib.Path(p).read_text())
def sha(p):
 h=hashlib.sha256()
 with pathlib.Path(p).open('rb')as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def need(v,m):
 if not v:raise RuntimeError(m)
def git(*a):return subprocess.run(['git',*a],capture_output=True,text=True,check=True,timeout=30).stdout.strip()
head=git('rev-parse','HEAD');branch=git('branch','--show-current');need(branch=='gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244','Wrong branch')
remote=git('ls-remote','--exit-code','origin','refs/heads/'+branch);need(remote.split()[0]==head,'Current committed checkpoint is not confirmed on origin')
score=read(ROOT/'agent/scorecard-10290244.json');status=read(OUT/'progress-state/status.json');manifest=read(OUT/'public/manifest.json');probe=read(OUT/'progress-public-probe.json');reply=read(OUT/'progress-public-reply-check.json')
need(manifest['sourceCommit']==head and sha(OUT/'public/src.tar.gz')==manifest['sha256'],'Current source package differs')
need(probe['passed']and reply['passed']and probe['publicSourceArchive']['fullByteHashVerified'],'Public delivery proof incomplete')
need(probe['publicSourceArchive']['sourceCommit']==head and probe['publicSourceArchive']['sha256']==manifest['sha256'],'Public source verification is stale')
need(probe['wpt']['binarySha256']==score['current_baseline_binary_sha256']==status['wpt']['binarySha256'],'Live accepted native artifact is stale')
need(probe['messageId']==reply['messageId']and reply['agentReplyObserved'],'Message/reply proof incomplete')
need(probe['websocket']['tlsCertificateValidated']and probe['websocket']['handshakeAcceptVerified'],'Live TLS/WebSocket proof failed')
need(1.5<=probe['websocket']['observedSampleIntervalSeconds']<=3,'Live metric refresh proof differs')
gates={g['id']:g for g in score['gates']};wpt_path=gates['wpt_original']['evidence'][0];wpt=read(ROOT/wpt_path)
need(wpt['candidate']['run_info']['browser_version']=='sha256:'+score['current_baseline_binary_sha256'],'WPT source artifact differs')
expanded=status.get('expandedWpt',{});categories={}
for name,row in expanded.get('categories',{}).items():
 categories[name]={k:row[k]for k in ('scheduled','lumenRecorded','lumenFullyPassing','lumenSubtestOutcomes','lumenFileOutcomes','lumenRemaining','referenceStatus','chromeRecorded','chromeFullyPassing','chromeSubtestOutcomes')if k in row}
ref_path=ROOT/'output/wpt-10290244/reference-review.json';reference=read(ref_path)if ref_path.is_file()else{}
perf={}
for key in ('perf_idle','perf_interaction','perf_soak','perf_budgets'):
 g=gates[key];perf[key]={k:g[k]for k in ('status','earned','weight','binary_sha256','scroll_cpu_p95_percent','ceiling_percent','review','build_seconds','prebuilt_v8','note')if k in g}
 # Historical paths are useful for reproducibility, but old measurements never
 # become current results merely because a delivery receipt is being saved.
 perf[key]['historicalEvidenceCount']=len(g.get('historical_evidence',[]))
receipt={'schema':1,'task':'10290244','createdUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'createdBy':{'model':'GPT-6 Astra Pro','agents':'Built-in same-model subagents;3workers plus root coordinator','remoteTool':'devinmac3d','role':'release_integrator'},'status':'Acceptance checkpoint; incomplete requested Apple task','goalScore':98,'acceptanceScore':score['score'],'scoreMeaning':score['scope'],'latestNativeIteration':score['history'][-1]['iteration'],'sessionStartUTC':score['session_start_utc'],'deadlineUTC':score['deadline_utc'],'source':{'branch':branch,'commit':head,'originBranchHEADVerified':True,'project':'/Users/devin/repos/lumen','requestedAlias':'~/project/lumen','codeStorage':'Git is source of truth. This standalone receipt does not duplicate the repository.','archiveURL':probe['publicURL']+'/src.tar.gz','archiveBytes':manifest['bytes'],'archiveSHA256':manifest['sha256'],'fullPublicByteHashVerified':True},'progress':{'url':probe['publicURL'],'tlsVerified':True,'webSocketHandshakeVerified':True,'sampleIntervalSeconds':probe['websocket']['observedSampleIntervalSeconds'],'roundTripMs':probe['websocket']['websocketRoundTripMs'],'messageAndAgentReplyVerified':True,'privateAccessDetails':'Access fragment and message receipt keys are intentionally omitted.'},'native':{'binarySHA256':score['current_baseline_binary_sha256'],'originalWptSubset':{'passed':wpt['candidate']['subtests']['passed'],'unionInventory':wpt['candidate']['subtests']['union_inventory'],'filesReported':wpt['candidate']['files']['reported'],'filesFullyPassing':wpt['candidate']['files']['passed'],'scope':'Pinned six-file upstream assertion subset, not full official WPT','rawComparisonPath':wpt_path,'rawComparisonSHA256':sha(ROOT/wpt_path)},'expandedPriority':{'selectedVariants':expanded.get('selectedVariants'),'selectedSourceFiles':expanded.get('selectedSourceFiles'),'lumenBinarySHA256':expanded.get('lumenBinarySHA256'),'isCurrentAcceptedNativeArtifact':expanded.get('isCurrentAcceptedNativeArtifact'),'scope':expanded.get('scope'),'selectionScheduleSHA256':expanded.get('selectionScheduleSHA256'),'reportSHA256':expanded.get('reportSHA256'),'categories':categories,'headersSupplemental':expanded.get('headersSupplemental')},'performance':perf},'referenceReview':{k:reference[k]for k in ('status','reason','binarySHA256','probeEvidence','probeSHA256')if k in reference},'acceptanceGates':[{k:g[k]for k in ('id','category','name','weight','earned','status')if k in g}for g in score['gates']],'appleSameDeviceIPA':{key:gates[key]['status']for key in ('ipa_mac','ipa_ipad','ipa_iphone')},'limitations':['The selected1850-assertion result is not a full Web Platform Tests pass rate.','Unsupported, unrun and incomplete variants remain visible; custom native regression counts are separate.','Historical resource evidence does not validate a changed executable.','Mac native and iPad simulator builds do not establish identical device-IPA execution.','No credentials, private access fragments, browser profiles or unrelated files are included.'],'evidence':{'scorecardSHA256':sha(ROOT/'agent/scorecard-10290244.json'),'publicProbeSHA256':sha(OUT/'progress-public-probe.json'),'replyCheckSHA256':sha(OUT/'progress-public-reply-check.json'),'sourceManifestSHA256':sha(OUT/'public/manifest.json')},'backupFallback':{'fileHarbor':'Public reservation API returned403; no alternate route or authentication bypass was attempted.','persistentReceipt':'Save this standalone receipt using the authorized Library alternative; source remains in Git.'}}
custom=[row for row in score.get('history',[])if row.get('binary_sha256')==score['current_baseline_binary_sha256']and isinstance(row.get('targeted_checks'),int)]
if custom:receipt['native']['projectRegressionChecks']={'passed':custom[-1]['targeted_checks'],'total':custom[-1]['targeted_checks'],'acceptedIteration':custom[-1]['iteration'],'scope':'Guarded custom native/window/worker checks, separate from upstream WPT.'}
resource_evidence=ROOT/'agent/evidence/10290244-010-current-native-resources.json'
if resource_evidence.is_file():
 resource=read(resource_evidence)
 if resource.get('nativeSHA256')==score['current_baseline_binary_sha256']:receipt['referenceDiagnostics']=resource.get('referenceDiagnostics',{})
surge=OUT/'surge-publication.json'
if surge.is_file():
 s=read(surge);receipt['surge']={k:s[k]for k in ('deployed','publicURL','sourceCommit','publicViewHashesVerified','publicArchiveLengthVerified','error','finishedUTC')if k in s}
path=OUT/NAME;need(not path.exists(),'Delivery receipt already exists; preserve its identity before replacing')
path.write_text(json.dumps(receipt,indent=2,ensure_ascii=False)+'\n')
print(json.dumps({'path':str(path.relative_to(ROOT)),'bytes':path.stat().st_size,'sha256':sha(path),'sourceCommit':head,'score':score['score'],'nativeSHA256':score['current_baseline_binary_sha256']},indent=2))
