(()=>{
const params=new URLSearchParams(window.location.search),nonce=params.get('nonce'),parentOrigin=params.get('parentOrigin');
let valid=false;
try{const url=new URL(parentOrigin);valid=['http:','https:'].includes(url.protocol)&&url.origin===parentOrigin&&Boolean(nonce);}catch{}
const streams=new Map(),generations={camera:0,microphone:0};
const out=document.getElementById('result');
document.getElementById('origin').textContent=`Origin: ${window.location.origin} · secure context: ${window.isSecureContext}`;
const send=(status,api,result,state)=>{if(valid)parent.postMessage({type:'lume-frame',nonce,status,api,result,state},parentOrigin);};

function stopCapture(api){generations[api]++;const stream=streams.get(api);if(stream)stream.getTracks().forEach(t=>t.stop());streams.delete(api);if(api==='camera')document.getElementById('preview').srcObject=null;}
function stop(){stopCapture('camera');stopCapture('microphone');out.textContent='All frame media tracks stopped.';}

async function capture(api){
  stopCapture(api);const current=generations[api];
  try{
    if(!window.isSecureContext)throw Error('Trusted HTTPS or localhost required');
    if(!navigator.mediaDevices?.getUserMedia)throw Error('getUserMedia unavailable');
    const stream=await navigator.mediaDevices.getUserMedia(api==='camera'?{video:true,audio:false}:{audio:true,video:false});
    if(current!==generations[api]){stream.getTracks().forEach(t=>t.stop());return;}
    streams.set(api,stream);
    const tracks=api==='camera'?stream.getVideoTracks():stream.getAudioTracks();
    if(!tracks.length||tracks.some(t=>t.readyState!=='live'))throw Error('Capture returned no usable live track.');
    if(api==='camera'){
      const video=document.getElementById('preview');video.srcObject=stream;await video.play();
      if(video.readyState<2)await new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>{video.removeEventListener('loadeddata',loaded);reject(Error('Decoded video timeout'));},10000);
        const loaded=()=>{clearTimeout(timer);resolve();};video.addEventListener('loadeddata',loaded,{once:true});
      });
      if(!(video.videoWidth>0&&video.videoHeight>0))throw Error('Capture returned no decoded video dimensions.');
    }
    if(current!==generations[api])return;
    const message=`PASS: ${api} granted in iframe; ${tracks.length} live track(s).`;
    out.textContent=message;send('result',api,message,'pass');
  }catch(error){
    if(current!==generations[api])return;
    stopCapture(api);
    const message=`${error.name||'Error'}: ${error.message}`;
    const state=['NotAllowedError','NotFoundError','NotReadableError'].includes(error.name)?'blocked':'fail';
    out.textContent=message;send('result',api,message,state);
  }
}
document.getElementById('camera').addEventListener('click',()=>capture('camera'));
document.getElementById('microphone').addEventListener('click',()=>capture('microphone'));
document.getElementById('location').addEventListener('click',()=>{
  if(!navigator.geolocation){out.textContent='Geolocation unavailable';send('result','location',out.textContent,'unsupported');return;}
  navigator.geolocation.getCurrentPosition(position=>{
    const c=position.coords;
    if(!c||![c.latitude,c.longitude,c.accuracy].every(Number.isFinite)||Math.abs(c.latitude)>90||Math.abs(c.longitude)>180||c.accuracy<0){out.textContent='Invalid coordinates or accuracy';send('result','location',out.textContent,'fail');return;}
    out.textContent=`PASS: location received with ±${Math.round(c.accuracy)} m accuracy (coordinates kept private).`;send('result','location',out.textContent,'pass');
  },error=>{out.textContent=`Location blocked/unavailable: ${error.message}`;send('result','location',out.textContent,'blocked');},{timeout:15000,maximumAge:0});
});
document.getElementById('stop').addEventListener('click',stop);
window.addEventListener('pagehide',stop);
document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();});
window.__lumeFrame={stop,get activeTrackCount(){return [...streams.values()].reduce((n,s)=>n+s.getTracks().filter(t=>t.readyState==='live').length,0);}};
window.addEventListener('message',event=>{if(event.origin===parentOrigin&&event.source===parent&&event.data?.type==='lume-frame-stop'&&event.data.nonce===nonce)stop();});
if(navigator.permissions){
  Promise.all(['camera','microphone','geolocation'].map(async name=>{try{return [name,(await navigator.permissions.query({name})).state];}catch{return [name,'query unsupported'];}})).then(entries=>send('policy',null,Object.fromEntries(entries)));
}
send('ready');

})();
