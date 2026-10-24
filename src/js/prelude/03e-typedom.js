/* CSS Typed OM: style values, numeric math, transforms and the element/rule style maps. */
/*@shared typedom.js*/
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
    if (/(^|-)color$/.test(prop) && /^[a-z]+$/i.test(text) && !/^(currentcolor|auto)$/i.test(text)) return tomGeneric(text);   /* named colours are not keywords */
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
const TOM_LEN_UNITS = new SSet(['', '%', 'px', 'em', 'rem', 'ex', 'rex', 'ch', 'rch', 'ic', 'ric', 'cap', 'rcap', 'lh', 'rlh', 'vw', 'vh', 'vi', 'vb', 'vmin', 'vmax', 'svw', 'svh', 'svi', 'svb', 'svmin', 'svmax', 'lvw', 'lvh', 'lvi', 'lvb', 'lvmin', 'lvmax', 'dvw', 'dvh', 'dvi', 'dvb', 'dvmin', 'dvmax', 'cqw', 'cqh', 'cqi', 'cqb', 'cqmin', 'cqmax', 'cm', 'mm', 'q', 'in', 'pt', 'pc']);
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
const TOM_CANON = { cm: ['px', 96 / 2.54], mm: ['px', 96 / 25.4], q: ['px', 96 / 101.6], in: ['px', 96], pt: ['px', 4 / 3], pc: ['px', 16], ms: ['s', 0.001], rad: ['deg', 180 / Math.PI], grad: ['deg', 0.9], turn: ['deg', 360] };
/* computed values: absolute units in their canonical unit, single-unit calc() folded */
const tomComputedText = (p, v) => {
    if (p.startsWith('--') || /["']|url\(|var\(/i.test(v)) return v;
    v = v.replace(/(^|[\s(,])([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)(cm|mm|q|in|pt|pc|ms|rad|grad|turn)(?=$|[\s),])/gi, (m, pre, n, u) => { const c = TOM_CANON[u.toLowerCase()]; return pre + tomNumStr(+n * c[1]) + c[0]; });
    return v.replace(/\bcalc\((?:[^()]|\([^()]*\))*\)/gi, m => { try { const r = tomSumMap(CSSNumericValue.parse(m)); if (r && r.size === 1) { const [[u, x]] = r; return tomNumStr(x) + (u === 'number' ? '' : u === 'percent' ? '%' : u); } } catch (e) {} return m; });
};
const tomRead = (m, p) => { const v = tomData(m).decl().getPropertyValue(p); return v == null ? '' : tomData(m).computed ? tomComputedText(p, String(v)) : String(v); };
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
    if (/^(-webkit-)?text-size-adjust$/.test(p)) return k.length === 1 && k[0] === 'percent';
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
