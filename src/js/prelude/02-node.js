const illegal = () => { throw new TypeError('Illegal constructor'); };
class Node extends EventTarget { constructor() { super(); illegal(); } }
Object.assign(Node, { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2, TEXT_NODE: 3, CDATA_SECTION_NODE: 4, PROCESSING_INSTRUCTION_NODE: 7, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_TYPE_NODE: 10, DOCUMENT_FRAGMENT_NODE: 11, DOCUMENT_POSITION_DISCONNECTED: 1, DOCUMENT_POSITION_PRECEDING: 2, DOCUMENT_POSITION_FOLLOWING: 4, DOCUMENT_POSITION_CONTAINS: 8, DOCUMENT_POSITION_CONTAINED_BY: 16, DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC: 32 });
Object.assign(Node.prototype, { ELEMENT_NODE: 1, TEXT_NODE: 3, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_FRAGMENT_NODE: 11 });
const kids = (n) => { const a = []; for (let c = N.first(n); c; c = N.next(c)) a.push(c); return a; };
const elKids = (n) => { const a = []; for (let c = N.first(n); c; c = N.next(c)) if (N.type(c) === 1) a.push(c); return a; };
class NodeList extends Array { item(i) { return this[i] ?? null; } static get [Symbol.species]() { return Array; } }
const nodeList = (a) => { Object.setPrototypeOf(a, NodeList.prototype); return a; };
const liveIdx = (k) => typeof k === 'string' && /^(0|[1-9]\d*)$/.test(k) ? +k : -1;
const liveHandler = {
    get(t, k, r) { if (k === 'length') return t._cur().length; const i = liveIdx(k); return i >= 0 ? t._cur()[i] : Reflect.get(t, k, r); },
    has(t, k) { const i = liveIdx(k); return i >= 0 ? i < t._cur().length : Reflect.has(t, k); },
    ownKeys(t) { return t._cur().map((_, i) => String(i)).concat(Reflect.ownKeys(t)); },
    getOwnPropertyDescriptor(t, k) {
        const i = liveIdx(k);
        if (i < 0) return Reflect.getOwnPropertyDescriptor(t, k);
        const a = t._cur();
        return i < a.length ? { value: a[i], enumerable: true, configurable: true, writable: false } : undefined;
    },
    set(t, k, v, r) { return liveIdx(k) >= 0 || k === 'length' ? true : Reflect.set(t, k, v, r); },
};
/* Live NodeList: recomputed lazily whenever the owner document has mutated. */
function liveList(owner, compute) {
    const t = Object.create(NodeList.prototype);
    let ver = -1, arr = [];
    Object.defineProperty(t, '_cur', { value() { const v = N.version(owner); if (v !== ver) { ver = v; arr = compute(); } return arr; } });
    return new Proxy(t, liveHandler);
}
const liveCache = new WeakMap();
function cachedLive(owner, key, compute) {
    let m = liveCache.get(owner);
    if (!m) liveCache.set(owner, m = {});
    return m[key] || (m[key] = liveList(owner, compute));
}
function toNode(x) { return N.isNode(x) ? x : N.textNode(String(x)); }
function insertNode(p, c, ref) {
    if (!N.isNode(c)) throw new TypeError("parameter is not of type 'Node'");
    if (N.contains(c, p)) throw new DOMException('The new child element contains the parent.', 'HierarchyRequestError');
    if (ref != null && N.parent(ref) !== p) throw new DOMException('The node before which the new node is to be inserted is not a child of this node.', 'NotFoundError');
    const added = N.type(c) === 11 ? kids(c) : [c];
    if (observers.length) for (const a of added) { const op = N.parent(a); if (op) notify('childList', op, { removedNodes: [a] }); }
    const prev = ref ? N.prev(ref) : N.last(p);
    N.insert(p, c, ref == null ? null : ref);
    if (added.length) notify('childList', p, { addedNodes: added, previousSibling: prev, nextSibling: ref || null });
    for (const a of added) ceConnected(a, true);
    return c;
}
function removeNode(c) {
    const p = N.parent(c); if (!p) return c;
    const prev = N.prev(c), next = N.next(c), was = N.connected(c);
    N.remove(c);
    notify('childList', p, { removedNodes: [c], previousSibling: prev, nextSibling: next });
    if (was) ceConnected(c, false);
    return c;
}
methods(Node.prototype, {
    get nodeType() { return N.type(this); },
    get nodeName() { const t = N.type(this); return t === 1 ? this.tagName : t === 3 ? '#text' : t === 4 ? '#cdata-section' : t === 8 ? '#comment' : t === 9 ? '#document' : t === 11 ? '#document-fragment' : t === 10 ? N.name(this) : ''; },
    get baseURI() { return document.baseURI; },
    get parentNode() { return N.parent(this); },
    get parentElement() { const p = N.parent(this); return p && N.type(p) === 1 ? p : null; },
    get firstChild() { return N.first(this); }, get lastChild() { return N.last(this); },
    get nextSibling() { return N.next(this); }, get previousSibling() { return N.prev(this); },
    get childNodes() { return cachedLive(this, 'childNodes', () => kids(this)); },
    hasChildNodes() { return !!N.first(this); },
    get ownerDocument() { return N.type(this) === 9 ? null : document; },
    get isConnected() { return N.connected(this); },
    get textContent() { const t = N.type(this); return t === 9 || t === 10 ? null : N.text(this); },
    set textContent(v) {
        const t = N.type(this); if (t === 9 || t === 10) return;
        v = v == null ? '' : String(v);
        if (t === 3 || t === 8 || t === 4) { const old = observers.length ? N.text(this) : null; N.setText(this, v); notify('characterData', this, { oldValue: old }); return; }
        const removed = kids(this);
        for (const r of removed) ceConnected(r, false);
        N.setText(this, v);
        if (removed.length || N.first(this)) notify('childList', this, { removedNodes: removed, addedNodes: kids(this) });
    },
    get nodeValue() { const t = N.type(this); return t === 3 || t === 8 || t === 4 ? N.text(this) : null; },
    set nodeValue(v) { const t = N.type(this); if (t === 3 || t === 8 || t === 4) this.textContent = v; },
    appendChild(c) { return insertNode(this, c, null); },
    insertBefore(c, ref) { return insertNode(this, c, ref === undefined ? null : ref); },
    removeChild(c) { if (!N.isNode(c) || N.parent(c) !== this) throw new DOMException('The node to be removed is not a child of this node.', 'NotFoundError'); return removeNode(c); },
    replaceChild(n, old) { if (N.parent(old) !== this) throw new DOMException('The node to be replaced is not a child of this node.', 'NotFoundError'); if (n === old) return old; const nx = N.next(old); removeNode(old); insertNode(this, n, nx === n ? N.next(n) : nx); return old; },
    cloneNode(deep) { return N.clone(this, !!deep); },
    contains(o) { return o != null && N.isNode(o) && N.contains(this, o); },
    getRootNode(o) { let n = this; for (;;) { const p = N.parent(n); if (p) { n = p; continue; } if (o && o.composed && N.type(n) === 11 && N.host(n)) { n = N.host(n); continue; } return n; } },
    isSameNode(o) { return this === o; },
    isEqualNode(o) { if (!o || N.type(o) !== N.type(this)) return false; return N.type(this) === 1 ? N.html(this, true) === N.html(o, true) : N.text(this) === N.text(o); },
    compareDocumentPosition(o) {
        if (o === this) return 0;
        const anc = (n) => { const a = []; for (; n; n = N.parent(n)) a.unshift(n); return a; };
        const a = anc(this), b = anc(o);
        if (a[0] !== b[0]) return 1 | 32 | 4;
        let i = 0; while (i < a.length && i < b.length && a[i] === b[i]) i++;
        if (i === a.length) return 16 | 4;
        if (i === b.length) return 8 | 2;
        for (let c = N.next(a[i]); c; c = N.next(c)) if (c === b[i]) return 4;
        return 2;
    },
    normalize() {},
    lookupNamespaceURI() { return null; }, isDefaultNamespace(ns) { return ns == null || ns === 'http://www.w3.org/1999/xhtml'; },
});
const ParentNode = {
    get children() { return cachedLive(this, 'children', () => elKids(this)); },
    get childElementCount() { return elKids(this).length; },
    get firstElementChild() { for (let c = N.first(this); c; c = N.next(c)) if (N.type(c) === 1) return c; return null; },
    get lastElementChild() { for (let c = N.last(this); c; c = N.prev(c)) if (N.type(c) === 1) return c; return null; },
    append(...ns) { for (const n of ns) insertNode(this, toNode(n), null); },
    prepend(...ns) { const f = N.first(this); for (const n of ns) insertNode(this, toNode(n), f); },
    replaceChildren(...ns) { for (const c of kids(this)) removeNode(c); this.append(...ns); },
    querySelector(s) { return N.query(this, String(s), false); },
    querySelectorAll(s) { return nodeList(N.query(this, String(s), true)); },
    getElementsByTagName(t) { t = String(t); const sel = t === '*' ? '*' : t.replace(/[^\w-]/g, ''); return liveList(this, () => sel ? N.query(this, sel, true) : []); },
    getElementsByClassName(c) { const p = String(c).trim().split(/\s+/).filter(Boolean); const sel = p.map(x => '.' + CSS.escape(x)).join(''); return liveList(this, () => sel ? N.query(this, sel, true) : []); },
};
const ChildNode = {
    before(...ns) { const p = N.parent(this); if (!p) return; for (const n of ns) insertNode(p, toNode(n), this); },
    after(...ns) { const p = N.parent(this); if (!p) return; const ref = N.next(this); for (const n of ns) insertNode(p, toNode(n), ref); },
    replaceWith(...ns) { const p = N.parent(this); if (!p) return; const ref = N.next(this); removeNode(this); for (const n of ns) insertNode(p, toNode(n), ref); },
    remove() { removeNode(this); },
    get nextElementSibling() { for (let c = N.next(this); c; c = N.next(c)) if (N.type(c) === 1) return c; return null; },
    get previousElementSibling() { for (let c = N.prev(this); c; c = N.prev(c)) if (N.type(c) === 1) return c; return null; },
};
class CharacterData extends Node {}
methods(CharacterData.prototype, ChildNode);
methods(CharacterData.prototype, {
    get data() { return N.text(this); }, set data(v) { this.textContent = v == null ? '' : String(v); },
    get length() { return N.text(this).length; },
    appendData(s) { this.data += s; },
    substringData(o, c) { return this.data.substr(o, c); },
    insertData(o, s) { const d = this.data; this.data = d.slice(0, o) + s + d.slice(o); },
    deleteData(o, c) { const d = this.data; this.data = d.slice(0, o) + d.slice(o + c); },
    replaceData(o, c, s) { const d = this.data; this.data = d.slice(0, o) + s + d.slice(o + c); },
});
class Text extends CharacterData { constructor(s = '') { return N.textNode(String(s)); } }
methods(Text.prototype, {
    get wholeText() { return this.data; },
    splitText(o) { const d = this.data; const t = N.textNode(d.slice(o)); this.data = d.slice(0, o); const p = N.parent(this); if (p) insertNode(p, t, N.next(this)); return t; },
    get assignedSlot() { return null; },
});
class CDATASection extends Text {}
class Comment extends CharacterData { constructor(s = '') { return N.comment(String(s)); } }
class DocumentType extends Node {}
methods(DocumentType.prototype, ChildNode);
methods(DocumentType.prototype, { get name() { return N.name(this); }, get publicId() { return ''; }, get systemId() { return ''; } });
class DocumentFragment extends Node { constructor() { return N.frag(); } }
methods(DocumentFragment.prototype, ParentNode);
methods(DocumentFragment.prototype, { getElementById(id) { return N.query(this, '#' + CSS.escape(String(id)), false); } });
class ShadowRoot extends DocumentFragment {}
methods(ShadowRoot.prototype, {
    get host() { return N.host(this); }, get mode() { return this.__mode || 'open'; },
    get innerHTML() { return N.html(this, false); }, set innerHTML(v) { setInner(this, String(v)); },
    get activeElement() { return null; }, get delegatesFocus() { return false; },
    get adoptedStyleSheets() { return this.__ass || []; }, set adoptedStyleSheets(v) { def(this, '__ass', v); },
    get styleSheets() { return []; },
});

class Attr {
    constructor(el, name) { def(this, '_el', el); this.name = name; this.localName = name; this.namespaceURI = null; this.prefix = null; this.specified = true; }
    get value() { return this._el ? N.attr(this._el, this.name) ?? '' : ''; } set value(v) { if (this._el) this._el.setAttribute(this.name, v); }
    get ownerElement() { return this._el; } get nodeType() { return 2; } get nodeName() { return this.name; } get nodeValue() { return this.value; } get textContent() { return this.value; }
}
class NamedNodeMap {
    constructor(el) { def(this, '_el', el); const n = N.attrs(el); for (let i = 0; i < n.length; i += 2) this[i / 2] = new Attr(el, n[i]); def(this, 'length', n.length / 2); }
    item(i) { return this[i] || null; }
    getNamedItem(n) { n = String(n).toLowerCase(); return N.attr(this._el, n) == null ? null : new Attr(this._el, n); }
    setNamedItem(a) { this._el.setAttribute(a.name, a.value); }
    removeNamedItem(n) { this._el.removeAttribute(n); }
    *[Symbol.iterator]() { for (let i = 0; i < this.length; i++) yield this[i]; }
}
class DOMTokenList {
    constructor(el, attr) { def(this, '_el', el); def(this, '_attr', attr); }
    _get() { const v = N.attr(this._el, this._attr); return v ? v.split(/[\t\n\f\r ]+/).filter(Boolean) : []; }
    _set(a) { this._el.setAttribute(this._attr, a.join(' ')); }
    get length() { return this._get().length; }
    get value() { return N.attr(this._el, this._attr) || ''; } set value(v) { this._el.setAttribute(this._attr, v); }
    item(i) { return this._get()[i] ?? null; }
    contains(t) { return this._get().includes(String(t)); }
    add(...ts) { const a = this._get(); let ch = false; for (let t of ts) { t = String(t); if (!t || /\s/.test(t)) throw new DOMException('The token provided contains invalid characters.', t ? 'InvalidCharacterError' : 'SyntaxError'); if (!a.includes(t)) { a.push(t); ch = true; } } if (ch) this._set(a); }
    remove(...ts) { const r = ts.map(String); const a = this._get(); const b = a.filter(x => !r.includes(x)); if (b.length !== a.length) this._set(b); }
    toggle(t, force) { t = String(t); const has = this.contains(t); if (force === undefined ? has : !force) { if (has) this.remove(t); return false; } if (!has) this.add(t); return true; }
    replace(a, b) { const l = this._get(); const i = l.indexOf(String(a)); if (i < 0) return false; l[i] = String(b); this._set([...new Set(l)]); return true; }
    supports() { return true; }
    forEach(fn, self) { this._get().forEach((v, i) => fn.call(self, v, i, this)); }
    entries() { return this._get().entries(); } keys() { return this._get().keys(); } values() { return this._get().values(); }
    [Symbol.iterator]() { return this._get()[Symbol.iterator](); }
    toString() { return this.value; }
}
