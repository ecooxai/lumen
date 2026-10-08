const illegal = () => { throw new TypeError('Illegal constructor'); };
class Node extends EventTarget { constructor() { super(); illegal(); } }
Object.assign(Node, { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2, TEXT_NODE: 3, CDATA_SECTION_NODE: 4, PROCESSING_INSTRUCTION_NODE: 7, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_TYPE_NODE: 10, DOCUMENT_FRAGMENT_NODE: 11, DOCUMENT_POSITION_DISCONNECTED: 1, DOCUMENT_POSITION_PRECEDING: 2, DOCUMENT_POSITION_FOLLOWING: 4, DOCUMENT_POSITION_CONTAINS: 8, DOCUMENT_POSITION_CONTAINED_BY: 16, DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC: 32 });
Object.assign(Node.prototype, { ELEMENT_NODE: 1, TEXT_NODE: 3, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_FRAGMENT_NODE: 11 });
const nextIn = (n, root) => { const f = N.first(n); if (f) return f; for (; n && n !== root; n = N.parent(n)) { const x = N.next(n); if (x) return x; } return null; };
const kids = (n) => { const a = []; for (let c = N.first(n); c; c = N.next(c)) a.push(c); return a; };
const elKids = (n) => { const a = []; for (let c = N.first(n); c; c = N.next(c)) if (N.type(c) === 1) a.push(c); return a; };
const liveRanges = new Set();
const rangesEach = (f) => { for (const w of liveRanges) { const r = w.deref(); if (r) f(r); else liveRanges.delete(w); } };
const idxOf = (n) => { let i = 0; for (let s = N.prev(n); s; s = N.prev(s)) i++; return i; };
const isCD = (n) => { const t = N.type(n); return t === 3 || t === 4 || t === 7 || t === 8; };
const isTextNode = (n) => { const t = N.type(n); return t === 3 || t === 4; };
const nodeLength = (n) => { const t = N.type(n); if (t === 10) return 0; if (t === 3 || t === 4 || t === 7 || t === 8) return N.text(n).length; let k = 0; for (let c = N.first(n); c; c = N.next(c)) k++; return k; };
const toU32 = (v) => { v = Number(v); if (!Number.isFinite(v)) return 0; v = Math.trunc(v) % 4294967296; return v < 0 ? v + 4294967296 : v; };
const hre = (m = 'The operation would yield an incorrect node tree.') => new DOMException(m, 'HierarchyRequestError');
function rangePreRemove(c) {
    const p = N.parent(c), i = idxOf(c);
    rangesEach(r => {
        if (N.contains(c, r._sc)) { r._sc = p; r._so = i; }
        if (N.contains(c, r._ec)) { r._ec = p; r._eo = i; }
        if (r._sc === p && r._so > i) r._so--;
        if (r._ec === p && r._eo > i) r._eo--;
    });
}
function rangeInserted(p, i, k) { rangesEach(r => { if (r._sc === p && r._so > i) r._so += k; if (r._ec === p && r._eo > i) r._eo += k; }); }
function replaceData(n, o, c, s) {
    const d = N.text(n), len = d.length;
    if (o > len) throw new DOMException(`The offset ${o} is greater than the node's length (${len}).`, 'IndexSizeError');
    if (o + c > len) c = len - o;
    N.setText(n, d.slice(0, o) + s + d.slice(o + c));
    notify('characterData', n, { oldValue: d });
    if (liveRanges.size) rangesEach(r => {
        if (r._sc === n && r._so > o && r._so <= o + c) r._so = o;
        if (r._ec === n && r._eo > o && r._eo <= o + c) r._eo = o;
        if (r._sc === n && r._so > o + c) r._so += s.length - c;
        if (r._ec === n && r._eo > o + c) r._eo += s.length - c;
    });
}
function preInsertCheck(p, c, ref) {
    const pt = N.type(p), ct = N.type(c);
    if (pt !== 1 && pt !== 9 && pt !== 11) throw hre('Nodes of this type may not contain children.');
    if (N.contains(c, p)) throw hre('The new child element contains the parent.');
    if (ref != null && N.parent(ref) !== p) throw new DOMException('The node before which the new node is to be inserted is not a child of this node.', 'NotFoundError');
    if (ct === 9 || ct === 2) throw hre();
    if ((ct === 3 || ct === 4) && pt === 9) throw hre('Nodes of type \'#text\' may not be inserted inside nodes of type \'#document\'.');
    if (ct === 10 && pt !== 9) throw hre();
    if (pt !== 9) return;
    const els = elKids(p).length, refAfterDoctype = () => { for (let s = ref; s; s = N.next(s)) if (N.type(s) === 10) return true; return false; };
    const one = () => { if (els || (ref && N.type(ref) === 10) || refAfterDoctype()) throw hre(); };
    if (ct === 11) { const k = elKids(c).length; if (k > 1 || kids(c).some(x => isTextNode(x))) throw hre(); if (k === 1) one(); }
    else if (ct === 1) one();
    else if (ct === 10) {
        if (kids(p).some(x => N.type(x) === 10)) throw hre();
        if (ref) { for (let s = N.prev(ref); s; s = N.prev(s)) if (N.type(s) === 1) throw hre(); }
        else if (els) throw hre();
    }
}
class NodeList {
    item(i) { return this[i] ?? null; }
    forEach(fn, self) { for (let i = 0; i < this.length; i++) fn.call(self, this[i], i, this); }
    *[Symbol.iterator]() { for (let i = 0; i < this.length; i++) yield this[i]; }
    *entries() { for (let i = 0; i < this.length; i++) yield [i, this[i]]; }
    *keys() { for (let i = 0; i < this.length; i++) yield i; }
    values() { return this[Symbol.iterator](); }
}
const nodeList = (a) => { const o = Object.create(NodeList.prototype); for (let i = 0; i < a.length; i++) o[i] = a[i]; Object.defineProperty(o, 'length', { value: a.length }); return o; };
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
    preInsertCheck(p, c, ref);
    const added = N.type(c) === 11 ? kids(c) : [c];
    if (observers.length) for (const a of added) { const op = N.parent(a); if (op) notify('childList', op, { removedNodes: [a] }); }
    if (ref === c) ref = N.next(c);
    const moved = added.filter(a => N.connected(a));
    if (liveRanges.size) for (const a of added) if (N.parent(a)) rangePreRemove(a);
    const prev = ref ? N.prev(ref) : N.last(p);
    N.insert(p, c, ref == null ? null : ref);
    if (liveRanges.size && added.length) rangeInserted(p, idxOf(added[0]), added.length);
    if (added.length) notify('childList', p, { addedNodes: added, previousSibling: prev, nextSibling: ref || null });
    for (const a of moved) ceConnected(a, false, true);
    for (const a of added) ceConnected(a, true);
    return c;
}
function removeNode(c) {
    const p = N.parent(c); if (!p) return c;
    const prev = N.prev(c), next = N.next(c), was = N.connected(c);
    if (liveRanges.size) rangePreRemove(c);
    N.remove(c);
    notify('childList', p, { removedNodes: [c], previousSibling: prev, nextSibling: next });
    if (was) ceConnected(c, false);
    return c;
}
methods(Node.prototype, {
    get nodeType() { return N.type(this); },
    get nodeName() { const t = N.type(this); return t === 1 ? this.tagName : t === 3 ? '#text' : t === 4 ? '#cdata-section' : t === 7 ? N.name(this) : t === 8 ? '#comment' : t === 9 ? '#document' : t === 11 ? '#document-fragment' : t === 10 ? N.name(this) : ''; },
    get baseURI() { return document.baseURI; },
    get parentNode() { return N.parent(this); },
    get parentElement() { const p = N.parent(this); return p && N.type(p) === 1 ? p : null; },
    get firstChild() { return N.first(this); }, get lastChild() { return N.last(this); },
    get nextSibling() { return N.next(this); }, get previousSibling() { return N.prev(this); },
    get childNodes() { return cachedLive(this, 'childNodes', () => kids(this)); },
    hasChildNodes() { return !!N.first(this); },
    get ownerDocument() { return N.type(this) === 9 ? null : N.ownerDoc(this); },
    get isConnected() { return N.connected(this); },
    get textContent() { const t = N.type(this); return t === 9 || t === 10 ? null : N.text(this); },
    set textContent(v) {
        const t = N.type(this); if (t === 9 || t === 10) return;
        v = v == null ? '' : String(v);
        if (t === 3 || t === 8 || t === 4 || t === 7) { replaceData(this, 0, N.text(this).length, v); return; }
        const removed = kids(this);
        if (liveRanges.size) for (let i = removed.length - 1; i >= 0; i--) rangePreRemove(removed[i]);
        for (const r of removed) ceConnected(r, false);
        N.setText(this, v);
        if (removed.length || N.first(this)) notify('childList', this, { removedNodes: removed, addedNodes: kids(this) });
    },
    get nodeValue() { return isCD(this) ? N.text(this) : null; },
    set nodeValue(v) { if (isCD(this)) this.textContent = v; },
    appendChild(c) { return insertNode(this, c, null); },
    insertBefore(c, ref) { return insertNode(this, c, ref === undefined ? null : ref); },
    removeChild(c) { if (!N.isNode(c) || N.parent(c) !== this) throw new DOMException('The node to be removed is not a child of this node.', 'NotFoundError'); return removeNode(c); },
    replaceChild(n, old) { if (N.parent(old) !== this) throw new DOMException('The node to be replaced is not a child of this node.', 'NotFoundError'); if (n === old) return old; const nx = N.next(old); removeNode(old); insertNode(this, n, nx === n ? N.next(n) : nx); return old; },
    cloneNode(deep) { const r = N.clone(this, !!deep); if (!N.inert(this)) ceUpgradeTree(r); return r; },
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
    normalize() {
        const texts = [];
        for (let n = N.first(this); n; n = nextIn(n, this)) if (N.type(n) === 3) texts.push(n);
        for (const node of texts) {
            if (!N.contains(this, node)) continue;
            let length = N.text(node).length;
            if (!length) { removeNode(node); continue; }
            let data = ''; for (let x = N.next(node); x && N.type(x) === 3; x = N.next(x)) data += N.text(x);
            if (!data) continue;
            replaceData(node, length, 0, data);
            for (let cur = N.next(node); cur && N.type(cur) === 3; cur = N.next(cur)) {
                if (liveRanges.size) { const cp = N.parent(cur), ci = idxOf(cur), L = length; rangesEach(r => {
                    if (r._sc === cur) { r._sc = node; r._so += L; } if (r._ec === cur) { r._ec = node; r._eo += L; }
                    if (r._sc === cp && r._so === ci) { r._sc = node; r._so = L; } if (r._ec === cp && r._eo === ci) { r._ec = node; r._eo = L; }
                }); }
                length += N.text(cur).length;
            }
            for (let x = N.next(node); x && N.type(x) === 3;) { const nx = N.next(x); removeNode(x); x = nx; }
        }
    },
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
    get data() { return N.text(this); }, set data(v) { replaceData(this, 0, N.text(this).length, v == null ? '' : String(v)); },
    get length() { return N.text(this).length; },
    appendData(s) { replaceData(this, N.text(this).length, 0, String(s)); },
    substringData(o, c) { o = toU32(o); c = toU32(c); const d = N.text(this); if (o > d.length) throw new DOMException(`The offset ${o} is greater than the node's length (${d.length}).`, 'IndexSizeError'); return d.substring(o, o + c); },
    insertData(o, s) { replaceData(this, toU32(o), 0, String(s)); },
    deleteData(o, c) { replaceData(this, toU32(o), toU32(c), ''); },
    replaceData(o, c, s) { replaceData(this, toU32(o), toU32(c), String(s)); },
});
class Text extends CharacterData { constructor(s = '') { return N.textNode(String(s)); } }
methods(Text.prototype, {
    get wholeText() { return this.data; },
    splitText(o) {
        o = toU32(o); const d = N.text(this), len = d.length;
        if (o > len) throw new DOMException(`The offset ${o} is larger than the Text node's length.`, 'IndexSizeError');
        const doc = N.ownerDoc(this), t = N.type(this) === 4 ? N.cdata(d.slice(o), doc) : N.textNode(d.slice(o), doc), p = N.parent(this);
        if (p) {
            insertNode(p, t, N.next(this));
            if (liveRanges.size) { const i = idxOf(this) + 1; rangesEach(r => {
                if (r._sc === this && r._so > o) { r._sc = t; r._so -= o; } if (r._ec === this && r._eo > o) { r._ec = t; r._eo -= o; }
                if (r._sc === p && r._so === i) r._so++; if (r._ec === p && r._eo === i) r._eo++;
            }); }
        }
        replaceData(this, o, len - o, '');
        return t;
    },
    get assignedSlot() { return null; },
});
class CDATASection extends Text {}
class Comment extends CharacterData { constructor(s = '') { return N.comment(String(s)); } }
class DocumentType extends Node {}
methods(DocumentType.prototype, ChildNode);
methods(DocumentType.prototype, { get name() { return N.name(this); }, get publicId() { return N.attr(this, 'publicId') || ''; }, get systemId() { return N.attr(this, 'systemId') || ''; } });
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
    _get() { const v = N.attr(this._el, this._attr); return v ? [...new Set(v.split(/[\t\n\f\r ]+/).filter(Boolean))] : []; }
    _set(a) { if (!a.length && N.attr(this._el, this._attr) == null) return; this._el.setAttribute(this._attr, a.join(' ')); }
    static _empty(t) { if (!t) throw new DOMException('The token provided must not be empty.', 'SyntaxError'); }
    static _ws(t) { if (/[\t\n\f\r ]/.test(t)) throw new DOMException(`The token provided ('${t}') contains HTML space characters, which are not valid in tokens.`, 'InvalidCharacterError'); }
    static _chk(t) { t = String(t); DOMTokenList._empty(t); DOMTokenList._ws(t); return t; }
    get length() { return this._get().length; }
    get value() { return N.attr(this._el, this._attr) || ''; } set value(v) { this._el.setAttribute(this._attr, v); }
    item(i) { return this._get()[i] ?? null; }
    contains(t) { return this._get().includes(String(t)); }
    add(...ts) { ts = ts.map(DOMTokenList._chk); const a = this._get(); for (const t of ts) if (!a.includes(t)) a.push(t); this._set(a); }
    remove(...ts) { ts = ts.map(DOMTokenList._chk); this._set(this._get().filter(x => !ts.includes(x))); }
    toggle(t, force) {
        t = DOMTokenList._chk(t); const a = this._get();
        if (a.includes(t)) { if (force === undefined || !force) { this._set(a.filter(x => x !== t)); return false; } return true; }
        if (force === undefined || force) { a.push(t); this._set(a); return true; }
        return false;
    }
    replace(t, n) {
        t = String(t); n = String(n); DOMTokenList._empty(t); DOMTokenList._empty(n); DOMTokenList._ws(t); DOMTokenList._ws(n);
        const a = this._get(); if (!a.includes(t)) return false;
        const out = []; for (const x of a) { if (x === t || x === n) { if (!out.includes(n)) out.push(n); } else out.push(x); }
        this._set(out); return true;
    }
    supports(t) { if (this._attr === 'class') throw new TypeError(`DOMTokenList has no supported tokens.`); return true; }
    forEach(fn, self) { this._get().forEach((v, i) => fn.call(self, v, i, this)); }
    entries() { return this._get().entries(); } keys() { return this._get().keys(); } values() { return this._get().values(); }
    [Symbol.iterator]() { return this._get()[Symbol.iterator](); }
    toString() { return this.value; }
}
