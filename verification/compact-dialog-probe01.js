/* Actual modal geometry on an owned fixture. No account or provider is used. */
(function () {
  'use strict';
  if (window.top !== window) return;
  var parsed = new URL(location.href), nonce=parsed.searchParams.get('nonce');
  if(parsed.hostname!=='127.0.0.1'||parsed.pathname!=='/compact-dialog'||!/^[a-f0-9]{24}$/.test(nonce||''))return;
  function rect(element){var r=element.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};}
  function frame(){return new Promise(function(resolve){requestAnimationFrame(resolve);});}
  async function run(){
    var dialog=document.getElementById('consent'),form=dialog.querySelector('form');
    var initial={open:dialog.open,rect:rect(dialog)};
    dialog.showModal();await frame();await frame();
    var result={schemaVersion:1,stage:'completed',nonce:nonce,scope:'Real script-opened modal geometry; native input is not exercised by this fixture',
      initial:initial,open:dialog.open,viewport:{width:innerWidth,height:innerHeight},dialog:rect(dialog),form:rect(form),
      controls:{accept:rect(document.getElementById('accept')),cancel:rect(document.getElementById('cancel'))},
      computed:{height:getComputedStyle(dialog).height,position:getComputedStyle(dialog).position},
      simulation:true,authenticated:false,providerContacted:false,physicalInputClaim:false,
      focusOrInertnessConformanceClaim:false,benchmarkExecuted:false,benchmarkScore:null};
    document.getElementById('fixture-status').textContent='Actual modal is open for geometry and window capture.';
    console.log('LUME_COMPACT_DIALOG '+JSON.stringify(result));console.log('LUME_CAPTURE_REQUEST overview');
    try{await fetch('/result?nonce='+nonce,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(result)});}catch(_){}
  }
  run();
})();
