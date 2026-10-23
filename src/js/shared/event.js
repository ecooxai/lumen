/* Event and CustomEvent per the DOM standard, shared by the page and worker preludes. The including prelude defines
   EVN (its native bridge, for now()) and def(). */
/* Event per WebIDL: its state is internal, read through brand-checked accessors on the prototype; isTrusted is
   an own unforgeable accessor. The dispatch code sets the state through evTarget/evCurrent/evPhase/evTrusted. */
let evTarget, evCurrent, evPhase, evTrusted, evTrustedGet, evIs;
class Event {
    #bubbles = false; #cancelable = false; #composed = false; #canceled = false;
    #target = null; #current = null; #phase = 0; #ts = 0; #trusted = false;
    constructor(type, init = undefined) {
        if (arguments.length < 1) throw new TypeError("Failed to construct 'Event': 1 argument required, but only 0 present.");
        if (init != null && typeof init !== 'object' && typeof init !== 'function') throw new TypeError("Failed to construct 'Event': The provided value is not of type 'EventInit'.");
        def(this, '_type', String(type));
        if (init) { this.#bubbles = !!init.bubbles; this.#cancelable = !!init.cancelable; this.#composed = !!init.composed; }
        this.#ts = EVN.now();
        Object.defineProperty(this, 'isTrusted', { get: evTrustedGet, enumerable: true, configurable: false });
        def(this, '_stop', false); def(this, '_imm', false); def(this, '_passive', false); def(this, '_path', null);
    }
    get type() { this.#ts; return this._type; }
    get target() { return this.#target; }
    get srcElement() { return this.#target; }
    get currentTarget() { return this.#current; }
    get eventPhase() { return this.#phase; }
    get bubbles() { return this.#bubbles; }
    get cancelable() { return this.#cancelable; }
    get composed() { return this.#composed; }
    get defaultPrevented() { return this.#canceled; }
    get timeStamp() { return this.#ts; }
    get returnValue() { return !this.#canceled; } set returnValue(v) { this.#ts; if (!v && this.#cancelable && !this._passive) this.#canceled = true; }
    get cancelBubble() { this.#ts; return this._stop; } set cancelBubble(v) { this.#ts; if (v) this._stop = true; }
    preventDefault() { if (this.#cancelable && !this._passive) this.#canceled = true; }
    stopPropagation() { this.#ts; this._stop = true; }
    stopImmediatePropagation() { this.#ts; this._stop = this._imm = true; }
    composedPath() { this.#ts; return this._path && this.#phase ? this._path.slice() : []; }
    initEvent(type, bubbles = false, cancelable = false) {
        this.#ts;
        if (arguments.length < 1) throw new TypeError("Failed to execute 'initEvent' on 'Event': 1 argument required, but only 0 present.");
        if (this.#phase) return;   /* while it is being dispatched */
        this._type = String(type); this.#bubbles = !!bubbles; this.#cancelable = !!cancelable;
        this._stop = this._imm = false; this.#canceled = false; this.#trusted = false; this.#target = null;
        if (this._uninit) this._uninit = false;
    }
    static {
        evTarget = (e, t) => { e.#target = t; };
        evCurrent = (e, t) => { e.#current = t; };
        evPhase = (e, p) => { e.#phase = p; };
        evTrusted = e => { e.#trusted = true; };
        evTrustedGet = { get isTrusted() { return this.#trusted; } }.__lookupGetter__('isTrusted');
        evIs = e => e !== null && typeof e === 'object' && #ts in e;
    }
}
for (const o of [Event, Event.prototype]) Object.assign(o, { NONE: 0, CAPTURING_PHASE: 1, AT_TARGET: 2, BUBBLING_PHASE: 3 });
/* createEvent's events stay uninitialized until initEvent (dispatchEvent throws) */
const markUninit = e => def(e, '_uninit', true);
class CustomEvent extends Event {
    #detail = null;
    constructor(t, i = undefined) { super(t, i); if (i && i.detail !== undefined) this.#detail = i.detail; }
    get detail() { return this.#detail; }
    initCustomEvent(t, b = false, c = false, d = null) { if (this.eventPhase) return; this.initEvent(t, b, c); this.#detail = d; }
}
