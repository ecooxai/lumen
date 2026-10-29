const allColl = new WeakMap(), allMeta = new WeakMap();
const documentContentTypes = privateDOMState(3), documentInterfaces = privateDOMState(4);
const allNamed = (l, k) => { for (const e of l) if (e.id === k || (e.getAttribute('name') === k)) return e; return null; };
class HTMLAllCollection {
    constructor() { illegal(); }
    get length() { return allMeta.get(this)().length; }
    item(k) { if (k === undefined) return null; const l = allMeta.get(this)(); return /^\d+$/.test(String(k)) ? l[+k] ?? null : allNamed(l, String(k)); }
    namedItem(k) { return allNamed(allMeta.get(this)(), String(k)); }
    [Symbol.iterator]() { return allMeta.get(this)()[Symbol.iterator](); }
}
class Document extends Node { constructor() { if (new.target === HTMLDocument) illegal(); const d = N.newXmlDoc(); Object.setPrototypeOf(d, new.target.prototype); documentInterfaces.set(d, new.target === XMLDocument ? XMLDocument.prototype : Document.prototype); return d; } }
class HTMLDocument extends Document {}
class XMLDocument extends Document {}
methods(Document.prototype, ParentNode);
installHandlers(Document.prototype, false);
const findChild = (p, tag) => { if (!p) return null; for (let c = N.first(p); c; c = N.next(c)) if (N.type(c) === 1 && N.name(c) === tag) return c; return null; };
methods(Document.prototype, {
    get all() { let o = allColl.get(this); if (!o) { const d = this, list = () => d.getElementsByTagName('*'); o = N.makeAll(function (k) { if (typeof k === 'number' || /^\d+$/.test(k)) return list()[+k] ?? undefined; return k === undefined ? undefined : allNamed(list(), String(k)) ?? undefined; }); Object.setPrototypeOf(o, HTMLAllCollection.prototype); allMeta.set(o, list); allColl.set(this, o); } return o; },
    get documentElement() { for (let c = N.first(this); c; c = N.next(c)) if (N.type(c) === 1) return c; return null; },
    get head() { return findChild(this.documentElement, 'head'); },
    get body() { return findChild(this.documentElement, 'body'); },
    get doctype() { for (let c = N.first(this); c; c = N.next(c)) if (N.type(c) === 10) return c; return null; },
    get title() { const t = N.query(this, 'title', false); return t ? N.text(t).replace(/\s+/g, ' ').trim() : ''; },
    set title(v) { let t = N.query(this, 'title', false); if (!t) { const h = this.head; if (!h) return; t = N.create('title', 0); insertNode(h, t, null); } t.textContent = v; },
    get URL() { return N.documentURL(this); }, get documentURI() { return N.documentURL(this); },
    get baseURI() { return N.documentBaseUrl(this); },
    get location() { return this === document ? location : null; }, set location(v) { if (this === document) location.href = v; },
    get domain() { const r = N.urlParse(N.url(), null); return r ? r[5] : ''; }, set domain(v) {},
    get referrer() { return N.referrer(); },
    get cookie() { return N.cookie(); }, set cookie(v) { N.setCookie(String(v)); },
    get readyState() { return ['loading', 'interactive', 'complete'][N.readyState()]; },
    get characterSet() { return N.documentEncoding(this); }, get charset() { return N.documentEncoding(this); }, get inputEncoding() { return N.documentEncoding(this); }, get contentType() { return documentContentTypes.get(this) ?? (N.ns(this) === 3 ? 'application/xml' : 'text/html'); },
    get compatMode() { return N.documentQuirks(this) ? 'BackCompat' : 'CSS1Compat'; },
    get visibilityState() { return 'visible'; }, get hidden() { return false; }, get webkitHidden() { return false; }, get prerendering() { return false; },
    get defaultView() { return this === document ? G : null; }, get activeElement() { return N.active() || this.body; },
    get currentScript() { return N.currentScript(); }, get scrollingElement() { return this.documentElement; },
    get forms() { return htmlColl(N.query(this, 'form', true)); }, get images() { return htmlColl(N.query(this, 'img', true)); },
    get links() { return htmlColl(N.query(this, 'a[href],area[href]', true)); }, get scripts() { return htmlColl(N.query(this, 'script', true)); },
    get styleSheets() { return scopeSheets(this); },
    get fonts() { return fontSet; }, get timeline() { if (!this.__tl) def(this, '__tl', new DocumentTimeline()); return this.__tl; }, get fullscreenElement() { return null; }, get fullscreenEnabled() { return false; },
    exitFullscreen() { return Promise.resolve(); }, get pictureInPictureEnabled() { return false; },
    get implementation() { if (this === document) return implementation; if (!this.__impl) def(this, '__impl', Object.create(implementation, { _doc: { value: this } })); return this.__impl; },
    createElement(tag, opts) {
        tag = String(tag); if (!validLocalName(tag)) throw new DOMException(`The tag name provided ('${tag}') is not a valid name.`, 'InvalidCharacterError');
        const xml = N.ns(this) === 3, name = xml ? tag : asciiLower(tag), ns = !xml || documentContentTypes.get(this) === 'application/xhtml+xml' ? 0 : 3, el = N.create(name, ns, this);
        if (N.name(el) !== name) elementNames.set(el, { localName: name, namespace: ns === 3 ? null : NSURI[ns], prefix: null });
        if (opts && typeof opts === 'object' && opts.is) N.setAttr(el, 'is', String(opts.is));
        if (!xml && registry.byName.has(name)) ceUpgrade(el);
        return el;
    },
    createElementNS(ns, q) {
        if (arguments.length < 2) throw new TypeError('createElementNS requires a namespace and qualified name.');
        const name = qualifiedName(ns, q), pfx = name.prefix, l = name.localName; ns = name.namespace;
        const n = NSURI.indexOf(ns), el = N.create(n === 1 || n === 2 ? asciiLower(l) : l, n < 0 ? 3 : n, this);
        elementNames.set(el, { localName: l, namespace: ns, prefix: pfx });
        if (n === 0 && pfx === null && registry.byName.has(l)) ceUpgrade(el);
        return el;
    },
    createTextNode(s) { return N.textNode(String(s), this); }, createComment(s) { return N.comment(String(s), this); },
    createDocumentFragment() { return N.frag(this); },
    createCDATASection(d) { if (N.ns(this) !== 3) throw new DOMException('This operation is not supported for HTML documents.', 'NotSupportedError'); d = String(d); if (d.includes(']]>')) throw new DOMException("String cannot contain ']]>' since that is the end delimiter of a CData section.", 'InvalidCharacterError'); return N.cdata(d, this); },
    createProcessingInstruction(t, d) { t = String(t); d = String(d); if (!/^[A-Za-z_:\u00C0-\uFFFF][\w.:\-\u00B7\u00C0-\uFFFF]*$/.test(t)) throw new DOMException(`The target provided ('${t}') is not a valid name.`, 'InvalidCharacterError'); if (d.includes('?>')) throw new DOMException("The data provided ('" + d + "') contains '?>'.", 'InvalidCharacterError'); return N.pi(t, d, this); },
    createAttribute(name) {
        if (!arguments.length) throw new TypeError('createAttribute requires a name.');
        name = attributeString(name);
        if (!validAttributeLocalName(name)) throw new DOMException('The attribute name is invalid.', 'InvalidCharacterError');
        if (N.ns(this) !== 3) name = asciiLower(name);
        return makeAttribute(null, name, this);
    },
    createAttributeNS(namespace, name) {
        if (arguments.length < 2) throw new TypeError('createAttributeNS requires a namespace and qualified name.');
        N.ns(this);
        const value = qualifiedName(namespace, name, true);
        return makeAttribute(null, value.name, this, value.namespace, value.prefix, value.localName);
    },
    createEvent(t) { if (!arguments.length) throw new TypeError('createEvent requires an interface name'); return createLegacyEvent(t); },
    createRange() { return new Range(); },
    createTreeWalker(root, what = 0xFFFFFFFF, filter = null) { return mkTraversal(TreeWalker, root, what, filter); },
    createNodeIterator(root, what = 0xFFFFFFFF, filter = null) { return mkTraversal(NodeIterator, root, what, filter); },
    getElementById(id) { return N.byId(String(id)); },
    getElementsByName(n) { return nodeList(N.query(this, `[name="${CSS.escape(String(n))}"]`, true)); },
    importNode(n, deep) {
        if (N.type(this) !== 9) throw new TypeError('Illegal invocation');
        if (!attributeState.has(n) && !N.isNode(n)) throw new TypeError("The imported value must be a Node.");
        if (!attributeState.has(n) && (N.type(n) === 9 || (N.type(n) === 11 && N.host(n)))) throw new DOMException('Documents and shadow roots cannot be imported.', 'NotSupportedError');
        const r = cloneDOMNode(n, !!deep, this);
        if (!attributeState.has(r)) ceUpgradeTree(r);
        return r;
    }, adoptNode(n) {
        if (N.type(this) !== 9) throw new TypeError('Illegal invocation');
        if (attributeState.has(n)) {
            const state = requireAttribute(n);
            if (state.element) Element.prototype.removeAttributeNode.call(state.element, n);
            state.document = this;
            return n;
        }
        if (!N.isNode(n)) throw new TypeError("Failed to execute 'adoptNode' on 'Document': parameter 1 is not of type 'Node'.");
        if (N.type(n) === 9) throw new DOMException('The node provided is a document, which may not be adopted.', 'NotSupportedError');
        if (N.type(n) === 11 && N.host(n)) throw new DOMException('A shadow root may not be adopted.', 'HierarchyRequestError');
        if (N.parent(n)) removeNode(n);
        N.adopt(n, this);
        return n;
    },
    hasFocus() { return true; },
    elementFromPoint(x, y) { return N.hit(+x, +y); },
    elementsFromPoint(x, y) { const r = []; for (let e = N.hit(+x, +y); e && N.type(e) === 1; e = N.parent(e)) r.push(e); return r; },
    getSelection() { return selection; },
    execCommand(cmd, ui, val) {
        cmd = String(cmd).toLowerCase();
        const ed = n => { for (; n; n = n.parentNode) if (n.nodeType === 1) { const v = n.getAttribute('contenteditable'); if (v === '' || v === 'true' || v === 'plaintext-only') return n; if (v === 'false') return null; } return null; };
        const ae = this.activeElement, sel = this.getSelection();
        let t = sel && sel.anchorNode; if (t && t.nodeType !== 1) t = t.parentNode;
        if (cmd === 'copy' || cmd === 'cut') {
            const dt = new DataTransfer(), ev = new ClipboardEvent(cmd, { clipboardData: dt, bubbles: true, cancelable: true, composed: true });
            (t || ae || this.body || this.documentElement).dispatchEvent(ev);
            const fld = ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA') ? ae : null;
            N.clipSet(ev.defaultPrevented ? dt.getData('text/plain') : fld ? String(fld.value).slice(fld.selectionStart, fld.selectionEnd) : String(sel || ''));
            return true;
        }
        if (!ed(t) && ae && ed(ae)) t = ae;
        if (!ed(t)) {
            if (cmd !== 'inserttext' || !ae || (ae.tagName !== 'INPUT' && ae.tagName !== 'TEXTAREA')) return false;
            const str = String(val ?? '');
            if (ae.setRangeText) ae.setRangeText(str, ae.selectionStart, ae.selectionEnd, 'end'); else ae.value += str;
            ae.dispatchEvent(new InputEvent('input', { inputType: 'insertText', data: str, bubbles: true }));
            return true;
        }
        if (cmd === 'inserttext') { { const ev = new KeyboardEvent('lumenedit', { key: String(val ?? ''), bubbles: true }); ev.lumenExec = true; t.dispatchEvent(ev); } return true; }
        if (cmd === 'delete') { { const ev = new KeyboardEvent('lumeneditdel', { key: 'Backspace', bubbles: true }); ev.lumenExec = true; t.dispatchEvent(ev); } return true; }
        return false;
    },
    queryCommandSupported(c) { return /^(inserttext|delete)$/i.test(c); },
    open() { if (this !== document) N.documentOpen(this); return this; },
    close() { if (this !== document) N.documentClose(this); },
    write(...s) { const text = s.join(''); if (this === document) docWrite(text); else N.documentWrite(this, text); },
    writeln(...s) { const text = s.join('') + '\n'; if (this === document) docWrite(text); else N.documentWrite(this, text); },
});
function docWrite(html) {
    const s = N.currentScript(), parent = s ? N.parent(s) : document.body;
    if (parent) insertNode(parent, N.parseFrag(parent, html), s ? N.next(s) : null);
}
const implementation = { hasFeature() { return true; }, createHTMLDocument(t) { const d = N.newDoc(t === undefined ? null : String(t)); Object.setPrototypeOf(d, HTMLDocument.prototype); return d; }, createDocumentType(n, p, s) { n = String(n); if (/[\t\n\f\r >\x00]/.test(n)) throw new DOMException(`The qualified name provided ('${n}') contains the invalid name-start character.`, 'InvalidCharacterError'); return N.doctype(n, String(p), String(s), this._doc || document); },
    createDocument(ns, q, dt = null) { const d = N.newXmlDoc(); if (ns === NSURI[0]) documentContentTypes.set(d, 'application/xhtml+xml'); else if (ns === NSURI[1]) documentContentTypes.set(d, 'image/svg+xml'); q = q == null ? '' : String(q); const el = q ? Document.prototype.createElementNS.call(d, ns, q) : null; if (dt) insertNode(d, dt, null); if (el) insertNode(d, el, null); return d; } };
methods(Document.prototype, { get adoptedStyleSheets() { return adoptedGet(this); }, set adoptedStyleSheets(v) { adoptSheets(this, v); } });
