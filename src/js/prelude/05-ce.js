const timeRanges = (r) => ({ length: r.length, start(i) { return r[i][0]; }, end(i) { return r[i][1]; } });
mk('HTMLMediaElement', HTMLElement, [], p => {
    reflectUrl(p, 'src'); reflectStr(p, 'crossOrigin', 'preload'); reflectBool(p, 'autoplay', 'loop', 'controls', 'playsInline');
    methods(p, {
        get currentSrc() { return this.src; }, get paused() { return this.__paused !== false; }, get ended() { return false; }, get seeking() { return false; },
        get readyState() { return 0; }, get networkState() { return this.src ? 3 : 0; }, get error() { return null; },
        get duration() { return NaN; }, get currentTime() { return this.__ct || 0; }, set currentTime(v) { def(this, '__ct', +v || 0); },
        get volume() { return this.__vol ?? 1; }, set volume(v) { def(this, '__vol', +v); },
        get muted() { return this.__muted ?? N.attr(this, 'muted') != null; }, set muted(v) { def(this, '__muted', !!v); },
        get playbackRate() { return this.__rate ?? 1; }, set playbackRate(v) { def(this, '__rate', +v); }, get defaultPlaybackRate() { return 1; },
        get buffered() { return timeRanges([]); }, get played() { return timeRanges([]); }, get seekable() { return timeRanges([]); },
        get textTracks() { return Object.assign([], { addEventListener() {}, removeEventListener() {}, getTrackById() { return null; } }); },
        get srcObject() { return this.__so ?? null; }, set srcObject(v) { def(this, '__so', v); },
        play() { return Promise.reject(new DOMException('Media playback is not implemented yet', 'NotSupportedError')); },
        pause() { def(this, '__paused', true); }, load() {}, canPlayType() { return ''; },
        addTextTrack() { return { mode: 'disabled', cues: [], addCue() {}, removeCue() {} }; }, setSinkId() { return Promise.resolve(); },
    });
});
Object.assign(H.HTMLMediaElement, { NETWORK_EMPTY: 0, NETWORK_IDLE: 1, NETWORK_LOADING: 2, NETWORK_NO_SOURCE: 3, HAVE_NOTHING: 0, HAVE_METADATA: 1, HAVE_CURRENT_DATA: 2, HAVE_FUTURE_DATA: 3, HAVE_ENOUGH_DATA: 4 });
mk('HTMLVideoElement', H.HTMLMediaElement, ['video'], p => { reflectUrl(p, 'poster'); reflectInt(p, 0, 'width', 'height'); methods(p, { get videoWidth() { return 0; }, get videoHeight() { return 0; }, getVideoPlaybackQuality() { return { totalVideoFrames: 0, droppedVideoFrames: 0, corruptedVideoFrames: 0, creationTime: N.now() }; }, requestVideoFrameCallback() { return 0; }, cancelVideoFrameCallback() {} }); });
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
        if (ns === 1) return (tag === 'svg' ? SVGSVGElement : /^(g|path|rect|circle|ellipse|line|polyline|polygon|use|text|image)$/.test(tag) ? SVGGraphicsElement : SVGElement).prototype;
        if (ns === 2) return MathMLElement.prototype;
        if (TAGS[tag]) return TAGS[tag].prototype;
        return PLAIN.has(tag) || tag.includes('-') ? HTMLElement.prototype : H.HTMLUnknownElement.prototype;
    case 3: return Text.prototype;
    case 4: return CDATASection.prototype;
    case 8: return Comment.prototype;
    case 9: return HTMLDocument.prototype;
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
function ceConnected(root, connected) {
    if (!registry.byName.size) return;
    const t = N.type(root); if (t !== 1 && t !== 11) return;
    for (const el of N.ceScan(root)) {
        if (!el.__ce) { ceUpgrade(el); continue; }
        if (connected !== N.connected(el)) continue;
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
