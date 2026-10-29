/* Performance timeline (User Timing marks and measures, PerformanceObserver), shared by pages and workers.
   The includer defines perfNow(), perfOrigin(), perfExtra(type) (its own entries, e.g. the page's navigation entry)
   and perfTiming(name) (a PerformanceTiming attribute offset, or undefined; pages only). */
const PERF_KEY = {};
/* Internal slots must not be forgeable by setting a prototype or public field. */
const perfBrands = Object.fromEntries(['entry', 'mark', 'measure', 'performance', 'observer', 'list'].map(n => [n, new WeakSet()]));
let perfNextId = 1;
const perfNavigationId = perfHasNavigation ? Math.floor(Math.random() * 0x1fffffffffffff) + 1 : 0;
const perfString = v => { if (typeof v === 'symbol') throw new TypeError('Cannot convert a Symbol value to a string'); return String(v); };
const PERF_TYPES = Object.freeze(perfHasNavigation ? ['mark', 'measure', 'navigation'] : ['mark', 'measure']);
const perfBuf = [];
const perfObservers = new SSet();
const perfClone = v => v === undefined ? null : typeof structuredClone === 'function' ? structuredClone(v) : v;
class PerformanceEntry {
    constructor(k = undefined) { if (k !== PERF_KEY) throw new TypeError('Illegal constructor'); perfBrands.entry.add(this); }
    get id() { return this._id; } get navigationId() { return perfNavigationId; }
    get name() { return this._n; } get entryType() { return this._t; } get startTime() { return this._s; } get duration() { return this._d; }
    toJSON() { return { id: this.id, name: this.name, entryType: this.entryType, startTime: this.startTime, duration: this.duration, navigationId: this.navigationId }; }
}
const perfInit = (e, n, t, s, d) => { perfBrands.entry.add(e); if (perfBrands[t]) perfBrands[t].add(e); def(e, '_id', perfNextId++); def(e, '_n', n); def(e, '_t', t); def(e, '_s', s); def(e, '_d', d); return e; };
class PerformanceMark extends PerformanceEntry {
    constructor(name, opts = undefined) {
        if (!arguments.length) throw new TypeError("Failed to construct 'PerformanceMark': 1 argument required, but only 0 present.");
        super(PERF_KEY);
        name = perfString(name);
        if (perfTiming(name) !== undefined) throw new DOMException(`Failed to construct 'PerformanceMark': '${name}' is part of the PerformanceTiming interface, and cannot be used as a mark name.`, 'SyntaxError');
        if (opts != null && typeof opts !== 'object') throw new TypeError("Failed to construct 'PerformanceMark': The provided value is not of type 'PerformanceMarkOptions'.");
        let s = opts ? opts.startTime : undefined;
        if (s !== undefined) { s = Number(s); if (!isFinite(s)) throw new TypeError("Failed to construct 'PerformanceMark': The provided double value is non-finite."); if (s < 0) throw new TypeError("Failed to construct 'PerformanceMark': 'startTime' cannot be negative."); }
        else s = perfNow();
        perfInit(this, name, 'mark', s, 0);
        def(this, '_detail', perfClone(opts ? opts.detail : undefined));
    }
    get detail() { return this._detail; }
    toJSON() { return Object.assign(super.toJSON(), { detail: this.detail }); }
}
class PerformanceMeasure extends PerformanceEntry {
    constructor(k = undefined) { super(k); perfBrands.measure.add(this); }
    get detail() { return this._detail; }
    toJSON() { return Object.assign(super.toJSON(), { detail: this.detail }); }
}
const perfSorted = l => l.slice().sort((a, b) => a.startTime - b.startTime);
const perfAll = () => perfSorted(perfExtra(null).concat(perfBuf));
const perfByType = t => t === 'mark' || t === 'measure' ? perfBuf.filter(e => e.entryType === t) : perfExtra(t);
const perfQueue = e => {
    for (const o of perfObservers) if (o._types.has(e.entryType)) o._push([e]);
};
/* a mark name or timestamp in a measure */
const perfTime = (m, what) => {
    if (typeof m === 'number' || (m !== null && typeof m === 'object')) { const v = Number(m); if (v < 0) throw new TypeError(`Failed to execute 'measure' on 'Performance': '${what}' cannot be negative.`); return v; }
    m = String(m);
    const t = perfTiming(m);
    if (t !== undefined) { if (t === null) throw new DOMException(`Failed to execute 'measure' on 'Performance': '${m}' is empty: either the event hasn't happened yet, or it would provide cross-origin timing information.`, 'InvalidAccessError'); return t; }
    for (let i = perfBuf.length; i--;) if (perfBuf[i].entryType === 'mark' && perfBuf[i].name === m) return perfBuf[i].startTime;
    throw new DOMException(`Failed to execute 'measure' on 'Performance': The mark '${m}' does not exist.`, 'SyntaxError');
};
class Performance extends EventTarget {
    constructor(k = undefined) { if (k !== PERF_KEY) throw new TypeError('Illegal constructor'); super(); perfBrands.performance.add(this); }
    now() { return perfNow(); }
    get timeOrigin() { return perfOrigin(); }
    mark(name, opts = undefined) {
        if (!arguments.length) throw new TypeError("Failed to execute 'mark' on 'Performance': 1 argument required, but only 0 present.");
        const e = new PerformanceMark(name, opts);
        perfBuf.push(e); if (perfBuf.length > 16384) perfBuf.splice(0, 4096);
        perfQueue(e); return e;
    }
    measure(name, so = undefined, endMark = undefined) {
        if (!arguments.length) throw new TypeError("Failed to execute 'measure' on 'Performance': 1 argument required, but only 0 present.");
        name = perfString(name);
        const dict = so !== null && typeof so === 'object';
        const has = k => dict && so[k] !== undefined;
        if (dict && (has('start') || has('end') || has('duration') || has('detail'))) {
            if (endMark !== undefined) throw new TypeError("Failed to execute 'measure' on 'Performance': If a non-empty PerformanceMeasureOptions object was passed, |end_mark| must not be passed.");
            if (!has('start') && !has('end')) throw new TypeError("Failed to execute 'measure' on 'Performance': If a non-empty PerformanceMeasureOptions object was passed, at least one of its 'start' or 'end' properties must be present.");
            if (has('start') && has('end') && has('duration')) throw new TypeError("Failed to execute 'measure' on 'Performance': If a non-empty PerformanceMeasureOptions object was passed, it must not have all of its 'start', 'duration', and 'end' properties defined");
        }
        const opt = dict && (has('start') || has('end') || has('duration') || has('detail'));
        let end, start;
        if (endMark !== undefined) end = perfTime(endMark, 'end');
        else if (opt && has('end')) end = perfTime(so.end, 'end');
        else if (opt && has('start') && has('duration')) end = perfTime(so.start, 'start') + perfTime(so.duration, 'duration');
        else end = perfNow();
        if (opt && has('start')) start = perfTime(so.start, 'start');
        else if (opt && has('duration') && has('end')) start = end - perfTime(so.duration, 'duration');
        else if (!dict && so !== undefined && so !== null) start = perfTime(so, 'start');
        else start = 0;
        const e = perfInit(Object.create(PerformanceMeasure.prototype), name, 'measure', start, end - start);
        def(e, '_detail', perfClone(opt ? so.detail : undefined));
        perfBuf.push(e); if (perfBuf.length > 16384) perfBuf.splice(0, 4096);
        perfQueue(e); return e;
    }
    getEntries() { return perfAll(); }
    getEntriesByType(t) { return perfSorted(perfByType(perfString(t))); }
    getEntriesByName(n, t = undefined) { n = perfString(n); return (t === undefined ? perfAll() : perfSorted(perfByType(perfString(t)))).filter(e => e.name === n); }
    clearMarks(n = undefined) { for (let i = perfBuf.length; i--;) if (perfBuf[i].entryType === 'mark' && (n === undefined || perfBuf[i].name === perfString(n))) perfBuf.splice(i, 1); }
    clearMeasures(n = undefined) { for (let i = perfBuf.length; i--;) if (perfBuf[i].entryType === 'measure' && (n === undefined || perfBuf[i].name === perfString(n))) perfBuf.splice(i, 1); }
    clearResourceTimings() {}
    setResourceTimingBufferSize(n) {}
    toJSON() { return { timeOrigin: this.timeOrigin }; }
}
class PerformanceObserverEntryList {
    constructor(k = undefined) { if (k !== PERF_KEY) throw new TypeError('Illegal constructor'); perfBrands.list.add(this); }
    getEntries() { return perfSorted(this._l); }
    getEntriesByType(t) { t = perfString(t); return perfSorted(this._l.filter(e => e.entryType === t)); }
    getEntriesByName(n, t = undefined) { n = perfString(n); t = t === undefined ? undefined : perfString(t); return perfSorted(this._l.filter(e => e.name === n && (t === undefined || e.entryType === t))); }
}
class PerformanceObserver {
    constructor(cb) {
        if (typeof cb !== 'function') throw new TypeError("Failed to construct 'PerformanceObserver': The callback provided as parameter 1 is not a function.");
        perfBrands.observer.add(this);
        def(this, '_cb', cb); def(this, '_types', new SSet()); def(this, '_q', []); def(this, '_due', false); def(this, '_mode', ''); def(this, '_dropped', true);
    }
    observe(o = {}) {
        if (o != null && typeof o !== 'object') throw new TypeError("Failed to execute 'observe' on 'PerformanceObserver': The provided value is not of type 'PerformanceObserverInit'.");
        o = o || {};
        const et = o.entryTypes, ty = o.type;
        if (et === undefined && ty === undefined) throw new TypeError("Failed to execute 'observe' on 'PerformanceObserver': An observe() call must include either entryTypes or type arguments.");
        if (et !== undefined && (ty !== undefined || o.buffered !== undefined)) throw new TypeError("Failed to execute 'observe' on 'PerformanceObserver': An observe() call must not include both entryTypes and type arguments.");
        if (this._mode === 'multiple' && ty !== undefined) throw new DOMException("Failed to execute 'observe' on 'PerformanceObserver': You cannot call observe() with a type argument after calling observe() with an entryTypes argument.", 'InvalidModificationError');
        if (this._mode === 'single' && et !== undefined) throw new DOMException("Failed to execute 'observe' on 'PerformanceObserver': You cannot call observe() with an entryTypes argument after calling observe() with a type argument.", 'InvalidModificationError');
        if (et !== undefined) {
            if (typeof et === 'string' || et === null || typeof et !== 'object' || typeof et[Symbol.iterator] !== 'function') throw new TypeError("Failed to execute 'observe' on 'PerformanceObserver': The provided value cannot be converted to a sequence.");
            const ts = Array.from(et, perfString).filter(t => PERF_TYPES.includes(t));
            if (!ts.length) return;
            this._mode = 'multiple'; this._types.clear(); for (const t of ts) this._types.add(t);
            perfObservers.add(this);
            return;
        }
        const t = perfString(ty);
        if (!PERF_TYPES.includes(t)) return;
        this._mode = 'single'; this._types.add(t);
        perfObservers.add(this);
        if (o.buffered) this._push(perfSorted(perfByType(t)));
    }
    _push(l) {
        if (!l.length) return; this._q.push(...l);
        if (this._due) return; this._due = true;
        setTimeout(() => {
            this._due = false; if (!this._q.length || !perfObservers.has(this)) return;
            const list = new PerformanceObserverEntryList(PERF_KEY); def(list, '_l', this._q.splice(0));
            const opts = this._dropped ? { droppedEntriesCount: 0 } : undefined; this._dropped = false;
            try { this._cb.call(this, list, this, opts); } catch (e) { setTimeout(() => { throw e; }, 0); }
        }, 0);
    }
    disconnect() { perfObservers.delete(this); this._q.length = 0; this._types.clear(); this._mode = ''; }
    takeRecords() { return this._q.splice(0); }
    static get supportedEntryTypes() { return PERF_TYPES; }
}

/* WebIDL methods validate their receiver before argument conversion. Keeping this
   in the shared implementation makes worker and Window semantics identical. */
for (const [C, brand] of [[PerformanceEntry, 'entry'], [PerformanceMark, 'mark'], [PerformanceMeasure, 'measure'],
    [Performance, 'performance'], [PerformanceObserver, 'observer'], [PerformanceObserverEntryList, 'list']]) {
    Object.defineProperty(C.prototype, Symbol.toStringTag, { value: C.name, configurable: true });
    for (const key of Object.getOwnPropertyNames(C.prototype)) {
        if (key === 'constructor' || key[0] === '_') continue;
        const desc = Object.getOwnPropertyDescriptor(C.prototype, key);
        const checked = fn => {
            const f = { [key](...args) {
                if (!perfBrands[brand].has(this)) throw new TypeError('Illegal invocation');
                if (args.length < fn.length) throw new TypeError(`Failed to execute '${key}' on '${C.name}': ${fn.length} argument(s) required, but only ${args.length} present.`);
                return Reflect.apply(fn, this, args);
            } }[key];
            Object.defineProperty(f, 'name', { value: fn.name, configurable: true });
            Object.defineProperty(f, 'length', { value: fn.length, configurable: true });
            return f;
        };
        if (typeof desc.value === 'function') desc.value = checked(desc.value);
        if (desc.get) desc.get = checked(desc.get);
        if (desc.set) desc.set = checked(desc.set);
        desc.enumerable = true;
        Object.defineProperty(C.prototype, key, desc);
    }
}
