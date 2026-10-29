/* URLPattern per the URL Pattern Standard, shared by the page and worker preludes; it follows the standard's
   algorithms step by step. The including prelude defines URLN with the URL natives urlParse(input, base) and
   urlSet(href, key, value, strict), whose parts are [href, protocol, username, password, host, hostname, port,
   pathname, search, hash, origin]; a strict urlSet returns null when the setter's parse fails. */
const UP_COMPONENTS = ['protocol', 'username', 'password', 'hostname', 'port', 'pathname', 'search', 'hash'];
const UP_SPECIAL = ['ftp', 'file', 'http', 'https', 'ws', 'wss'];
const UP_DEFAULT_PORT = { ftp: '21', http: '80', https: '443', ws: '80', wss: '443' };
const UP_DUMMY = 'https://dummy.invalid/';
const upErr = m => new TypeError(`Failed to construct 'URLPattern': ${m}`);
const UP_ID_START = /[$_\p{ID_Start}]/u, UP_ID_PART = /[$_‌‍\p{ID_Continue}]/u;

/* 2.1.2 tokenizing */
function upTokenize(input, policy) {
    const cps = Array.from(input), tokens = [];
    let index = 0, next = 0, cp = '';
    const nextCp = () => { cp = cps[next]; next++; };
    const seek = i => { next = i; nextCp(); };
    const add = (type, nextPos, valuePos, valueLen) => { tokens.push({ type, index, value: cps.slice(valuePos, valuePos + valueLen).join('') }); index = nextPos; };
    const addLen = (type, nextPos, valuePos) => add(type, nextPos, valuePos, nextPos - valuePos);
    const addDef = type => addLen(type, next, index);
    const error = (nextPos, valuePos) => { if (policy === 'strict') throw new TypeError('Invalid pattern'); addLen('invalid-char', nextPos, valuePos); };
    while (index < cps.length) {
        seek(index);
        if (cp === '*') { addDef('asterisk'); continue; }
        if (cp === '+' || cp === '?') { addDef('other-modifier'); continue; }
        if (cp === '\\') {
            if (index === cps.length - 1) { error(next, index); continue; }
            const escaped = next;
            nextCp();
            addLen('escaped-char', next, escaped);
            continue;
        }
        if (cp === '{') { addDef('open'); continue; }
        if (cp === '}') { addDef('close'); continue; }
        if (cp === ':') {
            let pos = next;
            const start = pos;
            while (pos < cps.length) {
                seek(pos);
                if (!(pos === start ? UP_ID_START : UP_ID_PART).test(cp)) break;
                pos = next;
            }
            if (pos <= start) { error(start, index); continue; }
            addLen('name', pos, start);
            continue;
        }
        if (cp === '(') {
            let depth = 1, pos = next, err = false;
            const start = pos;
            while (pos < cps.length) {
                seek(pos);
                if (cp.codePointAt(0) > 0x7f) { error(start, index); err = true; break; }
                if (pos === start && cp === '?') { error(start, index); err = true; break; }
                if (cp === '\\') {
                    if (pos === cps.length - 1) { error(start, index); err = true; break; }
                    nextCp();
                    if (cp.codePointAt(0) > 0x7f) { error(start, index); err = true; break; }
                    pos = next;
                    continue;
                }
                if (cp === ')') {
                    depth--;
                    if (depth === 0) { pos = next; break; }
                } else if (cp === '(') {
                    depth++;
                    if (pos === cps.length - 1) { error(start, index); err = true; break; }
                    const tmp = next;
                    nextCp();
                    if (cp !== '?') { error(start, index); err = true; break; }
                    next = tmp;
                }
                pos = next;
            }
            if (err) continue;
            if (depth !== 0) { error(start, index); continue; }
            const len = pos - start - 1;
            if (len === 0) { error(start, index); continue; }
            add('regexp', pos, start, len);
            continue;
        }
        addDef('char');
    }
    addLen('end', index, index);
    return tokens;
}

/* 2.1.5 parsing a pattern string into parts */
const upEscapeRegexp = s => s.replace(/[.+*?^${}()[\]|/\\]/g, '\\$&');
const upEscapePattern = s => s.replace(/[+*?:{}()\\]/g, '\\$&');
const upSegmentWildcard = o => `[^${upEscapeRegexp(o.delimiter)}]+?`;
const UP_FULL_WILDCARD = '.*';
function upParse(input, options, encode) {
    const p = { tokens: upTokenize(input, 'strict'), parts: [], pending: '', index: 0, nextName: 0, segment: upSegmentWildcard(options) };
    const tryTok = type => { const t = p.tokens[p.index]; if (t.type !== type) return null; p.index++; return t; };
    const tryModifier = () => tryTok('other-modifier') || tryTok('asterisk');
    const tryRegexpOrWildcard = name => { let t = tryTok('regexp'); if (!name && !t) t = tryTok('asterisk'); return t; };
    const required = type => { const t = tryTok(type); if (!t) throw new TypeError('Invalid pattern'); return t; };
    const text = () => { let r = ''; for (;;) { const t = tryTok('char') || tryTok('escaped-char'); if (!t) return r; r += t.value; } };
    const flush = () => {
        if (p.pending === '') return;
        const value = encode(p.pending);
        p.pending = '';
        p.parts.push({ type: 'fixed-text', value, modifier: 'none', name: '', prefix: '', suffix: '' });
    };
    const addPart = (prefix, nameTok, reTok, suffix, modTok) => {
        let modifier = 'none';
        if (modTok) modifier = modTok.value === '?' ? 'optional' : modTok.value === '*' ? 'zero-or-more' : 'one-or-more';
        if (!nameTok && !reTok && modifier === 'none') { p.pending += prefix; return; }
        flush();
        if (!nameTok && !reTok) {
            if (prefix === '') return;
            p.parts.push({ type: 'fixed-text', value: encode(prefix), modifier, name: '', prefix: '', suffix: '' });
            return;
        }
        let value = !reTok ? p.segment : reTok.type === 'asterisk' ? UP_FULL_WILDCARD : reTok.value, type = 'regexp';
        if (value === p.segment) { type = 'segment-wildcard'; value = ''; }
        else if (value === UP_FULL_WILDCARD) { type = 'full-wildcard'; value = ''; }
        let name = '';
        if (nameTok) name = nameTok.value;
        else if (reTok) name = String(p.nextName++);
        if (p.parts.some(q => q.name === name)) throw new TypeError('Duplicate name');
        p.parts.push({ type, value, modifier, name, prefix: encode(prefix), suffix: encode(suffix) });
    };
    while (p.index < p.tokens.length) {
        const charTok = tryTok('char');
        let nameTok = tryTok('name'), reTok = tryRegexpOrWildcard(nameTok);
        if (nameTok || reTok) {
            let prefix = charTok ? charTok.value : '';
            if (prefix !== '' && prefix !== options.prefix) { p.pending += prefix; prefix = ''; }
            flush();
            addPart(prefix, nameTok, reTok, '', tryModifier());
            continue;
        }
        const fixed = charTok || tryTok('escaped-char');
        if (fixed) { p.pending += fixed.value; continue; }
        if (tryTok('open')) {
            const prefix = text();
            nameTok = tryTok('name');
            reTok = tryRegexpOrWildcard(nameTok);
            const suffix = text();
            required('close');
            addPart(prefix, nameTok, reTok, suffix, tryModifier());
            continue;
        }
        flush();
        required('end');
    }
    return p.parts;
}
const upModifier = m => m === 'zero-or-more' ? '*' : m === 'optional' ? '?' : m === 'one-or-more' ? '+' : '';

/* 2.2 a part list's regular expression and group names */
function upRegexp(parts, options) {
    let r = '^';
    const names = [];
    for (const part of parts) {
        if (part.type === 'fixed-text') {
            r += part.modifier === 'none' ? upEscapeRegexp(part.value) : `(?:${upEscapeRegexp(part.value)})${upModifier(part.modifier)}`;
            continue;
        }
        names.push(part.name);
        const v = part.type === 'segment-wildcard' ? upSegmentWildcard(options) : part.type === 'full-wildcard' ? UP_FULL_WILDCARD : part.value;
        if (part.prefix === '' && part.suffix === '') {
            r += part.modifier === 'none' || part.modifier === 'optional' ? `(${v})${upModifier(part.modifier)}` : `((?:${v})${upModifier(part.modifier)})`;
            continue;
        }
        const pre = upEscapeRegexp(part.prefix), suf = upEscapeRegexp(part.suffix);
        if (part.modifier === 'none' || part.modifier === 'optional') { r += `(?:${pre}(${v})${suf})${upModifier(part.modifier)}`; continue; }
        r += `(?:${pre}((?:${v})(?:${suf}${pre}(?:${v}))*)${suf})`;
        if (part.modifier === 'zero-or-more') r += '?';
    }
    return [r + '$', names];
}

/* 2.3 a part list's normalized pattern string */
function upPatternString(parts, options) {
    let r = '';
    for (let i = 0; i < parts.length; i++) {
        const part = parts[i], prev = i > 0 ? parts[i - 1] : null, next = i < parts.length - 1 ? parts[i + 1] : null;
        if (part.type === 'fixed-text') {
            r += part.modifier === 'none' ? upEscapePattern(part.value) : `{${upEscapePattern(part.value)}}${upModifier(part.modifier)}`;
            continue;
        }
        const custom = !/^[0-9]/.test(part.name);
        let group = part.suffix !== '' || (part.prefix !== '' && part.prefix !== options.prefix);
        if (!group && custom && part.type === 'segment-wildcard' && part.modifier === 'none' && next && next.prefix === '' && next.suffix === '') {
            if (next.type === 'fixed-text') group = next.value !== '' && UP_ID_PART.test(Array.from(next.value)[0]);
            else group = /^[0-9]/.test(next.name);
        }
        if (!group && part.prefix === '' && prev && prev.type === 'fixed-text' && prev.value !== '' && Array.from(prev.value).pop() === options.prefix) group = true;
        if (group) r += '{';
        r += upEscapePattern(part.prefix);
        if (custom) r += ':' + part.name;
        if (part.type === 'regexp') r += `(${part.value})`;
        else if (part.type === 'segment-wildcard' && !custom) r += `(${upSegmentWildcard(options)})`;
        else if (part.type === 'full-wildcard') {
            if (!custom && (!prev || prev.type === 'fixed-text' || prev.modifier !== 'none' || group || part.prefix !== '')) r += '*';
            else r += `(${UP_FULL_WILDCARD})`;
        }
        if (part.type === 'segment-wildcard' && custom && part.suffix !== '' && UP_ID_PART.test(Array.from(part.suffix)[0])) r += '\\';
        r += upEscapePattern(part.suffix);
        if (group) r += '}';
        r += upModifier(part.modifier);
    }
    return r;
}

/* 3.1 the encoding callbacks */
function upCanonProtocol(v) {
    if (v === '') return v;
    const p = URLN.urlParse(v + '://dummy.invalid/', null);
    if (!p) throw upErr(`Invalid protocol '${v}'.`);
    return p[1].slice(0, -1);
}
const upCanonUsername = v => v === '' ? v : URLN.urlSet(UP_DUMMY, 'username', v)[2];
const upCanonPassword = v => v === '' ? v : URLN.urlSet(UP_DUMMY, 'password', v)[3];
function upCanonHostname(v) {
    if (v === '') return v;
    const p = URLN.urlSet(UP_DUMMY, 'hostname', v, true);
    if (!p) throw upErr(`Invalid hostname '${v}'.`);
    return p[5];
}
function upCanonIPv6(v) {
    let r = '';
    for (const c of v) {
        if (!/[0-9A-Fa-f[\]:]/.test(c)) throw upErr(`Invalid IPv6 hostname '${v}'.`);
        r += c.toLowerCase();
    }
    return r;
}
function upCanonPort(v, protocol) {
    if (v === '') return v;
    /* a scheme of the protocol's, or none, so that the URL parser knows the default port */
    const base = protocol !== undefined && /^[A-Za-z][A-Za-z0-9+.-]*$/.test(protocol) ? (UP_SPECIAL.includes(protocol.toLowerCase()) ? protocol + '://dummy.invalid/' : protocol + '://dummy.invalid/') : 'lumen-dummy://dummy.invalid/';
    const p = URLN.urlSet(base, 'port', v, true);
    if (!p) throw upErr(`Invalid port '${v}'.`);
    return p[6];
}
function upCanonPathname(v) {
    if (v === '') return v;
    const lead = v[0] === '/';
    const p = URLN.urlSet(UP_DUMMY, 'pathname', (lead ? '' : '/-') + v);
    return lead ? p[7] : p[7].slice(2);
}
function upCanonOpaque(v) {
    if (v === '') return v;
    const p = URLN.urlParse('lumen-dummy:' + v + 'x', null);   /* the x keeps trailing spaces, which URL parsing strips */
    if (!p) throw upErr(`Invalid pathname '${v}'.`);
    return p[7].slice(0, -1);
}
const upCanonSearch = v => v === '' ? v : URLN.urlSet(UP_DUMMY, 'search', '?' + v)[8].slice(1);
const upCanonHash = v => v === '' ? v : URLN.urlSet(UP_DUMMY, 'hash', '#' + v)[9].slice(1);

/* 3.2 processing a URLPatternInit */
const upBaseString = (s, type) => type !== 'pattern' ? s : upEscapePattern(s);
function upIsAbsolutePathname(s, type) {
    if (s === '') return false;
    if (s[0] === '/') return true;
    if (type === 'url' || s.length < 2) return false;
    return (s[0] === '\\' || s[0] === '{') && s[1] === '/';
}
function upProcessInit(init, type, base) {
    const result = Object.assign({}, base);
    const has = k => init[k] !== undefined;
    let baseURL = null;
    if (has('baseURL')) {
        baseURL = URLN.urlParse(init.baseURL, null);
        if (!baseURL) throw upErr(`Invalid baseURL '${init.baseURL}'.`);
        const [, protocol, username, password, , hostname, port, pathname, search, hash] = baseURL;
        if (!has('protocol')) result.protocol = upBaseString(protocol.slice(0, -1), type);
        if (type !== 'pattern' && !has('protocol') && !has('hostname') && !has('port') && !has('username')) result.username = upBaseString(username, type);
        if (type !== 'pattern' && !has('protocol') && !has('hostname') && !has('port') && !has('username') && !has('password')) result.password = upBaseString(password, type);
        if (!has('protocol') && !has('hostname')) result.hostname = upBaseString(hostname, type);
        if (!has('protocol') && !has('hostname') && !has('port')) result.port = port;
        if (!has('protocol') && !has('hostname') && !has('port') && !has('pathname')) result.pathname = upBaseString(pathname, type);
        if (!has('protocol') && !has('hostname') && !has('port') && !has('pathname') && !has('search')) result.search = upBaseString(search.replace(/^\?/, ''), type);
        if (!has('protocol') && !has('hostname') && !has('port') && !has('pathname') && !has('search') && !has('hash')) result.hash = upBaseString(hash.replace(/^#/, ''), type);
    }
    if (has('protocol')) { const v = String(init.protocol).replace(/:$/, ''); result.protocol = type === 'pattern' ? v : upCanonProtocol(v); }
    if (has('username')) result.username = type === 'pattern' ? String(init.username) : upCanonUsername(String(init.username));
    if (has('password')) result.password = type === 'pattern' ? String(init.password) : upCanonPassword(String(init.password));
    if (has('hostname')) result.hostname = type === 'pattern' ? String(init.hostname) : upCanonHostname(String(init.hostname));
    const protocol = result.protocol !== undefined ? result.protocol : '';
    if (has('port')) result.port = type === 'pattern' ? String(init.port) : upCanonPort(String(init.port), protocol);
    if (has('pathname')) {
        result.pathname = String(init.pathname);
        if (baseURL && !upOpaque(baseURL) && !upIsAbsolutePathname(result.pathname, type)) {
            const basePath = upBaseString(baseURL[7], type), slash = basePath.lastIndexOf('/');
            if (slash >= 0) result.pathname = basePath.slice(0, slash + 1) + result.pathname;
        }
        if (type !== 'pattern') result.pathname = UP_SPECIAL.includes(protocol) || protocol === '' ? upCanonPathname(result.pathname) : upCanonOpaque(result.pathname);
    }
    if (has('search')) { const v = String(init.search).replace(/^\?/, ''); result.search = type === 'pattern' ? v : upCanonSearch(v); }
    if (has('hash')) { const v = String(init.hash).replace(/^#/, ''); result.hash = type === 'pattern' ? v : upCanonHash(v); }
    return result;
}
/* a parsed URL (parts) has an opaque path: its path does not start with "/" after a scheme with no authority */
const upOpaque = parts => !UP_SPECIAL.includes(parts[1].slice(0, -1)) && !parts[0].slice(parts[1].length).startsWith('//') && !parts[7].startsWith('/');

/* 1.5 compiling a component */
const UP_DEFAULT_OPTIONS = { delimiter: '', prefix: '' }, UP_HOSTNAME_OPTIONS = { delimiter: '.', prefix: '' }, UP_PATHNAME_OPTIONS = { delimiter: '/', prefix: '/' };
function upCompile(input, encode, options) {
    const parts = upParse(input, options, encode);
    const [source, names] = upRegexp(parts, options);
    let regexp;
    try { regexp = new RegExp(source, options.ignoreCase ? 'vi' : 'v'); } catch (e) { throw upErr(`Invalid regular expression '${source}'.`); }
    return { pattern: upPatternString(parts, options), regexp, names, hasRegExpGroups: parts.some(q => q.type === 'regexp'), parts, encode, segment: upSegmentWildcard(options) };
}
const upMatchesSpecial = c => UP_SPECIAL.some(s => c.regexp.exec(s) !== null);
function upIsIPv6(s) {
    const c = Array.from(s);
    if (c.length < 2) return false;
    return c[0] === '[' || ((c[0] === '{' || c[0] === '\\') && c[1] === '[');
}

/* 1.6 the constructor string parser */
function upParseConstructor(input) {
    const p = { input: Array.from(input), tokens: upTokenize(input, 'lenient'), result: {}, start: 0, index: 0, inc: 1, depth: 0, ipv6: 0, special: false, state: 'init' };
    const safe = i => i < p.tokens.length ? p.tokens[i] : p.tokens[p.tokens.length - 1];
    const nonSpecial = (i, v) => { const t = safe(i); return t.value === v && (t.type === 'char' || t.type === 'escaped-char' || t.type === 'invalid-char'); };
    const isProtocolSuffix = () => nonSpecial(p.index, ':');
    const nextIsAuthoritySlashes = () => nonSpecial(p.index + 1, '/') && nonSpecial(p.index + 2, '/');
    const isIdentityTerminator = () => nonSpecial(p.index, '@');
    const isPasswordPrefix = () => nonSpecial(p.index, ':');
    const isPortPrefix = () => nonSpecial(p.index, ':');
    const isPathnameStart = () => nonSpecial(p.index, '/');
    const isSearchPrefix = () => {
        if (nonSpecial(p.index, '?')) return true;
        if (p.tokens[p.index].value !== '?') return false;
        if (p.index - 1 < 0) return true;
        const prev = safe(p.index - 1);
        return !['name', 'regexp', 'close', 'asterisk'].includes(prev.type);
    };
    const isHashPrefix = () => nonSpecial(p.index, '#');
    const componentString = () => p.input.slice(safe(p.start).index, p.tokens[p.index].index).join('');
    const changeState = (state, skip) => {
        if (p.state !== 'init' && p.state !== 'authority' && p.state !== 'done') p.result[p.state] = componentString();
        if (p.state !== 'init' && state !== 'done') {
            const early = ['protocol', 'authority', 'username', 'password'];
            if (early.includes(p.state) && ['port', 'pathname', 'search', 'hash'].includes(state) && p.result.hostname === undefined) p.result.hostname = '';
            if ([...early, 'hostname', 'port'].includes(p.state) && (state === 'search' || state === 'hash') && p.result.pathname === undefined) p.result.pathname = p.special ? '/' : '';
            if ([...early, 'hostname', 'port', 'pathname'].includes(p.state) && state === 'hash' && p.result.search === undefined) p.result.search = '';
        }
        p.state = state;
        p.index += skip;
        p.start = p.index;
        p.inc = 0;
    };
    const rewind = () => { p.index = p.start; p.inc = 0; };
    const rewindAndSet = state => { rewind(); p.state = state; };
    while (p.index < p.tokens.length) {
        p.inc = 1;
        if (p.tokens[p.index].type === 'end') {
            if (p.state === 'init') {
                rewind();
                if (isHashPrefix()) changeState('hash', 1);
                else if (isSearchPrefix()) changeState('search', 1);
                else changeState('pathname', 0);
                p.index += p.inc;
                continue;
            }
            if (p.state === 'authority') { rewindAndSet('hostname'); p.index += p.inc; continue; }
            changeState('done', 0);
            break;
        }
        if (p.tokens[p.index].type === 'open') { p.depth++; p.index += p.inc; continue; }
        if (p.depth > 0) {
            if (p.tokens[p.index].type === 'close') p.depth--;
            else { p.index += p.inc; continue; }
        }
        switch (p.state) {
        case 'init':
            if (isProtocolSuffix()) rewindAndSet('protocol');
            break;
        case 'protocol':
            if (isProtocolSuffix()) {
                const c = upCompile(componentString(), upCanonProtocol, UP_DEFAULT_OPTIONS);
                if (upMatchesSpecial(c)) p.special = true;
                let next = 'pathname', skip = 1;
                if (nextIsAuthoritySlashes()) { next = 'authority'; skip = 3; }
                else if (p.special) next = 'authority';
                changeState(next, skip);
            }
            break;
        case 'authority':
            if (isIdentityTerminator()) rewindAndSet('username');
            else if (isPathnameStart() || isSearchPrefix() || isHashPrefix()) rewindAndSet('hostname');
            break;
        case 'username':
            if (isPasswordPrefix()) changeState('password', 1);
            else if (isIdentityTerminator()) changeState('hostname', 1);
            break;
        case 'password':
            if (isIdentityTerminator()) changeState('hostname', 1);
            break;
        case 'hostname':
            if (nonSpecial(p.index, '[')) p.ipv6++;
            else if (nonSpecial(p.index, ']')) p.ipv6--;
            else if (isPortPrefix() && p.ipv6 === 0) changeState('port', 1);
            else if (isPathnameStart()) changeState('pathname', 0);
            else if (isSearchPrefix()) changeState('search', 1);
            else if (isHashPrefix()) changeState('hash', 1);
            break;
        case 'port':
            if (isPathnameStart()) changeState('pathname', 0);
            else if (isSearchPrefix()) changeState('search', 1);
            else if (isHashPrefix()) changeState('hash', 1);
            break;
        case 'pathname':
            if (isSearchPrefix()) changeState('search', 1);
            else if (isHashPrefix()) changeState('hash', 1);
            break;
        case 'search':
            if (isHashPrefix()) changeState('hash', 1);
            break;
        }
        p.index += p.inc;
    }
    if (p.result.hostname !== undefined && p.result.port === undefined) p.result.port = '';
    return p.result;
}

/* an IDL URLPatternInit: its USVString members, read in order */
function upInit(v, what) {
    if (v === null || v === undefined) return {};
    if (typeof v !== 'object' && typeof v !== 'function') throw new TypeError(`Failed to execute '${what}': The provided value is not of type 'URLPatternInit'.`);
    const r = {};
    for (const k of ['baseURL', 'hash', 'hostname', 'password', 'pathname', 'port', 'protocol', 'search', 'username']) {
        const x = v[k];
        if (x !== undefined) r[k] = String(x).toWellFormed();
    }
    return r;
}
const UP_KEY = Symbol('URLPattern');
class URLPattern {
    #c;
    constructor(input = undefined, baseURL = undefined, options = undefined) {
        if (baseURL !== null && typeof baseURL === 'object' && options === undefined) { options = baseURL; baseURL = undefined; }
        let init;
        if (typeof input === 'string' || (input !== null && typeof input !== 'object' && input !== undefined)) {
            input = String(input).toWellFormed();
            init = upParseConstructor(input);
            if (baseURL === undefined && init.protocol === undefined) throw upErr('Relative constructor string must have a base URL passed as the second argument.');
            if (baseURL !== undefined) init.baseURL = String(baseURL).toWellFormed();
        } else {
            if (baseURL !== undefined) throw upErr('Invalid second argument baseURL provided with a URLPatternInit input. Use the URLPatternInit.baseURL property instead.');
            init = upInit(input, 'URLPattern');
        }
        const ignoreCase = !!(options && options.ignoreCase);
        const pi = upProcessInit(init, 'pattern', {});
        for (const k of UP_COMPONENTS) if (pi[k] === undefined) pi[k] = '*';
        if (UP_SPECIAL.includes(pi.protocol) && UP_DEFAULT_PORT[pi.protocol] === pi.port) pi.port = '';
        const c = {};
        c.protocol = upCompile(pi.protocol, upCanonProtocol, UP_DEFAULT_OPTIONS);
        c.username = upCompile(pi.username, upCanonUsername, UP_DEFAULT_OPTIONS);
        c.password = upCompile(pi.password, upCanonPassword, UP_DEFAULT_OPTIONS);
        c.hostname = upCompile(pi.hostname, upIsIPv6(pi.hostname) ? upCanonIPv6 : upCanonHostname, UP_HOSTNAME_OPTIONS);
        c.port = upCompile(pi.port, v => upCanonPort(v), UP_DEFAULT_OPTIONS);
        const opts = Object.assign({}, UP_DEFAULT_OPTIONS, { ignoreCase });
        if (upMatchesSpecial(c.protocol)) c.pathname = upCompile(pi.pathname, upCanonPathname, Object.assign({}, UP_PATHNAME_OPTIONS, { ignoreCase }));
        else c.pathname = upCompile(pi.pathname, upCanonOpaque, opts);
        c.search = upCompile(pi.search, upCanonSearch, opts);
        c.hash = upCompile(pi.hash, upCanonHash, opts);
        this.#c = c;
    }
    test(input = undefined, baseURL = undefined) { return this.#match(input, baseURL, arguments.length > 1) !== null; }
    exec(input = undefined, baseURL = undefined) { return this.#match(input, baseURL, arguments.length > 1); }
    get protocol() { return this.#c.protocol.pattern; }
    get username() { return this.#c.username.pattern; }
    get password() { return this.#c.password.pattern; }
    get hostname() { return this.#c.hostname.pattern; }
    get port() { return this.#c.port.pattern; }
    get pathname() { return this.#c.pathname.pattern; }
    get search() { return this.#c.search.pattern; }
    get hash() { return this.#c.hash.pattern; }
    get hasRegExpGroups() { return UP_COMPONENTS.some(k => this.#c[k].hasRegExpGroups); }
    /* 1.4 match */
    #match(input, baseURL, baseGiven) {
        const values = {}, inputs = [];
        if (baseGiven && baseURL === undefined) baseGiven = false;
        if (input !== null && typeof input === 'object' || input === undefined) {
            const init = upInit(input, 'URLPattern');
            inputs.push(input === undefined ? {} : input);
            if (baseGiven) throw new TypeError("Failed to execute 'exec' on 'URLPattern': Invalid second argument baseURL provided with a URLPatternInit input. Use the URLPatternInit.baseURL property instead.");
            let r;
            try { r = upProcessInit(init, 'url', { protocol: '', username: '', password: '', hostname: '', port: '', pathname: '', search: '', hash: '' }); } catch (e) { return null; }
            Object.assign(values, r);
        } else {
            input = String(input).toWellFormed();
            inputs.push(input);
            let base = null;
            if (baseGiven) {
                baseURL = String(baseURL).toWellFormed();
                if (!URLN.urlParse(baseURL, null)) return null;
                base = baseURL;
                inputs.push(baseURL);
            }
            const u = URLN.urlParse(input, base);
            if (!u) return null;
            Object.assign(values, { protocol: u[1].slice(0, -1), username: u[2], password: u[3], hostname: u[5], port: u[6], pathname: u[7], search: u[8].replace(/^\?/, ''), hash: u[9].replace(/^#/, '') });
            if (u[0].slice(u[1].length).match(/^[^#]*\?/) && values.search === '') values.search = '';
        }
        const execs = {};
        for (const k of UP_COMPONENTS) {
            const m = this.#c[k].regexp.exec(values[k]);
            if (m === null) return null;
            execs[k] = m;
        }
        const result = { inputs };
        for (const k of UP_COMPONENTS) {
            const groups = {};
            this.#c[k].names.forEach((name, i) => { groups[name] = execs[k][i + 1]; });
            result[k] = { input: values[k], groups };
        }
        return result;
    }
    /* the component's URL from groups: only fixed text and named segment wildcards, without modifiers (as Chrome) */
    generate(component, groups) {
        const W = "Failed to execute 'generate' on 'URLPattern': ";
        if (arguments.length < 2) throw new TypeError(W + `2 arguments required, but only ${arguments.length} present.`);
        component = String(component);
        if (!UP_COMPONENTS.includes(component)) throw new TypeError(W + `The provided value '${component}' is not a valid enum value of type URLPatternComponent.`);
        if (groups === null || typeof groups !== 'object') throw new TypeError(W + "parameter 2 is not of type 'record<USVString, USVString>'.");
        const c = this.#c[component], seg = new RegExp('^(?:' + c.segment + ')$', 'v');
        let out = '';
        for (const part of c.parts) {
            if (part.modifier !== 'none') throw new TypeError(W + 'The pattern has a modifier.');
            if (part.type === 'fixed-text') { out += part.value; continue; }
            if (part.type !== 'segment-wildcard' || !Object.hasOwn(groups, part.name)) throw new TypeError(W + `No value for the group '${part.name}'.`);
            let v;
            try { v = c.encode(String(groups[part.name]).toWellFormed()); } catch (e) { throw new TypeError(W + 'Invalid group value.'); }
            if (!seg.test(v)) throw new TypeError(W + 'Invalid group value.');
            out += part.prefix + v + part.suffix;
        }
        return out;
    }
    /* orders two patterns' component by specificity: fixed text before wildcards, fewer modifiers, then the text */
    static compareComponent(component, left, right) {
        const W = "Failed to execute 'compareComponent' on 'URLPattern': ";
        if (arguments.length < 3) throw new TypeError(W + `3 arguments required, but only ${arguments.length} present.`);
        component = String(component);
        if (!UP_COMPONENTS.includes(component)) throw new TypeError(W + `The provided value '${component}' is not a valid enum value of type URLPatternComponent.`);
        if (!(#c in left) || !(#c in right)) throw new TypeError(W + "parameter is not of type 'URLPattern'.");
        const a = left.#c[component].parts, b = right.#c[component].parts;
        const TYPES = ['full-wildcard', 'regexp', 'segment-wildcard', 'fixed-text'], MODS = ['zero-or-more', 'optional', 'one-or-more', 'none'];
        const EMPTY = { type: 'fixed-text', modifier: 'none', prefix: '', value: '', suffix: '' };
        const cmp = (x, y) => x < y ? -1 : x > y ? 1 : 0;
        for (let i = 0; i < Math.max(a.length, b.length); i++) {
            const x = a[i] || EMPTY, y = b[i] || EMPTY;
            const r = cmp(TYPES.indexOf(x.type), TYPES.indexOf(y.type)) || cmp(MODS.indexOf(x.modifier), MODS.indexOf(y.modifier)) ||
                cmp(x.prefix, y.prefix) || cmp(x.value, y.value) || cmp(x.suffix, y.suffix);
            if (r) return r;
        }
        return 0;
    }
}
Object.defineProperty(URLPattern.prototype, Symbol.toStringTag, { value: 'URLPattern', configurable: true });
