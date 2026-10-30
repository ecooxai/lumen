const kebabCache = new SMap();
const camelToKebab = (p) => {
    let r = kebabCache.get(p);
    if (r !== undefined) return r;
    r = p.startsWith('--') ? p : p === 'cssFloat' ? 'float' : p.replace(/^(webkit|moz|ms)(?=[A-Z])/, '-$1').replace(/[A-Z]/g, c => '-' + c.toLowerCase());
    if (kebabCache.size < 4096) kebabCache.set(p, r);
    return r;
};
/* Pending substitution has one grammar in the native declaration parser and
 * CSSOM. Ordinary values avoid crossing into the tokenizer. */
function cssVarSyntax(value) { return !/var|\\/i.test(value) || N.cssVarValid(value); }
function parseDecls(s) {
    const m = new SMap(); if (!s) return m;
    let depth = 0, q = '', colon = -1, name = ''; const parts = [];
    const finish = end => {
        if (colon >= 0) parts.push([name, s.slice(colon + 1, end)]);
        colon = -1; name = '';
    };
    /* Delimiters inside strings, escapes and comments cannot end a declaration.
       Keep value slices intact: custom properties retain the author's comments. */
    for (let i = 0; i < s.length; i++) {
        const c = s[i];
        if (q) {
            if (colon < 0) name += c;
            if (c === '\\' && i + 1 < s.length) {
                if (colon < 0) name += s[i + 1];
                i++;
            } else if (c === q) q = '';
            continue;
        }
        if (c === '/' && s[i + 1] === '*') {
            if (colon < 0) name += ' ';
            const end = s.indexOf('*/', i + 2);
            i = end < 0 ? s.length : end + 1;
            continue;
        }
        if (c === '\\') {
            if (colon < 0) name += s.slice(i, i + 2);
            i++;
            continue;
        }
        if (c === ';' && !depth) { finish(i); continue; }
        if (c === ':' && !depth && colon < 0) { colon = i; continue; }
        if (colon < 0) name += c;
        if (c === '"' || c === "'") q = c;
        else if (c === '(') depth++;
        else if (c === ')') depth--;
    }
    finish(s.length);
    for (const [rawName, rawValue] of parts) { const k = rawName.trim(); let v = rawValue.trim(); if (!k) continue; let imp = ''; if (/!important\s*$/i.test(v)) { v = v.replace(/\s*!important\s*$/i, ''); imp = 'important'; } const lk = k.startsWith('--') ? k : k.toLowerCase(); if (!cssVarSyntax(v)) continue; if (cssGridRelevant(lk)) { cssGridSet(m, lk, v, imp, true); continue; } if (cssFlexRelevant(lk)) { cssFlexSet(m, lk, v, imp, true); continue; } if ((lk === 'font' || lk === 'font-family' || (v[0] === '-' && MATH_LEN_PROPS.test(lk)) || /^background-position(?:-[xy])?$/.test(lk)) && !mathValid(lk, v)) continue; if (CSS_POSITION.test(lk)) { v = specNormalize(lk, v); const old = m.get(lk); if (old && old[1] && !imp) continue; m.delete(lk); } m.set(lk, [v, imp]); }
    return m;
}
/* Flex declarations are stored as their longhands, so enumeration, removal,
   priority and later longhand assignments all operate on the same values.
   Pending substitution retains its shorthand token stream until resolution. */
const CSS_FLEX_PARTS = ['flex-grow', 'flex-shrink', 'flex-basis'];
const cssFlexRelevant = p => p === 'flex' || CSS_FLEX_PARTS.includes(p);
const cssFlexWide = v => /^(inherit|initial|unset|revert|revert-layer)$/i.test(v);
function cssFlexNumber(v) {
    if (/^[+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:e[+-]?\d+)?$/i.test(v))
        return Number.isFinite(+v) && +v >= 0 ? String(+v) : null;
    const [type, end] = mathType(v);
    if (type !== '' || end !== v.length || !/^[a-z-]+\(/i.test(v)) return null;
    const folded = mathEval(v);
    return folded && !folded.u ? mathSer(folded) : specNormalize('flex-grow', v);
}
function cssFlexBasis(v) {
    if (/^(auto|content|min-content|max-content|fit-content|stretch)$/i.test(v)) return v.toLowerCase();
    const [type, end] = mathType(v), literal = /^[+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:e[+-]?\d+)?(?:%|[a-z]+)?$/i.test(v);
    if ((!literal && !/^[a-z-]+\(/i.test(v)) || end !== v.length || !(type === 'length^1' || type === '%^1' || type === '' && literal && +v === 0)) return null;
    if (literal && parseFloat(v) < 0) return null;
    if (type === '') return '0px';
    const folded = /^[a-z-]+\(/i.test(v) && mathEval(v);
    return folded ? mathSer(folded) : specNormalize('flex-basis', v);
}
function cssFlexParse(p, value) {
    const raw = String(value).replace(/\/\*[\s\S]*?\*\//g, ' ').trim();
    if (!raw) return null;
    if (cssFlexWide(raw)) { const v = raw.toLowerCase(); return p === 'flex' ? { values: [v, v, v] } : { value: v }; }
    if (/\b(?:var|env|attr)\(/i.test(raw)) return { value: raw };
    if (p !== 'flex') { const v = p === 'flex-basis' ? cssFlexBasis(raw) : cssFlexNumber(raw); return v === null ? null : { value: v }; }
    if (/^none$/i.test(raw)) return { values: ['0', '0', 'auto'] };
    const tokens = specSplit(raw);
    if (!tokens.length || tokens.length > 3) return null;
    let grow = null, shrink = null, basis = null, numberEnd = -1;
    for (let i = 0; i < tokens.length; i++) {
        const token = tokens[i], n = cssFlexNumber(token);
        if (n !== null && grow === null) { grow = n; numberEnd = i; continue; }
        if (n !== null && shrink === null && i === numberEnd + 1) { shrink = n; numberEnd = i; continue; }
        const b = cssFlexBasis(token);
        if (basis !== null || b === null) return null;
        basis = b;
    }
    return { values: [grow ?? '1', shrink ?? '1', basis ?? '0%'] };
}
function cssFlexValue(m, p, priority = false) {
    const direct = m.get(p);
    if (p !== 'flex') return direct ? direct[priority ? 1 : 0] : priority ? m.get('flex')?.[1] || '' : '';
    if (direct) return CSS_FLEX_PARTS.some(name => m.has(name)) ? '' : direct[priority ? 1 : 0];
    const entries = CSS_FLEX_PARTS.map(name => m.get(name));
    if (entries.some(e => !e) || entries.some(e => e[1] !== entries[0][1])) return '';
    if (priority) return entries[0][1];
    const values = entries.map(e => e[0]);
    if (values.some(cssFlexWide)) return values.every(v => v === values[0]) ? values[0] : '';
    return values.some(v => /\b(?:var|env|attr)\(/i.test(v)) ? '' : values.join(' ');
}
function cssFlexSet(m, p, value, priority, cascading = false) {
    const parsed = cssFlexParse(p, value);
    if (!parsed) return false;
    if (p === 'flex') {
        if (cascading && m.get(p)?.[1] && !priority) return true;
        m.delete(p);
        if (!parsed.values) for (const name of CSS_FLEX_PARTS) {
            if (!cascading || !m.get(name)?.[1] || priority) m.delete(name);
        }
    }
    const names = parsed.values ? CSS_FLEX_PARTS : [p], values = parsed.values || [parsed.value];
    for (let i = 0; i < names.length; i++) {
        const old = m.get(names[i]);
        if (cascading && old?.[1] && !priority) continue;
        m.set(names[i], [values[i], priority]);
    }
    return true;
}
function cssFlexRemove(m, p) {
    const old = cssFlexValue(m, p);
    m.delete(p);
    if (p === 'flex') for (const name of CSS_FLEX_PARTS) m.delete(name);
    return old;
}
function serDecls(m) {
    /* Coalescing longhands must not move a declaration across all: it is
       itself cascaded at its original position and can reset flex components. */
    let flexSeen = false, allAfterFlex = false, mergeFlex = true;
    for (const name of m.keys()) {
        if (CSS_FLEX_PARTS.includes(name)) { if (allAfterFlex) mergeFlex = false; flexSeen = true; }
        else if (name === 'all' && flexSeen) allAfterFlex = true;
    }
    const out = [], flex = mergeFlex ? cssFlexValue(m, 'flex') : '';
    let flexWritten = false;
    for (const [k, [v, i]] of m) {
        if (CSS_FLEX_PARTS.includes(k) && flex && !m.has('flex')) {
            if (!flexWritten) out.push('flex: ' + flex + (i ? ' !important' : '') + ';');
            flexWritten = true;
        } else out.push(k + ': ' + v + (i ? ' !important' : '') + ';');
    }
    return out.join(' ');
}
/* a longhand only set through a shorthand in this declaration block: its share of the shorthand's value */
const SHORTHAND_OF = { 'align-items': 'place-items', 'justify-items': 'place-items', 'align-self': 'place-self', 'justify-self': 'place-self', 'align-content': 'place-content', 'justify-content': 'place-content',
    'row-gap': 'gap', 'column-gap': 'gap', 'overflow-x': 'overflow', 'overflow-y': 'overflow', 'top': 'inset', 'right': 'inset', 'bottom': 'inset', 'left': 'inset',
    'margin-top': 'margin', 'margin-right': 'margin', 'margin-bottom': 'margin', 'margin-left': 'margin', 'padding-top': 'padding', 'padding-right': 'padding', 'padding-bottom': 'padding', 'padding-left': 'padding',
    'margin-inline-start': 'margin-inline', 'margin-inline-end': 'margin-inline', 'margin-block-start': 'margin-block', 'margin-block-end': 'margin-block',
    'padding-inline-start': 'padding-inline', 'padding-inline-end': 'padding-inline', 'padding-block-start': 'padding-block', 'padding-block-end': 'padding-block',
    'inset-inline-start': 'inset-inline', 'inset-inline-end': 'inset-inline', 'inset-block-start': 'inset-block', 'inset-block-end': 'inset-block',
    'animation-range-start': 'animation-range', 'animation-range-end': 'animation-range' };
/* one animation-range item: start [end]; an omitted end takes the start's range name at 100%, else normal */
function animRangePart(v, end) {
    const t = tomSplitTop(v, ' ').filter(Boolean), isName = w => /^[a-z][\w-]*$/i.test(w) && w !== 'normal';
    const take = () => { if (!t.length) return null; const w = t.shift(); return isName(w) && t.length && !isName(t[0]) && t[0] !== 'normal' ? w + ' ' + t.shift() : w; };
    const a = take(), b = take() ?? (isName(a.split(' ')[0]) ? a.split(' ')[0] : 'normal');
    return end ? b.replace(/ 100%$/, '') : a.replace(/ 0%$/, '');
}
function shorthandPart(m, p) {
    const sh = SHORTHAND_OF[p], e = sh && m.get(sh);
    if (!e) return '';
    const v = e[0].trim();
    if (/var\(/i.test(v)) return '';
    if (/^(inherit|initial|unset|revert|revert-layer)$/i.test(v)) return v;
    if (sh === 'animation-range') return tomSplitTop(v, ',').map(x => animRangePart(x.trim(), p.endsWith('-end'))).join(', ');
    const t = tomSplitTop(v, ' ').filter(Boolean);
    if (sh.startsWith('place-')) {
        const first = p.startsWith('align-');
        let a = t, two = [];
        for (let i = 0; i < a.length; i++) { const w = a[i]; if ((w === 'first' || w === 'last' || w === 'safe' || w === 'unsafe' || w === 'legacy') && i + 1 < a.length) { two.push(w + ' ' + a[++i]); } else two.push(w); }
        if (two.length > 2 || !two.length) return '';
        const al = two[0].replace(/^first /, ''), ju = two.length > 1 ? two[1].replace(/^first /, '') : (sh === 'place-content' && /baseline/.test(two[0]) ? 'start' : al);
        return first ? al : ju;
    }
    if (sh === 'gap' || sh === 'overflow') { if (t.length > 2) return ''; return p === 'row-gap' || p === 'overflow-x' ? t[0] : t[t.length - 1]; }
    if (/^(margin|padding|inset)-(inline|block)$/.test(sh)) {
        if (!t.length || t.length > 2) return '';
        return p.endsWith('-start') ? t[0] : t[t.length - 1];
    }
    if (t.length > 4) return '';
    const side = ['top', 'right', 'bottom', 'left'].indexOf(p.replace(/^(margin|padding)-/, ''));
    const idx = [[0, 0, 0, 0], [0, 1, 0, 1], [0, 1, 2, 1], [0, 1, 2, 3]][t.length - 1][side];
    return t[idx];
}
/* specified-value canonicalisation: numbers as CSS serializes them, shortest 2- and 4-value shorthand forms */
const SPEC_PAIR = new SSet(['gap', 'grid-gap', 'place-items', 'place-self', 'place-content', 'overflow', 'overscroll-behavior', 'border-spacing', 'scroll-snap-align', 'contain-intrinsic-size', 'margin-block', 'margin-inline', 'padding-block', 'padding-inline', 'inset-block', 'inset-inline', 'scroll-margin-block', 'scroll-margin-inline', 'scroll-padding-block', 'scroll-padding-inline', 'border-block-width', 'border-inline-width', 'border-block-style', 'border-inline-style', 'border-block-color', 'border-inline-color']);
const SPEC_BOX = new SSet(['margin', 'padding', 'inset', 'border-width', 'border-style', 'border-color', 'scroll-margin', 'scroll-padding', 'border-image-outset', 'border-image-width']);
/* constant folding of math functions whose arguments are all numbers or same-type dimensions (Chrome folds these when parsing) */
const MATH_UNIT = { deg: ['deg', 1], rad: ['deg', 180 / Math.PI], grad: ['deg', 0.9], turn: ['deg', 360], s: ['s', 1], ms: ['s', 0.001], px: ['px', 1], '': ['', 1] };
function mathEval(src) {
    let i = 0;
    const ws = () => { while (/\s/.test(src[i])) i++; };
    const fail = () => { throw 0; };
    const same = (a, b) => { if (a.u !== b.u) fail(); return a.u; };
    function prim() {
        ws();
        if (src[i] === '(') { i++; const r = sum(); ws(); if (src[i++] !== ')') fail(); return r; }
        const m = /^([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)([a-z]*)/i.exec(src.slice(i));
        if (m) { const u = MATH_UNIT[m[2].toLowerCase()]; if (!u) fail(); i += m[0].length; return { v: +m[1] * u[1], u: u[0] }; }
        const id = /^(-?)([a-z][a-z0-9-]*)/i.exec(src.slice(i)); if (!id) fail();
        const name = id[2].toLowerCase(); i += id[0].length;
        const neg = id[1] ? -1 : 1;
        const K = { pi: Math.PI, e: Math.E, infinity: Infinity, nan: NaN };
        if (src[i] !== '(') { if (!(name in K)) fail(); return { v: neg * K[name], u: '' }; }
        i++; const args = [sum()]; ws();
        while (src[i] === ',') { i++; args.push(sum()); ws(); }
        if (src[i++] !== ')') fail();
        const n = a => { if (a.u) fail(); return a.v; }, ang = a => a.u === 'deg' ? a.v * Math.PI / 180 : n(a), D = r => ({ v: r * 180 / Math.PI, u: 'deg' });
        const F = {
            calc: () => args[0], sin: () => ({ v: Math.sin(ang(args[0])), u: '' }), cos: () => ({ v: Math.cos(ang(args[0])), u: '' }), tan: () => ({ v: Math.tan(ang(args[0])), u: '' }),
            asin: () => D(Math.asin(n(args[0]))), acos: () => D(Math.acos(n(args[0]))), atan: () => D(Math.atan(n(args[0]))),
            atan2: () => { same(args[0], args[1]); return D(Math.atan2(args[0].v, args[1].v)); },
            sqrt: () => ({ v: Math.sqrt(n(args[0])), u: '' }), exp: () => ({ v: Math.exp(n(args[0])), u: '' }), pow: () => ({ v: Math.pow(n(args[0]), n(args[1])), u: '' }),
            log: () => ({ v: args.length > 1 ? Math.log(n(args[0])) / Math.log(n(args[1])) : Math.log(n(args[0])), u: '' }),
        };
        if (!F[name] || (name !== 'atan2' && name !== 'pow' && name !== 'log' && args.length !== 1)) fail();
        const r = F[name](); r.v *= neg; return r;
    }
    function prod() {
        let a = prim();
        for (;;) { ws(); const c = src[i]; if (c !== '*' && c !== '/') return a; i++; const b = prim();
            if (c === '*') { if (a.u && b.u) fail(); a = { v: a.v * b.v, u: a.u || b.u }; } else { if (b.u) fail(); a = { v: a.v / b.v, u: a.u }; } }
    }
    function sum() {
        let a = prod();
        for (;;) { ws(); const c = src[i]; if ((c !== '+' && c !== '-') || !/\s/.test(src[i - 1])) return a; i++; const b = prod(); same(a, b); a = { v: c === '+' ? a.v + b.v : a.v - b.v, u: a.u }; }
    }
    try { const r = sum(); ws(); return i === src.length ? r : null; } catch (e) { return null; }
}
const mathSer = r => {
    const u = r.u || '';
    if (Number.isNaN(r.v)) return `calc(NaN${u ? ' * 1' + u : ''})`;
    if (!isFinite(r.v)) return `calc(${r.v < 0 ? '-' : ''}infinity${u ? ' * 1' + u : ''})`;
    return `calc(${String(+r.v.toPrecision(6)).replace(/^-0$/, '0')}${u})`;
};
function specFoldMath(t) {
    return t.replace(/\b(?:sin|cos|tan|asin|acos|atan|atan2|sqrt|exp|pow|log)\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)/gi, m => { const r = mathEval(m); return r ? mathSer(r) : m; })
        .replace(/\bcalc\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)/gi, m => { const r = /calc\(.*calc\(/i.test(m) && mathEval(m); return r ? mathSer(r) : m; });
}
/* CSS math functions (calc, min/max/clamp, round/mod/rem, trig, ...): a type check. Types are '' (number), '%',
   or a dimension kind; length + % is 'length%'. An unknown function, unit or var() makes the result unknown (no
   check). Number properties need a number (opacity-like also %), length properties a length or percentage. */
const MATH_UNITS = (() => {
    const m = new SMap();
    for (const u of 'px em rem ex rex ch rch cap rcap ic ric lh rlh vw vh vmin vmax vi vb svw svh svi svb svmin svmax lvw lvh lvi lvb lvmin lvmax dvw dvh dvi dvb dvmin dvmax cqw cqh cqi cqb cqmin cqmax cm mm q in pt pc'.split(' ')) m.set(u, 'length');
    for (const u of ['deg', 'rad', 'grad', 'turn']) m.set(u, 'angle');
    for (const u of ['s', 'ms']) m.set(u, 'time');
    for (const u of ['hz', 'khz']) m.set(u, 'frequency');
    for (const u of ['dpi', 'dpcm', 'dppx', 'x']) m.set(u, 'resolution');
    m.set('fr', 'flex');
    return m;
})();
const MATH_FNS = /^(calc|-webkit-calc|min|max|clamp|round|mod|rem|abs|sign|sin|cos|tan|asin|acos|atan|atan2|pow|sqrt|hypot|log|exp)$/;
function mathType(src, strictUnits = false) {   /* returns [type | null (invalid) | undefined (unknown), end] */
    let i = 0, unknown = false;
    const ws = () => { const s = i; while (/\s/.test(src[i] || '')) i++; return i > s; };
    const dec = t => { const m = {}; if (t) for (const e of t.split(',')) { const [d, n] = e.split('^'); m[d] = +n; } return m; };
    const enc = m => Object.keys(m).filter(d => m[d]).sort().map(d => d + '^' + m[d]).join(',');
    const mul = (a, b, sg) => { if (a === 'none' || b === 'none') return null; if (a === '?' || b === '?') return '?'; const m = dec(a), n = dec(b); for (const d in n) m[d] = (m[d] || 0) + sg * n[d]; return enc(m); };
    const pl = t => { const m = dec(t); if (m['%']) { m.length = (m.length || 0) + m['%']; delete m['%']; } return enc(m); };
    const add = (a, b) => {
        if (a === null || b === null || a === 'none' || b === 'none') return null;
        if (a === '?' || b === '?') return '?';
        if (a === b) return a;
        return pl(a) === pl(b) && pl(a) === 'length^1' ? 'length^1' : null;
    };
    function sum() {
        let t = prod(); if (t === null) return null;
        for (;;) {
            const s = i, w = ws();
            if ((src[i] === '+' || src[i] === '-') && w && /\s/.test(src[i + 1] || '')) { i++; ws(); t = add(t, prod()); if (t === null) return null; continue; }
            i = s; return t;
        }
    }
    function prod() {
        let t = val(); if (t === null) return null;
        for (;;) {
            const s = i; ws();
            const op = src[i];
            if (op !== '*' && op !== '/') { i = s; return t; }
            i++; ws(); const u = val(); if (u === null) return null;
            t = mul(t, u, op === '*' ? 1 : -1); if (t === null) return null;
        }
    }
    function args() {   /* comma list of sums up to ')' */
        const r = []; ws();
        if (src[i] === ')') { i++; return r; }
        for (;;) {
            ws(); const t = sum(); if (t === null) return null; r.push(t); ws();
            if (src[i] === ',') { i++; continue; }
            if (src[i] === ')' || i >= src.length) { i++; return r; }   /* end of input closes it */
            return null;
        }
    }
    function fn(name) {
        name = name.toLowerCase();
        if (!MATH_FNS.test(name)) {   /* not a math function: skip its balanced arguments, type unknown */
            let d = 1; while (i < src.length && d) { if (src[i] === '(') d++; else if (src[i] === ')') d--; i++; }
            unknown = true; return '?';
        }
        let strat = false;
        if (name === 'round') { const m = /^\s*(nearest|up|down|to-zero)\s*,/i.exec(src.slice(i)); if (m) { i += m[0].length; strat = true; } }
        const a = args(); if (a === null || name !== 'clamp' && a.includes('none')) return null;
        const same = () => a.reduce((x, y) => x === '?' || y === '?' ? '?' : x === null ? null : add(x, y));
        const num = t => t === '' || t === '?';
        switch (name) {
        case 'calc': case '-webkit-calc': case 'abs': return a.length === 1 ? a[0] : null;
        case 'min': case 'max': case 'hypot': return a.length ? same() : null;
        case 'clamp':
            if (a.length !== 3 || a[1] === 'none') return null;
            if (a[0] === 'none') a[0] = a[1];
            if (a[2] === 'none') a[2] = a[1];
            return same();
        case 'round': if (a.length === 1) return strat || num(a[0]) || a[0] === '?' ? (num(a[0]) ? a[0] : null) : null;
            return a.length === 2 ? same() : null;
        case 'mod': case 'rem': return a.length === 2 ? same() : null;
        case 'sign': return a.length === 1 ? '' : null;
        case 'sin': case 'cos': case 'tan': return a.length === 1 && (num(a[0]) || a[0] === 'angle^1') ? '' : null;
        case 'asin': case 'acos': case 'atan': return a.length === 1 && num(a[0]) ? 'angle^1' : null;
        case 'atan2': return a.length === 2 && same() !== null ? 'angle^1' : null;
        case 'pow': return a.length === 2 && a.every(num) ? '' : null;
        case 'sqrt': case 'exp': return a.length === 1 && num(a[0]) ? '' : null;
        case 'log': return (a.length === 1 || a.length === 2) && a.every(num) ? '' : null;
        }
        return null;
    }
    function val() {
        ws();
        if (src[i] === '(') { i++; ws(); const t = sum(); ws(); if (t === null || (src[i] !== ')' && i < src.length)) return null; i++; return t; }
        let m = /^[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?(%|[a-z]+)?/i.exec(src.slice(i));
        if (m) {
            i += m[0].length;
            if (!m[3]) return '';
            if (m[3] === '%') return '%^1';
            const u = MATH_UNITS.get(m[3].toLowerCase());
            if (u === undefined) { if (strictUnits) return null; unknown = true; return '?'; }
            return u + '^1';
        }
        m = /^-?[a-z_][\w-]*/i.exec(src.slice(i));
        if (!m) return null;
        i += m[0].length;
        if (src[i] === '(') { i++; return fn(m[0]); }
        if (/^(-?infinity|pi|e|nan)$/i.test(m[0])) return '';
        if (strictUnits) return /^none$/i.test(m[0]) ? 'none' : null;
        unknown = true; return '?';   /* e.g. a relative color's channel keyword */
    }
    const t = val();
    return [strictUnits && t === null ? null : unknown || t === '?' ? undefined : t, i];
}
/* Angle-bearing transform arguments need their own expected type: a nested
   min()/max() is not a whole-property expression. Other transform components
   keep their existing validation, including deferred var()/env()/attr(). */
function transformAnglesValid(value) {
    // Escaped tokens retain the existing parser path until token-level decoding.
    if (value.includes(String.fromCharCode(92))) return true;
    const src = value.includes('/*') ? value.replace(/\/\*[\s\S]*?(?:\*\/|$)/g, ' ') : value;
    const re = /(^|[\s)])(rotate(?:3d|[xyz])?|skew(?:[xy])?)\(/gi;
    let m;
    while ((m = re.exec(src))) {
        const start = re.lastIndex; let i = start, depth = 1;
        while (i < src.length && depth) { if (src[i] === '(') depth++; else if (src[i] === ')') depth--; i++; }
        const body = src.slice(start, depth ? src.length : i - 1), args = [];
        let part = 0, nested = 0;
        for (let j = 0; j < body.length; j++) {
            if (body[j] === '(') nested++;
            else if (body[j] === ')') nested--;
            else if (body[j] === ',' && !nested) { args.push(body.slice(part, j).trim()); part = j + 1; }
        }
        args.push(body.slice(part).trim());
        const name = m[2].toLowerCase(), first = name === 'rotate3d' ? 3 : 0;
        if (name === 'rotate3d' ? args.length !== 4 : name === 'skew' ? args.length < 1 || args.length > 2 : args.length !== 1) return false;
        for (let j = first; j < args.length; j++) {
            const arg = args[j];
            // The legacy unitless-zero exception does not apply inside math.
            if (/^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?$/i.test(arg) && Number(arg) === 0) continue;
            const [type, end] = mathType(arg, true);
            if (arg.slice(end).trim() || type !== undefined && type !== 'angle^1') return false;
        }
        re.lastIndex = Math.max(start, i - 1);
    }
    return true;
}
const MATH_LEN_PROPS = /^((min-|max-)?(width|height|block-size|inline-size))$/;
/* Canonical nonnegative px sizes below 1e9 with at most six fractional digits
   already have Lumen's existing specified serialization. Other values keep the full path. */
const CSS_CANONICAL_PX_SIZE = /^(?:0|[1-9]\d{0,8})(?:\.\d{0,5}[1-9])?px$/;
const MATH_NUM_PROPS = /^((fill-|stroke-|stop-|flood-)?opacity|flex-grow|flex-shrink)$/;
/* the <color-interpolation-method> of gradients: only "in <space> [<hue> hue]" at the start or end of the first argument */
const GRAD_SPACES = /^(srgb|srgb-linear|display-p3|a98-rgb|prophoto-rgb|rec2020|lab|oklab|xyz|xyz-d50|xyz-d65|hsl|hwb|lch|oklch)$/i;
function gradInterpValid(v) {
    const re = /\b((?:repeating-)?(?:linear|radial|conic)-gradient)\(/gi; let m;
    while ((m = re.exec(v))) {
        let d = 1, i = re.lastIndex; const s = i;
        for (; i < v.length && d; i++) { if (v[i] === '(') d++; else if (v[i] === ')') d--; }
        const args = tomSplitTop(v.slice(s, i - 1), ',').map(x => x.trim());
        if (!args[0]) return false;
        for (let k = 1; k < args.length; k++) { const w = args[k].split(/\s+/); if (w.some(x => /^(in|hue)$/i.test(x)) || GRAD_SPACES.test(w[0])) return false; }
        const w = tomSplitTop(args[0], ' ').map(x => x.trim()).filter(Boolean), at = w.findIndex(x => /^in$/i.test(x));
        if (at < 0) { if (w.some(x => GRAD_SPACES.test(x) || /^hue$/i.test(x))) return false; continue; }
        const sp = w[at + 1]; if (!sp || !GRAD_SPACES.test(sp)) return false;
        let end = at + 2;
        if (w[end] && /^hue$/i.test(w[end + 1] || '')) { if (!/^(hsl|hwb|lch|oklch)$/i.test(sp) || !/^(shorter|longer|increasing|decreasing)$/i.test(w[end])) return false; end += 2; }
        const rest = w.slice(0, at).concat(w.slice(end));
        if (at > 0 && end < w.length) return false;
        if (rest.some(x => /^(in|hue|shorter|longer|increasing|decreasing|specified)$/i.test(x) || GRAD_SPACES.test(x))) return false;
    }
    return true;
}
/* position-area: null when invalid, else Chrome's serialization (physical/logical pairs in a fixed order,
   a redundant 'span-all' or repeated keyword dropped) */
const PA_G = (() => {
    const m = new Map(), add = (g, ks) => ks.forEach(k => m.set(k, g));
    add('H', ['left', 'right', 'span-left', 'span-right', 'x-start', 'x-end', 'span-x-start', 'span-x-end', 'self-x-start', 'self-x-end', 'span-self-x-start', 'span-self-x-end']);
    add('V', ['top', 'bottom', 'span-top', 'span-bottom', 'y-start', 'y-end', 'span-y-start', 'span-y-end', 'self-y-start', 'self-y-end', 'span-self-y-start', 'span-self-y-end']);
    add('I', ['inline-start', 'inline-end', 'span-inline-start', 'span-inline-end']);
    add('B', ['block-start', 'block-end', 'span-block-start', 'span-block-end']);
    add('SI', ['self-inline-start', 'self-inline-end', 'span-self-inline-start', 'span-self-inline-end']);
    add('SB', ['self-block-start', 'self-block-end', 'span-self-block-start', 'span-self-block-end']);
    add('SE', ['start', 'end', 'span-start', 'span-end']);
    add('SSE', ['self-start', 'self-end', 'span-self-start', 'span-self-end']);
    add('A', ['center', 'span-all']);
    return m;
})();
function posArea(v) {
    const t = v.trim().toLowerCase().split(/\s+/);
    if (t.length === 1) return t[0] === 'none' || PA_G.has(t[0]) ? t[0] : null;
    if (t.length !== 2) return null;
    const [a, b] = t, ga = PA_G.get(a), gb = PA_G.get(b);
    if (!ga || !gb) return null;
    if (ga === 'A' && gb === 'A') return a === b ? a : a + ' ' + b;
    if (ga === 'A' || gb === 'A') {
        const [x, gx, y] = ga === 'A' ? [b, gb, a] : [a, ga, b];
        if (gx === 'SE' || gx === 'SSE') return a + ' ' + b;
        if (y === 'span-all') return x;
        return gx === 'H' || gx === 'B' || gx === 'SB' ? x + ' center' : 'center ' + x;
    }
    if (ga === gb && (ga === 'SE' || ga === 'SSE')) return a === b ? a : a + ' ' + b;
    const P = { H: 'V', B: 'I', SB: 'SI' };
    return P[ga] === gb ? a + ' ' + b : P[gb] === ga ? b + ' ' + a : null;
}
/* a <family-name>#: each item one string or a run of identifiers, none of them a CSS-wide keyword or 'default' */
function familyValid(v) {
    const items = []; let cur = '', q = '';
    for (let i = 0; i < v.length; i++) {
        const c = v[i];
        if (q) { cur += c; if (c === '\\') cur += v[++i] || ''; else if (c === q) q = ''; continue; }
        if (c === '"' || c === "'") { q = c; cur += c; continue; }
        if (c === ',') { items.push(cur); cur = ''; continue; }
        cur += c;
    }
    if (q) return false;
    items.push(cur);
    const BAD = /^(inherit|initial|unset|default|revert|revert-layer)$/i, ID = /^(-?([a-z_\u0080-\uffff]|\\.)|--)([\w\u0080-\uffff-]|\\.)*$/i;
    return items.every(it => {
        it = it.trim(); if (!it) return false;
        if (it[0] === '"' || it[0] === "'") return it.length > 1 && it[it.length - 1] === it[0] && !/^(["']).*\1.+/.test(it);
        const w = it.split(/\s+/);
        return w.every(x => ID.test(x)) && !(w.length === 1 && BAD.test(w[0]));
    });
}
const FONT_PRE = /^(normal|italic|oblique|small-caps|bold|bolder|lighter|[1-9]00|ultra-condensed|extra-condensed|condensed|semi-condensed|semi-expanded|expanded|extra-expanded|ultra-expanded)$/i;
const FONT_SIZE = /^(xx-small|x-small|small|medium|large|x-large|xx-large|xxx-large|smaller|larger|[+]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?([a-z]+|%)|0)$/i;
function fontFamilyPart(v) {   /* the family list of a font shorthand, or null when no size is found */
    const re = /[^\s\/]+/g; let m;
    while ((m = re.exec(v))) {
        if (FONT_SIZE.test(m[0])) {
            const lh = /^\s*\/\s*([a-z-]+\((?:[^()]|\([^()]*\))*\)|[^\s\/]+)/i.exec(v.slice(re.lastIndex)), rest = v.slice(re.lastIndex + (lh ? lh[0].length : 0));
            return /^\s/.test(rest) ? rest : null;
        }
        if (!FONT_PRE.test(m[0])) return null;
    }
    return null;
}
function mathValid(p, v) {
    if (!cssVarSyntax(v)) return false;
    if (cssFlexRelevant(p)) return cssFlexParse(p, v) !== null;
    if (cssGridRelevant(p)) return cssGridParse(p, v) !== null;
    /* Only a leading minus, whitespace or non-ASCII prefix can become a
       negative literal after trim(). Ordinary positive sizes need no scan. */
    if ((v[0] === '-' || v[0] <= ' ' || v[0] > '\x7f') && MATH_LEN_PROPS.test(p)) {
        const negative = /^-((?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)(?:[a-z]+|%)?$/i.exec(v.trim());
        if (negative && +negative[1] > 0) return false;
    }
    if ((p === 'font-family' || p === 'font') && !/var\(|env\(|attr\(/i.test(v) && !/^\s*(inherit|initial|unset|revert|revert-layer)\s*$/i.test(v)) {
        if (p === 'font-family') { if (!familyValid(v)) return false; }
        else if (!/^\s*(caption|icon|menu|message-box|small-caption|status-bar)\s*$/i.test(v)) { const f = fontFamilyPart(v); if (f !== null && !familyValid(f)) return false; }
    }
    if (p === 'position-area' && !/var\(|env\(|attr\(/i.test(v) && !/^\s*(inherit|initial|unset|revert|revert-layer)\s*$/i.test(v)) return posArea(v) !== null;
    /* css-ui-4 appearance: none | auto | base | base-select | <compat-auto> | <compat-special> */
    if ((p === 'appearance' || p === '-webkit-appearance') && !/var\(|env\(|attr\(/i.test(v))
        return /^\s*(inherit|initial|unset|revert|revert-layer|none|auto|base|base-select|textfield|menulist-button|button|checkbox|listbox|menulist|meter|progress-bar|radio|searchfield|textarea)\s*$/i.test(v);
    if (/-gradient\(/i.test(v) && !/var\(|env\(|attr\(/i.test(v) && !gradInterpValid(v)) return false;
    if (/var\(|env\(|attr\(|calc-size\(|anchor/i.test(v)) return true;
    if (p === 'transform' && !transformAnglesValid(v)) return false;
    if (/^background-position(?:-[xy])?$/.test(p) && !/^\s*(inherit|initial|unset|revert|revert-layer)\s*$/i.test(v)) return cssPositionParse(v, cssPositionAxis(p)) !== null;
    const re = /(^|[\s,(/])((?:-webkit-)?[a-z][a-z0-9-]*)\(/gi;
    let m;
    while ((m = re.exec(v))) {
        if (!MATH_FNS.test(m[2].toLowerCase())) continue;
        const st = m.index + m[1].length, [t, n] = mathType(v.slice(st));
        if (t === null) return false;
        re.lastIndex = st + Math.max(n, 1);
        if (t === undefined || st !== 0 && v.slice(0, st).trim()) continue;   /* only a whole-value function is checked against the property */
        if (MATH_NUM_PROPS.test(p) && !(t === '' || (t === '%^1' && /opacity$/.test(p)))) return false;
        if (MATH_LEN_PROPS.test(p) && n >= v.trim().length && !(t === 'length^1' || t === '%^1')) return false;
    }
    return true;
}
/* The background-position grammar is shared by declaration validation,
   specified-value serialization, and animation. A component retains its edge
   and optional offset; computed animation values normalize those separately. */
const CSS_POSITION = /^background-position(?:-[xy])?$/;
const cssPositionAxis = p => p.endsWith('-x') ? 0 : p.endsWith('-y') ? 1 : -1;
function cssPositionLength(text) {
    const [type, end] = mathType(text);
    return end === text.length && (type === 'length^1' || type === '%^1' || type === '' && /^[+-]?(?:0+(?:\.0*)?|\.0+)$/.test(text));
}
function cssPositionParse(value, axis = -1, maxLayers = Infinity) {
    const layers = tomSplitTop(String(value).replace(/\/\*[\s\S]*?\*\//g, ' '), ',');
    if (!layers.length) return null;
    const out = [], keyAxis = key => /^(left|right|x-start|x-end)$/.test(key) ? 0 : /^(top|bottom|y-start|y-end)$/.test(key) ? 1 : -1;
    const key = text => /^(left|right|top|bottom|center|[xy]-(start|end))$/i.test(text) ? text.toLowerCase() : null;
    const component = (text, ax, logical) => {
        const k = key(text);
        if (k && (k === 'center' || keyAxis(k) === ax) && (logical || !/^[xy]-/.test(k))) return { keyword: k, offset: null };
        return !k && cssPositionLength(text) ? { keyword: null, offset: text } : null;
    };
    for (const layer of layers) {
        const t = specSplit(layer.trim()), pair = [null, null];
        if (!t.length) return null;
        if (axis >= 0) {
            if (t.length === 1) pair[axis] = component(t[0], axis, true);
            else if (t.length === 2 && keyAxis(key(t[0])) === axis && cssPositionLength(t[1])) pair[axis] = { keyword: key(t[0]), offset: t[1] };
            if (!pair[axis]) return null;
        } else if (t.length === 1) {
            pair[0] = pair[1] = { keyword: 'center', offset: null };
            const ax = keyAxis(key(t[0])) === 1 ? 1 : 0; pair[ax] = component(t[0], ax, false);
            if (!pair[ax]) return null;
        } else if (t.length === 2) {
            pair[0] = component(t[0], 0, false); pair[1] = component(t[1], 1, false);
            if (!pair[0] || !pair[1]) {
                if (!key(t[0]) || !key(t[1])) return null;
                pair[0] = component(t[1], 0, false); pair[1] = component(t[0], 1, false);
            }
            if (!pair[0] || !pair[1]) return null;
        } else if (t.length === 3 || t.length === 4) {
            let center = 0;
            for (let i = 0; i < t.length; i++) {
                const k = key(t[i]);
                if (k === 'center') { center++; continue; }
                const ax = keyAxis(k);
                if (ax < 0 || /^[xy]-/.test(k) || pair[ax]) return null;
                let offset = null;
                if (i + 1 < t.length && cssPositionLength(t[i + 1])) offset = t[++i];
                pair[ax] = { keyword: k, offset };
            }
            if (center === 1 && !!pair[0] !== !!pair[1]) pair[pair[0] ? 1 : 0] = { keyword: 'center', offset: null };
            else if (center) return null;
            if (!pair[0] || !pair[1]) return null;
        } else return null;
        out.push(pair);
        if (out.length > maxLayers) return null;
    }
    return out;
}
function cssPositionOffsetText(text) {
    if (/^[+-]?(?:0+(?:\.0*)?|\.0+)$/.test(text)) return '0px';
    if (!/^calc\(/i.test(text)) return text;
    try {
        const m = tomSumMap(CSSNumericValue.parse(text));
        if (!m) return text;
        const terms = [...m].filter(([u, n]) => u === 'percent' || n !== 0).sort((a, b) => a[0] === 'percent' ? -1 : b[0] === 'percent' ? 1 : a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0);
        if (!terms.length) return '0px';
        const token = (u, n) => String(+n.toPrecision(12)) + (u === 'percent' ? '%' : u === 'number' ? '' : u);
        let result = token(...terms[0]);
        for (const [u, n] of terms.slice(1)) result += (n < 0 ? ' - ' : ' + ') + token(u, Math.abs(n));
        return 'calc(' + result + ')';
    } catch (_) { return text; }
}
function cssPositionPartText(part, axis, edgeForm) {
    const offset = part.offset === null ? null : cssPositionOffsetText(part.offset);
    if (part.keyword) return part.keyword + (offset === null ? '' : ' ' + offset);
    return (edgeForm ? (axis ? 'top ' : 'left ') : '') + offset;
}
function cssPositionPairText(pair) {
    const edge = pair.some(p => p.keyword && p.offset !== null);
    return pair.map((p, axis) => cssPositionPartText(p, axis, edge)).join(' ');
}
function cssPositionSpecified(value, axis) {
    const parsed = cssPositionParse(value, axis);
    return parsed && parsed.map(pair => axis < 0 ? cssPositionPairText(pair) : cssPositionPartText(pair[axis], axis, false)).join(', ');
}
function cssPositionDeclaration(map, prop, priority = false) {
    const values = [null, null], important = [false, false];
    for (const [name, entry] of map) {
        if (!CSS_POSITION.test(name)) continue;
        const axis = cssPositionAxis(name), wide = /^(inherit|initial|unset|revert|revert-layer)$/i.test(entry[0]);
        /* Pending shorthand values retain their common source identity. Their
           individual axes are not observable until substitution; direct pending
           longhands retain their own token stream. CSS Values 5, substitution. */
        const pending = /\b(?:var|env|attr)\s*\(/i.test(entry[0]);
        const parsed = pending ? { raw: entry[0], shorthand: axis < 0 } : wide ? entry[0].toLowerCase() : cssPositionParse(entry[0], axis);
        if (!parsed) continue;
        for (let ax = 0; ax < 2; ax++) if ((axis < 0 || axis === ax) && (!important[ax] || entry[1])) {
            values[ax] = pending || wide ? parsed : parsed.map(pair => pair[ax]); important[ax] = !!entry[1];
        }
    }
    const axis = cssPositionAxis(prop), present = v => v !== null;
    if (priority) return axis >= 0 ? present(values[axis]) && important[axis] ? 'important' : '' : values.every(present) && important.every(Boolean) ? 'important' : '';
    if (axis >= 0) {
        const value = values[axis];
        return !value ? '' : typeof value === 'string' ? value : !Array.isArray(value) ? value.shorthand ? '' : value.raw : value.map(p => cssPositionPartText(p, axis, false)).join(', ');
    }
    if (!values.every(present) || important[0] !== important[1]) return '';
    const pending = values.some(v => typeof v === 'object' && !Array.isArray(v));
    if (pending) return values[0] === values[1] && values[0].shorthand ? values[0].raw : '';
    if (typeof values[0] === 'string' || typeof values[1] === 'string') return values[0] === values[1] ? values[0] : '';
    if (values[0].length !== values[1].length) return '';
    /* Axis-only logical origins cannot be represented by the full shorthand's
       physical position grammar. Do not emit a value that cannot round-trip. */
    if (values.some(list => list.some(p => p.keyword && /^[xy]-(?:start|end)$/.test(p.keyword)))) return '';
    return values[0].map((p, i) => cssPositionPairText([p, values[1][i]])).join(', ');
}
/* the font shorthand split into its parts: [style || variant || weight || stretch]? size [/ line-height]? family */
const FONT_STRETCH_KW = /^(ultra-condensed|extra-condensed|condensed|semi-condensed|semi-expanded|expanded|extra-expanded|ultra-expanded)$/i;
function fontParts(v) {
    const w = tomSplitTop(v.replace(/\s*\/\s*/g, ' / '), ' ').map(x => x.trim()).filter(Boolean);
    const r = { style: 'normal', variant: 'normal', weight: 'normal', stretch: 'normal', lh: 'normal' }; const seen = new SSet();
    let i = 0, normals = 0;
    for (; i < w.length && i < 4; i++) {
        const k = w[i].toLowerCase(), set = (f, x) => { if (seen.has(f)) return false; seen.add(f); r[f] = x; return true; };
        if (k === 'normal') { normals++; continue; }
        if (/^(italic|oblique)$/.test(k)) { if (!set('style', k)) return null; continue; }
        if (k === 'small-caps') { if (!set('variant', k)) return null; continue; }
        if (/^(bold|bolder|lighter)$/.test(k) || (/^\d+(\.\d+)?$/.test(k) && +k >= 1 && +k <= 1000 && w[i + 1] && w[i + 1] !== '/')) { if (!set('weight', k)) return null; continue; }
        if (FONT_STRETCH_KW.test(k)) { if (!set('stretch', k)) return null; continue; }
        break;
    }
    if (normals + seen.size > 4 || i >= w.length) return null;
    r.size = w[i++];
    if (w[i] === '/') { r.lh = w[i + 1]; i += 2; if (!r.lh) return null; }
    if (i >= w.length || w[i] === '/') return null;
    r.family = w.slice(i).join(' ');
    return r;
}
/* computed font shorthand from the computed longhands; '' when a longhand can't be written in the shorthand */
const FONT_STRETCH_PCT = { 50: 'ultra-condensed', 62.5: 'extra-condensed', 75: 'condensed', 87.5: 'semi-condensed', 100: 'normal', 112.5: 'semi-expanded', 125: 'expanded', 150: 'extra-expanded', 200: 'ultra-expanded' };
function computedFont(cs) {
    const g = k => cs.getPropertyValue(k);
    const size = g('font-size'), family = g('font-family');
    if (!size || !family) return '';
    let st = g('font-stretch') || 'normal';
    if (/%$/.test(st)) { st = FONT_STRETCH_PCT[parseFloat(st)]; if (!st) return ''; }
    const vr = g('font-variant-caps') || g('font-variant') || 'normal';
    if (vr !== 'normal' && vr !== 'small-caps') return '';
    const w = g('font-weight') || '400', lh = g('line-height') || 'normal';
    return [g('font-style') || 'normal', vr, w === '400' ? 'normal' : w, st].filter(x => x !== 'normal').concat(lh === 'normal' ? size : `${size} / ${lh}`, family).join(' ');
}
/* circle()/ellipse() "at <position>": the specified form (a lone offset gets "center", the horizontal pair
   first) or, given a length resolver, the computed form (two values, keywords as percentages, px) */
function shapePos(v, px) {
    return v.replace(/\b(circle|ellipse)\(([^()]*)\)/gi, (m, fn, args) => {
        const i = args.search(/(^|\s)at\s/i); if (i < 0) return m;
        const head = args.slice(0, i).trim(), t = args.slice(i).trim().slice(3).trim().split(/\s+/);
        const H = /^(left|right)$/i, V = /^(top|bottom)$/i, K = /^(left|right|top|bottom|center)$/i;
        let out = null;
        if (!px) {
            if (t.length === 1 && !K.test(t[0])) out = t[0] + ' center';
            else if (t.length === 4 && V.test(t[0])) out = [t[2], t[3], t[0], t[1]].join(' ');
        } else {
            let x, y;
            if (t.length === 1) { if (V.test(t[0])) { x = ['center']; y = [t[0]]; } else { x = [t[0]]; y = ['center']; } }
            else if (t.length === 2) { const sw = V.test(t[0]) || H.test(t[1]); x = [sw ? t[1] : t[0]]; y = [sw ? t[0] : t[1]]; }
            else if (t.length === 4) { const sw = V.test(t[0]); x = sw ? t.slice(2) : t.slice(0, 2); y = sw ? t.slice(0, 2) : t.slice(2); }
            else return m;
            const one = (a, far) => {
                const kw = a[0].toLowerCase(), off = a[1];
                if (a.length === 1 && !K.test(kw)) { const r = px(kw); return r; }
                if (!off) return kw === 'center' ? '50%' : kw === far ? '100%' : '0%';
                if (/%$/.test(off)) { const n = parseFloat(off); return kw === far ? +(100 - n).toPrecision(6) + '%' : off; }
                const r = px(off); if (r == null) return null;
                return kw === far ? 'calc(100% - ' + r + ')' : r;
            };
            const xs = one(x, 'right'), ys = one(y, 'bottom');
            if (xs == null || ys == null) return m;
            out = xs + ' ' + ys;
        }
        return out == null ? m : fn + '(' + (head ? head + ' ' : '') + 'at ' + out + ')';
    });
}
/* inset(): "/ <vertical radii>" equal to the horizontal ones is dropped; with px, lengths become px */
function insetNorm(v, px) {
    return v.replace(/\binset\(([^()]*)\)/gi, (m, args) => {
        let t = args.trim().split(/\s+/);
        if (px) { const c = t.map(x => /^(round|\/)$/i.test(x) ? x : px(x)); if (c.some(x => x == null)) return m; t = c; }
        const r = t.findIndex(x => /^round$/i.test(x)), sl = t.indexOf('/');
        if (r >= 0 && sl > r) {
            const four = a => [a[0], a[1] ?? a[0], a[2] ?? a[0], a[3] ?? a[1] ?? a[0]].join(' ');
            const H = t.slice(r + 1, sl), V = t.slice(sl + 1);
            if (H.length && V.length && four(H) === four(V)) t = t.slice(0, sl);
        }
        return 'inset(' + t.join(' ') + ')';
    });
}
const SHAPE_UNITS = { px: 1, cm: 96 / 2.54, mm: 96 / 25.4, q: 96 / 101.6, in: 96, pt: 4 / 3, pc: 16 };
function shapeLenPx(el) {
    return a => {
        const m = /^(-?[\d.]+(?:e[+-]?\d+)?)([a-z%]*)$/i.exec(a); if (!m) return null;
        const n = parseFloat(m[1]), u = m[2].toLowerCase();
        if (u === '%') return a;
        let f = SHAPE_UNITS[u];
        if (f == null) {
            const W = innerWidth / 100, Hh = innerHeight / 100;
            if (u === 'em') f = parseFloat(N.computed(el, 'font-size')) || 16;
            else if (u === 'rem') f = parseFloat(N.computed(document.documentElement, 'font-size')) || 16;
            else if (u === 'vw') f = W; else if (u === 'vh') f = Hh; else if (u === 'vmin') f = Math.min(W, Hh); else if (u === 'vmax') f = Math.max(W, Hh);
            else if (u === '' && n === 0) f = 1;
            else return null;
        }
        return +(n * f).toPrecision(6) + 'px';
    };
}
function specNormalize(p, v) {
    if ((p === 'shape-outside' || p === 'clip-path') && /(circle|ellipse)\(/i.test(v)) v = shapePos(v, null);
    if ((p === 'shape-outside' || p === 'clip-path') && /inset\(/i.test(v)) v = insetNorm(v, null);
    if (/-gradient\(\s*in\s/i.test(v))   /* "in <space> <direction>" serializes as "<direction> in <space>" */
        v = v.replace(/((?:repeating-)?(?:linear|radial|conic)-gradient\(\s*)in\s+([a-z0-9-]+(?:\s+(?:shorter|longer|increasing|decreasing)\s+hue)?)\s+([^,()]+?)\s*,/gi, (m, f, cs, dir) => /^(shorter|longer|increasing|decreasing)\s+hue$/i.test(dir.trim()) ? m : f + dir.trim() + ' in ' + cs.replace(/\s+/g, ' ') + ',');
    if (/-gradient\(/i.test(v)) v = v.replace(/\bin\s+xyz(?=[\s,])/gi, 'in xyz-d65');
    if (/radial-gradient\(\s*ellipse\s/i.test(v)) v = v.replace(/(radial-gradient\(\s*)ellipse\s+(?=[\d.+-][^\s,]*\s+[\d.+-])/gi, '$1');   /* two sizes: an ellipse already */   /* xyz is xyz-d65 */
    if (/-gradient\([^)]*\bin\s+(srgb|oklab)\s*,/i.test(v))   /* the default space is not written: srgb for legacy colors only, else oklab */
        v = v.replace(/((?:repeating-)?(?:linear|radial|conic)-gradient\()((?:[^,()]*?\s)?)in\s+(srgb|oklab)\s*,(.*)$/i, (m, f, dir, cs, rest) => {
            const modern = /\b(color|lab|lch|oklab|oklch|hwb)\(/i.test(rest) || /\b(rgba?|hsla?)\([^)]*\bnone\b/i.test(rest);
            if ((cs.toLowerCase() === 'oklab') !== modern) return m;
            dir = dir.trim();
            return f + (dir ? dir + ',' : '') + rest.replace(/^\s*/, dir ? ' ' : '');
        });
    if (p === 'position-area') { const n = posArea(v); if (n !== null) return n; }
    if (/["'\\]|url\(|var\(|env\(|attr\(|#/i.test(v) || /timing-function$|^transition$|^animation$/.test(p)) return v;
    let t = v.trim().replace(/(^|[\s(,/*+-])([+-]?)(\d*\.\d+|\d+\.?)(e[+-]?\d+)?(?=[a-z%]*(?:$|[\s),/*]))/gi, (m, pre, sign, num, exp) => {
        const n = Number(num + (exp || ''));
        if (!isFinite(n)) return m;
        const r = String(n);
        return pre + (sign === '-' && n ? '-' : '') + r;
    }).replace(/\s+/g, ' ');
    if (/^(inherit|initial|unset|revert|revert-layer)$/i.test(t)) return CSS_POSITION.test(p) ? t.toLowerCase() : t;
    if (CSS_POSITION.test(p)) return cssPositionSpecified(t, cssPositionAxis(p)) || t;
    if (/\b(sin|cos|tan|asin|acos|atan|atan2|sqrt|exp|pow|log)\(/i.test(t)) t = specFoldMath(t);
    if (p !== 'line-height' && typeof TOM_LENGTH_PROPS !== 'undefined' && TOM_LENGTH_PROPS.test(p)) t = t.split(' ').map(w => w === '0' || w === '-0' ? '0px' : w).join(' ');
    if (/^(align|justify|place)-(content|items|self)$/.test(p)) t = t.replace(/\bfirst baseline\b/g, 'baseline');
    if (p === 'display') { const w = t.toLowerCase().split(' '); if (w.length === 2 && w.includes('list-item') && w.includes('flow')) t = 'list-item'; else if (w.length === 2 && w[1] === 'flow' && /^(block|inline|run-in)$/.test(w[0])) t = w[0]; else if (w.length === 2 && w[0] === 'block' && /^(flex|grid|table|flow-root|math)$/.test(w[1])) t = w[1]; else if (w.length === 2 && w[0] === 'inline' && w[1] === 'flow-root') t = 'inline-block'; else if (w.length === 2 && w[0] === 'inline' && /^(flex|grid|table)$/.test(w[1])) t = 'inline-' + w[1]; }
    if (p === 'text-decoration-line') { const order = ['spelling-error', 'grammar-error', 'underline', 'overline', 'line-through', 'blink'], w = t.toLowerCase().split(' '); if (w.length > 1 && w.every(x => order.includes(x))) t = order.filter(x => w.includes(x)).join(' '); }
    if (p === 'grid-area' || p === 'grid-row' || p === 'grid-column') { const w = tomSplitTop(t, '/').map(x => x.trim()); if (w.length > 1 && w.every(x => x.toLowerCase() === 'auto')) t = 'auto'; }
    if (/\banchor(-size)?\(/i.test(t)) t = t.replace(/\b(anchor-size|anchor)\(\s*([^\s,()-][^\s,()]*)\s+(--[\w-]+)/gi, '$1($3 $2');   /* the name comes first */
    if (p === 'font') { const f = fontParts(t); if (f) t = [f.style, f.variant, f.weight, f.stretch].filter(x => x !== 'normal').concat(f.lh === 'normal' ? f.size : `${f.size} / ${f.lh}`, f.family).join(' '); }
    if (SPEC_PAIR.has(p) || SPEC_BOX.has(p)) {
        const w = tomSplitTop(t, ' ').map(x => x.trim()).filter(Boolean);
        if (SPEC_PAIR.has(p) && w.length === 2 && w[0] === w[1] && !/^(first|last|safe|unsafe|legacy)$/.test(w[0])) t = w[0];
        if (SPEC_BOX.has(p) && w.length >= 2 && w.length <= 4) {
            const [a, b, c = a, d = b] = w;
            t = b === d ? (a === c ? (a === b ? a : `${a} ${b}`) : `${a} ${b} ${c}`) : `${a} ${b} ${c} ${d}`;
        }
    }
    return t;
}
/* specified values of single-colour properties: legacy colours serialize as rgb()/rgba(), keywords lowercase;
   an unparsable colour leaves the declaration unchanged (null) */
const SPEC_COLOR_PROPS = new SSet(['color', 'background-color', 'border-top-color', 'border-right-color', 'border-bottom-color', 'border-left-color', 'border-block-start-color', 'border-block-end-color',
    'border-inline-start-color', 'border-inline-end-color', 'outline-color', 'caret-color', 'column-rule-color', 'text-decoration-color', 'text-emphasis-color', 'flood-color', 'lighting-color', 'stop-color', '-webkit-text-fill-color', '-webkit-text-stroke-color']);
const specAlpha = a => { const b = Math.round(a * 255); for (const d of [2, 3]) { const t = +a.toFixed(d); if (Math.round(t * 255) === b) return String(t); } return String(+a.toFixed(6)); };
/* top-level pieces of s: split at commas (commas) or at whitespace with '/' its own piece */
function specSplit(s, commas) {
    const out = []; let d = 0, cur = '';
    for (const ch of s) {
        if (ch === '(') d++; else if (ch === ')') d--;
        if (!d && (commas ? ch === ',' : /\s|\//.test(ch))) { if (cur.trim()) out.push(cur.trim()); if (ch === '/') out.push('/'); cur = ''; continue; }
        cur += ch;
    }
    if (cur.trim()) out.push(cur.trim());
    return out;
}
const specTok = t => t.replace(/^([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)(%|[a-z]*)$/, (m, n, u) => String(+(+n).toFixed(6)) + u);
/* Chrome's specified form of relative colours and color-mix(): nested colours in their specified form, rgba/hsla as
   rgb/hsl, color-mix percentages normalized; null when the colour is invalid */
function specColorFn(low) {
    if (N.cssColor(low) === null) return null;
    let m = /^(rgba?|hsla?|hwb|lab|lch|oklab|oklch|color)\(\s*from\s+([\s\S]*)\)$/.exec(low);
    if (m) {
        const t = specSplit(m[2]), origin = specColor(t[0], 'color');
        if (origin == null) return null;
        const rest = t.slice(1).map(x => x === '/' ? x : specTok(x));
        return `${m[1].replace(/a$/, '')}(from ${origin} ${rest.join(' ')})`;
    }
    m = /^color-mix\(([\s\S]*)\)$/.exec(low);
    if (!m) return low;
    const parts = specSplit(m[1], true), head = /^in\s/.test(parts[0]) ? parts.shift().split(/\s+/).join(' ') : null, items = [];
    for (const part of parts) {
        const toks = specSplit(part); let pct = null, col = [];
        for (const x of toks) if (pct === null && (/%$/.test(x) || /^calc\(/.test(x))) pct = specTok(x); else col.push(x);
        const c = specColor(col.join(' '), 'color');
        if (c == null) return null;
        items.push([c, pct]);
    }
    if (items.length === 2) {
        const [a, b] = items, num = p => p !== null && /^[\d.]+%$/.test(p) ? parseFloat(p) : null;
        if (a[1] === null && num(b[1]) !== null) a[1] = (100 - num(b[1])) + '%';
        else if (b[1] === null && num(a[1]) !== null) b[1] = (100 - num(a[1])) + '%';
        if (num(a[1]) === 50 && num(b[1]) === 50) a[1] = b[1] = null;
    }
    return `color-mix(${head ? head + ', ' : ''}${items.map(([c, p]) => p === null ? c : `${c} ${p}`).join(', ')})`;
}
function specColor(v, p) {
    const t = v.replace(/\/\*[\s\S]*?\*\//g, '').trim(), low = t.toLowerCase();
    if (low === 'auto' && /^(caret|accent)-color$/.test(p)) return t;
    if (/rule-color$/.test(p || '')) { let f = low, g; while ((g = f.replace(/[a-z-]*\([^()]*\)/g, 'x')) !== f) f = g; if (/,|\brepeat\b|^x$/.test(f) && /,|repeat\(/.test(low)) return t; }
    if (/^(inherit|initial|unset|revert|revert-layer)$/.test(low) || /var\(|env\(|attr\(/i.test(t)) return v;
    /* forms cssColor does not model are kept as written unless plainly empty */
    if (/^(color-mix\(|(rgba?|hsla?|hwb|lab|lch|oklab|oklch|color)\(\s*from\s)/.test(low)) return specColorFn(low);
    if (/\bfrom\b|calc\(|color-mix\(|color-layers\(|alpha\(|contrast-color\(|light-dark\(|device-cmyk\(|sibling-(index|count)\(|display-p3-linear/.test(low)) return /\(\s*\)/.test(low) ? null : v;
    const c = cssColor(t);
    if (!c) return null;
    if (c.current) return 'currentcolor';
    if (/\bnone\b/.test(low)) return t;
    if (/^[a-z]+$/.test(low)) return low;
    if (/^color\(/.test(low) && c.space) return `color(${c.space === 'xyz' ? 'xyz-d65' : c.space} ${c.c.map(colorNum).join(' ')}${c.alpha < 1 ? ' / ' + colorNum(c.alpha) : ''})`;
    const lm = /^(lab|lch|oklab|oklch)\(([^()]*)\)$/.exec(low);
    if (lm) {
        const ok = lm[1][0] === 'o', tk = specSplit(lm[2]);
        if (tk.length === 3 || (tk.length === 5 && tk[3] === '/')) {
            const L = tk[0].endsWith('%') ? parseFloat(tk[0]) / (ok ? 100 : 1) : parseFloat(tk[0]);
            const ch = [String(+Math.min(ok ? 1 : 100, Math.max(0, L)).toFixed(6)), ...tk.slice(1, 3).map(x => specTok(x.replace(/deg$/, '')))];
            const a = tk[4] === undefined ? null : tk[4].endsWith('%') ? String(+(parseFloat(tk[4]) / 100).toFixed(6)) : specTok(tk[4]);
            return `${lm[1]}(${ch.join(' ')}${a !== null && a !== '1' ? ' / ' + a : ''})`;
        }
    }
    if (/^(ok)?lch\(/.test(low)) return t.replace(/([\d.])deg\b/gi, '$1');
    if (!c.legacy && !/^(#|rgba?\(|hsla?\(|hwb\()/.test(low)) return v;
    const [r, g, b] = (c.legacy ? c.c : colorTo(c, 'srgb')).map(x => Math.round(Math.min(1, Math.max(0, x)) * 255));
    return c.alpha >= 1 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${specAlpha(c.alpha)})`;
}
class CSSStyleDeclaration {
    constructor(el, ro) { def(this, '_el', el); def(this, '_ro', ro); def(this, '_src', null); def(this, '_map', null); }
    /* parsed declarations of the current style attribute; callers that modify take a copy */
    _m() {
        const s = this._el ? N.attr(this._el, 'style') : '';
        if (s !== this._src || !this._map) { this._map = parseDecls(s); this._src = s; }
        return this._map;
    }
    get cssText() { return serDecls(this._m()); } set cssText(v) { this._el.setAttribute('style', String(v)); }
    get length() { return this._m().size; }
    item(i) { return [...this._m().keys()][i] || ''; }
    getPropertyValue(p) { p = camelToKebab(String(p)); const m = this._m(), e = m.get(p); if (cssGridRelevant(p)) return cssGridValue(m, p); if (cssFlexRelevant(p)) return cssFlexValue(m, p); return e ? e[0] : shorthandPart(m, p); }
    getPropertyPriority(p) { p = camelToKebab(String(p)); const m = this._m(); if (cssGridRelevant(p)) return cssGridValue(m, p, true); if (cssFlexRelevant(p)) return cssFlexValue(m, p, true); const e = m.get(p); return e ? e[1] : ''; }
    setProperty(p, v, pri) { p = String(p); if (!p.startsWith('--')) p = camelToKebab(p);
        v = v == null ? '' : String(v);
        pri = pri == null ? '' : String(pri);
        if (v !== '' && ((pri && !/^important$/i.test(pri)) || !cssVarSyntax(v))) return;
        if (cssFlexRelevant(p)) {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri), m = new SMap(this._m());
            if (value === '') cssFlexRemove(m, p);
            else if (priority && !/^important$/i.test(priority) || !cssFlexSet(m, p, value, priority ? 'important' : '')) return;
            this._el.setAttribute('style', serDecls(m)); return;
        }
        if (cssGridRelevant(p)) {
            const value = v == null ? '' : String(v), priority = pri == null ? '' : String(pri), m = new SMap(this._m());
            if (value === '') cssGridRemove(m, p);
            else if (priority && !/^important$/i.test(priority) || !cssGridSet(m, p, value, priority ? 'important' : '')) return;
            this._el.setAttribute('style', serDecls(m)); return;
        }
        if (v != null && v !== '' && SPEC_COLOR_PROPS.has(p)) { v = specColor(String(v), p); if (v === null) return; } if (v != null && v !== '' && !p.startsWith('--')) { if (!(typeof v === 'string' && MATH_LEN_PROPS.test(p) && CSS_CANONICAL_PX_SIZE.test(v))) { if (!mathValid(p, String(v))) return; v = specNormalize(p, String(v)); } } const m = new SMap(this._m()); if (p === 'background-position') { m.delete(p); m.delete('background-position-x'); m.delete('background-position-y'); } else if (CSS_POSITION.test(p)) m.delete(p); if (v == null || v === '') m.delete(p); else m.set(p, [String(v), pri ? 'important' : '']); this._el.setAttribute('style', serDecls(m)); }
    removeProperty(p) { p = camelToKebab(String(p)); const m = new SMap(this._m()); if (cssFlexRelevant(p)) { const size = m.size, value = cssFlexRemove(m, p); if (size !== m.size) this._el.setAttribute('style', serDecls(m)); return value; } if (cssGridRelevant(p)) { const size = m.size, value = cssGridRemove(m, p); if (size !== m.size) this._el.setAttribute('style', serDecls(m)); return value; } if (p === 'background-position') { const v = cssPositionDeclaration(m, p), size = m.size; m.delete(p); m.delete('background-position-x'); m.delete('background-position-y'); if (m.size !== size) this._el.setAttribute('style', serDecls(m)); return v; } const e = m.get(p); if (e) { m.delete(p); this._el.setAttribute('style', serDecls(m)); } return e ? e[0] : ''; }
}
const styleHandler = {
    get(t, p) { if (typeof p !== 'string' || p in t) return Reflect.get(t, p, t); if (/^\d+$/.test(p)) return t.item(+p); return t.getPropertyValue(p); },
    set(t, p, v) { if (typeof p !== 'string' || p in t) Reflect.set(t, p, v, t); else t.setProperty(p, v); return true; },
    has(t, p) { return p in t || cssPropKnown(p); },
};
class ComputedStyleDeclaration extends CSSStyleDeclaration {
    getPropertyValue(p) { if (this._none || !this._el.isConnected) return ''; if (waDirty) waFlush(); let k = camelToKebab(String(p)); if (k === '-webkit-appearance') k = 'appearance'; if (k === 'font') return computedFont(this); const grid = cssGridChildren(k); if (grid) return cssGridValue(new SMap(grid.map(p => [p, [this.getPropertyValue(p), '']])), k); let v = N.computed(this._el, k, this._ps | 0); if (trPoll()) v = N.computed(this._el, k, this._ps | 0); if ((k === 'shape-outside' || k === 'clip-path') && v && /(circle|ellipse)\(/i.test(v)) v = shapePos(v, shapeLenPx(this._el)); if ((k === 'shape-outside' || k === 'clip-path') && v && /inset\(/i.test(v)) v = insetNorm(v, shapeLenPx(this._el)); return v == null ? '' : v; }
    get cssText() { return ''; } get length() { return this._names().length; }
    /* an invalid pseudo-element or an element outside the document has an empty style */
    _names() { return this._none || !this._el.isConnected || !N.computed(this._el, 'display') || mediaChild(this._el) ? [] : computedNames(); }   /* no style: outside the flat tree */
    item(i) { if (!arguments.length) throw new TypeError("Failed to execute 'item' on 'CSSStyleDeclaration': 1 argument required, but only 0 present."); return this._names()[i >>> 0] ?? ''; }
    *[Symbol.iterator]() { yield* this._names(); }
    setProperty() { throw new DOMException('These styles are computed, and therefore read-only.', 'NoModificationAllowedError'); }
}
/* inside <video>/<audio>, whose shadow trees have no slots */
const mediaChild = el => { for (let p = el.parentNode; p && p.nodeType === 1; p = p.parentNode) if (p.namespaceURI === 'http://www.w3.org/1999/xhtml' && (p.localName === 'video' || p.localName === 'audio')) return true; return false; };
/* the longhands a computed style lists (length / item / iteration), sorted like Chrome's */
const CSS_SHORTHANDS = /^(all|animation|animation-range|background|background-position|border|border-(block|inline)(-(start|end))?(-(color|style|width))?|border-(top|right|bottom|left|color|style|width|image|radius)|column-rule|columns|contain-intrinsic-size|container|flex|flex-flow|font|font-synthesis|font-variant|gap|grid|grid-area|grid-column|grid-row|grid-template|grid-gap|grid-row-gap|grid-column-gap|inset|inset-block|inset-inline|list-style|margin|margin-block|margin-inline|marker|mask|mask-border|offset|outline|overflow|overscroll-behavior|padding|padding-block|padding-inline|place-content|place-items|place-self|position-try|scroll-margin(-block|-inline)?|scroll-padding(-block|-inline)?|scroll-timeline|text-box|text-decoration|text-emphasis|text-wrap|transition|view-timeline|white-space|word-wrap|page-break-(before|after|inside)|-webkit-.*|-epub-.*)$/;
let computedList = null;
const computedNames = () => computedList || (computedList = Array.from(CSS_PROP_NAMES).filter(p => !CSS_SHORTHANDS.test(p)).sort());
/* CSS properties Chrome supports (those its WPT computed-style tests exercise), for `name in getComputedStyle(el)` */
const CSS_PROP_NAMES = new SSet("background border-top border-right border-bottom border-left touch-action pointer-events -webkit-user-select offset border-block border-inline -webkit-appearance page-break-before page-break-after page-break-inside -webkit-animation -webkit-animation-delay -webkit-animation-direction -webkit-animation-duration -webkit-animation-fill-mode -webkit-animation-iteration-count -webkit-animation-name -webkit-animation-play-state -webkit-animation-timing-function -webkit-transition -webkit-transition-delay -webkit-transition-duration -webkit-transition-property -webkit-transition-timing-function -webkit-transform-origin -webkit-transform-style -webkit-perspective -webkit-perspective-origin -webkit-backface-visibility -webkit-box-align -webkit-box-direction -webkit-box-flex -webkit-box-ordinal-group -webkit-box-orient -webkit-box-pack -webkit-box-shadow -webkit-box-sizing -webkit-box-reflect -webkit-box-decoration-break -webkit-mask -webkit-mask-image -webkit-mask-position -webkit-mask-size -webkit-mask-repeat -webkit-mask-origin -webkit-mask-clip -webkit-mask-composite -webkit-mask-box-image -webkit-border-radius -webkit-border-top-left-radius -webkit-border-top-right-radius -webkit-border-bottom-left-radius -webkit-border-bottom-right-radius -webkit-flex -webkit-flex-basis -webkit-flex-direction -webkit-flex-flow -webkit-flex-grow -webkit-flex-shrink -webkit-flex-wrap -webkit-align-content -webkit-align-items -webkit-align-self -webkit-justify-content -webkit-order -webkit-filter -webkit-clip-path -webkit-font-smoothing -webkit-tap-highlight-color -webkit-user-modify -webkit-user-drag -webkit-text-security -webkit-highlight -webkit-hyphenate-character -webkit-line-break -webkit-locale -webkit-rtl-ordering -webkit-text-combine -webkit-text-emphasis -webkit-text-emphasis-color -webkit-text-emphasis-position -webkit-text-emphasis-style -webkit-text-orientation -webkit-writing-mode -webkit-column-count -webkit-column-gap -webkit-column-rule -webkit-column-rule-color -webkit-column-rule-style -webkit-column-rule-width -webkit-column-span -webkit-column-width -webkit-columns -webkit-column-break-after -webkit-column-break-before -webkit-column-break-inside -webkit-print-color-adjust -webkit-ruby-position -webkit-shape-outside -webkit-shape-margin -webkit-shape-image-threshold -webkit-border-image -webkit-border-horizontal-spacing -webkit-border-vertical-spacing -webkit-background-clip -webkit-background-origin -webkit-background-size -webkit-font-feature-settings -webkit-hyphens -webkit-opacity -webkit-text-size-adjust -webkit-text-decorations-in-effect -webkit-text-stroke-color -webkit-text-fill-color -webkit-box-decoration-break -webkit-initial-letter -webkit-margin-before -webkit-margin-after -webkit-margin-start -webkit-margin-end -webkit-padding-before -webkit-padding-after -webkit-padding-start -webkit-padding-end -webkit-logical-width -webkit-logical-height -webkit-min-logical-width -webkit-min-logical-height -webkit-max-logical-width -webkit-max-logical-height -webkit-border-before -webkit-border-after -webkit-border-start -webkit-border-end -webkit-border-before-color -webkit-border-before-style -webkit-border-before-width -webkit-border-after-color -webkit-border-after-style -webkit-border-after-width -webkit-border-start-color -webkit-border-start-style -webkit-border-start-width -webkit-border-end-color -webkit-border-end-style -webkit-border-end-width -webkit-line-clamp -webkit-text-stroke -webkit-text-stroke-width -webkit-transform accent-color align-content align-items align-self alignment-baseline all anchor-name anchor-scope animation animation-composition animation-delay animation-direction animation-duration animation-fill-mode animation-iteration-count animation-name animation-play-state animation-range animation-range-end animation-range-start animation-timeline animation-timing-function appearance aspect-ratio backdrop-filter backface-visibility background-attachment background-blend-mode background-clip background-color background-image background-origin background-position background-position-x background-position-y background-repeat background-size baseline-shift baseline-source block-size border border-block-color border-block-end-color border-block-end-radius border-block-end-style border-block-end-width border-block-start-color border-block-start-radius border-block-start-style border-block-start-width border-block-style border-block-width border-bottom-color border-bottom-left-radius border-bottom-radius border-bottom-right-radius border-bottom-style border-bottom-width border-collapse border-color border-end-end-radius border-end-start-radius border-image-outset border-image-repeat border-image-slice border-image-source border-image-width border-inline-color border-inline-end-color border-inline-end-radius border-inline-end-style border-inline-end-width border-inline-start-color border-inline-start-radius border-inline-start-style border-inline-start-width border-inline-style border-inline-width border-left-color border-left-radius border-left-style border-left-width border-radius border-right-color border-right-radius border-right-style border-right-width border-shape border-spacing border-start-end-radius border-start-start-radius border-style border-top-color border-top-left-radius border-top-radius border-top-right-radius border-top-style border-top-width border-width bottom box-decoration-break box-shadow box-sizing break-after break-before break-inside caption-side caret-color caret-shape clear clip clip-path clip-rule color color-interpolation-filters color-scheme column-count column-fill column-gap column-rule column-rule-break column-rule-color column-rule-inset column-rule-inset-cap column-rule-inset-cap-end column-rule-inset-cap-start column-rule-inset-end column-rule-inset-junction column-rule-inset-junction-end column-rule-inset-junction-start column-rule-inset-start column-rule-style column-rule-visibility-items column-rule-width column-span column-width columns contain contain-intrinsic-height contain-intrinsic-size contain-intrinsic-width container container-name container-type content content-visibility corner corner-block-end-shape corner-block-start corner-block-start-shape corner-bottom-left-shape corner-bottom-right-shape corner-bottom-shape corner-inline-end-shape corner-inline-start-shape corner-left-shape corner-right-shape corner-shape corner-top corner-top-left corner-top-left-shape corner-top-right-shape corner-top-shape counter-increment counter-reset counter-set cursor cx cy direction display dominant-baseline dynamic-range-limit empty-cells field-sizing fill fill-opacity filter flex flex-basis flex-direction flex-flow flex-grow flex-line-count flex-shrink flex-wrap float flood-color flood-opacity font font-family font-feature-settings font-kerning font-language-override font-optical-sizing font-palette font-size font-size-adjust font-stretch font-style font-synthesis font-synthesis-small-caps font-synthesis-style font-synthesis-weight font-variant font-variant-alternates font-variant-caps font-variant-east-asian font-variant-emoji font-variant-ligatures font-variant-numeric font-variant-position font-variation-settings font-weight forced-color-adjust frame-sizing gap grid grid-area grid-auto-columns grid-auto-flow grid-auto-rows grid-column grid-column-end grid-column-gap grid-column-start grid-gap grid-lanes grid-lanes-direction grid-lanes-pack grid-row grid-row-end grid-row-gap grid-row-start grid-template-areas grid-template-columns grid-template-rows height hyphenate-character hyphenate-limit-chars hyphens image-orientation image-rendering initial-letter inline-size inset inset-block inset-block-end inset-block-start inset-inline inset-inline-end inset-inline-start interactivity interpolate-size isolation justify-content justify-items justify-self left letter-spacing lighting-color line-break line-height link-parameters list-style list-style-image list-style-position list-style-type margin margin-block margin-block-end margin-block-start margin-bottom margin-inline margin-inline-end margin-inline-start margin-left margin-right margin-top margin-trim mask mask-clip mask-composite mask-image mask-mode mask-origin mask-position mask-repeat mask-size mask-type math-depth max-block-size max-height max-inline-size max-width min-block-size min-height min-inline-size min-width mix-blend-mode object-fit object-position offset-anchor offset-distance offset-path offset-position offset-rotate opacity order orphans outline outline-color outline-offset outline-style outline-width overflow overflow-anchor overflow-block overflow-clip-margin overflow-inline overflow-wrap overflow-x overflow-y overlay overscroll-behavior overscroll-behavior-block overscroll-behavior-inline overscroll-behavior-x overscroll-behavior-y padding padding-block padding-block-end padding-block-start padding-bottom padding-inline padding-inline-end padding-inline-start padding-left padding-right padding-top page perspective perspective-origin place-content place-items place-self position position-anchor position-area position-try position-try-fallbacks position-try-order position-visibility print-color-adjust quotes reading-flow reading-order resize right rotate row-gap row-rule row-rule-break row-rule-color row-rule-inset row-rule-inset-cap row-rule-inset-cap-end row-rule-inset-cap-start row-rule-inset-end row-rule-inset-junction row-rule-inset-junction-end row-rule-inset-junction-start row-rule-inset-start row-rule-style row-rule-visibility-items row-rule-width ruby-align ruby-overhang ruby-position rule rule-break rule-color rule-inset rule-inset-cap rule-inset-end rule-inset-junction rule-inset-start rule-overlap rule-style rule-visibility-items rule-width rx ry scale scroll-axis-lock scroll-behavior scroll-initial-target scroll-margin scroll-margin-block scroll-margin-block-end scroll-margin-block-start scroll-margin-bottom scroll-margin-inline scroll-margin-inline-end scroll-margin-inline-start scroll-margin-left scroll-margin-right scroll-margin-top scroll-marker-group scroll-padding scroll-padding-block scroll-padding-block-end scroll-padding-block-start scroll-padding-bottom scroll-padding-inline scroll-padding-inline-end scroll-padding-inline-start scroll-padding-left scroll-padding-right scroll-padding-top scroll-snap-align scroll-snap-stop scroll-snap-type scroll-target-group scrollbar-color scrollbar-gutter scrollbar-width shape-image-threshold shape-margin shape-outside stroke stroke-dasharray stroke-dashoffset stroke-opacity stroke-width tab-size table-layout text-align text-align-last text-autospace text-box text-box-edge text-box-trim text-combine-upright text-decoration text-decoration-color text-decoration-inset text-decoration-line text-decoration-skip-ink text-decoration-skip-spaces text-decoration-style text-decoration-thickness text-emphasis text-emphasis-color text-emphasis-position text-emphasis-style text-fit text-indent text-justify text-orientation text-overflow text-shadow text-size-adjust text-spacing-trim text-transform text-underline-offset text-underline-position text-wrap text-wrap-mode text-wrap-style top transform transform-box transform-origin transform-style transition transition-behavior transition-delay transition-duration transition-property transition-timing-function translate unicode-bidi user-select vertical-align view-transition-class view-transition-name visibility white-space white-space-collapse widows width will-change word-break word-spacing word-wrap writing-mode x y z-index zoom r d fill-rule stop-color stop-opacity shape-rendering stroke-miterlimit color-interpolation color-rendering paint-order stroke-linecap stroke-linejoin text-anchor vector-effect column-height grid-template marker marker-start marker-mid marker-end mask-border mask-border-mode mask-border-outset mask-border-repeat mask-border-slice mask-border-source mask-border-width text-rendering border-image -webkit-text-fill-color -webkit-text-stroke-color -webkit-mask-image -webkit-box-orient".split(" "));
const cssPropName = p => typeof p !== 'string' ? null : p.includes('-') ? p : p.replace(/^webkit(?=[A-Z])/, '-webkit').replace(/[A-Z]/g, c => '-' + c.toLowerCase());
/* one notion of a supported property for `in style`, `in getComputedStyle()` and CSS.supports(): dashed, camelCase, webkitCase */
const cssPropKnown = p => typeof p === 'string' && (p.startsWith('--') ? false : p === 'cssFloat' || (p.includes('-') ? p === p.toLowerCase() && CSS_PROP_NAMES.has(p) : !/^-/.test(cssPropName(p)) || /^-webkit-/.test(cssPropName(p)) ? CSS_PROP_NAMES.has(cssPropName(p)) : false));
const computedHandler = { get(t, p) { if (typeof p !== 'string' || p in t) return Reflect.get(t, p, t); if (/^\d+$/.test(p)) return t._names()[+p]; return t.getPropertyValue(p); }, set() { return true; },
    has(t, p) { return p in t || (/^\d+$/.test(p) ? +p < t._names().length : cssPropKnown(p)); } };

class Element extends Node {}
methods(Element.prototype, ParentNode); methods(Element.prototype, ChildNode);
const lcName = (el, n) => { n = String(n); return N.ns(el) === 0 ? n.toLowerCase() : n; };
const NSURI = ['http://www.w3.org/1999/xhtml', 'http://www.w3.org/2000/svg', 'http://www.w3.org/1998/Math/MathML'];
const validLocalName = n => /^[A-Za-z][^\t\n\f\r \/>\x00]*$/.test(n) || /^[:_\u0080-\u{10FFFF}][A-Za-z0-9\-.:_\u0080-\u{10FFFF}]*$/u.test(n);
const asciiLower = s => s.replace(/[A-Z]+/g, c => c.toLowerCase()), asciiUpper = s => s.replace(/[a-z]+/g, c => c.toUpperCase());
methods(Element.prototype, {
    get tagName() { let ln = this.localName; if (typeof ln !== 'string') ln = String(N.name(this)); const q = this.__pfx ? this.__pfx + ':' + ln : ln; if (this.namespaceURI !== NSURI[0]) return q; const d = N.ownerDoc(this); return d && N.ns(d) === 3 ? q : asciiUpper(q); },
    get localName() { return this.__ln ?? N.name(this); }, get namespaceURI() { return this.__nsu !== undefined ? this.__nsu : N.ns(this) === 3 ? null : NSURI[N.ns(this)] || NSURI[0]; }, get prefix() { return this.__pfx ?? null; },
    get id() { return N.attr(this, 'id') ?? ''; }, set id(v) { this.setAttribute('id', v); },
    get className() { return N.attr(this, 'class') ?? ''; }, set className(v) { this.setAttribute('class', v); },
    get classList() { if (!this.__cl) def(this, '__cl', new DOMTokenList(TOK_KEY, this, 'class')); return this.__cl; }, set classList(v) { this.setAttribute('class', v); },
    get slot() { return N.attr(this, 'slot') ?? ''; }, set slot(v) { this.setAttribute('slot', v); },
    get attributes() { if (!this.__nnm) def(this, '__nnm', nnmOf(this)); return this.__nnm; },
    getAttribute(n) { return N.attrQ(this, lcName(this, n)); },
    getAttributeNS(ns, n) { return N.attrNS(this, ns == null || ns === '' ? null : String(ns), String(n)); },
    setAttribute(n, v) {
        n = lcName(this, n); if (!validAttrName(n)) throw new DOMException(`Failed to execute 'setAttribute' on 'Element': '${n}' is not a valid attribute name.`, 'InvalidCharacterError');
        attrSetQ(this, n, ttAttr(this, null, n, v));
    },
    setAttributeNS(ns, q, v) { const [u, p, l] = nsExtract(ns, q, true); attrChange(this, u, p, l, ttAttr(this, u, l, v)); },
    removeAttribute(n) {
        n = lcName(this, n);
        const old = N.attrQ(this, n); if (old == null) return;
        const a = attrByQName(this, n);
        if (a && (a._ns !== null || a._pfx !== null)) { attrRemove(this, a._ns, a._ln); return; }
        attrDetach(this, null, n, old);
        N.rmAttrQ(this, n);
        if (n.charCodeAt(0) === 111 && n.charCodeAt(1) === 110) handlerAttr(this, n, null);
        notify('attributes', this, { attributeName: n, attributeNamespace: null, oldValue: old });
        ceAttr(this, n, old, null, null);
    },
    removeAttributeNS(ns, n) { attrRemove(this, ns == null || ns === '' ? null : String(ns), String(n)); },
    hasAttribute(n) { return N.attrQ(this, lcName(this, n)) != null; },
    hasAttributeNS(ns, n) { return N.attrNS(this, ns == null || ns === '' ? null : String(ns), String(n)) != null; },
    hasAttributes() { return N.attrs(this).length > 0; },
    toggleAttribute(n, force) {
        n = lcName(this, n); if (!validAttrName(n)) throw new DOMException(`Failed to execute 'toggleAttribute' on 'Element': '${n}' is not a valid attribute name.`, 'InvalidCharacterError');
        if (N.attrQ(this, n) == null) { if (force === undefined || force) { attrSetQ(this, n, ''); return true; } return false; }
        if (force === undefined || !force) { this.removeAttribute(n); return false; }
        return true;
    },
    getAttributeNames() { const a = N.attrs(this); const r = []; for (let i = 0; i < a.length; i += 2) r.push(a[i]); return r; },
    getAttributeNode(n) { return attrByQName(this, lcName(this, n)); },
    getAttributeNodeNS(ns, n) { return attrByNS(this, ns == null || ns === '' ? null : String(ns), String(n)); },
    setAttributeNode(a) {
        if (!(a instanceof Attr)) throw new TypeError("Failed to execute 'setAttributeNode' on 'Element': parameter 1 is not of type 'Attr'.");
        const v = ttAttr(this, a._ns, a._ln, a.value);   /* first: the default policy may change the element's attributes */
        if (a._el && a._el !== this) throw new DOMException("Failed to execute 'setAttributeNode' on 'Element': The node provided is an attribute node that is already an attribute of another Element; attribute nodes must be explicitly cloned.", 'InUseAttributeError');
        const old = attrByNS(this, a._ns, a._ln);
        if (old === a) return a;
        if (old) attrDetach(this, a._ns, a._ln, old.value);
        attrChange(this, a._ns, a._pfx, a._ln, v);
        let m = attrNodes.get(this); if (!m) attrNodes.set(this, m = new SMap());
        a._el = this; m.set(attrKey(a._ns, a._ln), a);
        return old;
    },
    setAttributeNodeNS(a) { return this.setAttributeNode(a); },
    removeAttributeNode(a) {
        if (!(a instanceof Attr)) throw new TypeError("Failed to execute 'removeAttributeNode' on 'Element': parameter 1 is not of type 'Attr'.");
        if (a._el !== this) throw new DOMException("Failed to execute 'removeAttributeNode' on 'Element': The node provided is owned by another element.", 'NotFoundError');
        attrRemove(this, a._ns, a._ln);
        return a;
    },
    /* in an XML document the fragment serialization is XML's */
    get innerHTML() { return xmlDocOf(this) ? [...this.childNodes].map(c => xmlSer(c, null, Object.assign(Object.create(null), { xml: XML_NS }))).join('') : N.html(this, false); },
    set innerHTML(v) { const a = ttCompliantHTML(v == null ? '' : v, htmlSinkOptions(false), 'Element innerHTML', xmlDocOf(this)); setInner(this, a.html, a.options); },
    get outerHTML() { return xmlDocOf(this) ? xmlSer(this, null, Object.assign(Object.create(null), { xml: XML_NS })) : N.html(this, true); },
    set outerHTML(v) { const a = ttCompliantHTML(v == null ? '' : v, htmlSinkOptions(false), 'Element outerHTML', xmlDocOf(this)); const p = N.parent(this); if (!p) return; insertNode(p, legacyHTMLFragment(p, a.html, a.options, 'Element outerHTML'), this); removeNode(this); },
    insertAdjacentHTML(pos, html) { const a = ttCompliantHTML(html, htmlSinkOptions(false), 'Element insertAdjacentHTML', xmlDocOf(this)); const ctx = /^(beforebegin|afterend)$/i.test(pos) ? N.parent(this) : this; if (ctx) this.insertAdjacentElement(pos, legacyHTMLFragment(ctx, a.html, a.options, 'Element insertAdjacentHTML')); },
    insertAdjacentElement(pos, el) {
        switch (String(pos).toLowerCase()) {
        case 'beforebegin': { const p = N.parent(this); if (p) insertNode(p, el, this); break; }
        case 'afterbegin': insertNode(this, el, N.first(this)); break;
        case 'beforeend': insertNode(this, el, null); break;
        case 'afterend': { const p = N.parent(this); if (p) insertNode(p, el, N.next(this)); break; }
        default: throw new DOMException('Invalid position', 'SyntaxError');
        }
        return el;
    },
    insertAdjacentText(pos, t) { this.insertAdjacentElement(pos, N.textNode(String(t))); },
    matches(s) { if (!arguments.length) throw argsErr('matches', this, 1); return N.matches(this, cssPre(String(s))); },
    webkitMatchesSelector(s) { if (!arguments.length) throw argsErr('webkitMatchesSelector', this, 1); return N.matches(this, cssPre(String(s))); },
    closest(s) { if (!arguments.length) throw argsErr('closest', this, 1); s = cssPre(String(s)); for (let e = this; e && N.type(e) === 1; e = N.parent(e)) if (N.matches(e, s, this)) return e; return null; },
    getBoundingClientRect() { const r = N.vrect(this); return r ? new DOMRect(r[0], r[1], r[2], r[3]) : new DOMRect(); },
    getClientRects() { return N.rect(this) ? [this.getBoundingClientRect()] : []; },
    /* padding box (border box minus borders); 0 for inline boxes and elements without a box */
    get clientWidth() { if (this === document.documentElement) return N.viewport()[0]; const r = N.rect(this), b = N.borders(this); return r && b && !b[4] ? Math.round(Math.max(0, r[2] - b[1] - b[3])) : 0; },
    get clientHeight() { if (this === document.documentElement) return N.viewport()[1]; const r = N.rect(this), b = N.borders(this); return r && b && !b[4] ? Math.round(Math.max(0, r[3] - b[0] - b[2])) : 0; },
    get clientTop() { const b = N.borders(this); return b && !b[4] ? Math.round(b[0]) : 0; },
    get clientLeft() { const b = N.borders(this); return b && !b[4] ? Math.round(b[3]) : 0; },
    get scrollWidth() { const s = N.scrollPos(this); if (s) return Math.round(s[2]); const o = N.overflowSize(this); if (this === document.documentElement) return Math.max(this.clientWidth, o ? Math.round(o[0]) : 0); return o ? Math.round(o[0]) : 0; },
    get scrollHeight() { const s = N.scrollPos(this); if (s) return Math.round(s[3]); const o = N.overflowSize(this); if (this === document.documentElement) return Math.max(this.clientHeight, o ? Math.round(o[1]) : 0); return o ? Math.round(o[1]) : 0; },
    get scrollTop() { if (this === document.documentElement || this === document.body) return N.viewport()[3]; const s = N.scrollPos(this); return s ? s[1] : 0; },
    set scrollTop(v) { if (this === document.documentElement || this === document.body) N.scrollTo(N.viewport()[2], +v || 0); else this.__scrollSet(NaN, +v || 0); },
    get scrollLeft() { const s = N.scrollPos(this); return s ? s[0] : 0; }, set scrollLeft(v) { this.__scrollSet(+v || 0, NaN); },
    __scrollSet(x, y) { if (N.setScroll(this, x, y)) setTimeout(() => this.dispatchEvent(new Event('scroll'))); },
    scrollTo(x, y) { if (this === document.documentElement || this === document.body) return window.scrollTo(x, y); const o = x && typeof x === 'object' ? x : { left: x, top: y }; this.__scrollSet(o.left == null ? NaN : +o.left || 0, o.top == null ? NaN : +o.top || 0); },
    scroll(x, y) { this.scrollTo(x, y); },
    scrollBy(x, y) { const o = x && typeof x === 'object' ? x : { left: x, top: y }; this.scrollTo({ left: this.scrollLeft + (+o.left || 0), top: this.scrollTop + (+o.top || 0) }); },
    scrollIntoView() { const r = N.rect(this); if (r) N.scrollTo(0, r[1]); }, scrollIntoViewIfNeeded() { this.scrollIntoView(); },
    attachShadow(init) {
        const fail = m => new TypeError(`Failed to execute 'attachShadow' on 'Element': ${m}`);
        if (!arguments.length) throw fail('1 argument required, but only 0 present.');
        if (init === null || (typeof init !== 'object' && typeof init !== 'function')) throw fail("The provided value is not of type 'ShadowRootInit'.");
        /* the dictionary members, read in order */
        const clonable = !!init.clonable, delegatesFocus = !!init.delegatesFocus, mode = init.mode;
        if (mode === undefined) throw fail("Failed to read the 'mode' property from 'ShadowRootInit': Required member is undefined.");
        const m = String(mode);
        if (m !== 'open' && m !== 'closed') throw fail(`Failed to read the 'mode' property from 'ShadowRootInit': The provided value '${m}' is not a valid enum value of type ShadowRootMode.`);
        const rt = init.referenceTarget, refTarget = rt == null ? null : String(rt);
        const serializable = !!init.serializable, sa = init.slotAssignment === undefined ? 'named' : String(init.slotAssignment);
        if (sa !== 'named' && sa !== 'manual') throw fail(`Failed to read the 'slotAssignment' property from 'ShadowRootInit': The provided value '${sa}' is not a valid enum value of type SlotAssignmentMode.`);
        const ln = this.localName;
        if (this.namespaceURI !== NSURI[0] || !(SHADOW_HOSTS.has(ln) || validCEName(ln)))
            throw new DOMException(`Failed to execute 'attachShadow' on 'Element': This element does not support attachShadow`, 'NotSupportedError');
        const d = registry.byName.get(ln) || (this.getAttribute('is') && registry.byName.get(this.getAttribute('is')));
        if (d && d.ctor && Array.isArray(d.ctor.disabledFeatures) && d.ctor.disabledFeatures.map(String).includes('shadow'))
            throw new DOMException("Failed to execute 'attachShadow' on 'Element': attachShadow() is disabled by disabledFeatures static field.", 'NotSupportedError');
        const cur = N.shadowOf(this);
        if (cur) {
            /* a declarative shadow root of the same mode is emptied and reused */
            const f = N.srFlags(cur);
            if (!(f & 32) || ((f & 1) ? 'closed' : 'open') !== m) throw new DOMException("Failed to execute 'attachShadow' on 'Element': Shadow root cannot be created on a host which already hosts a shadow tree.", 'NotSupportedError');
            for (const k of kids(cur)) removeNode(k);
            N.srSetFlags(cur, 0, 32);
            return cur;
        }
        const sr = N.attachShadow(this, (m === 'closed' ? 1 : 0) | (delegatesFocus ? 2 : 0) | (serializable ? 4 : 0) | (clonable ? 8 : 0) | (sa === 'manual' ? 16 : 0));
        if (Object.getPrototypeOf(sr) !== ShadowRoot.prototype) Object.setPrototypeOf(sr, ShadowRoot.prototype);
        if (refTarget !== null) N.srRef(sr, refTarget);
        return sr;
    },
    get shadowRoot() { const sr = N.shadowOf(this); return sr && !(N.srFlags(sr) & 1) ? sr : null; },
    get assignedSlot() { return slotOpenFor(this); },
    requestFullscreen() { return fsRequest(this); }, webkitRequestFullscreen() { fsRequest(this).catch(() => {}); }, webkitRequestFullScreen() { fsRequest(this).catch(() => {}); },
    requestPointerLock() {}, setPointerCapture(id) { capturePointer(this, id, true); }, releasePointerCapture(id) { capturePointer(this, id, false); }, hasPointerCapture(id) { return hasCapture(this, id); },
    animate(kf, o) {
        const e = new KeyframeEffect(this, kf, o), tl = o !== null && typeof o === 'object' && o.timeline !== undefined ? o.timeline : document.timeline;
        const a = new Animation(e, tl);
        if (o !== null && typeof o === 'object' && o.id !== undefined) a.id = o.id;
        a.play(); return a;
    },
    getAnimations(o) { const sub = !!(o && o.subtree); return waGetAnims(t => t === this || (sub && this.contains(t))); },
    checkVisibility() { return !!N.rect(this); },
});
/* Legacy HTML sinks use the same vetted parser options, but never enable declarative shadow roots. */
function legacyHTMLFragment(context, html, o, m) {
    const cfg = sanFromOptions(o, false, m);
    if (!cfg && !o.runScripts) return N.parseFrag(context, html);
    const cx = N.type(context) === 1 ? context : N.host(context) || ttRead(tplOwner, context) || null;
    const fragment = N.parseFragEx(cx, html, cfg, false, !!o.runScripts);
    if (fragment && o.runScripts && ttReq) ttSlotsFrom(fragment);
    return fragment;
}
function setInner(el, html, o) {
    if (o && (o.sanitizer !== undefined || o.runScripts)) {
        const fragment = legacyHTMLFragment(el, html, o, 'set innerHTML');
        ParentNode.replaceChildren.call(htmlTarget(el), fragment);
        if (el.__ass) readopt(el);
        return;
    }
    const removed = kids(el);
    if (liveRanges.size || liveIters.size) for (let i = removed.length - 1; i >= 0; i--) rangePreRemove(removed[i]);
    for (const r of removed) ceConnected(r, false);
    N.setHTML(el, html);
    if (el.__ass) readopt(el);
    const added = kids(el);
    if (removed.length || added.length) notify('childList', el, { removedNodes: removed, addedNodes: added });
    for (const a of added) ceConnected(a, true);
}
/* getters the reflect* helpers made; 05r-reflect.js replaces those accessors with its spec types */
const REFLECTED = new SSet(), racc = (p, n, g, s) => { REFLECTED.add(g); acc(p, n, g, s); };
/* Reflection invokes the platform attribute algorithm, not overridable instance
   methods. Retain the original methods so mutation/custom-element hooks remain. */
const reflectSetAttrMethod = Element.prototype.setAttribute, reflectRemoveAttrMethod = Element.prototype.removeAttribute;
const reflectSetAttr = (el, name, value) => { if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol value to a string'); reflectSetAttrMethod.call(el, name, value); };
const reflectRemoveAttr = (el, name) => reflectRemoveAttrMethod.call(el, name);

const reflectStr = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); racc(p, n, function () { return N.attr(this, a) ?? ''; }, function (v) { reflectSetAttr(this, a, v); }); } };
const reflectBool = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); racc(p, n, function () { return N.attr(this, a) != null; }, function (v) { if (v) reflectSetAttr(this, a, ''); else reflectRemoveAttr(this, a); }); } };
/* the encoding a node's document gives its URLs' queries; undefined for UTF-8 */
const docEnc = n => { const e = N.charset(n); return e === 'UTF-8' ? undefined : e; };
/* set el's attribute with qualified name n (validated, lowercased) to the string v */
/* an element's contenteditable state: 'true', 'false', 'plaintext-only', or null (inherit) */
function ceState(el) {
    const v = N.attr(el, 'contenteditable');
    if (v == null) return null;
    const s = v.toLowerCase();
    return s === '' || s === 'true' ? 'true' : s === 'false' || s === 'plaintext-only' ? s : null;
}
function attrSetQ(el, n, v) {
    const a = N.attrQ(el, n) == null ? null : attrByQName(el, n);
    if (a && (a._ns !== null || a._pfx !== null)) return attrChange(el, a._ns, a._pfx, a._ln, v);
    const old = N.attrQ(el, n);
    N.setAttrQ(el, n, v);
    if ((n === 'src' || n === 'srcset' || n === 'sizes' || n === 'crossorigin' || n === 'referrerpolicy') && el instanceof H.HTMLImageElement) imgSrcChanged(el);
    if (n.charCodeAt(0) === 111 && n.charCodeAt(1) === 110) handlerAttr(el, n, v);
    notify('attributes', el, { attributeName: n, attributeNamespace: null, oldValue: old });
    ceAttr(el, n, old, v, null);
}
const reflectUrl = (p, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); racc(p, n, function () { const v = N.attr(this, a); if (v == null) return ''; const r = N.urlParse(v, document.baseURI, docEnc(this)); return r ? r[0] : v; }, function (v) { this.setAttribute(a, v); }); } };
const reflectInt = (p, d, ...ns) => { for (const n of ns) { const a = n.toLowerCase(); racc(p, n, function () { const v = parseInt(N.attr(this, a), 10); return isNaN(v) ? d : v; }, function (v) { this.setAttribute(a, String(v | 0)); }); } };
/* HTML GlobalEventHandlers IDL plus the existing compatibility handlers.
 * All properties use the common handler compilation and event dispatch path. */
const EVENT_HANDLERS = 'abort animationend animationiteration animationstart auxclick beforeinput beforematch beforetoggle blur cancel canplay canplaythrough change click close command contextlost contextmenu contextrestored copy cuechange cut dblclick drag dragend dragenter dragleave dragover dragstart drop durationchange emptied ended error focus focusin focusout formdata input invalid keydown keypress keyup load loadeddata loadedmetadata loadstart mousedown mouseenter mouseleave mousemove mouseout mouseover mouseup paste pause play playing pointercancel pointerdown pointerenter pointerleave pointermove pointerout pointerover pointerup progress ratechange reset resize scroll scrollend securitypolicyviolation seeked seeking select selectionchange selectstart slotchange stalled submit suspend timeupdate toggle touchcancel touchend touchmove touchstart transitionend volumechange waiting webkitanimationend webkitanimationiteration webkitanimationstart webkittransitionend wheel'.split(' ');
const EMPTY_SCOPE = Object.freeze(Object.create(null));
/* Body/frameset handlers target their document's Window when required. Only
   forwarded names require a Window; ordinary handlers remain local even on inert bodies. */
function eventHandlerTarget(el, name) {
    if (N.isNode(el) && N.type(el) === 1 && N.ns(el) === 0) {
        const tag = N.name(el);
        if ((tag === 'body' || tag === 'frameset') && WINDOW_FWD.includes(name.slice(2))) {
            const doc = N.ownerDoc(el);
            return doc && doc.defaultView || null;
        }
    }
    return el;
}
function compileHandler(el, k, src) {
    const c = el.__hc || (def(el, '__hc', {}), el.__hc);
    if (c[k] && c[k].src === src) return c[k].fn;
    const doc = N.ownerDoc(el), win = doc && doc.defaultView;
    if (!win) return null;   /* only uncompiled content attributes require scripting */
    if (win !== G) {
        const realm = N.realmOf(win);
        if (realm && realm.compileEventHandler) return realm.compileEventHandler(el, k, src);
    }
    let fn = null;
    /* the handler's scope is the element, its form owner, its document, then the global object */
    try {
        let make;
        const args = k === 'onerror' && eventHandlerTarget(el, k) === win ? 'event, source, lineno, colno, error' : N.ns(el) === 1 ? 'evt' : 'event';
        ttInternal++;
        try { make = new Function('__lumenScope', `with (__lumenScope[0]) with (__lumenScope[1]) with (__lumenScope[2]) return function ${k}(${args}) {\n${src}\n};`); } finally { ttInternal--; }
        let form = null;
        try { form = N.ns(el) === 0 ? el.form : null; } catch (e) {}
        fn = make([doc || EMPTY_SCOPE, form || EMPTY_SCOPE, el]);
    } catch (e) { report(e); }
    c[k] = { src, fn };
    return fn;
}
/* an on* content attribute set or removed: it replaces a handler the IDL attribute set; on <body> and <frameset> the
   window's events' handlers are the window's */
/* document.open deactivates handlers without deleting content attributes or
   depending on whether the page has frozen a platform-object wrapper. */
const openedHandlerState = new WeakMap(), OpenedHandlerSet = Set;
const openedHandlerGet = Function.prototype.call.bind(WeakMap.prototype.get);
const openedHandlerSet = Function.prototype.call.bind(WeakMap.prototype.set);
const openedHandlerHas = Function.prototype.call.bind(Set.prototype.has);
const openedHandlerAdd = Function.prototype.call.bind(Set.prototype.add);
function resetOpenedHandlers(target) { openedHandlerSet(openedHandlerState, target, new OpenedHandlerSet()); }
function reactivateOpenedHandler(target, key) { const state = openedHandlerGet(openedHandlerState, target); if (state) openedHandlerAdd(state, key); }
function handlerAttr(el, k, src) {
    if (el.__on && k in el.__on) delete el.__on[k];
    if (el.__hc) delete el.__hc[k];
    const target = eventHandlerTarget(el, k);
    if (target) reactivateOpenedHandler(target, k);
    if (target && target !== el) {
        if (!target.__on) def(target, '__on', {});
        target.__on[k] = src == null ? null : compileHandler(el, k, src);
    }
}
function installHandlers(proto, attrBacked, list = EVENT_HANDLERS) {
    for (const t of list) {
        const k = 'on' + t;
        acc(proto, k, function () {
            const target = eventHandlerTarget(this, k);
            if (!target) return null;
            if (target !== this) return target[k];
            const cleared = openedHandlerGet(openedHandlerState, target);
            if (cleared && !openedHandlerHas(cleared, k)) return null;
            const own = this.__on && this.__on[k];
            if (own !== undefined) return own;
            if (attrBacked) { const src = N.attr(this, k); if (src != null) return compileHandler(this, k, src); }
            return null;
        }, function (v) {
            const target = eventHandlerTarget(this, k);
            if (!target) return;
            if (target !== this) { target[k] = v; return; }
            if (!this.__on) def(this, '__on', {});
            this.__on[k] = typeof v === 'function' ? v : null;
            reactivateOpenedHandler(this, k);
        });
    }
}
/* pages pick the fullscreen event name by `'on' + name in document` (YouTube's player listens to none without it) */
const SHADOW_HOSTS = new SSet(['article', 'aside', 'blockquote', 'body', 'div', 'footer', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'header', 'main', 'nav', 'p', 'section', 'span']);
const FS_HANDLERS = ['fullscreenchange', 'fullscreenerror', 'webkitfullscreenchange', 'webkitfullscreenerror'];
installHandlers(Element.prototype, true, FS_HANDLERS);
/* ChildNode and ParentNode methods stay out of `with (element)` scopes (inline event handlers) */
const unscopable = (C, names) => Object.defineProperty(C.prototype, Symbol.unscopables, { value: Object.assign(Object.create(null), Object.fromEntries(names.map(n => [n, true]))), configurable: true });
unscopable(Element, ['after', 'append', 'before', 'prepend', 'remove', 'replaceChildren', 'replaceWith', 'slot']);
unscopable(CharacterData, ['after', 'before', 'remove', 'replaceWith']);
unscopable(DocumentType, ['after', 'before', 'remove', 'replaceWith']);
unscopable(DocumentFragment, ['append', 'prepend', 'replaceChildren']);
const makeStyle = (el) => new Proxy(new CSSStyleDeclaration(el), styleHandler);
class HTMLElement extends Element { constructor() { return ceConstruct(new.target, HTMLElement); } }
reflectStr(HTMLElement.prototype, 'title', 'lang', 'accessKey', 'nonce', 'dir');
reflectBool(HTMLElement.prototype, 'hidden', 'inert', 'autofocus');
installHandlers(HTMLElement.prototype, true);
methods(HTMLElement.prototype, {
    get translate() {
        for (let el = this; el && N.type(el) === 1; el = N.parent(el)) {
            if (el.namespaceURI !== NSURI[0]) continue;
            const raw = N.attr(el, 'translate');
            if (raw == null) continue;
            const mode = asciiLower(raw);
            if (mode === '' || mode === 'yes') return true;
            if (mode === 'no') return false;
        }
        return true;
    },
    set translate(v) { reflectSetAttr(this, 'translate', v ? 'yes' : 'no'); },
    get style() { if (!this.__style) def(this, '__style', makeStyle(this)); return this.__style; },
    set style(v) { this.setAttribute('style', v); },
    get dataset() {
        if (this.__ds) return this.__ds;
        const el = this, toAttr = (k) => 'data-' + k.replace(/[A-Z]/g, c => '-' + c.toLowerCase());
        const ds = new Proxy({}, {
            get(t, k) { return typeof k === 'string' ? N.attr(el, toAttr(k)) ?? undefined : undefined; },
            set(t, k, v) { el.setAttribute(toAttr(k), v); return true; },
            deleteProperty(t, k) { el.removeAttribute(toAttr(k)); return true; },
            has(t, k) { return typeof k === 'string' && N.attr(el, toAttr(k)) != null; },
            ownKeys() { return el.getAttributeNames().filter(n => n.startsWith('data-')).map(n => n.slice(5).replace(/-([a-z])/g, (m, c) => c.toUpperCase())); },
            getOwnPropertyDescriptor(t, k) { const v = N.attr(el, toAttr(k)); return v == null ? undefined : { value: v, enumerable: true, configurable: true, writable: true }; },
        });
        def(this, '__ds', ds); return ds;
    },
    get innerText() { return innerTextOf(this); }, set innerText(v) { this.textContent = v; }, get outerText() { return N.text(this); },
    get tabIndex() { const m = /^[\t\n\f\r ]*([-+]?\d+)/.exec(N.attr(this, 'tabindex') || ''), v = m && +m[1] >= -(2 ** 31) && +m[1] < 2 ** 31 ? +m[1] || 0 : NaN; return isNaN(v) ? (/^(a|button|input|select|textarea)$/.test(N.name(this)) ? 0 : -1) : v; },
    set tabIndex(v) { this.setAttribute('tabindex', String(v | 0)); },
    get offsetParent() {
        if (this === document.body || this === document.documentElement || !N.rect(this) || N.computed(this, 'position') === 'fixed') return null;
        const stat = N.computed(this, 'position') === 'static';
        for (let p = N.parent(this); p && N.type(p) === 1; p = N.parent(p)) {
            if (p === document.body) return p;
            const pos = N.computed(p, 'position');
            if ((pos && pos !== 'static') || (stat && /^(td|th|table)$/.test(N.name(p)))) return p;
        }
        return null;
    },
    /* from the offsetParent's padding edge */
    get offsetTop() { const r = N.rect(this); if (!r) return 0; const op = this.offsetParent; const pr = op && op !== document.body ? N.rect(op) : null, pb = pr && N.borders(op); return Math.round(r[1] - (pr ? pr[1] + (pb ? pb[0] : 0) : 0)); },
    get offsetLeft() { const r = N.rect(this); if (!r) return 0; const op = this.offsetParent; const pr = op && op !== document.body ? N.rect(op) : null, pb = pr && N.borders(op); return Math.round(r[0] - (pr ? pr[0] + (pb ? pb[3] : 0) : 0)); },
    get offsetWidth() { const r = N.rect(this); return r ? Math.round(r[2]) : 0; },
    get offsetHeight() { const r = N.rect(this); return r ? Math.round(r[3]) : 0; },
    get isContentEditable() {
        for (let n = this; n && N.type(n) === 1; n = N.parent(n)) { const s = ceState(n); if (s) return s !== 'false'; }
        return N.connected(this) && N.ownerDoc(this) === document && document.designMode === 'on';
    },
    get contentEditable() { return ceState(this) || 'inherit'; },
    set contentEditable(v) {
        const s = String(v).toLowerCase();
        if (s === 'inherit') this.removeAttribute('contenteditable');
        else if (s === 'true' || s === 'false' || s === 'plaintext-only') this.setAttribute('contenteditable', s);
        else throw new DOMException(`Failed to set the 'contentEditable' property on 'HTMLElement': The value provided ('${v}') is not one of 'true', 'false', 'plaintext-only', or 'inherit'.`, 'SyntaxError');
    },
    get draggable() { return N.attr(this, 'draggable') === 'true'; }, set draggable(v) { this.setAttribute('draggable', String(!!v)); },
    focus() { const o = N.active(); if (o === this) return; N.focus(this); if (N.active() !== this) return; if (o) o.dispatchEvent(new FocusEvent('focusout', { bubbles: true, composed: true, relatedTarget: this })); this.dispatchEvent(new FocusEvent('focusin', { bubbles: true, composed: true, relatedTarget: o })); }, blur() { if (N.active() !== this) return; N.focus(null); this.dispatchEvent(new FocusEvent('focusout', { bubbles: true, composed: true, relatedTarget: null })); },
    click() {
        if (this.disabled || this.__clicking) return;
        def(this, '__clicking', true);
        try { dispatch(this, new PointerEvent('click', { bubbles: true, cancelable: true, composed: true, view: G, detail: 1, pointerId: -1, pointerType: '' })); }
        finally { this.__clicking = false; }
    },
    attachInternals() { return { setFormValue() {}, setValidity() {}, checkValidity() { return true; }, reportValidity() { return true; }, states: new SSet(), form: null, labels: [] }; },
});
class SVGElement extends Element {}
installHandlers(SVGElement.prototype, true);
methods(SVGElement.prototype, {
    get style() { if (!this.__style) def(this, '__style', makeStyle(this)); return this.__style; },
    get dataset() { return Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'dataset').get.call(this); },
    get ownerSVGElement() { for (let p = N.parent(this); p && N.type(p) === 1; p = N.parent(p)) if (N.name(p) === 'svg') return p; return null; },
    getBBox() { const r = N.rect(this); return new DOMRect(0, 0, r ? r[2] : 0, r ? r[3] : 0); },
    focus() { N.focus(this); }, blur() {},
});
class SVGGraphicsElement extends SVGElement {}
class SVGSVGElement extends SVGGraphicsElement {}
methods(SVGSVGElement.prototype, { createSVGPoint() { return { x: 0, y: 0, matrixTransform() { return this; } }; }, createSVGMatrix() { return new DOMMatrix(); }, getScreenCTM() { return new DOMMatrix(); } });
class MathMLElement extends Element {}
installHandlers(MathMLElement.prototype, true);
methods(MathMLElement.prototype, {
    get style() { if (!this.__style) def(this, '__style', makeStyle(this)); return this.__style; },
    get dataset() { return Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'dataset').get.call(this); },
    focus() { N.focus(this); }, blur() { if (N.active() === this) N.focus(null); },
});
/* HTMLOrSVGElement's tabIndex, nonce and autofocus on SVG and MathML elements */
for (const P of [SVGElement.prototype, MathMLElement.prototype]) {
    acc(P, 'tabIndex', function () { const v = /^[\t\n\f\r ]*[-+]?\d+/.test(N.attr(this, 'tabindex') || '') ? parseInt(N.attr(this, 'tabindex'), 10) : NaN; return Number.isFinite(v) && Math.abs(v) < 2 ** 31 ? v : N.ns(this) === 1 && N.name(this) === 'a' && N.attr(this, 'href') != null ? 0 : -1; },
        function (v) { this.setAttribute('tabindex', String(v | 0)); });
    acc(P, 'autofocus', function () { return N.attr(this, 'autofocus') != null; }, function (v) { if (v) this.setAttribute('autofocus', ''); else this.removeAttribute('autofocus'); });
    acc(P, 'nonce', function () { return this.__nonce ?? N.attr(this, 'nonce') ?? ''; }, function (v) { def(this, '__nonce', String(v)); });
}

/* ARIAMixin: nullable string reflection of role and aria-* */
for (const k of ['role', 'ariaAtomic', 'ariaAutoComplete', 'ariaBrailleLabel', 'ariaBrailleRoleDescription', 'ariaBusy', 'ariaChecked',
    'ariaColCount', 'ariaColIndex', 'ariaColIndexText', 'ariaColSpan', 'ariaCurrent', 'ariaDescription', 'ariaDisabled', 'ariaExpanded',
    'ariaHasPopup', 'ariaHidden', 'ariaInvalid', 'ariaKeyShortcuts', 'ariaLabel', 'ariaLevel', 'ariaLive', 'ariaModal', 'ariaMultiLine',
    'ariaMultiSelectable', 'ariaOrientation', 'ariaPlaceholder', 'ariaPosInSet', 'ariaPressed', 'ariaReadOnly', 'ariaRelevant',
    'ariaRequired', 'ariaRoleDescription', 'ariaRowCount', 'ariaRowIndex', 'ariaRowIndexText', 'ariaRowSpan', 'ariaSelected',
    'ariaSetSize', 'ariaSort', 'ariaValueMax', 'ariaValueMin', 'ariaValueNow', 'ariaValueText']) {
    const a = k === 'role' ? 'role' : 'aria-' + k.slice(4).toLowerCase();
    acc(Element.prototype, k, function () { return N.attr(this, a) ?? null; }, function (v) { if (v == null) reflectRemoveAttr(this, a); else reflectSetAttr(this, a, v); });
}
/* the element a reference to el ends at, following shadow roots' referenceTarget; null when one names no element */
function refResolve(el) {
    for (let i = 0; el && i < 64; i++) {
        const sr = N.isNode(el) && N.type(el) === 1 ? N.shadowOf(el) : null, rt = sr ? N.srRef(sr) : null;
        if (rt === null || rt === undefined) return el;
        el = rt ? N.query(sr, '#' + CSS.escape(rt), false) : null;
    }
    return el || null;
}
/* element reflection (HTML "reflect" for Element / FrozenArray<Element> attributes): explicitly set elements count
   while they are in this element's tree or a shadow-including ancestor tree of it */
const refExplicit = new WeakMap();
const refVisible = (from, e) => { const t = e.getRootNode(); for (let r = from.getRootNode(); r; r = r.host ? r.host.getRootNode() : null) if (r === t) return true; return false; };
const refById = (el, id) => { const r = el.getRootNode(); return r && typeof r.getElementById === 'function' ? r.getElementById(id) : null; };
function reflectElement(proto, prop, attr) {
    acc(proto, prop, function () {
        const m = refExplicit.get(this), e = m && m[attr];
        if (e) return refVisible(this, e) ? e : null;
        const id = N.attr(this, attr);
        return id ? refById(this, id) : null;
    }, function (v) {
        let m = refExplicit.get(this);
        if (v == null) { if (m) delete m[attr]; this.removeAttribute(attr); return; }
        if (!(v instanceof Element)) throw new TypeError(`Failed to set the '${prop}' property: The provided value is not of type 'Element'.`);
        if (!m) refExplicit.set(this, m = {});
        this.setAttribute(attr, ''); m[attr] = v;
    });
}
function reflectElements(proto, prop, attr) {
    acc(proto, prop, function () {
        const m = refExplicit.get(this), l = m && m[attr];
        const out = l ? l.filter(e => refVisible(this, e)) : (N.attr(this, attr) ?? null) === null ? null
            : N.attr(this, attr).split(/[\t\n\f\r ]+/).filter(Boolean).map(id => refById(this, id)).filter(Boolean);
        if (out === null) return null;
        const c = m && m['#' + attr];
        if (c && c.length === out.length && c.every((e, i) => e === out[i])) return c;
        const f = Object.freeze(out);
        let mm = m; if (!mm) refExplicit.set(this, mm = {});
        mm['#' + attr] = f;
        return f;
    }, function (v) {
        let m = refExplicit.get(this);
        if (v == null) { if (m) delete m[attr]; this.removeAttribute(attr); return; }
        const l = [...v];
        if (!l.every(e => e instanceof Element)) throw new TypeError(`Failed to set the '${prop}' property: The provided value is not of type 'Element'.`);
        if (!m) refExplicit.set(this, m = {});
        this.setAttribute(attr, ''); m[attr] = l;
    });
}
reflectElement(Element.prototype, 'ariaActiveDescendantElement', 'aria-activedescendant');
for (const [p, a] of [['ariaControlsElements', 'aria-controls'], ['ariaDescribedByElements', 'aria-describedby'], ['ariaDetailsElements', 'aria-details'],
    ['ariaErrorMessageElements', 'aria-errormessage'], ['ariaFlowToElements', 'aria-flowto'], ['ariaLabelledByElements', 'aria-labelledby'], ['ariaOwnsElements', 'aria-owns']])
    reflectElements(Element.prototype, p, a);

/* innerText (HTML "rendered text collection steps"), from computed display, white-space, visibility and
   text-transform; elements that aren't rendered give their text content */
function innerTextOf(el) {
    const cs = (e, p) => N.computed(e, p) || '';
    for (let a = el; a; a = a.parentElement) if (cs(a, 'display') === 'none') return el.textContent;
    if (!el.isConnected) return el.textContent;
    const BLOCK = /^(block|flow-root|flex|grid|table|list-item|table-caption|-webkit-box|block flow|block flow-root|block flex|block grid|block table)/;
    const NOKIDS = /^(TEXTAREA|INPUT|IMG|VIDEO|AUDIO|CANVAS|IFRAME|OBJECT|EMBED|SELECT)$/;
    const items = [];
    let pendSpace = false, lineStart = true;
    const trimTail = () => { if (pendSpace) { const k = items.length - 1; if (typeof items[k] === 'string') items[k] = items[k].slice(0, -1); pendSpace = false; } };
    const text = (t, coll) => {
        if (coll) { if ((pendSpace || lineStart) && t[0] === ' ') t = t.slice(1); }
        if (!t) return;
        items.push(t); lineStart = t.endsWith('\n'); pendSpace = coll && t.endsWith(' ');
    };
    const brk = k => { trimTail(); items.push(k); lineStart = true; };
    const xform = (t, tt) => tt === 'uppercase' ? t.toUpperCase() : tt === 'lowercase' ? t.toLowerCase() : tt === 'capitalize' ? t.replace(/(^|[^\p{L}\p{N}'’])(\p{L})/gu, (m, a, b) => a + b.toUpperCase()) : t;
    const walk = n => {
        for (let c = n.firstChild; c; c = c.nextSibling) {
            if (c.nodeType === 3) {
                const par = c.parentElement || n;
                if (cs(par, 'visibility') === 'hidden' || cs(par, 'visibility') === 'collapse') continue;
                const ws = cs(par, 'white-space') || 'normal';
                let t = c.data;
                const coll = !/^(pre|pre-wrap|break-spaces)$/.test(ws) && !/preserve/.test(ws);
                if (coll) {
                    if (/pre-line|preserve-breaks/.test(ws)) t = t.replace(/[ \t\r]+/g, ' ').replace(/ ?\n ?/g, '\n');
                    else t = t.replace(/[ \t\r]*\n[ \t\r]*/g, ' ').replace(/[ \t\r\n]+/g, ' ');
                } else t = t.replace(/\r\n?/g, '\n');
                t = xform(t, cs(par, 'text-transform'));
                if (coll && t.startsWith('\n')) trimTail();
                text(t, coll);
            } else if (c.nodeType === 1) {
                const d = cs(c, 'display');
                if (d === 'none') continue;
                if (c.localName === 'br' && c.namespaceURI === 'http://www.w3.org/1999/xhtml') { if (cs(c, 'visibility') === 'visible' || !cs(c, 'visibility')) { trimTail(); items.push('\n'); lineStart = true; pendSpace = false; } continue; }
                if (d === 'contents') { walk(c); continue; }
                const k = c.localName === 'p' && c.namespaceURI === 'http://www.w3.org/1999/xhtml' ? 2 : BLOCK.test(d) ? 1 : 0;
                if (k) brk(k);
                if (!NOKIDS.test(c.tagName)) walk(c);
                if (k) brk(k);
                else if (d === 'table-cell') { let nx = c.nextElementSibling; while (nx && cs(nx, 'display') === 'none') nx = nx.nextElementSibling; if (nx) { trimTail(); items.push('\t'); lineStart = false; pendSpace = false; } }
                else if (d === 'table-row') { let nx = c.nextElementSibling; while (nx && cs(nx, 'display') === 'none') nx = nx.nextElementSibling; if (nx || c.parentElement?.nextElementSibling) brk(1); }
                else if (/^table-(row-group|header-group|footer-group)$/.test(d) && c.nextElementSibling) brk(1);
            }
        }
    };
    walk(el);
    trimTail();
    let out = '', req = -1;
    for (const it of items) {
        if (typeof it === 'number') { if (out !== '' ) req = Math.max(req, it); continue; }
        if (req > 0) out += '\n'.repeat(req);
        req = -1; out += it;
    }
    return out;
}
