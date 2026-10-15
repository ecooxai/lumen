// Native contacts keep their original DOM target, even across layout changes.
// Return flags to the host: prevent scroll=1, suppress mouse=2, suppress tap=4,
// native pointer sequence delivered=8. These are internal, not event properties.
const nativeContacts = new Map();
let nativePointerId = 2, nativeTap = null;
function touchList(items) {
    const list = items.slice();
    def(list, 'item', index => list[index] || null);
    return list;
}
function contactTouch(c, ended = false) {
    return { identifier: c.id, target: c.target, clientX: c.x, clientY: c.y,
        screenX: c.x, screenY: c.y, pageX: c.x + G.scrollX, pageY: c.y + G.scrollY,
        radiusX: 1, radiusY: 1, rotationAngle: 0, force: ended ? 0 : c.pressure };
}
function contactPointer(c, type) {
    const released = /(?:up|cancel|out|leave)$/.test(type);
    return dispatch(c.target, new PointerEvent(type, {
        bubbles: type !== 'pointerenter' && type !== 'pointerleave',
        cancelable: /(?:down|move|up|over|out)$/.test(type),
        composed: type !== 'pointerenter' && type !== 'pointerleave', view: G,
        pointerId: c.id, pointerType: 'touch', isPrimary: c.primary,
        clientX: c.x, clientY: c.y, screenX: c.x, screenY: c.y,
        button: type === 'pointermove' ? -1 : 0, buttons: released ? 0 : 1,
        pressure: released ? 0 : c.pressure,
    }), true);
}
function contactEndPointer(c, type) {
    if (c.pointerEnded) return;
    contactPointer(c, type);
    contactPointer(c, 'pointerout');
    contactPointer(c, 'pointerleave');
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
            nativeContacts.delete(id);
            contactEndPointer(c, 'pointercancel');
            contactEvent(c, 'touchcancel', true);
        }
        return 0;
    }
    if (phase === 'start') {
        if (!target || nativeContacts.has(key)) return 0;
        nativeTap = null;
        const c = { target, x, y, pressure, id: nativePointerId++,
            primary: nativeContacts.size === 0, flags: 8, scrolling: false, pointerEnded: false };
        nativeContacts.set(key, c);
        contactPointer(c, 'pointerover');
        contactPointer(c, 'pointerenter');
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
        nativeContacts.delete(key);
        contactEndPointer(c, 'pointerup');
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
