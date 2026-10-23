/* CSS <color> parsing and conversion (CSS Color 4) for script-visible serializations such as <input type=color>.
   cssColor(text) gives { space, c: [c0, c1, c2], alpha, legacy } or null; colorTo(color, 'srgb' | 'display-p3') gives
   the three components in that space, unclamped. 'none' components are 0. currentcolor gives { current: true }. */
const NAMED_COLORS = new Map();
{
    const t = ('aliceblue f0f8ff antiquewhite faebd7 aqua 00ffff aquamarine 7fffd4 azure f0ffff beige f5f5dc bisque ffe4c4 black 000000 blanchedalmond ffebcd blue 0000ff blueviolet 8a2be2 brown a52a2a burlywood deb887 cadetblue 5f9ea0 chartreuse 7fff00 chocolate d2691e coral ff7f50 cornflowerblue 6495ed cornsilk fff8dc crimson dc143c cyan 00ffff darkblue 00008b darkcyan 008b8b darkgoldenrod b8860b darkgray a9a9a9 darkgreen 006400 darkgrey a9a9a9 darkkhaki bdb76b darkmagenta 8b008b darkolivegreen 556b2f darkorange ff8c00 darkorchid 9932cc darkred 8b0000 darksalmon e9967a darkseagreen 8fbc8f darkslateblue 483d8b darkslategray 2f4f4f darkslategrey 2f4f4f darkturquoise 00ced1 darkviolet 9400d3 deeppink ff1493 deepskyblue 00bfff dimgray 696969 dimgrey 696969 dodgerblue 1e90ff firebrick b22222 floralwhite fffaf0 forestgreen 228b22 fuchsia ff00ff gainsboro dcdcdc ghostwhite f8f8ff gold ffd700 goldenrod daa520 gray 808080 green 008000 greenyellow adff2f grey 808080 honeydew f0fff0 hotpink ff69b4 indianred cd5c5c indigo 4b0082 ivory fffff0 khaki f0e68c lavender e6e6fa lavenderblush fff0f5 lawngreen 7cfc00 lemonchiffon fffacd lightblue add8e6 lightcoral f08080 lightcyan e0ffff lightgoldenrodyellow fafad2 lightgray d3d3d3 lightgreen 90ee90 lightgrey d3d3d3 lightpink ffb6c1 lightsalmon ffa07a lightseagreen 20b2aa lightskyblue 87cefa lightslategray 778899 lightslategrey 778899 lightsteelblue b0c4de lightyellow ffffe0 lime 00ff00 limegreen 32cd32 linen faf0e6 magenta ff00ff maroon 800000 mediumaquamarine 66cdaa mediumblue 0000cd mediumorchid ba55d3 mediumpurple 9370db mediumseagreen 3cb371 mediumslateblue 7b68ee mediumspringgreen 00fa9a mediumturquoise 48d1cc mediumvioletred c71585 midnightblue 191970 mintcream f5fffa mistyrose ffe4e1 moccasin ffe4b5 navajowhite ffdead navy 000080 oldlace fdf5e6 olive 808000 olivedrab 6b8e23 orange ffa500 orangered ff4500 orchid da70d6 palegoldenrod eee8aa palegreen 98fb98 paleturquoise afeeee palevioletred db7093 papayawhip ffefd5 peachpuff ffdab9 peru cd853f pink ffc0cb plum dda0dd powderblue b0e0e6 purple 800080 rebeccapurple 663399 red ff0000 rosybrown bc8f8f royalblue 4169e1 saddlebrown 8b4513 salmon fa8072 sandybrown f4a460 seagreen 2e8b57 seashell fff5ee sienna a0522d silver c0c0c0 skyblue 87ceeb slateblue 6a5acd slategray 708090 slategrey 708090 snow fffafa springgreen 00ff7f steelblue 4682b4 tan d2b48c teal 008080 thistle d8bfd8 tomato ff6347 turquoise 40e0d0 violet ee82ee wheat f5deb3 white ffffff whitesmoke f5f5f5 yellow ffff00 yellowgreen 9acd32 canvas ffffff canvastext 000000 linktext 0000ee buttonface efefef buttontext 000000 field ffffff fieldtext 000000 graytext 808080 highlight 3390ff highlighttext ffffff buttonborder 767676 windowtext 000000 window ffffff menu ffffff ' + 'accentcolor 0075ff accentcolortext ffffff activetext ff0000 mark ffff00 marktext 000000 selecteditem 0075ff selecteditemtext ffffff visitedtext 551a8b activeborder 767676 activecaption ffffff appworkspace ffffff background ffffff buttonhighlight efefef buttonshadow efefef captiontext 000000 inactiveborder 767676 inactivecaption ffffff inactivecaptiontext 808080 infobackground ffffff infotext 000000 menutext 000000 scrollbar ffffff threeddarkshadow 767676 threedface efefef threedhighlight 767676 threedlightshadow 767676 threedshadow 767676 windowframe 767676').split(' ');
    for (let i = 0; i < t.length; i += 2) NAMED_COLORS.set(t[i], parseInt(t[i + 1], 16));
}
const COLOR_NUM = /^([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)(%|deg|grad|rad|turn)?/i;
/* the components of a color function: numbers { v, u } (u = '%', an angle unit or ''), 'none', ',' and '/' */
function colorArgs(s) {
    const r = [];
    for (let i = 0; i < s.length;) {
        const ch = s[i];
        if (' \t\n\r\f'.includes(ch)) { i++; continue; }
        if (ch === ',' || ch === '/') { r.push(ch); i++; continue; }
        if (/^none/i.test(s.slice(i, i + 4)) && !/[\w-]/.test(s[i + 4] || '')) { r.push('none'); i += 4; continue; }
        const m = COLOR_NUM.exec(s.slice(i));
        if (!m || /[\w-]/.test(s[i + m[0].length] || '')) return null;
        r.push({ v: parseFloat(m[1]), u: (m[2] || '').toLowerCase() });
        i += m[0].length;
    }
    return r;
}
const colorHue = a => {
    if (a === 'none') return 0;
    const k = { '': 1, deg: 1, grad: 0.9, rad: 180 / Math.PI, turn: 360 }[a.u];
    return k === undefined ? NaN : a.v * k;
};
/* a component that may be a number or a percentage; pct gives the value of 100% */
const colorComp = (a, pct) => a === 'none' ? 0 : a.u === '%' ? a.v / 100 * pct : a.u === '' ? a.v : NaN;
const colorAlpha = a => a === undefined ? 1 : Math.min(1, Math.max(0, colorComp(a, 1)));
const q8 = v => Math.round(Math.min(1, Math.max(0, v)) * 255) / 255;
function hslToRgb(h, s, l) {
    h = ((h % 360) + 360) % 360;
    const f = n => { const k = (n + h / 30) % 12, a = s * Math.min(l, 1 - l); return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1)); };
    return [f(0), f(8), f(4)];
}
function cssColor(text) {
    const s = String(text).replace(/^[ \t\n\r\f]+|[ \t\n\r\f]+$/g, '');
    let m = /^#([0-9a-f]+)$/i.exec(s);
    if (m) {
        const h = m[1], n = h.length;
        if (n !== 3 && n !== 4 && n !== 6 && n !== 8) return null;
        const d = n < 5 ? [...h].map(x => parseInt(x + x, 16)) : h.match(/../g).map(x => parseInt(x, 16));
        return { space: 'srgb', c: d.slice(0, 3).map(x => x / 255), alpha: d.length > 3 ? d[3] / 255 : 1, legacy: true };
    }
    const low = s.replace(/[A-Z]/g, c => c.toLowerCase());
    if (/^[a-z]+$/.test(low)) {
        if (low === 'transparent') return { space: 'srgb', c: [0, 0, 0], alpha: 0, legacy: true };
        if (low === 'currentcolor') return { current: true };
        const v = NAMED_COLORS.get(low);
        return v === undefined ? null : { space: 'srgb', c: [v >> 16 & 255, v >> 8 & 255, v & 255].map(x => x / 255), alpha: 1, legacy: true };
    }
    m = /^([a-z-]+)\(([^()]*)\)$/.exec(low);
    if (!m) return null;
    const fn = m[1];
    let space = null, body = m[2];
    if (fn === 'color') {   /* color(<space> c0 c1 c2 [/ alpha]) */
        const sm = /^[ \t\n\r\f]*([a-z0-9-]+)(?![\w-])/.exec(body);
        space = sm && sm[1];
        if (!['srgb', 'srgb-linear', 'display-p3', 'a98-rgb', 'prophoto-rgb', 'rec2020', 'xyz', 'xyz-d50', 'xyz-d65'].includes(space)) return null;
        body = body.slice(sm[0].length);
    }
    const args = colorArgs(body);
    if (!args) return null;
    let comps, alphaArg;
    if (args.includes(',')) {   /* legacy comma syntax: rgb(), rgba(), hsl() and hsla() only, no 'none' */
        if (!/^(rgba?|hsla?)$/.test(fn) || args.includes('none') || args.includes('/')) return null;
        if (!(args.length === 5 || args.length === 7) || args.some((a, i) => (i % 2 === 1) !== (a === ','))) return null;
        comps = args.filter(a => a !== ','); alphaArg = comps[3]; comps = comps.slice(0, 3);
        if (fn[0] === 'r' && !(comps.slice(0, 3).every(a => a.u === '%') || comps.slice(0, 3).every(a => a.u === ''))) return null;
        if (fn[0] === 'h' && (comps[1].u !== '%' || comps[2].u !== '%')) return null;
    } else {
        const sl = args.indexOf('/');
        comps = sl < 0 ? args : args.slice(0, sl);
        if (sl >= 0) { if (args.length !== sl + 2) return null; alphaArg = args[sl + 1]; }
        if (comps.length !== 3) return null;
    }
    if (alphaArg !== undefined && alphaArg !== 'none' && alphaArg.u !== '' && alphaArg.u !== '%') return null;
    const alpha = colorAlpha(alphaArg);
    let r;
    switch (fn) {
    case 'rgb': case 'rgba': {
        const c = comps.map(a => colorComp(a, 255));
        if (c.some(Number.isNaN)) return null;
        r = { space: 'srgb', c: c.map(x => Math.round(Math.min(255, Math.max(0, x))) / 255), alpha: q8(alpha), legacy: true };
        break;
    }
    case 'hsl': case 'hsla': case 'hwb': {
        const h = colorHue(comps[0]), x = colorComp(comps[1], 100) / 100, y = colorComp(comps[2], 100) / 100;
        if ([h, x, y].some(Number.isNaN) || (fn === 'hwb' && comps.slice(1).some(a => a !== 'none' && a.u !== '%' && a.u !== ''))) return null;
        let c;
        if (fn === 'hwb') {
            if (x + y >= 1) { const g = x / (x + y); c = [g, g, g]; }
            else c = hslToRgb(h, 1, 0.5).map(v => v * (1 - x - y) + x);
        } else c = hslToRgb(h, Math.max(0, x), y);
        r = { space: 'srgb', c: c.map(q8), alpha: q8(alpha), legacy: true };
        break;
    }
    case 'lab': case 'oklab': case 'lch': case 'oklch': {
        const ok = fn[0] === 'o', polar = fn.endsWith('ch');
        const L = colorComp(comps[0], ok ? 1 : 100), C = colorComp(comps[1], polar ? (ok ? 0.4 : 150) : (ok ? 0.4 : 125));
        const third = polar ? colorHue(comps[2]) : colorComp(comps[2], ok ? 0.4 : 125);
        if ([L, C, third].some(Number.isNaN)) return null;
        const l = Math.min(Math.max(L, 0), ok ? 1 : 100);
        let ab;
        if (polar) { const cc = Math.max(0, C), hr = third * Math.PI / 180; ab = [cc * Math.cos(hr), cc * Math.sin(hr)]; }
        else ab = [C, third];
        r = { space: ok ? 'oklab' : 'lab', c: [l, ab[0], ab[1]], alpha, legacy: false };
        break;
    }
    case 'color': {
        const c = comps.map(a => colorComp(a, 1));
        if (c.some(Number.isNaN)) return null;
        r = { space, c, alpha, legacy: false };
        break;
    }
    default: return null;
    }
    return r;
}
const mat3 = (m, v) => [0, 1, 2].map(i => m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2]);
const signPow = (v, p) => Math.sign(v) * Math.pow(Math.abs(v), p);
const srgbLin = v => { const a = Math.abs(v); return a <= 0.04045 ? v / 12.92 : Math.sign(v) * Math.pow((a + 0.055) / 1.055, 2.4); };
const srgbGam = v => { const a = Math.abs(v); return a > 0.0031308 ? Math.sign(v) * (1.055 * Math.pow(a, 1 / 2.4) - 0.055) : 12.92 * v; };
const XYZ_FROM = {
    'srgb-linear': [[0.41239079926595934, 0.357584339383878, 0.1804807884018343], [0.21263900587151027, 0.715168678767756, 0.07219231536073371], [0.01933081871559182, 0.11919477979462598, 0.9505321522496607]],
    'display-p3': [[0.4865709486482162, 0.26566769316909306, 0.1982172852343625], [0.2289745640697488, 0.6917385218365064, 0.079286914093745], [0, 0.04511338185890264, 1.043944368900976]],
    'a98-rgb': [[0.5766690429101305, 0.1855582379065463, 0.1882286462349947], [0.29734497525053605, 0.6273635662554661, 0.0752914584939979], [0.02703136138641234, 0.07068885253582723, 0.9913375368376388]],
    'rec2020': [[0.6369580483012914, 0.14461690358620832, 0.1688809751641721], [0.2627002120112671, 0.6779980715188708, 0.05930171646986196], [0, 0.028072693049087428, 1.060985057710791]],
    'prophoto-rgb': [[0.7977666449006423, 0.13518129740053308, 0.0313477341283922], [0.2880748288194013, 0.711835234241873, 0.00008993693872564], [0, 0, 0.8251046025104602]],
};
const XYZ_TO = {
    'srgb-linear': [[3.2409699419045226, -1.537383177570094, -0.4986107602930034], [-0.9692436362808796, 1.8759675015077202, 0.04155505740717559], [0.05563007969699366, -0.20397695888897652, 1.0569715142428786]],
    'display-p3': [[2.493496911941425, -0.9313836179191239, -0.40271078445071684], [-0.8294889695615747, 1.7626640603183463, 0.023624685841943577], [0.03584583024378447, -0.07617238926804182, 0.9568845240076872]],
};
const D50_TO_D65 = [[0.955473421488075, -0.02309845494876471, 0.06325924320057072], [-0.0283697093338637, 1.0099953980813041, 0.021041441191917323], [0.012314014864481998, -0.020507649298898964, 1.330365926242124]];
/* any parsed color to CIE XYZ (D65) */
function colorXyz(col) {
    const c = col.c;
    switch (col.space) {
    case 'srgb': return mat3(XYZ_FROM['srgb-linear'], c.map(srgbLin));
    case 'srgb-linear': return mat3(XYZ_FROM['srgb-linear'], c);
    case 'display-p3': return mat3(XYZ_FROM['display-p3'], c.map(srgbLin));
    case 'a98-rgb': return mat3(XYZ_FROM['a98-rgb'], c.map(v => signPow(v, 563 / 256)));
    case 'rec2020': {
        const A = 1.09929682680944, B = 0.018053968510807;
        return mat3(XYZ_FROM.rec2020, c.map(v => Math.abs(v) < B * 4.5 ? v / 4.5 : signPow((Math.abs(v) + A - 1) / A, 1 / 0.45)));
    }
    case 'prophoto-rgb': return mat3(D50_TO_D65, mat3(XYZ_FROM['prophoto-rgb'], c.map(v => Math.abs(v) <= 16 / 512 ? v / 16 : signPow(v, 1.8))));
    case 'xyz': case 'xyz-d65': return c.slice();
    case 'xyz-d50': return mat3(D50_TO_D65, c);
    case 'lab': {
        const k = 24389 / 27, e = 216 / 24389, [L, a, b] = c;
        const f1 = (L + 16) / 116, f0 = a / 500 + f1, f2 = f1 - b / 200;
        const xyz = [f0 ** 3 > e ? f0 ** 3 : (116 * f0 - 16) / k, L > k * e ? f1 ** 3 : L / k, f2 ** 3 > e ? f2 ** 3 : (116 * f2 - 16) / k];
        return mat3(D50_TO_D65, [xyz[0] * 0.3457 / 0.3585, xyz[1], xyz[2] * (1 - 0.3457 - 0.3585) / 0.3585]);
    }
    case 'oklab': {
        const lms = mat3([[1, 0.3963377773761749, 0.2158037573099136], [1, -0.1055613458156586, -0.0638541728258133], [1, -0.0894841775298119, -1.2914855480194092]], c).map(v => v ** 3);
        return mat3([[1.2268798758459243, -0.5578149944602171, 0.2813910456659647], [-0.0405757452148008, 1.1122868032803170, -0.0717110580655164], [-0.0763729366746601, -0.4214933324022432, 1.5869240198367816]], lms);
    }
    }
    return [0, 0, 0];
}
function colorTo(col, space) {
    if (col.space === space) return col.c.slice();
    const lin = mat3(XYZ_TO[space === 'srgb' ? 'srgb-linear' : space], colorXyz(col));
    return space === 'srgb-linear' ? lin : lin.map(srgbGam);
}
/* a <number> as CSS serializes it: at most six decimals, no trailing zeros */
const colorNum = v => { const r = +v.toFixed(6); return String(Object.is(r, -0) ? 0 : r); };
