/* Forms per the HTML standard: input value modes and value sanitization, valueAsNumber/valueAsDate and stepUp/Down,
   form owners (the form attribute), form.elements, the select element's options and selectedness, labels, and
   constraint validation. Replaces the simpler members 04-tags.js installs. */
const isHtmlEl = (e, name) => N.isNode(e) && N.type(e) === 1 && N.ns(e) === 0 && (name === undefined || N.name(e) === name);
const LISTED = new Set(['button', 'fieldset', 'input', 'object', 'output', 'select', 'textarea']);
const LABELABLE = new Set(['button', 'input', 'meter', 'output', 'progress', 'select', 'textarea']);
const ASCII_WS = /^[\t\n\f\r ]+|[\t\n\f\r ]+$/g;
const stripWs = s => s.replace(ASCII_WS, '');
const inputTypeOf = raw => { const t = (raw || 'text').toLowerCase(); return INPUT_TYPES.test(t) ? t : 'text'; };
const inType = el => inputTypeOf(N.attr(el, 'type'));
const VALUE_MODE = /^(text|search|tel|url|email|password|date|month|week|time|datetime-local|number|range|color)$/;
const valueMode = t => VALUE_MODE.test(t) ? 'value' : t === 'checkbox' || t === 'radio' ? 'on' : t === 'file' ? 'file' : 'default';

/* ---- numbers, dates and times: the "convert a string to a number" and back algorithms of each input type ---- */
const FLOAT_RE = /^-?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][-+]?\d+)?$/;
const pNum = s => { if (!FLOAT_RE.test(s)) return NaN; const n = +s; return Number.isFinite(n) ? n + 0 : NaN; };
const daysIn = (y, m) => m === 2 ? ((y % 4 === 0 && y % 100 !== 0) || y % 400 === 0 ? 29 : 28) : [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1];
const utc = (y, m, d) => { const t = new Date(0); t.setUTCFullYear(y, m, d); t.setUTCHours(0, 0, 0, 0); return t.getTime(); };
const MAX_MS = 8.64e15;
const dateParts = s => {
    const m = /^(\d{4,})-(\d\d)-(\d\d)$/.exec(s); if (!m) return null;
    const y = +m[1], mo = +m[2], d = +m[3];
    return y >= 1 && mo >= 1 && mo <= 12 && d >= 1 && d <= daysIn(y, mo) ? [y, mo, d] : null;
};
const pDate = s => { const p = dateParts(s); if (!p) return NaN; const t = utc(p[0], p[1] - 1, p[2]); return Math.abs(t) <= MAX_MS ? t : NaN; };
const pMonth = s => {
    const m = /^(\d{4,})-(\d\d)$/.exec(s); if (!m) return NaN;
    const y = +m[1], mo = +m[2];
    if (y < 1 || mo < 1 || mo > 12 || Math.abs(utc(y, mo - 1, 1)) > MAX_MS) return NaN;
    return (y - 1970) * 12 + mo - 1;
};
const week1 = y => { const j4 = utc(y, 0, 4), dow = (new Date(j4).getUTCDay() + 6) % 7; return j4 - dow * 864e5; };
const weeksIn = y => { const j1 = new Date(utc(y, 0, 1)).getUTCDay(), leap = daysIn(y, 2) === 29; return j1 === 4 || (leap && j1 === 3) ? 53 : 52; };
const pWeek = s => {
    const m = /^(\d{4,})-W(\d\d)$/.exec(s); if (!m) return NaN;
    const y = +m[1], w = +m[2];
    if (y < 1 || w < 1 || w > weeksIn(y)) return NaN;
    const t = week1(y) + (w - 1) * 6048e5;
    return Math.abs(t) <= MAX_MS ? t : NaN;
};
const pTime = s => {
    const m = /^(\d\d):(\d\d)(?::(\d\d)(?:\.(\d{1,3}))?)?$/.exec(s); if (!m) return NaN;
    const h = +m[1], mi = +m[2], sec = m[3] ? +m[3] : 0;
    if (h > 23 || mi > 59 || sec > 59) return NaN;
    return ((h * 60 + mi) * 60 + sec) * 1000 + (m[4] ? +m[4].padEnd(3, '0') : 0);
};
const pDTL = s => {
    const m = /^([^T ]+)[T ](.+)$/.exec(s); if (!m) return NaN;
    const d = pDate(m[1]), t = pTime(m[2]);
    return Number.isNaN(d) || Number.isNaN(t) || Math.abs(d + t) > MAX_MS ? NaN : d + t;
};
const pad = (n, w = 2) => String(n).padStart(w, '0');
const sDate = ms => { const d = new Date(ms); if (Number.isNaN(d.getTime()) || d.getUTCFullYear() < 1) return ''; return `${pad(d.getUTCFullYear(), 4)}-${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())}`; };
const sMonth = n => { n = Math.floor(n); const y = 1970 + Math.floor(n / 12), mo = ((n % 12) + 12) % 12 + 1; return y < 1 ? '' : `${pad(y, 4)}-${pad(mo)}`; };
const sWeek = ms => {
    const d = new Date(ms); if (Number.isNaN(d.getTime())) return '';
    const day = utc(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()), thu = day + (3 - (new Date(day).getUTCDay() + 6) % 7) * 864e5;
    const y = new Date(thu).getUTCFullYear();
    if (y < 1) return '';
    return `${pad(y, 4)}-W${pad(Math.floor((thu - week1(y)) / 6048e5) + 1)}`;
};
const sTime = ms => {
    ms = ((Math.round(ms) % 864e5) + 864e5) % 864e5;
    const h = Math.floor(ms / 36e5), mi = Math.floor(ms / 6e4) % 60, s = Math.floor(ms / 1e3) % 60, f = ms % 1000;
    return `${pad(h)}:${pad(mi)}` + (s || f ? ':' + pad(s) : '') + (f ? '.' + pad(f, 3) : '');
};
const sDTL = ms => { const day = Math.floor(ms / 864e5) * 864e5, d = sDate(day); return d ? d + 'T' + sTime(ms - day) : ''; };
const sNum = n => String(+n.toPrecision(15));
/* per numeric type: parse, serialize, default step, step scale factor, default step base, whole-number steps */
const NUMERIC = {
    number: { parse: pNum, ser: sNum, step: 1, scale: 1, base: 0 },
    range: { parse: pNum, ser: sNum, step: 1, scale: 1, base: 0 },
    date: { parse: pDate, ser: sDate, step: 1, scale: 864e5, base: 0, int: true, date: true },
    month: { parse: pMonth, ser: sMonth, step: 1, scale: 1, base: 0, int: true, date: true },
    week: { parse: pWeek, ser: sWeek, step: 1, scale: 6048e5, base: -2592e5, int: true, date: true },
    time: { parse: pTime, ser: sTime, step: 60, scale: 1000, base: 0, date: true },
    'datetime-local': { parse: pDTL, ser: sDTL, step: 60, scale: 1000, base: 0 },
};
const numAttrOf = (el, t, a) => { const v = N.attr(el, a); return v == null ? NaN : NUMERIC[t].parse(v); };
/* the minimum and maximum (with range's defaults), and the allowed value step (null: "any") and its base */
function rangeOf(el, t) {
    const T = NUMERIC[t];
    let min = numAttrOf(el, t, 'min'), max = numAttrOf(el, t, 'max');
    if (t === 'range') { if (Number.isNaN(min)) min = 0; if (Number.isNaN(max)) max = 100; if (max < min) max = min; }
    let step = T.step * T.scale;
    const sa = N.attr(el, 'step');
    if (sa != null) {
        if (sa.toLowerCase() === 'any') step = null;
        else { let s = pNum(sa); if (s > 0) { if (T.int) s = Math.max(1, Math.round(s)); step = s * T.scale; } }
    }
    let base = numAttrOf(el, t, 'min');
    if (Number.isNaN(base)) base = numAttrOf(el, t, 'value');
    if (Number.isNaN(base)) base = t === 'range' ? min : T.base;
    if (t === 'range' && step !== null && max > min) { const k = Math.floor((max - base) / step + 1e-9); const m2 = base + k * step; if (m2 >= min) max = +m2.toPrecision(15); }
    return { min, max, step, base };
}
const misaligned = (v, r) => { if (r.step === null) return false; const q = (v - r.base) / r.step; return Math.abs(q - Math.round(q)) > 1e-9 * Math.max(1, Math.abs(q)); };

/* ---- value sanitization ---- */
function sanitizeAs(el, t, v) {
    switch (t) {
    case 'text': case 'search': case 'tel': case 'password': return v.replace(/[\r\n]/g, '');
    case 'url': return stripWs(v.replace(/[\r\n]/g, ''));
    case 'email': v = v.replace(/[\r\n]/g, ''); return N.attr(el, 'multiple') != null ? v.split(',').map(stripWs).join(',') : stripWs(v);
    case 'number': case 'date': case 'month': case 'week': case 'time': return Number.isNaN(NUMERIC[t].parse(v)) ? '' : v;
    case 'datetime-local': { const n = pDTL(v); return Number.isNaN(n) ? '' : sDTL(n); }
    case 'color': return colorWellValue(el, v);
    case 'range': {
        const r = rangeOf(el, t);
        let n = pNum(v);
        if (Number.isNaN(n)) n = r.max < r.min ? r.min : r.min + (r.max - r.min) / 2;
        if (r.step !== null && misaligned(n, r)) { n = r.base + Math.round((n - r.base) / r.step) * r.step; if (n > r.max) n -= r.step; }
        n = Math.min(Math.max(n, r.min), r.max);
        return sNum(n);
    }
    }
    return v;
}
/* a color well's value: any CSS color, serialized for the colorspace and alpha attributes; opaque black otherwise */
const colorSpaceOf = el => asciiLower(N.attr(el, 'colorspace') ?? '') === 'display-p3' ? 'display-p3' : 'limited-srgb';
function colorWellValue(el, v) {
    let col = cssColor(v);
    if (!col || col.current) col = { space: 'srgb', c: [0, 0, 0], alpha: 1 };
    const withAlpha = N.attr(el, 'alpha') != null, tail = a => withAlpha && a < 1 ? ' / ' + colorNum(a) : '';
    if (colorSpaceOf(el) === 'display-p3') return `color(display-p3 ${colorTo(col, 'display-p3').map(colorNum).join(' ')}${tail(col.alpha)})`;
    const c = colorTo(col, 'srgb').map(q8);
    if (!withAlpha) return '#' + c.map(x => Math.round(x * 255).toString(16).padStart(2, '0')).join('');
    return `color(srgb ${c.map(colorNum).join(' ')}${tail(q8(col.alpha))})`;
}
const inputValue = el => {
    const t = inType(el), d = N.value(el);
    switch (valueMode(t)) {
    case 'value': return sanitizeAs(el, t, d != null ? d : N.attr(el, 'value') ?? '');
    case 'on': return N.attr(el, 'value') ?? 'on';
    case 'file': { const f = el.files; return f && f.length ? 'C:\\fakepath\\' + f[0].name : ''; }
    }
    return N.attr(el, 'value') ?? '';
};
/* the type attribute's change steps: move the value between the dirty value and the value attribute, then sanitize */
function inputTypeChanged(el, oldRaw, newRaw) {
    const ot = inputTypeOf(oldRaw), nt = inputTypeOf(newRaw);
    if (ot === nt) return;
    const om = valueMode(ot), nm = valueMode(nt);
    if (om === 'value' && (nm === 'default' || nm === 'on')) {
        const d = N.value(el), v = sanitizeAs(el, ot, d != null ? d : N.attr(el, 'value') ?? '');
        N.setValue(el, null);
        if (v !== '' && d != null) el.setAttribute('value', v);
    } else if (om !== 'value' && nm === 'value') N.setValue(el, null);
    else if (nm === 'file') N.setValue(el, null);
    else if (nm === 'value') { const d = N.value(el); if (d != null) N.setValue(el, sanitizeAs(el, nt, d)); }
    if (om === 'file' && el._files) def(el, '_files', null);
    if (!SEL_TYPES.test(ot) && SEL_TYPES.test(nt)) selStore.set(el, [0, 0, 'none']);
}

const valueChangedSel = (el, before) => { if (el.value !== before) { const n = el.value.length; selStore.set(el, [n, n, 'none']); } };

/* ---- form owners ---- */
const rootById = (el, id) => {
    const r = el.getRootNode();
    if (r && typeof r.getElementById === 'function') return r.getElementById(id);
    return null;
};
function formOwner(el) {
    if (!isHtmlEl(el)) return null;
    const name = N.name(el);
    if (!LISTED.has(name) && name !== 'img') return null;
    const fa = name === 'img' ? null : N.attr(el, 'form');
    if (fa != null && el.isConnected) { const f = fa ? rootById(el, fa) : null; return isHtmlEl(f, 'form') ? f : null; }
    for (let p = N.parent(el); p; p = N.parent(p)) if (isHtmlEl(p, 'form')) return p;
    return null;
}
/* the form's listed elements in tree order; an image button is not among them */
function formElements(form) {
    const root = form.getRootNode();
    const all = N.query(root.nodeType === 1 ? form : root, 'button,fieldset,input,object,output,select,textarea', true);
    return all.filter(e => N.ns(e) === 0 && !(N.name(e) === 'input' && inType(e) === 'image') && formOwner(e) === form);
}
class RadioNodeList extends NodeList {
    get value() { for (const e of listOf(this, 'value', 'RadioNodeList')) if (isHtmlEl(e, 'input') && inType(e) === 'radio' && N.checked(e)) return e.value; return ''; }
    set value(v) {
        v = String(v);
        for (const e of listOf(this, 'value', 'RadioNodeList'))
            if (isHtmlEl(e, 'input') && inType(e) === 'radio' && e.value === v) { e.checked = true; return; }
    }
}
const radioList = a => staticList(RadioNodeList.prototype, a);
class HTMLFormControlsCollection extends HTMLCollection {
    namedItem(k) {
        listNeed(arguments.length, 'namedItem', 'HTMLFormControlsCollection');
        const a = listOf(this, 'namedItem', 'HTMLFormControlsCollection'); k = String(k);
        if (!k) return null;
        const m = a.filter(e => N.attr(e, 'id') === k || N.attr(e, 'name') === k);
        return m.length === 0 ? null : m.length === 1 ? m[0] : liveRadio(this, k);
    }
}
const radioCache = new WeakMap();
function liveRadio(coll, k) {
    let m = radioCache.get(coll); if (!m) radioCache.set(coll, m = new Map());
    let r = m.get(k);
    if (!r) { const f = listItems.get(coll); r = liveWith(null, () => f().filter(e => N.attr(e, 'id') === k || N.attr(e, 'name') === k), RadioNodeList.prototype, liveHandler); m.set(k, r); }
    return r;
}
const fccNamed = (t, k) => {
    if (typeof k !== 'string' || !k || liveIdx(k) >= 0 || Object.hasOwn(t, k) || k in Object.getPrototypeOf(t)) return null;
    return HTMLFormControlsCollection.prototype.namedItem.call(t, k);
};
const fccHandler = {
    ...collHandler,
    get(t, k, r) { if (k === 'length') return listItems.get(t)().length; const i = liveIdx(k); if (i >= 0) return listItems.get(t)()[i]; const e = fccNamed(t, k); return e || Reflect.get(t, k, r); },
    has(t, k) { const i = liveIdx(k); return i >= 0 ? i < listItems.get(t)().length : Reflect.has(t, k) || !!fccNamed(t, k); },
    getOwnPropertyDescriptor(t, k) {
        const i = liveIdx(k);
        if (i >= 0) { const a = listItems.get(t)(); return i < a.length ? { value: a[i], enumerable: true, configurable: true, writable: false } : undefined; }
        const e = fccNamed(t, k);
        return e ? { value: e, enumerable: false, configurable: true, writable: false } : Reflect.getOwnPropertyDescriptor(t, k);
    },
};
/* a live list with its own proxy handler, recomputed when the document mutates or, without an owner, on every use
   (attribute and form-state changes that decide membership need not bump the document's version) */
function liveWith(owner, compute, proto, handler = collHandler) {
    const t = Object.create(proto);
    let ver = -1, arr = [];
    const cur = owner ? () => { const v = N.version(owner); if (v !== ver) { ver = v; arr = compute(); } return arr; } : compute;
    const p = new Proxy(t, handler);
    listItems.set(t, cur); listItems.set(p, cur);
    return p;
}
const formColl = new WeakMap();
const elementsOf = form => { let c = formColl.get(form); if (!c) formColl.set(form, c = liveWith(null, () => formElements(form), HTMLFormControlsCollection.prototype, fccHandler)); return c; };

/* ---- select and option ---- */
/* the select's list of options: option children, and option children of optgroup children */
function optionList(sel) {
    const out = [];
    for (let c = N.first(sel); c; c = N.next(c)) {
        if (!isHtmlEl(c)) continue;
        const n = N.name(c);
        if (n === 'option') out.push(c);
        else if (n === 'optgroup') for (let d = N.first(c); d; d = N.next(d)) if (isHtmlEl(d, 'option')) out.push(d);
    }
    return out;
}
const selectOf = o => {
    const p = N.parent(o); if (!p || !isHtmlEl(p)) return null;
    if (N.name(p) === 'select') return p;
    if (N.name(p) === 'optgroup') { const q = N.parent(p); return q && isHtmlEl(q, 'select') ? q : null; }
    return null;
};
const displaySize = sel => { const s = N.attr(sel, 'size'); const n = s != null && /^\s*\d+/.test(s) ? parseInt(s, 10) : 0; return n > 0 ? n : N.attr(sel, 'multiple') != null ? 4 : 1; };
const optDisabled = o => { if (N.attr(o, 'disabled') != null) return true; const p = N.parent(o); return !!p && isHtmlEl(p, 'optgroup') && N.attr(p, 'disabled') != null; };
/* selectedness: an option's own (dirty) state or its selected attribute; a single-choice drop-down select shows
   exactly one, the last selected, or the first enabled option when none is */
function selectedOptions(sel) {
    const opts = optionList(sel), on = opts.filter(o => N.checked(o));
    if (N.attr(sel, 'multiple') != null) return on;
    if (on.length) return [on[on.length - 1]];
    if (displaySize(sel) === 1) { const f = opts.find(o => !optDisabled(o)); return f ? [f] : []; }
    return [];
}
const setSelected = (o, b) => N.setChecked(o, !!b);
const checkDirty = new WeakSet(), optDirty = new WeakSet();
/* attribute changes from script: the type's change steps, and checked/selected while the state is not dirty */
function formAttr(el, name, old, v) {
    if ((name === 'alpha' || name === 'colorspace') && N.name(el) === 'input' && inType(el) === 'color') {
        const d = N.value(el);   /* a dirty value is sanitized again; a clean one follows the value attribute */
        if (d != null) N.setValue(el, colorWellValue(el, d));
        return;
    }
    if ((old == null) === (v == null) && name !== 'type') return;
    const t = N.name(el);
    if (t === 'input') {
        if (name === 'type') inputTypeChanged(el, old, v);
        else if (name === 'checked' && !checkDirty.has(el)) setCheckedness(el, v != null);
    } else if (t === 'option' && name === 'selected' && !optDirty.has(el)) {
        const s = selectOf(el);
        if (v != null && s && N.attr(s, 'multiple') == null) selectOnly(s, el); else setSelected(el, v != null);
    }
}
function setCheckedness(el, v) {
    if (v && inType(el) === 'radio') for (const r of radioGroup(el)) if (r !== el && N.checked(r)) N.setChecked(r, false);
    N.setChecked(el, v);
}
function selectOnly(sel, opt) { for (const o of optionList(sel)) setSelected(o, o === opt); }
class HTMLOptionsCollection extends HTMLCollection {
    get length() { return listOf(this, 'length', 'HTMLOptionsCollection').length; }
    set length(v) { optsSetLength(optsOwner.get(this), v); }
    get selectedIndex() { return optsOwner.get(this).selectedIndex; }
    set selectedIndex(v) { optsOwner.get(this).selectedIndex = v; }
    add(element, before = null) { return selectAdd(optsOwner.get(this), element, before, 'HTMLOptionsCollection'); }
    remove(index) { const sel = optsOwner.get(this); const o = optionList(sel)[toI32(index)]; if (o) o.remove(); }
}
const optsOwner = new WeakMap();
const toI32 = v => { v = Number(v); return Number.isFinite(v) ? Math.trunc(v) | 0 : 0; };
function optsSetLength(sel, v) {
    const n = toU32(v), opts = optionList(sel);
    if (n > 100000) return;
    if (n > opts.length) { const f = document.createDocumentFragment(); for (let i = opts.length; i < n; i++) f.appendChild(document.createElement('option')); sel.appendChild(f); }
    else for (let i = opts.length - 1; i >= n; i--) opts[i].remove();
}
function optsSetIndex(sel, i, v) {
    if (v === null || v === undefined) { const o = optionList(sel)[i]; if (o) o.remove(); return true; }
    if (!isHtmlEl(v, 'option')) throw new TypeError(`Failed to set an indexed property [${i}] on 'HTMLOptionsCollection': The provided value is not of type 'HTMLOptionElement'.`);
    const opts = optionList(sel);
    if (i > opts.length) optsSetLength(sel, i);
    const cur = optionList(sel)[i];
    if (cur) { if (cur !== v) N.parent(cur).replaceChild(v, cur); }
    else sel.appendChild(v);
    return true;
}
function selectAdd(sel, element, before, iface) {
    if (!isHtmlEl(element, 'option') && !isHtmlEl(element, 'optgroup')) throw new TypeError(`Failed to execute 'add' on '${iface}': The provided value is not of type '(HTMLOptGroupElement or HTMLOptionElement)'.`);
    if (element.contains(sel)) throw new DOMException(`Failed to execute 'add' on '${iface}': The new child element contains the parent.`, 'HierarchyRequestError');
    let ref = null;
    if (typeof before === 'number' || (before !== null && typeof before !== 'object')) ref = optionList(sel)[toI32(before)] || null;
    else if (before !== null) {
        if (!N.isNode(before) || !isHtmlEl(before)) throw new TypeError(`Failed to execute 'add' on '${iface}': The provided value is not of type '(HTMLElement or long)'.`);
        if (!sel.contains(before)) throw new DOMException(`Failed to execute 'add' on '${iface}': The node before which the new node is to be inserted is not a child of this node.`, 'NotFoundError');
        ref = before;
    }
    if (ref === element) return;
    (ref ? N.parent(ref) : sel).insertBefore(element, ref);
}
const optsHandler = {
    ...collHandler,
    set(t, k, v, r) {
        const i = liveIdx(k);
        if (i >= 0) return optsSetIndex(optsOwner.get(t), i, v);
        if (k === 'length') { optsSetLength(optsOwner.get(t), v); return true; }
        return collNamed(t, k) ? false : Reflect.set(t, k, v, r);
    },
    defineProperty(t, k, d) {
        const i = liveIdx(k);
        if (i >= 0) { if (!('value' in d)) return false; return optsSetIndex(optsOwner.get(t), i, d.value); }
        return collNamed(t, k) ? false : Reflect.defineProperty(t, k, d);
    },
};
const optsColl = new WeakMap();
function optionsOf(sel) {
    let c = optsColl.get(sel);
    if (!c) {
        c = liveWith(sel, () => optionList(sel), HTMLOptionsCollection.prototype, optsHandler);
        optsOwner.set(c, sel);
        optsColl.set(sel, c);
    }
    return c;
}
const optText = o => {
    let s = '';
    const walk = n => {
        for (let c = N.first(n); c; c = N.next(c)) {
            const ty = N.type(c);
            if (ty === 3 || ty === 4) s += N.text(c);
            else if (ty === 1 && !(N.name(c) === 'script' && (N.ns(c) === 0 || N.ns(c) === 2))) walk(c);
        }
    };
    walk(o);
    return s.replace(/[\t\n\f\r ]+/g, ' ').replace(/^ | $/g, '');
};

/* ---- labels ---- */
const labelable = e => isHtmlEl(e) && (LABELABLE.has(N.name(e)) ? !(N.name(e) === 'input' && inType(e) === 'hidden') : !!(e.__ce && e.__ce.formAssociated));
function labelControl(label) {
    const f = N.attr(label, 'for');
    if (f != null) { const e = f ? rootById(label, f) : null; return e && labelable(e) ? e : null; }
    for (const e of N.query(label, '*', true)) if (labelable(e)) return e;
    return null;
}
function labelsOf(el) {
    if (!labelable(el)) return null;
    let c = labelsCache.get(el);
    if (!c) {
        c = liveWith(null, () => N.query(el.getRootNode(), 'label', true).filter(l => labelControl(l) === el), NodeList.prototype, liveHandler);
        labelsCache.set(el, c);
    }
    return c;
}
const labelsCache = new WeakMap();

/* ---- constraint validation ---- */
const customMsg = new WeakMap();
const READONLY_TYPES = /^(text|search|url|tel|email|password|date|month|week|time|datetime-local|number)$/;
const REQUIRED_TYPES = /^(text|search|url|tel|email|password|date|month|week|time|datetime-local|number|checkbox|radio|file)$/;
const PATTERN_TYPES = /^(text|search|url|tel|email|password)$/;
const inDatalist = el => { for (let p = N.parent(el); p; p = N.parent(p)) if (isHtmlEl(p, 'datalist')) return true; return false; };
const ctlDisabled = el => {
    if (N.attr(el, 'disabled') != null) return true;
    let prev = el;
    for (let p = N.parent(el); p; prev = p, p = N.parent(p)) {
        if (isHtmlEl(p, 'fieldset') && N.attr(p, 'disabled') != null) {
            let legend = null;
            for (let c = N.first(p); c; c = N.next(c)) if (isHtmlEl(c, 'legend')) { legend = c; break; }
            if (prev !== legend) return true;
        }
    }
    return false;
};
function willValidate(el) {
    if (!isHtmlEl(el)) return false;
    switch (N.name(el)) {
    case 'input': {
        const t = inType(el);
        if (t === 'hidden' || t === 'reset' || t === 'button') return false;
        if (READONLY_TYPES.test(t) && N.attr(el, 'readonly') != null) return false;
        break;
    }
    case 'button': { const t = el.type; if (t !== 'submit') return false; break; }
    case 'select': break;
    case 'textarea': if (N.attr(el, 'readonly') != null) return false; break;
    default: return false;
    }
    return !ctlDisabled(el) && !inDatalist(el);
}
let patternCache = new Map();
const patternRe = p => {
    let r = patternCache.get(p);
    if (r === undefined) {
        try { r = new RegExp('^(?:' + p + ')$', 'v'); } catch (e) { r = null; }
        if (patternCache.size > 200) patternCache = new Map();
        patternCache.set(p, r);
    }
    return r;
};
const EMAIL_RE = /^[a-zA-Z0-9.!#$%&'*+\/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/;
const radioGroup = el => {
    const name = N.attr(el, 'name'); if (!name) return [el];
    const owner = formOwner(el), root = el.getRootNode();
    const scope = owner || root;
    return N.query(scope.nodeType === 9 || scope.nodeType === 11 || scope.nodeType === 1 ? scope : root, 'input', true)
        .filter(r => r === el || (N.ns(r) === 0 && inType(r) === 'radio' && N.attr(r, 'name') === name && formOwner(r) === owner && r.getRootNode() === root));
};
/* the element's validity flags; all clear for an element barred from constraint validation */
function validityFlags(el) {
    const f = { valueMissing: false, typeMismatch: false, patternMismatch: false, tooLong: false, tooShort: false, rangeUnderflow: false, rangeOverflow: false, stepMismatch: false, badInput: false, customError: false };
    if (!willValidate(el)) return f;
    f.customError = !!customMsg.get(el);
    const name = N.name(el);
    if (name === 'input') {
        const t = inType(el), v = inputValue(el), req = N.attr(el, 'required') != null && REQUIRED_TYPES.test(t);
        if (req) {
            if (t === 'checkbox') f.valueMissing = !N.checked(el);
            else if (t === 'radio') f.valueMissing = !radioGroup(el).some(r => N.checked(r));
            else if (t === 'file') f.valueMissing = !(el.files && el.files.length);
            else f.valueMissing = v === '';
        } else if (t === 'radio' && N.attr(el, 'name')) {
            const g = radioGroup(el);
            f.valueMissing = g.some(r => N.attr(r, 'required') != null) && !g.some(r => N.checked(r));
        }
        if (v !== '') {
            if (t === 'email') f.typeMismatch = (N.attr(el, 'multiple') != null ? v.split(',') : [v]).some(x => !EMAIL_RE.test(x));
            else if (t === 'url') { try { new URL(v); } catch (e) { f.typeMismatch = true; } }
            const pat = N.attr(el, 'pattern');
            if (pat != null && PATTERN_TYPES.test(t)) {
                const re = patternRe(pat);
                if (re) f.patternMismatch = (t === 'email' && N.attr(el, 'multiple') != null ? v.split(',') : [v]).some(x => !re.test(x));
            }
            if (NUMERIC[t] && t !== 'range') {
                const n = NUMERIC[t].parse(v);
                if (!Number.isNaN(n)) {
                    const r = rangeOf(el, t);
                    if (t === 'time' && !Number.isNaN(r.min) && !Number.isNaN(r.max) && r.min > r.max) {
                        if (n < r.min && n > r.max) f.rangeUnderflow = f.rangeOverflow = true;
                    } else {
                        if (!Number.isNaN(r.min) && n < r.min) f.rangeUnderflow = true;
                        if (!Number.isNaN(r.max) && n > r.max) f.rangeOverflow = true;
                    }
                    f.stepMismatch = misaligned(n, r);
                }
            } else if (t === 'range') f.stepMismatch = misaligned(pNum(v), rangeOf(el, t));
        }
    } else if (name === 'select') {
        if (N.attr(el, 'required') != null) {
            const sel = selectedOptions(el);
            if (!sel.length) f.valueMissing = true;
            else if (N.attr(el, 'multiple') == null && displaySize(el) === 1) {
                const first = optionList(el)[0];
                f.valueMissing = sel[0] === first && first.value === '' && N.parent(first) === el;
            }
        }
    } else if (name === 'textarea') {
        if (N.attr(el, 'required') != null) f.valueMissing = el.value === '';
    }
    return f;
}
const isValid = el => { const f = validityFlags(el); for (const k in f) if (f[k]) return false; return true; };
const VALIDITY_KEY = Symbol('ValidityState'), validityEl = new WeakMap(), validityObj = new WeakMap();
class ValidityState {
    constructor(key) { if (key !== VALIDITY_KEY) throw new TypeError('Illegal constructor'); }
    get valid() { return isValid(validityEl.get(this)); }
}
for (const k of ['valueMissing', 'typeMismatch', 'patternMismatch', 'tooLong', 'tooShort', 'rangeUnderflow', 'rangeOverflow', 'stepMismatch', 'badInput', 'customError'])
    Object.defineProperty(ValidityState.prototype, k, { get: accName(function () { const el = validityEl.get(this); if (!el) throw new TypeError('Illegal invocation'); return validityFlags(el)[k]; }, 'get ' + k), enumerable: true, configurable: true });
Object.defineProperty(ValidityState.prototype, 'valid', { enumerable: true });
const validityOf = el => { let v = validityObj.get(el); if (!v) { v = new ValidityState(VALIDITY_KEY); validityEl.set(v, el); validityObj.set(el, v); } return v; };
const MESSAGES = {
    valueMissing: el => { const t = N.name(el) === 'input' ? inType(el) : N.name(el); return t === 'checkbox' ? 'Please check this box if you want to proceed.' : t === 'radio' || t === 'select' ? 'Please select one of these options.' : t === 'file' ? 'Please select a file.' : 'Please fill out this field.'; },
    typeMismatch: el => inType(el) === 'email' ? 'Please enter an email address.' : 'Please enter a URL.',
    patternMismatch: () => 'Please match the requested format.',
    rangeUnderflow: el => `Value must be greater than or equal to ${N.attr(el, 'min')}.`,
    rangeOverflow: el => `Value must be less than or equal to ${N.attr(el, 'max')}.`,
    stepMismatch: () => 'Please enter a valid value.',
};
function validationMessage(el) {
    if (!willValidate(el)) return '';
    const c = customMsg.get(el); if (c) return c;
    const f = validityFlags(el);
    for (const k in MESSAGES) if (f[k]) return MESSAGES[k](el);
    return '';
}
/* check validity: an invalid candidate gets a cancelable invalid event; returns whether the event was not canceled */
function checkOne(el) {
    if (!willValidate(el) || isValid(el)) return true;
    dispatch(el, new Event('invalid', { cancelable: true }), true);
    return false;
}
function formCheck(form, report) {
    let ok = true, first = null;
    for (const e of formElements(form)) {
        if (!willValidate(e) || isValid(e)) continue;
        ok = false;
        if (dispatch(e, new Event('invalid', { cancelable: true }), true) && !first) first = e;
    }
    if (!ok && report && first && first.isConnected) N.focus(first);
    return ok;
}
const formFiring = new WeakSet();
const formNoValidate = (form, submitter) => N.attr(form, 'novalidate') != null || (submitter && N.attr(submitter, 'formnovalidate') != null);
const validationMembers = {
    get validity() { return validityOf(this); },
    get validationMessage() { return validationMessage(this); },
    get willValidate() { return willValidate(this); },
    checkValidity() { return checkOne(this); },
    reportValidity() { const ok = checkOne(this); return ok; },
    setCustomValidity(msg) {
        if (arguments.length < 1) throw new TypeError(`Failed to execute 'setCustomValidity' on '${this.constructor.name}': 1 argument required, but only 0 present.`);
        customMsg.set(this, String(msg));
    },
};
for (const C of ['HTMLInputElement', 'HTMLButtonElement', 'HTMLSelectElement', 'HTMLTextAreaElement', 'HTMLOutputElement', 'HTMLFieldSetElement', 'HTMLObjectElement']) {
    const p = H[C].prototype;
    methods(p, validationMembers);
    acc(p, 'form', function () { return formOwner(this); });
    if (C !== 'HTMLFieldSetElement' && C !== 'HTMLObjectElement') acc(p, 'labels', function () { return labelsOf(this); });
    for (const k of ['validity', 'validationMessage', 'willValidate']) { const d = Object.getOwnPropertyDescriptor(p, k); d.enumerable = true; Object.defineProperty(p, k, d); }
}
for (const C of ['HTMLMeterElement', 'HTMLProgressElement']) acc(H[C].prototype, 'labels', function () { return labelsOf(this); });

/* showPicker()'s checks after mutability: a cross-origin frame (except file and color pickers), user activation, and
   for a select being rendered; then the activation is consumed */
function pickerGate(iface, anyFrame, select) {
    const what = `Failed to execute 'showPicker' on '${iface}': `;
    if (!anyFrame && G.top !== G) { let same = false; try { same = G.top.location.origin === G.location.origin; } catch (e) {} if (!same) throw new DOMException(what + `${iface}::showPicker() called from cross-origin iframe.`, 'SecurityError'); }
    if (!transientActivation()) throw new DOMException(what + `${iface}::showPicker() requires a user gesture.`, 'NotAllowedError');
    if (select && (!select.isConnected || !select.getClientRects().length)) throw new DOMException(what + `${iface}::showPicker() requires the select is rendered.`, 'NotSupportedError');
    consumeActivation();
}
/* ---- the elements ---- */
methods(H.HTMLInputElement.prototype, {
    get value() { return inputValue(this); },
    showPicker() {
        const t = inType(this);
        if (ctlDisabled(this) || (READONLY_TYPES.test(t) && N.attr(this, 'readonly') != null))
            throw new DOMException("Failed to execute 'showPicker' on 'HTMLInputElement': HTMLInputElement::showPicker() cannot be used on immutable controls.", 'InvalidStateError');
        pickerGate('HTMLInputElement', t === 'file' || t === 'color');
        if (t === 'file') pickFiles(this);
    },
    get alpha() { return N.attr(this, 'alpha') != null; },
    set alpha(v) { if (v) this.setAttribute('alpha', ''); else this.removeAttribute('alpha'); },
    get colorSpace() { return colorSpaceOf(this); },
    set colorSpace(v) { this.setAttribute('colorspace', String(v)); },
    set value(v) {
        const t = inType(this);
        v = v == null ? '' : String(v);
        switch (valueMode(t)) {
        case 'value': { const before = this.value; valueSet(this, sanitizeAs(this, t, v)); valueChangedSel(this, before); break; }
        case 'file':
            if (v !== '') throw new DOMException("Failed to set the 'value' property on 'HTMLInputElement': This input element accepts a filename, which may only be programmatically set to the empty string.", 'InvalidStateError');
            def(this, '_files', new FileList());
            break;
        default: this.setAttribute('value', v);
        }
    },
    get valueAsNumber() { const t = inType(this), T = NUMERIC[t]; return T ? T.parse(this.value) : NaN; },
    set valueAsNumber(v) {
        v = Number(v);
        if (v === Infinity || v === -Infinity) throw new TypeError("Failed to set the 'valueAsNumber' property on 'HTMLInputElement': The value provided is infinite.");
        const T = NUMERIC[inType(this)];
        if (!T) throw new DOMException("Failed to set the 'valueAsNumber' property on 'HTMLInputElement': This input element does not support Number values.", 'InvalidStateError');
        this.value = Number.isNaN(v) ? '' : T.ser(v);
    },
    get valueAsDate() {
        const t = inType(this);
        if (!(t === 'date' || t === 'month' || t === 'week' || t === 'time')) return null;
        const n = NUMERIC[t].parse(this.value);
        if (Number.isNaN(n)) return null;
        return new Date(t === 'month' ? utc(1970 + Math.floor(n / 12), ((n % 12) + 12) % 12, 1) : n);
    },
    set valueAsDate(v) {
        const t = inType(this);
        if (!(t === 'date' || t === 'month' || t === 'week' || t === 'time')) throw new DOMException("Failed to set the 'valueAsDate' property on 'HTMLInputElement': This input element does not support Date values.", 'InvalidStateError');
        if (v !== null && !(v instanceof Date)) throw new TypeError("Failed to set the 'valueAsDate' property on 'HTMLInputElement': The provided value is not a Date.");
        const ms = v === null ? NaN : v.getTime();
        if (Number.isNaN(ms)) { this.value = ''; return; }
        if (t === 'month') { const d = new Date(ms); this.value = sMonth((d.getUTCFullYear() - 1970) * 12 + d.getUTCMonth()); }
        else if (t === 'time') this.value = sTime(((ms % 864e5) + 864e5) % 864e5);
        else this.value = NUMERIC[t].ser(ms);
    },
    stepUp(n = 1) { stepBy(this, toI32(n), 'stepUp'); },
    stepDown(n = 1) { stepBy(this, -toI32(n), 'stepDown'); },
    get list() { const id = N.attr(this, 'list'); const e = id ? rootById(this, id) : null; return isHtmlEl(e, 'datalist') ? e : null; },
});
function stepBy(el, n, op) {
    const t = inType(el), T = NUMERIC[t];
    if (!T) throw new DOMException(`Failed to execute '${op}' on 'HTMLInputElement': This form element does not have an allowed value step.`, 'InvalidStateError');
    const r = rangeOf(el, t);
    if (r.step === null) throw new DOMException(`Failed to execute '${op}' on 'HTMLInputElement': This form element does not have an allowed value step.`, 'InvalidStateError');
    if (!Number.isNaN(r.min) && !Number.isNaN(r.max) && r.min > r.max) return;
    if (!Number.isNaN(r.min)) {   /* no step-aligned value at or above the minimum within the range */
        const first = r.base + Math.ceil((r.min - r.base) / r.step - 1e-9) * r.step;
        if (!Number.isNaN(r.max) && first > r.max) return;
    }
    let v = T.parse(el.value);
    if (Number.isNaN(v)) v = 0;
    const orig = v;
    if (misaligned(v, r)) {
        const q = (v - r.base) / r.step;
        v = r.base + (n > 0 ? Math.ceil(q) + (n - 1) : Math.floor(q) + (n + 1)) * r.step;
    } else v += n * r.step;
    if (!Number.isNaN(r.min) && v < r.min) v = r.base + Math.ceil((r.min - r.base) / r.step - 1e-9) * r.step;
    if (!Number.isNaN(r.max) && v > r.max) v = r.base + Math.floor((r.max - r.base) / r.step + 1e-9) * r.step;
    if ((n > 0 && v < orig) || (n < 0 && v > orig)) return;
    el.value = T.ser(t === 'number' || t === 'range' ? +v.toPrecision(15) : v);
}
const checkedMembers = {
    get checked() { return N.checked(this); },
    set checked(v) { checkDirty.add(this); setCheckedness(this, !!v); },
};
methods(H.HTMLInputElement.prototype, checkedMembers);
methods(H.HTMLTextAreaElement.prototype, {
    get value() { const v = N.value(this); return (v != null ? v : N.text(this)).replace(/\r\n?/g, '\n'); },
    set value(v) { const before = this.value; valueSet(this, (v == null ? '' : String(v)).replace(/\r\n?/g, '\n')); valueChangedSel(this, before); },
    get textLength() { return this.value.length; },
});
methods(H.HTMLSelectElement.prototype, {
    showPicker() {
        if (ctlDisabled(this)) throw new DOMException("Failed to execute 'showPicker' on 'HTMLSelectElement': HTMLSelectElement::showPicker() cannot be used on immutable controls.", 'InvalidStateError');
        pickerGate('HTMLSelectElement', false, this);
    },
    get options() { return optionsOf(this); },
    get length() { return optionList(this).length; },
    set length(v) { optsSetLength(this, v); },
    get selectedOptions() { let c = selOptsCache.get(this); if (!c) { const s = this; c = liveWith(null, () => selectedOptions(s), HTMLCollection.prototype); selOptsCache.set(this, c); } return c; },
    get selectedIndex() { const s = selectedOptions(this); return s.length ? optionList(this).indexOf(s[0]) : -1; },
    set selectedIndex(i) { i = toI32(i); const opts = optionList(this); for (let k = 0; k < opts.length; k++) setSelected(opts[k], k === i); if (opts[i]) optDirty.add(opts[i]); },
    get value() { const s = selectedOptions(this); return s.length ? s[0].value : ''; },
    set value(v) { v = String(v); let done = false; for (const o of optionList(this)) { const hit = !done && o.value === v; setSelected(o, hit); if (hit) { done = true; optDirty.add(o); } } },
    item(i) { return optionList(this)[toU32(i)] || null; },
    namedItem(k) { k = String(k); return optionList(this).find(o => N.attr(o, 'id') === k || N.attr(o, 'name') === k) || null; },
    add(element, before = null) { return selectAdd(this, element, before, 'HTMLSelectElement'); },
    remove(...a) { if (!a.length) { Element.prototype.remove.call(this); return; } const o = optionList(this)[toI32(a[0])]; if (o) o.remove(); },
});
const selOptsCache = new WeakMap();
methods(H.HTMLOptionElement.prototype, {
    get text() { return optText(this); }, set text(v) { this.textContent = v; },
    get label() { const l = N.attr(this, 'label'); return l != null ? l : this.text; }, set label(v) { this.setAttribute('label', v); },
    get value() { const v = N.attr(this, 'value'); return v != null ? v : this.text; }, set value(v) { this.setAttribute('value', v); },
    get selected() { const s = selectOf(this); return s ? selectedOptions(s).includes(this) : N.checked(this); },
    set selected(v) {
        v = !!v;
        optDirty.add(this);
        const s = selectOf(this);
        if (v && s && N.attr(s, 'multiple') == null) selectOnly(s, this);
        else setSelected(this, v);
    },
    get index() { const s = selectOf(this); if (s) return optionList(s).indexOf(this); const p = N.parent(this); if (p && isHtmlEl(p, 'datalist')) return [...N.query(p, 'option', true)].indexOf(this); return 0; },
    get form() { const s = selectOf(this); return s ? formOwner(s) : null; },
});
methods(H.HTMLFormElement.prototype, {
    get elements() { return elementsOf(this); },
    get length() { return formElements(this).length; },
    checkValidity() { return formCheck(this, false); },
    reportValidity() { return formCheck(this, true); },
    requestSubmit(s = null) {
        if (s !== null) {
            const sub = isHtmlEl(s) && ((N.name(s) === 'button' && s.type === 'submit') || (N.name(s) === 'input' && (inType(s) === 'submit' || inType(s) === 'image')));
            if (!sub) throw new TypeError("Failed to execute 'requestSubmit' on 'HTMLFormElement': The specified element is not a submit button.");
            if (formOwner(s) !== this) throw new DOMException("Failed to execute 'requestSubmit' on 'HTMLFormElement': The specified element is not owned by this form element.", 'NotFoundError');
        }
        if (!this.isConnected || formFiring.has(this)) return;
        formFiring.add(this);   /* its invalid and submit events' listeners cannot submit it again meanwhile */
        let go;
        try { go = (formNoValidate(this, s) || formCheck(this, true)) && dispatch(this, new SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: s }), true); }
        finally { formFiring.delete(this); }
        if (go && this.isConnected) submitForm(this, s);
    },
    reset() {
        if (!dispatch(this, new Event('reset', { bubbles: true, cancelable: true }), true)) return;
        for (const e of formElements(this)) resetControl(e);
    },
});
function resetControl(e) {
    switch (N.name(e)) {
    case 'input': N.setValue(e, null); checkDirty.delete(e); N.setChecked(e, N.attr(e, 'checked') != null); if (e._files) def(e, '_files', null); selStore.delete(e); break;
    case 'textarea': N.setValue(e, null); selStore.delete(e); break;
    case 'select': for (const o of optionList(e)) { optDirty.delete(o); setSelected(o, N.attr(o, 'selected') != null); } break;
    case 'output': e.textContent = e.defaultValue; outputDefault.delete(e); break;
    }
}
const outputDefault = new WeakMap();
methods(H.HTMLOutputElement.prototype, {
    get value() { return this.textContent; },
    set value(v) { if (!outputDefault.has(this)) outputDefault.set(this, this.textContent); this.textContent = v; },
    get defaultValue() { return outputDefault.has(this) ? outputDefault.get(this) : this.textContent; },
    set defaultValue(v) { v = String(v); if (outputDefault.has(this)) outputDefault.set(this, v); else this.textContent = v; },
});
methods(H.HTMLFieldSetElement.prototype, {
    get type() { return 'fieldset'; },
    get elements() { let c = fsColl.get(this); if (!c) { const f = this; c = liveWith(null, () => N.query(f, 'button,fieldset,input,object,output,select,textarea', true).filter(e => N.ns(e) === 0), HTMLCollection.prototype); fsColl.set(this, c); } return c; },
});
reflectStr(H.HTMLFieldSetElement.prototype, 'name');
const fsColl = new WeakMap();
methods(H.HTMLLabelElement.prototype, {
    get control() { return labelControl(this); },
    get form() { const c = labelControl(this); return c ? formOwner(c) : null; },
});
methods(H.HTMLLegendElement.prototype, { get form() { const f = N.parent(this); return f && isHtmlEl(f, 'fieldset') ? formOwner(f) : null; } });
