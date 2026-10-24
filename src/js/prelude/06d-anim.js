/* Web Animations: timing model, KeyframeEffect, Animation and the document timeline. Effect values go to the
   native animation declaration layer (N.setAnimStyle: above inline style, below !important); getComputedStyle
   flushes pending changes first (waFlush), and a frame loop runs only while an animation is running or pending. */
const waTypeErr = (m) => new TypeError(m);
/* ---- easing ---- */
function waBezier(x1, y1, x2, y2) {
    const cx = 3 * x1, bx = 3 * (x2 - x1) - cx, ax = 1 - cx - bx, cy = 3 * y1, by = 3 * (y2 - y1) - cy, ay = 1 - cy - by;
    const sx = t => ((ax * t + bx) * t + cx) * t, sy = t => ((ay * t + by) * t + cy) * t, dx = t => (3 * ax * t + 2 * bx) * t + cx;
    const solve = x => {
        let t = x;
        for (let i = 0; i < 8; i++) { const e = sx(t) - x; if (Math.abs(e) < 1e-7) return t; const d = dx(t); if (Math.abs(d) < 1e-6) break; t -= e / d; }
        let lo = 0, hi = 1; t = x;
        for (let i = 0; i < 60 && hi - lo > 1e-9; i++) { const v = sx(t); if (Math.abs(v - x) < 1e-7) return t; if (x > v) lo = t; else hi = t; t = (lo + hi) / 2; }
        return t;
    };
    return x => {
        if (x < 0) { const s = x1 > 0 ? y1 / x1 : !y1 && x2 > 0 ? y2 / x2 : 0; return s * x; }
        if (x > 1) { const s = x2 < 1 ? (y2 - 1) / (x2 - 1) : y2 === 1 && x1 < 1 ? (y1 - 1) / (x1 - 1) : 0; return 1 + s * (x - 1); }
        return sy(solve(x));
    };
}
const waSteps = (n, pos) => (x, before) => {
    let s = Math.floor(x * n);
    if (pos === 'jump-start' || pos === 'jump-both') s++;
    if (before && (x * n) % 1 === 0) s--;
    if (x >= 0 && s < 0) s = 0;
    const j = pos === 'jump-both' ? n + 1 : pos === 'jump-none' ? n - 1 : n;
    if (x <= 1 && s > j) s = j;
    return s / j;
};
const WA_NUM = /^[-+]?(\d+\.?\d*|\.\d+)(e[-+]?\d+)?$/;
function waLinear(body, bad) {
    const pts = [];
    for (const part of body.split(',')) {
        const t = part.trim().split(/\s+/); if (!t[0] || !WA_NUM.test(t[0]) || t.length > 3) bad();
        const ps = t.slice(1).map(x => { if (!/%$/.test(x) || !WA_NUM.test(x.slice(0, -1))) bad(); return +x.slice(0, -1) / 100; });
        if (!ps.length) pts.push([+t[0], null]); else for (const p of ps) pts.push([+t[0], p]);
    }
    if (pts.length < 2) bad();
    if (pts[0][1] === null) pts[0][1] = 0;
    if (pts[pts.length - 1][1] === null) pts[pts.length - 1][1] = 1;
    let mx = -Infinity; for (const p of pts) if (p[1] !== null) { p[1] = Math.max(p[1], mx); mx = p[1]; }
    for (let i = 1; i < pts.length; i++) if (pts[i][1] === null) {
        let j = i; while (pts[j][1] === null) j++;
        for (let k = i; k < j; k++) pts[k][1] = pts[i - 1][1] + (pts[j][1] - pts[i - 1][1]) * (k - i + 1) / (j - i + 1);
    }
    const f = x => {
        let i = 0; while (i < pts.length - 2 && x >= pts[i + 1][1]) i++;
        if (x >= pts[pts.length - 1][1]) i = pts.length - 2;
        const [y0, x0] = pts[i], [y1, x1v] = pts[i + 1];
        return x1v === x0 ? y1 : y0 + (y1 - y0) * (x - x0) / (x1v - x0);
    };
    return { s: 'linear(' + pts.map(p => `${p[0]} ${+(p[1] * 100).toFixed(4)}%`).join(', ') + ')', f };
}
function waEasing(src) {
    const t = String(src).replace(/\/\*[\s\S]*?\*\//g, '').replace(/\\([0-9a-fA-F]{1,6})\s?/g, (_, h) => String.fromCodePoint(parseInt(h, 16))).trim().toLowerCase();
    const bad = () => { throw waTypeErr(`Failed to execute: '${src}' is not a valid value for easing`); };
    const B = { ease: [0.25, 0.1, 0.25, 1], 'ease-in': [0.42, 0, 1, 1], 'ease-out': [0, 0, 0.58, 1], 'ease-in-out': [0.42, 0, 0.58, 1] };
    if (t === 'linear') return { s: t, f: x => x };
    if (B[t]) return { s: t, f: waBezier(...B[t]) };
    if (t === 'step-start') return { s: 'steps(1, start)', f: waSteps(1, 'jump-start') };
    if (t === 'step-end') return { s: 'steps(1)', f: waSteps(1, 'jump-end') };
    let m = /^cubic-bezier\(([^()]*)\)$/.exec(t);
    if (m) {
        const a = m[1].split(',').map(x => x.trim());
        if (a.length !== 4 || a.some(x => !WA_NUM.test(x))) bad();
        const v = a.map(Number); if (v[0] < 0 || v[0] > 1 || v[2] < 0 || v[2] > 1) bad();
        return { s: `cubic-bezier(${v.join(', ')})`, f: waBezier(...v) };
    }
    m = /^steps\(\s*([^,()]*?)\s*(?:,\s*([a-z-]+)\s*)?\)$/.exec(t);
    if (m) {
        if (!/^\+?\d+$/.test(m[1])) bad();
        const n = +m[1], kw = m[2] || 'end', P = { start: 'jump-start', end: 'jump-end', 'jump-start': 'jump-start', 'jump-end': 'jump-end', 'jump-none': 'jump-none', 'jump-both': 'jump-both' }[kw];
        if (!P || n < 1 || (P === 'jump-none' && n < 2)) bad();
        return { s: P === 'jump-end' ? `steps(${n})` : `steps(${n}, ${kw})`, f: waSteps(n, P) };
    }
    m = /^linear\((.*)\)$/.exec(t); if (m) return waLinear(m[1], bad);
    bad();
}
const WA_LINEAR = x => x;
/* ---- timing ---- */
const WA_FILL = ['none', 'forwards', 'backwards', 'both', 'auto'], WA_DIR = ['normal', 'reverse', 'alternate', 'alternate-reverse'];
const waEnum = (v, list, what) => { v = String(v); if (!list.includes(v)) throw waTypeErr(`Failed to execute: The provided value '${v}' is not a valid enum value of type ${what}.`); return v; };
const WA_T0 = { delay: 0, endDelay: 0, fill: 'auto', iterationStart: 0, iterations: 1, duration: 'auto', direction: 'normal', easing: 'linear', ease: WA_LINEAR };
function waTiming(base, o) {
    const r = Object.assign({}, base);
    if (o === undefined || o === null) return r;
    if (typeof o !== 'object' && typeof o !== 'function') o = { duration: +o };
    const fin = (k) => { const v = +o[k]; if (!isFinite(v)) throw waTypeErr(`Failed to execute: '${k}' must be a finite number.`); return v; };
    const d = o.delay, dir = o.direction, du = o.duration, ea = o.easing, ed = o.endDelay, fi = o.fill, is = o.iterationStart, it = o.iterations;
    if (d !== undefined) r.delay = fin('delay');
    if (dir !== undefined) r.direction = waEnum(dir, WA_DIR, 'PlaybackDirection');
    if (du !== undefined) {
        if (typeof du === 'string') { if (du !== 'auto') throw waTypeErr(`Failed to execute: Invalid duration '${du}'.`); r.duration = 'auto'; }
        else { const v = +du; if (!(v >= 0)) throw waTypeErr('Failed to execute: duration must be non-negative or auto.'); r.duration = v; }
    }
    if (ed !== undefined) r.endDelay = fin('endDelay');
    if (fi !== undefined) r.fill = waEnum(fi, WA_FILL, 'FillMode');
    if (is !== undefined) { const v = fin('iterationStart'); if (v < 0) throw waTypeErr('Failed to execute: iterationStart must be non-negative.'); r.iterationStart = v; }
    if (it !== undefined) { const v = +it; if (!(v >= 0)) throw waTypeErr('Failed to execute: iterationCount must be non-negative.'); r.iterations = v; }
    if (ea !== undefined) { const e = waEasing(ea); r.easing = e.s; r.ease = e.f; }
    return r;
}
const waDur = t => t.duration === 'auto' ? 0 : t.duration;
const waActive = t => { const d = waDur(t); return d === 0 || t.iterations === 0 ? 0 : d * t.iterations; };
const waEndTime = t => Math.max(t.delay + waActive(t) + t.endDelay, 0);
function waCompute(eff) {
    const t = eff._t, a = eff._anim, local = a ? a.currentTime : null;
    const dur = waDur(t), ad = waActive(t), end = waEndTime(t), fill = t.fill === 'auto' ? 'none' : t.fill;
    const r = { localTime: local, phase: 'idle', activeTime: null, progress: null, currentIteration: null, endTime: end, activeDuration: ad, duration: dur, fill };
    if (local === null) return r;
    const back = a._effRate() < 0, ba = Math.max(Math.min(t.delay, end), 0), aa = Math.max(Math.min(t.delay + ad, end), 0);
    r.phase = local < ba || (back && local === ba) ? 'before' : local > aa || (!back && local === aa) ? 'after' : 'active';
    if (r.phase === 'before') r.activeTime = fill === 'backwards' || fill === 'both' ? Math.max(local - t.delay, 0) : null;
    else if (r.phase === 'active') r.activeTime = local - t.delay;
    else r.activeTime = fill === 'forwards' || fill === 'both' ? Math.max(Math.min(local - t.delay, ad), 0) : null;
    if (r.activeTime === null) return r;
    const ov = dur === 0 ? (r.phase === 'before' ? t.iterationStart : t.iterationStart + t.iterations) : r.activeTime / dur + t.iterationStart;
    let sp = isFinite(ov) ? ov % 1 : t.iterationStart % 1;
    if (sp === 0 && (r.phase === 'active' || r.phase === 'after') && r.activeTime === ad && t.iterations !== 0) sp = 1;
    const it = r.phase === 'after' && t.iterations === Infinity ? Infinity : sp === 1 ? Math.floor(ov) - 1 : Math.floor(ov);
    let fwd = true;
    if (t.direction === 'reverse') fwd = false;
    else if (t.direction !== 'normal') fwd = (it + (t.direction === 'alternate-reverse' ? 1 : 0)) % 2 === 0;
    const dp = fwd ? sp : 1 - sp, before = (r.phase === 'before' && fwd) || (r.phase === 'after' && !fwd);
    r.progress = t.ease(dp, before); r.currentIteration = it; r.overall = ov;
    return r;
}
/* ---- keyframes ---- */
const WA_PROPS = new Set(('align-content align-items align-self all aspect-ratio backdrop-filter background background-attachment background-blend-mode background-clip background-color background-image background-origin background-position background-position-x background-position-y background-repeat background-size baseline-shift block-size border border-block border-block-end border-block-start border-bottom border-bottom-color border-bottom-left-radius border-bottom-right-radius border-bottom-style border-bottom-width border-collapse border-color border-image border-image-outset border-image-repeat border-image-slice border-image-source border-image-width border-inline border-inline-end border-inline-start border-left border-left-color border-left-style border-left-width border-radius border-right border-right-color border-right-style border-right-width border-spacing border-style border-top border-top-color border-top-left-radius border-top-right-radius border-top-style border-top-width border-width bottom box-shadow box-sizing break-after break-before break-inside caption-side caret-color clear clip clip-path clip-rule color color-interpolation color-interpolation-filters color-scheme column-count column-fill column-gap column-rule column-rule-color column-rule-style column-rule-width column-span column-width columns content content-visibility counter-increment counter-reset counter-set cursor cx cy d display dominant-baseline empty-cells fill fill-opacity fill-rule filter flex flex-basis flex-direction flex-flow flex-grow flex-shrink flex-wrap float flood-color flood-opacity font font-family font-feature-settings font-kerning font-optical-sizing font-palette font-size font-size-adjust font-stretch font-style font-synthesis font-variant font-variant-caps font-variant-east-asian font-variant-ligatures font-variant-numeric font-variation-settings font-weight gap grid grid-area grid-auto-columns grid-auto-flow grid-auto-rows grid-column grid-column-end grid-column-gap grid-column-start grid-gap grid-row grid-row-end grid-row-gap grid-row-start grid-template grid-template-areas grid-template-columns grid-template-rows height hyphens image-orientation image-rendering inline-size inset inset-block inset-block-end inset-block-start inset-inline inset-inline-end inset-inline-start isolation justify-content justify-items justify-self left letter-spacing lighting-color line-break line-height list-style list-style-image list-style-position list-style-type margin margin-block margin-block-end margin-block-start margin-bottom margin-inline margin-inline-end margin-inline-start margin-left margin-right margin-top marker marker-end marker-mid marker-start mask mask-image mask-position mask-repeat mask-size mask-type math-depth max-block-size max-height max-inline-size max-width min-block-size min-height min-inline-size min-width mix-blend-mode object-fit object-position offset offset-anchor offset-distance offset-path offset-position offset-rotate opacity order orphans outline outline-color outline-offset outline-style outline-width overflow overflow-anchor overflow-clip-margin overflow-wrap overflow-x overflow-y overscroll-behavior overscroll-behavior-x overscroll-behavior-y padding padding-block padding-block-end padding-block-start padding-bottom padding-inline padding-inline-end padding-inline-start padding-left padding-right padding-top page-break-after page-break-before page-break-inside paint-order perspective perspective-origin place-content place-items place-self pointer-events position quotes r resize right rotate row-gap rx ry scale scroll-behavior scroll-margin scroll-padding scroll-snap-align scroll-snap-stop scroll-snap-type scrollbar-color scrollbar-gutter scrollbar-width shape-image-threshold shape-margin shape-outside shape-rendering stop-color stop-opacity stroke stroke-dasharray stroke-dashoffset stroke-linecap stroke-linejoin stroke-miterlimit stroke-opacity stroke-width tab-size table-layout text-align text-align-last text-anchor text-decoration text-decoration-color text-decoration-line text-decoration-skip-ink text-decoration-style text-decoration-thickness text-emphasis text-emphasis-color text-emphasis-position text-emphasis-style text-indent text-overflow text-rendering text-shadow text-size-adjust text-transform text-underline-offset text-underline-position text-wrap top touch-action transform transform-box transform-origin transform-style translate user-select vector-effect vertical-align visibility white-space white-space-collapse widows width word-break word-spacing word-wrap x y z-index zoom -webkit-line-clamp -webkit-mask-image -webkit-text-stroke-color -webkit-text-fill-color -webkit-text-stroke-width').split(' '));
const waPropOf = (k) => {
    if (k.startsWith('--')) return k;
    if (k === 'cssFloat') return 'float';
    if (k === 'cssOffset') return 'offset';
    if (k === 'float' || k === 'offset' || /[^a-zA-Z]/.test(k)) return null;
    const p = camelToKebab(k);
    return WA_PROPS.has(p) ? p : null;
};
const waIdl = (p) => p.startsWith('--') ? p : p === 'float' ? 'cssFloat' : p === 'offset' ? 'cssOffset' : p.replace(/^-webkit-/, 'webkit-').replace(/-([a-z])/g, (_, c) => c.toUpperCase());
const waSeq = (v) => v !== null && typeof v === 'object' && typeof v[Symbol.iterator] === 'function';
const WA_COMP = ['replace', 'add', 'accumulate'];
const waComp = (v) => waEnum(v, [...WA_COMP, 'auto'], 'CompositeOperationOrAuto');
const waOff = (v) => { if (v === null) return null; const n = +v; if (!isFinite(n)) throw waTypeErr('Failed to execute: Non-finite offset.'); return n; };
function waReadKf(o, indexed) {
    const c = o.composite, e = o.easing, off = o.offset, r = { props: new Map() };
    if (indexed) {
        r.composite = c === undefined ? [] : waSeq(c) ? [...c].map(waComp) : [waComp(c)];
        r.easing = e === undefined ? [] : waSeq(e) ? [...e].map(String) : [String(e)];
        r.offset = off === undefined ? [] : waSeq(off) ? [...off].map(waOff) : [waOff(off)];
    } else {
        r.composite = c === undefined ? 'auto' : waComp(c); r.easing = e === undefined ? 'linear' : String(e);
        r.offset = off === undefined ? null : waOff(off);
    }
    for (const k of Object.keys(o).filter(waPropOf).sort()) {
        const v = o[k], p = waPropOf(k);
        r.props.set(p, indexed ? (waSeq(v) ? [...v].map(String) : [String(v)]) : String(v));
    }
    return r;
}
function waCheckOffsets(list) {
    let prev = -Infinity;
    for (const k of list) if (k.offset !== null) {
        if (k.offset < 0 || k.offset > 1) throw waTypeErr('Failed to execute: Offsets must be null or in the range [0,1].');
        if (k.offset < prev) throw waTypeErr('Failed to execute: Offsets must be monotonically non-decreasing.');
        prev = k.offset;
    }
}
function waKeyframes(obj) {
    if (obj === null || obj === undefined) return [];
    if (typeof obj !== 'object' && typeof obj !== 'function') throw waTypeErr("Failed to execute: The provided value is not of type '(object or null)'.");
    const out = [], easings = [];
    const iter = obj[Symbol.iterator];
    if (iter !== undefined && iter !== null) {
        if (typeof iter !== 'function') throw waTypeErr('Failed to execute: @@iterator is not callable.');
        for (const kf of { [Symbol.iterator]: () => iter.call(obj) }) {
            if (kf !== null && kf !== undefined && typeof kf !== 'object' && typeof kf !== 'function') throw waTypeErr('Failed to execute: Keyframes must be objects.');
            const k = waReadKf(kf == null ? {} : kf, false);
            out.push(k); easings.push(k.easing);
        }
        waCheckOffsets(out);
    } else {
        const b = waReadKf(obj, true), list = [];
        for (const [p, vals] of b.props) vals.forEach((v, i) => list.push({ co: vals.length === 1 ? 1 : i / (vals.length - 1), p, v }));
        list.sort((x, y) => x.co - y.co);
        for (const e of list) {
            const last = out[out.length - 1];
            if (last && last.co === e.co) last.props.set(e.p, e.v);
            else out.push({ offset: null, co: e.co, easing: 'linear', composite: 'auto', props: new Map([[e.p, e.v]]) });
        }
        b.offset.forEach((o, i) => { if (i < out.length) out[i].offset = o; });
        waCheckOffsets(out);
        const es = b.easing.length ? b.easing : ['linear'];
        for (let i = 0; i < Math.max(es.length, out.length); i++) { const ez = es[i % es.length]; if (i < out.length) out[i].easing = ez; easings.push(ez); }
        if (b.composite.length) out.forEach((k, i) => { k.composite = b.composite[i % b.composite.length]; });
    }
    const parsed = easings.map(waEasing);
    out.forEach((k, i) => { const e = parsed[i]; k.easing = e.s; k.ease = e.f; });
    waComputeOffsets(out);
    return out;
}
function waComputeOffsets(kfs) {
    const n = kfs.length; if (!n) return;
    kfs.forEach(k => { k.co = k.offset; });
    if (n === 1) { if (kfs[0].co === null) kfs[0].co = 1; return; }
    if (kfs[0].co === null) kfs[0].co = 0;
    if (kfs[n - 1].co === null) kfs[n - 1].co = 1;
    for (let i = 1; i < n; i++) if (kfs[i].co === null) {
        let j = i; while (kfs[j].co === null) j++;
        for (let k = i; k < j; k++) kfs[k].co = kfs[i - 1].co + (kfs[j].co - kfs[i - 1].co) * (k - i + 1) / (j - i + 1);
        i = j;
    }
}
/* ---- values: interpolation and composition on token lists ---- */
const waFmt = (v) => { const r = Math.round(v * 1e6) / 1e6; return String(Object.is(r, -0) ? 0 : r); };
const WA_TOK = /(#[0-9a-fA-F]{3,8}\b|(?:rgba?|hsla?|hwb|lab|lch|oklab|oklch|color)\([^()]*\))|([-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?)(%|[a-zA-Z]+)?|([a-zA-Z_-][\w-]*\(?)|("[^"]*"|'[^']*')|(\S)/g;
function waTokens(s) {
    const out = []; let m; WA_TOK.lastIndex = 0;
    while ((m = WA_TOK.exec(s))) {
        if (m[1] || (m[4] && NAMED_COLORS.has(m[4].toLowerCase())) || (m[4] && m[4].toLowerCase() === 'transparent')) {
            const c = cssColor(m[1] || m[4]);
            if (c && !c.current) { const rgb = colorTo(c, 'srgb'); out.push({ c: [rgb[0] * 255, rgb[1] * 255, rgb[2] * 255, c.alpha] }); continue; }
        }
        if (m[2] !== undefined) out.push({ n: +m[2], u: (m[3] || '').toLowerCase() });
        else out.push({ s: m[0] });
    }
    return out;
}
const waColorStr = (c) => { const [r, g, b] = c.map(v => Math.round(Math.min(255, Math.max(0, v)))); const a = Math.min(1, Math.max(0, c[3])); return a >= 1 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${waFmt(a)})`; };
function waJoin(toks) {
    let s = '';
    for (const t of toks) {
        const v = t.c ? waColorStr(t.c) : t.n !== undefined ? waFmt(t.n) + t.u : t.calc || t.s;
        if (s && !/[(\s]$/.test(s) && v !== ',' && v !== ')') s += ' ';
        if (v === ',') s = s.replace(/\s+$/, '');
        s += v;
    }
    return s.replace(/\(\s+/g, '(').replace(/\s+\)/g, ')');
}
const WA_INT = new Set(['z-index', 'order', 'column-count', 'orphans', 'widows', '-webkit-line-clamp']);
const WA_NONNEG = /^(width|height|min-|max-|padding|border-.*width|border-.*radius|outline-width|font-size|line-height|flex-basis|flex-grow|flex-shrink|column-width|column-rule-width|stroke-width|r$|rx$|ry$|perspective|letter-spacing$^)/;
const WA_UNIT01 = new Set(['opacity', 'fill-opacity', 'stroke-opacity', 'flood-opacity', 'stop-opacity', 'shape-image-threshold']);
function waMix(prop, a, b, p, op) {   /* op: interpolate (p) or add (p ignored, b added to a) */
    const A = waTokens(a), B = waTokens(b);
    if (A.length !== B.length) return null;
    const out = [];
    for (let i = 0; i < A.length; i++) {
        const x = A[i], y = B[i];
        if (x.c && y.c) {
            if (op) { out.push({ c: x.c.map((v, k) => v + y.c[k]) }); continue; }
            const al = x.c[3] + (y.c[3] - x.c[3]) * p;
            const ch = [0, 1, 2].map(k => al ? (x.c[k] * x.c[3] + (y.c[k] * y.c[3] - x.c[k] * x.c[3]) * p) / al : 0);
            out.push({ c: [...ch, al] }); continue;
        }
        if (x.n !== undefined && y.n !== undefined) {
            let u = x.u, xu = x.u, yu = y.u;
            if (xu !== yu) {
                if (x.n === 0 && !xu) xu = u = yu; else if (y.n === 0 && !yu) yu = u = xu;
                else if (xu && yu && !/^(deg|rad|turn|grad|s|ms)$/.test(xu + yu)) {
                    out.push({ calc: op ? `calc(${waFmt(x.n)}${xu} + ${waFmt(y.n)}${yu})` : `calc(${waFmt(x.n * (1 - p))}${xu} + ${waFmt(y.n * p)}${yu})` }); continue;
                } else return null;
            }
            let v = op ? x.n + y.n : x.n + (y.n - x.n) * p;
            if (WA_INT.has(prop)) v = Math.round(v);
            if (WA_UNIT01.has(prop) && !u) v = Math.min(1, Math.max(0, v));
            if (WA_NONNEG.test(prop) && v < 0) v = 0;
            out.push({ n: v, u }); continue;
        }
        if (x.s !== undefined && x.s === y.s) { out.push(x); continue; }
        return null;
    }
    return waJoin(out);
}
/* transforms: matching function lists interpolate per argument; otherwise through 2-D matrices */
const waAngle = (n, u) => u === 'rad' ? n : u === 'turn' ? n * 2 * Math.PI : u === 'grad' ? n * Math.PI / 200 : n * Math.PI / 180;
function waMatrix(s) {
    let M = [1, 0, 0, 1, 0, 0];
    const mul = (a, b) => [a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3], a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5]];
    if (s.trim() === 'none') return M;
    const re = /([a-zA-Z]+)\(([^()]*)\)/g; let m, any = false;
    while ((m = re.exec(s))) {
        any = true;
        const f = m[1].toLowerCase(), args = m[2].split(',').map(x => x.trim()).map(x => { const k = /^([-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?)([a-z%]*)$/i.exec(x); return k ? { n: +k[1], u: k[2].toLowerCase() } : null; });
        if (args.some(x => !x)) return null;
        const px = (a) => a && (a.u === 'px' || (!a.u && a.n === 0)) ? a.n : NaN, ang = (a) => waAngle(a.n, a.u);
        let T;
        if (f === 'translate') T = [1, 0, 0, 1, px(args[0]), args[1] ? px(args[1]) : 0];
        else if (f === 'translatex') T = [1, 0, 0, 1, px(args[0]), 0];
        else if (f === 'translatey') T = [1, 0, 0, 1, 0, px(args[0])];
        else if (f === 'scale') T = [args[0].n, 0, 0, args[1] ? args[1].n : args[0].n, 0, 0];
        else if (f === 'scalex') T = [args[0].n, 0, 0, 1, 0, 0];
        else if (f === 'scaley') T = [1, 0, 0, args[0].n, 0, 0];
        else if (f === 'rotate' || f === 'rotatez') { const r = ang(args[0]); T = [Math.cos(r), Math.sin(r), -Math.sin(r), Math.cos(r), 0, 0]; }
        else if (f === 'skew') T = [1, args[1] ? Math.tan(ang(args[1])) : 0, Math.tan(ang(args[0])), 1, 0, 0];
        else if (f === 'skewx') T = [1, 0, Math.tan(ang(args[0])), 1, 0, 0];
        else if (f === 'skewy') T = [1, Math.tan(ang(args[0])), 0, 1, 0, 0];
        else if (f === 'matrix' && args.length === 6) T = args.map(a => a.n);
        else return null;
        if (T.some(isNaN)) return null;
        M = mul(M, T);
    }
    return any ? M : null;
}
function waDecompose(M) {
    const [a, b, c, d, e, f] = M, sx = Math.hypot(a, b);
    if (!sx) return null;
    return { tx: e, ty: f, th: Math.atan2(b, a), sx, k: (a * c + b * d) / sx, sy: (a * d - b * c) / sx };
}
function waTransform(a, b, p, op) {
    const fa = a.trim() === 'none', fb = b.trim() === 'none';
    if (op) return fa ? b : fb ? a : a + ' ' + b;
    const ident = (s) => s.replace(/([a-zA-Z]+)\(([^()]*)\)/g, (_, f, args) => f + '(' + args.split(',').map(x => { const k = /^\s*[-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?([a-z%]*)\s*$/i.exec(x); return (/^scale/i.test(f) ? '1' : '0' + (k ? k[1] : '')); }).join(', ') + ')').replace(/matrix\(0, 0, 0, 0, 0, 0\)/i, 'matrix(1, 0, 0, 1, 0, 0)');
    if (fa && fb) return 'none';
    const A = fa ? ident(b) : a, B = fb ? ident(a) : b;
    const names = s => (s.match(/[a-zA-Z0-9]+\(/g) || []).join();
    if (names(A) === names(B)) { const r = waMix('transform', A, B, p); if (r !== null) return r; }
    const ma = waMatrix(A), mb = waMatrix(B), da = ma && waDecompose(ma), db = mb && waDecompose(mb);
    if (!da || !db) return p < 0.5 ? a : b;
    let t1 = da.th, t2 = db.th;
    if (Math.abs(t2 - t1) > Math.PI) { if (t1 < t2) t1 += 2 * Math.PI; else t2 += 2 * Math.PI; }
    const L = (x, y) => x + (y - x) * p, th = L(t1, t2), sx = L(da.sx, db.sx), k = L(da.k, db.k), sy = L(da.sy, db.sy), cs = Math.cos(th), sn = Math.sin(th);
    return `matrix(${[cs * sx, sn * sx, cs * k - sn * sy, sn * k + cs * sy, L(da.tx, db.tx), L(da.ty, db.ty)].map(waFmt).join(', ')})`;
}
function waInterp(prop, a, b, p) {
    if (p === 0) return a; if (p === 1) return b;
    if (prop === 'transform') return waTransform(a, b, p);
    if (prop === 'visibility' && (a === 'visible' || b === 'visible') && p > 0 && p < 1) return 'visible';
    if (!prop.startsWith('--')) { const r = waMix(prop, a, b, p); if (r !== null) return r; }
    return p < 0.5 ? a : b;
}
function waCompose(prop, under, v, op) {
    if (op === 'replace' || under == null) return v;
    if (prop === 'transform') return waTransform(under, v, 0, true);
    const r = prop.startsWith('--') ? null : waMix(prop, under, v, 0, true);
    return r === null ? v : r;
}
/* ---- KeyframeEffect ---- */
let waInternal = false;
class AnimationEffect {
    constructor() { if (!waInternal) illegal(); }
    getTiming() { const t = this._t; return { delay: t.delay, endDelay: t.endDelay, fill: t.fill, iterationStart: t.iterationStart, iterations: t.iterations, duration: t.duration, direction: t.direction, easing: t.easing }; }
    getComputedTiming() {
        const t = this._t, c = waCompute(this);
        return { delay: t.delay, endDelay: t.endDelay, fill: c.fill, iterationStart: t.iterationStart, iterations: t.iterations, duration: c.duration, direction: t.direction, easing: t.easing,
            endTime: c.endTime, activeDuration: c.activeDuration, localTime: c.localTime, progress: c.progress, currentIteration: c.currentIteration };
    }
    updateTiming(o) {
        if (o !== undefined && o !== null && typeof o !== 'object' && typeof o !== 'function') throw waTypeErr("Failed to execute 'updateTiming' on 'AnimationEffect': The provided value is not of type 'OptionalEffectTiming'.");
        this._t = waTiming(this._t, o);
        if (this._anim) this._anim._updateFinished(false, false);
        waInvalidate();
    }
}
class KeyframeEffect extends AnimationEffect {
    constructor(target, keyframes, options) {
        waInternal = true; try { super(); } finally { waInternal = false; }
        if (arguments.length === 1 && target instanceof KeyframeEffect) {
            const s = target;
            def(this, '_t', Object.assign({}, s._t)); def(this, '_target', s._target); def(this, '_pseudo', s._pseudo);
            def(this, '_comp', s._comp); def(this, '_icomp', s._icomp); def(this, '_kfs', s._kfs.map(k => Object.assign({}, k, { props: new Map(k.props) }))); def(this, '_anim', null);
            return;
        }
        if (target !== null && !(N.isNode(target) && N.type(target) === 1)) throw waTypeErr("Failed to construct 'KeyframeEffect': parameter 1 is not of type 'Element'.");
        def(this, '_target', target); def(this, '_anim', null);
        const kfs = waKeyframes(keyframes);
        let comp = 'replace', icomp = 'replace', pseudo = null;
        if (options !== null && typeof options === 'object') {
            if (options.composite !== undefined) comp = waEnum(options.composite, WA_COMP, 'CompositeOperation');
            if (options.iterationComposite !== undefined) icomp = waEnum(options.iterationComposite, ['replace', 'accumulate'], 'IterationCompositeOperation');
            if (options.pseudoElement !== undefined && options.pseudoElement !== null) pseudo = waPseudo(options.pseudoElement, "construct 'KeyframeEffect'");
        }
        def(this, '_t', waTiming(WA_T0, options)); def(this, '_kfs', kfs); def(this, '_comp', comp); def(this, '_icomp', icomp); def(this, '_pseudo', pseudo);
    }
    get target() { return this._target; }
    set target(v) { if (v !== null && !(N.isNode(v) && N.type(v) === 1)) throw waTypeErr("Failed to set the 'target' property on 'KeyframeEffect': The provided value is not of type 'Element'."); this._target = v; waInvalidate(); }
    get pseudoElement() { return this._pseudo; }
    set pseudoElement(v) { this._pseudo = v === null ? null : waPseudo(v, "set the 'pseudoElement' property on 'KeyframeEffect'"); waInvalidate(); }
    get composite() { return this._comp; } set composite(v) { this._comp = waEnum(v, WA_COMP, 'CompositeOperation'); waInvalidate(); }
    get iterationComposite() { return this._icomp; } set iterationComposite(v) { this._icomp = waEnum(v, ['replace', 'accumulate'], 'IterationCompositeOperation'); waInvalidate(); }
    getKeyframes() {
        return this._kfs.map(k => {
            const o = { offset: k.offset, computedOffset: k.co, easing: k.easing, composite: k.composite };
            for (const [p, v] of [...k.props].sort((x, y) => waIdl(x[0]) < waIdl(y[0]) ? -1 : 1)) o[waIdl(p)] = v;
            return o;
        });
    }
    setKeyframes(kf) { this._kfs = waKeyframes(kf); waInvalidate(); }
}
function waPseudo(v, what) {
    v = String(v);
    if (!/^::?[a-z-]+(\(.*\))?$/i.test(v)) throw new DOMException(`Failed to ${what}: A valid pseudo-selector must be null or start with ::.`, 'SyntaxError');
    if (/^:(before|after|first-line|first-letter)$/i.test(v)) v = ':' + v;
    if (!v.startsWith('::')) throw new DOMException(`Failed to ${what}: A valid pseudo-selector must be null or start with ::.`, 'SyntaxError');
    return v;
}
/* the value of one effect for one property, composed on the underlying value */
function waEffectValue(eff, prop, under, c) {
    const kfs = eff._kfs.filter(k => k.props.has(prop));
    if (!kfs.length) return under;
    const p = c.progress, val = (k) => {
        const v = k ? k.props.get(prop) : under;
        return k ? waCompose(prop, under, v, k.composite === 'auto' ? eff._comp : k.composite) : under;
    };
    let pts = kfs.map(k => ({ co: k.co, k }));
    if (pts[0].co !== 0) pts.unshift({ co: 0, k: null });
    if (pts[pts.length - 1].co !== 1) pts.push({ co: 1, k: null });
    let res;
    const z = pts.filter(x => x.co === 0), o = pts.filter(x => x.co === 1);
    if (p < 0 && z.length > 1) res = val(z[0].k);
    else if (p >= 1 && o.length > 1) res = val(o[o.length - 1].k);
    else {
        let si = -1; for (let i = 0; i < pts.length; i++) if (pts[i].co <= p && pts[i].co < 1) si = i;
        if (si < 0) si = pts.findIndex(x => x.co === 0) >= 0 ? pts.map(x => x.co).lastIndexOf(0) : 0;
        if (si >= pts.length - 1) si = pts.length - 2;
        const s = pts[si], e = pts[si + 1];
        const d = e.co === s.co ? 0 : (p - s.co) / (e.co - s.co), de = s.k ? s.k.ease(d, false) : d;
        res = waInterp(prop, val(s.k), val(e.k), de);
    }
    if (eff._icomp === 'accumulate' && c.currentIteration > 0 && isFinite(c.currentIteration)) {
        const last = pts[pts.length - 1].k, endV = last ? val(last) : under;
        for (let i = 0; i < Math.min(c.currentIteration, 1000); i++) res = waCompose(prop, res, endV, 'accumulate');
    }
    return res;
}
/* ---- timeline and frame loop ---- */
let waFrameTime = null, waInFrame = false, waSeqN = 0, waLoop = false, waDirty = false;
const waAll = new Set(), waApplied = new Map(), waEvents = [];
const waNow = () => { if (waInFrame && waFrameTime !== null) return waFrameTime; if (!waLoop) waKick(); return waFrameTime === null ? performance.now() : Math.max(waFrameTime, performance.now() - 1000 / 60); };
function waInvalidate() { waDirty = true; waKick(); }
function waKick() { if (waLoop) return; waLoop = true; N.raf((t) => { waLoop = false; waTick(t); }); }
function waTick(t) {
    waFrameTime = t; waInFrame = true;
    try {
        for (const a of [...waAll]) a._tick();
        waDirty = true; waFlush(); waRemoveReplaced();
        /* animations that no longer run or fill drop out until they are played or seeked again */
        for (const a of waAll) { const st = a.playState; if (a._replace === 'removed' || st === 'idle' || (st === 'finished' && a._finished.done && !a._task && (!a._effect || waCompute(a._effect).activeTime === null))) waAll.delete(a); }
        const evs = waEvents.splice(0).sort((x, y) => x.t - y.t);
        for (const e of evs) { try { const h = e.target['on' + e.ev.type]; if (typeof h === 'function') h.call(e.target, e.ev); dispatch(e.target, e.ev); } catch (x) { report(x); } }
    } finally { waInFrame = false; }
    if ([...waAll].some(a => a.pending || a.playState === 'running') || waEvents.length) waKick();
}
/* the properties an animation-layer change touches won't start transitions on the next restyle */
function trSuppressCss(el, a, b) {
    let s = trSuppress.get(el);
    for (const css of [a || '', b || '']) for (const m of css.matchAll(/(?:^|; )([-a-z]+):/g)) { if (!s) trSuppress.set(el, s = new Set()); s.add(m[1]); }
}
function waFlush() {
    if (!waDirty) return; waDirty = false;
    const byEl = new Map();
    for (const a of [...waAll].sort((x, y) => x._seq - y._seq)) {
        const e = a._effect;
        if (!e || !(e instanceof KeyframeEffect) || !e._target || e._pseudo || a._replace === 'removed') continue;
        const c = waCompute(e); if (c.progress === null) continue;
        let l = byEl.get(e._target); if (!l) byEl.set(e._target, l = []); l.push([e, c]);
    }
    for (const el of waApplied.keys()) if (!byEl.has(el)) { trSuppressCss(el, waApplied.get(el), ''); N.setAnimStyle(el, ''); waApplied.delete(el); }
    for (const [el, list] of byEl) {
        const props = new Set(); for (const [e] of list) for (const k of e._kfs) for (const p of k.props.keys()) props.add(p);
        const needBase = list.some(([e]) => e._comp !== 'replace' || e._icomp !== 'replace' || e._kfs.length < 2 || e._kfs[0].co !== 0 || e._kfs[e._kfs.length - 1].co !== 1 || e._kfs.some(k => k.composite !== 'auto' && k.composite !== 'replace'));
        if (needBase && waApplied.has(el)) { trSuppressCss(el, waApplied.get(el), ''); N.setAnimStyle(el, ''); waApplied.delete(el); }
        let css = '';
        for (const p of props) {
            let v = needBase ? (N.computed(el, p) || '') : null;
            for (const [e, c] of list) v = waEffectValue(e, p, v, c);
            if (v != null && v !== '') css += `${p}: ${v}; `;
        }
        if (waApplied.get(el) !== css) { trSuppressCss(el, waApplied.get(el), css); N.setAnimStyle(el, css); waApplied.set(el, css); }
    }
}
function waRemoveReplaced() {
    const list = [...waAll].filter(a => a._replaceable()).sort((x, y) => x._seq - y._seq);
    const seen = new Map();
    for (let i = list.length - 1; i >= 0; i--) {
        const a = list[i], el = a._effect._target, ps = seen.get(el) || new Set(), mine = new Set();
        for (const k of a._effect._kfs) for (const p of k.props.keys()) mine.add(p);
        if (a._replace === 'active' && mine.size && [...mine].every(p => ps.has(p))) {
            a._replace = 'removed'; waDirty = true;
            waEvents.push({ target: a, t: 0, ev: new AnimationPlaybackEvent('remove', { currentTime: a.currentTime, timelineTime: a._tlTime() }) });
        }
        for (const p of mine) ps.add(p); seen.set(el, ps);
    }
    if (waDirty) waFlush();
}
class AnimationTimeline {
    constructor() { if (!waInternal) illegal(); }
    get currentTime() { return null; }
    get duration() { return null; }
}
class DocumentTimeline extends AnimationTimeline {
    constructor(o) {
        waInternal = true; try { super(); } finally { waInternal = false; }
        const ot = o && o.originTime !== undefined ? +o.originTime : 0;
        if (!isFinite(ot)) throw waTypeErr("Failed to construct 'DocumentTimeline': The provided double value is non-finite.");
        def(this, '_origin', ot);
    }
    get currentTime() { const t = waNow() - this._origin; return t; }
}
/* ---- Animation ---- */
const waDefer = () => { let res, rej; const p = new Promise((a, b) => { res = a; rej = b; }); p.catch(() => {}); return { p, res, rej, done: false }; };
class AnimationPlaybackEvent extends Event {
    constructor(type, init) {
        super(type, init);
        def(this, '_ct', init && init.currentTime !== undefined && init.currentTime !== null ? +init.currentTime : null);
        def(this, '_tt', init && init.timelineTime !== undefined && init.timelineTime !== null ? +init.timelineTime : null);
    }
    get currentTime() { return this._ct; } get timelineTime() { return this._tt; }
}
class Animation extends EventTarget {
    constructor(effect = null, timeline) {
        super();
        if (effect !== null && !(effect instanceof AnimationEffect)) throw waTypeErr("Failed to construct 'Animation': parameter 1 is not of type 'AnimationEffect'.");
        if (timeline === undefined) timeline = document.timeline;
        else if (timeline !== null && !(timeline instanceof AnimationTimeline)) throw waTypeErr("Failed to construct 'Animation': parameter 2 is not of type 'AnimationTimeline'.");
        Object.defineProperties(this, Object.fromEntries(Object.entries({ _seq: ++waSeqN, _timeline: timeline, _effect: null, _start: null, _hold: null, _rate: 1, _pendRate: null,
            _task: null, _ready: Object.assign(waDefer(), { done: true }), _finished: waDefer(), _prevCT: null, _id: '', _replace: 'active', _finTask: null,
            onfinish: null, oncancel: null, onremove: null }).map(([k, v]) => [k, { value: v, writable: true, configurable: true, enumerable: k[0] !== '_' }])));
        this._ready.res(this);
        if (effect) this.effect = effect;
        waAll.add(this);
    }
    get id() { return this._id; } set id(v) { this._id = String(v); }
    get effect() { return this._effect; }
    set effect(e) {
        if (e === this._effect) return;
        if (e !== null && !(e instanceof AnimationEffect)) throw waTypeErr("Failed to set the 'effect' property on 'Animation': The provided value is not of type 'AnimationEffect'.");
        if (this._effect) this._effect._anim = null;
        if (e && e._anim) e._anim.effect = null;
        this._effect = e; if (e) e._anim = this;
        this._updateFinished(false, false); waInvalidate();
    }
    get timeline() { return this._timeline; }
    set timeline(tl) {
        if (tl === this._timeline) return;
        if (tl !== null && !(tl instanceof AnimationTimeline)) throw waTypeErr("Failed to set the 'timeline' property on 'Animation': The provided value is not of type 'AnimationTimeline'.");
        const prevState = this.playState, prevCT = this.currentTime;
        this._timeline = tl;
        if (this._start !== null) this._hold = null;
        if (prevState === 'finished' || prevState === 'running') { if (prevCT !== null && tl && tl.currentTime !== null) this._start = this._tlTime() - prevCT / this._rate; }
        else if (prevState === 'paused' && prevCT !== null) { this._hold = prevCT; this._start = null; }
        if (this._task) this._kick();
        this._updateFinished(false, false); waInvalidate();
    }
    get replaceState() { return this._replace; }
    get pending() { return this._task !== null; }
    get ready() { return this._ready.p; }
    get finished() { return this._finished.p; }
    get playbackRate() { return this._rate; }
    set playbackRate(r) {
        r = +r; if (!isFinite(r)) throw waTypeErr("Failed to set the 'playbackRate' property on 'Animation': The provided double value is non-finite.");
        this._pendRate = null;
        const prev = this.currentTime; this._rate = r;
        if (prev !== null) this.currentTime = prev;
        waInvalidate();
    }
    _effRate() { return this._pendRate !== null ? this._pendRate : this._rate; }
    _tlTime() { const tl = this._timeline; return tl ? tl.currentTime : null; }
    get startTime() { return this._start; }
    set startTime(v) {
        v = v === null ? null : +v;
        if (v !== null && !isFinite(v)) throw waTypeErr("Failed to set the 'startTime' property on 'Animation': The provided double value is non-finite.");
        const tt = this._tlTime();
        if (tt === null && v !== null) this._hold = null;
        const prevCT = this.currentTime;
        this._applyPendRate();
        this._start = v;
        if (v !== null) { if (this._rate !== 0) this._hold = null; } else this._hold = prevCT;
        if (this._task) { this._task = null; this._resolveReady(); }
        this._updateFinished(true, false); waInvalidate();
    }
    get currentTime() {
        if (this._hold !== null) return this._hold;
        const tt = this._tlTime();
        if (tt === null || this._start === null) return null;
        return (tt - this._start) * this._rate;
    }
    set currentTime(v) {
        if (v === null) { if (this._hold !== null || this._start !== null) throw waTypeErr("Failed to set the 'currentTime' property on 'Animation': currentTime may not be changed from resolved to unresolved."); return; }
        v = +v; if (!isFinite(v)) throw waTypeErr("Failed to set the 'currentTime' property on 'Animation': The provided double value is non-finite.");
        this._silentSetCT(v);
        if (this._task === 'pause') { this._hold = v; this._applyPendRate(); this._start = null; this._task = null; this._resolveReady(); }
        this._updateFinished(true, false); waInvalidate();
    }
    _silentSetCT(v) {
        const tt = this._tlTime();
        if (this._hold !== null || this._start === null || tt === null || this._rate === 0) this._hold = v;
        else this._start = tt - v / this._rate;
        if (!this._timeline || tt === null) this._start = null;
        this._prevCT = null;
    }
    _end() { return this._effect ? waEndTime(this._effect._t) : 0; }
    get playState() {
        const ct = this.currentTime;
        if (ct === null && this._start === null && this._task === null) return 'idle';
        if (this._task === 'pause' || (this._start === null && this._task !== 'play')) return 'paused';
        if (ct !== null && ((this._effRate() > 0 && ct >= this._end()) || (this._effRate() < 0 && ct <= 0))) return 'finished';
        return 'running';
    }
    get overallProgress() {
        const e = this._effect, ct = this.currentTime; if (!e || ct === null) return null;
        const end = this._end(); if (end === 0) return ct < 0 ? 0 : 1;
        if (!isFinite(end)) return null;
        return Math.min(Math.max(ct / end, 0), 1);
    }
    _resolveReady() { if (!this._ready.done) { this._ready.done = true; this._ready.res(this); } }
    _newReady() { if (this._ready.done) this._ready = waDefer(); }
    _applyPendRate() { if (this._pendRate !== null) { this._rate = this._pendRate; this._pendRate = null; } }
    _kick() { waKick(); }
    play() { this._play(true); }
    _play(autoRewind) {
        const abort = this._task === 'pause', rate = this._effRate(), ct = this.currentTime, end = this._end();
        let seek = null, hasPendReady = false;
        if (rate > 0 && autoRewind && (ct === null || ct < 0 || ct >= end)) seek = 0;
        else if (rate < 0 && autoRewind && (ct === null || ct <= 0 || ct > end)) {
            if (end === Infinity) throw new DOMException("Failed to execute 'play' on 'Animation': Cannot play reversed Animation with infinite target effect end.", 'InvalidStateError');
            seek = end;
        } else if (rate === 0 && ct === null) seek = 0;
        if (seek !== null) { this._start = null; this._hold = seek; }
        if (this._hold !== null) this._start = null;
        if (this._task) { this._task = null; hasPendReady = true; }
        if (this._hold === null && seek === null && !abort && this._pendRate === null) return;
        if (!hasPendReady) this._newReady();
        this._task = 'play';
        this._updateFinished(false, false); waInvalidate();
    }
    pause() {
        if (this._task === 'pause') return;
        if (this.playState === 'paused' && this._task === null) return;
        let seek = null;
        if (this.currentTime === null) {
            if (this._rate >= 0) seek = 0;
            else { if (this._end() === Infinity) throw new DOMException("Failed to execute 'pause' on 'Animation': Cannot pause, Animation has infinite target effect end.", 'InvalidStateError'); seek = this._end(); }
        }
        if (seek !== null) { this._hold = seek; this._start = null; }
        if (this._task === 'play') this._task = null; else this._newReady();
        this._task = 'pause';
        this._updateFinished(false, false); waInvalidate();
    }
    _tick() {
        const tt = this._tlTime();
        if (this._task && tt !== null) {
            if (this._task === 'play') {
                if (this._hold !== null) { this._applyPendRate(); this._start = this._rate === 0 ? tt : tt - this._hold / this._rate; if (this._rate !== 0) this._hold = null; }
                else if (this._start !== null && this._pendRate !== null) { const ct = (tt - this._start) * this._rate; this._applyPendRate(); this._start = this._rate === 0 ? tt : tt - ct / this._rate; }
            } else {
                if (this._start !== null && this._hold === null) this._hold = (tt - this._start) * this._rate;
                this._applyPendRate(); this._start = null;
            }
            this._task = null; this._resolveReady();
        }
        this._updateFinished(false, false);
    }
    _updateFinished(didSeek, sync) {
        waAll.add(this);
        const tt = this._tlTime(), end = this._end();
        if (this._start !== null && this._task === null) {
            const ct = this.currentTime;
            if (this._rate > 0 && ct >= end) this._hold = didSeek ? ct : Math.max(this._prevCT ?? -Infinity, end);
            else if (this._rate < 0 && ct <= 0) this._hold = didSeek ? ct : Math.min(this._prevCT ?? Infinity, 0);
            else if (this._rate !== 0 && this._hold !== null && tt !== null) { if (didSeek) this._start = tt - this._hold / this._rate; this._hold = null; }
        }
        this._prevCT = this.currentTime;
        const finished = this.playState === 'finished';
        if (finished && !this._finished.done) {
            const fire = () => {
                if (this.playState !== 'finished' || this._finished.done) return;
                this._finished.done = true; this._finished.res(this);
                waEvents.push({ target: this, t: this._end(), ev: new AnimationPlaybackEvent('finish', { currentTime: this.currentTime, timelineTime: this._tlTime() }) });
                waKick();
            };
            if (sync) fire(); else Promise.resolve().then(fire);
        } else if (!finished && this._finished.done) this._finished = waDefer();
    }
    finish() {
        const rate = this._effRate(), end = this._end();
        if (rate === 0 || (rate > 0 && end === Infinity)) throw new DOMException("Failed to execute 'finish' on 'Animation': Cannot finish Animation with a playbackRate of 0 or an infinite target effect end.", 'InvalidStateError');
        this._applyPendRate();
        const lim = this._rate > 0 ? end : 0;
        this._silentSetCT(lim);
        const tt = this._tlTime();
        if (this._start === null && tt !== null) this._start = tt - lim / this._rate;
        if (this._task === 'pause' && this._start !== null) { this._hold = null; this._task = null; this._resolveReady(); }
        if (this._task === 'play' && this._start !== null) { this._task = null; this._resolveReady(); }
        this._updateFinished(true, true); waInvalidate();
    }
    cancel() {
        if (this.playState !== 'idle') {
            if (this._task) { this._task = null; this._ready.done = true; this._ready.rej(new DOMException('The user aborted a request.', 'AbortError')); this._ready = Object.assign(waDefer(), { done: true }); this._ready.res(this); }
            if (!this._finished.done) { this._finished.done = true; this._finished.rej(new DOMException('The user aborted a request.', 'AbortError')); }
            this._finished = waDefer();
            const tt = this._tlTime();
            waEvents.push({ target: this, t: -Infinity, ev: new AnimationPlaybackEvent('cancel', { currentTime: null, timelineTime: tt }) }); waKick();
        }
        this._start = null; this._hold = null; this._pendRate = null; waInvalidate();
    }
    reverse() {
        const tt = this._tlTime();
        if (tt === null && this._timeline === null) throw new DOMException("Failed to execute 'reverse' on 'Animation': Cannot reverse an animation with no active timeline", 'InvalidStateError');
        const orig = this._pendRate;
        this._pendRate = -this._effRate();
        try { this._play(true); } catch (e) { this._pendRate = orig; throw e; }
        if (this._task === null) { this._applyPendRate(); }
    }
    updatePlaybackRate(r) {
        r = +r; if (!isFinite(r)) throw waTypeErr("Failed to execute 'updatePlaybackRate' on 'Animation': The provided double value is non-finite.");
        const prev = this.playState; this._pendRate = r;
        if (this._task) return;
        if (prev === 'idle' || prev === 'paused' || this._tlTime() === null) this._applyPendRate();
        else if (prev === 'finished') {
            const tt = this._tlTime(), u = this.currentTime;
            this._applyPendRate(); if (r === 0) this._start = tt; else if (u !== null) this._start = tt - u / r;
            this._updateFinished(false, false);
        } else this._play(false);
        waInvalidate();
    }
    persist() { if (this._replace === 'removed') waDirty = true; this._replace = 'persisted'; waInvalidate(); }
    _replaceable() {
        const e = this._effect;
        return this._replace !== 'removed' && this._timeline instanceof DocumentTimeline && e instanceof KeyframeEffect && e._target && !e._pseudo && this.playState === 'finished' && waCompute(e).activeTime !== null;
    }
    commitStyles() {
        const e = this._effect; if (!(e instanceof KeyframeEffect) || !e._target) return;
        const el = e._target;
        if (e._pseudo) throw new DOMException("Failed to execute 'commitStyles' on 'Animation': Animation target is a pseudo-element.", 'NoModificationAllowedError');
        if (!el.style || el.namespaceURI !== 'http://www.w3.org/1999/xhtml' && el.namespaceURI !== 'http://www.w3.org/2000/svg') throw new DOMException("Failed to execute 'commitStyles' on 'Animation': Target element does not have a style attribute.", 'NoModificationAllowedError');
        if (!el.isConnected || N.computed(el, 'display') === 'none') throw new DOMException("Failed to execute 'commitStyles' on 'Animation': Target element is not rendered.", 'InvalidStateError');
        const c = waCompute(e); if (c.progress === null) return;
        const props = new Set(); for (const k of e._kfs) for (const p of k.props.keys()) props.add(p);
        waFlush();
        for (const p of props) el.style.setProperty(p, waEffectValue(e, p, N.computed(el, p) || '', c));
    }
}
const waGetAnims = (pred) => [...waAll].filter(a => {
    const e = a._effect; if (!(e instanceof KeyframeEffect) || !e._target || a._replace === 'removed') return false;
    const st = a.playState; if (!(st === 'running' || st === 'paused' || a._task) && waCompute(e).activeTime === null) return false;
    return pred(e._target);
}).sort((x, y) => x._seq - y._seq);
/* CSS transitions: restyle reports changed computed values (N.trTake, or fireAnim 'lumen-transition'), and each
   becomes a CSSTransition animating from the old to the new value. Values the animation layer itself writes are
   suppressed once so animations don't start transitions. */
const trRunning = new Map(), trSuppress = new Map();
class CSSTransition extends Animation {
    get transitionProperty() { return this._trProp; }
}
const trTime = s => { const m = /^(-?[\d.]+(?:e[+-]?\d+)?)(m?s)$/i.exec(String(s).trim()); return m ? +m[1] * (m[2].toLowerCase() === 'ms' ? 1 : 1000) : 0; };
const trList = (el, p) => tomSplitTop(N.computed(el, p) || '', ',').map(s => s.trim());
function trStart(el, packed) {
    const [prop, from, to] = packed.split('\x01');
    const sup = trSuppress.get(el);
    if (sup && sup.has(prop)) { sup.delete(prop); if (!sup.size) trSuppress.delete(el); return; }
    const names = trList(el, 'transition-property');
    let i = names.lastIndexOf(prop); if (i < 0) i = names.lastIndexOf('all'); if (i < 0) return;
    const at = (p, d) => { const l = trList(el, p); return l.length && l[0] ? l[i % l.length] : d; };
    const duration = Math.max(0, trTime(at('transition-duration', '0s'))), delay = trTime(at('transition-delay', '0s'));
    const running = trRunning.get(el), old = running && running.get(prop);
    if (old) { running.delete(prop); old.cancel(); }
    if (duration + delay <= 0 || duration <= 0) return;
    const discrete = prop !== 'transform' && prop !== 'visibility' && waMix(prop, from, to, 0.5) === null;
    if (discrete && at('transition-behavior', 'normal') !== 'allow-discrete') return;
    const key = prop === 'float' ? 'cssFloat' : prop.replace(/^-webkit-/, 'webkit-').replace(/-([a-z])/g, (m, c) => c.toUpperCase());
    let eff;
    try { eff = new KeyframeEffect(el, [{ [key]: from }, { [key]: to }], { duration, delay, easing: at('transition-timing-function', 'ease'), fill: 'backwards' }); }
    catch (e) { eff = new KeyframeEffect(el, [{ [key]: from }, { [key]: to }], { duration, delay, fill: 'backwards' }); }
    const a = new CSSTransition(eff, document.timeline);
    a._trProp = prop;
    const done = () => { const m = trRunning.get(el); if (m && m.get(prop) === a) { m.delete(prop); if (!m.size) trRunning.delete(el); } };
    a.addEventListener('finish', done); a.addEventListener('cancel', done);
    if (!trRunning.has(el)) trRunning.set(el, new Map());
    trRunning.get(el).set(prop, a);
    a.play();
}
function trPoll() {
    const l = N.trTake();
    for (const [el, packed] of l) trStart(el, packed);
    if (l.length) { waDirty = true; waFlush(); }
    return l.length > 0;
}
