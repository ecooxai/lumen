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
/* a button's commandfor behavior; false if it has no commandfor target (the popovertarget behavior runs then) */
function buttonCommand(el) {
    const target = el.commandForElement, command = el.command;
    if (!target) return false;
    if (!command) return true;
    const custom = command.startsWith('--'), pop = POP_CMDS.has(command);
    if (!custom && !(pop ? popState(target) : target instanceof H.HTMLDialogElement && ['show-modal', 'close', 'request-close'].includes(command))) return true;
    if (pop && (command === 'show-popover' ? N.popover(target) : command === 'hide-popover' ? !N.popover(target) : false)) return true;
    if (!dispatch(target, new CommandEvent('command', { command, source: el, cancelable: true }), true) || !target.isConnected || custom) return true;
    if (pop) {
        if (N.popover(target)) { if (command !== 'show-popover') popHide(target, true, true, false, el); }
        else if (command !== 'hide-popover' && popValid(target, false, false, null)) popShow(target, false, el);
    } else if (command === 'show-modal') { if (!target.open) dialogShow(target, true, el); }
    else if (command === 'close') dialogClose(target, el.getAttribute('value') ?? undefined, el);
    else dialogRequestClose(target, el.getAttribute('value') ?? undefined, el);
    return true;
}
// <a download>, blob: and data: links become downloads; http(s) ones go to the download manager with cookies
function saveHref(href, name) {
    if (/^https?:/i.test(href)) { N.download(href, name != null && new URL(href).origin === location.origin ? String(name) : ''); return; }
    const body = localBody(href);
    if (!body) return;
    N.downloadBytes(name ? String(name) : 'download', String(body[1] || '').split(';')[0], body[0]);
}
Object.defineProperty(globalThis, '__lumenSaveHref', { value: saveHref });
const ACT_INPUT = new SSet(['checkbox', 'radio', 'submit', 'image', 'reset', 'file', 'button', 'color']);
const INTERACTIVE = new SSet(['a', 'audio', 'button', 'details', 'embed', 'iframe', 'input', 'label', 'select', 'textarea', 'video']);
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
        /* an SVG <a>'s href is an SVGAnimatedString: resolve the attribute */
        let href = a.href;
        if (typeof href !== 'string') { try { href = new URL(raw, a.baseURI).href; } catch (e) { href = ''; } }
        if (/^javascript:/i.test(raw)) runJsURL(raw);
        else if (href && (a.hasAttribute('download') || /^(blob|data):/i.test(href))) saveHref(href, a.getAttribute('download'));
        else if (href && !N.navTarget(linkTarget(a), href)) location.assign(href);
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
                if ((el.getAttribute('type') || '').toLowerCase() !== 'button') { popTargetActivate(el, ev); return; }
            }
            if (!buttonCommand(el)) popTargetActivate(el, ev);
            return;
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
        if (name === 'input' && (t === 'button' || ((t === 'reset' || t === 'submit' || t === 'image') && !el.form))) popTargetActivate(el, ev);
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
/* a click in a list box (multiple or size > 1) picks the option in that row; vertical writing modes lay the
   rows out along x, right to left for *-rl */
function listboxClick(el, ev) {
    const size = el.size || (el.multiple ? 4 : 1);
    if (size <= 1 || typeof ev.clientX !== 'number') return;
    const r = el.getBoundingClientRect(), cs = getComputedStyle(el), px = (k) => parseFloat(cs[k]) || 0;
    const l = r.left + px('borderLeftWidth') + px('paddingLeft'), t = r.top + px('borderTopWidth') + px('paddingTop');
    const w = r.width - (l - r.left) - px('borderRightWidth') - px('paddingRight'), h = r.height - (t - r.top) - px('borderBottomWidth') - px('paddingBottom');
    const wm = cs.writingMode || '', vert = /^(vertical|sideways)/.test(wm);
    let f = vert ? (ev.clientX - l) / w : (ev.clientY - t) / h;
    if (!(f >= 0 && f < 1)) return;
    if (vert && wm.endsWith('-rl')) f = 1 - f;
    const opts = el.options, i = Math.floor(f * size) + Math.floor(el.scrollTop / ((vert ? w : h) / size) || 0);
    const o = opts[i];
    if (!o || o.disabled) return;
    const before = Array.prototype.map.call(opts, (x) => x.selected).join();
    if (el.multiple && (ev.ctrlKey || ev.metaKey)) o.selected = !o.selected;
    else { for (const x of opts) x.selected = x === o; }
    if (Array.prototype.map.call(opts, (x) => x.selected).join() !== before) { dispatch(el, new Event('input', { bubbles: true, composed: true })); dispatch(el, new Event('change', { bubbles: true })); }
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
function cssSplit(s, rawPrelude = false) {
    const out = []; const n = s.length; let i = 0, pre = '', start = 0;
    const skipStr = (j, q) => { j++; while (j < n && s[j] !== q) { if (s[j] === '\\') j++; j++; } return j; };
    while (i < n) {
        const c = s[i];
        if (c === '/' && s[i + 1] === '*') { const e = s.indexOf('*/', i + 2); i = e < 0 ? n : e + 2; pre += ' '; continue; }
        if (c === '\\') { pre += s.slice(i, i + 2); i += 2; continue; }
        if (c === '"' || c === "'") { const j = skipStr(i, c); pre += s.slice(i, j + 1); i = j + 1; continue; }
        if (c === '{') {
            let d = 1, j = i + 1;
            while (j < n && d) { const k = s[j]; if (k === '\\') j++; else if (k === '"' || k === "'") j = skipStr(j, k); else if (k === '/' && s[j + 1] === '*') { const e = s.indexOf('*/', j + 2); j = e < 0 ? n : e + 1; } else if (k === '{') d++; else if (k === '}') d--; j++; }
            /* Page selectors forbid whitespace between components. Retain their
               original prelude: comment removal and whitespace folding would
               otherwise change escaped identifiers or invent separators. */
            const text = pre.trim(), pagePre = /^@page(?:\s|:|$)/i.test(text) ? s.slice(start, i) : null;
            const item = { pre: text, pagePre, body: s.slice(i + 1, d ? j : j - 1) };
            /* Keyframe selectors need original token boundaries; appendRule
             * also needs to reject anything outside its single parsed block. */
            if (rawPrelude) { item.rawPre = s.slice(start, i); item.start = start; item.end = Math.min(j, n); }
            out.push(item); pre = ''; i = j; start = j; continue;
        }
        if (c === ';' && pre.trim()[0] === '@') { out.push({ pre: pre.trim(), body: null }); pre = ''; i++; start = i; continue; }
        if (c === '}') { pre = ''; i++; start = i; continue; }
        pre += c; i++;
    }
    if (pre.trim()[0] === '@') out.push({ pre: pre.trim(), body: null });
    return out;
}
/* Only ownership lives here; rule contents and list objects are retained. */
const cssOwners = new WeakMap();
const cssOwnerGet = Function.prototype.call.bind(WeakMap.prototype.get, cssOwners);
const cssOwnerSet = Function.prototype.call.bind(WeakMap.prototype.set, cssOwners);
const cssListDefine = Object.defineProperty;
const cssListOwn = Object.getOwnPropertyDescriptor;
function cssRuleSheet(rule) {
    let d = cssOwnerGet(rule);
    while (d.parent) d = cssOwnerGet(d.parent);
    return d.owner ? d.owner.sheet : null;
}
function cssDetachRule(rule) {
    const d = cssOwnerGet(rule);
    d.parent = null; d.owner = null;
}
function cssReplaceRules(sheet, next) {
    const d = cssOwnerGet(sheet), owner = { sheet }, list = d.list;
    for (let i = 0; i < next.length; i++) cssOwnerGet(next[i]).owner = owner;
    /* Keep the sheet's live list; never walk old public rules or lists. */
    cssListDefine(list, 'length', { value: 0 });
    for (let i = 0; i < next.length; i++)
        cssListDefine(list, i, { value: next[i], writable: true, enumerable: true, configurable: true });
    d.owner.sheet = null;
    d.owner = owner;
}
function cssReplaceSheet(sheet, text) {
    const next = cssParseList(String(text), sheet, null);
    cssReplaceRules(sheet, next);
    sheet._sync();
}
class CSSRuleList extends Array { static get [Symbol.species]() { return Array; } item(i) { return this[i] ?? null; } }
/* MediaList stores parsed query serializations privately. Splitting and
 * normalization operate on CSS component values, so a comma in a function or
 * string cannot become a list separator. Native matching stays unchanged.
 * https://drafts.csswg.org/cssom/#the-medialist-interface */
const cssMediaStates = new WeakMap();
const cssMediaGet = Function.prototype.call.bind(WeakMap.prototype.get, cssMediaStates);
const cssMediaSet = Function.prototype.call.bind(WeakMap.prototype.set, cssMediaStates);
var cssMediaOwnerStates = new WeakMap();
const cssMediaOwnerGet = Function.prototype.call.bind(WeakMap.prototype.get, cssMediaOwnerStates);
const cssMediaOwnerSet = Function.prototype.call.bind(WeakMap.prototype.set, cssMediaOwnerStates);
const cssMediaReflect = {get: Reflect.get, set: Reflect.set, has: Reflect.has,
    ownKeys: Reflect.ownKeys, descriptor: Reflect.getOwnPropertyDescriptor,
    define: Reflect.defineProperty, remove: Reflect.deleteProperty};
const cssMediaStringValue = String, cssMediaNumber = Number, cssMediaFinite = Number.isFinite;
const cssMediaArrayJoin = Function.prototype.call.bind(Array.prototype.join);
const cssMediaArraySlice = Function.prototype.call.bind(Array.prototype.slice);
const cssMediaArraySome = Function.prototype.call.bind(Array.prototype.some);
const cssMediaArrayMap = Function.prototype.call.bind(Array.prototype.map);
const cssMediaArrayConcat = Function.prototype.call.bind(Array.prototype.concat);
const cssMediaArrayIncludes = Function.prototype.call.bind(Array.prototype.includes);
const cssMediaArrayFilter = Function.prototype.call.bind(Array.prototype.filter);
let cssMediaRuleChanged;
function cssMediaString(value) {
    if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol to a CSSOMString.');
    return cssMediaStringValue(value);
}
const cssMediaLower = value => value.replace(/[A-Z]/g, c => c.toLowerCase());
function cssMediaEscape(value) {
    let result = '';
    for (let i = 0; i < value.length; i++) {
        const c = value[i], code = value.charCodeAt(i);
        if (!code) result += '\ufffd';
        else if (code < 32 || code === 127 || i === 0 && code >= 48 && code <= 57 ||
                 i === 1 && code >= 48 && code <= 57 && value[0] === '-')
            result += '\\' + code.toString(16) + ' ';
        else if (i === 0 && c === '-' && value.length === 1) result += '\\-';
        else result += code >= 128 || /[-_a-z0-9]/i.test(c) ? c : '\\' + c;
    }
    return result;
}
function cssMediaTokens(input) {
    const text = input.replace(/\r\n?|\f/g, '\n').replace(/\0|[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/g, '\ufffd');
    let i = 0;
    const start = c => c !== undefined && (/[_a-z]/i.test(c) || c.charCodeAt(0) >= 128);
    const escape = at => text[at] === '\\' && text[at + 1] !== '\n';
    const identAt = at => start(text[at]) || escape(at) ||
        text[at] === '-' && (text[at + 1] === '-' || start(text[at + 1]) || escape(at + 1));
    const ident = () => {
        const first = i;
        while (i < text.length) {
            if (start(text[i]) || /[-0-9]/.test(text[i])) { i++; continue; }
            if (!escape(i)) break;
            i++; const hex = i;
            while (i < text.length && i - hex < 6 && /[0-9a-f]/i.test(text[i])) i++;
            if (i !== hex) { if (/[ \t\n]/.test(text[i] || 'x')) i++; }
            else if (i < text.length) i++;
        }
        return N.cssPropertyIdentifier(text.slice(first, i));
    };
    const result = [], frames = [{body: result, close: '', token: null}];
    const append = token => { const frame = frames[frames.length - 1]; if (frame.body) apush(frame.body, token); };
    const open = (token, first, close) => {
        const frame = frames[frames.length - 1];
        /* Keep arbitrary input nesting off the JavaScript call stack. The
         * semantic parser conservatively rejects depth beyond this bound. */
        if (!frame.body || frames.length >= 128) {
            append({kind: 'bad'}); apush(frames, {body: null, close, token: null}); return;
        }
        token.body = []; append(token); apush(frames, {body: token.body, close, token, first});
    };
    while (i < text.length) {
        const c = text[i], frame = frames[frames.length - 1];
        if (/[ \t\n]/.test(c)) { i++; continue; }
        if (c === '/' && text[i + 1] === '*') {
            const end = text.indexOf('*/', i + 2); i = end < 0 ? text.length : end + 2; continue;
        }
        if (c === frame.close) {
            i++; if (frame.token) frame.token.raw = text.slice(frame.first, i);
            frames.length--; continue;
        }
        if (c === ')' || c === ']' || c === '}') { append({kind: 'bad'}); i++; continue; }
        if (c === '"' || c === "'") {
            const first = i++; let bad = false;
            while (i < text.length && text[i] !== c) {
                if (text[i] === '\n') { bad = true; break; }
                if (text[i] === '\\' && i + 1 < text.length) i++;
                i++;
            }
            if (text[i] === c) i++;
            append(bad ? {kind: 'bad'} : {kind: 'string', text: text.slice(first, i) + (text[i - 1] === c ? '' : c)});
            continue;
        }
        if (c === '(' || c === '[' || c === '{') {
            const first = i++; open({kind: 'block', open: c}, first, c === '(' ? ')' : c === '[' ? ']' : '}'); continue;
        }
        const number = /^[+-]?(?:\d*\.\d+|\d+)(?:[eE][+-]?\d+)?/.exec(text.slice(i));
        if (number) {
            i += number[0].length; let unit = '';
            if (identAt(i)) unit = ident();
            else if (text[i] === '%') { unit = '%'; i++; }
            append(unit === null ? {kind: 'bad'} : {kind: 'number', text: number[0], unit}); continue;
        }
        if (identAt(i)) {
            const first = i, value = ident();
            if (value === null) { append({kind: 'bad'}); continue; }
            if (text[i] === '(') { i++; open({kind: 'function', value}, first, ')'); }
            else append({kind: 'ident', value});
            continue;
        }
        let value = c; i++;
        if ((c === '<' || c === '>') && text[i] === '=') { value += '='; i++; }
        append({kind: 'delim', value});
    }
    return result; // CSS Syntax closes component-value blocks at EOF.
}
const cssMediaKnownValue = /^(?:(?:min|max)-)?(?:width|height|device-width|device-height|aspect-ratio|device-aspect-ratio|resolution|color|color-index|monochrome|grid|orientation|scan|hover|any-hover|pointer|any-pointer|scripting|update|color-gamut|dynamic-range|video-dynamic-range|display-mode|display-state|overflow-block|overflow-inline|prefers-color-scheme|prefers-reduced-motion|prefers-reduced-data|prefers-reduced-transparency|prefers-contrast|forced-colors|inverted-colors|environment-blending|resizable|navigation-controls)$/;
function cssMediaComponentText(token, lower = false) {
    if (token.kind === 'ident') return cssMediaEscape(lower ? cssMediaLower(token.value) : token.value);
    if (token.kind === 'number') {
        const number = cssMediaNumber(token.text);
        const value = lower && cssMediaFinite(number) ? cssMediaStringValue(number) : token.text;
        return value + (token.unit === '%' ? '%' : cssMediaEscape(lower ? cssMediaLower(token.unit) : token.unit));
    }
    if (token.kind === 'string') return token.text;
    if (token.kind === 'delim') return token.value;
    if (token.kind === 'function' && !lower && token.raw !== undefined) return token.raw;
    if (token.kind === 'function') return cssMediaEscape(lower ? cssMediaLower(token.value) : token.value) + '(' + cssMediaValueText(token.body, lower) + ')';
    if (token.kind === 'block') return token.open + cssMediaValueText(token.body, lower) + (token.open === '(' ? ')' : token.open === '[' ? ']' : '}');
    return '';
}
function cssMediaValueText(tokens, lower = false) {
    let result = '', previous = '';
    for (const token of tokens) {
        const text = cssMediaComponentText(token, lower), punct = token.kind === 'delim' ? token.value : '';
        if (result && punct !== ',' && punct !== ':' && previous !== '') result += ' ';
        result += text; previous = text;
    }
    return result;
}
function cssMediaInParens(token) {
    if (token.kind === 'function') return cssMediaComponentText(token);
    if (token.kind !== 'block' || token.open !== '(' || !token.body.length) return null;
    const body = token.body, condition = cssMediaCondition(body, true);
    if (condition !== null) return '(' + condition + ')';
    if (cssMediaArraySome(body, t => t.kind === 'delim' && /^(?:[<>]=?|=)$/.test(t.value)) &&
        cssMediaArraySome(body, t => t.kind === 'ident' && cssMediaKnownValue.test(cssMediaLower(t.value))))
        return '(' + cssMediaValueText(body, true) + ')';
    if (body[0].kind === 'ident') {
        const name = cssMediaLower(body[0].value), first = cssMediaEscape(name);
        if (body.length === 1) return cssMediaKnownValue.test(name) ? '(' + first + ')' : (token.raw ?? '(' + first + ')');
        if (body[1].kind === 'delim' && body[1].value === ':') {
            if (body.length === 2 || !cssMediaKnownValue.test(name)) return token.raw ?? '(' + cssMediaValueText(body) + ')';
            return '(' + first + ': ' + cssMediaValueText(cssMediaArraySlice(body, 2), true) + ')';
        }
        return token.raw ?? '(' + cssMediaValueText(body) + ')'; // general-enclosed: preserve unknown values.
    }
    return null;
}
function cssMediaCondition(tokens, allowOr) {
    if (!tokens.length) return null;
    if (tokens[0].kind === 'ident' && cssMediaLower(tokens[0].value) === 'not') {
        if (tokens.length !== 2) return null;
        const value = cssMediaInParens(tokens[1]); return value === null ? null : 'not ' + value;
    }
    const first = cssMediaInParens(tokens[0]); if (first === null) return null;
    let result = first, operator = '';
    for (let i = 1; i < tokens.length; i += 2) {
        if (i + 1 >= tokens.length || tokens[i].kind !== 'ident') return null;
        const op = cssMediaLower(tokens[i].value);
        if (op !== 'and' && (op !== 'or' || !allowOr) || operator && op !== operator) return null;
        const next = cssMediaInParens(tokens[i + 1]); if (next === null) return null;
        operator = op; result += ' ' + op + ' ' + next;
    }
    return result;
}
function cssMediaQuery(tokens) {
    const invalid = list => cssMediaArraySome(list, t => t.kind === 'bad' || t.body && invalid(t.body));
    if (!tokens.length || invalid(tokens)) return 'not all';
    const condition = cssMediaCondition(tokens, true);
    if (condition !== null) return N.mediaValid(condition) ? condition : 'not all';
    let i = 0, prefix = '';
    if (tokens[i].kind === 'ident' && /^(not|only)$/i.test(tokens[i].value)) prefix = cssMediaLower(tokens[i++].value);
    if (i >= tokens.length || tokens[i].kind !== 'ident' || /^(not|only|and|or)$/i.test(tokens[i].value)) return 'not all';
    const type = cssMediaLower(tokens[i++].value);
    if (i === tokens.length) return (prefix ? prefix + ' ' : '') + cssMediaEscape(type);
    if (tokens[i].kind !== 'ident' || cssMediaLower(tokens[i++].value) !== 'and') return 'not all';
    const tail = cssMediaCondition(cssMediaArraySlice(tokens, i), false); if (tail === null) return 'not all';
    const result = type === 'all' && !prefix ? tail : (prefix ? prefix + ' ' : '') + cssMediaEscape(type) + ' and ' + tail;
    return N.mediaValid(result) ? result : 'not all';
}
function cssMediaParse(text) {
    const tokens = cssMediaTokens(text); if (!tokens.length) return [];
    const queries = []; let start = 0;
    for (let i = 0; i <= tokens.length; i++) {
        if (i === tokens.length || tokens[i].kind === 'delim' && tokens[i].value === ',') {
            apush(queries, cssMediaQuery(cssMediaArraySlice(tokens, start, i))); start = i + 1;
        }
    }
    return queries;
}
function cssMediaData(object) {
    const data = cssMediaGet(object);
    if (!data) throw new TypeError('Illegal invocation');
    return data;
}
function cssMediaChanged(data) {
    if (data.owner) cssMediaRuleChanged(data.owner);
}
function cssMediaAttributeChanged(element) {
    if (!cssMediaOwnerStates) return;
    const data = cssMediaOwnerGet(element);
    if (data) data.queries = cssMediaParse(N.attrNS(element, null, 'media') || '');
}
const cssMediaIndex = key => typeof key === 'string' && /^(?:0|[1-9][0-9]*)$/.test(key) && +key < 4294967295;
const cssMediaHandler = {
    get(target, key, receiver) {
        if (cssMediaIndex(key)) {
            const values = cssMediaData(target).queries;
            if (+key < values.length) return values[+key];
        }
        return cssMediaReflect.get(target, key, receiver);
    },
    has(target, key) { return cssMediaIndex(key) && +key < cssMediaData(target).queries.length || cssMediaReflect.has(target, key); },
    ownKeys(target) { return cssMediaArrayConcat(cssMediaArrayMap(cssMediaData(target).queries, (_, i) => '' + i), cssMediaReflect.ownKeys(target)); },
    getOwnPropertyDescriptor(target, key) {
        const values = cssMediaData(target).queries;
        if (cssMediaIndex(key) && +key < values.length) return {value: values[+key], writable: false, enumerable: true, configurable: true};
        return cssMediaReflect.descriptor(target, key);
    },
    set(target, key, value, receiver) { return cssMediaIndex(key) ? false : cssMediaReflect.set(target, key, value, receiver); },
    defineProperty(target, key, descriptor) { return cssMediaIndex(key) ? false : cssMediaReflect.define(target, key, descriptor); },
    deleteProperty(target, key) { return cssMediaIndex(key) && +key < cssMediaData(target).queries.length ? false : cssMediaReflect.remove(target, key); },
    preventExtensions() { return false; }
};
function cssCreateMediaList(text, owner = null, element = null) {
    const target = Object.create(MediaList.prototype), data = {queries: cssMediaParse(text), owner};
    const proxy = new Proxy(target, cssMediaHandler);
    cssMediaSet(target, data); cssMediaSet(proxy, data);
    if (element) cssMediaOwnerSet(element, data);
    return proxy;
}
class MediaList {
    constructor() { throw new TypeError('Illegal constructor'); }
    get mediaText() { return cssMediaArrayJoin(cssMediaData(this).queries, ', '); }
    set mediaText(value) {
        const data = cssMediaData(this), text = value === null ? '' : cssMediaString(value);
        data.queries = cssMediaParse(text); cssMediaChanged(data);
    }
    get length() { return cssMediaData(this).queries.length; }
    item(index) {
        const data = cssMediaData(this);
        if (!arguments.length) throw new TypeError('item requires an index.');
        index = (+index) >>> 0; return index < data.queries.length ? data.queries[index] : null;
    }
    toString() { return cssMediaArrayJoin(cssMediaData(this).queries, ', '); }
    appendMedium(value) {
        const data = cssMediaData(this);
        if (!arguments.length) throw new TypeError('appendMedium requires a medium.');
        const parsed = cssMediaParse(cssMediaString(value));
        if (parsed.length !== 1 || cssMediaArrayIncludes(data.queries, parsed[0])) return;
        apush(data.queries, parsed[0]); cssMediaChanged(data);
    }
    deleteMedium(value) {
        const data = cssMediaData(this);
        if (!arguments.length) throw new TypeError('deleteMedium requires a medium.');
        const parsed = cssMediaParse(cssMediaString(value));
        if (parsed.length !== 1) return;
        const next = cssMediaArrayFilter(data.queries, query => query !== parsed[0]);
        if (next.length === data.queries.length) throw new DOMException('Failed to delete the medium.', 'NotFoundError');
        data.queries = next; cssMediaChanged(data);
    }
}
Object.defineProperty(MediaList.prototype, Symbol.toStringTag, {value: 'MediaList', configurable: true});
Object.defineProperty(MediaList.prototype, Symbol.iterator, {value: Array.prototype.values, writable: true, configurable: true});
/* A keyframe block has its own declaration context. Animation controls do
 * not nest animations, while timing/composition describe the keyframe itself.
 * Priority must be rejected before duplicate declarations are resolved. */
function cssKeyframePropertyAllowed(property) {
    const name = property.toLowerCase().replace(/^-webkit-/, '');
    return !/^animation(?:-|$)/.test(name) ||
        name === 'animation-timing-function' || name === 'animation-composition';
}
function cssKeyframeImportant(value) {
    const tokens = cssMediaTokens(value), count = tokens.length;
    return count >= 2 && tokens[count - 2].kind === 'delim' &&
        tokens[count - 2].value === '!' && tokens[count - 1].kind === 'ident' &&
        tokens[count - 1].value.toLowerCase() === 'important';
}
function cssRuleDeclarations(rule, text) {
    return parseDecls(text, rule instanceof CSSFontFaceRule, cssOwnerGet(rule).type === 8);
}
class CSSRuleStyle extends CSSStyleDeclaration {
    constructor(rule, text) { super(null); if (cssOwnerGet(rule).type === 5) cssDescriptorStyleAdd(this); def(this, '_rule', rule); def(this, '_map', cssRuleDeclarations(rule, text || '')); }
    _m() { return new SMap(this._map); }
    _w(m) { this._map = m; this._rule._changed(); }
    get cssText() { return serDecls(this._map); } set cssText(v) { this._w(cssRuleDeclarations(this._rule, String(v))); }
    setProperty(p, v, pri) { p = String(p); if (!p.startsWith('--')) p = cssStylePropertyName(this, p);
        v = v == null ? '' : String(v);
        if (cssOwnerGet(this._rule).type === 8) {
            if (!cssKeyframePropertyAllowed(p)) return;
            if (v !== '') {
                pri = pri == null ? '' : String(pri);
                if (pri && !/^important$/i.test(pri) || cssKeyframeImportant(v)) return;
            }
        }
        if (v !== '' && !cssVarSyntax(v)) return;
        if (p === 'z-index' && v !== '') {
            pri = pri == null ? '' : String(pri);
            if (pri && !/^important$/i.test(pri) || !mathValid(p, v)) return;
        }
        const m = this._m();
        if (p === 'interpolate-size') {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri);
            if (!cssInterpolateSizeSet(m, value, priority)) return;
            this._w(m); return;
        }
        if (CSS_EASING_LONGHAND.test(p)) {
            const priority = pri == null ? '' : String(pri);
            if (!cssEasingSet(m, p, v, priority)) return;
            this._w(m); return;
        }
        if (CSS_IMAGE_LONGHAND.test(p)) {
            const priority = pri == null ? '' : String(pri);
            if (!cssImageLonghandSet(m, p, v, priority)) return;
            this._w(m); return;
        }
        if (p === 'overflow') {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri);
            if (!cssOverflowSet(m, value, priority)) return;
            this._w(m); return;
        }
        if (this._rule instanceof CSSFontFaceRule && p === 'unicode-range') {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri);
            if (value === '') m.delete(p);
            else {
                if (priority && !/^important$/i.test(priority)) return;
                const range = cssUnicodeRangeText(value); if (range === null) return;
                m.delete(p); m.set(p, [range, priority ? 'important' : '']);
            }
            this._w(m); return;
        }
        if (cssBorderRelevant(p)) {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri);
            if (value === '') cssBorderRemove(m, p);
            else if (priority && !/^important$/i.test(priority) || !cssBorderSet(m, p, value, priority ? 'important' : '')) return;
        } else if (p === 'box-shadow') {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri);
            if (value === '') m.delete(p);
            else {
                if (priority && !/^important$/i.test(priority)) return;
                const parsed = cssBoxShadowParse(value); if (parsed === null) return;
                m.delete(p); m.set(p, [parsed, priority ? 'important' : '']);
            }
        } else if (p === 'transition-property') {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri);
            if (value === '') m.delete(p);
            else {
                if (priority && !/^important$/i.test(priority)) return;
                const parsed = N.cssTransitionProperty(value); if (parsed === null) return;
                m.delete(p); m.set(p, [parsed, priority ? 'important' : '']);
            }
        } else if (CSS_NUMERIC_DIMENSION.test(p)) {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri);
            if (value === '') m.delete(p);
            else {
                if (priority && !/^important$/i.test(priority)) return;
                const parsed = cssNumericDimensionParse(p, value); if (parsed === null) return;
                m.delete(p); m.set(p, [parsed, priority ? 'important' : '']);
            }
        } else if (cssGridRelevant(p)) {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri);
            if (value === '') cssGridRemove(m, p);
            else if (priority && !/^important$/i.test(priority) || !cssGridSet(m, p, value, priority ? 'important' : '')) return;
        } else if (v == null || v === '') m.delete(p);
        else if (!p.startsWith('--') && CSS_MATH_VALUE.test(String(v))) {
            const priority = pri == null ? '' : String(pri);
            if (priority && !/^important$/i.test(priority)) return;
            const value = cssMathValueText(String(v));
            if (value === null || !mathValid(p, value)) return;
            m.set(p, [specNormalize(p, value), priority ? 'important' : '']);
        } else m.set(p, [/^(?:fill-|stroke-|stop-|flood-)?opacity$/.test(p) ? cssScalarSpecified(p, String(v)) : String(v), pri ? 'important' : '']); this._w(m);
    }
    removeProperty(p) { p = cssStylePropertyName(this, String(p)); const m = this._m(); if (cssBorderRelevant(p)) { const size = m.size, value = cssBorderRemove(m, p); if (size !== m.size) this._w(m); return value; } if (cssGridRelevant(p)) { const size = m.size, value = cssGridRemove(m, p); if (size !== m.size) this._w(m); return value; } const e = m.get(p); if (e) { m.delete(p); this._w(m); } return e ? e[0] : ''; }
}
class CSSRule {
    constructor() { illegal(); }
    get parentRule() { return cssOwnerGet(this).parent; } get parentStyleSheet() { return cssRuleSheet(this); }
    get type() { const data = cssOwnerGet(this); if (!data || data.type === undefined) throw new TypeError('Illegal invocation'); return data.type; }
    get cssText() { return ''; } set cssText(v) {}
    _changed() { for (let r = this; r; r = cssOwnerGet(r).parent) r._text = null; const s = cssRuleSheet(this); if (s) s._sync(); }
}
for (const [k, v] of Object.entries({ STYLE_RULE: 1, CHARSET_RULE: 2, IMPORT_RULE: 3, MEDIA_RULE: 4, FONT_FACE_RULE: 5, PAGE_RULE: 6, KEYFRAMES_RULE: 7, KEYFRAME_RULE: 8, MARGIN_RULE: 9, NAMESPACE_RULE: 10, COUNTER_STYLE_RULE: 11, SUPPORTS_RULE: 12, FONT_FEATURE_VALUES_RULE: 14 })) { Object.defineProperty(CSSRule, k, { value: v, enumerable: true }); Object.defineProperty(CSSRule.prototype, k, { value: v, enumerable: true }); }
const ruleStyle = r => {
    if (r._style) return r._style;
    const target = new CSSRuleStyle(r, r._body), style = new Proxy(target, styleHandler);
    if (cssDescriptorStyleHas(target)) cssDescriptorStyleAdd(style);
    return r._style = style;
};
const blockText = (r) => { const d = r._style ? r._style.cssText : serDecls(cssRuleDeclarations(r, r._body || '')); return d ? `{ ${d} }` : '{ }'; };
cssMediaRuleChanged = Function.prototype.call.bind(CSSRule.prototype._changed);
class CSSStyleRule extends CSSRule {
    get type() { return 1; }
    get selectorText() { return this._sel; }
    set selectorText(v) { const s = N.cssSelText(String(v)); if (s != null) { this._sel = s; this._changed(); } }
    get style() { return ruleStyle(this); } set style(v) { this.style.cssText = v; }
    get cssText() { return this._text || (this._text = `${this._sel} ${blockText(this)}`); }
}
class CSSGroupingRule extends CSSRule {
    get cssRules() { return this._list; }
    insertRule(rule, i = 0) { const r = cssInsert(this._list, rule, i, cssRuleSheet(this), this); this._changed(); return r; }
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
/* The rule is a read-only snapshot; parsing it must not reserve registration
 * names or modify the document's active styles. Keep its metadata private. */
const cssPropertyRules = new WeakMap();
const cssPropertyGet = Function.prototype.call.bind(WeakMap.prototype.get, cssPropertyRules);
const cssPropertySet = Function.prototype.call.bind(WeakMap.prototype.set, cssPropertyRules);
const cssPropertyEscape = value => {
    let output = '';
    for (const ch of value) {
        const cp = ch.codePointAt(0);
        if (cp < 32 || cp === 127) output += '\\' + cp.toString(16) + ' ';
        else output += cp >= 128 || /[-_a-z0-9]/i.test(ch) ? ch : '\\' + ch;
    }
    return output;
};
const cssPropertyQuote = value => '"' + value.replace(/["\\\x00-\x1f\x7f]/g, ch => {
    const cp = ch.charCodeAt(0);
    return cp < 32 || cp === 127 ? '\\' + cp.toString(16) + ' ' : '\\' + ch;
}) + '"';
class CSSPropertyRule extends CSSRule {
    get name() { return cssPropertyGet(this)[0]; }
    get syntax() { return cssPropertyGet(this)[1]; }
    get inherits() { return cssPropertyGet(this)[2]; }
    get initialValue() { return cssPropertyGet(this)[3]; }
    get cssText() {
        const [name, syntax, inherits, initial] = cssPropertyGet(this);
        return '@property ' + cssPropertyEscape(name) + ' { syntax: ' + cssPropertyQuote(syntax) +
            '; inherits: ' + (inherits ? 'true' : 'false') + ';' +
            (initial === null ? '' : ' initial-value: ' + initial + ';') + ' }';
    }
}
for (const key of ['name', 'syntax', 'inherits', 'initialValue'])
    Object.defineProperty(CSSPropertyRule.prototype, key, {enumerable: true});
function cssMkPropertyRule(name, body, base) {
    const data = N.cssPropertyRule(name, body);
    if (!data) return null;
    let syntax = null;
    for (let i = data[1].length - 1; i >= 0; i--) {
        const candidate = data[1][i]; syntax = rpParseSyntax(candidate);
        if (syntax) { data[1] = candidate; break; }
    }
    const initial = data[3];
    if (!syntax || (syntax !== '*' && (initial === null || !rpMatches(syntax, initial))) ||
        (initial !== null && syntax === '*' && !N.cssVarValid(initial, true))) return null;
    const rule = cssMk(CSSPropertyRule, base);
    cssPropertySet(rule, data);
    return rule;
}
class CSSFontFaceRule extends CSSRule { get type() { return 5; } get style() { return ruleStyle(this); } get cssText() { return `@font-face ${blockText(this)}`; } }
/* @position-try: only insets, margins, sizes, self-alignment, position-anchor and position-area */
const PT_ALLOWED = /^(top|right|bottom|left|inset(-block|-inline)?(-start|-end)?|margin(-top|-right|-bottom|-left|-block|-inline)?(-start|-end)?|(min-|max-)?(width|height|block-size|inline-size)|align-self|justify-self|place-self|position-anchor|position-area)$/;
class CSSPositionTryDescriptors extends CSSRuleStyle {
    constructor(rule, text) { super(rule, text); for (const k of [...this._map.keys()]) if (!PT_ALLOWED.test(k)) this._map.delete(k); }
    set cssText(v) { const m = parseDecls(String(v)); for (const k of [...m.keys()]) if (!PT_ALLOWED.test(k)) m.delete(k); this._w(m); }
    get cssText() { return super.cssText; }
    setProperty(p, v, pri) { p = String(p); if (!p.startsWith('--')) p = camelToKebab(p); if (PT_ALLOWED.test(p)) super.setProperty(p, v, pri); }
}
class CSSPositionTryRule extends CSSRule {
    get type() { return 0; } get name() { return this._name; }
    get style() { return this._style || (this._style = new Proxy(new CSSPositionTryDescriptors(this, this._body), styleHandler)); }
    get cssText() { const d = this.style.cssText; return `@position-try ${this._name} ${d ? `{ ${d} }` : '{ }'}`; }
}
/* CSS Paged Media3 page-selector-list. This is deliberately separate from
 * element selectors: page names are identifiers (including auto), pseudo names
 * are ASCII-insensitive, and whitespace is forbidden within each selector.
 * Comments separate tokens but are not whitespace; they must not join names.
 * https://drafts.csswg.org/css-page-3/#syntax-page-selector */
function cssPageSelectorText(input, atRule = false) {
    const s = input.replace(/\r\n?|\f/g, '\n').replace(/\0|[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/g, '\ufffd');
    let i = 0;
    const whitespace = c => c !== undefined && /[ \t\n]/.test(c);
    const nameStart = c => c !== undefined && (/[_a-z]/i.test(c) || c.charCodeAt(0) >= 128);
    const escapeAt = j => s[j] === '\\' && s[j + 1] !== '\n';
    const comments = () => {
        while (s[i] === '/' && s[i + 1] === '*') {
            const end = s.indexOf('*/', i + 2); i = end < 0 ? s.length : end + 2;
        }
    };
    const trivia = () => { for (;;) { const before = i; while (whitespace(s[i])) i++; comments(); if (i === before) return; } };
    const ident = () => {
        if (!(nameStart(s[i]) || escapeAt(i) || s[i] === '-' && (s[i + 1] === '-' || nameStart(s[i + 1]) || escapeAt(i + 1)))) return null;
        let value = '';
        while (i < s.length) {
            const c = s[i];
            if (nameStart(c) || /[-0-9]/.test(c)) { value += c; i++; continue; }
            if (!escapeAt(i)) break;
            i++;
            const start = i;
            while (i < s.length && i - start < 6 && /[0-9a-f]/i.test(s[i])) i++;
            if (i !== start) {
                const n = parseInt(s.slice(start, i), 16);
                value += n && n <= 0x10ffff && !(n >= 0xd800 && n <= 0xdfff) ? String.fromCodePoint(n) : '\ufffd';
                if (whitespace(s[i])) i++;
            } else value += i < s.length ? s[i++] : '\ufffd';
        }
        return value;
    };
    const serialize = text => {
        let out = '';
        for (let j = 0; j < text.length; j++) {
            const c = text[j], n = text.charCodeAt(j);
            if (n < 32 || n === 127 || /[0-9]/.test(c) && (j === 0 || j === 1 && text[0] === '-')) out += '\\' + n.toString(16) + ' ';
            else if (c === '-' && text.length === 1) out += '\\-';
            else out += n >= 128 || /[-_a-z0-9]/i.test(c) ? c : '\\' + c;
        }
        return out;
    };
    trivia();
    if (atRule) { if (s.slice(i, i + 5).toLowerCase() !== '@page') return null; i += 5; trivia(); }
    if (i === s.length) return '';
    const selectors = [];
    for (;;) {
        let selector = '';
        if (s[i] !== ':') { const name = ident(); if (name === null) return null; selector = serialize(name); }
        comments();
        while (s[i] === ':') {
            i++; comments(); const name = ident();
            if (name === null || !/^(left|right|first|blank)$/i.test(name)) return null;
            selector += ':' + name.toLowerCase(); comments();
        }
        if (!selector) return null;
        selectors.push(selector); trivia();
        if (i === s.length) return selectors.join(', ');
        if (s[i++] !== ',') return null;
        trivia(); if (i === s.length) return null;
    }
}
class CSSPageRule extends CSSRule {
    get type() { return 6; }
    get selectorText() { return this._sel; }
    set selectorText(v) {
        if (typeof v === 'symbol') throw new TypeError('Cannot convert a Symbol value to a string');
        const text = cssPageSelectorText(String(v));
        if (text !== null) { this._sel = text; this._changed(); }
    }
    get style() { return ruleStyle(this); }
    get cssText() { return `@page ${this._sel ? this._sel + ' ' : ''}${blockText(this)}`; }
}
/* One selector parser is shared by stylesheet parsing, keyText mutation and
 * keyframes lookup. Comments remain token boundaries; percentages are CSS
 * number tokens, including exponents, and are normalized only after validation. */
/* Numeric offsets remain separate from their CSSOM display representation.
 * Lookup and animation sampling must not use the rounded keyText string. */
const cssKeyframeValues = new WeakMap();
const cssKeyframeGet = Function.prototype.call.bind(WeakMap.prototype.get, cssKeyframeValues);
const cssKeyframeSet = Function.prototype.call.bind(WeakMap.prototype.set, cssKeyframeValues);
const cssKeyframePrecision = Function.prototype.call.bind(Number.prototype.toPrecision);
const cssKeyframeNumber = number => {
    const text = cssKeyframePrecision(number, 6);
    return text.includes('e') ? text : text.includes('.') ? text.replace(/0+$/, '').replace(/\.$/, '') : text;
};
function cssKeyframeSkipTrivia(text, offset) {
    while (offset < text.length) {
        if (/[\t\n\f\r ]/.test(text[offset])) { offset++; continue; }
        if (text[offset] === '/' && text[offset + 1] === '*') {
            const end = text.indexOf('*/', offset + 2); offset = end < 0 ? text.length : end + 2; continue;
        }
        break;
    }
    return offset;
}
function cssKeyframeString(value) {
    if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol to a CSSOMString.');
    return String(value);
}
function cssKeyframeSelectorData(input) {
    const text = cssKeyframeString(input), parts = []; let start = 0;
    for (let i = 0; i < text.length; i++) {
        if (text[i] === '\\') { i++; continue; }
        if (text[i] === '/' && text[i + 1] === '*') {
            const end = text.indexOf('*/', i + 2); i = end < 0 ? text.length : end + 1;
        } else if (text[i] === ',') { parts.push(text.slice(start, i)); start = i + 1; }
    }
    parts.push(text.slice(start));
    const values = [], offsets = [];
    for (const part of parts) {
        const first = cssKeyframeSkipTrivia(part, 0), match = /^[+-]?(?:\d*\.\d+|\d+)(?:[eE][+-]?\d+)?%/.exec(part.slice(first));
        if (match && cssKeyframeSkipTrivia(part, first + match[0].length) === part.length) {
            const number = Number(match[0].slice(0, -1));
            if (!Number.isFinite(number) || number < 0 || number > 100) return null;
            const offset = number / 100;
            offsets.push(offset); values.push(cssKeyframeNumber(offset * 100) + '%');
        } else {
            const keyword = N.cssPropertyIdentifier(part);
            if (keyword === null || !/^(from|to)$/i.test(keyword)) return null;
            const offset = /^from$/i.test(keyword) ? 0 : 1;
            offsets.push(offset); values.push(offset ? '100%' : '0%');
        }
    }
    return {text: values.join(', '), offsets};
}
function cssKeyframeIndex(list, input) {
    const selector = cssKeyframeSelectorData(input);
    if (selector !== null) for (let i = list.length - 1; i >= 0; i--) {
        const offsets = cssKeyframeGet(list[i]).offsets;
        if (offsets.length === selector.offsets.length && offsets.every((offset, index) => offset === selector.offsets[index])) return i;
    }
    return -1;
}
class CSSKeyframeRule extends CSSRule {
    get type() { return 8; }
    get keyText() { return cssKeyframeGet(this).text; }
    set keyText(value) {
        const selector = cssKeyframeSelectorData(value);
        if (selector === null) throw new DOMException('Invalid keyframe selector.', 'SyntaxError');
        cssKeyframeSet(this, selector); this._changed();
    }
    get style() { return ruleStyle(this); } set style(value) { this.style.cssText = value; }
    get cssText() { return `${cssKeyframeGet(this).text} ${blockText(this)}`; }
}
class CSSKeyframesRule extends CSSRule {
    get type() { return 7; } get name() { return this._name; } set name(v) { this._name = String(v); this._changed(); }
    get cssRules() { return this._list; } get length() { return this._list.length; }
    appendRule(value) {
        if (!arguments.length) throw new TypeError('appendRule requires a rule.');
        const text = cssKeyframeString(value), items = cssSplit(text, true);
        if (items.length !== 1) return;
        const item = items[0];
        if (item.body === null || cssKeyframeSkipTrivia(text.slice(0, item.start), 0) !== item.start ||
            cssKeyframeSkipTrivia(text, item.end) !== text.length) return;
        const rule = cssMkKeyframe(item, cssRuleSheet(this), this);
        if (rule) { this._list.push(rule); this._changed(); }
    }
    deleteRule(selector) {
        if (!arguments.length) throw new TypeError('deleteRule requires a selector.');
        const index = cssKeyframeIndex(this._list, selector);
        if (index >= 0) { cssDelete(this._list, index); this._changed(); }
    }
    findRule(selector) {
        if (!arguments.length) throw new TypeError('findRule requires a selector.');
        const index = cssKeyframeIndex(this._list, selector);
        return index < 0 ? null : this._list[index];
    }
    get cssText() { return `@keyframes ${this._name} ${CSSGroupingRule.prototype._inner.call(this)}`; }
}
function cssMk(C, props) {
    const r = Object.create(C.prototype), parent = props._parent;
    const type = C === CSSStyleRule ? 1 : C === CSSImportRule ? 3 : C === CSSMediaRule ? 4 :
        C === CSSFontFaceRule ? 5 : C === CSSPageRule ? 6 : C === CSSKeyframesRule ? 7 :
        C === CSSKeyframeRule ? 8 : C === CSSNamespaceRule ? 10 : C === CSSSupportsRule ? 12 : 0;
    cssOwnerSet(r, { parent, type, owner: !parent && props._sheet ? cssOwnerGet(props._sheet).owner : null });
    for (const k in props) if (k !== '_sheet' && k !== '_parent') def(r, k, props[k]);
    def(r, '_text', null); return r;
}
function cssMkKeyframe(it, sheet, parent) {
    if (it.body == null) return null;
    const key = cssKeyframeSelectorData(it.rawPre === undefined ? it.pre : it.rawPre);
    if (key === null) return null;
    const rule = cssMk(CSSKeyframeRule, { _body: it.body, _style: null, _sheet: sheet, _parent: parent });
    cssKeyframeSet(rule, key); return rule;
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
    if (kw === 'media' && blk) { r = cssMk(CSSMediaRule, { ...base, _media: null }); r._media = cssCreateMediaList(rest, r); }
    else if (kw === 'supports' && blk) r = cssMk(CSSSupportsRule, { ...base, _cond: rest });
    else if (kw === 'container' && blk) r = cssMk(CSSContainerRule, { ...base, _cond: rest });
    else if (kw === 'layer') { if (blk) r = cssMk(CSSLayerBlockRule, { ...base, _name: rest }); else return cssMk(CSSLayerStatementRule, { ...base, _names: rest.split(',').map(x => x.trim()).filter(Boolean) }); }
    else if (kw === 'import' && !blk) {
        const u = /^(?:url\(\s*(?:"([^"]*)"|'([^']*)'|([^)]*?))\s*\)|"([^"]*)"|'([^']*)')\s*([\s\S]*)$/.exec(rest); if (!u) return null;
        r = cssMk(CSSImportRule, { ...base, _href: u[1] ?? u[2] ?? u[3] ?? u[4] ?? u[5], _media: null }); r._media = cssCreateMediaList(u[6] || '', r); return r;
    }
    else if (kw === 'namespace' && !blk) { const q = /^(?:([-\w]+)\s+)?(?:url\(\s*["']?([^"')]*)["']?\s*\)|"([^"]*)"|'([^']*)')$/.exec(rest); if (!q) return null; return cssMk(CSSNamespaceRule, { ...base, _prefix: q[1] || '', _ns: q[2] ?? q[3] ?? q[4] }); }
    else if (kw === 'font-face' && blk) return cssMk(CSSFontFaceRule, { ...base, _body: it.body, _style: null });
    else if (kw === 'property' && blk) return cssMkPropertyRule(m[2], it.body, base);
    else if (kw === 'position-try' && blk && /^--\S+$/.test(rest)) return cssMk(CSSPositionTryRule, { ...base, _name: rest, _body: it.body, _style: null });
    else if (kw === 'page' && blk) {
        const raw = typeof it.pagePre === 'string', selector = cssPageSelectorText(raw ? it.pagePre : m[2], raw);
        if (selector === null) return null;
        return cssMk(CSSPageRule, { ...base, _sel: selector, _body: it.body, _style: null });
    }
    else if ((kw === 'keyframes' || kw === '-webkit-keyframes') && blk && rest) { r = cssMk(CSSKeyframesRule, { ...base, _name: rest, _list: null }); const l = new CSSRuleList(); for (const k of cssSplit(it.body, true)) { const kr = cssMkKeyframe(k, sheet, r); if (kr) l.push(kr); } r._list = l; return r; }
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
    const removed = cssListOwn(list, i).value;
    for (let j = i + 1; j < list.length; j++)
        cssListDefine(list, j - 1, { value: cssListOwn(list, j).value, writable: true, enumerable: true, configurable: true });
    cssListDefine(list, 'length', { value: list.length - 1 });
    cssDetachRule(removed);
}
class StyleSheet {}
class CSSStyleSheet extends StyleSheet {
    constructor(opts) { super(); const list = new CSSRuleList(); cssOwnerSet(this, { owner: { sheet: this }, list }); def(this, '_owner', null); def(this, '_src', null); def(this, '_list', list); def(this, '_media', cssCreateMediaList(opts && opts.media != null ? cssMediaString(opts.media) : '')); this.disabled = false; }
    get ownerNode() { return this._owner; } get type() { return 'text/css'; } get parentStyleSheet() { return null; } get ownerRule() { return null; }
    get href() { const o = this._owner; return o && N.name(o) === 'link' ? o.href : null; }
    get title() { const o = this._owner; return (o && N.attr(o, 'title')) || null; }
    get media() { return this._media; } set media(value) { this._media.mediaText = value; }
    _load() { const o = this._owner; if (o && N.name(o) === 'style') { const t = N.text(o); if (t !== this._src) { cssReplaceRules(this, cssParseList(t, this, null)); this._src = t; } } }
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
    replace(t) { cssReplaceSheet(this, t); return Promise.resolve(this); }
    replaceSync(t) { cssReplaceSheet(this, t); }
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
        if (!s._mirrors) def(s, '_mirrors', new SMap());
        let n = s._mirrors.get(scope);
        if (n) { if (n.parentNode) N.remove(n); } else { n = N.create('style', 0); def(n, '__mirror', true); s._mirrors.set(scope, n); N.setText(n, s._text()); }
        if (root) N.insert(root, n, null);
    }
}
function readopt(scope) { if (scope.__ass.some(s => { const n = s._mirrors && s._mirrors.get(scope); return !n || !n.parentNode; })) adoptSheets(scope, scope.__ass); }
function adoptedGet(scope) { return new Proxy((scope.__ass || []).slice(), { set(t, k, val) { t[k] = val; adoptSheets(scope, t); return true; } }); }
function scopeSheets(scope) { return N.query(scope, 'style,link[rel~=stylesheet]', true).filter(el => !el.__mirror).map(sheetFor); }
function sheetFor(el) { if (!el.__sheet) { const s = new CSSStyleSheet(); def(s, '_owner', el); def(s, '_media', cssCreateMediaList(N.attrNS(el, null, 'media') || '', null, el)); def(el, '__sheet', s); } return el.__sheet; }
/* CSS.registerProperty: validates the definition (name, syntax, a computationally independent initial value) */
const registeredProps = new SMap();
const RP_TYPES = new SSet(['length', 'number', 'percentage', 'length-percentage', 'color', 'image', 'url', 'integer', 'angle', 'time', 'resolution', 'transform-function', 'custom-ident', 'transform-list', 'string']);
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
        if (!m) {
            /* Escaped and non-ASCII literal syntax components are identifiers,
             * not words. The native lexer preserves their actual codepoints. */
            const text = part.trim(); let ident = N.cssPropertyIdentifier(text), mult = '';
            if (text.includes('/*')) return null;
            if (ident === null && /[+#]$/.test(text)) {
                mult = text.slice(-1); ident = N.cssPropertyIdentifier(text.slice(0, -1));
            }
            if (ident === null || /^(inherit|initial|unset|revert|revert-layer|default)$/i.test(ident)) return null;
            out.push({ident, mult}); continue;
        }
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
        const one = x => c.ident ? (x === c.ident || N.cssPropertyIdentifier(x) === c.ident) : RP_CHECK[c.type](x);
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
    if (iv !== undefined && (comps === '*' ? !N.cssVarValid(iv, true) : !rpMatches(comps, iv))) throw new DOMException('The initial value provided does not parse for the given syntax.', 'SyntaxError');
    if (registeredProps.has(name)) throw new DOMException('The name provided has already been registered.', 'InvalidModificationError');
    registeredProps.set(name, { syntax, inherits: !!d.inherits, initialValue: iv });
    N.registerProp(name, iv === undefined ? "" : iv.trim(), syntax.trim(), registeredProps.get(name).inherits ? "true" : "false", iv === undefined ? "false" : "true");
}
const CSS = {
    escape(s) { s = String(s); let r = ''; for (let i = 0; i < s.length; i++) { const c = s.charCodeAt(i); if (c === 0) r += '\uFFFD'; else if ((c >= 1 && c <= 31) || c === 127 || (i === 0 && c >= 48 && c <= 57) || (i === 1 && c >= 48 && c <= 57 && s.charCodeAt(0) === 45)) r += '\\' + c.toString(16) + ' '; else if (i === 0 && c === 45 && s.length === 1) r += '\\-'; else if (c >= 128 || c === 45 || c === 95 || (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122)) r += s[i]; else r += '\\' + s[i]; } return r; },
    supports(p, v) {
        if (v === undefined) return N.cssSupports(String(p));
        p = String(p); v = String(v);
        if (p.startsWith('--')) return N.cssSupports(`(${p}: ${v})`);
        const lp = p.toLowerCase();
        if (!CSS_PROP_NAMES.has(lp)) return false;
        if (/^\s*(inherit|initial|unset|revert|revert-layer)\s*$/i.test(v)) return true;
        if (SPEC_COLOR_PROPS.has(lp) && !/var\(|env\(|attr\(/i.test(v)) return specColor(v, lp) !== null;
        if (!mathValid(lp, v)) return false;
        return N.cssSupports(`(${lp}: initial)`) ? N.cssSupports(`(${lp}: ${v})`) : !!v.trim() && tomValidText(lp, v);
    },
    registerProperty(d) { registerProperty(d); },
};
/* set-like over the faces a page adds: pages spread it (`[...document.fonts]`) and read .size */
const fontFaces = new SSet();
/* loading while the document loads, a FontFace in the set loads, or laid-out text waits for an @font-face fetch */
const fontsBusy = () => {
    if (N.readyState() !== 2 || N.fontsPending() > 0) return true;
    for (const f of fontFaces) if (f.status === 'loading') return true;
    return false;
};
let fontsReady = null, fontsReadyDone = false;
const fontsWait = () => {
    if (fontsReady && !fontsReadyDone) return fontsReady;
    if (fontsReady && !fontsBusy()) return fontsReady;
    fontsReadyDone = false;
    let res; fontsReady = new Promise(r => { res = r; });
    const tick = () => { if (fontsBusy()) setTimeout(tick, 16); else { fontsReadyDone = true; res(fontSet); } };
    tick();
    return fontsReady;
};
const fontSet = Object.assign(new EventTarget(), { load() { return Promise.resolve([]); }, check() { return true; },
    add(f) { fontFaces.add(f); return this; }, delete(f) { return fontFaces.delete(f); }, clear() { fontFaces.clear(); },
    forEach(cb, t) { for (const f of fontFaces) cb.call(t, f, f, this); }, has(f) { return fontFaces.has(f); },
    values() { return fontFaces.values(); }, keys() { return fontFaces.values(); }, entries() { return fontFaces.entries(); },
    [Symbol.iterator]() { return fontFaces.values(); } });
Object.defineProperty(fontSet, 'size', { get() { return fontFaces.size; } });
Object.defineProperty(fontSet, 'ready', { get() { return fontsWait(); } });
Object.defineProperty(fontSet, 'status', { get() { return fontsBusy() ? 'loading' : 'loaded'; } });
/*@shared fontface.js*/
