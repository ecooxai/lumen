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

const DOMEX = { IndexSizeError: 1, HierarchyRequestError: 3, WrongDocumentError: 4, InvalidCharacterError: 5, NoModificationAllowedError: 7, NotFoundError: 8, NotSupportedError: 9, InUseAttributeError: 10, InvalidStateError: 11, SyntaxError: 12, InvalidModificationError: 13, NamespaceError: 14, InvalidAccessError: 15, TypeMismatchError: 17, SecurityError: 18, NetworkError: 19, AbortError: 20, URLMismatchError: 21, QuotaExceededError: 22, TimeoutError: 23, InvalidNodeTypeError: 24, DataCloneError: 25 };
class DOMException extends Error {
    constructor(message = '', name = 'Error') { super(message); def(this, 'name', name); }
    get code() { return DOMEX[this.name] || 0; }
}
['INDEX_SIZE_ERR', 'DOMSTRING_SIZE_ERR', 'HIERARCHY_REQUEST_ERR', 'WRONG_DOCUMENT_ERR', 'INVALID_CHARACTER_ERR', 'NO_DATA_ALLOWED_ERR', 'NO_MODIFICATION_ALLOWED_ERR', 'NOT_FOUND_ERR', 'NOT_SUPPORTED_ERR', 'INUSE_ATTRIBUTE_ERR', 'INVALID_STATE_ERR', 'SYNTAX_ERR', 'INVALID_MODIFICATION_ERR', 'NAMESPACE_ERR', 'INVALID_ACCESS_ERR', 'VALIDATION_ERR', 'TYPE_MISMATCH_ERR', 'SECURITY_ERR', 'NETWORK_ERR', 'ABORT_ERR', 'URL_MISMATCH_ERR', 'QUOTA_EXCEEDED_ERR', 'TIMEOUT_ERR', 'INVALID_NODE_TYPE_ERR', 'DATA_CLONE_ERR'].forEach((k, i) => { for (const o of [DOMException, DOMException.prototype]) Object.defineProperty(o, k, { value: i + 1, enumerable: true }); });
class DOMRectReadOnly {
    constructor(x = 0, y = 0, w = 0, h = 0) { this.x = x; this.y = y; this.width = w; this.height = h; }
    get left() { return Math.min(this.x, this.x + this.width); } get right() { return Math.max(this.x, this.x + this.width); }
    get top() { return Math.min(this.y, this.y + this.height); } get bottom() { return Math.max(this.y, this.y + this.height); }
    toJSON() { const { x, y, width, height, top, right, bottom, left } = this; return { x, y, width, height, top, right, bottom, left }; }
    static fromRect(r = {}) { return new this(r.x, r.y, r.width, r.height); }
}
class DOMRect extends DOMRectReadOnly {}

class Event {
    constructor(type, init) {
        if (arguments.length < 1) throw new TypeError("Failed to construct 'Event': 1 argument required");
        init = init || {};
        def(this, '_type', String(type)); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed;
        this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.eventPhase = 0; this.timeStamp = N.now(); this.isTrusted = false;
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
    initEvent(type, bubbles, cancelable) { this._type = String(type); this.bubbles = !!bubbles; this.cancelable = !!cancelable; }
}
Object.assign(Event, { NONE: 0, CAPTURING_PHASE: 1, AT_TARGET: 2, BUBBLING_PHASE: 3 });
class CustomEvent extends Event { constructor(t, i) { super(t, i); this.detail = i && i.detail !== undefined ? i.detail : null; } initCustomEvent(t, b, c, d) { this.initEvent(t, b, c); this.detail = d; } }
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
class ErrorEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.message = i.message || ''; this.filename = i.filename || ''; this.lineno = i.lineno || 0; this.colno = i.colno || 0; this.error = i.error; } }
class ProgressEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.lengthComputable = !!i.lengthComputable; this.loaded = i.loaded || 0; this.total = i.total || 0; } }
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data; this.origin = i.origin || ''; this.lastEventId = i.lastEventId || ''; this.source = i.source || null; this.ports = i.ports || []; } }
class PopStateEvent extends Event { constructor(t, i) { super(t, i); this.state = i && i.state !== undefined ? i.state : null; } }
class HashChangeEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.oldURL = i.oldURL || ''; this.newURL = i.newURL || ''; } }
class PageTransitionEvent extends Event { constructor(t, i) { super(t, i); this.persisted = !!(i && i.persisted); } }
class AnimationEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.animationName = i.animationName || ''; this.elapsedTime = i.elapsedTime || 0; } }
class TransitionEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.propertyName = i.propertyName || ''; this.elapsedTime = i.elapsedTime || 0; } }

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
        if (!(ev instanceof Event)) throw new TypeError("Failed to execute 'dispatchEvent': parameter 1 is not of type 'Event'.");
        return dispatch(this, ev);
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
function invoke(t, ev, phase) {
    ev.currentTarget = t;
    if (phase !== 1) {
        let h = null;
        try { h = t['on' + ev._type]; } catch (e) {}
        if (typeof h === 'function') {
            let r;
            const winErr = t === G && ev._type === 'error' && ev instanceof ErrorEvent;
            try { r = winErr ? h.call(t, ev.message, ev.filename, ev.lineno, ev.colno, ev.error) : h.call(t, ev); } catch (e) { report(e); }
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
        try { if (typeof l.fn === 'function') l.fn.call(t, ev); else if (l.fn && typeof l.fn.handleEvent === 'function') l.fn.handleEvent(ev); }
        catch (e) { report(e); }
        ev._passive = false;
        if (ev._imm) break;
    }
    ev._type = type;
}
function dispatch(target, ev, trusted) {
    ev.target = target; if (trusted) ev.isTrusted = true;
    ev._stop = ev._imm = false;
    const path = [];
    for (let t = parentForEvent(target, ev); t; t = parentForEvent(t, ev)) path.push(t);
    ev._path = [target].concat(path);
    ev.eventPhase = 1;
    for (let i = path.length - 1; i >= 0 && !ev._stop; i--) invoke(path[i], ev, 1);
    if (!ev._stop) { ev.eventPhase = 2; invoke(target, ev, 2); }
    if (ev.bubbles) { ev.eventPhase = 3; for (const t of path) { if (ev._stop) break; invoke(t, ev, 3); } }
    ev.eventPhase = 0; ev.currentTarget = null;
    return !ev.defaultPrevented;
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
    for (const mo of observers) {
        for (const { t, o } of mo._targets) {
            if (!(t === target || (o.subtree && N.contains(t, target)))) continue;
            if (type === 'childList' && !o.childList) continue;
            if (type === 'attributes' && (!o.attributes || (o.attributeFilter && !o.attributeFilter.includes(extra.attributeName)))) continue;
            if (type === 'characterData' && !o.characterData) continue;
            const rec = new MutationRecord(Object.assign({ type, target }, extra));
            if (!((type === 'attributes' && o.attributeOldValue) || (type === 'characterData' && o.characterDataOldValue))) rec.oldValue = null;
            mo._records.push(rec);
            break;
        }
    }
    if (!moQueued) { moQueued = true; Promise.resolve().then(deliverMutations); }
}
function deliverMutations() {
    moQueued = false;
    for (const mo of observers.slice()) { const r = mo.takeRecords(); if (r.length) { try { mo._cb.call(mo, r, mo); } catch (e) { report(e); } } }
}
