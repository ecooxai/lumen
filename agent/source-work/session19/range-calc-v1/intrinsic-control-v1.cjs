const apushDef = Reflect.defineProperty, apush = (a, v) => { apushDef(a, a.length, { value: v, writable: true, enumerable: true, configurable: true }); return a.length; };
const fs=require('fs'); const SMap=Map, OMap=Map;
const MATH_UNIT = { in: ['px', 96], cm: ['px', 96 / 2.54], mm: ['px', 96 / 25.4], q: ['px', 96 / 101.6], pt: ['px', 4 / 3], pc: ['px', 16], deg: ['deg', 1], rad: ['deg', 180 / Math.PI], grad: ['deg', 0.9], turn: ['deg', 360], s: ['s', 1], ms: ['s', 0.001], px: ['px', 1], '': ['', 1] };
const cssBorderPending = value => /(?:^|[^a-z0-9_-])(?:var|env|attr|if)\(|--/i.test(value);
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
const MATH_FNS = /^(calc|-webkit-calc|min|max|clamp|round|mod|rem|abs|sign|sin|cos|tan|asin|acos|atan|atan2|pow|sqrt|hypot|log|exp|progress)$/;

/* Specified range offsets retain calc() and typed zero percentages. This
 * bounded serializer runs only after native grammar validation. Functions
 * needing a value context remain opaque; unsupported trees keep their text. */
const cssRangeMathApply = Reflect.apply, cssRangeMathSort = Array.prototype.sort;
const cssRangeMathExec = RegExp.prototype.exec, cssRangeMathSlice = String.prototype.slice;
const cssRangeMathLower = String.prototype.toLowerCase, cssRangeMathMapGet = OMap.prototype.get;
const cssRangeMathOwn = Object.prototype.hasOwnProperty, cssRangeMathNumber = Number;
const cssRangeMathString = String, cssRangeMathFinite = Number.isFinite;
const cssRangeMathSafe = Number.isSafeInteger, cssRangeMathIs = Object.is;
function cssAnimationRangeSpecifiedMath(value) {
    const match = (re, text) => cssRangeMathApply(cssRangeMathExec, re, [text]);
    const slice = (start, end) => cssRangeMathApply(cssRangeMathSlice, value, [start, end]);
    const lower = text => cssRangeMathApply(cssRangeMathLower, text, []);
    if (!match(/^calc\(/i, value) || value.length > 4096 || match(/[\\'\"]|\/\*/, value) || cssBorderPending(value)) return value;
    let index = 0, depth = 0;
    const fail = () => { throw null; };
    const ws = () => { const before = index; while (index < value.length && match(/\s/, value[index])) index++; return index !== before; };
    const numeric = (number, unit) => {
        // Do not simplify away a signed-zero or nonfinite intermediate.
        if (!cssRangeMathFinite(number) || cssRangeMathIs(number, -0)) fail();
        return {kind: 'number', number, unit};
    };
    // Fold only exactly representable safe-integer arithmetic; other finite
    // decimal calculations retain their existing representation.
    const exact = number => { if (!cssRangeMathSafe(number)) fail(); return number; };
    const rank = node => node.kind !== 'number' ? 3 : !node.unit ? 0 : node.unit === '%' ? 1 : 2;
    const ordered = nodes => cssRangeMathApply(cssRangeMathSort, nodes, [(a, b) => {
        const ar = rank(a), br = rank(b);
        return ar !== br ? ar - br : ar === 2 ? (a.unit < b.unit ? -1 : a.unit > b.unit ? 1 : 0) : 0;
    }]);
    const sumOf = nodes => {
        const children = [];
        for (let n = 0; n < nodes.length; n++) {
            const node = nodes[n], terms = node.kind === 'sum' ? node.children : [node];
            for (let t = 0; t < terms.length; t++) {
                const term = terms[t]; let previous = -1;
                if (term.kind === 'number') for (let c = 0; c < children.length; c++)
                    if (children[c].kind === 'number' && children[c].unit === term.unit) { previous = c; break; }
                if (previous < 0) apush(children, term);
                else children[previous] = numeric(exact(exact(children[previous].number) + exact(term.number)), term.unit);
            }
        }
        // Keep zero-valued units:0% still carries a percentage dependency.
        ordered(children);
        return children.length === 1 ? children[0] : {kind: 'sum', children};
    };
    const productOf = nodes => {
        let combined = null; const rest = [];
        for (let n = 0; n < nodes.length; n++) {
            const node = nodes[n], terms = node.kind === 'product' ? node.children : [node];
            for (let t = 0; t < terms.length; t++) {
                const term = terms[t];
                if (term.kind === 'sum') fail(); // Distribution is outside this bounded path.
                if (term.kind !== 'number') { apush(rest, term); continue; }
                if (combined && combined.unit && term.unit) fail();
                combined = combined ? numeric(exact(exact(combined.number) * exact(term.number)), combined.unit || term.unit) : term;
            }
        }
        const children = [];
        if (combined) apush(children, combined);
        for (let n = 0; n < rest.length; n++) apush(children, rest[n]);
        ordered(children);
        return children.length === 1 ? children[0] : {kind: 'product', children};
    };
    function atom() {
        ws(); if (++depth > 32) fail();
        let result;
        if (value[index] === '(') {
            index++; result = sum(); ws(); if (value[index++] !== ')') fail();
        } else {
            const token = match(/^([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)(%|[a-z]+)?/i, slice(index));
            if (token) {
                index += token[0].length;
                let number = cssRangeMathNumber(token[1]), unit = lower(token[2] || '');
                if (unit && unit !== '%' && cssRangeMathApply(cssRangeMathMapGet, MATH_UNITS, [unit]) !== 'length') fail();
                if (unit && unit !== '%' && cssRangeMathApply(cssRangeMathOwn, MATH_UNIT, [unit])) {
                    if (MATH_UNIT[unit][0] !== 'px') fail();
                    number = exact(exact(number) * exact(MATH_UNIT[unit][1])); unit = 'px';
                }
                result = numeric(number, unit);
            } else {
                const fn = match(/^([a-z-][a-z0-9-]*)\(/i, slice(index)); if (!fn) fail();
                const start = index; index += fn[0].length;
                if (lower(fn[1]) === 'calc') {
                    result = sum(); ws(); if (value[index++] !== ')') fail();
                } else {
                    let nesting = 1;
                    while (index < value.length && nesting) {
                        if (value[index] === '(') nesting++;
                        else if (value[index] === ')') nesting--;
                        index++;
                    }
                    if (nesting) fail();
                    result = {kind: 'opaque', text: slice(start, index)};
                }
            }
        }
        depth--; return result;
    }
    function product() {
        const terms = [atom()];
        for (;;) {
            const before = index; ws(); const operation = value[index];
            if (operation !== '*' && operation !== '/') { index = before; break; }
            index++; let term = atom();
            if (operation === '/') {
                if (term.kind !== 'number' || term.unit || term.number === 0) fail();
                term = numeric(exact(1 / exact(term.number)), '');
            }
            apush(terms, term);
        }
        return productOf(terms);
    }
    function sum() {
        const terms = [product()];
        for (;;) {
            const before = index, spaced = ws(), operation = value[index];
            if ((operation !== '+' && operation !== '-') || !spaced || !match(/\s/, value[index + 1] || '')) { index = before; break; }
            index++; let term = product();
            if (operation === '-') {
                if (term.kind !== 'number') fail();
                term = numeric(-term.number, term.unit);
            }
            apush(terms, term);
        }
        return sumOf(terms);
    }
    function text(node, parent = 0) {
        if (node.kind === 'number') return cssRangeMathString(node.number) + node.unit;
        if (node.kind === 'opaque') return node.text;
        const precedence = node.kind === 'sum' ? 1 : 2;
        let result = '';
        for (let i = 0; i < node.children.length; i++) {
            const child = node.children[i];
            if (i && node.kind === 'sum' && child.kind === 'number' && child.number < 0)
                result += ' - ' + text(numeric(-child.number, child.unit), precedence);
            else result += (i ? node.kind === 'sum' ? ' + ' : ' * ' : '') + text(child, precedence);
        }
        return precedence < parent ? '(' + result + ')' : result;
    }
    try {
        const root = atom(); ws(); if (index !== value.length) return value;
        return 'calc(' + text(root) + ')';
    } catch (_) { return value; }
}



const original={Number, String, finite:Number.isFinite, safe:Number.isSafeInteger, is:Object.is, sort:Array.prototype.sort, push:Array.prototype.push, iter:Array.prototype[Symbol.iterator], mapGet:Map.prototype.get, slice:String.prototype.slice, lower:String.prototype.toLowerCase};
const define=Reflect.defineProperty, zero=Reflect.getOwnPropertyDescriptor(Array.prototype,'0');let calls=0,result,error;
const replaced=()=>{calls++;throw new Error('author intrinsic called');};
try {
 Number.isFinite=replaced;Number.isSafeInteger=replaced;Object.is=replaced;Array.prototype.sort=replaced;Array.prototype.push=replaced;Array.prototype[Symbol.iterator]=replaced;Map.prototype.get=replaced;String.prototype.slice=replaced;String.prototype.toLowerCase=replaced;
 globalThis.Number=replaced;globalThis.String=replaced;
 define(Array.prototype,'0',{configurable:true,set:replaced});
 result=cssAnimationRangeSpecifiedMath('calc(sign(100em - 1px) * 1%)');
} catch(e){error=''+e;}
finally {
 globalThis.Number=original.Number;globalThis.String=original.String;Number.isFinite=original.finite;Number.isSafeInteger=original.safe;Object.is=original.is;Array.prototype.sort=original.sort;Array.prototype.push=original.push;Array.prototype[Symbol.iterator]=original.iter;Map.prototype.get=original.mapGet;String.prototype.slice=original.slice;String.prototype.toLowerCase=original.lower;if(zero)define(Array.prototype,'0',zero);else Reflect.deleteProperty(Array.prototype,'0');
}
const report={scope:'Isolated new helper only; pre-existing/native parser intrinsic behavior is not exercised.',expected:'calc(1% * sign(100em - 1px))',actual:result,author_calls:calls,error:error||null};report.passed=report.actual===report.expected&&calls===0&&!error;fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));process.exitCode=report.passed?0:1;
