const timeRanges = (r) => ({ length: r.length, start(i) { return r[i][0]; }, end(i) { return r[i][1]; } });
const mediaEls = new Set();
let mediaPumpId = 0;
const mediaFire = (el, t) => el.dispatchEvent(new Event(t));
const mediaLater = (el, t) => setTimeout(() => mediaFire(el, t), 0);
const mediaRanges = (flat) => { const r = []; for (let i = 0; i + 1 < flat.length; i += 2) r.push([flat[i], flat[i + 1]]); return new TimeRanges(r); };
function mediaPoll() {
    for (const el of mediaEls) {
        if (!el.__mp) { mediaEls.delete(el); continue; }
        const s = N.mediaState(el.__mp); if (!s) continue;
        const [ready, , ended, seeking, waiting, error, time, duration, w, h, msg] = s;
        const o = el.__st || { ready: 0, ended: false, seeking: false, waiting: false, error: false, duration: NaN, w: 0, h: 0, tu: -1 };
        const n = { ready, ended, seeking, waiting, error, duration, w, h, tu: o.tu };
        def(el, '__st', n);
        const playing = el.__paused === false;
        if (error && !o.error) { def(el, '__err', new MediaError(msg === 'network error' ? 2 : 3, msg || '')); mediaFire(el, 'error'); continue; }
        if (!(Number.isNaN(duration) && Number.isNaN(o.duration)) && duration !== o.duration) mediaFire(el, 'durationchange');
        if (ready >= 1 && o.ready < 1) mediaFire(el, 'loadedmetadata');
        if ((w !== o.w || h !== o.h) && ready >= 1) mediaFire(el, 'resize');
        if (ready >= 2 && o.ready < 2) mediaFire(el, 'loadeddata');
        if (ready >= 3 && o.ready < 3) { mediaFire(el, 'canplay'); if (playing) mediaFire(el, 'playing'); }
        if (ready >= 4 && o.ready < 4) mediaFire(el, 'canplaythrough');
        if (waiting && !o.waiting) mediaFire(el, 'waiting');
        if (!waiting && o.waiting && playing) mediaFire(el, 'playing');
        if (!seeking && o.seeking) { n.tu = time; mediaFire(el, 'timeupdate'); mediaFire(el, 'seeked'); }
        if (playing && !seeking && Math.abs(time - n.tu) >= 0.25) { n.tu = time; mediaFire(el, 'timeupdate'); }
        if (ended && !o.ended) {
            if (el.loop) { N.mediaSeek(el.__mp, 0); N.mediaPlay(el.__mp); continue; }
            def(el, '__paused', true); mediaFire(el, 'timeupdate'); mediaFire(el, 'pause'); mediaFire(el, 'ended');
        }
    }
    if (!mediaEls.size && mediaPumpId) { clearInterval(mediaPumpId); mediaPumpId = 0; }
}
function mediaDetach(el) {
    if (el.__mp) { N.mediaFree(el.__mp); def(el, '__mp', 0); }
    mediaEls.delete(el);
    def(el, '__st', null); def(el, '__err', null); def(el, '__src', '');
}
function mediaAttach(el, src) {
    const id = N.mediaNew(el); def(el, '__mp', id);
    mediaEls.add(el); if (!mediaPumpId) mediaPumpId = setInterval(mediaPoll, 50);
    N.mediaVolume(id, el.volume, el.muted);
    if (src instanceof MediaSource) { def(el, '__src', ''); src._attach(el, id); }
    else {
        const ms = objectURLs.get(src);
        def(el, '__src', src);
        if (ms instanceof MediaSource) ms._attach(el, id); else N.mediaOpen(id, src);
    }
    mediaLater(el, 'loadstart');
}
mk('HTMLMediaElement', HTMLElement, [], p => {
    reflectStr(p, 'crossOrigin', 'preload'); reflectBool(p, 'autoplay', 'loop', 'controls', 'playsInline', 'defaultMuted');
    const st = (el) => el.__mp ? N.mediaState(el.__mp) : null;
    methods(p, {
        get src() { const v = N.attr(this, 'src'); return v == null ? '' : new URL(v, document.baseURI).href; },
        set src(v) { N.setAttr(this, 'src', String(v)); this.load(); },
        get currentSrc() { return this.__src || ''; },
        get paused() { return this.__paused !== false; },
        get ended() { const s = st(this); return !!(s && s[2]); },
        get seeking() { const s = st(this); return !!(s && s[3]); },
        get readyState() { const s = st(this); return s ? s[0] : 0; },
        get networkState() { const s = st(this); return s ? (s[0] >= 4 ? 1 : 2) : (N.attr(this, 'src') != null ? 3 : 0); },
        get error() { return this.__err || null; },
        get duration() { const s = st(this); return s ? s[7] : NaN; },
        get currentTime() { const s = st(this); return s ? s[6] : (this.__ct || 0); },
        set currentTime(v) {
            v = +v || 0;
            if (!this.__mp) { def(this, '__ct', v); return; }
            N.mediaSeek(this.__mp, v); if (this.__st) this.__st.seeking = true;
            mediaFire(this, 'seeking');
        },
        fastSeek(t) { this.currentTime = t; },
        get volume() { return this.__vol ?? 1; },
        set volume(v) { v = +v; if (!(v >= 0 && v <= 1)) throw new DOMException('volume out of range', 'IndexSizeError'); def(this, '__vol', v); if (this.__mp) N.mediaVolume(this.__mp, v, this.muted); mediaLater(this, 'volumechange'); },
        get muted() { return this.__muted ?? N.attr(this, 'muted') != null; },
        set muted(v) { def(this, '__muted', !!v); if (this.__mp) N.mediaVolume(this.__mp, this.volume, !!v); mediaLater(this, 'volumechange'); },
        get playbackRate() { return this.__rate ?? 1; }, set playbackRate(v) { def(this, '__rate', +v); mediaLater(this, 'ratechange'); },
        get defaultPlaybackRate() { return 1; }, set defaultPlaybackRate(v) {}, get preservesPitch() { return true; }, set preservesPitch(v) {},
        get buffered() { return this.__mp ? mediaRanges(N.mediaBuffered(this.__mp, -1)) : new TimeRanges([]); },
        get played() { return new TimeRanges([]); },
        get seekable() { const d = this.duration; return new TimeRanges(d > 0 && Number.isFinite(d) ? [[0, d]] : []); },
        get textTracks() { return this.__tt || def(this, '__tt', Object.assign([], { addEventListener() {}, removeEventListener() {}, getTrackById() { return null; } })) || this.__tt; },
        get audioTracks() { return Object.assign([], { addEventListener() {}, removeEventListener() {}, getTrackById() { return null; } }); },
        get videoTracks() { return Object.assign([], { addEventListener() {}, removeEventListener() {}, getTrackById() { return null; } }); },
        get srcObject() { return this.__so ?? null; },
        set srcObject(v) { def(this, '__so', v ?? null); mediaDetach(this); def(this, '__paused', true); if (v instanceof MediaSource) mediaAttach(this, v); },
        load() {
            const was = !!this.__mp;
            mediaDetach(this);
            if (was || this.__paused === false) { def(this, '__paused', true); mediaLater(this, 'emptied'); }
            let src = N.attr(this, 'src');
            if (src == null) { const s = this.querySelector('source[src]'); src = s && s.getAttribute('src'); }
            if (src == null || src === '') return;
            mediaAttach(this, new URL(src, document.baseURI).href);
            if (this.__ct) { N.mediaSeek(this.__mp, this.__ct); def(this, '__ct', 0); }
        },
        play() {
            if (!this.__mp) this.load();
            if (!this.__mp) return Promise.reject(new DOMException('The element has no supported sources.', 'NotSupportedError'));
            if (this.__err) return Promise.reject(new DOMException(this.__err.message || 'playback error', 'NotSupportedError'));
            if (this.__paused !== false) { def(this, '__paused', false); mediaLater(this, 'play'); if (this.readyState >= 3) mediaLater(this, 'playing'); }
            N.mediaPlay(this.__mp);
            return Promise.resolve();
        },
        pause() {
            if (this.__mp) N.mediaPause(this.__mp);
            if (this.__paused === false) { def(this, '__paused', true); mediaLater(this, 'timeupdate'); mediaLater(this, 'pause'); }
        },
        canPlayType(t) { return ['', 'maybe', 'probably'][N.mediaCanPlay(String(t), false)] || ''; },
        addTextTrack() { return { mode: 'disabled', cues: [], addCue() {}, removeCue() {} }; }, setSinkId() { return Promise.resolve(); },
    });
});
Object.assign(H.HTMLMediaElement, { NETWORK_EMPTY: 0, NETWORK_IDLE: 1, NETWORK_LOADING: 2, NETWORK_NO_SOURCE: 3, HAVE_NOTHING: 0, HAVE_METADATA: 1, HAVE_CURRENT_DATA: 2, HAVE_FUTURE_DATA: 3, HAVE_ENOUGH_DATA: 4 });
mk('HTMLVideoElement', H.HTMLMediaElement, ['video'], p => { reflectUrl(p, 'poster'); reflectInt(p, 0, 'width', 'height'); methods(p, { get videoWidth() { return this.__mp ? N.mediaState(this.__mp)[8] : 0; }, get videoHeight() { return this.__mp ? N.mediaState(this.__mp)[9] : 0; }, getVideoPlaybackQuality() { return { totalVideoFrames: 0, droppedVideoFrames: 0, corruptedVideoFrames: 0, creationTime: N.now() }; }, requestVideoFrameCallback() { return 0; }, cancelVideoFrameCallback() {} }); });
mk('HTMLAudioElement', H.HTMLMediaElement, ['audio']);
function Image(w, h) { const e = document.createElement('img'); if (w !== undefined) e.width = w; if (h !== undefined) e.height = h; return e; }
Image.prototype = H.HTMLImageElement.prototype;
function Audio(src) { const e = document.createElement('audio'); if (src !== undefined) e.src = src; return e; }
Audio.prototype = H.HTMLAudioElement.prototype;
function Option(text = '', value, ds, sel) { const o = document.createElement('option'); o.text = text; if (value !== undefined) o.value = value; if (ds) o.setAttribute('selected', ''); if (sel) o.selected = true; return o; }
Option.prototype = H.HTMLOptionElement.prototype;
function urlParts(p, attr) {
    const get = (el) => { const v = N.attr(el, attr); if (v == null) return null; try { return new URL(v, document.baseURI); } catch (e) { return null; } };
    acc(p, attr, function () { const u = get(this); return u ? u.href : (N.attr(this, attr) ?? ''); }, function (v) { this.setAttribute(attr, v); });
    for (const k of ['protocol', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'username', 'password']) acc(p, k, function () { const u = get(this); return u ? u[k] : ''; }, function (v) { const u = get(this); if (u) { u[k] = v; this.setAttribute(attr, u.href); } });
    acc(p, 'origin', function () { const u = get(this); return u ? u.origin : ''; });
    def(p, 'toString', function () { return this[attr]; });
}
const PLAIN = new Set('abbr address article aside b bdi bdo cite code dd dfn dt em figcaption figure footer header hgroup i kbd main mark nav noscript rp rt ruby s samp search section small strong sub summary sup u var wbr center nobr big tt acronym strike font marquee'.split(' '));
function protoFor(type, tag, ns) {
    switch (type) {
    case 1:
        if (ns === 3) return Element.prototype;
        if (ns === 1) return svgProto(tag);
        if (ns === 2) return MathMLElement.prototype;
        if (TAGS[tag]) return TAGS[tag].prototype;
        return PLAIN.has(tag) || tag.includes('-') ? HTMLElement.prototype : H.HTMLUnknownElement.prototype;
    case 3: return Text.prototype;
    case 4: return CDATASection.prototype;
    case 8: return Comment.prototype;
    case 7: return ProcessingInstruction.prototype;
    case 9: return ns === 3 ? XMLDocument.prototype : HTMLDocument.prototype;
    case 10: return DocumentType.prototype;
    case 11: return DocumentFragment.prototype;
    }
    return Node.prototype;
}

const registry = { byName: new Map(), byCtor: new Map(), waiting: new Map(), upgrading: [] };
const validCEName = (n) => /^[a-z][.0-9_a-z\u00b7\u00c0-\uffff-]*-[.0-9_a-z\u00b7\u00c0-\uffff-]*$/.test(n) && !/^(annotation-xml|color-profile|font-face|font-face-src|font-face-uri|font-face-format|font-face-name|missing-glyph)$/.test(n);
function ceConstruct(nt) {
    if (registry.upgrading.length) { const el = registry.upgrading.pop(); Object.setPrototypeOf(el, nt.prototype); return el; }
    const d = registry.byCtor.get(nt);
    if (!d) illegal();
    const el = N.create(d.name, 0);
    Object.setPrototypeOf(el, nt.prototype);
    def(el, '__ce', d);
    return el;
}
function ceUpgradeTree(root) {
    if (!registry.byName.size) return;
    const t = N.type(root);
    if (t !== 1 && t !== 11) return;
    for (const el of N.ceScan(root)) if (!el.__ce) ceUpgrade(el);
}

function ceUpgrade(el) {
    if (el.__ce || N.type(el) !== 1 || N.ns(el) !== 0) return;
    const d = registry.byName.get(N.name(el)); if (!d) return;
    def(el, '__ce', d);
    registry.upgrading.push(el);
    try { new d.ctor(); }
    catch (e) { report(e); Object.setPrototypeOf(el, d.ctor.prototype); }
    finally { const i = registry.upgrading.indexOf(el); if (i >= 0) registry.upgrading.splice(i, 1); }
    if (d.observed.length && typeof el.attributeChangedCallback === 'function')
        for (const a of d.observed) { const v = N.attr(el, a); if (v != null) { try { el.attributeChangedCallback(a, null, v, null); } catch (e) { report(e); } } }
    if (N.connected(el) && typeof el.connectedCallback === 'function') { try { el.connectedCallback(); } catch (e) { report(e); } }
}
function ceConnected(root, connected, moved) {
    if (!registry.byName.size) return;
    const t = N.type(root); if (t !== 1 && t !== 11) return;
    for (const el of N.ceScan(root)) {
        if (!el.__ce) { if (!moved && N.connected(el)) ceUpgrade(el); continue; }
        if (!moved && connected !== N.connected(el)) continue;
        const cb = connected ? el.connectedCallback : el.disconnectedCallback;
        if (typeof cb === 'function') { try { cb.call(el); } catch (e) { report(e); } }
    }
}
function ceAttr(el, name, old, v) {
    const d = el.__ce; if (!d || !d.observed.includes(name) || typeof el.attributeChangedCallback !== 'function') return;
    try { el.attributeChangedCallback(name, old, v, null); } catch (e) { report(e); }
}
class CustomElementRegistry {
    define(name, ctor, opts) {
        name = String(name);
        if (typeof ctor !== 'function') throw new TypeError("Failed to execute 'define': The provided value is not a constructor");
        if (!validCEName(name)) throw new DOMException(`"${name}" is not a valid custom element name`, 'SyntaxError');
        if (registry.byName.has(name)) throw new DOMException(`the name "${name}" has already been used with this registry`, 'NotSupportedError');
        if (registry.byCtor.has(ctor)) throw new DOMException('this constructor has already been used with this registry', 'NotSupportedError');
        const oa = ctor.observedAttributes;
        const d = { name, ctor, observed: oa ? [...oa].map(String) : [], ext: opts && opts.extends };
        registry.byName.set(name, d); registry.byCtor.set(ctor, d);
        for (const el of N.query(document, CSS.escape(name), true)) ceUpgrade(el);
        const w = registry.waiting.get(name); if (w) { registry.waiting.delete(name); w.resolve(ctor); }
    }
    get(name) { const d = registry.byName.get(String(name)); return d ? d.ctor : undefined; }
    getName(ctor) { const d = registry.byCtor.get(ctor); return d ? d.name : null; }
    whenDefined(name) {
        name = String(name);
        if (registry.byName.has(name)) return Promise.resolve(registry.byName.get(name).ctor);
        let w = registry.waiting.get(name);
        if (!w) { let resolve; const promise = new Promise(r => resolve = r); w = { promise, resolve }; registry.waiting.set(name, w); }
        return w.promise;
    }
    upgrade(root) { for (const el of N.ceScan(root)) ceUpgrade(el); }
}
