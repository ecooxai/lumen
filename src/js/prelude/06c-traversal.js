/* NodeFilter is a legacy callback interface: its interface object is a function that only throws, with the constants
   and no prototype property */
const NodeFilter = (() => { const f = { NodeFilter() { throw new TypeError('Illegal constructor'); } }.NodeFilter; return f; })();
for (const [k, v] of Object.entries({ FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3, SHOW_ALL: 0xFFFFFFFF, SHOW_ELEMENT: 0x1, SHOW_ATTRIBUTE: 0x2, SHOW_TEXT: 0x4, SHOW_CDATA_SECTION: 0x8, SHOW_ENTITY_REFERENCE: 0x10, SHOW_ENTITY: 0x20, SHOW_PROCESSING_INSTRUCTION: 0x40, SHOW_COMMENT: 0x80, SHOW_DOCUMENT: 0x100, SHOW_DOCUMENT_TYPE: 0x200, SHOW_DOCUMENT_FRAGMENT: 0x400, SHOW_NOTATION: 0x800 })) Object.defineProperty(NodeFilter, k, { value: v, enumerable: true });
function travFilter(t, n) {
    if (t._active) throw new DOMException('The filter is already running.', 'InvalidStateError');
    if (!(t._what & (1 << (n.nodeType - 1)))) return 3;
    const f = t._filter; if (f == null) return 1;
    t._active = true;
    try {
        let r;
        if (typeof f === 'function') r = f.call(undefined, n);
        else {
            let m;
            try { m = f.acceptNode; } catch (e) { throw e instanceof TypeError ? cbTypeError(f, e.message) : e; }   /* a revoked proxy */
            if (typeof m !== 'function') throw cbTypeError(f, "Failed to execute 'acceptNode' on 'NodeFilter': The provided callback is not callable.");
            r = m.call(f, n);
        }
        return toU32(r) & 0xFFFF;
    } finally { t._active = false; }
}
function mkTraversal(C, root, what, filter) {
    needNode(root, C === TreeWalker ? 'createTreeWalker' : 'createNodeIterator');
    const t = Object.create(C.prototype);
    def(t, '_root', root); def(t, '_what', toU32(what)); def(t, '_filter', filter === undefined ? null : filter); def(t, '_active', false);
    if (C === TreeWalker) def(t, '_cur', root);
    else { def(t, '_ref', root); def(t, '_before', true); linkLiveSets(); liveIters.add(new WeakRef(t)); }
    return t;
}
class TreeWalker {
    constructor() { illegal(); }
    get root() { return this._root; } get whatToShow() { return this._what; } get filter() { return this._filter; }
    get currentNode() { return this._cur; }
    set currentNode(n) { needNode(n, 'currentNode'); this._cur = n; }
    parentNode() {
        let n = this._cur;
        while (n && n !== this._root) { n = n.parentNode; if (n && travFilter(this, n) === 1) { this._cur = n; return n; } }
        return null;
    }
    _children(first) {
        let n = first ? this._cur.firstChild : this._cur.lastChild;
        while (n) {
            const r = travFilter(this, n);
            if (r === 1) { this._cur = n; return n; }
            if (r === 3) { const c = first ? n.firstChild : n.lastChild; if (c) { n = c; continue; } }
            while (n) {
                const s = first ? n.nextSibling : n.previousSibling;
                if (s) { n = s; break; }
                const p = n.parentNode;
                if (p == null || p === this._root || p === this._cur) return null;
                n = p;
            }
        }
        return null;
    }
    firstChild() { return this._children(true); }
    lastChild() { return this._children(false); }
    _siblings(next) {
        let n = this._cur; if (n === this._root) return null;
        for (;;) {
            let s = next ? n.nextSibling : n.previousSibling;
            while (s) {
                n = s; const r = travFilter(this, n);
                if (r === 1) { this._cur = n; return n; }
                s = next ? n.firstChild : n.lastChild;
                if (r === 2 || !s) s = next ? n.nextSibling : n.previousSibling;
            }
            n = n.parentNode;
            if (n == null || n === this._root) return null;
            if (travFilter(this, n) === 1) return null;
        }
    }
    nextSibling() { return this._siblings(true); }
    previousSibling() { return this._siblings(false); }
    previousNode() {
        let n = this._cur;
        while (n !== this._root) {
            let s = n.previousSibling;
            while (s) {
                n = s; let r = travFilter(this, n);
                while (r !== 2 && n.lastChild) { n = n.lastChild; r = travFilter(this, n); }
                if (r === 1) { this._cur = n; return n; }
                s = n.previousSibling;
            }
            if (n === this._root || n.parentNode == null) return null;
            n = n.parentNode;
            if (travFilter(this, n) === 1) { this._cur = n; return n; }
        }
        return null;
    }
    nextNode() {
        let n = this._cur, r = 1;
        for (;;) {
            while (r !== 2 && n.firstChild) { n = n.firstChild; r = travFilter(this, n); if (r === 1) { this._cur = n; return n; } }
            let s = null, t = n;
            while (t) { if (t === this._root) return null; s = t.nextSibling; if (s) break; t = t.parentNode; }
            if (!s) return null;
            n = s; r = travFilter(this, n);
            if (r === 1) { this._cur = n; return n; }
        }
    }
}
const travFollowing = (n, root) => { if (n.firstChild) return n.firstChild; for (; n; n = n.parentNode) { if (n === root) return null; if (n.nextSibling) return n.nextSibling; } return null; };
const travPreceding = (n, root) => { if (n === root) return null; let s = n.previousSibling; if (s) { while (s.lastChild) s = s.lastChild; return s; } return n.parentNode; };
class NodeIterator {
    constructor() { illegal(); }
    get root() { return this._root; } get whatToShow() { return this._what; } get filter() { return this._filter; }
    get referenceNode() { return this._ref; } get pointerBeforeReferenceNode() { return this._before; }
    detach() {}
    _trav(next) {
        let n = this._ref, before = this._before;
        for (;;) {
            if (next) { if (!before) { n = travFollowing(n, this._root); if (!n) return null; } else before = false; }
            else { if (before) { n = travPreceding(n, this._root); if (!n) return null; } else before = true; }
            if (travFilter(this, n) === 1) { this._ref = n; this._before = before; return n; }
        }
    }
    nextNode() { return this._trav(true); }
    previousNode() { return this._trav(false); }
}
function itersPreRemove(c) {
    for (const w of liveIters) {
        const it = w.deref(); if (!it) { liveIters.delete(w); continue; }
        if (N.contains(c, it._root) || !N.contains(c, it._ref)) continue;
        if (it._before) {
            let t = c, next = null;
            for (; t; t = t.parentNode) { if (t === it._root) break; if (t.nextSibling) { next = t.nextSibling; break; } }
            if (next) { it._ref = next; continue; }
            it._before = false;
        }
        let p = c.previousSibling;
        if (p) { while (p.lastChild) p = p.lastChild; it._ref = p; } else it._ref = c.parentNode;
    }
}
const rootOf = (n) => { if (n instanceof Attr) return n; for (;;) { const p = N.parent(n); if (!p) return n; n = p; } };
const childAt = (n, i) => { let c = N.first(n); while (c && i-- > 0) c = N.next(c); return c; };
const nextSkip = (n) => { for (; n; n = N.parent(n)) { const s = N.next(n); if (s) return s; } return null; };
const nextTree = (n) => N.first(n) || nextSkip(n);
function precedes(x, y) {
    if (N.contains(x, y)) return true;
    if (N.contains(y, x)) return false;
    const ax = [], ay = [];
    for (let n = x; n; n = N.parent(n)) ax.push(n);
    for (let n = y; n; n = N.parent(n)) ay.push(n);
    let i = ax.length - 1, j = ay.length - 1;
    while (i >= 0 && j >= 0 && ax[i] === ay[j]) { i--; j--; }
    for (let s = N.next(ax[i]); s; s = N.next(s)) if (s === ay[j]) return true;
    return false;
}
function bpCompare(a, ao, b, bo) {
    if (a === b) return ao === bo ? 0 : ao < bo ? -1 : 1;
    if (precedes(b, a)) return -bpCompare(b, bo, a, ao);
    if (N.contains(a, b)) { let ch = b; while (N.parent(ch) !== a) ch = N.parent(ch); return idxOf(ch) < ao ? 1 : -1; }
    return -1;
}
const toU16 = (v) => { v = Number(v); if (!Number.isFinite(v)) return 0; v = Math.trunc(v) % 65536; return v < 0 ? v + 65536 : v; };
const rErr = (m, name) => new DOMException(m, name);
const needNode = (n, m, cls = 'Range') => { if (!N.isNode(n)) throw new TypeError(`Failed to execute '${m}' on '${cls}': parameter 1 is not of type 'Node'.`); };
const needArgs = (args, k, m, cls = 'Range') => { if (args.length < k) throw new TypeError(`Failed to execute '${m}' on '${cls}': ${k} arguments required, but only ${args.length} present.`); };
const noDoctype = (n) => { if (!(n instanceof Attr) && N.type(n) === 10) throw rErr("The node provided is of type 'DocumentType'.", 'InvalidNodeTypeError'); };
const checkOffset = (n, o) => { const L = n instanceof Attr ? 0 : nodeLength(n); if (o > L) throw rErr(`The offset ${o} is larger than the node's length (${L}).`, 'IndexSizeError'); };
/* a Range boundary may be an Attr (a JS object here, its own root with length 0): Range methods take one */
const rangeNode = (n, m) => { if (!(n instanceof Attr)) needNode(n, m); };
const cloneCD = (n, data) => { const c = N.clone(n, false); N.setText(c, data); return c; };
class AbstractRange {
    constructor() { if (new.target === AbstractRange) illegal(); }
    get startContainer() { return this._sc; } get startOffset() { if (this._el) oprSync(this); return this._so; }
    get endContainer() { return this._ec; } get endOffset() { if (this._el) oprSync(this); return this._eo; }
    get collapsed() { if (this._el) oprSync(this); return this._sc === this._ec && this._so === this._eo; }
}
/* OpaqueRange (Chrome's tentative createValueRange): a live range over an <input>/<textarea> value in UTF-16
   offsets, its containers undefined. Value edits move it as replaceData moves a Range; removing the control from its
   document, adopting it or changing an <input>'s type disconnects it (offsets 0, no geometry). */
const OPR_KEY = Symbol('OpaqueRange'), oprOf = new WeakMap();
class OpaqueRange extends AbstractRange {
    constructor(key = undefined) { if (key !== OPR_KEY) illegal(); super(); }
    getClientRects() {
        oprSync(this);
        const el = this._el;
        if (!el || this._so === this._eo) return [];
        const v = el.value;
        return textRects(el, u16Cp(v, this._so), u16Cp(v, this._eo));
    }
    getBoundingClientRect() {
        oprSync(this);
        const el = this._el;
        if (!el) return new DOMRect();
        if (this._so !== this._eo) return unionRect(this.getClientRects());
        const c = u16Cp(el.value, this._so), r = textRects(el, c, c);
        return r.length ? r[0] : new DOMRect();
    }
    disconnect() {
        const el = this._el;
        if (el) { const set = oprOf.get(el); if (set) set.delete(this); }
        this._el = null; this._so = this._eo = 0;
    }
}
function oprNew(el, s, e) {
    const r = new OpaqueRange(OPR_KEY);
    def(r, '_el', el); def(r, '_doc', el.ownerDocument); def(r, '_conn', N.connected(el));
    r._so = s; r._eo = e;
    let set = oprOf.get(el); if (!set) oprOf.set(el, set = new SSet());
    set.add(r);
    return r;
}
/* removal and adoption are noticed when the range is next used */
function oprSync(r) {
    const el = r._el, c = N.connected(el);
    if ((r._conn && !c) || el.ownerDocument !== r._doc) r.disconnect(); else r._conn = c;
}
function oprDisconnectAll(el) { const set = oprOf.get(el); if (set) for (const r of [...set]) r.disconnect(); }
/* the value's UTF-16 [s, e) became n code units: "replace data" on each range of the control */
function oprReplace(el, s, e, n) {
    const set = oprOf.get(el); if (!set) return;
    const move = o => o > s && o <= e ? s : o > e ? o + n - (e - s) : o;
    for (const r of [...set]) { oprSync(r); if (r._el === el) { r._so = move(r._so); r._eo = move(r._eo); } }
}
class StaticRange extends AbstractRange {
    constructor(init) {
        super();
        if (arguments.length < 1) throw new TypeError("Failed to construct 'StaticRange': 1 argument required, but only 0 present.");
        if (init == null || typeof init !== 'object') throw new TypeError("Failed to construct 'StaticRange': parameter 1 is not of type 'StaticRangeInit'.");
        for (const k of ['startContainer', 'startOffset', 'endContainer', 'endOffset']) if (init[k] === undefined) throw new TypeError(`Failed to construct 'StaticRange': required member ${k} is undefined.`);
        for (const k of ['startContainer', 'endContainer']) {
            const n = init[k];
            if (n instanceof Attr || (N.isNode(n) && N.type(n) === 10)) throw rErr("Failed to construct 'StaticRange': The supplied node is of an invalid type.", 'InvalidNodeTypeError');
            if (!N.isNode(n)) throw new TypeError(`Failed to construct 'StaticRange': member ${k} is not of type 'Node'.`);
        }
        this._sc = init.startContainer; this._so = toU32(init.startOffset); this._ec = init.endContainer; this._eo = toU32(init.endOffset);
    }
}
class Range extends AbstractRange {
    constructor() { super(); this._sc = this._ec = document; this._so = this._eo = 0; linkLiveSets(); liveRanges.add(new WeakRef(this)); }
    get commonAncestorContainer() { let n = this._sc; if (n === this._ec) return n; while (!N.contains(n, this._ec)) n = N.parent(n); return n; }
    _set(start, n, o) {
        noDoctype(n); checkOffset(n, o);
        if (start) { if (rootOf(n) !== rootOf(this._sc) || bpCompare(n, o, this._ec, this._eo) > 0) { this._ec = n; this._eo = o; } this._sc = n; this._so = o; }
        else { if (rootOf(n) !== rootOf(this._sc) || bpCompare(n, o, this._sc, this._so) < 0) { this._sc = n; this._so = o; } this._ec = n; this._eo = o; }
    }
    _parentOf(n, m) { rangeNode(n, m); const p = n instanceof Attr ? null : N.parent(n); if (!p) throw rErr('the given Node has no parent.', 'InvalidNodeTypeError'); return p; }
    setStart(n, o) { needArgs(arguments, 2, 'setStart'); rangeNode(n, 'setStart'); this._set(true, n, toU32(o)); }
    setEnd(n, o) { needArgs(arguments, 2, 'setEnd'); rangeNode(n, 'setEnd'); this._set(false, n, toU32(o)); }
    setStartBefore(n) { const p = this._parentOf(n, 'setStartBefore'); this._set(true, p, idxOf(n)); }
    setStartAfter(n) { const p = this._parentOf(n, 'setStartAfter'); this._set(true, p, idxOf(n) + 1); }
    setEndBefore(n) { const p = this._parentOf(n, 'setEndBefore'); this._set(false, p, idxOf(n)); }
    setEndAfter(n) { const p = this._parentOf(n, 'setEndAfter'); this._set(false, p, idxOf(n) + 1); }
    collapse(toStart = false) { if (toStart) { this._ec = this._sc; this._eo = this._so; } else { this._sc = this._ec; this._so = this._eo; } }
    selectNode(n) { const p = this._parentOf(n, 'selectNode'), i = idxOf(n); this._sc = this._ec = p; this._so = i; this._eo = i + 1; }
    selectNodeContents(n) { rangeNode(n, 'selectNodeContents'); noDoctype(n); this._sc = this._ec = n; this._so = 0; this._eo = n instanceof Attr ? 0 : nodeLength(n); }
    compareBoundaryPoints(how, src) {
        needArgs(arguments, 2, 'compareBoundaryPoints'); how = toU16(how);
        if (!(src instanceof Range)) throw new TypeError("Failed to execute 'compareBoundaryPoints' on 'Range': parameter 2 is not of type 'Range'.");
        if (how > 3) throw rErr('The comparison method provided must be one of START_TO_START, START_TO_END, END_TO_END, or END_TO_START.', 'NotSupportedError');
        if (rootOf(this._sc) !== rootOf(src._sc)) throw rErr('The source range is in a different document than this range.', 'WrongDocumentError');
        switch (how) {
        case 0: return bpCompare(this._sc, this._so, src._sc, src._so);
        case 1: return bpCompare(this._ec, this._eo, src._sc, src._so);
        case 2: return bpCompare(this._ec, this._eo, src._ec, src._eo);
        default: return bpCompare(this._sc, this._so, src._ec, src._eo);
        }
    }
    comparePoint(n, o) {
        needArgs(arguments, 2, 'comparePoint'); rangeNode(n, 'comparePoint'); o = toU32(o);
        if (rootOf(n) !== rootOf(this._sc)) throw rErr('The node provided and the Range are not in the same tree.', 'WrongDocumentError');
        noDoctype(n); checkOffset(n, o);
        if (bpCompare(n, o, this._sc, this._so) < 0) return -1;
        return bpCompare(n, o, this._ec, this._eo) > 0 ? 1 : 0;
    }
    isPointInRange(n, o) {
        needArgs(arguments, 2, 'isPointInRange'); rangeNode(n, 'isPointInRange'); o = toU32(o);
        if (rootOf(n) !== rootOf(this._sc)) return false;
        noDoctype(n); checkOffset(n, o);
        return bpCompare(n, o, this._sc, this._so) >= 0 && bpCompare(n, o, this._ec, this._eo) <= 0;
    }
    intersectsNode(n) {
        rangeNode(n, 'intersectsNode');
        if (rootOf(n) !== rootOf(this._sc)) return false;
        const p = n instanceof Attr ? null : N.parent(n); if (!p) return true;
        const i = idxOf(n);
        return bpCompare(p, i, this._ec, this._eo) < 0 && bpCompare(p, i + 1, this._sc, this._so) > 0;
    }
    _walk() {
        const sc = this._sc, ec = this._ec;
        const begin = isCD(sc) ? nextSkip(sc) : (childAt(sc, this._so) || nextSkip(sc));
        const stop = isCD(ec) ? ec : (childAt(ec, this._eo) || nextSkip(ec));
        return [begin, stop];
    }
    toString() {
        const sc = this._sc, ec = this._ec;
        if (sc instanceof Attr) return '';
        if (sc === ec && isTextNode(sc)) return N.text(sc).substring(this._so, this._eo);
        let s = isTextNode(sc) ? N.text(sc).substring(this._so) : '';
        if (!(sc === ec && this._so === this._eo)) { const [b, e] = this._walk(); for (let n = b; n && n !== e; n = nextTree(n)) if (isTextNode(n)) s += N.text(n); }
        if (isTextNode(ec)) s += N.text(ec).substring(0, this._eo);
        return s;
    }
    _split() {
        const sc = this._sc, ec = this._ec;
        let ca = sc; while (!N.contains(ca, ec)) ca = N.parent(ca);
        let fp = null, lp = null;
        if (!N.contains(sc, ec)) { fp = sc; while (N.parent(fp) !== ca) fp = N.parent(fp); }
        if (!N.contains(ec, sc)) { lp = ec; while (N.parent(lp) !== ca) lp = N.parent(lp); }
        const contained = [], stopC = lp || childAt(ec, this._eo);
        for (let c = fp ? N.next(fp) : childAt(sc, this._so); c && c !== stopC; c = N.next(c)) contained.push(c);
        if (contained.some(c => N.type(c) === 10)) throw hre('A DocumentType node cannot be part of a range\'s contents.');
        return { ca, fp, lp, contained };
    }
    _frag() { const s = this._sc instanceof Attr ? this._sc.ownerDocument || document : this._sc; return N.frag(N.ownerDoc(s) || s); }
    _sub(sc, so, ec, eo) { const r = Object.create(Range.prototype); r._sc = sc; r._so = so; r._ec = ec; r._eo = eo; return r; }
    cloneContents() {
        const f = this._frag();
        if (this.collapsed) return f;
        const sc = this._sc, so = this._so, ec = this._ec, eo = this._eo;
        if (sc === ec && isCD(sc)) { insertNode(f, cloneCD(sc, N.text(sc).substring(so, eo)), null); return f; }
        const { fp, lp, contained } = this._split();
        if (fp && isCD(fp)) insertNode(f, cloneCD(sc, N.text(sc).substring(so)), null);
        else if (fp) { const c = N.clone(fp, false); insertNode(f, c, null); insertNode(c, this._sub(sc, so, fp, nodeLength(fp)).cloneContents(), null); }
        for (const ch of contained) insertNode(f, N.clone(ch, true), null);
        if (lp && isCD(lp)) insertNode(f, cloneCD(ec, N.text(ec).substring(0, eo)), null);
        else if (lp) { const c = N.clone(lp, false); insertNode(f, c, null); insertNode(c, this._sub(lp, 0, ec, eo).cloneContents(), null); }
        return f;
    }
    _newPoint() {
        const sc = this._sc, ec = this._ec;
        if (N.contains(sc, ec)) return [sc, this._so];
        let ref = sc; while (N.parent(ref) && !N.contains(N.parent(ref), ec)) ref = N.parent(ref);
        return [N.parent(ref), idxOf(ref) + 1];
    }
    extractContents() {
        const f = this._frag();
        if (this.collapsed) return f;
        const sc = this._sc, so = this._so, ec = this._ec, eo = this._eo;
        if (sc === ec && isCD(sc)) { insertNode(f, cloneCD(sc, N.text(sc).substring(so, eo)), null); replaceData(sc, so, eo - so, ''); return f; }
        const { fp, lp, contained } = this._split();
        const [nn, no] = this._newPoint();
        if (fp && isCD(fp)) { insertNode(f, cloneCD(sc, N.text(sc).substring(so)), null); replaceData(sc, so, nodeLength(sc) - so, ''); }
        else if (fp) { const c = N.clone(fp, false); insertNode(f, c, null); insertNode(c, this._sub(sc, so, fp, nodeLength(fp)).extractContents(), null); }
        for (const ch of contained) insertNode(f, ch, null);
        if (lp && isCD(lp)) { insertNode(f, cloneCD(ec, N.text(ec).substring(0, eo)), null); replaceData(ec, 0, eo, ''); }
        else if (lp) { const c = N.clone(lp, false); insertNode(f, c, null); insertNode(c, this._sub(lp, 0, ec, eo).extractContents(), null); }
        this._sc = this._ec = nn; this._so = this._eo = no;
        return f;
    }
    deleteContents() {
        if (this.collapsed) return;
        const sc = this._sc, so = this._so, ec = this._ec, eo = this._eo;
        if (sc === ec && isCD(sc)) { replaceData(sc, so, eo - so, ''); return; }
        const rm = [], [b, e] = this._walk();
        for (let n = b; n && n !== e;) { if (N.contains(n, ec)) { n = nextTree(n); continue; } rm.push(n); n = nextSkip(n); }
        const [nn, no] = this._newPoint();
        if (isCD(sc)) replaceData(sc, so, nodeLength(sc) - so, '');
        for (const n of rm) if (N.parent(n)) removeNode(n);
        if (isCD(ec)) replaceData(ec, 0, eo, '');
        this._sc = this._ec = nn; this._so = this._eo = no;
    }
    insertNode(node) {
        rangeNode(node, 'insertNode');
        if (this._sc instanceof Attr || node instanceof Attr) throw hre('Nodes of this type may not contain children.');
        const sc = this._sc, t = N.type(sc);
        if (t === 7 || t === 8 || (isTextNode(sc) && !N.parent(sc)) || sc === node) throw hre('The Range starts in a node that cannot have children inserted.');
        let ref = isTextNode(sc) ? sc : childAt(sc, this._so);
        const parent = ref ? N.parent(ref) : sc;
        preInsertCheck(parent, node, ref);
        if (isTextNode(sc)) ref = sc.splitText(this._so);
        if (node === ref) ref = N.next(ref);
        if (N.parent(node)) removeNode(node);
        let off = ref ? idxOf(ref) : nodeLength(parent);
        off += N.type(node) === 11 ? nodeLength(node) : 1;
        insertNode(parent, node, ref);
        if (this.collapsed) { this._ec = parent; this._eo = off; }
    }
    surroundContents(np) {
        rangeNode(np, 'surroundContents');
        const sc = this._sc, ec = this._ec;
        if (sc instanceof Attr || np instanceof Attr) throw hre('Nodes of this type may not contain children.');
        for (let n = sc; n && !N.contains(n, ec); n = N.parent(n)) if (!isTextNode(n)) throw rErr('The Range has partially selected a non-Text node.', 'InvalidStateError');
        for (let n = ec; n && !N.contains(n, sc); n = N.parent(n)) if (!isTextNode(n)) throw rErr('The Range has partially selected a non-Text node.', 'InvalidStateError');
        const t = N.type(np); if (t === 9 || t === 10 || t === 11) throw rErr("The node provided is of an invalid type.", 'InvalidNodeTypeError');
        const f = this.extractContents();
        while (N.first(np)) removeNode(N.first(np));
        this.insertNode(np);
        insertNode(np, f, null);
        this.selectNode(np);
    }
    cloneRange() { const r = new Range(); r._sc = this._sc; r._so = this._so; r._ec = this._ec; r._eo = this._eo; return r; }
    detach() {}
    createContextualFragment(html) {
        const doc = N.type(this._sc) === 9 ? this._sc : N.ownerDoc(this._sc);
        const xml = !!doc && (N.ctype(doc) || (N.ns(doc) === 3 ? 'application/xml' : 'text/html')) !== 'text/html';
        const a = ttCompliantHTML(html, htmlSinkOptions(true), 'Range createContextualFragment', xml);
        let el = this._sc; if (N.type(el) !== 1) el = N.parent(el);
        if (!el || N.type(el) !== 1 || N.name(el) === 'html') el = document.body;
        return xml ? N.parseFrag(el, a.html) : legacyHTMLFragment(el, a.html, a.options, 'Range createContextualFragment');
    }
    /* CSSOM View: the border boxes of the elements the range contains (not those inside one of them) and the
       selected part of each Text node's boxes, a zero-width caret box where the range is collapsed in one */
    getClientRects() { return boundsRects(this._sc, this._so, this._ec, this._eo); }
    getBoundingClientRect() { return unionRect(this.getClientRects()); }
}
function boundsRects(sc, so, ec, eo) {
        const out = [];
        if (sc instanceof Attr || ec instanceof Attr) return out;
        if (isTextNode(sc) && sc === ec) return textRects(sc, so, eo);
        const after = n => { for (; n; n = N.parent(n)) { const x = N.next(n); if (x) return x; } return null; };
        const startT = N.type(sc), endT = N.type(ec);
        const charData = t => t === 3 || t === 4 || t === 7 || t === 8;
        let n = charData(startT) ? sc : childAt(sc, so) || after(sc);
        const endData = charData(endT), stop = endData ? null : childAt(ec, eo) || after(ec);
        const contained = new SSet();
        for (let k = 0; n && n !== stop && k < 100000; n = nextIn(n, null), k++) {
            const t = N.type(n);
            if (t === 3 || t === 4) { for (const r of textRects(n, n === sc ? so : 0, n === ec ? eo : N.text(n).length)) out.push(r); }
            else if (t === 1 && !N.contains(n, ec)) {
                contained.add(n);
                if (!contained.has(N.parent(n))) for (const r of n.getClientRects()) out.push(r);
            }
            if (endData && n === ec) break;
        }
        return out;
}
/* the rect around a list: the first when all are empty, else around the non-empty ones */
function unionRect(rs) {
    if (!rs.length) return new DOMRect();
    const ne = rs.filter(r => r.width && r.height);
    if (!ne.length) return DOMRect.fromRect(rs[0]);
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const r of ne) { x0 = Math.min(x0, r.left); y0 = Math.min(y0, r.top); x1 = Math.max(x1, r.right); y1 = Math.max(y1, r.bottom); }
    return new DOMRect(x0, y0, x1 - x0, y1 - y0);
}
/* viewport rects of a text node's UTF-16 range, or of a text control's value codepoints (the layout gives document
   coordinates; the nearest element with a box gives the scroll and transforms between) */
function textRects(node, s, e) {
    let el = N.type(node) === 1 ? node : N.parent(node);
    while (el && N.type(el) === 1 && !N.rect(el)) el = N.parent(el);
    if (!el || N.type(el) !== 1) return [];
    const r = N.textRects(node, s, e);
    if (!r.length) return [];
    const a = N.rect(el), b = N.vrect(el);
    if (!a || !b) return [];
    const dx = b[0] - a[0], dy = b[1] - a[1], out = [];
    for (let i = 0; i < r.length; i += 4) out.push(new DOMRect(r[i] + dx, r[i + 1] + dy, r[i + 2], r[i + 3]));
    return out;
}
for (const [k, v] of [['START_TO_START', 0], ['START_TO_END', 1], ['END_TO_END', 2], ['END_TO_START', 3]]) {
    Object.defineProperty(Range, k, { value: v, enumerable: true });
    Object.defineProperty(Range.prototype, k, { value: v, enumerable: true });
}
const utf8Off = (s, o) => { let b = 0; for (let i = 0; i < o && i < s.length; i++) { const c = s.charCodeAt(i); b += c < 0x80 ? 1 : c < 0x800 ? 2 : c >= 0xd800 && c < 0xdc00 ? (i++, 4) : 3; } return b; };
const utf16Off = (s, b) => { let i = 0; for (let k = 0; i < s.length && k < b; i++) { const c = s.charCodeAt(i); k += c < 0x80 ? 1 : c < 0x800 ? 2 : c >= 0xd800 && c < 0xdc00 ? (i++, 4) : 3; } return i; };
class Selection {
    constructor() { this._r = null; this._back = false; }
    get _r() { return this.__r; } set _r(v) { this.__r = v; this._sync(); }
    get _back() { return this.__back; } set _back(v) { this.__back = v; this._sync(); }
    _sync() {
        const r = this.__r, b = (n, o) => n && n.nodeType === 3 ? utf8Off(n.data, o) : o;
        if (N.selSet) N.selSet(r ? r._sc : null, r ? b(r._sc, r._so) : 0, r ? r._ec : null, r ? b(r._ec, r._eo) : 0, !!this.__back);
    }
    get rangeCount() { return this._r ? 1 : 0; }
    get anchorNode() { const r = this._r; return r ? (this._back ? r._ec : r._sc) : null; }
    get anchorOffset() { const r = this._r; return r ? (this._back ? r._eo : r._so) : 0; }
    get focusNode() { const r = this._r; return r ? (this._back ? r._sc : r._ec) : null; }
    get focusOffset() { const r = this._r; return r ? (this._back ? r._so : r._eo) : 0; }
    get isCollapsed() { return !this._r || this._r.collapsed; }
    get type() { return !this._r ? 'None' : this._r.collapsed ? 'Caret' : 'Range'; }
    get direction() { return !this._r || this._r.collapsed ? 'none' : this._back ? 'backward' : 'forward'; }
    getRangeAt(i) { if (toU32(i) !== 0 || !this._r) throw rErr(`The index provided (${i}) is greater than or equal to the maximum bound (${this.rangeCount}).`, 'IndexSizeError'); return this._r; }
    addRange(r) { if (!(r instanceof Range) && !(r !== null && typeof r === 'object' && '_sc' in r && typeof r.collapse === 'function')) throw new TypeError("Failed to execute 'addRange' on 'Selection': parameter 1 is not of type 'Range'."); if (this._r || rootOf(r._sc) !== document) return; this._r = r; this._back = false; }
    removeRange(r) { if (!(r instanceof Range)) throw new TypeError("Failed to execute 'removeRange' on 'Selection': parameter 1 is not of type 'Range'."); if (r !== this._r) throw rErr('The given range isn\'t in document.', 'NotFoundError'); this._r = null; }
    removeAllRanges() { this._r = null; } empty() { this._r = null; }
    _point(n, o, m) { needNode(n, m, 'Selection'); noDoctype(n); o = toU32(o); checkOffset(n, o); return o; }
    collapse(n, o = 0) {
        if (n === null) { this._r = null; return; }
        o = this._point(n, o, 'collapse');
        if (rootOf(n) !== document) return;
        const r = new Range(); r._sc = r._ec = n; r._so = r._eo = o; this._r = r; this._back = false;
    }
    setPosition(n, o = 0) { this.collapse(n, o); }
    collapseToStart() { if (!this._r) throw rErr('There is no selection to collapse.', 'InvalidStateError'); this.collapse(this._r._sc, this._r._so); }
    collapseToEnd() { if (!this._r) throw rErr('There is no selection to collapse.', 'InvalidStateError'); this.collapse(this._r._ec, this._r._eo); }
    extend(n, o = 0) {
        needNode(n, 'extend', 'Selection');
        if (rootOf(n) !== document) return;
        if (!this._r) throw rErr('This Selection object doesn\'t have any Ranges.', 'InvalidStateError');
        o = this._point(n, o, 'extend');
        const an = this.anchorNode, ao = this.anchorOffset, r = new Range();
        if (rootOf(an) !== rootOf(n) || bpCompare(an, ao, n, o) <= 0) { r._sc = an; r._so = ao; r._ec = n; r._eo = o; this._back = false; if (rootOf(an) !== rootOf(n)) { r._sc = n; r._so = o; } }
        else { r._sc = n; r._so = o; r._ec = an; r._eo = ao; this._back = true; }
        this._r = r;
    }
    setBaseAndExtent(an, ao, fn, fo) {
        needArgs(arguments, 4, 'setBaseAndExtent', 'Selection');
        ao = this._point(an, ao, 'setBaseAndExtent'); fo = this._point(fn, fo, 'setBaseAndExtent');
        if (rootOf(an) !== document || rootOf(fn) !== document) return;
        const r = new Range();
        if (bpCompare(an, ao, fn, fo) <= 0) { r._sc = an; r._so = ao; r._ec = fn; r._eo = fo; this._back = false; }
        else { r._sc = fn; r._so = fo; r._ec = an; r._eo = ao; this._back = true; }
        this._r = r;
    }
    selectAllChildren(n) { needNode(n, 'selectAllChildren', 'Selection'); noDoctype(n); if (rootOf(n) !== document) return; const r = new Range(); r._sc = r._ec = n; r._so = 0; r._eo = kids(n).length; this._r = r; this._back = false; }
    deleteFromDocument() { if (this._r) this._r.deleteContents(); }
    containsNode(n, partial = false) {
        needNode(n, 'containsNode', 'Selection');
        const r = this._r; if (!r || rootOf(n) !== rootOf(r._sc)) return false;
        const L = nodeLength(n);
        return partial ? bpCompare(n, 0, r._ec, r._eo) <= 0 && bpCompare(n, L, r._sc, r._so) >= 0
            : bpCompare(r._sc, r._so, n, 0) <= 0 && bpCompare(r._ec, r._eo, n, L) >= 0;
    }
    modify() {}
    getComposedRanges() { const r = this._r; return r ? [new StaticRange({ startContainer: r._sc, startOffset: r._so, endContainer: r._ec, endOffset: r._eo })] : []; }
    /* the selected text as rendered, as in Chrome: a line break between blocks and for each <br> */
    toString() {
        const r = this._r; if (!r) return '';
        const root = r.commonAncestorContainer;
        if (root.nodeType !== 1 && root.nodeType !== 9 && root.nodeType !== 11) return r.toString();
        const disp = e => { try { return getComputedStyle(e).display || ''; } catch (_) { return ''; } };
        const blockOf = n => {
            for (let e = n.parentNode; e && e !== root; e = e.parentNode) {
                if (e.nodeType !== 1) continue;
                const d = disp(e);
                if (d && !d.startsWith('inline') && d !== 'contents') return e;
            }
            return root;
        };
        let out = '', prev = null;
        const brk = b => { if (prev && b !== prev && out && !out.endsWith('\n')) out += '\n'; prev = b; };
        const tw = document.createTreeWalker(root, 5);
        for (let n = tw.nextNode(); n; n = tw.nextNode()) {
            if (!this.containsNode(n, true)) continue;
            if (n.nodeType === 1) { if (n.localName === 'br' && n !== r._ec) { brk(blockOf(n)); out += '\n'; } continue; }
            const e = n.parentNode; if (e && e.nodeType === 1 && disp(e) === 'none') continue;
            let t = n.data;
            if (n === r._ec) t = t.slice(0, r._eo);
            if (n === r._sc) t = t.slice(r._so);
            if (!t) continue;
            brk(blockOf(n)); out += t;
        }
        return out;
    }
}
const selection = new Selection();
const XML_TYPES = ['text/xml', 'application/xml', 'application/xhtml+xml', 'image/svg+xml'];
function parseXML(src, type) {
    const doc = N.newXmlDoc(type, true, true);
    N.parseXml(doc, src);
    return doc;
}
class DOMParser {
    parseFromString(s, type) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'parseFromString' on 'DOMParser': 2 arguments required, but only ${arguments.length} present.`);
        if (!ttIsHTML(s)) s = String(s);
        type = String(type);
        if (type !== 'text/html' && !XML_TYPES.includes(type)) throw new TypeError(`Failed to execute 'parseFromString' on 'DOMParser': The provided value '${type}' is not a valid enum value of type DOMParserSupportedType.`);
        s = ttString(TrustedHTML, s, 'DOMParser parseFromString');
        return type === 'text/html' ? N.parseDoc(s) : parseXML(s, type);
    }
}
const xmlEscT = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const xmlEscA = (s) => s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/\t/g, '&#9;').replace(/\n/g, '&#10;').replace(/\r/g, '&#13;');
function xmlDocOf(n) { const d = n.ownerDocument; return !!d && d.contentType !== 'text/html'; }
function xmlSer(n, dns, map) {
    switch (N.type(n)) {
    case 1: {
        const ns = n.namespaceURI, pfx = n.prefix, q = pfx ? pfx + ':' + n.localName : n.localName;
        let s = '<' + q, m = map;
        const put = (p, u) => { if (m === map) m = Object.assign(Object.create(null), map); m[p] = u; };
        if (pfx) { if (m[pfx] !== ns) { s += ` xmlns:${pfx}="${xmlEscA(ns ?? '')}"`; put(pfx, ns); } }
        else if (ns !== dns) { s += ` xmlns="${xmlEscA(ns ?? '')}"`; dns = ns; }
        for (const k of n.getAttributeNames()) {
            const v = n.getAttribute(k);
            if (k === 'xmlns') continue;
            if (k.startsWith('xmlns:')) { const p = k.slice(6); if (p === pfx || m[p] === v) continue; put(p, v); }
            s += ` ${k}="${xmlEscA(v)}"`;
        }
        const src = n.localName === 'template' && n.content ? n.content : n;
        let body = ''; for (let c = N.first(src); c; c = N.next(c)) body += xmlSer(c, dns, m);
        if (body) return s + '>' + body + '</' + q + '>';
        if (ns === NSURI[0]) return html_void.has(n.localName) ? s + ' />' : s + '></' + q + '>';
        return s + '/>';
    }
    case 3: return xmlEscT(N.text(n));
    case 4: return '<![CDATA[' + N.text(n) + ']]>';
    case 7: return '<?' + N.name(n) + ' ' + N.text(n) + '?>';
    case 8: return '<!--' + N.text(n) + '-->';
    case 10: { const p = n.publicId, sy = n.systemId; return '<!DOCTYPE ' + n.name + (p ? ` PUBLIC "${p}"` : sy ? ' SYSTEM' : '') + (sy ? ` "${sy}"` : '') + '>'; }
    default: { let s = ''; for (let c = N.first(n); c; c = N.next(c)) s += xmlSer(c, dns, map); return s; }
    }
}
const html_void = new SSet(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'source', 'track', 'wbr']);
class XMLSerializer { serializeToString(n) { if (!N.isNode(n)) throw new TypeError("Failed to execute 'serializeToString' on 'XMLSerializer': parameter 1 is not of type 'Node'."); return xmlSer(n, null, Object.assign(Object.create(null), { xml: XML_NS })); } }

/* CSS Custom Highlight API. Highlight is setlike and HighlightRegistry maplike over Set/Map intrinsics captured
   here, so a page's changes to Set.prototype or Map.prototype cannot break them */
const HL = (() => {
    const ap = Reflect.apply, SP = Set.prototype, MP = Map.prototype;
    const f = (P, k) => { const v = P[k]; return (o, ...a) => ap(v, o, a); };
    const sz = P => { const g = Object.getOwnPropertyDescriptor(P, 'size').get; return o => ap(g, o, []); };
    return {
        sAdd: f(SP, 'add'), sHas: f(SP, 'has'), sDel: f(SP, 'delete'), sClear: f(SP, 'clear'), sValues: f(SP, 'values'), sEntries: f(SP, 'entries'), sSize: sz(SP),
        mGet: f(MP, 'get'), mSet: f(MP, 'set'), mHas: f(MP, 'has'), mDel: f(MP, 'delete'), mClear: f(MP, 'clear'), mKeys: f(MP, 'keys'), mValues: f(MP, 'values'), mEntries: f(MP, 'entries'), mSize: sz(MP),
        next: f(Object.getPrototypeOf(new SSet().values()), 'next'), mNext: f(Object.getPrototypeOf(new SMap().values()), 'next'),
    };
})();
const HL_TYPES = ['highlight', 'spelling-error', 'grammar-error'];
const hlRange = (r, m, i = 1) => { if (!(r instanceof AbstractRange)) throw new TypeError(`Failed to ${m}: parameter ${i} is not of type 'AbstractRange'.`); return r; };
const hlCallable = (cb, m) => { if (typeof cb !== 'function') throw new TypeError(`Failed to execute 'forEach' on '${m}': The callback provided as parameter 1 is not a function.`); };
let hlSetOf;
class Highlight {
    #set = new SSet(); #priority = 0; #type = 'highlight';
    constructor(...ranges) {
        for (let i = 0; i < ranges.length; i++) HL.sAdd(this.#set, hlRange(ranges[i], "construct 'Highlight'", i + 1));
    }
    get priority() { return this.#priority; }
    set priority(v) { this.#priority = Number(v) | 0; }
    get type() { return this.#type; }
    set type(v) { v = String(v); if (HL_TYPES.includes(v)) this.#type = v; }
    get size() { return HL.sSize(this.#set); }
    has(r) { return HL.sHas(this.#set, hlRange(r, "execute 'has' on 'Highlight'")); }
    add(r) { HL.sAdd(this.#set, hlRange(r, "execute 'add' on 'Highlight'")); return this; }
    delete(r) { return HL.sDel(this.#set, hlRange(r, "execute 'delete' on 'Highlight'")); }
    clear() { HL.sClear(this.#set); }
    entries() { return HL.sEntries(this.#set); }
    values() { return HL.sValues(this.#set); }
    forEach(cb, thisArg = undefined) {
        hlCallable(cb, 'Highlight');
        const it = HL.sValues(this.#set);
        for (let x = HL.next(it); !x.done; x = HL.next(it)) Reflect.apply(cb, thisArg, [x.value, x.value, this]);
    }
    static { hlSetOf = h => h.#set; }
}
Object.defineProperty(Highlight.prototype, 'keys', { value: Highlight.prototype.values, writable: true, configurable: true });
Object.defineProperty(Highlight.prototype, Symbol.iterator, { value: Highlight.prototype.values, writable: true, configurable: true });
const HLR_KEY = Symbol('HighlightRegistry');
class HighlightRegistry {
    #map = new SMap();
    constructor(key = undefined) { if (key !== HLR_KEY) illegal(); }
    get size() { return HL.mSize(this.#map); }
    get(k) { return HL.mGet(this.#map, String(k)); }
    has(k) { return HL.mHas(this.#map, String(k)); }
    set(k, h) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'set' on 'HighlightRegistry': 2 arguments required, but only ${arguments.length} present.`);
        k = String(k);
        if (!(h instanceof Highlight)) throw new TypeError("Failed to execute 'set' on 'HighlightRegistry': parameter 2 is not of type 'Highlight'.");
        HL.mSet(this.#map, k, h); hlChanged(); return this;
    }
    delete(k) { const r = HL.mDel(this.#map, String(k)); if (r) hlChanged(); return r; }
    clear() { HL.mClear(this.#map); hlChanged(); }
    entries() { return HL.mEntries(this.#map); }
    keys() { return HL.mKeys(this.#map); }
    values() { return HL.mValues(this.#map); }
    forEach(cb, thisArg = undefined) {
        hlCallable(cb, 'HighlightRegistry');
        const it = HL.mEntries(this.#map);
        for (let x = HL.mNext(it); !x.done; x = HL.mNext(it)) Reflect.apply(cb, thisArg, [x.value[1], x.value[0], this]);
    }
    /* the highlights with a non-collapsed, valid range whose client rects contain the viewport point: highest
       priority first, then the later registered */
    highlightsFromPoint(x, y, options = undefined) {
        const m = "Failed to execute 'highlightsFromPoint' on 'HighlightRegistry'";
        if (arguments.length < 2) throw new TypeError(`${m}: 2 arguments required, but only ${arguments.length} present.`);
        x = Number(x); y = Number(y);
        if (!Number.isFinite(x) || !Number.isFinite(y)) throw new TypeError(`${m}: The provided float value is non-finite.`);
        if (options !== undefined && options !== null && typeof options !== 'object' && typeof options !== 'function') throw new TypeError(`${m}: The provided value is not of type 'HighlightsFromPointOptions'.`);
        const roots = [];
        if (options != null && options.shadowRoots !== undefined) {
            const sr = options.shadowRoots;
            if (sr == null || typeof sr[Symbol.iterator] !== 'function') throw new TypeError(`${m}: The provided value cannot be converted to a sequence.`);
            for (const r of sr) { if (!(r instanceof ShadowRoot)) throw new TypeError(`${m}: Failed to convert value to 'ShadowRoot'.`); roots.push(r); }
        }
        const de = document.documentElement;
        if (!de || !N.rect(de) || x < 0 || y < 0 || x > de.clientWidth || y > de.clientHeight) return [];
        const visible = n => { const r = rootOf(n); return r === document || roots.includes(r); };
        const out = [];
        let i = 0;
        for (const [, h] of this.entries()) {
            const ranges = [], order = i++;
            for (const r of hlSetOf(h)) {
                if (r.collapsed) continue;
                let rects;
                if (r instanceof OpaqueRange) { if (!r._el || !visible(r._el)) continue; rects = r.getClientRects(); }
                else {
                    const sc = r.startContainer, ec = r.endContainer, so = r.startOffset, eo = r.endOffset;
                    if (sc instanceof Attr || ec instanceof Attr || !visible(sc) || rootOf(sc) !== rootOf(ec)) continue;
                    if (so > nodeLength(sc) || eo > nodeLength(ec)) continue;
                    if (!(r instanceof Range) && bpCompare(sc, so, ec, eo) > 0) continue;
                    rects = boundsRects(sc, so, ec, eo);
                }
                for (const b of rects) if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) { ranges.push(r); break; }
            }
            if (ranges.length) out.push({ h, ranges, order });
        }
        out.sort((a, b) => b.h.priority - a.h.priority || b.order - a.order);
        return out.map(e => ({ highlight: e.h, ranges: e.ranges }));
    }
}
Object.defineProperty(HighlightRegistry.prototype, Symbol.iterator, { value: HighlightRegistry.prototype.entries, writable: true, configurable: true });
for (const C of [Highlight, HighlightRegistry]) Object.defineProperty(C.prototype, Symbol.toStringTag, { value: C.name, configurable: true });
const highlightRegistry = new HighlightRegistry(HLR_KEY);
function hlChanged() {}
Object.defineProperty(CSS, 'highlights', { get() { return highlightRegistry; }, enumerable: true, configurable: true });
