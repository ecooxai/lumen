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
class Range {
    constructor() { this.startContainer = this.endContainer = document; this.startOffset = this.endOffset = 0; }
    get collapsed() { return this.startContainer === this.endContainer && this.startOffset === this.endOffset; }
    setStart(n, o) { this.startContainer = n; this.startOffset = o; } setEnd(n, o) { this.endContainer = n; this.endOffset = o; }
    selectNodeContents(n) { this.setStart(n, 0); this.setEnd(n, kids(n).length); } selectNode(n) { this.selectNodeContents(n); } collapse() {}
    createContextualFragment(html) { return N.parseFrag(N.type(this.startContainer) === 1 ? this.startContainer : document.body, String(html)); }
    getBoundingClientRect() { return new DOMRect(); } getClientRects() { return []; } cloneRange() { return new Range(); } detach() {} toString() { return ''; }
}
class Selection { constructor() { this.rangeCount = 0; this.anchorNode = this.focusNode = null; this.isCollapsed = true; this.type = 'None'; } getRangeAt() { throw new DOMException('Index out of range', 'IndexSizeError'); } addRange() {} removeAllRanges() {} empty() {} collapse() {} toString() { return ''; } }
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
class DOMParser { parseFromString() { throw new DOMException('DOMParser is not supported yet', 'NotSupportedError'); } }
class XMLSerializer { serializeToString(n) { return N.html(n, N.type(n) === 1); } }
