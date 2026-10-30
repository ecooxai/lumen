(function () {
  var started=false,attempts=0,poll;
  function emit(kind,payload){console.log('LUME_LAB_EVIDENCE '+JSON.stringify({kind:kind,payload:payload}));}
  function start(){
    if(started||!window.LumeLab)return;
    started=true;clearInterval(poll);
    var capability=navigator.lumenCapabilities;
    var descriptor=Object.getOwnPropertyDescriptor(navigator,'lumenCapabilities');
    emit('environment',LumeLab.snapshot().environment);
    emit('transport-contract',{capabilities:capability||null,capabilitiesFrozen:!!capability&&Object.isFrozen(capability),propertyReadOnly:!!descriptor&&descriptor.writable===false&&descriptor.configurable===false,constructorPresent:typeof WebTransport==='function'});
    emit('start',{time:Date.now(),coverage:'webtransport-only',viewport:{width:innerWidth,height:innerHeight},visibility:document.visibilityState});
    LumeLab.run('webtransport').then(function(result){
      emit('result',result);
      emit('completed',{time:Date.now(),coverage:'webtransport-only',resources:LumeLab.snapshot().resources});
      var control=document.querySelector('[data-test="run-webtransport"]');
      if(control&&typeof control.scrollIntoView==='function')control.scrollIntoView({block:'center'});
      console.log('LUME_CAPTURE_REQUEST overview');
      setTimeout(function(){LumeLab.stopAll('independent native WebTransport verifier teardown');emit('post-stop',{time:Date.now(),resources:LumeLab.snapshot().resources});},2500);
    },function(error){emit('error',{name:error.name,message:error.message});LumeLab.stopAll('transport probe rejection');emit('post-stop',{time:Date.now(),resources:LumeLab.snapshot().resources});});
  }
  window.addEventListener('lume-lab-ready',start,{once:true});
  poll=setInterval(function(){start();if(++attempts>100){clearInterval(poll);if(!started)emit('startup-timeout',{readyState:document.readyState,scriptCount:document.scripts.length});}},200);
  start();
}());
