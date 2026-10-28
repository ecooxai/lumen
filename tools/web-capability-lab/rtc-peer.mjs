import {ResourceScope,within,validateEcho,observeDataChannelClose,finishDataChannelClose} from './core.mjs';
const params=new URLSearchParams(location.search);const role=params.get('role')==='answer'?'answer':'offer';const room=params.get('room')||'';
const result={schemaVersion:4,test:'independent-browser-webrtc',role,room,state:'idle',stage:'ready',payloadMatched:false,independentPeerEcho:false,peerCompletionAcknowledged:false,peerChannelCloseAcknowledged:false,peerCompletionSignalSequence:null,peerChannelCloseSignalSequence:null,completionSignalsSent:0,gracefulChannelCloseObserved:false,dataChannelReadyState:null,dataChannelErrorCount:0,processSeparationVerified:false,crossBrowserInteroperability:false,coverage:'Peer-page nonce echo and both-endpoint observed graceful channel close; OS process separation requires the external harness.',resourcesReleased:true,reportContainsSDP:false,userAgent:navigator.userAgent,engineCapabilities:navigator.lumenCapabilities??null};
let scope=null,pc=null,active=false,lastSequence=0;const $=id=>document.getElementById(id);
$('role').textContent=role;$('room').textContent=room||'Add ?room=unique-room-id&role=offer or answer';
function publish(final=false){$('state').textContent=result.state+' · '+result.stage;$('evidence').textContent=JSON.stringify(result,null,2);if(final)console.log('LUME_RTC_INTEROP '+JSON.stringify(result));}
function stop(reason='stopped'){if(!active)return;active=false;scope?.cancel();result.resourcesReleased=true;if(result.state==='running'){result.state='cancelled';result.stage=reason;publish(true);} $('start').disabled=false;}
function pause(ms){return new Promise(resolve=>scope.timer(resolve,ms));}
async function signal(message){const response=await fetch('/api/rtc-signal',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({room,role,message})});if(!response.ok)throw new Error('Signal request rejected');return response.json();}
async function poll(type){while(!scope.closed){const response=await fetch(`/api/rtc-signal?room=${encodeURIComponent(room)}&role=${role}&after=${lastSequence}`,{cache:'no-store'});if(!response.ok)throw new Error('Signal polling rejected');const data=await response.json();if(scope.closed)throw new Error('Stopped');for(const entry of data.messages){lastSequence=Math.max(lastSequence,entry.sequence);if(entry.message.type===type)return entry.message;if(entry.message.type==='error')throw new Error('Remote peer error');}await pause(350);}throw new Error('Stopped');}
async function gathered(){if(pc.iceGatheringState==='complete')return;await new Promise(resolve=>{scope.listen(pc,'icegatheringstatechange',()=>{if(pc.iceGatheringState==='complete')resolve();});scope.timer(resolve,3500);});}
async function networkEvidence(){let evidence={};for(let i=0;i<5;i++){const stats=[...(await pc.getStats()).values()];const transport=stats.find(s=>s.type==='transport'&&s.dtlsState==='connected');const pair=stats.find(s=>s.type==='candidate-pair'&&s.state==='succeeded'&&s.nominated);const channel=stats.find(s=>s.type==='data-channel'&&s.state==='open');evidence={dtlsState:transport?.dtlsState??null,icePairState:pair?.state??null,transportBytesSent:transport?.bytesSent??null,transportBytesReceived:transport?.bytesReceived??null,candidateBytesSent:pair?.bytesSent??null,candidateBytesReceived:pair?.bytesReceived??null,dataChannelBytesSent:channel?.bytesSent??null,dataChannelBytesReceived:channel?.bytesReceived??null};if((evidence.transportBytesSent>0&&evidence.transportBytesReceived>0)||(navigator.lumenCapabilities?.webrtcBackend==='libdatachannel-0.24.6'&&evidence.dataChannelBytesSent>0&&evidence.dataChannelBytesReceived>0))break;await pause(200);}return evidence;}
async function exchange(){
  if(!/^[a-zA-Z0-9_-]{8,80}$/.test(room))throw new Error('Valid room id required');if(navigator.lumenCapabilities?.webrtcNetworkTransport===false){result.state='unsupported';result.stage='engine-declares-no-network-transport';return;}if(!window.RTCPeerConnection){result.state='unsupported';result.stage='RTCPeerConnection unavailable';return;}
  pc=new RTCPeerConnection({iceServers:[]});scope.add(()=>pc.close());result.stage='signaling';publish();const nonce='lume-interop-'+(crypto.randomUUID?.()||Date.now()+'-'+Math.random());
  let complete,channelCompletion;const received=new Promise((resolve,reject)=>{complete=resolve;scope.listen(pc,'connectionstatechange',()=>{if(pc.connectionState==='failed')reject(new Error('Peer connection failed'));});});
  received.catch(()=>{});
  function attach(channel){
    scope.add(()=>channel.close());let sentNonce=false;
    channelCompletion=observeDataChannelClose(channel,scope);
    channel.onerror=()=>{
      Object.assign(result,channelCompletion.recordError());
      if(result.state==='pass'){result.state='fail';result.stage='data-channel-error';publish(true);}
      stop('data-channel-error');
    };
    const begin=()=>{if(sentNonce)return;sentNonce=true;result.stage='data-channel-open';publish();channel.send(nonce);};
    channel.onopen=begin;
    channel.onmessage=event=>{
      if(typeof event.data!=='string')return;
      if(event.data===nonce){validateEcho(nonce,event.data);result.payloadMatched=true;complete();}
      else if(event.data.startsWith('lume-interop-')&&event.data.length<256)channel.send(event.data);
    };
    if(channel.readyState==='open')begin();
  }
  if(role==='offer'){attach(pc.createDataChannel('lume-independent-peer'));await pc.setLocalDescription(await pc.createOffer());await gathered();await signal({type:'offer',sdp:pc.localDescription.sdp});const answer=await poll('answer');await pc.setRemoteDescription({type:'answer',sdp:answer.sdp});}
  else{pc.ondatachannel=event=>attach(event.channel);const offer=await poll('offer');await pc.setRemoteDescription({type:'offer',sdp:offer.sdp});await pc.setLocalDescription(await pc.createAnswer());await gathered();await signal({type:'answer',sdp:pc.localDescription.sdp});}
  result.stage='waiting-for-nonce-echo';publish();await received;const evidence=await networkEvidence();Object.assign(result,evidence);result.connectionState=pc.connectionState;result.stage='nonce-echo-matched-awaiting-peer';result.independentPeerEcho=true;
  await signal({type:'complete'});result.completionSignalsSent=1;
  await poll('complete');result.peerCompletionAcknowledged=true;result.peerCompletionSignalSequence=lastSequence;
  result.stage='peer-acknowledged-awaiting-channel-close';publish();
  Object.assign(result,await finishDataChannelClose(channelCompletion,scope,async()=>{
    Object.assign(result,channelCompletion.snapshot());
    result.stage='channel-closed-awaiting-peer-close';publish();
    await signal({type:'complete'});result.completionSignalsSent=2;
  },async()=>{await poll('complete');return lastSequence;},result.peerCompletionSignalSequence));
  result.state='pass';result.stage='independent-peer-nonce-echo-and-both-close-acknowledgements';
}
async function run(){if(active)return;active=true;scope=new ResourceScope();lastSequence=0;result.state='running';result.stage='starting';result.payloadMatched=false;result.independentPeerEcho=false;result.peerCompletionAcknowledged=false;result.peerChannelCloseAcknowledged=false;result.peerCompletionSignalSequence=null;result.peerChannelCloseSignalSequence=null;result.completionSignalsSent=0;result.gracefulChannelCloseObserved=false;result.dataChannelReadyState=null;result.dataChannelErrorCount=0;result.crossBrowserInteroperability=false;result.resourcesReleased=false;$('start').disabled=true;publish();
  try{await within(exchange(),20000,scope);}catch(error){if(result.state==='running'){result.state='fail';result.errorName=error.name||'Error';result.stage=result.stage==='starting'?'initialization-failed':result.stage+'-failed';/* Do not copy error.message: browser SDP errors may contain raw SDP. */}}
  finally{scope.dispose();active=false;result.resourcesReleased=true;result.finalizedAt=new Date().toISOString();$('start').disabled=false;publish(true);}
  return JSON.parse(JSON.stringify(result));
}
window.LumeRTCPeer={run,stop,snapshot:()=>JSON.parse(JSON.stringify(result))};$('start').addEventListener('click',run);$('stop').addEventListener('click',()=>stop('user-stop'));document.addEventListener('visibilitychange',()=>{if(document.hidden)stop('page-hidden');});window.addEventListener('pagehide',()=>stop('page-unloaded'));publish();if(params.get('autostart')==='1')run();
