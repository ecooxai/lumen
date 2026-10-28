export function runtimeEngine(navigation) {
  if(navigation.lumenCapabilities?.engine==='Lumen')return 'Lumen';
  const ua=String(navigation.userAgent||'');
  return ua.includes('Lumen')||ua.includes('Lume')?'Lumen':ua.includes('Chrome/')?'Chrome':ua.includes('Safari/')?'Safari / WebKit':'Browser';
}

export class LabView {
  constructor() {this.logCount=0;}
  render(report, resources=0) {
    const {summary,results}=report;
    document.getElementById('pass-count').innerHTML=`${summary.pass} <small>/ ${summary.total}</small>`;
    document.getElementById('pass-meter').style.width=`${summary.pass/summary.total*100}%`;
    document.getElementById('resources').textContent=`${resources} active`;
    const parts=[`${summary.pass} passed`,`${summary.fail} failed`,`${summary.blocked} blocked`,`${summary.unsupported} unsupported`,`${summary.untested} untested`];
    document.getElementById('summary-line').textContent=parts.join(' · ');
    for(const result of results){
      for(const element of document.querySelectorAll(`[data-status-for="${result.id}"], [data-inline-status="${result.id}"]`)){
        element.dataset.state=result.state;element.textContent=result.state;
      }
      const output=document.querySelector(`[data-output-for="${result.id}"]`);if(output&&result.state!=='untested')output.textContent=result.evidence;
      const button=document.querySelector(`[data-testid="run-${result.id}"]`);if(button)button.disabled=result.state==='running';
    }
  }
  log(message) {
    const list=document.getElementById('event-log');if(!this.logCount)list.replaceChildren();
    const row=document.createElement('li'),time=document.createElement('time'),text=document.createElement('span');
    time.textContent=new Date().toLocaleTimeString([], {hour12:false});text.textContent=message;row.append(time,text);list.prepend(row);
    if(list.children.length>40)list.lastElementChild.remove();this.logCount++;
  }
  runtime() {
    const engine=runtimeEngine(navigator);
    document.getElementById('runtime').textContent=`${engine} · ${innerWidth} × ${innerHeight} · DPR ${devicePixelRatio}`;
    const badge=document.getElementById('context-badge');badge.textContent=window.isSecureContext?'● SECURE CONTEXT':'● INSECURE CONTEXT';badge.classList.toggle('warn',!window.isSecureContext);
    const mem=performance.memory;
    document.getElementById('memory').textContent=mem?`${(mem.usedJSHeapSize/1048576).toFixed(1)} MiB JS heap only`:'Not exposed by this engine';
  }
  iteration(data) {
    const iteration=Number.isInteger(data.iteration)?data.iteration:0;
    const score=Number.isFinite(data.score)?` · ${data.score}/100`:'';
    const local=data.localDemoChecks;
    const checks=Number.isInteger(local?.passed)&&Number.isInteger(local?.total)?`\nLocal Chromium checks: ${local.passed}/${local.total}`:'';
    const badge=document.getElementById('iteration-badge');
    badge.textContent=`Iteration ${iteration}/${data.total||6} · ${data.scope||'evidence pending'}${score}${checks}`;
    badge.title=data.scoreNote||'Development checkpoints; independent platform gates remain separate.';
  }
}
