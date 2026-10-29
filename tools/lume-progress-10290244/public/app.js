const $=id=>document.getElementById(id);
const config=await fetch('/config.json',{cache:'no-store'}).then(r=>r.json()).catch(()=>({liveBase:''}));
const base=config.liveBase||location.origin;
const sessionPrefix='lume'+(config.messageSessionId||config.taskId||'10290244');
const access=new URLSearchParams(location.hash.slice(1)).get('access')||sessionStorage.getItem(sessionPrefix+'-access');
if(access){sessionStorage.setItem(sessionPrefix+'-access',access);history.replaceState(null,'',location.pathname+location.search);$('send').disabled=false;$('message-state').textContent='Private message access is enabled.';}
const number=(v,digits=1)=>typeof v==='number'&&Number.isFinite(v)?v.toFixed(digits):'—';
const text=(id,value)=>{$(id).textContent=value;};
const fact=(name,value)=>{const row=document.createElement('div');row.className='fact';const key=document.createElement('span'),detail=document.createElement('strong');key.textContent=name;detail.textContent=value;row.append(key,detail);return row;};
function render(snapshot){
  const {status={},metrics,scorecard}=snapshot;
  document.title=`Lume ${status.humanTaskId||status.taskId||config.taskId||''} · Apple WPT progress`;
  const taskLabel=document.getElementById('task-label');if(taskLabel)taskLabel.textContent=`Lume · task ${status.humanTaskId||status.taskId||config.taskId||'pending'}`;
  text('phase',status.phase||'Waiting for a verified progress update');
  const wpt=status.wpt;
  const priority=status.prioritySummary;
  const current=status.priorityObserved||status.currentWpt;
  const fixed=status.fixedComparisonWpt;
  const catalog=status.candidateCatalog,measured=catalog?.latestMeasuredCohort,chrome=catalog?.qualifiedChrome;
  if(current){const rate=current.recorded?100*current.fullyPassing/current.recorded:null;const coverage=current.scheduled?100*current.fullyPassing/current.scheduled:0;text('wpt-rate',rate===null?'Pending':number(rate,2)+'%');text('wpt-count',`${current.fullyPassing.toLocaleString()} / ${current.recorded.toLocaleString()} audited priority variants · ${current.binarySha256.slice(0,12)}…`);text('wpt-scope',`${current.unrun.toLocaleString()} of ${current.scheduled.toLocaleString()} frozen variants remain unrun on this artifact. ${number(coverage,2)}% of the full frozen schedule is recorded fully passing. Earlier artifacts remain separate below. Full official WPT rate is unknown.`);}
  else if(measured){text('wpt-rate',number(measured.passingPercentOfRun,2)+'%');text('wpt-count',`${measured.fullyPassing.toLocaleString()} / ${measured.recorded.toLocaleString()} observed variants · candidate ${measured.binarySha256.slice(0,12)}…`);text('wpt-scope',`${measured.unrun.toLocaleString()} of ${measured.scheduled.toLocaleString()} frozen variants remain unrun. ${number(measured.passingCoveragePercent,2)}% of the full frozen schedule is recorded fully passing. Latest measured cohort; accepted artifact and resource gates are separate. Full official WPT rate is unknown.`);}
  else if(priority){text('wpt-rate',number(priority.observedVariantPassPercent,2)+'%');text('wpt-count',`${priority.fullyPassing.toLocaleString()} / ${priority.recorded.toLocaleString()} observed priority variants · ${priority.scheduled.toLocaleString()} scheduled`);text('wpt-scope',`${priority.isCurrentAcceptedNativeArtifact===false?'Previous native artifact; current build needs this broader run. ':''}${priority.unrun.toLocaleString()} unrun variants remain. ${number(priority.scheduledFullyPassingPercent,2)}% of the frozen schedule is recorded fully passing. Full WPT rate is unmeasured.`);}
  else if(wpt){text('wpt-rate',number(wpt.passPercent,2)+'%');text('wpt-count',`${wpt.passed.toLocaleString()} / ${wpt.total.toLocaleString()} assertions · ${wpt.reportedFiles} files reported`);text('wpt-scope',wpt.scope||'Pinned upstream subset; not a full official WPT rate.');}
  const facts=[fact('Chrome reference',(status.chromeControl?.qualifiedForPairedMeasurement?(chrome?`${status.chromeControl.version} · ${chrome.fullyPassing}/${chrome.recorded} ${chrome.scopeLabel||'Encoding Streams'} variants fully pass · ${chrome.assertions.PASS||0} PASS / ${chrome.assertions.FAIL||0} FAIL assertions`:`${status.chromeControl.version} · known URL failures retained · fresh comparisons pending`):wpt?(Number.isFinite(wpt.chromePassed)&&Number.isFinite(wpt.chromeTotal)?`${wpt.chromePassed} / ${wpt.chromeTotal} assertions${wpt.referenceStatus?' · '+wpt.referenceStatus:''}`:wpt.referenceStatus||'Unverified'):'Unverified')+(status.chromeControl?.observationScope?' · '+status.chromeControl.observationScope:'')),fact('Original subset files',wpt?`${wpt.fullyPassingFiles} / ${wpt.reportedFiles}`:'Unverified'),fact('Full official WPT rate','Unmeasured'),fact('Current native idle CPU',status.performance?.idleStatus?`${status.performance.idleStatus} · p95 ${number(status.performance.idleCpuP95Percent,2)}%`:'Unverified'),fact('Current native idle RSS',typeof status.performance?.idleRssMiB==='number'?number(status.performance.idleRssMiB,2)+' MiB':'Unverified'),fact('Native scrolling CPU',status.performance?`${status.performance.interactionStatus} · p95 ${number(status.performance.interactionCpuP95Percent,2)}% (target ≤${status.performance.interactionCpuCeilingPercent}%)`:'Unverified'),fact('Mac native runtime',status.apple?.macNative||'Unverified'),fact('iPad simulator',status.apple?.ipadSimulator||'Unverified'),fact('Same signed device IPA',status.apple?.sameIpa||'Unverified'),fact('Last verified source',status.sourceCommit?.slice(0,12)||'Pending')];
  if(fixed)facts.unshift(fact('Unchanged fixed-selection comparison',`${fixed.fullyPassing} / ${fixed.recorded} fully passing versus ${fixed.baselineFullyPassing} / ${fixed.baselineRecorded} baseline · overlaps expanded coverage`));
  if(wpt)facts.unshift(fact('Original six-file subset',`${wpt.passed.toLocaleString()} / ${wpt.total.toLocaleString()} assertions · ${wpt.fullyPassingFiles} fully passing files`));
  const historical=status.performance?.historicalInteraction;
  if(historical)facts.push(fact('Previous-build scrolling CPU',`${historical.status} · p95 ${number(historical.cpuP95Percent,2)}% · ${historical.binarySha256?.slice(0,12)||'unknown'}… (historical)`));
  const candidate=status.functionalCandidate;
  if(candidate)facts.push(fact(candidate.historicalRun===false?'Functional candidate':current&&candidate.binarySha256!==current.binarySha256?'Historical functional candidate':'Functional candidate',`${candidate.binarySha256.slice(0,12)}… · ${candidate.fullyPassingVariants}/${candidate.recordedVariants} ${candidate.scopeLabel||'Encoding Stream'} variants fully pass · ${candidate.scheduledFrozenVariants} frozen total · resource acceptance pending`));
  for(const row of catalog?.cohorts||[])facts.push(fact(`${row.historicalRun?'Earlier run':current&&row.binarySha256!==current.binarySha256?'Historical':'Measured'} cohort ${row.binarySha256.slice(0,12)}…`,`${row.fullyPassing}/${row.recorded} variants fully pass (${number(row.passingPercentOfRun,2)}% observed) · ${row.unrun}/${row.scheduled} unrun · ${row.assertions.PASS||0} PASS / ${row.assertions.FAIL||0} FAIL assertions · ${row.historicalRun?'earlier run; counts excluded from current coverage; ':''}${current&&row.binarySha256!==current.binarySha256?'historical evidence; separate artifact':row.acceptedArtifact&&row.binarySha256===current?.binarySha256?'accepted artifact':'candidate; resource acceptance pending'}`));
  if(priority)facts.push(fact('Historical priority matrix',`${priority.binarySha256?.slice(0,12)||'earlier artifact'}… · ${priority.fullyPassing}/${priority.recorded} observed variants · separate historical comparator`));
  if(catalog?.validationErrors?.length)facts.push(fact('Cohort evidence under review',catalog.validationErrors.map(value=>value.cohort+': '+value.error).join('; ')));
  $('compatibility').replaceChildren(...facts);
  renderExpanded(status.expandedWpt&&current?{...status.expandedWpt,isCurrentAcceptedNativeArtifact:status.expandedWpt.lumenBinarySHA256===current.binarySha256}:status.expandedWpt);
  $('source-link').href=base+'/src.tar.gz';
  text('source-detail',status.sourceArchive?`Archive: ${status.sourceArchive.sha256?.slice(0,16)||'hash pending'}… · committed source only`:'Source archive availability is checked separately.');
  if(metrics){
    text('cpu',number(metrics.cpuPercent,1)+'%');text('cpu-detail',`Across ${metrics.cpuCores} logical CPUs`);
    text('ram',number(metrics.memory?.usedGB,2)+' GB');text('ram-detail',`${number(metrics.memory?.totalGB,2)} GB total · ${number(metrics.memory?.freeGB,2)} GB free · includes caches`);
    text('updated',new Date(metrics.sampledAt).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'}));
    const rows=(metrics.disks||[]).map(disk=>{const row=document.createElement('tr');for(const value of[disk.mount,number(disk.totalGB,2),number(disk.usedGB,2),number(disk.availableGB,2),disk.capacityPercent+'%']){const cell=document.createElement('td');cell.textContent=value;row.append(cell);}return row;});
    if(rows.length)$('disks').replaceChildren(...rows);
  }
  const score=scorecard?.score_state==='baseline_audit_pending'?'—':number(scorecard?.score,2);
  text('score',`Iteration ${status.iteration??'—'} · ${score}/100 ${(scorecard?.accepted_artifact_status==='inherited_baseline'||scorecard?.score_state?.startsWith('inherited'))?'inherited task gates':'task gates'}`);
  text('artifact',current?.binarySha256?`Current native ${current.binarySha256.slice(0,20)}… · source ${current.sourceCommit?.slice(0,12)||'pending'}`:wpt?.binarySha256?`Accepted native ${wpt.binarySha256.slice(0,20)}…${measured?' · measured cohort '+measured.binarySha256.slice(0,12)+'…':''}`:'Artifact identity pending');
}
function renderExpanded(data){
  if(!data)return;
  const order=['dom','events','url','headers','microtasks','storage','encoding'];
  const names={dom:'DOM construction / cloning',events:'Events',url:'URL',headers:'Fetch headers',microtasks:'Microtasks',storage:'Web storage',encoding:'Encoding'};
  const rows=order.filter(name=>data.categories?.[name]).map(name=>{
    const value=data.categories[name],row=document.createElement('tr');
    const phase=value.referenceStatus==='quarantined'&&value.lumenRecorded+value.chromeRecorded>0?'Control under review':value.lumenRecorded+value.chromeRecorded===0?'Unrun':value.bothComplete?(value.harnessesHealthy?(value.lumenFullyPassing===value.scheduled?'All pass':`${value.scheduled-value.lumenFullyPassing} Lume files need fixes`):'Errors / timeouts / incomplete harnesses'):'Partial';
    const retention=value.referenceStatus==='quarantined'?'Quarantined':typeof value.chromePassRetention==='number'?number(value.chromePassRetention*100,2)+'%':'Unmeasured';
    for(const content of[names[name],value.scheduled,`${value.lumenRecorded} / ${value.lumenFullyPassing??'—'}`,`${value.chromeRecorded} / ${value.chromeFullyPassing??'—'}${value.chromeSubtestOutcomes?.FAIL?' · '+value.chromeSubtestOutcomes.FAIL+' failed assertions':''}`,retention,phase]){const cell=document.createElement('td');cell.textContent=content;row.append(cell);}
    return row;
  });
  $('expanded-rows').replaceChildren(...rows);
  text('expanded-summary',`${data.selectedVariants.toLocaleString()} frozen variants · ${data.selectedSourceFiles} sources${data.isCurrentAcceptedNativeArtifact===false?' · earlier native artifact':''}`);
  text('expanded-scope',`All generated worker and HTTPS variants remain scheduled. Rates describe observed matched Chrome-passing assertions; unrun files cannot establish a complete category result.${data.lumenBinarySHA256?' Native artifact '+data.lumenBinarySHA256.slice(0,16)+'…':''}${data.referenceReview?' '+data.referenceReview.reason:''}`);
}
let socket,pingTimer,reconnectTimer;
const pending=new Map();
function connect(){
  const url=new URL('/live',base);url.protocol=url.protocol==='https:'?'wss:':'ws:';
  socket=new WebSocket(url);
  socket.onopen=()=>{text('connection','Live · 2s updates');$('connection').className='connection live';clearInterval(pingTimer);pingTimer=setInterval(()=>{if(socket.readyState!==1)return;pending.clear();const nonce=crypto.randomUUID(),now=performance.now();pending.set(nonce,now);socket.send(JSON.stringify({type:'ping',nonce,clientTime:now}));},2000);};
  socket.onmessage=event=>{try{const value=JSON.parse(event.data);if(value.type==='snapshot')render(value.value);if(value.type==='pong'&&pending.has(value.nonce)){text('latency',number(performance.now()-pending.get(value.nonce),1)+' ms');pending.delete(value.nonce);}}catch{}};
  socket.onerror=()=>socket.close();
  socket.onclose=()=>{clearInterval(pingTimer);text('connection','Disconnected · last verified data');$('connection').className='connection';text('latency','—');clearTimeout(reconnectTimer);reconnectTimer=setTimeout(connect,3000);};
}
fetch(base+'/api/progress',{cache:'no-store'}).then(r=>r.json()).then(render).catch(()=>text('phase','Live endpoint is unavailable. No measurements have been invented.'));
connect();
let messages=[];
try{messages=JSON.parse(sessionStorage.getItem(sessionPrefix+'-messages')||'[]');}catch{}
const saveMessages=()=>sessionStorage.setItem(sessionPrefix+'-messages',JSON.stringify(messages.slice(-10)));
function showMessages(){
  $('replies').replaceChildren(...messages.slice(-5).map(message=>{const item=document.createElement('div');item.className='reply';const label=document.createElement('small'),question=document.createElement('p'),answer=document.createElement('p');label.textContent=message.reply?'Agent replied':'Stored for agent review';question.textContent=message.text;answer.textContent=message.reply||'Awaiting the next agent checkpoint.';item.append(label,question,answer);return item;}));
}
$('message-form').addEventListener('submit',async event=>{
  event.preventDefault();if(!access)return;$('send').disabled=true;
  try{const response=await fetch(base+'/api/message',{method:'POST',headers:{'content-type':'application/json',authorization:'Bearer '+access},body:JSON.stringify({text:$('message').value})});const value=await response.json();if(!response.ok)throw Error(value.error||'Message failed');messages.push({...value,text:$('message').value,reply:null});saveMessages();showMessages();$('message').value='';text('message-state','Stored successfully. A reply appears here after agent review.');}catch(error){text('message-state',error.message);}finally{$('send').disabled=false;}
});
async function replies(){for(const message of messages.filter(x=>!x.reply).slice(-10)){try{const response=await fetch(base+`/api/message/${message.id}?key=${encodeURIComponent(message.key)}`,{cache:'no-store'});if(response.ok)Object.assign(message,await response.json());}catch{}}saveMessages();showMessages();}
showMessages();setInterval(replies,2000);
