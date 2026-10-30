(function () {
  var begun = false, tries = 0, poll;
  function report(kind, payload) { console.log('LUME_LAB_EVIDENCE ' + JSON.stringify({kind:kind,payload:payload})); }
  function request(stage, selector) {
    var node = selector && document.querySelector(selector);
    if (node) node.scrollIntoView({block:'start',behavior:'instant'}); else window.scrollTo(0,0);
    setTimeout(function(){console.log('LUME_CAPTURE_REQUEST ' + stage);},500);
  }
  function start() {
    if (begun || !window.LumeLab) return;
    begun = true; clearInterval(poll);
    LumeLab.runAutomatic().then(function(result){
      Object.keys(result.results).forEach(function(id){report('result',result.results[id]);});
      report('completed',{summary:result.summary,resources:result.resources,scope:'real neutral demo; no permission or account actions'});
      request('overview');
      setTimeout(function(){request('gpu','[data-capability="webgl"]');},4000);
      setTimeout(function(){request('network','[data-capability="websocket"]');},8000);
      setTimeout(function(){LumeLab.stopAll('showcase capture complete');report('post-stop',{resources:LumeLab.snapshot().resources});},12500);
    },function(error){report('error',{name:error.name,message:error.message});LumeLab.stopAll('showcase failed');});
  }
  window.addEventListener('lume-lab-ready',start,{once:true});
  poll=setInterval(function(){start();if(++tries>100){clearInterval(poll);if(!begun)report('startup-timeout',{readyState:document.readyState});}},200);
  start();
}());
