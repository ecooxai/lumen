/* legacy-pre-activation: checkbox/radio state flips before click listeners run, reverted if cancelled */
/* a disabled checkbox or radio still toggles for a script's click event; the user's clicks don't reach it */
function preActivate(el, ev) {
    if (!el || el.nodeType !== 1 || N.name(el) !== 'input' || (el.disabled && ev && ev.isTrusted)) return null;
    const t = el.type;
    if (t !== 'checkbox' && t !== 'radio') return null;
    const was = el.checked;
    if (t === 'radio' && was) return { noop: true };
    const prev = t === 'radio' ? [...N.query(el.form || document, `input[type=radio][name="${CSS.escape(el.name)}"]`, true)].filter(r => r.checked) : [];
    for (const r of prev) N.setChecked(r, false);
    N.setChecked(el, t === 'radio' ? true : !was);   /* native: page-installed `checked` setters (React's value tracker) must not see it */
    return { restore() { N.setChecked(el, was); for (const r of prev) N.setChecked(r, true); } };
}
const MIME = { png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp', svg: 'image/svg+xml', heic: 'image/heic', bmp: 'image/bmp', pdf: 'application/pdf', txt: 'text/plain', md: 'text/markdown', csv: 'text/csv', json: 'application/json', html: 'text/html', js: 'text/javascript', py: 'text/x-python', mp3: 'audio/mpeg', wav: 'audio/wav', ogg: 'audio/ogg', mp4: 'video/mp4', webm: 'video/webm', zip: 'application/zip', doc: 'application/msword', docx: 'application/vnd.openxmlformats-officedocument.wordprocessingml.document', xlsx: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet', pptx: 'application/vnd.openxmlformats-officedocument.presentationml.presentation' };
function pickFiles(el) {
    N.pickFiles(list => {
        if (!list) { dispatch(el, new Event('cancel', { bubbles: true })); return; }
        const files = list.map(o => new File([o.data], o.name, { type: MIME[(o.name.split('.').pop() || '').toLowerCase()] || '' }));
        def(el, '_files', new FileList(el.multiple ? files : files.slice(0, 1)));
        dispatch(el, new Event('input', { bubbles: true, composed: true })); dispatch(el, new Event('change', { bubbles: true }));
    }, !!el.multiple, el.accept || '');
}
function buttonCommand(el) {
    const target = el.commandForElement, command = el.command;
    if (!target || !command) return;
    const custom = command.startsWith('--');
    if (!custom && (!(target instanceof H.HTMLDialogElement) || !['show-modal', 'close', 'request-close'].includes(command))) return;
    if (!dispatch(target, new CommandEvent('command', { command, source: el, cancelable: true }), true) || !target.isConnected || custom) return;
    if (command === 'show-modal') { if (!target.open) dialogShow(target, true, el); }
    else if (command === 'close') dialogClose(target, el.getAttribute('value') ?? undefined, el);
    else dialogRequestClose(target, el.getAttribute('value') ?? undefined, el);
}
// <a download>, blob: and data: links become downloads; http(s) ones go to the download manager with cookies
function saveHref(href, name) {
    if (/^https?:/i.test(href)) { N.download(href, name != null && new URL(href).origin === location.origin ? String(name) : ''); return; }
    const body = localBody(href);
    if (!body) return;
    N.downloadBytes(name ? String(name) : 'download', String(body[1] || '').split(';')[0], body[0]);
}
Object.defineProperty(globalThis, '__lumenSaveHref', { value: saveHref });
const ACT_INPUT = new Set(['checkbox', 'radio', 'submit', 'image', 'reset', 'file', 'button', 'color']);
const INTERACTIVE = new Set(['a', 'audio', 'button', 'details', 'embed', 'iframe', 'input', 'label', 'select', 'textarea', 'video']);
/* elements with an activation behavior: links, buttons, labels, summaries and the clickable input types */
function hasActivation(n) {
    if (!N.isNode(n) || N.type(n) !== 1 || N.ns(n) !== 0) return false;
    const t = N.name(n);
    if (t === 'a' || t === 'area') return N.attr(n, 'href') != null;
    if (t === 'input') return ACT_INPUT.has(n.type);
    if (t === 'summary') { const d = N.parent(n); return !!d && N.name(d) === 'details' && N.ns(d) === 0; }
    return t === 'button' || t === 'label';
}
function activate(el, ev, pre) {
    const a = (N.name(el) === 'a' || N.name(el) === 'area') && N.attr(el, 'href') != null ? el : null;
    if (a) {
        const raw = N.attr(a, 'href');
        if (/^javascript:/i.test(raw)) runJsURL(raw);
        else if (a.href && (a.hasAttribute('download') || /^(blob|data):/i.test(a.href))) saveHref(a.href, a.getAttribute('download'));
        else if (a.href && !N.navTarget(linkTarget(a), a.href)) location.assign(a.href);
        return;
    }
    const name = N.name(el);
    if (name === 'input' || name === 'button') {
        const t = el.type;
        if (el.matches(':disabled') && (name !== 'input' || (t !== 'checkbox' && t !== 'radio') || ev.isTrusted)) return;
        if (name === 'button') {
            const f = el.form;
            if (f) {
                if (t === 'submit') { f.requestSubmit(el); return; }
                if (t === 'reset') { f.reset(); return; }
                if ((el.getAttribute('type') || '').toLowerCase() !== 'button') return;
            }
            buttonCommand(el); return;
        }
        if (pre) { if (!pre.noop) { dispatch(el, new Event('input', { bubbles: true })); dispatch(el, new Event('change', { bubbles: true })); } }
        else if (t === 'checkbox' || (t === 'radio' && !el.checked)) {
            if (t === 'radio') for (const r of N.query(el.form || document, `input[type=radio][name="${CSS.escape(el.name)}"]`, true)) N.setChecked(r, false);
            el.checked = t === 'radio' ? true : !el.checked;
            dispatch(el, new Event('input', { bubbles: true })); dispatch(el, new Event('change', { bubbles: true }));
        } else if (t === 'file' && name === 'input') pickFiles(el);
        else if (t === 'submit' || t === 'image') { const f = el.form; if (f) f.requestSubmit(el); }
        else if (t === 'reset') { const f = el.form; if (f) f.reset(); }
        else if (name === 'input') N.focus(el);
    } else if (name === 'label') {
        /* a click on the control itself or on interactive content inside the label is that element's alone */
        for (let n = ev && ev.target; n && n !== el; n = N.parent(n)) if (N.type(n) === 1 && INTERACTIVE.has(N.name(n))) return;
        const c = el.control;
        if (!c || c === el) return;
        if (hasActivation(c)) c.click(); else N.focus(c);
    }
    else if (name === 'summary') { const d = N.parent(el); if (d && N.name(d) === 'details') d.open = !d.open; }
    else if (name === 'textarea' || name === 'select') N.focus(el);
}
/* the browsing context a link or form navigates: its target, else the document's <base target> */
function linkTarget(el, submitter) {
    const t = (submitter && submitter.getAttribute('formtarget')) ?? el.getAttribute('target');
    if (t !== null) return t;
    const b = document.querySelector('base[target]');
    return b ? b.getAttribute('target') : '';
}
function submitForm(form, submitter) {
    /* method=dialog closes the form's dialog with the submitter's value instead of navigating */
    if (((submitter && submitter.getAttribute('formmethod')) || N.attr(form, 'method') || '').toLowerCase() === 'dialog') {
        const d = form.closest('dialog');
        if (d && d.open) dialogClose(d, submitter ? submitter.value : undefined, submitter);
        return;
    }
    const fd = new FormData(form, submitter && submitter.type !== 'image' && submitter.form === form ? submitter : null);
    if (submitter && submitter.name && (submitter.type === 'image' || submitter.form !== form)) fd.append(submitter.name, submitter.value);
    /* the form's encoding: the first accept-charset label that names one, else its document's */
    let cs = null;
    for (const l of (N.attr(form, 'accept-charset') || '').split(/[\t\n\f\r ]+/)) if (l && (cs = N.encLabel(l))) break;
    cs = cs || N.charset(form);
    const action = N.urlParse((submitter && submitter.getAttribute('formaction')) || N.attr(form, 'action') || N.url(), document.baseURI, docEnc(form));
    if (!action) return;
    const u = new URL(action[0]);
    const urlencoded = kv => N.formEncode(kv.flat(), cs);
    const target = linkTarget(form, submitter);
    const navigatePost = (url, body, ctype) => { if (!N.navTarget(target, url, body, ctype)) N.navigatePost(url, body, ctype); };
    /* names and string values get their newlines normalized to CRLF; urlencoded and text/plain send a file's name */
    const crlf = v => v.replace(/\r\n|\r|\n/g, '\r\n');
    const pairs = () => [...fd].map(([k, v]) => [crlf(k), crlf(typeof v === 'string' ? v : v.name)]);
    if (form.method === 'get') { u.search = urlencoded(pairs()); if (!N.navTarget(target, u.href)) N.navigate(u.href); }
    else {
        const et = ((submitter && submitter.getAttribute('formenctype')) || N.attr(form, 'enctype') || '').toLowerCase();
        if (et === 'multipart/form-data') {
            const bd = '----LumenFormBoundary' + Math.random().toString(36).slice(2);
            const esc = s => s.replace(/\n/g, '%0A').replace(/\r/g, '%0D').replace(/"/g, '%22');
            /* text parts in the form's encoding (unmappable code points as &#N;), file contents as their bytes */
            const enc = t => new Uint8Array(N.encodeHtml(t, cs)), parts = [];
            for (const [k, v] of fd) {
                const head = `--${bd}\r\nContent-Disposition: form-data; name="${esc(crlf(k))}"`;
                if (typeof v === 'string') parts.push(enc(`${head}\r\n\r\n${crlf(v)}\r\n`));
                else parts.push(enc(`${head}; filename="${esc(v.name)}"\r\nContent-Type: ${v.type || 'application/octet-stream'}\r\n\r\n`), new Uint8Array(v._buf), enc('\r\n'));
            }
            parts.push(enc(`--${bd}--\r\n`));
            const body = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
            parts.reduce((o, p) => (body.set(p, o), o + p.length), 0);
            navigatePost(u.href, body, 'multipart/form-data; boundary=' + bd);
        } else if (et === 'text/plain') navigatePost(u.href, N.encodeHtml(pairs().map(([k, v]) => `${k}=${v}\r\n`).join(''), cs), 'text/plain');
        else navigatePost(u.href, urlencoded(pairs()), 'application/x-www-form-urlencoded');
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
    insertRule(rule, i = 0) {
        this._load(); const r = cssInsert(this._list, rule, i, this, null), o = this._owner;
        /* appending to a <style>: extend its text so the engine parses only the new rule */
        if (o && r === this._list.length - 1 && N.name(o) === 'style' && /^\s*$|\}\s*$/.test(this._src || '')) { const t = (this._src || '') + '\n' + this._list[r].cssText; this._src = t; N.setText(o, t); }
        else this._sync();
        return r;
    }
    deleteRule(i) { this._load(); cssDelete(this._list, i); this._sync(); }
    addRule(sel = 'undefined', style = 'undefined', i) { this._load(); this.insertRule(`${sel} { ${style} }`, i === undefined ? this._list.length : i); return -1; }
    removeRule(i = 0) { this.deleteRule(i); }
    replace(t) { this.replaceSync(t); return Promise.resolve(this); }
    replaceSync(t) { this._list = cssParseList(String(t), this, null); this._sync(); }
    _text() { return this._list.map(r => r.cssText).join('\n'); }
    _sync() {   /* constructed sheets apply only through adoptedStyleSheets: mirror their text into each adopting scope */
        const o = this._owner;
        if (!o) { if (this._mirrors) { const t = this._text(); for (const n of this._mirrors.values()) N.setText(n, t); } return; }
        if (N.name(o) === 'style') { const t = this._text(); this._src = t; N.setText(o, t); }
    }
}
function adoptSheets(scope, v) {
    const list = Array.from(v || []).filter(s => s instanceof CSSStyleSheet && !s._owner), old = scope.__ass || [];
    for (const s of old) if (!list.includes(s) && s._mirrors) { const n = s._mirrors.get(scope); if (n) { if (n.parentNode) N.remove(n); s._mirrors.delete(scope); } }
    def(scope, '__ass', list);
    const root = scope.nodeType === 9 ? (scope.head || scope.documentElement) : scope;
    for (const s of list) {
        if (!s._mirrors) def(s, '_mirrors', new Map());
        let n = s._mirrors.get(scope);
        if (n) { if (n.parentNode) N.remove(n); } else { n = N.create('style', 0); def(n, '__mirror', true); s._mirrors.set(scope, n); N.setText(n, s._text()); }
        if (root) N.insert(root, n, null);
    }
}
function readopt(scope) { if (scope.__ass.some(s => { const n = s._mirrors && s._mirrors.get(scope); return !n || !n.parentNode; })) adoptSheets(scope, scope.__ass); }
function adoptedGet(scope) { return new Proxy((scope.__ass || []).slice(), { set(t, k, val) { t[k] = val; adoptSheets(scope, t); return true; } }); }
function scopeSheets(scope) { return N.query(scope, 'style,link[rel~=stylesheet]', true).filter(el => !el.__mirror).map(sheetFor); }
function sheetFor(el) { if (!el.__sheet) { const s = new CSSStyleSheet(); def(s, '_owner', el); def(el, '__sheet', s); } return el.__sheet; }
/* CSS.registerProperty: validates the definition (name, syntax, a computationally independent initial value) */
const registeredProps = new Map();
const RP_TYPES = new Set(['length', 'number', 'percentage', 'length-percentage', 'color', 'image', 'url', 'integer', 'angle', 'time', 'resolution', 'transform-function', 'custom-ident', 'transform-list', 'string']);
const RP_NUM = '[+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:e[+-]?\\d+)?';
const RP_MATH = /^(calc|min|max|clamp|abs|sign|round|mod|rem|hypot)\(.*\)$/i;
const rpRe = (u) => new RegExp('^' + RP_NUM + u + '$', 'i');
const RP_CHECK = {
    length: v => v === '0' || rpRe('(px|cm|mm|in|pt|pc|q)').test(v) || RP_MATH.test(v) && !/\d(em|ex|ch|rem|vw|vh|vmin|vmax|lh|%)|var\(/i.test(v.replace(/sign\([^()]*\)/gi, '0')),
    number: v => rpRe('').test(v), integer: v => /^[+-]?\d+$/.test(v), percentage: v => rpRe('%').test(v),
    'length-percentage': v => RP_CHECK.length(v) || RP_CHECK.percentage(v) || RP_MATH.test(v) && !/\d(em|ex|ch|rem|vw|vh|vmin|vmax|lh)|var\(/i.test(v),
    color: v => !/^currentcolor$/i.test(v) && (!!cssColor(v) || /^(color-mix|contrast-color|light-dark|[a-z]+)\(.*\)$/i.test(v) && /\(\s*\S/.test(v)), angle: v => v === '0' || rpRe('(deg|rad|grad|turn)').test(v), time: v => rpRe('(s|ms)').test(v),
    resolution: v => rpRe('(dpi|dpcm|dppx|x)').test(v), url: v => /^url\(.*\)$/i.test(v), image: v => /^(url|(repeating-)?(linear|radial|conic)-gradient)\(.*\)$/i.test(v),
    'transform-function': v => /^[a-z0-9-]+\(.*\)$/i.test(v) && !/\d(em|ex|ch|rem|vw|vh|lh)\b/i.test(v), 'transform-list': v => v.split(/\)\s*/).filter(Boolean).every(f => RP_CHECK['transform-function'](f + ')')),
    'custom-ident': v => /^-?[_a-z][\w-]*$/i.test(v) && !/^(inherit|initial|unset|revert|revert-layer|default)$/i.test(v), string: v => /^(["']).*\1$/.test(v),
};
function rpParseSyntax(syn) {
    const s = syn.trim();
    if (s === '*') return '*';
    if (!s) return null;
    const out = [];
    for (const part of s.split('|')) {
        const m = /^\s*(?:<([a-z-]+)>|([-_a-zA-Z][\w-]*))([+#]?)\s*$/.exec(part);
        if (!m) return null;
        if (m[1] && !RP_TYPES.has(m[1])) return null;
        if (m[2] && /^(inherit|initial|unset|revert|revert-layer|default)$/i.test(m[2])) return null;
        if (m[1] === 'transform-list' && m[3]) return null;
        out.push({ type: m[1], ident: m[2], mult: m[3] });
    }
    return out;
}
function rpMatches(comps, v) {
    v = v.replace(/\/\*[\s\S]*?\*\//g, ' ').trim();
    if (/var\(|env\(|attr\(/i.test(v)) return false;
    return comps.some(c => {
        const one = x => c.ident ? x === c.ident : RP_CHECK[c.type](x);
        if (!c.mult) return one(v);
        const items = (c.mult === '#' ? tomSplitTop(v, ',') : tomSplitTop(v, ' ')).map(x => x.trim()).filter(x => x || c.mult === '#');
        return items.length > 0 && items.every(x => x && one(x));
    });
}
function registerProperty(d) {
    if (d === null || typeof d !== 'object' && typeof d !== 'function') throw new TypeError("Failed to execute 'registerProperty' on 'CSS': parameter 1 is not of type 'PropertyDefinition'.");
    if (d.name === undefined) throw new TypeError("Failed to execute 'registerProperty' on 'CSS': required member name is undefined.");
    if (d.inherits === undefined) throw new TypeError("Failed to execute 'registerProperty' on 'CSS': required member inherits is undefined.");
    const name = String(d.name), syntax = d.syntax === undefined ? '*' : String(d.syntax), iv = d.initialValue === undefined ? undefined : String(d.initialValue);
    if (!/^--/.test(name)) throw new DOMException(`Custom property names must start with '--'.`, 'SyntaxError');
    const comps = rpParseSyntax(syntax);
    if (!comps) throw new DOMException(`The given syntax '${syntax}' is invalid.`, 'SyntaxError');
    if (comps !== '*' && iv === undefined) throw new DOMException('An initial value must be provided if the syntax is not "*"', 'SyntaxError');
    if (iv !== undefined && (comps === '*' ? /var\(/i.test(iv) : !rpMatches(comps, iv))) throw new DOMException('The initial value provided does not parse for the given syntax.', 'SyntaxError');
    if (registeredProps.has(name)) throw new DOMException('The name provided has already been registered.', 'InvalidModificationError');
    registeredProps.set(name, { syntax, inherits: !!d.inherits, initialValue: iv });
    if (iv !== undefined) N.registerProp(name, iv.trim());
}
const CSS = {
    escape(s) { s = String(s); let r = ''; for (let i = 0; i < s.length; i++) { const c = s.charCodeAt(i); if (c === 0) r += '\uFFFD'; else if ((c >= 1 && c <= 31) || c === 127 || (i === 0 && c >= 48 && c <= 57) || (i === 1 && c >= 48 && c <= 57 && s.charCodeAt(0) === 45)) r += '\\' + c.toString(16) + ' '; else if (i === 0 && c === 45 && s.length === 1) r += '\\-'; else if (c >= 128 || c === 45 || c === 95 || (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122)) r += s[i]; else r += '\\' + s[i]; } return r; },
    supports(p, v) { return N.cssSupports(v === undefined ? String(p) : `(${p}: ${v})`); },
    registerProperty(d) { registerProperty(d); },
};
/* set-like over the faces a page adds: pages spread it (`[...document.fonts]`) and read .size */
const fontFaces = new Set();
const fontSet = Object.assign(new EventTarget(), { status: 'loaded', load() { return Promise.resolve([]); }, check() { return true; },
    add(f) { fontFaces.add(f); return this; }, delete(f) { return fontFaces.delete(f); }, clear() { fontFaces.clear(); },
    forEach(cb, t) { for (const f of fontFaces) cb.call(t, f, f, this); }, has(f) { return fontFaces.has(f); },
    values() { return fontFaces.values(); }, keys() { return fontFaces.values(); }, entries() { return fontFaces.entries(); },
    [Symbol.iterator]() { return fontFaces.values(); } });
Object.defineProperty(fontSet, 'size', { get() { return fontFaces.size; } });
Object.defineProperty(fontSet, 'ready', { get() { return Promise.resolve(fontSet); } });
/*@shared fontface.js*/
