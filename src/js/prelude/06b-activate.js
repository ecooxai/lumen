function activate(el, ev) {
    const a = el.closest ? el.closest('a[href]') : null;
    if (a) { const raw = N.attr(a, 'href'); if (/^javascript:/i.test(raw)) { try { (0, eval)(decodeURIComponent(raw.slice(11))); } catch (e) { report(e); } } else if (a.href) location.assign(a.href); return; }
    const name = N.name(el);
    if (name === 'input' || name === 'button') {
        const t = el.type;
        if (t === 'checkbox' || (t === 'radio' && !el.checked)) {
            if (t === 'radio') for (const r of N.query(el.form || document, `input[type=radio][name="${CSS.escape(el.name)}"]`, true)) N.setChecked(r, false);
            el.checked = t === 'radio' ? true : !el.checked;
            dispatch(el, new Event('input', { bubbles: true })); dispatch(el, new Event('change', { bubbles: true }));
        } else if (t === 'submit' || t === 'image') { const f = el.form; if (f) f.requestSubmit(el); }
        else if (t === 'reset') { const f = el.form; if (f) f.reset(); }
        else if (name === 'input') N.focus(el);
    } else if (name === 'label') { const c = el.control; if (c && c !== el) c.click(); }
    else if (name === 'summary') { const d = N.parent(el); if (d && N.name(d) === 'details') d.open = !d.open; }
    else if (name === 'textarea' || name === 'select') N.focus(el);
}
function submitForm(form, submitter) {
    const fd = new FormData(form);
    if (submitter && submitter.name) fd.append(submitter.name, submitter.value);
    const u = new URL((submitter && submitter.getAttribute('formaction')) || N.attr(form, 'action') || N.url(), document.baseURI);
    if (form.method === 'get') { u.search = new URLSearchParams([...fd].filter(([, v]) => typeof v === 'string')).toString(); N.navigate(u.href); }
    else N.log(2, 'POST form submission is not supported yet: ' + u.href);
}
class CSSRule { constructor(text) { this.cssText = text; } }
class CSSStyleSheet {
    constructor() { def(this, '_owner', null); def(this, '_rules', []); this.disabled = false; this.media = { mediaText: '' }; }
    get ownerNode() { return this._owner; } get cssRules() { return this._rules; } get rules() { return this._rules; }
    insertRule(rule, i = 0) { this._rules.splice(i, 0, new CSSRule(String(rule))); this._sync(); return i; }
    deleteRule(i) { this._rules.splice(i, 1); this._sync(); }
    replace(t) { this.replaceSync(t); return Promise.resolve(this); }
    replaceSync(t) { def(this, '_rules', [new CSSRule(String(t))]); this._sync(); }
    _sync() {
        let o = this._owner;
        if (!o) { o = N.create('style', 0); def(this, '_owner', o); const h = document.head || document.documentElement; if (h) N.insert(h, o, null); }
        if (N.name(o) === 'style') N.setText(o, this._rules.map(r => r.cssText).join('\n'));
    }
}
function sheetFor(el) { if (!el.__sheet) { const s = new CSSStyleSheet(); def(s, '_owner', el); def(el, '__sheet', s); } return el.__sheet; }
const CSS = {
    escape(s) { s = String(s); let r = ''; for (let i = 0; i < s.length; i++) { const c = s.charCodeAt(i); if (c === 0) r += '\uFFFD'; else if ((c >= 1 && c <= 31) || c === 127 || (i === 0 && c >= 48 && c <= 57) || (i === 1 && c >= 48 && c <= 57 && s.charCodeAt(0) === 45)) r += '\\' + c.toString(16) + ' '; else if (i === 0 && c === 45 && s.length === 1) r += '\\-'; else if (c >= 128 || c === 45 || c === 95 || (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122)) r += s[i]; else r += '\\' + s[i]; } return r; },
    supports(p, v) { return N.cssSupports(v === undefined ? String(p) : `(${p}: ${v})`); },
    registerProperty() {},
};
const fontSet = Object.assign(new EventTarget(), { status: 'loaded', size: 0, load() { return Promise.resolve([]); }, check() { return true; }, add() {}, delete() {}, clear() {}, forEach() {}, has() { return false; } });
Object.defineProperty(fontSet, 'ready', { get() { return Promise.resolve(fontSet); } });
class FontFace { constructor(family, source, d) { this.family = family; this.source = source; Object.assign(this, d); this.status = 'loaded'; this.loaded = Promise.resolve(this); } load() { return Promise.resolve(this); } }
