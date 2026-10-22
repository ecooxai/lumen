class ResizeObserver {
    constructor(cb) { this._cb = cb; this._t = new Map(); }
    observe(el) { if (this._t.has(el)) return; this._t.set(el, null); roAll.add(this); N.resizePump(roRun); }
    unobserve(el) { this._t.delete(el); if (!this._t.size) { roAll.delete(this); roStop(); } }
    disconnect() { this._t.clear(); roAll.delete(this); roStop(); }
    _check() {
        const recs = [];
        for (const [el, old] of this._t) { const r = N.rect(el) || [0, 0, 0, 0]; const k = r[2] + 'x' + r[3]; if (k === old) continue; this._t.set(el, k); const cr = new DOMRect(0, 0, r[2], r[3]); const sz = [{ inlineSize: r[2], blockSize: r[3] }]; recs.push({ target: el, contentRect: cr, borderBoxSize: sz, contentBoxSize: sz, devicePixelContentBoxSize: sz }); }
        if (recs.length) { try { this._cb(recs, this); } catch (e) { report(e); } }
    }
}
// Pages test for the entry class (with intersectionRatio on its prototype) before trusting the native
// observer; without it YouTube installs a polyfill that rechecks on every DOM mutation and scroll.
class IntersectionObserverEntry {
    constructor(init = {}) {
        for (const k of ['target', 'time', 'rootBounds', 'boundingClientRect', 'intersectionRect', 'isIntersecting', 'isVisible', 'intersectionRatio'])
            def(this, '_' + k, init[k] ?? null);
    }
    get target() { return this._target; } get time() { return this._time; } get rootBounds() { return this._rootBounds; }
    get boundingClientRect() { return this._boundingClientRect; } get intersectionRect() { return this._intersectionRect; }
    get isIntersecting() { return !!this._isIntersecting; } get isVisible() { return !!this._isVisible; }
    get intersectionRatio() { return this._intersectionRatio || 0; }
}
class IntersectionObserver {
    constructor(cb, o = {}) {
        if (typeof cb !== 'function') throw new TypeError("Failed to construct 'IntersectionObserver': The callback provided as parameter 1 is not a function.");
        this._cb = cb; this._t = new Map(); this.root = o.root || null;
        this._m = ioMargin(o.rootMargin ?? '0px');
        this.rootMargin = this._m.map(([v, pct]) => v + (pct ? '%' : 'px')).join(' ');
        const th = [].concat(o.threshold ?? 0).map(Number).sort((x, y) => x - y);
        if (th.some(t => !(t >= 0 && t <= 1))) throw new RangeError("Failed to construct 'IntersectionObserver': Threshold values must be numbers between 0 and 1");
        this.thresholds = Object.freeze(th.length ? th : [0]);
    }
    observe(el) { if (this._t.has(el)) return; this._t.set(el, null); ioAll.add(this); N.intersectionPump(ioRun); }
    // Per target: the threshold index crossed and whether it touches the root; an entry when either changes
    _check() {
        const root = this.root && this.root.nodeType === 1 ? this.root : null, out = [];
        let rb;
        if (root) { const r = root.getBoundingClientRect(); rb = [r.left, r.top, r.right, r.bottom]; }
        else { const v = N.viewport(); rb = [0, 0, v[0], v[1]]; }
        const m = this._m, w = rb[2] - rb[0], h = rb[3] - rb[1], px = ([v, pct], d) => pct ? v * d / 100 : v;
        rb = [rb[0] - px(m[3], w), rb[1] - px(m[0], h), rb[2] + px(m[1], w), rb[3] + px(m[2], h)];
        const rootOn = !root || root.isConnected;
        for (const [el, prev] of this._t) {
            const r = el.getBoundingClientRect();
            let on = rootOn && el.isConnected && (!root || ioWithin(el, root)), x0 = r.left, y0 = r.top, x1 = r.right, y1 = r.bottom;
            if (on) {
                const c = N.vclip(el, root);   // ancestors with overflow clip cut the target
                if (c === null) on = false;
                else if (c) { x0 = Math.max(x0, c[0]); y0 = Math.max(y0, c[1]); x1 = Math.min(x1, c[2]); y1 = Math.min(y1, c[3]); }
            }
            x0 = Math.max(x0, rb[0]); y0 = Math.max(y0, rb[1]); x1 = Math.min(x1, rb[2]); y1 = Math.min(y1, rb[3]);
            on = on && x0 <= x1 && y0 <= y1;   // edge-adjacent and zero-area targets intersect too
            const area = r.width * r.height, ratio = !on ? 0 : area > 0 ? Math.min(1, (x1 - x0) * (y1 - y0) / area) : 1;
            let idx = 0;
            if (on) while (idx < this.thresholds.length && this.thresholds[idx] <= ratio) idx++;
            if (prev && prev[0] === idx && prev[1] === on) continue;
            this._t.set(el, [idx, on]);
            out.push(new IntersectionObserverEntry({ target: el, isIntersecting: on, isVisible: false, intersectionRatio: ratio, boundingClientRect: r,
                intersectionRect: on ? new DOMRect(x0, y0, x1 - x0, y1 - y0) : new DOMRect(),
                rootBounds: new DOMRect(rb[0], rb[1], rb[2] - rb[0], rb[3] - rb[1]), time: N.now() }));
        }
        if (out.length) try { this._cb(out, this); } catch (e) { report(e); }
    }
    unobserve(el) { this._t.delete(el); if (!this._t.size) { ioAll.delete(this); ioStop(); } }
    disconnect() { this._t.clear(); ioAll.delete(this); ioStop(); } takeRecords() { return []; }
}
// rootMargin is a margin shorthand of one to four px or % lengths; % of the root's width or height
function ioMargin(s) {
    const parts = String(s).trim().split(/\s+/).filter(Boolean).map(t => {
        const m = /^([-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?)(px|%)?$/i.exec(t);
        if (!m || (!m[2] && +m[1] !== 0)) throw new DOMException("Failed to construct 'IntersectionObserver': rootMargin must be specified in pixels or percent.", 'SyntaxError');
        return [+m[1], m[2] === '%'];
    });
    if (parts.length > 4) throw new DOMException("Failed to construct 'IntersectionObserver': Extra text found at the end of rootMargin.", 'SyntaxError');
    const [t = [0, false], r = t, b = t, l = r] = parts;
    return [t, r, b, l];
}
function ioWithin(el, root) {   // through shadow roots
    for (let n = el.parentNode; n; n = n.parentNode || (n.nodeType === 11 ? n.host : null)) if (n === root) return true;
    return false;
}
const roAll = new Set();
const roStop = () => { if (!roAll.size) N.resizePump(null); };
const roRun = () => { for (const o of [...roAll]) if (o._t.size) o._check(); roStop(); };
const ioAll = new Set();
// Native layout/style/scroll changes drive observation; idle documents do no work.
const ioStop = () => { if (!ioAll.size) N.intersectionPump(null); };
const ioRun = () => { for (const o of [...ioAll]) if (o._t.size) o._check(); ioStop(); };
// Marks, measures and the page's navigation entry; observers get them in a microtask, buffered ones too
const PERF_TYPES = Object.freeze(['mark', 'measure', 'navigation']);
const perfObservers = new Set();
const perfNotify = (e) => { for (const o of perfObservers) if (o._types.has(e.entryType)) o._push([e]); };
class PerformanceObserverEntryList {
    constructor(l) { def(this, '_l', l); }
    getEntries() { return this._l.slice(); } getEntriesByType(t) { return this._l.filter(e => e.entryType === t); }
    getEntriesByName(n, t) { return this._l.filter(e => e.name === n && (t == null || e.entryType === t)); }
}
class PerformanceObserver {
    constructor(cb) { if (typeof cb !== 'function') throw new TypeError('PerformanceObserver: callback must be a function'); def(this, '_cb', cb); def(this, '_types', new Set()); def(this, '_q', []); def(this, '_due', false); }
    observe(o) {
        o = o || {}; const ts = o.entryTypes || (o.type != null ? [String(o.type)] : []);
        for (const t of ts) if (PERF_TYPES.includes(t)) this._types.add(t);
        if (!this._types.size) return;
        perfObservers.add(this);
        if (o.buffered && o.type != null && this._types.has(o.type)) this._push(performance.getEntriesByType(o.type));
    }
    _push(l) {
        if (!l.length) return; this._q.push(...l);
        if (this._due) return; this._due = true;
        queueMicrotask(() => { this._due = false; if (!this._q.length || !perfObservers.has(this)) return; const q = this._q.splice(0); try { this._cb.call(this, new PerformanceObserverEntryList(q), this); } catch (e) { report(e); } });
    }
    disconnect() { perfObservers.delete(this); this._q.length = 0; this._types.clear(); }
    takeRecords() { return this._q.splice(0); }
    static get supportedEntryTypes() { return PERF_TYPES; }
}
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
    assign(u) { const n = new URL(String(u), document.baseURI); const cur = new URL(N.url()); if (n.href.split('#')[0] === cur.href.split('#')[0] && n.hash) fragmentNav(n.href, cur.href); else N.navigate(n.href); }
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
/* a navigation to another fragment of this document, as Chrome does it: a new history entry, the
   target scrolled into view, popstate at once (routers such as React Router's follow it) and then
   hashchange; the same fragment again only scrolls */
function fragmentNav(href, old) {
    const hash = href.slice(href.indexOf('#') + 1);
    let id = hash; try { id = decodeURIComponent(hash); } catch (e) {}
    const target = id && (document.getElementById(id) || document.getElementsByName(id)[0]);
    if (href === old) { if (target) target.scrollIntoView(); return; }
    histState = null; histStates.length = ++histIdx; histStates[histIdx] = null;
    N.setUrl(href, true);
    if (target) target.scrollIntoView(); else if (!id || id.toLowerCase() === 'top') G.scrollTo(0, 0);
    dispatch(G, new PopStateEvent('popstate', { state: null }));
    setTimeout(() => dispatch(G, new HashChangeEvent('hashchange', { oldURL: old, newURL: href })));
}
Object.defineProperty(globalThis, '__lumenFragment', { value: u => location.assign(u) });
/* Lumen's own pages (lumen://newtab) read and change the browser's state through this */
if (N.internal('ping') === 'pong') Object.defineProperty(G, 'lumenInternal', { value: cmd => N.internal(String(cmd)) ?? null });
const history = {
    get length() { return N.histLen(); }, get state() { return histState; }, scrollRestoration: 'auto',
    pushState(s, t, u) { if (this !== history && this && typeof this.pushState === 'function' && this.pushState !== history.pushState) return this.pushState(s, t, u); histState = structuredClone(s); histStates.length = ++histIdx; histStates[histIdx] = histState; N.setUrl(u != null ? new URL(String(u), N.url()).href : N.url(), true); },
    replaceState(s, t, u) { if (this !== history && this && typeof this.replaceState === 'function' && this.replaceState !== history.replaceState) return this.replaceState(s, t, u); histState = structuredClone(s); histStates[histIdx] = histState; if (u != null) N.setUrl(new URL(String(u), N.url()).href, false); },
    back() { N.histGo(-1); }, forward() { N.histGo(1); }, go(d) { N.histGo(d | 0); },
};
/* Web Locks for this page: per name, a FIFO of requests granted from the head while compatible
   (shared with shared); a lock is held until its callback's promise settles. */
class Lock { constructor(name, mode) { def(this, '_n', name); def(this, '_m', mode); } get name() { return this._n; } get mode() { return this._m; } }
const lockHeld = [], lockWait = [];
const lockClash = (a, b) => a.name === b.name && (a.mode === 'exclusive' || b.mode === 'exclusive');
function lockPump() {
    for (let i = 0; i < lockWait.length; i++) {
        const r = lockWait[i];
        if (lockHeld.some(h => lockClash(h, r)) || lockWait.slice(0, i).some(q => q.name === r.name)) continue;
        lockWait.splice(i--, 1); lockGrant(r);
    }
}
function lockGrant(r) {
    lockHeld.push(r);
    if (r.signal) r.signal.removeEventListener('abort', r.onabort);
    const done = (ok, v) => {
        const i = lockHeld.indexOf(r);
        if (i >= 0) { lockHeld.splice(i, 1); lockPump(); }
        if (!r.stolen) (ok ? r.res : r.rej)(v);
    };
    Promise.resolve().then(() => r.cb(new Lock(r.name, r.mode))).then(v => done(true, v), e => done(false, e));
}
const locks = {
    request(name, o, cb) {
        if (typeof o === 'function') { cb = o; o = {}; }
        o = o || {}; name = String(name);
        const mode = o.mode === undefined ? 'exclusive' : String(o.mode);
        if (typeof cb !== 'function') return Promise.reject(new TypeError("Failed to execute 'request' on 'LockManager': callback is not a function"));
        if (mode !== 'exclusive' && mode !== 'shared') return Promise.reject(new TypeError("Failed to execute 'request' on 'LockManager': invalid mode"));
        if (name.startsWith('-') || (o.steal && (o.ifAvailable || mode !== 'exclusive')) || (o.signal && (o.steal || o.ifAvailable)))
            return Promise.reject(new DOMException('The request options are not supported.', 'NotSupportedError'));
        if (o.signal && o.signal.aborted) return Promise.reject(o.signal.reason);
        return new Promise((res, rej) => {
            const r = { name, mode, cb, res, rej, signal: o.signal || null, stolen: false, onabort: null };
            if (o.steal) {
                for (let i = lockHeld.length; i--;) if (lockHeld[i].name === name) {
                    const h = lockHeld.splice(i, 1)[0]; h.stolen = true;
                    h.rej(new DOMException('The lock was stolen.', 'AbortError'));
                }
                lockGrant(r); return;
            }
            if (o.ifAvailable) {
                if (lockHeld.some(h => lockClash(h, r)) || lockWait.some(q => q.name === name)) Promise.resolve().then(() => cb(null)).then(res, rej);
                else lockGrant(r);
                return;
            }
            if (r.signal) {
                r.onabort = () => { const i = lockWait.indexOf(r); if (i >= 0) { lockWait.splice(i, 1); rej(r.signal.reason); lockPump(); } };
                r.signal.addEventListener('abort', r.onabort);
            }
            lockWait.push(r); lockPump();
        });
    },
    query() {
        const info = r => ({ name: r.name, mode: r.mode, clientId: 'lumen' });
        return Promise.resolve({ held: lockHeld.map(info), pending: lockWait.map(info) });
    },
};
class BatteryManager extends EventTarget {}
const battery = Object.assign(Reflect.construct(EventTarget, [], BatteryManager), { charging: true, chargingTime: 0, dischargingTime: Infinity, level: 1, onchargingchange: null, onchargingtimechange: null, ondischargingtimechange: null, onlevelchange: null });
const __uaChrome = (/ Chrome\/(\d+)/.exec(N.userAgent()) || [])[1];
const __uaMobile = /\bMobile\b/.test(N.userAgent());
const __uaPlatform = /\bAndroid\b/.test(N.userAgent()) ? 'Android' : N.platform() === 'MacIntel' ? 'macOS' : 'Linux';
function uaLowEntropy() {
    return { brands: [{ brand: 'Google Chrome', version: __uaChrome }, { brand: 'Not?A_Brand', version: '8' }, { brand: 'Chromium', version: __uaChrome }], mobile: __uaMobile, platform: __uaPlatform };
}
const userAgentData = __uaChrome ? {
    get brands() { return uaLowEntropy().brands; },
    get mobile() { return __uaMobile; }, get platform() { return __uaPlatform; },
    // Do not expose additional device identifiers; low-entropy values are always returned.
    getHighEntropyValues() { return Promise.resolve(uaLowEntropy()); },
    toJSON() { return uaLowEntropy(); },
} : undefined;
/* Permissions API: the microphone reflects the Android grant; its statuses fire change once getUserMedia settles it */
class PermissionStatus extends EventTarget {
    constructor(name, state) { super(); def(this, '_', { name, state }); this.onchange = null; }
    get name() { return this._.name; }
    get state() { return this._.state; }
}
const permStatuses = { microphone: new Set(), camera: new Set() };
const permNative = { microphone: () => N.micPermission ? N.micPermission() : 0, camera: () => N.camPermission ? N.camPermission() : 0 };
const permState = name => { const p = permNative[name](); return p > 0 ? 'granted' : p < 0 ? 'denied' : 'prompt'; };
const micPermState = () => permState('microphone');
function permChanged(name) {
    const state = permState(name), set = permStatuses[name];
    for (const r of [...set]) {
        const p = r.deref();
        if (!p) { set.delete(r); continue; }
        if (p._.state === state) continue;
        p._.state = state;
        dispatch(p, new Event('change'), true);
    }
}
const micPermChanged = () => permChanged('microphone'), camPermChanged = () => permChanged('camera');
/* a constraint's wanted value: exact, ideal, or the bound given */
const mcVal = v => v && typeof v === 'object' ? (v.exact ?? v.ideal ?? v.max ?? v.min) : v;
/* waits for a device that opens once Android grants the permission: state 0 asking, 1 open, -1 refused */
const mcWait = (state, close, changed, ok) => new Promise((res, rej) => {
    const wait = () => {
        const st = state();
        if (st === 0) { setTimeout(wait, 20); return; }
        changed();
        if (st < 0) { close(); rej(new DOMException('Permission denied', 'NotAllowedError')); return; }
        res(ok());
    };
    wait();
});
function openMic(a) {
    const ec = a === true || typeof a !== 'object' ? undefined : a.echoCancellation;   /* on unless false, {exact: false} or {ideal: false} */
    const id = N.micOpen(!(ec === false || (ec && typeof ec === 'object' && (ec.exact === false || ec.ideal === false))));
    if (id < 0) return Promise.reject(new DOMException('Requested device not found', 'NotFoundError'));
    return mcWait(() => N.micState(id), () => N.micClose(id), micPermChanged, () => micTrack(id));
}
function openCam(v) {
    const o = v && typeof v === 'object' ? v : {};
    let facing = mcVal(o.facingMode); if (Array.isArray(facing)) facing = facing[0];
    const dev = String(mcVal(o.deviceId) ?? '');
    const f = dev === 'camera1' ? 1 : dev === 'camera2' ? 2 : facing === 'environment' ? 2 : facing === 'user' ? 1 : 0;
    const w = +mcVal(o.width) || 0, h = +mcVal(o.height) || 0, fps = +mcVal(o.frameRate) || 30;
    const id = N.camCount() > 0 ? N.camOpen(Math.min(w || (h ? h * 4 / 3 : 640), 1920), Math.min(h || (w ? w * 3 / 4 : 480), 1920), Math.min(fps, 60), f) : -1;
    if (id < 0) return Promise.reject(new DOMException('Requested device not found', 'NotFoundError'));
    return mcWait(() => N.camState(id), () => N.camClose(id), camPermChanged, () => camTrack(id));
}
const navigator = {
    userAgent: N.userAgent(), appVersion: N.userAgent().replace(/^Mozilla\//, ''), appName: 'Netscape', appCodeName: 'Mozilla', product: 'Gecko', productSub: __uaChrome ? '20030107' : '20100101', vendor: __uaChrome ? 'Google Inc.' : '', vendorSub: '',
    platform: N.platform(), language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: true, doNotTrack: null, webdriver: false, pdfViewerEnabled: false,
    hardwareConcurrency: N.cpus(), deviceMemory: 8, maxTouchPoints: N.touchPoints ? N.touchPoints() : 0, plugins: [], mimeTypes: [],
    userAgentData,
    connection: { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false, addEventListener() {}, removeEventListener() {} },
    javaEnabled() { return false; }, sendBeacon() { return true; }, vibrate() { return false; }, registerProtocolHandler() {},
    clipboard: Object.assign(new EventTarget(), {
        writeText(t) { N.clipSet(String(t)); return Promise.resolve(); },
        readText() { return Promise.resolve(N.clipGet()); },
        async write(items) { for (const it of items || []) if (it.types.includes('text/plain')) { N.clipSet(await (await it.getType('text/plain')).text()); return; } },
        async read() { return [new ClipboardItem({ 'text/plain': new Blob([N.clipGet()], { type: 'text/plain' }) })]; },
    }),
    permissions: { query(d) {
        if (!d || typeof d !== 'object' || d.name === undefined) return Promise.reject(new TypeError("Failed to execute 'query' on 'Permissions': required member name is undefined."));
        const name = String(d.name);
        if (permStatuses[name]) { const p = new PermissionStatus(name, permState(name)); permStatuses[name].add(new WeakRef(p)); return Promise.resolve(p); }
        return Promise.resolve(new PermissionStatus(name, 'prompt'));
    } },
    mediaDevices: Object.assign(new EventTarget(), {
        enumerateDevices() {
            /* like Chrome, labels only once the microphone was granted (sites read an empty label as "no permission") */
            const dev = (deviceId, kind, label) => { const d = { deviceId, groupId: deviceId, kind, label }; d.toJSON = () => ({ deviceId, groupId: deviceId, kind, label }); return d; };
            const list = N.micOpen ? [dev('default', 'audioinput', micPermState() === 'granted' ? 'Default' : '')] : [];
            const cams = N.camCount ? N.camCount() : 0, camLabels = permState('camera') === 'granted';
            for (let i = 0; i < Math.min(cams, 2); i++) list.push(dev(camLabels ? 'camera' + (i ? 2 : 1) : '', 'videoinput', camLabels ? (i ? 'camera 0, facing back' : 'camera 1, facing front') : ''));
            return Promise.resolve(list);
        },
        getUserMedia(c) {
            if (!c || typeof c !== 'object' || (!c.audio && !c.video)) return Promise.reject(new TypeError("Failed to execute 'getUserMedia': At least one of audio and video must be requested"));
            const opens = [];
            if (c.audio) opens.push(N.micOpen ? openMic(c.audio) : Promise.resolve(new MediaStreamTrack(RTC_INTERNAL, 'audio', 'Fake Default Audio Input')));
            if (c.video) opens.push(N.camOpen ? openCam(c.video) : Promise.resolve(new MediaStreamTrack(RTC_INTERNAL, 'video', 'Fake Default Video Input')));
            return Promise.allSettled(opens).then(r => {
                const bad = r.find(x => x.status === 'rejected');
                if (bad) { for (const x of r) if (x.status === 'fulfilled') x.value.stop(); throw bad.reason; }
                return new MediaStream(r.map(x => x.value));
            });
        }, getDisplayMedia(c) {
            if (c === undefined) c = { video: true };
            if (!c || typeof c !== 'object' || c.video === false) return Promise.reject(new TypeError("Failed to execute 'getDisplayMedia' on 'MediaDevices': video must be requested"));
            if (!N.screenOpen) return Promise.reject(new DOMException('Not supported', 'NotSupportedError'));
            const v = c.video && typeof c.video === 'object' ? c.video : {};
            const fps = Math.min(+mcVal(v.frameRate) || 30, 60);
            /* audio: the system audio (Android's playback capture), as desktop Chrome shares it, unless systemAudio is 'exclude' */
            const audio = !!c.audio && c.systemAudio !== 'exclude' && !(c.audio && typeof c.audio === 'object' && c.audio.exact === false);
            const id = N.screenOpen(Math.min(+mcVal(v.width) || 1920, 4096), Math.min(+mcVal(v.height) || 1920, 4096), fps, audio);
            if (id < 0) return Promise.reject(new DOMException('Could not start video source', 'NotReadableError'));
            return mcWait(() => N.screenState(id), () => N.screenClose(id), audio ? micPermChanged : () => {}, () => new MediaStream(screenTracks(id, fps)));
        }, getSupportedConstraints() { return {}; } }),
    serviceWorker: Object.assign(new EventTarget(), {
        controller: null, ready: new Promise(() => {}),
        register() { return Promise.reject(new DOMException('Service workers are not supported', 'SecurityError')); },
        getRegistration() { return Promise.resolve(undefined); }, getRegistrations() { return Promise.resolve([]); },
        startMessages() {},
    }),
    storage: { estimate() { return Promise.resolve({ quota: 1e9, usage: 0 }); }, persist() { return Promise.resolve(false); }, persisted() { return Promise.resolve(false); } },
    locks,
    getGamepads() { return []; }, getBattery() { return Promise.resolve(battery); },
};
const __uaFirefox = / Firefox\//.test(N.userAgent());
if (__uaFirefox) {   /* as Firefox (sign-in pages): its own fields, and none of Chrome's */
    Object.assign(navigator, { oscpu: N.platform(), buildID: '20181001000000' });
    for (const k of ['userAgentData', 'deviceMemory', 'connection']) delete navigator[k];
}
if (gpuNavigator && !__uaFirefox) def(navigator, 'gpu', gpuNavigator);
// in fullscreen the page's window is the whole screen, as pages comparing the two expect
const screenSize = i => N.viewport()[fsEl ? i - 5 : i];
const screen = { get width() { return screenSize(5); }, get height() { return screenSize(6); }, get availWidth() { return screenSize(5); }, get availHeight() { return screenSize(6); }, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0, addEventListener() {} } };
const T0 = Date.now() - N.now();
const marks = [];
/* the navigation entry's times: the document's DOMContentLoaded and load, once they happen */
const navTimes = { dcl: 0, load: 0 };
let navEntry = null;
const navigationEntry = () => {
    if (navEntry && navTimes.load) return navEntry;
    const end = navTimes.load || 0, dcl = navTimes.dcl || 0;
    navEntry = { name: location.href, entryType: 'navigation', initiatorType: 'navigation', type: 'navigate', startTime: 0, duration: end, redirectCount: 0,
        fetchStart: 0, domainLookupStart: 0, domainLookupEnd: 0, connectStart: 0, connectEnd: 0, requestStart: 0, responseStart: 0, responseEnd: 0,
        domInteractive: dcl, domContentLoadedEventStart: dcl, domContentLoadedEventEnd: dcl, domComplete: end, loadEventStart: end, loadEventEnd: end,
        transferSize: 0, encodedBodySize: 0, decodedBodySize: 0, nextHopProtocol: 'h2', serverTiming: [], toJSON() { return Object.assign({}, this); } };
    return navEntry;
};
Promise.resolve().then(() => {
    document.addEventListener('DOMContentLoaded', () => { navTimes.dcl = N.now(); }, { once: true });
    G.addEventListener('load', () => { navTimes.load = N.now(); setTimeout(() => perfNotify(navigationEntry())); }, { once: true });
});
const perfEntries = (t) => t === 'navigation' ? (navTimes.dcl ? [navigationEntry()] : []) : marks.filter(m => m.entryType === t);
const performance = Object.assign(new EventTarget(), {
    now() { return N.now(); }, timeOrigin: T0,
    timing: { navigationStart: T0, fetchStart: T0, domLoading: T0, responseStart: T0 },
    navigation: { type: 0, redirectCount: 0 },
    mark(n, o) { const e = { name: n, entryType: 'mark', startTime: o && o.startTime != null ? o.startTime : N.now(), duration: 0, detail: o && o.detail }; marks.push(e); if (marks.length > 4096) marks.splice(0, 1024); perfNotify(e); return e; },
    measure(n, s, e) { const f = (x) => typeof x === 'string' ? (marks.findLast(m => m.name === x) || { startTime: 0 }).startTime : x ?? N.now(); const st = typeof s === 'object' && s ? f(s.start) : f(s ?? 0); const en = typeof s === 'object' && s ? f(s.end) : f(e); const m = { name: n, entryType: 'measure', startTime: st, duration: en - st, detail: typeof s === 'object' && s ? s.detail ?? null : null }; marks.push(m); if (marks.length > 4096) marks.splice(0, 1024); perfNotify(m); return m; },
    getEntries() { return perfEntries('navigation').concat(marks); }, getEntriesByType(t) { return perfEntries(t); }, getEntriesByName(n, t) { return (t == null ? this.getEntries() : perfEntries(t)).filter(m => m.name === n); },
    clearMarks(n) { for (let i = marks.length; i--;) if (marks[i].entryType === 'mark' && (n == null || marks[i].name === n)) marks.splice(i, 1); },
    clearMeasures(n) { for (let i = marks.length; i--;) if (marks[i].entryType === 'measure' && (n == null || marks[i].name === n)) marks.splice(i, 1); }, clearResourceTimings() {}, setResourceTimingBufferSize() {},
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
function mediaChanged() {
    const pending = [];
    let retained = 0;
    for (const reference of mqls) {
        const query = reference.deref();
        if (!query) continue;
        mqls[retained++] = reference;
        pending.push(query);
    }
    mqls.length = retained;
    for (const query of pending) {
        const matches = N.media(query.media);
        if (matches === query._m) continue;
        query._m = matches;
        dispatch(query, new MediaQueryListEvent('change', { matches, media: query.media }), true);
    }
}
class MediaQueryListEvent extends Event { constructor(t, i = {}) { super(t, i); this.matches = !!i.matches; this.media = i.media || ''; } }
function getComputedStyle(el) { if (!el || !N.isNode(el) || N.type(el) !== 1) throw new TypeError("Failed to execute 'getComputedStyle': parameter 1 is not of type 'Element'."); if (!el.__cstyle) def(el, '__cstyle', new Proxy(new ComputedStyleDeclaration(el, true), computedHandler)); return el.__cstyle; }   /* live, so one per element */
const timerFn = (repeat) => (fn, ms, ...args) => { if (typeof fn !== 'function') { const src = String(fn); fn = () => (0, eval)(src); } return N.timer(() => { try { fn(...args); } catch (e) { report(e); } }, Math.max(0, +ms || 0), repeat); };
const setTimeout = timerFn(false), setInterval = timerFn(true);
const clearTimeout = (id) => { if (id) N.clearTimer(id); }, clearInterval = clearTimeout;
const requestAnimationFrame = (fn) => N.raf((t) => { try { fn(t); } catch (e) { report(e); } });
const cancelAnimationFrame = (id) => N.cancelRaf(id);
const requestIdleCallback = (fn, o) => setTimeout(() => fn({ didTimeout: false, timeRemaining() { return 10; } }), 1);
const cancelIdleCallback = clearTimeout;
const queueMicrotask = (fn) => { Promise.resolve().then(() => { try { fn(); } catch (e) { report(e); } }); };
N.cloneError(DOMException);
function structuredClone(v, options) {
    // Modules are serializable, never transferable. Validate even when the
    // transfer-list entry is not reachable from the value being cloned.
    if (options && options.transfer) for (const item of options.transfer)
        if (N.isWasmModule(item)) throw new DOMException('WebAssembly.Module is not transferable.', 'DataCloneError');
    return cloneValue(v, new Map());
}
function cloneValue(v, seen) {
    if (v === null || typeof v !== 'object') { if (typeof v === 'function' || typeof v === 'symbol') throw new DOMException('could not be cloned.', 'DataCloneError'); return v; }
    if (seen.has(v)) return seen.get(v);
    const moduleCopy = N.cloneWasm(v);
    if (moduleCopy !== undefined) { seen.set(v, moduleCopy); return moduleCopy; }
    const tag = Object.prototype.toString.call(v).slice(8, -1);
    if (tag === 'Date') return new Date(Date.prototype.getTime.call(v)); if (tag === 'RegExp') return new RegExp(v.source, v.flags);
    if (tag === 'ArrayBuffer') { const b = new ArrayBuffer(v.byteLength); new Uint8Array(b).set(new Uint8Array(v)); seen.set(v, b); return b; }
    if (ArrayBuffer.isView(v)) { const b = cloneValue(v.buffer, seen); return tag === 'DataView' ? new DataView(b, v.byteOffset, v.byteLength) : new globalThis[tag](b, v.byteOffset, v.length); }
    if (v instanceof Blob || tag === 'Blob' || tag === 'File') return v; if (N.isNode(v)) throw new DOMException('could not be cloned.', 'DataCloneError');
    if (tag === 'Map') { const m = new Map(); seen.set(v, m); for (const [k, x] of v) m.set(cloneValue(k, seen), cloneValue(x, seen)); return m; }
    if (tag === 'Set') { const s = new Set(); seen.set(v, s); for (const x of v) s.add(cloneValue(x, seen)); return s; }
    const o = Array.isArray(v) ? [] : {}; seen.set(v, o); for (const k of Object.keys(v)) o[k] = cloneValue(v[k], seen); return o;
}
const xferPorts = (o, t) => { const tr = o && typeof o === 'object' ? o.transfer : t; return (tr && typeof tr[Symbol.iterator] === 'function' ? [...tr] : []).filter(p => p && typeof p.postMessage === 'function' && typeof p.start === 'function'); };
function postMessage(data, origin, transfer) { const [src, org] = N.caller(); const d = structuredClone(data), ports = xferPorts(origin, transfer); N.timer(() => { try { dispatch(G, new MessageEvent('message', { data: d, origin: org, source: asWin(src), ports }), true); } catch (e) { report(e); } }, 0, false, true); }
function rejection(promise, reason) { const e = new PromiseRejectionEvent('unhandledrejection', { promise, reason, cancelable: true }); try { dispatch(G, e, true); } catch (x) { report(x); } return e.defaultPrevented; }
function queueMessage(data, origin, src, ports) { setTimeout(() => dispatch(G, new MessageEvent('message', { data, origin, source: asWin(src), ports: ports ? [...ports].map(p => typeof p === 'number' ? importPort(src, p) : p) : [] }), true)); }
const remoteWins = new Map();
const postArgs = (v, d, o, t) => N.postTo(v, d, o && typeof o === 'object' ? String(o.targetOrigin ?? '/') : String(o ?? '/'), xferPorts(o, t));
// A window this page can't touch directly: a cross-origin frame (its context id) or another tab (-its window id,
// as window.open returns and a popup's opener and message sources give)
function asWin(v) {
    if (typeof v !== 'number') return v;
    let w = remoteWins.get(v);
    if (w) return w;
    const tab = v < 0, wid = -v, abs = u => new URL(String(u), location.href).href;
    const nav = tab ? u => { N.winOp(wid, 2, abs(u)); } : u => N.ctxRel(v, 4, String(u));
    const loc = Object.freeze({ set href(u) { nav(u); }, replace: nav, assign: nav });
    w = Object.freeze({
        postMessage(d, o, t) { postArgs(v, d, o, t); },
        get window() { return w; }, get self() { return w; }, get frames() { return w; },
        get parent() { return tab ? w : asWin(N.ctxRel(v, 0)) ?? w; }, get top() { return tab ? w : asWin(N.ctxRel(v, 1)) ?? w; },
        get length() { return tab ? 0 : N.ctxRel(v, 2); }, get closed() { return tab ? !!N.winOp(wid, 0) : N.ctxRel(v, 3); }, opener: null,
        get location() { return loc; }, set location(u) { nav(u); },
        focus() { if (tab) N.winOp(wid, 3); }, blur() {}, close() { if (tab) N.winOp(wid, 1); },
    });
    remoteWins.set(v, w);
    return w;
}
// window.open: a new tab, its proxy for the opener; _self, _top, _parent and frame names navigate those
function openWin(u, t, f) {
    u = u === undefined || u === null ? '' : String(u); t = t === undefined || t === null || t === '' ? '_blank' : String(t); f = String(f ?? '');
    const url = u ? new URL(u, document.baseURI).href : 'about:blank', lt = t.toLowerCase();
    if (/^javascript:/i.test(url)) return null;
    if (lt === '_self' || (lt === '_top' && top === G) || (lt === '_parent' && parent === G)) { location.href = url; return G; }
    if (lt !== '_blank' && N.navTarget(t, url)) return lt === '_top' ? top : lt === '_parent' ? parent : null;
    const noop = /(^|[\s,])(noopener|noreferrer)\s*(=\s*(yes|1|true))?\s*($|,)/i.test(f);
    const id = N.openWin(url, lt === '_blank' ? '' : t, !noop);
    return id && !noop ? asWin(-id) : null;
}
let openerSet;
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
    ProcessingInstruction, Touch, TouchEvent, CompositionEvent, ClipboardEvent, ClipboardItem, DragEvent, StorageEvent, PromiseRejectionEvent, SubmitEvent,
    StyleSheet, IdleDeadline, TimeRanges, MediaError, MediaSource, SourceBuffer, SourceBufferList, ImageData, Path2D, CanvasGradient, CanvasPattern, CanvasRenderingContext2D, History, Navigator, Screen, Performance, Crypto, SubtleCrypto,
    DOMException, DOMRectReadOnly, DOMRect, Event, CustomEvent, CommandEvent, ToggleEvent, UIEvent, FocusEvent, MouseEvent, PointerEvent, WheelEvent, KeyboardEvent, InputEvent, ErrorEvent, ProgressEvent, MessageEvent, PopStateEvent, HashChangeEvent, PageTransitionEvent, AnimationEvent, TransitionEvent, MediaQueryListEvent,
    EventTarget, AbortSignal, AbortController, MutationObserver, MutationRecord, Node, NodeList, HTMLCollection, AnimationTimeline, DocumentTimeline, CharacterData, Text, CDATASection, Comment, DocumentType, DocumentFragment, ShadowRoot, Attr, NamedNodeMap, DOMTokenList,
    CSSStyleDeclaration, SVGFEBlendElement, SVGFEComponentTransferElement, SVGFEDiffuseLightingElement, SVGFESpecularLightingElement, SVGFEDropShadowElement, SVGFEFloodElement, SVGFEGaussianBlurElement, SVGFEImageElement, SVGFEMergeElement, SVGFEMergeNodeElement, SVGFEOffsetElement, SVGFETileElement, SVGFEDistantLightElement, SVGFEPointLightElement, SVGFESpotLightElement, SVGFEFuncRElement, SVGFEFuncGElement, SVGFEFuncBElement, SVGFEFuncAElement, SVGLength, SVGNumber, SVGAnimatedLength, SVGAnimatedNumber, SVGAnimatedInteger, SVGAnimatedBoolean, SVGAnimatedString, SVGAnimatedEnumeration, SVGRect, SVGAnimatedRect, SVGUnitTypes, SVGGeometryElement, SVGRectElement, SVGCircleElement, SVGEllipseElement, SVGLineElement, SVGPathElement, SVGPolylineElement, SVGPolygonElement, SVGGElement, SVGDefsElement, SVGUseElement, SVGImageElement, SVGForeignObjectElement, SVGAElement, SVGSwitchElement, SVGTextContentElement, SVGTextPositioningElement, SVGTextElement, SVGTSpanElement, SVGTextPathElement, SVGGradientElement, SVGLinearGradientElement, SVGRadialGradientElement, SVGStopElement, SVGClipPathElement, SVGMaskElement, SVGPatternElement, SVGFilterElement, SVGMarkerElement, SVGSymbolElement, SVGTitleElement, SVGDescElement, SVGMetadataElement, SVGStyleElement, SVGScriptElement, SVGFEColorMatrixElement, SVGFECompositeElement, SVGFEMorphologyElement, SVGFETurbulenceElement, SVGFEDisplacementMapElement, SVGFEConvolveMatrixElement, SVGComponentTransferFunctionElement, CSSRuleList, CSSStyleRule, CSSGroupingRule, CSSConditionRule, CSSMediaRule, CSSSupportsRule, CSSContainerRule, CSSLayerBlockRule, CSSLayerStatementRule, CSSImportRule, CSSNamespaceRule, CSSFontFaceRule, CSSPageRule, CSSKeyframeRule, CSSKeyframesRule, MediaList, Element, HTMLElement, SVGElement, SVGGraphicsElement, SVGSVGElement, MathMLElement, Image, Audio, Option, CustomElementRegistry, Document, HTMLDocument, XMLDocument, CSSRule, CSSStyleSheet, CSS, FontFace,
    TreeWalker, NodeIterator, NodeFilter, Range, StaticRange, AbstractRange, Selection, Animation, DOMMatrixReadOnly, DOMMatrix, DOMPoint, DOMParser, XMLSerializer, XPathResult, XPathExpression, XPathEvaluator,
    TextEncoder, TextDecoder, btoa, atob, Blob, File, FileList, DataTransfer, DataTransferItem, DataTransferItemList, FileReader, ReadableStream, ReadableStreamDefaultReader, WritableStream, WritableStreamDefaultWriter, TransformStream, ByteLengthQueuingStrategy, CountQueuingStrategy, TextEncoderStream, TextDecoderStream, URLSearchParams, URL, webkitURL: URL, Headers, Request, Response, fetch, FormData, XMLHttpRequestEventTarget, XMLHttpRequestUpload, XMLHttpRequest, WebSocket, CloseEvent, MessagePort, MessageChannel, Worker, BroadcastChannel,
    WebGLRenderingContext, WebGL2RenderingContext, WebGLObject, WebGLBuffer, WebGLFramebuffer, WebGLRenderbuffer, WebGLTexture, WebGLProgram, WebGLShader, WebGLVertexArrayObject, WebGLQuery, WebGLSampler, WebGLSync, WebGLTransformFeedback, WebGLUniformLocation, WebGLActiveInfo, WebGLShaderPrecisionFormat, WebGLContextEvent, ImageBitmap, createImageBitmap,
    GPUSupportedFeatures, WGSLLanguageFeatures, GPUSupportedLimits, GPUAdapterInfo, GPUError, GPUValidationError, GPUOutOfMemoryError, GPUInternalError, GPUPipelineError, GPUUncapturedErrorEvent, GPUDeviceLostInfo, GPUCompilationMessage, GPUCompilationInfo,
    GPUBufferUsage, GPUTextureUsage, GPUShaderStage, GPUMapMode, GPUColorWrite, GPU, GPUAdapter, GPUDevice, GPUBuffer, GPUTexture, GPUTextureView, GPUSampler, GPUBindGroupLayout, GPUPipelineLayout, GPUBindGroup, GPUCommandBuffer, GPURenderBundle, GPUExternalTexture,
    GPUQuerySet, GPUShaderModule, GPUComputePipeline, GPURenderPipeline, GPUCommandEncoder, GPURenderPassEncoder, GPUComputePassEncoder, GPURenderBundleEncoder, GPUQueue, GPUCanvasContext,
    MediaRecorder, BlobEvent, RTCPeerConnection, RTCSessionDescription, RTCIceCandidate, RTCPeerConnectionIceEvent, RTCPeerConnectionIceErrorEvent, RTCDataChannel, RTCDataChannelEvent, RTCTrackEvent, RTCRtpSender, RTCRtpReceiver, RTCRtpTransceiver, RTCIceTransport, RTCDtlsTransport, RTCSctpTransport, RTCCertificate, RTCStatsReport, RTCError, RTCErrorEvent, MediaStream, MediaStreamTrack, MediaStreamTrackEvent,
    IDBFactory, IDBDatabase, IDBTransaction, IDBObjectStore, IDBIndex, IDBCursor, IDBCursorWithValue, IDBKeyRange, IDBRequest, IDBOpenDBRequest, IDBVersionChangeEvent, DOMStringList, indexedDB,
    ResizeObserver, IntersectionObserver, IntersectionObserverEntry, HTMLAllCollection, PerformanceObserver, Storage, localStorage, sessionStorage, Location, location, history, navigator, screen, performance, crypto, MediaQueryList, matchMedia, getComputedStyle, PermissionStatus,
    setTimeout, setInterval, clearTimeout, clearInterval, requestAnimationFrame, cancelAnimationFrame, requestIdleCallback, cancelIdleCallback, queueMicrotask, structuredClone, postMessage, console, customElements, Window, document,
    window: G, self: G, globalThis: G, get top() { return asWin(N.topWin()) ?? G; }, get parent() { return asWin(N.parentWin()) ?? G; }, frames: G, get opener() { return openerSet !== undefined ? openerSet : asWin(N.openerWin()); }, set opener(v) { openerSet = v; }, get frameElement() { return N.frameEl(); }, closed: false, name: '', get length() { return N.frameCount(); }, origin: location.origin, isSecureContext: location.protocol === 'https:', crossOriginIsolated: false,
    get innerWidth() { return N.viewport()[0]; }, get innerHeight() { return N.viewport()[1]; }, get outerWidth() { return N.viewport()[0]; }, get outerHeight() { return N.viewport()[1] + 40; },
    get scrollX() { return N.viewport()[2]; }, get scrollY() { return N.viewport()[3]; }, get pageXOffset() { return N.viewport()[2]; }, get pageYOffset() { return N.viewport()[3]; }, get devicePixelRatio() { return N.viewport()[4]; },
    screenX: 0, screenY: 0, screenLeft: 0, screenTop: 0,
    scrollTo(x, y) { if (typeof x === 'object' && x) { y = x.top ?? N.viewport()[3]; x = x.left ?? N.viewport()[2]; } N.scrollTo(+x || 0, +y || 0); }, scroll(x, y) { G.scrollTo(x, y); },
    scrollBy(x, y) { if (typeof x === 'object' && x) { y = x.top || 0; x = x.left || 0; } const v = N.viewport(); N.scrollTo(v[2] + (+x || 0), v[3] + (+y || 0)); },
    alert(m) { N.log(1, 'alert: ' + m); }, confirm() { return false; }, prompt() { return null; }, print() {}, focus() {}, blur() {}, close() { if (top === G) N.winOp(0, 4); }, stop() {}, open: openWin,
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
class VisualViewport extends EventTarget {
    constructor() { super(); throw new TypeError('Illegal constructor'); }
    get width() { return N.viewport()[0]; }
    get height() { return N.viewport()[1]; }
    get offsetLeft() { return 0; }
    get offsetTop() { return 0; }
    get pageLeft() { return N.viewport()[2]; }
    get pageTop() { return N.viewport()[3]; }
    get scale() { return 1; }
}
Object.defineProperty(VisualViewport.prototype, Symbol.toStringTag, { value: 'VisualViewport', configurable: true });
Object.defineProperty(G, 'VisualViewport', { value: VisualViewport, writable: true, configurable: true });
const visual = Object.assign(Object.create(VisualViewport.prototype), { onresize: null, onscroll: null, onscrollend: null });
G.visualViewport = visual;
function viewportChanged(resized, scrolled, media = true) {
    if (resized) {
        dispatch(G, new Event('resize'), true);
        dispatch(visual, new Event('resize'), true);
        fsResized();
    }
    if (scrolled) dispatch(document, new Event('scroll', { bubbles: true }), true);
    if (media) mediaChanged();
}
// Bare global calls like `addEventListener(...)` run with an undefined receiver in strict code.
if (navigator.maxTouchPoints) for (const type of ['touchstart', 'touchmove', 'touchend', 'touchcancel']) def(G, 'on' + type, null);
for (const k of ['addEventListener', 'removeEventListener', 'dispatchEvent']) {
    const f = EventTarget.prototype[k];
    Object.defineProperty(G, k, { value: function (...a) { return f.apply(this ?? G, a); }, writable: true, configurable: true });
}
function fire(target, type, init, Ctor = Event) {
    const touchClick = type === 'click' && init && init.__touch;
    if (init && init.__touch) init = nativeTouchInit(type, init);
    const pre = type === 'click' ? preActivate(target) : null;
    const ev = new Ctor(type, init), ok = dispatch(target, ev, true);
    if (!ok && pre && pre.restore) pre.restore();
    if (ok && type === 'click' && target && target.nodeType === 1 && !(target.closest && target.closest('a[href]')) && !(target.disabled || (target.closest && target.closest('button:disabled')))) {
        try { activate(target, ev, pre); } catch (e) { report(e); }   /* anchors are navigated natively by the shell */
    }
    if (touchClick) nativeTap = null;
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
return { queueMessage, exportPort, portMessage, rejection, workerEvent, protoFor, dispatch, fire, fireAnim, report, mediaChanged, viewportChanged, ceConnected, nativeTouch, Event, MouseEvent, PointerEvent, KeyboardEvent, FocusEvent, WheelEvent, InputEvent, PopStateEvent, ErrorEvent };
})
