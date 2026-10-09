function activate(el, ev) {
    const a = el.closest ? el.closest('a[href]') : null;
    if (a) { const raw = N.attr(a, 'href'); if (/^javascript:/i.test(raw)) { try { (0, eval)(decodeURIComponent(raw.slice(11))); } catch (e) { report(e); } } else if (a.href) location.assign(a.href); return; }
    if (el.closest && !/^(input|button|label|summary|textarea|select)$/.test(N.name(el))) {
        const t = el.closest('button,label,summary');
        if (t) el = t;
    }
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
    else {
        const et = ((submitter && submitter.getAttribute('formenctype')) || N.attr(form, 'enctype') || '').toLowerCase();
        const kv = [...fd].filter(([, v]) => typeof v === 'string');
        if (et === 'multipart/form-data') {
            const bd = '----LumenFormBoundary' + Math.random().toString(36).slice(2);
            const esc = s => s.replace(/"/g, '%22').replace(/\r?\n/g, '%0D%0A');
            const body = kv.map(([k, v]) => `--${bd}\r\nContent-Disposition: form-data; name="${esc(k)}"\r\n\r\n${v}\r\n`).join('') + `--${bd}--\r\n`;
            N.navigatePost(u.href, body, 'multipart/form-data; boundary=' + bd);
        } else if (et === 'text/plain') N.navigatePost(u.href, kv.map(([k, v]) => `${k}=${v}\r\n`).join(''), 'text/plain');
        else N.navigatePost(u.href, new URLSearchParams(kv).toString(), 'application/x-www-form-urlencoded');
    }
}
function cssSplit(s) {
    const out = []; const n = s.length; let i = 0, pre = '';
    const skipStr = (j, q) => { j++; while (j < n && s[j] !== q) { if (s[j] === '\\') j++; j++; } return j; };
    while (i < n) {
        const c = s[i];
        if (c === '/' && s[i + 1] === '*') { const e = s.indexOf('*/', i + 2); i = e < 0 ? n : e + 2; pre += ' '; continue; }
        if (c === '\\') { pre += s.slice(i, i + 2); i += 2; continue; }
        if (c === '"' || c === "'") { const j = skipStr(i, c); pre += s.slice(i, j + 1); i = j + 1; continue; }
        if (c === '{') {
            let d = 1, j = i + 1;
            while (j < n && d) { const k = s[j]; if (k === '\\') j++; else if (k === '"' || k === "'") j = skipStr(j, k); else if (k === '/' && s[j + 1] === '*') { const e = s.indexOf('*/', j + 2); j = e < 0 ? n : e + 1; } else if (k === '{') d++; else if (k === '}') d--; j++; }
            out.push({ pre: pre.trim(), body: s.slice(i + 1, d ? j : j - 1) }); pre = ''; i = j; continue;
        }
        if (c === ';' && pre.trim()[0] === '@') { out.push({ pre: pre.trim(), body: null }); pre = ''; i++; continue; }
        if (c === '}') { pre = ''; i++; continue; }
        pre += c; i++;
    }
    if (pre.trim()[0] === '@') out.push({ pre: pre.trim(), body: null });
    return out;
}
class CSSRuleList extends Array { static get [Symbol.species]() { return Array; } item(i) { return this[i] ?? null; } }
class MediaList {
    constructor(t, o) { def(this, '_m', []); def(this, '_o', o); this._set(t); }
    _set(t) { this._m = String(t || '').split(',').map(x => x.trim().replace(/\s+/g, ' ')).filter(Boolean); }
    get mediaText() { return this._m.join(', '); } set mediaText(v) { this._set(v); this._o && this._o._changed(); }
    get length() { return this._m.length; } item(i) { return this._m[i] ?? null; } toString() { return this.mediaText; }
    appendMedium(m) { m = String(m).trim(); if (!this._m.includes(m)) { this._m.push(m); this._o && this._o._changed(); } }
    deleteMedium(m) { const i = this._m.indexOf(String(m).trim()); if (i < 0) throw new DOMException('Failed to delete the medium.', 'NotFoundError'); this._m.splice(i, 1); this._o && this._o._changed(); }
}
class CSSRuleStyle extends CSSStyleDeclaration {
    constructor(rule, text) { super(null); def(this, '_rule', rule); def(this, '_map', parseDecls(text || '')); }
    _m() { return new Map(this._map); }
    _w(m) { this._map = m; this._rule._changed(); }
    get cssText() { return serDecls(this._map); } set cssText(v) { this._w(parseDecls(String(v))); }
    setProperty(p, v, pri) { p = String(p); if (!p.startsWith('--')) p = camelToKebab(p); const m = this._m(); if (v == null || v === '') m.delete(p); else m.set(p, [String(v), pri ? 'important' : '']); this._w(m); }
    removeProperty(p) { p = camelToKebab(String(p)); const m = this._m(); const e = m.get(p); if (e) { m.delete(p); this._w(m); } return e ? e[0] : ''; }
}
class CSSRule {
    constructor() { illegal(); }
    get parentRule() { return this._parent || null; } get parentStyleSheet() { return this._sheet || null; }
    get cssText() { return ''; } set cssText(v) {}
    _changed() { for (let r = this; r; r = r._parent) r._text = null; if (this._sheet) this._sheet._sync(); }
}
for (const [k, v] of Object.entries({ STYLE_RULE: 1, CHARSET_RULE: 2, IMPORT_RULE: 3, MEDIA_RULE: 4, FONT_FACE_RULE: 5, PAGE_RULE: 6, KEYFRAMES_RULE: 7, KEYFRAME_RULE: 8, MARGIN_RULE: 9, NAMESPACE_RULE: 10, COUNTER_STYLE_RULE: 11, SUPPORTS_RULE: 12, FONT_FEATURE_VALUES_RULE: 14 })) { Object.defineProperty(CSSRule, k, { value: v, enumerable: true }); Object.defineProperty(CSSRule.prototype, k, { value: v, enumerable: true }); }
const ruleStyle = (r) => r._style || (r._style = new Proxy(new CSSRuleStyle(r, r._body), styleHandler));
const blockText = (r) => { const d = r._style ? r._style.cssText : serDecls(parseDecls(r._body || '')); return d ? `{ ${d} }` : '{ }'; };
class CSSStyleRule extends CSSRule {
    get type() { return 1; }
    get selectorText() { return this._sel; }
    set selectorText(v) { const s = N.cssSelText(String(v)); if (s != null) { this._sel = s; this._changed(); } }
    get style() { return ruleStyle(this); } set style(v) { this.style.cssText = v; }
    get cssText() { return this._text || (this._text = `${this._sel} ${blockText(this)}`); }
}
class CSSGroupingRule extends CSSRule {
    get cssRules() { return this._list; }
    insertRule(rule, i = 0) { const r = cssInsert(this._list, rule, i, this._sheet, this); this._changed(); return r; }
    deleteRule(i) { cssDelete(this._list, i); this._changed(); }
    _inner() { return this._list.length ? `{\n${this._list.map(r => '  ' + r.cssText).join('\n')}\n}` : '{\n}'; }
}
class CSSConditionRule extends CSSGroupingRule { get conditionText() { return this._cond; } }
class CSSMediaRule extends CSSConditionRule {
    get type() { return 4; } get media() { return this._media; } set media(v) { this._media.mediaText = v; }
    get conditionText() { return this._media.mediaText; }
    get cssText() { return `@media ${this._media.mediaText} ${this._inner()}`; }
}
class CSSSupportsRule extends CSSConditionRule { get type() { return 12; } get cssText() { return `@supports ${this._cond} ${this._inner()}`; } }
class CSSContainerRule extends CSSConditionRule { get containerName() { return ''; } get containerQuery() { return this._cond; } get cssText() { return `@container ${this._cond} ${this._inner()}`; } }
class CSSLayerBlockRule extends CSSGroupingRule { get name() { return this._name; } get cssText() { return `@layer ${this._name ? this._name + ' ' : ''}${this._inner()}`; } }
class CSSLayerStatementRule extends CSSRule { get nameList() { return Object.freeze(this._names.slice()); } get cssText() { return `@layer ${this._names.join(', ')};`; } }
class CSSImportRule extends CSSRule {
    get type() { return 3; } get href() { return this._href; } get media() { return this._media; } get styleSheet() { return null; } get layerName() { return null; } get supportsText() { return null; }
    get cssText() { const m = this._media.mediaText; return `@import url("${this._href}")${m ? ' ' + m : ''};`; }
}
class CSSNamespaceRule extends CSSRule { get type() { return 10; } get namespaceURI() { return this._ns; } get prefix() { return this._prefix; } get cssText() { return `@namespace ${this._prefix ? this._prefix + ' ' : ''}url("${this._ns}");`; } }
class CSSFontFaceRule extends CSSRule { get type() { return 5; } get style() { return ruleStyle(this); } get cssText() { return `@font-face ${blockText(this)}`; } }
class CSSPageRule extends CSSRule { get type() { return 6; } get selectorText() { return this._sel; } set selectorText(v) { this._sel = String(v); this._changed(); } get style() { return ruleStyle(this); } get cssText() { return `@page ${this._sel ? this._sel + ' ' : ''}${blockText(this)}`; } }
class CSSKeyframeRule extends CSSRule { get type() { return 8; } get keyText() { return this._key; } set keyText(v) { this._key = String(v); this._changed(); } get style() { return ruleStyle(this); } get cssText() { return `${this._key} ${blockText(this)}`; } }
class CSSKeyframesRule extends CSSRule {
    get type() { return 7; } get name() { return this._name; } set name(v) { this._name = String(v); this._changed(); }
    get cssRules() { return this._list; } get length() { return this._list.length; }
    appendRule(t) { const it = cssSplit(String(t)); const r = it.length === 1 && cssMkKeyframe(it[0], this._sheet, this); if (r) { this._list.push(r); this._changed(); } }
    deleteRule(k) { const i = this._list.findIndex(r => r._key === String(k).trim()); if (i >= 0) { this._list.splice(i, 1); this._changed(); } }
    findRule(k) { return this._list.find(r => r._key === String(k).trim()) || null; }
    get cssText() { return `@keyframes ${this._name} ${CSSGroupingRule.prototype._inner.call(this)}`; }
}
function cssMk(C, props) { const r = Object.create(C.prototype); for (const k in props) def(r, k, props[k]); def(r, '_text', null); return r; }
function cssMkKeyframe(it, sheet, parent) {
    if (it.body == null || !it.pre) return null;
    const key = it.pre.split(',').map(k => { k = k.trim().toLowerCase(); return k === 'from' ? '0%' : k === 'to' ? '100%' : k; });
    if (!key.every(k => /^[+-]?(\d+\.?\d*|\.\d+)%$/.test(k))) return null;
    return cssMk(CSSKeyframeRule, { _key: key.join(', '), _body: it.body, _style: null, _sheet: sheet, _parent: parent });
}
function cssMkRule(it, sheet, parent) {
    const p = it.pre, base = { _sheet: sheet, _parent: parent };
    if (p[0] !== '@') {
        if (it.body == null) return null;
        const st = N.cssSelText(p); if (st == null) return null;
        return cssMk(CSSStyleRule, { ...base, _sel: st, _body: it.body, _style: null });
    }
    const m = /^@([-\w]+)\s*([\s\S]*)$/.exec(p); if (!m) return null;
    const kw = m[1].toLowerCase(), rest = m[2].trim().replace(/\s+/g, ' '), blk = it.body != null;
    let r = null;
    if (kw === 'media' && blk) { r = cssMk(CSSMediaRule, { ...base, _media: null }); r._media = new MediaList(rest, r); }
    else if (kw === 'supports' && blk) r = cssMk(CSSSupportsRule, { ...base, _cond: rest });
    else if (kw === 'container' && blk) r = cssMk(CSSContainerRule, { ...base, _cond: rest });
    else if (kw === 'layer') { if (blk) r = cssMk(CSSLayerBlockRule, { ...base, _name: rest }); else return cssMk(CSSLayerStatementRule, { ...base, _names: rest.split(',').map(x => x.trim()).filter(Boolean) }); }
    else if (kw === 'import' && !blk) {
        const u = /^(?:url\(\s*(?:"([^"]*)"|'([^']*)'|([^)]*?))\s*\)|"([^"]*)"|'([^']*)')\s*([\s\S]*)$/.exec(rest); if (!u) return null;
        r = cssMk(CSSImportRule, { ...base, _href: u[1] ?? u[2] ?? u[3] ?? u[4] ?? u[5], _media: null }); r._media = new MediaList(u[6], r); return r;
    }
    else if (kw === 'namespace' && !blk) { const q = /^(?:([-\w]+)\s+)?(?:url\(\s*["']?([^"')]*)["']?\s*\)|"([^"]*)"|'([^']*)')$/.exec(rest); if (!q) return null; return cssMk(CSSNamespaceRule, { ...base, _prefix: q[1] || '', _ns: q[2] ?? q[3] ?? q[4] }); }
    else if (kw === 'font-face' && blk) return cssMk(CSSFontFaceRule, { ...base, _body: it.body, _style: null });
    else if (kw === 'page' && blk) return cssMk(CSSPageRule, { ...base, _sel: rest, _body: it.body, _style: null });
    else if ((kw === 'keyframes' || kw === '-webkit-keyframes') && blk && rest) { r = cssMk(CSSKeyframesRule, { ...base, _name: rest, _list: null }); const l = new CSSRuleList(); for (const k of cssSplit(it.body)) { const kr = cssMkKeyframe(k, sheet, r); if (kr) l.push(kr); } r._list = l; return r; }
    else return null;
    def(r, '_list', cssParseList(it.body, sheet, r));
    return r;
}
function cssParseList(src, sheet, parent) { const l = new CSSRuleList(); for (const it of cssSplit(src)) { const r = cssMkRule(it, sheet, parent); if (r) l.push(r); } return l; }
function cssInsert(list, rule, index, sheet, parent) {
    index = toU32(index);
    if (index > list.length) throw new DOMException(`Failed to execute 'insertRule' on 'CSSStyleSheet': The index provided (${index}) is larger than the maximum index (${list.length}).`, 'IndexSizeError');
    const items = cssSplit(String(rule)), r = items.length === 1 ? cssMkRule(items[0], sheet, parent) : null;
    if (!r) throw new DOMException(`Failed to execute 'insertRule' on 'CSSStyleSheet': Failed to parse the rule '${rule}'.`, 'SyntaxError');
    list.splice(index, 0, r); return index;
}
function cssDelete(list, i) {
    i = toU32(i);
    if (i >= list.length) throw new DOMException(`Failed to execute 'deleteRule' on 'CSSStyleSheet': The index provided (${i}) is larger than the maximum index (${list.length - 1}).`, 'IndexSizeError');
    list.splice(i, 1);
}
class StyleSheet {}
class CSSStyleSheet extends StyleSheet {
    constructor(opts) { super(); def(this, '_owner', null); def(this, '_src', null); def(this, '_list', new CSSRuleList()); def(this, '_media', new MediaList(opts && opts.media != null ? String(opts.media) : '', null)); this.disabled = false; }
    get ownerNode() { return this._owner; } get type() { return 'text/css'; } get parentStyleSheet() { return null; } get ownerRule() { return null; }
    get href() { const o = this._owner; return o && N.name(o) === 'link' ? o.href : null; }
    get title() { const o = this._owner; return (o && N.attr(o, 'title')) || null; }
    get media() { return this._media; }
    _load() { const o = this._owner; if (o && N.name(o) === 'style') { const t = N.text(o); if (t !== this._src) { this._src = t; this._list = cssParseList(t, this, null); } } }
    get cssRules() { this._load(); return this._list; } get rules() { return this.cssRules; }
    insertRule(rule, i = 0) { this._load(); const r = cssInsert(this._list, rule, i, this, null); this._sync(); return r; }
    deleteRule(i) { this._load(); cssDelete(this._list, i); this._sync(); }
    addRule(sel = 'undefined', style = 'undefined', i) { this._load(); this.insertRule(`${sel} { ${style} }`, i === undefined ? this._list.length : i); return -1; }
    removeRule(i = 0) { this.deleteRule(i); }
    replace(t) { this.replaceSync(t); return Promise.resolve(this); }
    replaceSync(t) { this._list = cssParseList(String(t), this, null); this._sync(); }
    _sync() {
        let o = this._owner;
        if (!o) { o = N.create('style', 0); def(this, '_owner', o); const h = document.head || document.documentElement; if (h) N.insert(h, o, null); }
        if (N.name(o) === 'style') { const t = this._list.map(r => r.cssText).join('\n'); this._src = t; N.setText(o, t); }
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
