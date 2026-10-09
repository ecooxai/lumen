
// ---- WebRTC: API surface over an in-process loopback transport (peers inside this Lumen process connect) ----
const rtcTask = f => setTimeout(f, 0);
const rtcRand = n => { const a = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'; let s = ''; for (let i = 0; i < n; i++) s += a[Math.random() * 64 | 0]; return s; };
const rtcUUID = () => 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => { const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); });
const rtcHex = n => Array.from({ length: n }, () => (Math.random() * 256 | 0).toString(16).toUpperCase().padStart(2, '0')).join(':');
const rtcFire = (t, ev) => dispatch(t, ev, true);
const rtcEv = (t, type) => rtcFire(t, new Event(type));
const rtcErr = (name, msg) => new DOMException(msg || name, name);
const rtcU8 = s => new TextEncoder().encode(s).length;
const rtcUSV = v => String(v).replace(/[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/g, '\uFFFD');
const rtcRange = (v, max, what) => { const n = Number(v); if (!Number.isFinite(n)) throw new TypeError(`${what} is not a finite number.`); const t = Math.trunc(n); if (t < 0 || t > max) throw new TypeError(`${what} is outside the range [0, ${max}].`); return t; };
const rtcRO = (C, keys) => { for (const k of keys) Object.defineProperty(C.prototype, k, { get() { return this._[k]; }, configurable: true, enumerable: true }); };
const rtcOn = (C, names) => { for (const n of names) Object.defineProperty(C.prototype, 'on' + n, { get() { return (this._on && this._on[n]) || null; }, set(f) { if (!this._on) def(this, '_on', {}); this._on[n] = typeof f === 'function' ? f : null; }, configurable: true, enumerable: true }); };
const RTC_INTERNAL = Symbol('rtc');
const rtcPeers = new Map();

const RTC_ERROR_DETAILS = ['data-channel-failure', 'dtls-failure', 'fingerprint-failure', 'sctp-failure', 'sdp-syntax-error', 'hardware-encoder-not-available', 'hardware-encoder-error'];
class RTCError extends DOMException {
    constructor(init, message = '') {
        if (init === undefined || init === null || typeof init !== 'object' || init.errorDetail === undefined) throw new TypeError("Failed to construct 'RTCError': required member errorDetail is undefined.");
        const d = String(init.errorDetail);
        if (!RTC_ERROR_DETAILS.includes(d)) throw new TypeError(`Failed to construct 'RTCError': The provided value '${d}' is not a valid enum value of type RTCErrorDetailType.`);
        super(message === undefined ? '' : String(message), 'OperationError');
        const n = k => init[k] === undefined || init[k] === null ? null : Number(init[k]);
        def(this, '_', { errorDetail: d, sdpLineNumber: n('sdpLineNumber'), sctpCauseCode: n('sctpCauseCode'), receivedAlert: n('receivedAlert'), sentAlert: n('sentAlert'), httpRequestStatusCode: n('httpRequestStatusCode') });
    }
}
rtcRO(RTCError, ['errorDetail', 'sdpLineNumber', 'sctpCauseCode', 'receivedAlert', 'sentAlert', 'httpRequestStatusCode']);
class RTCErrorEvent extends Event {
    constructor(t, i) { if (!i || !(i.error instanceof RTCError)) throw new TypeError("Failed to construct 'RTCErrorEvent': required member error is undefined."); super(t, i); def(this, '_', { error: i.error }); }
}
rtcRO(RTCErrorEvent, ['error']);

const RTC_SDP_TYPES = ['offer', 'pranswer', 'answer', 'rollback'];
class RTCSessionDescription {
    constructor(init = {}) {
        if (init === null || typeof init !== 'object') init = {};
        if (init.type === undefined) throw new TypeError("Failed to construct 'RTCSessionDescription': required member type is undefined.");
        const type = String(init.type);
        if (!RTC_SDP_TYPES.includes(type)) throw new TypeError(`Failed to construct 'RTCSessionDescription': The provided value '${type}' is not a valid enum value of type RTCSdpType.`);
        def(this, '_', { type, sdp: init.sdp === undefined || init.sdp === null ? '' : String(init.sdp) });
    }
    toJSON() { return { type: this.type, sdp: this.sdp }; }
}
rtcRO(RTCSessionDescription, ['type', 'sdp']);

const rtcParseCand = s => {
    const r = { foundation: null, component: null, priority: null, address: null, protocol: null, port: null, type: null, tcpType: null, relatedAddress: null, relatedPort: null, ufrag: null };
    if (!s.startsWith('candidate:') || /^\s/.test(s.slice(10))) return r;
    const p = s.slice(10).trim().split(/\s+/);
    if (p.length < 8 || p[6] !== 'typ') return r;
    const proto = p[2].toLowerCase(), typ = p[7].toLowerCase();
    if (!/^[A-Za-z0-9+\/]{1,32}$/.test(p[0]) || !/^\d{1,3}$/.test(p[1]) || +p[1] < 1 || +p[1] > 256) return r;
    if (!['udp', 'tcp'].includes(proto) || !/^\d{1,10}$/.test(p[3]) || +p[3] < 1 || +p[3] > 2147483647 || !/^\d{1,5}$/.test(p[5]) || +p[5] > 65535) return r;
    if (!['host', 'srflx', 'prflx', 'relay'].includes(typ)) return r;
    let i = 8, raddr = null, rport = null, tcpType = null;
    if (typ !== 'host') {
        if (p[i] !== 'raddr' || p[i + 2] !== 'rport' || !/^\d{1,5}$/.test(p[i + 3] || '')) return r;
        raddr = p[i + 1]; rport = +p[i + 3]; i += 4;
    }
    if (proto === 'tcp' && typ !== 'relay') {
        if (p[i] !== 'tcptype' || !['active', 'passive', 'so'].includes((p[i + 1] || '').toLowerCase())) return r;
        tcpType = p[i + 1].toLowerCase(); i += 2;
    }
    Object.assign(r, { foundation: p[0], component: p[1] === '1' ? 'rtp' : p[1] === '2' ? 'rtcp' : null, protocol: proto, priority: +p[3], address: p[4], port: +p[5], type: typ, tcpType, relatedAddress: raddr, relatedPort: rport });
    for (; i + 1 < p.length; i += 2) if (p[i] === 'ufrag') r.ufrag = p[i + 1];
    return r;
};
class RTCIceCandidate {
    constructor(init = {}) {
        if (init === null || typeof init !== 'object') init = {};
        const sdpMid = init.sdpMid === undefined || init.sdpMid === null ? null : String(init.sdpMid);
        const sdpMLineIndex = init.sdpMLineIndex === undefined || init.sdpMLineIndex === null ? null : rtcRange(init.sdpMLineIndex, 65535, 'sdpMLineIndex');
        if (sdpMid === null && sdpMLineIndex === null) throw new TypeError("Failed to construct 'RTCIceCandidate': sdpMid and sdpMLineIndex are both null.");
        const candidate = init.candidate === undefined ? '' : String(init.candidate);
        const c = rtcParseCand(candidate);
        const usernameFragment = init.usernameFragment === undefined || init.usernameFragment === null ? null : String(init.usernameFragment);
        const opt = k => init[k] === undefined || init[k] === null ? null : String(init[k]);
        def(this, '_', Object.assign(c, { candidate, sdpMid, sdpMLineIndex, usernameFragment: usernameFragment ?? c.ufrag, relayProtocol: opt('relayProtocol'), url: opt('url') }));
    }
    toJSON() { return { candidate: this.candidate, sdpMid: this.sdpMid, sdpMLineIndex: this.sdpMLineIndex, usernameFragment: this.usernameFragment }; }
}
rtcRO(RTCIceCandidate, ['candidate', 'sdpMid', 'sdpMLineIndex', 'foundation', 'component', 'priority', 'address', 'protocol', 'port', 'type', 'tcpType', 'relatedAddress', 'relatedPort', 'usernameFragment', 'relayProtocol', 'url']);
class RTCPeerConnectionIceEvent extends Event {
    constructor(t, i = {}) { super(t, i); i = i || {}; def(this, '_', { candidate: i.candidate ?? null, url: i.url ?? null }); }
}
rtcRO(RTCPeerConnectionIceEvent, ['candidate', 'url']);
class RTCPeerConnectionIceErrorEvent extends Event {
    constructor(t, i) {
        if (!i || i.errorCode === undefined) throw new TypeError("Failed to construct 'RTCPeerConnectionIceErrorEvent': required member errorCode is undefined.");
        super(t, i);
        def(this, '_', { address: i.address ?? null, port: i.port ?? null, url: i.url === undefined ? '' : String(i.url), errorCode: rtcRange(i.errorCode, 65535, 'errorCode'), errorText: i.errorText === undefined ? '' : String(i.errorText) });
    }
}
rtcRO(RTCPeerConnectionIceErrorEvent, ['address', 'port', 'url', 'errorCode', 'errorText']);

class MediaStreamTrack extends EventTarget {
    constructor(tok, kind, label) {
        if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor');
        super();
        def(this, '_', { kind, id: rtcUUID(), label: label || '', enabled: true, muted: false, readyState: 'live', contentHint: '' });
    }
    get enabled() { return this._.enabled; } set enabled(v) { this._.enabled = !!v; }
    get contentHint() { return this._.contentHint; } set contentHint(v) { this._.contentHint = String(v); }
    stop() { this._.readyState = 'ended'; }
    clone() { const t = new MediaStreamTrack(RTC_INTERNAL, this._.kind, this._.label); t._.enabled = this._.enabled; t._.muted = this._.muted; t._.readyState = this._.readyState; return t; }
    getSettings() { return this._.kind === 'audio' ? { deviceId: 'default', groupId: '', sampleRate: 48000, channelCount: 1 } : { deviceId: 'default', groupId: '', width: 640, height: 480, frameRate: 30 }; }
    getConstraints() { return {}; }
    getCapabilities() { return {}; }
    applyConstraints() { return Promise.resolve(); }
    _mute(m) { if (this._.muted === m) return; this._.muted = m; rtcEv(this, m ? 'mute' : 'unmute'); }
}
rtcRO(MediaStreamTrack, ['kind', 'id', 'label', 'muted', 'readyState']);
rtcOn(MediaStreamTrack, ['mute', 'unmute', 'ended']);
class MediaStreamTrackEvent extends Event {
    constructor(t, i) { if (!i || !(i.track instanceof MediaStreamTrack)) throw new TypeError("Failed to construct 'MediaStreamTrackEvent': required member track is undefined."); super(t, i); def(this, '_', { track: i.track }); }
}
rtcRO(MediaStreamTrackEvent, ['track']);
class MediaStream extends EventTarget {
    constructor(a) {
        super();
        const tracks = a instanceof MediaStream ? a.getTracks() : a === undefined ? [] : [...a];
        for (const t of tracks) if (!(t instanceof MediaStreamTrack)) throw new TypeError("Failed to construct 'MediaStream': Failed to convert value to 'MediaStreamTrack'.");
        def(this, '_', { id: rtcUUID(), tracks: [...new Set(tracks)] });
    }
    get active() { return this._.tracks.some(t => t.readyState === 'live'); }
    getTracks() { return this._.tracks.slice(); }
    getAudioTracks() { return this._.tracks.filter(t => t.kind === 'audio'); }
    getVideoTracks() { return this._.tracks.filter(t => t.kind === 'video'); }
    getTrackById(id) { return this._.tracks.find(t => t.id === id) || null; }
    addTrack(t) { if (!this._.tracks.includes(t)) this._.tracks.push(t); }
    removeTrack(t) { this._.tracks = this._.tracks.filter(x => x !== t); }
    clone() { return new MediaStream(this._.tracks.map(t => t.clone())); }
}
rtcRO(MediaStream, ['id']);
rtcOn(MediaStream, ['addtrack', 'removetrack']);

class RTCCertificate {
    constructor(tok, expires) { if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); def(this, '_', { expires, fp: rtcHex(32), der: crypto.getRandomValues(new Uint8Array(64)).buffer }); }
    get expires() { return this._.expires; }
    getFingerprints() { return [{ algorithm: 'sha-256', value: this._.fp.toLowerCase() }]; }
}

const RTC_CODECS = {
    audio: [
        { pt: 111, mimeType: 'audio/opus', clockRate: 48000, channels: 2, sdpFmtpLine: 'minptime=10;useinbandfec=1' },
        { pt: 9, mimeType: 'audio/G722', clockRate: 8000, channels: 1 },
        { pt: 0, mimeType: 'audio/PCMU', clockRate: 8000, channels: 1 },
        { pt: 8, mimeType: 'audio/PCMA', clockRate: 8000, channels: 1 },
        { pt: 13, mimeType: 'audio/CN', clockRate: 8000, channels: 1 },
        { pt: 126, mimeType: 'audio/telephone-event', clockRate: 8000, channels: 1 },
    ],
    video: [
        { pt: 96, mimeType: 'video/VP8', clockRate: 90000 },
        { pt: 97, mimeType: 'video/rtx', clockRate: 90000, sdpFmtpLine: 'apt=96' },
        { pt: 98, mimeType: 'video/VP9', clockRate: 90000, sdpFmtpLine: 'profile-id=0' },
        { pt: 99, mimeType: 'video/rtx', clockRate: 90000, sdpFmtpLine: 'apt=98' },
        { pt: 102, mimeType: 'video/H264', clockRate: 90000, sdpFmtpLine: 'level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f' },
        { pt: 103, mimeType: 'video/rtx', clockRate: 90000, sdpFmtpLine: 'apt=102' },
        { pt: 116, mimeType: 'video/red', clockRate: 90000 },
        { pt: 117, mimeType: 'video/ulpfec', clockRate: 90000 },
    ],
};
const RTC_HDREXT = { audio: ['urn:ietf:params:rtp-hdrext:ssrc-audio-level', 'urn:ietf:params:rtp-hdrext:sdes:mid'], video: ['urn:ietf:params:rtp-hdrext:toffset', 'http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time', 'urn:3gpp:video-orientation', 'urn:ietf:params:rtp-hdrext:sdes:mid'] };
const rtcCodecDict = c => { const o = { mimeType: c.mimeType, clockRate: c.clockRate }; if (c.channels !== undefined) o.channels = c.channels; if (c.sdpFmtpLine) o.sdpFmtpLine = c.sdpFmtpLine; return o; };
const rtcCaps = kind => RTC_CODECS[kind] ? { codecs: RTC_CODECS[kind].map(rtcCodecDict), headerExtensions: RTC_HDREXT[kind].map(uri => ({ uri })) } : null;
const rtcCodecEq = (a, b) => a.mimeType.toLowerCase() === b.mimeType.toLowerCase() && a.clockRate === b.clockRate && (a.channels ?? null) === (b.channels ?? null) && (a.sdpFmtpLine ?? '') === (b.sdpFmtpLine ?? '');

class RTCIceTransport extends EventTarget {
    constructor(tok) { if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); super(); def(this, '_', { role: 'unknown', component: 'rtp', state: 'new', gatheringState: 'new', local: [], remote: [], pair: null, lp: null, rp: null }); }
    getLocalCandidates() { return this._.local.slice(); }
    getRemoteCandidates() { return this._.remote.slice(); }
    getSelectedCandidatePair() { return this._.pair; }
    getLocalParameters() { return this._.lp; }
    getRemoteParameters() { return this._.rp; }
    _set(k, v, ev) { if (this._[k] === v) return; this._[k] = v; if (ev) rtcEv(this, ev); }
}
rtcRO(RTCIceTransport, ['role', 'component', 'state', 'gatheringState']);
rtcOn(RTCIceTransport, ['statechange', 'gatheringstatechange', 'selectedcandidatepairchange']);
class RTCDtlsTransport extends EventTarget {
    constructor(tok, ice) { if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); super(); def(this, '_', { iceTransport: ice, state: 'new', remoteCerts: [] }); }
    getRemoteCertificates() { return this._.remoteCerts.slice(); }
    _set(v) { if (this._.state === v || this._.state === 'closed') return; this._.state = v; rtcEv(this, 'statechange'); }
}
rtcRO(RTCDtlsTransport, ['iceTransport', 'state']);
rtcOn(RTCDtlsTransport, ['statechange', 'error']);
class RTCSctpTransport extends EventTarget {
    constructor(tok, dtls) { if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); super(); def(this, '_', { transport: dtls, state: 'connecting', maxMessageSize: 262144, maxChannels: null }); }
    _set(v) { if (this._.state === v || this._.state === 'closed') return; this._.state = v; rtcEv(this, 'statechange'); }
}
rtcRO(RTCSctpTransport, ['transport', 'state', 'maxMessageSize', 'maxChannels']);
rtcOn(RTCSctpTransport, ['statechange']);

class RTCStatsReport {
    constructor(tok, m) { if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); def(this, '_m', m); }
    get size() { return this._m.size; }
    get(k) { return this._m.get(k); } has(k) { return this._m.has(k); }
    keys() { return this._m.keys(); } values() { return this._m.values(); } entries() { return this._m.entries(); }
    forEach(f, t) { this._m.forEach((v, k) => f.call(t, v, k, this)); }
    [Symbol.iterator]() { return this._m.entries(); }
}

const RTC_DIRS = ['sendrecv', 'sendonly', 'recvonly', 'inactive', 'stopped'];
const rtcRev = d => d === 'sendonly' ? 'recvonly' : d === 'recvonly' ? 'sendonly' : d;
const rtcHasSend = d => d === 'sendrecv' || d === 'sendonly';
const rtcHasRecv = d => d === 'sendrecv' || d === 'recvonly';
const rtcMkDir = (s, r) => s && r ? 'sendrecv' : s ? 'sendonly' : r ? 'recvonly' : 'inactive';
const rtcCheckEncodings = (encs, kind) => {
    if (!encs) return;
    for (const e of encs) {
        if (e.scaleResolutionDownBy !== undefined && (kind === 'video' ? !(+e.scaleResolutionDownBy >= 1) : false)) throw new RangeError('scaleResolutionDownBy must be >= 1.0');
        if (e.maxFramerate !== undefined && !(+e.maxFramerate >= 0)) throw new RangeError('maxFramerate must be >= 0');
    }
};
class RTCRtpSender {
    constructor(tok, pc, kind, track) { if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); def(this, '_', { pc, kind, track, streams: [], tid: null, encodings: [kind === 'video' ? { active: true, scaleResolutionDownBy: 1 } : { active: true }], dtmf: null, ssrc: (Math.random() * 0xffffffff) >>> 0, used: !!track }); }
    get track() { return this._.track; }
    get transport() { return this._.pc._.dtlsReady ? this._.pc._.dtls : null; }
    get dtmf() { if (this._.kind !== 'audio') return null; return this._.dtmf || (this._.dtmf = Object.assign(new EventTarget(), { canInsertDTMF: false, toneBuffer: '', ontonechange: null, insertDTMF() {} })); }
    static getCapabilities(kind) { return rtcCaps(String(kind)); }
    getParameters() {
        this._.tid = rtcUUID();
        const codecs = this._.pc._codecsFor(this);
        this._.lastCodecs = JSON.stringify(codecs);
        return { transactionId: this._.tid, encodings: this._.encodings.map(e => Object.assign({}, e)), headerExtensions: [], rtcp: { cname: this._.pc._.cname, reducedSize: true }, codecs, degradationPreference: undefined };
    }
    setParameters(p, opts) {
        const pc = this._.pc;
        if (pc._.closed) return Promise.reject(rtcErr('InvalidStateError'));
        if (!p || p.transactionId === undefined || p.transactionId !== this._.tid) return Promise.reject(rtcErr('InvalidStateError', 'getParameters() needs to be called before setParameters().'));
        this._.tid = null;
        if (!Array.isArray(p.encodings) || p.encodings.length !== this._.encodings.length) return Promise.reject(rtcErr('InvalidModificationError', 'encodings can not be added or removed.'));
        for (let i = 0; i < p.encodings.length; i++) if ((p.encodings[i].rid ?? undefined) !== (this._.encodings[i].rid ?? undefined)) return Promise.reject(rtcErr('InvalidModificationError', 'rid can not be modified.'));
        try { rtcCheckEncodings(p.encodings, this._.kind); } catch (e) { return Promise.reject(e); }
        if (this._.lastCodecs !== undefined && JSON.stringify(p.codecs) !== this._.lastCodecs) return Promise.reject(rtcErr('InvalidModificationError', 'codecs can not be modified.'));
        const neg = pc._codecsFor(this), allowed = neg.length ? neg : RTC_CODECS[this._.kind];
        for (const e of p.encodings) if (e.codec !== undefined && !allowed.some(c => rtcCodecEq(c, e.codec))) return Promise.reject(rtcErr('InvalidModificationError', 'codec is not negotiated.'));
        this._.encodings = p.encodings.map(e => Object.assign({}, e));
        return new Promise(r => rtcTask(() => r(undefined)));
    }
    replaceTrack(t) {
        const pc = this._.pc;
        if (pc._.closed) return Promise.reject(rtcErr('InvalidStateError'));
        if (t !== null && !(t instanceof MediaStreamTrack)) return Promise.reject(new TypeError("Failed to execute 'replaceTrack': parameter 1 is not of type 'MediaStreamTrack'."));
        if (t && t.kind !== this._.kind) return Promise.reject(new TypeError('Track kind does not match sender kind.'));
        const tr = pc._.transceivers.find(x => x.sender === this);
        if (tr && tr._.stopping) return Promise.reject(rtcErr('InvalidStateError'));
        return new Promise(r => rtcTask(() => { this._.track = t; if (t) this._.used = true; r(undefined); }));
    }
    setStreams(...s) { if (this._.pc._.closed) throw rtcErr('InvalidStateError'); this._.streams = [...new Set(s)]; this._.pc._updateNeg(); }
    getStats() { return this._.pc.getStats(this._.track); }
}
class RTCRtpReceiver {
    constructor(tok, pc, kind) { if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); const t = new MediaStreamTrack(RTC_INTERNAL, kind, 'remote ' + kind); t._.muted = true; def(this, '_', { pc, kind, track: t, jbt: null, streams: [] }); }
    get track() { return this._.track; }
    get transport() { return this._.pc._.dtlsReady ? this._.pc._.dtls : null; }
    get jitterBufferTarget() { return this._.jbt; }
    set jitterBufferTarget(v) { if (v === null) { this._.jbt = null; return; } const n = Number(v); if (!Number.isFinite(n)) throw new TypeError('jitterBufferTarget must be finite.'); if (n < 0 || n > 4000) throw new RangeError('jitterBufferTarget out of range.'); this._.jbt = n; }
    static getCapabilities(kind) { return rtcCaps(String(kind)); }
    getParameters() { return { headerExtensions: [], rtcp: { cname: '', reducedSize: true }, codecs: this._.pc._codecsFor(this) }; }
    getContributingSources() { return []; }
    getSynchronizationSources() { return []; }
    getStats() { return this._.pc.getStats(this._.track); }
}
class RTCRtpTransceiver {
    constructor(tok, pc, kind, track, dir) {
        if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor');
        def(this, '_', { pc, kind, sender: new RTCRtpSender(RTC_INTERNAL, pc, kind, track), receiver: new RTCRtpReceiver(RTC_INTERNAL, pc, kind), direction: dir, current: null, mid: null, stopping: false, stopped: false, prefs: null, fromSRD: false, addTrack: false, fired: false });
    }
    get mid() { return this._.mid; }
    get sender() { return this._.sender; }
    get receiver() { return this._.receiver; }
    get direction() { return this._.stopping ? 'stopped' : this._.direction; }
    set direction(v) {
        v = String(v);
        if (!RTC_DIRS.includes(v)) throw new TypeError(`The provided value '${v}' is not a valid enum value of type RTCRtpTransceiverDirection.`);
        if (this._.pc._.closed || this._.stopping) throw rtcErr('InvalidStateError');
        if (v === 'stopped') throw new TypeError("'stopped' is not a valid direction.");
        if (v === this._.direction) return;
        this._.direction = v; this._.pc._updateNeg();
    }
    get currentDirection() { return this._.stopped ? 'stopped' : this._.current; }
    stop() {
        const pc = this._.pc;
        if (pc._.closed) throw rtcErr('InvalidStateError');
        if (this._.stopping) return;
        this._.stopping = true; this._.sender._.track = null;
        const t = this._.receiver._.track; t._.readyState = 'ended'; t._mute(true);
        pc._updateNeg();
    }
    setCodecPreferences(codecs) {
        const list = [...codecs];
        if (!list.length) { this._.prefs = null; return; }
        const caps = [...RTC_CODECS[this._.kind]];
        for (const c of list) if (!caps.some(k => rtcCodecEq(k, c))) throw rtcErr('InvalidModificationError', 'Invalid codec preferences.');
        if (list.every(c => /\/(rtx|red|ulpfec|flexfec-03|CN|telephone-event)$/i.test(c.mimeType))) throw rtcErr('InvalidModificationError', 'Only resiliency codecs.');
        this._.prefs = list.map(c => caps.find(k => rtcCodecEq(k, c)));
    }
}

const RTC_DC_PRIO = ['very-low', 'low', 'medium', 'high'];
class RTCDataChannel extends EventTarget {
    constructor(tok, pc, o) {
        if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor');
        super();
        def(this, '_', Object.assign({ pc, readyState: 'connecting', bufferedAmount: 0, threshold: 0, binaryType: 'arraybuffer', peer: null, q: Promise.resolve(), sent: 0, bs: 0, recv: 0, br: 0 }, o));
    }
    get bufferedAmountLowThreshold() { return this._.threshold; }
    set bufferedAmountLowThreshold(v) { this._.threshold = Number(v) >>> 0; }
    get binaryType() { return this._.binaryType; }
    set binaryType(v) { if (v === 'blob' || v === 'arraybuffer') this._.binaryType = v; }
    get reliable() { return undefined; }
    send(data) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'send' on 'RTCDataChannel': 1 argument required, but only 0 present.");
        if (this._.readyState !== 'open') throw rtcErr('InvalidStateError', "Failed to execute 'send' on 'RTCDataChannel': RTCDataChannel.readyState is not 'open'");
        let size, bin, str;
        if (data instanceof Blob) { size = data.size; bin = true; }
        else if (data instanceof ArrayBuffer || Object.prototype.toString.call(data) === '[object ArrayBuffer]') { size = data.byteLength; bin = true; data = new Uint8Array(new Uint8Array(data)).buffer; }
        else if (ArrayBuffer.isView(data)) { size = data.byteLength; bin = true; data = new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice().buffer; }
        else { str = String(data); size = rtcU8(str); }
        const sctp = this._.pc._.sctp;
        if (sctp && size > sctp._.maxMessageSize) throw new TypeError('Message too large.');
        this._.bufferedAmount += size; this._.sent++; this._.bs += size;
        const blob = data instanceof Blob ? data : null;
        this._.q = this._.q.then(async () => {
            const payload = blob ? await blob.arrayBuffer() : bin ? data : str;
            await new Promise(r => rtcTask(r));
            const before = this._.bufferedAmount;
            this._.bufferedAmount -= size;
            const peer = this._.peer;
            if (peer && peer._.readyState === 'open') {
                peer._.recv++; peer._.br += size;
                const d = typeof payload === 'string' ? payload : peer._.binaryType === 'blob' ? new Blob([payload]) : payload;
                rtcFire(peer, new MessageEvent('message', { data: d }));
            }
            if (before > this._.threshold && this._.bufferedAmount <= this._.threshold && this._.readyState === 'open') rtcEv(this, 'bufferedamountlow');
        });
    }
    close() {
        const s = this._.readyState;
        if (s === 'closing' || s === 'closed') return;
        this._.readyState = 'closing';
        const peer = this._.peer;
        this._.q.then(() => rtcTask(() => {
            if (peer && peer._.readyState === 'open') { peer._.readyState = 'closing'; rtcEv(peer, 'closing'); }
            rtcTask(() => { this._closed(); if (peer) peer._closed(); });
        }));
    }
    _open() { if (this._.readyState !== 'connecting') return; this._.readyState = 'open'; this._.pc._.dcOpened++; rtcEv(this, 'open'); }
    _closed(err) {
        if (this._.readyState === 'closed') return;
        this._.readyState = 'closed'; this._.pc._.dcClosed++;
        if (err) rtcFire(this, new RTCErrorEvent('error', { error: new RTCError({ errorDetail: 'sctp-failure' }, err) }));
        rtcEv(this, 'close');
    }
}
rtcRO(RTCDataChannel, ['label', 'ordered', 'maxPacketLifeTime', 'maxRetransmits', 'protocol', 'negotiated', 'id', 'readyState', 'bufferedAmount', 'priority']);
rtcOn(RTCDataChannel, ['open', 'bufferedamountlow', 'error', 'closing', 'close', 'message']);
delete RTCDataChannel.prototype.reliable;
class RTCDataChannelEvent extends Event {
    constructor(t, i) { if (!i || !(i.channel instanceof RTCDataChannel)) throw new TypeError("Failed to construct 'RTCDataChannelEvent': required member channel is undefined."); super(t, i); def(this, '_', { channel: i.channel }); }
}
rtcRO(RTCDataChannelEvent, ['channel']);
class RTCTrackEvent extends Event {
    constructor(t, i) {
        if (!i || !(i.receiver instanceof RTCRtpReceiver) || !(i.track instanceof MediaStreamTrack) || !(i.transceiver instanceof RTCRtpTransceiver)) throw new TypeError("Failed to construct 'RTCTrackEvent': required member is undefined.");
        super(t, i);
        def(this, '_', { receiver: i.receiver, track: i.track, streams: Object.freeze([...(i.streams || [])]), transceiver: i.transceiver });
    }
}
rtcRO(RTCTrackEvent, ['receiver', 'track', 'streams', 'transceiver']);

/* --- SDP --- */
const rtcParseSdp = sdp => {
    if (typeof sdp !== 'string' || !/^v=0\r?\n/.test(sdp)) return null;
    const lines = sdp.split(/\r?\n/).filter(l => l);
    const sess = { attrs: [], media: [] };
    let cur = null;
    for (const l of lines) {
        if (!/^[a-z]=/.test(l)) return null;
        if (l.startsWith('m=')) {
            const p = l.slice(2).split(' ');
            if (p.length < 3) return null;
            cur = { kind: p[0], port: +p[1], proto: p[2], fmts: p.slice(3), attrs: [] };
            sess.media.push(cur);
        } else if (l.startsWith('a=')) (cur || sess).attrs.push(l.slice(2));
    }
    const get = (o, k) => { const a = o.attrs.find(x => x === k || x.startsWith(k + ':')); return a === undefined ? undefined : a.includes(':') ? a.slice(a.indexOf(':') + 1) : ''; };
    for (const m of sess.media) {
        m.mid = get(m, 'mid') ?? null;
        m.dir = RTC_DIRS.slice(0, 4).find(d => m.attrs.includes(d)) || get(sess, 'sendrecv') !== undefined && 'sendrecv' || 'sendrecv';
        m.ufrag = get(m, 'ice-ufrag') ?? get(sess, 'ice-ufrag') ?? null;
        m.pwd = get(m, 'ice-pwd') ?? get(sess, 'ice-pwd') ?? null;
        m.setup = get(m, 'setup') ?? get(sess, 'setup') ?? null;
        m.fp = get(m, 'fingerprint') ?? get(sess, 'fingerprint') ?? null;
        m.msids = m.attrs.filter(a => a.startsWith('msid:')).map(a => a.slice(5).split(' '));
        m.cands = m.attrs.filter(a => a.startsWith('candidate:'));
        m.rtpmap = m.attrs.filter(a => a.startsWith('rtpmap:')).map(a => { const [pt, r] = a.slice(7).split(' '); const [name, rate, ch] = (r || '').split('/'); return { pt: +pt, name, rate: +rate, ch: ch ? +ch : undefined }; });
        m.fmtp = Object.fromEntries(m.attrs.filter(a => a.startsWith('fmtp:')).map(a => { const i = a.indexOf(' '); return [+a.slice(5, i), a.slice(i + 1)]; }));
        m.trickle = (get(m, 'ice-options') ?? get(sess, 'ice-options') ?? '').split(' ').includes('trickle');
        m.ssrcs = m.attrs.filter(a => a.startsWith('ssrc:'));
    }
    return sess;
};
const rtcCodecsOf = m => m.rtpmap.map(r => { const c = { payloadType: r.pt, mimeType: `${m.kind}/${r.name}`, clockRate: r.rate }; if (r.ch) c.channels = r.ch; else if (m.kind === 'audio') c.channels = 1; if (m.fmtp[r.pt]) c.sdpFmtpLine = m.fmtp[r.pt]; return c; });

let rtcSessSeq = 0;
class RTCPeerConnection extends EventTarget {
    constructor(cfg = undefined) {
        super();
        const c = RTCPeerConnection._cfg(cfg);
        const ice = new RTCIceTransport(RTC_INTERNAL);
        const dtls = new RTCDtlsTransport(RTC_INTERNAL, ice);
        if (c.certificates.some(x => x.expires <= Date.now())) throw rtcErr('InvalidAccessError', 'Certificate expired.');
        def(this, '_', {
            cfg: c, signalingState: 'stable', iceGatheringState: 'new', iceConnectionState: 'new', connectionState: 'new', closed: false,
            curLocal: null, pendLocal: null, curRemote: null, pendRemote: null, lastOffer: null, lastAnswer: null,
            transceivers: [], channels: [], order: [], dataMid: null, dataInOrder: false, ice, dtls, sctp: null, dtlsReady: false,
            ufrag: rtcRand(4), pwd: rtcRand(24), cert: c.certificates[0] || new RTCCertificate(RTC_INTERNAL, Date.now() + 2592e6), cname: rtcRand(16),
            sessId: String(Date.now()) + (++rtcSessSeq), sessVer: 1, ops: [], nn: false, nnLater: false, restart: false, localCands: [], gatherGen: 0,
            remoteCands: [], canTrickle: null, connected: false, peer: null, role: null, streams: new Map(), dcOpened: 0, dcClosed: 0, nextMid: 0, gatheredFor: null,
        });
        rtcPeers.set(this._.ufrag, this);
    }
    static _cfg(cfg, old) {
        if (cfg === undefined || cfg === null) cfg = {};
        else if (typeof cfg !== 'object' && typeof cfg !== 'function') throw new TypeError("Failed to construct 'RTCPeerConnection': parameter 1 is not of type 'RTCConfiguration'.");
        const en = (k, vals, d) => { const v = cfg[k]; if (v === undefined) return d; const s = String(v); if (!vals.includes(s)) throw new TypeError(`The provided value '${s}' is not a valid enum value of type ${k}.`); return s; };
        const out = {
            bundlePolicy: en('bundlePolicy', ['balanced', 'max-compat', 'max-bundle'], 'balanced'),
            certificates: (() => { const v = cfg.certificates; if (v === undefined) return []; if (v === null || typeof v[Symbol.iterator] !== 'function') throw new TypeError('certificates is not iterable.'); const a = [...v]; for (const x of a) if (!(x instanceof RTCCertificate)) throw new TypeError("Failed to convert value to 'RTCCertificate'."); return a; })(),
            iceCandidatePoolSize: cfg.iceCandidatePoolSize === undefined ? 0 : rtcRange(cfg.iceCandidatePoolSize, 255, 'iceCandidatePoolSize'),
            iceServers: (() => {
                const v = cfg.iceServers; if (v === undefined) return [];
                if (v === null || typeof v !== 'object' || typeof v[Symbol.iterator] !== 'function') throw new TypeError('iceServers is not iterable.');
                return [...v].map(s => {
                    if (s === null || typeof s !== 'object') throw new TypeError("Failed to convert value to 'RTCIceServer'.");
                    if (s.urls === undefined) throw new TypeError("Failed to read the 'urls' property from 'RTCIceServer': Required member is undefined.");
                    const urls = typeof s.urls === 'object' && s.urls && typeof s.urls[Symbol.iterator] === 'function' ? [...s.urls].map(String) : [String(s.urls)];
                    const o = { urls }; if (s.username !== undefined) o.username = String(s.username); if (s.credential !== undefined) o.credential = String(s.credential);
                    return o;
                });
            })(),
            iceTransportPolicy: en('iceTransportPolicy', ['relay', 'all'], 'all'),
            rtcpMuxPolicy: en('rtcpMuxPolicy', ['require'], 'require'),
        };
        for (const s of out.iceServers) {
            const urls = s.urls;
            if (!urls.length) throw rtcErr('SyntaxError', 'ICE server urls is empty.');
            for (const u of urls) if (/[\\#]/.test(u)) throw rtcErr('SyntaxError', `Invalid ICE server URL '${u}'.`);
            for (const u of urls) {
                const m = /^(stuns?|turns?):([^?]*)(\?.*)?$/i.exec(u);
                if (!m || !m[2] || /[\s/@]/.test(m[2])) throw rtcErr('SyntaxError', `Invalid ICE server URL '${u}'.`);
                const sch = m[1].toLowerCase(), q = m[3];
                if (sch.startsWith('stun') && q) throw rtcErr('SyntaxError', `Invalid STUN URL '${u}'.`);
                if (sch.startsWith('turn') && q && !/^\?transport=(udp|tcp)$/i.test(q)) throw rtcErr('SyntaxError', `Invalid TURN URL '${u}'.`);
                const hp = /^(\[[0-9a-fA-F:.]+\]|[^:\[\]]+)(:(\d{1,5}))?$/.exec(m[2]);
                if (!hp || (hp[3] !== undefined && (+hp[3] < 1 || +hp[3] > 65535))) throw rtcErr('SyntaxError', `Invalid ICE server host in '${u}'.`);
                if (sch.startsWith('turn')) {
                    if (s.username === undefined || s.credential === undefined) throw rtcErr('InvalidAccessError', 'TURN server requires username and credential.');
                    if (rtcU8(s.username) > 509) throw rtcErr('InvalidAccessError', 'TURN username too long.');
                    if (s.credential === '') throw rtcErr('InvalidAccessError', 'TURN credential is empty.');
                }
            }
        }
        if (old) {
            if (cfg.certificates !== undefined && (out.certificates.length !== old.certificates.length || out.certificates.some((x, i) => x !== old.certificates[i]))) throw rtcErr('InvalidModificationError', 'Certificates can not be changed.');
            out.certificates = old.certificates;
            if (out.bundlePolicy !== old.bundlePolicy) throw rtcErr('InvalidModificationError', 'bundlePolicy can not be changed.');
            if (out.rtcpMuxPolicy !== old.rtcpMuxPolicy) throw rtcErr('InvalidModificationError', 'rtcpMuxPolicy can not be changed.');
        }
        return out;
    }
    static generateCertificate(alg) {
        return new Promise((res, rej) => {
            let name = typeof alg === 'string' ? alg : alg && alg.name;
            name = String(name || '').toUpperCase();
            let ok = false;
            if (alg && typeof alg === 'object') {
                if (name === 'ECDSA') ok = String(alg.namedCurve).toUpperCase() === 'P-256';
                else if (name === 'RSASSA-PKCS1-V1_5') { const e = alg.publicExponent; ok = (alg.modulusLength === undefined || +alg.modulusLength >= 1024) && (!e || [...e].join() === '1,0,1') && (alg.hash === undefined || /^sha-256$/i.test(alg.hash.name || alg.hash)); }
            }
            if (!ok) return rej(rtcErr('NotSupportedError', 'The 1st argument provided is an AlgorithmIdentifier, but the algorithm is not supported.'));
            const exp = alg.expires !== undefined ? Math.min(+alg.expires, 31536e6) : 2592e6;
            rtcTask(() => res(new RTCCertificate(RTC_INTERNAL, Date.now() + exp)));
        });
    }
    get localDescription() { return this._desc(this._.pendLocal || this._.curLocal, true); }
    get currentLocalDescription() { return this._desc(this._.curLocal, true); }
    get pendingLocalDescription() { return this._desc(this._.pendLocal, true); }
    get remoteDescription() { return this._.pendRemote || this._.curRemote; }
    get currentRemoteDescription() { return this._.curRemote; }
    get pendingRemoteDescription() { return this._.pendRemote; }
    get signalingState() { return this._.signalingState; }
    get iceGatheringState() { return this._.iceGatheringState; }
    get iceConnectionState() { return this._.iceConnectionState; }
    get connectionState() { return this._.connectionState; }
    get canTrickleIceCandidates() { return this._.canTrickle; }
    get sctp() { return this._.sctp; }
    _desc(d, local) {
        if (!d) return null;
        if (!local || !this._.localCands.length && this._.iceGatheringState !== 'complete') return d;
        const key = d.sdp + '|' + this._.localCands.length + this._.iceGatheringState;
        if (d._cacheKey === key) return d._cache;
        let first = true;
        const sdp = d.sdp.replace(/(a=mid:[^\r\n]*\r\n)/g, m => { if (!first) return m; first = false; return m + this._.localCands.map(c => 'a=' + c.candidate + '\r\n').join('') + (this._.iceGatheringState === 'complete' ? 'a=end-of-candidates\r\n' : ''); });
        const r = new RTCSessionDescription({ type: d.type, sdp });
        def(d, '_cacheKey', key); def(d, '_cache', r);
        return r;
    }
    getConfiguration() { const c = this._.cfg; return { iceServers: c.iceServers.map(s => Object.assign({}, s, { urls: Array.isArray(s.urls) ? s.urls.slice() : s.urls })), iceTransportPolicy: c.iceTransportPolicy, bundlePolicy: c.bundlePolicy, rtcpMuxPolicy: c.rtcpMuxPolicy, iceCandidatePoolSize: c.iceCandidatePoolSize, certificates: c.certificates.slice() }; }
    setConfiguration(cfg = undefined) {
        if (this._.closed) throw rtcErr('InvalidStateError');
        const c = RTCPeerConnection._cfg(cfg, this._.cfg);
        if (c.iceCandidatePoolSize !== this._.cfg.iceCandidatePoolSize && (this._.curLocal || this._.pendLocal)) throw rtcErr('InvalidModificationError', 'iceCandidatePoolSize can not be changed after setLocalDescription.');
        this._.cfg = c;
    }
    _chain(fn) {
        if (this._.closed) return Promise.reject(rtcErr('InvalidStateError', 'The RTCPeerConnection is closed.'));
        const run = () => {
            let p;
            try { p = Promise.resolve(fn()); } catch (e) { p = Promise.reject(e); }
            return p.then(v => { if (this._.closed) return new Promise(() => {}); return v; }, e => { if (this._.closed) return new Promise(() => {}); throw e; })
                .finally(() => { this._.ops.shift(); if (this._.ops.length) this._.ops[0](); else if (this._.nnLater) { this._.nnLater = false; this._updateNeg(); } });
        };
        return new Promise((res, rej) => {
            const go = () => run().then(res, rej);
            this._.ops.push(go);
            if (this._.ops.length === 1) go();
        });
    }
    _sigState(s) { if (this._.signalingState === s) return; this._.signalingState = s; rtcEv(this, 'signalingstatechange'); }
    _assocSections(remoteSdp) {
        /* sections for an offer: negotiated ones keep their place, then new ones in creation order */
        const prev = this._.curLocal || this._.curRemote ? rtcParseSdp((this._.curLocal || this._.curRemote).sdp) : null;
        const secs = [];
        if (prev) for (const m of prev.media) {
            if (m.kind === 'application') { secs.push({ kind: 'application', mid: m.mid }); continue; }
            const t = this._.transceivers.find(x => x._.mid === m.mid);
            secs.push(t ? { kind: m.kind, mid: m.mid, t } : { kind: m.kind, mid: m.mid, dead: true });
        }
        for (const o of this._.order) {
            if (o === 'data') { if (!secs.some(s => s.kind === 'application')) secs.push({ kind: 'application', mid: this._.dataMid }); continue; }
            if (o._.stopped || secs.some(s => s.t === o)) continue;
            if (o._.stopping && !o._.mid) continue;
            secs.push({ kind: o._.kind, mid: o._.mid, t: o });
        }
        return secs;
    }
    _freshMid() { const used = new Set([...this._.transceivers.map(t => t._.mid), this._.dataMid].filter(x => x !== null)); for (const d of [this._.curLocal, this._.curRemote, this._.pendRemote]) if (d) for (const m of rtcParseSdp(d.sdp)?.media || []) used.add(m.mid); while (used.has(String(this._.nextMid))) this._.nextMid++; return String(this._.nextMid++); }
    _codecList(t) { const all = RTC_CODECS[t._.kind]; return t._.prefs ? t._.prefs.concat(all.filter(c => /\/rtx$/.test(c.mimeType) && t._.prefs.some(p => c.sdpFmtpLine === 'apt=' + p.pt))) : all; }
    _codecsFor(x) {
        const t = this._.transceivers.find(k => k.sender === x || k.receiver === x);
        if (!t || !t._.mid) return [];
        const d = x instanceof RTCRtpSender ? this._.curRemote : this._.curLocal;
        const m = d && rtcParseSdp(d.sdp)?.media.find(k => k.mid === t._.mid);
        return m ? rtcCodecsOf(m) : [];
    }
    _sdp(type, secs, setup) {
        const ufrag = this._.ufrag, pwd = this._.pwd, fp = this._.cert._.fp;
        const live = secs.filter(s => !s.rejected);
        let s = `v=0\r\no=- ${this._.sessId} ${this._.sessVer++} IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n`;
        if (live.length) s += `a=group:BUNDLE ${live.map(x => x.mid).join(' ')}\r\n`;
        s += 'a=extmap-allow-mixed\r\na=msid-semantic: WMS\r\n';
        for (const sec of secs) {
            const port = sec.rejected ? 0 : 9;
            if (sec.kind === 'application') {
                s += `m=application ${port} UDP/DTLS/SCTP webrtc-datachannel\r\nc=IN IP4 0.0.0.0\r\na=ice-ufrag:${ufrag}\r\na=ice-pwd:${pwd}\r\na=ice-options:trickle\r\na=fingerprint:sha-256 ${fp}\r\na=setup:${setup}\r\na=mid:${sec.mid}\r\na=sctp-port:5000\r\na=max-message-size:262144\r\n`;
                continue;
            }
            const t = sec.t, codecs = sec.codecs || (t ? this._codecList(t) : RTC_CODECS[sec.kind]);
            s += `m=${sec.kind} ${port} UDP/TLS/RTP/SAVPF ${codecs.map(c => c.pt).join(' ')}\r\nc=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:${ufrag}\r\na=ice-pwd:${pwd}\r\na=ice-options:trickle\r\na=fingerprint:sha-256 ${fp}\r\na=setup:${setup}\r\na=mid:${sec.mid}\r\n`;
            RTC_HDREXT[sec.kind].forEach((u, i) => { s += `a=extmap:${i + 1} ${u}\r\n`; });
            const dir = sec.rejected ? 'inactive' : sec.dir;
            s += `a=${dir}\r\n`;
            const snd = t && t._.sender;
            if (snd && rtcHasSend(dir)) { const ids = snd._.streams.length ? snd._.streams.map(x => x.id) : ['-']; const tid = snd._.track ? snd._.track.id : snd._.tid0 || (snd._.tid0 = rtcUUID()); for (const id of ids) s += `a=msid:${id} ${tid}\r\n`; }
            s += 'a=rtcp-mux\r\n' + (sec.kind === 'video' ? 'a=rtcp-rsize\r\n' : '');
            for (const c of codecs) { s += `a=rtpmap:${c.pt} ${c.mimeType.split('/')[1]}/${c.clockRate}${c.channels > 1 ? '/' + c.channels : ''}\r\n`; if (c.sdpFmtpLine) s += `a=fmtp:${c.pt} ${c.sdpFmtpLine}\r\n`; }
            if (snd && rtcHasSend(dir)) s += `a=ssrc:${snd._.ssrc} cname:${this._.cname}\r\n`;
        }
        return s;
    }
    createOffer(opts) { return this._chain(() => { const sdp = this._offerImpl(opts); return new Promise(r => rtcTask(() => r({ type: 'offer', sdp }))); }); }
    _offerImpl(opts) {
        {
            const st = this._.signalingState;
            if (st !== 'stable' && st !== 'have-local-offer') throw rtcErr('InvalidStateError', `createOffer in state ${st}`);
            if (opts && opts.iceRestart) this._.restart = true;
            if (opts && typeof opts === 'object') for (const [k, kind] of [['offerToReceiveAudio', 'audio'], ['offerToReceiveVideo', 'video']]) {
                if (opts[k] === undefined) continue;
                const want = !!opts[k];
                const ts = this._.transceivers.filter(t => t._.kind === kind && !t._.stopping);
                if (want && !ts.some(t => rtcHasRecv(t._.direction))) { const t = ts.find(t => !t._.stopping); if (t) t._.direction = rtcMkDir(rtcHasSend(t._.direction), true); else this._addTr(kind, null, 'recvonly'); }
                if (!want) for (const t of ts) t._.direction = rtcMkDir(rtcHasSend(t._.direction), false);
            }
            if (this._.restart) { this._.restartUfrag = rtcRand(4); }
            const secs = this._assocSections().map(s => {
                if (s.dead) return Object.assign(s, { rejected: true });
                const mid = s.mid ?? (s._mid = this._freshMid());
                if (s.kind === 'application') return Object.assign(s, { mid });
                if (s.t._.mid === null) s.t._.pendMid = mid;
                return Object.assign(s, { mid, dir: s.t._.direction, rejected: s.t._.stopping });
            });
            for (const s of secs) if (s.kind === 'application' && this._.dataMid === null) this._.pendDataMid = s.mid;
            const ufrag0 = this._.ufrag; if (this._.restart) { this._.ufrag = this._.restartUfrag; this._.pwd0 = this._.pwd; }
            const sdp = this._sdp('offer', secs, 'actpass');
            if (this._.restart) { this._.offerUfrag = this._.ufrag; this._.ufrag = ufrag0; }
            this._.lastOffer = sdp;
            return sdp;
        }
    }
    createAnswer() { return this._chain(() => { const sdp = this._answerImpl(); return new Promise(r => rtcTask(() => r({ type: 'answer', sdp }))); }); }
    _answerImpl() {
        {
            const st = this._.signalingState;
            if (st !== 'have-remote-offer' && st !== 'have-local-pranswer') throw rtcErr('InvalidStateError', `createAnswer in state ${st}`);
            const off = rtcParseSdp(this._.pendRemote.sdp);
            const secs = off.media.map(m => {
                if (m.kind === 'application') return { kind: 'application', mid: m.mid, rejected: m.port === 0 };
                const t = this._.transceivers.find(x => x._.mid === m.mid);
                if (!t || t._.stopping || m.port === 0 || !RTC_CODECS[m.kind]) return { kind: m.kind, mid: m.mid, rejected: true, codecs: [] , t };
                const offered = rtcCodecsOf(m), mine = this._codecList(t);
                let codecs = mine.filter(c => offered.some(o => o.mimeType.toLowerCase() === c.mimeType.toLowerCase()));
                if (!codecs.length) codecs = mine;
                return { kind: m.kind, mid: m.mid, t, codecs, dir: rtcMkDir(rtcHasSend(t._.direction) && rtcHasRecv(m.dir), rtcHasRecv(t._.direction) && rtcHasSend(m.dir)) };
            });
            const setup = (off.media[0] && off.media[0].setup === 'active') ? 'passive' : 'active';
            const sdp = this._sdp('answer', secs, setup);
            this._.lastAnswer = sdp;
            return sdp;
        }
    }
    setLocalDescription(desc) {
        const pc = this;
        return this._chain(async () => {
            let type = desc && desc.type !== undefined ? String(desc.type) : undefined, sdp = desc && desc.sdp ? String(desc.sdp) : '';
            if (type !== undefined && !RTC_SDP_TYPES.includes(type)) throw new TypeError(`The provided value '${type}' is not a valid enum value of type RTCSdpType.`);
            const st = this._.signalingState;
            if (type === undefined) type = ['stable', 'have-local-offer', 'have-remote-pranswer'].includes(st) ? 'offer' : 'answer';
            if (type === 'rollback') {
                if (st !== 'have-local-offer' && st !== 'have-local-pranswer') throw rtcErr('InvalidStateError', `Rollback in state ${st}`);
                this._rollbackLocal(); return;
            }
            if (type === 'offer') {
                if (st !== 'stable' && st !== 'have-local-offer') throw rtcErr('InvalidStateError', `setLocalDescription(offer) in state ${st}`);
                if (!sdp) { if (!this._.lastOffer) this._offerImpl(); sdp = this._.lastOffer; }
                if (sdp !== this._.lastOffer) throw rtcErr('InvalidModificationError', 'The SDP does not match the previously generated SDP for this type');
            } else {
                if (st !== 'have-remote-offer' && st !== 'have-local-pranswer') throw rtcErr('InvalidStateError', `setLocalDescription(${type}) in state ${st}`);
                if (!sdp) { if (!this._.lastAnswer) this._answerImpl(); sdp = this._.lastAnswer; }
                if (sdp !== this._.lastAnswer) throw rtcErr('InvalidModificationError', 'The SDP does not match the previously generated SDP for this type');
            }
            await new Promise(r => rtcTask(r));
            if (this._.closed) return;
            const d = new RTCSessionDescription({ type, sdp });
            const parsed = rtcParseSdp(sdp);
            if (type === 'offer') {
                this._.prevPendLocal = this._.pendLocal;
                this._.pendLocal = d;
                for (const t of this._.transceivers) if (t._.mid === null && t._.pendMid !== undefined) { t._.mid = t._.pendMid; t._.midFromSLD = true; }
                if (this._.pendDataMid !== undefined && this._.dataMid === null) { this._.dataMid = this._.pendDataMid; this._.dataMidFromSLD = true; }
                if (this._.restart && this._.offerUfrag) { this._.ufrag = this._.offerUfrag; rtcPeers.set(this._.ufrag, this); this._.restart = false; this._.restartPending = true; }
                this._ensureSctp(parsed);
                this._.dtlsReady = true;
                this._sigState('have-local-offer');
            } else {
                const offer = this._.pendRemote;
                if (type === 'answer') { this._.curLocal = d; this._.curRemote = offer; this._.pendLocal = this._.pendRemote = null; }
                else this._.pendLocal = d;
                this._applyAnswerDirs(parsed, false);
                this._.role = (parsed.media[0] && parsed.media[0].setup) === 'passive' ? 'server' : 'client';
                if (this._.ice._.role === 'unknown') this._.ice._.role = 'controlled';
                this._ensureSctp(parsed);
                if (type === 'answer') { this._.lastOffer = this._.lastAnswer = null; this._.nn = false; }
                this._sigState(type === 'answer' ? 'stable' : 'have-local-pranswer');
            }
            this._gather();
            this._tryConnect();
            if (this._.signalingState === 'stable') this._updateNeg();
        });
    }
    _ensureSctp(parsed) {
        if (this._.sctp || !parsed.media.some(m => m.kind === 'application' && m.port !== 0)) return;
        this._.sctp = new RTCSctpTransport(RTC_INTERNAL, this._.dtls);
    }
    _applyAnswerDirs(parsed, remote) {
        for (const m of parsed.media) {
            const t = this._.transceivers.find(x => x._.mid === m.mid);
            if (!t) continue;
            if (m.port === 0) { t._.stopped = true; t._.stopping = true; t._.current = 'stopped'; t._.receiver._.track._.readyState = 'ended'; continue; }
            const dir = remote ? rtcRev(m.dir) : m.dir;
            t._.current = dir;
            if (remote) this._trackEvents(t, m, rtcHasRecv(dir));
            if (t._.stopping) { t._.stopped = true; t._.current = 'stopped'; }
        }
        this._.dtlsReady = true;
    }
    _trackEvents(t, m, recv) {
        const r = t._.receiver;
        if (recv) {
            const ids = (m.msids || []).map(x => x[0]).filter(x => x !== '-');
            const streams = ids.map(id => { let s = this._.streams.get(id); if (!s) { s = new MediaStream(); s._.id = id; this._.streams.set(id, s); } return s; });
            for (const s of r._.streams) if (!streams.includes(s)) { s.removeTrack(r._.track); rtcFire(s, new MediaStreamTrackEvent('removetrack', { track: r._.track })); }
            for (const s of streams) if (!s._.tracks.includes(r._.track)) s.addTrack(r._.track);
            r._.streams = streams;
            if (!t._.fired) { t._.fired = true; rtcFire(this, new RTCTrackEvent('track', { receiver: r, track: r._.track, streams, transceiver: t })); }
        } else if (t._.fired) {
            t._.fired = false;
            for (const s of r._.streams) { s.removeTrack(r._.track); rtcFire(s, new MediaStreamTrackEvent('removetrack', { track: r._.track })); }
            r._.streams = [];
            r._.track._mute(true);
        }
    }
    setRemoteDescription(desc) {
        return this._chain(async () => {
            if (!desc || desc.type === undefined) throw new TypeError("Failed to execute 'setRemoteDescription': required member type is undefined.");
            const type = String(desc.type);
            if (!RTC_SDP_TYPES.includes(type)) throw new TypeError(`The provided value '${type}' is not a valid enum value of type RTCSdpType.`);
            const st = this._.signalingState;
            if (type === 'rollback') {
                if (st !== 'have-remote-offer' && st !== 'have-remote-pranswer') throw rtcErr('InvalidStateError', `Rollback in state ${st}`);
                await new Promise(r => rtcTask(r));
                this._rollbackRemote(); return;
            }
            const ok = type === 'offer' ? st === 'stable' || st === 'have-remote-offer' : st === 'have-local-offer' || st === 'have-remote-pranswer';
            const implicitRollback = !ok && type === 'offer' && st === 'have-local-offer';
            if (!ok && !implicitRollback) {
                throw rtcErr('InvalidStateError', `Failed to set remote ${type} sdp: Called in wrong state: ${st}`);
            }
            const sdp = desc.sdp === undefined || desc.sdp === null ? '' : String(desc.sdp);
            const parsed = rtcParseSdp(sdp);
            if (!parsed) throw new RTCError({ errorDetail: 'sdp-syntax-error' }, 'Failed to parse SessionDescription.');
            if (/a=ssrc-group:SIM|a=simulcast/.test(sdp) === false && parsed.media.some(m => m.msids.length > 1 && m.ssrcs.length > 2 && m.kind !== 'application' && /plan-b/i.test(sdp))) throw rtcErr('OperationError', 'Plan B is not supported.');
            for (const m of parsed.media) if (m.kind !== 'application' && m.mid === null) throw rtcErr('InvalidAccessError', 'Media section without mid.');
            if (parsed.media.some(m => m.port !== 0 && (!m.ufrag || !m.pwd))) throw rtcErr('InvalidAccessError', 'Missing ICE credentials.');
            if (parsed.media.some(m => m.port !== 0 && !m.fp)) throw rtcErr('InvalidAccessError', 'Missing DTLS fingerprint.');
            if (type !== 'offer') {
                const local = rtcParseSdp((this._.pendLocal || this._.curLocal).sdp);
                if (parsed.media.length !== local.media.length || parsed.media.some((m, i) => m.mid !== local.media[i].mid)) throw rtcErr('InvalidAccessError', 'Answer m-lines do not match offer.');
            }
            await new Promise(r => rtcTask(r));
            if (this._.closed) return;
            if (implicitRollback) { this._rollbackLocal(true); await new Promise(r => rtcTask(r)); if (this._.closed) return; }
            const d = new RTCSessionDescription({ type, sdp });
            const first = parsed.media.find(m => m.port !== 0) || parsed.media[0];
            this._.remoteUfrag = first ? first.ufrag : null;
            this._.canTrickle = parsed.media.some(m => m.trickle);
            for (const m of parsed.media) for (const c of m.cands) this._.remoteCands.push(c);
            if (type === 'offer') {
                this._.prevPendRemote = this._.pendRemote;
                this._.pendRemote = d;
                this._.srdCreated = [];
                parsed.media.forEach((m, i) => {
                    if (m.kind === 'application') { if (this._.dataMid === null) { this._.dataMid = m.mid; this._.dataMidFromSRD = true; } return; }
                    if (!RTC_CODECS[m.kind]) return;
                    let t = this._.transceivers.find(x => x._.mid === m.mid);
                    if (!t && m.port !== 0) {
                        t = this._.transceivers.find(x => x._.mid === null && x._.kind === m.kind && x._.addTrack && !x._.stopping);
                        if (t) { t._.mid = m.mid; t._.midFromSRD = true; }
                        else { t = this._addTr(m.kind, null, 'recvonly', true); t._.mid = m.mid; t._.fromSRD = true; this._.srdCreated.push(t); }
                    }
                    if (!t) return;
                    if (m.port === 0) { if (!t._.stopped) { t._.stopping = true; t._.stopped = true; t._.current = 'stopped'; t._.receiver._.track._.readyState = 'ended'; } return; }
                    this._trackEvents(t, m, rtcHasSend(m.dir));
                    t._.remoteDir = m.dir;
                });
                this._ensureSctp(parsed);
                this._.dtlsReady = true;
                this._sigState('have-remote-offer');
            } else {
                if (type === 'answer') { this._.curRemote = d; this._.curLocal = this._.pendLocal; this._.pendLocal = this._.pendRemote = null; this._.lastOffer = null; }
                else this._.pendRemote = d;
                this._applyAnswerDirs(parsed, true);
                this._.role = (first && first.setup) === 'active' ? 'server' : 'client';
                if (this._.ice._.role === 'unknown') this._.ice._.role = 'controlling';
                this._ensureSctp(parsed);
                if (type === 'answer') this._.nn = false;
                this._sigState(type === 'answer' ? 'stable' : 'have-remote-pranswer');
            }
            this._tryConnect();
            if (this._.signalingState === 'stable') this._updateNeg();
        });
    }
    _rollbackLocal(implicit) {
        for (const t of this._.transceivers) if (t._.midFromSLD && !this._.curLocal?.sdp.includes('a=mid:' + t._.mid + '\r\n')) { t._.mid = null; t._.midFromSLD = false; }
        if (this._.dataMidFromSLD) { this._.dataMid = null; this._.dataMidFromSLD = false; }
        if (this._.restartPending) { this._.restartPending = false; this._.restart = true; }
        this._.pendLocal = null; this._.lastOffer = null;
        if (!this._.curLocal && !this._.curRemote) { this._.dtlsReady = false; this._.sctp = null; }
        if (!this._.curLocal && !this._.curRemote && this._.iceGatheringState !== 'new') { this._.localCands = []; this._.iceGatheringState = 'new'; this._.ice._set('gatheringState', 'new'); this._.gatherGen++; rtcEv(this, 'icegatheringstatechange'); }
        this._sigState('stable');
        if (!implicit) { this._.nn = false; this._updateNeg(); }
    }
    _rollbackRemote() {
        for (const t of this._.srdCreated || []) {
            const r = t._.receiver;
            if (t._.fired) { t._.fired = false; for (const s of r._.streams) { s.removeTrack(r._.track); rtcFire(s, new MediaStreamTrackEvent('removetrack', { track: r._.track })); } r._.streams = []; r._.track._mute(true); }
            if (t._.addTrackLater) { t._.mid = null; t._.fromSRD = false; }
            else this._.transceivers = this._.transceivers.filter(x => x !== t);
        }
        for (const t of this._.transceivers) { if (t._.midFromSRD) { t._.mid = null; t._.midFromSRD = false; } if (!this._.curRemote && t._.fired) { t._.fired = false; const r = t._.receiver; for (const s of r._.streams) { s.removeTrack(r._.track); rtcFire(s, new MediaStreamTrackEvent('removetrack', { track: r._.track })); } r._.streams = []; r._.track._mute(true); } }
        this._.order = this._.order.filter(o => o === 'data' || this._.transceivers.includes(o));
        if (this._.dataMidFromSRD) { this._.dataMid = null; this._.dataMidFromSRD = false; }
        this._.srdCreated = [];
        this._.pendRemote = null; this._.lastAnswer = null;
        if (!this._.curLocal && !this._.curRemote) { this._.dtlsReady = false; this._.sctp = null; }
        this._sigState('stable');
        this._.nn = false; this._updateNeg();
    }
    _gather() {
        if (this._.gatheredFor === this._.ufrag) return;
        this._.gatheredFor = this._.ufrag;
        const gen = ++this._.gatherGen, ufrag = this._.ufrag;
        const parsed = rtcParseSdp((this._.pendLocal || this._.curLocal).sdp);
        const live = parsed.media.filter(m => m.port !== 0);
        if (!live.length) return;
        const mid = live[0].mid, idx = parsed.media.indexOf(live[0]);
        this._.localCands = [];
        rtcTask(() => {
            if (gen !== this._.gatherGen || this._.closed) return;
            this._.iceGatheringState = 'gathering'; this._.ice._set('gatheringState', 'gathering', 'gatheringstatechange'); rtcEv(this, 'icegatheringstatechange');
            rtcTask(() => {
                if (gen !== this._.gatherGen || this._.closed) return;
                if (this._.cfg.iceTransportPolicy !== 'relay') {
                    const port = 40000 + (Math.random() * 20000 | 0);
                    const c = new RTCIceCandidate({ candidate: `candidate:${Math.random() * 1e9 | 0} 1 udp 2122260223 127.0.0.1 ${port} typ host generation 0 ufrag ${ufrag} network-id 1`, sdpMid: mid, sdpMLineIndex: idx, usernameFragment: ufrag });
                    this._.localCands.push(c); this._.ice._.local.push(c);
                    rtcFire(this, new RTCPeerConnectionIceEvent('icecandidate', { candidate: c }));
                    this._tryConnect();
                }
                rtcTask(() => {
                    if (gen !== this._.gatherGen || this._.closed) return;
                    this._.iceGatheringState = 'complete'; this._.ice._set('gatheringState', 'complete', 'gatheringstatechange'); rtcEv(this, 'icegatheringstatechange');
                    rtcFire(this, new RTCPeerConnectionIceEvent('icecandidate', { candidate: null }));
                });
            });
        });
    }
    addIceCandidate(c = undefined, ok = undefined, bad = undefined) {
        if (c !== undefined && c !== null && typeof c !== 'object') return Promise.reject(new TypeError("Failed to execute 'addIceCandidate': parameter 1 is not of type 'RTCIceCandidateInit'."));
        const cand = c === undefined || c === null ? '' : c.candidate === undefined || c.candidate === null ? (c.candidate === null ? 'null' : '') : String(c.candidate);
        if (cand !== '' && (c.sdpMid === undefined || c.sdpMid === null) && (c.sdpMLineIndex === undefined || c.sdpMLineIndex === null)) return Promise.reject(new TypeError("Failed to execute 'addIceCandidate': Candidate missing values for both sdpMid and sdpMLineIndex"));
        const p = this._chain(async () => {
            const rd = this.remoteDescription;
            if (!rd) throw rtcErr('InvalidStateError', "Failed to execute 'addIceCandidate': The remote description was null");
            const parsed = rtcParseSdp(rd.sdp);
            let m = null;
            if (c && c.sdpMid !== undefined && c.sdpMid !== null) { m = parsed.media.find(x => x.mid === String(c.sdpMid)); if (!m) throw rtcErr('OperationError', 'Unknown sdpMid.'); }
            else if (c && c.sdpMLineIndex !== undefined && c.sdpMLineIndex !== null) { m = parsed.media[+c.sdpMLineIndex]; if (!m) throw rtcErr('OperationError', 'sdpMLineIndex out of range.'); }
            const ufRaw = c ? c.usernameFragment : undefined;
            const uf = ufRaw === undefined || ufRaw === null ? null : String(ufRaw);
            if (uf !== null && !(m ? [m.ufrag] : parsed.media.map(x => x.ufrag)).includes(uf)) throw rtcErr('OperationError', 'Unknown ufrag.');
            let line;
            if (cand !== '') {
                const pc = rtcParseCand(cand);
                if (pc.type === null) throw rtcErr('OperationError', 'Failed to parse candidate.');
                line = 'a=' + cand;
            } else line = 'a=end-of-candidates';
            await new Promise(r => rtcTask(r));
            if (cand !== '') {
                const ice = new RTCIceCandidate({ candidate: cand, sdpMid: m ? m.mid : null, sdpMLineIndex: m ? parsed.media.indexOf(m) : 0, usernameFragment: uf });
                this._.remoteCands.push(cand); this._.ice._.remote.push(ice);
            }
            const target = this._.pendRemote || this._.curRemote;
            const lines = target.sdp.split(/\r?\n/).filter(Boolean), out = [];
            let sec = -1;
            const want = m ? parsed.media.indexOf(m) : cand === '' ? null : 0;
            const flush = () => { if (sec >= 0 && (want === null || want === sec)) out.push(line); };
            for (const l of lines) { if (l.startsWith('m=')) { flush(); sec++; } out.push(l); }
            flush();
            const nd = new RTCSessionDescription({ type: target.type, sdp: out.join('\r\n') + '\r\n' });
            if (this._.pendRemote) this._.pendRemote = nd; else this._.curRemote = nd;
            this._tryConnect();
        });
        if (typeof ok === 'function') p.then(() => ok(), e => typeof bad === 'function' && bad(e));
        return p;
    }
    _tryConnect() {
        const me = this._;
        if (me.closed || me.connected) return;
        if (!(me.curLocal || me.pendLocal) || !this.remoteDescription) return;
        const peer = rtcPeers.get(me.remoteUfrag);
        if (!peer || peer === this || peer._.closed || !(peer._.curLocal || peer._.pendLocal) || !peer.remoteDescription || rtcPeers.get(peer._.remoteUfrag) !== this) return;
        if (!me.remoteCands.length && !peer._.remoteCands.length) return;
        if (!me.localCands.length || !peer._.localCands.length) return;
        if (me.cfg.iceTransportPolicy === 'relay' || peer._.cfg.iceTransportPolicy === 'relay') return;
        for (const pc of [this, peer]) { pc._.connected = true; pc._.peer = pc === this ? peer : this; }
        for (const pc of [this, peer]) pc._connecting();
        rtcTask(() => { for (const pc of [this, peer]) if (!pc._.closed) pc._connectedUp(); rtcTask(() => this._linkChannels(peer)); });
    }
    _iceState(s) { if (this._.iceConnectionState === s) return; this._.iceConnectionState = s; this._.ice._set('state', s === 'connected' ? 'connected' : s, 'statechange'); rtcEv(this, 'iceconnectionstatechange'); }
    _connState(s) { if (this._.connectionState === s) return; this._.connectionState = s; rtcEv(this, 'connectionstatechange'); }
    _connecting() { this._iceState('checking'); this._.dtls._set('connecting'); this._connState('connecting'); }
    _connectedUp() {
        const peer = this._.peer, ice = this._.ice;
        const lc = this._.localCands[0] || null, rc = ice._.remote[0] || (peer._.localCands[0] || null);
        ice._.pair = lc && rc ? { local: lc, remote: rc } : null;
        ice._.lp = { usernameFragment: this._.ufrag, password: this._.pwd }; ice._.rp = { usernameFragment: peer._.ufrag, password: peer._.pwd };
        this._iceState('connected');
        if (ice._.pair) rtcEv(ice, 'selectedcandidatepairchange');
        this._.dtls._.remoteCerts = [peer._.cert._.der];
        this._.dtls._set('connected');
        this._connState('connected');
    }
    _linkChannels(peer) {
        for (const pc of [this, peer]) if (pc._.sctp && !pc._.closed) { pc._.sctp._.maxChannels = 65535; pc._.sctp._set('connected'); }
        for (const pc of [this, peer]) for (const dc of pc._.channels.slice()) pc._announce(dc);
    }
    _assignId(dc) {
        if (dc._.id !== null) return true;
        const used = new Set(this._.channels.filter(x => x._.readyState !== 'closed').map(x => x._.id));
        if (this._.peer) for (const x of this._.peer._.channels) if (x._.readyState !== 'closed') used.add(x._.id);
        for (let i = this._.role === 'client' ? 0 : 1; i < 65535; i += 2) if (!used.has(i)) { dc._.id = i; return true; }
        return false;
    }
    _announce(dc) {
        const peer = this._.peer;
        if (!this._.connected || !peer || !this._.sctp || dc._.peer || dc._.readyState !== 'connecting' || this._.closed) return;
        if (dc._.negotiated) {
            const other = peer._.channels.find(x => x._.negotiated && x._.id === dc._.id && !x._.peer && x._.readyState === 'connecting');
            if (!other) return;
            dc._.peer = other; other._.peer = dc;
            rtcTask(() => { dc._open(); other._open(); });
            return;
        }
        if (!this._assignId(dc)) { rtcTask(() => dc._closed('No channel id available')); return; }
        const r = new RTCDataChannel(RTC_INTERNAL, peer, { label: dc._.label, ordered: dc._.ordered, maxPacketLifeTime: dc._.maxPacketLifeTime, maxRetransmits: dc._.maxRetransmits, protocol: dc._.protocol, negotiated: false, id: dc._.id, priority: dc._.priority });
        dc._.peer = r; r._.peer = dc;
        peer._.channels.push(r);
        rtcTask(() => {
            if (peer._.closed || this._.closed) return;
            dc._open();
            rtcFire(peer, new RTCDataChannelEvent('datachannel', { channel: r }));
            rtcTask(() => r._open());
        });
    }
    createDataChannel(label, init = undefined) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'createDataChannel' on 'RTCPeerConnection': 1 argument required, but only 0 present.");
        if (this._.closed) throw rtcErr('InvalidStateError', "Failed to execute 'createDataChannel' on 'RTCPeerConnection': The RTCPeerConnection's signalingState is 'closed'.");
        init = init === undefined || init === null ? {} : init;
        const nul = k => init[k] === undefined || init[k] === null ? null : rtcRange(init[k], 65535, k);
        label = rtcUSV(label);
        const o = { label, ordered: init.ordered === undefined ? true : !!init.ordered, maxPacketLifeTime: nul('maxPacketLifeTime'), maxRetransmits: nul('maxRetransmits'), protocol: init.protocol === undefined ? '' : rtcUSV(init.protocol), negotiated: !!init.negotiated, id: init.id === undefined || init.id === null ? null : rtcRange(init.id, 65535, 'id') };
        const p = init.priority === undefined ? 'low' : String(init.priority);
        if (!RTC_DC_PRIO.includes(p)) throw new TypeError(`The provided value '${p}' is not a valid enum value of type RTCPriorityType.`);
        o.priority = p;
        if (rtcU8(label) > 65535) throw new TypeError('label is too long.');
        if (rtcU8(o.protocol) > 65535) throw new TypeError('protocol is too long.');
        if (o.maxPacketLifeTime !== null && o.maxRetransmits !== null) throw new TypeError('maxPacketLifeTime and maxRetransmits can not both be set.');
        if (o.negotiated && o.id === null) throw new TypeError('id is required for negotiated channels.');
        if (!o.negotiated) o.id = null;
        if (o.id === 65535) throw new TypeError('id 65535 is reserved.');
        if (o.id !== null && this._.channels.some(x => x._.id === o.id && x._.readyState !== 'closed')) throw rtcErr('OperationError', `RTCDataChannel with id ${o.id} already exists.`);
        if (this._.sctp && this._.sctp._.maxChannels !== null && o.id !== null && o.id >= this._.sctp._.maxChannels) throw rtcErr('OperationError', 'id exceeds maxChannels.');
        const dc = new RTCDataChannel(RTC_INTERNAL, this, o);
        if (!o.negotiated && this._.connected) this._assignId(dc);
        this._.channels.push(dc);
        if (!this._.dataInOrder) { this._.dataInOrder = true; this._.order.push('data'); }
        if (this._.connected) rtcTask(() => this._announce(dc));
        this._updateNeg();
        return dc;
    }
    _addTr(kind, track, dir, internal) {
        const t = new RTCRtpTransceiver(RTC_INTERNAL, this, kind, track, dir);
        this._.transceivers.push(t); this._.order.push(t);
        return t;
    }
    addTransceiver(trackOrKind, init = undefined) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'addTransceiver' on 'RTCPeerConnection': 1 argument required, but only 0 present.");
        init = init || {};
        let kind, track = null;
        if (trackOrKind instanceof MediaStreamTrack) { track = trackOrKind; kind = track.kind; }
        else { kind = String(trackOrKind); if (kind !== 'audio' && kind !== 'video') throw new TypeError(`The provided value '${kind}' is not a valid enum value of type MediaStreamTrackKind.`); }
        const dir = init.direction === undefined ? 'sendrecv' : String(init.direction);
        if (!RTC_DIRS.slice(0, 4).includes(dir)) throw new TypeError(`The provided value '${dir}' is not a valid enum value of type RTCRtpTransceiverDirection.`);
        if (init.streams !== undefined) for (const s of init.streams) if (!(s instanceof MediaStream)) throw new TypeError("Failed to convert value to 'MediaStream'.");
        let encs = init.sendEncodings === undefined ? null : [...init.sendEncodings];
        if (encs) {
            const rids = encs.filter(e => e.rid !== undefined).map(e => String(e.rid));
            if (rids.some(r => !/^[A-Za-z0-9]{1,255}$/.test(r))) throw new TypeError('Invalid rid.');
            if (encs.length > 1 && rids.length !== encs.length) throw new TypeError('rid is required for every encoding with simulcast.');
            if (new Set(rids).size !== rids.length) throw new TypeError('Duplicate rid.');
            rtcCheckEncodings(encs, kind);
            for (const e of encs) if (e.codec !== undefined && !RTC_CODECS[kind].some(c => rtcCodecEq(c, e.codec))) throw rtcErr('OperationError', 'Unsupported codec in sendEncodings.');
            if (kind === 'audio') encs = encs.length > 1 ? [{ active: true }] : encs.map(e => { const o = Object.assign({}, e); delete o.scaleResolutionDownBy; return o; });
            else if (encs.length) {
                encs = encs.slice(0, 4).map(e => Object.assign({}, e));
                const any = encs.some(e => e.scaleResolutionDownBy !== undefined);
                encs.forEach((e, i) => { if (e.scaleResolutionDownBy === undefined) e.scaleResolutionDownBy = any ? 1 : 2 ** (encs.length - 1 - i); });
            }
        }
        if (this._.closed) throw rtcErr('InvalidStateError', "The RTCPeerConnection's signalingState is 'closed'.");
        const t = this._addTr(kind, track, dir);
        t._.sender._.streams = init.streams ? [...init.streams] : [];
        if (encs && encs.length) t._.sender._.encodings = encs.map(e => Object.assign({ active: true }, e));
        this._updateNeg();
        return t;
    }
    addTrack(track, ...streams) {
        if (arguments.length < 1 || !(track instanceof MediaStreamTrack)) throw new TypeError("Failed to execute 'addTrack' on 'RTCPeerConnection': parameter 1 is not of type 'MediaStreamTrack'.");
        for (const s of streams) if (!(s instanceof MediaStream)) throw new TypeError("Failed to execute 'addTrack': parameter 2 is not of type 'MediaStream'.");
        if (this._.closed) throw rtcErr('InvalidStateError', "The RTCPeerConnection's signalingState is 'closed'.");
        if (this._.transceivers.some(t => t._.sender._.track === track && !t._.stopping)) throw rtcErr('InvalidAccessError', 'A sender already exists for the track.');
        let t = this._.transceivers.find(x => !x._.stopping && x._.kind === track.kind && !x._.sender._.used && x._.sender._.track === null && !x._.addTrack && x._.fromSRD);
        if (t) { t._.addTrackLater = true; t._.sender._.track = track; t._.sender._.used = true; t._.direction = rtcMkDir(true, rtcHasRecv(t._.direction)); }
        else { t = this._addTr(track.kind, track, 'sendrecv'); t._.addTrack = true; }
        t._.sender._.streams = [...new Set(streams)];
        this._updateNeg();
        return t._.sender;
    }
    removeTrack(sender) {
        if (!(sender instanceof RTCRtpSender)) throw new TypeError("Failed to execute 'removeTrack': parameter 1 is not of type 'RTCRtpSender'.");
        if (this._.closed) throw rtcErr('InvalidStateError', "The RTCPeerConnection's signalingState is 'closed'.");
        const t = this._.transceivers.find(x => x.sender === sender);
        if (!t) throw rtcErr('InvalidAccessError', 'The sender was not created by this peer connection.');
        if (t._.stopping || sender._.track === null) return;
        sender._.track = null;
        t._.direction = rtcMkDir(false, rtcHasRecv(t._.direction));
        this._updateNeg();
    }
    getSenders() { return this._.transceivers.filter(t => !t._.stopped).map(t => t.sender); }
    getReceivers() { return this._.transceivers.filter(t => !t._.stopped).map(t => t.receiver); }
    getTransceivers() { return this._.transceivers.filter(t => !t._.stopped); }
    restartIce() { if (this._.closed) return; this._.restart = true; this._updateNeg(); }
    _needed() {
        const me = this._;
        if (me.restart) return true;
        const cl = me.curLocal && rtcParseSdp(me.curLocal.sdp), cr = me.curRemote && rtcParseSdp(me.curRemote.sdp);
        if (me.channels.length && !(cl && cl.media.some(m => m.kind === 'application'))) return true;
        for (const t of me.transceivers) {
            if (t._.stopped) continue;
            if (t._.stopping) { if (t._.mid !== null) return true; continue; }
            const lm = cl && cl.media.find(m => m.mid === t._.mid);
            if (t._.mid === null || !lm) return true;
            if (rtcHasSend(t._.direction)) {
                const ids = t._.sender._.streams.map(s => s.id).sort().join(), have = (lm.msids || []).map(x => x[0]).filter(x => x !== '-').sort().join();
                if (ids !== have) return true;
            }
            if (me.curLocal.type === 'offer') {
                const rm = cr && cr.media.find(m => m.mid === t._.mid);
                if (lm.dir !== t._.direction && (!rm || rtcRev(rm.dir) !== t._.direction)) return true;
            } else {
                const om = cr && cr.media.find(m => m.mid === t._.mid);
                const want = om ? rtcMkDir(rtcHasSend(t._.direction) && rtcHasRecv(om.dir), rtcHasRecv(t._.direction) && rtcHasSend(om.dir)) : t._.direction;
                if (lm.dir !== want) return true;
            }
        }
        return false;
    }
    _updateNeg() {
        const me = this._;
        if (me.closed) return;
        if (me.ops.length) { me.nnLater = true; return; }
        if (me.signalingState !== 'stable') return;
        if (!this._needed()) { me.nn = false; return; }
        if (me.nn) return;
        me.nn = true;
        rtcTask(() => {
            if (me.closed || me.signalingState !== 'stable' || !me.nn) return;
            if (me.ops.length) { me.nnLater = true; me.nn = false; return; }
            if (!this._needed()) { me.nn = false; return; }
            rtcEv(this, 'negotiationneeded');
        });
    }
    getStats(sel = null) {
        if (sel !== null && sel !== undefined && !(sel instanceof MediaStreamTrack)) return Promise.reject(new TypeError("Failed to execute 'getStats': parameter 1 is not of type 'MediaStreamTrack'."));
        if (sel && !this._.transceivers.some(t => t.sender.track === sel || t.receiver.track === sel)) return Promise.reject(rtcErr('InvalidAccessError', 'Track not found.'));
        return new Promise(res => rtcTask(() => {
            const ts = performance.timeOrigin + performance.now(), m = new Map(), me = this._;
            const add = o => m.set(o.id, Object.assign({ timestamp: ts }, o));
            add({ id: 'P', type: 'peer-connection', dataChannelsOpened: me.dcOpened, dataChannelsClosed: me.dcClosed });
            const fp = me.cert.getFingerprints()[0];
            add({ id: 'CF' + fp.value.slice(0, 8), type: 'certificate', fingerprint: fp.value, fingerprintAlgorithm: fp.algorithm, base64Certificate: btoa('lumen') });
            if (me.connected) {
                const pair = me.ice._.pair;
                if (pair) {
                    add({ id: 'I' + pair.local.foundation, type: 'local-candidate', transportId: 'T01', address: pair.local.address, port: pair.local.port, protocol: pair.local.protocol, candidateType: pair.local.type, priority: pair.local.priority, foundation: pair.local.foundation, usernameFragment: pair.local.usernameFragment });
                    add({ id: 'I' + pair.remote.foundation + 'r', type: 'remote-candidate', transportId: 'T01', address: pair.remote.address, port: pair.remote.port, protocol: pair.remote.protocol, candidateType: pair.remote.type, priority: pair.remote.priority, foundation: pair.remote.foundation, usernameFragment: pair.remote.usernameFragment });
                    add({ id: 'CP', type: 'candidate-pair', transportId: 'T01', localCandidateId: 'I' + pair.local.foundation, remoteCandidateId: 'I' + pair.remote.foundation + 'r', state: 'succeeded', nominated: true, bytesSent: 0, bytesReceived: 0 });
                }
                add({ id: 'T01', type: 'transport', bytesSent: 0, bytesReceived: 0, dtlsState: me.dtls.state, iceState: me.ice.state, selectedCandidatePairId: pair ? 'CP' : undefined, localCertificateId: 'CF' + fp.value.slice(0, 8), dtlsRole: me.role, iceRole: me.ice.role, iceLocalUsernameFragment: me.ufrag, selectedCandidatePairChanges: 1 });
            }
            if (!sel) for (const dc of me.channels) add({ id: 'D' + (dc._.id ?? 'x') + me.channels.indexOf(dc), type: 'data-channel', label: dc._.label, protocol: dc._.protocol, dataChannelIdentifier: dc._.id, state: dc._.readyState, messagesSent: dc._.sent, bytesSent: dc._.bs, messagesReceived: dc._.recv, bytesReceived: dc._.br });
            res(new RTCStatsReport(RTC_INTERNAL, m));
        }));
    }
    close() {
        const me = this._;
        if (me.closed) return;
        me.closed = true;
        me.signalingState = 'closed';
        for (const t of me.transceivers) { t._.stopping = t._.stopped = true; t._.current = null; t._.receiver._.track._.readyState = 'ended'; }
        const peer = me.peer;
        for (const dc of me.channels) { if (dc._.readyState !== 'closed') { dc._.readyState = 'closed'; } }
        if (me.sctp) me.sctp._.state = 'closed';
        me.dtls._.state = 'closed'; me.ice._.state = 'closed';
        me.iceConnectionState = 'closed'; me.connectionState = 'closed';
        me.ops = [];
        if (peer && !peer._.closed) rtcTask(() => {
            if (peer._.closed) return;
            for (const dc of peer._.channels) if (dc._.peer && dc._.peer._.pc === this && dc._.readyState !== 'closed') dc._closed();
            if (peer._.sctp) peer._.sctp._set('closed');
            peer._.dtls._set('closed');
        });
    }
}
rtcOn(RTCPeerConnection, ['negotiationneeded', 'icecandidate', 'icecandidateerror', 'signalingstatechange', 'iceconnectionstatechange', 'icegatheringstatechange', 'connectionstatechange', 'datachannel', 'track']);
