/* Real CanvasRenderingContext2D: paths are flattened here, rasterized natively into the canvas backing store */
const CSS_NAMED = { black: '000000', white: 'ffffff', red: 'ff0000', green: '008000', blue: '0000ff', yellow: 'ffff00', cyan: '00ffff', aqua: '00ffff', magenta: 'ff00ff', fuchsia: 'ff00ff', gray: '808080', grey: '808080', silver: 'c0c0c0', maroon: '800000', olive: '808000', lime: '00ff00', navy: '000080', purple: '800080', teal: '008080', orange: 'ffa500', pink: 'ffc0cb', brown: 'a52a2a', gold: 'ffd700', indigo: '4b0082', violet: 'ee82ee', coral: 'ff7f50', salmon: 'fa8072', tomato: 'ff6347', crimson: 'dc143c', darkgray: 'a9a9a9', lightgray: 'd3d3d3', darkblue: '00008b', darkgreen: '006400', darkred: '8b0000', skyblue: '87ceeb', steelblue: '4682b4', royalblue: '4169e1', tan: 'd2b48c', beige: 'f5f5dc', khaki: 'f0e68c', orchid: 'da70d6', plum: 'dda0dd', turquoise: '40e0d0', chocolate: 'd2691e', firebrick: 'b22222', slategray: '708090', whitesmoke: 'f5f5f5', lightblue: 'add8e6', lightgreen: '90ee90' };
const colorCache = new Map();
function parseColor(s) {
    if (typeof s !== 'string') return [0, 0, 0, 255];
    let c = colorCache.get(s); if (c) return c;
    let t = s.trim().toLowerCase(), m;
    if (t === 'transparent') c = [0, 0, 0, 0];
    else { if (CSS_NAMED[t]) t = '#' + CSS_NAMED[t];
        if ((m = /^#([0-9a-f]{3,8})$/.exec(t))) { let h = m[1]; if (h.length <= 4) h = [...h].map(x => x + x).join(''); const n = parseInt(h, 16); c = h.length === 8 ? [n >>> 24, (n >>> 16) & 255, (n >>> 8) & 255, n & 255] : [n >>> 16, (n >>> 8) & 255, n & 255, 255]; }
        else if ((m = /^rgba?\(([^)]*)\)$/.exec(t))) { const p = m[1].split(/[\s,/]+/).filter(Boolean); const v = (x, i) => x.endsWith('%') ? parseFloat(x) * (i < 3 ? 2.55 : 2.55) : i < 3 ? parseFloat(x) : parseFloat(x) * 255; c = [v(p[0], 0), v(p[1], 1), v(p[2], 2), p[3] != null ? v(p[3], 3) : 255].map(x => Math.max(0, Math.min(255, Math.round(x)))); }
        else if ((m = /^hsla?\(([^)]*)\)$/.exec(t))) { const p = m[1].split(/[\s,/]+/).filter(Boolean); const h = parseFloat(p[0]) / 360, sa = parseFloat(p[1]) / 100, l = parseFloat(p[2]) / 100; const q = l < 0.5 ? l * (1 + sa) : l + sa - l * sa, pp = 2 * l - q; const f = x => { x = (x % 1 + 1) % 1; return x < 1 / 6 ? pp + (q - pp) * 6 * x : x < 0.5 ? q : x < 2 / 3 ? pp + (q - pp) * (2 / 3 - x) * 6 : pp; }; const al = p[3] != null ? (p[3].endsWith('%') ? parseFloat(p[3]) / 100 : parseFloat(p[3])) : 1; c = [f(h + 1 / 3), f(h), f(h - 1 / 3), al].map(x => Math.round(x * 255)); }
        else c = null; }
    if (c && colorCache.size < 512) colorCache.set(s, c);
    return c;
}
const C2D = CanvasRenderingContext2D.prototype;
const cvm = (ctx) => ctx._m || (ctx._m = [1, 0, 0, 1, 0, 0]);
function cvT(ctx, x, y) { const m = cvm(ctx); return [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]]; }
function cvMul(ctx, a, b, c, d, e, f) { const m = cvm(ctx); ctx._m = [m[0] * a + m[2] * b, m[1] * a + m[3] * b, m[0] * c + m[2] * d, m[1] * c + m[3] * d, m[0] * e + m[2] * f + m[4], m[1] * e + m[3] * f + m[5]]; }
function cvSync(ctx) { const el = ctx.canvas; if (N.cvW(el) < 0) N.cvSize(el, el.width, el.height); return el; }
function cvPaint(ctx, style) { const c = typeof style === 'string' ? parseColor(style) : style instanceof CanvasGradient && style._stops && style._stops.length ? parseColor(style._stops[style._stops.length >> 1][1]) : [0, 0, 0, 255]; if (!c) return null; return [c[0], c[1], c[2], Math.round(c[3] * Math.max(0, Math.min(1, ctx.globalAlpha)))]; }
const cvMode = ctx => ctx.globalCompositeOperation === 'copy' ? 1 : 0;
Object.assign(C2D, {
    save() { (this._stack || (this._stack = [])).push({ fillStyle: this.fillStyle, strokeStyle: this.strokeStyle, lineWidth: this.lineWidth, font: this.font, globalAlpha: this.globalAlpha, globalCompositeOperation: this.globalCompositeOperation, textAlign: this.textAlign, textBaseline: this.textBaseline, lineCap: this.lineCap, lineJoin: this.lineJoin, m: cvm(this).slice() }); },
    restore() { const s = (this._stack || []).pop(); if (s) { const { m, ...r } = s; Object.assign(this, r); this._m = m; } },
    scale(x, y) { cvMul(this, x, 0, 0, y, 0, 0); }, translate(x, y) { cvMul(this, 1, 0, 0, 1, x, y); },
    rotate(a) { const c = Math.cos(a), s = Math.sin(a); cvMul(this, c, s, -s, c, 0, 0); },
    transform(a, b, c, d, e, f) { cvMul(this, a, b, c, d, e, f); },
    setTransform(a, b, c, d, e, f) { if (a && typeof a === 'object') ({ a, b, c, d, e, f } = a); this._m = a === undefined ? [1, 0, 0, 1, 0, 0] : [a, b, c, d, e, f]; },
    resetTransform() { this._m = [1, 0, 0, 1, 0, 0]; },
    getTransform() { const m = cvm(this); return new DOMMatrix([m[0], m[1], m[2], m[3], m[4], m[5]]); },
    reset() { this._m = null; this._path = []; this._stack = []; const el = this.canvas; N.cvSize(el, el.width, el.height); },
    beginPath() { this._path = []; this._sub = null; },
    moveTo(x, y) { this._sub = [cvT(this, x, y)]; (this._path || (this._path = [])).push(this._sub); },
    lineTo(x, y) { if (!this._sub) return this.moveTo(x, y); this._sub.push(cvT(this, x, y)); },
    closePath() { if (this._sub && this._sub.length) { this._sub.closed = true; const p = this._sub[0]; this._sub = [p]; this._path.push(this._sub); } },
    _last() { const s = this._sub; if (!s || !s.length) return null; const p = s[s.length - 1]; const m = cvm(this), det = m[0] * m[3] - m[1] * m[2] || 1, x = p[0] - m[4], y = p[1] - m[5]; return [(m[3] * x - m[2] * y) / det, (m[0] * y - m[1] * x) / det]; },
    quadraticCurveTo(cx, cy, x, y) { const p = this._last() || [cx, cy]; for (let i = 1; i <= 16; i++) { const t = i / 16, u = 1 - t; this.lineTo(u * u * p[0] + 2 * u * t * cx + t * t * x, u * u * p[1] + 2 * u * t * cy + t * t * y); } },
    bezierCurveTo(a, b, c, d, x, y) { const p = this._last() || [a, b]; for (let i = 1; i <= 20; i++) { const t = i / 20, u = 1 - t; this.lineTo(u * u * u * p[0] + 3 * u * u * t * a + 3 * u * t * t * c + t * t * t * x, u * u * u * p[1] + 3 * u * u * t * b + 3 * u * t * t * d + t * t * t * y); } },
    arc(x, y, r, a0, a1, ccw) { let d = a1 - a0; const T = Math.PI * 2; if (!ccw && d < 0) d = d <= -T ? T : d % T + T; else if (ccw && d > 0) d = d >= T ? -T : d % T - T; if (!ccw && d > T) d = T; if (ccw && d < -T) d = -T; const n = Math.max(8, Math.min(256, Math.ceil(Math.abs(d) * Math.max(r, 1) * Math.hypot(cvm(this)[0], cvm(this)[1]) / 3))); for (let i = 0; i <= n; i++) { const t = a0 + d * i / n; this.lineTo(x + r * Math.cos(t), y + r * Math.sin(t)); } },
    ellipse(x, y, rx, ry, rot, a0, a1, ccw) { this.save(); this.translate(x, y); this.rotate(rot); this.scale(rx, ry || 1e-9); const s = this._stack[this._stack.length - 1]; this.arc(0, 0, 1, a0, a1, ccw); const m = this._m; this.restore(); void s; void m; },
    arcTo(x1, y1, x2, y2, r) { this.lineTo(x1, y1); this.lineTo(x2, y2); },
    rect(x, y, w, h) { this.moveTo(x, y); this.lineTo(x + w, y); this.lineTo(x + w, y + h); this.lineTo(x, y + h); this.closePath(); },
    roundRect(x, y, w, h, r = 0) { const rr = Math.min(Math.abs(Array.isArray(r) ? (typeof r[0] === 'object' ? r[0].x : r[0]) || 0 : r), Math.abs(w) / 2, Math.abs(h) / 2); if (!rr) return this.rect(x, y, w, h); this.moveTo(x + rr, y); this.arc(x + w - rr, y + rr, rr, -Math.PI / 2, 0); this.arc(x + w - rr, y + h - rr, rr, 0, Math.PI / 2); this.arc(x + rr, y + h - rr, rr, Math.PI / 2, Math.PI); this.arc(x + rr, y + rr, rr, Math.PI, Math.PI * 1.5); this.closePath(); },
    _fillPolys(polys, style, eo) {
        const c = cvPaint(this, style); if (!c || !c[3]) return; const el = cvSync(this);
        let n = 0; for (const p of polys) n += p.length; const pts = new Float64Array(n * 2), cnt = new Int32Array(polys.length); let o = 0;
        polys.forEach((p, i) => { cnt[i] = p.length; for (const q of p) { pts[o++] = q[0]; pts[o++] = q[1]; } });
        N.cvPath(el, pts, cnt, c[0], c[1], c[2], c[3], !!eo, cvMode(this));
    },
    fill(a, b) { const path = a instanceof Path2D ? a._ops(this) : this._path || []; const rule = typeof a === 'string' ? a : b; this._fillPolys(path.filter(p => p.length > 2), this.fillStyle, rule === 'evenodd'); },
    stroke(pa) {
        const path = pa instanceof Path2D ? pa._ops(this) : this._path || [], m = cvm(this), hw = Math.max(this.lineWidth, 0) * Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2])) / 2 || 0.5, quads = [];
        for (const s of path) { const pts = s.closed && s.length > 1 ? [...s, s[0]] : s;
            for (let i = 0; i + 1 < pts.length; i++) { const [x0, y0] = pts[i], [x1, y1] = pts[i + 1], dx = x1 - x0, dy = y1 - y0, l = Math.hypot(dx, dy); if (!l) continue; const nx = -dy / l * hw, ny = dx / l * hw, ex = this.lineCap === 'butt' && !(s.closed || (i > 0 && i + 2 < pts.length)) ? 0 : dx / l * hw * (i + 2 < pts.length || s.closed || this.lineCap !== 'butt' ? 0.5 : 0), ey = ex ? dy / l * hw * 0.5 : 0;
                quads.push([[x0 + nx - ex, y0 + ny - ey], [x1 + nx + ex, y1 + ny + ey], [x1 - nx + ex, y1 - ny + ey], [x0 - nx - ex, y0 - ny - ey]]); } }
        if (quads.length) this._fillPolys(quads, this.strokeStyle, false);
    },
    fillRect(x, y, w, h) { const m = cvm(this); if (!m[1] && !m[2]) { const c = cvPaint(this, this.fillStyle); if (!c) return; const el = cvSync(this); N.cvRect(el, m[0] * x + m[4], m[3] * y + m[5], m[0] * w, m[3] * h, c[0], c[1], c[2], c[3], cvMode(this)); } else this._fillPolys([[cvT(this, x, y), cvT(this, x + w, y), cvT(this, x + w, y + h), cvT(this, x, y + h)]], this.fillStyle); },
    clearRect(x, y, w, h) { const m = cvm(this), el = cvSync(this); if (!m[1] && !m[2]) N.cvRect(el, m[0] * x + m[4], m[3] * y + m[5], m[0] * w, m[3] * h, 0, 0, 0, 0, 1); else N.cvPath(el, new Float64Array([...cvT(this, x, y), ...cvT(this, x + w, y), ...cvT(this, x + w, y + h), ...cvT(this, x, y + h)]), new Int32Array([4]), 0, 0, 0, 0, false, 1); },
    strokeRect(x, y, w, h) { const p = this._path, s = this._sub; this.beginPath(); this.rect(x, y, w, h); this.stroke(); this._path = p; this._sub = s; },
    putImageData(d, dx, dy) { const el = cvSync(this); N.cvPut(el, d.data, d.width, d.height, dx | 0, dy | 0); },
    getImageData(x, y, w, h) { const el = cvSync(this); w = Math.abs(w) | 0; h = Math.abs(h) | 0; const d = new ImageData(w, h); d.data.set(new Uint8ClampedArray(N.cvGet(el, x | 0, y | 0, w, h))); return d; },
    drawImage(img, ...a) {
        const el = cvSync(this), src = img && (img._cv || img); if (!src || !N.isNode(src)) return;
        const iw = img.naturalWidth || img.videoWidth || img.width || 0, ih = img.naturalHeight || img.videoHeight || img.height || 0;
        let [sx, sy, sw, sh, dx, dy, dw, dh] = a.length >= 8 ? a : a.length >= 4 ? [0, 0, iw, ih, a[0], a[1], a[2], a[3]] : [0, 0, iw, ih, a[0], a[1], iw, ih];
        const m = cvm(this); N.cvDraw(el, src, sx, sy, sw, sh, m[0] * dx + m[4], m[3] * dy + m[5], m[0] * dw, m[3] * dh, Math.max(0, Math.min(1, this.globalAlpha)));
    },
    clip() {}, isPointInPath() { return false; }, fillText() {}, strokeText() {},
});
for (const k of ['moveTo', 'lineTo', 'closePath', 'quadraticCurveTo', 'bezierCurveTo', 'arc', 'ellipse', 'arcTo', 'rect', 'roundRect'])
    Path2D.prototype[k] = function (...a) { (this._o || (this._o = [])).push([k, a]); };
Path2D.prototype.addPath = function (p) { if (p && p._o) (this._o || (this._o = [])).push(...p._o); };
Path2D.prototype._ops = function (ctx) { const sp = ctx._path, ss = ctx._sub; ctx.beginPath(); for (const [k, a] of this._o || []) ctx[k](...a); const r = ctx._path || []; ctx._path = sp; ctx._sub = ss; return r; };
CanvasGradient.prototype.addColorStop = function (o, c) { (this._stops || (this._stops = [])).push([o, c]); };
{   /* resizing a canvas clears it */
    const P = H.HTMLCanvasElement.prototype;
    for (const k of ['width', 'height']) { const d = Object.getOwnPropertyDescriptor(P, k); if (!d || !d.set) continue; Object.defineProperty(P, k, { configurable: true, enumerable: true, get: d.get, set(v) { d.set.call(this, v); if (N.cvW(this) >= 0) N.cvSize(this, this.width, this.height); if (this._ctx2d) this._ctx2d._m = null; } }); }
    P.toDataURL = function () { return 'data:,'; };
}
