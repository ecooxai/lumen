import { ReportStore, ResourceScope, errorState, within, validateEcho } from './core.mjs';

const definitions = [
  ['camera','Camera','CAM','Request a live video track. Preview stops after 15 seconds.','device'],
  ['microphone','Microphone','MIC','Read real microphone levels. Nothing is recorded or uploaded.','device'],
  ['location','Geolocation','LOC','Request one position. Export includes accuracy, not coordinates.','device'],
  ['wasm','WebAssembly','WASM','Compile a real module and verify that add(20, 22) returns 42.','device'],
  ['webgl','WebGL 2 / 1','GL','Draw a triangle and verify pixels read from the GPU framebuffer.','device'],
  ['webgpu','WebGPU','GPU','Render with WGSL and verify texture bytes copied back from the GPU.','device'],
  ['iframe','Embedded permissions','IF','Load a real iframe. Camera and location have separate child controls.','connection'],
  ['webrtc','WebRTC','RTC','Check local channel behavior and reported ICE/DTLS stats.','connection'],
  ['websocket','WebSocket','WS','Connect to this server and verify an exact echoed message.','connection'],
  ['webtransport','WebTransport / QUIC','H3','Echo a datagram through the optional HTTP/3-only local server.','connection'],
  ['google','Google sign-in','G','Compare a clearly labeled consent simulation with real provider sign-in.','identity'],
  ['apple','Apple sign-in','A','Use a real provider page or preview an explicitly simulated consent flow.','identity']
];
const report = new ReportStore(definitions.map(([id])=>id));
const scopes = new Map();
const liveTracks = new Set();
const generations = new Map();
const logLines = [];
let autoRunning = false;
let autoGeneration = 0;
let config = {};
const $ = selector => document.querySelector(selector);
const snapshot = () => ({ schemaVersion: 1, generatedAt: new Date().toISOString(), environment: { userAgent: navigator.userAgent, secureContext: isSecureContext, origin: location.origin, visibility: document.visibilityState, platform: navigator.platform, hardwareConcurrency: navigator.hardwareConcurrency ?? null, engineCapabilities: navigator.lumenCapabilities ?? null }, summary: report.summary(), resources: resources(), results: structuredClone(report.results), log: [...logLines] });

function resources() {
  let childTracks=0;for(const frame of document.querySelectorAll('iframe')){try{childTracks+=frame.contentWindow.LumeFrame?.activeTracks()||0;}catch{}}
  return { activeScopes: [...scopes.values()].filter(s=>!s.closed && s.size>0).length, registeredCleanups: [...scopes.values()].reduce((n,s)=>n+s.size,0), activeMediaTracks: [...liveTracks].filter(track=>track.readyState==='live').length+childTracks };
}
function log(message) { logLines.push(`${new Date().toLocaleTimeString()}  ${message}`); if(logLines.length>70)logLines.shift(); $('#event-log').textContent=logLines.join('\n'); }
function render() {
  const sum=report.summary(); $('#pass-count').textContent=sum.pass; $('#total-count').textContent=sum.total;
  $('#pass-small').textContent=sum.pass; $('#fail-small').textContent=sum.fail;
  $('#other-small').textContent=sum.unsupported+sum.blocked+sum.configuration;
  $('#run-state').textContent=sum.running?'RUNNING':'READY';
  $('#resource-count').textContent=`${resources().activeScopes} active resource scopes`;
  $('#iteration-status').textContent=`Iteration 01 · Lab checks ${sum.pass}/${sum.total} passed · ${navigator.platform}`;
  for(const result of Object.values(report.results)) {
    const card=$(`[data-capability="${result.id}"]`); const state=card.querySelector('.state');
    state.textContent=result.state==='configuration'?'SETUP NEEDED':result.state; state.className=`state ${result.state}`;
    card.querySelector('.detail').textContent=result.detail;
    card.querySelector('[data-action="run"]').disabled=result.state==='running';
  }
}
function set(id,state,detail,evidence={}) { report.set(id,state,detail,evidence); render(); if(state!=='running') log(`${id.toUpperCase()} · ${state.toUpperCase()} · ${detail}`); }
function stop(id,reason='Stopped by user') {
  const previous=report.results[id];const hadResources=!!scopes.get(id)?.size;
  generations.set(id,(generations.get(id)||0)+1);
  scopes.get(id)?.cancel(); scopes.delete(id);
  if(report.results[id].state==='running')set(id,'cancelled',reason);
  else if(previous.state==='pass'&&hadResources&&!previous.evidence.resourcesStopped)report.set(id,'pass',`${previous.detail} Last run verified; preview stopped.`,{...previous.evidence,resourcesStopped:true});
  render();
}
function stopAll(reason='Stopped by user') { autoGeneration++; for(const [id] of definitions) stop(id,reason); log(`RESOURCES · ${reason}; active scopes ${resources().activeScopes}`); }
function cardFor(id) { return $(`[data-capability="${id}"]`); }
function canvasFor(id) { const previous=cardFor(id).querySelector('canvas');const canvas=previous.cloneNode(false);previous.replaceWith(canvas);canvas.classList.add('visible');return canvas; }
function unsupported(reason) { return { state:'unsupported',detail:reason }; }
function requireSecure() { if(!isSecureContext) {const error=new Error('Use HTTPS or a loopback origin; this page is not a secure context.'); error.name='SecurityError'; throw error;} }

for(const [id,title,icon,description,group] of definitions) {
  const article=document.createElement('article');article.className='card';article.dataset.capability=id;article.dataset.test=`card-${id}`;
  article.innerHTML=`<div class="card-top"><span class="icon"></span><span class="state">IDLE</span></div><h3></h3><p class="description"></p><div class="card-actions"><button data-action="run" data-test="run-${id}">${['camera','microphone','location'].includes(id)?'Request permission':'Run test'}</button><button class="stop" data-action="stop" aria-label="Stop ${title}">■</button></div><p class="detail">Ready to test</p>`;
  article.querySelector('.icon').textContent=icon; article.querySelector('h3').textContent=title; article.querySelector('.description').textContent=description;
  const actions=article.querySelector('.card-actions');
  if(id==='camera') {const video=document.createElement('video');video.className='preview';video.muted=true;video.playsInline=true;video.setAttribute('aria-label','Live camera preview');actions.before(video);}
  if(id==='microphone') {const meter=document.createElement('div');meter.className='mic-meter';meter.innerHTML='<div></div>';meter.setAttribute('role','meter');meter.setAttribute('aria-label','Microphone signal');meter.setAttribute('aria-valuemin','0');meter.setAttribute('aria-valuemax','100');actions.before(meter);}
  if(['webgl','webgpu'].includes(id)) {const canvas=document.createElement('canvas');canvas.className='preview';canvas.width=320;canvas.height=160;canvas.setAttribute('aria-label',`${title} rendered triangle`);actions.before(canvas);}
  if(id==='webgpu'){const mode=document.createElement('select');mode.className='inline-input';mode.setAttribute('aria-label','WebGPU presentation mode');mode.innerHTML='<option value="canvas">Native WebGPU canvas</option><option value="readback">Offscreen GPU + pixel readback</option>';actions.before(mode);}
  if(id==='webrtc'){const peer=document.createElement('a');peer.href='rtc-peer.html';peer.target='_blank';peer.rel='noopener';peer.textContent='Independent browser peer test ↗';actions.before(peer);}
  if(group==='identity') {const provider=document.createElement('a');provider.href=id==='google'?'https://accounts.google.com/':'https://appleid.apple.com/';provider.target='_blank';provider.rel='noopener noreferrer';provider.textContent=`Open real ${title.split(' ')[0]} account page ↗`;actions.before(provider);actions.querySelector('[data-action="run"]').textContent='Preview simulated consent';provider.addEventListener('click',()=>set(id,'configuration','Provider page opened. Authentication is not verified by this lab.',{authenticated:false,providerNavigation:true}));}
  article.querySelector('[data-action="run"]').addEventListener('click',()=>run(id));article.querySelector('[data-action="stop"]').addEventListener('click',()=>stop(id));
  $(`#${group}-grid`).append(article);
}

async function media(kind,scope) {
  requireSecure();if(!navigator.mediaDevices?.getUserMedia)return unsupported('getUserMedia is not exposed by this browser.');
  const request=navigator.mediaDevices.getUserMedia(kind==='camera'?{video:{width:{ideal:640},height:{ideal:360}},audio:false}:{video:false,audio:true});
  request.then(stream=>{if(scope.closed)stream.getTracks().forEach(track=>track.stop());},()=>{});
  const stream=await request;stream.getTracks().forEach(track=>{liveTracks.add(track);scope.add(()=>{track.stop();liveTracks.delete(track);});});
  if(scope.closed)throw new Error('Permission request completed after the test stopped.');
  if(kind==='camera') {
    const video=cardFor(kind).querySelector('video');video.srcObject=stream;video.classList.add('visible');scope.add(()=>{video.pause();video.srcObject=null;video.classList.remove('visible');});await video.play();
  } else {
    const Audio=navigator.audioContext||window.AudioContext||window.webkitAudioContext;
    if(Audio){const audio=new Audio();scope.add(()=>audio.close().catch(()=>{}));await audio.resume();const analyser=audio.createAnalyser();analyser.fftSize=256;audio.createMediaStreamSource(stream).connect(analyser);const values=new Uint8Array(analyser.fftSize);const meter=cardFor(kind).querySelector('.mic-meter');meter.classList.add('visible');scope.add(()=>meter.classList.remove('visible'));const sample=()=>{if(scope.closed)return;analyser.getByteTimeDomainData(values);const rms=Math.sqrt(values.reduce((n,v)=>n+(v-128)**2,0)/values.length);const percent=Math.min(100,Math.round(rms*4));meter.firstElementChild.style.width=`${percent}%`;meter.setAttribute('aria-valuenow',String(percent));scope.timer(sample,100);};sample();}
  }
  scope.timer(()=>{scope.dispose();const previous=report.results[kind];set(kind,'pass',`${kind} acquisition verified; preview stopped automatically after 15s.`,{...previous.evidence,resourcesStopped:true});render();},15000);
  return {detail:`Live ${kind} track acquired. Capture ends after 15s or Stop.`,evidence:{trackCount:stream.getTracks().length,kinds:stream.getTracks().map(t=>t.kind),live:stream.getTracks().every(t=>t.readyState==='live'),automaticStopSeconds:15}};
}

const probes={
  camera:scope=>media('camera',scope),
  microphone:scope=>media('microphone',scope),
  async location() {
    requireSecure();if(!navigator.geolocation)return unsupported('Geolocation is not exposed by this browser.');
    const position=await new Promise((resolve,reject)=>navigator.geolocation.getCurrentPosition(resolve,error=>{const e=new Error(error.message);e.name=error.code===1?'NotAllowedError':error.code===2?'NotFoundError':'Error';reject(e);},{enableHighAccuracy:false,maximumAge:0,timeout:15000}));
    if(!Number.isFinite(position.coords.latitude)||!Number.isFinite(position.coords.longitude))throw new Error('Invalid coordinates returned');
    const provider=navigator.lumenCapabilities?.geolocationProvider||'browser-managed';return {detail:`Position returned (${provider}); accuracy ±${Math.round(position.coords.accuracy)} m. Coordinates omitted from export.`,evidence:{validLatitude:true,validLongitude:true,accuracyMeters:position.coords.accuracy,positionTimestamp:position.timestamp,provider,physicalGPSVerified:false}};
  },
  async wasm() {
    if(!window.WebAssembly)return unsupported('WebAssembly is not exposed.');
    const bytes=new Uint8Array([0,97,115,109,1,0,0,0,1,7,1,96,2,127,127,1,127,3,2,1,0,7,7,1,3,97,100,100,0,0,10,9,1,7,0,32,0,32,1,106,11]);
    const {instance}=await WebAssembly.instantiate(bytes);const actual=instance.exports.add(20,22);if(actual!==42)throw new Error(`Incorrect WASM result ${actual}`);
    return {detail:'Compiled and executed: add(20, 22) = 42.',evidence:{moduleBytes:bytes.length,expected:42,actual}};
  },
  async webgl(scope) {
    const canvas=canvasFor('webgl');const gl=canvas.getContext('webgl2',{preserveDrawingBuffer:true})||canvas.getContext('webgl',{preserveDrawingBuffer:true});
    if(!gl)return unsupported('No WebGL context could be created.');
    scope.add(()=>{gl.getExtension('WEBGL_lose_context')?.loseContext();canvas.classList.remove('visible');});
    const webgl2=gl instanceof (window.WebGL2RenderingContext||Function);
    const compile=(type,source)=>{const shader=gl.createShader(type);gl.shaderSource(shader,source);gl.compileShader(shader);if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(shader));scope.add(()=>gl.deleteShader(shader));return shader;};
    const vertex=compile(gl.VERTEX_SHADER,webgl2?'#version 300 es\nin vec2 p;out vec3 c;void main(){gl_Position=vec4(p,0.,1.);c=vec3(p*.5+.5,.65);}':'attribute vec2 p;varying vec3 c;void main(){gl_Position=vec4(p,0.,1.);c=vec3(p*.5+.5,.65);}');
    const fragment=compile(gl.FRAGMENT_SHADER,webgl2?'#version 300 es\nprecision mediump float;in vec3 c;out vec4 outColor;void main(){outColor=vec4(c,1.);}':'precision mediump float;varying vec3 c;void main(){gl_FragColor=vec4(c,1.);}');
    const program=gl.createProgram();gl.attachShader(program,vertex);gl.attachShader(program,fragment);gl.linkProgram(program);if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(program));scope.add(()=>gl.deleteProgram(program));gl.useProgram(program);
    const buffer=gl.createBuffer();scope.add(()=>gl.deleteBuffer(buffer));gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([0,.8,-.72,-.65,.72,-.65]),gl.STATIC_DRAW);const attr=gl.getAttribLocation(program,'p');gl.vertexAttribPointer(attr,2,gl.FLOAT,false,0,0);gl.enableVertexAttribArray(attr);gl.viewport(0,0,320,160);gl.clearColor(.035,.13,.18,1);gl.clear(gl.COLOR_BUFFER_BIT);gl.drawArrays(gl.TRIANGLES,0,3);
    const pixel=new Uint8Array(4);gl.readPixels(160,80,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);if(gl.getError()!==gl.NO_ERROR||pixel[0]<40||pixel[1]<40||pixel[2]<80)throw new Error(`GPU framebuffer readback invalid: ${pixel}`);
    return {detail:`${webgl2?'WebGL 2':'WebGL 1'} triangle rendered. Center pixel verified.`,evidence:{version:gl.getParameter(gl.VERSION),centerRGBA:[...pixel],drawCalls:1,animationLoop:false}};
  },
  async webgpu(scope) {
    requireSecure();if(!navigator.gpu)return unsupported('navigator.gpu is not exposed by this engine.');
    const adapter=await navigator.gpu.requestAdapter();if(!adapter)return unsupported('WebGPU exists, but no adapter is available.');
    const device=await adapter.requestDevice();scope.add(()=>device.destroy());device.lost.then(info=>{if(!scope.closed)log(`WEBGPU · device lost: ${info.reason}`);});
    const canvas=canvasFor('webgpu');const readbackMode=cardFor('webgpu').querySelector('select').value==='readback';const context=canvas.getContext(readbackMode?'2d':'webgpu');if(!context)throw new Error('Requested canvas context unavailable');
    const format=readbackMode?'rgba8unorm':navigator.gpu.getPreferredCanvasFormat();if(!readbackMode)context.configure({device,format,alphaMode:'opaque',usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});scope.add(()=>{if(!readbackMode)context.unconfigure();canvas.classList.remove('visible');});
    const code=`struct VOut{@builtin(position) position:vec4f,@location(0) color:vec3f};@vertex fn vs(@builtin(vertex_index) i:u32)->VOut{var p=array<vec2f,3>(vec2f(0.,.8),vec2f(-.72,-.65),vec2f(.72,-.65));var o:VOut;o.position=vec4f(p[i],0.,1.);o.color=vec3f(p[i]*vec2f(.5)+vec2f(.5),.65);return o;}@fragment fn fs(i:VOut)->@location(0) vec4f{return vec4f(i.color,1.);}`;
    const shader=device.createShaderModule({code});const compilation=await shader.getCompilationInfo();const compileErrors=compilation.messages.filter(m=>m.type==='error');if(compileErrors.length)throw new Error(compileErrors.map(m=>m.message).join('; '));device.pushErrorScope('validation');
    const pipeline=device.createRenderPipeline({layout:'auto',vertex:{module:shader,entryPoint:'vs'},fragment:{module:shader,entryPoint:'fs',targets:[{format}]},primitive:{topology:'triangle-list'}});
    const texture=device.createTexture({size:[320,160],format,usage:GPUTextureUsage.RENDER_ATTACHMENT|GPUTextureUsage.COPY_SRC});scope.add(()=>texture.destroy());
    const readback=device.createBuffer({size:readbackMode?320*160*4:256,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});scope.add(()=>readback.destroy());
    const canvasTexture=readbackMode?null:context.getCurrentTexture();const targets=readbackMode?[texture.createView()]:[canvasTexture.createView(),texture.createView()];
    const encoder=device.createCommandEncoder();for(const target of targets){const pass=encoder.beginRenderPass({colorAttachments:[{view:target,clearValue:{r:.035,g:.13,b:.18,a:1},loadOp:'clear',storeOp:'store'}]});pass.setPipeline(pipeline);pass.draw(3);pass.end();}
    encoder.copyTextureToBuffer({texture:readbackMode?texture:canvasTexture,origin:readbackMode?[0,0,0]:[160,80,0]},{buffer:readback,bytesPerRow:readbackMode?1280:256},readbackMode?[320,160,1]:[1,1,1]);device.queue.submit([encoder.finish()]);
    await readback.mapAsync(GPUMapMode.READ);const raw=new Uint8Array(readback.getMappedRange());const offset=readbackMode?(80*320+160)*4:0;const pixel=[...raw.slice(offset,offset+4)];if(readbackMode)context.putImageData(new ImageData(new Uint8ClampedArray(raw),320,160),0,0);readback.unmap();const validation=await device.popErrorScope();if(validation)throw new Error(validation.message);if(pixel[0]<40||pixel[1]<40||pixel[2]<40||pixel[3]!==255)throw new Error(`WebGPU readback invalid: ${pixel}`);
    return {detail:`WGSL triangle rendered. ${readbackMode?'Offscreen GPU pixels displayed; native surface not tested.':'Native canvas + GPU texture readback verified.'}`,evidence:{format,centerNativeChannels:pixel,drawCalls:targets.length,animationLoop:false,presentation:readbackMode?'GPU-rendered pixels copied to 2D canvas':'native WebGPU canvas',nativeCanvasVerified:!readbackMode,readbackSource:readbackMode?'offscreen texture':'native canvas texture',windowCompositorVerified:false}};
  },
  async iframe(scope) {
    const card=cardFor('iframe');card.querySelector('iframe')?.remove();const frame=document.createElement('iframe');frame.className='preview visible';frame.title='Embedded camera and location permission probe';frame.allow="camera 'self'; microphone 'self'; geolocation 'self'";frame.src='iframe-probe.html';scope.add(()=>{try{frame.contentWindow.LumeFrame?.stop();}catch{}frame.remove();});
    const ready=new Promise(resolve=>scope.listen(window,'message',event=>{if(event.origin!==location.origin||event.source!==frame.contentWindow)return;if(event.data?.type==='lume-frame-ready')resolve(event.data);if(event.data?.type==='lume-frame-result'){const current=report.results.iframe;const childPermissionTests={...(current.evidence.childPermissionTests||{}),[event.data.capability]:{state:event.data.state,detail:event.data.detail,evidence:event.data.evidence}};report.set('iframe',current.state,current.detail,{...current.evidence,childPermissionTests});log(`IFRAME · ${event.data.capability}: ${event.data.state} · ${event.data.detail}`);render();}}));card.querySelector('.card-actions').before(frame);
    const child=await ready;return {detail:'Iframe loaded and message origin verified. Child permissions need separate actions.',evidence:{originVerified:true,secureContext:child.secureContext,policy:child.policy,permissionTests:'awaiting child button actions'}};
  },
  async webrtc(scope) {
    if(navigator.lumenCapabilities?.webrtcNetworkTransport===false)return unsupported('This engine declares no network WebRTC transport. Experimental in-process loopback is not a transport test.');
    if(!window.RTCPeerConnection)return unsupported('RTCPeerConnection is not exposed.');
    const left=new RTCPeerConnection({iceServers:[]}),right=new RTCPeerConnection({iceServers:[]});scope.add(()=>left.close());scope.add(()=>right.close());
    const leftCandidates=[],rightCandidates=[];left.onicecandidate=event=>{if(event.candidate){if(right.remoteDescription)right.addIceCandidate(event.candidate).catch(()=>{});else rightCandidates.push(event.candidate);}};right.onicecandidate=event=>{if(event.candidate){if(left.remoteDescription)left.addIceCandidate(event.candidate).catch(()=>{});else leftCandidates.push(event.candidate);}};
    const channel=left.createDataChannel('lume-verified');scope.add(()=>channel.close());const sent=`lume-rtc-${crypto.randomUUID?.()||Date.now()}`;let received;
    const echo=new Promise((resolve,reject)=>{channel.onerror=()=>reject(new Error('WebRTC channel error'));channel.onopen=()=>channel.send(sent);channel.onmessage=event=>{received=event.data;try{validateEcho(sent,received);resolve();}catch(error){reject(error);}};right.ondatachannel=event=>{scope.add(()=>event.channel.close());event.channel.onmessage=message=>event.channel.send(message.data);};});
    await left.setLocalDescription(await left.createOffer());await right.setRemoteDescription(left.localDescription);await Promise.all(rightCandidates.map(c=>right.addIceCandidate(c)));await right.setLocalDescription(await right.createAnswer());await left.setRemoteDescription(right.localDescription);await Promise.all(leftCandidates.map(c=>left.addIceCandidate(c)));await echo;
    const state=left.connectionState;await new Promise(resolve=>scope.timer(resolve,200));const stats=[...(await left.getStats()).values()];const pair=stats.find(item=>item.type==='candidate-pair'&&item.state==='succeeded'&&item.nominated);const transport=stats.find(item=>item.type==='transport'&&item.dtlsState==='connected');const transportBytesSent=transport?.bytesSent||pair?.bytesSent||0;const transportBytesReceived=transport?.bytesReceived||pair?.bytesReceived||0;const networkStatsObserved=!!(pair&&transport&&transportBytesSent>0&&transportBytesReceived>0);scope.dispose();return {state:networkStatsObserved?'pass':'configuration',detail:networkStatsObserved?'Local API echo matched; ICE/DTLS reports connected. External interoperability untested.':'Local API echo matched, but ICE/DTLS transport is unverified.',evidence:{payloadMatched:true,bytes:new TextEncoder().encode(sent).length,connectionState:state,iceServers:[],coverage:'same-browser loopback only',networkStatsObserved,transportBytesSent,transportBytesReceived,dtlsState:transport?.dtlsState??null,candidatePairState:pair?.state??null,externalSTUNTURN:false,crossBrowserInteroperability:false}};
  },
  async websocket(scope) {
    if(!window.WebSocket)return unsupported('WebSocket is not exposed.');
    const url=new URL('/echo',location.href);url.protocol=location.protocol==='https:'?'wss:':'ws:';const socket=new WebSocket(url);scope.add(()=>socket.close());const sent=`lume-ws-${crypto.randomUUID?.()||Date.now()}`;
    await new Promise((resolve,reject)=>{socket.onopen=()=>socket.send(sent);socket.onerror=()=>reject(new Error('WebSocket connection failed. Start the lab server.'));socket.onmessage=event=>{try{validateEcho(sent,event.data);resolve();}catch(error){reject(error);}};});scope.dispose();
    return {detail:'Real WebSocket echo matched. Connection closed.',evidence:{url:url.origin+url.pathname,payloadMatched:true,bytes:sent.length}};
  },
  async webtransport(scope) {
    if(navigator.lumenCapabilities?.webTransport===false)return unsupported('This engine declares WebTransport unavailable.');
    requireSecure();if(!window.WebTransport)return unsupported('WebTransport is not exposed by this engine.');
    if(!config.webtransport?.available)return {state:'configuration',detail:'Start the optional HTTP/3 server to test WebTransport / QUIC.',evidence:{apiPresent:true,quicVerified:false}};
    const settings=config.webtransport;const options={serverCertificateHashes:[{algorithm:'sha-256',value:new Uint8Array(settings.certificateHash)}]};const transport=new WebTransport(settings.url,options);transport.closed.catch(()=>{});scope.add(()=>transport.close());await transport.ready;
    const writer=transport.datagrams.writable.getWriter(),reader=transport.datagrams.readable.getReader();scope.add(()=>{writer.releaseLock();reader.cancel().catch(()=>{});reader.releaseLock();});const sent=`lume-h3-${crypto.randomUUID?.()||Date.now()}`;await writer.write(new TextEncoder().encode(sent));const {value,done}=await reader.read();if(done)throw new Error('WebTransport datagram stream closed without echo');validateEcho(sent,new TextDecoder().decode(value));
    scope.dispose();return {detail:'Datagram echo matched over the HTTP/3-only QUIC endpoint.',evidence:{payloadMatched:true,endpoint:settings.url,serverTransport:'HTTP/3 over QUIC only',quicVerified:true,bytes:value.byteLength}};
  },
  async google(scope){return simulateIdentity('Google',scope);},
  async apple(scope){return simulateIdentity('Apple',scope);}
};

async function simulateIdentity(provider,scope) {
  const dialog=$('#oauth-dialog');$('#mock-provider').textContent=`${provider} consent preview`;dialog.showModal();scope.add(()=>{if(dialog.open)dialog.close('cancel');});
  const choice=await new Promise(resolve=>scope.listen(dialog,'close',()=>resolve(dialog.returnValue),{once:true}));scope.dispose();return {state:'configuration',detail:`${provider} UI simulation ${choice==='accepted'?'completed':'cancelled'}. Real authentication remains unverified.`,evidence:{simulation:true,authenticated:false,providerContacted:false,choice}};
}

async function run(id) {
  if(!probes[id])throw new Error(`Unknown probe ${id}`);stop(id);const generation=generations.get(id);const scope=new ResourceScope();scopes.set(id,scope);set(id,'running','Testing actual behavior…');
  try {const outcome=await within(probes[id](scope),['camera','microphone','google','apple'].includes(id)?60000:20000,scope);if(generations.get(id)!==generation)return report.results[id];set(id,outcome.state||'pass',outcome.detail,outcome.evidence);if(['wasm','location'].includes(id)||outcome.state==='unsupported'||outcome.state==='configuration')scope.dispose();}
  catch(error){scope.dispose();if(generations.get(id)===generation)set(id,errorState(error),`${error.name||'Error'}: ${error.message}`,{errorName:error.name||'Error'});}
  render();return structuredClone(report.results[id]);
}
async function runAutomatic() {if(autoRunning)return snapshot();autoRunning=true;const generation=autoGeneration;$('#run-auto').disabled=true;try{for(const id of ['wasm','webgl','webgpu','iframe','webrtc','websocket','webtransport']){if(document.hidden||generation!==autoGeneration)break;await run(id);}}finally{autoRunning=false;$('#run-auto').disabled=false;}return snapshot();}

$('#run-auto').addEventListener('click',runAutomatic);$('#stop-all').addEventListener('click',()=>stopAll());$('#export').addEventListener('click',()=>{const blob=new Blob([JSON.stringify(snapshot(),null,2)],{type:'application/json'});const url=URL.createObjectURL(blob);const anchor=document.createElement('a');anchor.href=url;anchor.download='GPT6-AstraPro-subagents-Web-Lume-capabilities.json';anchor.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
document.addEventListener('visibilitychange',()=>{if(document.hidden)stopAll('Page hidden; all resources released');});window.addEventListener('pagehide',()=>stopAll('Page unloaded'));
$('#context-badge').textContent=isSecureContext?'SECURE CONTEXT · LOCAL EVIDENCE':'INSECURE CONTEXT · PERMISSIONS LIMITED';
try{config=await(await fetch('/api/config',{cache:'no-store'})).json();}catch{}
window.LumeLab={snapshot,run,runAutomatic,stopAll};render();window.dispatchEvent(new Event('lume-lab-ready'));
