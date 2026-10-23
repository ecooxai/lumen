const allColl = new WeakMap(), allMeta = new WeakMap();
const baseURICache = new WeakMap();
function documentBaseURI(doc) {
    const version = N.version(doc), url = N.docBase(), cached = baseURICache.get(doc);
    if (cached && cached.version === version && cached.url === url) return cached.value;
    const base = N.query(doc, 'base[href]', false);
    const parsed = base && N.urlParse(N.attr(base, 'href'), url);
    const value = parsed ? parsed[0] : url;
    baseURICache.set(doc, { version, url, value });
    return value;
}
const allNamed = (l, k) => { for (const e of l) if (e.id === k || (e.getAttribute('name') === k)) return e; return null; };
class HTMLAllCollection {
    constructor() { illegal(); }
    get length() { return allMeta.get(this)().length; }
    item(k) { if (k === undefined) return null; const l = allMeta.get(this)(); return /^\d+$/.test(String(k)) ? l[+k] ?? null : allNamed(l, String(k)); }
    namedItem(k) { return allNamed(allMeta.get(this)(), String(k)); }
    [Symbol.iterator]() { return allMeta.get(this)()[Symbol.iterator](); }
}
class Document extends Node { constructor() { if (new.target === HTMLDocument) illegal(); const d = N.newXmlDoc(); Object.setPrototypeOf(d, new.target.prototype); def(d, '__url', 'about:blank'); return d; } }
unscopable(Document, ['append', 'fullscreen', 'prepend', 'replaceChildren']);
class HTMLDocument extends Document {}
class XMLDocument extends Document { constructor() { illegal(); } }
methods(Document.prototype, ParentNode);
installHandlers(Document.prototype, false, EVENT_HANDLERS.concat(FS_HANDLERS, ['readystatechange', 'visibilitychange', 'pointerlockchange', 'pointerlockerror', 'freeze', 'resume']));
const findChild = (p, tag) => { if (!p) return null; for (let c = N.first(p); c; c = N.next(c)) if (N.type(c) === 1 && N.name(c) === tag) return c; return null; };
/* the document's live HTMLCollections (forms, images, ...): the same object each time, with named properties */
const docColl = (doc, sel) => cachedLive(doc, 'coll:' + sel, () => N.query(doc, sel, true).filter(e => N.ns(e) === 0), HTMLCollection.prototype);
methods(Document.prototype, {
    get all() { let o = allColl.get(this); if (!o) { const d = this, list = () => d.getElementsByTagName('*'); o = N.makeAll(function (k) { if (typeof k === 'number' || /^\d+$/.test(k)) return list()[+k] ?? undefined; return k === undefined ? undefined : allNamed(list(), String(k)) ?? undefined; }); Object.setPrototypeOf(o, HTMLAllCollection.prototype); allMeta.set(o, list); allColl.set(this, o); } return o; },
    get documentElement() { for (let c = N.first(this); c; c = N.next(c)) if (N.type(c) === 1) return c; return null; },
    get head() { return findChild(this.documentElement, 'head'); },
    get body() { return findChild(this.documentElement, 'body'); },
    get doctype() { for (let c = N.first(this); c; c = N.next(c)) if (N.type(c) === 10) return c; return null; },
    get title() { const t = N.query(this, 'title', false); return t ? N.text(t).replace(/\s+/g, ' ').trim() : ''; },
    set title(v) { let t = N.query(this, 'title', false); if (!t) { const h = this.head; if (!h) return; t = N.create('title', 0); insertNode(h, t, null); } t.textContent = v; },
    get URL() { return this.__url ?? N.url(); }, get documentURI() { return this.__url ?? N.url(); },
    get baseURI() { return documentBaseURI(this); },
    get location() { return this === document ? location : null; }, set location(v) { if (this === document) location.href = v; },
    get domain() { const r = N.urlParse(N.url(), null); return r ? r[5] : ''; }, set domain(v) {},
    get referrer() { return ''; },
    get cookie() { return N.cookie(); }, set cookie(v) { N.setCookie(String(v)); },
    get readyState() { return ['loading', 'interactive', 'complete'][N.readyState()]; },
    get characterSet() { return N.charset(this); }, get charset() { return N.charset(this); }, get inputEncoding() { return N.charset(this); }, get contentType() { return this.__ct ?? N.ctype(this) ?? (N.ns(this) === 3 ? 'application/xml' : 'text/html'); },
    get compatMode() { return N.quirks() ? 'BackCompat' : 'CSS1Compat'; },
    get visibilityState() { return N.hidden() ? 'hidden' : 'visible'; }, get hidden() { return N.hidden(); }, get webkitHidden() { return N.hidden(); }, get prerendering() { return false; },
    get defaultView() { return this === document ? G : null; }, get activeElement() { return N.active() || this.body; },
    get currentScript() { return N.currentScript(); }, get scrollingElement() { return this.documentElement; },
    get forms() { return docColl(this, 'form'); }, get images() { return docColl(this, 'img'); },
    get links() { return docColl(this, 'a[href],area[href]'); }, get scripts() { return docColl(this, 'script'); },
    get embeds() { return docColl(this, 'embed'); }, get plugins() { return docColl(this, 'embed'); },
    get anchors() { return docColl(this, 'a[name]'); }, get applets() { return docColl(this, ':not(*)'); },
    get styleSheets() { return scopeSheets(this); },
    get adoptedStyleSheets() { return adoptedGet(this); }, set adoptedStyleSheets(v) { adoptSheets(this, v); },
    get fonts() { return fontSet; }, get timeline() { if (!this.__tl) def(this, '__tl', new DocumentTimeline()); return this.__tl; }, get fullscreenElement() { return fsCurrent(this); }, get fullscreenEnabled() { return true; },
    get webkitFullscreenElement() { return fsCurrent(this); }, get webkitCurrentFullScreenElement() { return fsCurrent(this); }, get webkitIsFullScreen() { return !!fsCurrent(this); }, get webkitFullscreenEnabled() { return true; },
    get fullscreen() { return !!fsCurrent(this); }, set fullscreen(v) {}, set fullscreenElement(v) {}, set fullscreenEnabled(v) {},
    exitFullscreen() { return fsExit(); }, webkitExitFullscreen() { fsExit().catch(() => {}); }, webkitCancelFullScreen() { fsExit().catch(() => {}); }, get pictureInPictureEnabled() { return false; },
    get implementation() { if (this === document) return implementation; if (!this.__impl) def(this, '__impl', Object.create(DOMImplementation.prototype, { _doc: { value: this } })); return this.__impl; },
    createElement(tag, opts) {
        tag = String(tag); if (!validLocalName(tag)) throw new DOMException(`The tag name provided ('${tag}') is not a valid name.`, 'InvalidCharacterError');
        /* XML documents keep the case; only XHTML ones make HTML elements */
        const xml = N.ns(this) === 3, html = !xml || this.contentType === 'application/xhtml+xml', name = xml ? tag : asciiLower(tag), el = N.create(name, html ? 0 : 3, this); if (N.name(el) !== name) def(el, '__ln', name);
        if (opts && typeof opts === 'object' && opts.is) N.setAttr(el, 'is', String(opts.is));
        if (html && registry.byName.has(name)) ceUpgrade(el);
        return el;
    },
    createElementNS(ns, q) {
        let pfx, l; [ns, pfx, l] = nsExtract(ns, q, false);
        const n = NSURI.indexOf(ns), el = N.create(n === 1 || n === 2 ? asciiLower(l) : l, n < 0 ? 3 : n, this);
        if (N.name(el) !== l) def(el, '__ln', l);
        if (n < 0) def(el, '__nsu', ns);
        if (pfx !== null) def(el, '__pfx', pfx);
        if (n < 0 || pfx !== null || N.name(el) !== l) N.xname(el, n < 0 ? ns : null, pfx, N.name(el) !== l ? l : null);   /* clones keep them */
        if (n === 0 && pfx === null && registry.byName.has(l)) ceUpgrade(el);
        return el;
    },
    createAttribute(n) {
        n = String(n); if (!validAttrName(n)) throw new DOMException(`Failed to execute 'createAttribute' on 'Document': The localName provided ('${n}') contains an invalid character.`, 'InvalidCharacterError');
        return mkAttr(null, null, null, N.ns(this) !== 3 ? asciiLower(n) : n, '', this);
    },
    createAttributeNS(ns, q) { const [u, p, l] = nsExtract(ns, q, true); return mkAttr(null, u, p, l, '', this); },
    createTextNode(s) { return N.textNode(String(s), this); }, createComment(s) { return N.comment(String(s), this); },
    createDocumentFragment() { return N.frag(this); },
    createCDATASection(d) { if (N.ns(this) !== 3) throw new DOMException('This operation is not supported for HTML documents.', 'NotSupportedError'); d = String(d); if (d.includes(']]>')) throw new DOMException("String cannot contain ']]>' since that is the end delimiter of a CData section.", 'InvalidCharacterError'); return N.cdata(d, this); },
    createProcessingInstruction(t, d) { t = String(t); d = String(d); if (!/^[A-Za-z_:\u00C0-\uFFFF][\w.:\-\u00B7\u00C0-\uFFFF]*$/.test(t)) throw new DOMException(`The target provided ('${t}') is not a valid name.`, 'InvalidCharacterError'); if (d.includes('?>')) throw new DOMException("The data provided ('" + d + "') contains '?>'.", 'InvalidCharacterError'); return N.pi(t, d, this); },
    createEvent(t) {
        const m = { event: Event, events: Event, htmlevents: Event, svgevents: Event, customevent: CustomEvent, uievent: UIEvent, uievents: UIEvent, mouseevent: MouseEvent, mouseevents: MouseEvent,
            keyboardevent: KeyboardEvent, focusevent: FocusEvent, messageevent: MessageEvent, beforeunloadevent: BeforeUnloadEvent, compositionevent: CompositionEvent,
            devicemotionevent: DeviceMotionEvent, deviceorientationevent: DeviceOrientationEvent, dragevent: DragEvent, hashchangeevent: HashChangeEvent, storageevent: StorageEvent,
            textevent: TextEvent, touchevent: TouchEvent };
        const C = m[asciiLower(String(t))]; if (!C) throw new DOMException(`The provided event type ('${t}') is invalid.`, 'NotSupportedError');
        if (C === BeforeUnloadEvent || C === TextEvent) { const e = Reflect.construct(Event, [''], C); markUninit(e); return e; }
        const e = new C(''); markUninit(e); return e;
    },
    createRange() { const r = new Range(); r._sc = r._ec = this; return r; },
    createTreeWalker(root, what = 0xFFFFFFFF, filter = null) { return mkTraversal(TreeWalker, root, what, filter); },
    createNodeIterator(root, what = 0xFFFFFFFF, filter = null) { return mkTraversal(NodeIterator, root, what, filter); },
    getElementById(id) { return N.byId(String(id)); },
    getElementsByName(n) { return nodeList(N.query(this, `[name="${CSS.escape(String(n))}"]`, true)); },
    importNode(n, deep = undefined) { const r = N.clone(n, !!deep); ceUpgradeTree(r); return r; }, adoptNode(n) { if (!N.isNode(n)) throw new TypeError("Failed to execute 'adoptNode' on 'Document': parameter 1 is not of type 'Node'."); if (N.type(n) === 9) throw new DOMException('The node provided is a document, which may not be adopted.', 'NotSupportedError'); if (N.type(n) === 11 && N.host(n)) throw new DOMException('The node provided is a shadow root, which may not be adopted.', 'HierarchyRequestError'); if (N.parent(n)) removeNode(n); N.adopt(n, this); return n; },
    hasFocus() { return true; }, getAnimations() { return []; },   /* Element.animate() animations finish at once */
    elementFromPoint(x, y) { return N.hit(+x, +y); },
    elementsFromPoint(x, y) { const r = []; for (let e = N.hit(+x, +y); e && N.type(e) === 1; e = N.parent(e)) r.push(e); return r; },
    getSelection() { return selection; },
    execCommand(cmd, ui, val) {
        if (!arguments.length) throw argErr('execCommand');
        cmd = String(cmd).toLowerCase();
        if (cmd === 'inserthtml') val = ttString(TrustedHTML, val === undefined ? '' : val, 'Document execCommand');
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
        if (!ed(t) && cmd === 'inserttext' && ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA')) {
            const str = String(val ?? '');
            if (ae.setRangeText) ae.setRangeText(str, ae.selectionStart, ae.selectionEnd, 'end'); else ae.value += str;
            ae.dispatchEvent(new InputEvent('input', { inputType: 'insertText', data: str, bubbles: true }));
            return true;
        }
        if (this !== document) return false;
        const E = editing();
        val = val === undefined ? '' : `${val}`;
        if (!E.supported(cmd) || !E.enabled(cmd)) return false;
        const host = editHostOf(sel), type = EXEC_INPUT[cmd];
        let ok = false;
        try { ok = E.exec(cmd, !!ui, val, undefined); } catch (e) { ok = false; }
        if ((ok || EXEC_COLOR.has(cmd)) && host && type !== undefined)
            dispatch(host, new InputEvent('input', { inputType: type, data: execData(cmd, val), bubbles: true, composed: true }), true);
        return ok;
    },
    queryCommandEnabled(c) { if (!arguments.length) throw argErr('queryCommandEnabled'); return this === document && editQuery(E => E.enabled(String(c)), false); },
    queryCommandIndeterm(c) { if (!arguments.length) throw argErr('queryCommandIndeterm'); return this === document && editQuery(E => E.indeterm(String(c)), false); },
    queryCommandState(c) { if (!arguments.length) throw argErr('queryCommandState'); return this === document && editQuery(E => E.state(String(c)), false); },
    queryCommandSupported(c) {
        if (!arguments.length) throw argErr('queryCommandSupported');
        c = String(c).toLowerCase();
        return /^(copy|cut|paste|undo|redo)$/.test(c) || editing().supported(c);
    },
    queryCommandValue(c) { if (!arguments.length) throw argErr('queryCommandValue'); return this === document ? String(editQuery(E => E.value(String(c)), '')) : ''; },
    get designMode() { return designModes.get(this) || 'off'; },
    set designMode(v) { const s = String(v).toLowerCase(); if (s === 'on' || s === 'off') designModes.set(this, s); },
    open(a, b, c) {
        if (arguments.length > 2) return G.open(a, b, c);
        if (this !== document || docOpened.has(this)) return this;
        if (this.contentType !== 'text/html') throw new DOMException("Failed to execute 'open' on 'Document': Only HTML documents support open().", 'InvalidStateError');
        if (N.currentScript() && N.readyState() === 0) return this;   /* the parser's own script */
        while (this.firstChild) this.removeChild(this.firstChild);
        docOpened.set(this, '');
        return this;
    },
    close() {
        if (this !== document || !docOpened.has(this)) return;
        const html = docOpened.get(this);
        docOpened.delete(this);
        N.docReparse(html);
    },
    write(...s) { docWriteTo(this, ttWriteText(s, false)); }, writeln(...s) { docWriteTo(this, ttWriteText(s, true)); },
});
/* a document script opened (document.open(), or a write once it has loaded): the markup written so far, parsed
   when it is closed. A write that opened it implicitly closes it in a later task if the script does not. */
const docOpened = new WeakMap(), designModes = new WeakMap();
/* the editing commands, compiled on the document's first editing call with the prelude's own globals */
let editEngine = null;
function editing() {
    if (editEngine) return editEngine;
    ttInternal++;
    try { editEngine = new Function('document', 'getSelection', 'getComputedStyle', 'Node', N.lazySrc('editing'))(document, () => selection, getComputedStyle, Node); }
    finally { ttInternal--; }
    return editEngine;
}
/* a queryCommand*() answer; the specification's implementation throws strings on states it does not handle */
function editQuery(f, none) { try { return f(editing()); } catch (e) { return none; } }
const argErr = m => new TypeError(`Failed to execute '${m}' on 'Document': 1 argument required, but only 0 present.`);
/* the editing host of the selection's start, before a command runs: where its input event goes */
function editHostOf(sel) {
    let n = sel && sel.rangeCount ? sel.getRangeAt(0).startContainer : null, host = null;
    for (; n && N.type(n) !== 9; n = N.parent(n)) if (N.type(n) === 1) {
        const s = ceState(n);
        if (s === 'false') break;
        if (s) host = n;
    }
    return host || (document.designMode === 'on' ? document.body : null);
}
/* execCommand()'s input event types (Input Events Level 2); commands missing here fire none */
const EXEC_INPUT = { inserttext: 'insertText', delete: 'deleteContentBackward', forwarddelete: 'deleteContentForward', insertparagraph: 'insertParagraph',
    insertlinebreak: 'insertLineBreak', insertorderedlist: 'insertOrderedList', insertunorderedlist: 'insertUnorderedList', inserthorizontalrule: 'insertHorizontalRule',
    inserthtml: 'insertHTML', insertimage: 'insertImage', createlink: 'insertLink', unlink: '', bold: 'formatBold', italic: 'formatItalic', underline: 'formatUnderline',
    strikethrough: 'formatStrikeThrough', superscript: 'formatSuperscript', subscript: 'formatSubscript', backcolor: 'formatBackColor', hilitecolor: 'formatBackColor',
    forecolor: 'formatFontColor', fontname: 'formatFontName', fontsize: 'formatFontSize', justifycenter: 'formatJustifyCenter', justifyfull: 'formatJustifyFull',
    justifyleft: 'formatJustifyLeft', justifyright: 'formatJustifyRight', removeformat: 'formatRemove', indent: 'formatIndent', outdent: 'formatOutdent', formatblock: 'formatBlock' };
const EXEC_COLOR = new Set(['backcolor', 'hilitecolor', 'forecolor']);
/* the input event's data: the text, URL or font name as given, a color as its computed value */
function execData(cmd, val) {
    if (cmd === 'inserttext' || cmd === 'createlink' || cmd === 'fontname') return val;
    if (!EXEC_COLOR.has(cmd)) return null;
    const k = val.trim().toLowerCase();
    if (k === 'inherit' || k === 'initial' || k === 'reset' || k === 'currentcolor') return k;
    if (!N.cssSupports(`(color: ${val})`)) return val;
    const sp = document.createElement('span');
    sp.style.color = val;
    (document.body || document.documentElement).appendChild(sp);
    const c = getComputedStyle(sp).color;
    sp.remove();
    return c;
}
function docWriteTo(doc, html) {
    if (doc !== document) return;
    if (doc.contentType !== 'text/html') throw new DOMException("Failed to execute 'write' on 'Document': Only HTML documents support write().", 'InvalidStateError');
    if (docOpened.has(doc)) { docOpened.set(doc, docOpened.get(doc) + html); return; }
    if (!N.currentScript() && N.readyState() === 2) {
        doc.open();
        docOpened.set(doc, html);
        setTimeout(() => doc.close());
        return;
    }
    docWrite(html);
}
/* Each script's writes follow one another: insert before what followed the script at its first write. */
const docWriteRef = new WeakMap();
function docWrite(html) {
    const s = N.currentScript(), parent = s ? N.parent(s) : document.body;
    if (!parent) return;
    let ref = s ? N.next(s) : null;
    if (s) {
        if (docWriteRef.has(s)) { const r = docWriteRef.get(s); ref = r && N.parent(r) === parent ? r : null; }
        else docWriteRef.set(s, ref);
    }
    const f = N.parseFrag(parent, html, !!s);
    if (ttReq) ttSlotsFrom(f);
    insertNode(parent, f, ref);
}
class DOMImplementation { constructor(key = undefined) { illegal(); } }
methods(DOMImplementation.prototype, { hasFeature() { return true; }, createHTMLDocument(t = undefined) { const d = N.newDoc(t === undefined ? null : String(t)); Object.setPrototypeOf(d, HTMLDocument.prototype); def(d, '__url', 'about:blank'); return d; }, createDocumentType(n, p, s) { n = String(n); if (/[\t\n\f\r >\x00]/.test(n)) throw new DOMException(`The qualified name provided ('${n}') contains the invalid name-start character.`, 'InvalidCharacterError'); return N.doctype(n, String(p), String(s), this._doc || document); },
    createDocument(ns, q, dt = null) { const d = N.newXmlDoc(); def(d, '__url', 'about:blank'); if (ns === NSURI[0]) def(d, '__ct', 'application/xhtml+xml'); else if (ns === NSURI[1]) def(d, '__ct', 'image/svg+xml'); q = q == null ? '' : String(q); const el = q ? Document.prototype.createElementNS.call(d, ns, q) : null; if (dt) insertNode(d, dt, null); if (el) insertNode(d, el, null); return d; } });
const implementation = Object.create(DOMImplementation.prototype, { _doc: { get() { return document; } } });
/* a DOMImplementation from another (same-origin) realm makes its nodes and documents in that realm */
for (const k of ['createHTMLDocument', 'createDocumentType', 'createDocument']) {
    const f = DOMImplementation.prototype[k];
    Object.defineProperty(DOMImplementation.prototype, k, { value: { [k](...args) {
        const P = this !== null && typeof this === 'object' ? Object.getPrototypeOf(this) : null;
        if (P && P !== DOMImplementation.prototype && !DOMImplementation.prototype.isPrototypeOf(this) && typeof P[k] === 'function' && P[k] !== DOMImplementation.prototype[k]) return P[k].apply(this, args);
        return f.apply(this, args);
    } }[k], writable: true, configurable: true });
    Object.defineProperty(DOMImplementation.prototype[k], 'length', { value: f.length });
}

/* The Fullscreen API. The browser hides its own bars and gives the page the whole window; the element
   is :fullscreen, which the UA sheet stretches over the viewport. One element at a time; Back (or the
   browser otherwise) leaves through __lumenFullscreenExit. */
let fsEl = null;
function fsFire(el, type) {
    const t = el && el.isConnected ? el : document;
    for (const n of [type, 'webkit' + type]) t.dispatchEvent(new Event(n, { bubbles: true, composed: true }));
}
function fsRequest(el) {
    /* a promise-returning operation on a receiver that is not an Element rejects, as WebIDL's do */
    if (!N.isNode(el) || el.nodeType !== 1) return Promise.reject(new TypeError("Failed to execute 'requestFullscreen' on 'Element': Illegal invocation"));
    return new Promise((resolve, reject) => setTimeout(() => {
        if (!el.isConnected) {
            fsFire(el, 'fullscreenerror');
            reject(new TypeError("Failed to execute 'requestFullscreen' on 'Element': Element not connected"));
            return;
        }
        if (fsEl === el) { resolve(); return; }
        if (fsEl) N.fullscreen(fsEl, false, true);
        fsEl = el; N.fullscreen(el, true, true);
        fsAfterResize(() => { fsFire(el, 'fullscreenchange'); resolve(); });
    }, 0));
}
function fsExit() {
    if (!fsEl) return Promise.reject(new TypeError("Failed to execute 'exitFullscreen' on 'Document': Document not active"));
    const el = fsEl; fsEl = null; N.fullscreen(el, false, false);
    return new Promise(resolve => fsAfterResize(() => { fsFire(el, 'fullscreenchange'); resolve(); }));
}
/* As in Chrome, fullscreenchange follows the window's resize: pages measure the new viewport in it.
   The browser's bars go away in steps, so it waits for the resizes to settle (or a moment without one). */
let fsQueue = [], fsTimer = 0, fsDeadline = 0;
function fsAfterResize(fn) {
    fsQueue.push(fn);
    if (!fsTimer) { fsDeadline = N.now() + 1000; fsTimer = setTimeout(fsFlush, 600); }
}
function fsResized() {
    if (!fsTimer) return;
    clearTimeout(fsTimer);
    fsTimer = setTimeout(fsFlush, Math.max(0, Math.min(150, fsDeadline - N.now())));
}
function fsFlush() {
    fsTimer = 0;
    const q = fsQueue; fsQueue = [];
    for (const fn of q) fn();
}
/* the fullscreen element as doc sees it: a shadow tree's element shows as its host */
function fsCurrent(doc) {
    if (fsEl && !fsEl.isConnected) { fsExit().catch(() => {}); return null; }
    let e = fsEl;
    while (e && e.getRootNode() !== doc) { const r = e.getRootNode(); e = r && r.host || null; }
    return e;
}
Object.defineProperty(globalThis, '__lumenFullscreenExit', { value: () => { if (fsEl) fsExit().catch(() => {}); } });
