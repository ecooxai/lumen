/* The popover API (HTML "The popover attribute"): show/hide/toggle, the showing auto and hint popover lists, light
   dismiss, popovertarget invokers and toggle events. The visibility state is the native :popover-open flag, which
   disconnecting clears (the removal steps hide without events). There is no top layer: the UA sheet makes an open
   popover position:fixed. */
const popStates = new WeakMap(), popDocs = new WeakMap();
function popSt(el) {
    let s = popStates.get(el);
    if (!s) { s = { mode: null, invoker: null, prevFocus: null, busy: false, toggle: null }; popStates.set(el, s); }
    return s;
}
/* the popover attribute's state: null (no popover), 'auto', 'hint' or 'manual' */
function popState(el) {
    if (!N.isNode(el) || N.type(el) !== 1 || N.ns(el) !== 0) return null;
    return popKw(N.attr(el, 'popover'));
}
function popKw(v) { if (v == null) return null; v = asciiLower(v); return v === '' || v === 'auto' ? 'auto' : v === 'hint' ? 'hint' : 'manual'; }
/* the document's showing auto and hint popover lists; entries the removal steps hid are dropped with those above them */
function popLists(doc) {
    let L = popDocs.get(doc);
    if (!L) { L = { auto: [], hint: [], pd: null }; popDocs.set(doc, L); }
    for (const a of [L.hint, L.auto]) {
        const i = a.findIndex(e => !N.popover(e));
        if (i < 0) continue;
        for (const e of a.slice(i)) { N.popover(e, false); const s = popSt(e); s.mode = s.invoker = s.prevFocus = null; }
        a.length = i;
    }
    return L;
}
const popDoc = el => el.ownerDocument || document;
const flatParent = n => {
    if (N.type(n) === 1 && n.assignedSlot) return n.assignedSlot;
    const p = N.parent(n);
    return p && N.type(p) === 11 ? N.host(p) || null : p;
};
/* a shadow-including inclusive ancestor test */
const popContains = (a, b) => { for (let n = b; n; n = N.parent(n) || (N.type(n) === 11 ? N.host(n) : null)) if (n === a) return true; return false; };
function popValid(el, expectShowing, thr, expectedDoc, ignoreDom = false) {
    if (!ignoreDom && !popState(el)) {
        if (thr) throw new DOMException("Not supported on elements that do not have a valid value for the 'popover' attribute.", 'NotSupportedError');
        return false;
    }
    if (expectShowing !== N.popover(el)) return false;
    if (!el.isConnected || (expectedDoc && popDoc(el) !== expectedDoc) || (N.name(el) === 'dialog' && N.modal(el)) || el.matches(':fullscreen')) {
        if (thr) throw new DOMException(!el.isConnected ? 'Invalid on disconnected popover elements.' : 'Invalid on popover elements that are modal dialogs or fullscreen.', 'InvalidStateError');
        return false;
    }
    return true;
}
/* "queue a popover toggle event task": a pending one keeps its oldState and takes the new newState */
function popToggle(el, oldState, newState, source) {
    const s = popSt(el);
    if (s.toggle) { s.toggle.newState = newState; s.toggle.source = source; return; }
    s.toggle = { oldState, newState, source };
    setTimeout(() => { const init = s.toggle; s.toggle = null; dispatch(el, new ToggleEvent('toggle', init), true); }, 0);
}
function nearestOpenPopover(n) {
    for (; n; n = flatParent(n)) { const t = popState(n); if ((t === 'auto' || t === 'hint') && N.popover(n)) return n; }
    return null;
}
const POP_CMDS = new SSet(['toggle-popover', 'show-popover', 'hide-popover']);
/* the popover a popovertarget or commandfor invoker controls, else null */
function invokerTarget(n) {
    if (!N.isNode(n) || N.type(n) !== 1 || N.ns(n) !== 0) return null;
    const p = popoverTargetOf(n);
    if (p) return p;
    if (N.name(n) === 'button' && N.attr(n, 'commandfor') != null && POP_CMDS.has(n.command)) return n.commandForElement;
    return null;
}
function nearestTargetPopover(n) {
    for (; n; n = flatParent(n)) { const p = invokerTarget(n), t = p && popState(p); if ((t === 'auto' || t === 'hint') && N.popover(p)) return p; }
    return null;
}
function popPos(L, p) { if (!p) return -1; const i = L.auto.indexOf(p); if (i >= 0) return i; const j = L.hint.indexOf(p); return j >= 0 ? L.auto.length + j : -1; }
function topmostClicked(n, L) { const a = nearestOpenPopover(n), b = nearestTargetPopover(n); return popPos(L, a) > popPos(L, b) ? a : b; }
function topmostAncestor(el, list, invoker, isPopover) {
    const pos = new SMap(); list.forEach((p, i) => pos.set(p, i));
    if (isPopover) pos.set(el, list.length);
    let top = null;
    const check = (c) => {
        if (!c) return;
        let anc = null;
        for (let ok = false; !ok;) {
            anc = nearestOpenPopover(c);
            if (!anc || !pos.has(anc)) return;
            ok = !isPopover || popState(el) === 'hint' || popState(anc) === 'auto';
            if (!ok) c = flatParent(anc);
        }
        if (!top || pos.get(top) < pos.get(anc)) top = anc;
    };
    check(flatParent(el));
    check(invoker);
    return top;
}
function closeList(list, focusPrev, fire) { while (list.length) { const n = list.length; popHide(list[n - 1], focusPrev, fire, false); if (list.length === n) list.pop(); } }
function hideStackUntil(end, list, focusPrev, fire) {
    for (let repeat = true; repeat;) {
        const i = list.indexOf(end);
        if (i < 0) return;
        while (list.length > i + 1) { const n = list.length; popHide(list[n - 1], focusPrev, fire, false); if (list.length === n) list.pop(); }
        repeat = list.includes(end) && list[list.length - 1] !== end;
        if (repeat) fire = false;
    }
}
function hideAllUntil(end, focusPrev, fire) {
    const isDoc = N.type(end) === 9;
    if (!isDoc && !N.popover(end)) return;
    const L = popLists(isDoc ? end : popDoc(end));
    if (isDoc) { closeList(L.hint, focusPrev, fire); closeList(L.auto, focusPrev, fire); return; }
    if (L.hint.includes(end)) { hideStackUntil(end, L.hint, focusPrev, fire); return; }
    closeList(L.hint, focusPrev, fire);
    hideStackUntil(end, L.auto, focusPrev, fire);
}
function popFocus(el) {
    let c = el.hasAttribute('autofocus') ? el : el.querySelector('[autofocus]:not(:disabled)');
    if (!c && N.name(el) === 'dialog') c = el;
    if (c) c.focus();
}
function popShow(el, thr, invoker) {
    if (!popValid(el, false, thr, null)) return;
    const doc = popDoc(el), s = popSt(el);
    const nested = s.busy, fire = !nested;
    s.busy = true;
    try {
        if (fire && !dispatch(el, new ToggleEvent('beforetoggle', { oldState: 'closed', newState: 'open', source: invoker, cancelable: true }), true)) return;
        if (!popValid(el, false, thr, doc)) return;
        const type = popState(el);
        let L = popLists(doc), stack = null;
        if (type === 'auto') {
            closeList(L.hint, false, fire);
            hideAllUntil(topmostAncestor(el, L.auto, invoker, true) || doc, false, fire);
            stack = 'auto';
        } else if (type === 'hint') {
            const anc = topmostAncestor(el, L.hint, invoker, true);
            if (anc) hideStackUntil(anc, L.hint, false, fire);
            else closeList(L.hint, false, fire);
            stack = 'hint';
        }
        let restore = false;
        if (stack) {
            if (popState(el) !== type) { if (thr) throw new DOMException('The popover attribute changed while showing the popover.', 'InvalidStateError'); return; }
            if (!popValid(el, false, thr, doc)) return;
            L = popLists(doc);
            restore = !L.auto.length && !L.hint.length;
            L[stack].push(el);
        }
        s.mode = stack || 'manual';
        s.prevFocus = null;
        const was = N.active();
        N.popover(el, true);
        s.invoker = invoker || null;
        popFocus(el);
        if (restore) s.prevFocus = was;
        popToggle(el, 'closed', 'open', invoker || null);
    } finally { s.busy = false; }
}
function popHide(el, focusPrev, fire, thr, source = null, ignoreDom = false) {
    if (!popValid(el, true, thr, null, ignoreDom)) return;
    const s = popSt(el), doc = popDoc(el);
    const nested = s.busy;
    s.busy = true;
    if (nested) fire = false;
    try {
        if (s.mode === 'auto' || s.mode === 'hint') {
            hideAllUntil(el, focusPrev, fire);
            if (!popValid(el, true, thr, null, ignoreDom)) return;
        }
        const L = popLists(doc);
        const wasTop = L.auto[L.auto.length - 1] === el;
        s.invoker = null;
        if (fire) {
            dispatch(el, new ToggleEvent('beforetoggle', { oldState: 'open', newState: 'closed', source }), true);
            if (wasTop && L.auto[L.auto.length - 1] !== el) hideAllUntil(el, focusPrev, false);
            if (!popValid(el, true, thr, null, ignoreDom)) return;
        }
        for (const a of [L.auto, L.hint]) { const i = a.indexOf(el); if (i >= 0) a.splice(i, 1); }
        N.popover(el, false);
        s.mode = null;
        if (fire) popToggle(el, 'open', 'closed', source);
        const prev = s.prevFocus;
        s.prevFocus = null;
        if (prev && focusPrev && prev.isConnected) { const a = N.active(); if (a && popContains(el, a)) prev.focus(); }
    } finally { s.busy = false; }
}
const popSource = (o, m) => {
    const src = o == null ? undefined : o.source;
    if (src != null && !(src instanceof HTMLElement)) throw new TypeError(`Failed to execute '${m}' on 'HTMLElement': Failed to read the 'source' property from '${m === 'showPopover' ? 'ShowPopoverOptions' : 'TogglePopoverOptions'}': Failed to convert value to 'HTMLElement'.`);
    return src || null;
};
methods(HTMLElement.prototype, {
    showPopover(options = undefined) { popShow(this, true, popSource(options, 'showPopover')); },
    hidePopover() { popHide(this, true, true, true); },
    togglePopover(options = undefined) {
        let force, source = null;
        if (typeof options === 'boolean') force = options;
        else if (options != null && typeof options === 'object') { if (options.force !== undefined) force = !!options.force; source = popSource(options, 'togglePopover'); }
        else if (options !== undefined) force = !!options;
        if (N.popover(this) && !force) popHide(this, true, true, true);
        else if (force === undefined || force) popShow(this, true, source);
        else popValid(this, N.popover(this), true, null);
        return N.popover(this);
    },
});
/* "get the popover target element" */
function popoverTargetOf(n) {
    const name = N.name(n);
    if (name === 'input') { if (!['button', 'submit', 'reset', 'image'].includes(n.type)) return null; }
    else if (name !== 'button') return null;
    if (n.matches(':disabled')) return null;
    if (n.form && (n.type === 'submit' || n.type === 'image')) return null;
    const p = n.popoverTargetElement;
    return p && popState(p) ? p : null;
}
/* the popover target attribute activation behavior */
function popTargetActivate(n, ev) {
    const p = popoverTargetOf(n);
    if (!p) return;
    const t = ev && ev.target;
    if (t && N.isNode(t) && popContains(p, t) && p !== n && popContains(n, p)) return;
    const act = n.popoverTargetAction;
    if (N.popover(p)) { if (act !== 'show') popHide(p, true, true, false, n); }
    else if (act !== 'hide' && popValid(p, false, false, null)) popShow(p, false, n);
}
/* the popover attribute changing between states hides a showing popover */
function popAttrChanged(el, old, v) { if (N.popover(el) && popKw(old) !== popKw(v)) popHide(el, true, true, false, null, true); }
/* light dismiss (a trusted pointerdown and pointerup outside the topmost popovers) and Escape (a close request) */
function popUserEvent(target, ev) {
    if (!N.isNode(target)) return;
    const doc = N.type(target) === 9 ? target : popDoc(target), L = popDocs.get(doc);
    const t = ev._type;
    if (t === 'keydown') {
        if (ev.key !== 'Escape' || ev.defaultPrevented) return;
        if (L) { popLists(doc); const top = L.hint[L.hint.length - 1] || L.auto[L.auto.length - 1]; if (top) { popHide(top, true, true, false); return; } }
        const d = [...N.query(doc, 'dialog', true)].filter(x => N.modal(x)).pop();
        if (d) dialogRequestClose(d);
        return;
    }
    if (!L || (!L.auto.length && !L.hint.length)) return;
    popLists(doc);
    if (t === 'pointerdown') { L.pd = topmostClicked(target, L); return; }
    const anc = topmostClicked(target, L), same = anc === L.pd;
    L.pd = null;
    if (same) hideAllUntil(anc || doc, false, true);
}
