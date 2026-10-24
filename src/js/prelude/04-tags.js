const H = {}, TAGS = Object.create(null);
const mk = (name, base, tags, body) => { const C = { [name]: class extends base {} }[name]; if (body) body(C.prototype); H[name] = C; for (const t of tags) TAGS[t] = C; return C; };
const formControl = (p) => methods(p, {
    get form() { return this.closest('form'); },
    get labels() { return nodeList(this.id ? N.query(document, `label[for="${CSS.escape(this.id)}"]`, true) : []); },
    get validity() { return { valid: true, valueMissing: false, typeMismatch: false, patternMismatch: false, tooLong: false, tooShort: false, rangeUnderflow: false, rangeOverflow: false, stepMismatch: false, badInput: false, customError: false }; },
    get validationMessage() { return ''; }, get willValidate() { return true; },
    checkValidity() { return true; }, reportValidity() { return true; }, setCustomValidity() {},
});
const textProp = (p) => methods(p, { get text() { return N.text(this); }, set text(v) { this.textContent = v; } });
mk('HTMLHtmlElement', HTMLElement, ['html']); mk('HTMLHeadElement', HTMLElement, ['head']);
/* <body> and <frameset> event handlers for the window's events are the window's own (on* IDL attributes and content
   attributes both) */
const WINDOW_FWD = ['blur', 'error', 'focus', 'load', 'resize', 'scroll', 'afterprint', 'beforeprint', 'beforeunload', 'hashchange', 'languagechange', 'message', 'messageerror',
    'offline', 'online', 'pagehide', 'pagereveal', 'pageshow', 'pageswap', 'popstate', 'rejectionhandled', 'storage', 'unhandledrejection', 'unload'];
const fwdHandlers = p => { for (const t of WINDOW_FWD) acc(p, 'on' + t, function () { return G['on' + t] ?? null; }, function (v) { G['on' + t] = v; }); };
mk('HTMLBodyElement', HTMLElement, ['body'], p => { fwdHandlers(p); reflectStr(p, 'text', 'link', 'vLink', 'aLink', 'bgColor', 'background'); });
mk('HTMLFrameSetElement', HTMLElement, ['frameset'], p => { fwdHandlers(p); reflectStr(p, 'cols', 'rows'); });
mk('HTMLDivElement', HTMLElement, ['div']); mk('HTMLSpanElement', HTMLElement, ['span']); mk('HTMLParagraphElement', HTMLElement, ['p']);
mk('HTMLHeadingElement', HTMLElement, ['h1', 'h2', 'h3', 'h4', 'h5', 'h6']); mk('HTMLBRElement', HTMLElement, ['br']); mk('HTMLHRElement', HTMLElement, ['hr']);
mk('HTMLPreElement', HTMLElement, ['pre']); mk('HTMLQuoteElement', HTMLElement, ['blockquote', 'q']); mk('HTMLUListElement', HTMLElement, ['ul']);
mk('HTMLOListElement', HTMLElement, ['ol'], p => reflectInt(p, 1, 'start')); mk('HTMLLIElement', HTMLElement, ['li']); mk('HTMLDListElement', HTMLElement, ['dl']);
mk('HTMLPictureElement', HTMLElement, ['picture']); mk('HTMLTimeElement', HTMLElement, ['time'], p => reflectStr(p, 'dateTime'));
mk('HTMLTrackElement', HTMLElement, ['track'], p => { reflectUrl(p, 'src'); reflectStr(p, 'kind', 'srclang', 'label'); });
mk('HTMLObjectElement', HTMLElement, ['object'], p => { reflectUrl(p, 'data'); reflectStr(p, 'type', 'name', 'width', 'height'); });
mk('HTMLEmbedElement', HTMLElement, ['embed'], p => { reflectUrl(p, 'src'); reflectStr(p, 'type', 'width', 'height'); });
mk('HTMLSourceElement', HTMLElement, ['source'], p => { reflectUrl(p, 'src'); reflectStr(p, 'type', 'srcset', 'sizes', 'media'); });
/* table DOM: rows in thead, body and tfoot order; insertRow/insertCell/deleteRow/deleteCell and the section helpers */
const tableKid = (t, name) => elKids(t).find(e => N.name(e) === name) || null;
const rowsOf = p => elKids(p).filter(e => N.name(e) === 'tr');
const tableRows = t => {
    const kids = elKids(t), out = [];
    for (const k of kids) if (N.name(k) === 'thead') out.push(...rowsOf(k));
    for (const k of kids) { const n = N.name(k); if (n === 'tr') out.push(k); else if (n === 'tbody') out.push(...rowsOf(k)); }
    for (const k of kids) if (N.name(k) === 'tfoot') out.push(...rowsOf(k));
    return out;
};
const indexError = (what, i, n) => new DOMException(`Failed to execute '${what}': The index provided (${i}) is outside the range [-1, ${n}].`, 'IndexSizeError');
const sectionInsertRow = (sec, i, what) => {
    i = i === undefined ? -1 : Math.trunc(+i) || 0;
    const rows = rowsOf(sec);
    if (i < -1 || i > rows.length) throw indexError(what, i, rows.length);
    const tr = N.create('tr', 0);
    insertNode(sec, tr, i === -1 || i === rows.length ? null : rows[i]);
    return tr;
};
const deleteFrom = (rows, i, what) => {
    i = Math.trunc(+i) || 0;
    if (i === -1) { if (rows.length) rows[rows.length - 1].remove(); return; }
    if (i < 0 || i >= rows.length) throw indexError(what, i, rows.length);
    rows[i].remove();
};
mk('HTMLTableElement', HTMLElement, ['table'], p => methods(p, {
    get rows() { return nodeList(tableRows(this)); },
    get tBodies() { return nodeList(elKids(this).filter(e => N.name(e) === 'tbody')); },
    get caption() { return tableKid(this, 'caption'); }, get tHead() { return tableKid(this, 'thead'); }, get tFoot() { return tableKid(this, 'tfoot'); },
    createCaption() { let c = this.caption; if (!c) { c = N.create('caption', 0); insertNode(this, c, N.first(this)); } return c; },
    deleteCaption() { const c = this.caption; if (c) c.remove(); },
    createTHead() {
        let h = this.tHead;
        if (!h) { h = N.create('thead', 0); insertNode(this, h, elKids(this).find(e => N.name(e) !== 'caption' && N.name(e) !== 'colgroup') || null); }
        return h;
    },
    deleteTHead() { const h = this.tHead; if (h) h.remove(); },
    createTFoot() { let f = this.tFoot; if (!f) { f = N.create('tfoot', 0); insertNode(this, f, null); } return f; },
    deleteTFoot() { const f = this.tFoot; if (f) f.remove(); },
    createTBody() {
        const b = N.create('tbody', 0), bodies = elKids(this).filter(e => N.name(e) === 'tbody'), last = bodies[bodies.length - 1];
        insertNode(this, b, last ? N.next(last) : null);
        return b;
    },
    insertRow(i) {
        i = i === undefined ? -1 : Math.trunc(+i) || 0;
        const rows = tableRows(this);
        if (i < -1 || i > rows.length) throw indexError("insertRow' on 'HTMLTableElement", i, rows.length);
        const tr = N.create('tr', 0);
        if (!rows.length) {
            const bodies = elKids(this).filter(e => N.name(e) === 'tbody');
            if (bodies.length) insertNode(bodies[bodies.length - 1], tr, null);
            else { const b = N.create('tbody', 0); insertNode(b, tr, null); insertNode(this, b, null); }
        } else if (i === -1 || i === rows.length) { const last = rows[rows.length - 1]; insertNode(N.parent(last), tr, null); }
        else insertNode(N.parent(rows[i]), tr, rows[i]);
        return tr;
    },
    deleteRow(i) { deleteFrom(tableRows(this), i, "deleteRow' on 'HTMLTableElement"); },
}));
mk('HTMLTableSectionElement', HTMLElement, ['tbody', 'thead', 'tfoot'], p => methods(p, {
    get rows() { return nodeList(rowsOf(this)); },
    insertRow(i) { return sectionInsertRow(this, i, "insertRow' on 'HTMLTableSectionElement"); },
    deleteRow(i) { deleteFrom(rowsOf(this), i, "deleteRow' on 'HTMLTableSectionElement"); },
}));
const cellsOf = r => elKids(r).filter(e => /^t[dh]$/.test(N.name(e)));
mk('HTMLTableRowElement', HTMLElement, ['tr'], p => methods(p, {
    get cells() { return nodeList(cellsOf(this)); },
    get rowIndex() {
        let t = N.parent(this); if (t && /^t(head|body|foot)$/.test(N.name(t))) t = N.parent(t);
        return t && N.name(t) === 'table' ? tableRows(t).indexOf(this) : -1;
    },
    get sectionRowIndex() { const s = N.parent(this); return s ? rowsOf(s).indexOf(this) : -1; },
    insertCell(i) {
        i = i === undefined ? -1 : Math.trunc(+i) || 0;
        const c = cellsOf(this);
        if (i < -1 || i > c.length) throw indexError("insertCell' on 'HTMLTableRowElement", i, c.length);
        const td = N.create('td', 0); insertNode(this, td, i === -1 || i === c.length ? null : c[i]); return td;
    },
    deleteCell(i) { deleteFrom(cellsOf(this), i, "deleteCell' on 'HTMLTableRowElement"); },
}));
mk('HTMLTableCellElement', HTMLElement, ['td', 'th'], p => reflectInt(p, 1, 'colSpan', 'rowSpan'));
mk('HTMLAnchorElement', HTMLElement, ['a'], p => { reflectStr(p, 'target', 'download', 'rel', 'hreflang', 'type', 'referrerPolicy'); urlParts(p, 'href'); textProp(p); methods(p, { get relList() { return new DOMTokenList(TOK_KEY, this, 'rel'); } }); });
mk('HTMLAreaElement', HTMLElement, ['area'], p => { urlParts(p, 'href'); reflectStr(p, 'alt', 'target'); });
mk('HTMLImageElement', HTMLElement, ['img'], p => {
    reflectUrl(p, 'src'); reflectStr(p, 'alt', 'srcset', 'sizes', 'crossOrigin', 'useMap', 'referrerPolicy', 'decoding', 'loading', 'fetchPriority');
    methods(p, {
        get width() { const v = parseInt(N.attr(this, 'width'), 10); if (!isNaN(v)) return v; const r = N.rect(this); return r ? Math.round(r[2]) : 0; }, set width(v) { this.setAttribute('width', String(v | 0)); },
        get height() { const v = parseInt(N.attr(this, 'height'), 10); if (!isNaN(v)) return v; const r = N.rect(this); return r ? Math.round(r[3]) : 0; }, set height(v) { this.setAttribute('height', String(v | 0)); },
        get naturalWidth() { const s = N.imgSize(this); return s ? s[0] : 0; }, get naturalHeight() { const s = N.imgSize(this); return s ? s[1] : 0; },
        get complete() { const s = this.getAttribute('src'); return !s || !s.trim() || !!N.imgSize(this) || N.imgDone(this) || imgFailed.get(this) === s || /^\s*(about|javascript|mailto|tel):/i.test(s); }, get currentSrc() { return this.src; }, decode() { return Promise.resolve(); },
    });
});
mk('HTMLScriptElement', HTMLElement, ['script'], p => {
    reflectUrl(p, 'src'); reflectStr(p, 'type', 'charset', 'crossOrigin', 'integrity', 'referrerPolicy', 'nonce'); reflectBool(p, 'defer', 'noModule'); textProp(p);
    methods(p, { get async() { return this.__async !== undefined ? this.__async : N.attr(this, 'async') != null; }, set async(v) { def(this, '__async', !!v); } });
});
H.HTMLScriptElement.supports = (t) => t === 'classic';
mk('HTMLStyleElement', HTMLElement, ['style'], p => { reflectStr(p, 'media', 'type'); reflectBool(p, 'disabled'); methods(p, { get sheet() { return sheetFor(this); } }); });
mk('HTMLLinkElement', HTMLElement, ['link'], p => { reflectUrl(p, 'href'); reflectStr(p, 'rel', 'media', 'hreflang', 'type', 'as', 'crossOrigin', 'integrity', 'referrerPolicy', 'sizes', 'fetchPriority'); reflectBool(p, 'disabled'); methods(p, { get relList() { return new DOMTokenList(TOK_KEY, this, 'rel'); }, get sheet() { return /stylesheet/i.test(N.attr(this, 'rel') || '') ? sheetFor(this) : null; } }); });
mk('HTMLMetaElement', HTMLElement, ['meta'], p => reflectStr(p, 'name', 'content', 'httpEquiv', 'charset', 'media'));
mk('HTMLTitleElement', HTMLElement, ['title'], textProp);
mk('HTMLBaseElement', HTMLElement, ['base'], p => { reflectUrl(p, 'href'); reflectStr(p, 'target'); });
mk('HTMLTemplateElement', HTMLElement, ['template'], p => methods(p, { get content() { return N.templateContent(this); } }));
mk('HTMLSlotElement', HTMLElement, ['slot'], p => { reflectStr(p, 'name'); methods(p, { assignedNodes() { return []; }, assignedElements() { return []; }, assign() {} }); });
mk('HTMLIFrameElement', HTMLElement, ['iframe'], p => { reflectUrl(p, 'src'); reflectStr(p, 'srcdoc', 'name', 'allow', 'width', 'height', 'referrerPolicy', 'loading', 'frameBorder', 'scrolling', 'marginWidth', 'marginHeight', 'align'); reflectBool(p, 'allowFullscreen'); methods(p, { get sandbox() { if (!this.__sb) def(this, '__sb', new DOMTokenList(TOK_KEY, this, 'sandbox')); return this.__sb; }, set sandbox(v) { this.setAttribute('sandbox', v); }, get contentWindow() { return asWin(N.frameWin(this)); }, get contentDocument() { return N.frameDoc(this); } }); });
mk('HTMLCanvasElement', HTMLElement, ['canvas'], p => {
    /* resizing resizes the WebGL drawing buffer; WebGPU reads the size each frame */
    for (const [n, d] of [['width', 300], ['height', 150]])
        acc(p, n, function () { const v = parseInt(N.attr(this, n), 10); return isNaN(v) || v < 0 ? d : v; }, function (v) { this.setAttribute(n, String(v >>> 0)); canvasResized(this); });
    methods(p, {
        /* one context per canvas: asking for another kind gives null */
        getContext(t, o) {
            t = String(t);
            const kind = t === 'experimental-webgl' ? 'webgl' : t;
            if (this.__offscreen) throw new DOMException("Failed to execute 'getContext' on 'HTMLCanvasElement': Cannot get context from a canvas that has transferred its control to offscreen.", 'InvalidStateError');
            if (this.__ck) return this.__ck === kind ? this.__cx : null;
            const x = kind === '2d' ? c2dCreate(this, o) : kind === 'webgl' ? glCreate(this, 1, o) : kind === 'webgl2' ? glCreate(this, 2, o)
                : kind === 'webgpu' && gpuNavigator ? gpuCanvasContext(this) : null;
            if (x) { def(this, '__ck', kind); def(this, '__cx', x); }
            return x;
        },
        captureStream(fps) { return canvasStream(this, fps); },
        transferControlToOffscreen() {
            if (this.__ck || this.__offscreen) throw new DOMException("Failed to execute 'transferControlToOffscreen' on 'HTMLCanvasElement': Cannot transfer control from a canvas that has a rendering context.", 'InvalidStateError');
            const o = new OffscreenCanvas(this.width, this.height); o._.ph = this; def(this, '__offscreen', o); return o;
        },
        toDataURL() { return canvasDataURL(this); },
        toBlob(cb) { if (typeof cb !== 'function') throw new TypeError("Failed to execute 'toBlob' on 'HTMLCanvasElement': parameter 1 is not of type 'BlobCallback'."); const b = canvasBlob(this); setTimeout(() => cb(b)); },
    });
});
/* a control's autocomplete IDL value: its autofill tokens, normalised, or "" when they are not valid */
const AF_NORMAL = new Set('name honorific-prefix given-name additional-name family-name honorific-suffix nickname username new-password current-password one-time-code organization-title organization street-address address-line1 address-line2 address-line3 address-level4 address-level3 address-level2 address-level1 country country-name postal-code cc-name cc-given-name cc-additional-name cc-family-name cc-number cc-exp cc-exp-month cc-exp-year cc-csc cc-type transaction-currency transaction-amount language bday bday-day bday-month bday-year sex url photo'.split(' '));
const AF_CONTACT = new Set('tel tel-country-code tel-national tel-area-code tel-local tel-local-prefix tel-local-suffix tel-extension email impp'.split(' '));
const autofill = el => {
    const t = (N.attr(el, 'autocomplete') || '').toLowerCase().split(/[\t\n\f\r ]+/).filter(Boolean);
    if (!t.length) return '';
    if (t.length === 1 && t[0] === 'webauthn') return 'webauthn';
    const webauthn = t[t.length - 1] === 'webauthn' && t.length > 1;
    if (webauthn) t.pop();
    const f = t.pop();
    if ((f === 'on' || f === 'off') && !webauthn && !t.length) return el.localName === 'input' && el.type === 'hidden' ? '' : f;
    const contact = AF_CONTACT.has(f);
    if (!contact && !AF_NORMAL.has(f)) return '';
    const out = [f];
    if (contact && t.length && /^(home|work|mobile|fax|pager)$/.test(t[t.length - 1])) out.unshift(t.pop());
    if (t.length && /^(shipping|billing)$/.test(t[t.length - 1])) out.unshift(t.pop());
    if (t.length && t[t.length - 1].startsWith('section-')) out.unshift(t.pop());
    if (t.length) return '';
    if (webauthn) out.push('webauthn');
    return out.join(' ');
};
const autofillAcc = p => acc(p, 'autocomplete', function () { return autofill(this); }, function (v) { this.setAttribute('autocomplete', v); });
mk('HTMLFormElement', HTMLElement, ['form'], p => {
    reflectUrl(p, 'action'); reflectStr(p, 'name', 'target');
    acc(p, 'autocomplete', function () { return (N.attr(this, 'autocomplete') || '').toLowerCase() === 'off' ? 'off' : 'on'; }, function (v) { this.setAttribute('autocomplete', v); }); acc(p, 'acceptCharset', function () { return N.attr(this, 'accept-charset') ?? ''; }, function (v) { this.setAttribute('accept-charset', v); }); reflectBool(p, 'noValidate');
    methods(p, {
        get method() { const v = (N.attr(this, 'method') || '').toLowerCase(); return v === 'post' || v === 'dialog' ? v : 'get'; }, set method(v) { this.setAttribute('method', v); },
        get enctype() { const v = (N.attr(this, 'enctype') || '').toLowerCase(); return /^(multipart\/form-data|text\/plain)$/.test(v) ? v : 'application/x-www-form-urlencoded'; },
        set enctype(v) { this.setAttribute('enctype', v); },
        get encoding() { return this.enctype; }, set encoding(v) { this.setAttribute('enctype', v); },
        get elements() { return nodeList(N.query(this, 'input,select,textarea,button,fieldset,output', true)); }, get length() { return this.elements.length; },
        /* a form that is not connected cannot navigate: it neither fires submit nor submits */
        submit() { if (this.isConnected) submitForm(this, null); },
        requestSubmit(s = null) {
            if (s !== null) {
                const sub = N.isNode(s) && N.type(s) === 1 && N.ns(s) === 0 && ((N.name(s) === 'button' && s.type === 'submit') || (N.name(s) === 'input' && (s.type === 'submit' || s.type === 'image')));
                if (!sub) throw new TypeError("Failed to execute 'requestSubmit' on 'HTMLFormElement': The specified element is not a submit button.");
                if (s.form !== this) throw new DOMException("Failed to execute 'requestSubmit' on 'HTMLFormElement': The specified element is not owned by this form element.", 'NotFoundError');
            }
            if (this.isConnected && dispatch(this, new SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: s }), true)) submitForm(this, s);
        },
        reset() { if (dispatch(this, new Event('reset', { bubbles: true, cancelable: true }))) for (const e of this.elements) N.setValue(e, null); },
        checkValidity() { return true; }, reportValidity() { return true; },
    });
});
/* text selection of <input>/<textarea>: native keeps [anchor, focus] in codepoints, the DOM API speaks UTF-16 */
const cpU16 = (v, cp) => { let i = 0; for (; cp > 0 && i < v.length; cp--) i += v.codePointAt(i) > 0xffff ? 2 : 1; return i; };
const u16Cp = (v, o) => { let n = 0; for (let i = 0; i < o && i < v.length; n++) i += v.codePointAt(i) > 0xffff ? 2 : 1; return n; };
const SEL_TYPES = /^(text|search|url|tel|password)$/;
const selOk = el => el.localName === 'textarea' || SEL_TYPES.test(el.type);
/* each control keeps its own selection; the one holding the native caret reports that (a user's change) */
const selStore = new WeakMap(), selPending = new WeakSet();
const selFire = el => {
    if (selPending.has(el)) return;
    selPending.add(el);
    setTimeout(() => { selPending.delete(el); dispatch(el, new Event('select', { bubbles: true }), true); });
};
const selGet = el => {
    const v = el.value, n = v.length, r = N.ctlSel(el), st = selStore.get(el);
    if (r) {
        const a = Math.min(cpU16(v, r[0]), n), f = Math.min(cpU16(v, r[1]), n), s = Math.min(a, f), e = Math.max(a, f);
        if (st && st[0] === s && st[1] === e) return st.slice();
        return [s, e, f < a ? 'backward' : 'forward'];
    }
    if (st) return [Math.min(st[0], n), Math.min(st[1], n), st[2]];
    return [n, n, 'none'];
};
const selSet = (el, s, e, dir, quiet) => {
    const v = el.value, n = v.length;
    s = Math.min(Math.max(0, toU32(s)), n); e = Math.min(Math.max(0, toU32(e)), n); if (e < s) s = e;
    dir = dir === 'backward' || dir === 'forward' ? dir : 'none';
    const old = selGet(el);
    selStore.set(el, [s, e, dir]);
    if (N.ctlSel(el) || (el.isConnected && document.activeElement === el)) { const back = dir === 'backward'; N.setCtlSel(el, u16Cp(v, back ? e : s), u16Cp(v, back ? s : e)); }
    if (!quiet && (old[0] !== s || old[1] !== e || old[2] !== dir)) selFire(el);
};
let oprSplicing = false;
/* a script's value change: a full replacement moves the control's OpaqueRanges to 0 unless nothing changed */
const valueSet = (el, v) => {
    const old = oprOf.has(el) && !oprSplicing ? el.value : null;
    N.setValue(el, v);
    if (old !== null && el.value !== old) oprReplace(el, 0, old.length, el.value.length);
};
const selNo = () => { throw new DOMException("The input element's type does not support selection.", 'InvalidStateError'); };
const textSelection = {
    get selectionStart() { return selOk(this) ? selGet(this)[0] : null; },
    set selectionStart(v) { if (!selOk(this)) selNo(); const r = selGet(this); v = toU32(v); selSet(this, v, Math.max(v, r[1]), r[2]); },
    get selectionEnd() { return selOk(this) ? selGet(this)[1] : null; },
    set selectionEnd(v) { if (!selOk(this)) selNo(); const r = selGet(this); v = toU32(v); selSet(this, Math.min(r[0], v), v, r[2]); },
    get selectionDirection() { return selOk(this) ? selGet(this)[2] : null; },
    set selectionDirection(d) { if (!selOk(this)) selNo(); const r = selGet(this); selSet(this, r[0], r[1], d); },
    setSelectionRange(s, e, dir) { if (!selOk(this)) selNo(); selSet(this, s, e, dir); },
    createValueRange(start, end) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'createValueRange' on '${this.localName === 'input' ? 'HTMLInputElement' : 'HTMLTextAreaElement'}': 2 arguments required, but only ${arguments.length} present.`);
        if (!selOk(this)) throw new DOMException("The element's type does not support value ranges.", 'NotSupportedError');
        start = toU32(start); end = toU32(end);
        const n = this.value.length;
        if (start > n || end > n) throw new DOMException(`The offset ${Math.max(start, end)} is larger than the value's length (${n}).`, 'IndexSizeError');
        return oprNew(this, start, Math.max(start, end));
    },
    select() { if (!selOk(this)) return; selSet(this, 0, this.value.length, 'none'); },
    setRangeText(text, start, end, mode = 'preserve') {
        if (!selOk(this)) selNo();
        text = String(text);
        const before = selGet(this);
        let [ss, se] = before;
        if (start === undefined) { start = ss; end = se; }
        const v = this.value, n = v.length;
        start = Math.min(Math.max(0, Math.trunc(+start) || 0), n); end = Math.min(Math.max(0, Math.trunc(+end) || 0), n);
        if (start > end) throw new DOMException('The provided start value is larger than the end.', 'IndexSizeError');
        oprSplicing = true;
        try { this.value = v.slice(0, start) + text + v.slice(end); } finally { oprSplicing = false; }
        oprReplace(this, start, end, this.value.length - (n - (end - start)));
        const nl = start + text.length;
        if (mode === 'select') { ss = start; se = nl; }
        else if (mode === 'start') ss = se = start;
        else if (mode === 'end') ss = se = nl;
        else {   /* preserve */
            const d = text.length - (end - start);
            if (ss > end) ss += d; else if (ss > start) ss = start;
            if (se > end) se += d; else if (se > start) se = nl;
        }
        /* select fires only when the selection differs from the one before the call */
        selSet(this, ss, se, undefined, true);
        const now = selGet(this);
        if (now[0] !== before[0] || now[1] !== before[1] || now[2] !== before[2]) selFire(this);
    },
};
const INPUT_TYPES = /^(hidden|text|search|tel|url|email|password|date|month|week|time|datetime-local|number|range|color|checkbox|radio|file|submit|image|reset|button)$/;
mk('HTMLInputElement', HTMLElement, ['input'], p => {
    autofillAcc(p);
    reflectStr(p, 'name', 'placeholder', 'accept', 'alt', 'inputMode', 'max', 'min', 'pattern', 'step', 'enterKeyHint');
    reflectBool(p, 'disabled', 'readOnly', 'required', 'multiple'); reflectUrl(p, 'src'); reflectInt(p, 20, 'size'); reflectInt(p, -1, 'maxLength', 'minLength'); formControl(p);
    methods(p, {
        get type() { const t = (N.attr(this, 'type') || 'text').toLowerCase(); return INPUT_TYPES.test(t) ? t : 'text'; }, set type(v) { this.setAttribute('type', v); },
        get value() { if (this.type === 'file') { const f = this.files; return f.length ? 'C:\\fakepath\\' + f[0].name : ''; } const v = N.value(this); return v != null ? v : (/^(checkbox|radio)$/.test(this.type) ? (N.attr(this, 'value') ?? 'on') : N.attr(this, 'value') ?? ''); },
        set value(v) { if (this.type === 'file') { if (v === '' || v == null) def(this, '_files', new FileList()); return; } valueSet(this, v == null ? '' : String(v)); },
        get defaultValue() { return N.attr(this, 'value') ?? ''; }, set defaultValue(v) { this.setAttribute('value', v); },
        get checked() { return N.checked(this); }, set checked(v) { N.setChecked(this, !!v); },
        get defaultChecked() { return this.hasAttribute('checked'); }, set defaultChecked(v) { this.toggleAttribute('checked', !!v); },
        get valueAsNumber() { return parseFloat(this.value); }, set valueAsNumber(v) { this.value = String(v); },
        get files() { if (this.type !== 'file') return null; if (!this._files) def(this, '_files', new FileList()); return this._files; },
        set files(v) { if (this.type === 'file' && v instanceof FileList) def(this, '_files', v); },
        get indeterminate() { return !!this.__ind; }, set indeterminate(v) { def(this, '__ind', !!v); },
        showPicker() { if (this.type === 'file') pickFiles(this); }, stepUp() {}, stepDown() {},
    });
    methods(p, textSelection);
});
mk('HTMLTextAreaElement', HTMLElement, ['textarea'], p => {
    autofillAcc(p);
    reflectStr(p, 'name', 'placeholder', 'wrap'); reflectBool(p, 'disabled', 'readOnly', 'required'); reflectInt(p, 20, 'cols'); reflectInt(p, 2, 'rows'); formControl(p);
    methods(p, {
        get type() { return 'textarea'; },
        get value() { const v = N.value(this); return v != null ? v : N.text(this); }, set value(v) { valueSet(this, v == null ? '' : String(v)); },
        get defaultValue() { return N.text(this); }, set defaultValue(v) { this.textContent = v; },
    });
    methods(p, textSelection);
});
mk('HTMLButtonElement', HTMLElement, ['button'], p => {
    reflectStr(p, 'name', 'value'); reflectBool(p, 'disabled'); formControl(p);
    methods(p, {
        get type() {
            const t = (N.attr(this, 'type') || '').toLowerCase();
            if (['submit', 'reset', 'button'].includes(t)) return t;
            return this.hasAttribute('command') || this.hasAttribute('commandfor') || this.parentElement?.localName === 'select' ? 'button' : 'submit';
        },
        set type(v) { this.setAttribute('type', v); },
        get command() {
            const raw = N.attr(this, 'command') || '', lower = raw.toLowerCase();
            return raw.startsWith('--') ? raw : ['show-modal', 'close', 'request-close', 'toggle-popover', 'show-popover', 'hide-popover'].includes(lower) ? lower : '';
        },
        set command(v) { this.setAttribute('command', v); },
        get commandForElement() {
            const explicit = commandTargets.get(this), root = this.getRootNode();
            if (explicit) {
                const targetRoot = explicit.getRootNode();
                for (let scope = root; scope; scope = scope.host?.getRootNode()) if (scope === targetRoot) return explicit;
                return null;
            }
            const id = N.attr(this, 'commandfor');
            return id ? root.querySelector?.('#' + CSS.escape(id)) || null : null;
        },
        set commandForElement(v) {
            if (v == null) { this.removeAttribute('commandfor'); commandTargets.delete(this); return; }
            if (!(v instanceof Element)) throw new TypeError('commandForElement must be an Element');
            this.setAttribute('commandfor', ''); commandTargets.set(this, v);
        },
    });
});
reflectElement(HTMLElement.prototype, 'anchorElement', 'anchor');
for (const C of [H.HTMLButtonElement, H.HTMLAnchorElement, H.HTMLAreaElement]) reflectElement(C.prototype, 'interestForElement', 'interestfor');
for (const C of [H.HTMLButtonElement, H.HTMLInputElement]) reflectElement(C.prototype, 'popoverTargetElement', 'popovertarget');
mk('HTMLSelectElement', HTMLElement, ['select'], p => {
    autofillAcc(p);
    reflectStr(p, 'name'); reflectBool(p, 'disabled', 'multiple', 'required'); formControl(p);
    methods(p, {
        get type() { return this.multiple ? 'select-multiple' : 'select-one'; },
        get options() { return nodeList(N.query(this, 'option', true)); }, get length() { return this.options.length; },
        get selectedOptions() { return nodeList([...this.options].filter(o => o.selected)); },
        get selectedIndex() { const o = this.options; for (let i = 0; i < o.length; i++) if (N.checked(o[i])) return i; for (let i = 0; i < o.length; i++) if (o[i].hasAttribute('selected')) return i; return o.length && !this.multiple ? 0 : -1; },
        set selectedIndex(i) { this.options.forEach((o, k) => N.setChecked(o, k === i)); },
        get value() { const o = this.options[this.selectedIndex]; return o ? o.value : ''; },
        set value(v) { const i = [...this.options].findIndex(o => o.value === String(v)); this.selectedIndex = i; },
        item(i) { return this.options[i] || null; },
        add(o, before) { insertNode(this, o, typeof before === 'number' ? this.options[before] || null : before || null); },
    });
});
mk('HTMLOptionElement', HTMLElement, ['option'], p => {
    reflectBool(p, 'disabled'); reflectStr(p, 'label'); textProp(p);
    methods(p, {
        get value() { return N.attr(this, 'value') ?? N.text(this).trim(); }, set value(v) { this.setAttribute('value', v); },
        get selected() { const s = this.closest('select'); return s ? s.options[s.selectedIndex] === this : N.checked(this); },
        set selected(v) { const s = this.closest('select'); if (v && s && !s.multiple) for (const o of s.options) N.setChecked(o, false); N.setChecked(this, !!v); },
        get index() { const s = this.closest('select'); return s ? [...s.options].indexOf(this) : 0; },
        get defaultSelected() { return this.hasAttribute('selected'); }, set defaultSelected(v) { this.toggleAttribute('selected', !!v); },
    });
});
mk('HTMLLabelElement', HTMLElement, ['label'], p => { acc(p, 'htmlFor', function () { return N.attr(this, 'for') ?? ''; }, function (v) { this.setAttribute('for', v); }); methods(p, { get control() { const f = N.attr(this, 'for'); return f ? N.byId(f) : this.querySelector('input,select,textarea,button'); } }); });
mk('HTMLFieldSetElement', HTMLElement, ['fieldset'], p => reflectBool(p, 'disabled'));
mk('HTMLDetailsElement', HTMLElement, ['details'], p => acc(p, 'open', function () { return N.attr(this, 'open') != null; }, function (v) { const was = this.open; this.toggleAttribute('open', !!v); if (was !== !!v) setTimeout(() => dispatch(this, new Event('toggle'))); }));
const dialogStates = new WeakMap();
function dialogState(d) {
    let s = dialogStates.get(d);
    if (!s) { s = { returnValue: '', previousFocus: null, toggle: null }; dialogStates.set(d, s); }
    return s;
}
function dialogToggle(d, oldState, newState, source) {
    const s = dialogState(d);
    if (s.toggle) { s.toggle.newState = newState; s.toggle.source = source; return; }
    s.toggle = { oldState, newState, source };
    setTimeout(() => { const init = s.toggle; s.toggle = null; dispatch(d, new ToggleEvent('toggle', init), true); }, 0);
}
function dialogShow(d, modal, source = null) {
    if (d.open) {
        if (!!N.modal(d) !== modal) throw new DOMException('Dialog is already open in another mode', 'InvalidStateError');
        return;
    }
    if (modal && !d.isConnected) throw new DOMException('Dialog is not connected', 'InvalidStateError');
    if (!dispatch(d, new ToggleEvent('beforetoggle', { oldState: 'closed', newState: 'open', source, cancelable: true }), true)) return;
    if (d.open || (modal && !d.isConnected)) return;
    const s = dialogState(d); s.previousFocus = document.activeElement;
    dialogToggle(d, 'closed', 'open', source);
    d.open = true; N.modal(d, modal);
    const focus = d.hasAttribute('autofocus') ? d : d.querySelector('[autofocus]') || d.querySelector('button:not(:disabled),input:not(:disabled):not([type=hidden]),select:not(:disabled),textarea:not(:disabled),a[href],[tabindex]') || d;
    focus.focus();
}
function dialogClose(d, value, source = null) {
    if (!d.open) return;
    dispatch(d, new ToggleEvent('beforetoggle', { oldState: 'open', newState: 'closed', source }), true);
    if (!d.open) return;
    const modal = N.modal(d), s = dialogState(d);
    dialogToggle(d, 'open', 'closed', source);
    d.open = false;
    if (value !== undefined) s.returnValue = String(value);
    if (s.previousFocus?.isConnected && (modal || d.contains(document.activeElement))) s.previousFocus.focus();
    s.previousFocus = null;
    setTimeout(() => dispatch(d, new Event('close'), true), 0);
}
function dialogRequestClose(d, value, source = null) {
    if (d.open && dispatch(d, new Event('cancel', { cancelable: true }), true)) dialogClose(d, value, source);
}
mk('HTMLDialogElement', HTMLElement, ['dialog'], p => {
    reflectBool(p, 'open');
    methods(p, {
        get returnValue() { return dialogState(this).returnValue; }, set returnValue(v) { dialogState(this).returnValue = String(v); },
        show() { dialogShow(this, false); }, showModal() { dialogShow(this, true); },
        close(rv) { dialogClose(this, rv); }, requestClose(rv) { dialogRequestClose(this, rv); },
    });
});
mk('HTMLTableCaptionElement', HTMLElement, ['caption'], p => reflectStr(p, 'align'));
mk('HTMLTableColElement', HTMLElement, ['col', 'colgroup'], p => {
    reflectStr(p, 'align', 'ch', 'chOff', 'vAlign', 'width');
    acc(p, 'span', function () { const v = parseInt(N.attr(this, 'span'), 10); return v >= 1 ? Math.min(v, 1000) : 1; }, function (v) { this.setAttribute('span', String(v >>> 0)); });
});
mk('HTMLDataElement', HTMLElement, ['data'], p => reflectStr(p, 'value'));
mk('HTMLDataListElement', HTMLElement, ['datalist'], p => methods(p, { get options() { return htmlColl(N.query(this, 'option', true)); } }));
mk('HTMLModElement', HTMLElement, ['ins', 'del'], p => { reflectUrl(p, 'cite'); reflectStr(p, 'dateTime'); });
mk('HTMLDirectoryElement', HTMLElement, ['dir'], p => reflectBool(p, 'compact'));
mk('HTMLFontElement', HTMLElement, ['font'], p => reflectStr(p, 'color', 'face', 'size'));
mk('HTMLFrameElement', HTMLElement, ['frame'], p => { reflectStr(p, 'name', 'scrolling', 'frameBorder', 'marginHeight', 'marginWidth'); reflectUrl(p, 'src', 'longDesc'); reflectBool(p, 'noResize'); });
mk('HTMLOptGroupElement', HTMLElement, ['optgroup'], p => { reflectBool(p, 'disabled'); reflectStr(p, 'label'); });
mk('HTMLLegendElement', HTMLElement, ['legend'], p => {
    reflectStr(p, 'align');
    methods(p, { get form() { const f = N.parent(this); return f && N.name(f) === 'fieldset' ? f.form : null; } });
});
mk('HTMLOutputElement', HTMLElement, ['output'], p => {
    reflectStr(p, 'name');
    methods(p, {
        get type() { return 'output'; }, get form() { return this.closest('form'); },
        get value() { return this.textContent; }, set value(v) { this.textContent = v; },
        get defaultValue() { return this.__dv ?? this.textContent; }, set defaultValue(v) { def(this, '__dv', String(v)); },
        get willValidate() { return false; }, checkValidity() { return true; }, reportValidity() { return true; }, setCustomValidity() {},
    });
});
const numAttr = (el, a, d) => { const v = parseFloat(N.attr(el, a)); return Number.isFinite(v) ? v : d; };
const setNum = (el, a, v) => { v = +v; if (!Number.isFinite(v)) throw new TypeError(`The provided double value is non-finite.`); el.setAttribute(a, String(v)); };
mk('HTMLProgressElement', HTMLElement, ['progress'], p => methods(p, {
    get max() { const m = numAttr(this, 'max', 1); return m > 0 ? m : 1; }, set max(v) { if (+v > 0) setNum(this, 'max', v); },
    get value() { const v = numAttr(this, 'value', 0); return Math.min(Math.max(v, 0), this.max); }, set value(v) { setNum(this, 'value', v); },
    get position() { return N.attr(this, 'value') == null ? -1 : this.value / this.max; },
    get labels() { return nodeList([]); },
}));
mk('HTMLMeterElement', HTMLElement, ['meter'], p => methods(p, {
    get min() { return numAttr(this, 'min', 0); }, set min(v) { setNum(this, 'min', v); },
    get max() { return Math.max(numAttr(this, 'max', 1), this.min); }, set max(v) { setNum(this, 'max', v); },
    get value() { return Math.min(Math.max(numAttr(this, 'value', 0), this.min), this.max); }, set value(v) { setNum(this, 'value', v); },
    get low() { return Math.min(Math.max(numAttr(this, 'low', this.min), this.min), this.max); }, set low(v) { setNum(this, 'low', v); },
    get high() { return Math.max(Math.min(Math.max(numAttr(this, 'high', this.max), this.min), this.max), this.low); }, set high(v) { setNum(this, 'high', v); },
    get optimum() { return Math.min(Math.max(numAttr(this, 'optimum', (this.min + this.max) / 2), this.min), this.max); }, set optimum(v) { setNum(this, 'optimum', v); },
    get labels() { return nodeList([]); },
}));
mk('HTMLMapElement', HTMLElement, ['map'], p => { reflectStr(p, 'name'); methods(p, { get areas() { return htmlColl(N.query(this, 'area', true)); } }); });
mk('HTMLParamElement', HTMLElement, ['param'], p => reflectStr(p, 'name', 'value', 'type', 'valueType'));
mk('HTMLMenuElement', HTMLElement, ['menu'], p => reflectBool(p, 'compact'));
mk('HTMLMarqueeElement', HTMLElement, ['marquee'], p => { reflectStr(p, 'behavior', 'bgColor', 'direction', 'height', 'width'); reflectBool(p, 'trueSpeed'); methods(p, { start() {}, stop() {} }); });
mk('HTMLUnknownElement', HTMLElement, []);
