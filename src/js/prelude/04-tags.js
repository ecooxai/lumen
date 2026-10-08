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
mk('HTMLTableElement', HTMLElement, ['table'], p => methods(p, { get rows() { return nodeList(N.query(this, 'tr', true)); }, get tBodies() { return nodeList(elKids(this).filter(e => N.name(e) === 'tbody')); } }));
mk('HTMLTableSectionElement', HTMLElement, ['tbody', 'thead', 'tfoot'], p => methods(p, { get rows() { return nodeList(elKids(this).filter(e => N.name(e) === 'tr')); } }));
mk('HTMLTableRowElement', HTMLElement, ['tr'], p => methods(p, { get cells() { return nodeList(elKids(this).filter(e => /^t[dh]$/.test(N.name(e)))); }, insertCell(i = -1) { const td = N.create('td', 0); const c = this.cells; insertNode(this, td, i < 0 || i >= c.length ? null : c[i]); return td; } }));
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
mk('HTMLIFrameElement', HTMLElement, ['iframe'], p => { reflectUrl(p, 'src'); reflectStr(p, 'srcdoc', 'name', 'allow', 'width', 'height', 'referrerPolicy', 'loading', 'sandbox'); methods(p, { get contentWindow() { return asWin(N.frameWin(this)); }, get contentDocument() { return N.frameDoc(this); } }); });
mk('HTMLCanvasElement', HTMLElement, ['canvas'], p => { reflectInt(p, 300, 'width'); reflectInt(p, 150, 'height'); methods(p, { getContext(t) { return t === '2d' ? (this._ctx2d || (this._ctx2d = new CanvasRenderingContext2D(this))) : null; }, toDataURL() { return 'data:,'; }, toBlob(cb) { setTimeout(() => cb(null)); } }); });
mk('HTMLFormElement', HTMLElement, ['form'], p => {
    reflectUrl(p, 'action'); reflectStr(p, 'name', 'target', 'acceptCharset', 'autocomplete'); reflectBool(p, 'noValidate');
    methods(p, {
        get method() { return (N.attr(this, 'method') || 'get').toLowerCase() === 'post' ? 'post' : 'get'; }, set method(v) { this.setAttribute('method', v); },
        get enctype() { return N.attr(this, 'enctype') || 'application/x-www-form-urlencoded'; },
        get elements() { return nodeList(N.query(this, 'input,select,textarea,button,fieldset,output', true)); }, get length() { return this.elements.length; },
        submit() { submitForm(this, null); },
        requestSubmit(s) { if (dispatch(this, new Event('submit', { bubbles: true, cancelable: true }))) submitForm(this, s); },
        reset() { if (dispatch(this, new Event('reset', { bubbles: true, cancelable: true }))) for (const e of this.elements) N.setValue(e, null); },
        checkValidity() { return true; }, reportValidity() { return true; },
    });
});
const INPUT_TYPES = /^(hidden|text|search|tel|url|email|password|date|month|week|time|datetime-local|number|range|color|checkbox|radio|file|submit|image|reset|button)$/;
mk('HTMLInputElement', HTMLElement, ['input'], p => {
    reflectStr(p, 'name', 'placeholder', 'accept', 'alt', 'autocomplete', 'inputMode', 'max', 'min', 'pattern', 'step', 'enterKeyHint');
    reflectBool(p, 'disabled', 'readOnly', 'required', 'multiple'); reflectUrl(p, 'src'); reflectInt(p, 20, 'size'); reflectInt(p, -1, 'maxLength', 'minLength'); formControl(p);
    methods(p, {
        get type() { const t = (N.attr(this, 'type') || 'text').toLowerCase(); return INPUT_TYPES.test(t) ? t : 'text'; }, set type(v) { this.setAttribute('type', v); },
        get value() { const v = N.value(this); return v != null ? v : (/^(checkbox|radio)$/.test(this.type) ? (N.attr(this, 'value') ?? 'on') : N.attr(this, 'value') ?? ''); },
        set value(v) { N.setValue(this, v == null ? '' : String(v)); },
        get defaultValue() { return N.attr(this, 'value') ?? ''; }, set defaultValue(v) { this.setAttribute('value', v); },
        get checked() { return N.checked(this); }, set checked(v) { N.setChecked(this, !!v); },
        get defaultChecked() { return this.hasAttribute('checked'); },
        get valueAsNumber() { return parseFloat(this.value); }, set valueAsNumber(v) { this.value = String(v); },
        get files() { return this.type === 'file' ? [] : null; },
        get selectionStart() { return this.value.length; }, set selectionStart(v) {}, get selectionEnd() { return this.value.length; }, set selectionEnd(v) {},
        get indeterminate() { return !!this.__ind; }, set indeterminate(v) { def(this, '__ind', !!v); },
        select() {}, setSelectionRange() {}, setRangeText() {}, showPicker() {}, stepUp() {}, stepDown() {},
    });
});
mk('HTMLTextAreaElement', HTMLElement, ['textarea'], p => {
    reflectStr(p, 'name', 'placeholder', 'autocomplete', 'wrap'); reflectBool(p, 'disabled', 'readOnly', 'required'); reflectInt(p, 20, 'cols'); reflectInt(p, 2, 'rows'); formControl(p);
    methods(p, {
        get type() { return 'textarea'; },
        get value() { const v = N.value(this); return v != null ? v : N.text(this); }, set value(v) { N.setValue(this, v == null ? '' : String(v)); },
        get defaultValue() { return N.text(this); }, set defaultValue(v) { this.textContent = v; },
        get selectionStart() { return this.value.length; }, set selectionStart(v) {}, get selectionEnd() { return this.value.length; }, set selectionEnd(v) {},
        select() {}, setSelectionRange() {},
    });
});
mk('HTMLButtonElement', HTMLElement, ['button'], p => {
    reflectStr(p, 'name', 'value'); reflectBool(p, 'disabled'); formControl(p);
    methods(p, { get type() { const t = (N.attr(this, 'type') || 'submit').toLowerCase(); return t === 'reset' || t === 'button' ? t : 'submit'; }, set type(v) { this.setAttribute('type', v); } });
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
    });
});
mk('HTMLLabelElement', HTMLElement, ['label'], p => { acc(p, 'htmlFor', function () { return N.attr(this, 'for') ?? ''; }, function (v) { this.setAttribute('for', v); }); methods(p, { get control() { const f = N.attr(this, 'for'); return f ? N.byId(f) : this.querySelector('input,select,textarea,button'); } }); });
mk('HTMLFieldSetElement', HTMLElement, ['fieldset'], p => reflectBool(p, 'disabled'));
mk('HTMLDetailsElement', HTMLElement, ['details'], p => acc(p, 'open', function () { return N.attr(this, 'open') != null; }, function (v) { const was = this.open; this.toggleAttribute('open', !!v); if (was !== !!v) setTimeout(() => dispatch(this, new Event('toggle'))); }));
mk('HTMLDialogElement', HTMLElement, ['dialog'], p => { reflectBool(p, 'open'); methods(p, { show() { this.open = true; }, showModal() { this.open = true; }, close(rv) { if (!this.open) return; this.open = false; if (rv !== undefined) this.returnValue = rv; dispatch(this, new Event('close')); } }); });
mk('HTMLUnknownElement', HTMLElement, []);
