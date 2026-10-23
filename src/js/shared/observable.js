/* Observable and Subscriber (WICG Observable, as Chrome ships it) and EventTarget#when(), shared by the page and worker
   preludes. The including prelude defines OBS_REPORT(error), which reports an exception to the global, and
   EventTarget. Needs abort.js (abortSignalRun/Add/Remove) before it. One producer serves every subscriber of an
   Observable while it is active; the last observer to unsubscribe closes it. */
const SUBSCRIBER_KEY = Symbol('Subscriber');
const obsThen = Promise.prototype.then;
const obsNeedFn = (f, what) => { if (typeof f !== 'function') throw new TypeError(`Failed to execute '${what}' on 'Observable': The callback provided as parameter 1 is not a function.`); return f; };
const obsAborted = s => !!s.aborted;
const obsIsSignal = s => s instanceof AbortSignal || (s !== null && typeof s === 'object' && Object.prototype.toString.call(s) === '[object AbortSignal]');
/* a SubscribeOptions dictionary: its signal, or null */
function obsOptions(o, what) {
    if (o === undefined || o === null) return null;
    if (typeof o !== 'object' && typeof o !== 'function') throw new TypeError(`Failed to execute '${what}' on 'Observable': The provided value is not of type 'SubscribeOptions'.`);
    const s = o.signal;
    if (s === undefined) return null;
    if (!obsIsSignal(s)) throw new TypeError(`Failed to execute '${what}' on 'Observable': Failed to read the 'signal' property from 'SubscribeOptions': Failed to convert value to 'AbortSignal'.`);
    return s;
}
/* WebIDL unsigned long long */
const obsU64 = v => { v = Number(v); if (!Number.isFinite(v)) return 0; v = Math.trunc(v); return v < 0 ? Math.max(0, 2 ** 64 + v) : v; };
/* the callbacks of a dictionary, read in WebIDL's (lexicographic) member order; each must be callable */
function obsDict(o, keys, what) {
    const out = {};
    if (o === undefined || o === null) return out;
    if (typeof o !== 'object' && typeof o !== 'function') throw new TypeError(`Failed to execute '${what}' on 'Observable': The provided value is not of type '${keys.type}'.`);
    for (const k of keys) {
        const v = o[k];
        if (v === undefined) continue;
        if (typeof v !== 'function') throw new TypeError(`Failed to execute '${what}' on 'Observable': Failed to read the '${k}' property from '${keys.type}': The provided callback is not a function.`);
        out[k] = v;
    }
    return out;
}
const OBSERVER_KEYS = Object.assign(['complete', 'error', 'next'], { type: 'SubscriptionObserver' });
const INSPECTOR_KEYS = Object.assign(['abort', 'complete', 'error', 'next', 'subscribe'], { type: 'ObservableInspector' });
const obsInvokeReport = (f, ...args) => { try { f(...args); } catch (e) { OBS_REPORT(e); } };

let subNew, subNext, subError, subComplete, subClose, subActive, subSignal, subAdd, subRemove, subEmpty;
class Subscriber {
    #observers = []; #teardowns = []; #controller = new AbortController(); #active = true;
    constructor(key = undefined) { if (key !== SUBSCRIBER_KEY) throw new TypeError("Failed to construct 'Subscriber': Illegal constructor"); }
    next(value) {
        if (!arguments.length) throw new TypeError("Failed to execute 'next' on 'Subscriber': 1 argument required, but only 0 present.");
        subNext(this, value);
    }
    error(error) {
        if (!arguments.length) throw new TypeError("Failed to execute 'error' on 'Subscriber': 1 argument required, but only 0 present.");
        subError(this, error);
    }
    complete() { subComplete(this); }
    addTeardown(teardown) {
        if (!arguments.length) throw new TypeError("Failed to execute 'addTeardown' on 'Subscriber': 1 argument required, but only 0 present.");
        if (typeof teardown !== 'function') throw new TypeError("Failed to execute 'addTeardown' on 'Subscriber': The callback provided as parameter 1 is not a function.");
        if (this.#active) this.#teardowns.push(teardown);
        else obsInvokeReport(teardown);
    }
    get active() { return this.#active; }
    get signal() { return this.#controller.signal; }
    static {
        subNew = () => new Subscriber(SUBSCRIBER_KEY);
        subActive = s => s.#active;
        subSignal = s => s.#controller.signal;
        subAdd = (s, o) => { s.#observers.push(o); };
        subRemove = (s, o) => { const i = s.#observers.indexOf(o); if (i >= 0) s.#observers.splice(i, 1); };
        subEmpty = s => !s.#observers.length;
        subNext = (s, v) => {
            if (!s.#active) return;
            for (const o of s.#observers.slice()) o.next(v);
        };
        subError = (s, e) => {
            if (!s.#active) { OBS_REPORT(e); return; }
            subClose(s, e);
            for (const o of s.#observers.slice()) o.error(e);
        };
        subComplete = s => {
            if (!s.#active) return;
            subClose(s);
            for (const o of s.#observers.slice()) o.complete();
        };
        /* "close a subscription": inactive first (so teardowns and abort handlers cannot re-enter), then the
           subscriber's signal aborts, then the teardowns run newest first */
        subClose = (s, reason) => {
            if (!s.#active) return;
            s.#active = false;
            abortSignalRun(s.#controller.signal, reason);
            const t = s.#teardowns; s.#teardowns = [];
            for (let i = t.length - 1; i >= 0; i--) obsInvokeReport(t[i]);
        };
    }
}

/* an internal observer: next/error/complete steps that never throw */
const obsObserver = (next, error, complete) => ({ next: next || (() => {}), error: error || OBS_REPORT, complete: complete || (() => {}) });

let obsSubscribe, obsNative, obsBrand;
class Observable {
    #callback = null; #native = null; #subscriber = null;
    constructor(callback) {
        if (!arguments.length) throw new TypeError("Failed to construct 'Observable': 1 argument required, but only 0 present.");
        if (typeof callback !== 'function') throw new TypeError("Failed to construct 'Observable': The callback provided as parameter 1 is not a function.");
        this.#callback = callback;
    }
    subscribe(observer = {}, options = {}) {
        let internal;
        if (typeof observer === 'function') { const f = observer; internal = obsObserver(v => obsInvokeReport(f, v)); }
        else {
            const d = obsDict(observer, OBSERVER_KEYS, 'subscribe');
            internal = obsObserver(d.next && (v => obsInvokeReport(d.next, v)), d.error && (e => obsInvokeReport(d.error, e)), d.complete && (() => obsInvokeReport(d.complete)));
        }
        obsSubscribe(this, internal, obsOptions(options, 'subscribe'));
    }
    static from(value) {
        if (!arguments.length) throw new TypeError("Failed to execute 'from' on 'Observable': 1 argument required, but only 0 present.");
        return obsFrom(value);
    }
    static {
        obsBrand = o => o !== null && typeof o === 'object' && #callback in o;
        obsNative = steps => { const o = new Observable(() => {}); o.#callback = null; o.#native = steps; return o; };
        /* "subscribe to an Observable" */
        obsSubscribe = (obs, internal, signal) => {
            let s = obs.#subscriber;
            const watch = () => {
                if (!signal) return;
                if (obsAborted(signal)) { subRemove(s, internal); if (subEmpty(s)) subClose(s, signal.reason); return; }
                abortSignalAdd(signal, () => {
                    if (!subActive(s)) return;
                    subRemove(s, internal);
                    if (subEmpty(s)) subClose(s, signal.reason);
                });
            };
            if (s && subActive(s)) {
                subAdd(s, internal);
                if (signal && obsAborted(signal)) subRemove(s, internal); else watch();
                return;
            }
            s = subNew();
            subAdd(s, internal);
            obs.#subscriber = s;
            if (signal) { if (obsAborted(signal)) subClose(s, signal.reason); else watch(); }
            if (obs.#native) { obs.#native(s); return; }
            try { obs.#callback.call(undefined, s); } catch (e) { subError(s, e); }
        };
    }
}

/* operators that make a new Observable mirroring this one through an internal observer */
const obsPipe = (source, setup) => obsNative(sub => {
    const observer = setup(sub);
    if (observer) obsSubscribe(source, observer, subSignal(sub));
});
const obsForward = (sub, next) => obsObserver(next || (v => subNext(sub, v)), e => subError(sub, e), () => subComplete(sub));

/* "convert to an Observable" */
function obsFrom(value) {
    if (value === null || (typeof value !== 'object' && typeof value !== 'function'))
        throw new TypeError("Failed to execute 'from' on 'Observable': Cannot convert value to an Observable. Input value must be an Observable, async iterable, iterable, or Promise.");
    if (obsBrand(value)) return value;
    const asyncMethod = value[Symbol.asyncIterator];
    if (asyncMethod !== undefined && asyncMethod !== null) {
        if (typeof asyncMethod !== 'function') throw new TypeError("Failed to execute 'from' on 'Observable': @@asyncIterator must be a callable.");
        return obsNative(sub => obsFromAsync(value, sub));
    }
    const iterMethod = value[Symbol.iterator];
    if (iterMethod !== undefined && iterMethod !== null) {
        if (typeof iterMethod !== 'function') throw new TypeError("Failed to execute 'from' on 'Observable': @@iterator must be a callable.");
        return obsNative(sub => obsFromIterable(value, sub));
    }
    if (obsIsPromise(value)) return obsNative(sub => {
        obsThen.call(value, v => { subNext(sub, v); subComplete(sub); }, r => subError(sub, r));
    });
    throw new TypeError("Failed to execute 'from' on 'Observable': Cannot convert value to an Observable. Input value must be an Observable, async iterable, iterable, or Promise.");
}
/* IsPromise: then() on anything without [[PromiseState]] throws before it reads anything */
function obsIsPromise(v) {
    try { obsThen.call(v, undefined, () => {}); return true; } catch (e) { return false; }
}
/* GetIterator(value, sync): the iterator and its next method */
function obsGetIterator(value) {
    const m = value[Symbol.iterator];
    if (m === undefined || m === null) throw new TypeError('The object is not iterable.');
    if (typeof m !== 'function') throw new TypeError('@@iterator is not callable.');
    const it = m.call(value);
    if (it === null || (typeof it !== 'object' && typeof it !== 'function')) throw new TypeError('Result of the Symbol.iterator method is not an object.');
    return { it, next: it.next, done: false };
}
function obsFromIterable(value, sub) {
    const signal = subSignal(sub);
    if (signal.aborted) return;
    let rec;
    try { rec = obsGetIterator(value); } catch (e) { subError(sub, e); return; }
    if (signal.aborted) return;
    /* IteratorClose: return() is called on an unfinished iterator; what it throws (or a non-object result) reaches abort() */
    abortSignalAdd(signal, () => {
        if (rec.done) return;
        rec.done = true;
        const ret = rec.it.return;
        if (ret === undefined || ret === null) return;
        const r = ret.call(rec.it);
        if (r === null || (typeof r !== 'object' && typeof r !== 'function')) throw new TypeError("The iterator's return() method must return an Object.");
    });
    for (;;) {
        let r, v;
        try {
            r = rec.next.call(rec.it);
            if (r === null || (typeof r !== 'object' && typeof r !== 'function')) throw new TypeError('Iterator result ' + String(r) + ' is not an object');
            if (r.done) { rec.done = true; subComplete(sub); return; }
            v = r.value;
        } catch (e) { rec.done = true; subError(sub, e); return; }
        subNext(sub, v);
        if (signal.aborted) return;
    }
}
/* GetIterator(value, async), falling back to the sync iterator as CreateAsyncFromSyncIterator does */
function obsGetAsyncIterator(value) {
    const m = value[Symbol.asyncIterator];
    if (m === undefined || m === null) {
        const s = obsGetIterator(value);
        const it = {
            next() {
                try {
                    const r = s.next.call(s.it);
                    if (r === null || (typeof r !== 'object' && typeof r !== 'function')) throw new TypeError('Iterator result ' + String(r) + ' is not an object');
                    const done = !!r.done;
                    return obsThen.call(Promise.resolve(r.value), v => ({ value: v, done }));
                } catch (e) { return Promise.reject(e); }
            },
            return(v) {
                const ret = s.it.return;
                if (ret === undefined || ret === null) return Promise.resolve({ value: v, done: true });
                return Promise.resolve(ret.call(s.it, v));
            },
        };
        return { it, next: it.next, done: false };
    }
    if (typeof m !== 'function') throw new TypeError('@@asyncIterator is not callable.');
    const it = m.call(value);
    if (it === null || (typeof it !== 'object' && typeof it !== 'function')) throw new TypeError('Result of the Symbol.asyncIterator method is not an object.');
    return { it, next: it.next, done: false };
}
function obsFromAsync(value, sub) {
    const signal = subSignal(sub);
    if (signal.aborted) return;
    let rec;
    try { rec = obsGetAsyncIterator(value); } catch (e) { subError(sub, e); return; }
    if (signal.aborted) return;
    /* AsyncIteratorClose with the abort reason: its failures become an unhandled rejection */
    abortSignalAdd(signal, () => {
        if (rec.done) return;
        rec.done = true;
        let p;
        try {
            const ret = rec.it.return;
            if (ret === undefined || ret === null) return;
            p = Promise.resolve(ret.call(rec.it, signal.reason));
        } catch (e) { p = Promise.reject(e); }
        obsThen.call(p, r => { if (r === null || (typeof r !== 'object' && typeof r !== 'function')) throw new TypeError("The iterator's return() method must return an Object."); });
    });
    const step = () => {
        if (signal.aborted) return;
        let p;
        try {
            const r = rec.next.call(rec.it);
            if (r === null || (typeof r !== 'object' && typeof r !== 'function')) throw new TypeError('Iterator result ' + String(r) + ' is not an object');
            p = Promise.resolve(r);
        } catch (e) { rec.done = true; p = Promise.reject(e); }
        obsThen.call(p, r => {
            if (r === null || (typeof r !== 'object' && typeof r !== 'function')) { rec.done = true; subError(sub, new TypeError('Iterator result ' + String(r) + ' is not an object')); return; }
            let done, v;
            try { done = r.done; } catch (e) { rec.done = true; subError(sub, e); return; }
            if (done) { rec.done = true; subComplete(sub); return; }
            try { v = r.value; } catch (e) { rec.done = true; subError(sub, e); return; }
            subNext(sub, v);
            step();
        }, e => { rec.done = true; subError(sub, e); });
    };
    step();
}

/* a promise-returning operator: p settles once; ctl (when made) aborts the subscription early */
function obsPromiseOp(source, options, what, withController, make) {
    const signal = obsOptions(options, what);
    let resolve, reject;
    const p = new Promise((a, b) => { resolve = a; reject = b; });
    const ctl = withController ? new AbortController() : null;
    const internal = ctl ? (signal ? AbortSignal.any([ctl.signal, signal]) : ctl.signal) : signal;
    if (internal) {
        if (obsAborted(internal)) { reject(internal.reason); return p; }
        abortSignalAdd(internal, () => reject(internal.reason));
    }
    obsSubscribe(source, make(resolve, reject, ctl), internal);
    return p;
}
const obsCall = (f, ...args) => f.call(undefined, ...args);

Object.defineProperties(Observable.prototype, Object.getOwnPropertyDescriptors({
    takeUntil(value) {
        if (!arguments.length) throw new TypeError("Failed to execute 'takeUntil' on 'Observable': 1 argument required, but only 0 present.");
        const notifier = obsFrom(value);
        return obsPipe(this, sub => {
            obsSubscribe(notifier, obsObserver(() => subComplete(sub), () => subComplete(sub)), subSignal(sub));
            if (!subActive(sub)) return null;
            return obsForward(sub);
        });
    },
    map(mapper) {
        obsNeedFn(mapper, 'map');
        return obsPipe(this, sub => {
            let idx = 0;
            return obsForward(sub, v => {
                let m;
                try { m = obsCall(mapper, v, idx); } catch (e) { subError(sub, e); return; }
                idx++;
                subNext(sub, m);
            });
        });
    },
    filter(predicate) {
        obsNeedFn(predicate, 'filter');
        return obsPipe(this, sub => {
            let idx = 0;
            return obsForward(sub, v => {
                let ok;
                try { ok = obsCall(predicate, v, idx); } catch (e) { subError(sub, e); return; }
                idx++;
                if (ok) subNext(sub, v);
            });
        });
    },
    take(amount) {
        if (!arguments.length) throw new TypeError("Failed to execute 'take' on 'Observable': 1 argument required, but only 0 present.");
        amount = obsU64(amount);
        return obsPipe(this, sub => {
            let remaining = amount;
            if (remaining === 0) { subComplete(sub); return null; }
            return obsForward(sub, v => { subNext(sub, v); if (--remaining === 0) subComplete(sub); });
        });
    },
    drop(amount) {
        if (!arguments.length) throw new TypeError("Failed to execute 'drop' on 'Observable': 1 argument required, but only 0 present.");
        amount = obsU64(amount);
        return obsPipe(this, sub => {
            let remaining = amount;
            return obsForward(sub, v => { if (remaining > 0) { remaining--; return; } subNext(sub, v); });
        });
    },
    flatMap(mapper) {
        obsNeedFn(mapper, 'flatMap');
        return obsPipe(this, sub => {
            const st = { idx: 0, outerDone: false, queue: [], active: false };
            const process = value => {
                let inner;
                try { inner = obsFrom(obsCall(mapper, value, st.idx)); } catch (e) { subError(sub, e); return; }
                st.idx++;
                obsSubscribe(inner, obsObserver(v => subNext(sub, v), e => subError(sub, e), () => {
                    if (st.queue.length) process(st.queue.shift());
                    else { st.active = false; if (st.outerDone) subComplete(sub); }
                }), subSignal(sub));
            };
            return obsObserver(v => {
                if (st.active) { st.queue.push(v); return; }
                st.active = true;
                process(v);
            }, e => subError(sub, e), () => {
                st.outerDone = true;
                if (!st.active && !st.queue.length) subComplete(sub);
            });
        });
    },
    switchMap(mapper) {
        obsNeedFn(mapper, 'switchMap');
        return obsPipe(this, sub => {
            const st = { idx: 0, outerDone: false, ctl: null };
            const process = (value, ctl) => {
                let inner;
                try { inner = obsFrom(obsCall(mapper, value, st.idx)); } catch (e) { subError(sub, e); return; }
                st.idx++;
                obsSubscribe(inner, obsObserver(v => subNext(sub, v), e => subError(sub, e), () => {
                    if (st.outerDone) subComplete(sub);
                    else if (st.ctl === ctl) st.ctl = null;
                }), AbortSignal.any([ctl.signal, subSignal(sub)]));
            };
            return obsObserver(v => {
                if (st.ctl) st.ctl.abort();
                const ctl = st.ctl = new AbortController();
                process(v, ctl);
            }, e => subError(sub, e), () => {
                st.outerDone = true;
                if (!st.ctl) subComplete(sub);
            });
        });
    },
    inspect(inspector = {}) {
        const cb = typeof inspector === 'function' ? { next: inspector } : obsDict(inspector, INSPECTOR_KEYS, 'inspect');
        return obsPipe(this, sub => {
            if (cb.subscribe) { try { obsCall(cb.subscribe); } catch (e) { subError(sub, e); return null; } }
            const signal = subSignal(sub);
            const onAbort = cb.abort ? () => obsInvokeReport(cb.abort, signal.reason) : null;
            if (onAbort) abortSignalAdd(signal, onAbort);
            const unwatch = () => { if (onAbort) abortSignalRemove(signal, onAbort); };
            return obsObserver(v => {
                if (cb.next) { try { obsCall(cb.next, v); } catch (e) { unwatch(); subError(sub, e); return; } }
                subNext(sub, v);
            }, e => {
                unwatch();
                if (cb.error) { try { obsCall(cb.error, e); } catch (e2) { subError(sub, e2); return; } }
                subError(sub, e);
            }, () => {
                unwatch();
                if (cb.complete) { try { obsCall(cb.complete); } catch (e) { subError(sub, e); return; } }
                subComplete(sub);
            });
        });
    },
    catch(callback) {
        obsNeedFn(callback, 'catch');
        return obsPipe(this, sub => obsObserver(v => subNext(sub, v), e => {
            let inner;
            try { inner = obsFrom(obsCall(callback, e)); } catch (e2) { subError(sub, e2); return; }
            obsSubscribe(inner, obsForward(sub), subSignal(sub));
        }, () => subComplete(sub)));
    },
    finally(callback) {
        obsNeedFn(callback, 'finally');
        return obsPipe(this, sub => { sub.addTeardown(callback); return obsForward(sub); });
    },
    toArray(options = {}) {
        return obsPromiseOp(this, options, 'toArray', false, (resolve, reject) => {
            const values = [];
            return obsObserver(v => { values.push(v); }, reject, () => resolve(values));
        });
    },
    forEach(callback, options = {}) {
        obsNeedFn(callback, 'forEach');
        return obsPromiseOp(this, options, 'forEach', true, (resolve, reject, ctl) => {
            let idx = 0;
            return obsObserver(v => {
                try { obsCall(callback, v, idx); } catch (e) { reject(e); ctl.abort(e); return; }
                idx++;
            }, reject, () => resolve(undefined));
        });
    },
    every(predicate, options = {}) {
        obsNeedFn(predicate, 'every');
        return obsPromiseOp(this, options, 'every', true, (resolve, reject, ctl) => {
            let idx = 0;
            return obsObserver(v => {
                let ok;
                try { ok = obsCall(predicate, v, idx); } catch (e) { reject(e); ctl.abort(e); return; }
                idx++;
                if (!ok) { resolve(false); ctl.abort(); }
            }, reject, () => resolve(true));
        });
    },
    first(options = {}) {
        return obsPromiseOp(this, options, 'first', true, (resolve, reject, ctl) =>
            obsObserver(v => { resolve(v); ctl.abort(); }, reject, () => reject(new RangeError('No values in Observable'))));
    },
    last(options = {}) {
        return obsPromiseOp(this, options, 'last', false, (resolve, reject) => {
            let has = false, last;
            return obsObserver(v => { has = true; last = v; }, reject, () => { if (has) resolve(last); else reject(new RangeError('No values in Observable')); });
        });
    },
    find(predicate, options = {}) {
        obsNeedFn(predicate, 'find');
        return obsPromiseOp(this, options, 'find', true, (resolve, reject, ctl) => {
            let idx = 0;
            return obsObserver(v => {
                let ok;
                try { ok = obsCall(predicate, v, idx); } catch (e) { reject(e); ctl.abort(e); return; }
                idx++;
                if (ok) { resolve(v); ctl.abort(); }
            }, reject, () => resolve(undefined));
        });
    },
    some(predicate, options = {}) {
        obsNeedFn(predicate, 'some');
        return obsPromiseOp(this, options, 'some', true, (resolve, reject, ctl) => {
            let idx = 0;
            return obsObserver(v => {
                let ok;
                try { ok = obsCall(predicate, v, idx); } catch (e) { reject(e); ctl.abort(e); return; }
                idx++;
                if (ok) { resolve(true); ctl.abort(); }
            }, reject, () => resolve(false));
        });
    },
    reduce(reducer, initialValue = undefined, options = {}) {
        obsNeedFn(reducer, 'reduce');
        let has = arguments.length > 1 && initialValue !== undefined, acc = initialValue;
        return obsPromiseOp(this, options, 'reduce', true, (resolve, reject, ctl) => {
            let idx = 0;
            return obsObserver(v => {
                if (!has) { has = true; acc = v; idx++; return; }
                try { acc = obsCall(reducer, acc, v, idx); } catch (e) { reject(e); ctl.abort(e); return; }
                idx++;
            }, reject, () => { if (has) resolve(acc); else reject(new TypeError('Reduce of empty Observable with no initial value')); });
        });
    },
}));
for (const k of Object.getOwnPropertyNames(Observable.prototype)) if (k !== 'constructor') Object.defineProperty(Observable.prototype, k, { enumerable: true });
for (const k of ['next', 'error', 'complete', 'addTeardown', 'active', 'signal']) Object.defineProperty(Subscriber.prototype, k, { enumerable: true });
Object.defineProperty(Observable, 'from', { enumerable: true });
for (const C of [Observable, Subscriber]) Object.defineProperty(C.prototype, Symbol.toStringTag, { value: C.name, configurable: true });

/* EventTarget#when(type, options): each subscription adds its own listener, removed when the subscription closes */
Object.defineProperty(EventTarget.prototype, 'when', {
    value: function when(type, options = {}) {
        if (!arguments.length) throw new TypeError("Failed to execute 'when' on 'EventTarget': 1 argument required, but only 0 present.");
        const target = this == null ? globalThis : this;
        type = String(type);
        let capture = false, passive;
        if (options !== undefined && options !== null) {
            if (typeof options !== 'object' && typeof options !== 'function') throw new TypeError("Failed to execute 'when' on 'EventTarget': The provided value is not of type 'ObservableEventListenerOptions'.");
            capture = !!options.capture;
            if (options.passive !== undefined) passive = !!options.passive;
        }
        return obsNative(sub => {
            const signal = subSignal(sub);
            if (signal.aborted) return;
            const o = { capture, signal };
            if (passive !== undefined) o.passive = passive;
            EventTarget.prototype.addEventListener.call(target, type, e => subNext(sub, e), o);
        });
    },
    writable: true, enumerable: true, configurable: true,
});
