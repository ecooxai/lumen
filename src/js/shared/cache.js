/* The Cache API (Service Workers § 5.4-5.5): caches, CacheStorage and Cache, shared by the page and worker preludes.
   The caches live in this realm's memory, so a page and its service worker each see their own, and none survive a
   restart; together they hold at most CACHE_LIMIT bytes of bodies. The including prelude defines CACHE_URL(input),
   the absolute URL of a request or a string, and CACHE_RESP(entry), a new Response from a stored
   { status, statusText, headers: [[name, value]], bytes, url, type, redirected }. */
const CACHE_KEY = Symbol('Cache'), CACHE_LIMIT = 64 << 20;
let cacheBytes = 0;
const cacheErr = (m, msg) => new TypeError(`Failed to execute '${m}' on 'Cache': ${msg}`);
/* a request as the cache keeps it: its URL without the fragment, method and headers */
function cacheReq(input, m) {
    const url = CACHE_URL(input);
    if (url == null) throw cacheErr(m, 'Invalid URL');
    const o = typeof input === 'object' && input !== null;
    const headers = [];
    if (o && input.headers && typeof input.headers.forEach === 'function') input.headers.forEach((v, k) => headers.push([String(k).toLowerCase(), String(v)]));
    return { url: url.replace(/#.*$/s, ''), method: o && input.method ? String(input.method).toUpperCase() : 'GET', headers };
}
const cacheHget = (list, k) => { for (const [n, v] of list) if (n === k) return v; return null; };
/* the stored entry matches request r (Service Workers § "Request Matches Cached Item") */
function cacheMatches(e, r, opt) {
    if (!opt.ignoreMethod && r.method !== 'GET') return false;
    const strip = u => opt.ignoreSearch ? u.replace(/\?.*$/s, '') : u;
    if (strip(e.req.url) !== strip(r.url)) return false;
    if (opt.ignoreVary) return true;
    const vary = cacheHget(e.res.headers, 'vary');
    if (vary == null) return true;
    for (const f of vary.split(',')) {
        const k = f.trim().toLowerCase();
        if (!k) continue;
        if (k === '*' || cacheHget(e.req.headers, k) !== cacheHget(r.headers, k)) return false;
    }
    return true;
}
const cacheOpts = o => ({ ignoreSearch: !!(o && o.ignoreSearch), ignoreMethod: !!(o && o.ignoreMethod), ignoreVary: !!(o && o.ignoreVary) });
const cacheMake = e => CACHE_RESP(e.res);
const cacheReqObj = e => typeof Request === 'function' ? new Request(e.req.url, { method: e.req.method, headers: e.req.headers }) : { url: e.req.url, method: e.req.method };
/* a response's entry, reading its body; throws for the responses a cache refuses */
async function cacheStore(res, m) {
    if (!res || typeof res.arrayBuffer !== 'function') throw cacheErr(m, "parameter 2 is not of type 'Response'.");
    if (res.status === 206) throw cacheErr(m, 'Partial response (status code 206) is unsupported');
    const vary = res.headers && res.headers.get('vary');
    if (vary && vary.split(',').some(f => f.trim() === '*')) throw cacheErr(m, "Vary header contains *");
    if (res.bodyUsed) throw cacheErr(m, 'Response body is already used');
    const headers = [];
    res.headers.forEach((v, k) => headers.push([String(k).toLowerCase(), String(v)]));
    const bytes = new Uint8Array(await res.arrayBuffer());
    return { status: res.status, statusText: res.statusText, headers, bytes, url: res.url || '', type: res.type || 'basic', redirected: !!res.redirected };
}
function cacheCheckReq(r, m) {
    if (!/^https?:/i.test(r.url)) throw cacheErr(m, "Request scheme '" + r.url.split(':')[0] + "' is unsupported");
    if (r.method !== 'GET') throw cacheErr(m, "Request method '" + r.method + "' is unsupported");
}
class Cache {
    #entries = [];
    constructor(key = undefined) { if (key !== CACHE_KEY) throw new TypeError("Failed to construct 'Cache': Illegal constructor"); }
    async match(request, options = undefined) { const all = await this.matchAll(request, options); return all.length ? all[0] : undefined; }
    async matchAll(request = undefined, options = undefined) {
        const opt = cacheOpts(options);
        if (request === undefined) return this.#entries.map(cacheMake);
        const r = cacheReq(request, 'matchAll');
        return this.#entries.filter(e => cacheMatches(e, r, opt)).map(cacheMake);
    }
    async add(request) {
        if (!arguments.length) throw cacheErr('add', '1 argument required, but only 0 present.');
        return this.addAll([request]);
    }
    async addAll(requests) {
        if (!arguments.length) throw cacheErr('addAll', '1 argument required, but only 0 present.');
        const list = Array.from(requests, r => { const k = cacheReq(r, 'addAll'); cacheCheckReq(k, 'addAll'); return [r, k]; });
        const responses = await Promise.all(list.map(([r]) => fetch(r)));
        const stored = [];
        for (const res of responses) {
            if (!res.ok) throw cacheErr('addAll', 'Request failed');
            stored.push(await cacheStore(res, 'addAll'));
        }
        list.forEach(([, k], i) => this.#put(k, stored[i]));
    }
    async put(request, response) {
        if (arguments.length < 2) throw cacheErr('put', `2 arguments required, but only ${arguments.length} present.`);
        const k = cacheReq(request, 'put');
        cacheCheckReq(k, 'put');
        this.#put(k, await cacheStore(response, 'put'));
    }
    #put(req, res) {
        const old = this.#entries.filter(e => cacheMatches(e, req, cacheOpts(null)));
        const add = res.bytes.byteLength - old.reduce((n, e) => n + e.res.bytes.byteLength, 0);
        if (cacheBytes + add > CACHE_LIMIT) throw new DOMException('The cache quota was exceeded.', 'QuotaExceededError');
        cacheBytes += add;
        this.#entries = this.#entries.filter(e => !old.includes(e));
        this.#entries.push({ req, res });
    }
    async delete(request, options = undefined) {
        if (!arguments.length) throw cacheErr('delete', '1 argument required, but only 0 present.');
        const r = cacheReq(request, 'delete'), opt = cacheOpts(options);
        const hit = this.#entries.filter(e => cacheMatches(e, r, opt));
        for (const e of hit) cacheBytes -= e.res.bytes.byteLength;
        this.#entries = this.#entries.filter(e => !hit.includes(e));
        return hit.length > 0;
    }
    async keys(request = undefined, options = undefined) {
        if (request === undefined) return this.#entries.map(cacheReqObj);
        const r = cacheReq(request, 'keys'), opt = cacheOpts(options);
        return this.#entries.filter(e => cacheMatches(e, r, opt)).map(cacheReqObj);
    }
    static drop(c) { for (const e of c.#entries) cacheBytes -= e.res.bytes.byteLength; c.#entries = []; }
}
const cacheDrop = Cache.drop;
delete Cache.drop;
class CacheStorage {
    #caches = new SMap();
    constructor(key = undefined) { if (key !== CACHE_KEY) throw new TypeError("Failed to construct 'CacheStorage': Illegal constructor"); }
    async match(request, options = undefined) {
        const name = options && options.cacheName;
        if (name !== undefined) { const c = this.#caches.get(String(name)); return c ? c.match(request, options) : undefined; }
        for (const c of this.#caches.values()) { const r = await c.match(request, options); if (r !== undefined) return r; }
        return undefined;
    }
    async has(name) { return this.#caches.has(String(name)); }
    async open(name) {
        name = String(name);
        let c = this.#caches.get(name);
        if (!c) { c = new Cache(CACHE_KEY); this.#caches.set(name, c); }
        return c;
    }
    async delete(name) {
        name = String(name);
        const c = this.#caches.get(name);
        if (!c) return false;
        cacheDrop(c);
        this.#caches.delete(name);
        return true;
    }
    async keys() { return [...this.#caches.keys()]; }
}
for (const C of [Cache, CacheStorage]) Object.defineProperty(C.prototype, Symbol.toStringTag, { value: C.name, configurable: true });
const caches = new CacheStorage(CACHE_KEY);
