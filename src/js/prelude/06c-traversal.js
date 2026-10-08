class TreeWalker {
    constructor(root, what, filter) { this.root = root; this.whatToShow = what; this.filter = filter; this.currentNode = root; }
    _ok(n) { if (!((this.whatToShow >>> (N.type(n) - 1)) & 1)) return 3; if (!this.filter) return 1; return typeof this.filter === 'function' ? this.filter(n) : this.filter.acceptNode(n); }
    _next(n) { const f = N.first(n); if (f) return f; for (; n && n !== this.root; n = N.parent(n)) { const s = N.next(n); if (s) return s; } return null; }
    nextNode() { for (let n = this._next(this.currentNode); n; n = this._next(n)) if (this._ok(n) === 1) return this.currentNode = n; return null; }
    parentNode() { for (let n = this.currentNode; n && n !== this.root;) { n = N.parent(n); if (n && this._ok(n) === 1) return this.currentNode = n; } return null; }
    firstChild() { for (let c = N.first(this.currentNode); c; c = N.next(c)) if (this._ok(c) === 1) return this.currentNode = c; return null; }
    nextSibling() { for (let c = N.next(this.currentNode); c; c = N.next(c)) if (this._ok(c) === 1) return this.currentNode = c; return null; }
}
class NodeIterator extends TreeWalker { nextNode() { if (!this._started) { this._started = true; if (this._ok(this.root) === 1) return this.root; } return super.nextNode(); } detach() {} }
const NodeFilter = { FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3, SHOW_ALL: 0xFFFFFFFF, SHOW_ELEMENT: 1, SHOW_TEXT: 4, SHOW_COMMENT: 128 };
const rootOf = (n) => { for (;;) { const p = N.parent(n); if (!p) return n; n = p; } };
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
const noDoctype = (n) => { if (N.type(n) === 10) throw rErr("The node provided is of type 'DocumentType'.", 'InvalidNodeTypeError'); };
const checkOffset = (n, o) => { const L = nodeLength(n); if (o > L) throw rErr(`The offset ${o} is larger than the node's length (${L}).`, 'IndexSizeError'); };
const cloneCD = (n, data) => { const c = N.clone(n, false); N.setText(c, data); return c; };
class AbstractRange {
    constructor() { if (new.target === AbstractRange) illegal(); }
    get startContainer() { return this._sc; } get startOffset() { return this._so; }
    get endContainer() { return this._ec; } get endOffset() { return this._eo; }
    get collapsed() { return this._sc === this._ec && this._so === this._eo; }
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
    constructor() { super(); this._sc = this._ec = document; this._so = this._eo = 0; liveRanges.add(new WeakRef(this)); }
    get commonAncestorContainer() { let n = this._sc; while (!N.contains(n, this._ec)) n = N.parent(n); return n; }
    _set(start, n, o) {
        noDoctype(n); checkOffset(n, o);
        if (start) { if (rootOf(n) !== rootOf(this._sc) || bpCompare(n, o, this._ec, this._eo) > 0) { this._ec = n; this._eo = o; } this._sc = n; this._so = o; }
        else { if (rootOf(n) !== rootOf(this._sc) || bpCompare(n, o, this._sc, this._so) < 0) { this._sc = n; this._so = o; } this._ec = n; this._eo = o; }
    }
    _parentOf(n, m) { needNode(n, m); const p = N.parent(n); if (!p) throw rErr('the given Node has no parent.', 'InvalidNodeTypeError'); return p; }
    setStart(n, o) { needArgs(arguments, 2, 'setStart'); needNode(n, 'setStart'); this._set(true, n, toU32(o)); }
    setEnd(n, o) { needArgs(arguments, 2, 'setEnd'); needNode(n, 'setEnd'); this._set(false, n, toU32(o)); }
    setStartBefore(n) { const p = this._parentOf(n, 'setStartBefore'); this._set(true, p, idxOf(n)); }
    setStartAfter(n) { const p = this._parentOf(n, 'setStartAfter'); this._set(true, p, idxOf(n) + 1); }
    setEndBefore(n) { const p = this._parentOf(n, 'setEndBefore'); this._set(false, p, idxOf(n)); }
    setEndAfter(n) { const p = this._parentOf(n, 'setEndAfter'); this._set(false, p, idxOf(n) + 1); }
    collapse(toStart = false) { if (toStart) { this._ec = this._sc; this._eo = this._so; } else { this._sc = this._ec; this._so = this._eo; } }
    selectNode(n) { const p = this._parentOf(n, 'selectNode'), i = idxOf(n); this._sc = this._ec = p; this._so = i; this._eo = i + 1; }
    selectNodeContents(n) { needNode(n, 'selectNodeContents'); noDoctype(n); this._sc = this._ec = n; this._so = 0; this._eo = nodeLength(n); }
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
        needArgs(arguments, 2, 'comparePoint'); needNode(n, 'comparePoint'); o = toU32(o);
        if (rootOf(n) !== rootOf(this._sc)) throw rErr('The node provided and the Range are not in the same tree.', 'WrongDocumentError');
        noDoctype(n); checkOffset(n, o);
        if (bpCompare(n, o, this._sc, this._so) < 0) return -1;
        return bpCompare(n, o, this._ec, this._eo) > 0 ? 1 : 0;
    }
    isPointInRange(n, o) {
        needArgs(arguments, 2, 'isPointInRange'); needNode(n, 'isPointInRange'); o = toU32(o);
        if (rootOf(n) !== rootOf(this._sc)) return false;
        noDoctype(n); checkOffset(n, o);
        return bpCompare(n, o, this._sc, this._so) >= 0 && bpCompare(n, o, this._ec, this._eo) <= 0;
    }
    intersectsNode(n) {
        needNode(n, 'intersectsNode');
        if (rootOf(n) !== rootOf(this._sc)) return false;
        const p = N.parent(n); if (!p) return true;
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
    _frag() { return N.frag(N.ownerDoc(this._sc) || this._sc); }
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
        needNode(node, 'insertNode');
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
        needNode(np, 'surroundContents');
        const sc = this._sc, ec = this._ec;
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
        let el = this._sc; if (N.type(el) !== 1) el = N.parent(el);
        if (!el || N.type(el) !== 1 || N.name(el) === 'html') el = document.body;
        return N.parseFrag(el, String(html));
    }
    getBoundingClientRect() {
        let el = this._sc; if (N.type(el) !== 1) el = N.parent(el);
        if (N.type(this._sc) === 1 && this._sc === this._ec && this._eo === this._so + 1) el = childAt(this._sc, this._so);
        return el && N.type(el) === 1 ? el.getBoundingClientRect() : new DOMRect();
    }
    getClientRects() { const r = this.getBoundingClientRect(); return r.width || r.height ? [r] : []; }
}
for (const [k, v] of [['START_TO_START', 0], ['START_TO_END', 1], ['END_TO_END', 2], ['END_TO_START', 3]]) {
    Object.defineProperty(Range, k, { value: v, enumerable: true });
    Object.defineProperty(Range.prototype, k, { value: v, enumerable: true });
}
class Selection {
    constructor() { this._r = null; this._back = false; }
    get rangeCount() { return this._r ? 1 : 0; }
    get anchorNode() { const r = this._r; return r ? (this._back ? r._ec : r._sc) : null; }
    get anchorOffset() { const r = this._r; return r ? (this._back ? r._eo : r._so) : 0; }
    get focusNode() { const r = this._r; return r ? (this._back ? r._sc : r._ec) : null; }
    get focusOffset() { const r = this._r; return r ? (this._back ? r._so : r._eo) : 0; }
    get isCollapsed() { return !this._r || this._r.collapsed; }
    get type() { return !this._r ? 'None' : this._r.collapsed ? 'Caret' : 'Range'; }
    get direction() { return !this._r || this._r.collapsed ? 'none' : this._back ? 'backward' : 'forward'; }
    getRangeAt(i) { if (toU32(i) !== 0 || !this._r) throw rErr(`The index provided (${i}) is greater than or equal to the maximum bound (${this.rangeCount}).`, 'IndexSizeError'); return this._r; }
    addRange(r) { if (!(r instanceof Range)) throw new TypeError("Failed to execute 'addRange' on 'Selection': parameter 1 is not of type 'Range'."); if (this._r || rootOf(r._sc) !== document) return; this._r = r; this._back = false; }
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
        if (!this._r) throw rErr('This Selection object doesn\'t have any Ranges.', 'InvalidStateError');
        o = this._point(n, o, 'extend');
        if (rootOf(n) !== document) return;
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
    selectAllChildren(n) { needNode(n, 'selectAllChildren', 'Selection'); noDoctype(n); if (rootOf(n) !== document) return; const r = new Range(); r.selectNodeContents(n); this._r = r; this._back = false; }
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
    toString() { return this._r ? this._r.toString() : ''; }
}
const selection = new Selection();
class Animation extends EventTarget {
    constructor(el, kf, o) {
        super(); this.effect = { target: el }; this.playState = 'finished'; this.onfinish = null; this.finished = Promise.resolve(this); this.ready = Promise.resolve(this);
        const last = Array.isArray(kf) ? kf[kf.length - 1] : null;
        if (last && o && (o.fill === 'forwards' || o.fill === 'both') && el.style) for (const k in last) if (k !== 'offset' && k !== 'easing') el.style[k] = last[k];
        setTimeout(() => { const ev = new Event('finish'); if (this.onfinish) this.onfinish(ev); dispatch(this, ev); });
    }
    play() {} pause() {} finish() {} cancel() { this.playState = 'idle'; } reverse() {} commitStyles() {} persist() {}
}
class DOMMatrixReadOnly { constructor() { Object.assign(this, { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0, m41: 0, m42: 0, is2D: true, isIdentity: true }); } translate() { return new DOMMatrix(); } scale() { return new DOMMatrix(); } multiply() { return new DOMMatrix(); } inverse() { return new DOMMatrix(); } transformPoint(p) { return p; } toString() { return 'matrix(1, 0, 0, 1, 0, 0)'; } }
class DOMMatrix extends DOMMatrixReadOnly {}
class DOMPoint { constructor(x = 0, y = 0, z = 0, w = 1) { Object.assign(this, { x, y, z, w }); } }
const XML_TYPES = ['text/xml', 'application/xml', 'application/xhtml+xml', 'image/svg+xml'];
function parseXML(src, type) {
    const doc = N.newXmlDoc(); Object.setPrototypeOf(doc, Document.prototype); def(doc, '__ct', type);
    const ents = { lt: '<', gt: '>', amp: '&', quot: '"', apos: "'" };
    const decode = (s) => s.indexOf('&') < 0 ? s : s.replace(/&([^;\s&<]*);?/g, (m, e) => {
        if (!m.endsWith(';')) throw 'unterminated entity';
        if (e[0] === '#') { const v = e[1] === 'x' ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10); if (!(v > 0 && v <= 0x10FFFF)) throw 'bad char ref'; return String.fromCodePoint(v); }
        if (!(e in ents)) throw `undefined entity &${e};`; return ents[e];
    });
    const stack = [doc], scopes = [{ xml: XML_NS, xmlns: XMLNS_NS }];
    const tagRe = /<([^\s\/>]+)/y, attrRe = /\s+([^\s=\/>]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/y, endRe = /\s*(\/?)>/y, closeRe = /<\/([^\s>]+)\s*>/y;
    let i = 0, line = 1;
    try {
        if (src.charCodeAt(0) === 0xFEFF) i = 1;
        while (i < src.length) {
            const cur = stack[stack.length - 1];
            if (src[i] !== '<') {
                let j = src.indexOf('<', i); if (j < 0) j = src.length;
                const t = src.slice(i, j);
                if (cur !== doc) insertNode(cur, N.textNode(decode(t), doc), null);
                else if (/\S/.test(t)) throw 'content outside the root element';
                i = j; continue;
            }
            if (src.startsWith('<!--', i)) { const j = src.indexOf('-->', i + 4); if (j < 0) throw 'unterminated comment'; insertNode(cur, N.comment(src.slice(i + 4, j), doc), null); i = j + 3; continue; }
            if (src.startsWith('<![CDATA[', i)) { const j = src.indexOf(']]>', i); if (j < 0 || cur === doc) throw 'bad CDATA section'; insertNode(cur, N.cdata(src.slice(i + 9, j), doc), null); i = j + 3; continue; }
            if (src.startsWith('<?', i)) {
                const j = src.indexOf('?>', i); if (j < 0) throw 'unterminated processing instruction';
                const m = /^([^\s?]+)\s*([\s\S]*)$/.exec(src.slice(i + 2, j)); if (!m) throw 'bad processing instruction';
                if (m[1].toLowerCase() !== 'xml') insertNode(cur, N.pi(m[1], m[2], doc), null);
                else if (i !== 0 && !(i === 1 && src.charCodeAt(0) === 0xFEFF)) throw 'XML declaration not at start';
                i = j + 2; continue;
            }
            if (src.startsWith('<!DOCTYPE', i)) {
                let j = i + 9, depth = 0;
                for (; j < src.length; j++) { const ch = src[j]; if (ch === '[') depth++; else if (ch === ']') depth--; else if (ch === '>' && depth <= 0) break; }
                const m = /^<!DOCTYPE\s+([^\s>\[]+)(?:\s+(?:PUBLIC\s+(?:"([^"]*)"|'([^']*)')\s*(?:"([^"]*)"|'([^']*)')?|SYSTEM\s+(?:"([^"]*)"|'([^']*)')))?/.exec(src.slice(i, j + 1));
                if (!m || cur !== doc) throw 'bad doctype';
                insertNode(doc, N.doctype(m[1], m[2] ?? m[3] ?? '', m[4] ?? m[5] ?? m[6] ?? m[7] ?? '', doc), null);
                i = j + 1; continue;
            }
            if (src[i + 1] === '/') {
                closeRe.lastIndex = i; const m = closeRe.exec(src);
                if (!m || stack.length < 2 || stack[stack.length - 1].tagName !== m[1]) throw `mismatched end tag ${m ? m[1] : ''}`;
                stack.pop(); scopes.pop(); i = closeRe.lastIndex; continue;
            }
            tagRe.lastIndex = i; const tm = tagRe.exec(src); if (!tm) throw 'bad start tag';
            i = tagRe.lastIndex;
            const attrs = []; const scope = Object.create(scopes[scopes.length - 1]);
            for (;;) {
                attrRe.lastIndex = i; const am = attrRe.exec(src); if (!am) break;
                const v = decode(am[2] ?? am[3]).replace(/[\t\n\r]/g, ' ');
                if (attrs.some(a => a[0] === am[1])) throw `duplicate attribute ${am[1]}`;
                attrs.push([am[1], v]); i = attrRe.lastIndex;
                if (am[1] === 'xmlns') scope[''] = v || null; else if (am[1].startsWith('xmlns:')) scope[am[1].slice(6)] = v;
            }
            endRe.lastIndex = i; const em = endRe.exec(src); if (!em) throw `bad start tag ${tm[1]}`;
            i = endRe.lastIndex;
            const q = tm[1], c = q.indexOf(':'), pfx = c > 0 ? q.slice(0, c) : '';
            const uri = scope[pfx]; if (pfx && uri == null) throw `unbound prefix ${pfx}`;
            if (cur === doc && doc.documentElement) throw 'extra content at the end of the document';
            const el = Document.prototype.createElementNS.call(doc, uri ?? null, q);
            for (const [k, v] of attrs) N.setAttr(el, k, v);
            insertNode(cur, el, null);
            if (!em[1]) { stack.push(el); scopes.push(scope); }
        }
        if (stack.length > 1) throw `unclosed element ${stack[stack.length - 1].tagName}`;
        if (!doc.documentElement) throw 'no root element';
    } catch (e) {
        if (typeof e !== 'string') throw e;
        while (N.first(doc)) removeNode(N.first(doc));
        const pe = Document.prototype.createElementNS.call(doc, 'http://www.mozilla.org/newlayout/xml/parsererror.xml', 'parsererror');
        insertNode(pe, N.textNode('XML Parsing Error: ' + e, doc), null);
        insertNode(doc, pe, null);
    }
    return doc;
}
class DOMParser {
    parseFromString(s, type) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'parseFromString' on 'DOMParser': 2 arguments required, but only ${arguments.length} present.`);
        s = String(s); type = String(type);
        if (type === 'text/html') return N.parseDoc(s);
        if (!XML_TYPES.includes(type)) throw new TypeError(`Failed to execute 'parseFromString' on 'DOMParser': The provided value '${type}' is not a valid enum value of type DOMParserSupportedType.`);
        return parseXML(s, type);
    }
}
const xmlEscT = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const xmlEscA = (s) => s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/\t/g, '&#9;').replace(/\n/g, '&#10;').replace(/\r/g, '&#13;');
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
const html_void = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'source', 'track', 'wbr']);
class XMLSerializer { serializeToString(n) { if (!N.isNode(n)) throw new TypeError("Failed to execute 'serializeToString' on 'XMLSerializer': parameter 1 is not of type 'Node'."); return xmlSer(n, null, Object.assign(Object.create(null), { xml: XML_NS })); } }
