class ResizeObserver {
    constructor(cb) { this._cb = cb; this._t = new SMap(); }
    observe(el) { if (this._t.has(el)) return; this._t.set(el, null); roAll.add(this); N.resizePump(roRun); }
    unobserve(el) { this._t.delete(el); if (!this._t.size) { roAll.delete(this); roStop(); } }
    disconnect() { this._t.clear(); roAll.delete(this); roStop(); }
    _check() {
        const recs = [];
        for (const [el, old] of this._t) { const r = N.rect(el) || [0, 0, 0, 0]; if (N.skippedAncestor(el)) continue; const k = r[2] + 'x' + r[3]; if (k === old) continue; this._t.set(el, k); const cr = new DOMRect(0, 0, r[2], r[3]); const sz = [{ inlineSize: r[2], blockSize: r[3] }]; recs.push({ target: el, contentRect: cr, borderBoxSize: sz, contentBoxSize: sz, devicePixelContentBoxSize: sz }); }
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
        this._cb = cb; this._t = new SMap(); this.root = o.root || null;
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
            // CSS Contain skips descendant intersection, while the owner keeps geometry.
            let on = rootOn && el.isConnected && !N.skippedAncestor(el) && (!root || ioWithin(el, root)), x0 = r.left, y0 = r.top, x1 = r.right, y1 = r.bottom;
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
const roAll = new SSet();
const roStop = () => { if (!roAll.size) N.resizePump(null); };
const roRun = () => { for (const o of [...roAll]) if (o._t.size) o._check(); roStop(); };
const ioAll = new SSet();
// Native layout/style/scroll changes drive observation; idle documents do no work.
const ioStop = () => { if (!ioAll.size) N.intersectionPump(null); };
const ioRun = () => { for (const o of [...ioAll]) if (o._t.size) o._check(); ioStop(); };
// Marks, measures and the page's navigation entry; observers get them in a task, buffered ones too
const perfNow = () => N.now(), perfOrigin = () => T0;
const perfHasNavigation = true;
const perfExtra = t => t == null || t === 'navigation' ? (navTimes.dcl ? [navigationEntry()] : []) : [];
const PERF_TIMING = new SSet('navigationStart unloadEventStart unloadEventEnd redirectStart redirectEnd fetchStart domainLookupStart domainLookupEnd connectStart connectEnd secureConnectionStart requestStart responseStart responseEnd domLoading domInteractive domContentLoadedEventStart domContentLoadedEventEnd domComplete loadEventStart loadEventEnd'.split(' '));
const perfTiming = n => !PERF_TIMING.has(n) ? undefined : /^(unload|redirect|secureConnection)/.test(n) ? null : /^(domInteractive|domContentLoaded)/.test(n) ? navTimes.dcl || null : /^(domComplete|loadEvent)/.test(n) ? navTimes.load || null : 0;
/*@shared perf.js*/
const perfNotify = perfQueue;
class Storage {
    constructor() { def(this, '_m', new SMap()); }
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
// Location and window.open report an unparsable URL as a SyntaxError DOMException
const navURL = (u, what) => { const n = URL.parse(String(u), document.baseURI); if (!n) throw new DOMException(`Failed to ${what}: '${u}' is not a valid URL.`, 'SyntaxError'); return n; };
class Location {
    get href() { return N.url(); } set href(v) { this.assign(v); }
    assign(u) { const n = navURL(u, "execute 'assign' on 'Location'"); if (n.protocol === 'javascript:') { runJsURL(n.href); return; } const cur = new URL(N.url()); if (n.href.split('#')[0] === cur.href.split('#')[0] && n.href.includes('#')) fragmentNav(n.href, cur.href); else N.navigate(n.href); }
    replace(u) { const n = navURL(u, "execute 'replace' on 'Location'"); if (n.protocol === 'javascript:') { runJsURL(n.href); return; } const cur = new URL(N.url()); if (n.href.split('#')[0] === cur.href.split('#')[0] && n.href.includes('#')) fragmentNav(n.href, cur.href, true); else N.navigate(n.href); }
    reload() { N.navigate(N.url()); }
    toString() { return N.url(); }
    get ancestorOrigins() { return []; }
}
URL_KEYS.forEach((k, i) => { if (k !== 'href') acc(Location.prototype, k, function () { return N.urlParse(N.url(), null)[i]; }, k === 'origin' ? undefined : function (v) {
    const u = new URL(N.url());
    if (k === 'protocol' && !URL.canParse(String(v).replace(/:.*/s, '') + ':')) throw new DOMException(`Failed to set the 'protocol' property on 'Location': '${v}' is an invalid protocol.`, 'SyntaxError');
    /* hash: a present but empty fragment stays (URL's own setter drops it), and the same fragment does nothing */
    if (k === 'hash') { const n = new URL('#' + String(v).replace(/^#/, ''), u); if (n.hash === u.hash && n.href.includes('#') === u.href.includes('#')) return; this.assign(n.href); return; }
    u[k] = v; this.assign(u.href);
}); });
const location = new Location();
let histState = null, histIdx = 0;
const histStates = [null], histUrls = [N.url()];
const noHash = u => { const i = u.indexOf('#'); return i < 0 ? u : u.slice(0, i); };
/* a traversal within this document: popstate, then hashchange when only the fragment changed */
Object.defineProperty(globalThis, '__lumenPopState', { value: d => {
    const old = histUrls[histIdx] ?? N.url();
    histIdx = Math.max(0, Math.min(histStates.length - 1, histIdx + d));
    histState = histStates[histIdx] ?? null;
    const now = N.url(); histUrls[histIdx] = now;
    globalThis.dispatchEvent(new PopStateEvent('popstate', { state: histState }));
    if (old !== now && noHash(old) === noHash(now)) setTimeout(() => dispatch(G, new HashChangeEvent('hashchange', { oldURL: old, newURL: now })));
} });
/* a navigation to another fragment of this document, as Chrome does it: a new history entry, the
   target scrolled into view, popstate at once (routers such as React Router's follow it) and then
   hashchange; the same fragment again only scrolls */
function fragmentNav(href, old, replace = false) {
    const hash = href.slice(href.indexOf('#') + 1);
    let id = hash; try { id = decodeURIComponent(hash); } catch (e) {}
    const target = id && (document.getElementById(id) || document.getElementsByName(id)[0]);
    if (href === old) { if (target) target.scrollIntoView(); return; }
    histState = null; if (!replace) { histStates.length = ++histIdx; histStates[histIdx] = null; }
    N.setUrl(href, !replace); histUrls.length = histIdx + 1; histUrls[histIdx] = N.url();
    if (target) target.scrollIntoView(); else if (!id || id.toLowerCase() === 'top') G.scrollTo(0, 0);
    dispatch(G, new PopStateEvent('popstate', { state: null }));
    setTimeout(() => dispatch(G, new HashChangeEvent('hashchange', { oldURL: old, newURL: href })));
}
Object.defineProperty(globalThis, '__lumenFragment', { value: u => location.assign(u) });
/* Lumen's own pages (lumen://newtab) read and change the browser's state through this */
if (N.internal('ping') === 'pong') Object.defineProperty(G, 'lumenInternal', { value: cmd => N.internal(String(cmd)) ?? null });
// WPT runs (LUMEN_TESTDRIVER): testdriver-vendor.js sends real input through __lumenTd(ops), resolved once handled
/* the input is done in the top document, which resolves it: frames send theirs through it */
const topTd = () => { try { const t = window.top; return t !== window && t.__lumenTd; } catch (e) { return null; } };
/* a frame's viewport origin in the top viewport: its frame elements' boxes plus their borders and padding */
const tdOrigin = () => {
    let x = 0, y = 0;
    try {
        for (let w = window; w !== w.top; w = w.parent) {
            const f = w.frameElement; if (!f) break;
            const r = f.getBoundingClientRect(), cs = w.parent.getComputedStyle(f);
            x += r.left + (parseFloat(cs.borderLeftWidth) || 0) + (parseFloat(cs.paddingLeft) || 0);
            y += r.top + (parseFloat(cs.borderTopWidth) || 0) + (parseFloat(cs.paddingTop) || 0);
        }
    } catch (e) {}
    return [x, y];
};
if (N.internal('td?ping') === 'pong') {
    const waiting = new SMap();
    Object.defineProperty(G, '__lumenTdDone', { value: (id, error) => { const r = waiting.get(id); waiting.delete(id); if (r) error ? r[1](new DOMException('Queued input was cancelled or could not be delivered', error)) : r[0](); } });
    Object.defineProperty(G, '__lumenTd', { value: ops => {
        const t = topTd();
        if (t) { const [ox, oy] = tdOrigin(); return t(String(ops).replace(/^move (\S+) (\S+)$/gm, (m, x, y) => `move ${+x + ox} ${+y + oy}`)); }
        /* the top document resolves its own ops; a frame without access to it resolves after a short wait */
        return new Promise((resolve, reject) => { const id = +N.internal('td?' + ops); if (!id) reject(new DOMException('Input target is unavailable', 'InvalidStateError')); else if (window === window.top) waiting.set(id, [resolve, reject]); else setTimeout(resolve, 50); });
    } });
}
const history = {
    get length() { return N.histLen(); }, get state() { return histState; }, scrollRestoration: 'auto',
    pushState(s, t, u) { if (this !== history && this && typeof this.pushState === 'function' && this.pushState !== history.pushState) return this.pushState(s, t, u); histState = structuredClone(s); histStates.length = ++histIdx; histStates[histIdx] = histState; N.setUrl(u != null ? new URL(String(u), N.url()).href : N.url(), true); histUrls.length = histIdx + 1; histUrls[histIdx] = N.url(); },
    replaceState(s, t, u) { if (this !== history && this && typeof this.replaceState === 'function' && this.replaceState !== history.replaceState) return this.replaceState(s, t, u); histState = structuredClone(s); histStates[histIdx] = histState; if (u != null) N.setUrl(new URL(String(u), N.url()).href, false); histUrls[histIdx] = N.url(); },
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
const permStatuses = { microphone: new SSet(), camera: new SSet() };
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
    if (id === -2) return Promise.reject(new DOMException('Camera or microphone permission is denied', 'NotAllowedError'));
    if (id < 0) return Promise.reject(new DOMException('Requested device not found', 'NotFoundError'));
    return mcWait(() => N.micState(id), () => N.micClose(id), micPermChanged, () => micTrack(id));
}
function openCam(v) {
    if (N.camPermission && N.camPermission() === -2) return Promise.reject(new DOMException('Camera permission is denied', 'NotAllowedError'));
    const o = v && typeof v === 'object' ? v : {};
    let facing = mcVal(o.facingMode); if (Array.isArray(facing)) facing = facing[0];
    const dev = String(mcVal(o.deviceId) ?? '');
    const f = dev === 'camera1' ? 1 : dev === 'camera2' ? 2 : facing === 'environment' ? 2 : facing === 'user' ? 1 : 0;
    const w = +mcVal(o.width) || 0, h = +mcVal(o.height) || 0, fps = +mcVal(o.frameRate) || 30;
    const id = N.camCount() > 0 ? N.camOpen(Math.min(w || (h ? h * 4 / 3 : 640), 1920), Math.min(h || (w ? w * 3 / 4 : 480), 1920), Math.min(fps, 60), f) : -1;
    if (id === -2) return Promise.reject(new DOMException('Camera or microphone permission is denied', 'NotAllowedError'));
    if (id < 0) return Promise.reject(new DOMException('Requested device not found', 'NotFoundError'));
    return mcWait(() => N.camState(id), () => N.camClose(id), camPermChanged, () => camTrack(id));
}
class UserActivation {
    constructor(key) { if (key !== RTC_INTERNAL) throw new TypeError('Illegal constructor'); }
    get hasBeenActive() { return stickyActivation; }
    get isActive() { return transientActivation(); }
}
const navigator = {
    userActivation: new UserActivation(RTC_INTERNAL),
    userAgent: N.userAgent(), appVersion: N.userAgent().replace(/^Mozilla\//, ''), appName: 'Netscape', appCodeName: 'Mozilla', product: 'Gecko', productSub: __uaChrome ? '20030107' : '20100101', vendor: __uaChrome ? 'Google Inc.' : '', vendorSub: '',
    platform: N.platform(), language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: true, doNotTrack: null, webdriver: false, pdfViewerEnabled: false,
    hardwareConcurrency: N.cpus(), deviceMemory: 8, maxTouchPoints: N.touchPoints ? N.touchPoints() : 0, plugins: [], mimeTypes: [],
    userAgentData,
    connection: { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false, addEventListener() {}, removeEventListener() {} },
    javaEnabled() { return false; }, sendBeacon(url, data = null) {
        const u = URL.parse(String(url), document.baseURI);
        if (!u) throw new TypeError(`Failed to execute 'sendBeacon' on 'Navigator': '${url}' is not a valid URL.`);
        if (u.protocol !== 'http:' && u.protocol !== 'https:') throw new TypeError("Failed to execute 'sendBeacon' on 'Navigator': Beacons are only supported over HTTP(S).");
        fetch(u.href, { method: 'POST', body: data, keepalive: true, credentials: 'include', mode: 'no-cors' }).catch(() => {});
        return true;
    }, vibrate() { return false; }, registerProtocolHandler() {},
    clipboard: Object.assign(new EventTarget(), {
        writeText(t) { N.clipSet(String(t)); return Promise.resolve(); },
        readText() { return Promise.resolve(N.clipGet()); },
        async write(items) { for (const it of items || []) if (it.types.includes('text/plain')) { N.clipSet(await (await it.getType('text/plain')).text()); return; } },
        async read() {
            const o = {}, t = N.clipGet(), img = N.clipImage();
            if (t) o['text/plain'] = new Blob([t], { type: 'text/plain' });
            if (img) o[img[0]] = new Blob([img[1]], { type: img[0] });
            return Object.keys(o).length ? [new ClipboardItem(o)] : [];
        },
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
    serviceWorker: swContainer,
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
const performance = new Performance(PERF_KEY);
Object.defineProperties(performance, {
    timing: { value: { navigationStart: T0, fetchStart: T0, domLoading: T0, responseStart: T0 }, configurable: true },
    navigation: { value: { type: 0, redirectCount: 0 }, configurable: true },
    memory: { get() { const h = N.heap(); return { usedJSHeapSize: h[0], totalJSHeapSize: h[1], jsHeapSizeLimit: h[2] }; }, configurable: true },
});
/* a secure context: https, file, or a loopback host (as Chrome treats them); about: and data: frames inherit it, and a frame
   inside a non-secure same-origin parent is never secure */
const secureContext = (l => {
    let ps = true;
    try { ps = N.parentSecure(); } catch (e) {}
    if (l.protocol === 'about:' || l.protocol === 'data:') return ps;
    return ps && (l.protocol === 'https:' || l.protocol === 'file:' || /^(localhost|127(\.\d+){3}|\[::1\])$|\.localhost$/.test(l.hostname));
})(location);
if (!secureContext) delete navigator.serviceWorker;   /* as Chrome: service workers only in secure contexts */
const CN = N, CRYPTO_SECURE = () => secureContext;
/*@shared crypto.js*/
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
/* pseudo-elements getComputedStyle resolves; any other non-empty argument gives an empty style */
const CS_PSEUDO = /^::?(before|after|first-line|first-letter)$|^::(marker|placeholder|selection|backdrop|file-selector-button|search-text|target-text|spelling-error|grammar-error|details-content|scroll-marker|scroll-marker-group|column|checkmark|picker-icon|picker\(select\)|highlight\(\s*-?[a-zA-Z_][\w-]*\s*\)|view-transition)$/;
const csNone = new WeakMap();
const csPseudo = new WeakMap();
function getComputedStyle(el, pseudo) { if (!el || !N.isNode(el) || N.type(el) !== 1) throw new TypeError("Failed to execute 'getComputedStyle': parameter 1 is not of type 'Element'."); if (pseudo != null && pseudo !== '' && !CS_PSEUDO.test(String(pseudo))) { let n = csNone.get(el); if (!n) { n = new ComputedStyleDeclaration(el, true); def(n, '_none', true); n = new Proxy(n, computedHandler); csNone.set(el, n); } return n; } const ps = pseudo == null ? 0 : /^::?before$/i.test(String(pseudo)) ? 1 : /^::?after$/i.test(String(pseudo)) ? 2 : 0; if (ps) { let m = csPseudo.get(el); if (!m) csPseudo.set(el, m = []); if (!m[ps]) { const d = new ComputedStyleDeclaration(el, true); def(d, '_ps', ps); m[ps] = new Proxy(d, computedHandler); } return m[ps]; } if (!el.__cstyle) def(el, '__cstyle', new Proxy(new ComputedStyleDeclaration(el, true), computedHandler)); return el.__cstyle; }   /* live, so one per element */
for (const u of CSS_UNIT_FACTORIES) CSS[u] = { [u](v) { return new CSSUnitValue(v, u); } }[u];
const timerFn = (repeat) => (fn, ms, ...args) => { if (typeof fn !== 'function') { const src = ttString(TrustedScript, fn, repeat ? 'Window setInterval' : 'Window setTimeout'); fn = () => { ttTimerSrc = src; (0, eval)(src); }; } return N.timer(() => { try { fn(...args); } catch (e) { report(e); } }, Math.max(0, +ms || 0), repeat); };
const setTimeout = timerFn(false), setInterval = timerFn(true);
const clearTimeout = (id) => { if (id) N.clearTimer(id); }, clearInterval = clearTimeout;
const requestAnimationFrame = (fn) => N.raf((t) => { waFrameTime = t; waInFrame = true; try { fn(t); } catch (e) { report(e); } finally { waInFrame = false; } });
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
    return cloneValue(v, new SMap());
}
const PLATFORM_PROTOS = new WeakSet();   /* prototypes of the WebIDL interfaces; filled when the globals are shaped */
function cloneValue(v, seen) {
    if (v === null || typeof v !== 'object') { if (typeof v === 'function' || typeof v === 'symbol') throw new DOMException('could not be cloned.', 'DataCloneError'); return v; }
    // These platform objects are not serializable, including genuine values
    // from another realm and values whose public prototype was removed.
    for (let kind = 1; kind <= 5; kind++) if (N.trustedData(v, kind) !== undefined)
        throw new DOMException('Trusted Types and Sanitizer objects could not be cloned.', 'DataCloneError');
    if (seen.has(v)) return seen.get(v);
    const moduleCopy = N.cloneWasm(v);
    if (moduleCopy !== undefined) { seen.set(v, moduleCopy); return moduleCopy; }
    // Recognize genuine DOMExceptions before inspecting an author-controlled
    // @@toStringTag, and serialize their Web IDL data rather than public getters.
    const domState = N.domExceptionState(v);
    if (domState !== undefined) {
        const e = domExceptionClone(domState, N.domExceptionStack(v));
        seen.set(v, e); return e;
    }
    const tag = Object.prototype.toString.call(v).slice(8, -1);
    if (tag === 'Boolean') { const b = Object(Boolean.prototype.valueOf.call(v)); seen.set(v, b); return b; } if (tag === 'Number') { const b = Object(Number.prototype.valueOf.call(v)); seen.set(v, b); return b; }
    if (tag === 'String') { const b = Object(String.prototype.valueOf.call(v)); seen.set(v, b); return b; } if (tag === 'BigInt') { const b = Object(BigInt.prototype.valueOf.call(v)); seen.set(v, b); return b; }
    if (tag === 'Error') {   /* HTML: name picks the constructor among the standard ones; message is kept when it is an own property */
        const n = v.name, C = ['EvalError', 'RangeError', 'ReferenceError', 'SyntaxError', 'TypeError', 'URIError'].includes(n) ? globalThis[n] : Error, e = new C();
        const md = Object.getOwnPropertyDescriptor(v, 'message'); if (md && 'value' in md) def(e, 'message', String(md.value));
        if (typeof v.stack === 'string') def(e, 'stack', v.stack);
        // Record the shell before descending into cause: causes may contain
        // the same Error, mutually recursive errors, or shared graph nodes.
        seen.set(v, e);
        const cd = Object.getOwnPropertyDescriptor(v, 'cause'); if (cd && 'value' in cd) def(e, 'cause', cloneValue(cd.value, seen));
        return e;
    }
    if (tag === 'Date') { const d = new Date(Date.prototype.getTime.call(v)); seen.set(v, d); return d; } if (tag === 'RegExp') { const r = new RegExp(v.source, v.flags); seen.set(v, r); return r; }
    if (tag === 'ArrayBuffer') { const b = new ArrayBuffer(v.byteLength); new Uint8Array(b).set(new Uint8Array(v)); seen.set(v, b); return b; }
    if (ArrayBuffer.isView(v)) { const b = cloneValue(v.buffer, seen), view = tag === 'DataView' ? new DataView(b, v.byteOffset, v.byteLength) : new globalThis[tag](b, v.byteOffset, v.length); seen.set(v, view); return view; }
    if (tag === 'CryptoKey') { const k = cryptoKeyClone(v); seen.set(v, k); return k; }
    if (v instanceof ImageBitmap) {   /* serializable: same pixels; a closed bitmap cannot be cloned */
        if (!v._.src) throw new DOMException('An ImageBitmap is detached and could not be cloned.', 'DataCloneError');
        const b = Object.create(ImageBitmap.prototype); def(b, '_', Object.assign({}, v._)); seen.set(v, b); return b;
    }
    { const gc = v && typeof v === 'object' ? geoClone(v) : null; if (gc) { seen.set(v, gc); return gc; } }
    if (v instanceof ImageData) { const d = new ImageData(new Uint8ClampedArray(v.data), v.width, v.height, { colorSpace: v.colorSpace }); seen.set(v, d); return d; }
    if (v instanceof Blob || tag === 'Blob' || tag === 'File') { const b = v instanceof File ? new File([v], v.name, { type: v.type, lastModified: v.lastModified }) : new Blob([v], { type: v.type }); seen.set(v, b); return b; } if (N.isNode(v) || v instanceof URL || v instanceof URLSearchParams) throw new DOMException(`${tag} object could not be cloned.`, 'DataCloneError');
    if (tag === 'Map') { const m = new OMap(), set = SMap.prototype.set; seen.set(v, m); for (const [k, x] of SMap.prototype.entries.call(v)) set.call(m, cloneValue(k, seen), cloneValue(x, seen)); return m; }
    if (tag === 'Set') { const s = new OSet(), add = SSet.prototype.add; seen.set(v, s); for (const x of SSet.prototype.values.call(v)) add.call(s, cloneValue(x, seen)); return s; }
    for (let p = Object.getPrototypeOf(v); p; p = Object.getPrototypeOf(p)) if (p !== DOMException.prototype && PLATFORM_PROTOS.has(p)) throw new DOMException(`${tag} object could not be cloned.`, 'DataCloneError');
    const o = Array.isArray(v) ? [] : {}; seen.set(v, o); for (const k of Object.keys(v)) o[k] = cloneValue(v[k], seen); return o;
}
const xferPorts = (o, t) => { const tr = o && typeof o === 'object' ? o.transfer : t; return (tr && typeof tr[Symbol.iterator] === 'function' ? [...tr] : []).filter(p => p && typeof p.postMessage === 'function' && typeof p.start === 'function'); };
/* transferred ArrayBuffers are detached in the sender (after the clone took their contents) */
const xferBuffers = (o, t) => { const tr = o && typeof o === 'object' && !Array.isArray(o) ? o.transfer : t; for (const b of (tr && typeof tr[Symbol.iterator] === 'function' ? [...tr] : [])) if (b instanceof ArrayBuffer && !b.detached) b.transfer(); };
function postMessage(data, origin, transfer) { const [src, org] = N.caller(); const d = structuredClone(data), ports = xferPorts(origin, transfer); xferBuffers(origin, transfer); N.timer(() => { try { dispatch(G, new MessageEvent('message', { data: d, origin: org, source: asWin(src), ports }), true); } catch (e) { report(e); } }, 0, false, true); }
function rejection(promise, reason) { const e = new PromiseRejectionEvent('unhandledrejection', { promise, reason, cancelable: true }); try { dispatch(G, e, true); } catch (x) { report(x); } return e.defaultPrevented; }
function queueMessage(data, origin, src, ports) { setTimeout(() => dispatch(G, new MessageEvent('message', { data, origin, source: asWin(src), ports: ports ? [...ports].map(p => typeof p === 'number' ? importPort(src, p) : p) : [] }), true)); }
const remoteWins = new SMap();
const postArgs = (v, d, o, t) => N.postTo(v, d, o && typeof o === 'object' ? String(o.targetOrigin ?? '/') : String(o ?? '/'), xferPorts(o, t));
// A window this page can't touch directly: a cross-origin frame (its context id) or another tab (-its window id,
// as window.open returns and a popup's opener and message sources give)
function asWin(v) {
    if (typeof v !== 'number') return v;
    let w = remoteWins.get(v);
    if (w) return w;
    const tab = v < 0, wid = -v, abs = u => new URL(String(u), location.href).href;
    const nav = tab ? u => { navURL(u, "set the 'href' property on 'Location'"); N.winOp(wid, 2, abs(u)); } : u => { navURL(u, "set the 'href' property on 'Location'"); N.ctxRel(v, 4, String(u)); };
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
    const url = u ? navURL(u, "execute 'open' on 'Window'").href : 'about:blank', lt = t.toLowerCase();
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
/* a node's registry: the window's for nodes of this window's document, none for documents made by scripts */
const nodeRegistry = n => (N.ownerDoc(n) || n) === document ? customElements : null;
acc(Document.prototype, 'customElementRegistry', function () { return nodeRegistry(this); });
acc(Element.prototype, 'customElementRegistry', function () { return nodeRegistry(this); });
acc(ShadowRoot.prototype, 'customElementRegistry', function () { return nodeRegistry(this); });
class Window extends EventTarget {}
/* Global interface attributes are own properties of the Window global. */
installHandlers(G, false, EVENT_HANDLERS.concat(['beforeunload', 'hashchange', 'message', 'messageerror', 'offline', 'online', 'pagehide', 'pageshow', 'popstate', 'storage', 'unhandledrejection', 'rejectionhandled', 'unload', 'DOMContentLoaded']));
/* <body onload="..."> from the parser: the window's handler until a script sets one */
for (const t of WINDOW_FWD) {
    const k = 'on' + t, d = Object.getOwnPropertyDescriptor(G, k);
    if (!d) continue;
    acc(G, k, function () {
        const cleared = openedHandlerGet(openedHandlerState, this);
        if (cleared && !openedHandlerHas(cleared, k)) return null;
        const own = this.__on && this.__on[k];
        if (own !== undefined) return own;
        const d = this === G && document, b = d && d.body;
        if (b && N.ns(b) === 0) { const src = N.attr(b, k); if (src != null) return compileHandler(b, k, src); }
        return null;
    }, d.set);
}
Object.setPrototypeOf(G, Window.prototype);
{
    /* window's named properties: a child frame by name (its WindowProxy), else the HTML elements with that id or the
       embed/form/img/object elements with that name (one element, or a collection of them in tree order). The name
       index is rebuilt only after the DOM changes. */
    let namedVer = -1, namedMap = new SMap();
    const byName = (d, p) => {
        const v = N.version(d);
        if (v !== namedVer) {
            namedVer = v; namedMap = new SMap();
            for (const e of N.query(d, 'embed[name],form[name],img[name],object[name],iframe[name],frame[name]', true)) {
                if (N.ns(e) !== 0) continue;
                const n = N.attr(e, 'name'); if (!n) continue;
                let a = namedMap.get(n); if (!a) namedMap.set(n, a = []); a.push(e);
            }
        }
        return namedMap.get(p);
    };
    const namedGet = (p) => {
        const d = G.document;
        if (typeof p !== 'string' || !p || !d) return null;
        const named = byName(d, p), byId = N.byId(p);
        if (!named && !byId) return null;
        let els = [];
        if (named) for (const e of named) {
            const t = N.name(e);
            if (t === 'iframe' || t === 'frame') { const w = e.contentWindow; if (w) return w; } else els.push(e);
        }
        if (byId && N.ns(byId) === 0) {
            const ids = N.query(d, '[id="' + CSS.escape(p) + '"]', true);
            if (ids.length === 1 && !els.length) return ids[0];
            for (const e of ids) if (N.ns(e) === 0 && !els.includes(e)) els.push(e);
            els.sort((x, y) => x === y ? 0 : x.compareDocumentPosition(y) & 4 ? -1 : 1);
        }
        return els.length === 0 ? null : els.length === 1 ? els[0] : htmlColl(els);
    };
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
Object.setPrototypeOf(history, History.prototype); Object.setPrototypeOf(navigator, Navigator.prototype); Object.setPrototypeOf(screen, Screen.prototype);
const globals = {
    ...(secureContext ? { CryptoKey, SubtleCrypto, ServiceWorker, ServiceWorkerContainer, ServiceWorkerRegistration, NavigationPreloadManager } : {}), QuotaExceededError,
    TrustedHTML, TrustedScript, TrustedScriptURL, TrustedHTMLParserOptions, TrustedTypePolicy, TrustedTypePolicyFactory, SecurityPolicyViolationEvent, get trustedTypes() { return ttFactory; },
    ProcessingInstruction, Touch, TouchEvent, CompositionEvent, BeforeUnloadEvent, TextEvent, DeviceOrientationEvent, DeviceMotionEvent, ClipboardEvent, ClipboardItem, DragEvent, StorageEvent, PromiseRejectionEvent, SubmitEvent,
    StyleSheet, IdleDeadline, TimeRanges, MediaError, MediaSource, SourceBuffer, SourceBufferList, ImageData, Path2D, CanvasGradient, CanvasPattern, CanvasRenderingContext2D, TextMetrics, OffscreenCanvas, OffscreenCanvasRenderingContext2D, History, Navigator, Screen, Performance, Crypto,
    DOMException, DOMRectReadOnly, DOMRect, Event, CustomEvent, CommandEvent, ToggleEvent, UIEvent, FocusEvent, MouseEvent, PointerEvent, WheelEvent, KeyboardEvent, InputEvent, ErrorEvent, ProgressEvent, MessageEvent, PopStateEvent, HashChangeEvent, PageTransitionEvent, AnimationEvent, TransitionEvent, MediaQueryListEvent,
    EventTarget, AbortSignal, AbortController, Observable, Subscriber, MutationObserver, MutationRecord, Node, NodeList, HTMLCollection, HTMLFormControlsCollection, HTMLOptionsCollection, RadioNodeList, ValidityState, UserActivation, AnimationTimeline, DocumentTimeline, AnimationEffect, KeyframeEffect, AnimationPlaybackEvent, SVGStringList, SVGNumberList, SVGLengthList, SVGPointList, SVGTransformList, SVGTransform, SVGAnimatedTransformList, SVGAnimatedNumberList, SVGAnimatedLengthList, SVGPreserveAspectRatio, SVGAnimatedPreserveAspectRatio, SVGAngle, SVGAnimatedAngle, SVGViewElement, SVGMPathElement, SVGAnimationElement, SVGAnimateElement, SVGSetElement, SVGAnimateMotionElement, SVGAnimateTransformElement, TimeEvent, CharacterData, Text, CDATASection, Comment, DocumentType, DocumentFragment, ShadowRoot, Attr, NamedNodeMap, DOMTokenList,
    CSSStyleDeclaration, CSSPositionTryRule, CSSPositionTryDescriptors, CSSColorValue, CSSRGB, CSSHSL, CSSHWB, SVGFEBlendElement, SVGFEComponentTransferElement, SVGFEDiffuseLightingElement, SVGFESpecularLightingElement, SVGFEDropShadowElement, SVGFEFloodElement, SVGFEGaussianBlurElement, SVGFEImageElement, SVGFEMergeElement, SVGFEMergeNodeElement, SVGFEOffsetElement, SVGFETileElement, SVGFEDistantLightElement, SVGFEPointLightElement, SVGFESpotLightElement, SVGFEFuncRElement, SVGFEFuncGElement, SVGFEFuncBElement, SVGFEFuncAElement, SVGLength, SVGNumber, SVGAnimatedLength, SVGAnimatedNumber, SVGAnimatedInteger, SVGAnimatedBoolean, SVGAnimatedString, SVGAnimatedEnumeration, SVGRect, SVGAnimatedRect, SVGUnitTypes, SVGGeometryElement, SVGRectElement, SVGCircleElement, SVGEllipseElement, SVGLineElement, SVGPathElement, SVGPolylineElement, SVGPolygonElement, SVGGElement, SVGDefsElement, SVGUseElement, SVGImageElement, SVGForeignObjectElement, SVGAElement, SVGSwitchElement, SVGTextContentElement, SVGTextPositioningElement, SVGTextElement, SVGTSpanElement, SVGTextPathElement, SVGGradientElement, SVGLinearGradientElement, SVGRadialGradientElement, SVGStopElement, SVGClipPathElement, SVGMaskElement, SVGPatternElement, SVGFilterElement, SVGMarkerElement, SVGSymbolElement, SVGTitleElement, SVGDescElement, SVGMetadataElement, SVGStyleElement, SVGScriptElement, SVGFEColorMatrixElement, SVGFECompositeElement, SVGFEMorphologyElement, SVGFETurbulenceElement, SVGFEDisplacementMapElement, SVGFEConvolveMatrixElement, SVGComponentTransferFunctionElement, CSSRuleList, CSSStyleRule, CSSGroupingRule, CSSConditionRule, CSSMediaRule, CSSSupportsRule, CSSContainerRule, CSSLayerBlockRule, CSSLayerStatementRule, CSSImportRule, CSSNamespaceRule, CSSFontFaceRule, CSSPageRule, CSSKeyframeRule, CSSKeyframesRule, MediaList, Element, HTMLElement, SVGElement, SVGGraphicsElement, SVGSVGElement, MathMLElement, Image, Audio, Option, CustomElementRegistry, Document, HTMLDocument, XMLDocument, CSSRule, CSSStyleSheet, CSS, FontFace,
    TreeWalker, NodeIterator, NodeFilter, DOMImplementation, Range, StaticRange, AbstractRange, OpaqueRange, Highlight, HighlightRegistry, Selection, Animation, DOMMatrixReadOnly, DOMMatrix, DOMPoint, DOMPointReadOnly, DOMQuad, DOMRectList, WebKitCSSMatrix: DOMMatrix, CaretPosition, CSSTransition, CSSAnimation, CSSStyleValue, CSSNumericValue, CSSUnitValue, CSSMathValue, CSSMathSum, CSSMathProduct, CSSMathNegate, CSSMathInvert, CSSMathMin, CSSMathMax, CSSMathClamp, CSSNumericArray, CSSKeywordValue, CSSUnparsedValue, CSSVariableReferenceValue, CSSImageValue, CSSTransformValue, CSSTransformComponent, CSSTranslate, CSSRotate, CSSScale, CSSSkew, CSSSkewX, CSSSkewY, CSSPerspective, CSSMatrixComponent, StylePropertyMapReadOnly, StylePropertyMap, DOMParser, XMLSerializer, XPathResult, XPathExpression, XPathEvaluator, Sanitizer,
    TextEncoder, TextDecoder, btoa, atob, Blob, File, FileList, DataTransfer, DataTransferItem, DataTransferItemList, FileReader, ReadableStream, ReadableStreamDefaultReader, ReadableStreamBYOBReader, ReadableStreamDefaultController, ReadableByteStreamController, ReadableStreamBYOBRequest, WritableStream, WritableStreamDefaultWriter, WritableStreamDefaultController, TransformStream, TransformStreamDefaultController, ByteLengthQueuingStrategy, CountQueuingStrategy, TextEncoderStream, TextDecoderStream, CompressionStream, DecompressionStream, URLSearchParams, URL, webkitURL: URL, URLPattern, Headers, Request, Response, fetch, Cache, CacheStorage, caches, FormData, XMLHttpRequestEventTarget, XMLHttpRequestUpload, XMLHttpRequest, WebSocket, CloseEvent, MessagePort, MessageChannel, Worker, SharedWorker, BroadcastChannel,
    WebGLRenderingContext, WebGL2RenderingContext, WebGLObject, WebGLBuffer, WebGLFramebuffer, WebGLRenderbuffer, WebGLTexture, WebGLProgram, WebGLShader, WebGLVertexArrayObject, WebGLQuery, WebGLSampler, WebGLSync, WebGLTransformFeedback, WebGLUniformLocation, WebGLActiveInfo, WebGLShaderPrecisionFormat, WebGLContextEvent, ImageBitmap, createImageBitmap,
    GPUSupportedFeatures, WGSLLanguageFeatures, GPUSupportedLimits, GPUAdapterInfo, GPUError, GPUValidationError, GPUOutOfMemoryError, GPUInternalError, GPUPipelineError, GPUUncapturedErrorEvent, GPUDeviceLostInfo, GPUCompilationMessage, GPUCompilationInfo,
    GPUBufferUsage, GPUTextureUsage, GPUShaderStage, GPUMapMode, GPUColorWrite, GPU, GPUAdapter, GPUDevice, GPUBuffer, GPUTexture, GPUTextureView, GPUSampler, GPUBindGroupLayout, GPUPipelineLayout, GPUBindGroup, GPUCommandBuffer, GPURenderBundle, GPUExternalTexture,
    GPUQuerySet, GPUShaderModule, GPUComputePipeline, GPURenderPipeline, GPUCommandEncoder, GPURenderPassEncoder, GPUComputePassEncoder, GPURenderBundleEncoder, GPUQueue, GPUCanvasContext,
    MediaRecorder, BlobEvent, RTCPeerConnection, RTCSessionDescription, RTCIceCandidate, RTCPeerConnectionIceEvent, RTCPeerConnectionIceErrorEvent, RTCDataChannel, RTCDataChannelEvent, RTCTrackEvent, RTCRtpSender, RTCRtpReceiver, RTCRtpTransceiver, RTCIceTransport, RTCDtlsTransport, RTCSctpTransport, RTCCertificate, RTCStatsReport, RTCError, RTCErrorEvent, MediaStream, MediaStreamTrack, MediaStreamTrackEvent,
    IDBFactory, IDBDatabase, IDBTransaction, IDBObjectStore, IDBIndex, IDBCursor, IDBCursorWithValue, IDBKeyRange, IDBRecord, IDBRequest, FormDataEvent, IDBOpenDBRequest, IDBVersionChangeEvent, DOMStringList, indexedDB,
    ResizeObserver, IntersectionObserver, IntersectionObserverEntry, HTMLAllCollection, PerformanceObserver, PerformanceObserverEntryList, PerformanceEntry, PerformanceMark, PerformanceMeasure, Performance, Storage, localStorage, sessionStorage, Location, get location() { return location; }, set location(v) { location.href = v; }, history, navigator, screen, get performance() { if (this !== G) throw new TypeError('Illegal invocation'); return performance; }, get crypto() { return crypto; }, MediaQueryList, matchMedia, getComputedStyle, PermissionStatus,
    setTimeout, setInterval, clearTimeout, clearInterval, requestAnimationFrame, cancelAnimationFrame, requestIdleCallback, cancelIdleCallback, queueMicrotask, structuredClone, postMessage, console, customElements, Window, document,
    window: G, self: G, globalThis: G, get top() { return asWin(N.topWin()) ?? G; }, get parent() { return asWin(N.parentWin()) ?? G; }, frames: G, get opener() { return openerSet !== undefined ? openerSet : asWin(N.openerWin()); }, set opener(v) { openerSet = v; }, get frameElement() { return N.frameEl(); }, closed: false, name: '', get length() { return N.frameCount(); }, origin: location.origin, isSecureContext: secureContext, crossOriginIsolated: false,
    get innerWidth() { return N.viewport()[0]; }, get innerHeight() { return N.viewport()[1]; }, get outerWidth() { return N.viewport()[0]; }, get outerHeight() { return N.viewport()[1] + 40; },
    get scrollX() { return N.viewport()[2]; }, get scrollY() { return N.viewport()[3]; }, get pageXOffset() { return N.viewport()[2]; }, get pageYOffset() { return N.viewport()[3]; }, get devicePixelRatio() { return N.viewport()[4]; },
    screenX: 0, screenY: 0, screenLeft: 0, screenTop: 0,
    scrollTo(x, y) { if (typeof x === 'object' && x) { y = x.top ?? N.viewport()[3]; x = x.left ?? N.viewport()[2]; } N.scrollTo(+x || 0, +y || 0); }, scroll(x, y) { G.scrollTo(x, y); },
    scrollBy(x, y) { if (typeof x === 'object' && x) { y = x.top || 0; x = x.left || 0; } const v = N.viewport(); N.scrollTo(v[2] + (+x || 0), v[3] + (+y || 0)); },
    alert(m) { N.log(1, 'alert: ' + m); }, confirm() { return false; }, prompt() { return null; }, print() {}, focus() {}, blur() {}, close() { if (top === G) N.winOp(0, 4); }, stop() {}, open: openWin,
    getSelection() { return selection; }, reportError(e) { if (!arguments.length) throw new TypeError("Failed to execute 'reportError' on 'Window': 1 argument required, but only 0 present."); report(e); }, visualViewport: null, speechSynthesis: undefined,
};
for (const k of Object.keys(H)) globals[k] = H[k];
for (const k of Object.keys(globals)) { const d = Object.getOwnPropertyDescriptor(globals, k); d.configurable = true; if (!d.get) d.writable = true; d.enumerable = !!d.get; Object.defineProperty(G, k, d); }   /* attributes enumerate, interfaces do not */
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
/* window.event is [Replaceable]: assigning it replaces the accessor with a plain value */
Object.defineProperty(G, 'event', { get: { get event() {
    if (this != null && this !== G) throw new TypeError("Failed to read the 'event' property from 'Window': Illegal invocation");
    return curEvent; } }.__lookupGetter__('event'), set(v) { if (this != null && this !== G) throw new TypeError("Failed to set the 'event' property on 'Window': Illegal invocation"); Object.defineProperty(G, 'event', { value: v, writable: true, configurable: true, enumerable: true }); }, configurable: true, enumerable: true });
const visual = Object.assign(Object.create(VisualViewport.prototype), { onresize: null, onscroll: null, onscrollend: null });
N.etBrand(visual);
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
if (navigator.maxTouchPoints) for (const type of ['touchstart', 'touchmove', 'touchend', 'touchcancel']) def(G, 'on' + type, null);
function fire(target, type, init, Ctor = Event, legacyTargetOverride = false) {
    const touchClick = type === 'click' && init && init.__touch;
    if (init && init.__touch) init = nativeTouchInit(type, init);
    if (init && init.__edit) oprReplace(target, init.__edit[0], init.__edit[1], init.__edit[2]);
    const ev = new Ctor(type, init), ok = dispatch(target, ev, true, legacyTargetOverride);   /* dispatch runs the click's activation behavior */
    /* a tap on a text field, textarea or select focuses it */
    if (ok && type === 'click' && target && N.isNode(target) && N.type(target) === 1 && !target.disabled && !(target.closest && target.closest('a[href]'))) {
        const n = N.name(target);
        if (n === 'textarea' || n === 'select' || (n === 'input' && !ACT_INPUT.has(target.type))) N.focus(target);
    }
    if (touchClick) nativeTap = null;
    return ok;
}
function fireAnim(t, type, name, elapsed, anim) {
    if (type === 'lumen-transition' || type === 'lumen-animation') { (type === 'lumen-animation' ? caStart : trStart)(t, name); waDirty = true; waFlush(); return; }
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
    const fire = (h, type, inputType, data, targetRanges = [], dataTransfer = null) => h.dispatchEvent(new InputEvent(type, { inputType, data, targetRanges, dataTransfer, bubbles: true, cancelable: type === 'beforeinput', composed: true }));
    /* the DataTransfer of the paste being performed, for its beforeinput/input (insertFromPaste) */
    let pasting = null;
    /* the text next to the caret within host h, in tree order: [text node, offset] at its end (back) or start */
    const textBeside = (h, n, back) => {
        const tw = document.createTreeWalker(h, 4);
        tw.currentNode = n;
        for (let t = back ? tw.previousNode() : tw.nextNode(); t; t = back ? tw.previousNode() : tw.nextNode()) if (t.data.length) return [t, back ? t.data.length : 0];
        return null;
    };
    const wordChar = /[\p{L}\p{N}_]/u;
    /* the StaticRanges a key edit will change (Input Events' getTargetRanges()) */
    const editRanges = (h, s, type) => {
        const r = s.rangeCount ? s.getRangeAt(0) : null;
        if (!r) return [];
        if (!r.collapsed || !type.startsWith('delete')) {
            /* element boundaries move into the text they bound, as Chrome reports them */
            const down = (n, o, atEnd) => {
                for (;;) {
                    const c = n.nodeType === 1 ? (atEnd ? n.childNodes[o - 1] : n.childNodes[o]) : null;
                    if (!c || (c.nodeType === 1 && (c.localName === 'br' || !c.firstChild)) || (c.nodeType !== 1 && c.nodeType !== 3)) return [n, o];
                    if (c.nodeType === 3) return [c, atEnd ? c.data.length : 0];
                    n = c; o = atEnd ? c.childNodes.length : 0;
                }
            };
            const [sn, so] = r.collapsed ? [r.startContainer, r.startOffset] : down(r.startContainer, r.startOffset, false);
            const [en, eo] = r.collapsed ? [r.endContainer, r.endOffset] : down(r.endContainer, r.endOffset, true);
            return [new StaticRange({ startContainer: sn, startOffset: so, endContainer: en, endOffset: eo })];
        }
        const back = type.endsWith('Backward'), word = type.startsWith('deleteWord');
        let n = r.startContainer, o = r.startOffset;
        /* a <br> right before (Backspace) or after (Delete) the caret is what goes */
        const beside = n.nodeType === 3 ? ((back ? !o : o >= n.data.length) ? (back ? n.previousSibling : n.nextSibling) : null) : back ? n.childNodes[o - 1] : n.childNodes[o];
        if (beside && beside.nodeType === 1 && beside.localName === 'br' && (back || beside.nextSibling)) {
            const p = beside.parentNode, i = [...p.childNodes].indexOf(beside);
            return [new StaticRange({ startContainer: p, startOffset: i, endContainer: p, endOffset: i + 1 })];
        }
        if (n.nodeType !== 3 || (back ? !o : o >= n.data.length)) {
            const ref = n.nodeType === 3 ? n : (back ? n.childNodes[o - 1] : n.childNodes[o]) || n;
            const t = textBeside(h, ref, back);
            if (!t) return [new StaticRange({ startContainer: n, startOffset: o, endContainer: n, endOffset: o })];
            const [tn, to] = t;
            /* across a block boundary only the break goes (the blocks join), as in Chrome */
            const blockOf = x => { for (let e = x.nodeType === 1 ? x : x.parentNode; e && e !== h; e = e.parentNode) if (/^(div|p|li|h[1-6]|blockquote|pre|tr|td|th|section|article|dd|dt)$/.test(e.localName)) return e; return h; };
            const cross = blockOf(tn) !== blockOf(n);
            return [back ? new StaticRange({ startContainer: tn, startOffset: cross ? to : Math.max(0, to - 1), endContainer: n, endOffset: o }) : new StaticRange({ startContainer: n, startOffset: o, endContainer: tn, endOffset: cross ? to : Math.min(tn.data.length, 1) })];
        }
        let a = o, b = o;
        const d = n.data;
        if (word) {
            if (back) { while (a > 0 && !wordChar.test(d[a - 1])) a--; while (a > 0 && wordChar.test(d[a - 1])) a--; }
            else { while (b < d.length && !wordChar.test(d[b])) b++; while (b < d.length && wordChar.test(d[b])) b++; }
        } else if (back) a -= a > 1 && /[\udc00-\udfff]/.test(d[a - 1]) ? 2 : 1;
        else b += /[\ud800-\udbff]/.test(d[b]) && b + 1 < d.length ? 2 : 1;
        return [new StaticRange({ startContainer: n, startOffset: a, endContainer: n, endOffset: b })];
    };
    /* key edits: [inputType, editing command]; Ctrl deletes a word, Shift+Enter inserts a line break */
    const keyEdit = (e, h) => {
        if (e.key === 'Enter') return e.shiftKey || h.getAttribute('contenteditable') === 'plaintext-only' ? ['insertLineBreak', 'insertlinebreak'] : ['insertParagraph', 'insertparagraph'];
        if (e.key === 'Delete') return [e.ctrlKey ? 'deleteWordForward' : 'deleteContentForward', 'forwarddelete'];
        return [e.ctrlKey ? 'deleteWordBackward' : 'deleteContentBackward', 'delete'];
    };
    const edit = (e, del) => {
        e.stopImmediatePropagation();
        const h = editHost(e.target); if (!h) return;
        const dt = del ? null : pasting; pasting = null;
        const s = caret(h), data = del ? null : e.key, [type, cmd] = del ? keyEdit(e, h) : [dt ? 'insertFromPaste' : 'insertText', 'inserttext'];
        /* typed text runs Chrome's sequence: keypress, beforeinput, textInput (React's onBeforeInput, which Draft.js
           editors rely on), then the insertion and input; cancelling any of them cancels the insertion */
        const typed = !del && !dt && !e.lumenExec;
        if (del && e.key === 'Enter' && !dispatch(e.target, new KeyboardEvent('keypress', { key: 'Enter', charCode: 13, keyCode: 13, which: 13, bubbles: true, cancelable: true, composed: true, view: G }), true)) return;
        if (typed && [...data].length === 1) {
            const cc = data.codePointAt(0);
            if (!dispatch(e.target, new KeyboardEvent('keypress', { key: data, charCode: cc, keyCode: cc, which: cc, bubbles: true, cancelable: true, composed: true, view: G }), true)) return;
        }
        if (!e.lumenExec && !fire(h, 'beforeinput', type, dt ? null : data, editRanges(h, s, type), dt)) return;
        if (typed) {
            const te = document.createEvent('TextEvent');
            te.initTextEvent('textInput', true, true, G, data);
            if (!dispatch(e.target, te, true)) return;
        }
        const c = s.focusNode, o = s.focusOffset;
        /* typing or pasting into a text node: the common case, kept cheap (a typed space goes through the editing
           commands, which keep it visible as a no-break space where needed) */
        if (!del && s.isCollapsed && c.nodeType === 3 && !data.includes('\n') && (data.length > 1 || !/\s/.test(data))) { c.insertData(o, data); s.collapse(c, o + data.length); }
        else {
            const before = del ? h.innerHTML : null;
            if (type.startsWith('deleteWord')) {
                const r = editRanges(h, s, type)[0];
                if (r && !r.collapsed) { const x = document.createRange(); x.setStart(r.startContainer, r.startOffset); x.setEnd(r.endContainer, r.endOffset); s.removeAllRanges(); s.addRange(x); }
            }
            let ok = false;
            try { ok = editing().exec(type.startsWith('deleteWord') ? 'delete' : cmd, false, data ?? '', undefined); } catch (x) { ok = false; }
            if (!ok || (before !== null && h.innerHTML === before)) return;   /* nothing changed: no input event */
        }
        fire(h, 'input', type, dt ? null : data, [], dt);
        document.dispatchEvent(new Event('selectionchange'));
    };
    /* a native paste (e.key: the clipboard text): the paste event with the clipboard's text and image; cancelling it
       cancels the native insertion. An image alone pastes into an editing host as an <img>, as in Chrome. */
    document.addEventListener('lumenpaste', e => {
        e.stopImmediatePropagation();
        const dt = new DataTransfer(), text = e.key || '', t = e.target;
        if (text) dt.setData('text/plain', text);
        const img = N.clipImage();
        const file = img ? new File([img[1]], 'image.' + (img[0].split('/')[1] || 'png').replace('jpeg', 'jpg'), { type: img[0], lastModified: Date.now() }) : null;
        if (file) dt.items.add(file);
        if (!t.dispatchEvent(new ClipboardEvent('paste', { clipboardData: dt, bubbles: true, cancelable: true, composed: true }))) { e.preventDefault(); return; }
        const h = editHost(t);
        if (text) { pasting = h ? dt : null; return; }
        if (!file || !h) return;
        e.preventDefault();
        if (!fire(h, 'beforeinput', 'insertFromPaste', null, editRanges(h, caret(h), 'insertFromPaste'), dt)) return;
        const r = new FileReader();
        r.onload = () => {
            let ok = false;
            try { ok = editing().exec('insertimage', false, r.result, undefined); } catch (x) { ok = false; }
            if (ok) { fire(h, 'input', 'insertFromPaste', null, [], dt); document.dispatchEvent(new Event('selectionchange')); }
        };
        r.readAsDataURL(file);
    }, true);
    /* a focused editing host always holds the selection, as in Chrome: one that gains focus with the selection
       elsewhere gets a caret at its first position before its focus handlers run (editors such as Draft.js treat
       no selection as no focus); a click then moves it to the clicked point */
    document.addEventListener('focus', e => {
        const t = e.target;
        if (!(t instanceof Element) || !t.isContentEditable || editHost(t) !== t) return;
        const s = document.getSelection();
        if (s.rangeCount && t.contains(s.anchorNode)) return;
        /* as Chrome: the first visible character, or before a <br>, <img> or empty element */
        let n = t, o = 0;
        while (n.firstChild) {
            const c = n.firstChild;
            if (c.nodeType === 1 && (c.tagName === 'BR' || c.tagName === 'IMG')) break;
            if (c.nodeType === 1 && !c.firstChild) {
                const cs = getComputedStyle(c), box = ['Top', 'Right', 'Bottom', 'Left'].some(k => parseFloat(cs['padding' + k]) > 0 || parseFloat(cs['border' + k + 'Width']) > 0);
                if (box) n = c;
                break;
            }
            n = c;
            if (c.nodeType === 3) {
                const ws = /^(pre|pre-wrap|break-spaces)$/.test(getComputedStyle(c.parentNode).whiteSpace) ? 0 : /^[ \t\n\r\f]*/.exec(c.data)[0].length;
                o = ws < c.data.length ? ws : 0;
                break;
            }
        }
        s.collapse(n, o);
    }, true);
    /* the edit bar's Undo: each text field's earlier values (an editing host's markup), taken as input events
       change it; undoing restores one through the value setter, so editors that track it see the change */
    const undoStack = new WeakMap(), undoLast = new WeakMap();
    const undoState = t => t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement ? t.value : t.innerHTML;
    const undoTarget = t => t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement ? t : t instanceof Element && t.isContentEditable ? editHost(t) : null;
    document.addEventListener('focusin', e => { const t = undoTarget(e.target); if (t && !undoLast.has(t)) undoLast.set(t, undoState(t)); }, true);
    document.addEventListener('input', e => {
        const t = undoTarget(e.target);
        if (!t || e.inputType === 'historyUndo') return;
        const was = undoLast.get(t), now = undoState(t);
        if (was !== undefined && was !== now) {
            let st = undoStack.get(t);
            if (!st) undoStack.set(t, st = []);
            st.push(was);
            if (st.length > 100) st.shift();
        }
        undoLast.set(t, now);
    }, true);
    document.addEventListener('lumenundo', e => {
        const t = undoTarget(e.target), st = t && undoStack.get(t);
        if (!st || !st.length) return;
        const v = st.pop();
        if (!fire(t, 'beforeinput', 'historyUndo', null, [], null)) { st.push(v); return; }
        if (t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement) {
            Object.getOwnPropertyDescriptor(Object.getPrototypeOf(t), 'value').set.call(t, v);
            try { t.setSelectionRange(v.length, v.length); } catch (_) { }
        } else {
            t.innerHTML = v;
            const s = document.getSelection(); s.selectAllChildren(t); s.collapseToEnd();
        }
        undoLast.set(t, v);
        fire(t, 'input', 'historyUndo', null, [], null);
    }, true);
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
/* WebIDL shape of the interfaces defined above: attributes and operations are enumerable, constants are read-only,
   and operations reject calls with fewer arguments than they require */
{
    const REQUIRED = {
        EventTarget: { addEventListener: 2, removeEventListener: 2, dispatchEvent: 1 },
        Event: { initEvent: 1 }, CustomEvent: { initCustomEvent: 1 },
        Node: { appendChild: 1, removeChild: 1, insertBefore: 2, replaceChild: 2, contains: 1, compareDocumentPosition: 1, isEqualNode: 1,
                isSameNode: 1, lookupPrefix: 1, lookupNamespaceURI: 1, isDefaultNamespace: 1 },
        Attr: { contains: 1, isEqualNode: 1, isSameNode: 1, lookupPrefix: 1, lookupNamespaceURI: 1, isDefaultNamespace: 1 },
        CharacterData: { appendData: 1, deleteData: 2, insertData: 2, replaceData: 3, substringData: 2 },
        Text: { splitText: 1 },
        Document: { createAttribute: 1, createAttributeNS: 2, createCDATASection: 1, createComment: 1, createElement: 1, createElementNS: 2,
                    createEvent: 1, createExpression: 1, createNSResolver: 1, createProcessingInstruction: 2, createTextNode: 1, getElementById: 1,
                    getElementsByClassName: 1, getElementsByTagName: 1, getElementsByTagNameNS: 2, querySelector: 1, querySelectorAll: 1, moveBefore: 2 },
        DocumentFragment: { getElementById: 1, querySelector: 1, querySelectorAll: 1, moveBefore: 2 },
        DOMImplementation: { createDocument: 2, createDocumentType: 3 },
        Element: { attachShadow: 1, closest: 1, getAttribute: 1, getAttributeNS: 2, getAttributeNode: 1, getAttributeNodeNS: 2, getElementsByClassName: 1,
                   getElementsByTagName: 1, getElementsByTagNameNS: 2, hasAttribute: 1, hasAttributeNS: 2, insertAdjacentElement: 2, insertAdjacentText: 2,
                   matches: 1, webkitMatchesSelector: 1, moveBefore: 2, querySelector: 1, querySelectorAll: 1, removeAttribute: 1, removeAttributeNS: 2,
                   removeAttributeNode: 1, setAttribute: 2, setAttributeNS: 3, toggleAttribute: 1 },
        HTMLCollection: { item: 1, namedItem: 1 }, NodeList: { item: 1 },
        XPathEvaluator: { createExpression: 1, createNSResolver: 1 },
    };
    for (const [iface, ops] of Object.entries(REQUIRED)) {
        const P = G[iface] && G[iface].prototype;
        if (!P) continue;
        for (const [k, n] of Object.entries(ops)) {
            const d = Object.getOwnPropertyDescriptor(P, k);
            if (!d || typeof d.value !== 'function' || !d.configurable) continue;
            const f = d.value;
            d.value = { [k]() {
                if (arguments.length < n) throw new TypeError(`Failed to execute '${k}' on '${iface}': ${n} argument${n > 1 ? 's' : ''} required, but only ${arguments.length} present.`);
                return f.apply(this, arguments);
            } }[k];
            Object.defineProperty(d.value, 'length', { value: n });
            Object.defineProperty(P, k, d);
        }
    }
    /* members that would run on any receiver: a call on an object that is not one of the interface's throws, as
       WebIDL's operations and attributes do (the others reach a native check first) */
    const BRAND = {
        Node: ['baseURI', 'childNodes', 'contains', 'isEqualNode', 'isSameNode', 'lookupPrefix', 'removeChild'],
        Document: ['URL', 'children', 'compatMode', 'fullscreen', 'importNode', 'createAttributeNS', 'createComment', 'createDocumentFragment', 'createElementNS', 'createEvent',
                   'createExpression', 'createNSResolver', 'createNodeIterator', 'createProcessingInstruction', 'createRange', 'createTextNode',
                   'createTreeWalker', 'documentURI', 'evaluate', 'fullscreenElement', 'fullscreenEnabled', 'getElementById', 'getElementsByClassName',
                   'getElementsByTagName', 'getElementsByTagNameNS', 'implementation', 'onfullscreenchange', 'onfullscreenerror'],
        DocumentFragment: ['children'], ShadowRoot: ['clonable', 'delegatesFocus', 'fullscreenElement', 'mode', 'onslotchange', 'serializable', 'slotAssignment'], Text: ['assignedSlot'],
        Element: ['assignedSlot', 'attributes', 'children', 'classList', 'closest', 'getElementsByClassName', 'getElementsByTagName',
                  'getElementsByTagNameNS', 'insertAdjacentElement', 'onfullscreenchange', 'onfullscreenerror', 'prefix', 'shadowRoot'],
        Attr: ['localName', 'name', 'namespaceURI', 'ownerElement', 'prefix', 'specified', 'value'],
        AbstractRange: ['collapsed', 'endOffset', 'startOffset'], Range: ['cloneRange', 'collapse', 'commonAncestorContainer', 'detach', 'selectNodeContents'],
        NodeIterator: ['detach', 'filter', 'pointerBeforeReferenceNode', 'referenceNode', 'root', 'whatToShow'],
        TreeWalker: ['currentNode', 'filter', 'parentNode', 'previousNode', 'root', 'whatToShow'],
        DOMTokenList: ['supports'], DOMImplementation: ['createDocument', 'createDocumentType', 'createHTMLDocument', 'hasFeature'], HTMLCollection: ['namedItem'], NodeList: ['item'], MutationObserver: ['disconnect', 'takeRecords'],
        XPathEvaluator: ['createExpression', 'createNSResolver', 'evaluate'], XPathResult: ['invalidIteratorState', 'resultType'],
    };
    const isNodeObj = o => N.isNode(o);
    const FOREIGN_BRANDS = { Node: isNodeObj, Document: isNodeObj, DocumentFragment: isNodeObj, ShadowRoot: isNodeObj, Text: isNodeObj, Element: isNodeObj,
        DOMImplementation: o => '_doc' in o };
    for (const [iface, names] of Object.entries(BRAND)) {
        const P = G[iface] && G[iface].prototype;
        if (!P) continue;
        const own = Object.prototype.isPrototypeOf.bind(P);
        /* a node from another same-origin realm passes when its own realm's interface matches */
        const isP = !FOREIGN_BRANDS[iface] ? own : o => own(o) || (o !== null && typeof o === 'object' && FOREIGN_BRANDS[iface](o) && (() => {
            for (let p = Object.getPrototypeOf(o); p; p = Object.getPrototypeOf(p)) { const c = Object.getOwnPropertyDescriptor(p, 'constructor'); if (c && typeof c.value === 'function' && c.value.name === iface) return true; }
            return false;
        })());
        for (const k of names) {
            const d = Object.getOwnPropertyDescriptor(P, k);
            if (!d || !d.configurable) continue;
            const check = o => { if (o === null || (typeof o !== 'object' && typeof o !== 'function') || !isP(o)) throw new TypeError(`Failed to execute '${k}' on '${iface}': Illegal invocation`); };
            if (typeof d.value === 'function') {
                const f = d.value;
                d.value = { [k]() { check(this); return f.apply(this, arguments); } }[k];
                Object.defineProperty(d.value, 'length', { value: f.length });
            } else if (d.get) {
                const g = d.get, st = d.set;
                d.get = Object.getOwnPropertyDescriptor({ get [k]() { check(this); return g.call(this); } }, k).get;
                if (st) d.set = Object.getOwnPropertyDescriptor({ set [k](v) { check(this); st.call(this, v); } }, k).set;
            } else continue;
            Object.defineProperty(P, k, d);
        }
    }
    const CONST = /^[A-Z][A-Z0-9_]*$/;
    const shape = (o, skip) => {
        for (const k of Object.getOwnPropertyNames(o)) {
            if (skip.has(k) || k[0] === '_') continue;
            const d = Object.getOwnPropertyDescriptor(o, k);
            if (!d.configurable) continue;
            if (CONST.test(k) && typeof d.value === 'number') Object.defineProperty(o, k, { value: d.value, writable: false, enumerable: true, configurable: false });
            else if (!d.enumerable) { d.enumerable = true; Object.defineProperty(o, k, d); }
        }
    };
    const PROTO_SKIP = new SSet(['constructor']), STATIC_SKIP = new SSet(['length', 'name', 'prototype', 'arguments', 'caller']);
    for (const name of Object.getOwnPropertyNames(G)) {
        if (PRE_GLOBALS.has(name) || !/^[A-Z]/.test(name)) continue;
        const d = Object.getOwnPropertyDescriptor(G, name), C = d && d.value;
        if (typeof C !== 'function' || !C.prototype || typeof C.prototype !== 'object') continue;
        PLATFORM_PROTOS.add(C.prototype);
        shape(C.prototype, PROTO_SKIP);
        shape(C, STATIC_SKIP);
    }
}
/* Temporal: this V8 is built without it, so the temporal-polyfill (src/js/lazy/temporal.js, which also installs a
   Temporal-aware Intl.DateTimeFormat) is compiled on a page's first use */
{
    const load = () => { delete globalThis.Temporal; delete Date.prototype.toTemporalInstant; new Function(N.lazySrc('temporal'))(); return globalThis.Temporal; };
    Object.defineProperty(globalThis, 'Temporal', { get: load, set(v) { Object.defineProperty(globalThis, 'Temporal', { value: v, writable: true, configurable: true }); }, configurable: true });
    Object.defineProperty(Date.prototype, 'toTemporalInstant', { value: function toTemporalInstant() { load(); return Date.prototype.toTemporalInstant.call(this); }, writable: true, configurable: true });
}
return { ttInit, ttScript, ttParserScript, ttEval, queueMessage, exportPort, portMessage, rejection, workerEvent, protoFor, dispatch, fire, fireAnim, report, mediaChanged, viewportChanged, ceConnected, ceParserCreate, ceParserInserted, nativeTouch, Event, MouseEvent, PointerEvent, KeyboardEvent, FocusEvent, WheelEvent, InputEvent, PopStateEvent, PageTransitionEvent, ErrorEvent };
})
