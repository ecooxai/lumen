import {nonce, timeout, validateAuthMessage, validateFrameMessage, safeTransportURL, validPosition, validVideo, inspectRTCProof} from './model.js';

const $ = id => document.getElementById(id);
const unsupported = message => ({state:'unsupported', evidence:message});
const blocked = message => ({state:'blocked', evidence:message});
const passed = (evidence, keep = false) => ({state:'pass', evidence, keep});
const requireSecure = () => {if (!window.isSecureContext) throw new DOMException('Use trusted HTTPS or localhost.','SecurityError');};
function trackStream(stream, ctx) {
  if (ctx.signal.aborted) {stream.getTracks().forEach(t=>t.stop()); throw new DOMException('Stopped','AbortError');}
  ctx.bag.add(()=>stream.getTracks().forEach(t=>t.stop())); return stream;
}
async function media(constraints, ctx) {
  requireSecure();
  if (!navigator.mediaDevices?.getUserMedia) return null;
  return timeout(navigator.mediaDevices.getUserMedia(constraints).then(stream=>trackStream(stream,ctx)),20000,ctx.signal);
}
function waitEvent(target, type, ctx, ms = 10000) {
  return timeout(new Promise((resolve,reject)=>{
    const done = event=>{clean();resolve(event);}; const fail = event=>{clean();reject(event.error||Error(`${type} failed`));};
    const clean = ()=>{target.removeEventListener(type,done);target.removeEventListener('error',fail);};
    target.addEventListener(type,done,{once:true});target.addEventListener('error',fail,{once:true});ctx.bag.add(clean);
  }),ms,ctx.signal);
}

export async function camera(ctx) {
  const stream = await media({video:{width:{ideal:640},height:{ideal:480}},audio:false},ctx);
  if (!stream) return unsupported('getUserMedia is not exposed by this engine.');
  const video = $('camera-preview'); video.srcObject=stream;ctx.bag.add(()=>{video.pause();video.srcObject=null;});
  await timeout(video.play(),10000,ctx.signal);
  if (video.readyState<2) await waitEvent(video,'loadeddata',ctx);
  if(!validVideo(video,stream.getVideoTracks()[0]))throw Error('Camera returned no live decoded video frames.');
  const settings=stream.getVideoTracks()[0].getSettings();
  return passed(`Live video decoded: ${video.videoWidth} × ${video.videoHeight}; track ${stream.getVideoTracks()[0].readyState}; device ${settings.facingMode||'default'}.`,true);
}

let recordingURL = null;
export function clearRecording() {if(recordingURL){URL.revokeObjectURL(recordingURL);recordingURL=null;}const player=$('mic-replay');player.removeAttribute('src');player.load();}
export async function microphone(ctx) {
  clearRecording();
  const stream=await media({audio:{echoCancellation:true},video:false},ctx);
  if(!stream)return unsupported('getUserMedia is not exposed by this engine.');
  if(!stream.getAudioTracks().some(track=>track.readyState==='live'))throw Error('Microphone returned no live audio track.');
  const AudioContext=window.AudioContext||window.webkitAudioContext;
  if(!AudioContext)return unsupported('A microphone stream opened, but Web Audio is unavailable. Stream will be closed.');
  const audio=new AudioContext();ctx.bag.add(()=>audio.close());
  await timeout(audio.resume(),5000,ctx.signal);
  const analyser=audio.createAnalyser();analyser.fftSize=256;
  const source=audio.createMediaStreamSource(stream);source.connect(analyser);ctx.bag.add(()=>source.disconnect());
  const samples=new Uint8Array(analyser.fftSize);
  const draw=()=>{analyser.getByteTimeDomainData(samples);const rms=Math.sqrt(samples.reduce((sum,v)=>sum+((v-128)/128)**2,0)/samples.length);$('mic-level').value=Math.min(1,rms*4);};draw();
  const meterTimer=setInterval(draw,100);
  ctx.bag.add(()=>{clearInterval(meterTimer);$('mic-level').value=0;});
  let recorderSupported=false;
  if(window.MediaRecorder){
    try {
      const recorder=new MediaRecorder(stream);const chunks=[];let size=0;
      recorder.ondataavailable=e=>{size+=e.data.size;if(size<8*1024*1024&&e.data.size)chunks.push(e.data);};
      recorder.onstop=()=>{if(chunks.length){if(recordingURL)URL.revokeObjectURL(recordingURL);recordingURL=URL.createObjectURL(new Blob(chunks,{type:recorder.mimeType}));$('mic-replay').src=recordingURL;}};
      recorder.start(500);recorderSupported=true;
      ctx.bag.add(()=>new Promise(resolve=>{if(recorder.state==='inactive'){resolve();return;}recorder.addEventListener('stop',resolve,{once:true});recorder.stop();}));
    } catch { /* Live audio still works if this engine cannot record its format. */ }
  }
  const cap=setTimeout(()=>ctx.stop('microphone'),30000);ctx.bag.add(()=>clearTimeout(cap));
  return passed(`Live audio track + ${audio.sampleRate} Hz Web Audio analyser. ${recorderSupported?'Recording available after Stop (30-second cap).':'MediaRecorder unavailable; live capture only.'}`,true);
}

export async function location(ctx) {
  requireSecure();if(!navigator.geolocation)return unsupported('Geolocation is not exposed by this engine.');
  const position=await timeout(new Promise((resolve,reject)=>navigator.geolocation.getCurrentPosition(resolve,reject,{enableHighAccuracy:true,timeout:15000,maximumAge:0})),17000,ctx.signal);
  if(ctx.signal.aborted)throw new DOMException('Stopped','AbortError');
  const {latitude,longitude,accuracy}=position.coords;
  if(!validPosition(position.coords))throw Error('Geolocation returned invalid coordinates or accuracy.');
  $('location-readout').textContent=`${latitude.toFixed(5)}, ${longitude.toFixed(5)} · ±${Math.round(accuracy)} m`;
  const provider=navigator.lumenCapabilities?.geolocationProvider||'browser-managed';
  ctx.detail('location',{provider,accuracyMeters:accuracy,physicalGPSVerified:false});
  return passed(`Received a finite location fix (${provider}) with ±${Math.round(accuracy)} m accuracy. Coordinates omitted; physical GPS is not verified.`);
}

export async function frame(ctx) {
  const cross=$('frame-origin').value==='cross';const url=new URL('./frame.html',window.location.href);
  if(cross){if(!['localhost','127.0.0.1'].includes(url.hostname))return blocked('Cross-origin demo is configured for localhost ↔ 127.0.0.1. Run the local companion.');url.hostname=url.hostname==='localhost'?'127.0.0.1':'localhost';}
  const token=nonce();url.searchParams.set('nonce',token);url.searchParams.set('parentOrigin',window.location.origin);
  for(const api of ['camera','microphone','location'])ctx.detail(`iframe.${api}`,{state:'untested',evidence:'Not exercised in this frame instance.',origin:url.origin});
  const iframe=document.createElement('iframe');iframe.title='Lume delegated permission exercise';iframe.sandbox='allow-scripts allow-same-origin';iframe.allow='camera *; microphone *; geolocation *';
  const host=$('frame-host');host.replaceChildren(iframe);ctx.bag.add(()=>{
    try{iframe.contentWindow.__lumeFrame?.stop();}catch{}
    try{iframe.contentWindow.postMessage({type:'lume-frame-stop',nonce:token},url.origin);}catch{}
    iframe.src='about:blank';iframe.remove();host.textContent='Frame unloaded. All embedded captures stopped.';
  });
  const response=new Promise(resolve=>{
    const onmessage=event=>{
      if(!validateFrameMessage(event,{source:iframe.contentWindow,origin:url.origin,nonce:token}))return;
      if(event.data.status==='ready')resolve(event.data);
      else if(event.data.status==='policy')ctx.detail('iframe.permissionQueries',{states:event.data.result,origin:url.origin,scope:'Permissions API observation; capture is tested separately.'});
      else if(event.data.status==='result'){
        ctx.log(`Iframe ${event.data.api}: ${event.data.result}`);
        if(['camera','microphone','location'].includes(event.data.api))ctx.detail(`iframe.${event.data.api}`,{state:event.data.state||'unknown',evidence:event.data.result,origin:url.origin});
      }
    };
    window.addEventListener('message',onmessage);ctx.bag.add(()=>window.removeEventListener('message',onmessage));
  });iframe.src=url.href;
  await timeout(response,10000,ctx.signal);
  return passed(`${cross?'Cross':'Same'}-origin frame handshake verified (${url.origin}). Use its buttons to test delegated capture/location; handshake alone does not verify them.`,true);
}

export async function wasm() {
  if(!window.WebAssembly)return unsupported('WebAssembly is not exposed by this engine.');
  const binary=new Uint8Array([0,97,115,109,1,0,0,0,1,7,1,96,2,127,127,1,127,3,2,1,0,7,7,1,3,97,100,100,0,0,10,9,1,7,0,32,0,32,1,106,11]);
  const {instance}=await WebAssembly.instantiate(binary);const {add}=instance.exports;
  if(add(20,22)!==42||add(-9,4)!==-5||add(0,0)!==0)throw Error('WASM output mismatch');
  $('wasm-answer').textContent='42';return passed('A real WASM module compiled and executed. add(20,22)=42; add(-9,4)=-5; add(0,0)=0.');
}

export async function webrtc(ctx) {
  if(navigator.lumenCapabilities?.webrtcNetworkTransport===false)return unsupported('This engine declares no network WebRTC transport. Its in-process compatibility shim is not accepted as real RTC transport.');
  if($('rtc-media').checked&&navigator.lumenCapabilities?.webrtcMediaTransport===false)return unsupported('This engine declares native RTC media transport unsupported. Uncheck media to exercise its data channel.');
  if(!window.RTCPeerConnection)return unsupported('RTCPeerConnection is not exposed by this engine.');
  const left=new RTCPeerConnection({iceServers:[]});ctx.bag.add(()=>left.close());
  const right=new RTCPeerConnection({iceServers:[]});ctx.bag.add(()=>right.close());
  let leftRemote=false,rightRemote=false;const toLeft=[],toRight=[];
  left.onicecandidate=e=>{if(e.candidate){if(rightRemote)right.addIceCandidate(e.candidate).catch(()=>{});else toRight.push(e.candidate);}};
  right.onicecandidate=e=>{if(e.candidate){if(leftRemote)left.addIceCandidate(e.candidate).catch(()=>{});else toLeft.push(e.candidate);}};
  const channel=left.createDataChannel('lume-verified-echo');ctx.bag.add(()=>channel.close());
  const token=nonce();let receiver;
  const received=new Promise((resolve,reject)=>{
    right.ondatachannel=e=>{receiver=e.channel;ctx.bag.add(()=>receiver.close());receiver.onmessage=message=>receiver.send(message.data);};
    channel.onopen=()=>channel.send(token);channel.onmessage=e=>e.data===token?resolve():reject(Error('RTC nonce mismatch'));channel.onerror=()=>reject(Error('RTC data channel error'));
  });
  let mediaPromise=null;
  if($('rtc-media').checked){
    const stream=await media({video:{width:{ideal:320},height:{ideal:240}},audio:false},ctx);
    if(!stream)return unsupported('Camera media is unavailable. Uncheck media to test the data channel.');
    stream.getTracks().forEach(track=>left.addTrack(track,stream));
    const preview=$('rtc-preview');preview.classList.add('active');ctx.bag.add(()=>{preview.pause();preview.srcObject=null;preview.classList.remove('active');});
    mediaPromise=new Promise((resolve,reject)=>{right.ontrack=async e=>{preview.srcObject=e.streams[0];try{await preview.play();if(preview.readyState<2)await waitEvent(preview,'loadeddata',ctx);resolve();}catch(error){reject(error);}};});
  }
  await left.setLocalDescription(await left.createOffer());await right.setRemoteDescription(left.localDescription);rightRemote=true;await Promise.all(toRight.splice(0).map(c=>right.addIceCandidate(c)));
  await right.setLocalDescription(await right.createAnswer());await left.setRemoteDescription(right.localDescription);leftRemote=true;await Promise.all(toLeft.splice(0).map(c=>left.addIceCandidate(c)));
  await timeout(received,15000,ctx.signal);if(mediaPromise)await timeout(mediaPromise,15000,ctx.signal);
  await timeout(new Promise(resolve=>{const timer=setTimeout(resolve,200);ctx.bag.add(()=>clearTimeout(timer));}),1000,ctx.signal);
  const stats=[...(await left.getStats()).values()];const proof=inspectRTCProof({stats,capabilities:navigator.lumenCapabilities,connectionState:left.connectionState,iceConnectionState:left.iceConnectionState});
  ctx.detail('webrtc',{...proof,payloadMatched:true,scope:'same-browser peer exercise',remoteVideoDecoded:Boolean(mediaPromise)});
  $('rtc-route').textContent='← nonce echoed →';
  if(!proof.verified)return blocked('Local data-channel nonce echoed, but genuine network transport statistics were not verified. Same-process compatibility behavior is not enough for a transport pass.');
  return passed(`Local peer API exercise: nonce echo plus ${proof.networkStatsObserved?'ICE/DTLS counters':'native selected ICE pair and measured payload counters'} verified.${mediaPromise?' Remote camera frames decoded.':''} Independent cross-browser interoperability remains unverified.`,Boolean(mediaPromise));
}

export async function websocket(ctx) {
  if(!window.WebSocket)return unsupported('WebSocket is not exposed by this engine.');
  const url=new URL($('ws-endpoint').value);if(!['ws:','wss:'].includes(url.protocol)||url.username||url.password)throw Error('Use a ws:// or wss:// echo URL without credentials.');
  const socket=new WebSocket(url);ctx.bag.add(()=>{if(socket.readyState<2)socket.close(1000,'Lab cleanup');});const token=`lume-${nonce()}`;const started=performance.now();
  const echo=new Promise((resolve,reject)=>{socket.onopen=()=>socket.send(token);socket.onmessage=e=>e.data===token?resolve():reject(Error('WebSocket echo payload mismatch'));socket.onerror=()=>reject(Error('WebSocket connection failed. Start the companion server or configure a reachable echo endpoint.'));socket.onclose=()=>reject(Error('WebSocket closed before an echo was received.'));});
  await timeout(echo,10000,ctx.signal);const ms=performance.now()-started;$('ws-roundtrip').textContent=`VERIFIED ROUND TRIP · ${ms.toFixed(1)} ms`;
  return passed(`Real WebSocket echo matched ${token.length} bytes exactly in ${ms.toFixed(1)} ms. Connection closed after verification.`);
}

export async function webtransport(ctx) {
  if(navigator.lumenCapabilities?.webTransport===false)return unsupported('This engine explicitly declares WebTransport unavailable.');
  requireSecure();if(!window.WebTransport)return unsupported('WebTransport is not exposed. WebSocket cannot substitute for a QUIC test.');
  if(!$('wt-endpoint').value.trim())return blocked('Configure a real HTTPS HTTP/3 WebTransport echo endpoint. No connection has been attempted.');
  const url=safeTransportURL($('wt-endpoint').value);const hash=$('wt-cert').value.replace(/[:\s]/g,'');const options={requireUnreliable:true};
  if(hash){if(!/^[a-f\d]{64}$/i.test(hash))throw Error('Certificate SHA-256 must be exactly 64 hexadecimal digits.');options.serverCertificateHashes=[{algorithm:'sha-256',value:Uint8Array.from(hash.match(/../g),x=>parseInt(x,16))}];}
  const transport=new WebTransport(url,options);transport.closed.catch(()=>{});ctx.bag.add(()=>transport.close({closeCode:0,reason:'Lab cleanup'}));
  await timeout(transport.ready,15000,ctx.signal);
  const stream=await timeout(transport.createBidirectionalStream(),5000,ctx.signal);const writer=stream.writable.getWriter(),reader=stream.readable.getReader();
  ctx.bag.add(()=>writer.abort().catch(()=>{}));ctx.bag.add(()=>reader.cancel().catch(()=>{}));
  const token=`lume-quic-${nonce()}`;await timeout(writer.write(new TextEncoder().encode(token)),5000,ctx.signal);await timeout(writer.close(),5000,ctx.signal);
  let text='',streamFinished=false;for(let count=0;count<128;count++){const value=await timeout(reader.read(),5000,ctx.signal);if(value.done){streamFinished=true;break;}text+=new TextDecoder().decode(value.value);if(text.length>token.length)throw Error('Echo payload exceeded expected length');}
  if(!streamFinished)throw Error('Reliable stream did not finish within the bounded read limit');
  if(text!==token)throw Error('WebTransport reliable-stream payload mismatch');
  let datagram='not available';
  if(transport.datagrams?.writable&&transport.datagrams?.readable){
    const dw=transport.datagrams.writable.getWriter(),dr=transport.datagrams.readable.getReader();ctx.bag.add(()=>dw.abort().catch(()=>{}));ctx.bag.add(()=>dr.cancel().catch(()=>{}));
    const message=`datagram-${nonce()}`;await timeout(dw.write(new TextEncoder().encode(message)),5000,ctx.signal);const echoed=await timeout(dr.read(),5000,ctx.signal);if(new TextDecoder().decode(echoed.value)!==message)throw Error('WebTransport datagram mismatch');datagram='nonce verified';
  }else throw Error('WebTransport datagrams are unavailable; reliable echo alone is not a full pass.');
  const reliability=transport.reliability||'engine does not expose reliability';
  const stats=typeof transport.getStats==='function'?await timeout(transport.getStats(),3000,ctx.signal).catch(()=>null):null;
  transport.close({closeCode:0,reason:'Verified echo completed'});await timeout(transport.closed,5000,ctx.signal);
  ctx.detail('webtransport',{reliableStreamEchoMatched:true,datagramEchoMatched:true,streamFinished:true,closedPromiseResolved:true,reliability,quicVerified:false,quicProofRequirement:'Correlate this run with the HTTP/3-only endpoint h3-quic audit log.'});
  return passed(`Reliable stream nonce and EOF verified; datagram ${datagram}; session closed; reliability=${reliability}; requireUnreliable=true. ${stats?'Statistics received. ':''}QUIC protocol proof remains a separate HTTP/3 server-log assertion.`);
}

export async function graphics(ctx) {
  const backend=$('graphics-backend').value;const [width,height]=$('graphics-size').value.split('x').map(Number);
  const canvas=document.createElement('canvas');canvas.id='graphics-canvas';canvas.width=width;canvas.height=height;canvas.setAttribute('aria-label',`${backend} rendered test triangle`);$('graphics-canvas').replaceWith(canvas);
  $('graphics-placeholder').hidden=true;
  ctx.detail(`graphics.${backend}`,{state:'running',canvasPresentationVerified:false});
  try{
    const result=await (backend==='webgpu'||backend==='webgpu-offscreen'?webgpu(canvas,ctx,backend==='webgpu-offscreen'):webgl(canvas,ctx));
    if(result.state!=='pass')ctx.detail(`graphics.${backend}`,{state:result.state,evidence:result.evidence,canvasPresentationVerified:false});
    return result;
  }catch(error){ctx.detail(`graphics.${backend}`,{state:ctx.signal.aborted?'stopped':'fail',evidence:`${error.name||'Error'}: ${error.message}`,canvasPresentationVerified:false});throw error;}
}

function webgl(canvas,ctx) {
  const gl=canvas.getContext('webgl2',{preserveDrawingBuffer:true})||canvas.getContext('webgl',{preserveDrawingBuffer:true});
  if(!gl)return unsupported('WebGL context creation failed. No different backend was selected automatically.');
  ctx.bag.add(()=>gl.getExtension('WEBGL_lose_context')?.loseContext());
  const is2=typeof WebGL2RenderingContext!=='undefined'&&gl instanceof WebGL2RenderingContext;
  const vertex=is2?'#version 300 es\nin vec2 a;void main(){gl_Position=vec4(a,0.0,1.0);}':'attribute vec2 a;void main(){gl_Position=vec4(a,0.0,1.0);}';
  const fragment=is2?'#version 300 es\nprecision mediump float;out vec4 color;void main(){color=vec4(0.1,0.45,0.95,1.0);}':'precision mediump float;void main(){gl_FragColor=vec4(0.1,0.45,0.95,1.0);}';
  const compile=(type,source)=>{const shader=gl.createShader(type);ctx.bag.add(()=>gl.deleteShader(shader));gl.shaderSource(shader,source);gl.compileShader(shader);if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(shader));return shader;};
  const program=gl.createProgram();ctx.bag.add(()=>gl.deleteProgram(program));gl.attachShader(program,compile(gl.VERTEX_SHADER,vertex));gl.attachShader(program,compile(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));gl.useProgram(program);
  const buffer=gl.createBuffer();ctx.bag.add(()=>gl.deleteBuffer(buffer));gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([0,.68,-.68,-.58,.68,-.58]),gl.STATIC_DRAW);const attribute=gl.getAttribLocation(program,'a');gl.enableVertexAttribArray(attribute);gl.vertexAttribPointer(attribute,2,gl.FLOAT,false,0,0);
  gl.viewport(0,0,canvas.width,canvas.height);gl.clearColor(.91,.95,1,1);gl.clear(gl.COLOR_BUFFER_BIT);gl.drawArrays(gl.TRIANGLES,0,3);
  const pixel=new Uint8Array(4);gl.readPixels(Math.floor(canvas.width/2),Math.floor(canvas.height/2),1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);if(gl.getError()!==gl.NO_ERROR||pixel[2]<200||pixel[0]>60)throw Error(`WebGL pixel verification failed: ${pixel}`);
  ctx.detail('graphics.webgl',{state:'pass',backend:is2?'WebGL 2':'WebGL 1',width:canvas.width,height:canvas.height,pixel:[...pixel],framebufferVerified:true,canvasPresentationVerified:false});
  return passed(`${is2?'WebGL 2':'WebGL 1'} shaders compiled; ${canvas.width}×${canvas.height} triangle drawn; center pixel [${pixel.join(',')}] verified. Static frame, no animation loop.`,true);
}

async function webgpu(canvas,ctx,offscreen=false) {
  requireSecure();if(!navigator.gpu)return unsupported('WebGPU is not exposed. Select WebGL explicitly to test the other backend.');
  const adapter=await timeout(navigator.gpu.requestAdapter(),10000,ctx.signal);if(!adapter)return unsupported('No WebGPU adapter is available. Select WebGL explicitly if desired.');
  const device=await timeout(adapter.requestDevice().then(device=>{if(ctx.signal.aborted){device.destroy();throw new DOMException('Stopped','AbortError');}ctx.bag.add(()=>device.destroy());return device;}),10000,ctx.signal);
  if(ctx.signal.aborted){device.destroy();throw new DOMException('Stopped','AbortError');}
  device.lost.then(info=>{if(!ctx.signal.aborted&&info.reason!=='destroyed')ctx.log(`WebGPU device lost: ${info.message}`);});
  let context=null;
  const format=offscreen?'rgba8unorm':navigator.gpu.getPreferredCanvasFormat();
  if(!offscreen){context=canvas.getContext('webgpu');if(!context)return unsupported('WebGPU canvas context unavailable.');context.configure({device,format,alphaMode:'opaque',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});ctx.bag.add(()=>context.unconfigure());}
  device.pushErrorScope('validation');
  const shader=device.createShaderModule({code:'@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {var p=array<vec2f,3>(vec2f(0.0,0.68),vec2f(-0.68,-0.58),vec2f(0.68,-0.58));return vec4f(p[i],0.0,1.0);}@fragment fn fs()->@location(0) vec4f{return vec4f(0.1,0.45,0.95,1.0);}'});
  const pipeline=await device.createRenderPipelineAsync({layout:'auto',vertex:{module:shader,entryPoint:'vs'},fragment:{module:shader,entryPoint:'fs',targets:[{format}]},primitive:{topology:'triangle-list'}});
  const texture=offscreen?device.createTexture({size:[canvas.width,canvas.height,1],format,usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC}):context.getCurrentTexture();
  if(offscreen)ctx.bag.add(()=>texture.destroy());
  const encoder=device.createCommandEncoder();const pass=encoder.beginRenderPass({colorAttachments:[{view:texture.createView(),clearValue:{r:.91,g:.95,b:1,a:1},loadOp:'clear',storeOp:'store'}]});pass.setPipeline(pipeline);pass.draw(3);pass.end();
  const rowBytes=offscreen?Math.ceil(canvas.width*4/256)*256:256;
  const buffer=device.createBuffer({size:rowBytes*(offscreen?canvas.height:1),usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});ctx.bag.add(()=>buffer.destroy());
  encoder.copyTextureToBuffer({texture,origin:offscreen?{x:0,y:0}:{x:Math.floor(canvas.width/2),y:Math.floor(canvas.height/2)}},{buffer,bytesPerRow:rowBytes},{width:offscreen?canvas.width:1,height:offscreen?canvas.height:1});device.queue.submit([encoder.finish()]);await timeout(buffer.mapAsync(GPUMapMode.READ),10000,ctx.signal);
  const mapped=new Uint8Array(buffer.getMappedRange());const center=offscreen?Math.floor(canvas.height/2)*rowBytes+Math.floor(canvas.width/2)*4:0;
  const pixel=[...mapped.slice(center,center+4)];
  if(offscreen){
    const display=canvas.getContext('2d');if(!display)throw Error('2D display for explicitly selected GPU readback is unavailable');
    const image=display.createImageData(canvas.width,canvas.height);
    for(let y=0;y<canvas.height;y++)image.data.set(mapped.subarray(y*rowBytes,y*rowBytes+canvas.width*4),y*canvas.width*4);
    display.putImageData(image,0,0);
  }
  buffer.unmap();const error=await device.popErrorScope();if(error)throw Error(error.message);
  const blue=format.startsWith('bgra')?pixel[0]:pixel[2],red=format.startsWith('bgra')?pixel[2]:pixel[0];if(blue<200||red>60)throw Error(`WebGPU pixel verification failed: ${pixel}`);
  ctx.detail(`graphics.${offscreen?'webgpu-offscreen':'webgpu'}`,{state:'pass',backend:'WebGPU',presentation:offscreen?'explicit offscreen texture readback displayed through 2D':'native canvas texture',width:canvas.width,height:canvas.height,pixel,format,pipelineVerified:true,nativeCanvasTextureVerified:!offscreen,canvasPresentationVerified:false,adapterVendor:adapter.info?.vendor||null,adapterArchitecture:adapter.info?.architecture||null,isFallbackAdapter:adapter.info?.isFallbackAdapter??adapter.isFallbackAdapter??null});
  return passed(`${offscreen?'Explicit offscreen WebGPU':'Native WebGPU canvas texture'}: pipeline compiled; ${canvas.width}×${canvas.height} triangle drawn; ${format} center pixel [${pixel.join(',')}] verified.${offscreen?' Display shows actual GPU readback pixels through 2D. Native WebGPU canvas presentation is NOT verified.':' Window/compositor presentation requires separate screenshot evidence.'} Static frame.`,true);
}

export async function storage(ctx) {
  const token=nonce(),key='lume-web-lab-temporary';
  try{localStorage.setItem(key,token);if(localStorage.getItem(key)!==token)throw Error('localStorage mismatch');}finally{localStorage.removeItem(key);}
  if(!window.indexedDB)return unsupported('localStorage round trip passed; IndexedDB unavailable.');
  const db=await timeout(new Promise((resolve,reject)=>{const request=indexedDB.open('lume-web-lab',1);request.onupgradeneeded=()=>{if(!request.result.objectStoreNames.contains('checks'))request.result.createObjectStore('checks');};request.onsuccess=()=>{if(ctx.signal.aborted){request.result.close();reject(new DOMException('Stopped','AbortError'));}else resolve(request.result);};request.onerror=()=>reject(request.error);request.onblocked=()=>reject(Error('IndexedDB upgrade blocked by another tab'));}),8000,ctx.signal);
  ctx.bag.add(()=>db.close());
  await timeout(new Promise((resolve,reject)=>{const tx=db.transaction('checks','readwrite'),store=tx.objectStore('checks');store.put(token,key);const read=store.get(key);read.onsuccess=()=>{if(read.result!==token){tx.abort();reject(Error('IndexedDB mismatch'));}else store.delete(key);};tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);tx.onabort=()=>reject(Error('IndexedDB transaction aborted'));}),8000,ctx.signal);
  return passed('localStorage and IndexedDB each persisted and returned the exact nonce. Temporary entries removed; database connection closed.');
}

export async function worker(ctx) {
  if(!window.Worker)return unsupported('Dedicated workers are unavailable.');
  const job=new Worker(new URL('./worker.js',import.meta.url));ctx.bag.add(()=>job.terminate());
  const value=await timeout(new Promise((resolve,reject)=>{job.onmessage=e=>resolve(e.data);job.onerror=e=>reject(Error(e.message));job.postMessage({limit:10000});}),8000,ctx.signal);
  if(value.sum!==50005000)throw Error('Worker calculation mismatch');return passed('A real dedicated worker computed 1+…+10,000 = 50,005,000; result verified and worker terminated.');
}

export function auth(provider,ctx) {
  ctx.detail(provider,{simulation:true,authenticated:false,providerContacted:false});
  requireSecure();const state=nonce();const url=new URL('./auth.html',window.location.href);url.searchParams.set('provider',provider);url.searchParams.set('state',state);url.searchParams.set('parentOrigin',window.location.origin);
  const popup=window.open(url.href,`lume-mock-${provider}`,'popup,width=480,height=560');
  if(!popup)return Promise.resolve(blocked('Popup blocked. Allow popups for this local test page and retry.'));
  const pending={origin:url.origin,popup,state,provider,expires:Date.now()+60000};ctx.bag.add(()=>popup.close());
  return timeout(new Promise(resolve=>{
    const receive=e=>{if(!validateAuthMessage(e,pending))return;resolve(e.data.status==='approved'?passed(`LOCAL SIMULATION ONLY: ${provider}-style popup returned with exact state, origin, and popup-window validation. No provider credentials or tokens used.`):blocked('The local simulated flow was cancelled. No real account was involved.'));};
    window.addEventListener('message',receive);ctx.bag.add(()=>window.removeEventListener('message',receive));
    const closed=setInterval(()=>{if(popup.closed)resolve(blocked('Popup closed before the simulated flow completed.'));},500);ctx.bag.add(()=>clearInterval(closed));
  }),60000,ctx.signal);
}

export async function fetchDemo(ctx) {
  const response=await timeout(fetch(new URL('./fixture.json',window.location.href),{cache:'no-store',signal:ctx.signal}),8000,ctx.signal);
  if(!response.ok)throw Error(`HTTP ${response.status}`);const data=await response.json();if(data.expected!==42||data.schemaVersion!==1)throw Error('JSON fixture mismatch');
  $('fetch-info').textContent=`HTTP ${response.status} · JSON VERIFIED · ${window.isSecureContext?'SECURE':'INSECURE'}`;
  return passed(`Same-origin fetch returned HTTP ${response.status} and verified JSON expected=42. Secure context=${window.isSecureContext}.`);
}
