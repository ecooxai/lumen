/* CSS Typed OM: style values, numeric math, transforms and the element/rule style maps. */
const TOM = Symbol('tom');
const tomIllegal = () => { throw new TypeError('Illegal constructor'); };
const tomData = v => { if (!v || !Object.prototype.hasOwnProperty.call(v, '_tom')) throw new TypeError('Illegal invocation'); return v._tom; };
const tomInit = (o, d) => Object.defineProperty(o, '_tom', { value: d, writable: true });
const TOM_UNITS = {
    number: ['', 1], percent: ['percent', 1],
    em: ['length'], rem: ['length'], ex: ['length'], rex: ['length'], ch: ['length'], rch: ['length'], ic: ['length'], ric: ['length'], cap: ['length'], rcap: ['length'], lh: ['length'], rlh: ['length'],
    vw: ['length'], vh: ['length'], vi: ['length'], vb: ['length'], vmin: ['length'], vmax: ['length'],
    svw: ['length'], svh: ['length'], svi: ['length'], svb: ['length'], svmin: ['length'], svmax: ['length'],
    lvw: ['length'], lvh: ['length'], lvi: ['length'], lvb: ['length'], lvmin: ['length'], lvmax: ['length'],
    dvw: ['length'], dvh: ['length'], dvi: ['length'], dvb: ['length'], dvmin: ['length'], dvmax: ['length'],
    cqw: ['length'], cqh: ['length'], cqi: ['length'], cqb: ['length'], cqmin: ['length'], cqmax: ['length'],
    px: ['length', 1], cm: ['length', 96 / 2.54], mm: ['length', 96 / 25.4], q: ['length', 96 / 101.6], in: ['length', 96], pt: ['length', 4 / 3], pc: ['length', 16],
    deg: ['angle', 1], grad: ['angle', 0.9], rad: ['angle', 180 / Math.PI], turn: ['angle', 360],
    s: ['time', 1], ms: ['time', 0.001], hz: ['frequency', 1], khz: ['frequency', 1000],
    dpi: ['resolution', 1 / 96], dpcm: ['resolution', 2.54 / 96], dppx: ['resolution', 1], x: ['resolution', 1], fr: ['flex', 1],
};
const TOM_BASE = ['length', 'angle', 'time', 'frequency', 'resolution', 'flex', 'percent'];
const tomUnit = u => { u = String(u).toLowerCase(); if (u === '%') u = 'percent'; if (!Object.prototype.hasOwnProperty.call(TOM_UNITS, u)) throw new TypeError(`Invalid unit: ${u}`); return u; };
const tomCat = u => TOM_UNITS[u.toLowerCase()][0];
const tomNum = (v, what = 'value') => { v = Number(v); if (!Number.isFinite(v) && !Number.isNaN(v) && Math.abs(v) !== Infinity) throw new TypeError(`Invalid ${what}`); return v; };
/* a numeric type: { length: 1, ... , percentHint } */
const tomTypeOf = v => {
    const d = tomData(v);
    if (d.k === 'unit') { const c = tomCat(d.unit); return c ? { [c]: 1 } : {}; }
    if (d.k === 'negate') return tomTypeOf(d.v);
    if (d.k === 'invert') { const t = tomTypeOf(d.v), r = {}; for (const k in t) if (k !== 'percentHint') r[k] = -t[k]; return r; }
    let t = null;
    for (const x of d.vals) { const u = tomTypeOf(x); t = t === null ? u : d.k === 'product' ? tomMulType(t, u) : tomAddType(t, u); if (!t) throw new TypeError('Incompatible types'); }
    return t;
};
const tomKeys = t => Object.keys(t).filter(k => k !== 'percentHint' && t[k]);
const tomAddType = (a, b) => {
    const ka = tomKeys(a), kb = tomKeys(b), same = ka.length === kb.length && ka.every(k => a[k] === b[k]);
    if (same) return Object.assign({}, a, a.percentHint || b.percentHint ? { percentHint: a.percentHint || b.percentHint } : {});
    const pa = ka.length === 1 && ka[0] === 'percent' && a.percent === 1, pb = kb.length === 1 && kb[0] === 'percent' && b.percent === 1;
    if (pa && kb.length === 1 && b[kb[0]] === 1 && kb[0] !== 'percent') return Object.assign({}, b, { percentHint: kb[0] });
    if (pb && ka.length === 1 && a[ka[0]] === 1 && ka[0] !== 'percent') return Object.assign({}, a, { percentHint: ka[0] });
    return null;
};
const tomMulType = (a, b) => { const r = {}; for (const k of new Set([...tomKeys(a), ...tomKeys(b)])) { const e = (a[k] || 0) + (b[k] || 0); if (e) r[k] = e; } return r; };
const tomRect = v => { if (v instanceof CSSNumericValue) return v; if (typeof v === 'number') return new CSSUnitValue(v, 'number'); if (typeof v === 'string' || typeof v === 'boolean') { const n = Number(v); if (Number.isFinite(n)) return new CSSUnitValue(n, 'number'); } throw new TypeError('Not a numeric value'); };   /* WebIDL (double or CSSNumericValue) */
const tomMath = (k, vals) => { const o = Object.create(({ sum: CSSMathSum, product: CSSMathProduct, min: CSSMathMin, max: CSSMathMax })[k].prototype); tomInit(o, { k, vals }); tomTypeOf(o); return o; };
const tomConv = (v, unit) => {   /* value of unit value v in unit, or null */
    const d = tomData(v); if (d.k !== 'unit') return null;
    if (d.unit === unit) return d.value;
    const a = TOM_UNITS[d.unit.toLowerCase()], b = TOM_UNITS[unit.toLowerCase()];
    return a[0] && a[0] === b[0] && a[1] && b[1] ? d.value * a[1] / b[1] : null;
};
const tomSer = (v, nested) => {
    const d = tomData(v);
    if (d.k === 'unit') return d.unit === 'number' ? tomNumStr(d.value) : tomNumStr(d.value) + (d.unit === 'percent' ? '%' : d.unit);
    const o = nested ? '(' : 'calc(';
    if (d.k === 'min' || d.k === 'max') return d.k + '(' + d.vals.map(x => tomSer(x, true).replace(/^\((.*)\)$/s, (m, i) => tomData(x).k === 'sum' || tomData(x).k === 'product' ? 'calc(' + i + ')' : m)).join(', ') + ')';
    if (d.k === 'clamp') return 'clamp(' + [d.lo, d.v, d.hi].map(x => tomSer(x, false)).join(', ') + ')';
    if (d.k === 'negate') return o + '-' + tomSer(d.v, true) + ')';
    if (d.k === 'invert') return o + '1 / ' + tomSer(d.v, true) + ')';
    let s = o + tomSer(d.vals[0], true);
    for (const x of d.vals.slice(1)) {
        const xd = tomData(x);
        if (d.k === 'sum') s += xd.k === 'negate' ? ' - ' + tomSer(xd.v, true) : ' + ' + tomSer(x, true);
        else s += xd.k === 'invert' ? ' / ' + tomSer(xd.v, true) : ' * ' + tomSer(x, true);
    }
    return s + ')';
};
const tomNumStr = n => { if (Number.isInteger(n)) return String(n); const r = +n.toPrecision(6); return String(r); };
const tomEq = (a, b) => {
    const x = tomData(a), y = tomData(b);
    if (x.k !== y.k) return false;
    if (x.k === 'unit') return x.unit === y.unit && x.value === y.value;
    if (x.k === 'negate' || x.k === 'invert') return tomEq(x.v, y.v);
    if (x.k === 'clamp') return tomEq(x.lo, y.lo) && tomEq(x.v, y.v) && tomEq(x.hi, y.hi);
    return x.vals.length === y.vals.length && x.vals.every((v, i) => tomEq(v, y.vals[i]));
};
/* sum of unit values grouped by unit, or null when v holds something else */
const tomSumMap = v => {
    const d = tomData(v);
    if (d.k === 'unit') return new Map([[d.unit, d.value]]);
    if (d.k === 'negate') { const m = tomSumMap(d.v); if (m) for (const [k, x] of m) m.set(k, -x); return m; }
    if (d.k === 'sum') { const r = new Map(); for (const x of d.vals) { const m = tomSumMap(x); if (!m) return null; for (const [k, y] of m) r.set(k, (r.get(k) || 0) + y); } return r; }
    if (d.k === 'product') {
        let r = new Map([['number', 1]]);
        for (const x of d.vals) {
            const xd = tomData(x); let m;
            if (xd.k === 'invert') { m = tomSumMap(xd.v); if (!m || m.size !== 1) return null; const [[k, y]] = m; if (k !== 'number') return null; m = new Map([['number', 1 / y]]); }
            else m = tomSumMap(x);
            if (!m) return null;
            const n = new Map();
            for (const [k1, a] of r) for (const [k2, b] of m) { if (k1 !== 'number' && k2 !== 'number') return null; const k = k1 === 'number' ? k2 : k1; n.set(k, (n.get(k) || 0) + a * b); }
            r = n;
        }
        return r;
    }
    return null;
};

class CSSStyleValue {
    constructor(k) { if (k !== TOM) tomIllegal(); }
    toString() { const d = tomData(this); return d.k ? tomSer(this, false) : d.text; }
    static parse(prop, text) { return tomParseProp(prop, text, false)[0]; }
    static parseAll(prop, text) { return tomParseProp(prop, text, true); }
}
class CSSNumericValue extends CSSStyleValue {
    constructor(k) { if (k !== TOM) tomIllegal(); super(TOM); }
    add(...vs) { return tomCombine('sum', [this, ...vs.map(tomRect)]); }
    sub(...vs) { return tomCombine('sum', [this, ...vs.map(v => tomRect(v).negate())]); }
    mul(...vs) { return tomCombine('product', [this, ...vs.map(tomRect)]); }
    div(...vs) { return tomCombine('product', [this, ...vs.map(v => { v = tomRect(v); const d = tomData(v); if (d.k === 'unit' && d.unit === 'number' && d.value === 0) throw new RangeError('Division by zero'); return d.k === 'unit' && d.unit === 'number' ? new CSSUnitValue(1 / d.value, 'number') : v.invert(); })]); }
    min(...vs) { return tomCombine('min', [this, ...vs.map(tomRect)]); }
    max(...vs) { return tomCombine('max', [this, ...vs.map(tomRect)]); }
    negate() { const d = tomData(this); if (d.k === 'unit') return new CSSUnitValue(-d.value, d.unit); if (d.k === 'negate') return d.v; return new CSSMathNegate(this); }
    invert() { const d = tomData(this); if (d.k === 'unit' && d.unit === 'number') { if (d.value === 0) throw new RangeError('Division by zero'); return new CSSUnitValue(1 / d.value, 'number'); } if (d.k === 'invert') return d.v; return new CSSMathInvert(this); }
    equals(...vs) { return vs.every(v => v instanceof CSSNumericValue && tomEq(this, v)); }
    to(unit) {
        unit = tomUnit(unit);
        const m = tomSumMap(this); if (!m) throw new TypeError('Cannot convert value');
        let total = 0;
        for (const [k, x] of m) { const c = tomConv(new CSSUnitValue(x, k), unit); if (c === null) throw new TypeError(`Cannot convert to ${unit}`); total += c; }
        return new CSSUnitValue(total, unit);
    }
    toSum(...units) {
        units = units.map(tomUnit);
        const m = tomSumMap(this); if (!m) throw new TypeError('Cannot convert value to a sum');
        if (!units.length) {
            const out = new Map();
            for (const [k, x] of m) { const c = TOM_UNITS[k.toLowerCase()]; const canon = c[0] === 'length' && c[1] ? 'px' : c[0] === 'angle' ? 'deg' : c[0] === 'time' ? 's' : c[0] === 'frequency' ? 'Hz' : c[0] === 'resolution' && c[1] ? 'dppx' : k; const y = tomConv(new CSSUnitValue(x, k), canon); out.set(canon, (out.get(canon) || 0) + (y ?? x)); }
            const vals = [...out].sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0).map(([k, x]) => new CSSUnitValue(x, k));
            return tomMath('sum', vals);
        }
        const acc = units.map(() => 0);
        for (const [k, x] of m) { let ok = false; for (let i = 0; i < units.length && !ok; i++) { const c = tomConv(new CSSUnitValue(x, k), units[i]); if (c !== null) { acc[i] += c; ok = true; } } if (!ok) throw new TypeError('Cannot convert to the given units'); }
        return tomMath('sum', units.map((u, i) => new CSSUnitValue(acc[i], u)));
    }
    type() { const t = tomTypeOf(this), r = {}; for (const k of TOM_BASE) if (t[k]) r[k] = t[k]; if (t.percentHint) r.percentHint = t.percentHint; return r; }
    static parse(text) {
        const toks = tomTokens(String(text)); let i = 0;
        const skip = () => { while (toks[i] && toks[i].t === 'ws') i++; };
        skip();
        const v = tomExprAtom(toks, () => i, n => { i = n; }, true);
        skip();
        if (!v || i < toks.length) throw new DOMException(`Failed to parse '${text}' as a numeric value`, 'SyntaxError');
        return v;
    }
}
const tomCombine = (k, vals) => {
    const ds = vals.map(tomData);
    if (ds.every(d => d.k === 'unit')) {
        if (k === 'sum' && ds.every(d => d.unit === ds[0].unit)) return new CSSUnitValue(ds.reduce((s, d) => s + d.value, 0), ds[0].unit);
        if ((k === 'min' || k === 'max') && ds.every(d => d.unit === ds[0].unit)) return new CSSUnitValue(Math[k](...ds.map(d => d.value)), ds[0].unit);
        if (k === 'product' && ds.filter(d => d.unit !== 'number').length <= 1) return new CSSUnitValue(ds.reduce((s, d) => s * d.value, 1), (ds.find(d => d.unit !== 'number') || ds[0]).unit);
    }
    return tomMath(k, vals);
};
class CSSUnitValue extends CSSNumericValue {
    constructor(value, unit) { super(TOM); if (arguments.length < 2) throw new TypeError('2 arguments required'); tomInit(this, { k: 'unit', value: tomNum(value), unit: tomUnit(unit) }); }
    get value() { return tomData(this).value; } set value(v) { tomData(this).value = tomNum(v); }
    get unit() { return tomData(this).unit; }
}
class CSSMathValue extends CSSNumericValue {
    constructor(k) { if (k !== TOM) tomIllegal(); super(TOM); }
    get operator() { return tomData(this).k; }
}
class CSSNumericArray {
    constructor(k) { if (k !== TOM) tomIllegal(); }
    get length() { return tomData(this).length; }
    [Symbol.iterator]() { return tomData(this)[Symbol.iterator](); }
    forEach(f, t) { tomData(this).forEach((v, i) => f.call(t, v, i, this)); }
}
const tomArray = vals => new Proxy(tomInit(new CSSNumericArray(TOM), vals), {
    get(t, p, r) { if (typeof p === 'string' && /^\d+$/.test(p)) return vals[+p]; return Reflect.get(t, p, r); },
    has(t, p) { return (typeof p === 'string' && /^\d+$/.test(p) && +p < vals.length) || p in t; },
    set(t, p, v) { if (typeof p === 'string' && /^\d+$/.test(p)) return false; return Reflect.set(t, p, v); },
    ownKeys(t) { return [...vals.keys()].map(String).concat(Reflect.ownKeys(t)); },
    getOwnPropertyDescriptor(t, p) { if (typeof p === 'string' && /^\d+$/.test(p) && +p < vals.length) return { value: vals[+p], enumerable: true, configurable: true, writable: false }; return Reflect.getOwnPropertyDescriptor(t, p); },
});
const tomVariadic = (name, k) => class extends CSSMathValue {
    constructor(...vals) { super(TOM); if (!vals.length) throw new SyntaxError(`${name} requires at least one argument`); tomInit(this, { k, vals: vals.map(tomRect) }); tomTypeOf(this); }
    get values() { return tomArray(tomData(this).vals); }
};
const CSSMathSum = tomVariadic('CSSMathSum', 'sum'), CSSMathProduct = tomVariadic('CSSMathProduct', 'product'), CSSMathMin = tomVariadic('CSSMathMin', 'min'), CSSMathMax = tomVariadic('CSSMathMax', 'max');
for (const [C, n] of [[CSSMathSum, 'CSSMathSum'], [CSSMathProduct, 'CSSMathProduct'], [CSSMathMin, 'CSSMathMin'], [CSSMathMax, 'CSSMathMax']]) Object.defineProperty(C, 'name', { value: n });
class CSSMathNegate extends CSSMathValue {
    constructor(v) { super(TOM); tomInit(this, { k: 'negate', v: tomRect(v) }); }
    get value() { return tomData(this).v; }
}
class CSSMathInvert extends CSSMathValue {
    constructor(v) { super(TOM); tomInit(this, { k: 'invert', v: tomRect(v) }); }
    get value() { return tomData(this).v; }
}
class CSSMathClamp extends CSSMathValue {
    constructor(lo, v, hi) {
        super(TOM); if (arguments.length < 3) throw new TypeError('3 arguments required');
        const d = { k: 'clamp', lo: tomRect(lo), v: tomRect(v), hi: tomRect(hi) };
        if (!tomAddType(tomAddType(tomTypeOf(d.lo), tomTypeOf(d.v)) || {}, tomTypeOf(d.hi))) throw new TypeError('Incompatible types');
        tomInit(this, d);
    }
    get lower() { return tomData(this).lo; } get value() { return tomData(this).v; } get upper() { return tomData(this).hi; }
}
Object.defineProperty(CSSMathValue.prototype, 'operator', Object.getOwnPropertyDescriptor(CSSMathValue.prototype, 'operator'));
for (const [C, op] of [[CSSMathNegate, 'negate'], [CSSMathInvert, 'invert'], [CSSMathClamp, 'clamp']]) void op;

/* tokens of a numeric expression: num (value, unit), op, open (fn), close, comma, ws */
function tomTokens(s) {
    const out = []; let i = 0;
    while (i < s.length) {
        const c = s[i];
        if (/\s/.test(c)) { while (i < s.length && /\s/.test(s[i])) i++; out.push({ t: 'ws' }); continue; }
        const m = /^[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?/i.exec(s.slice(i));
        if (m && !(c === '+' || c === '-') || (m && (c === '+' || c === '-') && (!out.length || out[out.length - 1].t !== 'num' && out[out.length - 1].t !== 'close'))) {
            i += m[0].length; let u = '';
            if (s[i] === '%') { u = 'percent'; i++; } else { const um = /^[a-z]+/i.exec(s.slice(i)); if (um) { u = um[0]; i += u.length; } }
            out.push({ t: 'num', v: +m[0], u: u || 'number' }); continue;
        }
        const f = /^(calc|min|max|clamp)\(/i.exec(s.slice(i));
        if (f) { out.push({ t: 'open', fn: f[1].toLowerCase() }); i += f[0].length; continue; }
        if (c === '(') { out.push({ t: 'open', fn: '' }); i++; continue; }
        if (c === ')') { out.push({ t: 'close' }); i++; continue; }
        if (c === ',') { out.push({ t: 'comma' }); i++; continue; }
        if ('+-*/'.includes(c)) { out.push({ t: 'op', v: c }); i++; continue; }
        out.push({ t: 'bad' }); i++;
    }
    return out;
}
/* parses one atom (number or function) at the token cursor; top: the whole value must be a single atom */
function tomExprAtom(toks, get, set) {
    let i = get();
    const tk = toks[i];
    if (!tk) return null;
    if (tk.t === 'num') {
        let u; try { u = tomUnit(tk.u); } catch (e) { return null; }
        set(i + 1); return new CSSUnitValue(tk.v, u);
    }
    if (tk.t !== 'open') return null;
    i++;
    const ws = () => { while (toks[i] && toks[i].t === 'ws') i++; };
    const sum = () => {
        let terms = [prod()]; if (!terms[0]) return null;
        for (;;) {
            const save = i; ws(); const t = toks[i];
            if (t && t.t === 'op' && (t.v === '+' || t.v === '-') && toks[i - 1] && toks[i - 1].t === 'ws' && toks[i + 1] && toks[i + 1].t === 'ws') { i++; ws(); const p = prod(); if (!p) return null; terms.push(t.v === '-' ? p.negate() : p); }
            else { i = save; break; }
        }
        return terms.length === 1 ? terms[0] : tomCombine('sum', terms);
    };
    const prod = () => {
        ws(); let f = [unary()]; if (!f[0]) return null;
        for (;;) {
            const save = i; ws(); const t = toks[i];
            if (t && t.t === 'op' && (t.v === '*' || t.v === '/')) { i++; ws(); const u = unary(); if (!u) return null; f.push(t.v === '/' ? u.invert() : u); }
            else { i = save; break; }
        }
        return f.length === 1 ? f[0] : tomCombine('product', f);
    };
    const unary = () => { const r = tomExprAtom(toks, () => i, n => { i = n; }); return r; };
    const fn = tk.fn;
    const args = [];
    for (;;) { ws(); const a = sum(); if (!a) return null; args.push(a); ws(); if (toks[i] && toks[i].t === 'comma') { i++; continue; } break; }
    ws(); if (!toks[i] || toks[i].t !== 'close') return null;
    set(i + 1);
    if (fn === 'min' || fn === 'max') return args.length === 1 ? tomMath(fn, args) : tomMath(fn, args);
    if (fn === 'clamp') return args.length === 3 ? new CSSMathClamp(...args) : null;
    if (args.length !== 1) return null;
    const a = args[0];
    return fn === 'calc' && tomData(a).k === 'unit' ? tomMath('sum', [a]) : a;
}

class CSSKeywordValue extends CSSStyleValue {
    constructor(v) { super(TOM); v = String(v); if (!v) throw new TypeError('CSSKeywordValue does not support empty strings'); tomInit(this, { text: v }); }
    get value() { return tomData(this).text; } set value(v) { v = String(v); if (!v) throw new TypeError('CSSKeywordValue does not support empty strings'); tomData(this).text = v; }
}
class CSSVariableReferenceValue {
    constructor(variable, fallback = null) { variable = String(variable); if (!variable.startsWith('--')) throw new TypeError(`Invalid custom property name: ${variable}`); if (fallback !== null && !(fallback instanceof CSSUnparsedValue)) throw new TypeError('Fallback must be a CSSUnparsedValue'); tomInit(this, { variable, fallback }); }
    get variable() { return tomData(this).variable; } set variable(v) { v = String(v); if (!v.startsWith('--')) throw new TypeError(`Invalid custom property name: ${v}`); tomData(this).variable = v; }
    get fallback() { return tomData(this).fallback; }
}
class CSSUnparsedValue extends CSSStyleValue {
    constructor(members) {
        super(TOM);
        if (members == null || typeof members[Symbol.iterator] !== 'function') throw new TypeError('CSSUnparsedValue requires a sequence');
        const m = [...members].map(x => x instanceof CSSVariableReferenceValue ? x : String(x));
        tomInit(this, { text: null, m });
        return new Proxy(this, {
            get(t, p) { if (typeof p === 'string' && /^\d+$/.test(p)) return tomData(t).m[+p]; return Reflect.get(t, p, t); },
            set(t, p, v) {
                if (typeof p === 'string' && /^\d+$/.test(p)) { const a = tomData(t).m; if (+p > a.length) throw new RangeError('Index out of range'); a[+p] = v instanceof CSSVariableReferenceValue ? v : String(v); return true; }
                return Reflect.set(t, p, v, t);
            },
            has(t, p) { return (typeof p === 'string' && /^\d+$/.test(p) && +p < tomData(t).m.length) || p in t; },
        });
    }
    get length() { return tomData(this).m.length; }
    [Symbol.iterator]() { return tomData(this).m[Symbol.iterator](); }
    entries() { return tomData(this).m.entries(); } keys() { return tomData(this).m.keys(); } values() { return tomData(this).m.values(); }
    forEach(f, t) { tomData(this).m.forEach((v, i) => f.call(t, v, i, this)); }
    toString() { return tomData(this).m.map(x => x instanceof CSSVariableReferenceValue ? `var(${x.variable}${x.fallback ? ',' + x.fallback.toString() : ''})` : x).join(''); }
}
class CSSImageValue extends CSSStyleValue { constructor(k) { if (k !== TOM) tomIllegal(); super(TOM); } }

/* transforms */
class CSSTransformComponent {
    constructor(k) { if (k !== TOM) tomIllegal(); }
    get is2D() { return tomData(this).is2D; } set is2D(v) { tomData(this).is2D = !!v; }
    toMatrix() { return new DOMMatrix(tomTfStr(this, true)); }
    toString() { return tomTfStr(this, false); }
}
const tomLen = (v, what) => { v = tomRect(v); const t = tomTypeOf(v), k = tomKeys(t); if (!(k.length === 1 && (k[0] === 'length' || (what === 'lp' && k[0] === 'percent')) && t[k[0]] === 1) && !(what === 'lp' && t.percentHint === 'length')) throw new TypeError('Must be a length'); return v; };
const tomAng = v => { v = tomRect(v); const k = tomKeys(tomTypeOf(v)); if (!(k.length === 1 && k[0] === 'angle')) throw new TypeError('Must be an angle'); return v; };
const tomNumV = v => { v = tomRect(v); if (tomKeys(tomTypeOf(v)).length) throw new TypeError('Must be a number'); return v; };
const tomS = v => v.toString();
function tomTfStr(c, abs) {
    const d = tomData(c), s = abs ? (v => { try { return tomData(v).k === 'unit' ? tomS(v) : tomS(v); } catch (e) { return tomS(v); } }) : tomS;
    switch (d.t) {
        case 'translate': return d.is2D ? `translate(${s(d.x)}, ${s(d.y)})` : `translate3d(${s(d.x)}, ${s(d.y)}, ${s(d.z)})`;
        case 'rotate': return d.is2D ? `rotate(${s(d.a)})` : `rotate3d(${s(d.x)}, ${s(d.y)}, ${s(d.z)}, ${s(d.a)})`;
        case 'scale': return d.is2D ? `scale(${s(d.x)}, ${s(d.y)})` : `scale3d(${s(d.x)}, ${s(d.y)}, ${s(d.z)})`;
        case 'skew': return `skew(${s(d.ax)}, ${s(d.ay)})`;
        case 'skewX': return `skewX(${s(d.ax)})`;
        case 'skewY': return `skewY(${s(d.ay)})`;
        case 'perspective': return `perspective(${typeof d.l === 'string' ? d.l : s(d.l)})`;
        case 'matrix': return d.is2D ? (() => { const m = d.m; return `matrix(${[m.a, m.b, m.c, m.d, m.e, m.f].join(', ')})`; })() : `matrix3d(${['m11', 'm12', 'm13', 'm14', 'm21', 'm22', 'm23', 'm24', 'm31', 'm32', 'm33', 'm34', 'm41', 'm42', 'm43', 'm44'].map(k => d.m[k]).join(', ')})`;
    }
    return '';
}
const tomComp = (C, d) => { const o = Object.create(C.prototype); tomInit(o, d); return o; };
class CSSTranslate extends CSSTransformComponent {
    constructor(x, y, z) { super(TOM); if (arguments.length < 2) throw new TypeError('2 arguments required'); tomInit(this, { t: 'translate', x: tomLen(x, 'lp'), y: tomLen(y, 'lp'), z: z === undefined ? new CSSUnitValue(0, 'px') : tomLen(z), is2D: z === undefined }); }
    get x() { return tomData(this).x; } set x(v) { tomData(this).x = tomLen(v, 'lp'); }
    get y() { return tomData(this).y; } set y(v) { tomData(this).y = tomLen(v, 'lp'); }
    get z() { return tomData(this).z; } set z(v) { tomData(this).z = tomLen(v); }
}
class CSSRotate extends CSSTransformComponent {
    constructor(...a) {
        super(TOM);
        if (a.length === 1) tomInit(this, { t: 'rotate', x: new CSSUnitValue(0, 'number'), y: new CSSUnitValue(0, 'number'), z: new CSSUnitValue(1, 'number'), a: tomAng(a[0]), is2D: true });
        else if (a.length >= 4) tomInit(this, { t: 'rotate', x: tomNumV(a[0]), y: tomNumV(a[1]), z: tomNumV(a[2]), a: tomAng(a[3]), is2D: false });
        else throw new TypeError('Invalid arguments');
    }
    get x() { return tomData(this).x; } set x(v) { tomData(this).x = tomNumV(v); }
    get y() { return tomData(this).y; } set y(v) { tomData(this).y = tomNumV(v); }
    get z() { return tomData(this).z; } set z(v) { tomData(this).z = tomNumV(v); }
    get angle() { return tomData(this).a; } set angle(v) { tomData(this).a = tomAng(v); }
}
class CSSScale extends CSSTransformComponent {
    constructor(x, y, z) { super(TOM); if (arguments.length < 2) throw new TypeError('2 arguments required'); tomInit(this, { t: 'scale', x: tomNumV(x), y: tomNumV(y), z: z === undefined ? new CSSUnitValue(1, 'number') : tomNumV(z), is2D: z === undefined }); }
    get x() { return tomData(this).x; } set x(v) { tomData(this).x = tomNumV(v); }
    get y() { return tomData(this).y; } set y(v) { tomData(this).y = tomNumV(v); }
    get z() { return tomData(this).z; } set z(v) { tomData(this).z = tomNumV(v); }
}
class CSSSkew extends CSSTransformComponent {
    constructor(ax, ay) { super(TOM); if (arguments.length < 2) throw new TypeError('2 arguments required'); tomInit(this, { t: 'skew', ax: tomAng(ax), ay: tomAng(ay), is2D: true }); }
    get ax() { return tomData(this).ax; } set ax(v) { tomData(this).ax = tomAng(v); }
    get ay() { return tomData(this).ay; } set ay(v) { tomData(this).ay = tomAng(v); }
    get is2D() { return true; } set is2D(v) {}
}
class CSSSkewX extends CSSTransformComponent {
    constructor(ax) { super(TOM); if (!arguments.length) throw new TypeError('1 argument required'); tomInit(this, { t: 'skewX', ax: tomAng(ax), is2D: true }); }
    get ax() { return tomData(this).ax; } set ax(v) { tomData(this).ax = tomAng(v); }
    get is2D() { return true; } set is2D(v) {}
}
class CSSSkewY extends CSSTransformComponent {
    constructor(ay) { super(TOM); if (!arguments.length) throw new TypeError('1 argument required'); tomInit(this, { t: 'skewY', ay: tomAng(ay), is2D: true }); }
    get ay() { return tomData(this).ay; } set ay(v) { tomData(this).ay = tomAng(v); }
    get is2D() { return true; } set is2D(v) {}
}
class CSSPerspective extends CSSTransformComponent {
    constructor(l) { super(TOM); if (!arguments.length) throw new TypeError('1 argument required'); tomInit(this, { t: 'perspective', l: tomPersp(l), is2D: false }); }
    get length() { const l = tomData(this).l; return typeof l === 'string' ? new CSSKeywordValue(l) : l; } set length(v) { tomData(this).l = tomPersp(v); }
    get is2D() { return false; } set is2D(v) {}
}
const tomPersp = l => { if (l instanceof CSSKeywordValue) { if (l.value !== 'none') throw new TypeError('Invalid perspective'); return 'none'; } if (typeof l === 'string' && l === 'none') return 'none'; return tomLen(l); };
class CSSMatrixComponent extends CSSTransformComponent {
    constructor(m, opts) { super(TOM); if (!(m instanceof DOMMatrixReadOnly)) throw new TypeError('Must be a DOMMatrixReadOnly'); tomInit(this, { t: 'matrix', m: DOMMatrix.fromMatrix(m), is2D: opts && 'is2D' in opts ? !!opts.is2D : m.is2D }); }
    get matrix() { return tomData(this).m; } set matrix(v) { if (!(v instanceof DOMMatrixReadOnly)) throw new TypeError('Must be a DOMMatrixReadOnly'); tomData(this).m = DOMMatrix.fromMatrix(v); }
}
class CSSTransformValue extends CSSStyleValue {
    constructor(list) {
        super(TOM);
        if (list == null || typeof list[Symbol.iterator] !== 'function') throw new TypeError('CSSTransformValue requires a sequence');
        const l = [...list]; if (!l.length || l.some(c => !(c instanceof CSSTransformComponent))) throw new TypeError('Invalid transform components');
        tomInit(this, { tf: l });
        return new Proxy(this, {
            get(t, p) { if (typeof p === 'string' && /^\d+$/.test(p)) return tomData(t).tf[+p]; return Reflect.get(t, p, t); },
            set(t, p, v) { if (typeof p === 'string' && /^\d+$/.test(p)) { if (!(v instanceof CSSTransformComponent)) throw new TypeError('Invalid transform component'); const a = tomData(t).tf; if (+p > a.length) throw new RangeError('Index out of range'); a[+p] = v; return true; } return Reflect.set(t, p, v, t); },
            has(t, p) { return (typeof p === 'string' && /^\d+$/.test(p) && +p < tomData(t).tf.length) || p in t; },
        });
    }
    get length() { return tomData(this).tf.length; }
    get is2D() { return tomData(this).tf.every(c => c.is2D); }
    [Symbol.iterator]() { return tomData(this).tf[Symbol.iterator](); }
    entries() { return tomData(this).tf.entries(); } keys() { return tomData(this).tf.keys(); } values() { return tomData(this).tf.values(); }
    forEach(f, t) { tomData(this).tf.forEach((v, i) => f.call(t, v, i, this)); }
    toMatrix() { let m = new DOMMatrix(); for (const c of tomData(this).tf) m = m.multiply(c.toMatrix()); return m; }
    toString() { return tomData(this).tf.map(c => c.toString()).join(' '); }
}
function tomParseTransform(text) {
    const fns = [], re = /\s*([a-z0-9]+)\(([^()]*(?:\([^()]*\)[^()]*)*)\)\s*/giy; let m, end = 0;
    while ((m = re.exec(text))) { fns.push([m[1].toLowerCase(), m[2]]); end = re.lastIndex; }
    if (!fns.length || end !== text.length) return null;
    try {
        return new CSSTransformValue(fns.map(([f, a]) => {
            const args = a.split(',').map(s => s.trim()).filter(Boolean).map(s => (f === 'perspective' && s === 'none') ? 'none' : CSSNumericValue.parse(s));
            switch (f) {
                case 'translate': return new CSSTranslate(args[0], args[1] || new CSSUnitValue(0, 'px'));
                case 'translatex': return new CSSTranslate(args[0], new CSSUnitValue(0, 'px'));
                case 'translatey': return new CSSTranslate(new CSSUnitValue(0, 'px'), args[0]);
                case 'translatez': return new CSSTranslate(new CSSUnitValue(0, 'px'), new CSSUnitValue(0, 'px'), args[0]);
                case 'translate3d': return new CSSTranslate(...args);
                case 'rotate': case 'rotatez': return f === 'rotate' ? new CSSRotate(args[0]) : new CSSRotate(0, 0, 1, args[0]);
                case 'rotatex': return new CSSRotate(1, 0, 0, args[0]);
                case 'rotatey': return new CSSRotate(0, 1, 0, args[0]);
                case 'rotate3d': return new CSSRotate(...args);
                case 'scale': return new CSSScale(args[0], args[1] || args[0]);
                case 'scalex': return new CSSScale(args[0], 1);
                case 'scaley': return new CSSScale(1, args[0]);
                case 'scalez': return new CSSScale(1, 1, args[0]);
                case 'scale3d': return new CSSScale(...args);
                case 'skew': return new CSSSkew(args[0], args[1] || new CSSUnitValue(0, 'deg'));
                case 'skewx': return new CSSSkewX(args[0]);
                case 'skewy': return new CSSSkewY(args[0]);
                case 'perspective': return new CSSPerspective(args[0] === 'none' ? new CSSKeywordValue('none') : args[0]);
                case 'matrix': return new CSSMatrixComponent(new DOMMatrixReadOnly(args.map(x => x.value)));
                case 'matrix3d': return new CSSMatrixComponent(new DOMMatrixReadOnly(args.map(x => x.value)));
            }
            throw new TypeError('Unknown transform function');
        }));
    } catch (e) { return null; }
}

/* text -> typed values for a property */
const TOM_LIST_PROPS = /^(animation-(?!composition$|range|timeline$).*|transition-.*|background-(?!color$).*|mask-(?!type$).*|will-change|scroll-timeline-.*|view-timeline-.*)$/;
const TOM_WIDE = /^(initial|inherit|unset|revert|revert-layer)$/i;
const tomSplitTop = (s, sep) => { const out = []; let d = 0, cur = '', q = ''; for (const c of s) { if (q) { cur += c; if (c === q) q = ''; continue; } if (c === '"' || c === "'") q = c; if (c === '(') d++; else if (c === ')') d--; if (c === sep && !d) { out.push(cur); cur = ''; } else cur += c; } out.push(cur); return out; };
const tomGeneric = text => { const o = Object.create(CSSStyleValue.prototype); tomInit(o, { text }); return o; };
const tomUnparsed = text => {
    const parts = []; let rest = text, m;
    while ((m = /var\(\s*(--[^,)\s]+)\s*(?:,([^()]*(?:\([^()]*\)[^()]*)*))?\)/.exec(rest))) {
        if (m.index) parts.push(rest.slice(0, m.index));
        parts.push(new CSSVariableReferenceValue(m[1], m[2] !== undefined ? tomUnparsed(m[2]) : null));
        rest = rest.slice(m.index + m[0].length);
    }
    if (rest || !parts.length) parts.push(rest);
    return new CSSUnparsedValue(parts);
};
function tomReify(prop, text) {
    text = String(text).trim();
    if (prop.startsWith('--')) return tomUnparsed(text);
    if (TOM_WIDE.test(text)) return new CSSKeywordValue(text.toLowerCase());
    if (/var\(/i.test(text)) return tomUnparsed(text);
    if (/^-?[a-z_][a-z0-9_-]*$/i.test(text) && !/^-?\d/.test(text)) return new CSSKeywordValue(text);
    if (/^[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?(%|[a-z]+)?$/i.test(text) || /^(calc|min|max|clamp)\(/i.test(text)) { try { return CSSNumericValue.parse(text); } catch (e) {} }
    if ((prop === 'transform') && text !== 'none') { const t = tomParseTransform(text); if (t) return t; }
    if (/^url\(/i.test(text) && /image|content|cursor|src/.test(prop)) { const o = Object.create(CSSImageValue.prototype); tomInit(o, { text }); return o; }
    return tomGeneric(text);
}
const tomValues = (prop, text) => TOM_LIST_PROPS.test(prop) && !TOM_WIDE.test(String(text).trim()) ? tomSplitTop(String(text), ',').map(s => tomReify(prop, s)) : [tomReify(prop, text)];
const tomPropName = p => { p = String(p); return p.startsWith('--') ? p : p.toLowerCase(); };
const tomKnown = p => p.startsWith('--') || CSS_PROP_NAMES.has(p) || CSS.supports(p, 'initial');
/* coarse value-type validation: dimensions must suit the property, keyword-only properties take no numbers */
const TOM_LENGTH_PROPS = /^(width|height|(min|max)-(width|height)|(min-|max-)?(block|inline)-size|margin(-.*)?|padding(-.*)?|top|right|bottom|left|inset(-.*)?|gap|row-gap|column-gap|column-width|flex-basis|border(-[a-z]+)*-width|outline-width|outline-offset|letter-spacing|word-spacing|text-indent|font-size|line-height|vertical-align|perspective|shape-margin|scroll-(margin|padding)(-.*)?|text-underline-offset|text-decoration-thickness|border(-[a-z]+)*-radius)$/;
const TOM_DIM = /([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)([a-z%]+)?/gi;
const TOM_LEN_UNITS = new Set(['', '%', 'px', 'em', 'rem', 'ex', 'rex', 'ch', 'rch', 'ic', 'ric', 'cap', 'rcap', 'lh', 'rlh', 'vw', 'vh', 'vi', 'vb', 'vmin', 'vmax', 'svw', 'svh', 'svi', 'svb', 'svmin', 'svmax', 'lvw', 'lvh', 'lvi', 'lvb', 'lvmin', 'lvmax', 'dvw', 'dvh', 'dvi', 'dvb', 'dvmin', 'dvmax', 'cqw', 'cqh', 'cqi', 'cqb', 'cqmin', 'cqmax', 'cm', 'mm', 'q', 'in', 'pt', 'pc']);
function tomValidText(prop, text) {
    const t = text.trim();
    if (TOM_WIDE.test(t) || /var\(|env\(|attr\(/i.test(t)) return true;
    if (TOM_NO_NUMERIC.test(prop) && !/-color$|^color$/.test(prop) && /^[+-]?(\d|\.\d)/.test(t)) return false;
    if (TOM_LENGTH_PROPS.test(prop)) {
        const bare = t.replace(/["'][^"']*["']/g, '');
        for (const m of bare.matchAll(TOM_DIM)) {
            const pre = bare[m.index - 1];
            if (pre && /[a-z_-]/i.test(pre)) continue;
            if (!TOM_LEN_UNITS.has((m[2] || '').toLowerCase())) return false;
        }
        if (/^[a-z-]+$/i.test(t) && !/^(auto|none|normal|thin|medium|thick|min-content|max-content|fit-content|stretch|-webkit-fill-available|content|baseline|sub|super|text-top|text-bottom|middle|top|bottom|xx-small|x-small|small|large|x-large|xx-large|xxx-large|smaller|larger|math|anchor-size|infinite)$/i.test(t)) return false;
    }
    return true;
}
function tomParseProp(prop, text, all) {
    prop = tomPropName(prop);
    if (!tomKnown(prop)) throw new TypeError(`Invalid property name: ${prop}`);
    text = String(text);
    if (prop.startsWith('--') && !text.trim()) throw new TypeError(`Failed to parse '${text}' for '${prop}'`);
    if (!prop.startsWith('--') && (!text.trim() || !CSS.supports(prop, text) || !tomValidText(prop, text))) throw new TypeError(`Failed to parse '${text}' for '${prop}'`);
    const vs = tomValues(prop, text);
    return all ? vs : vs.slice(0, 1);
}

/* style maps over a declaration block (getPropertyValue/setProperty/removeProperty/length/item) */
class StylePropertyMapReadOnly {
    constructor(k) { if (k !== TOM) tomIllegal(); }
    get(p) { if (!arguments.length) throw new TypeError('1 argument required'); p = tomPropName(p); if (!tomKnown(p)) throw new TypeError(`Invalid property name: ${p}`); const v = tomRead(this, p); return v ? tomValues(p, v)[0] : (tomData(this).computed && !p.startsWith('--') ? tomValues(p, '')[0] : undefined); }
    getAll(p) { p = tomPropName(p); if (!tomKnown(p)) throw new TypeError(`Invalid property name: ${p}`); const v = tomRead(this, p); return v ? tomValues(p, v) : []; }
    has(p) { p = tomPropName(p); if (!tomKnown(p)) throw new TypeError(`Invalid property name: ${p}`); return !!tomRead(this, p); }
    get size() { return tomNames(this).length; }
    entries() { return tomNames(this).map(n => [n, tomValues(n, tomRead(this, n))])[Symbol.iterator](); }
    keys() { return tomNames(this)[Symbol.iterator](); }
    values() { return tomNames(this).map(n => tomValues(n, tomRead(this, n)))[Symbol.iterator](); }
    forEach(f, t) { for (const [k, v] of this.entries()) f.call(t, v, k, this); }
    [Symbol.iterator]() { return this.entries(); }
}
class StylePropertyMap extends StylePropertyMapReadOnly {
    constructor(k) { if (k !== TOM) tomIllegal(); super(TOM); }
    set(p, ...vals) { p = tomPropName(p); tomData(this).decl().setProperty(p, tomText(p, vals)); }
    append(p, ...vals) {
        p = tomPropName(p);
        if (!TOM_LIST_PROPS.test(p)) throw new TypeError(`Property '${p}' is not list-valued`);
        const cur = tomRead(this, p), t = tomText(p, vals);
        tomData(this).decl().setProperty(p, cur ? cur + ', ' + t : t);
    }
    delete(p) { p = tomPropName(p); if (!tomKnown(p)) throw new TypeError(`Invalid property name: ${p}`); tomData(this).decl().removeProperty(p); }
    clear() { const d = tomData(this).decl(); for (const n of tomNames(this)) d.removeProperty(n); }
}
const tomRead = (m, p) => { const v = tomData(m).decl().getPropertyValue(p); return v == null ? '' : String(v); };
const tomNames = m => {
    const d = tomData(m), decl = d.decl();
    if (d.computed) { const names = [...CSS_PROP_NAMES].filter(n => tomRead(m, n)).sort(), cust = []; return names.concat(cust); }
    const out = []; for (let i = 0; i < decl.length; i++) out.push(decl.item(i)); return out;
};
/* properties whose grammar has no <number>/<length>/<percentage>/<angle>/<time> */
const TOM_NO_NUMERIC = /^(color|background-color|.*-color|fill-rule|clip-rule|d|paint-order|shape-rendering|color-interpolation(-filters)?|color-rendering|text-rendering|image-rendering|stroke-linecap|stroke-linejoin|text-anchor|marker(-start|-mid|-end)?|vector-effect|mix-blend-mode|isolation|display|position|float|clear|visibility|direction|unicode-bidi|writing-mode|text-orientation|table-layout|caption-side|empty-cells|border-collapse|box-sizing|resize|pointer-events|user-select|backface-visibility|transform-style|transform-box|mask-type|dominant-baseline|alignment-baseline|white-space|word-break|overflow-wrap|word-wrap|hyphens|text-transform|font-kerning|font-variant-caps|break-(before|after|inside)|appearance|forced-color-adjust|print-color-adjust|content-visibility|container-type|scroll-behavior|overscroll-behavior(-[xy]|-block|-inline)?|overflow(-[xy]|-block|-inline)?|list-style-position|object-fit|ruby-position|text-decoration-style|text-decoration-line|border(-[a-z]+)*-style|outline-style|column-fill|column-span)$/;
/* typed values whose type the property's grammar cannot hold */
const TOM_ANGLE_PROPS = /^(rotate|offset-rotate|image-orientation|font-style|filter|backdrop-filter|offset-path|hue-rotate)$/;
const TOM_TIME_PROPS = /^(transition|animation)(-delay|-duration)?$/;
const TOM_FLEX_PROPS = /^grid(-template)?(-(columns|rows))?$|^grid-auto-(columns|rows)$/;
/* properties (beyond the length/angle/time/flex ones) whose grammar has a numeric component */
const TOM_NUMERIC_OK = /^(opacity|text-size-adjust|-webkit-text-size-adjust|z-index|order|flex(-grow|-shrink)?|orphans|widows|tab-size|font-weight|font-size-adjust|font-stretch|font-width|font-style|columns|column-count|(fill|stroke|stop|flood)-opacity|stroke(-width|-miterlimit|-dashoffset|-dasharray)?|r|rx|ry|cx|cy|x|y|background(-position(-[xy])?|-size)?|object-position|transform-origin|perspective-origin|mask(-position|-size)?|-webkit-mask(-position|-size)?|border-image(-outset|-slice|-width)?|shape-(outside|image-threshold)|offset(-distance|-position|-anchor|-rotate)?|aspect-ratio|zoom|scale|rotate|translate|grid-(template-(rows|columns)|auto-(rows|columns)|(row|column)-(start|end)|row|column|area)|initial-letter|hyphenate-limit-chars|math-depth|text-shadow|box-shadow|animation-iteration-count|animation(-delay|-duration)?|transition(-delay|-duration)?|counter-(increment|reset|set)|line-clamp|-webkit-line-clamp|max-lines|contain-intrinsic-(size|width|height|block-size|inline-size)|scroll-snap-(stop|type)|font|text-decoration|outline|border(-top|-right|-bottom|-left|-block|-inline)?|column-rule|rule|filter|backdrop-filter|transform|image-resolution|block-step-size|scroll-timeline-inset|view-timeline-inset|animation-range(-start|-end)?|-webkit-text-stroke(-width)?|text-emphasis-position|baseline-shift|shape-margin|clip|clip-path|corner-shape|interest-delay(-start|-end)?|reading-flow|position-try)$/;
function tomTypedOk(p, v) {
    if (v instanceof CSSTransformValue) return p === 'transform';
    if (v instanceof CSSNumericValue && /^(touch-action|background|border(-block|-inline|-top|-right|-bottom|-left)?|outline|font|list-style|text-decoration|text-emphasis|column-rule|columns|flex-flow|grid(-area|-row|-column|-template)?|transition|animation|mask|offset|overflow|place-(content|items|self))$/.test(p)) return false;
    if (!(v instanceof CSSNumericValue)) return true;
    if (/^(font-synthesis(-.*)?|white-space-collapse|text-wrap(-mode|-style)?|container-name|(backdrop-)?filter|font-style|offset-path|text-spacing-trim|text-box-trim|font-variant(-.*)?|anchor-name|position-anchor|view-transition-name|timeline-scope)$/.test(p)) return false;
    const k = tomKeys(tomTypeOf(v));
    if (!TOM_LENGTH_PROPS.test(p) && !TOM_ANGLE_PROPS.test(p) && !TOM_TIME_PROPS.test(p) && !TOM_FLEX_PROPS.test(p) && !TOM_NUMERIC_OK.test(p)) return false;
    if (/^grid-(row|column)-(start|end)$/.test(p)) return !k.length;
    if (p === 'border-image-width') return k.every(x => x === 'length' || x === 'percent');
    if (p !== 'line-height' && TOM_LENGTH_PROPS.test(p) && !k.length) return false;
    if (k.includes('percent') && /^(border(-[a-z]+)*-width|outline-width|outline-offset|scroll-margin(-.*)?|column-width|perspective)$/.test(p)) return false;
    const t = v.type();
    if (t.angle) return TOM_ANGLE_PROPS.test(p);
    if (t.time) return TOM_TIME_PROPS.test(p);
    if (t.flex) return TOM_FLEX_PROPS.test(p);
    if (t.frequency || t.resolution) return /^image-resolution$/.test(p);
    if ((t.length || t.percent) && TOM_TIME_PROPS.test(p)) return false;
    return true;
}
const tomText = (p, vals) => {
    if (!tomKnown(p)) throw new TypeError(`Invalid property name: ${p}`);
    if (!vals.length) throw new TypeError('At least one value required');
    if (vals.length > 1 && !TOM_LIST_PROPS.test(p)) throw new TypeError(`Property '${p}' does not take multiple values`);
    const parts = vals.map(v => {
        if (v instanceof CSSNumericValue && TOM_NO_NUMERIC.test(p) || !p.startsWith('--') && !tomTypedOk(p, v)) throw new TypeError(`Property '${p}' does not accept ${v}`);
        if (v instanceof CSSStyleValue) return v.toString();
        if (v instanceof CSSStyleValue) return v.toString();
        const s = String(v);
        if (!p.startsWith('--') && !CSS.supports(p, s)) throw new TypeError(`Invalid value '${s}' for '${p}'`);
        return s;
    });
    if (p.startsWith('--') && vals.some(v => v instanceof CSSStyleValue && !(v instanceof CSSUnparsedValue))) throw new TypeError(`Invalid value for custom property '${p}'`);
    return parts.join(', ');
};
const tomMap = (C, decl, computed) => { const o = Object.create(C.prototype); tomInit(o, { decl, computed }); return o; };
const tomStyleMaps = new WeakMap();
methods(Element.prototype, {
    get attributeStyleMap() { let m = tomStyleMaps.get(this); if (!m) tomStyleMaps.set(this, m = tomMap(StylePropertyMap, () => this.style, false)); return m; },
    computedStyleMap() { return tomMap(StylePropertyMapReadOnly, () => getComputedStyle(this), true); },
});
const CSS_UNIT_FACTORIES = ['number', 'percent', 'em', 'rem', 'ex', 'rex', 'ch', 'rch', 'ic', 'ric', 'cap', 'rcap', 'lh', 'rlh', 'vw', 'vh', 'vi', 'vb', 'vmin', 'vmax', 'svw', 'svh', 'svi', 'svb', 'svmin', 'svmax', 'lvw', 'lvh', 'lvi', 'lvb', 'lvmin', 'lvmax', 'dvw', 'dvh', 'dvi', 'dvb', 'dvmin', 'dvmax', 'cqw', 'cqh', 'cqi', 'cqb', 'cqmin', 'cqmax', 'cm', 'mm', 'Q', 'in', 'pt', 'pc', 'px', 'deg', 'grad', 'rad', 'turn', 's', 'ms', 'Hz', 'kHz', 'dpi', 'dpcm', 'dppx', 'fr'];
