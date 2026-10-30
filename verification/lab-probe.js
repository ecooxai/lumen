(function () {
  var started = false, retries = 0, poll;
  function emit(kind, payload) {
    try { console.log('LUME_LAB_EVIDENCE ' + JSON.stringify({kind:kind,payload:payload})); }
    catch(error) { console.log('LUME_LAB_EVIDENCE '+JSON.stringify({kind:'report-error',error:String(error)})); }
  }
  function start() {
    if(started || !window.LumeLab)return;
    started=true;clearInterval(poll);
    emit('environment',LumeLab.snapshot().environment);
    emit('start',{time:Date.now(),viewport:{width:innerWidth,height:innerHeight},devicePixelRatio:devicePixelRatio,visibility:document.visibilityState});
    var commonOnly = /[?&]coverage=common(?:&|$)/.test(location.search);
    var work = commonOnly ? (async function(){for(var id of ['wasm','webgl','webgpu','iframe','websocket'])await LumeLab.run(id);return LumeLab.snapshot();}()) : LumeLab.runAutomatic();
    work.then(function(result){
      Object.keys(result.results).forEach(function(id){emit('result',result.results[id]);});
      emit('completed',{time:Date.now(),coverage:commonOnly?'five-common-functional-probes':'seven-automatic-probes',summary:result.summary,resources:result.resources});
      if(commonOnly)console.log('LUME_CAPTURE_REQUEST overview');
      setTimeout(function(){LumeLab.stopAll('independent native verifier teardown');emit('post-stop',{time:Date.now(),resources:LumeLab.snapshot().resources});},3500);
    },function(error){emit('error',{name:error.name,message:error.message});LumeLab.stopAll('probe failure');});
  }
  window.addEventListener('lume-lab-ready',start,{once:true});
  poll=setInterval(function(){start();if(++retries>100){clearInterval(poll);if(!started)emit('startup-timeout',{readyState:document.readyState,title:document.title,scriptCount:document.scripts.length});}},200);
  start();
}());
