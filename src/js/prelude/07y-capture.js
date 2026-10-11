/* getUserMedia: MediaStreamTracks backed by native capture handles (N.cap*); microphone PCM fans out to
 * Web Audio and MediaRecorder subscribers; <video srcObject> shows camera frames painted natively. */
const capLive = new Set();
let capPumpId = 0, capWatchId = 0;
const capNames = (k) => N.capList(k).split('\n').filter(Boolean);
function capDevId(kind, name) {
    const s = location.origin + '|' + kind + '|' + name; let h = '', x = 0x811c9dc5;
    for (let r = 0; r < 8; r++) { for (let i = 0; i < s.length; i++) x = Math.imul(x ^ s.charCodeAt(i), 16777619) >>> 0; x = Math.imul(x ^ r, 16777619) >>> 0; h += x.toString(16).padStart(8, '0'); }
    return h;
}
function capPump() {
    let any = false;
    for (const cap of capLive) if (cap.subs.size) {
        any = true;
        const b = N.capRead(cap.id); if (!b) continue;
        const f = new Float32Array(b);
        for (const s of [...cap.subs]) try { s(f); } catch (e) { console.error(e); }
    }
    if (!any) { clearInterval(capPumpId); capPumpId = 0; }
}
function capSubscribe(cap, fn) {
    if (!cap.virtual && !cap.subs.size) N.capRead(cap.id);   /* drop audio buffered while nobody listened */
    cap.subs.add(fn);
    if (!cap.virtual && !capPumpId) capPumpId = setInterval(capPump, 20);
}
function capUnsubscribe(cap, fn) { cap.subs.delete(fn); }
function capFree(cap) {
    capLive.delete(cap); cap.subs.clear();
    if (cap.virtual) return;
    for (const el of cap.els) { N.capDetach(el); def(el, '__mcap', null); }
    cap.els.clear(); N.capClose(cap.id);
}
function capRelease(cap, t) { cap.tracks.delete(t); if (!cap.tracks.size) capFree(cap); }
function capWatch() {   /* OS permission arriving (unmute) or device lost/denied (ended) */
    for (const cap of capLive) {
        const st = N.capState(cap.id)[0];
        if (st === 1 && cap.st !== 1) for (const t of cap.tracks) t._mute(false);
        cap.st = st;
        if (st === -1) { for (const t of [...cap.tracks]) if (t._.readyState === 'live') { t._.readyState = 'ended'; rtcEv(t, 'ended'); } capFree(cap); }
    }
    if (!capLive.size) { clearInterval(capWatchId); capWatchId = 0; }
}
function capSettings(t) {
    const cap = t._.cap, s = cap.virtual ? null : N.capState(cap.id);
    if (t._.kind === 'audio') return { deviceId: cap.devId || 'default', groupId: cap.groupId || '', sampleRate: 48000, sampleSize: 16, channelCount: 1, latency: 0.01, echoCancellation: true, autoGainControl: true, noiseSuppression: true, voiceIsolation: false };
    const w = s ? s[1] : 640, h = s ? s[2] : 480;
    return { deviceId: cap.devId, groupId: cap.groupId, width: w, height: h, aspectRatio: h ? Math.round(w / h * 1e4) / 1e4 : 0, frameRate: 30, resizeMode: 'none' };
}
function capCapabilities(t) {
    const cap = t._.cap;
    if (t._.kind === 'audio') return { deviceId: cap.devId || 'default', groupId: cap.groupId || '', sampleRate: { min: 48000, max: 48000 }, sampleSize: { min: 16, max: 16 }, channelCount: { min: 1, max: 1 }, echoCancellation: [true, false], autoGainControl: [true, false], noiseSuppression: [true, false], latency: { min: 0.01, max: 0.01 } };
    const s = capSettings(t);
    return { deviceId: cap.devId, groupId: cap.groupId, width: { min: 1, max: s.width }, height: { min: 1, max: s.height }, aspectRatio: { min: 1 / s.height, max: s.width }, frameRate: { min: 1, max: 30 }, resizeMode: ['none', 'crop-and-scale'] };
}
const capNum = (v, d) => typeof v === 'number' ? v : v && typeof v === 'object' ? (v.exact ?? v.ideal ?? v.max ?? v.min ?? d) : d;
class OverconstrainedError extends DOMException {
    constructor(constraint, message = '') { super(message, 'OverconstrainedError'); def(this, '_c', String(constraint)); }
    get constraint() { return this._c; }
}
class MediaDeviceInfo {
    constructor(tok, deviceId, kind, label, groupId) { if (tok !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); def(this, '_', { deviceId, kind, label, groupId }); }
    get deviceId() { return this._.deviceId; } get kind() { return this._.kind; } get label() { return this._.label; } get groupId() { return this._.groupId; }
    toJSON() { return { deviceId: this.deviceId, kind: this.kind, label: this.label, groupId: this.groupId }; }
}
class InputDeviceInfo extends MediaDeviceInfo {
    getCapabilities() { return this.deviceId ? { deviceId: this.deviceId, groupId: this.groupId } : {}; }
}
function capEnumerate() {
    const out = [], grant = {};
    for (const [k, kind] of [[1, 'audioinput'], [2, 'videoinput']]) {
        const names = capNames(k);
        grant[k] = N.capPermission(location.origin, k) > 0 || [...capLive].some(c => c.kind === k);
        if (!grant[k]) { if (names.length) out.push(new InputDeviceInfo(RTC_INTERNAL, '', kind, '', '')); continue; }
        if (k === 1 && names.length) out.push(new InputDeviceInfo(RTC_INTERNAL, 'default', kind, 'Default - ' + names[0], capDevId('group', names[0])));
        for (const nm of names) out.push(new InputDeviceInfo(RTC_INTERNAL, capDevId(kind, nm), kind, nm, capDevId('group', nm)));
    }
    out.push(grant[1] ? new MediaDeviceInfo(RTC_INTERNAL, 'default', 'audiooutput', 'Default - Speakers', capDevId('group', 'out')) : new MediaDeviceInfo(RTC_INTERNAL, '', 'audiooutput', '', ''));
    return Promise.resolve(out);
}
function capGetUserMedia(c) {
    if (!c || typeof c !== 'object' || (!c.audio && !c.video)) return Promise.reject(new TypeError("Failed to execute 'getUserMedia' on 'MediaDevices': At least one of audio and video must be requested"));
    return new Promise((res, rej) => setTimeout(() => {
        const kinds = (c.audio ? 1 : 0) | (c.video ? 2 : 0), devs = {};
        for (const k of [1, 2]) if (kinds & k) { devs[k] = capNames(k); if (!devs[k].length) return rej(new DOMException('Requested device not found', 'NotFoundError')); }
        if (!N.capPermit(location.origin, kinds)) return rej(new DOMException('Permission denied', 'NotAllowedError'));
        const tracks = [];
        for (const k of [1, 2]) if (kinds & k) {
            const o = typeof (k === 1 ? c.audio : c.video) === 'object' ? (k === 1 ? c.audio : c.video) : {}, kind = k === 1 ? 'audioinput' : 'videoinput';
            let want = o.deviceId, exact = false;
            if (want && typeof want === 'object' && !Array.isArray(want)) { exact = want.exact !== undefined; want = want.exact ?? want.ideal; }
            const wants = (Array.isArray(want) ? want : want == null ? [] : [want]).map(String);
            let dev = devs[k].findIndex(nm => wants.includes(capDevId(kind, nm)));
            if (dev < 0 && exact && !wants.includes('default')) { tracks.forEach(t => t.stop()); return rej(new OverconstrainedError('deviceId', '')); }
            const id = N.capOpen(k, dev, capNum(o.width, 640) | 0, capNum(o.height, 480) | 0);
            if (id < 0) { tracks.forEach(t => t.stop()); return rej(new DOMException(k === 2 ? 'Could not start video source' : 'Could not start audio source', 'NotReadableError')); }
            const st = N.capState(id), nm = devs[k][dev < 0 ? 0 : dev];
            const cap = { id, kind: k, st: st[0], devId: capDevId(kind, nm), groupId: capDevId('group', nm), tracks: new Set(), subs: new Set(), els: new Set() };
            const t = new MediaStreamTrack(RTC_INTERNAL, k === 1 ? 'audio' : 'video', st[3]);
            t._.cap = cap; t._.cons = o; t._.muted = st[0] !== 1; cap.tracks.add(t); capLive.add(cap); tracks.push(t);
        }
        if (!capWatchId) capWatchId = setInterval(capWatch, 250);
        res(new MediaStream(tracks));
    }, 0));
}
function capPermQuery(d) {
    const name = d && d.name;
    if (typeof name !== 'string') return Promise.reject(new TypeError("Failed to execute 'query' on 'Permissions': required member name is undefined."));
    const k = name === 'camera' ? 2 : name === 'microphone' ? 1 : 0, s = k ? N.capPermission(location.origin, k) : 0;
    return Promise.resolve(Object.assign(new EventTarget(), { name, state: s > 0 ? 'granted' : s < 0 ? 'denied' : 'prompt', onchange: null }));
}
function capMediaState(el) {
    const c = el.__mcap, s = c && capLive.has(c) ? N.capState(c.id) : null;
    return [4, 0, false, false, false, false, (N.now() - (el.__ms0 || 0)) / 1000, Infinity, s ? s[1] : 0, s ? s[2] : 0, ''];
}
function capVideoDetach(el) {
    if (el.__mcap) { N.capDetach(el); el.__mcap.els.delete(el); def(el, '__mcap', null); }
    if (el.__ms) def(el, '__ms', null);
}
function capVideoAttach(el, s) {
    def(el, '__ms', s); def(el, '__ms0', N.now());
    const vt = s.getVideoTracks().find(t => t._.cap && !t._.cap.virtual && t._.readyState === 'live');
    if (vt) { N.capAttach(el, vt._.cap.id); vt._.cap.els.add(el); def(el, '__mcap', vt._.cap); }
    for (const e of ['loadstart', 'durationchange', 'loadedmetadata', 'loadeddata', 'canplay', 'canplaythrough']) mediaLater(el, e);
    if (el.autoplay) setTimeout(() => { if (el.__ms === s) el.play(); }, 0);
}
class BlobEvent extends Event {
    constructor(t, i) { if (!i || !(i.data instanceof Blob)) throw new TypeError("Failed to construct 'BlobEvent': required member data is undefined."); super(t, i); def(this, '_', { data: i.data, timecode: i.timecode ?? N.now() }); }
    get data() { return this._.data; } get timecode() { return this._.timecode; }
}
class MediaRecorder extends EventTarget {
    constructor(stream, o = {}) {
        super();
        if (!(stream instanceof MediaStream)) throw new TypeError("Failed to construct 'MediaRecorder': parameter 1 is not of type 'MediaStream'.");
        const mt = o && o.mimeType ? String(o.mimeType) : '';
        if (mt && !MediaRecorder.isTypeSupported(mt)) throw new DOMException(`Failed to construct 'MediaRecorder': Unsupported mime type '${mt}'.`, 'NotSupportedError');
        def(this, '_', { stream, mime: mt, state: 'inactive', abps: +(o && (o.audioBitsPerSecond || o.bitsPerSecond)) || 128000, id: -1, timer: 0, sil: 0 });
    }
    get stream() { return this._.stream; } get mimeType() { return this._.mime; } get state() { return this._.state; }
    get audioBitsPerSecond() { return this._.abps; } get videoBitsPerSecond() { return 0; } get audioBitrateMode() { return 'variable'; }
    static isTypeSupported(t) { return /^audio\/webm(\s*;\s*codecs\s*=\s*"?opus"?)?\s*$/i.test(String(t)); }
    _bad(m) { return new DOMException(`Failed to execute '${m}' on 'MediaRecorder': The MediaRecorder's state is 'inactive'.`, 'InvalidStateError'); }
    start(slice) {
        const _ = this._;
        if (_.state !== 'inactive') throw new DOMException(`Failed to execute 'start' on 'MediaRecorder': The MediaRecorder's state is '${_.state}'.`, 'InvalidStateError');
        const t = _.stream.getAudioTracks().find(x => x.readyState === 'live');
        if (!t) throw new DOMException("Failed to execute 'start' on 'MediaRecorder': There was an error starting the MediaRecorder.", 'NotSupportedError');
        _.id = N.recNew(Math.min(_.abps, 510000));
        if (_.id < 0) throw new DOMException("Failed to execute 'start' on 'MediaRecorder': There was an error starting the MediaRecorder.", 'NotSupportedError');
        _.mime = 'audio/webm;codecs=opus'; _.state = 'recording'; _.track = t; _.cap = t._.cap || null;
        _.sub = (f) => { if (_.state === 'recording') N.recPush(_.id, t.enabled ? f : new Float32Array(f.length)); };
        if (_.cap) capSubscribe(_.cap, _.sub); else _.sil = setInterval(() => _.sub(new Float32Array(960)), 20);
        _.onend = () => this.stop(); t.addEventListener('ended', _.onend);
        if (slice !== undefined && +slice >= 0) _.timer = setInterval(() => this._flush(false), Math.max(+slice, 100));
        setTimeout(() => this.dispatchEvent(new Event('start')), 0);
    }
    stop() {
        const _ = this._; if (_.state === 'inactive') return;
        _.state = 'inactive'; clearInterval(_.timer); clearInterval(_.sil);
        if (_.cap) capUnsubscribe(_.cap, _.sub); _.track.removeEventListener('ended', _.onend);
        this._flush(true);
        setTimeout(() => this.dispatchEvent(new Event('stop')), 0);
    }
    pause() { const _ = this._; if (_.state === 'inactive') throw this._bad('pause'); if (_.state === 'recording') { _.state = 'paused'; setTimeout(() => this.dispatchEvent(new Event('pause')), 0); } }
    resume() { const _ = this._; if (_.state === 'inactive') throw this._bad('resume'); if (_.state === 'paused') { _.state = 'recording'; setTimeout(() => this.dispatchEvent(new Event('resume')), 0); } }
    requestData() { if (this._.state === 'inactive') throw this._bad('requestData'); this._flush(false); }
    _flush(fin) {
        const b = N.recTake(this._.id, fin), data = new Blob(b && b.byteLength ? [b] : [], { type: this._.mime });
        setTimeout(() => this.dispatchEvent(new BlobEvent('dataavailable', { data })), 0);
    }
}
rtcOn(MediaRecorder, ['start', 'stop', 'dataavailable', 'pause', 'resume', 'error']);
