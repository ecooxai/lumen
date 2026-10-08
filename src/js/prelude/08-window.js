class ResizeObserver {
    constructor(cb) { this._cb = cb; this._t = new Map(); }
    observe(el) { if (this._t.has(el)) return; this._t.set(el, null); setTimeout(() => this._check()); }
    unobserve(el) { this._t.delete(el); } disconnect() { this._t.clear(); }
    _check() {
        const recs = [];
        for (const [el, old] of this._t) { const r = N.rect(el) || [0, 0, 0, 0]; const k = r[2] + 'x' + r[3]; if (k === old) continue; this._t.set(el, k); const cr = new DOMRect(0, 0, r[2], r[3]); const sz = [{ inlineSize: r[2], blockSize: r[3] }]; recs.push({ target: el, contentRect: cr, borderBoxSize: sz, contentBoxSize: sz, devicePixelContentBoxSize: sz }); }
        if (recs.length) { try { this._cb(recs, this); } catch (e) { report(e); } }
    }
}
class IntersectionObserver {
    constructor(cb, o = {}) { this._cb = cb; this._t = new Set(); this.root = o.root || null; this.rootMargin = o.rootMargin || '0px'; this.thresholds = [].concat(o.threshold ?? 0); }
    observe(el) { this._t.add(el); setTimeout(() => { if (!this._t.has(el)) return; const r = el.getBoundingClientRect(); const v = N.viewport(); const vis = r.bottom > 0 && r.top < v[1] && r.right > 0 && r.left < v[0] && r.width + r.height > 0; try { this._cb([{ target: el, isIntersecting: vis, intersectionRatio: vis ? 1 : 0, boundingClientRect: r, intersectionRect: vis ? r : new DOMRect(), rootBounds: new DOMRect(0, 0, v[0], v[1]), time: N.now() }], this); } catch (e) { report(e); } }); }
    unobserve(el) { this._t.delete(el); } disconnect() { this._t.clear(); } takeRecords() { return []; }
}
class PerformanceObserver { constructor(cb) { this._cb = cb; } observe() {} disconnect() {} takeRecords() { return []; } static get supportedEntryTypes() { return []; } }
class Storage {
    constructor() { def(this, '_m', new Map()); }
    get length() { return this._m.size; } key(i) { return [...this._m.keys()][i] ?? null; }
    getItem(k) { return this._m.has(String(k)) ? this._m.get(String(k)) : null; }
    setItem(k, v) { this._m.set(String(k), String(v)); } removeItem(k) { this._m.delete(String(k)); } clear() { this._m.clear(); }
}
const storageProxy = () => new Proxy(new Storage(), {
    get(t, k) { if (typeof k !== 'string' || k in t) { const v = Reflect.get(t, k, t); return typeof v === 'function' ? v.bind(t) : v; } return t.getItem(k) ?? undefined; },
    set(t, k, v) { t.setItem(k, v); return true; }, deleteProperty(t, k) { t.removeItem(k); return true; },
    has(t, k) { return k in t || t._m.has(String(k)); }, ownKeys(t) { return [...t._m.keys()]; },
    getOwnPropertyDescriptor(t, k) { return t._m.has(k) ? { value: t._m.get(k), enumerable: true, configurable: true, writable: true } : undefined; },
});
const localStorage = storageProxy(), sessionStorage = storageProxy();
class Location {
    get href() { return N.url(); } set href(v) { this.assign(v); }
    assign(u) { const n = new URL(String(u), document.baseURI); const cur = new URL(N.url()); if (n.href.split('#')[0] === cur.href.split('#')[0] && n.hash) { const old = cur.href; N.setUrl(n.href, true); setTimeout(() => dispatch(G, new HashChangeEvent('hashchange', { oldURL: old, newURL: n.href }))); } else N.navigate(n.href); }
    replace(u) { N.navigate(new URL(String(u), document.baseURI).href); }
    reload() { N.navigate(N.url()); }
    toString() { return N.url(); }
    get ancestorOrigins() { return []; }
}
URL_KEYS.forEach((k, i) => { if (k !== 'href') acc(Location.prototype, k, function () { return N.urlParse(N.url(), null)[i]; }, k === 'origin' ? undefined : function (v) { const u = new URL(N.url()); u[k] = v; this.assign(u.href); }); });
const location = new Location();
let histState = null, histIdx = 0;
const histStates = [null];
Object.defineProperty(globalThis, '__lumenPopState', { value: d => {
    histIdx = Math.max(0, Math.min(histStates.length - 1, histIdx + d));
    histState = histStates[histIdx] ?? null;
    globalThis.dispatchEvent(new PopStateEvent('popstate', { state: histState }));
} });
const history = {
    get length() { return N.histLen(); }, get state() { return histState; }, scrollRestoration: 'auto',
    pushState(s, t, u) { histState = structuredClone(s); histStates.length = ++histIdx; histStates[histIdx] = histState; N.setUrl(u != null ? new URL(String(u), N.url()).href : N.url(), true); },
    replaceState(s, t, u) { histState = structuredClone(s); histStates[histIdx] = histState; if (u != null) N.setUrl(new URL(String(u), N.url()).href, false); },
    back() { N.histGo(-1); }, forward() { N.histGo(1); }, go(d) { N.histGo(d | 0); },
};
const navigator = {
    userAgent: N.userAgent(), appVersion: N.userAgent().replace(/^Mozilla\//, ''), appName: 'Netscape', appCodeName: 'Mozilla', product: 'Gecko', productSub: '20030107', vendor: 'Google Inc.', vendorSub: '',
    platform: N.platform(), language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: true, doNotTrack: null, webdriver: false, pdfViewerEnabled: false,
    hardwareConcurrency: N.cpus(), deviceMemory: 8, maxTouchPoints: 0, plugins: [], mimeTypes: [],
    userAgentData: { brands: [{ brand: 'Chromium', version: '131' }, { brand: 'Not_A Brand', version: '24' }], mobile: false, platform: N.platform() === 'MacIntel' ? 'macOS' : 'Linux', getHighEntropyValues() { return Promise.resolve({}); }, toJSON() { return {}; } },
    connection: { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false, addEventListener() {}, removeEventListener() {} },
    javaEnabled() { return false; }, sendBeacon() { return true; }, vibrate() { return false; }, registerProtocolHandler() {},
    clipboard: { writeText() { return Promise.resolve(); }, readText() { return Promise.resolve(''); } },
    permissions: { query() { return Promise.resolve({ state: 'prompt', addEventListener() {} }); } },
    mediaDevices: Object.assign(new EventTarget(), { enumerateDevices() { return Promise.resolve([]); }, getUserMedia() { return Promise.reject(new DOMException('Capture is not implemented yet', 'NotSupportedError')); }, getDisplayMedia() { return Promise.reject(new DOMException('Capture is not implemented yet', 'NotSupportedError')); }, getSupportedConstraints() { return {}; } }),
    serviceWorker: Object.assign(new EventTarget(), {
        controller: null, ready: new Promise(() => {}),
        register() { return Promise.reject(new DOMException('Service workers are not supported', 'SecurityError')); },
        getRegistration() { return Promise.resolve(undefined); }, getRegistrations() { return Promise.resolve([]); },
        startMessages() {},
    }),
    storage: { estimate() { return Promise.resolve({ quota: 1e9, usage: 0 }); }, persist() { return Promise.resolve(false); } },
    locks: { request(n, o, cb) { cb = typeof o === 'function' ? o : cb; return Promise.resolve().then(() => cb({ name: n })); } },
    getGamepads() { return []; }, getBattery() { return Promise.reject(new DOMException('', 'NotSupportedError')); },
};
const screen = { get width() { return N.viewport()[5]; }, get height() { return N.viewport()[6]; }, get availWidth() { return N.viewport()[5]; }, get availHeight() { return N.viewport()[6]; }, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0, addEventListener() {} } };
const T0 = Date.now() - N.now();
const marks = [];
const performance = Object.assign(new EventTarget(), {
    now() { return N.now(); }, timeOrigin: T0,
    timing: { navigationStart: T0, fetchStart: T0, domLoading: T0, responseStart: T0 },
    navigation: { type: 0, redirectCount: 0 },
    mark(n, o) { const e = { name: n, entryType: 'mark', startTime: o && o.startTime != null ? o.startTime : N.now(), duration: 0, detail: o && o.detail }; marks.push(e); return e; },
    measure(n, s, e) { const f = (x) => typeof x === 'string' ? (marks.findLast(m => m.name === x) || { startTime: 0 }).startTime : x ?? N.now(); const st = typeof s === 'object' && s ? f(s.start) : f(s ?? 0); const en = typeof s === 'object' && s ? f(s.end) : f(e); const m = { name: n, entryType: 'measure', startTime: st, duration: en - st }; marks.push(m); return m; },
    getEntries() { return marks.slice(); }, getEntriesByType(t) { return marks.filter(m => m.entryType === t); }, getEntriesByName(n) { return marks.filter(m => m.name === n); },
    clearMarks() { marks.length = 0; }, clearMeasures() {}, clearResourceTimings() {}, setResourceTimingBufferSize() {},
    get memory() { const h = N.heap(); return { usedJSHeapSize: h[0], totalJSHeapSize: h[1], jsHeapSizeLimit: h[2] }; },
    toJSON() { return { timeOrigin: T0 }; },
});
const ckeys = new WeakMap();
class CryptoKey { constructor() { throw new TypeError('Illegal constructor'); } }
function mkKey(type, extractable, algorithm, usages, data) {
    const k = Object.create(CryptoKey.prototype);
    Object.defineProperties(k, { type: { value: type, enumerable: true }, extractable: { value: !!extractable, enumerable: true }, algorithm: { value: Object.freeze(algorithm), enumerable: true }, usages: { value: Object.freeze([...usages]), enumerable: true } });
    ckeys.set(k, data); return k;
}
const ALGS = ['AES-GCM', 'AES-CBC', 'AES-CTR', 'AES-KW', 'HMAC', 'ECDH', 'ECDSA', 'HKDF', 'PBKDF2', 'SHA-1', 'SHA-256', 'SHA-384', 'SHA-512', 'RSA-OAEP', 'RSA-PSS', 'RSASSA-PKCS1-v1_5', 'Ed25519', 'X25519'];
function normAlg(a) {
    const o = typeof a === 'string' ? { name: a } : Object.assign({}, a);
    const n = ALGS.find(x => x.toLowerCase() === String(o.name).toLowerCase());
    if (!n) throw new DOMException('Unrecognized algorithm name', 'NotSupportedError');
    o.name = n; return o;
}
const hashOf = h => normAlg(h).name;
function cbytes(x) {
    if (x instanceof ArrayBuffer) return x.slice(0);
    if (ArrayBuffer.isView(x)) return new Uint8Array(x.buffer, x.byteOffset, x.byteLength).slice().buffer;
    throw new TypeError('Expected a BufferSource');
}
const b64u = {
    enc(b) { let s = ''; for (const x of new Uint8Array(b)) s += String.fromCharCode(x); return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''); },
    dec(s) { s = String(s).replace(/-/g, '+').replace(/_/g, '/'); while (s.length % 4) s += '='; const t = atob(s), u = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) u[i] = t.charCodeAt(i); return u.buffer; },
};
const opErr = r => { if (r === undefined) throw new DOMException('The operation failed for an operation-specific reason', 'OperationError'); return r; };
const keyData = (k, usage) => {
    if (!(k instanceof CryptoKey)) throw new TypeError('Expected a CryptoKey');
    if (usage && !k.usages.includes(usage)) throw new DOMException(`Key usages do not permit ${usage}`, 'InvalidAccessError');
    return ckeys.get(k);
};
const hmacLen = h => (h === 'SHA-384' || h === 'SHA-512') ? 1024 : 512;
const isEC = n => n === 'ECDH' || n === 'ECDSA';
const ecPoint = (x, y) => { const a = new Uint8Array(x), b = new Uint8Array(y), u = new Uint8Array(1 + a.length + b.length); u[0] = 4; u.set(a, 1); u.set(b, 1 + a.length); return u.buffer; };
const ecXY = pub => { const u = new Uint8Array(pub), n = (u.length - 1) / 2; return [b64u.enc(u.slice(1, 1 + n)), b64u.enc(u.slice(1 + n))]; };
const ecUsages = (name, type, usages) => usages.filter(u => name === 'ECDH' ? type === 'private' && (u === 'deriveKey' || u === 'deriveBits') : u === (type === 'private' ? 'sign' : 'verify'));
const SC = {
    digest(alg, data) { return opErr(N.cDigest(hashOf(alg), cbytes(data))); },
    generateKey(alg, ext, usages) {
        const a = normAlg(alg), n = a.name;
        if (n.startsWith('AES-')) { if (![128, 192, 256].includes(a.length)) throw new DOMException('AES key length must be 128, 192 or 256 bits', 'OperationError'); return mkKey('secret', ext, { name: n, length: a.length }, usages, crypto.getRandomValues(new Uint8Array(a.length / 8)).buffer); }
        if (n === 'HMAC') { const h = hashOf(a.hash), len = a.length || hmacLen(h); return mkKey('secret', ext, { name: n, hash: { name: h }, length: len }, usages, crypto.getRandomValues(new Uint8Array(Math.ceil(len / 8))).buffer); }
        if (isEC(n)) {
            const kp = opErr(N.cEcGen(a.namedCurve)), al = { name: n, namedCurve: a.namedCurve };
            return { publicKey: mkKey('public', true, al, ecUsages(n, 'public', usages), { curve: a.namedCurve, pub: kp[1] }), privateKey: mkKey('private', ext, al, ecUsages(n, 'private', usages), { curve: a.namedCurve, priv: kp[0], pub: kp[1] }) };
        }
        throw new DOMException(`${n} key generation is not supported`, 'NotSupportedError');
    },
    exportKey(fmt, key) {
        const d = keyData(key), al = key.algorithm;
        if (!key.extractable) throw new DOMException('key is not extractable', 'InvalidAccessError');
        const jwkBase = { ext: true, key_ops: [...key.usages] };
        if (key.type === 'secret') {
            if (fmt === 'raw') return d.slice(0);
            if (fmt === 'jwk') { const bits = d.byteLength * 8, alg = al.name === 'HMAC' ? 'HS' + al.hash.name.slice(4) : al.name.startsWith('AES-') ? `A${bits}${al.name.slice(4)}` : undefined; return Object.assign({ kty: 'oct', k: b64u.enc(d) }, alg ? { alg } : {}, jwkBase); }
        } else if (key.type === 'public') {
            if (fmt === 'raw') return d.pub.slice(0);
            if (fmt === 'spki') return opErr(N.cSpki(d.curve, d.pub));
            if (fmt === 'jwk') { const [x, y] = ecXY(d.pub); return Object.assign({ kty: 'EC', crv: d.curve, x, y }, jwkBase); }
        } else {
            if (fmt === 'pkcs8') return d.priv.slice(0);
            if (fmt === 'jwk') { const r = opErr(N.cPkcs8Parse(d.priv)), [x, y] = ecXY(r[2]); return Object.assign({ kty: 'EC', crv: r[0], x, y, d: b64u.enc(r[1]) }, jwkBase); }
        }
        throw new DOMException(`Unsupported export format ${fmt}`, 'NotSupportedError');
    },
    importKey(fmt, data, alg, ext, usages) {
        const a = normAlg(alg), n = a.name;
        if (isEC(n)) {
            let curve = a.namedCurve, pub, priv;
            if (fmt === 'raw') pub = cbytes(data);
            else if (fmt === 'spki') { const r = opErr(N.cSpkiParse(cbytes(data))); curve = r[0]; pub = r[1]; }
            else if (fmt === 'pkcs8') { priv = cbytes(data); const r = opErr(N.cPkcs8Parse(priv)); curve = r[0]; pub = r[2]; }
            else if (fmt === 'jwk') { curve = data.crv || curve; pub = ecPoint(b64u.dec(data.x), b64u.dec(data.y)); if (data.d) priv = opErr(N.cEcFromD(curve, b64u.dec(data.d), pub)); }
            else throw new DOMException(`Unsupported import format ${fmt}`, 'NotSupportedError');
            if (curve !== a.namedCurve) throw new DOMException('Curve mismatch', 'DataError');
            const type = priv ? 'private' : 'public', al = { name: n, namedCurve: curve };
            if (type === 'public' && !N.cSpki(curve, pub)) throw new DOMException('Invalid EC public key', 'DataError');
            return mkKey(type, type === 'public' ? true : ext, al, ecUsages(n, type, usages), { curve, pub, priv });
        }
        let raw;
        if (fmt === 'raw') raw = cbytes(data);
        else if (fmt === 'jwk' && data && data.kty === 'oct') raw = b64u.dec(data.k);
        else throw new DOMException(`Unsupported import format ${fmt}`, 'NotSupportedError');
        if (n.startsWith('AES-')) { if (![16, 24, 32].includes(raw.byteLength)) throw new DOMException('Invalid AES key length', 'DataError'); return mkKey('secret', ext, { name: n, length: raw.byteLength * 8 }, usages, raw); }
        if (n === 'HMAC') return mkKey('secret', ext, { name: n, hash: { name: hashOf(a.hash) }, length: a.length || raw.byteLength * 8 }, usages, raw);
        if (n === 'HKDF' || n === 'PBKDF2') return mkKey('secret', false, { name: n }, usages, raw);
        throw new DOMException(`${n} import is not supported`, 'NotSupportedError');
    },
    encrypt(alg, key, data, dec, usage) {
        const a = normAlg(alg), d = keyData(key, usage || (dec ? 'decrypt' : 'encrypt'));
        if (key.algorithm.name !== a.name) throw new DOMException('Algorithm does not match key', 'InvalidAccessError');
        const iv = a.name === 'AES-CTR' ? a.counter : a.iv;
        if (!a.name.startsWith('AES-') || a.name === 'AES-KW') throw new DOMException(`${a.name} is not supported`, 'NotSupportedError');
        return opErr(N.cAes(a.name, !dec, d, cbytes(iv), cbytes(data), a.additionalData ? cbytes(a.additionalData) : new ArrayBuffer(0), a.tagLength || 128));
    },
    decrypt(alg, key, data) { return SC.encrypt(alg, key, data, true); },
    sign(alg, key, data) {
        const a = normAlg(alg), d = keyData(key, 'sign');
        if (a.name === 'HMAC') return opErr(N.cHmac(key.algorithm.hash.name, d, cbytes(data)));
        if (a.name === 'ECDSA') return opErr(N.cEcSign(d.curve, hashOf(a.hash), d.priv, cbytes(data)));
        throw new DOMException(`${a.name} signing is not supported`, 'NotSupportedError');
    },
    verify(alg, key, sig, data) {
        const a = normAlg(alg), d = keyData(key, 'verify');
        if (a.name === 'HMAC') { const m = new Uint8Array(opErr(N.cHmac(key.algorithm.hash.name, d, cbytes(data)))), s = new Uint8Array(cbytes(sig)); let diff = m.length ^ s.length; for (let i = 0; i < m.length; i++) diff |= m[i] ^ (s[i] | 0); return diff === 0; }
        if (a.name === 'ECDSA') return N.cEcVerify(d.curve, hashOf(a.hash), d.pub, cbytes(data), cbytes(sig));
        throw new DOMException(`${a.name} verification is not supported`, 'NotSupportedError');
    },
    deriveBits(alg, key, length, usage = 'deriveBits') {
        const a = normAlg(alg), d = keyData(key, usage);
        if (a.name === 'ECDH') { const peer = keyData(a.public); const s = opErr(N.cEcDerive(d.curve, d.priv, peer.pub)); return length == null ? s : s.slice(0, length / 8); }
        if (length == null || length % 8) throw new DOMException('length must be a multiple of 8', 'OperationError');
        if (a.name === 'HKDF') return opErr(N.cHkdf(hashOf(a.hash), d, cbytes(a.salt), cbytes(a.info), length / 8));
        if (a.name === 'PBKDF2') return opErr(N.cPbkdf2(hashOf(a.hash), d, cbytes(a.salt), a.iterations, length / 8));
        throw new DOMException(`${a.name} derivation is not supported`, 'NotSupportedError');
    },
    deriveKey(alg, key, dAlg, ext, usages) {
        const da = normAlg(dAlg), len = da.length || (da.name === 'HMAC' ? hmacLen(hashOf(da.hash)) : undefined);
        return SC.importKey('raw', SC.deriveBits(alg, key, len, 'deriveKey'), da, ext, usages);
    },
    wrapKey(fmt, key, wk, wAlg) {
        const e = SC.exportKey(fmt, key);
        return SC.encrypt(wAlg, wk, fmt === 'jwk' ? new TextEncoder().encode(JSON.stringify(e)) : e, false, 'wrapKey');
    },
    unwrapKey(fmt, data, uk, uAlg, kAlg, ext, usages) {
        const raw = SC.encrypt(uAlg, uk, data, true, 'unwrapKey');
        return SC.importKey(fmt, fmt === 'jwk' ? JSON.parse(new TextDecoder().decode(raw)) : raw, kAlg, ext, usages);
    },
};
class SubtleCrypto { constructor() { throw new TypeError('Illegal constructor'); } }
for (const k of Object.keys(SC)) Object.defineProperty(SubtleCrypto.prototype, k, { value: { [k](...a) { try { return Promise.resolve(SC[k](...a)); } catch (e) { return Promise.reject(e); } } }[k], writable: true, configurable: true });
const subtle = Object.create(SubtleCrypto.prototype);
const crypto = {
    getRandomValues(a) { if (!ArrayBuffer.isView(a)) throw new TypeError('getRandomValues requires an ArrayBufferView'); const u = new Uint8Array(a.buffer, a.byteOffset, a.byteLength); N.random(u.buffer, u.byteOffset, u.byteLength); return a; },
    randomUUID() { const b = crypto.getRandomValues(new Uint8Array(16)); b[6] = (b[6] & 15) | 64; b[8] = (b[8] & 63) | 128; const h = [...b].map(x => x.toString(16).padStart(2, '0')).join(''); return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`; },
    subtle,
};
const mqls = [];
class MediaQueryList extends EventTarget {
    constructor(q) { super(); this.media = q; this.onchange = null; def(this, '_m', N.media(q)); }
    get matches() { return N.media(this.media); }
    addListener(f) { this.addEventListener('change', f); } removeListener(f) { this.removeEventListener('change', f); }
}
function matchMedia(q) { const m = new MediaQueryList(String(q)); mqls.push(new WeakRef(m)); return m; }
function mediaChanged() { for (const r of mqls) { const m = r.deref(); if (!m) continue; const now = N.media(m.media); if (now !== m._m) { m._m = now; dispatch(m, new MediaQueryListEvent('change', { matches: now, media: m.media })); } } }
class MediaQueryListEvent extends Event { constructor(t, i = {}) { super(t, i); this.matches = !!i.matches; this.media = i.media || ''; } }
function getComputedStyle(el) { if (!el || !N.isNode(el) || N.type(el) !== 1) throw new TypeError("Failed to execute 'getComputedStyle': parameter 1 is not of type 'Element'."); return new Proxy(new ComputedStyleDeclaration(el, true), computedHandler); }
const timerFn = (repeat) => (fn, ms, ...args) => { if (typeof fn !== 'function') { const src = String(fn); fn = () => (0, eval)(src); } return N.timer(() => { try { fn(...args); } catch (e) { report(e); } }, Math.max(0, +ms || 0), repeat); };
const setTimeout = timerFn(false), setInterval = timerFn(true);
const clearTimeout = (id) => { if (id) N.clearTimer(id); }, clearInterval = clearTimeout;
const requestAnimationFrame = (fn) => N.raf((t) => { try { fn(t); } catch (e) { report(e); } });
const cancelAnimationFrame = (id) => N.cancelRaf(id);
const requestIdleCallback = (fn, o) => setTimeout(() => fn({ didTimeout: false, timeRemaining() { return 10; } }), 1);
const cancelIdleCallback = clearTimeout;
const queueMicrotask = (fn) => { Promise.resolve().then(() => { try { fn(); } catch (e) { report(e); } }); };
function structuredClone(v, seen = new Map()) {
    if (v === null || typeof v !== 'object') { if (typeof v === 'function' || typeof v === 'symbol') throw new DOMException('could not be cloned.', 'DataCloneError'); return v; }
    if (seen.has(v)) return seen.get(v);
    if (v instanceof Date) return new Date(v); if (v instanceof RegExp) return new RegExp(v.source, v.flags);
    if (v instanceof ArrayBuffer) return v.slice(0); if (ArrayBuffer.isView(v)) return new v.constructor(v);
    if (v instanceof Blob) return v; if (N.isNode(v)) throw new DOMException('could not be cloned.', 'DataCloneError');
    if (v instanceof Map) { const m = new Map(); seen.set(v, m); for (const [k, x] of v) m.set(structuredClone(k, seen), structuredClone(x, seen)); return m; }
    if (v instanceof Set) { const s = new Set(); seen.set(v, s); for (const x of v) s.add(structuredClone(x, seen)); return s; }
    const o = Array.isArray(v) ? [] : {}; seen.set(v, o); for (const k of Object.keys(v)) o[k] = structuredClone(v[k], seen); return o;
}
function postMessage(data, origin) { const [src, org] = N.caller(); const d = structuredClone(data); setTimeout(() => dispatch(G, new MessageEvent('message', { data: d, origin: org, source: asWin(src) }))); }
function queueMessage(data, origin, src) { setTimeout(() => dispatch(G, new MessageEvent('message', { data, origin, source: asWin(src) }))); }
const remoteWins = new Map();
function asWin(v) {
    if (typeof v !== 'number') return v;
    let w = remoteWins.get(v);
    if (w) return w;
    const nav = u => N.ctxRel(v, 4, String(u));
    w = Object.freeze({
        postMessage(d, o) { N.postTo(v, d, o && typeof o === 'object' ? String(o.targetOrigin ?? '/') : String(o ?? '/')); },
        get window() { return w; }, get self() { return w; }, get frames() { return w; },
        get parent() { return asWin(N.ctxRel(v, 0)) ?? w; }, get top() { return asWin(N.ctxRel(v, 1)) ?? w; },
        get length() { return N.ctxRel(v, 2); }, get closed() { return N.ctxRel(v, 3); }, opener: null,
        location: Object.freeze({ set href(u) { nav(u); }, replace: nav, assign: nav }),
        focus() {}, blur() {}, close() {},
    });
    remoteWins.set(v, w);
    return w;
}
const counts = {}, timers = {};
const console = {
    log: (...a) => N.log(1, fmt(a)), info: (...a) => N.log(1, fmt(a)), debug: (...a) => N.log(0, fmt(a)), trace: (...a) => N.log(0, fmt(a)),
    warn: (...a) => N.log(2, fmt(a)), error: (...a) => N.log(3, fmt(a)),
    assert: (c, ...a) => { if (!c) N.log(3, 'Assertion failed: ' + fmt(a)); },
    dir: (...a) => N.log(1, fmt(a)), dirxml: (...a) => N.log(1, fmt(a)), table: (...a) => N.log(1, fmt(a)),
    group: (...a) => N.log(1, fmt(a)), groupCollapsed: (...a) => N.log(1, fmt(a)), groupEnd() {},
    count: (l = 'default') => N.log(1, l + ': ' + (counts[l] = (counts[l] || 0) + 1)), countReset: (l = 'default') => { counts[l] = 0; },
    time: (l = 'default') => { timers[l] = N.now(); }, timeEnd: (l = 'default') => { N.log(1, l + ': ' + (N.now() - (timers[l] || 0)).toFixed(1) + ' ms'); delete timers[l]; }, timeLog: (l = 'default') => N.log(1, l + ': ' + (N.now() - (timers[l] || 0)).toFixed(1) + ' ms'),
    timeStamp() {}, profile() {}, profileEnd() {}, clear() {},
};
const customElements = new CustomElementRegistry();
class Window extends EventTarget {}
installHandlers(Window.prototype, false, EVENT_HANDLERS.concat(['beforeunload', 'hashchange', 'message', 'messageerror', 'offline', 'online', 'pagehide', 'pageshow', 'popstate', 'storage', 'unhandledrejection', 'rejectionhandled', 'unload', 'DOMContentLoaded']));
Object.setPrototypeOf(G, Window.prototype);
{
    const namedGet = (p) => { const d = G.document; return typeof p === 'string' && p && d ? N.byId(p) : null; };
    Object.setPrototypeOf(Window.prototype, new Proxy(EventTarget.prototype, {
        get(t, p, r) { if (typeof p !== 'string' || p in t) return Reflect.get(t, p, r); const e = namedGet(p); return e === null ? undefined : e; },
        has(t, p) { return p in t || namedGet(p) !== null; },
        getOwnPropertyDescriptor(t, p) { const d = Reflect.getOwnPropertyDescriptor(t, p); if (d || typeof p !== 'string') return d; const e = namedGet(p); return e ? { value: e, writable: true, enumerable: false, configurable: true } : undefined; },
    }));
}
const document = N.doc();
Object.setPrototypeOf(document, HTMLDocument.prototype);
function illegalCtor() { throw new TypeError('Illegal constructor'); }
const History = function History() { illegalCtor(); }, Navigator = function Navigator() { illegalCtor(); }, Screen = function Screen() { illegalCtor(); };
const Performance = function Performance() { illegalCtor(); }, Crypto = function Crypto() { illegalCtor(); };
Object.setPrototypeOf(Performance.prototype, EventTarget.prototype);
Object.setPrototypeOf(history, History.prototype); Object.setPrototypeOf(navigator, Navigator.prototype); Object.setPrototypeOf(screen, Screen.prototype);
Object.setPrototypeOf(performance, Performance.prototype); Object.setPrototypeOf(crypto, Crypto.prototype);
const globals = {
    CryptoKey, SubtleCrypto,
    ProcessingInstruction, TouchEvent, CompositionEvent, ClipboardEvent, DragEvent, StorageEvent, PromiseRejectionEvent, SubmitEvent,
    StyleSheet, IdleDeadline, TimeRanges, MediaError, MediaSource, SourceBuffer, SourceBufferList, ImageData, Path2D, CanvasGradient, CanvasPattern, CanvasRenderingContext2D, History, Navigator, Screen, Performance, Crypto, SubtleCrypto,
    DOMException, DOMRectReadOnly, DOMRect, Event, CustomEvent, UIEvent, FocusEvent, MouseEvent, PointerEvent, WheelEvent, KeyboardEvent, InputEvent, ErrorEvent, ProgressEvent, MessageEvent, PopStateEvent, HashChangeEvent, PageTransitionEvent, AnimationEvent, TransitionEvent, MediaQueryListEvent,
    EventTarget, AbortSignal, AbortController, MutationObserver, MutationRecord, Node, NodeList, HTMLCollection: NodeList, CharacterData, Text, CDATASection, Comment, DocumentType, DocumentFragment, ShadowRoot, Attr, NamedNodeMap, DOMTokenList,
    CSSStyleDeclaration, SVGLength, SVGNumber, SVGAnimatedLength, SVGAnimatedNumber, SVGAnimatedInteger, SVGAnimatedBoolean, SVGAnimatedString, SVGAnimatedEnumeration, SVGRect, SVGAnimatedRect, SVGUnitTypes, SVGGeometryElement, SVGRectElement, SVGCircleElement, SVGEllipseElement, SVGLineElement, SVGPathElement, SVGPolylineElement, SVGPolygonElement, SVGGElement, SVGDefsElement, SVGUseElement, SVGImageElement, SVGForeignObjectElement, SVGAElement, SVGSwitchElement, SVGTextContentElement, SVGTextPositioningElement, SVGTextElement, SVGTSpanElement, SVGTextPathElement, SVGGradientElement, SVGLinearGradientElement, SVGRadialGradientElement, SVGStopElement, SVGClipPathElement, SVGMaskElement, SVGPatternElement, SVGFilterElement, SVGMarkerElement, SVGSymbolElement, SVGTitleElement, SVGDescElement, SVGMetadataElement, SVGStyleElement, SVGScriptElement, SVGFEColorMatrixElement, SVGFECompositeElement, SVGFEMorphologyElement, SVGFETurbulenceElement, SVGFEDisplacementMapElement, SVGFEConvolveMatrixElement, SVGComponentTransferFunctionElement, CSSRuleList, CSSStyleRule, CSSGroupingRule, CSSConditionRule, CSSMediaRule, CSSSupportsRule, CSSContainerRule, CSSLayerBlockRule, CSSLayerStatementRule, CSSImportRule, CSSNamespaceRule, CSSFontFaceRule, CSSPageRule, CSSKeyframeRule, CSSKeyframesRule, MediaList, Element, HTMLElement, SVGElement, SVGGraphicsElement, SVGSVGElement, MathMLElement, Image, Audio, Option, CustomElementRegistry, Document, HTMLDocument, XMLDocument, CSSRule, CSSStyleSheet, CSS, FontFace,
    TreeWalker, NodeIterator, NodeFilter, Range, StaticRange, AbstractRange, Selection, Animation, DOMMatrixReadOnly, DOMMatrix, DOMPoint, DOMParser, XMLSerializer,
    TextEncoder, TextDecoder, btoa, atob, Blob, File, FileReader, ReadableStream, URLSearchParams, URL, webkitURL: URL, Headers, Request, Response, fetch, FormData, XMLHttpRequestEventTarget, XMLHttpRequestUpload, XMLHttpRequest, WebSocket, MessagePort, MessageChannel, Worker, BroadcastChannel,
    ResizeObserver, IntersectionObserver, PerformanceObserver, Storage, localStorage, sessionStorage, Location, location, history, navigator, screen, performance, crypto, MediaQueryList, matchMedia, getComputedStyle,
    setTimeout, setInterval, clearTimeout, clearInterval, requestAnimationFrame, cancelAnimationFrame, requestIdleCallback, cancelIdleCallback, queueMicrotask, structuredClone, postMessage, console, customElements, Window, document,
    window: G, self: G, globalThis: G, get top() { return asWin(N.topWin()) ?? G; }, get parent() { return asWin(N.parentWin()) ?? G; }, frames: G, opener: null, get frameElement() { return N.frameEl(); }, closed: false, name: '', get length() { return N.frameCount(); }, origin: location.origin, isSecureContext: location.protocol === 'https:', crossOriginIsolated: false,
    get innerWidth() { return N.viewport()[0]; }, get innerHeight() { return N.viewport()[1]; }, get outerWidth() { return N.viewport()[0]; }, get outerHeight() { return N.viewport()[1] + 40; },
    get scrollX() { return N.viewport()[2]; }, get scrollY() { return N.viewport()[3]; }, get pageXOffset() { return N.viewport()[2]; }, get pageYOffset() { return N.viewport()[3]; }, get devicePixelRatio() { return N.viewport()[4]; },
    screenX: 0, screenY: 0, screenLeft: 0, screenTop: 0,
    scrollTo(x, y) { if (typeof x === 'object' && x) { y = x.top ?? N.viewport()[3]; x = x.left ?? N.viewport()[2]; } N.scrollTo(+x || 0, +y || 0); }, scroll(x, y) { G.scrollTo(x, y); },
    scrollBy(x, y) { if (typeof x === 'object' && x) { y = x.top || 0; x = x.left || 0; } const v = N.viewport(); N.scrollTo(v[2] + (+x || 0), v[3] + (+y || 0)); },
    alert(m) { N.log(1, 'alert: ' + m); }, confirm() { return false; }, prompt() { return null; }, print() {}, focus() {}, blur() {}, close() {}, stop() {}, open() { return null; },
    getSelection() { return selection; }, reportError: report, visualViewport: null, speechSynthesis: undefined,
};
for (const k of Object.keys(H)) globals[k] = H[k];
for (const k of Object.keys(globals)) { const d = Object.getOwnPropertyDescriptor(globals, k); d.configurable = true; if (!d.get) d.writable = true; d.enumerable = false; Object.defineProperty(G, k, d); }
for (const k of Object.keys(globals)) {
    const d = Object.getOwnPropertyDescriptor(globals, k), v = d.value;
    if (typeof v === 'function' && /^[A-Z]/.test(k) && v.prototype && v.prototype.constructor === v && !Object.prototype.hasOwnProperty.call(v.prototype, Symbol.toStringTag))
        Object.defineProperty(v.prototype, Symbol.toStringTag, { value: k, configurable: true });
}
for (let i = 0; i < 32; i++) Object.defineProperty(G, i, { get() { return asWin(N.frameAt(i)); }, configurable: true });
const visual = { get width() { return N.viewport()[0]; }, get height() { return N.viewport()[1]; }, offsetLeft: 0, offsetTop: 0, pageLeft: 0, pageTop: 0, scale: 1, addEventListener() {}, removeEventListener() {} };
G.visualViewport = visual;
// Bare global calls like `addEventListener(...)` run with an undefined receiver in strict code.
for (const k of ['addEventListener', 'removeEventListener', 'dispatchEvent']) {
    const f = EventTarget.prototype[k];
    Object.defineProperty(G, k, { value: function (...a) { return f.apply(this ?? G, a); }, writable: true, configurable: true });
}
function fire(target, type, init, Ctor = Event) { return dispatch(target, new Ctor(type, init), true); }
function fireAnim(t, type, name, elapsed, anim) {
    const init = anim ? { bubbles: true, animationName: name, elapsedTime: elapsed } : { bubbles: true, propertyName: name, elapsedTime: elapsed };
    return dispatch(t, new (anim ? AnimationEvent : TransitionEvent)(type, init), true);
}
return { queueMessage, workerEvent, protoFor, dispatch, fire, fireAnim, report, mediaChanged, ceConnected, Event, MouseEvent, PointerEvent, KeyboardEvent, FocusEvent, WheelEvent, InputEvent, PopStateEvent, ErrorEvent };
})

