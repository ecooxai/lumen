const camelToKebab = (p) => p.startsWith('--') ? p : p === 'cssFloat' ? 'float' : p.replace(/^(webkit|moz|ms)(?=[A-Z])/, '-$1').replace(/[A-Z]/g, c => '-' + c.toLowerCase());
function parseDecls(s) {
    const m = new Map(); if (!s) return m;
    let depth = 0, q = 0, start = 0; const parts = [];
    for (let i = 0; i < s.length; i++) { const c = s[i]; if (q) { if (c === q) q = 0; } else if (c === '"' || c === "'") q = c; else if (c === '(') depth++; else if (c === ')') depth--; else if (c === ';' && !depth) { parts.push(s.slice(start, i)); start = i + 1; } }
    parts.push(s.slice(start));
    for (const p of parts) { const i = p.indexOf(':'); if (i < 0) continue; const k = p.slice(0, i).trim(); let v = p.slice(i + 1).trim(); if (!k) continue; let imp = ''; if (/!important\s*$/i.test(v)) { v = v.replace(/\s*!important\s*$/i, ''); imp = 'important'; } m.set(k.startsWith('--') ? k : k.toLowerCase(), [v, imp]); }
    return m;
}
const serDecls = (m) => [...m].map(([k, [v, i]]) => k + ': ' + v + (i ? ' !important' : '') + ';').join(' ');
class CSSStyleDeclaration {
    constructor(el, ro) { def(this, '_el', el); def(this, '_ro', ro); }
    _m() { return parseDecls(this._el ? N.attr(this._el, 'style') : ''); }
    get cssText() { return serDecls(this._m()); } set cssText(v) { this._el.setAttribute('style', String(v)); }
    get length() { return this._m().size; }
    item(i) { return [...this._m().keys()][i] || ''; }
    getPropertyValue(p) { const e = this._m().get(camelToKebab(String(p))); return e ? e[0] : ''; }
    getPropertyPriority(p) { const e = this._m().get(camelToKebab(String(p))); return e ? e[1] : ''; }
    setProperty(p, v, pri) { p = camelToKebab(String(p)); const m = this._m(); if (v == null || v === '') m.delete(p); else m.set(p, [String(v), pri ? 'important' : '']); this._el.setAttribute('style', serDecls(m)); }
    removeProperty(p) { p = camelToKebab(String(p)); const m = this._m(); const e = m.get(p); if (e) { m.delete(p); this._el.setAttribute('style', serDecls(m)); } return e ? e[0] : ''; }
}
const styleHandler = {
    get(t, p) { if (typeof p !== 'string' || p in t) return Reflect.get(t, p, t); if (/^\d+$/.test(p)) return t.item(+p); return t.getPropertyValue(p); },
    set(t, p, v) { if (typeof p !== 'string' || p in t) Reflect.set(t, p, v, t); else t.setProperty(p, v); return true; },
    has(t, p) { return p in t || typeof p === 'string'; },
};
class ComputedStyleDeclaration extends CSSStyleDeclaration {
    getPropertyValue(p) { const v = N.computed(this._el, camelToKebab(String(p))); return v == null ? '' : v; }
    get cssText() { return ''; } get length() { return 0; }
    setProperty() { throw new DOMException('These styles are computed, and therefore read-only.', 'NoModificationAllowedError'); }
}
const computedHandler = { get(t, p) { if (typeof p !== 'string' || p in t) return Reflect.get(t, p, t); return t.getPropertyValue(p); }, set() { return true; } };

class Element extends Node {}
methods(Element.prototype, ParentNode); methods(Element.prototype, ChildNode);
const lcName = (el, n) => { n = String(n); return N.ns(el) === 0 ? n.toLowerCase() : n; };
const NSURI = ['http://www.w3.org/1999/xhtml', 'http://www.w3.org/2000/svg', 'http://www.w3.org/1998/Math/MathML'];
const XML_NS = 'http://www.w3.org/XML/1998/namespace', XMLNS_NS = 'http://www.w3.org/2000/xmlns/';
const validLocalName = n => /^[A-Za-z][^\t\n\f\r \/>\x00]*$/.test(n) || /^[:_\u0080-\u{10FFFF}][A-Za-z0-9\-.:_\u0080-\u{10FFFF}]*$/u.test(n);
const asciiLower = s => s.replace(/[A-Z]+/g, c => c.toLowerCase()), asciiUpper = s => s.replace(/[a-z]+/g, c => c.toUpperCase());
methods(Element.prototype, {
    get tagName() { let ln = this.localName; if (typeof ln !== 'string') ln = String(N.name(this)); const q = this.__pfx ? this.__pfx + ':' + ln : ln; return this.namespaceURI === NSURI[0] ? asciiUpper(q) : q; },
    get localName() { return this.__ln ?? N.name(this); }, get namespaceURI() { return this.__nsu !== undefined ? this.__nsu : N.ns(this) === 3 ? null : NSURI[N.ns(this)] || NSURI[0]; }, get prefix() { return this.__pfx ?? null; },
    get id() { return N.attr(this, 'id') ?? ''; }, set id(v) { this.setAttribute('id', v); },
    get className() { return N.attr(this, 'class') ?? ''; }, set className(v) { this.setAttribute('class', v); },
    get classList() { if (!this.__cl) def(this, '__cl', new DOMTokenList(this, 'class')); return this.__cl; }, set classList(v) { this.setAttribute('class', v); },
    get slot() { return N.attr(this, 'slot') ?? ''; }, set slot(v) { this.setAttribute('slot', v); },
    get attributes() { return new NamedNodeMap(this); },
    getAttribute(n) { return N.attr(this, lcName(this, n)); },
    getAttributeNS(ns, n) { return N.attr(this, String(n)); },
    setAttribute(n, v) {
        n = lcName(this, n); if (!/^[^\s"'>\/=\x00-\x1f]+$/.test(n)) throw new DOMException(`'${n}' is not a valid attribute name.`, 'InvalidCharacterError');
        const old = N.attr(this, n); v = String(v);
        N.setAttr(this, n, v);
        notify('attributes', this, { attributeName: n, oldValue: old });
        ceAttr(this, n, old, v);
    },
    setAttributeNS(ns, n, v) { n = String(n); const i = n.indexOf(':'); this.setAttribute(i >= 0 ? n.slice(i + 1) : n, v); },
    removeAttribute(n) { n = lcName(this, n); const old = N.attr(this, n); if (old == null) return; N.rmAttr(this, n); notify('attributes', this, { attributeName: n, oldValue: old }); ceAttr(this, n, old, null); },
    removeAttributeNS(ns, n) { this.removeAttribute(n); },
    hasAttribute(n) { return N.attr(this, lcName(this, n)) != null; }, hasAttributeNS(ns, n) { return N.attr(this, String(n)) != null; },
    hasAttributes() { return N.attrs(this).length > 0; },
    toggleAttribute(n, force) { const has = this.hasAttribute(n); if (force === undefined ? has : !force) { if (has) this.removeAttribute(n); return false; } if (!has) this.setAttribute(n, ''); return true; },
    getAttributeNames() { const a = N.attrs(this); const r = []; for (let i = 0; i < a.length; i += 2) r.push(a[i]); return r; },
    getAttributeNode(n) { return this.hasAttribute(n) ? new Attr(this, lcName(this, n)) : null; },
    getAttributeNodeNS(ns, n) { return this.getAttributeNode(n); },
    setAttributeNode(a) { const old = this.getAttributeNode(a.name); this.setAttribute(a.name, a.value); def(a, '_el', this); return old; },
    setAttributeNodeNS(a) { return this.setAttributeNode(a); },
    removeAttributeNode(a) { if (!a || !this.hasAttribute(a.name)) throw new DOMException("Failed to execute 'removeAttributeNode' on 'Element': The node provided is owned by another element.", 'NotFoundError'); const v = a.value; this.removeAttribute(a.name); const d = new Attr(null, a.name); Object.defineProperty(d, 'value', { value: v, writable: true }); return d; },
    get innerHTML() { return N.html(this, false); }, set innerHTML(v) { setInner(this, v == null ? '' : String(v)); },
    get outerHTML() { return N.html(this, true); },
    set outerHTML(v) { const p = N.parent(this); if (!p) return; insertNode(p, N.parseFrag(p, String(v)), this); removeNode(this); },
    insertAdjacentHTML(pos, html) { const ctx = /^(beforebegin|afterend)$/i.test(pos) ? N.parent(this) : this; if (ctx) this.insertAdjacentElement(pos, N.parseFrag(ctx, String(html))); },
    insertAdjacentElement(pos, el) {
        switch (String(pos).toLowerCase()) {
        case 'beforebegin': { const p = N.parent(this); if (p) insertNode(p, el, this); break; }
        case 'afterbegin': insertNode(this, el, N.first(this)); break;
        case 'beforeend': insertNode(this, el, null); break;
        case 'afterend': { const p = N.parent(this); if (p) insertNode(p, el, N.next(this)); break; }
        default: throw new DOMException('Invalid position', 'SyntaxError');
        }
        return el;
    },
    insertAdjacentText(pos, t) { this.insertAdjacentElement(pos, N.textNode(String(t))); },
    matches(s) { return N.matches(this, String(s)); }, webkitMatchesSelector(s) { return N.matches(this, String(s)); },
    closest(s) { s = String(s); for (let e = this; e && N.type(e) === 1; e = N.parent(e)) if (N.matches(e, s)) return e; return null; },
    getBoundingClientRect() { const r = N.rect(this); if (!r) return new DOMRect(); const v = N.viewport(); return new DOMRect(r[0] - v[2], r[1] - v[3], r[2], r[3]); },
    getClientRects() { return N.rect(this) ? [this.getBoundingClientRect()] : []; },
    get clientWidth() { if (this === document.documentElement) return N.viewport()[0]; const r = N.rect(this); return r ? Math.round(r[2]) : 0; },
    get clientHeight() { if (this === document.documentElement) return N.viewport()[1]; const r = N.rect(this); return r ? Math.round(r[3]) : 0; },
    get clientTop() { return 0; }, get clientLeft() { return 0; },
    get scrollWidth() { return this.clientWidth; }, get scrollHeight() { const r = N.rect(this); return r ? Math.round(r[3]) : 0; },
    get scrollTop() { return this === document.documentElement || this === document.body ? N.viewport()[3] : (this.__st || 0); },
    set scrollTop(v) { if (this === document.documentElement || this === document.body) N.scrollTo(N.viewport()[2], +v || 0); else def(this, '__st', +v || 0); },
    get scrollLeft() { return this.__sl || 0; }, set scrollLeft(v) { def(this, '__sl', +v || 0); },
    scrollTo() {}, scrollBy() {}, scroll() {},
    scrollIntoView() { const r = N.rect(this); if (r) N.scrollTo(0, r[1]); }, scrollIntoViewIfNeeded() { this.scrollIntoView(); },
    attachShadow(init) {
        if (this.__shadow) throw new DOMException('Shadow root cannot be created on a host which already hosts a shadow tree.', 'NotSupportedError');
        const sr = N.attachShadow(this); Object.setPrototypeOf(sr, ShadowRoot.prototype);
        def(sr, '__mode', (init && init.mode) || 'open'); def(this, '__shadow', sr);
        return sr;
    },
    get shadowRoot() { return this.__shadow && this.__shadow.__mode === 'open' ? this.__shadow : null; },
    get assignedSlot() { return null; },
    requestFullscreen() { return Promise.reject(new DOMException('Fullscreen is not supported yet', 'NotSupportedError')); },
    requestPointerLock() {}, setPointerCapture() {}, releasePointerCapture() {}, hasPointerCapture() { return false; },
    animate(kf, o) { return new Animation(this, kf, o); }, getAnimations() { return []; },
    checkVisibility() { return !!N.rect(this); },
});
function setInner(el, html) {
    const removed = kids(el);
    for (const r of removed) ceConnected(r, false);
    N.setHTML(el, html);
    const added = kids(el);
    if (removed.length || added.length) notify('childList', el, { removedNodes: removed, addedNodes: added });
    for (const a of added) ceConnected(a, true);
}
const reflectStr = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); acc(p, n, function () { return N.attr(this, a) ?? ''; }, function (v) { this.setAttribute(a, v); }); } };
const reflectBool = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); acc(p, n, function () { return N.attr(this, a) != null; }, function (v) { if (v) this.setAttribute(a, ''); else this.removeAttribute(a); }); } };
const reflectUrl = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); acc(p, n, function () { const v = N.attr(this, a); if (v == null) return ''; const r = N.urlParse(v, document.baseURI); return r ? r[0] : v; }, function (v) { this.setAttribute(a, v); }); } };
const reflectInt = (p, d, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); acc(p, n, function () { const v = parseInt(N.attr(this, a), 10); return isNaN(v) ? d : v; }, function (v) { this.setAttribute(a, String(v | 0)); }); } };
const EVENT_HANDLERS = 'abort animationend animationiteration animationstart auxclick beforeinput blur cancel canplay canplaythrough change click close contextmenu copy cut dblclick drag dragend dragenter dragleave dragover dragstart drop durationchange emptied ended error focus focusin focusout input invalid keydown keypress keyup load loadeddata loadedmetadata loadstart mousedown mouseenter mouseleave mousemove mouseout mouseover mouseup paste pause play playing pointercancel pointerdown pointerenter pointerleave pointermove pointerout pointerover pointerup progress ratechange reset resize scroll seeked seeking select selectionchange selectstart stalled submit suspend timeupdate toggle touchcancel touchend touchmove touchstart transitionend volumechange waiting wheel'.split(' ');
function compileHandler(el, k, src) {
    const c = el.__hc || (def(el, '__hc', {}), el.__hc);
    if (c[k] && c[k].src === src) return c[k].fn;
    let fn = null;
    try { fn = new Function('event', src); } catch (e) { report(e); }
    c[k] = { src, fn };
    return fn;
}
function installHandlers(proto, attrBacked, list = EVENT_HANDLERS) {
    for (const t of list) {
        const k = 'on' + t;
        acc(proto, k, function () {
            const own = this.__on && this.__on[k];
            if (own !== undefined) return own;
            if (attrBacked) { const src = N.attr(this, k); if (src != null) return compileHandler(this, k, src); }
            return null;
        }, function (v) { if (!this.__on) def(this, '__on', {}); this.__on[k] = typeof v === 'function' ? v : null; });
    }
}
const makeStyle = (el) => new Proxy(new CSSStyleDeclaration(el), styleHandler);
class HTMLElement extends Element { constructor() { return ceConstruct(new.target); } }
reflectStr(HTMLElement.prototype, 'title', 'lang', 'accessKey', 'nonce', 'dir');
reflectBool(HTMLElement.prototype, 'hidden', 'inert', 'autofocus');
installHandlers(HTMLElement.prototype, true);
methods(HTMLElement.prototype, {
    get style() { if (!this.__style) def(this, '__style', makeStyle(this)); return this.__style; },
    set style(v) { this.setAttribute('style', v); },
    get dataset() {
        if (this.__ds) return this.__ds;
        const el = this, toAttr = (k) => 'data-' + k.replace(/[A-Z]/g, c => '-' + c.toLowerCase());
        const ds = new Proxy({}, {
            get(t, k) { return typeof k === 'string' ? N.attr(el, toAttr(k)) ?? undefined : undefined; },
            set(t, k, v) { el.setAttribute(toAttr(k), v); return true; },
            deleteProperty(t, k) { el.removeAttribute(toAttr(k)); return true; },
            has(t, k) { return typeof k === 'string' && N.attr(el, toAttr(k)) != null; },
            ownKeys() { return el.getAttributeNames().filter(n => n.startsWith('data-')).map(n => n.slice(5).replace(/-([a-z])/g, (m, c) => c.toUpperCase())); },
            getOwnPropertyDescriptor(t, k) { const v = N.attr(el, toAttr(k)); return v == null ? undefined : { value: v, enumerable: true, configurable: true, writable: true }; },
        });
        def(this, '__ds', ds); return ds;
    },
    get innerText() { return N.text(this); }, set innerText(v) { this.textContent = v; }, get outerText() { return N.text(this); },
    get tabIndex() { const v = parseInt(N.attr(this, 'tabindex'), 10); return isNaN(v) ? (/^(a|button|input|select|textarea)$/.test(N.name(this)) ? 0 : -1) : v; },
    set tabIndex(v) { this.setAttribute('tabindex', String(v | 0)); },
    get offsetParent() { for (let p = N.parent(this); p && N.type(p) === 1; p = N.parent(p)) { if (p === document.body) return p; const pos = N.computed(p, 'position'); if (pos && pos !== 'static') return p; } return null; },
    get offsetTop() { const r = N.rect(this); if (!r) return 0; const op = this.offsetParent; const pr = op && op !== document.body ? N.rect(op) : null; return Math.round(r[1] - (pr ? pr[1] : 0)); },
    get offsetLeft() { const r = N.rect(this); if (!r) return 0; const op = this.offsetParent; const pr = op && op !== document.body ? N.rect(op) : null; return Math.round(r[0] - (pr ? pr[0] : 0)); },
    get offsetWidth() { const r = N.rect(this); return r ? Math.round(r[2]) : 0; },
    get offsetHeight() { const r = N.rect(this); return r ? Math.round(r[3]) : 0; },
    get isContentEditable() { const v = N.attr(this, 'contenteditable'); return v === '' || v === 'true'; },
    get contentEditable() { return N.attr(this, 'contenteditable') ?? 'inherit'; }, set contentEditable(v) { this.setAttribute('contenteditable', v); },
    get draggable() { return N.attr(this, 'draggable') === 'true'; }, set draggable(v) { this.setAttribute('draggable', String(!!v)); },
    focus() { const o = N.active(); if (o === this) return; N.focus(this); if (N.active() !== this) return; if (o) o.dispatchEvent(new FocusEvent('focusout', { bubbles: true, composed: true, relatedTarget: this })); this.dispatchEvent(new FocusEvent('focusin', { bubbles: true, composed: true, relatedTarget: o })); }, blur() { if (N.active() !== this) return; N.focus(null); this.dispatchEvent(new FocusEvent('focusout', { bubbles: true, composed: true, relatedTarget: null })); },
    click() { if (this.disabled) return; const ev = new MouseEvent('click', { bubbles: true, cancelable: true, composed: true, view: G, detail: 1 }); if (dispatch(this, ev)) activate(this, ev); },
    attachInternals() { return { setFormValue() {}, setValidity() {}, checkValidity() { return true; }, reportValidity() { return true; }, states: new Set(), form: null, labels: [] }; },
    showPopover() {}, hidePopover() {}, togglePopover() {},
});
class SVGElement extends Element {}
installHandlers(SVGElement.prototype, true);
methods(SVGElement.prototype, {
    get style() { if (!this.__style) def(this, '__style', makeStyle(this)); return this.__style; },
    get dataset() { return Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'dataset').get.call(this); },
    get ownerSVGElement() { for (let p = N.parent(this); p && N.type(p) === 1; p = N.parent(p)) if (N.name(p) === 'svg') return p; return null; },
    getBBox() { const r = N.rect(this); return new DOMRect(0, 0, r ? r[2] : 0, r ? r[3] : 0); },
    focus() { N.focus(this); }, blur() {},
});
class SVGGraphicsElement extends SVGElement {}
class SVGSVGElement extends SVGGraphicsElement {}
methods(SVGSVGElement.prototype, { createSVGPoint() { return { x: 0, y: 0, matrixTransform() { return this; } }; }, createSVGMatrix() { return new DOMMatrix(); }, getScreenCTM() { return new DOMMatrix(); } });
class MathMLElement extends Element {}
