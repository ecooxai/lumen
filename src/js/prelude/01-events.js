/* Lumen DOM/Web API layer on top of the native bridge N. Runs once per page context. */
(function (N, G) {
'use strict';
const def = (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, configurable: true, enumerable: false });
const acc = (proto, name, get, set) => Object.defineProperty(proto, name, { get, set, configurable: true, enumerable: true });
const methods = (proto, obj) => { for (const k of Object.getOwnPropertyNames(obj)) { const d = Object.getOwnPropertyDescriptor(obj, k); d.enumerable = false; Object.defineProperty(proto, k, d); } };
const fmt = (args) => args.map(a => { if (typeof a === 'string') return a; if (a instanceof Error) return a.stack || String(a); if (a && N.isNode(a)) return '<' + a.nodeName.toLowerCase() + '>'; try { return JSON.stringify(a) ?? String(a); } catch (e) { return String(a); } }).join(' ');
let reporting = false;
function report(e) {
    if (reporting) { N.log(3, 'Uncaught ' + (e && e.stack || e)); return; }
    reporting = true;
    try {
        const ev = new ErrorEvent('error', { message: String(e && e.message || e), error: e, cancelable: true });
        if (dispatch(G, ev)) N.log(3, 'Uncaught ' + (e && e.stack || e));
    } catch (e2) { N.log(3, 'Uncaught ' + (e && e.stack || e)); }
    reporting = false;
}

const nativeIDL = { get: N.idlGet, put: N.idlPut, register: N.idlRegister, fallback: N.idlFallback };
const DOMException = createDOMExceptionInterface(nativeIDL);
class DOMRectReadOnly {
    constructor(x = 0, y = 0, w = 0, h = 0) { this.x = x; this.y = y; this.width = w; this.height = h; }
    get left() { return Math.min(this.x, this.x + this.width); } get right() { return Math.max(this.x, this.x + this.width); }
    get top() { return Math.min(this.y, this.y + this.height); } get bottom() { return Math.max(this.y, this.y + this.height); }
    toJSON() { const { x, y, width, height, top, right, bottom, left } = this; return { x, y, width, height, top, right, bottom, left }; }
    static fromRect(r = {}) { return new this(r.x, r.y, r.width, r.height); }
}
class DOMRect extends DOMRectReadOnly {}

// Native private state is shared across same-origin realms and cannot be
// forged by inheriting Event.prototype or assigning a page-visible expando.
const eventState = event => {
    const state = N.domStateGet(event, 5);
    if (state === undefined) throw new TypeError("The object does not implement Event");
    return state;
};
function initializeEvent(event, type, bubbles, cancelable) {
    const state = eventState(event);
    if (state.dispatching) return false;
    state.initialized = true;
    event._stop = event._imm = event.defaultPrevented = false;
    event.isTrusted = false; event.target = null;
    event._type = type; event.bubbles = bubbles; event.cancelable = cancelable;
    return true;
}
class Event {
    get currentTarget() { return this._ct === undefined ? null : this._ct; }
    constructor(type, init) {
        if (arguments.length < 1) throw new TypeError("Failed to construct 'Event': 1 argument required");
        init = init || {};
        N.domStateSet(this, 5, { initialized: true, dispatching: false });
        def(this, '_type', String(type)); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed;
        this.defaultPrevented = false; this.target = null; this._ct = null; this.eventPhase = 0; this.timeStamp = N.now(); this.isTrusted = false;
        def(this, '_stop', false); def(this, '_imm', false); def(this, '_passive', false); def(this, '_path', null);
    }
    get type() { return this._type; }
    get srcElement() { return this.target; }
    get returnValue() { return !this.defaultPrevented; } set returnValue(v) { if (!v) this.preventDefault(); }
    get cancelBubble() { return this._stop; } set cancelBubble(v) { if (v) this._stop = true; }
    preventDefault() { if (this.cancelable && !this._passive) this.defaultPrevented = true; }
    stopPropagation() { this._stop = true; }
    stopImmediatePropagation() { this._stop = this._imm = true; }
    composedPath() { return this._path ? this._path.slice() : []; }
    initEvent(type, bubbles = false, cancelable = false) {
        eventState(this);
        if (!arguments.length) throw new TypeError("initEvent requires a type");
        initializeEvent(this, String(type), !!bubbles, !!cancelable);
    }
}
for (const [name, value] of Object.entries({ NONE: 0, CAPTURING_PHASE: 1, AT_TARGET: 2, BUBBLING_PHASE: 3 })) {
    Object.defineProperty(Event, name, { value, enumerable: true });
    Object.defineProperty(Event.prototype, name, { value, enumerable: true });
}
class CustomEvent extends Event {
    constructor(t, i) {
        if (!arguments.length) throw new TypeError("CustomEvent requires a type");
        super(t, i); this.detail = i && i.detail !== undefined ? i.detail : null;
    }
    initCustomEvent(t, b = false, c = false, d = null) {
        eventState(this);
        if (!arguments.length) throw new TypeError("initCustomEvent requires a type");
        if (initializeEvent(this, String(t), !!b, !!c)) this.detail = d;
    }
}
class UIEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.view = i.view || null; this.detail = i.detail || 0; } }
class FocusEvent extends UIEvent { constructor(t, i) { super(t, i); this.relatedTarget = (i && i.relatedTarget) || null; } }
class MouseEvent extends UIEvent {
    constructor(t, i) {
        super(t, i); i = i || {};
        for (const k of ['screenX', 'screenY', 'clientX', 'clientY', 'button', 'buttons', 'movementX', 'movementY']) this[k] = +i[k] || 0;
        for (const k of ['ctrlKey', 'shiftKey', 'altKey', 'metaKey']) this[k] = !!i[k];
        this.relatedTarget = i.relatedTarget || null;
    }
    get pageX() { return this.clientX + G.scrollX; } get pageY() { return this.clientY + G.scrollY; }
    get x() { return this.clientX; } get y() { return this.clientY; }
    get offsetX() { const r = this.target && this.target.getBoundingClientRect ? this.target.getBoundingClientRect() : null; return r ? this.clientX - r.left : this.clientX; }
    get offsetY() { const r = this.target && this.target.getBoundingClientRect ? this.target.getBoundingClientRect() : null; return r ? this.clientY - r.top : this.clientY; }
    getModifierState(k) { return k === 'Shift' ? this.shiftKey : k === 'Control' ? this.ctrlKey : k === 'Alt' ? this.altKey : k === 'Meta' ? this.metaKey : false; }
}
class PointerEvent extends MouseEvent { constructor(t, i) { super(t, i); i = i || {}; this.pointerId = i.pointerId || 1; this.pointerType = i.pointerType || 'mouse'; this.isPrimary = i.isPrimary !== false; this.width = i.width || 1; this.height = i.height || 1; this.pressure = i.pressure || 0; } }
class WheelEvent extends MouseEvent { constructor(t, i) { super(t, i); i = i || {}; this.deltaX = i.deltaX || 0; this.deltaY = i.deltaY || 0; this.deltaZ = i.deltaZ || 0; this.deltaMode = i.deltaMode || 0; } }
class KeyboardEvent extends UIEvent {
    constructor(t, i) {
        super(t, i); i = i || {};
        this.key = i.key || ''; this.code = i.code || ''; this.location = i.location || 0; this.repeat = !!i.repeat; this.isComposing = !!i.isComposing;
        for (const k of ['ctrlKey', 'shiftKey', 'altKey', 'metaKey']) this[k] = !!i[k];
        this.keyCode = i.keyCode || (this.key.length === 1 ? this.key.toUpperCase().charCodeAt(0) : ({ Enter: 13, Escape: 27, Backspace: 8, Tab: 9, ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40, Delete: 46 })[this.key] || 0);
        this.which = this.keyCode; this.charCode = i.charCode || 0;
    }
    getModifierState(k) { return MouseEvent.prototype.getModifierState.call(this, k); }
}
class InputEvent extends UIEvent { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data ?? null; this.inputType = i.inputType || ''; this.isComposing = !!i.isComposing; } }
class ErrorEvent extends Event { constructor(t, i) { super(t, i); eventState(this).errorEvent = true; i = i || {}; this.message = i.message || ''; this.filename = i.filename || ''; this.lineno = i.lineno || 0; this.colno = i.colno || 0; this.error = i.error; } }
const progressEventSlots = {
    get: event => N.domStateGet(event, 10),
    set: (event, state) => N.domStateSet(event, 10, state),
};
const progressEventState = event => {
    const state = progressEventSlots.get(event);
    if (!state) throw new TypeError('Illegal invocation');
    return state;
};
function progressUnsignedLongLong(value) {
    value = +value;
    if (!Number.isFinite(value) || value === 0) return 0;
    const remainder = Math.trunc(value) % 18446744073709551616;
    if (remainder === 0) return 0;
    return remainder < 0 ? remainder + 18446744073709551616 : remainder;
}
class ProgressEvent extends Event {
    constructor(type, init = {}) {
        if (!arguments.length) throw new TypeError('ProgressEvent requires a type');
        if (init !== null && typeof init !== 'object' && typeof init !== 'function')
            throw new TypeError('ProgressEvent options must be a dictionary');
        super(type, init); init = init || {};
        progressEventSlots.set(this, { lengthComputable: !!init.lengthComputable,
            loaded: progressUnsignedLongLong(init.loaded), total: progressUnsignedLongLong(init.total) });
    }
    get lengthComputable() { return progressEventState(this).lengthComputable; }
    get loaded() { return progressEventState(this).loaded; }
    get total() { return progressEventState(this).total; }
}
for (const name of ['lengthComputable', 'loaded', 'total']) {
    const descriptor = Object.getOwnPropertyDescriptor(ProgressEvent.prototype, name);
    descriptor.enumerable = true; Object.defineProperty(ProgressEvent.prototype, name, descriptor);
}
Object.defineProperty(ProgressEvent.prototype, Symbol.toStringTag, { value: 'ProgressEvent', configurable: true });
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data; this.origin = i.origin || ''; this.lastEventId = i.lastEventId || ''; this.source = i.source || null; this.ports = i.ports || []; } }
class PopStateEvent extends Event { #state; constructor(t, i) { super(t, i); this.#state = i && i.state !== undefined ? i.state : null; } get state() { return this.#state; } }
class HashChangeEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.oldURL = i.oldURL || ''; this.newURL = i.newURL || ''; } }
class PageTransitionEvent extends Event { constructor(t, i) { super(t, i); this.persisted = !!(i && i.persisted); } }
class AnimationEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.animationName = i.animationName || ''; this.elapsedTime = i.elapsedTime || 0; } }
class TransitionEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.propertyName = i.propertyName || ''; this.elapsedTime = i.elapsedTime || 0; } }


// Legacy createEvent interfaces retain their actual data fields. Sensor event
// constructors describe supplied data; they do not grant sensor permissions.
const legacyEventToken = {};
class BeforeUnloadEvent extends Event {
    constructor(token) {
        if (token !== legacyEventToken) throw new TypeError('Illegal constructor');
        super(''); eventState(this).beforeUnloadEvent = true; this._returnValue = '';
    }
    get returnValue() { return this._returnValue; }
    set returnValue(value) { this._returnValue = String(value); }
}
class TextEvent extends UIEvent {
    constructor(token) {
        if (token !== legacyEventToken) throw new TypeError('Illegal constructor');
        super(''); this._data = '';
    }
    get data() { return this._data; }
    initTextEvent(type, bubbles = false, cancelable = false, view = null, data = 'undefined') {
        eventState(this);
        if (!arguments.length) throw new TypeError('initTextEvent requires a type');
        type = String(type); data = String(data);
        if (initializeEvent(this, type, !!bubbles, !!cancelable)) { this.view = view; this._data = data; }
    }
}
function eventDouble(value) {
    const number = +value;
    if (!Number.isFinite(number)) throw new TypeError('Event number must be finite');
    return number;
}
const nullableEventDouble = value => value == null ? null : eventDouble(value);
class DeviceOrientationEvent extends Event {
    constructor(type, init = {}) {
        if (!arguments.length) throw new TypeError('DeviceOrientationEvent requires a type');
        super(type, init); init = init || {};
        this._alpha = nullableEventDouble(init.alpha); this._beta = nullableEventDouble(init.beta);
        this._gamma = nullableEventDouble(init.gamma); this._absolute = !!init.absolute;
    }
    get alpha() { return this._alpha; } get beta() { return this._beta; }
    get gamma() { return this._gamma; } get absolute() { return this._absolute; }
}
class DeviceMotionEventAcceleration {
    constructor(token, value) {
        if (token !== legacyEventToken) throw new TypeError('Illegal constructor');
        this._x = nullableEventDouble(value.x); this._y = nullableEventDouble(value.y); this._z = nullableEventDouble(value.z);
    }
    get x() { return this._x; } get y() { return this._y; } get z() { return this._z; }
}
class DeviceMotionEventRotationRate {
    constructor(token, value) {
        if (token !== legacyEventToken) throw new TypeError('Illegal constructor');
        this._alpha = nullableEventDouble(value.alpha); this._beta = nullableEventDouble(value.beta); this._gamma = nullableEventDouble(value.gamma);
    }
    get alpha() { return this._alpha; } get beta() { return this._beta; } get gamma() { return this._gamma; }
}
class DeviceMotionEvent extends Event {
    constructor(type, init = {}) {
        if (!arguments.length) throw new TypeError('DeviceMotionEvent requires a type');
        super(type, init); init = init || {};
        this._acceleration = init.acceleration == null ? null : new DeviceMotionEventAcceleration(legacyEventToken, init.acceleration);
        this._accelerationIncludingGravity = init.accelerationIncludingGravity == null ? null : new DeviceMotionEventAcceleration(legacyEventToken, init.accelerationIncludingGravity);
        this._rotationRate = init.rotationRate == null ? null : new DeviceMotionEventRotationRate(legacyEventToken, init.rotationRate);
        this._interval = init.interval === undefined ? 0 : eventDouble(init.interval);
    }
    get acceleration() { return this._acceleration; }
    get accelerationIncludingGravity() { return this._accelerationIncludingGravity; }
    get rotationRate() { return this._rotationRate; } get interval() { return this._interval; }
}
function createLegacyEvent(type) {
    const name = String(type).toLowerCase();
    const constructors = {
        beforeunloadevent: BeforeUnloadEvent, compositionevent: CompositionEvent, customevent: CustomEvent,
        devicemotionevent: DeviceMotionEvent, deviceorientationevent: DeviceOrientationEvent, dragevent: DragEvent,
        event: Event, events: Event, focusevent: FocusEvent, hashchangeevent: HashChangeEvent, htmlevents: Event,
        keyboardevent: KeyboardEvent, messageevent: MessageEvent, mouseevent: MouseEvent, mouseevents: MouseEvent,
        storageevent: StorageEvent, svgevents: Event, textevent: TextEvent, uievent: UIEvent, uievents: UIEvent,
    };
    const C = Object.prototype.hasOwnProperty.call(constructors, name) ? constructors[name] : null;
    if (!C) throw new DOMException('The provided event type is invalid.', 'NotSupportedError');
    const event = new C(C === BeforeUnloadEvent || C === TextEvent ? legacyEventToken : '');
    eventState(event).initialized = false;
    return event;
}

class EventTarget {
    addEventListener(type, fn, opts) {
        if (!fn) return;
        const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
        const o = typeof opts === 'object' && opts ? opts : {};
        if (o.signal && o.signal.aborted) return;
        let m = this.__ls; if (!m) { m = new Map(); def(this, '__ls', m); }
        type = String(type);
        let a = m.get(type); if (!a) m.set(type, a = []);
        for (const l of a) if (l.fn === fn && l.capture === capture) return;
        a.push({ fn, capture, once: !!o.once, passive: !!o.passive, removed: false });
        if (o.signal) o.signal.addEventListener('abort', () => this.removeEventListener(type, fn, capture));
    }
    removeEventListener(type, fn, opts) {
        const m = this.__ls; if (!m) return;
        const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
        const a = m.get(String(type)); if (!a) return;
        const i = a.findIndex(l => l.fn === fn && l.capture === capture);
        if (i >= 0) { a[i].removed = true; a.splice(i, 1); }
    }
    dispatchEvent(ev) {
        eventState(ev);
        return dispatch(this, ev, false, undefined, nativeIDL.get(4, this) !== undefined);
    }
}
function parentForEvent(t, ev) {
    if (t === G) return null;
    if (N.isNode(t)) {
        const p = N.parent(t);
        if (p) return p;
        const ty = N.type(t);
        if (ty === 11) { const h = N.host(t); return h && ev.composed ? h : null; }
        if (ty === 9) return ev._type === 'load' ? null : G;
    }
    return null;
}
/* Blink fires webkit-prefixed listeners on a target only when it has no unprefixed ones */
const LEGACY_EV = { animationstart: 'webkitAnimationStart', animationend: 'webkitAnimationEnd', animationiteration: 'webkitAnimationIteration', transitionend: 'webkitTransitionEnd' };
function invoke(t, ev, phase, checkpoint, skipHandler) {
    ev._ct = t;
    if (phase !== 1 && !skipHandler && !handlerHasRegisteredListener(t, ev._type)) {
        let h = null;
        try { h = t['on' + ev._type]; } catch (e) {}
        if (typeof h === 'function') {
            let r;
            const winErr = t === G && ev._type === 'error' && ev instanceof ErrorEvent;
            try {
                try { r = winErr ? h.call(t, ev.message, ev.filename, ev.lineno, ev.colno, ev.error) : h.call(t, ev); }
                finally { if (checkpoint) checkpoint(); }
            } catch (e) { report(e); if (checkpoint) checkpoint(); }
            if (r === false || (winErr && r === true)) ev.preventDefault();
            if (ev._imm) return;
        }
    }
    const m = t.__ls; if (!m) return;
    const type = ev._type;
    let a = m.get(type);
    if ((!a || !a.length) && ev.isTrusted && LEGACY_EV[type]) { a = m.get(LEGACY_EV[type]); ev._type = LEGACY_EV[type]; }
    if (!a || !a.length) { ev._type = type; return; }
    for (const l of a.slice()) {
        if (l.removed) continue;
        if (phase === 1 && !l.capture) continue;
        if (phase === 3 && l.capture) continue;
        if (l.once) t.removeEventListener(ev._type, l.fn, l.capture);
        ev._passive = l.passive;
        try {
            try { if (typeof l.fn === 'function') l.fn.call(t, ev); else if (l.fn && typeof l.fn.handleEvent === 'function') l.fn.handleEvent(ev); }
            finally { if (checkpoint) checkpoint(); }
        } catch (e) { report(e); if (checkpoint) checkpoint(); }
        ev._passive = false;
        if (ev._imm) break;
    }
    ev._type = type;
}
function dispatch(target, ev, trusted, checkpoint, skipHandler) {
    const state = eventState(ev);
    if (state.dispatching || !state.initialized) throw new DOMException('The event is uninitialized or already dispatching.', 'InvalidStateError');
    state.dispatching = true;
    try {
        ev.target = target; if (trusted !== undefined) ev.isTrusted = !!trusted;
        const path = [];
        for (let t = parentForEvent(target, ev); t; t = parentForEvent(t, ev)) path.push(t);
        ev._path = [target].concat(path);
        ev.eventPhase = 1;
        for (let i = path.length - 1; i >= 0 && !ev._stop; i--) invoke(path[i], ev, 1, checkpoint, false);
        if (!ev._stop) { ev.eventPhase = 2; invoke(target, ev, 1, checkpoint, true); }
        if (!ev._stop) { ev.eventPhase = 2; invoke(target, ev, 3, checkpoint, skipHandler); }
        if (ev.bubbles) { ev.eventPhase = 3; for (const t of path) { if (ev._stop) break; invoke(t, ev, 3, checkpoint, false); } }
        return !ev.defaultPrevented;
    } finally {
        state.dispatching = false;
        ev.eventPhase = 0; ev._ct = null; ev._path = null;
        ev._stop = ev._imm = ev._passive = false;
    }
}
class AbortSignal extends EventTarget {
    constructor() { super(); this.aborted = false; this.reason = undefined; this.onabort = null; }
    throwIfAborted() { if (this.aborted) throw this.reason; }
    static abort(reason) { const c = new AbortController(); c.abort(reason); return c.signal; }
    static timeout(ms) { const c = new AbortController(); setTimeout(() => c.abort(new DOMException('signal timed out', 'TimeoutError')), ms); return c.signal; }
    static any(signals) { const c = new AbortController(); for (const s of signals) { if (s.aborted) { c.abort(s.reason); break; } s.addEventListener('abort', () => c.abort(s.reason)); } return c.signal; }
}
class AbortController {
    constructor() { this.signal = new AbortSignal(); }
    abort(reason) { const s = this.signal; if (s.aborted) return; s.aborted = true; s.reason = reason === undefined ? new DOMException('signal is aborted without reason', 'AbortError') : reason; const ev = new Event('abort'); if (typeof s.onabort === 'function') { try { s.onabort(ev); } catch (e) { report(e); } } dispatch(s, ev); }
}

const observers = [];
let moQueued = false;
class MutationRecord { constructor(o) { Object.assign(this, { type: '', target: null, addedNodes: [], removedNodes: [], previousSibling: null, nextSibling: null, attributeName: null, attributeNamespace: null, oldValue: null }, o); } }
class MutationObserver {
    constructor(cb) { if (typeof cb !== 'function') throw new TypeError('MutationObserver callback must be a function'); this._cb = cb; this._targets = []; this._records = []; }
    observe(target, o = {}) {
        if (o.attributeOldValue || o.attributeFilter) o = Object.assign({ attributes: true }, o);
        if (o.characterDataOldValue) o = Object.assign({ characterData: true }, o);
        const ex = this._targets.find(x => x.t === target);
        if (ex) ex.o = o; else this._targets.push({ t: target, o });
        if (!observers.includes(this)) observers.push(this);
    }
    disconnect() { this._targets = []; this._records = []; const i = observers.indexOf(this); if (i >= 0) observers.splice(i, 1); }
    takeRecords() { const r = this._records; this._records = []; return r; }
}
function notify(type, target, extra) {
    if (!observers.length) return;
    let added = false;
    for (const mo of observers) {
        for (const { t, o } of mo._targets) {
            if (!(t === target || (o.subtree && N.contains(t, target)))) continue;
            if (type === 'childList' && !o.childList) continue;
            if (type === 'attributes' && (!o.attributes || (o.attributeFilter && !o.attributeFilter.includes(extra.attributeName)))) continue;
            if (type === 'characterData' && !o.characterData) continue;
            const rec = new MutationRecord(Object.assign({ type, target }, extra));
            if (!((type === 'attributes' && o.attributeOldValue) || (type === 'characterData' && o.characterDataOldValue))) rec.oldValue = null;
            mo._records.push(rec); added = true;
            break;
        }
    }
    if (added && !moQueued) { moQueued = true; Promise.resolve().then(deliverMutations); }
}
function deliverMutations() {
    moQueued = false;
    for (const mo of observers.slice()) { const r = mo.takeRecords(); if (r.length) { try { mo._cb.call(mo, r, mo); } catch (e) { report(e); } } }
}
