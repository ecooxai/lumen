const kebabCache = new Map();
const camelToKebab = (p) => {
    let r = kebabCache.get(p);
    if (r !== undefined) return r;
    r = p.startsWith('--') ? p : p === 'cssFloat' ? 'float' : p.replace(/^(webkit|moz|ms)(?=[A-Z])/, '-$1').replace(/[A-Z]/g, c => '-' + c.toLowerCase());
    if (kebabCache.size < 4096) kebabCache.set(p, r);
    return r;
};
function parseDecls(s) {
    const m = new Map(); if (!s) return m;
    let depth = 0, q = 0, start = 0; const parts = [];
    for (let i = 0; i < s.length; i++) { const c = s[i]; if (q) { if (c === q) q = 0; } else if (c === '"' || c === "'") q = c; else if (c === '(') depth++; else if (c === ')') depth--; else if (c === ';' && !depth) { parts.push(s.slice(start, i)); start = i + 1; } }
    parts.push(s.slice(start));
    for (const p of parts) { const i = p.indexOf(':'); if (i < 0) continue; const k = p.slice(0, i).trim(); let v = p.slice(i + 1).trim(); if (!k) continue; let imp = ''; if (/!important\s*$/i.test(v)) { v = v.replace(/\s*!important\s*$/i, ''); imp = 'important'; } m.set(k.startsWith('--') ? k : k.toLowerCase(), [v, imp]); }
    return m;
}
const serDecls = (m) => [...m].map(([k, [v, i]]) => k + ': ' + v + (i ? ' !important' : '') + ';').join(' ');
/* a longhand only set through a shorthand in this declaration block: its share of the shorthand's value */
const SHORTHAND_OF = { 'align-items': 'place-items', 'justify-items': 'place-items', 'align-self': 'place-self', 'justify-self': 'place-self', 'align-content': 'place-content', 'justify-content': 'place-content',
    'row-gap': 'gap', 'column-gap': 'gap', 'overflow-x': 'overflow', 'overflow-y': 'overflow', 'top': 'inset', 'right': 'inset', 'bottom': 'inset', 'left': 'inset',
    'margin-top': 'margin', 'margin-right': 'margin', 'margin-bottom': 'margin', 'margin-left': 'margin', 'padding-top': 'padding', 'padding-right': 'padding', 'padding-bottom': 'padding', 'padding-left': 'padding' };
function shorthandPart(m, p) {
    const sh = SHORTHAND_OF[p], e = sh && m.get(sh);
    if (!e) return '';
    const v = e[0].trim();
    if (/var\(/i.test(v)) return '';
    if (/^(inherit|initial|unset|revert|revert-layer)$/i.test(v)) return v;
    const t = tomSplitTop(v, ' ').filter(Boolean);
    if (sh.startsWith('place-')) {
        const first = p.startsWith('align-');
        let a = t, two = [];
        for (let i = 0; i < a.length; i++) { const w = a[i]; if ((w === 'first' || w === 'last' || w === 'safe' || w === 'unsafe' || w === 'legacy') && i + 1 < a.length) { two.push(w + ' ' + a[++i]); } else two.push(w); }
        if (two.length > 2 || !two.length) return '';
        const al = two[0].replace(/^first /, ''), ju = two.length > 1 ? two[1].replace(/^first /, '') : (sh === 'place-content' && /baseline/.test(two[0]) ? 'start' : al);
        return first ? al : ju;
    }
    if (sh === 'gap' || sh === 'overflow') { if (t.length > 2) return ''; return p === 'row-gap' || p === 'overflow-x' ? t[0] : t[t.length - 1]; }
    if (t.length > 4) return '';
    const side = ['top', 'right', 'bottom', 'left'].indexOf(p.replace(/^(margin|padding)-/, ''));
    const idx = [[0, 0, 0, 0], [0, 1, 0, 1], [0, 1, 2, 1], [0, 1, 2, 3]][t.length - 1][side];
    return t[idx];
}
/* specified-value canonicalisation: numbers as CSS serializes them, shortest 2- and 4-value shorthand forms */
const SPEC_PAIR = new Set(['gap', 'grid-gap', 'place-items', 'place-self', 'place-content', 'overflow', 'overscroll-behavior', 'border-spacing', 'scroll-snap-align', 'contain-intrinsic-size', 'margin-block', 'margin-inline', 'padding-block', 'padding-inline', 'inset-block', 'inset-inline', 'scroll-margin-block', 'scroll-margin-inline', 'scroll-padding-block', 'scroll-padding-inline', 'border-block-width', 'border-inline-width', 'border-block-style', 'border-inline-style', 'border-block-color', 'border-inline-color']);
const SPEC_BOX = new Set(['margin', 'padding', 'inset', 'border-width', 'border-style', 'border-color', 'scroll-margin', 'scroll-padding', 'border-image-outset', 'border-image-width']);
function specNormalize(p, v) {
    if (/["'\\]|url\(|var\(|env\(|attr\(|#/i.test(v)) return v;
    let t = v.trim().replace(/(^|[\s(,/*+-])([+-]?)(\d*\.\d+|\d+\.?)(e[+-]?\d+)?(?=[a-z%]*(?:$|[\s),/*]))/gi, (m, pre, sign, num, exp) => {
        const n = Number(num + (exp || ''));
        if (!isFinite(n)) return m;
        const r = String(+n.toFixed(6));
        return pre + (sign === '-' && n ? '-' : '') + r;
    }).replace(/\s+/g, ' ');
    if (/^(inherit|initial|unset|revert|revert-layer)$/i.test(t)) return t;
    if (SPEC_PAIR.has(p) || SPEC_BOX.has(p)) {
        const w = t.split(' ');
        if (w.some(x => /\(/.test(x))) return t;
        if (SPEC_PAIR.has(p) && w.length === 2 && w[0] === w[1] && !/^(first|last|safe|unsafe|legacy)$/.test(w[0])) t = w[0];
        if (SPEC_BOX.has(p) && w.length >= 2 && w.length <= 4) {
            const [a, b, c = a, d = b] = w;
            t = b === d ? (a === c ? (a === b ? a : `${a} ${b}`) : `${a} ${b} ${c}`) : `${a} ${b} ${c} ${d}`;
        }
    }
    return t;
}
/* specified values of single-colour properties: legacy colours serialize as rgb()/rgba(), keywords lowercase;
   an unparsable colour leaves the declaration unchanged (null) */
const SPEC_COLOR_PROPS = new Set(['color', 'background-color', 'border-top-color', 'border-right-color', 'border-bottom-color', 'border-left-color', 'border-block-start-color', 'border-block-end-color',
    'border-inline-start-color', 'border-inline-end-color', 'outline-color', 'caret-color', 'column-rule-color', 'text-decoration-color', 'text-emphasis-color', 'flood-color', 'lighting-color', 'stop-color', '-webkit-text-fill-color', '-webkit-text-stroke-color']);
const specAlpha = a => { const b = Math.round(a * 255); for (const d of [2, 3]) { const t = +a.toFixed(d); if (Math.round(t * 255) === b) return String(t); } return String(+a.toFixed(6)); };
function specColor(v) {
    const t = v.replace(/\/\*[\s\S]*?\*\//g, '').trim(), low = t.toLowerCase();
    if (/^(inherit|initial|unset|revert|revert-layer)$/.test(low) || /var\(|env\(|attr\(/i.test(t)) return v;
    /* forms cssColor does not model are kept as written unless plainly empty */
    if (/\bfrom\b|calc\(|color-mix\(|color-layers\(|alpha\(|contrast-color\(|light-dark\(|device-cmyk\(|sibling-(index|count)\(|display-p3-linear/.test(low)) return /\(\s*\)/.test(low) ? null : v;
    const c = cssColor(t);
    if (!c) return null;
    if (c.current) return 'currentcolor';
    if (/\bnone\b/.test(low)) return t;
    if (/^[a-z]+$/.test(low)) return low;
    if (!c.legacy && !/^(#|rgba?\(|hsla?\(|hwb\()/.test(low)) return v;
    const [r, g, b] = (c.legacy ? c.c : colorTo(c, 'srgb')).map(x => Math.round(Math.min(1, Math.max(0, x)) * 255));
    return c.alpha >= 1 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${specAlpha(c.alpha)})`;
}
class CSSStyleDeclaration {
    constructor(el, ro) { def(this, '_el', el); def(this, '_ro', ro); def(this, '_src', null); def(this, '_map', null); }
    /* parsed declarations of the current style attribute; callers that modify take a copy */
    _m() {
        const s = this._el ? N.attr(this._el, 'style') : '';
        if (s !== this._src || !this._map) { this._map = parseDecls(s); this._src = s; }
        return this._map;
    }
    get cssText() { return serDecls(this._m()); } set cssText(v) { this._el.setAttribute('style', String(v)); }
    get length() { return this._m().size; }
    item(i) { return [...this._m().keys()][i] || ''; }
    getPropertyValue(p) { p = camelToKebab(String(p)); const m = this._m(), e = m.get(p); return e ? e[0] : shorthandPart(m, p); }
    getPropertyPriority(p) { const e = this._m().get(camelToKebab(String(p))); return e ? e[1] : ''; }
    setProperty(p, v, pri) { p = String(p); if (!p.startsWith('--')) p = camelToKebab(p); if (v != null && v !== '' && SPEC_COLOR_PROPS.has(p)) { v = specColor(String(v)); if (v === null) return; } if (v != null && v !== '' && !p.startsWith('--')) v = specNormalize(p, String(v)); const m = new Map(this._m()); if (v == null || v === '') m.delete(p); else m.set(p, [String(v), pri ? 'important' : '']); this._el.setAttribute('style', serDecls(m)); }
    removeProperty(p) { p = camelToKebab(String(p)); const m = new Map(this._m()); const e = m.get(p); if (e) { m.delete(p); this._el.setAttribute('style', serDecls(m)); } return e ? e[0] : ''; }
}
const styleHandler = {
    get(t, p) { if (typeof p !== 'string' || p in t) return Reflect.get(t, p, t); if (/^\d+$/.test(p)) return t.item(+p); return t.getPropertyValue(p); },
    set(t, p, v) { if (typeof p !== 'string' || p in t) Reflect.set(t, p, v, t); else t.setProperty(p, v); return true; },
    has(t, p) { return p in t || typeof p === 'string'; },
};
class ComputedStyleDeclaration extends CSSStyleDeclaration {
    getPropertyValue(p) { if (waDirty) waFlush(); const k = camelToKebab(String(p)); let v = N.computed(this._el, k); if (trPoll()) v = N.computed(this._el, k); return v == null ? '' : v; }
    get cssText() { return ''; } get length() { return 0; }
    setProperty() { throw new DOMException('These styles are computed, and therefore read-only.', 'NoModificationAllowedError'); }
}
/* CSS properties Chrome supports (those its WPT computed-style tests exercise), for `name in getComputedStyle(el)` */
const CSS_PROP_NAMES = new Set("-webkit-line-clamp -webkit-text-stroke -webkit-text-stroke-width -webkit-transform accent-color align-content align-items align-self alignment-baseline all anchor-name anchor-scope animation animation-composition animation-delay animation-direction animation-duration animation-fill-mode animation-iteration-count animation-name animation-play-state animation-range animation-range-end animation-range-start animation-timeline animation-timing-function appearance aspect-ratio backdrop-filter backface-visibility background-attachment background-blend-mode background-clip background-color background-image background-origin background-position background-position-x background-position-y background-repeat background-size baseline-shift baseline-source block-size border border-block-color border-block-end-color border-block-end-radius border-block-end-style border-block-end-width border-block-start-color border-block-start-radius border-block-start-style border-block-start-width border-block-style border-block-width border-bottom-color border-bottom-left-radius border-bottom-radius border-bottom-right-radius border-bottom-style border-bottom-width border-collapse border-color border-end-end-radius border-end-start-radius border-image-outset border-image-repeat border-image-slice border-image-source border-image-width border-inline-color border-inline-end-color border-inline-end-radius border-inline-end-style border-inline-end-width border-inline-start-color border-inline-start-radius border-inline-start-style border-inline-start-width border-inline-style border-inline-width border-left-color border-left-radius border-left-style border-left-width border-radius border-right-color border-right-radius border-right-style border-right-width border-shape border-spacing border-start-end-radius border-start-start-radius border-style border-top-color border-top-left-radius border-top-radius border-top-right-radius border-top-style border-top-width border-width bottom box-decoration-break box-shadow box-sizing break-after break-before break-inside caption-side caret-color caret-shape clear clip clip-path clip-rule color color-interpolation-filters color-scheme column-count column-fill column-gap column-rule column-rule-break column-rule-color column-rule-inset column-rule-inset-cap column-rule-inset-cap-end column-rule-inset-cap-start column-rule-inset-end column-rule-inset-junction column-rule-inset-junction-end column-rule-inset-junction-start column-rule-inset-start column-rule-style column-rule-visibility-items column-rule-width column-span column-width columns contain contain-intrinsic-height contain-intrinsic-size contain-intrinsic-width container container-name container-type content content-visibility corner corner-block-end-shape corner-block-start corner-block-start-shape corner-bottom-left-shape corner-bottom-right-shape corner-bottom-shape corner-inline-end-shape corner-inline-start-shape corner-left-shape corner-right-shape corner-shape corner-top corner-top-left corner-top-left-shape corner-top-right-shape corner-top-shape counter-increment counter-reset counter-set cursor cx cy direction display dominant-baseline dynamic-range-limit empty-cells field-sizing fill fill-opacity filter flex flex-basis flex-direction flex-flow flex-grow flex-line-count flex-shrink flex-wrap float flood-color flood-opacity font font-family font-feature-settings font-kerning font-language-override font-optical-sizing font-palette font-size font-size-adjust font-stretch font-style font-synthesis font-synthesis-small-caps font-synthesis-style font-synthesis-weight font-variant font-variant-alternates font-variant-caps font-variant-east-asian font-variant-emoji font-variant-ligatures font-variant-numeric font-variant-position font-variation-settings font-weight forced-color-adjust frame-sizing gap grid grid-area grid-auto-columns grid-auto-flow grid-auto-rows grid-column grid-column-end grid-column-gap grid-column-start grid-gap grid-lanes grid-lanes-direction grid-lanes-pack grid-row grid-row-end grid-row-gap grid-row-start grid-template-areas grid-template-columns grid-template-rows height hyphenate-character hyphenate-limit-chars hyphens image-orientation image-rendering initial-letter inline-size inset inset-block inset-block-end inset-block-start inset-inline inset-inline-end inset-inline-start interactivity interpolate-size isolation justify-content justify-items justify-self left letter-spacing lighting-color line-break line-height link-parameters list-style list-style-image list-style-position list-style-type margin margin-block margin-block-end margin-block-start margin-bottom margin-inline margin-inline-end margin-inline-start margin-left margin-right margin-top margin-trim mask mask-clip mask-composite mask-image mask-mode mask-origin mask-position mask-repeat mask-size mask-type math-depth max-block-size max-height max-inline-size max-width min-block-size min-height min-inline-size min-width mix-blend-mode object-fit object-position offset-anchor offset-distance offset-path offset-position offset-rotate opacity order orphans outline outline-color outline-offset outline-style outline-width overflow overflow-anchor overflow-block overflow-clip-margin overflow-inline overflow-wrap overflow-x overflow-y overlay overscroll-behavior overscroll-behavior-block overscroll-behavior-inline overscroll-behavior-x overscroll-behavior-y padding padding-block padding-block-end padding-block-start padding-bottom padding-inline padding-inline-end padding-inline-start padding-left padding-right padding-top page perspective perspective-origin place-content place-items place-self position position-anchor position-area position-try position-try-fallbacks position-try-order position-visibility print-color-adjust quotes reading-flow reading-order resize right rotate row-gap row-rule row-rule-break row-rule-color row-rule-inset row-rule-inset-cap row-rule-inset-cap-end row-rule-inset-cap-start row-rule-inset-end row-rule-inset-junction row-rule-inset-junction-end row-rule-inset-junction-start row-rule-inset-start row-rule-style row-rule-visibility-items row-rule-width ruby-align ruby-overhang ruby-position rule rule-break rule-color rule-inset rule-inset-cap rule-inset-end rule-inset-junction rule-inset-start rule-overlap rule-style rule-visibility-items rule-width rx ry scale scroll-axis-lock scroll-behavior scroll-initial-target scroll-margin scroll-margin-block scroll-margin-block-end scroll-margin-block-start scroll-margin-bottom scroll-margin-inline scroll-margin-inline-end scroll-margin-inline-start scroll-margin-left scroll-margin-right scroll-margin-top scroll-marker-group scroll-padding scroll-padding-block scroll-padding-block-end scroll-padding-block-start scroll-padding-bottom scroll-padding-inline scroll-padding-inline-end scroll-padding-inline-start scroll-padding-left scroll-padding-right scroll-padding-top scroll-snap-align scroll-snap-stop scroll-snap-type scroll-target-group scrollbar-color scrollbar-gutter scrollbar-width shape-image-threshold shape-margin shape-outside stroke stroke-dasharray stroke-dashoffset stroke-opacity stroke-width tab-size table-layout text-align text-align-last text-autospace text-box text-box-edge text-box-trim text-combine-upright text-decoration text-decoration-color text-decoration-inset text-decoration-line text-decoration-skip-ink text-decoration-skip-spaces text-decoration-style text-decoration-thickness text-emphasis text-emphasis-color text-emphasis-position text-emphasis-style text-fit text-indent text-justify text-orientation text-overflow text-shadow text-size-adjust text-spacing-trim text-transform text-underline-offset text-underline-position text-wrap text-wrap-mode text-wrap-style top transform transform-box transform-origin transform-style transition transition-behavior transition-delay transition-duration transition-property transition-timing-function translate unicode-bidi user-select vertical-align view-transition-class view-transition-name visibility white-space white-space-collapse widows width will-change word-break word-spacing word-wrap writing-mode x y z-index zoom r d fill-rule stop-color stop-opacity shape-rendering stroke-miterlimit color-interpolation color-rendering paint-order stroke-linecap stroke-linejoin text-anchor vector-effect column-height grid-template marker marker-start marker-mid marker-end mask-border mask-border-mode mask-border-outset mask-border-repeat mask-border-slice mask-border-source mask-border-width text-rendering border-image -webkit-text-fill-color -webkit-text-stroke-color -webkit-mask-image -webkit-box-orient".split(" "));
const cssPropName = p => typeof p !== 'string' ? null : p.includes('-') ? p : p.replace(/^webkit(?=[A-Z])/, '-webkit').replace(/[A-Z]/g, c => '-' + c.toLowerCase());
const computedHandler = { get(t, p) { if (typeof p !== 'string' || p in t) return Reflect.get(t, p, t); return t.getPropertyValue(p); }, set() { return true; },
    has(t, p) { return p in t || CSS_PROP_NAMES.has(cssPropName(p)); } };

class Element extends Node {}
methods(Element.prototype, ParentNode); methods(Element.prototype, ChildNode);
const lcName = (el, n) => { n = String(n); return N.ns(el) === 0 ? n.toLowerCase() : n; };
const NSURI = ['http://www.w3.org/1999/xhtml', 'http://www.w3.org/2000/svg', 'http://www.w3.org/1998/Math/MathML'];
const validLocalName = n => /^[A-Za-z][^\t\n\f\r \/>\x00]*$/.test(n) || /^[:_\u0080-\u{10FFFF}][A-Za-z0-9\-.:_\u0080-\u{10FFFF}]*$/u.test(n);
const asciiLower = s => s.replace(/[A-Z]+/g, c => c.toLowerCase()), asciiUpper = s => s.replace(/[a-z]+/g, c => c.toUpperCase());
methods(Element.prototype, {
    get tagName() { let ln = this.localName; if (typeof ln !== 'string') ln = String(N.name(this)); const q = this.__pfx ? this.__pfx + ':' + ln : ln; if (this.namespaceURI !== NSURI[0]) return q; const d = N.ownerDoc(this); return d && N.ns(d) === 3 ? q : asciiUpper(q); },
    get localName() { return this.__ln ?? N.name(this); }, get namespaceURI() { return this.__nsu !== undefined ? this.__nsu : N.ns(this) === 3 ? null : NSURI[N.ns(this)] || NSURI[0]; }, get prefix() { return this.__pfx ?? null; },
    get id() { return N.attr(this, 'id') ?? ''; }, set id(v) { this.setAttribute('id', v); },
    get className() { return N.attr(this, 'class') ?? ''; }, set className(v) { this.setAttribute('class', v); },
    get classList() { if (!this.__cl) def(this, '__cl', new DOMTokenList(TOK_KEY, this, 'class')); return this.__cl; }, set classList(v) { this.setAttribute('class', v); },
    get slot() { return N.attr(this, 'slot') ?? ''; }, set slot(v) { this.setAttribute('slot', v); },
    get attributes() { if (!this.__nnm) def(this, '__nnm', nnmOf(this)); return this.__nnm; },
    getAttribute(n) { return N.attrQ(this, lcName(this, n)); },
    getAttributeNS(ns, n) { return N.attrNS(this, ns == null || ns === '' ? null : String(ns), String(n)); },
    setAttribute(n, v) {
        n = lcName(this, n); if (!validAttrName(n)) throw new DOMException(`Failed to execute 'setAttribute' on 'Element': '${n}' is not a valid attribute name.`, 'InvalidCharacterError');
        attrSetQ(this, n, ttAttr(this, null, n, v));
    },
    setAttributeNS(ns, q, v) { const [u, p, l] = nsExtract(ns, q, true); attrChange(this, u, p, l, ttAttr(this, u, l, v)); },
    removeAttribute(n) {
        n = lcName(this, n);
        const old = N.attrQ(this, n); if (old == null) return;
        const a = attrByQName(this, n);
        if (a && (a._ns !== null || a._pfx !== null)) { attrRemove(this, a._ns, a._ln); return; }
        attrDetach(this, null, n, old);
        N.rmAttrQ(this, n);
        if (n.charCodeAt(0) === 111 && n.charCodeAt(1) === 110) handlerAttr(this, n, null);
        notify('attributes', this, { attributeName: n, attributeNamespace: null, oldValue: old });
        ceAttr(this, n, old, null, null);
    },
    removeAttributeNS(ns, n) { attrRemove(this, ns == null || ns === '' ? null : String(ns), String(n)); },
    hasAttribute(n) { return N.attrQ(this, lcName(this, n)) != null; },
    hasAttributeNS(ns, n) { return N.attrNS(this, ns == null || ns === '' ? null : String(ns), String(n)) != null; },
    hasAttributes() { return N.attrs(this).length > 0; },
    toggleAttribute(n, force) {
        n = lcName(this, n); if (!validAttrName(n)) throw new DOMException(`Failed to execute 'toggleAttribute' on 'Element': '${n}' is not a valid attribute name.`, 'InvalidCharacterError');
        if (N.attrQ(this, n) == null) { if (force === undefined || force) { attrSetQ(this, n, ''); return true; } return false; }
        if (force === undefined || !force) { this.removeAttribute(n); return false; }
        return true;
    },
    getAttributeNames() { const a = N.attrs(this); const r = []; for (let i = 0; i < a.length; i += 2) r.push(a[i]); return r; },
    getAttributeNode(n) { return attrByQName(this, lcName(this, n)); },
    getAttributeNodeNS(ns, n) { return attrByNS(this, ns == null || ns === '' ? null : String(ns), String(n)); },
    setAttributeNode(a) {
        if (!(a instanceof Attr)) throw new TypeError("Failed to execute 'setAttributeNode' on 'Element': parameter 1 is not of type 'Attr'.");
        const v = ttAttr(this, a._ns, a._ln, a.value);   /* first: the default policy may change the element's attributes */
        if (a._el && a._el !== this) throw new DOMException("Failed to execute 'setAttributeNode' on 'Element': The node provided is an attribute node that is already an attribute of another Element; attribute nodes must be explicitly cloned.", 'InUseAttributeError');
        const old = attrByNS(this, a._ns, a._ln);
        if (old === a) return a;
        if (old) attrDetach(this, a._ns, a._ln, old.value);
        attrChange(this, a._ns, a._pfx, a._ln, v);
        let m = attrNodes.get(this); if (!m) attrNodes.set(this, m = new Map());
        a._el = this; m.set(attrKey(a._ns, a._ln), a);
        return old;
    },
    setAttributeNodeNS(a) { return this.setAttributeNode(a); },
    removeAttributeNode(a) {
        if (!(a instanceof Attr)) throw new TypeError("Failed to execute 'removeAttributeNode' on 'Element': parameter 1 is not of type 'Attr'.");
        if (a._el !== this) throw new DOMException("Failed to execute 'removeAttributeNode' on 'Element': The node provided is owned by another element.", 'NotFoundError');
        attrRemove(this, a._ns, a._ln);
        return a;
    },
    /* in an XML document the fragment serialization is XML's */
    get innerHTML() { return xmlDocOf(this) ? [...this.childNodes].map(c => xmlSer(c, null, Object.assign(Object.create(null), { xml: XML_NS }))).join('') : N.html(this, false); },
    set innerHTML(v) { setInner(this, ttString(TrustedHTML, v == null ? '' : v, 'Element innerHTML')); },
    get outerHTML() { return xmlDocOf(this) ? xmlSer(this, null, Object.assign(Object.create(null), { xml: XML_NS })) : N.html(this, true); },
    set outerHTML(v) { v = ttString(TrustedHTML, v == null ? '' : v, 'Element outerHTML'); const p = N.parent(this); if (!p) return; insertNode(p, N.parseFrag(p, v), this); removeNode(this); },
    insertAdjacentHTML(pos, html) { html = ttString(TrustedHTML, html, 'Element insertAdjacentHTML'); const ctx = /^(beforebegin|afterend)$/i.test(pos) ? N.parent(this) : this; if (ctx) this.insertAdjacentElement(pos, N.parseFrag(ctx, String(html))); },
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
    matches(s) { if (!arguments.length) throw argsErr('matches', this, 1); return N.matches(this, cssPre(String(s))); },
    webkitMatchesSelector(s) { if (!arguments.length) throw argsErr('webkitMatchesSelector', this, 1); return N.matches(this, cssPre(String(s))); },
    closest(s) { if (!arguments.length) throw argsErr('closest', this, 1); s = cssPre(String(s)); for (let e = this; e && N.type(e) === 1; e = N.parent(e)) if (N.matches(e, s, this)) return e; return null; },
    getBoundingClientRect() { const r = N.vrect(this); return r ? new DOMRect(r[0], r[1], r[2], r[3]) : new DOMRect(); },
    getClientRects() { return N.rect(this) ? [this.getBoundingClientRect()] : []; },
    get clientWidth() { if (this === document.documentElement) return N.viewport()[0]; const r = N.rect(this); return r ? Math.round(r[2]) : 0; },
    get clientHeight() { if (this === document.documentElement) return N.viewport()[1]; const r = N.rect(this); return r ? Math.round(r[3]) : 0; },
    get clientTop() { return 0; }, get clientLeft() { return 0; },
    get scrollWidth() { const s = N.scrollPos(this); return s ? Math.round(s[2]) : this.clientWidth; },
    get scrollHeight() { const s = N.scrollPos(this); if (s) return Math.round(s[3]); const r = N.rect(this); return r ? Math.round(r[3]) : 0; },
    get scrollTop() { if (this === document.documentElement || this === document.body) return N.viewport()[3]; const s = N.scrollPos(this); return s ? s[1] : 0; },
    set scrollTop(v) { if (this === document.documentElement || this === document.body) N.scrollTo(N.viewport()[2], +v || 0); else this.__scrollSet(NaN, +v || 0); },
    get scrollLeft() { const s = N.scrollPos(this); return s ? s[0] : 0; }, set scrollLeft(v) { this.__scrollSet(+v || 0, NaN); },
    __scrollSet(x, y) { if (N.setScroll(this, x, y)) setTimeout(() => this.dispatchEvent(new Event('scroll'))); },
    scrollTo(x, y) { if (this === document.documentElement || this === document.body) return window.scrollTo(x, y); const o = x && typeof x === 'object' ? x : { left: x, top: y }; this.__scrollSet(o.left == null ? NaN : +o.left || 0, o.top == null ? NaN : +o.top || 0); },
    scroll(x, y) { this.scrollTo(x, y); },
    scrollBy(x, y) { const o = x && typeof x === 'object' ? x : { left: x, top: y }; this.scrollTo({ left: this.scrollLeft + (+o.left || 0), top: this.scrollTop + (+o.top || 0) }); },
    scrollIntoView() { const r = N.rect(this); if (r) N.scrollTo(0, r[1]); }, scrollIntoViewIfNeeded() { this.scrollIntoView(); },
    attachShadow(init) {
        const fail = m => new TypeError(`Failed to execute 'attachShadow' on 'Element': ${m}`);
        if (!arguments.length) throw fail('1 argument required, but only 0 present.');
        if (init === null || (typeof init !== 'object' && typeof init !== 'function')) throw fail("The provided value is not of type 'ShadowRootInit'.");
        /* the dictionary members, read in order */
        const clonable = !!init.clonable, delegatesFocus = !!init.delegatesFocus, mode = init.mode;
        if (mode === undefined) throw fail("Failed to read the 'mode' property from 'ShadowRootInit': Required member is undefined.");
        const m = String(mode);
        if (m !== 'open' && m !== 'closed') throw fail(`Failed to read the 'mode' property from 'ShadowRootInit': The provided value '${m}' is not a valid enum value of type ShadowRootMode.`);
        const rt = init.referenceTarget, refTarget = rt == null ? null : String(rt);
        const serializable = !!init.serializable, sa = init.slotAssignment === undefined ? 'named' : String(init.slotAssignment);
        if (sa !== 'named' && sa !== 'manual') throw fail(`Failed to read the 'slotAssignment' property from 'ShadowRootInit': The provided value '${sa}' is not a valid enum value of type SlotAssignmentMode.`);
        const ln = this.localName;
        if (this.namespaceURI !== NSURI[0] || !(SHADOW_HOSTS.has(ln) || validCEName(ln)))
            throw new DOMException(`Failed to execute 'attachShadow' on 'Element': This element does not support attachShadow`, 'NotSupportedError');
        const d = registry.byName.get(ln) || (this.getAttribute('is') && registry.byName.get(this.getAttribute('is')));
        if (d && d.ctor && Array.isArray(d.ctor.disabledFeatures) && d.ctor.disabledFeatures.map(String).includes('shadow'))
            throw new DOMException("Failed to execute 'attachShadow' on 'Element': attachShadow() is disabled by disabledFeatures static field.", 'NotSupportedError');
        const cur = N.shadowOf(this);
        if (cur) {
            /* a declarative shadow root of the same mode is emptied and reused */
            const f = N.srFlags(cur);
            if (!(f & 32) || ((f & 1) ? 'closed' : 'open') !== m) throw new DOMException("Failed to execute 'attachShadow' on 'Element': Shadow root cannot be created on a host which already hosts a shadow tree.", 'NotSupportedError');
            for (const k of kids(cur)) removeNode(k);
            N.srSetFlags(cur, 0, 32);
            return cur;
        }
        const sr = N.attachShadow(this, (m === 'closed' ? 1 : 0) | (delegatesFocus ? 2 : 0) | (serializable ? 4 : 0) | (clonable ? 8 : 0) | (sa === 'manual' ? 16 : 0));
        if (Object.getPrototypeOf(sr) !== ShadowRoot.prototype) Object.setPrototypeOf(sr, ShadowRoot.prototype);
        if (refTarget !== null) N.srRef(sr, refTarget);
        return sr;
    },
    get shadowRoot() { const sr = N.shadowOf(this); return sr && !(N.srFlags(sr) & 1) ? sr : null; },
    get assignedSlot() { return null; },
    requestFullscreen() { return fsRequest(this); }, webkitRequestFullscreen() { fsRequest(this).catch(() => {}); }, webkitRequestFullScreen() { fsRequest(this).catch(() => {}); },
    requestPointerLock() {}, setPointerCapture(id) { capturePointer(this, id, true); }, releasePointerCapture(id) { capturePointer(this, id, false); }, hasPointerCapture(id) { return hasCapture(this, id); },
    animate(kf, o) {
        const e = new KeyframeEffect(this, kf, o), tl = o !== null && typeof o === 'object' && o.timeline !== undefined ? o.timeline : document.timeline;
        const a = new Animation(e, tl);
        if (o !== null && typeof o === 'object' && o.id !== undefined) a.id = o.id;
        a.play(); return a;
    },
    getAnimations(o) { const sub = !!(o && o.subtree); return waGetAnims(t => t === this || (sub && this.contains(t))); },
    checkVisibility() { return !!N.rect(this); },
});
function setInner(el, html) {
    const removed = kids(el);
    for (const r of removed) ceConnected(r, false);
    N.setHTML(el, html);
    if (el.__ass) readopt(el);
    const added = kids(el);
    if (removed.length || added.length) notify('childList', el, { removedNodes: removed, addedNodes: added });
    for (const a of added) ceConnected(a, true);
}
const reflectStr = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); acc(p, n, function () { return N.attr(this, a) ?? ''; }, function (v) { this.setAttribute(a, v); }); } };
const reflectBool = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); acc(p, n, function () { return N.attr(this, a) != null; }, function (v) { if (v) this.setAttribute(a, ''); else this.removeAttribute(a); }); } };
/* the encoding a node's document gives its URLs' queries; undefined for UTF-8 */
const docEnc = n => { const e = N.charset(n); return e === 'UTF-8' ? undefined : e; };
/* set el's attribute with qualified name n (validated, lowercased) to the string v */
/* an element's contenteditable state: 'true', 'false', 'plaintext-only', or null (inherit) */
function ceState(el) {
    const v = N.attr(el, 'contenteditable');
    if (v == null) return null;
    const s = v.toLowerCase();
    return s === '' || s === 'true' ? 'true' : s === 'false' || s === 'plaintext-only' ? s : null;
}
function attrSetQ(el, n, v) {
    const a = N.attrQ(el, n) == null ? null : attrByQName(el, n);
    if (a && (a._ns !== null || a._pfx !== null)) return attrChange(el, a._ns, a._pfx, a._ln, v);
    const old = N.attrQ(el, n);
    N.setAttrQ(el, n, v);
    if (n === 'src' && el instanceof H.HTMLImageElement && !el.isConnected) N.imgWatch(el);   /* new Image(): still loads */
    if (n.charCodeAt(0) === 111 && n.charCodeAt(1) === 110) handlerAttr(el, n, v);
    notify('attributes', el, { attributeName: n, attributeNamespace: null, oldValue: old });
    ceAttr(el, n, old, v, null);
}
const reflectUrl = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); acc(p, n, function () { const v = N.attr(this, a); if (v == null) return ''; const r = N.urlParse(v, document.baseURI, docEnc(this)); return r ? r[0] : v; }, function (v) { this.setAttribute(a, v); }); } };
const reflectInt = (p, d, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); acc(p, n, function () { const v = parseInt(N.attr(this, a), 10); return isNaN(v) ? d : v; }, function (v) { this.setAttribute(a, String(v | 0)); }); } };
const EVENT_HANDLERS = 'abort animationend animationiteration animationstart auxclick beforeinput blur cancel canplay canplaythrough change click close contextmenu copy cut dblclick drag dragend dragenter dragleave dragover dragstart drop durationchange emptied ended error focus focusin focusout input invalid keydown keypress keyup load loadeddata loadedmetadata loadstart mousedown mouseenter mouseleave mousemove mouseout mouseover mouseup paste pause play playing pointercancel pointerdown pointerenter pointerleave pointermove pointerout pointerover pointerup progress ratechange reset resize scroll seeked seeking select selectionchange selectstart stalled submit suspend timeupdate toggle touchcancel touchend touchmove touchstart transitionend volumechange waiting wheel webkitanimationend webkitanimationiteration webkitanimationstart webkittransitionend'.split(' ');
const EMPTY_SCOPE = Object.freeze(Object.create(null));
function compileHandler(el, k, src) {
    const c = el.__hc || (def(el, '__hc', {}), el.__hc);
    if (c[k] && c[k].src === src) return c[k].fn;
    let fn = null;
    /* the handler's scope is the element, its form owner, its document, then the global object */
    try {
        ttInternal++;
        let make;
        try { make = new Function('__lumenScope', `with (__lumenScope[0]) with (__lumenScope[1]) return function (${N.ns(el) === 1 ? 'evt' : 'event'}) { with (this) {\n${src}\n} };`); } finally { ttInternal--; }
        let form = null;
        try { form = N.ns(el) === 0 ? el.form : null; } catch (e) {}
        fn = make([el.ownerDocument || EMPTY_SCOPE, form || EMPTY_SCOPE]);
    } catch (e) { report(e); }
    c[k] = { src, fn };
    return fn;
}
/* an on* content attribute set or removed: it replaces a handler the IDL attribute set; on <body> and <frameset> the
   window's events' handlers are the window's */
function handlerAttr(el, k, src) {
    if (el.__on && k in el.__on) delete el.__on[k];
    const t = N.name(el);
    if ((t === 'body' || t === 'frameset') && N.ns(el) === 0 && WINDOW_FWD.includes(k.slice(2))) {
        if (!G.__on) def(G, '__on', {});
        G.__on[k] = src == null ? null : compileHandler(el, k, src);
    }
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
/* pages pick the fullscreen event name by `'on' + name in document` (YouTube's player listens to none without it) */
const SHADOW_HOSTS = new Set(['article', 'aside', 'blockquote', 'body', 'div', 'footer', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'header', 'main', 'nav', 'p', 'section', 'span']);
const FS_HANDLERS = ['fullscreenchange', 'fullscreenerror', 'webkitfullscreenchange', 'webkitfullscreenerror'];
installHandlers(Element.prototype, true, FS_HANDLERS);
/* ChildNode and ParentNode methods stay out of `with (element)` scopes (inline event handlers) */
const unscopable = (C, names) => Object.defineProperty(C.prototype, Symbol.unscopables, { value: Object.assign(Object.create(null), Object.fromEntries(names.map(n => [n, true]))), configurable: true });
unscopable(Element, ['after', 'append', 'before', 'prepend', 'remove', 'replaceChildren', 'replaceWith', 'slot']);
unscopable(CharacterData, ['after', 'before', 'remove', 'replaceWith']);
unscopable(DocumentType, ['after', 'before', 'remove', 'replaceWith']);
unscopable(DocumentFragment, ['append', 'prepend', 'replaceChildren']);
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
    get innerText() { return innerTextOf(this); }, set innerText(v) { this.textContent = v; }, get outerText() { return N.text(this); },
    get tabIndex() { const v = parseInt(N.attr(this, 'tabindex'), 10); return isNaN(v) ? (/^(a|button|input|select|textarea)$/.test(N.name(this)) ? 0 : -1) : v; },
    set tabIndex(v) { this.setAttribute('tabindex', String(v | 0)); },
    get offsetParent() {
        if (this === document.body || this === document.documentElement || !N.rect(this) || N.computed(this, 'position') === 'fixed') return null;
        const stat = N.computed(this, 'position') === 'static';
        for (let p = N.parent(this); p && N.type(p) === 1; p = N.parent(p)) {
            if (p === document.body) return p;
            const pos = N.computed(p, 'position');
            if ((pos && pos !== 'static') || (stat && /^(td|th|table)$/.test(N.name(p)))) return p;
        }
        return null;
    },
    get offsetTop() { const r = N.rect(this); if (!r) return 0; const op = this.offsetParent; const pr = op && op !== document.body ? N.rect(op) : null; return Math.round(r[1] - (pr ? pr[1] : 0)); },
    get offsetLeft() { const r = N.rect(this); if (!r) return 0; const op = this.offsetParent; const pr = op && op !== document.body ? N.rect(op) : null; return Math.round(r[0] - (pr ? pr[0] : 0)); },
    get offsetWidth() { const r = N.rect(this); return r ? Math.round(r[2]) : 0; },
    get offsetHeight() { const r = N.rect(this); return r ? Math.round(r[3]) : 0; },
    get isContentEditable() {
        for (let n = this; n && N.type(n) === 1; n = N.parent(n)) { const s = ceState(n); if (s) return s !== 'false'; }
        return N.connected(this) && N.ownerDoc(this) === document && document.designMode === 'on';
    },
    get contentEditable() { return ceState(this) || 'inherit'; },
    set contentEditable(v) {
        const s = String(v).toLowerCase();
        if (s === 'inherit') this.removeAttribute('contenteditable');
        else if (s === 'true' || s === 'false' || s === 'plaintext-only') this.setAttribute('contenteditable', s);
        else throw new DOMException(`Failed to set the 'contentEditable' property on 'HTMLElement': The value provided ('${v}') is not one of 'true', 'false', 'plaintext-only', or 'inherit'.`, 'SyntaxError');
    },
    get draggable() { return N.attr(this, 'draggable') === 'true'; }, set draggable(v) { this.setAttribute('draggable', String(!!v)); },
    focus() { const o = N.active(); if (o === this) return; N.focus(this); if (N.active() !== this) return; if (o) o.dispatchEvent(new FocusEvent('focusout', { bubbles: true, composed: true, relatedTarget: this })); this.dispatchEvent(new FocusEvent('focusin', { bubbles: true, composed: true, relatedTarget: o })); }, blur() { if (N.active() !== this) return; N.focus(null); this.dispatchEvent(new FocusEvent('focusout', { bubbles: true, composed: true, relatedTarget: null })); },
    click() {
        if (this.disabled || this.__clicking) return;
        def(this, '__clicking', true);
        try { dispatch(this, new PointerEvent('click', { bubbles: true, cancelable: true, composed: true, view: G, detail: 1, pointerId: -1, pointerType: '' })); }
        finally { this.__clicking = false; }
    },
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
installHandlers(MathMLElement.prototype, true);
methods(MathMLElement.prototype, {
    get style() { if (!this.__style) def(this, '__style', makeStyle(this)); return this.__style; },
    get dataset() { return Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'dataset').get.call(this); },
    focus() { N.focus(this); }, blur() { if (N.active() === this) N.focus(null); },
});
/* HTMLOrSVGElement's tabIndex, nonce and autofocus on SVG and MathML elements */
for (const P of [SVGElement.prototype, MathMLElement.prototype]) {
    acc(P, 'tabIndex', function () { const v = /^[\t\n\f\r ]*[-+]?\d+/.test(N.attr(this, 'tabindex') || '') ? parseInt(N.attr(this, 'tabindex'), 10) : NaN; return Number.isFinite(v) && Math.abs(v) < 2 ** 31 ? v : N.ns(this) === 1 && N.name(this) === 'a' && N.attr(this, 'href') != null ? 0 : -1; },
        function (v) { this.setAttribute('tabindex', String(v | 0)); });
    acc(P, 'autofocus', function () { return N.attr(this, 'autofocus') != null; }, function (v) { if (v) this.setAttribute('autofocus', ''); else this.removeAttribute('autofocus'); });
    acc(P, 'nonce', function () { return this.__nonce ?? N.attr(this, 'nonce') ?? ''; }, function (v) { def(this, '__nonce', String(v)); });
}

/* ARIAMixin: nullable string reflection of role and aria-* */
for (const k of ['role', 'ariaAtomic', 'ariaAutoComplete', 'ariaBrailleLabel', 'ariaBrailleRoleDescription', 'ariaBusy', 'ariaChecked',
    'ariaColCount', 'ariaColIndex', 'ariaColIndexText', 'ariaColSpan', 'ariaCurrent', 'ariaDescription', 'ariaDisabled', 'ariaExpanded',
    'ariaHasPopup', 'ariaHidden', 'ariaInvalid', 'ariaKeyShortcuts', 'ariaLabel', 'ariaLevel', 'ariaLive', 'ariaModal', 'ariaMultiLine',
    'ariaMultiSelectable', 'ariaOrientation', 'ariaPlaceholder', 'ariaPosInSet', 'ariaPressed', 'ariaReadOnly', 'ariaRelevant',
    'ariaRequired', 'ariaRoleDescription', 'ariaRowCount', 'ariaRowIndex', 'ariaRowIndexText', 'ariaRowSpan', 'ariaSelected',
    'ariaSetSize', 'ariaSort', 'ariaValueMax', 'ariaValueMin', 'ariaValueNow', 'ariaValueText']) {
    const a = k === 'role' ? 'role' : 'aria-' + k.slice(4).toLowerCase();
    acc(Element.prototype, k, function () { return N.attr(this, a) ?? null; }, function (v) { if (v === null) this.removeAttribute(a); else this.setAttribute(a, v); });
}
/* the element a reference to el ends at, following shadow roots' referenceTarget; null when one names no element */
function refResolve(el) {
    for (let i = 0; el && i < 64; i++) {
        const sr = N.isNode(el) && N.type(el) === 1 ? N.shadowOf(el) : null, rt = sr ? N.srRef(sr) : null;
        if (rt === null || rt === undefined) return el;
        el = rt ? N.query(sr, '#' + CSS.escape(rt), false) : null;
    }
    return el || null;
}
/* element reflection (HTML "reflect" for Element / FrozenArray<Element> attributes): explicitly set elements count
   while they are in this element's tree or a shadow-including ancestor tree of it */
const refExplicit = new WeakMap();
const refVisible = (from, e) => { const t = e.getRootNode(); for (let r = from.getRootNode(); r; r = r.host ? r.host.getRootNode() : null) if (r === t) return true; return false; };
const refById = (el, id) => { const r = el.getRootNode(); return r && typeof r.getElementById === 'function' ? r.getElementById(id) : null; };
function reflectElement(proto, prop, attr) {
    acc(proto, prop, function () {
        const m = refExplicit.get(this), e = m && m[attr];
        if (e) return refVisible(this, e) ? e : null;
        const id = N.attr(this, attr);
        return id ? refById(this, id) : null;
    }, function (v) {
        let m = refExplicit.get(this);
        if (v == null) { if (m) delete m[attr]; this.removeAttribute(attr); return; }
        if (!(v instanceof Element)) throw new TypeError(`Failed to set the '${prop}' property: The provided value is not of type 'Element'.`);
        if (!m) refExplicit.set(this, m = {});
        this.setAttribute(attr, ''); m[attr] = v;
    });
}
function reflectElements(proto, prop, attr) {
    acc(proto, prop, function () {
        const m = refExplicit.get(this), l = m && m[attr];
        const out = l ? l.filter(e => refVisible(this, e)) : (N.attr(this, attr) ?? null) === null ? null
            : N.attr(this, attr).split(/[\t\n\f\r ]+/).filter(Boolean).map(id => refById(this, id)).filter(Boolean);
        if (out === null) return null;
        const c = m && m['#' + attr];
        if (c && c.length === out.length && c.every((e, i) => e === out[i])) return c;
        const f = Object.freeze(out);
        let mm = m; if (!mm) refExplicit.set(this, mm = {});
        mm['#' + attr] = f;
        return f;
    }, function (v) {
        let m = refExplicit.get(this);
        if (v == null) { if (m) delete m[attr]; this.removeAttribute(attr); return; }
        const l = [...v];
        if (!l.every(e => e instanceof Element)) throw new TypeError(`Failed to set the '${prop}' property: The provided value is not of type 'Element'.`);
        if (!m) refExplicit.set(this, m = {});
        this.setAttribute(attr, ''); m[attr] = l;
    });
}
reflectElement(Element.prototype, 'ariaActiveDescendantElement', 'aria-activedescendant');
for (const [p, a] of [['ariaControlsElements', 'aria-controls'], ['ariaDescribedByElements', 'aria-describedby'], ['ariaDetailsElements', 'aria-details'],
    ['ariaErrorMessageElements', 'aria-errormessage'], ['ariaFlowToElements', 'aria-flowto'], ['ariaLabelledByElements', 'aria-labelledby'], ['ariaOwnsElements', 'aria-owns']])
    reflectElements(Element.prototype, p, a);

function innerTextOf(el) {
    const BLK = /^(ADDRESS|ARTICLE|ASIDE|BLOCKQUOTE|CAPTION|DD|DETAILS|DIALOG|DIV|DL|DT|FIELDSET|FIGCAPTION|FIGURE|FOOTER|FORM|H[1-6]|HEADER|HR|LI|MAIN|NAV|OL|PRE|SECTION|SUMMARY|TABLE|TBODY|TFOOT|THEAD|TR|UL)$/;
    const SKIP = /^(SCRIPT|STYLE|TEMPLATE|NOSCRIPT|HEAD|TITLE)$/i;
    let out = '', nlc = 0;
    const add = t => { if (!t) return; out += t; const m = /\n*$/.exec(t)[0].length; nlc = m === t.length ? nlc + m : m; };
    const nl = k => { if (out && nlc < k) add('\n'.repeat(k - nlc)); };
    const walk = (n, pre) => {
        for (let c = n.firstChild; c; c = c.nextSibling) {
            if (c.nodeType === 3) {
                let t = c.data;
                if (!pre) { t = t.replace(/[\t\n\r ]+/g, ' '); const last = out[out.length - 1]; if (!out || last === ' ' || last === '\n' || last === '\t') t = t.replace(/^ /, ''); }
                add(t);
            } else if (c.nodeType === 1) {
                const tg = c.tagName;
                if (SKIP.test(tg) || c.hasAttribute('hidden')) continue;
                if (tg === 'BR') { add('\n'); continue; }
                const k = tg === 'P' ? 2 : BLK.test(tg) ? 1 : 0;
                if (k) nl(k);
                walk(c, pre || tg === 'PRE' || tg === 'TEXTAREA');
                if (k) nl(k); else if ((tg === 'TD' || tg === 'TH') && c.nextElementSibling) add('\t');
            }
        }
    };
    walk(el, false);
    return out.replace(/ +\n/g, '\n').replace(/^\n+|[\n ]+$/g, '');
}
