/* URL and URLSearchParams per the URL standard, shared by the page and worker preludes. Parsing and the setters run in C
   (URLN.urlParse / URLN.urlSet return [href, protocol, username, password, host, hostname, port, pathname, search, hash, origin]). */
const urlUsv = v => String(v).toWellFormed();
const urlArgs = (args, n, what) => { if (args.length < n) throw new TypeError(`Failed to execute '${what}': ${n} argument${n > 1 ? 's' : ''} required, but only ${args.length} present.`); };
/* application/x-www-form-urlencoded: percent-decoded bytes are UTF-8 decoded with U+FFFD for invalid sequences */
const urlUtf8 = b => {
    let s = '', i = 0;
    const n = b.length;
    while (i < n) {
        const c = b[i];
        if (c < 0x80) { s += String.fromCharCode(c); i++; continue; }
        let need = 0, cp = 0, lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) { need = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; cp = c & 0xF; if (c === 0xE0) lo = 0xA0; if (c === 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; cp = c & 7; if (c === 0xF0) lo = 0x90; if (c === 0xF4) hi = 0x8F; }
        else { s += '�'; i++; continue; }
        let j = i + 1, ok = true;
        for (let k = 0; k < need; k++, j++) {
            const d = b[j];
            if (j >= n || d < lo || d > hi) { ok = false; break; }
            cp = (cp << 6) | (d & 0x3F); lo = 0x80; hi = 0xBF;
        }
        s += ok ? String.fromCodePoint(cp) : '�';
        i = j;
    }
    return s;
};
const urlHex = c => c >= 48 && c <= 57 ? c - 48 : (c | 32) >= 97 && (c | 32) <= 102 ? (c | 32) - 87 : -1;
const formDecode = s => {
    s = s.replace(/\+/g, ' ');
    if (!s.includes('%')) return s;
    const b = new TextEncoder().encode(s), o = new Uint8Array(b.length);
    let n = 0;
    for (let i = 0; i < b.length; i++) {
        const h = b[i] === 37 && i + 2 < b.length ? urlHex(b[i + 1]) : -1, l = h >= 0 ? urlHex(b[i + 2]) : -1;
        if (l >= 0) { o[n++] = h * 16 + l; i += 2; } else o[n++] = b[i];
    }
    return urlUtf8(o.subarray(0, n));
};
const formParse = s => {
    const l = [];
    for (const p of s.split('&')) {
        if (!p) continue;
        const i = p.indexOf('=');
        l.push(i < 0 ? [formDecode(p), ''] : [formDecode(p.slice(0, i)), formDecode(p.slice(i + 1))]);
    }
    return l;
};
const formEncode = s => {
    if (/^[A-Za-z0-9*._-]*$/.test(s)) return s;
    let o = '';
    for (const b of new TextEncoder().encode(s)) {
        if (b === 32) o += '+';
        else if ((b >= 48 && b <= 57) || (b >= 65 && b <= 90) || (b >= 97 && b <= 122) || b === 42 || b === 45 || b === 46 || b === 95) o += String.fromCharCode(b);
        else o += '%' + (b < 16 ? '0' : '') + b.toString(16).toUpperCase();
    }
    return o;
};
/* WebIDL: attributes and operations are enumerable; classes get their toStringTag */
const urlIdl = (C, tag) => {
    for (const o of [C.prototype, C]) for (const k of Object.getOwnPropertyNames(o)) {
        if (k === 'constructor' || k === 'prototype' || k === 'length' || k === 'name') continue;
        const d = Object.getOwnPropertyDescriptor(o, k); d.enumerable = true; Object.defineProperty(o, k, d);
    }
    Object.defineProperty(C.prototype, Symbol.toStringTag, { value: tag, configurable: true });
};
let uspAttach, uspReset, urlSetQuery, uspIter;
class URLSearchParams {
    #l = []; #u = null;
    constructor(init = '') {
        if (init !== null && (typeof init === 'object' || typeof init === 'function')) {
            const it = init[Symbol.iterator];
            if (it !== undefined && it !== null) {
                if (typeof it !== 'function') throw new TypeError("Failed to construct 'URLSearchParams': The object's @@iterator property is not callable.");
                for (const pair of init) {
                    if (pair === null || (typeof pair !== 'object' && typeof pair !== 'function')) throw new TypeError("Failed to construct 'URLSearchParams': The provided value cannot be converted to a sequence.");
                    const a = [...pair];
                    if (a.length !== 2) throw new TypeError("Failed to construct 'URLSearchParams': Sequence initializer must only contain pair elements");
                    this.#l.push([urlUsv(a[0]), urlUsv(a[1])]);
                }
            } else {
                // a WebIDL record: keys that become equal as USVStrings keep the first position and the last value
                const m = new SMap();
                for (const k of Reflect.ownKeys(init)) {
                    const d = Reflect.getOwnPropertyDescriptor(init, k);
                    if (d && d.enumerable) m.set(urlUsv(k), urlUsv(init[k]));
                }
                this.#l = [...m];
            }
        } else {
            init = urlUsv(init);
            this.#l = formParse(init[0] === '?' ? init.slice(1) : init);
        }
    }
    static {
        uspAttach = (sp, url) => { sp.#u = url; };
        uspReset = (sp, l) => { sp.#l = l; };
        const IterProto = Object.create(Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())));
        const state = new WeakMap();
        Object.defineProperty(IterProto, 'next', { value: function next() {
            const s = state.get(this);
            if (!s) throw new TypeError('Illegal invocation');
            const l = s.sp.#l;
            if (s.i >= l.length) return { value: undefined, done: true };
            const e = l[s.i++];
            return { value: s.k === 0 ? e[0] : s.k === 1 ? e[1] : [e[0], e[1]], done: false };
        }, writable: true, enumerable: true, configurable: true });
        Object.defineProperty(IterProto, Symbol.toStringTag, { value: 'URLSearchParams Iterator', configurable: true });
        uspIter = (sp, k) => { sp.#l; const it = Object.create(IterProto); state.set(it, { sp, k, i: 0 }); return it; };
    }
    #update() { if (this.#u) urlSetQuery(this.#u, this.toString()); }
    get size() { return this.#l.length; }
    append(name, value) { urlArgs(arguments, 2, 'append'); this.#l.push([urlUsv(name), urlUsv(value)]); this.#update(); }
    delete(name, value = undefined) {
        urlArgs(arguments, 1, 'delete'); name = urlUsv(name);
        if (value === undefined) this.#l = this.#l.filter(e => e[0] !== name);
        else { value = urlUsv(value); this.#l = this.#l.filter(e => e[0] !== name || e[1] !== value); }
        this.#update();
    }
    get(name) { urlArgs(arguments, 1, 'get'); name = urlUsv(name); const e = this.#l.find(x => x[0] === name); return e ? e[1] : null; }
    getAll(name) { urlArgs(arguments, 1, 'getAll'); name = urlUsv(name); return this.#l.filter(x => x[0] === name).map(x => x[1]); }
    has(name, value = undefined) {
        urlArgs(arguments, 1, 'has'); name = urlUsv(name);
        if (value === undefined) return this.#l.some(e => e[0] === name);
        value = urlUsv(value); return this.#l.some(e => e[0] === name && e[1] === value);
    }
    set(name, value) {
        urlArgs(arguments, 2, 'set'); name = urlUsv(name); value = urlUsv(value);
        const i = this.#l.findIndex(e => e[0] === name);
        if (i < 0) this.#l.push([name, value]);
        else { this.#l[i] = [name, value]; this.#l = this.#l.filter((e, j) => j <= i || e[0] !== name); }
        this.#update();
    }
    sort() { this.#l.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); this.#update(); }
    forEach(callback, thisArg = undefined) {
        urlArgs(arguments, 1, 'forEach');
        if (typeof callback !== 'function') throw new TypeError("Failed to execute 'forEach' on 'URLSearchParams': The callback provided as parameter 1 is not a function.");
        for (let i = 0; i < this.#l.length; i++) { const e = this.#l[i]; callback.call(thisArg, e[1], e[0], this); }
    }
    keys() { return uspIter(this, 0); }
    values() { return uspIter(this, 1); }
    entries() { return uspIter(this, 2); }
    toString() { return this.#l.map(e => formEncode(e[0]) + '=' + formEncode(e[1])).join('&'); }
}
urlIdl(URLSearchParams, 'URLSearchParams');
Object.defineProperty(URLSearchParams.prototype, Symbol.iterator, { value: URLSearchParams.prototype.entries, writable: true, configurable: true });

class URL {
    #p; #sp = null;
    constructor(url, base = undefined) {
        urlArgs(arguments, 1, 'URL');
        const r = URLN.urlParse(urlUsv(url), base === undefined ? null : urlUsv(base));
        if (!r) throw new TypeError("Failed to construct 'URL': Invalid URL");
        this.#p = r;
    }
    static {
        urlSetQuery = (url, s) => { url.#p = URLN.urlSet(url.#p[0], 'search', s) || url.#p; };
    }
    static parse(url, base = undefined) { urlArgs(arguments, 1, 'parse'); try { return new URL(url, base); } catch (e) { return null; } }
    static canParse(url, base = undefined) { urlArgs(arguments, 1, 'canParse'); return !!URLN.urlParse(urlUsv(url), base === undefined ? null : urlUsv(base)); }
    #set(k, v) { const r = URLN.urlSet(this.#p[0], k, urlUsv(v)); if (r) this.#p = r; }
    get href() { return this.#p[0]; }
    set href(v) {
        const r = URLN.urlParse(urlUsv(v), null);
        if (!r) throw new TypeError(`Failed to set the 'href' property on 'URL': Invalid URL '${v}'`);
        this.#p = r;
        if (this.#sp) uspReset(this.#sp, formParse(r[8].slice(1)));
    }
    get origin() { return this.#p[10]; }
    get protocol() { return this.#p[1]; } set protocol(v) { this.#set('protocol', v); }
    get username() { return this.#p[2]; } set username(v) { this.#set('username', v); }
    get password() { return this.#p[3]; } set password(v) { this.#set('password', v); }
    get host() { return this.#p[4]; } set host(v) { this.#set('host', v); }
    get hostname() { return this.#p[5]; } set hostname(v) { this.#set('hostname', v); }
    get port() { return this.#p[6]; } set port(v) { this.#set('port', v); }
    get pathname() { return this.#p[7]; } set pathname(v) { this.#set('pathname', v); }
    get search() { return this.#p[8]; }
    set search(v) {
        v = urlUsv(v); this.#set('search', v);
        if (this.#sp) uspReset(this.#sp, v === '' ? [] : formParse(v[0] === '?' ? v.slice(1) : v));
    }
    get searchParams() {
        if (!this.#sp) { this.#sp = new URLSearchParams(this.#p[8]); uspAttach(this.#sp, this); }
        return this.#sp;
    }
    get hash() { return this.#p[9]; } set hash(v) { this.#set('hash', v); }
    toJSON() { return this.#p[0]; }
    toString() { return this.#p[0]; }
}
urlIdl(URL, 'URL');
