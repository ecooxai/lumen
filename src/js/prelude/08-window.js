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
let histState = null;
const history = {
    get length() { return N.histLen(); }, get state() { return histState; }, scrollRestoration: 'auto',
    pushState(s, t, u) { histState = structuredClone(s); if (u != null) N.setUrl(new URL(String(u), N.url()).href, true); },
    replaceState(s, t, u) { histState = structuredClone(s); if (u != null) N.setUrl(new URL(String(u), N.url()).href, false); },
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
    serviceWorker: undefined, storage: { estimate() { return Promise.resolve({ quota: 1e9, usage: 0 }); }, persist() { return Promise.resolve(false); } },
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
const crypto = {
    getRandomValues(a) { if (!ArrayBuffer.isView(a)) throw new TypeError('getRandomValues requires an ArrayBufferView'); const u = new Uint8Array(a.buffer, a.byteOffset, a.byteLength); N.random(u.buffer, u.byteOffset, u.byteLength); return a; },
    randomUUID() { const b = crypto.getRandomValues(new Uint8Array(16)); b[6] = (b[6] & 15) | 64; b[8] = (b[8] & 63) | 128; const h = [...b].map(x => x.toString(16).padStart(2, '0')).join(''); return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`; },
    subtle: { digest() { return Promise.reject(new DOMException('SubtleCrypto is not supported yet', 'NotSupportedError')); } },
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
function postMessage(data, origin) { const d = structuredClone(data); setTimeout(() => dispatch(G, new MessageEvent('message', { data: d, origin: location.origin, source: G }))); }
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
const document = N.doc();
Object.setPrototypeOf(document, HTMLDocument.prototype);
const globals = {
    DOMException, DOMRectReadOnly, DOMRect, Event, CustomEvent, UIEvent, FocusEvent, MouseEvent, PointerEvent, WheelEvent, KeyboardEvent, InputEvent, ErrorEvent, ProgressEvent, MessageEvent, PopStateEvent, HashChangeEvent, PageTransitionEvent, AnimationEvent, TransitionEvent, MediaQueryListEvent,
    EventTarget, AbortSignal, AbortController, MutationObserver, MutationRecord, Node, NodeList, HTMLCollection: NodeList, CharacterData, Text, CDATASection, Comment, DocumentType, DocumentFragment, ShadowRoot, Attr, NamedNodeMap, DOMTokenList,
    CSSStyleDeclaration, Element, HTMLElement, SVGElement, SVGGraphicsElement, SVGSVGElement, MathMLElement, Image, Audio, Option, CustomElementRegistry, Document, HTMLDocument, CSSRule, CSSStyleSheet, CSS, FontFace,
    TreeWalker, NodeIterator, NodeFilter, Range, Selection, Animation, DOMMatrixReadOnly, DOMMatrix, DOMPoint, DOMParser, XMLSerializer,
    TextEncoder, TextDecoder, btoa, atob, Blob, File, FileReader, ReadableStream, URLSearchParams, URL, webkitURL: URL, Headers, Request, Response, fetch, FormData, XMLHttpRequestEventTarget, XMLHttpRequest, WebSocket, MessagePort, MessageChannel, BroadcastChannel,
    ResizeObserver, IntersectionObserver, PerformanceObserver, Storage, localStorage, sessionStorage, Location, location, history, navigator, screen, performance, crypto, MediaQueryList, matchMedia, getComputedStyle,
    setTimeout, setInterval, clearTimeout, clearInterval, requestAnimationFrame, cancelAnimationFrame, requestIdleCallback, cancelIdleCallback, queueMicrotask, structuredClone, postMessage, console, customElements, Window, document,
    window: G, self: G, globalThis: G, top: G, parent: G, frames: G, opener: null, frameElement: null, closed: false, name: '', length: 0, origin: location.origin, isSecureContext: location.protocol === 'https:', crossOriginIsolated: false,
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
const visual = { get width() { return N.viewport()[0]; }, get height() { return N.viewport()[1]; }, offsetLeft: 0, offsetTop: 0, pageLeft: 0, pageTop: 0, scale: 1, addEventListener() {}, removeEventListener() {} };
G.visualViewport = visual;
function fire(target, type, init, Ctor = Event) { return dispatch(target, new Ctor(type, init), true); }
return { protoFor, dispatch, fire, report, mediaChanged, ceConnected, Event, MouseEvent, PointerEvent, KeyboardEvent, FocusEvent, WheelEvent, InputEvent, PopStateEvent, ErrorEvent };
})
