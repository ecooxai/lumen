/* XPath 1.0 (document.evaluate, XPathEvaluator, XPathExpression, XPathResult): expressions compile to
   closures over the public DOM. Node-sets are arrays in document order; attribute nodes are cached
   per element so the same attribute is the same node across steps and evaluations. In HTML documents
   unprefixed name tests match HTML elements and attributes case-insensitively, as in browsers. */
const xpErr = (msg, name) => new DOMException(msg, name || 'SyntaxError');
const xpAttrs = new WeakMap();
function xpAttr(el, name) {
    let m = xpAttrs.get(el); if (!m) xpAttrs.set(el, m = new Map());
    let a = m.get(name); if (!a) m.set(name, a = new Attr(el, name));
    return a;
}
const xpParent = n => n.nodeType === 2 ? n.ownerElement : n.parentNode;
const xpHtmlDoc = n => { const d = n.nodeType === 9 ? n : n.ownerDocument; return !!d && N.ns(d) !== 3; };
function xpStr(n) {
    const t = n.nodeType;
    if (t === 1 || t === 9 || t === 11) { let s = ''; for (let c = n.firstChild; c && c !== n;) {
        if (c.nodeType === 3 || c.nodeType === 4) s += c.data;
        if (c.firstChild && c.nodeType !== 3) { c = c.firstChild; continue; }
        while (c && c !== n && !c.nextSibling) c = c.parentNode;
        if (c && c !== n) c = c.nextSibling;
    } return s; }
    if (t === 2) return n.value;
    return n.nodeValue ?? '';
}
/* conversions */
const xpNum = v => typeof v === 'number' ? v : typeof v === 'boolean' ? +v : typeof v === 'string' ? (/^\s*-?(\d+(\.\d*)?|\.\d+)\s*$/.test(v) ? +v : NaN) : xpNum(xpString(v));
const xpString = v => typeof v === 'string' ? v : typeof v === 'boolean' ? String(v) : typeof v === 'number' ? (Object.is(v, -0) ? '0' : Number.isInteger(v) && Math.abs(v) >= 1e21 ? BigInt(v).toString() : String(v)) : v.length ? xpStr(v[0]) : '';
const xpBool = v => typeof v === 'boolean' ? v : typeof v === 'number' ? v !== 0 && !Number.isNaN(v) : typeof v === 'string' ? v.length > 0 : v.length > 0;
/* document order: a preorder index of each tree the nodes are in, attributes right after their element */
function xpSort(a) {
    if (a.length < 2) return a;
    const u = [...new Set(a)];
    if (u.length < 2) return u;
    const ix = new Map(); let k = 0;
    for (const n of u) {
        if (ix.has(n)) continue;
        let r = n; for (let p; (p = xpParent(r));) r = p;
        if (ix.has(r)) continue;
        for (let c = r; c;) {
            ix.set(c, k++);
            if (c.nodeType === 1) { const m = xpAttrs.get(c); if (m) { const names = c.getAttributeNames(); for (const [nm, at] of m) ix.set(at, k + names.indexOf(nm) / (names.length + 1)); } }
            if (c.firstChild) { c = c.firstChild; continue; }
            while (c && c !== r && !c.nextSibling) c = c.parentNode;
            c = c && c !== r ? c.nextSibling : null;
        }
    }
    return u.sort((x, y) => (ix.get(x) ?? 1e15) - (ix.get(y) ?? 1e15));
}
/* axes: nodes in axis order (reverse axes nearest first) */
function* xpDesc(n) {
    for (let c = n.firstChild; c;) {
        yield c;
        if (c.firstChild) { c = c.firstChild; continue; }
        while (c && c !== n && !c.nextSibling) c = c.parentNode;
        c = c && c !== n ? c.nextSibling : null;
    }
}
const xpAxes = {
    child: function* (n) { if (n.nodeType !== 2) for (let c = n.firstChild; c; c = c.nextSibling) yield c; },
    descendant: function* (n) { if (n.nodeType !== 2) yield* xpDesc(n); },
    'descendant-or-self': function* (n) { yield n; if (n.nodeType !== 2) yield* xpDesc(n); },
    self: function* (n) { yield n; },
    parent: function* (n) { const p = xpParent(n); if (p) yield p; },
    ancestor: function* (n) { for (let p = xpParent(n); p; p = p.parentNode) yield p; },
    'ancestor-or-self': function* (n) { yield n; for (let p = xpParent(n); p; p = p.parentNode) yield p; },
    'following-sibling': function* (n) { if (n.nodeType !== 2) for (let c = n.nextSibling; c; c = c.nextSibling) yield c; },
    'preceding-sibling': function* (n) { if (n.nodeType !== 2) for (let c = n.previousSibling; c; c = c.previousSibling) yield c; },
    following: function* (n) {
        let c = n.nodeType === 2 ? n.ownerElement : n;
        if (n.nodeType === 2 && c) yield* xpDesc(c);
        for (; c; c = c.parentNode) for (let s = c.nextSibling; s; s = s.nextSibling) { yield s; yield* xpDesc(s); }
    },
    preceding: function* (n) {
        let c = n.nodeType === 2 ? n.ownerElement : n;
        for (; c; c = c.parentNode) for (let s = c.previousSibling; s; s = s.previousSibling) {
            const d = [...xpDesc(s)]; for (let i = d.length - 1; i >= 0; i--) yield d[i];
            yield s;
        }
    },
    attribute: function* (n) { if (n.nodeType === 1) for (const nm of n.getAttributeNames()) if (nm !== 'xmlns' && !nm.startsWith('xmlns:')) yield xpAttr(n, nm); },
    namespace: function* () {},
};
const xpReverse = new Set(['ancestor', 'ancestor-or-self', 'preceding', 'preceding-sibling']);
/* tokens: {t: 'num'|'str'|'name'|'var'|'op', v} */
function xpLex(s) {
    const out = []; let i = 0;
    const nc = /[A-Za-z_À-￿][\w.\-·À-￿]*/y;
    while (i < s.length) {
        const c = s[i];
        if (/\s/.test(c)) { i++; continue; }
        if (c === '"' || c === "'") { const e = s.indexOf(c, i + 1); if (e < 0) throw xpErr('Unterminated string literal'); out.push({ t: 'str', v: s.slice(i + 1, e) }); i = e + 1; continue; }
        if (/\d/.test(c) || (c === '.' && /\d/.test(s[i + 1] || ''))) { const m = /\d*(\.\d*)?/y; m.lastIndex = i; const r = m.exec(s)[0]; out.push({ t: 'num', v: +r }); i += r.length; continue; }
        const two = s.substr(i, 2);
        if (['//', '..', '::', '!=', '<=', '>='].includes(two)) { out.push({ t: 'op', v: two }); i += 2; continue; }
        if ('/()[].@,|+-=<>*'.includes(c)) { out.push({ t: 'op', v: c }); i++; continue; }
        if (c === '$') { nc.lastIndex = i + 1; const m = nc.exec(s); if (!m) throw xpErr('Bad variable reference'); out.push({ t: 'var', v: m[0] }); i += 1 + m[0].length; continue; }
        nc.lastIndex = i; const m = nc.exec(s);
        if (!m) throw xpErr("The string '" + s + "' is not a valid XPath expression.");
        let v = m[0]; i += v.length;
        if (s[i] === ':' && s[i + 1] !== ':') {
            if (s[i + 1] === '*') { v += ':*'; i += 2; }
            else { nc.lastIndex = i + 1; const m2 = nc.exec(s); if (!m2) throw xpErr('Bad qualified name'); v += ':' + m2[0]; i += 1 + m2[0].length; }
        }
        out.push({ t: 'name', v });
    }
    return out;
}
const xpNodeTypes = new Set(['node', 'text', 'comment', 'processing-instruction']);
function xpCompile(src, resolver) {
    const tk = xpLex(src); let p = 0;
    const peek = (o = 0) => tk[p + o], isOp = (v, o = 0) => { const t = tk[p + o]; return !!t && t.t === 'op' && t.v === v; };
    const isName = (v, o = 0) => { const t = tk[p + o]; return !!t && t.t === 'name' && t.v === v; };
    const take = v => { if (!isOp(v)) throw xpErr("The string '" + src + "' is not a valid XPath expression."); p++; };
    const nsOf = pfx => {
        let u = null;
        if (typeof resolver === 'function') u = resolver(pfx);
        else if (resolver && typeof resolver.lookupNamespaceURI === 'function') u = resolver.lookupNamespaceURI(pfx);
        if (u == null || u === '') { if (pfx === 'xml') return 'http://www.w3.org/XML/1998/namespace'; throw xpErr("The string '" + pfx + "' is not a valid namespace prefix.", 'NamespaceError'); }
        return String(u);
    };
    const bin = (next, ops) => () => {
        let l = next();
        for (;;) {
            const t = peek(); if (!t) return l;
            const op = t.t === 'op' && ops.includes(t.v) ? t.v : t.t === 'name' && ops.includes(t.v) ? t.v : null;
            if (!op) return l;
            p++; const a = l, b = next(); l = xpBinary(op, a, b);
        }
    };
    const unary = () => { if (isOp('-')) { p++; const e = unary(); return c => -xpNum(e(c)); } return union(); };
    const mult = bin(unary, ['*', 'div', 'mod']), add = bin(mult, ['+', '-']), rel = bin(add, ['<', '>', '<=', '>=']), eq = bin(rel, ['=', '!=']), and = bin(eq, ['and']), or = bin(and, ['or']);
    function union() {
        let l = path();
        while (isOp('|')) { p++; const a = l, b = path(); l = c => { const x = a(c), y = b(c); if (!Array.isArray(x) || !Array.isArray(y)) throw new TypeError('The union operator needs node-sets.'); return xpSort(x.concat(y)); }; }
        return l;
    }
    function predicates() { const ps = []; while (isOp('[')) { p++; ps.push(or()); take(']'); } return ps; }
    function primary() {
        const t = peek();
        if (!t) throw xpErr("The string '" + src + "' is not a valid XPath expression.");
        if (t.t === 'num' || t.t === 'str') { p++; const v = t.v; return () => v; }
        if (t.t === 'var') { p++; const nm = t.v; return () => { throw xpErr('Undefined variable $' + nm, 'NotSupportedError'); }; }
        if (isOp('(')) { p++; const e = or(); take(')'); return e; }
        p++; take('(');
        const args = [];
        if (!isOp(')')) { args.push(or()); while (isOp(',')) { p++; args.push(or()); } }
        take(')');
        const f = xpFuncs[t.v];
        if (!f) throw xpErr("The string '" + src + "' is not a valid XPath expression.");
        return c => f(c, args);
    }
    const startsFilter = () => { const t = peek(); return t && (t.t === 'num' || t.t === 'str' || t.t === 'var' || isOp('(') || (t.t === 'name' && isOp('(', 1) && !xpNodeTypes.has(t.v))); };
    function path() {
        if (startsFilter()) {
            let e = primary();
            const ps = predicates();
            if (ps.length) { const base = e; e = c => { const v = base(c); if (!Array.isArray(v)) throw new TypeError('Predicates apply only to node-sets.'); return xpFilter(v, ps, c, false); }; }
            if (isOp('/') || isOp('//')) { const steps = relSteps(); const base = e; return c => { const v = base(c); if (!Array.isArray(v)) throw new TypeError('Paths apply only to node-sets.'); return xpRun(v, steps, c); }; }
            return e;
        }
        let abs = false, steps = [];
        if (isOp('/')) {
            p++; abs = true;
            const t = peek();
            if (t && (t.t === 'name' || isOp('.') || isOp('..') || isOp('@') || (isOp('*') ))) steps = relSteps(true);
        } else if (isOp('//')) { abs = true; steps = relSteps(); }
        else steps = relSteps(true);
        return c => {
            let n = c.node;
            if (abs) { while (xpParent(n)) n = xpParent(n); }
            return xpRun([n], steps, c);
        };
    }
    function relSteps(first) {
        const steps = [];
        if (first) steps.push(step());
        while (isOp('/') || isOp('//')) {
            if (isOp('//')) steps.push({ axis: 'descendant-or-self', test: () => true, ps: [] });
            p++; steps.push(step());
        }
        /* descendant-or-self::node()/child::x with no predicates is descendant::x */
        for (let i = 0; i + 1 < steps.length; i++)
            if (steps[i].axis === 'descendant-or-self' && steps[i].any && steps[i + 1].axis === 'child' && !steps[i + 1].ps.length) { steps[i + 1] = { ...steps[i + 1], axis: 'descendant' }; steps.splice(i, 1); i--; }
        return steps;
    }
    function step() {
        if (isOp('.')) { p++; return { axis: 'self', test: () => true, ps: [] }; }
        if (isOp('..')) { p++; return { axis: 'parent', test: () => true, ps: [] }; }
        let axis = 'child';
        if (isOp('@')) { p++; axis = 'attribute'; }
        else if (peek() && peek().t === 'name' && isOp('::', 1)) {
            axis = peek().v; if (!xpAxes[axis]) throw xpErr("The string '" + src + "' is not a valid XPath expression."); p += 2;
        }
        const t = peek(); let test, any = false;
        if (isOp('*')) { p++; test = n => n.nodeType === (axis === 'attribute' ? 2 : 1); }
        else if (t && t.t === 'name' && xpNodeTypes.has(t.v) && isOp('(', 1)) {
            p += 2; let lit = null;
            if (t.v === 'processing-instruction' && peek() && peek().t === 'str') { lit = peek().v; p++; }
            take(')');
            test = t.v === 'node' ? (any = true, () => true) : t.v === 'text' ? n => n.nodeType === 3 || n.nodeType === 4 : t.v === 'comment' ? n => n.nodeType === 8 : n => n.nodeType === 7 && (lit === null || n.target === lit);
        } else if (t && t.t === 'name') {
            p++;
            const colon = t.v.indexOf(':'), principal = axis === 'attribute' ? 2 : 1;
            if (colon >= 0) {
                const ns = nsOf(t.v.slice(0, colon)), local = t.v.slice(colon + 1);
                test = local === '*' ? n => n.nodeType === principal && n.namespaceURI === ns : n => n.nodeType === principal && n.namespaceURI === ns && n.localName === local;
            } else {
                const local = t.v, lower = local.toLowerCase();
                test = principal === 2 ? n => n.nodeType === 2 && (n.localName === local || (xpHtmlDoc(n.ownerElement) && n.ownerElement.namespaceURI === 'http://www.w3.org/1999/xhtml' && n.localName === lower))
                    : n => n.nodeType === 1 && (n.namespaceURI === 'http://www.w3.org/1999/xhtml' && xpHtmlDoc(n) ? n.localName === lower : n.namespaceURI == null && n.localName === local);
            }
        } else throw xpErr("The string '" + src + "' is not a valid XPath expression.");
        return { axis, test, any, ps: predicates() };
    }
    const e = or();
    if (p < tk.length) throw xpErr("The string '" + src + "' is not a valid XPath expression.");
    return e;
}
function xpFilter(nodes, ps, c, reverse) {
    for (const pr of ps) {
        const size = nodes.length, kept = [];
        for (let i = 0; i < size; i++) {
            const v = pr({ node: nodes[i], pos: reverse ? size - i : i + 1, size });
            if (typeof v === 'number' ? v === (reverse ? size - i : i + 1) : xpBool(v)) kept.push(nodes[i]);
        }
        nodes = kept;
    }
    return nodes;
}
function xpRun(ctx, steps, c) {
    for (const s of steps) {
        const rev = xpReverse.has(s.axis), out = [];
        for (const n of ctx) {
            let got = [];
            for (const x of xpAxes[s.axis](n)) if (s.test(x)) got.push(x);
            if (s.ps.length) got = xpFilter(got, s.ps, c, false);   /* positions count in axis order */
            if (rev) got.reverse();
            for (const x of got) out.push(x);
        }
        ctx = ctx.length > 1 || rev || s.axis === 'attribute' ? xpSort(out) : out;
    }
    return ctx;
}
function xpCmp(op, a, b) {
    const f = op === '=' ? (x, y) => x === y : op === '!=' ? (x, y) => x !== y : op === '<' ? (x, y) => x < y : op === '>' ? (x, y) => x > y : op === '<=' ? (x, y) => x <= y : (x, y) => x >= y;
    const eqOp = op === '=' || op === '!=';
    const A = Array.isArray(a), B = Array.isArray(b);
    if (A && B) { const bs = b.map(xpStr); return a.some(x => { const sx = xpStr(x); return bs.some(sy => eqOp ? f(sx, sy) : f(xpNum(sx), xpNum(sy))); }); }
    if (A || B) {
        const set = A ? a : b, o = A ? b : a, g = A ? f : (x, y) => f(y, x);
        if (typeof o === 'boolean') return g(xpBool(set), o);
        if (typeof o === 'number') return set.some(x => g(xpNum(xpStr(x)), o));
        return set.some(x => eqOp ? g(xpStr(x), o) : g(xpNum(xpStr(x)), xpNum(o)));
    }
    if (eqOp) {
        if (typeof a === 'boolean' || typeof b === 'boolean') return f(xpBool(a), xpBool(b));
        if (typeof a === 'number' || typeof b === 'number') return f(xpNum(a), xpNum(b));
        return f(xpString(a), xpString(b));
    }
    return f(xpNum(a), xpNum(b));
}
function xpBinary(op, a, b) {
    switch (op) {
    case 'or': return c => xpBool(a(c)) || xpBool(b(c));
    case 'and': return c => xpBool(a(c)) && xpBool(b(c));
    case '+': return c => xpNum(a(c)) + xpNum(b(c));
    case '-': return c => xpNum(a(c)) - xpNum(b(c));
    case '*': return c => xpNum(a(c)) * xpNum(b(c));
    case 'div': return c => xpNum(a(c)) / xpNum(b(c));
    case 'mod': return c => xpNum(a(c)) % xpNum(b(c));
    default: return c => xpCmp(op, a(c), b(c));
    }
}
const xpSet = (c, e) => { const v = e(c); if (!Array.isArray(v)) throw new TypeError('Expected a node-set.'); return v; };
const xpArg = (c, args, i) => args[i] ? xpString(args[i](c)) : xpStr(c.node);
const xpFuncs = {
    last: c => c.size, position: c => c.pos,
    count: (c, a) => xpSet(c, a[0]).length,
    id: (c, a) => {
        const v = a[0](c), ids = (Array.isArray(v) ? v.map(xpStr).join(' ') : xpString(v)).split(/\s+/).filter(Boolean);
        const d = c.node.nodeType === 9 ? c.node : c.node.ownerDocument;
        return xpSort(ids.map(i => d && d.getElementById(i)).filter(Boolean));
    },
    'local-name': (c, a) => { const n = a[0] ? xpSet(c, a[0])[0] : c.node; return !n ? '' : n.nodeType === 1 || n.nodeType === 2 ? n.localName : n.nodeType === 7 ? n.target : ''; },
    'namespace-uri': (c, a) => { const n = a[0] ? xpSet(c, a[0])[0] : c.node; return n && (n.nodeType === 1 || n.nodeType === 2) ? n.namespaceURI || '' : ''; },
    name: (c, a) => { const n = a[0] ? xpSet(c, a[0])[0] : c.node; return !n ? '' : n.nodeType === 1 ? (n.prefix ? n.prefix + ':' : '') + n.localName : n.nodeType === 2 ? n.name : n.nodeType === 7 ? n.target : ''; },
    string: (c, a) => xpArg(c, a, 0),
    concat: (c, a) => a.map(e => xpString(e(c))).join(''),
    'starts-with': (c, a) => xpString(a[0](c)).startsWith(xpString(a[1](c))),
    contains: (c, a) => xpString(a[0](c)).includes(xpString(a[1](c))),
    'substring-before': (c, a) => { const s = xpString(a[0](c)), t = xpString(a[1](c)), i = s.indexOf(t); return i < 0 ? '' : s.slice(0, i); },
    'substring-after': (c, a) => { const s = xpString(a[0](c)), t = xpString(a[1](c)), i = s.indexOf(t); return i < 0 ? '' : s.slice(i + t.length); },
    substring: (c, a) => {
        const s = [...xpString(a[0](c))], st = Math.round(xpNum(a[1](c))), ln = a[2] ? Math.round(xpNum(a[2](c))) : Infinity;
        let r = ''; for (let i = 0; i < s.length; i++) { const q = i + 1; if (q >= st && q < st + ln) r += s[i]; } return r;
    },
    'string-length': (c, a) => [...xpArg(c, a, 0)].length,
    'normalize-space': (c, a) => xpArg(c, a, 0).replace(/[\x20\t\r\n]+/g, ' ').replace(/^ | $/g, ''),
    translate: (c, a) => {
        const s = xpString(a[0](c)), from = [...xpString(a[1](c))], to = [...xpString(a[2](c))];
        let r = ''; for (const ch of s) { const i = from.indexOf(ch); r += i < 0 ? ch : i < to.length ? to[i] : ''; } return r;
    },
    boolean: (c, a) => xpBool(a[0](c)), not: (c, a) => !xpBool(a[0](c)), true: () => true, false: () => false,
    lang: (c, a) => {
        const want = xpString(a[0](c)).toLowerCase();
        for (let n = c.node.nodeType === 1 ? c.node : xpParent(c.node); n && n.nodeType === 1; n = n.parentNode) {
            const l = n.getAttribute('xml:lang') ?? n.getAttribute('lang');
            if (l != null) { const v = l.toLowerCase(); return v === want || v.startsWith(want + '-'); }
        }
        return false;
    },
    number: (c, a) => a[0] ? xpNum(a[0](c)) : xpNum(xpStr(c.node)),
    sum: (c, a) => xpSet(c, a[0]).reduce((s, n) => s + xpNum(xpStr(n)), 0),
    floor: (c, a) => Math.floor(xpNum(a[0](c))), ceiling: (c, a) => Math.ceil(xpNum(a[0](c))),
    round: (c, a) => { const v = xpNum(a[0](c)); return Number.isNaN(v) || !Number.isFinite(v) ? v : v < 0 && v >= -0.5 ? -0 : Math.floor(v + 0.5); },
};
class XPathResult {
    constructor() { illegal(); }
    get resultType() { return this._type; }
    get numberValue() { if (this._type !== 1) throw new TypeError('The result type is not a number.'); return this._v; }
    get stringValue() { if (this._type !== 2) throw new TypeError('The result type is not a string.'); return this._v; }
    get booleanValue() { if (this._type !== 3) throw new TypeError('The result type is not a boolean.'); return this._v; }
    get singleNodeValue() { if (this._type !== 8 && this._type !== 9) throw new TypeError('The result type is not a single node.'); return this._v[0] ?? null; }
    get invalidIteratorState() { return false; }
    get snapshotLength() { if (this._type !== 6 && this._type !== 7) throw new TypeError('The result type is not a snapshot.'); return this._v.length; }
    snapshotItem(i) { if (this._type !== 6 && this._type !== 7) throw new TypeError('The result type is not a snapshot.'); return this._v[i] ?? null; }
    iterateNext() { if (this._type !== 4 && this._type !== 5) throw new TypeError('The result type is not an iterator.'); return this._v[this._i++] ?? null; }
}
Object.entries({ ANY_TYPE: 0, NUMBER_TYPE: 1, STRING_TYPE: 2, BOOLEAN_TYPE: 3, UNORDERED_NODE_ITERATOR_TYPE: 4, ORDERED_NODE_ITERATOR_TYPE: 5, UNORDERED_NODE_SNAPSHOT_TYPE: 6, ORDERED_NODE_SNAPSHOT_TYPE: 7, ANY_UNORDERED_NODE_TYPE: 8, FIRST_ORDERED_NODE_TYPE: 9 })
    .forEach(([k, v]) => { Object.defineProperty(XPathResult, k, { value: v, enumerable: true }); Object.defineProperty(XPathResult.prototype, k, { value: v, enumerable: true }); });
function xpResult(v, type) {
    type = toU32(type ?? 0) & 0xFFFF;
    if (type === 0) type = typeof v === 'number' ? 1 : typeof v === 'string' ? 2 : typeof v === 'boolean' ? 3 : 4;
    const r = Object.create(XPathResult.prototype);
    def(r, '_type', type); def(r, '_i', 0);
    if (type === 1) def(r, '_v', xpNum(v));
    else if (type === 2) def(r, '_v', xpString(v));
    else if (type === 3) def(r, '_v', xpBool(v));
    else if (type >= 4 && type <= 9) { if (!Array.isArray(v)) throw new TypeError('The result is not a node-set.'); def(r, '_v', v); }
    else throw new DOMException('The result type is not supported.', 'NotSupportedError');
    return r;
}
class XPathExpression {
    constructor() { illegal(); }
    evaluate(node, type = 0, result = null) {
        if (!N.isNode(node) && !(node instanceof Attr)) throw new TypeError("Failed to execute 'evaluate' on 'XPathExpression': parameter 1 is not of type 'Node'.");
        return xpResult(this._f({ node, pos: 1, size: 1 }), type);
    }
}
const xpCreate = (expr, resolver) => { const e = Object.create(XPathExpression.prototype); def(e, '_f', xpCompile(String(expr), resolver ?? null)); return e; };
const xpMixin = {
    createExpression(expr, resolver = null) { return xpCreate(expr, resolver); },
    createNSResolver(n) { return n; },
    evaluate(expr, node, resolver = null, type = 0, result = null) { return xpCreate(expr, resolver).evaluate(node, type, result); },
};
class XPathEvaluator { }
Object.assign(XPathEvaluator.prototype, xpMixin);
for (const k of Object.keys(xpMixin)) Object.defineProperty(Document.prototype, k, { value: xpMixin[k], writable: true, configurable: true });
