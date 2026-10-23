/* AbortController and AbortSignal per the DOM standard, shared by the page and worker preludes. The including
   prelude defines ABORT_FIRE(target, event), which dispatches a trusted event. abortSignalAdd(signal, algorithm) and
   abortSignalRemove give other specs' "abort algorithms", which run before the abort event fires; a signal from
   another realm gets an abort listener instead. */
const ABORT_KEY = Symbol('AbortSignal');
let abortSignalRun, abortSignalAdd, abortSignalRemove, abortSignalIs;
class AbortSignal extends EventTarget {
    #aborted = false; #reason = undefined; #onabort = null;
    #dependent = false; #sources = []; #dependents = []; #algorithms = [];
    constructor(key = undefined) {
        if (key !== ABORT_KEY) throw new TypeError("Failed to construct 'AbortSignal': Illegal constructor");
        super();
    }
    get aborted() { return this.#aborted; }
    get reason() { return this.#reason; }
    get onabort() { return this.#onabort; }
    set onabort(v) { this.#onabort = typeof v === 'function' || (v !== null && typeof v === 'object') ? v : null; }
    throwIfAborted() { if (this.#aborted) throw this.#reason; }
    static abort(reason = undefined) {
        const s = new AbortSignal(ABORT_KEY);
        s.#aborted = true;
        s.#reason = reason === undefined ? new DOMException('signal is aborted without reason', 'AbortError') : reason;
        return s;
    }
    static timeout(ms) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'timeout' on 'AbortSignal': 1 argument required, but only 0 present.");
        ms = Math.trunc(Number(ms));
        if (!Number.isFinite(ms) || ms < 0 || ms > Number.MAX_SAFE_INTEGER) throw new TypeError("Failed to execute 'timeout' on 'AbortSignal': Value is outside the 'unsigned long long' value range.");
        const s = new AbortSignal(ABORT_KEY);
        setTimeout(() => abortSignalRun(s, new DOMException('signal timed out', 'TimeoutError')), ms);
        return s;
    }
    static any(signals) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'any' on 'AbortSignal': 1 argument required, but only 0 present.");
        if (signals == null || typeof signals[Symbol.iterator] !== 'function') throw new TypeError("Failed to execute 'any' on 'AbortSignal': The provided value cannot be converted to a sequence.");
        const list = [...signals];
        for (const s of list) if (!(s instanceof AbortSignal)) throw new TypeError("Failed to execute 'any' on 'AbortSignal': Failed to convert value to 'AbortSignal'.");
        const r = new AbortSignal(ABORT_KEY);
        for (const s of list) if (s.#aborted) { r.#aborted = true; r.#reason = s.#reason; return r; }
        r.#dependent = true;
        const link = src => { if (!r.#sources.includes(src)) { r.#sources.push(src); src.#dependents.push(r); } };
        for (const s of list) { if (!s.#dependent) link(s); else for (const src of s.#sources) link(src); }
        return r;
    }
    static {
        /* "signal abort": the dependents are marked aborted before any abort event fires */
        abortSignalRun = (s, reason) => {
            if (s.#aborted) return;
            s.#aborted = true;
            s.#reason = reason === undefined ? new DOMException('signal is aborted without reason', 'AbortError') : reason;
            const deps = [];
            for (const d of s.#dependents) if (!d.#aborted) { d.#aborted = true; d.#reason = s.#reason; deps.push(d); }
            const steps = x => {
                const algorithms = x.#algorithms; x.#algorithms = [];
                for (const f of algorithms) f();
                ABORT_FIRE(x, new Event('abort'));
            };
            steps(s);
            for (const d of deps) steps(d);
        };
        abortSignalIs = s => s !== null && typeof s === 'object' && #aborted in s;
        abortSignalAdd = (s, f) => {
            if (!(#algorithms in s)) { s.addEventListener('abort', f); return; }
            if (!s.#aborted) s.#algorithms.push(f);
        };
        abortSignalRemove = (s, f) => {
            if (!(#algorithms in s)) { s.removeEventListener('abort', f); return; }
            const i = s.#algorithms.indexOf(f); if (i >= 0) s.#algorithms.splice(i, 1);
        };
    }
}
class AbortController {
    #signal = new AbortSignal(ABORT_KEY);
    get signal() { return this.#signal; }
    abort(reason = undefined) { abortSignalRun(this.#signal, reason); }
}
for (const C of [AbortSignal, AbortController])
    Object.defineProperty(C.prototype, Symbol.toStringTag, { value: C.name, configurable: true });
