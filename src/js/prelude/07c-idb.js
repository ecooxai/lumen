// IndexedDB: in-memory per origin for the life of the process (transactions, indexes, cursors, key ranges)
(() => {
    const dbs = new Map(), conns = new Map();
    const dbKey = n => (globalThis.location ? location.origin : '') + '\u0000' + n;
    const E = (name, msg) => new DOMException(msg, name);
    const ktype = k => typeof k === 'number' ? (Number.isNaN(k) ? 0 : 1) : k instanceof Date ? (isNaN(k) ? 0 : 2) : typeof k === 'string' ? 3 :
        k instanceof ArrayBuffer || ArrayBuffer.isView(k) ? 4 : Array.isArray(k) ? 5 : 0;
    const valid = k => { const t = ktype(k); return t === 5 ? k.every(valid) : t > 0; };
    const bytes = k => k instanceof ArrayBuffer ? new Uint8Array(k) : new Uint8Array(k.buffer, k.byteOffset, k.byteLength);
    function cmp(a, b) {
        const ta = ktype(a), tb = ktype(b);
        if (ta !== tb) return ta < tb ? -1 : 1;
        if (ta === 1 || ta === 3) return a < b ? -1 : a > b ? 1 : 0;
        if (ta === 2) return cmp(+a, +b);
        if (ta === 4) { const x = bytes(a), y = bytes(b); for (let i = 0; i < Math.min(x.length, y.length); i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1; return cmp(x.length, y.length); }
        for (let i = 0; i < Math.min(a.length, b.length); i++) { const c = cmp(a[i], b[i]); if (c) return c; }
        return cmp(a.length, b.length);
    }
    const ckey = k => { if (!valid(k)) throw E('DataError', 'The parameter is not a valid key.'); return k instanceof Date ? new Date(+k) : Array.isArray(k) ? k.map(ckey) : ArrayBuffer.isView(k) ? bytes(k).slice().buffer : k; };
    function extract(v, kp) {
        if (Array.isArray(kp)) { const r = kp.map(p => extract(v, p)); return r.some(x => x === undefined) ? undefined : r; }
        if (kp === '') return v;
        let o = v; for (const p of kp.split('.')) { if (o == null || typeof o !== 'object' && typeof o !== 'string' || !(p in Object(o))) return undefined; o = o[p]; }
        return o;
    }
    function inject(v, kp, k) { const ps = kp.split('.'), last = ps.pop(); let o = v; for (const p of ps) { if (o[p] == null) o[p] = {}; o = o[p]; } o[last] = k; }
    class IDBKeyRange {
        constructor(lo, hi, lo_, hi_) { def(this, '_', 1); this.lower = lo; this.upper = hi; this.lowerOpen = !!lo_; this.upperOpen = !!hi_; }
        static only(k) { k = ckey(k); return new IDBKeyRange(k, k, false, false); }
        static lowerBound(k, o) { return new IDBKeyRange(ckey(k), undefined, o, true); }
        static upperBound(k, o) { return new IDBKeyRange(undefined, ckey(k), true, o); }
        static bound(a, b, ao, bo) { a = ckey(a); b = ckey(b); const c = cmp(a, b); if (c > 0 || (c === 0 && (ao || bo))) throw E('DataError', 'The lower key is greater than the upper key.'); return new IDBKeyRange(a, b, ao, bo); }
        includes(k) { k = ckey(k); if (this.lower !== undefined) { const c = cmp(k, this.lower); if (c < 0 || (c === 0 && this.lowerOpen)) return false; } if (this.upper !== undefined) { const c = cmp(k, this.upper); if (c > 0 || (c === 0 && this.upperOpen)) return false; } return true; }
    }
    const range = q => q == null ? null : q instanceof IDBKeyRange ? q : IDBKeyRange.only(q);
    const inR = (r, k) => !r || r.includes(k);
    const find = (recs, k) => { let lo = 0, hi = recs.length; while (lo < hi) { const m = (lo + hi) >> 1; if (cmp(recs[m][0], k) < 0) lo = m + 1; else hi = m; } return lo; };
    const clone = v => structuredClone(v);
    class DOMStringList extends Array { contains(s) { return this.includes(s); } item(i) { return this[i] ?? null; } }
    const names = o => DOMStringList.from([...o.keys()].sort());
    class IDBRequest extends EventTarget {
        constructor(src, tx) { super(); def(this, '_', { result: undefined, error: null }); this.source = src; this.transaction = tx; this.readyState = 'pending'; this.onsuccess = this.onerror = null; }
        get result() { if (this.readyState !== 'done') throw E('InvalidStateError', 'The request has not finished.'); return this._.result; }
        get error() { if (this.readyState !== 'done') throw E('InvalidStateError', 'The request has not finished.'); return this._.error; }
    }
    class IDBOpenDBRequest extends IDBRequest { constructor() { super(null, null); this.onupgradeneeded = this.onblocked = null; } }
    class IDBVersionChangeEvent extends Event { constructor(t, i = {}) { super(t, i); this.oldVersion = i.oldVersion ?? 0; this.newVersion = i.newVersion ?? null; } }
    function fire(t, type, init, ev) { const e = ev || new Event(type, Object.assign({ bubbles: false, cancelable: false }, init)); t.dispatchEvent(e); return e; }
    class IDBTransaction extends EventTarget {
        constructor(db, scope, mode, dur) {
            super(); def(this, '_', { q: [], active: true, done: false, snap: new Map(), sched: false }); this.db = db; this.mode = mode; this.durability = dur || 'default';
            this.error = null; this.oncomplete = this.onerror = this.onabort = null; def(this, '_scope', scope); this._sched();
        }
        get objectStoreNames() { return DOMStringList.from([...this._scope].sort()); }
        objectStore(n) {
            if (this._.done) throw E('InvalidStateError', 'The transaction has finished.');
            if (!this._scope.includes(n) || !this.db._.d.stores.has(n)) throw E('NotFoundError', "Failed to execute 'objectStore' on 'IDBTransaction': The specified object store was not found.");
            return new IDBObjectStore(this, n);
        }
        _sched() { if (this._.sched || this._.done) return; this._.sched = true; setTimeout(() => this._run()); }
        _req(src, fn, req) {
            if (!this._.active || this._.done) throw E('TransactionInactiveError', 'The transaction is not active.');
            const r = req || new IDBRequest(src, this); this._.q.push([r, fn]); this._sched(); return r;
        }
        _w(n) { if (this.mode === 'readonly') throw E('ReadOnlyError', 'The transaction is read-only.'); const st = this.db._.d.stores.get(n); if (!this._.snap.has(n)) this._.snap.set(n, { recs: st.recs.slice(), next: st.next }); return st; }
        _run() {
            this._.sched = false;
            while (this._.q.length && !this._.done) {
                const [r, fn] = this._.q.shift(); let ok = true, v;
                try { v = fn(); } catch (e) { ok = false; v = e instanceof DOMException ? e : E('UnknownError', String(e)); }
                r.readyState = 'done';
                if (ok) { r._.result = v; this._.active = true; fire(r, 'success'); }
                else {
                    r._.error = v; r._.result = undefined; this._.active = true;
                    const e = fire(r, 'error', { bubbles: true, cancelable: true });
                    if (!e.defaultPrevented) { this.error = v; fire(this, 'error', { bubbles: true }); this.abort(v); }
                }
            }
            if (this._.done) return;
            this._.active = false;
            setTimeout(() => { if (this._.done) return; if (this._.q.length) { this._.active = true; return this._run(); } this._.done = true; fire(this, 'complete'); if (this._onfin) this._onfin(true); });
        }
        abort(err) {
            if (this._.done) throw E('InvalidStateError', 'The transaction has finished.');
            this._.done = true; this._.active = false;
            for (const [n, s] of this._.snap) { const st = this.db._.d.stores.get(n); if (st) { st.recs = s.recs; st.next = s.next; } }
            if (this._undo) this._undo();
            for (const [r] of this._.q) { r.readyState = 'done'; r._.error = E('AbortError', 'The transaction was aborted.'); fire(r, 'error', { bubbles: true, cancelable: true }); }
            this._.q = []; if (!this.error && err) this.error = err;
            setTimeout(() => { fire(this, 'abort', { bubbles: true }); if (this._onfin) this._onfin(false); });
        }
        commit() { if (this._.done) throw E('InvalidStateError', 'The transaction has finished.'); this._.active = false; }
    }
    function scanIdx(st, ix, r) {
        const out = [];
        for (const [pk, v] of st.recs) {
            let k = extract(v, ix.keyPath); if (k === undefined) continue;
            const ks = ix.multiEntry && Array.isArray(k) ? [...new Map(k.filter(valid).map(x => [JSON.stringify(x), x])).values()] : valid(k) ? [k] : [];
            for (const x of ks) if (inR(r, x)) out.push([x, pk, v]);
        }
        return out.sort((a, b) => cmp(a[0], b[0]) || cmp(a[1], b[1]));
    }
    class IDBCursor {
        constructor(src, req, list, dir, keyOnly) { def(this, '_', { list, i: -1, req, keyOnly, got: false }); this.source = src; this.direction = dir; this.request = req; this.key = undefined; this.primaryKey = undefined; }
        _step(n, pred) {
            const L = this._.list, back = this.direction.startsWith('prev'), uniq = this.direction.endsWith('unique');
            let i = this._.i;
            while (n > 0) {
                i += back ? -1 : 1; if (i < 0 || i >= L.length) { i = -1; break; }
                if (uniq && this.key !== undefined && cmp(L[i][0], this.key) === 0) continue;
                if (pred && !pred(L[i])) continue;
                n--;
            }
            if (back && uniq && i >= 0) while (i > 0 && cmp(L[i - 1][0], L[i][0]) === 0) i--;
            this._.i = i; return i;
        }
        _set() { const e = this._.list[this._.i]; if (!e) return null; this.key = e[0]; this.primaryKey = e[1]; if (!this._.keyOnly) this.value = clone(e[2]); return this; }
        _go(n, pred) {
            const tx = this.source.transaction || this.source.objectStore.transaction;
            if (this._.got) throw E('InvalidStateError', 'The cursor is being iterated or has iterated past its end.');
            this._.got = true;
            tx._req(this.source, () => { this._.got = false; if (this._step(n, pred) < 0) { this._.got = true; return null; } return this._set(); }, this._.req);
            this._.req.readyState = 'pending';
        }
        continue(k) { if (k === undefined) return this._go(1); k = ckey(k); const back = this.direction.startsWith('prev'); this._go(1, e => back ? cmp(e[0], k) <= 0 : cmp(e[0], k) >= 0); }
        continuePrimaryKey(k, pk) { k = ckey(k); pk = ckey(pk); this._go(1, e => { const c = cmp(e[0], k); return c > 0 || (c === 0 && cmp(e[1], pk) >= 0); }); }
        advance(n) { if (!(n > 0)) throw new TypeError("Failed to execute 'advance' on 'IDBCursor': A count argument with value 0 (zero) was supplied, must be greater than 0."); this._go(n >>> 0); }
        update(v) { const os = this.source instanceof IDBIndex ? this.source.objectStore : this.source; return os.put(v, os.keyPath == null ? this.primaryKey : undefined); }
        delete() { const os = this.source instanceof IDBIndex ? this.source.objectStore : this.source; return os.delete(this.primaryKey); }
    }
    class IDBCursorWithValue extends IDBCursor {}
    function openCur(src, st, list, q, dir, keyOnly) {
        dir = dir || 'next'; if (!['next', 'nextunique', 'prev', 'prevunique'].includes(dir)) throw new TypeError("Failed to execute 'openCursor': The provided value '" + dir + "' is not a valid enum value of type IDBCursorDirection.");
        const tx = src.transaction || src.objectStore.transaction, req = new IDBRequest(src, tx);
        const c = new (keyOnly ? IDBCursor : IDBCursorWithValue)(src, req, null, dir, keyOnly);
        c._.got = true;
        tx._req(src, () => { c._.list = list(); c._.i = dir.startsWith('prev') ? c._.list.length : -1; c._.got = false; if (c._step(1) < 0) { c._.got = true; return null; } return c._set(); }, req);
        return req;
    }
    class IDBIndex {
        constructor(os, n) { const ix = os._st.idx.get(n); this.objectStore = os; this.name = n; this.keyPath = ix.keyPath; this.unique = ix.unique; this.multiEntry = ix.multiEntry; def(this, '_ix', ix); }
        _l(q) { const r = range(q); return () => scanIdx(this.objectStore._st, this._ix, r); }
        get(q) { const l = this._l(q); return this.objectStore.transaction._req(this, () => { const e = l()[0]; return e ? clone(e[2]) : undefined; }); }
        getKey(q) { const l = this._l(q); return this.objectStore.transaction._req(this, () => l()[0]?.[1]); }
        getAll(q, n) { const l = this._l(q); return this.objectStore.transaction._req(this, () => l().slice(0, n || undefined).map(e => clone(e[2]))); }
        getAllKeys(q, n) { const l = this._l(q); return this.objectStore.transaction._req(this, () => l().slice(0, n || undefined).map(e => e[1])); }
        count(q) { const l = this._l(q); return this.objectStore.transaction._req(this, () => l().length); }
        openCursor(q, d) { return openCur(this, null, this._l(q), q, d, false); }
        openKeyCursor(q, d) { return openCur(this, null, this._l(q), q, d, true); }
    }
    class IDBObjectStore {
        constructor(tx, n) { const st = tx.db._.d.stores.get(n); this.transaction = tx; this.name = n; this.keyPath = st.keyPath; this.autoIncrement = st.autoIncrement; def(this, '_st', st); }
        get indexNames() { return names(this._st.idx); }
        _l(q, recs) { const r = range(q); return () => (recs || this._st.recs).filter(e => inR(r, e[0])).map(e => [e[0], e[0], e[1]]); }
        _put(v, k, noOverwrite) {
            const tx = this.transaction; tx._w(this.name); const st = this._st;
            if (st.keyPath != null && k !== undefined) throw E('DataError', 'The object store uses in-line keys and the key parameter was provided.');
            let val = clone(v);
            if (st.keyPath != null) { k = extract(val, st.keyPath); if (k === undefined && st.autoIncrement) { k = st.next++; inject(val, st.keyPath, k); } }
            else if (k === undefined && st.autoIncrement) k = st.next++;
            if (k === undefined) throw E('DataError', 'No key or key range specified.');
            k = ckey(k);
            return tx._req(this, () => {
                const i = find(st.recs, k), exists = i < st.recs.length && cmp(st.recs[i][0], k) === 0;
                if (exists && noOverwrite) throw E('ConstraintError', 'Key already exists in the object store.');
                for (const ix of st.idx.values()) if (ix.unique) { const ik = extract(val, ix.keyPath); if (ik !== undefined && valid(ik) && scanIdx(st, ix, IDBKeyRange.only(ik)).some(e => cmp(e[1], k))) throw E('ConstraintError', 'Unable to add key to index: at least one key does not satisfy the uniqueness requirements.'); }
                if (exists) st.recs[i][1] = val; else st.recs.splice(i, 0, [k, val]);
                if (st.autoIncrement && typeof k === 'number' && k >= st.next) st.next = Math.floor(k) + 1;
                return k;
            });
        }
        put(v, k) { return this._put(v, k, false); }
        add(v, k) { return this._put(v, k, true); }
        delete(q) { this.transaction._w(this.name); const r = range(q); if (!r) throw E('DataError', 'No key or key range specified.'); return this.transaction._req(this, () => { this._st.recs = this._st.recs.filter(e => !r.includes(e[0])); return undefined; }); }
        clear() { this.transaction._w(this.name); return this.transaction._req(this, () => { this._st.recs = []; return undefined; }); }
        get(q) { const r = range(q); if (!r) throw E('DataError', 'No key or key range specified.'); return this.transaction._req(this, () => { const e = this._st.recs.find(e => r.includes(e[0])); return e ? clone(e[1]) : undefined; }); }
        getKey(q) { const r = range(q); return this.transaction._req(this, () => this._st.recs.find(e => inR(r, e[0]))?.[0]); }
        getAll(q, n) { const l = this._l(q); return this.transaction._req(this, () => l().slice(0, n || undefined).map(e => clone(e[2]))); }
        getAllKeys(q, n) { const l = this._l(q); return this.transaction._req(this, () => l().slice(0, n || undefined).map(e => e[0])); }
        count(q) { const l = this._l(q); return this.transaction._req(this, () => l().length); }
        openCursor(q, d) { const l = this._l(q); return openCur(this, this._st, l, q, d, false); }
        openKeyCursor(q, d) { const l = this._l(q); return openCur(this, this._st, l, q, d, true); }
        index(n) { if (!this._st.idx.has(n)) throw E('NotFoundError', "Failed to execute 'index' on 'IDBObjectStore': The specified index was not found."); return new IDBIndex(this, n); }
        createIndex(n, kp, o = {}) {
            if (this.transaction.mode !== 'versionchange') throw E('InvalidStateError', 'Not a version change transaction.');
            if (this._st.idx.has(n)) throw E('ConstraintError', 'An index with the specified name already exists.');
            this._st.idx.set(n, { keyPath: Array.isArray(kp) ? [...kp].map(String) : String(kp), unique: !!o.unique, multiEntry: !!o.multiEntry }); return new IDBIndex(this, n);
        }
        deleteIndex(n) { if (this.transaction.mode !== 'versionchange') throw E('InvalidStateError', 'Not a version change transaction.'); if (!this._st.idx.delete(n)) throw E('NotFoundError', 'The specified index was not found.'); }
    }
    class IDBDatabase extends EventTarget {
        constructor(d) { super(); def(this, '_', { d, closed: false, vtx: null }); this.name = d.name; this.version = d.version; this.onabort = this.onclose = this.onerror = this.onversionchange = null; }
        get objectStoreNames() { return names(this._.d.stores); }
        createObjectStore(n, o = {}) {
            const tx = this._.vtx; if (!tx || tx._.done) throw E('InvalidStateError', 'Not a version change transaction.');
            n = String(n); if (this._.d.stores.has(n)) throw E('ConstraintError', 'An object store with the specified name already exists.');
            const kp = o.keyPath == null ? null : Array.isArray(o.keyPath) ? o.keyPath.map(String) : String(o.keyPath);
            if (o.autoIncrement && (Array.isArray(kp) || kp === '')) throw E('InvalidAccessError', 'The autoIncrement option was set but the keyPath option was empty or an array.');
            this._.d.stores.set(n, { keyPath: kp, autoIncrement: !!o.autoIncrement, recs: [], next: 1, idx: new Map() }); tx._scope.push(n); return new IDBObjectStore(tx, n);
        }
        deleteObjectStore(n) { if (!this._.vtx || this._.vtx._.done) throw E('InvalidStateError', 'Not a version change transaction.'); if (!this._.d.stores.delete(n)) throw E('NotFoundError', 'The specified object store was not found.'); }
        transaction(sn, mode = 'readonly', o = {}) {
            if (this._.closed) throw E('InvalidStateError', 'The database connection is closing.');
            if (mode !== 'readonly' && mode !== 'readwrite') throw new TypeError("Failed to execute 'transaction' on 'IDBDatabase': The provided value '" + mode + "' is not a valid enum value of type IDBTransactionMode.");
            const s = typeof sn === 'string' ? [sn] : [...sn].map(String);
            if (!s.length) throw E('InvalidAccessError', 'The storeNames parameter was empty.');
            for (const n of s) if (!this._.d.stores.has(n)) throw E('NotFoundError', "Failed to execute 'transaction' on 'IDBDatabase': One of the specified object stores was not found.");
            return new IDBTransaction(this, [...new Set(s)], mode, o.durability);
        }
        close() { this._.closed = true; const c = conns.get(dbKey(this.name)); if (c) c.delete(this); }
    }
    class IDBFactory {
        open(name, version) {
            if (arguments.length < 1) throw new TypeError("Failed to execute 'open' on 'IDBFactory': 1 argument required, but only 0 present.");
            if (version !== undefined && !(version >= 1)) throw new TypeError("Failed to execute 'open' on 'IDBFactory': Value is outside the 'unsigned long long' value range.");
            name = String(name); const req = new IDBOpenDBRequest(), key = dbKey(name);
            setTimeout(() => {
                let d = dbs.get(key); const old = d ? d.version : 0, nv = version === undefined ? (old || 1) : Math.floor(version);
                req.readyState = 'done';
                if (nv < old) { req._.error = E('VersionError', 'The requested version (' + nv + ') is less than the existing version (' + old + ').'); fire(req, 'error', { bubbles: true, cancelable: true }); return; }
                if (!d) { d = { name, version: 0, stores: new Map() }; dbs.set(key, d); }
                const db = new IDBDatabase(d), set = conns.get(key) || new Set(); conns.set(key, set);
                const ok = () => { set.add(db); req._.result = db; req.transaction = null; fire(req, 'success'); };
                if (nv === old) return ok();
                for (const c of set) fire(c, 'versionchange', null, new IDBVersionChangeEvent('versionchange', { oldVersion: old, newVersion: nv }));
                const saved = new Map([...d.stores].map(([n, s]) => [n, s])), tx = new IDBTransaction(db, [...d.stores.keys()], 'versionchange');
                db._.vtx = tx; d.version = nv; db.version = nv; req._.result = db; req.transaction = tx;
                tx._undo = () => { d.stores = saved; d.version = old; db.version = old; if (!old) dbs.delete(key); };
                tx._onfin = okd => { db._.vtx = null; req.transaction = null; if (okd) ok(); else { req._.result = undefined; req._.error = E('AbortError', 'Version change transaction was aborted in upgradeneeded event handler.'); fire(req, 'error', { bubbles: true, cancelable: true }); } };
                req.readyState = 'done';
                try { fire(req, 'upgradeneeded', null, new IDBVersionChangeEvent('upgradeneeded', { oldVersion: old, newVersion: nv })); }
                catch (e) { console.error(e); }
            });
            return req;
        }
        deleteDatabase(name) {
            const req = new IDBOpenDBRequest(), key = dbKey(String(name));
            setTimeout(() => { const d = dbs.get(key), old = d ? d.version : 0; for (const c of conns.get(key) || []) fire(c, 'versionchange', null, new IDBVersionChangeEvent('versionchange', { oldVersion: old, newVersion: null }));
                dbs.delete(key); conns.delete(key); req.readyState = 'done'; fire(req, 'success', null, new IDBVersionChangeEvent('success', { oldVersion: old, newVersion: null })); });
            return req;
        }
        databases() { const p = (globalThis.location ? location.origin : '') + '\u0000'; return Promise.resolve([...dbs].filter(([k]) => k.startsWith(p)).map(([, d]) => ({ name: d.name, version: d.version }))); }
        cmp(a, b) { return cmp(ckey(a), ckey(b)); }
    }
    const indexedDB = new IDBFactory();
    Object.assign(globalThis, { indexedDB, IDBFactory, IDBDatabase, IDBObjectStore, IDBIndex, IDBKeyRange, IDBCursor, IDBCursorWithValue, IDBRequest, IDBOpenDBRequest, IDBTransaction, IDBVersionChangeEvent });
    if (!globalThis.DOMStringList) globalThis.DOMStringList = DOMStringList;
})();
