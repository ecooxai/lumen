import {errorState} from './core.mjs';
let tracks=[];let timer;let generation=0;
window.LumeFrame={activeTracks:()=>tracks.filter(t=>t.readyState==='live').length,stop};
const result=document.querySelector('#result');const video=document.querySelector('video');
function stop(){generation++;tracks.forEach(track=>track.stop());tracks=[];clearTimeout(timer);video.pause();video.srcObject=null;video.hidden=true;}
function tell(capability,state,detail,evidence={}){result.textContent=detail;parent.postMessage({type:'lume-frame-result',capability,state,detail,evidence},location.origin);}
const policy=document.permissionsPolicy||document.featurePolicy;
parent.postMessage({type:'lume-frame-ready',secureContext:isSecureContext,policy:Object.fromEntries(['camera','microphone','geolocation'].map(name=>[name,policy?.allowsFeature?.(name)??null]))},location.origin);
for(const button of document.querySelectorAll('[data-capability]'))button.addEventListener('click',async()=>{
  stop();const token=generation;const kind=button.dataset.capability;
  try{
    if(kind==='location'){
      if(!navigator.geolocation){tell(kind,'unsupported','Geolocation API is absent.');return;}
      const p=await new Promise((resolve,reject)=>navigator.geolocation.getCurrentPosition(resolve,reject,{timeout:15000,maximumAge:0}));
      if(token!==generation)return;
      const provider=navigator.lumenCapabilities?.geolocationProvider||'browser-managed';tell(kind,'pass',`Child frame position returned (${provider}), ±${Math.round(p.coords.accuracy)} m.`,{accuracyMeters:p.coords.accuracy,provider,physicalGPSVerified:false});
    }else{
      if(!navigator.mediaDevices?.getUserMedia){tell(kind,'unsupported','Media capture API is absent.');return;}
      const stream=await navigator.mediaDevices.getUserMedia(kind==='camera'?{video:true,audio:false}:{audio:true,video:false});
      if(token!==generation){stream.getTracks().forEach(t=>t.stop());return;}
      tracks=stream.getTracks();if(kind==='camera'){video.srcObject=stream;video.hidden=false;await video.play();}
      tell(kind,'pass',`Child frame ${kind} acquired. Stops in 8 seconds.`,{trackCount:tracks.length,live:tracks.every(t=>t.readyState==='live')});
      timer=setTimeout(stop,8000);
    }
  }catch(error){if(token===generation)tell(kind,error.code===1?'blocked':errorState(error),`${error.name||'Error'}: ${error.message}`);}
});
document.querySelector('#stop').addEventListener('click',stop);document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();});window.addEventListener('pagehide',stop);
