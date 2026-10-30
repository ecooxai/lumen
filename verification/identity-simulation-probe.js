(function () {
  'use strict';
  var started=false,tries=0,poll;
  var evidence={schema:1,status:'waiting',coverage:'local consent-dialog wiring with synthetic DOM clicks',providerAuthenticationTested:false,checks:[],resourcesReleased:false};
  window.LumeIdentityEvidence={snapshot:function(){return JSON.parse(JSON.stringify(evidence));}};
  var pause=function(ms){return new Promise(function(resolve){setTimeout(resolve,ms);});};
  async function bounded(promise){var timer;try{return await Promise.race([promise,new Promise(function(resolve,reject){timer=setTimeout(function(){reject(new Error('Consent dialog did not complete'));},4000);})]);}finally{clearTimeout(timer);}}
  async function run(){
    if(started||!window.LumeLab)return;
    started=true;clearInterval(poll);evidence.status='running';
    try{
      for(var provider of ['google','apple'])for(var choice of ['accepted','cancel']){
        var pending=LumeLab.run(provider),dialog=document.querySelector('#oauth-dialog');
        await pause(300);
        var open=!!dialog&&dialog.open;
        var button=dialog&&dialog.querySelector('button[value="'+choice+'"]');
        if(!open||!button)throw new Error('Consent dialog or choice button unavailable');
        button.click();
        var outcome=await bounded(pending);
        var check={provider:provider,choice:choice,dialogOpened:open,dialogClosed:!dialog.open,
          simulation:outcome.evidence.simulation,authenticated:outcome.evidence.authenticated,
          providerContacted:outcome.evidence.providerContacted,returnedChoice:outcome.evidence.choice,
          state:outcome.state,syntheticClick:true};
        check.passed=check.dialogClosed&&check.simulation===true&&check.authenticated===false&&check.providerContacted===false&&check.returnedChoice===choice&&check.state==='configuration';
        evidence.checks.push(check);
        if(!check.passed)throw new Error('Consent simulation state was incorrect');
      }
      evidence.status='pass';
    }catch(error){evidence.status='fail';evidence.error={name:error.name,message:String(error.message).slice(0,300)};}
    finally{
      LumeLab.stopAll('identity simulation verification finished');
      evidence.resources=LumeLab.snapshot().resources;
      evidence.resourcesReleased=evidence.resources.activeScopes===0&&evidence.resources.registeredCleanups===0&&evidence.resources.activeMediaTracks===0;
      console.log('LUME_IDENTITY_EVIDENCE '+JSON.stringify(evidence));
    }
  }
  window.addEventListener('lume-lab-ready',run,{once:true});
  poll=setInterval(function(){run();if(++tries>100){clearInterval(poll);if(!started){evidence.status='startup-timeout';console.log('LUME_IDENTITY_EVIDENCE '+JSON.stringify(evidence));}}},200);
  run();
}());
