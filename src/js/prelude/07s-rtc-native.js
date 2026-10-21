// Real WebRTC data channels. The earlier 07r value/capture types are reused;
// its in-process peers, fabricated transports and counters are never used here.
const nativeRTCGlobals = (() => {
    if (typeof N.rtcNativeAvailable !== 'function' || !N.rtcNativeAvailable()) return null;
    const peers = new WeakMap(), channels = new WeakMap(), reports = new WeakMap(), token = Symbol('native RTC');
    const utf8 = new TextEncoder();
    const error = (name, message) => new DOMException(message || name, name);
    const unsupported = what => error('NotSupportedError', what + ' is not implemented by the native data-channel backend.');
    const nativeError = cause => error(['InvalidStateError', 'InvalidModificationError', 'NotSupportedError', 'OperationError', 'SyntaxError'].includes(cause?.name) ? cause.name : 'OperationError', 'The native WebRTC operation failed.');
    const peer = target => { const state = peers.get(target); if (!state) throw new TypeError('Illegal invocation'); return state; };
    const channel = target => { const state = channels.get(target); if (!state) throw new TypeError('Illegal invocation'); return state; };
    const integer = (value, max, name) => {
        const number = Number(value);
        if (!Number.isFinite(number) || Math.trunc(number) < 0 || Math.trunc(number) > max) throw new TypeError(name + ' is outside its supported range.');
        return Math.trunc(number);
    };
    const emit = (target, name, properties = {}) => {
        const event = new Event(name);
        for (const key of Object.keys(properties)) Object.defineProperty(event, key, {value: properties[key], enumerable: true});
        dispatch(target, event, true);
    };
    const handlers = (Class, names) => rtcOn(Class, names);
    const readonly = (Class, names, lookup) => {
        for (const name of names) Object.defineProperty(Class.prototype, name, {get() { return lookup(this)[name]; }, configurable: true, enumerable: true});
    };
    const queuePeerTask = (target, work) => {
        const state = peer(target);
        if (state.closed) return;
        const timer = setTimeout(() => { state.tasks.delete(timer); if (!state.closed) work(); }, 0);
        state.tasks.add(timer);
    };
    const description = input => input == null ? null : new RTCSessionDescription({type: input.type, sdp: input.sdp});
    const requireOpenPeer = state => { if (state.closed) throw error('InvalidStateError', 'The peer connection is closed.'); };
    const stateChange = (target, key, value, event) => {
        const state = peer(target);
        if (state[key] === value) return;
        state[key] = value;
        emit(target, event);
    };
    const refreshLocal = target => {
        const state = peer(target);
        if (state.closed) return;
        let current;
        try { current = N.rtcGetLocalDescription(state.id); } catch { return; }
        if (!current || !['offer', 'answer'].includes(current.type) || typeof current.sdp !== 'string') return;
        const key = state.pendingLocalDescription ? 'pendingLocalDescription' : 'currentLocalDescription';
        if (!state[key] || state[key].type !== current.type) return;
        if (state[key].sdp !== current.sdp) state[key] = description(current);
    };
    const configuration = input => {
        input = input ?? {};
        if (typeof input !== 'object') throw new TypeError('RTCConfiguration must be a dictionary.');
        const result = {iceServers: [], iceTransportPolicy: input.iceTransportPolicy === undefined ? 'all' : String(input.iceTransportPolicy), bundlePolicy: input.bundlePolicy === undefined ? 'balanced' : String(input.bundlePolicy), rtcpMuxPolicy: input.rtcpMuxPolicy === undefined ? 'require' : String(input.rtcpMuxPolicy), iceCandidatePoolSize: integer(input.iceCandidatePoolSize ?? 0, 255, 'iceCandidatePoolSize'), certificates: []};
        if (!['all', 'relay'].includes(result.iceTransportPolicy) || !['balanced', 'max-compat', 'max-bundle'].includes(result.bundlePolicy) || result.rtcpMuxPolicy !== 'require') throw new TypeError('Invalid RTC configuration enum.');
        if (result.iceCandidatePoolSize) throw unsupported('ICE candidate prefetch');
        if (input.certificates !== undefined && Array.from(input.certificates).length) throw unsupported('Certificate import');
        if (input.iceServers !== undefined) {
            for (const item of Array.from(input.iceServers)) {
                if (!item || item.urls === undefined) throw new TypeError('ICE server urls are required.');
                const urls = typeof item.urls === 'string' ? [item.urls] : Array.from(item.urls, String);
                if (!urls.length) throw error('SyntaxError', 'An ICE server URL is required.');
                for (const url of urls) {
                    if (!/^(stun|stuns|turn|turns):[^\s/@#]+(?:\?transport=(?:udp|tcp))?$/.test(url) || (/^stuns?:/.test(url) && url.includes('?'))) throw error('SyntaxError', 'Invalid ICE server URL.');
                    if (/^turns?:/.test(url) && (item.username === undefined || item.credential === undefined)) throw error('InvalidAccessError', 'TURN requires username and credential.');
                }
                const server = {urls};
                if (item.username !== undefined) server.username = String(item.username);
                if (item.credential !== undefined) server.credential = String(item.credential);
                result.iceServers.push(server);
            }
        }
        return result;
    };
    const optionsForChannel = (label, options) => {
        options = options ?? {};
        const result = {label: rtcUSV(label), ordered: options.ordered === undefined ? true : !!options.ordered, protocol: rtcUSV(options.protocol ?? ''), negotiated: !!options.negotiated, id: options.id == null ? null : integer(options.id, 65534, 'id'), maxPacketLifeTime: options.maxPacketLifeTime == null ? null : integer(options.maxPacketLifeTime, 65535, 'maxPacketLifeTime'), maxRetransmits: options.maxRetransmits == null ? null : integer(options.maxRetransmits, 65535, 'maxRetransmits')};
        if (utf8.encode(result.label).byteLength > 65535 || utf8.encode(result.protocol).byteLength > 65535) throw new TypeError('Data channel label or protocol exceeds 65535 UTF-8 bytes.');
        if (result.maxPacketLifeTime !== null && result.maxRetransmits !== null) throw new TypeError('Both reliability limits cannot be set.');
        if (result.maxPacketLifeTime === 0) throw unsupported('Zero-lifetime unreliable channels');
        if (result.negotiated && result.id === null) throw new TypeError('A negotiated data channel requires an id.');
        return result;
    };
    const channelInfo = target => {
        const state = channel(target);
        if (!peer(state.owner).closed && state.readyState !== 'closed') {
            try {
                const info = N.rtcChannelInfo(state.nativeId);
                for (const key of ['id', 'bufferedAmount', 'maxMessageSize']) if (info?.[key] !== undefined) state[key] = info[key];
            } catch {}
        }
        return state;
    };

    class RTCDataChannel extends EventTarget {
        constructor(secret, owner, nativeId, info) {
            if (secret !== token) throw new TypeError('Illegal constructor');
            super();
            channels.set(this, {owner, nativeId, label: String(info.label ?? ''), protocol: String(info.protocol ?? ''), ordered: info.ordered !== false, negotiated: !!info.negotiated, id: info.id ?? null, maxPacketLifeTime: info.maxPacketLifeTime ?? null, maxRetransmits: info.maxRetransmits ?? null, readyState: 'connecting', binaryType: 'arraybuffer', bufferedAmount: Number(info.bufferedAmount) || 0, bufferedAmountLowThreshold: 0, maxMessageSize: Number(info.maxMessageSize) || 0});
        }
        get binaryType() { return channel(this).binaryType; }
        set binaryType(value) {
            value = String(value);
            if (!['blob', 'arraybuffer'].includes(value)) return;
            channel(this).binaryType = value;
        }
        get bufferedAmount() { return channelInfo(this).bufferedAmount; }
        get bufferedAmountLowThreshold() { return channel(this).bufferedAmountLowThreshold; }
        set bufferedAmountLowThreshold(value) {
            const state = channel(this), amount = integer(value, 4294967295, 'bufferedAmountLowThreshold');
            if (!peer(state.owner).closed && state.readyState !== 'closed') N.rtcSetBufferedAmountLowThreshold(state.nativeId, amount);
            state.bufferedAmountLowThreshold = amount;
        }
        get id() { return channelInfo(this).id; }
        send(data) {
            const state = channelInfo(this);
            if (state.readyState !== 'open' || peer(state.owner).closed) throw error('InvalidStateError', 'The data channel is not open.');
            let payload, size, binary = false;
            if (typeof data === 'string') { payload = rtcUSV(data); if (payload.includes('\0')) throw unsupported('Text messages containing NUL'); size = utf8.encode(payload).byteLength; }
            else if (data instanceof ArrayBuffer) { size = data.byteLength; binary = true; }
            else if (ArrayBuffer.isView(data)) { if (!(data.buffer instanceof ArrayBuffer)) throw new TypeError('Shared buffers are not supported.'); size = data.byteLength; binary = true; }
            else if (typeof Blob !== 'undefined' && data instanceof Blob) throw unsupported('Blob data channel sends');
            else throw new TypeError('Data must be a string, ArrayBuffer or ArrayBuffer view.');
            if (state.maxMessageSize > 0 && size > state.maxMessageSize) throw new TypeError('The message exceeds the negotiated maximum size.');
            if (binary) payload = data instanceof ArrayBuffer ? data.slice(0) : data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
            try { N.rtcSend(state.nativeId, payload); } catch (cause) { throw nativeError(cause); }
        }
        close() {
            const state = channel(this);
            if (state.readyState === 'closing' || state.readyState === 'closed') return;
            state.readyState = 'closing';
            emit(this, 'closing');
            try { N.rtcCloseChannel(state.nativeId); } catch {}
        }
    }
    readonly(RTCDataChannel, ['label', 'ordered', 'maxPacketLifeTime', 'maxRetransmits', 'protocol', 'negotiated', 'readyState'], channel);
    handlers(RTCDataChannel, ['open', 'closing', 'close', 'message', 'error', 'bufferedamountlow']);

    class RTCDataChannelEvent extends Event {
        constructor(type, init) {
            if (!init || !channels.has(init.channel)) throw new TypeError('A native RTCDataChannel is required.');
            super(type, init);
            Object.defineProperty(this, 'channel', {value: init.channel, enumerable: true});
        }
    }
    class RTCStatsReport {
        constructor(secret, entries) {
            if (secret !== token) throw new TypeError('Illegal constructor');
            reports.set(this, new Map(entries));
        }
        get size() { return reports.get(this).size; }
        get(key) { return reports.get(this).get(key); }
        has(key) { return reports.get(this).has(key); }
        keys() { return reports.get(this).keys(); }
        values() { return reports.get(this).values(); }
        entries() { return reports.get(this).entries(); }
        forEach(callback, thisArg) { reports.get(this).forEach((value, key) => callback.call(thisArg, value, key, this)); }
        [Symbol.iterator]() { return reports.get(this).entries(); }
    }

    function terminatePeer(target, failure = null) {
        const state = peer(target);
        if (state.closed) return;
        const changed = ['signalingState', 'iceConnectionState', 'connectionState'].map(key => state[key] !== 'closed');
        state.closed = true;
        state.signalingState = 'closed'; state.iceConnectionState = 'closed'; state.connectionState = 'closed';
        const rejection = failure || error('InvalidStateError', 'The peer connection was closed.');
        for (const pending of state.requests.values()) pending.reject(rejection);
        state.requests.clear();
        for (const timer of state.tasks) clearTimeout(timer);
        state.tasks.clear();
        for (const dataChannel of state.channels.values()) {
            const item = channel(dataChannel);
            if (item.readyState !== 'closed') {
                item.readyState = 'closed';
                if (failure) emit(dataChannel, 'error', {error: failure});
                emit(dataChannel, 'close');
            }
        }
        if (failure) {
            // fatal:true is delivered only after native closes this handle.
            // Notify consumers, but never re-enter or retry the dead handle.
            for (const [index, event] of ['signalingstatechange', 'iceconnectionstatechange', 'connectionstatechange'].entries()) if (changed[index]) emit(target, event);
        } else {
            try { N.rtcClosePeer(state.id); } catch {}
        }
    }

    function nativeEvent(target, event) {
        const state = peer(target);
        if (state.closed || !event || typeof event.type !== 'string') return;
        if (event.type === 'error' && event.fatal === true) {
            terminatePeer(target, nativeError(event));
            return;
        }
        if (event.type === 'description' || event.type === 'operation' || (event.type === 'error' && event.requestId !== undefined)) {
            const pending = state.requests.get(event.requestId);
            if (!pending) return;
            state.requests.delete(event.requestId);
            if (event.type === 'error') pending.reject(nativeError(event));
            else if (event.type === 'description') {
                if (!['offer', 'answer'].includes(event.descriptionType) || typeof event.sdp !== 'string') pending.reject(error('OperationError', 'Invalid native description.'));
                else pending.resolve({type: event.descriptionType, sdp: event.sdp});
            } else pending.resolve();
            return;
        }
        if (event.type === 'state') {
            refreshLocal(target);
            for (const [key, allowed, eventName] of [
                ['iceGatheringState', ['new', 'gathering', 'complete'], 'icegatheringstatechange'],
                ['iceConnectionState', ['new', 'checking', 'connected', 'completed', 'disconnected', 'failed', 'closed'], 'iceconnectionstatechange'],
                ['connectionState', ['new', 'connecting', 'connected', 'disconnected', 'failed', 'closed'], 'connectionstatechange']
            ]) if (allowed.includes(event[key])) stateChange(target, key, event[key], eventName);
            return;
        }
        if (event.type === 'candidate') {
            refreshLocal(target);
            let candidate = null;
            if (event.candidate !== null && event.candidate !== undefined) {
                try { candidate = new RTCIceCandidate({candidate: event.candidate, sdpMid: event.sdpMid ?? null, sdpMLineIndex: event.sdpMLineIndex ?? 0}); } catch { return; }
            }
            dispatch(target, new RTCPeerConnectionIceEvent('icecandidate', {candidate}), true);
            return;
        }
        if (event.type === 'datachannel') {
            if (state.channels.has(event.channelId)) return;
            let info;
            try { info = N.rtcChannelInfo(event.channelId); } catch { return; }
            const dataChannel = new RTCDataChannel(token, target, event.channelId, info);
            state.channels.set(event.channelId, dataChannel);
            dispatch(target, new RTCDataChannelEvent('datachannel', {channel: dataChannel}), true);
            if (info.readyState === 'open') queuePeerTask(target, () => nativeEvent(target, {type: 'channel-open', channelId: event.channelId}));
            return;
        }
        const dataChannel = state.channels.get(event.channelId);
        if (!dataChannel) return;
        const item = channel(dataChannel);
        if (item.readyState === 'closed') return;
        if (event.type === 'channel-open' && item.readyState === 'connecting') {
            channelInfo(dataChannel);
            item.readyState = 'open';
            emit(dataChannel, 'open');
        } else if (event.type === 'channel-close' && item.readyState !== 'closed') {
            item.readyState = 'closed';
            emit(dataChannel, 'close');
        } else if (event.type === 'channel-message' && item.readyState === 'open') {
            let data = event.data;
            if (data instanceof ArrayBuffer) data = item.binaryType === 'blob' ? new Blob([data]) : data.slice(0);
            else if (ArrayBuffer.isView(data)) {
                const copied = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
                data = item.binaryType === 'blob' ? new Blob([copied]) : copied;
            } else if (typeof data !== 'string') return;
            emit(dataChannel, 'message', {data});
        } else if (event.type === 'channel-error') emit(dataChannel, 'error', {error: nativeError(event)});
        else if (event.type === 'channel-bufferedamountlow' && item.readyState === 'open') emit(dataChannel, 'bufferedamountlow');
    }

    class RTCPeerConnection extends EventTarget {
        constructor(config = {}) {
            super();
            const checked = configuration(config);
            const state = {id: null, closed: false, config: checked, channels: new Map(), tasks: new Set(), requests: new Map(), nextRequest: 0, chain: Promise.resolve(), generated: {}, currentLocalDescription: null, pendingLocalDescription: null, currentRemoteDescription: null, pendingRemoteDescription: null, signalingState: 'stable', iceGatheringState: 'new', iceConnectionState: 'new', connectionState: 'new', canTrickleIceCandidates: null, negotiationQueued: false};
            peers.set(this, state);
            try {
                state.id = N.rtcCreate(JSON.stringify(checked), event => nativeEvent(this, event));
                if (!Number.isInteger(state.id) || state.id < 0) throw error('OperationError');
            } catch (cause) { state.closed = true; throw nativeError(cause); }
        }
        #enqueue(work) {
            const state = peer(this);
            const next = state.chain.then(() => { requireOpenPeer(state); return work(); });
            state.chain = next.catch(() => {});
            return next;
        }
        #request(method, ...args) {
            const state = peer(this);
            requireOpenPeer(state);
            return new Promise((resolve, reject) => {
                const requestId = ++state.nextRequest;
                state.requests.set(requestId, {resolve, reject});
                try { N[method](state.id, ...args, requestId); }
                catch (cause) { state.requests.delete(requestId); reject(nativeError(cause)); }
            });
        }
        async #generate(type) {
            const state = peer(this);
            if (type === 'answer' && state.signalingState !== 'have-remote-offer') throw error('InvalidStateError', 'A remote offer is required.');
            const result = await this.#request('rtcGenerateDescription', type);
            requireOpenPeer(state);
            state.generated[type] = result.sdp;
            return result;
        }
        createOffer(options = {}) {
            return this.#enqueue(() => {
                if (options?.iceRestart) throw unsupported('ICE restart');
                if (options?.offerToReceiveAudio || options?.offerToReceiveVideo) throw unsupported('RTP media');
                return this.#generate('offer');
            });
        }
        createAnswer() { return this.#enqueue(() => this.#generate('answer')); }
        setLocalDescription(input) {
            let supplied;
            try { supplied = input == null ? {} : {type: input.type === undefined ? undefined : String(input.type), sdp: input.sdp === undefined ? '' : String(input.sdp)}; }
            catch (cause) { return Promise.reject(cause); }
            return this.#enqueue(async () => {
                const state = peer(this), type = supplied.type ?? (state.signalingState === 'have-remote-offer' ? 'answer' : 'offer');
                if (!['offer', 'answer', 'pranswer', 'rollback'].includes(type)) throw new TypeError('Invalid SDP type.');
                if (!['offer', 'answer'].includes(type)) throw unsupported('Provisional answers and rollback');
                if ((type === 'offer' && !['stable', 'have-local-offer'].includes(state.signalingState)) || (type === 'answer' && state.signalingState !== 'have-remote-offer')) throw error('InvalidStateError', 'Invalid signaling state.');
                let sdp = supplied.sdp || '';
                if (sdp && sdp !== state.generated[type]) throw error('InvalidModificationError', 'The supplied SDP differs from the last generated description.');
                if (!sdp) sdp = state.generated[type] || (await this.#generate(type)).sdp;
                await this.#request('rtcSetLocalDescription', type, sdp);
                requireOpenPeer(state);
                const local = description({type, sdp});
                if (type === 'offer') state.pendingLocalDescription = local;
                else { state.currentLocalDescription = local; state.currentRemoteDescription = state.pendingRemoteDescription; state.pendingLocalDescription = null; state.pendingRemoteDescription = null; }
                refreshLocal(this);
                stateChange(this, 'signalingState', type === 'offer' ? 'have-local-offer' : 'stable', 'signalingstatechange');
            });
        }
        setRemoteDescription(input) {
            let remote;
            try { remote = description(input); if (!remote) throw new TypeError('A session description is required.'); }
            catch (cause) { return Promise.reject(cause); }
            return this.#enqueue(async () => {
                const state = peer(this), {type, sdp} = remote;
                if (!['offer', 'answer'].includes(type)) throw unsupported('Provisional answers and rollback');
                if (/^m=(audio|video)\s/m.test(sdp)) throw unsupported('RTP media');
                if ((type === 'offer' && state.signalingState !== 'stable') || (type === 'answer' && state.signalingState !== 'have-local-offer')) throw error('InvalidStateError', 'Invalid signaling state.');
                await this.#request('rtcSetRemoteDescription', type, sdp);
                requireOpenPeer(state);
                if (type === 'offer') state.pendingRemoteDescription = remote;
                else { state.currentRemoteDescription = remote; state.currentLocalDescription = state.pendingLocalDescription; state.pendingRemoteDescription = null; state.pendingLocalDescription = null; }
                state.canTrickleIceCandidates = /^a=ice-options:.*\btrickle\b/m.test(sdp);
                stateChange(this, 'signalingState', type === 'offer' ? 'have-remote-offer' : 'stable', 'signalingstatechange');
            });
        }
        addIceCandidate(input = null) {
            let candidate;
            try {
                candidate = input == null ? {candidate: '', sdpMid: null, sdpMLineIndex: null} : {candidate: String(input.candidate ?? ''), sdpMid: input.sdpMid == null ? null : String(input.sdpMid), sdpMLineIndex: input.sdpMLineIndex == null ? null : integer(input.sdpMLineIndex, 65535, 'sdpMLineIndex')};
                if (candidate.candidate && candidate.sdpMid === null && candidate.sdpMLineIndex === null) throw new TypeError('A candidate requires a mid or m-line index.');
            } catch (cause) { return Promise.reject(cause); }
            return this.#enqueue(async () => {
                const state = peer(this), remote = state.pendingRemoteDescription || state.currentRemoteDescription;
                if (!remote) throw error('InvalidStateError', 'A remote description is required.');
                const sections = remote.sdp.split(/\r?\nm=/).slice(1);
                const mids = sections.map(section => /^a=mid:(.+)$/m.exec(section)?.[1]?.trim() ?? null);
                let mid = candidate.sdpMid;
                if (mid !== null && !mids.includes(mid)) throw error('OperationError', 'The candidate mid is not present in the remote description.');
                if (mid === null && candidate.sdpMLineIndex !== null) {
                    if (candidate.sdpMLineIndex >= sections.length) throw error('OperationError', 'The candidate index is outside the remote description.');
                    mid = mids[candidate.sdpMLineIndex];
                }
                await this.#request('rtcAddCandidate', candidate.candidate, mid ?? '');
                requireOpenPeer(state);
            });
        }
        createDataChannel(label, options = {}) {
            if (arguments.length === 0) throw new TypeError('A data channel label is required.');
            const state = peer(this); requireOpenPeer(state);
            const checked = optionsForChannel(label, options);
            let id, info;
            try { id = N.rtcCreateChannel(state.id, JSON.stringify(checked)); info = N.rtcChannelInfo(id); }
            catch (cause) { if (Number.isInteger(id) && id >= 0) { try { N.rtcCloseChannel(id); } catch {} } throw nativeError(cause); }
            if (!Number.isInteger(id) || id < 0 || state.channels.has(id)) throw error('OperationError', 'Invalid native channel handle.');
            const dataChannel = new RTCDataChannel(token, this, id, {...checked, ...info});
            state.channels.set(id, dataChannel);
            if (info.readyState === 'open') queuePeerTask(this, () => nativeEvent(this, {type: 'channel-open', channelId: id}));
            if (!state.negotiationQueued && state.channels.size === 1) {
                state.negotiationQueued = true;
                queuePeerTask(this, () => { state.negotiationQueued = false; if (state.signalingState === 'stable' && !state.currentLocalDescription && !state.pendingLocalDescription) emit(this, 'negotiationneeded'); });
            }
            return dataChannel;
        }
        getConfiguration() { return JSON.parse(JSON.stringify(peer(this).config)); }
        setConfiguration(input) {
            const state = peer(this); requireOpenPeer(state);
            const next = configuration(input);
            if (JSON.stringify(next) !== JSON.stringify(state.config)) throw unsupported('Changing native ICE configuration');
        }
        get localDescription() { refreshLocal(this); const state = peer(this); return state.pendingLocalDescription || state.currentLocalDescription; }
        get currentLocalDescription() { refreshLocal(this); return peer(this).currentLocalDescription; }
        get pendingLocalDescription() { refreshLocal(this); return peer(this).pendingLocalDescription; }
        get remoteDescription() { const state = peer(this); return state.pendingRemoteDescription || state.currentRemoteDescription; }
        getStats(selector = null) {
            const state = peer(this);
            if (selector !== null) return Promise.reject(unsupported('RTP track statistics'));
            if (state.closed) return Promise.reject(error('InvalidStateError', 'The peer connection is closed.'));
            try {
                const measured = N.rtcStats(state.id), timestamp = performance.now(), entries = [];
                for (const item of measured.channels || []) {
                    const id = 'native-data-channel-' + state.id + '-' + item.channelId;
                    const stat = {id, type: 'data-channel', timestamp, label: item.label, protocol: item.protocol, state: item.state};
                    if (Number.isInteger(item.id) && item.id >= 0) stat.dataChannelIdentifier = item.id;
                    for (const key of ['messagesSent', 'bytesSent', 'messagesReceived', 'bytesReceived']) if (Number.isFinite(item[key]) && item[key] >= 0) stat[key] = item[key];
                    entries.push([id, Object.freeze(stat)]);
                }
                // A pair exists only if the library actually selected it. Its
                // candidate metadata is real; nomination flags and network byte
                // totals are not exposed by this C API and remain absent.
                const selected = measured.selectedCandidatePair;
                if (selected && ['connected', 'completed'].includes(measured.iceConnectionState)) {
                    const local = rtcParseCand(String(selected.localCandidate ?? '').replace(/^a=/, ''));
                    const remote = rtcParseCand(String(selected.remoteCandidate ?? '').replace(/^a=/, ''));
                    if (local.protocol && remote.protocol) {
                        const pairId = 'native-candidate-pair-' + state.id;
                        for (const [role, value] of [['local', local], ['remote', remote]]) {
                            const id = 'native-' + role + '-candidate-' + state.id;
                            entries.push([id, Object.freeze({id, type: role + '-candidate', timestamp, candidateType: value.type, protocol: value.protocol, address: value.address, port: value.port, priority: value.priority, foundation: value.foundation})]);
                        }
                        entries.push([pairId, Object.freeze({id: pairId, type: 'candidate-pair', timestamp, state: 'succeeded', localCandidateId: 'native-local-candidate-' + state.id, remoteCandidateId: 'native-remote-candidate-' + state.id})]);
                    }
                }
                // Do not relabel application bytes as ICE/DTLS/UDP statistics.
                return Promise.resolve(new RTCStatsReport(token, entries));
            } catch (cause) { return Promise.reject(nativeError(cause)); }
        }
        getSenders() { peer(this); return []; }
        getReceivers() { peer(this); return []; }
        getTransceivers() { peer(this); return []; }
        addTrack() { requireOpenPeer(peer(this)); throw unsupported('RTP media'); }
        addTransceiver() { requireOpenPeer(peer(this)); throw unsupported('RTP media'); }
        removeTrack() { requireOpenPeer(peer(this)); throw unsupported('RTP media'); }
        restartIce() { requireOpenPeer(peer(this)); throw unsupported('ICE restart'); }
        static generateCertificate() { return Promise.reject(unsupported('Certificate generation')); }
        close() {
            terminatePeer(this);
        }
    }
    readonly(RTCPeerConnection, ['signalingState', 'iceGatheringState', 'iceConnectionState', 'connectionState', 'canTrickleIceCandidates', 'currentRemoteDescription', 'pendingRemoteDescription'], peer);
    handlers(RTCPeerConnection, ['negotiationneeded', 'icecandidate', 'icecandidateerror', 'signalingstatechange', 'iceconnectionstatechange', 'icegatheringstatechange', 'connectionstatechange', 'datachannel', 'track']);
    return {RTCPeerConnection, RTCDataChannel, RTCDataChannelEvent, RTCStatsReport};
})();
