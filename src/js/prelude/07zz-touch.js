// Native contacts keep their original DOM target, even across layout changes.
// Return flags to the host: prevent scroll=1, suppress mouse=2, suppress tap=4,
// native pointer sequence delivered=8. These are internal, not event properties.
const nativeContacts = new Map();
let nativePointerId = 2, nativeTap = null;
class Touch {
    constructor(i = {}) {
        if (!i || !(i.target instanceof EventTarget)) throw new TypeError("Failed to construct 'Touch': target is required");
        this.identifier = +i.identifier || 0; this.target = i.target;
        this.clientX = +i.clientX || 0; this.clientY = +i.clientY || 0; this.screenX = +i.screenX || 0; this.screenY = +i.screenY || 0;
        this.pageX = i.pageX ?? this.clientX + G.scrollX; this.pageY = i.pageY ?? this.clientY + G.scrollY;
        this.radiusX = +i.radiusX || 0; this.radiusY = +i.radiusY || 0; this.rotationAngle = +i.rotationAngle || 0; this.force = +i.force || 0;
        this.altitudeAngle = i.altitudeAngle ?? Math.PI / 2; this.azimuthAngle = +i.azimuthAngle || 0; this.touchType = i.touchType || 'direct';
    }
}
function touchList(items) {
    const list = items.slice();
    def(list, 'item', index => list[index] || null);
    return list;
}
function contactTouch(c, ended = false) {
    return new Touch({ identifier: c.id, target: c.target, clientX: c.x, clientY: c.y, screenX: c.x, screenY: c.y,
        radiusX: 11.5, radiusY: 11.5, force: ended ? 0 : c.pressure });
}
/* Pointer capture: a touch is implicitly captured by its pointerdown target. setPointerCapture and
   releasePointerCapture set the pending target; it takes effect, with got/lostpointercapture, before
   the next pointer event. Without capture, events go to the element under the contact. */
function pointerContact(id) {
    for (const c of nativeContacts.values()) if (c.id === id && !c.pointerEnded) return c;
    return null;
}
const mouseCaptures = new Map();   /* the mouse (pointerId 1) has no contact; its capture is only recorded */
function capturePointer(el, id, on) {
    id = +id;
    const c = pointerContact(id);
    if (!c) {
        if (id !== 1) { if (on) throw new DOMException('No active pointer with the given id is found.', 'NotFoundError'); return; }
        if (on) mouseCaptures.set(id, el); else if (mouseCaptures.get(id) === el) mouseCaptures.delete(id);
        return;
    }
    if (on) {
        if (!el.isConnected) throw new DOMException('The element is not connected.', 'InvalidStateError');
        c.pending = el;
    } else if (c.pending === el) c.pending = null;
}
function hasCapture(el, id) {
    id = +id;
    const c = pointerContact(id);
    return c ? c.pending === el : mouseCaptures.get(id) === el;
}
function pointerInit(c, type) {
    const released = /(?:up|cancel|out|leave)$/.test(type), move = type === 'pointermove';
    return { bubbles: type !== 'pointerenter' && type !== 'pointerleave',
        cancelable: /(?:down|move|up|over|out)$/.test(type),
        composed: type !== 'pointerenter' && type !== 'pointerleave', view: G,
        pointerId: c.id, pointerType: 'touch', isPrimary: c.primary,
        clientX: c.x, clientY: c.y, screenX: c.x, screenY: c.y,
        movementX: move ? c.x - c.px : 0, movementY: move ? c.y - c.py : 0,
        width: 23, height: 23,
        button: move ? -1 : 0, buttons: released ? 0 : 1,
        pressure: released ? 0 : c.pressure };
}
const elementPath = n => { const r = []; for (; n && n.nodeType === 1; n = n.parentNode) r.push(n); return r; };
function pointerAt(c, type, target) {
    return dispatch(target, new PointerEvent(type, pointerInit(c, type)), true);
}
function captureUpdate(c) {
    if (c.pending === c.capture) return;
    const old = c.capture;
    c.capture = c.pending;
    if (old && old.isConnected) pointerAt(c, 'lostpointercapture', old);
    if (c.capture) pointerAt(c, 'gotpointercapture', c.capture);
}
/* the target of c's next pointer event: its capture, else what is under it, with over/out/enter/leave on a change */
function pointerTarget(c) {
    captureUpdate(c);
    let t = c.capture;
    if (!t) {
        t = document.elementFromPoint(c.x, c.y) || c.over;
        if (t && t !== c.over) {
            const from = c.over, old = elementPath(from), now = elementPath(t);
            if (from) { pointerAt(c, 'pointerout', from); for (const n of old) { if (now.includes(n)) break; pointerAt(c, 'pointerleave', n); } }
            pointerAt(c, 'pointerover', t);
            for (const n of now.filter(n => !old.includes(n)).reverse()) pointerAt(c, 'pointerenter', n);
            c.over = t;
        }
    }
    return t || c.target;
}
function contactPointer(c, type) {
    const t = type === 'pointerover' || type === 'pointerdown' ? c.target : pointerTarget(c);
    const ok = pointerAt(c, type, t);
    c.px = c.x; c.py = c.y;
    return ok;
}
function contactEndPointer(c, type) {
    if (c.pointerEnded) return;
    contactPointer(c, type);
    const t = c.capture || c.over || c.target;
    c.pending = null; captureUpdate(c);
    pointerAt(c, 'pointerout', t);
    for (const n of elementPath(t)) pointerAt(c, 'pointerleave', n);
    c.pointerEnded = true;
}
function contactEvent(c, type, ended = false) {
    const touches = Array.from(nativeContacts.values(), contact => contactTouch(contact));
    const ev = new TouchEvent(type, { bubbles: true, composed: true,
        cancelable: type !== 'touchcancel' && !c.scrolling, view: G,
        touches: touchList(touches),
        targetTouches: touchList(touches.filter(t => t.target === c.target)),
        changedTouches: touchList([contactTouch(c, ended)]),
    });
    return dispatch(c.target, ev, true);
}
function nativeTouch(target, phase, key, x, y, pressure) {
    if (phase === 'reset' || phase === 'cancel') {
        nativeTap = null;
        for (const [id, c] of nativeContacts) {
            contactEndPointer(c, 'pointercancel');
            nativeContacts.delete(id);
            contactEvent(c, 'touchcancel', true);
        }
        return 0;
    }
    if (phase === 'start') {
        if (!target || nativeContacts.has(key)) return 0;
        nativeTap = null;
        const c = { target, x, y, px: x, py: y, pressure, id: nativePointerId++,
            primary: nativeContacts.size === 0, flags: 8, scrolling: false, pointerEnded: false,
            capture: null, pending: target, over: target };
        nativeContacts.set(key, c);
        contactPointer(c, 'pointerover');
        for (const n of elementPath(target).reverse()) pointerAt(c, 'pointerenter', n);
        if (!contactPointer(c, 'pointerdown')) c.flags |= 2;
        if (!contactEvent(c, 'touchstart')) c.flags |= 7;
        return c.flags;
    }
    const c = nativeContacts.get(key);
    if (!c) return 0;
    c.x = x; c.y = y; c.pressure = pressure;
    if (phase === 'scroll') {
        c.scrolling = true;
        c.flags |= 6;
        contactEndPointer(c, 'pointercancel');
    } else if (phase === 'move') {
        if (!c.pointerEnded) contactPointer(c, 'pointermove');
        if (!contactEvent(c, 'touchmove')) c.flags |= 7;
    } else if (phase === 'end') {
        contactEndPointer(c, 'pointerup');   /* still active, and captured, during pointerup */
        nativeContacts.delete(key);
        if (!contactEvent(c, 'touchend', true)) c.flags |= 6;
        nativeTap = c.primary && !(c.flags & 4) ? c : null;
    }
    return c.flags;
}
function nativeTouchInit(type, init) {
    const c = nativeTap;
    if (!c) return init;
    return Object.assign({}, init, { pointerId: c.id, pointerType: 'touch',
        isPrimary: c.primary, pressure: 0,
        sourceCapabilities: { firesTouchEvents: true } });
}
