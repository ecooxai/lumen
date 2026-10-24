/* IndexedDB, kept in memory for the page's lifetime like localStorage. Requests run one per microtask
   once their transaction may start (transactions whose scopes overlap with a writer run in creation
   order); a transaction commits when a task passes with no request outstanding, and an abort restores
   the stores it wrote. Errors bubble request -> transaction -> database (EventTarget __evp). */
const idbDBs = new SMap(), idbOps = new SMap();
const idbErr = (name, msg) => new DOMException(msg || name, name);
const idbTag = v => Object.prototype.toString.call(v).slice(8, -1);
/* keys: number < Date < string < binary (ArrayBuffer) < array, kept as private copies */
function idbKey(v, seen) {
    if (typeof v === 'number') return Number.isNaN(v) ? undefined : v;
    if (typeof v === 'string') return v;
    if (v === null || typeof v !== 'object') return undefined;
    const tag = idbTag(v);
    if (tag === 'Date') { const t = Date.prototype.getTime.call(v); return Number.isNaN(t) ? undefined : new Date(t); }
    if (tag === 'ArrayBuffer') return v.slice(0);
    if (ArrayBuffer.isView(v)) return v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength);
    if (!Array.isArray(v)) return undefined;
    seen = seen || new SSet();
    if (seen.has(v)) return undefined;
    seen.add(v);
    const o = [];
    for (let i = 0; i < v.length; i++) { if (!(i in v)) return undefined; const k = idbKey(v[i], seen); if (k === undefined) return undefined; o.push(k); }
    seen.delete(v);
    return o;
}
const idbMustKey = v => { const k = idbKey(v); if (k === undefined) throw idbErr('DataError', 'The parameter is not a valid key.'); return k; };
const idbKeyType = k => typeof k === 'number' ? 0 : typeof k === 'string' ? 2 : k instanceof Date ? 1 : Array.isArray(k) ? 4 : 3;
function idbCmp(a, b) {
    const ta = idbKeyType(a), tb = idbKeyType(b);
    if (ta !== tb) return ta < tb ? -1 : 1;
    if (ta === 1) { a = a.getTime(); b = b.getTime(); }
    if (ta <= 2) return a < b ? -1 : a > b ? 1 : 0;
    if (ta === 3) {
        const x = new Uint8Array(a), y = new Uint8Array(b), n = Math.min(x.length, y.length);
        for (let i = 0; i < n; i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
        return x.length < y.length ? -1 : x.length > y.length ? 1 : 0;
    }
    const n = Math.min(a.length, b.length);
    for (let i = 0; i < n; i++) { const c = idbCmp(a[i], b[i]); if (c) return c; }
    return a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
}
const idbCopyKey = k => typeof k !== 'object' ? k : k instanceof Date ? new Date(k.getTime()) : Array.isArray(k) ? k.map(idbCopyKey) : k.slice(0);
/* key paths */
const idbPathOk = p => typeof p === 'string' ? p === '' || p.split('.').every(s => /^[$A-Za-z_][$\w]*$/.test(s)) : Array.isArray(p) && p.length > 0 && p.every(x => typeof x === 'string' && idbPathOk(x));
function idbEval(v, path) {   /* raw value at the path; undefined when missing */
    if (Array.isArray(path)) { const o = []; for (const p of path) { const x = idbEval(v, p); if (x === undefined) return undefined; o.push(x); } return o; }
    if (path === '') return v;
    for (const s of path.split('.')) {
        if (v === null || v === undefined || (typeof v !== 'object' && typeof v !== 'function' && !(typeof v === 'string' && s === 'length'))) return undefined;
        v = v[s];
    }
    return v;
}
function idbInjectable(v, path) {
    const segs = path.split('.');
    for (let i = 0; i < segs.length - 1; i++) { if (v === null || typeof v !== 'object') return false; v = v[segs[i]]; if (v === undefined) return true; }
    return v !== null && typeof v === 'object';
}
function idbInject(v, path, key) {
    const segs = path.split('.');
    for (let i = 0; i < segs.length - 1; i++) { if (v[segs[i]] === undefined) v[segs[i]] = {}; v = v[segs[i]]; }
    v[segs[segs.length - 1]] = idbCopyKey(key);
}
const idbCopyPath = p => Array.isArray(p) ? p.slice() : p;
/* sorted lists: store records { k, v } by key; index entries { k, pk, r } by index key then primary key */
function idbBound(list, lo, hi, past) { while (lo < hi) { const m = (lo + hi) >> 1; if (past(list[m])) hi = m; else lo = m + 1; } return lo; }
function idbSpan(list, range) {
    let lo = 0, hi = list.length;
    if (range && range._lo !== undefined) { const k = range._lo, open = range.lowerOpen; lo = idbBound(list, 0, hi, e => { const c = idbCmp(e.k, k); return c > 0 || (c === 0 && !open); }); }
    if (range && range._hi !== undefined) { const k = range._hi, open = range.upperOpen; hi = idbBound(list, lo, hi, e => { const c = idbCmp(e.k, k); return c > 0 || (c === 0 && open); }); }
    return [lo, Math.max(lo, hi)];
}
function idbIndexKeys(ix, v) {
    const raw = idbEval(v, ix.keyPath);
    if (raw === undefined) return [];
    if (ix.multi && Array.isArray(raw)) {
        const ks = [];
        for (const x of raw) { const k = idbKey(x); if (k !== undefined && !ks.some(y => idbCmp(y, k) === 0)) ks.push(k); }
        return ks;
    }
    const k = idbKey(raw);
    return k === undefined ? [] : [k];
}
function idbEntries(s, ix) {
    if (ix.cver === s.ver) return ix.cache;
    const out = [];
    for (const r of s.recs) for (const k of idbIndexKeys(ix, r.v)) out.push({ k, pk: r.k, r });
    out.sort((a, b) => idbCmp(a.k, b.k) || idbCmp(a.pk, b.pk));
    ix.cache = out; ix.cver = s.ver;
    return out;
}
const idbUniqueClash = (s, ix, v, pk) => {
    const es = idbEntries(s, ix);
    for (const k of idbIndexKeys(ix, v)) {
        const i = idbBound(es, 0, es.length, e => idbCmp(e.k, k) >= 0);
        for (let j = i; j < es.length && idbCmp(es[j].k, k) === 0; j++) if (idbCmp(es[j].pk, pk) !== 0) return true;
    }
    return false;
};
const idbRange = q => q === undefined || q === null ? null : q instanceof IDBKeyRange ? q : IDBKeyRange.only(q);
/* interfaces without a constructor: only the implementation makes them */
let idbMaking = 0;
const idbNew = (C, ...a) => { idbMaking++; try { return new C(...a); } finally { idbMaking--; } };
/* getAll/getAllKeys/getAllRecords arguments: a key or range and a count, or an IDBGetAllOptions dictionary */
const idbAllArgs = (q, c) => {
    if (q && typeof q === 'object' && !(q instanceof IDBKeyRange) && !Array.isArray(q) && !(q instanceof Date) && !(q instanceof ArrayBuffer) && !ArrayBuffer.isView(q)) {
        const dir = q.direction === undefined ? 'next' : String(q.direction);
        if (!IDB_DIRS.includes(dir)) throw new TypeError(`The provided value '${dir}' is not a valid enum value of type IDBCursorDirection.`);
        return { r: idbRange(q.query), n: idbCount(q.count), dir };
    }
    return { r: idbRange(q), n: idbCount(c), dir: 'next' };
};
/* the entries of list[lo, hi) a getAll* call returns, in its direction; unique keeps each key's first entry */
const idbPick = (list, lo, hi, a, keyOf) => {
    let es = list.slice(lo, hi);
    if (a.dir.endsWith('unique')) es = es.filter((e, i) => i === 0 || idbCmp(keyOf(es[i - 1]), keyOf(e)) !== 0);
    if (a.dir.startsWith('prev')) es.reverse();
    return a.n ? es.slice(0, a.n) : es;
};
class IDBRecord {
    constructor() { throw new TypeError('Illegal constructor'); }
    get key() { return this._k; } get primaryKey() { return this._pk; } get value() { return this._v; }
}
const idbRecord = (k, pk, v) => { const r = Object.create(IDBRecord.prototype); def(r, '_k', k); def(r, '_pk', pk); def(r, '_v', v); return r; };
const idbCount = c => { if (c === undefined) return 0; c = Number(c); if (!(c >= 0) || c > 4294967295) throw new TypeError('count must be an unsigned long'); return Math.floor(c); };

class DOMStringList {
    constructor(a) { if (!idbMaking) throw new TypeError('Illegal constructor'); def(this, '_a', a); for (let i = 0; i < a.length; i++) this[i] = a[i]; }
    get length() { return this._a.length; }
    item(i) { return this._a[i] ?? null; }
    contains(s) { return this._a.includes(String(s)); }
    [Symbol.iterator]() { return this._a[Symbol.iterator](); }
}
class IDBKeyRange {
    constructor(lo, hi, lo_open, hi_open) { if (!idbMaking) throw new TypeError('Illegal constructor'); def(this, '_lo', lo); def(this, '_hi', hi); this.lowerOpen = !!lo_open; this.upperOpen = !!hi_open; }
    get lower() { return this._lo === undefined ? undefined : idbCopyKey(this._lo); }
    get upper() { return this._hi === undefined ? undefined : idbCopyKey(this._hi); }
    includes(key) {
        const k = idbMustKey(key);
        if (this._lo !== undefined) { const c = idbCmp(k, this._lo); if (c < 0 || (c === 0 && this.lowerOpen)) return false; }
        if (this._hi !== undefined) { const c = idbCmp(k, this._hi); if (c > 0 || (c === 0 && this.upperOpen)) return false; }
        return true;
    }
    static only(v) { const k = idbMustKey(v); return idbNew(IDBKeyRange, k, k, false, false); }
    static lowerBound(v, open) { return idbNew(IDBKeyRange, idbMustKey(v), undefined, open, true); }
    static upperBound(v, open) { return idbNew(IDBKeyRange, undefined, idbMustKey(v), true, open); }
    static bound(a, b, ao, bo) {
        const lo = idbMustKey(a), hi = idbMustKey(b), c = idbCmp(lo, hi);
        if (c > 0 || (c === 0 && (ao || bo))) throw idbErr('DataError', 'The lower key is greater than the upper key.');
        return idbNew(IDBKeyRange, lo, hi, ao, bo);
    }
}
class IDBVersionChangeEvent extends Event {
    constructor(t, i) { super(t, i); i = i || {}; this.oldVersion = i.oldVersion || 0; this.newVersion = i.newVersion === undefined ? null : i.newVersion; }
}
class IDBRequest extends EventTarget {
    constructor() { if (!idbMaking) throw new TypeError('Illegal constructor'); super(); def(this, '_done', false); def(this, '_r', undefined); def(this, '_e', null); this.source = null; this.transaction = null; this.onsuccess = null; this.onerror = null; }
    get result() { if (!this._done) throw idbErr('InvalidStateError', 'The request has not finished.'); return this._r; }
    get error() { if (!this._done) throw idbErr('InvalidStateError', 'The request has not finished.'); return this._e; }
    get readyState() { return this._done ? 'done' : 'pending'; }
}
class IDBOpenDBRequest extends IDBRequest {
    constructor() { super(); this.onblocked = null; this.onupgradeneeded = null; }
}
const idbSucceed = (r, v, ev) => { r._done = true; r._r = v; r._e = null; dispatch(r, ev || new Event('success'), true); };
const idbFail = (r, e) => { r._done = true; r._r = undefined; r._e = e; const ev = new Event('error', { bubbles: true, cancelable: true }); dispatch(r, ev, true); return ev; };

class IDBTransaction extends EventTarget {
    constructor(db, names, mode, durability) { if (!idbMaking) throw new TypeError('Illegal constructor'); 
        super();
        this.db = db; this.mode = mode; this.durability = durability || 'default'; this.error = null;
        this.onabort = null; this.oncomplete = null; this.onerror = null;
        def(this, '__evp', db); def(this, '_names', names); def(this, '_q', []); def(this, '_state', 'active'); def(this, '_ok', false);
        def(this, '_busy', false); def(this, '_timer', 0); def(this, '_snap', new SMap()); def(this, '_stores', new SMap()); def(this, '_vsnap', null); def(this, '_onfinish', null);
        db._d.txs.push(this);
        this._wait();
    }
    get objectStoreNames() { return this.mode === 'versionchange' ? this.db.objectStoreNames : idbNew(DOMStringList, this._names); }
    objectStore(name) {
        if (this._state === 'finished') throw idbErr('InvalidStateError', 'The transaction has finished.');
        name = String(name);
        const s = this.db._d.stores.get(name);
        if (!s || (this.mode !== 'versionchange' && !this._names.includes(name))) throw idbErr('NotFoundError', 'No objectStore named ' + name + ' in this transaction.');
        let o = this._stores.get(s);
        if (!o) { o = idbNew(IDBObjectStore, this, s); this._stores.set(s, o); }
        return o;
    }
    abort() { if (this._state === 'finished') throw idbErr('InvalidStateError', 'The transaction has finished.'); this._abort(null); }
    commit() { if (this._state !== 'active') throw idbErr('InvalidStateError', 'The transaction is not active.'); this._state = 'committing'; this._wait(); }
    _req(source, op, r) {
        if (this._state !== 'active') throw idbErr('TransactionInactiveError', 'The transaction is not active.');
        if (!r) { r = idbNew(IDBRequest); r.source = source; r.transaction = this; def(r, '__evp', this); }
        r._done = false;
        this._q.push([r, op]); this._kick();
        return r;
    }
    _started() {
        if (this._ok) return true;
        for (const t of this.db._d.txs) {
            if (t === this) break;
            if (t._state === 'finished') continue;
            if (t.mode === 'readonly' && this.mode === 'readonly') continue;
            if (t.mode === 'versionchange' || this.mode === 'versionchange' || t._names.some(n => this._names.includes(n))) return false;
        }
        return (this._ok = true);
    }
    _kick() {
        if (this._busy || !this._q.length || this._state === 'finished' || !this._started()) return;
        this._busy = true;
        Promise.resolve().then(() => this._step());
    }
    _step() {
        this._busy = false;
        if (this._state === 'finished') return;
        const it = this._q.shift();
        if (!it) { this._wait(); return; }
        const [r, op] = it;
        let v, err = null;
        try { v = op(); } catch (e) { err = e instanceof DOMException ? e : idbErr('UnknownError', String(e && e.message || e)); }
        if (err) { const ev = idbFail(r, err); if (!ev.defaultPrevented && this._state !== 'finished') this._abort(err); }
        else idbSucceed(r, v);
        if (this._q.length) this._kick(); else this._wait();
    }
    _wait() {   /* commit once a whole task passes with nothing queued */
        if (this._timer || this._state === 'finished') return;
        this._timer = setTimeout(() => {
            this._timer = 0;
            if (this._state === 'finished') return;
            if (this._q.length || this._busy || !this._started()) { this._kick(); return; }
            this._state = 'finished';
            idbTxDone(this);
            dispatch(this, new Event('complete'), true);
            if (this._onfinish) this._onfinish(false);
        });
    }
    _touch(s) { if (!this._snap.has(s)) this._snap.set(s, { recs: s.recs.slice(), gen: s.gen }); }
    _abort(err) {
        if (this._state === 'finished') return;
        this._state = 'finished'; this.error = err;
        for (const [s, x] of this._snap) { s.recs = x.recs; s.gen = x.gen; s.ver++; }
        const v = this._vsnap, d = this.db._d;
        if (v) {
            for (const s of d.stores.values()) if (!v.stores.has(s.name) || v.stores.get(s.name) !== s) s.deleted = true;
            d.version = v.version; d.stores = v.stores;
            for (const [s, x] of v.idx) { s.indexes = x.indexes; s.name = x.name; s.deleted = false; s.ver++; }
        }
        const q = this._q.splice(0);
        if (this._timer) { clearTimeout(this._timer); this._timer = 0; }
        idbTxDone(this);
        setTimeout(() => {
            for (const [r] of q) idbFail(r, idbErr('AbortError', 'The transaction was aborted.'));
            dispatch(this, new Event('abort', { bubbles: true }), true);
            if (this._onfinish) this._onfinish(true);
        });
    }
}
function idbTxDone(tx) {
    const d = tx.db._d, i = d.txs.indexOf(tx);
    if (i >= 0) d.txs.splice(i, 1);
    for (const t of d.txs.slice()) { t._kick(); if (t._q.length === 0) t._wait(); }
}

class IDBObjectStore {
    constructor(tx, s) { if (!idbMaking) throw new TypeError('Illegal constructor'); def(this, '_t', tx); def(this, '_s', s); def(this, '_ix', new SMap()); }
    get name() { return this._s.name; }
    set name(v) {
        v = String(v); const t = this._t, d = t.db._d, s = this._s;
        if (t.mode !== 'versionchange' || t._state !== 'active') throw idbErr('InvalidStateError', 'Stores are renamed only in a version change transaction.');
        if (v === s.name) return;
        if (d.stores.has(v)) throw idbErr('ConstraintError', 'An object store named ' + v + ' already exists.');
        d.stores.delete(s.name); s.name = v; d.stores.set(v, s);
    }
    get keyPath() { return idbCopyPath(this._s.keyPath); }
    get autoIncrement() { return this._s.auto; }
    get indexNames() { return idbNew(DOMStringList, [...this._s.indexes.keys()].sort()); }
    get transaction() { return this._t; }
    _check(write) {
        const t = this._t;
        if (this._s.deleted) throw idbErr('InvalidStateError', 'The object store has been deleted.');
        if (t._state !== 'active') throw idbErr('TransactionInactiveError', 'The transaction is not active.');
        if (write && t.mode === 'readonly') throw idbErr('ReadOnlyError', 'The transaction is read-only.');
    }
    put(value, key) { return this._put(value, key, false); }
    add(value, key) { return this._put(value, key, true); }
    _put(value, key, add, cursorKey, src) {
        this._check(true);
        const s = this._s, v = structuredClone(value);
        let k;
        if (s.keyPath !== null) {
            if (key !== undefined) throw idbErr('DataError', 'An object store with in-line keys cannot take a key parameter.');
            const raw = idbEval(v, s.keyPath);
            if (raw !== undefined) { k = idbKey(raw); if (k === undefined) throw idbErr('DataError', 'The key path did not yield a valid key.'); }
            else if (!s.auto) throw idbErr('DataError', 'The key path did not yield a value.');
            else if (!idbInjectable(v, s.keyPath)) throw idbErr('DataError', 'A generated key cannot be stored in this value.');
            if (cursorKey !== undefined && (k === undefined || idbCmp(k, cursorKey) !== 0)) throw idbErr('DataError', "The value's key differs from the cursor's key.");
        } else if (key !== undefined) k = idbMustKey(key);
        else if (!s.auto) throw idbErr('DataError', 'No key was given and the store has no key generator.');
        const t = this._t;
        return t._req(src || this, () => {
            t._touch(s);
            let kk = k;
            if (kk === undefined) {
                if (s.gen > 9007199254740992) throw idbErr('ConstraintError', 'The key generator is exhausted.');
                kk = s.gen++;
                if (s.keyPath !== null) idbInject(v, s.keyPath, kk);
            } else if (s.auto && typeof kk === 'number' && kk >= s.gen) s.gen = Math.floor(Math.min(kk, 9007199254740992)) + 1;
            const i = idbBound(s.recs, 0, s.recs.length, e => idbCmp(e.k, kk) >= 0), have = i < s.recs.length && idbCmp(s.recs[i].k, kk) === 0;
            if (have && add) throw idbErr('ConstraintError', 'A record with this key already exists.');
            for (const ix of s.indexes.values()) if (ix.unique && idbUniqueClash(s, ix, v, kk)) throw idbErr('ConstraintError', 'Unique index ' + ix.name + ' already has this key.');
            if (have) s.recs[i] = { k: kk, v }; else s.recs.splice(i, 0, { k: kk, v });
            s.ver++;
            return idbCopyKey(kk);
        });
    }
    _first(q) { const [lo, hi] = idbSpan(this._s.recs, idbRange(q)); return lo < hi ? this._s.recs[lo] : null; }
    get(q) { this._check(); if (q === undefined) throw idbErr('DataError', 'No key or key range was given.'); const r = idbRange(q); return this._t._req(this, () => { const e = this._first(r); return e ? structuredClone(e.v) : undefined; }); }
    getKey(q) { this._check(); if (q === undefined) throw idbErr('DataError', 'No key or key range was given.'); const r = idbRange(q); return this._t._req(this, () => { const e = this._first(r); return e ? idbCopyKey(e.k) : undefined; }); }
    getAll(q, c) { this._check(); const a = idbAllArgs(q, c); return this._t._req(this, () => { const [lo, hi] = idbSpan(this._s.recs, a.r); return idbPick(this._s.recs, lo, hi, a, e => e.k).map(e => structuredClone(e.v)); }); }
    getAllKeys(q, c) { this._check(); const a = idbAllArgs(q, c); return this._t._req(this, () => { const [lo, hi] = idbSpan(this._s.recs, a.r); return idbPick(this._s.recs, lo, hi, a, e => e.k).map(e => idbCopyKey(e.k)); }); }
    getAllRecords(o = {}) { this._check(); const a = idbAllArgs(o && typeof o === 'object' ? o : {}); return this._t._req(this, () => { const [lo, hi] = idbSpan(this._s.recs, a.r); return idbPick(this._s.recs, lo, hi, a, e => e.k).map(e => idbRecord(idbCopyKey(e.k), idbCopyKey(e.k), structuredClone(e.v))); }); }
    count(q) { this._check(); const r = idbRange(q); return this._t._req(this, () => { const [lo, hi] = idbSpan(this._s.recs, r); return hi - lo; }); }
    delete(q) {
        this._check(true);
        if (q === undefined) throw idbErr('DataError', 'No key or key range was given.');
        const r = idbRange(q), s = this._s, t = this._t;
        return t._req(this, () => { const [lo, hi] = idbSpan(s.recs, r); if (hi > lo) { t._touch(s); s.recs.splice(lo, hi - lo); s.ver++; } return undefined; });
    }
    clear() { this._check(true); const s = this._s, t = this._t; return t._req(this, () => { t._touch(s); s.recs = []; s.ver++; return undefined; }); }
    openCursor(q, dir) { this._check(); return idbOpenCursor(this, null, idbRange(q), dir, false); }
    openKeyCursor(q, dir) { this._check(); return idbOpenCursor(this, null, idbRange(q), dir, true); }
    index(name) {
        name = String(name);
        if (this._s.deleted || this._t._state === 'finished') throw idbErr('InvalidStateError', 'The object store or transaction is gone.');
        const ix = this._s.indexes.get(name);
        if (!ix) throw idbErr('NotFoundError', 'No index named ' + name + '.');
        let o = this._ix.get(ix);
        if (!o) { o = idbNew(IDBIndex, this, ix); this._ix.set(ix, o); }
        return o;
    }
    createIndex(name, keyPath, o) {
        const t = this._t, s = this._s; name = String(name); o = o || {};
        if (t.mode !== 'versionchange' || s.deleted) throw idbErr('InvalidStateError', 'Indexes are created only in a version change transaction.');
        if (t._state !== 'active') throw idbErr('TransactionInactiveError', 'The transaction is not active.');
        if (s.indexes.has(name)) throw idbErr('ConstraintError', 'An index named ' + name + ' already exists.');
        if (!idbPathOk(keyPath)) throw idbErr('SyntaxError', 'The key path is not valid.');
        if (Array.isArray(keyPath) && o.multiEntry) throw idbErr('InvalidAccessError', 'A multiEntry index cannot have an array key path.');
        const ix = { name, keyPath: idbCopyPath(keyPath), unique: !!o.unique, multi: !!o.multiEntry, cache: null, cver: -1 };
        s.indexes.set(name, ix);
        if (ix.unique) {
            const es = idbEntries(s, ix);
            for (let i = 1; i < es.length; i++) if (idbCmp(es[i - 1].k, es[i].k) === 0) { setTimeout(() => t._abort(idbErr('ConstraintError', 'Existing records break the unique index ' + name + '.'))); break; }
        }
        return this.index(name);
    }
    deleteIndex(name) {
        const t = this._t, s = this._s; name = String(name);
        if (t.mode !== 'versionchange' || s.deleted) throw idbErr('InvalidStateError', 'Indexes are deleted only in a version change transaction.');
        if (t._state !== 'active') throw idbErr('TransactionInactiveError', 'The transaction is not active.');
        const ix = s.indexes.get(name);
        if (!ix) throw idbErr('NotFoundError', 'No index named ' + name + '.');
        s.indexes.delete(name); this._ix.delete(ix);
    }
}
class IDBIndex {
    constructor(store, ix) { if (!idbMaking) throw new TypeError('Illegal constructor'); def(this, '_o', store); def(this, '_x', ix); }
    get name() { return this._x.name; }
    set name(v) {
        v = String(v); const s = this._o._s, t = this._o._t;
        if (t.mode !== 'versionchange' || t._state !== 'active') throw idbErr('InvalidStateError', 'Indexes are renamed only in a version change transaction.');
        if (v === this._x.name) return;
        if (s.indexes.has(v)) throw idbErr('ConstraintError', 'An index named ' + v + ' already exists.');
        s.indexes.delete(this._x.name); this._x.name = v; s.indexes.set(v, this._x);
    }
    get objectStore() { return this._o; }
    get keyPath() { return idbCopyPath(this._x.keyPath); }
    get unique() { return this._x.unique; }
    get multiEntry() { return this._x.multi; }
    _check() { if (this._o._s.indexes.get(this._x.name) !== this._x) throw idbErr('InvalidStateError', 'The index has been deleted.'); this._o._check(); }
    _span(r) { const es = idbEntries(this._o._s, this._x), [lo, hi] = idbSpan(es, r); return [es, lo, hi]; }
    get(q) { this._check(); if (q === undefined) throw idbErr('DataError', 'No key or key range was given.'); const r = idbRange(q); return this._o._t._req(this, () => { const [es, lo, hi] = this._span(r); return lo < hi ? structuredClone(es[lo].r.v) : undefined; }); }
    getKey(q) { this._check(); if (q === undefined) throw idbErr('DataError', 'No key or key range was given.'); const r = idbRange(q); return this._o._t._req(this, () => { const [es, lo, hi] = this._span(r); return lo < hi ? idbCopyKey(es[lo].pk) : undefined; }); }
    getAll(q, c) { this._check(); const a = idbAllArgs(q, c); return this._o._t._req(this, () => { const [es, lo, hi] = this._span(a.r); return idbPick(es, lo, hi, a, e => e.k).map(e => structuredClone(e.r.v)); }); }
    getAllKeys(q, c) { this._check(); const a = idbAllArgs(q, c); return this._o._t._req(this, () => { const [es, lo, hi] = this._span(a.r); return idbPick(es, lo, hi, a, e => e.k).map(e => idbCopyKey(e.pk)); }); }
    getAllRecords(o = {}) { this._check(); const a = idbAllArgs(o && typeof o === 'object' ? o : {}); return this._o._t._req(this, () => { const [es, lo, hi] = this._span(a.r); return idbPick(es, lo, hi, a, e => e.k).map(e => idbRecord(idbCopyKey(e.k), idbCopyKey(e.pk), structuredClone(e.r.v))); }); }
    count(q) { this._check(); const r = idbRange(q); return this._o._t._req(this, () => { const [, lo, hi] = this._span(r); return hi - lo; }); }
    openCursor(q, dir) { this._check(); return idbOpenCursor(this._o, this, idbRange(q), dir, false); }
    openKeyCursor(q, dir) { this._check(); return idbOpenCursor(this._o, this, idbRange(q), dir, true); }
}

/* cursors: each step re-finds its place from the last (key, primary key), so writes in between are seen */
const IDB_DIRS = ['next', 'nextunique', 'prev', 'prevunique'];
function idbOpenCursor(store, index, range, dir, keyOnly) {
    dir = dir === undefined ? 'next' : String(dir);
    if (!IDB_DIRS.includes(dir)) throw new TypeError('Invalid cursor direction: ' + dir);
    const c = keyOnly ? idbNew(IDBCursor, store, index, range, dir) : idbNew(IDBCursorWithValue, store, index, range, dir);
    c._r = store._t._req(index || store, () => c._advance(1));
    return c._r;
}
class IDBCursor {
    constructor(store, index, range, dir) { if (!idbMaking) throw new TypeError('Illegal constructor'); 
        def(this, '_o', store); def(this, '_i', index); def(this, '_range', range); def(this, '_dir', dir); def(this, '_r', null);
        def(this, '_k', undefined); def(this, '_pk', undefined); def(this, '_v', undefined); def(this, '_got', false); def(this, '_tk', undefined); def(this, '_tpk', undefined);
    }
    get source() { return this._i || this._o; }
    get direction() { return this._dir; }
    get key() { return this._k === undefined ? undefined : idbCopyKey(this._k); }
    get primaryKey() { return this._pk === undefined ? undefined : idbCopyKey(this._pk); }
    get request() { return this._r; }
    _list() { return this._i ? idbEntries(this._o._s, this._i._x) : this._o._s.recs; }
    _advance(n) {
        const list = this._list(), [lo, hi] = idbSpan(list, this._range), ix = !!this._i, uniq = this._dir.endsWith('unique'), fwd = this._dir.startsWith('next');
        const pkOf = e => ix ? e.pk : e.k;
        let e = null;
        for (let step = 0; step < n; step++) {
            const ck = this._k, cpk = this._pk, tk = step === 0 ? this._tk : undefined, tpk = step === 0 ? this._tpk : undefined;
            let at;
            if (fwd) {
                at = ck === undefined ? lo : idbBound(list, lo, hi, x => { const c = idbCmp(x.k, ck); return c > 0 || (c === 0 && !uniq && ix && idbCmp(x.pk, cpk) > 0); });
                if (tk !== undefined) at = Math.max(at, idbBound(list, lo, hi, x => { const c = idbCmp(x.k, tk); return c > 0 || (c === 0 && (tpk === undefined || idbCmp(x.pk, tpk) >= 0)); }));
                e = at < hi ? list[at] : null;
            } else {
                at = ck === undefined ? hi : idbBound(list, lo, hi, x => { const c = idbCmp(x.k, ck); return c > 0 || (c === 0 && (uniq || !ix || idbCmp(x.pk, cpk) >= 0)); });
                if (tk !== undefined) at = Math.min(at, idbBound(list, lo, hi, x => { const c = idbCmp(x.k, tk); return c > 0 || (c === 0 && tpk !== undefined && idbCmp(x.pk, tpk) > 0); }));
                e = at > lo ? list[at - 1] : null;
                if (e && uniq) { const k = e.k; e = list[idbBound(list, lo, at, x => idbCmp(x.k, k) >= 0)]; }
            }
            if (!e) break;
            this._k = e.k; this._pk = pkOf(e);
        }
        this._tk = this._tpk = undefined;
        if (!e) { this._k = this._pk = this._v = undefined; this._got = false; return null; }
        if (this instanceof IDBCursorWithValue) this._v = structuredClone(ix ? e.r.v : e.v);
        this._got = true;
        return this;
    }
    _again(n) {
        const t = this._o._t;
        if (t._state !== 'active') throw idbErr('TransactionInactiveError', 'The transaction is not active.');
        if (this._o._s.deleted || (this._i && this._o._s.indexes.get(this._i._x.name) !== this._i._x)) throw idbErr('InvalidStateError', 'The cursor source has been deleted.');
        if (!this._got) throw idbErr('InvalidStateError', 'The cursor is being iterated or has iterated past its end.');
        this._got = false;
        t._req(this.source, () => this._advance(n), this._r);
    }
    advance(n) { n = Number(n); if (!(n >= 1) || n > 4294967295) throw new TypeError('advance count must be a positive integer'); this._again(Math.floor(n)); }
    continue(key) {
        if (key !== undefined) {
            const k = idbMustKey(key), c = this._k === undefined ? 1 : idbCmp(k, this._k);
            if (this._dir.startsWith('next') ? c <= 0 : c >= 0) throw idbErr('DataError', 'The key is not past the cursor position.');
            this._tk = k;
        }
        this._again(1);
    }
    continuePrimaryKey(key, pk) {
        if (!this._i) throw idbErr('InvalidAccessError', 'continuePrimaryKey needs an index cursor.');
        if (this._dir.endsWith('unique')) throw idbErr('InvalidAccessError', 'continuePrimaryKey needs a non-unique direction.');
        const k = idbMustKey(key), p = idbMustKey(pk), c = idbCmp(k, this._k), fwd = this._dir === 'next';
        if (fwd ? c < 0 || (c === 0 && idbCmp(p, this._pk) <= 0) : c > 0 || (c === 0 && idbCmp(p, this._pk) >= 0)) throw idbErr('DataError', 'The key is not past the cursor position.');
        this._tk = k; this._tpk = p;
        this._again(1);
    }
    update(value) {
        if (!(this instanceof IDBCursorWithValue)) throw idbErr('InvalidStateError', 'A key cursor cannot update.');
        if (!this._got) throw idbErr('InvalidStateError', 'The cursor is not on a record.');
        const s = this._o._s;
        return this._o._put(value, s.keyPath === null ? this._pk : undefined, false, s.keyPath === null ? undefined : this._pk, this);
    }
    delete() {
        if (!(this instanceof IDBCursorWithValue)) throw idbErr('InvalidStateError', 'A key cursor cannot delete.');
        if (!this._got) throw idbErr('InvalidStateError', 'The cursor is not on a record.');
        this._o._check(true);
        const s = this._o._s, t = this._o._t, pk = this._pk;
        return t._req(this, () => {
            const i = idbBound(s.recs, 0, s.recs.length, e => idbCmp(e.k, pk) >= 0);
            if (i < s.recs.length && idbCmp(s.recs[i].k, pk) === 0) { t._touch(s); s.recs.splice(i, 1); s.ver++; }
            return undefined;
        });
    }
}
class IDBCursorWithValue extends IDBCursor { get value() { return this._v; } }

class IDBDatabase extends EventTarget {
    constructor(d) { if (!idbMaking) throw new TypeError('Illegal constructor'); super(); def(this, '_d', d); def(this, '_closing', false); def(this, '_vtx', null); this.onabort = null; this.onclose = null; this.onerror = null; this.onversionchange = null; }
    get name() { return this._d.name; }
    get version() { return this._d.version; }
    get objectStoreNames() { return idbNew(DOMStringList, [...this._d.stores.keys()].sort()); }
    _vt() {
        const t = this._vtx;
        if (!t || t._state === 'finished') throw idbErr('InvalidStateError', 'Stores change only in a version change transaction.');
        if (t._state !== 'active') throw idbErr('TransactionInactiveError', 'The transaction is not active.');
        return t;
    }
    createObjectStore(name, o) {
        const t = this._vt(); name = String(name); o = o || {};
        const kp = o.keyPath === undefined ? null : o.keyPath;
        if (kp !== null && !idbPathOk(kp)) throw idbErr('SyntaxError', 'The key path is not valid.');
        if (this._d.stores.has(name)) throw idbErr('ConstraintError', 'An object store named ' + name + ' already exists.');
        if (o.autoIncrement && (kp === '' || Array.isArray(kp))) throw idbErr('InvalidAccessError', 'An auto-increment store needs a non-empty string key path or none.');
        this._d.stores.set(name, { name, keyPath: idbCopyPath(kp), auto: !!o.autoIncrement, gen: 1, recs: [], ver: 0, indexes: new SMap(), deleted: false });
        return t.objectStore(name);
    }
    deleteObjectStore(name) {
        const t = this._vt(); name = String(name);
        const s = this._d.stores.get(name);
        if (!s) throw idbErr('NotFoundError', 'No object store named ' + name + '.');
        this._d.stores.delete(name); s.deleted = true; t._stores.delete(s);
    }
    transaction(names, mode, opts) {
        if (this._closing) throw idbErr('InvalidStateError', 'The database connection is closing.');
        if (this._vtx && this._vtx._state !== 'finished') throw idbErr('InvalidStateError', 'A version change transaction is running.');
        names = typeof names === 'string' ? [names] : [...names].map(String);
        names = [...new SSet(names)].sort();
        if (!names.length) throw idbErr('InvalidAccessError', 'The scope is empty.');
        for (const n of names) if (!this._d.stores.has(n)) throw idbErr('NotFoundError', 'No object store named ' + n + '.');
        mode = mode === undefined ? 'readonly' : String(mode);
        if (mode !== 'readonly' && mode !== 'readwrite') throw new TypeError('Invalid transaction mode: ' + mode);
        return idbNew(IDBTransaction, this, names, mode, opts && opts.durability);
    }
    close() { if (this._closing) return; this._closing = true; this._d.conns.delete(this); idbUnblock(this._d); }
}
function idbUnblock(d) { if (d.conns.size) return; const w = d.waiters.splice(0); for (const f of w) f(); }
/* opens and deletes of one name run one after another */
function idbQueue(name, fn) {
    const prev = idbOps.get(name) || Promise.resolve();
    const next = prev.then(() => new Promise(done => setTimeout(() => { try { fn(done); } catch (e) { report(e); done(); } })));
    idbOps.set(name, next);
}
const idbDB = name => { let d = idbDBs.get(name); if (!d) { d = { name, version: 0, stores: new SMap(), conns: new SSet(), txs: [], waiters: [] }; idbDBs.set(name, d); } return d; };
function idbBlock(d, req, oldV, newV, then) {   /* tell the other connections; wait for them to close */
    for (const c of [...d.conns]) if (!c._closing) dispatch(c, new IDBVersionChangeEvent('versionchange', { oldVersion: oldV, newVersion: newV }), true);
    if (!d.conns.size) { then(); return; }
    d.waiters.push(then);
    dispatch(req, new IDBVersionChangeEvent('blocked', { oldVersion: oldV, newVersion: newV }), true);
}
class IDBFactory {
    constructor() { if (!idbMaking) throw new TypeError('Illegal constructor'); }
    open(name, version) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'open' on 'IDBFactory': 1 argument required");
        name = String(name);
        if (version !== undefined) { version = Number(version); if (!(version >= 1) || version > 9007199254740991) throw new TypeError('The version must be a positive integer.'); version = Math.floor(version); }
        const req = idbNew(IDBOpenDBRequest);
        idbQueue(name, done => {
            const d = idbDB(name), old = d.version, want = version === undefined ? old || 1 : version;
            if (want < old) { idbFail(req, idbErr('VersionError', 'The requested version is less than the existing version.')); done(); return; }
            const conn = idbNew(IDBDatabase, d);
            if (want === old) { d.conns.add(conn); idbSucceed(req, conn); done(); return; }
            idbBlock(d, req, old, want, () => {
                d.conns.add(conn);
                const tx = idbNew(IDBTransaction, conn, [], 'versionchange');
                tx._vsnap = { version: old, stores: new SMap(d.stores), idx: new SMap([...d.stores.values()].map(s => [s, { indexes: new SMap(s.indexes), name: s.name }])) };
                d.version = want; conn._vtx = tx;
                tx._onfinish = aborted => {
                    conn._vtx = null; req.transaction = null;
                    if (!aborted) { idbSucceed(req, conn); done(); return; }
                    conn._closing = true; d.conns.delete(conn);
                    if (old === 0 && !d.conns.size) idbDBs.delete(name);
                    idbFail(req, idbErr('AbortError', 'The version change transaction was aborted.'));
                    done();
                };
                req.transaction = tx;
                req._done = true; req._r = conn; req._e = null;
                dispatch(req, new IDBVersionChangeEvent('upgradeneeded', { oldVersion: old, newVersion: want }), true);
            });
        });
        return req;
    }
    deleteDatabase(name) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'deleteDatabase' on 'IDBFactory': 1 argument required");
        name = String(name);
        const req = idbNew(IDBOpenDBRequest);
        idbQueue(name, done => {
            const d = idbDBs.get(name), old = d ? d.version : 0;
            const finish = () => { if (d) { for (const s of d.stores.values()) s.deleted = true; idbDBs.delete(name); } idbSucceed(req, undefined, new IDBVersionChangeEvent('success', { oldVersion: old, newVersion: null })); done(); };
            if (d) idbBlock(d, req, old, null, finish); else finish();
        });
        return req;
    }
    databases() { return Promise.resolve([...idbDBs.values()].filter(d => d.version > 0).map(d => ({ name: d.name, version: d.version }))); }
    cmp(a, b) { return idbCmp(idbMustKey(a), idbMustKey(b)); }
}
const indexedDB = idbNew(IDBFactory);
