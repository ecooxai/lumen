class Document extends Node { constructor() { super(); illegal(); } }
class HTMLDocument extends Document {}
methods(Document.prototype, ParentNode);
installHandlers(Document.prototype, false);
const findChild = (p, tag) => { if (!p) return null; for (let c = N.first(p); c; c = N.next(c)) if (N.type(c) === 1 && N.name(c) === tag) return c; return null; };
methods(Document.prototype, {
    get documentElement() { for (let c = N.first(this); c; c = N.next(c)) if (N.type(c) === 1) return c; return null; },
    get head() { return findChild(this.documentElement, 'head'); },
    get body() { return findChild(this.documentElement, 'body'); },
    get doctype() { for (let c = N.first(this); c; c = N.next(c)) if (N.type(c) === 10) return c; return null; },
    get title() { const t = N.query(this, 'title', false); return t ? N.text(t).replace(/\s+/g, ' ').trim() : ''; },
    set title(v) { let t = N.query(this, 'title', false); if (!t) { const h = this.head; if (!h) return; t = N.create('title', 0); insertNode(h, t, null); } t.textContent = v; },
    get URL() { return N.url(); }, get documentURI() { return N.url(); },
    get baseURI() { const b = N.query(this, 'base[href]', false); const u = N.url(); if (b) { const r = N.urlParse(N.attr(b, 'href'), u); if (r) return r[0]; } return u; },
    get location() { return location; }, set location(v) { location.href = v; },
    get domain() { const r = N.urlParse(N.url(), null); return r ? r[5] : ''; }, set domain(v) {},
    get referrer() { return ''; },
    get cookie() { return N.cookie(); }, set cookie(v) { N.setCookie(String(v)); },
    get readyState() { return ['loading', 'interactive', 'complete'][N.readyState()]; },
    get characterSet() { return 'UTF-8'; }, get charset() { return 'UTF-8'; }, get contentType() { return 'text/html'; },
    get compatMode() { return N.quirks() ? 'BackCompat' : 'CSS1Compat'; },
    get visibilityState() { return 'visible'; }, get hidden() { return false; }, get webkitHidden() { return false; }, get prerendering() { return false; },
    get defaultView() { return G; }, get activeElement() { return N.active() || this.body; },
    get currentScript() { return N.currentScript(); }, get scrollingElement() { return this.documentElement; },
    get forms() { return nodeList(N.query(this, 'form', true)); }, get images() { return nodeList(N.query(this, 'img', true)); },
    get links() { return nodeList(N.query(this, 'a[href],area[href]', true)); }, get scripts() { return nodeList(N.query(this, 'script', true)); },
    get styleSheets() { return N.query(this, 'style,link[rel~=stylesheet]', true).map(sheetFor); },
    get fonts() { return fontSet; }, get fullscreenElement() { return null; }, get fullscreenEnabled() { return false; },
    exitFullscreen() { return Promise.resolve(); }, get pictureInPictureEnabled() { return false; },
    get implementation() { return implementation; },
    createElement(tag, opts) {
        tag = String(tag); if (!validLocalName(tag)) throw new DOMException(`The tag name provided ('${tag}') is not a valid name.`, 'InvalidCharacterError');
        const name = asciiLower(tag), el = N.create(name, 0); if (N.name(el) !== name) def(el, '__ln', name);
        if (opts && typeof opts === 'object' && opts.is) N.setAttr(el, 'is', String(opts.is));
        if (registry.byName.has(name)) ceUpgrade(el);
        return el;
    },
    createElementNS(ns, q) {
        ns = ns == null || ns === '' ? null : String(ns); q = String(q);
        const i = q.indexOf(':'), pfx = i >= 0 ? q.slice(0, i) : null, l = i >= 0 ? q.slice(i + 1) : q;
        if ((pfx !== null && !/^[^\t\n\f\r \/>\x00]+$/.test(pfx)) || !validLocalName(l)) throw new DOMException(`The qualified name provided ('${q}') contains the invalid name-start character.`, 'InvalidCharacterError');
        if ((pfx !== null && ns === null) || (pfx === 'xml' && ns !== XML_NS) || ((q === 'xmlns' || pfx === 'xmlns') !== (ns === XMLNS_NS)))
            throw new DOMException(`The namespace configuration for '${q}' is invalid.`, 'NamespaceError');
        const n = NSURI.indexOf(ns), el = N.create(n === 0 ? l : asciiLower(l), n < 0 ? 0 : n);
        if (N.name(el) !== l) def(el, '__ln', l);
        if (n < 0) def(el, '__nsu', ns);
        if (pfx !== null) def(el, '__pfx', pfx);
        if (n === 0 && pfx === null && registry.byName.has(l)) ceUpgrade(el);
        return el;
    },
    createTextNode(s) { return N.textNode(String(s)); }, createComment(s) { return N.comment(String(s)); },
    createDocumentFragment() { return N.frag(); }, createAttribute(n) { return new Attr(null, String(n).toLowerCase()); },
    createEvent(t) { const m = { event: Event, events: Event, htmlevents: Event, customevent: CustomEvent, uievent: UIEvent, uievents: UIEvent, mouseevent: MouseEvent, mouseevents: MouseEvent, keyboardevent: KeyboardEvent, focusevent: FocusEvent, messageevent: MessageEvent }; const C = m[String(t).toLowerCase()]; if (!C) throw new DOMException(`The provided event type ('${t}') is invalid.`, 'NotSupportedError'); return new C(''); },
    createRange() { return new Range(); },
    createTreeWalker(root, what = 0xFFFFFFFF, filter = null) { return new TreeWalker(root, what, filter); },
    createNodeIterator(root, what = 0xFFFFFFFF, filter = null) { return new NodeIterator(root, what, filter); },
    getElementById(id) { return N.byId(String(id)); },
    getElementsByName(n) { return nodeList(N.query(this, `[name="${CSS.escape(String(n))}"]`, true)); },
    importNode(n, deep) { const r = N.clone(n, !!deep); ceUpgradeTree(r); return r; }, adoptNode(n) { removeNode(n); return n; },
    hasFocus() { return true; },
    elementFromPoint(x, y) { return N.hit(+x, +y); },
    elementsFromPoint(x, y) { const r = []; for (let e = N.hit(+x, +y); e && N.type(e) === 1; e = N.parent(e)) r.push(e); return r; },
    getSelection() { return selection; },
    execCommand() { return false; }, queryCommandSupported() { return false; },
    open() { return this; }, close() {},
    write(...s) { docWrite(s.join('')); }, writeln(...s) { docWrite(s.join('') + '\n'); },
});
function docWrite(html) {
    const s = N.currentScript(), parent = s ? N.parent(s) : document.body;
    if (parent) insertNode(parent, N.parseFrag(parent, html), s ? N.next(s) : null);
}
const implementation = { hasFeature() { return true; }, createHTMLDocument(t) { const d = N.newDoc(t === undefined ? null : String(t)); Object.setPrototypeOf(d, HTMLDocument.prototype); return d; }, createDocumentType(n) { return { name: n, nodeType: 10 }; } };
