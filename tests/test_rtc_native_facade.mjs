import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

// This is a native-boundary fixture, not network or cryptographic evidence.
// A separate browser process must prove real ICE/DTLS/SCTP interoperability.
const here = path.dirname(fileURLToPath(import.meta.url));
const sourcePath = process.env.LUME_NATIVE_RTC_SOURCE || path.resolve(here, '../src/js/prelude/07s-rtc-native.js');
const valuePath = process.env.LUME_RTC_VALUES_SOURCE || path.resolve(here, '../src/js/prelude/07r-rtc.js');
const wholeValues = fs.readFileSync(valuePath, 'utf8');
const valueTypes = wholeValues.slice(0, wholeValues.indexOf('class MediaStreamTrack extends'));
const source = fs.readFileSync(sourcePath, 'utf8');
const offer = 'v=0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:data\r\na=ice-options:trickle\r\n';

function harness({available = true, deferGeneration = false} = {}) {
    const states = new Map(), nativeChannels = new Map(), calls = [], timers = new Set();
    let nextPeer = 1, nextChannel = 20;
    const deliver = (id, event) => states.get(id).callback(event);
    const later = (id, event) => queueMicrotask(() => deliver(id, event));
    const N = {
        rtcNativeAvailable: () => available,
        rtcCreate(config, callback) { const id = nextPeer++; states.set(id, {callback, config: JSON.parse(config), local: null, pending: [], closed: false}); calls.push(['create', id]); return id; },
        rtcGenerateDescription(id, type, requestId) { calls.push(['generate', id, type]); const event = {type: 'description', requestId, descriptionType: type, sdp: offer}; if (deferGeneration) states.get(id).pending.push(event); else later(id, event); },
        rtcSetLocalDescription(id, type, sdp, requestId) { calls.push(['setLocal', id, type]); states.get(id).local = {type, sdp}; later(id, {type: 'operation', requestId}); },
        rtcSetRemoteDescription(id, type, sdp, requestId) { calls.push(['setRemote', id, type]); states.get(id).remote = {type, sdp}; later(id, {type: 'operation', requestId}); },
        rtcAddCandidate(id, candidate, mid, requestId) { calls.push(['candidate', id, candidate, mid]); later(id, {type: 'operation', requestId}); },
        rtcGetLocalDescription(id) { return states.get(id).local; },
        rtcCreateChannel(id, options) { const nativeId = nextChannel++, info = {...JSON.parse(options), id: 7, readyState: 'connecting', bufferedAmount: 0, maxMessageSize: 64}; nativeChannels.set(nativeId, {peerId: id, info, sent: [], messagesSent: 0, bytesSent: 0, messagesReceived: 0, bytesReceived: 0}); calls.push(['channel', id, nativeId]); return nativeId; },
        rtcChannelInfo(id) { return {...nativeChannels.get(id).info}; },
        rtcSend(id, data) { const state = nativeChannels.get(id); state.sent.push(data); state.messagesSent++; state.bytesSent += typeof data === 'string' ? new TextEncoder().encode(data).byteLength : data.byteLength; },
        rtcSetBufferedAmountLowThreshold(id, threshold) { nativeChannels.get(id).threshold = threshold; },
        rtcCloseChannel(id) { calls.push(['closeChannel', id]); const state = nativeChannels.get(id); state.info.readyState = 'closed'; later(state.peerId, {type: 'channel-close', channelId: id}); },
        rtcClosePeer(id) { states.get(id).closed = true; calls.push(['closePeer', id]); },
        rtcStats(id) { return {connectionState: 'new', iceConnectionState: 'new', iceGatheringState: 'new', channels: [...nativeChannels].filter(([, item]) => item.peerId === id).map(([channelId, item]) => ({channelId, ...item.info, state: item.info.readyState, messagesSent: item.messagesSent, bytesSent: item.bytesSent, messagesReceived: item.messagesReceived, bytesReceived: item.bytesReceived})), selectedCandidatePair: null}; }
    };
    const context = vm.createContext({N, Event, EventTarget, DOMException, Blob, TextEncoder, ArrayBuffer, Uint8Array, performance,
        setTimeout(callback, delay) { const handle = setTimeout(() => { timers.delete(handle); callback(); }, delay); timers.add(handle); return handle; },
        clearTimeout(handle) { timers.delete(handle); clearTimeout(handle); },
        def(object, name, value) { Object.defineProperty(object, name, {value, writable: true, configurable: true}); },
        dispatch(target, event) { target.dispatchEvent(event); target._on?.[event.type]?.call(target, event); }
    });
    vm.runInContext(valueTypes + '\n' + source + '\nglobalThis.api = nativeRTCGlobals;', context, {filename: 'native-rtc-fixture.js'});
    return {api: context.api, N, states, nativeChannels, calls, timers, deliver,
        open(id = 20) { const item = nativeChannels.get(id); item.info.readyState = 'open'; deliver(item.peerId, {type: 'channel-open', channelId: id}); },
        cleanup() { for (const timer of timers) clearTimeout(timer); timers.clear(); }
    };
}
const tick = () => new Promise(resolve => setImmediate(resolve));
const rejectedName = name => error => error?.name === name;

test('no native transport means no native constructors; internal dispatcher is inaccessible', () => {
    assert.equal(harness({available: false}).api, null);
    const h = harness(), pc = new h.api.RTCPeerConnection();
    assert.equal(pc._request, undefined);
    assert.equal(pc._enqueue, undefined);
    pc.close(); h.cleanup();
});

test('createOffer stages SDP; setLocal starts negotiation and gathered SDP stays current', async () => {
    const h = harness(), pc = new h.api.RTCPeerConnection(), events = [];
    pc.onsignalingstatechange = () => events.push([pc.signalingState, pc.localDescription?.type]);
    const generated = await pc.createOffer();
    assert.equal(generated.type, 'offer'); assert.equal(pc.localDescription, null); assert.equal(pc.signalingState, 'stable');
    assert.equal(h.calls.some(call => call[0] === 'setLocal'), false);
    await pc.setLocalDescription(generated);
    assert.equal(pc.signalingState, 'have-local-offer'); assert.deepEqual(events, [['have-local-offer', 'offer']]);
    h.states.get(1).local.sdp += 'a=candidate:fixture 1 UDP 1 127.0.0.1 1234 typ host\r\n';
    let lastCandidate = 'unset'; pc.onicecandidate = event => { lastCandidate = event.candidate; };
    h.deliver(1, {type: 'state', iceGatheringState: 'complete'}); h.deliver(1, {type: 'candidate', candidate: null});
    assert.equal(pc.iceGatheringState, 'complete'); assert.match(pc.localDescription.sdp, /candidate:fixture/); assert.equal(lastCandidate, null);
    assert.equal(pc.connectionState, 'new');
    pc.close(); h.cleanup();
});

test('answer flow uses acknowledged descriptions and rejects munged local SDP', async () => {
    const h = harness(), pc = new h.api.RTCPeerConnection();
    await assert.rejects(pc.createAnswer(), rejectedName('InvalidStateError'));
    await pc.setRemoteDescription({type: 'offer', sdp: offer});
    assert.equal(pc.signalingState, 'have-remote-offer'); assert.equal(pc.canTrickleIceCandidates, true);
    const answer = await pc.createAnswer();
    await assert.rejects(pc.setLocalDescription({...answer, sdp: answer.sdp + 'a=changed\r\n'}), rejectedName('InvalidModificationError'));
    assert.equal(h.calls.some(call => call[0] === 'setLocal'), false);
    await pc.setLocalDescription(answer);
    assert.equal(pc.signalingState, 'stable'); assert.equal(pc.currentRemoteDescription.type, 'offer'); assert.equal(pc.currentLocalDescription.type, 'answer');
    assert.equal(pc.pendingLocalDescription, null); assert.equal(pc.pendingRemoteDescription, null);
    pc.close(); h.cleanup();
});

test('close cancels pending/queued operations and drops all late native events', async () => {
    const h = harness({deferGeneration: true}), pc = new h.api.RTCPeerConnection();
    let stateEvents = 0; pc.onconnectionstatechange = () => stateEvents++;
    const first = pc.createOffer(), second = pc.createOffer();
    const firstCheck = assert.rejects(first, rejectedName('InvalidStateError'));
    const secondCheck = assert.rejects(second, rejectedName('InvalidStateError'));
    await tick(); const pending = h.states.get(1).pending[0];
    pc.close(); pc.close(); h.deliver(1, pending); h.deliver(1, {type: 'state', connectionState: 'connected'});
    await Promise.all([firstCheck, secondCheck]);
    assert.equal(pc.connectionState, 'closed'); assert.equal(pc.localDescription, null); assert.equal(stateEvents, 0);
    assert.equal(h.calls.filter(call => call[0] === 'closePeer').length, 1); assert.equal(h.timers.size, 0);
    h.cleanup();
});

test('fatal native shutdown rejects outstanding work and releases channels without reusing a dead handle', async () => {
    const h = harness({deferGeneration: true}), pc = new h.api.RTCPeerConnection(), dc = pc.createDataChannel('fatal-fixture');
    h.open();
    const events = [];
    dc.onerror = event => events.push(['error', event.error.name, dc.readyState]);
    dc.onclose = () => events.push(['close', dc.readyState]);
    pc.onconnectionstatechange = () => events.push(['peer', pc.connectionState]);
    const pending = pc.createOffer(), queued = pc.createOffer();
    const pendingCheck = assert.rejects(pending, rejectedName('OperationError'));
    const queuedCheck = assert.rejects(queued, rejectedName('InvalidStateError'));
    await tick();
    let queuedChannel, lateOpens = 0;
    h.nativeChannels.set(44, {peerId: 1, info: {label: 'queued-native-open', protocol: '', id: 3, readyState: 'open', ordered: true, maxMessageSize: 100}, sent: []});
    pc.ondatachannel = event => { queuedChannel = event.channel; queuedChannel.onopen = () => lateOpens++; };
    // Incoming already-open native channels always queue their JavaScript open
    // event. A second locally created channel need not queue negotiation.
    h.deliver(1, {type: 'datachannel', channelId: 44});
    assert.ok(h.timers.size > 0, 'a queued peer task must exist before fatal shutdown');
    // Fatal callbacks are delivered after native closes its peer. JavaScript
    // must not re-enter a deleted native handle, even through a later close().
    const lateDescription = h.states.get(1).pending[0];
    h.deliver(1, {type: 'error', fatal: true, name: 'OperationError', message: 'native fixture queue limit'});
    assert.equal(pc.signalingState, 'closed');
    assert.equal(pc.iceConnectionState, 'closed');
    assert.equal(pc.connectionState, 'closed');
    assert.equal(dc.readyState, 'closed');
    assert.equal(queuedChannel.readyState, 'closed');
    assert.equal(h.timers.size, 0);
    await Promise.all([pendingCheck, queuedCheck]);
    await tick(); assert.equal(lateOpens, 0);
    const callsAfterFailure = h.calls.length;
    h.deliver(1, lateDescription);
    h.deliver(1, {type: 'state', connectionState: 'connected'});
    h.deliver(1, {type: 'channel-message', channelId: 20, data: 'late fixture'});
    pc.close(); dc.close(); void dc.bufferedAmount; void pc.localDescription;
    await assert.rejects(pc.createOffer(), rejectedName('InvalidStateError'));
    await assert.rejects(pc.getStats(), rejectedName('InvalidStateError'));
    assert.throws(() => dc.send('late fixture'), rejectedName('InvalidStateError'));
    assert.equal(h.calls.length, callsAfterFailure);
    assert.equal(h.calls.some(call => call[0] === 'closePeer' || call[0] === 'closeChannel'), false);
    assert.deepEqual(events, [['error', 'OperationError', 'closed'], ['close', 'closed'], ['peer', 'closed']]);
    h.cleanup();
});

test('native state is the only source of ICE and connection success', () => {
    const h = harness(), pc = new h.api.RTCPeerConnection();
    assert.equal(pc.iceConnectionState, 'new'); assert.equal(pc.connectionState, 'new');
    h.deliver(1, {type: 'state', connectionState: 'connected', iceConnectionState: 'completed'});
    assert.equal(pc.connectionState, 'connected'); assert.equal(pc.iceConnectionState, 'completed');
    h.deliver(1, {type: 'state', connectionState: 'fabricated-unknown'}); assert.equal(pc.connectionState, 'connected');
    pc.close(); h.cleanup();
});

test('data channel validation occurs before native allocation', () => {
    const h = harness(), pc = new h.api.RTCPeerConnection();
    for (const options of [{maxPacketLifeTime: 10, maxRetransmits: 1}, {negotiated: true}, {id: 65535}, {maxRetransmits: -1}]) assert.throws(() => pc.createDataChannel('fixture', options), rejectedName('TypeError'));
    assert.throws(() => pc.createDataChannel('é'.repeat(32768)), rejectedName('TypeError'));
    assert.throws(() => pc.createDataChannel('fixture', {maxPacketLifeTime: 0}), rejectedName('NotSupportedError'));
    assert.equal(h.nativeChannels.size, 0);
    const dc = pc.createDataChannel('fixture', {ordered: false, negotiated: true, id: 7, maxRetransmits: 0});
    assert.equal(dc.ordered, false); assert.equal(dc.maxRetransmits, 0); assert.equal(dc.negotiated, true);
    pc.close(); assert.equal(h.timers.size, 0); h.cleanup();
});

test('send copies the selected binary bytes and never emits messages locally', () => {
    const h = harness(), pc = new h.api.RTCPeerConnection(), dc = pc.createDataChannel('fixture');
    assert.throws(() => dc.send('early'), rejectedName('InvalidStateError'));
    let messages = 0; dc.onmessage = () => messages++;
    h.open(); const bytes = new Uint8Array([1, 2, 3, 4]); dc.send(bytes.subarray(1, 3)); bytes.fill(9);
    assert.deepEqual([...new Uint8Array(h.nativeChannels.get(20).sent[0])], [2, 3]); assert.equal(messages, 0);
    assert.throws(() => dc.send(new Blob(['fixture'])), rejectedName('NotSupportedError'));
    assert.throws(() => dc.send('text\0not-truncated'), rejectedName('NotSupportedError'));
    assert.throws(() => dc.send(new Uint8Array(65)), rejectedName('TypeError'));
    dc.close(); assert.throws(() => dc.send('closed'), rejectedName('InvalidStateError'));
    pc.close(); h.cleanup();
});

test('incoming channels deliver datachannel before open and preserve copied binary messages', async () => {
    const h = harness(), pc = new h.api.RTCPeerConnection(), order = [];
    h.nativeChannels.set(44, {peerId: 1, info: {label: 'remote', protocol: '', id: 3, readyState: 'open', ordered: true, maxMessageSize: 100}, sent: []});
    let dc, received;
    pc.ondatachannel = event => { dc = event.channel; order.push('datachannel'); dc.onopen = () => order.push('open'); dc.onmessage = event => { received = event.data; }; };
    h.deliver(1, {type: 'datachannel', channelId: 44});
    assert.deepEqual(order, ['datachannel']);
    await new Promise(resolve => setTimeout(resolve, 5)); assert.deepEqual(order, ['datachannel', 'open']);
    const bytes = new Uint8Array([5, 6]); h.deliver(1, {type: 'channel-message', channelId: 44, data: bytes.buffer}); bytes.fill(0);
    assert.deepEqual([...new Uint8Array(received)], [5, 6]);
    pc.close(); h.cleanup();
});

test('native buffered callback is event-driven and statistics contain only measured payload counters', async () => {
    const h = harness(), pc = new h.api.RTCPeerConnection(), dc = pc.createDataChannel('fixture');
    h.open(); dc.bufferedAmountLowThreshold = 12; let lows = 0; dc.onbufferedamountlow = () => lows++;
    assert.equal(h.nativeChannels.get(20).threshold, 12);
    h.deliver(1, {type: 'channel-bufferedamountlow', channelId: 20}); assert.equal(lows, 1);
    dc.send('abc'); const stats = await pc.getStats(), items = [...stats.values()];
    assert.equal(items.length, 1); assert.equal(items[0].type, 'data-channel'); assert.equal(items[0].bytesSent, 3); assert.equal(items[0].bytesReceived, 0);
    assert.equal(items.some(item => ['transport', 'certificate', 'candidate-pair'].includes(item.type)), false);
    pc.close(); h.cleanup();
});

test('RTP, ICE restart and unsupported configuration changes fail explicitly', async () => {
    const h = harness(), pc = new h.api.RTCPeerConnection();
    assert.throws(() => pc.addTrack({}), rejectedName('NotSupportedError'));
    assert.throws(() => pc.addTransceiver('audio'), rejectedName('NotSupportedError'));
    assert.throws(() => pc.restartIce(), rejectedName('NotSupportedError'));
    await assert.rejects(pc.createOffer({iceRestart: true}), rejectedName('NotSupportedError'));
    await assert.rejects(pc.setRemoteDescription({type: 'offer', sdp: 'v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\n'}), rejectedName('NotSupportedError'));
    assert.throws(() => pc.setConfiguration({iceTransportPolicy: 'relay'}), rejectedName('NotSupportedError'));
    assert.equal(h.calls.some(call => call[0] === 'setRemote'), false);
    pc.close(); h.cleanup();
});

test('candidate validation is checked against the applied remote description', async () => {
    const h = harness(), pc = new h.api.RTCPeerConnection();
    await assert.rejects(pc.addIceCandidate(null), rejectedName('InvalidStateError'));
    await pc.setRemoteDescription({type: 'offer', sdp: offer});
    await assert.rejects(pc.addIceCandidate({candidate: 'candidate:fixture'}), rejectedName('TypeError'));
    await assert.rejects(pc.addIceCandidate({candidate: 'candidate:fixture', sdpMid: 'missing'}), rejectedName('OperationError'));
    await pc.addIceCandidate({candidate: 'candidate:fixture', sdpMLineIndex: 0});
    assert.equal(h.calls.at(-1)[3], 'data');
    await pc.addIceCandidate(null); assert.equal(h.calls.at(-1)[2], '');
    pc.close(); h.cleanup();
});


test('selected candidate metadata is reported without invented nomination or transport totals', async () => {
    const h = harness(), pc = new h.api.RTCPeerConnection();
    h.N.rtcStats = () => ({channels: [], iceConnectionState: 'connected', selectedCandidatePair: {localCandidate: 'candidate:fixtureL 1 UDP 100 127.0.0.1 1234 typ host', remoteCandidate: 'candidate:fixtureR 1 UDP 100 127.0.0.1 5678 typ host'}});
    const entries = [...(await pc.getStats()).values()], pair = entries.find(item => item.type === 'candidate-pair');
    assert.equal(pair.state, 'succeeded'); assert.equal('nominated' in pair, false); assert.equal('bytesSent' in pair, false);
    assert.equal(entries.some(item => item.type === 'transport'), false);
    assert.equal(entries.find(item => item.type === 'local-candidate').port, 1234);
    pc.close(); h.cleanup();
});
