/* Lumen DOM/Web API layer on top of the native bridge N. Runs once per page context. */
(function (N, G) {
'use strict';
/*@shared safecoll.js*/
const PRE_GLOBALS = new SSet(Object.getOwnPropertyNames(G));   /* the engine's own globals: not WebIDL interfaces */
/* Trusted Types (07l): whether a CSP policy requires them; internal string compiles (inline handlers) skip the checks */
let ttReq = false, ttInternal = 0;
{   /* href reflection, baseURI and new URL() re-parse the same (url, base) pairs; parsed parts are never mutated, so share them */
    const parse = N.urlParse, memo = new SMap();
    N.urlParse = (u, b, enc) => {
        const k = (enc ? enc + ';' : '') + (b == null ? 'a' + u : b.length + ':' + b + u);
        let r = memo.get(k);
        if (r === undefined) { r = enc ? parse(u, b, enc) : parse(u, b); if (memo.size >= 2048) memo.clear(); memo.set(k, r); }
        return r;
    };
}
const def = (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, configurable: true, enumerable: false });
/* WebIDL names accessor functions "get x" / "set x"; anonymous ones get those names */
const accName = (f, n) => { if (typeof f === 'function' && (f.name === '' || f.name === 'get' || f.name === 'set')) Object.defineProperty(f, 'name', { value: n }); return f; };
const acc = (proto, name, get, set) => Object.defineProperty(proto, name, { get: accName(get, 'get ' + name), set: accName(set, 'set ' + name), configurable: true, enumerable: true });
const methods = (proto, obj) => { for (const k of Object.getOwnPropertyNames(obj)) { const d = Object.getOwnPropertyDescriptor(obj, k); d.enumerable = false; Object.defineProperty(proto, k, d); } };
const fmt = (args) => args.map(a => { if (typeof a === 'string') return a; if (a instanceof Error) return a.stack || String(a); if (a && N.isNode(a)) return '<' + a.nodeName.toLowerCase() + '>'; try { return JSON.stringify(a) ?? String(a); } catch (e) { return String(a); } }).join(' ');
let reporting = false;
/* appends v to the array a without [[Set]]: index accessors a page puts on Array.prototype must not see it */
const apushDef = Reflect.defineProperty, apush = (a, v) => { apushDef(a, a.length, { value: v, writable: true, enumerable: true, configurable: true }); return a.length; };
function report(e) {
    if (reporting) { N.log(3, 'Uncaught ' + (e && e.stack || e)); return; }
    reporting = true;
    try {
        const [filename, lineno, colno] = N.errPos(e);
        const ev = new ErrorEvent('error', { message: 'Uncaught ' + (e instanceof Error || e instanceof DOMException ? e.name + ': ' + e.message : String(e)), filename, lineno, colno, error: e, cancelable: true });
        if (dispatch(G, ev)) N.log(3, 'Uncaught ' + (e && e.stack || e));
    } catch (e2) { N.log(3, 'Uncaught ' + (e && e.stack || e)); }
    reporting = false;
}

/*@shared domexception.js*/
/*@shared quota.js*/
/*@shared geometry.js*/

const EVN = N;
/*@shared event.js*/
class CommandEvent extends Event {
    constructor(t, i = {}) {
        super(t, i); i = i || {};
        if (i.source != null && !(i.source instanceof Element)) throw new TypeError('source must be an Element');
        def(this, '_source', i.source ?? null); def(this, '_command', String(i.command ?? ''));
    }
    get source() { return this._source; } get command() { return this._command; }
}
class ToggleEvent extends Event {
    constructor(t, i = {}) {
        super(t, i); i = i || {};
        if (i.source != null && !(i.source instanceof Element)) throw new TypeError('source must be an Element');
        def(this, '_source', i.source ?? null);
        def(this, '_oldState', String(i.oldState ?? '')); def(this, '_newState', String(i.newState ?? ''));
    }
    get source() { return this._source; }
    get oldState() { return this._oldState; } get newState() { return this._newState; }
}
const commandTargets = new WeakMap();
class UIEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.view = i.view || null; this.detail = i.detail || 0; this.sourceCapabilities = i.sourceCapabilities || null; } }
class FocusEvent extends UIEvent { constructor(t, i) { super(t, i); this.relatedTarget = (i && i.relatedTarget) || null; } }
class MouseEvent extends UIEvent {
    constructor(t, i) {
        super(t, i); i = i || {};
        for (const k of ['screenX', 'screenY', 'clientX', 'clientY', 'button', 'buttons', 'movementX', 'movementY']) this[k] = +i[k] || 0;
        /* as Chrome: whole pixels, except pointer events other than click/auxclick/contextmenu */
        if (!(this instanceof PointerEvent) || /^(?:aux)?click$|^contextmenu$/.test(this.type))
            for (const k of ['screenX', 'screenY', 'clientX', 'clientY']) this[k] = Math.floor(this[k]);
        for (const k of ['ctrlKey', 'shiftKey', 'altKey', 'metaKey']) this[k] = !!i[k];
        this.relatedTarget = i.relatedTarget || null;
    }
    get pageX() { return this.clientX + G.scrollX; } get pageY() { return this.clientY + G.scrollY; }
    get x() { return this.clientX; } get y() { return this.clientY; }
    get offsetX() { const r = this.target && this.target.getBoundingClientRect ? this.target.getBoundingClientRect() : null; return r ? this.clientX - r.left : this.clientX; }
    get offsetY() { const r = this.target && this.target.getBoundingClientRect ? this.target.getBoundingClientRect() : null; return r ? this.clientY - r.top : this.clientY; }
    get which() { return /^(?:mouse(?:down|up)|(?:aux|dbl)?click|contextmenu)$/.test(this.type) ? this.button + 1 : this.buttons & 1 ? 1 : this.buttons & 4 ? 2 : this.buttons & 2 ? 3 : 0; }
    get layerX() { return this.offsetX; } get layerY() { return this.offsetY; }
    getModifierState(k) { return k === 'Shift' ? this.shiftKey : k === 'Control' ? this.ctrlKey : k === 'Alt' ? this.altKey : k === 'Meta' ? this.metaKey : false; }
}
class PointerEvent extends MouseEvent {
    constructor(t, i) {
        super(t, i); i = i || {};
        this.pointerId = i.pointerId ?? 1; this.pointerType = i.pointerType || 'mouse'; this.isPrimary = i.isPrimary !== false;
        this.width = i.width || 1; this.height = i.height || 1; this.pressure = +i.pressure || 0; this.tangentialPressure = +i.tangentialPressure || 0;
        this.tiltX = +i.tiltX || 0; this.tiltY = +i.tiltY || 0; this.twist = +i.twist || 0;
        this.altitudeAngle = i.altitudeAngle ?? Math.PI / 2; this.azimuthAngle = +i.azimuthAngle || 0; this.persistentDeviceId = +i.persistentDeviceId || 0;
        def(this, '__co', i.coalescedEvents ? Array.from(i.coalescedEvents) : null);
        def(this, '__pr', i.predictedEvents ? Array.from(i.predictedEvents) : []);
    }
    /* a native move is one sample: it is its own coalesced event */
    getCoalescedEvents() { return this.__co || (this.isTrusted && this.type === 'pointermove' ? [this] : []); }
    getPredictedEvents() { return this.__pr.slice(); }
}
class WheelEvent extends MouseEvent { constructor(t, i) { super(t, i); i = i || {}; this.deltaX = i.deltaX || 0; this.deltaY = i.deltaY || 0; this.deltaZ = i.deltaZ || 0; this.deltaMode = i.deltaMode || 0; } }
class KeyboardEvent extends UIEvent {
    constructor(t, i) {
        super(t, i); i = i || {};
        this.key = i.key || ''; this.code = i.code || ''; this.location = i.location || 0; this.repeat = !!i.repeat; this.isComposing = !!i.isComposing;
        for (const k of ['ctrlKey', 'shiftKey', 'altKey', 'metaKey']) this[k] = !!i[k];
        this.keyCode = i.keyCode || 0; this.charCode = i.charCode || 0;
        this.which = i.which || 0;
    }
    getModifierState(k) { return MouseEvent.prototype.getModifierState.call(this, k); }
}
class InputEvent extends UIEvent {
    constructor(t, i) {
        super(t, i); i = i || {};
        const tr = i.targetRanges === undefined ? [] : Array.from(i.targetRanges);
        if (tr.some(r => !(r instanceof StaticRange))) throw new TypeError("Failed to construct 'InputEvent': Failed to read the 'targetRanges' property from 'InputEventInit': Failed to convert value to 'StaticRange'.");
        this.data = i.data == null ? null : String(i.data); this.inputType = i.inputType === undefined ? '' : String(i.inputType); this.isComposing = !!i.isComposing;
        this.dataTransfer = i.dataTransfer ?? null;
        def(this, '_tr', tr);
    }
    getTargetRanges() { return this._tr.slice(); }
}
class ErrorEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.message = i.message || ''; this.filename = i.filename || ''; this.lineno = i.lineno || 0; this.colno = i.colno || 0; this.error = i.error; } }
class ProgressEvent extends Event { constructor(t, i = undefined) { super(t, i); i = i || {}; this.lengthComputable = !!i.lengthComputable; this.loaded = i.loaded || 0; this.total = i.total || 0; } }
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data; this.origin = i.origin || ''; this.lastEventId = i.lastEventId || ''; this.source = i.source || null; this.ports = i.ports || []; } }
class PopStateEvent extends Event { constructor(t, i) { super(t, i); this.state = i && i.state !== undefined ? i.state : null; } }
class HashChangeEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.oldURL = i.oldURL || ''; this.newURL = i.newURL || ''; } }
class PageTransitionEvent extends Event { constructor(t, i) { super(t, i); this.persisted = !!(i && i.persisted); } }
class AnimationEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.animationName = i.animationName || ''; this.elapsedTime = i.elapsedTime || 0; } }
class TransitionEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.propertyName = i.propertyName || ''; this.elapsedTime = i.elapsedTime || 0; } }

/* the receiver of an EventTarget operation: a bare call (`addEventListener(...)`) is the window's, as for any
   interface on the global's prototype chain; anything but an EventTarget is an illegal invocation */
const etThis = (o, op) => {
    if (o == null) return G;
    if ((typeof o === 'object' || typeof o === 'function') && (N.isNode(o) || N.etBranded(o))) return o;
    throw new TypeError(`Failed to execute '${op}' on 'EventTarget': Illegal invocation`);
};
let etListenerChanged = null;
class EventTarget {
    constructor() { N.etBrand(this); }
    addEventListener(type, fn, opts) {
        const self = etThis(this, 'addEventListener');
        if (self !== this) return self.addEventListener(type, fn, opts);
        if (!fn) return;
        const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
        const o = typeof opts === 'object' && opts ? opts : {};
        if (o.signal && o.signal.aborted) return;
        let m = this.__ls; if (!m) { m = new SMap(); def(this, '__ls', m); }
        type = String(type);
        let a = m.get(type); if (!a) m.set(type, a = []);
        for (const l of a) if (l.fn === fn && l.capture === capture) return;
        /* Chrome's intervention: unless told otherwise, these listeners on the window, document, html or body are passive */
        const passive = o.passive !== undefined ? !!o.passive : /^(?:touchstart|touchmove|wheel|mousewheel)$/.test(type) && (this === G || this.nodeType === 9 ||
            (this.nodeType === 1 && this.parentNode && (this.parentNode.nodeType === 9 || (this.localName === 'body' && this.parentNode.parentNode && this.parentNode.parentNode.nodeType === 9))));
        apush(a, { fn, capture, once: !!o.once, passive, removed: false });
        if (etListenerChanged) etListenerChanged(this);
        if (o.signal) abortSignalAdd(o.signal, () => this.removeEventListener(type, fn, capture));
    }
    removeEventListener(type, fn, opts) {
        const self = etThis(this, 'removeEventListener');
        if (self !== this) return self.removeEventListener(type, fn, opts);
        const m = this.__ls; if (!m) return;
        const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
        const a = m.get(String(type)); if (!a) return;
        const i = a.findIndex(l => l.fn === fn && l.capture === capture);
        if (i >= 0) { a[i].removed = true; a.splice(i, 1); if (etListenerChanged) etListenerChanged(this); }
    }
    dispatchEvent(ev) {
        const self = etThis(this, 'dispatchEvent');
        if (self !== this) return self.dispatchEvent(ev);
        if (!evIs(ev)) {
            const r = ev !== null && typeof ev === 'object' ? N.realmOf(ev) : undefined;
            if (r && r !== REALM && r.isEvent(ev)) return r.dispatch(this, ev);
            throw new TypeError("Failed to execute 'dispatchEvent': parameter 1 is not of type 'Event'.");
        }
        if (ev._uninit) throw new DOMException("Failed to execute 'dispatchEvent' on 'EventTarget': The event provided is uninitialized.", 'InvalidStateError');
        return dispatch(this, ev);
    }
}
N.etBrand(G);
function parentForEvent(t, ev) {
    if (t === G) return null;
    if (N.isNode(t)) {
        const p = N.parent(t);
        if (p) return p;
        const ty = N.type(t);
        if (ty === 11) { const h = N.host(t); return h && ev.composed ? h : null; }
        if (ty === 9) return ev._type === 'load' ? null : G;
        return null;
    }
    return t.__evp || null;   /* IndexedDB: request -> transaction -> database */
}
/* Blink fires webkit-prefixed listeners on a target only when it has no unprefixed ones */
const LEGACY_KEY = { webkitAnimationStart: 'webkitanimationstart', webkitAnimationEnd: 'webkitanimationend', webkitAnimationIteration: 'webkitanimationiteration', webkitTransitionEnd: 'webkittransitionend' };
const LEGACY_EV = { animationstart: 'webkitAnimationStart', animationend: 'webkitAnimationEnd', animationiteration: 'webkitAnimationIteration', transitionend: 'webkitTransitionEnd' };
/* a listener runs with its own realm's window.event set (undefined while the invocation target is in a shadow tree) */
const listenerRealm = f => typeof f === 'function' && Object.getPrototypeOf(f) === Function.prototype ? REALM : (N.realmOf(f) || REALM);
function invoke(t, ev, phase, shadow) {
    evCurrent(ev, t);
    const cur = shadow ? undefined : ev;
    // the listeners are fixed when the target's turn starts: ones the event handler adds don't see this event
    const m = t.__ls, type = ev._type;
    const handler = ty => { try { const h = t['on' + (LEGACY_KEY[ty] || ty)]; return typeof h === 'function' ? h : null; } catch (e) { return null; } };
    let a = m && m.get(type), legacy = false, h = phase !== 1 || (ev.isTrusted && LEGACY_EV[type]) ? handler(type) : null;
    /* a trusted animation/transition event no listener (handlers included) takes goes to the webkit-prefixed type's */
    if ((!a || !a.length) && !h && ev.isTrusted && LEGACY_EV[type]) { a = m && m.get(LEGACY_EV[type]); h = handler(LEGACY_EV[type]); legacy = true; }
    a = a && a.length ? a.slice() : null;
    if (legacy) ev._type = LEGACY_EV[type];
    if (phase !== 1) {
        if (h && N.fullyActive(t) && N.fullyActive(h)) {
            let r;
            const winErr = t === G && ev._type === 'error' && ev instanceof ErrorEvent;
            const realm = listenerRealm(h), prev = realm.swap(cur);
            try { r = winErr ? h.call(t, ev.message, ev.filename, ev.lineno, ev.colno, ev.error) : h.call(t, ev); } catch (e) { report(e); } finally { realm.swap(prev); }
            if (r === false || (winErr && r === true)) ev.preventDefault();
            if (ev._imm) { ev._type = type; return; }
        }
    }
    if (!a) { ev._type = type; return; }
    for (const l of a) {
        if (l.removed) continue;
        if (phase === 1 && !l.capture) continue;
        if (phase === 3 && l.capture) continue;
        if (l.once) t.removeEventListener(ev._type, l.fn, l.capture);
        /* a listener whose target or callback belongs to a global that is no longer fully active does not run */
        if (!N.fullyActive(t) || (l.fn !== null && (typeof l.fn === 'object' || typeof l.fn === 'function') && !N.fullyActive(l.fn))) continue;
        ev._passive = l.passive;
        const realm = l.fn !== null && typeof l.fn === 'object' || typeof l.fn === 'function' ? listenerRealm(l.fn) : REALM, prev = realm.swap(cur);
        try { if (typeof l.fn === 'function') l.fn.call(t, ev); else if (l.fn && typeof l.fn.handleEvent === 'function') l.fn.handleEvent(ev); }
        catch (e) { report(e); } finally { realm.swap(prev); }
        ev._passive = false;
        if (ev._imm) break;
    }
    ev._type = type;
}
/* window.event: the event a listener of this realm is handling */
let curEvent;
/* this realm's hooks for other realms: an Event is dispatched by its own realm (its state is in private fields) */
const REALM = Object.freeze({
    isEvent: e => evIs(e),
    compileEventHandler: (el, name, source) => compileHandler(el, name, source),
    dispatch(t, ev) {
        if (ev._uninit) throw new DOMException("Failed to execute 'dispatchEvent' on 'EventTarget': The event provided is uninitialized.", 'InvalidStateError');
        return dispatch(t, ev);
    },
    swap(ev) { const p = curEvent; curEvent = ev; return p; },
    typeError: m => new TypeError(m),
    blob(u) { const b = objectURLs.get(u); return b ? [new Uint8Array(b._buf), b.type] : null; },   /* a frame loading one of this realm's blob: URLs */
});
/* a TypeError of a callback object's realm (WebIDL throws those when calling a user object fails) */
const cbTypeError = (o, m) => { const r = o !== null && (typeof o === 'object' || typeof o === 'function') ? N.realmOf(o) : null; return r ? r.typeError(m) : new TypeError(m); };
N.setRealm(REALM);
/* HTML user activation, with Chrome's 5 s transient window: trusted keydown (but Escape), mousedown, touchend, mouse
   pointerdown and touch or pen pointerup activate the window; activation-gated APIs consume it */
const ACTIVATION_MS = 5000;
let activationAt = -Infinity, stickyActivation = false;
const activates = ev => {
    const t = ev._type;
    if (t === 'keydown') return ev.key !== 'Escape';
    if (t === 'pointerdown') return ev.pointerType === 'mouse';
    if (t === 'pointerup') return ev.pointerType !== 'mouse';
    return t === 'mousedown' || t === 'touchend';
};
const transientActivation = () => N.now() - activationAt < ACTIVATION_MS;
const consumeActivation = () => { activationAt = -Infinity; };
function dispatch(target, ev, trusted) {
    evTarget(ev, target); if (trusted) evTrusted(ev);
    if (trusted && activates(ev)) { activationAt = N.now(); stickyActivation = true; }
    ev._stop = ev._imm = false;
    const path = [];
    for (let t = parentForEvent(target, ev); t; t = parentForEvent(t, ev)) apush(path, t);
    ev._path = [target].concat(path);
    /* whether each path entry is in a shadow tree, fixed now: an entry below a shadow root (inclusive) is */
    const shadow = new Array(ev._path.length);
    for (let i = ev._path.length - 1, inside = false; i >= 0; i--) {
        const n = ev._path[i];
        if (!inside && N.isNode(n) && N.type(n) === 11 && N.host(n)) inside = true;
        shadow[i] = inside;
    }
    /* a click runs one activation behavior: the target's, else (bubbling) the nearest ancestor's that has one. The shell
       navigates links for clicks it fires itself. */
    let act = null, pre = null;
    if (ev._type === 'click' && ev instanceof MouseEvent && !(trusted && target.closest && target.closest('a[href]'))) {
        act = hasActivation(target) ? target : ev.bubbles ? path.find(hasActivation) || null : null;
        pre = act && preActivate(act, ev);
    }
    evPhase(ev, 1);
    for (let i = path.length - 1; i >= 0 && !ev._stop; i--) invoke(path[i], ev, 1, shadow[i + 1]);
    if (!ev._stop) { evPhase(ev, 2); invoke(target, ev, 2, shadow[0]); }
    /* a submit or reset event from a nested form stops at the next form up, as in Blink and Gecko */
    const formStop = (ev._type === 'submit' || ev._type === 'reset') && (t => t instanceof HTMLFormElement);
    if (ev.bubbles) { evPhase(ev, 3); for (let i = 0; i < path.length; i++) { if (ev._stop) break; if (formStop && formStop(path[i])) { ev._stop = true; break; } invoke(path[i], ev, 3, shadow[i + 1]); } }
    evPhase(ev, 0); evCurrent(ev, null);
    if (act) { if (!ev.defaultPrevented) { try { activate(act, ev, pre); } catch (e) { report(e); } } else if (pre && pre.restore) pre.restore(); }
    /* popover light dismiss and close requests (04p-popover.js) */
    const ty = ev._type;
    if (trusted && ty === 'click' && !ev.defaultPrevented && target.closest) { const sel = target.closest('select'); if (sel && !sel.disabled) { try { listboxClick(sel, ev); } catch (e) { report(e); } } }
    if (trusted && (ty === 'pointerdown' || ty === 'pointerup' || ty === 'keydown') && typeof popUserEvent === 'function') { try { popUserEvent(target, ev); } catch (e) { report(e); } }
    return !ev.defaultPrevented;
}
const ABORT_FIRE = (t, ev) => dispatch(t, ev, true);
/*@shared abort.js*/
const OBS_REPORT = report;
/*@shared observable.js*/

const observers = [];
let moQueued = false;
const MR_KEY = Symbol('MutationRecord');
class MutationRecord {
    #r;
    constructor(key = undefined, o = undefined) {
        if (key !== MR_KEY) throw new TypeError('Illegal constructor');
        this.#r = Object.assign({ type: '', target: null, addedNodes: [], removedNodes: [], previousSibling: null, nextSibling: null, attributeName: null, attributeNamespace: null, oldValue: null }, o);
        this.#r.addedNodes = nodeList(this.#r.addedNodes); this.#r.removedNodes = nodeList(this.#r.removedNodes);
    }
    get type() { return this.#r.type; } get target() { return this.#r.target; }
    get addedNodes() { return this.#r.addedNodes; } get removedNodes() { return this.#r.removedNodes; }
    get previousSibling() { return this.#r.previousSibling; } get nextSibling() { return this.#r.nextSibling; }
    get attributeName() { return this.#r.attributeName; } get attributeNamespace() { return this.#r.attributeNamespace; }
    get oldValue() { return this.#r.oldValue; }
}
class MutationObserver {
    constructor(cb) { if (typeof cb !== 'function') throw new TypeError('MutationObserver callback must be a function'); this._cb = cb; this._targets = []; this._records = []; }
    observe(target, opts = {}) {
        const fail = m => new TypeError(`Failed to execute 'observe' on 'MutationObserver': ${m}`);
        if (!N.isNode(target)) throw fail("parameter 1 is not of type 'Node'.");
        if (opts != null && typeof opts !== 'object' && typeof opts !== 'function') throw fail("parameter 2 is not of type 'MutationObserverInit'.");
        opts = opts || {};
        /* the dictionary is read once, in its members' order */
        const get = k => opts[k];
        const o = { attributeFilter: get('attributeFilter'), attributeOldValue: get('attributeOldValue'), attributes: get('attributes'),
            characterData: get('characterData'), characterDataOldValue: get('characterDataOldValue'), childList: !!get('childList'), subtree: !!get('subtree') };
        if (o.attributeFilter !== undefined) {
            if (o.attributeFilter === null || typeof o.attributeFilter !== 'object' || typeof o.attributeFilter[Symbol.iterator] !== 'function') throw fail("The provided value cannot be converted to a sequence.");
            o.attributeFilter = Array.from(o.attributeFilter, String);
        }
        for (const k of ['attributeOldValue', 'attributes', 'characterData', 'characterDataOldValue']) if (o[k] !== undefined) o[k] = !!o[k];
        if ((o.attributeOldValue !== undefined || o.attributeFilter !== undefined) && o.attributes === undefined) o.attributes = true;
        if (o.characterDataOldValue !== undefined && o.characterData === undefined) o.characterData = true;
        if (!o.childList && !o.attributes && !o.characterData) throw fail("The options object must set at least one of 'attributes', 'characterData', or 'childList' to true.");
        if (o.attributeOldValue && !o.attributes) throw fail("The options object may only set 'attributeOldValue' to true when 'attributes' is true or not present.");
        if (o.attributeFilter !== undefined && !o.attributes) throw fail("The options object may only set 'attributeFilter' when 'attributes' is true or not present.");
        if (o.characterDataOldValue && !o.characterData) throw fail("The options object may only set 'characterDataOldValue' to true when 'characterData' is true or not present.");
        const ex = this._targets.find(x => x.t === target && !x.source);
        if (ex) { ex.o = o; this._targets = this._targets.filter(x => x.source !== ex); } else apush(this._targets, { t: target, o });
        if (!observers.includes(this)) apush(observers, this);
    }
    disconnect() { this._targets = []; this._records = []; const i = observers.indexOf(this); if (i >= 0) observers.splice(i, 1); }
    takeRecords() { const r = this._records; this._records = []; return r; }
}
function notify(type, target, extra) {
    if (type === 'attributes' && extra.attributeName === 'commandfor') commandTargets.delete(target);
    if (type !== 'characterData') slotMutated(type, target, extra);
    if (!observers.length) return;
    /* nodes removed from an observed subtree stay observed, through transient registrations, until the observers
       are next notified */
    if (type === 'childList' && extra.removedNodes && extra.removedNodes.length) {
        for (const mo of observers) for (const reg of mo._targets.slice()) {
            if (!reg.o.subtree || !(reg.t === target || N.contains(reg.t, target))) continue;
            for (const n of extra.removedNodes) apush(mo._targets, { t: n, o: reg.o, source: reg });
        }
    }
    for (const mo of observers) {
        for (const { t, o } of mo._targets) {
            if (!(t === target || (o.subtree && N.contains(t, target)))) continue;
            if (type === 'childList' && !o.childList) continue;
            if (type === 'attributes' && (!o.attributes || (o.attributeFilter && (extra.attributeNamespace != null || !o.attributeFilter.includes(extra.attributeName))))) continue;
            if (type === 'characterData' && !o.characterData) continue;
            const keepOld = (type === 'attributes' && o.attributeOldValue) || (type === 'characterData' && o.characterDataOldValue);
            const rec = new MutationRecord(MR_KEY, Object.assign({ type, target }, extra, keepOld ? {} : { oldValue: null }));
            apush(mo._records, rec);
            break;
        }
    }
    if (!moQueued) { moQueued = true; Promise.resolve().then(deliverMutations); }
}
function deliverMutations() {
    moQueued = false;
    for (const mo of observers.slice()) { mo._targets = mo._targets.filter(x => !x.source); const r = mo.takeRecords(); if (r.length) { try { mo._cb.call(mo, r, mo); } catch (e) { report(e); } } }
}
