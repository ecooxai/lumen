class ResizeObserver {
    constructor(cb) { this._cb = cb; this._t = new Map(); }
    observe(el) { if (this._t.has(el)) return; this._t.set(el, null); roAll.add(this); setTimeout(() => this._check()); roKick(); }
    unobserve(el) { this._t.delete(el); } disconnect() { this._t.clear(); roAll.delete(this); }
    _check() {
        const recs = [];
        for (const [el, old] of this._t) { const r = N.rect(el) || [0, 0, 0, 0]; const k = r[2] + 'x' + r[3]; if (k === old) continue; this._t.set(el, k); const cr = new DOMRect(0, 0, r[2], r[3]); const sz = [{ inlineSize: r[2], blockSize: r[3] }]; recs.push({ target: el, contentRect: cr, borderBoxSize: sz, contentBoxSize: sz, devicePixelContentBoxSize: sz }); }
        if (recs.length) { try { this._cb(recs, this); } catch (e) { report(e); } }
    }
}
class IntersectionObserver {
    constructor(cb, o = {}) { this._cb = cb; this._t = new Map(); this.root = o.root || null; this.rootMargin = o.rootMargin || '0px'; this.thresholds = [].concat(o.threshold ?? 0); const m = /^\s*(-?[\d.]+)(px|%)?/.exec(this.rootMargin); this._m = m ? +m[1] * (m[2] === '%' ? N.viewport()[1] / 100 : 1) : 0; }
    observe(el) { if (this._t.has(el)) return; this._t.set(el, null); ioAll.add(this); ioKick(); }
    _check() { const v = N.viewport(), m = this._m, out = []; for (const [el, prev] of this._t) { const r = el.getBoundingClientRect(); const vis = el.isConnected && r.bottom > -m && r.top < v[1] + m && r.right > -m && r.left < v[0] + m && r.width + r.height > 0; if (vis === prev) continue; this._t.set(el, vis); out.push({ target: el, isIntersecting: vis, intersectionRatio: vis ? 1 : 0, boundingClientRect: r, intersectionRect: vis ? r : new DOMRect(), rootBounds: new DOMRect(0, 0, v[0], v[1]), time: N.now() }); } if (out.length) try { this._cb(out, this); } catch (e) { report(e); } }
    unobserve(el) { this._t.delete(el); } disconnect() { this._t.clear(); ioAll.delete(this); } takeRecords() { return []; }
}
const roAll = new Set();
let roTimer = 0;
/* sizes change after layout, not only at observe(): re-check every observed element while any observer is live */
const roKick = () => { if (!roTimer) roTimer = setInterval(() => { if (!roAll.size) { clearInterval(roTimer); roTimer = 0; return; } for (const o of [...roAll]) if (o._t.size) o._check(); else roAll.delete(o); }, 100); };
const ioAll = new Set();
let ioPending = false, ioTimer = 0;
const ioRun = () => { ioPending = false; for (const o of [...ioAll]) if (o._t.size) o._check(); else ioAll.delete(o); };
const ioKick = () => { if (!ioPending) { ioPending = true; setTimeout(ioRun, 0); } if (!ioTimer) ioTimer = setInterval(() => { if (!ioAll.size) { clearInterval(ioTimer); ioTimer = 0; } else ioRun(); }, 300); };
class PerformanceObserver { constructor(cb) { this._cb = cb; } observe() {} disconnect() {} takeRecords() { return []; } static get supportedEntryTypes() { return []; } }
class Storage {
    constructor(save) { def(this, '_m', new Map()); def(this, '_s', save || null); }
    get length() { return this._m.size; } key(i) { return [...this._m.keys()][i] ?? null; }
    getItem(k) { return this._m.has(String(k)) ? this._m.get(String(k)) : null; }
    setItem(k, v) { this._m.set(String(k), String(v)); this._s?.(this._m); } removeItem(k) { if (this._m.delete(String(k))) this._s?.(this._m); } clear() { this._m.clear(); this._s?.(this._m); }
}
const storageProxy = (st = new Storage()) => new Proxy(st, {
    get(t, k) { if (typeof k !== 'string' || k in t) { const v = Reflect.get(t, k, t); return typeof v === 'function' ? v.bind(t) : v; } return t.getItem(k) ?? undefined; },
    set(t, k, v) { t.setItem(k, v); return true; }, deleteProperty(t, k) { t.removeItem(k); return true; },
    has(t, k) { return k in t || t._m.has(String(k)); }, ownKeys(t) { return [...t._m.keys()]; },
    getOwnPropertyDescriptor(t, k) { return t._m.has(k) ? { value: t._m.get(k), enumerable: true, configurable: true, writable: true } : undefined; },
});
const ssOrigin = (() => { try { return new URL(N.url()).origin; } catch { return 'null'; } })();
const sessionStorage = storageProxy(new Storage(ssOrigin === 'null' ? null : m => N.ssSet(ssOrigin, JSON.stringify([...m]))));
try { const j = ssOrigin !== 'null' && N.ssGet(ssOrigin); if (j) for (const [k, v] of JSON.parse(j)) sessionStorage.setItem(k, v); } catch {}
const localStorage = storageProxy();
class Location {
    get href() { return N.url(); } set href(v) { this.assign(v); }
    assign(u) { const n = new URL(String(u), document.baseURI); const cur = new URL(N.url()); if (n.href.split('#')[0] === cur.href.split('#')[0] && n.hash) { const old = cur.href; N.setUrl(n.href, true); setTimeout(() => dispatch(G, new HashChangeEvent('hashchange', { oldURL: old, newURL: n.href }))); } else N.navigate(n.href); }
    replace(u) { N.navigate(new URL(String(u), document.baseURI).href); }
    reload() { N.navigate(N.url()); }
    toString() { return N.url(); }
    get ancestorOrigins() { return []; }
}
URL_KEYS.forEach((k, i) => { if (k !== 'href') acc(Location.prototype, k, function () { return N.urlParse(N.url(), null)[i]; }, k === 'origin' ? undefined : function (v) { const u = new URL(N.url()); u[k] = v; this.assign(u.href); }); });
const location_ = new Location();
/* [LegacyUnforgeable]: `window.location = url` and bare `location = url` navigate */
Object.defineProperty(globalThis, 'location', { get() { return location_; }, set(v) { location_.href = v; }, enumerable: true, configurable: false });
let histState = null, histIdx = 0;
const histStates = [null];
Object.defineProperty(globalThis, '__lumenPopState', { value: d => {
    histIdx = Math.max(0, Math.min(histStates.length - 1, histIdx + d));
    histState = histStates[histIdx] ?? null;
    globalThis.dispatchEvent(new PopStateEvent('popstate', { state: histState }));
} });
const history = {
    get length() { return N.histLen(); }, get state() { return histState; }, scrollRestoration: 'auto',
    pushState(s, t, u) { if (this !== history && this && typeof this.pushState === 'function' && this.pushState !== history.pushState) return this.pushState(s, t, u); histState = structuredClone(s); histStates.length = ++histIdx; histStates[histIdx] = histState; N.setUrl(u != null ? new URL(String(u), N.url()).href : N.url(), true); },
    replaceState(s, t, u) { if (this !== history && this && typeof this.replaceState === 'function' && this.replaceState !== history.replaceState) return this.replaceState(s, t, u); histState = structuredClone(s); histStates[histIdx] = histState; if (u != null) N.setUrl(new URL(String(u), N.url()).href, false); },
    back() { N.histGo(-1); }, forward() { N.histGo(1); }, go(d) { N.histGo(d | 0); },
};
class BatteryManager extends EventTarget {}
const battery = Object.assign(Reflect.construct(EventTarget, [], BatteryManager), { charging: true, chargingTime: 0, dischargingTime: Infinity, level: 1, onchargingchange: null, onchargingtimechange: null, ondischargingtimechange: null, onlevelchange: null });
const __uaChrome = (/ Chrome\/(\d+)/.exec(N.userAgent()) || [])[1];
const navigator = {
    gpu: LUMEN_GPU,
    userAgent: N.userAgent(), appVersion: N.userAgent().replace(/^Mozilla\//, ''), appName: 'Netscape', appCodeName: 'Mozilla', product: 'Gecko', productSub: __uaChrome ? '20030107' : '20100101', vendor: __uaChrome ? 'Google Inc.' : '', vendorSub: '',
    platform: N.platform(), language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: true, doNotTrack: null, webdriver: false, pdfViewerEnabled: false,
    hardwareConcurrency: N.cpus(), deviceMemory: 8, maxTouchPoints: 0, plugins: [], mimeTypes: [],
    userAgentData: __uaChrome ? { brands: [{ brand: 'Google Chrome', version: __uaChrome }, { brand: 'Not?A_Brand', version: '8' }, { brand: 'Chromium', version: __uaChrome }], mobile: false, platform: N.platform() === 'MacIntel' ? 'macOS' : 'Linux', getHighEntropyValues() { return Promise.resolve({}); }, toJSON() { return {}; } } : undefined,
    connection: { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false, addEventListener() {}, removeEventListener() {} },
    javaEnabled() { return false; }, sendBeacon() { return true; }, vibrate() { return false; }, registerProtocolHandler() {},
    clipboard: Object.assign(new EventTarget(), {
        writeText(t) { N.clipSet(String(t)); return Promise.resolve(); },
        readText() { return Promise.resolve(N.clipGet()); },
        async write(items) { for (const it of items || []) if (it.types.includes('text/plain')) { N.clipSet(await (await it.getType('text/plain')).text()); return; } },
        async read() { return [new ClipboardItem({ 'text/plain': new Blob([N.clipGet()], { type: 'text/plain' }) })]; },
    }),
    permissions: { query() { return Promise.resolve({ state: 'prompt', addEventListener() {} }); } },
    mediaDevices: Object.assign(new EventTarget(), { enumerateDevices() { return Promise.resolve([]); }, getUserMedia(c) {
            if (!c || typeof c !== 'object' || (!c.audio && !c.video)) return Promise.reject(new TypeError("Failed to execute 'getUserMedia': At least one of audio and video must be requested"));
            const t = [];
            if (c.audio) t.push(new MediaStreamTrack(RTC_INTERNAL, 'audio', 'Fake Default Audio Input'));
            if (c.video) t.push(new MediaStreamTrack(RTC_INTERNAL, 'video', 'Fake Default Video Input'));
            return new Promise(r => setTimeout(() => r(new MediaStream(t))));
        }, getDisplayMedia() { return Promise.reject(new DOMException('Capture is not implemented yet', 'NotSupportedError')); }, getSupportedConstraints() { return {}; } }),
    serviceWorker: Object.assign(new EventTarget(), {
        controller: null, ready: new Promise(() => {}),
        register() { return Promise.reject(new DOMException('Service workers are not supported', 'SecurityError')); },
        getRegistration() { return Promise.resolve(undefined); }, getRegistrations() { return Promise.resolve([]); },
        startMessages() {},
    }),
    storage: { estimate() { return Promise.resolve({ quota: 1e9, usage: 0 }); }, persist() { return Promise.resolve(false); } },
    locks: { request(n, o, cb) { cb = typeof o === 'function' ? o : cb; return Promise.resolve().then(() => cb({ name: n })); } },
    getGamepads() { return []; }, getBattery() { return Promise.resolve(battery); },
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
    const o = typeof a === 'string' || a instanceof String ? { name: String(a) } : Object.assign({}, a);
    if (a && typeof a === 'object') for (const k of ['name', 'hash', 'length', 'namedCurve', 'iv', 'counter', 'additionalData', 'tagLength', 'salt', 'info', 'iterations', 'publicExponent', 'modulusLength', 'saltLength', 'label', 'public']) if (k in a) o[k] = a[k];
    const n = ALGS.find(x => x.toLowerCase() === String(o.name).toLowerCase());
    if (!n) throw new DOMException('Unrecognized algorithm name' + (o.name !== undefined ? ': ' + String(o.name) : ''), 'NotSupportedError');
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
function viewportChanged() { mediaChanged(); try { dispatch(G, new Event('resize')); } catch (e) { report(e); } }
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
    const tag = Object.prototype.toString.call(v).slice(8, -1);
    if (tag === 'Date') return new Date(Date.prototype.getTime.call(v)); if (tag === 'RegExp') return new RegExp(v.source, v.flags);
    if (tag === 'ArrayBuffer') { const b = new ArrayBuffer(v.byteLength); new Uint8Array(b).set(new Uint8Array(v)); seen.set(v, b); return b; }
    if (ArrayBuffer.isView(v)) { const b = structuredClone(v.buffer, seen); return tag === 'DataView' ? new DataView(b, v.byteOffset, v.byteLength) : new globalThis[tag](b, v.byteOffset, v.length); }
    if (v instanceof Blob || tag === 'Blob' || tag === 'File') return v; if (N.isNode(v)) throw new DOMException('could not be cloned.', 'DataCloneError');
    if (tag === 'Map') { const m = new Map(); seen.set(v, m); for (const [k, x] of v) m.set(structuredClone(k, seen), structuredClone(x, seen)); return m; }
    if (tag === 'Set') { const s = new Set(); seen.set(v, s); for (const x of v) s.add(structuredClone(x, seen)); return s; }
    const o = Array.isArray(v) ? [] : {}; seen.set(v, o); for (const k of Object.keys(v)) o[k] = structuredClone(v[k], seen); return o;
}
const xferPorts = (o, t) => { const tr = o && typeof o === 'object' ? o.transfer : t; return (tr && typeof tr[Symbol.iterator] === 'function' ? [...tr] : []).filter(p => p && typeof p.postMessage === 'function' && typeof p.start === 'function'); };
function postMessage(data, origin, transfer) { if (!arguments.length) throw new TypeError("Failed to execute 'postMessage' on 'Window': 1 argument required, but only 0 present."); const [src, org] = N.caller(); const d = structuredClone(data), ports = xferPorts(origin, transfer); setTimeout(() => dispatch(G, new MessageEvent('message', { data: d, origin: org, source: asWin(src), ports }), true)); }
function queueMessage(data, origin, src, ports) { setTimeout(() => dispatch(G, new MessageEvent('message', { data, origin, source: asWin(src), ports: ports ? [...ports] : [] }), true)); }
const remoteWins = new Map();
function asWin(v) {
    if (typeof v !== 'number') return v;
    let w = remoteWins.get(v);
    if (w) return w;
    const nav = u => N.ctxRel(v, 4, String(u));
    w = Object.freeze({
        postMessage(d, o, t) { N.postTo(v, d, o && typeof o === 'object' ? String(o.targetOrigin ?? '/') : String(o ?? '/'), xferPorts(o, t)); },
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
const LOG_LEVEL = N.logLevel();
const clog = (lv, a, pre = '') => { if (lv >= LOG_LEVEL) N.log(lv, pre + fmt(a)); };
const console = {
    log: (...a) => clog(1, a), info: (...a) => clog(1, a), debug: (...a) => clog(0, a), trace: (...a) => clog(0, a),
    warn: (...a) => clog(2, a), error: (...a) => clog(3, a),
    assert: (c, ...a) => { if (!c) clog(3, a, 'Assertion failed: '); },
    dir: (...a) => clog(1, a), dirxml: (...a) => clog(1, a), table: (...a) => clog(1, a),
    group: (...a) => clog(1, a), groupCollapsed: (...a) => clog(1, a), groupEnd() {},
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
    ProcessingInstruction, TouchEvent, CompositionEvent, ClipboardEvent, ClipboardItem, DragEvent, StorageEvent, PromiseRejectionEvent, SubmitEvent,
    StyleSheet, IdleDeadline, TimeRanges, MediaError, MediaSource, SourceBuffer, SourceBufferList, ImageData, Path2D, CanvasGradient, CanvasPattern, CanvasRenderingContext2D, History, Navigator, Screen, Performance, Crypto, SubtleCrypto,
    DOMException, DOMRectReadOnly, DOMRect, Event, CustomEvent, UIEvent, FocusEvent, MouseEvent, PointerEvent, WheelEvent, KeyboardEvent, InputEvent, ErrorEvent, ProgressEvent, MessageEvent, PopStateEvent, HashChangeEvent, PageTransitionEvent, AnimationEvent, TransitionEvent, MediaQueryListEvent,
    EventTarget, AbortSignal, AbortController, MutationObserver, MutationRecord, Node, NodeList, HTMLCollection, AnimationTimeline, DocumentTimeline, CharacterData, Text, CDATASection, Comment, DocumentType, DocumentFragment, ShadowRoot, Attr, NamedNodeMap, DOMTokenList,
    CSSStyleDeclaration, SVGFEBlendElement, SVGFEComponentTransferElement, SVGFEDiffuseLightingElement, SVGFESpecularLightingElement, SVGFEDropShadowElement, SVGFEFloodElement, SVGFEGaussianBlurElement, SVGFEImageElement, SVGFEMergeElement, SVGFEMergeNodeElement, SVGFEOffsetElement, SVGFETileElement, SVGFEDistantLightElement, SVGFEPointLightElement, SVGFESpotLightElement, SVGFEFuncRElement, SVGFEFuncGElement, SVGFEFuncBElement, SVGFEFuncAElement, SVGLength, SVGNumber, SVGAnimatedLength, SVGAnimatedNumber, SVGAnimatedInteger, SVGAnimatedBoolean, SVGAnimatedString, SVGAnimatedEnumeration, SVGRect, SVGAnimatedRect, SVGUnitTypes, SVGGeometryElement, SVGRectElement, SVGCircleElement, SVGEllipseElement, SVGLineElement, SVGPathElement, SVGPolylineElement, SVGPolygonElement, SVGGElement, SVGDefsElement, SVGUseElement, SVGImageElement, SVGForeignObjectElement, SVGAElement, SVGSwitchElement, SVGTextContentElement, SVGTextPositioningElement, SVGTextElement, SVGTSpanElement, SVGTextPathElement, SVGGradientElement, SVGLinearGradientElement, SVGRadialGradientElement, SVGStopElement, SVGClipPathElement, SVGMaskElement, SVGPatternElement, SVGFilterElement, SVGMarkerElement, SVGSymbolElement, SVGTitleElement, SVGDescElement, SVGMetadataElement, SVGStyleElement, SVGScriptElement, SVGFEColorMatrixElement, SVGFECompositeElement, SVGFEMorphologyElement, SVGFETurbulenceElement, SVGFEDisplacementMapElement, SVGFEConvolveMatrixElement, SVGComponentTransferFunctionElement, CSSRuleList, CSSStyleRule, CSSGroupingRule, CSSConditionRule, CSSMediaRule, CSSSupportsRule, CSSContainerRule, CSSLayerBlockRule, CSSLayerStatementRule, CSSImportRule, CSSNamespaceRule, CSSFontFaceRule, CSSPageRule, CSSKeyframeRule, CSSKeyframesRule, MediaList, Element, HTMLElement, SVGElement, SVGGraphicsElement, SVGSVGElement, MathMLElement, Image, Audio, Option, CustomElementRegistry, Document, HTMLDocument, XMLDocument, CSSRule, CSSStyleSheet, CSS, FontFace,
    TreeWalker, NodeIterator, NodeFilter, Range, StaticRange, AbstractRange, Selection, Animation, DOMMatrixReadOnly, DOMMatrix, DOMPoint, DOMParser, XMLSerializer,
    TextEncoder, TextDecoder, btoa, atob, Blob, File, FileList, DataTransfer, DataTransferItem, DataTransferItemList, FileReader, ReadableStream, ReadableStreamDefaultReader, WritableStream, WritableStreamDefaultWriter, TransformStream, ByteLengthQueuingStrategy, CountQueuingStrategy, TextEncoderStream, TextDecoderStream, URLSearchParams, URL, webkitURL: URL, Headers, Request, Response, fetch, FormData, XMLHttpRequestEventTarget, XMLHttpRequestUpload, XMLHttpRequest, WebSocket, CloseEvent, MessagePort, MessageChannel, Worker, BroadcastChannel,
    RTCPeerConnection, RTCSessionDescription, RTCIceCandidate, RTCPeerConnectionIceEvent, RTCPeerConnectionIceErrorEvent, RTCDataChannel, RTCDataChannelEvent, RTCTrackEvent, RTCRtpSender, RTCRtpReceiver, RTCRtpTransceiver, RTCIceTransport, RTCDtlsTransport, RTCSctpTransport, RTCCertificate, RTCStatsReport, RTCError, RTCErrorEvent, MediaStream, MediaStreamTrack, MediaStreamTrackEvent,
    ResizeObserver, IntersectionObserver, HTMLAllCollection, PerformanceObserver, Storage, localStorage, sessionStorage, Location, history, navigator, screen, performance, crypto, MediaQueryList, matchMedia, getComputedStyle,
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
const CAPTURED_ = new Set(['pointermove', 'pointerup', 'pointercancel', 'mousemove', 'mouseup']);
function fire(target, type, init, Ctor = Event) {
    if (pointerCapture_ && CAPTURED_.has(type)) { if (pointerCapture_.isConnected) target = pointerCapture_; else pointerCapture_ = null; }
    const pre = type === 'click' ? preActivate(target) : null;
    const ev = new Ctor(type, init), ok = dispatch(target, ev, true);
    if ((type === 'pointerup' || type === 'pointercancel') && pointerCapture_) releaseCapture_();
    if (!ok && pre && pre.restore) pre.restore();
    if (ok && type === 'click' && target && target.nodeType === 1 && !(target.closest && target.closest('a[href]')) && !(target.disabled || (target.closest && target.closest('button:disabled')))) {
        try { activate(target, ev, pre); } catch (e) { report(e); }   /* anchors are navigated natively by the shell */
    }
    return ok;
}
function fireAnim(t, type, name, elapsed, anim) {
    const init = anim ? { bubbles: true, animationName: name, elapsedTime: elapsed } : { bubbles: true, propertyName: name, elapsedTime: elapsed };
    return dispatch(t, new (anim ? AnimationEvent : TransitionEvent)(type, init), true);
}
{
    const editHost = n => {
        for (; n; n = n.parentNode) if (n.nodeType === 1) {
            const v = n.getAttribute('contenteditable');
            if (v === '' || v === 'true' || v === 'plaintext-only') return n;
            if (v === 'false') return null;
        }
        return null;
    };
    const caret = h => {
        const s = document.getSelection();
        if (s.rangeCount && h.contains(s.anchorNode)) return s;
        let n = h;
        while (n.lastChild && !(n.lastChild.nodeType === 1 && n.lastChild.tagName === 'BR')) n = n.lastChild;
        s.collapse(n, n.nodeType === 3 ? n.data.length : n.childNodes.length);
        return s;
    };
    const fire = (h, type, inputType, data) => h.dispatchEvent(new InputEvent(type, { inputType, data, bubbles: true, cancelable: type === 'beforeinput', composed: true }));
    const edit = (e, del) => {
        e.stopImmediatePropagation();
        const h = editHost(e.target); if (!h) return;
        const s = caret(h), data = del ? null : e.key, type = del ? 'deleteContentBackward' : 'insertText';
        if (!e.lumenExec && !fire(h, 'beforeinput', type, data)) return;
        if (!s.isCollapsed) {
            const blocks = [...h.children].some(c => /^(P|DIV|LI|H[1-6]|BLOCKQUOTE|PRE)$/.test(c.tagName));
            s.deleteFromDocument();
            if (blocks && !h.firstChild) { const p = document.createElement('p'); p.appendChild(document.createElement('br')); h.appendChild(p); s.collapse(p, 0); }
            else s.collapse(s.anchorNode, s.anchorOffset);
            if (del) { fire(h, 'input', type, data); document.dispatchEvent(new Event('selectionchange')); return; }
        }
        const c = s.focusNode, o = s.focusOffset;
        if (del) {
            if (c.nodeType !== 3 || !o) return;
            const n = o > 1 && /[\udc00-\udfff]/.test(c.data[o - 1]) ? 2 : 1;
            c.deleteData(o - n, n); s.collapse(c, o - n);
        } else if (c.nodeType === 3) { c.insertData(o, data); s.collapse(c, o + data.length); }
        else {
            let ref = c.childNodes[o] ?? null;
            if (!ref && c.lastChild && c.lastChild.nodeType === 1 && c.lastChild.tagName === 'BR') ref = c.lastChild;
            const t = document.createTextNode(data); c.insertBefore(t, ref); s.collapse(t, data.length);
        }
        fire(h, 'input', type, data);
        document.dispatchEvent(new Event('selectionchange'));
    };
    document.addEventListener('lumenedit', e => edit(e, false), true);
    document.addEventListener('lumeneditdel', e => edit(e, true), true);
    /* native mouse/keyboard selection in contenteditable: target + byte offset (clientX), extend flag (clientY) */
    const texts = h => { const a = [], tw = document.createTreeWalker(h, 4); for (let n; (n = tw.nextNode());) a.push(n); return a; };
    const wordCh = /[\p{L}\p{N}_]/u;
    const selAt = (type, f) => document.addEventListener('lumensel-' + type, e => {
        e.stopImmediatePropagation();
        const n = e.target, h = editHost(n); if (!h) return;
        f(document.getSelection(), n, n.nodeType === 3 ? utf16Off(n.data, e.clientX | 0) : e.clientX | 0, h, e.clientY > 0);
        document.dispatchEvent(new Event('selectionchange'));
    }, true);
    const to = (s, ext, n, o) => ext ? s.extend(n, o) : s.collapse(n, o);
    selAt('collapse', (s, n, o) => s.collapse(n, o));
    selAt('extend', (s, n, o, h) => s.rangeCount && h.contains(s.anchorNode) ? s.extend(n, o) : s.collapse(n, o));
    selAt('word', (s, n, o) => {
        if (n.nodeType !== 3) return s.collapse(n, o);
        const d = n.data; let a = o, b = o;
        while (a > 0 && wordCh.test(d[a - 1])) a--;
        while (b < d.length && wordCh.test(d[b])) b++;
        if (a === b && b < d.length) b++;
        s.setBaseAndExtent(n, a, n, b);
    });
    selAt('all', (s, n, o, h) => s.selectAllChildren(h));
    selAt('home', (s, n, o, h, ext) => { const t = texts(h); to(s, ext, t.length ? t[0] : h, 0); });
    selAt('end', (s, n, o, h, ext) => { const t = texts(h), l = t[t.length - 1]; l ? to(s, ext, l, l.data.length) : to(s, ext, h, h.childNodes.length); });
    const step = (h, n, o, dir) => {
        const t = texts(h);
        if (!t.length) return [n, o];
        if (n.nodeType !== 3) {
            const c = n.childNodes[o], i = c ? t.findIndex(x => c.contains(x) || (c.compareDocumentPosition(x) & 4)) : -1;
            if (i < 0) { n = t[t.length - 1]; o = n.data.length; } else { n = t[i]; o = 0; }
        }
        const i = t.indexOf(n), d = n.data;
        if (dir < 0) {
            if (o > 0) o -= o > 1 && /[\udc00-\udfff]/.test(d[o - 1]) ? 2 : 1;
            else if (i > 0) { const p = t[i - 1]; o = p.data.length - (p.parentNode === n.parentNode && p.data.length ? 1 : 0); n = p; }
        } else if (o < d.length) o += /[\ud800-\udbff]/.test(d[o]) ? 2 : 1;
        else if (i < t.length - 1) { const q = t[i + 1]; o = q.parentNode === n.parentNode && q.data.length ? 1 : 0; n = q; }
        return [n, o];
    };
    const move = dir => (s, n, o, h, ext) => {
        if (!s.rangeCount || !h.contains(s.focusNode)) caret(h);
        if (!ext && !s.isCollapsed) { const r = s.getRangeAt(0); return dir < 0 ? s.collapse(r.startContainer, r.startOffset) : s.collapse(r.endContainer, r.endOffset); }
        to(s, ext, ...step(h, s.focusNode, s.focusOffset, dir));
    };
    selAt('left', move(-1)); selAt('right', move(1));
}
/* getUserMedia (real camera/mic), enumerateDevices, permissions, geolocation */
(() => {
    const origin = () => { try { return location.origin || 'file://'; } catch { return 'file://'; } };
    const PN = { camera: 'camera', microphone: 'microphone', geolocation: 'location', 'display-capture': 'screen' };
    const state = n => { const s = N.capState(PN[n] || n, origin()); return s > 0 ? 'granted' : s < 0 ? 'denied' : 'prompt'; };
    const ask = n => N.capAsk(PN[n] || n, origin());
    class PermissionStatus extends EventTarget { constructor(n) { super(); this.name = n; this.onchange = null; } get state() { return state(this.name); } }
    navigator.permissions.query = d => { const n = d && d.name; if (!n) return Promise.reject(new TypeError('name required')); return Promise.resolve(new PermissionStatus(n)); };
    const ideal = v => v == null ? 0 : typeof v === 'number' ? v : v.exact || v.ideal || v.max || v.min || 0;
    const md = navigator.mediaDevices;
    md.enumerateDevices = () => Promise.resolve([
        ...N.capCameras().map((l, i) => ({ deviceId: 'cam' + i, groupId: 'g' + i, kind: 'videoinput', label: state('camera') === 'granted' ? l : '', toJSON() { return { ...this }; } })),
        ...N.capMics().map((l, i) => ({ deviceId: i ? 'mic' + i : 'default', groupId: 'm' + i, kind: 'audioinput', label: state('microphone') === 'granted' ? l : '', toJSON() { return { ...this }; } }))]);
    md.getSupportedConstraints = () => ({ deviceId: true, width: true, height: true, frameRate: true, aspectRatio: true, facingMode: true, sampleRate: true, channelCount: true, echoCancellation: true, noiseSuppression: true, autoGainControl: true });
    const NA = () => new DOMException('Permission denied', 'NotAllowedError'), NF = k => new DOMException('Requested device not found (' + k + ')', 'NotFoundError');
    md.getUserMedia = c => new Promise((res, rej) => setTimeout(() => {
        if (!c || typeof c !== 'object' || (!c.audio && !c.video)) return rej(new TypeError("Failed to execute 'getUserMedia': At least one of audio and video must be requested"));
        const tracks = [];
        try {
            if (c.video) { const cams = N.capCameras(); if (!cams.length) throw NF('camera'); if (!ask('camera')) throw NA();
                const v = typeof c.video === 'object' ? c.video : {}; let idx = 0; const did = ideal(v.deviceId); if (typeof did === 'string' && /^cam\d+$/.test(did)) idx = +did.slice(3);
                const id = N.capCamOpen(idx, ideal(v.width), ideal(v.height), ideal(v.frameRate)); if (id < 0) throw NA(); if (!id) throw new DOMException('Could not start video source', 'NotReadableError');
                const t = new MediaStreamTrack(RTC_INTERNAL, 'video', cams[idx]); t._.cam = id; t._.dev = 'cam' + idx; tracks.push(t); }
            if (c.audio) { const mics = N.capMics(); if (!mics.length) throw NF('microphone'); if (!ask('microphone')) throw NA();
                const a = typeof c.audio === 'object' ? c.audio : {}; const id = N.capMicOpen(ideal(a.sampleRate) || 48000, ideal(a.channelCount) || 1);
                if (!id) throw new DOMException('Could not start audio source', 'NotReadableError');
                const t = new MediaStreamTrack(RTC_INTERNAL, 'audio', mics[0]); t._.mic = id; t._.rate = ideal(a.sampleRate) || 48000; t._.ch = ideal(a.channelCount) || 1; tracks.push(t); }
        } catch (e) { for (const t of tracks) t.stop(); return rej(e); }
        res(new MediaStream(tracks));
    }));
    md.getDisplayMedia = (c = {}) => new Promise((res, rej) => setTimeout(() => {
        if (c && (c.video === false || c.audio && !c.video && c.video !== undefined)) return rej(new TypeError("Failed to execute 'getDisplayMedia': video must be requested"));
        if (!ask('display-capture')) return rej(NA());
        const id = N.capScreenOpen(); if (!id) return rej(new DOMException('Could not start video source', 'NotReadableError'));
        const t = new MediaStreamTrack(RTC_INTERNAL, 'video', 'web-contents-media-stream://' + id); t._.cam = id; t._.dev = 'screen'; t._.display = true;
        res(new MediaStream([t]));
    }));
    class BlobEvent extends Event { constructor(t, i = {}) { super(t, i); this.data = i.data; this.timecode = i.timecode || 0; } }
    class MediaRecorder extends EventTarget {
        constructor(stream, o = {}) {
            super(); if (!(stream instanceof MediaStream)) throw new TypeError("Failed to construct 'MediaRecorder': parameter 1 is not of type 'MediaStream'.");
            if (o.mimeType && !MediaRecorder.isTypeSupported(o.mimeType)) throw new DOMException("Failed to construct 'MediaRecorder': Unsupported mimeType: " + o.mimeType, 'NotSupportedError');
            def(this, '_', { id: 0 }); this.stream = stream; this.mimeType = o.mimeType || 'video/mp4'; this.state = 'inactive';
            for (const n of ['start', 'stop', 'dataavailable', 'pause', 'resume', 'error']) this['on' + n] = null;
            this.videoBitsPerSecond = o.videoBitsPerSecond || o.bitsPerSecond || 0; this.audioBitsPerSecond = o.audioBitsPerSecond || 0; this.videoKeyFrameIntervalDuration = 0;
        }
        static isTypeSupported(t) { return /^video\/mp4(;|$)/i.test(String(t).trim()) && !/vp8|vp9|opus/i.test(String(t)); }
        _ev(n, e) { this.dispatchEvent(e || new Event(n)); }
        start() {
            if (this.state !== 'inactive') throw new DOMException("Failed to execute 'start' on 'MediaRecorder': The MediaRecorder's state is '" + this.state + "'.", 'InvalidStateError');
            const t = this.stream.getVideoTracks().find(t => t._.cam && t.readyState === 'live');
            if (!t) throw new DOMException("Failed to execute 'start' on 'MediaRecorder': only video tracks from getDisplayMedia/getUserMedia can be recorded", 'NotSupportedError');
            this._.id = N.recStart(t._.cam, this.videoBitsPerSecond | 0); if (!this._.id) throw new DOMException('Could not start the video encoder', 'NotSupportedError');
            this.state = 'recording'; setTimeout(() => this._ev('start'));
        }
        stop() {
            if (this.state === 'inactive') return; const ab = N.recStop(this._.id); this._.id = 0; this.state = 'inactive';
            setTimeout(() => { this._ev('dataavailable', new BlobEvent('dataavailable', { data: new Blob([ab], { type: this.mimeType }), timecode: Date.now() })); this._ev('stop'); });
        }
        pause() { if (this.state !== 'recording') return; N.recPause(this._.id, true); this.state = 'paused'; setTimeout(() => this._ev('pause')); }
        resume() { if (this.state !== 'paused') return; N.recPause(this._.id, false); this.state = 'recording'; setTimeout(() => this._ev('resume')); }
        requestData() { if (this.state === 'inactive') throw new DOMException("Failed to execute 'requestData' on 'MediaRecorder': The MediaRecorder's state is 'inactive'.", 'InvalidStateError'); }
    }
    Object.assign(globalThis, { MediaRecorder, BlobEvent });
    const TP = MediaStreamTrack.prototype, stop0 = TP.stop, gs0 = TP.getSettings;
    TP.stop = function () { if (this._.cam) { N.capCamClose(this._.cam); this._.cam = 0; } if (this._.mic) { N.capMicClose(this._.mic); this._.mic = 0; } stop0.call(this); };
    TP.getSettings = function () { if (this._.cam) { const [w, h, f] = N.capCamInfo(this._.cam); return { deviceId: this._.dev, groupId: '', width: w, height: h, frameRate: f, aspectRatio: h ? w / h : 0, facingMode: 'user', resizeMode: 'none' }; }
        if (this._.kind === 'audio' && this._.rate) return { deviceId: 'default', groupId: '', sampleRate: this._.rate, channelCount: this._.ch, sampleSize: 32, echoCancellation: false, autoGainControl: false, noiseSuppression: false }; return gs0.call(this); };
    { const gs1 = TP.getSettings; TP.getSettings = function () { const r = gs1.call(this); if (this._.display) { delete r.facingMode; r.displaySurface = 'browser'; r.logicalSurface = true; r.cursor = 'never'; } return r; }; }
    TP.getCapabilities = function () { return this._.kind === 'video' ? { width: { min: 1, max: 4096 }, height: { min: 1, max: 4096 }, frameRate: { min: 1, max: 60 }, facingMode: ['user'] } : { sampleRate: { min: 8000, max: 96000 }, channelCount: { min: 1, max: 2 } }; };
    /** pull recorded PCM (Float32Array, interleaved) for an audio track; used by WebAudio / MediaRecorder */
    TP._readPCM = function () { return this._.mic ? N.capMicRead(this._.mic) || new Float32Array(0) : new Float32Array(0); };
    /* <video>.srcObject = MediaStream with a camera track */
    const VP = H.HTMLVideoElement.prototype, MP = H.HTMLMediaElement.prototype;
    const camOf = el => { const s = el.__so; if (!(s instanceof MediaStream)) return null; return s.getVideoTracks().find(t => t._.cam && t.readyState === 'live') || null; };
    const desc = (p, k) => { for (let o = p; o; o = Object.getPrototypeOf(o)) { const d = Object.getOwnPropertyDescriptor(o, k); if (d) return d; } return null; };
    const so = desc(MP, 'srcObject');
    Object.defineProperty(MP, 'srcObject', { configurable: true, get: so.get, set(v) { so.set.call(this, v); const t = camOf(this); N.capCamBind(t ? t._.cam : 0, this);
        if (v instanceof MediaStream) setTimeout(() => { for (const e of ['loadstart', 'loadedmetadata', 'loadeddata', 'canplay', 'canplaythrough']) this.dispatchEvent(new Event(e)); if (this.autoplay) this.play(); }); } });
    for (const [k, i] of [['videoWidth', 0], ['videoHeight', 1]]) { const d = desc(VP, k); Object.defineProperty(VP, k, { configurable: true, get() { const t = camOf(this); return t ? N.capCamInfo(t._.cam)[i] : d.get.call(this); } }); }
    for (const k of ['readyState', 'paused']) { const d = desc(MP, k); if (d && d.get) Object.defineProperty(MP, k, { configurable: true, get() { if (this.__so instanceof MediaStream) return k === 'readyState' ? 4 : !!this.__spaused; return d.get.call(this); } }); }
    const play0 = MP.play, pause0 = MP.pause;
    MP.play = function () { if (this.__so instanceof MediaStream) { this.__spaused = false; setTimeout(() => { this.dispatchEvent(new Event('play')); this.dispatchEvent(new Event('playing')); }); return Promise.resolve(); } return play0.call(this); };
    MP.pause = function () { if (this.__so instanceof MediaStream) { this.__spaused = true; this.dispatchEvent(new Event('pause')); return; } return pause0.call(this); };
    /* geolocation */
    class GeolocationCoordinates { constructor(o) { Object.assign(this, { latitude: o.lat, longitude: o.lon, accuracy: o.acc, altitude: null, altitudeAccuracy: null, heading: null, speed: null }); } toJSON() { return { ...this }; } }
    class GeolocationPosition { constructor(c) { this.coords = c; this.timestamp = Date.now(); } toJSON() { return { coords: this.coords.toJSON(), timestamp: this.timestamp }; } }
    class GeolocationPositionError { constructor(code, m) { this.code = code; this.message = m; } }
    Object.assign(GeolocationPositionError, { PERMISSION_DENIED: 1, POSITION_UNAVAILABLE: 2, TIMEOUT: 3 }); Object.assign(GeolocationPositionError.prototype, { PERMISSION_DENIED: 1, POSITION_UNAVAILABLE: 2, TIMEOUT: 3 });
    let cache = null;
    async function locate() {
        const f = N.geoFixed(); if (f) { const [lat, lon, acc] = f.split(',').map(Number); return { lat, lon, acc: acc || 50 }; }
        if (cache && Date.now() - cache.t < 600000) return cache.p;
        for (const [u, pick] of [['https://ipapi.co/json/', j => [j.latitude, j.longitude]], ['https://ipinfo.io/json', j => (j.loc || '').split(',').map(Number)]]) {
            try { const j = await (await fetch(u)).json(); const [lat, lon] = pick(j); if (isFinite(lat) && isFinite(lon)) { const p = { lat, lon, acc: 5000 }; cache = { t: Date.now(), p }; return p; } } catch { }
        }
        throw new GeolocationPositionError(2, 'Position unavailable');
    }
    const watches = new Map(); let wid = 0;
    function get(ok, er, o) {
        if (!ask('geolocation')) { setTimeout(() => er && er(new GeolocationPositionError(1, 'User denied Geolocation'))); return; }
        const to = o && o.timeout > 0 && isFinite(o.timeout) ? o.timeout : 0; let done = false;
        if (to) setTimeout(() => { if (!done) { done = true; er && er(new GeolocationPositionError(3, 'Timeout expired')); } }, to);
        locate().then(p => { if (!done) { done = true; ok(new GeolocationPosition(new GeolocationCoordinates(p))); } }, e => { if (!done) { done = true; er && er(e); } });
    }
    const geolocation = { getCurrentPosition(ok, er, o) { get(ok, er, o); },
        watchPosition(ok, er, o) { const id = ++wid; get(ok, er, o); watches.set(id, setInterval(() => get(ok, er, o), 60000)); return id; },
        clearWatch(id) { clearInterval(watches.get(id)); watches.delete(id); } };
    Object.defineProperty(navigator, 'geolocation', { value: geolocation, configurable: true });
    Object.assign(globalThis, { GeolocationCoordinates, GeolocationPosition, GeolocationPositionError, PermissionStatus, Geolocation: function Geolocation() { } });
})();

/* WebIDL attributes are enumerable accessors: libraries copy events with for-in (video.js fixEvent needs e.type) */
(() => {
    const EP = Event.prototype;
    for (const k of Object.getOwnPropertyNames(globalThis)) {
        if (!/^[A-Z]/.test(k)) continue;
        let C; try { C = globalThis[k]; } catch (e) { continue; }
        if (typeof C !== 'function' || !C.prototype || !(C === Event || EP.isPrototypeOf(C.prototype))) continue;
        const p = C.prototype;
        for (const n of Object.getOwnPropertyNames(p)) {
            const d = Object.getOwnPropertyDescriptor(p, n);
            if (d && d.get && !d.enumerable && d.configurable) Object.defineProperty(p, n, { get: d.get, set: d.set, enumerable: true, configurable: true });
        }
    }
})();

return { queueMessage, workerEvent, protoFor, dispatch, fire, fireAnim, report, mediaChanged, viewportChanged, ceConnected, Event, MouseEvent, PointerEvent, KeyboardEvent, FocusEvent, WheelEvent, InputEvent, PopStateEvent, ErrorEvent };
})
