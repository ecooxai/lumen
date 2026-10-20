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
mk('HTMLHtmlElement', HTMLElement, ['html']); mk('HTMLHeadElement', HTMLElement, ['head']); mk('HTMLBodyElement', HTMLElement, ['body']);
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
mk('HTMLAnchorElement', HTMLElement, ['a'], p => { reflectStr(p, 'target', 'download', 'rel', 'hreflang', 'type', 'referrerPolicy'); urlParts(p, 'href'); textProp(p); methods(p, { get relList() { return new DOMTokenList(this, 'rel'); } }); });
mk('HTMLAreaElement', HTMLElement, ['area'], p => { urlParts(p, 'href'); reflectStr(p, 'alt', 'target'); });
mk('HTMLImageElement', HTMLElement, ['img'], p => {
    reflectUrl(p, 'src'); reflectStr(p, 'alt', 'srcset', 'sizes', 'crossOrigin', 'useMap', 'referrerPolicy', 'decoding', 'loading', 'fetchPriority');
    methods(p, {
        get width() { const v = parseInt(N.attr(this, 'width'), 10); if (!isNaN(v)) return v; const r = N.rect(this); return r ? Math.round(r[2]) : 0; }, set width(v) { this.setAttribute('width', String(v | 0)); },
        get height() { const v = parseInt(N.attr(this, 'height'), 10); if (!isNaN(v)) return v; const r = N.rect(this); return r ? Math.round(r[3]) : 0; }, set height(v) { this.setAttribute('height', String(v | 0)); },
        get naturalWidth() { const s = N.imgSize(this); return s ? s[0] : 0; }, get naturalHeight() { const s = N.imgSize(this); return s ? s[1] : 0; },
        get complete() { return true; }, get currentSrc() { return this.src; }, decode() { return Promise.resolve(); },
    });
});
mk('HTMLScriptElement', HTMLElement, ['script'], p => {
    reflectUrl(p, 'src'); reflectStr(p, 'type', 'charset', 'crossOrigin', 'integrity', 'referrerPolicy', 'nonce'); reflectBool(p, 'defer', 'noModule'); textProp(p);
    methods(p, { get async() { return this.__async !== undefined ? this.__async : N.attr(this, 'async') != null; }, set async(v) { def(this, '__async', !!v); } });
});
H.HTMLScriptElement.supports = (t) => t === 'classic';
mk('HTMLStyleElement', HTMLElement, ['style'], p => { reflectStr(p, 'media', 'type'); reflectBool(p, 'disabled'); methods(p, { get sheet() { return sheetFor(this); } }); });
mk('HTMLLinkElement', HTMLElement, ['link'], p => { reflectUrl(p, 'href'); reflectStr(p, 'rel', 'media', 'hreflang', 'type', 'as', 'crossOrigin', 'integrity', 'referrerPolicy', 'sizes', 'fetchPriority'); reflectBool(p, 'disabled'); methods(p, { get relList() { return new DOMTokenList(this, 'rel'); }, get sheet() { return /stylesheet/i.test(N.attr(this, 'rel') || '') ? sheetFor(this) : null; } }); });
mk('HTMLMetaElement', HTMLElement, ['meta'], p => reflectStr(p, 'name', 'content', 'httpEquiv', 'charset', 'media'));
mk('HTMLTitleElement', HTMLElement, ['title'], textProp);
mk('HTMLBaseElement', HTMLElement, ['base'], p => { reflectUrl(p, 'href'); reflectStr(p, 'target'); });
mk('HTMLTemplateElement', HTMLElement, ['template'], p => methods(p, { get content() { return N.templateContent(this); } }));
mk('HTMLSlotElement', HTMLElement, ['slot'], p => { reflectStr(p, 'name'); methods(p, { assignedNodes() { return []; }, assignedElements() { return []; }, assign() {} }); });
mk('HTMLIFrameElement', HTMLElement, ['iframe'], p => { reflectUrl(p, 'src'); reflectStr(p, 'srcdoc', 'name', 'allow', 'width', 'height', 'referrerPolicy', 'loading'); methods(p, { get sandbox() { if (!this.__sb) def(this, '__sb', new DOMTokenList(this, 'sandbox')); return this.__sb; }, set sandbox(v) { this.setAttribute('sandbox', v); }, get contentWindow() { return asWin(N.frameWin(this)); }, get contentDocument() { return N.frameDoc(this); } }); });
mk('HTMLCanvasElement', HTMLElement, ['canvas'], p => {
    /* resizing resizes the WebGL drawing buffer; WebGPU reads the size each frame */
    for (const [n, d] of [['width', 300], ['height', 150]])
        acc(p, n, function () { const v = parseInt(N.attr(this, n), 10); return isNaN(v) || v < 0 ? d : v; }, function (v) { this.setAttribute(n, String(v >>> 0)); canvasResized(this); });
    methods(p, {
        /* one context per canvas: asking for another kind gives null */
        getContext(t, o) {
            t = String(t);
            const kind = t === 'experimental-webgl' ? 'webgl' : t;
            if (this.__ck) return this.__ck === kind ? this.__cx : null;
            const x = kind === '2d' ? new CanvasRenderingContext2D(this) : kind === 'webgl' ? glCreate(this, 1, o) : kind === 'webgl2' ? glCreate(this, 2, o)
                : kind === 'webgpu' && gpuNavigator ? gpuCanvasContext(this) : null;
            if (x) { def(this, '__ck', kind); def(this, '__cx', x); }
            return x;
        },
        captureStream(fps) { return canvasStream(this, fps); },
        toDataURL() { return 'data:,'; }, toBlob(cb) { setTimeout(() => cb(null)); },
    });
});
mk('HTMLFormElement', HTMLElement, ['form'], p => {
    reflectUrl(p, 'action'); reflectStr(p, 'name', 'target', 'acceptCharset', 'autocomplete'); reflectBool(p, 'noValidate');
    methods(p, {
        get method() { return (N.attr(this, 'method') || 'get').toLowerCase() === 'post' ? 'post' : 'get'; }, set method(v) { this.setAttribute('method', v); },
        get enctype() { return N.attr(this, 'enctype') || 'application/x-www-form-urlencoded'; },
        get elements() { return nodeList(N.query(this, 'input,select,textarea,button,fieldset,output', true)); }, get length() { return this.elements.length; },
        submit() { submitForm(this, null); },
        requestSubmit(s) { if (dispatch(this, new SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: s || null }), true)) submitForm(this, s); },
        reset() { if (dispatch(this, new Event('reset', { bubbles: true, cancelable: true }))) for (const e of this.elements) N.setValue(e, null); },
        checkValidity() { return true; }, reportValidity() { return true; },
    });
});
/* text selection of <input>/<textarea>: native keeps [anchor, focus] in codepoints, the DOM API speaks UTF-16 */
const cpU16 = (v, cp) => { let i = 0; for (; cp > 0 && i < v.length; cp--) i += v.codePointAt(i) > 0xffff ? 2 : 1; return i; };
const u16Cp = (v, o) => { let n = 0; for (let i = 0; i < o && i < v.length; n++) i += v.codePointAt(i) > 0xffff ? 2 : 1; return n; };
const SEL_TYPES = /^(text|search|url|tel|password)$/;
const selOk = el => el.localName === 'textarea' || SEL_TYPES.test(el.type);
const selGet = el => {
    const v = el.value, r = N.ctlSel(el);
    if (!r) return [v.length, v.length, 'none'];
    const a = Math.min(cpU16(v, r[0]), v.length), f = Math.min(cpU16(v, r[1]), v.length);
    return [Math.min(a, f), Math.max(a, f), f < a ? 'backward' : 'forward'];
};
const selSet = (el, s, e, dir) => {
    const v = el.value, n = v.length;
    s = Math.min(Math.max(0, Math.trunc(+s) || 0), n); e = Math.min(Math.max(0, Math.trunc(+e) || 0), n); if (e < s) s = e;
    const back = dir === 'backward';
    N.setCtlSel(el, u16Cp(v, back ? e : s), u16Cp(v, back ? s : e));
};
const selNo = () => { throw new DOMException("The input element's type does not support selection.", 'InvalidStateError'); };
const textSelection = {
    get selectionStart() { return selOk(this) ? selGet(this)[0] : null; },
    set selectionStart(v) { if (!selOk(this)) selNo(); const r = selGet(this); selSet(this, v, Math.max(+v || 0, r[1]), r[2]); },
    get selectionEnd() { return selOk(this) ? selGet(this)[1] : null; },
    set selectionEnd(v) { if (!selOk(this)) selNo(); const r = selGet(this); selSet(this, Math.min(r[0], +v || 0), v, r[2]); },
    get selectionDirection() { return selOk(this) ? selGet(this)[2] : null; },
    set selectionDirection(d) { if (!selOk(this)) selNo(); const r = selGet(this); selSet(this, r[0], r[1], d); },
    setSelectionRange(s, e, dir) { if (!selOk(this)) selNo(); selSet(this, s, e, dir); },
    select() { if (!selOk(this)) return; selSet(this, 0, this.value.length); setTimeout(() => this.dispatchEvent(new Event('select', { bubbles: true }))); },
    setRangeText(text, start, end, mode = 'preserve') {
        if (!selOk(this)) selNo();
        text = String(text);
        let [ss, se] = selGet(this);
        if (start === undefined) { start = ss; end = se; }
        const v = this.value, n = v.length;
        start = Math.min(Math.max(0, Math.trunc(+start) || 0), n); end = Math.min(Math.max(0, Math.trunc(+end) || 0), n);
        if (start > end) throw new DOMException('The provided start value is larger than the end.', 'IndexSizeError');
        this.value = v.slice(0, start) + text + v.slice(end);
        const nl = start + text.length;
        if (mode === 'select') { ss = start; se = nl; }
        else if (mode === 'start') ss = se = start;
        else if (mode === 'end') ss = se = nl;
        else {   /* preserve */
            const d = text.length - (end - start);
            if (ss > end) ss += d; else if (ss > start) ss = start;
            if (se > end) se += d; else if (se > start) se = nl;
        }
        selSet(this, ss, se);
    },
};
const INPUT_TYPES = /^(hidden|text|search|tel|url|email|password|date|month|week|time|datetime-local|number|range|color|checkbox|radio|file|submit|image|reset|button)$/;
mk('HTMLInputElement', HTMLElement, ['input'], p => {
    reflectStr(p, 'name', 'placeholder', 'accept', 'alt', 'autocomplete', 'inputMode', 'max', 'min', 'pattern', 'step', 'enterKeyHint');
    reflectBool(p, 'disabled', 'readOnly', 'required', 'multiple'); reflectUrl(p, 'src'); reflectInt(p, 20, 'size'); reflectInt(p, -1, 'maxLength', 'minLength'); formControl(p);
    methods(p, {
        get type() { const t = (N.attr(this, 'type') || 'text').toLowerCase(); return INPUT_TYPES.test(t) ? t : 'text'; }, set type(v) { this.setAttribute('type', v); },
        get value() { if (this.type === 'file') { const f = this.files; return f.length ? 'C:\\fakepath\\' + f[0].name : ''; } const v = N.value(this); return v != null ? v : (/^(checkbox|radio)$/.test(this.type) ? (N.attr(this, 'value') ?? 'on') : N.attr(this, 'value') ?? ''); },
        set value(v) { if (this.type === 'file') { if (v === '' || v == null) def(this, '_files', new FileList()); return; } N.setValue(this, v == null ? '' : String(v)); },
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
    reflectStr(p, 'name', 'placeholder', 'autocomplete', 'wrap'); reflectBool(p, 'disabled', 'readOnly', 'required'); reflectInt(p, 20, 'cols'); reflectInt(p, 2, 'rows'); formControl(p);
    methods(p, {
        get type() { return 'textarea'; },
        get value() { const v = N.value(this); return v != null ? v : N.text(this); }, set value(v) { N.setValue(this, v == null ? '' : String(v)); },
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
mk('HTMLSelectElement', HTMLElement, ['select'], p => {
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
mk('HTMLUnknownElement', HTMLElement, []);
