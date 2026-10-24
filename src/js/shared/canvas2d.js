// ---- canvas 2D: state, paths and strokes here; coverage, paints and compositing natively (paint/canvas2d.c) ----
// shared by the page prelude and the worker prelude (OffscreenCanvas); page-only globals are looked up lazily
const C2N = typeof N !== 'undefined' ? N : W;
const C2H = k => typeof H !== 'undefined' ? H[k] : globalThis[k];
const c2dIs = (x, k) => { const C = k === 'SVGImageElement' ? (typeof SVGImageElement !== 'undefined' ? SVGImageElement : undefined) : C2H(k); return typeof C === 'function' && x instanceof C; };
const C2Node = typeof Node !== 'undefined' ? Node : class {};
const C2IB = typeof ImageBitmap !== 'undefined' ? ImageBitmap : class ImageBitmap {
    constructor() { c2dIllegal(); }
    get width() { return this._.w; }
    get height() { return this._.h; }
    close() { this._.src = null; this._.w = this._.h = 0; }
};
const C2D_KEY = Symbol('c2d');
const C2D_OPS = ['source-over', 'source-in', 'source-out', 'source-atop', 'destination-over', 'destination-in', 'destination-out', 'destination-atop',
    'lighter', 'copy', 'xor', 'multiply', 'screen', 'overlay', 'darken', 'lighten', 'color-dodge', 'color-burn', 'hard-light', 'soft-light',
    'difference', 'exclusion', 'hue', 'saturation', 'color', 'luminosity'];
const c2dIllegal = () => { throw new TypeError('Illegal constructor'); };
const c2dIndex = m => new DOMException(m, 'IndexSizeError');
/* WebIDL [EnforceRange] long / unsigned long */
const erLong = (v, lo = -2147483648, hi = 2147483647) => {
    const x = Number(v);
    if (!Number.isFinite(x)) throw new TypeError('Value is not a finite number.');
    const t = Math.trunc(x);
    if (t < lo || t > hi) throw new TypeError('Value is outside the range.');
    return t;
};
const erULong = v => erLong(v, 0, 4294967295);
const fin = (...a) => a.every(Number.isFinite);
const toDouble = (v, what) => { const x = Number(v); if (!Number.isFinite(x)) throw new TypeError(`The provided ${what || 'value'} is non-finite.`); return x; };

/* ImageData state lives in a WeakMap (not private fields) so a structured-clone revival can fill it in */
const idState = new WeakMap();
const idSet = (o, w, h, d, cs) => { idState.set(o, { w, h, d, cs }); if (cs && FILEN.cloneRec) FILEN.cloneRec(o, [d, w, h, cs]); };
const idGet = o => { const r = idState.get(o); if (!r) throw new TypeError('Illegal invocation'); return r; };
class ImageData {
    constructor(a, b, c, d) {
        if (arguments.length < 2) throw new TypeError(`Failed to construct 'ImageData': 2 arguments required, but only ${arguments.length} present.`);
        let settings;
        if (a instanceof Uint8ClampedArray) {
            const n = a.length, sw = erULong(b);
            if (!n || n % 4) throw new DOMException('The input data has a zero byte length or is not a multiple of 4.', 'InvalidStateError');
            if (!sw) throw c2dIndex('The source width is zero.');
            if (n % (4 * sw)) throw c2dIndex('The input data length is not a multiple of (4 * width).');
            const h = n / (4 * sw);
            if (c !== undefined && erULong(c) !== h) throw c2dIndex('The input data length is not equal to (4 * width * height).');
            idSet(this, sw, h, a); settings = d;
        } else {
            const sw = erULong(a), sh = erULong(b);
            if (!sw || !sh) throw c2dIndex(`The source ${sw ? 'height' : 'width'} is zero.`);
            if (sw * sh * 4 > 2 ** 31) throw new RangeError('Out of memory at ImageData creation');
            idSet(this, sw, sh, new Uint8ClampedArray(sw * sh * 4)); settings = c;
        }
        const cs = settings != null && settings.colorSpace !== undefined ? String(settings.colorSpace) : 'srgb';
        if (!C2D_SPACES.includes(cs)) throw new TypeError(`The provided value '${cs}' is not a valid enum value of type PredefinedColorSpace.`);
        const st = idGet(this); idSet(this, st.w, st.h, st.d, cs);
    }
    get width() { return idGet(this).w; }
    get height() { return idGet(this).h; }
    get data() { return idGet(this).d; }
    get colorSpace() { return idGet(this).cs; }
    get pixelFormat() { return 'rgba-unorm8'; }
}
if (FILEN.cloneHost) FILEN.cloneHost(ImageData.prototype, (o, r) => idSet(o, r[1], r[2], r[0], r[3]), 4);

/* ---- paths: subpaths of [x, y, ...] points; curves are flattened as they are added, in the coordinate
   space of the matrix m (device space for the context's path, user space for Path2D) ---- */
const mApply = (m, x, y) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]];
const mMul = (m, n) => [m[0] * n[0] + m[2] * n[1], m[1] * n[0] + m[3] * n[1], m[0] * n[2] + m[2] * n[3], m[1] * n[2] + m[3] * n[3],
    m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5]];
const mInv = m => {
    const d = m[0] * m[3] - m[1] * m[2];
    if (!d || !Number.isFinite(d)) return null;
    return [m[3] / d, -m[1] / d, -m[2] / d, m[0] / d, (m[2] * m[5] - m[3] * m[4]) / d, (m[1] * m[4] - m[0] * m[5]) / d];
};
const mScale = m => Math.sqrt(Math.max(Math.abs(m[0] * m[3] - m[1] * m[2]), 1e-12)) || 1;
class PathData {
    constructor() { this.sub = []; this.need = true; this.ux = 0; this.uy = 0; }   /* need: the next point starts a subpath; ux/uy: last point (path space) */
    clone() { const p = new PathData(); p.sub = this.sub.map(s => ({ pts: s.pts.slice(), closed: s.closed })); p.need = this.need; p.ux = this.ux; p.uy = this.uy; return p; }
    move(m, x, y) { const [dx, dy] = mApply(m, x, y); this.sub.push({ pts: [dx, dy], closed: false }); this.need = false; this.ux = x; this.uy = y; this.m = m; }
    line(m, x, y) {
        if (this.need || !this.sub.length) return this.move(m, x, y);
        const [dx, dy] = mApply(m, x, y); this.sub[this.sub.length - 1].pts.push(dx, dy); this.ux = x; this.uy = y; this.m = m;
    }
    ensure(m, x, y) { if (this.need || !this.sub.length) this.move(m, x, y); }
    close() {
        const s = this.sub[this.sub.length - 1];
        if (!s || this.need) return;
        s.closed = true;
        this.sub.push({ pts: [s.pts[0], s.pts[1]], closed: false });
        const inv = this.m && mInv(this.m);
        if (inv) [this.ux, this.uy] = mApply(inv, s.pts[0], s.pts[1]);
    }
    /* the current point in the space of matrix m (the matrix may have changed since it was added) */
    cur(m) {
        if (!this.m || this.m === m) return [this.ux, this.uy];
        const s = this.sub[this.sub.length - 1], inv = mInv(m);
        return inv ? mApply(inv, s.pts[s.pts.length - 2], s.pts[s.pts.length - 1]) : [this.ux, this.uy];
    }
    cubic(m, x1, y1, x2, y2, x, y) {
        const [x0, y0] = this.cur(m), sc = mScale(m);
        const len = (Math.hypot(x1 - x0, y1 - y0) + Math.hypot(x2 - x1, y2 - y1) + Math.hypot(x - x2, y - y2)) * sc;
        const n = Math.min(512, Math.max(1, Math.ceil(1.6 * Math.sqrt(len))));
        for (let i = 1; i <= n; i++) {
            const t = i / n, u = 1 - t;
            this.line(m, u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x, u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y);
        }
    }
    quad(m, qx, qy, x, y) { const [x0, y0] = this.cur(m); this.cubic(m, x0 + 2 / 3 * (qx - x0), y0 + 2 / 3 * (qy - y0), x + 2 / 3 * (qx - x), y + 2 / 3 * (qy - y), x, y); }
    /* an elliptical arc from angle a0 sweeping by sw (radians), centre cx, cy, radii rx, ry, rotated by rot */
    ellipse(m, cx, cy, rx, ry, rot, a0, sw) {
        const cr = Math.cos(rot), sr = Math.sin(rot);
        const pt = a => { const ex = rx * Math.cos(a), ey = ry * Math.sin(a); return [cx + cr * ex - sr * ey, cy + sr * ex + cr * ey]; };
        const [sx, sy] = pt(a0);
        if (this.need || !this.sub.length) this.move(m, sx, sy); else this.line(m, sx, sy);
        const n = Math.min(1024, Math.max(4, Math.ceil(Math.abs(sw) * Math.max(rx, ry) * mScale(m) / 2), Math.ceil(Math.abs(sw) * 1.6 * Math.sqrt(Math.max(rx, ry) * mScale(m)))));
        for (let i = 1; i <= n; i++) { const [x, y] = pt(a0 + sw * i / n); this.line(m, x, y); }
    }
    rect(m, x, y, w, h) { this.move(m, x, y); this.line(m, x + w, y); this.line(m, x + w, y + h); this.line(m, x, y + h); this.close(); }
    add(o, m) { for (const s of o.sub) { const pts = []; for (let i = 0; i < s.pts.length; i += 2) pts.push(...mApply(m, s.pts[i], s.pts[i + 1])); this.sub.push({ pts, closed: s.closed }); } if (o.sub.length) { this.need = o.need; const l = this.sub[this.sub.length - 1].pts; this.m = null; this.ux = l[l.length - 2]; this.uy = l[l.length - 1]; } }
    transformed(m) { const p = new PathData(); p.add(this, m); return p; }
}
const TAU = 2 * Math.PI;
const arcSweep = (a0, a1, ccw) => {
    if (!ccw && a1 - a0 >= TAU) return TAU;
    if (ccw && a0 - a1 >= TAU) return -TAU;
    const mod = (v) => { const r = v % TAU; return r < 0 ? r + TAU : r; };
    if (!ccw && a0 > a1) return TAU - mod(a0 - a1);
    if (ccw && a0 < a1) return -(TAU - mod(a1 - a0));
    return a1 - a0;
};
const roundRadii = (radii) => {
    const list = radii === undefined ? [0] : typeof radii === 'object' && radii !== null && typeof radii[Symbol.iterator] === 'function' ? Array.from(radii) : [radii];
    if (list.length < 1 || list.length > 4) throw new RangeError(`${list.length} radii provided. Between one and four radii are necessary.`);
    const r = list.map(v => {
        if (v !== null && typeof v === 'object') { const x = Number(v.x ?? 0), y = Number(v.y ?? 0); if (!fin(x, y)) return null; if (x < 0 || y < 0) throw new RangeError('Radius value is negative.'); return [x, y]; }
        const x = Number(v); if (!Number.isFinite(x)) return null; if (x < 0) throw new RangeError('Radius value is negative.'); return [x, x];
    });
    if (r.includes(null)) return null;
    const [a, b = a, c = a, d = b] = r;
    return r.length === 3 ? [a, b, c, b] : r.length === 2 ? [a, b, a, b] : [a, b, c, d];
};
const PATH_METHODS = {
    closePath() { this._p.close(); },
    moveTo(x, y) { x = +x; y = +y; if (fin(x, y)) this._p.move(this._m(), x, y); },
    lineTo(x, y) { x = +x; y = +y; if (fin(x, y)) this._p.line(this._m(), x, y); },
    quadraticCurveTo(cx, cy, x, y) { const a = [cx, cy, x, y].map(Number); if (!fin(...a)) return; this._p.ensure(this._m(), a[0], a[1]); this._p.quad(this._m(), ...a); },
    bezierCurveTo(c1x, c1y, c2x, c2y, x, y) { const a = [c1x, c1y, c2x, c2y, x, y].map(Number); if (!fin(...a)) return; this._p.ensure(this._m(), a[0], a[1]); this._p.cubic(this._m(), ...a); },
    arcTo(x1, y1, x2, y2, r) {
        [x1, y1, x2, y2, r] = [x1, y1, x2, y2, r].map(Number);
        if (!fin(x1, y1, x2, y2, r)) return;
        if (r < 0) throw c2dIndex(`The radius provided (${r}) is negative.`);
        const m = this._m(), p = this._p;
        p.ensure(m, x1, y1);
        const [x0, y0] = p.cur(m);
        const d01x = x0 - x1, d01y = y0 - y1, d21x = x2 - x1, d21y = y2 - y1, cross = d01x * d21y - d01y * d21x;
        if ((x0 === x1 && y0 === y1) || (x1 === x2 && y1 === y2) || r === 0 || Math.abs(cross) < 1e-12 * Math.hypot(d01x, d01y) * Math.hypot(d21x, d21y)) { p.line(m, x1, y1); return; }
        const l01 = Math.hypot(d01x, d01y), l21 = Math.hypot(d21x, d21y), ang = Math.acos(Math.max(-1, Math.min(1, (d01x * d21x + d01y * d21y) / (l01 * l21))));
        const t = r / Math.tan(ang / 2), tx0 = x1 + d01x / l01 * t, ty0 = y1 + d01y / l01 * t, tx1 = x1 + d21x / l21 * t, ty1 = y1 + d21y / l21 * t;
        const bis = Math.hypot(r, t), mx = (d01x / l01 + d21x / l21), my = (d01y / l01 + d21y / l21), ml = Math.hypot(mx, my);
        const cx = x1 + mx / ml * bis, cy = y1 + my / ml * bis;
        const a0 = Math.atan2(ty0 - cy, tx0 - cx), a1 = Math.atan2(ty1 - cy, tx1 - cx);
        p.line(m, tx0, ty0);
        p.ellipse(m, cx, cy, r, r, 0, a0, arcSweep(a0, a1, cross > 0));
    },
    arc(x, y, r, a0, a1, ccw = false) {
        [x, y, r, a0, a1] = [x, y, r, a0, a1].map(Number);
        if (!fin(x, y, r, a0, a1)) return;
        if (r < 0) throw c2dIndex(`The radius provided (${r}) is negative.`);
        this._p.ellipse(this._m(), x, y, r, r, 0, a0, arcSweep(a0, a1, !!ccw));
    },
    ellipse(x, y, rx, ry, rot, a0, a1, ccw = false) {
        [x, y, rx, ry, rot, a0, a1] = [x, y, rx, ry, rot, a0, a1].map(Number);
        if (!fin(x, y, rx, ry, rot, a0, a1)) return;
        if (rx < 0 || ry < 0) throw c2dIndex(`The ${rx < 0 ? 'major' : 'minor'}-axis radius provided is negative.`);
        this._p.ellipse(this._m(), x, y, rx, ry, rot, a0, arcSweep(a0, a1, !!ccw));
    },
    rect(x, y, w, h) { const a = [x, y, w, h].map(Number); if (fin(...a)) this._p.rect(this._m(), ...a); },
    roundRect(x, y, w, h, radii) {
        [x, y, w, h] = [x, y, w, h].map(Number);
        const r = roundRadii(radii);
        if (!fin(x, y, w, h) || !r) return;
        const m = this._m(), p = this._p;
        let [tl, tr, br, bl] = r;
        if (w < 0) { x += w; w = -w; [tl, tr, br, bl] = [tr, tl, bl, br]; }
        if (h < 0) { y += h; h = -h; [tl, tr, br, bl] = [bl, br, tr, tl]; }
        const f = Math.min(1, w / (tl[0] + tr[0]) || 1, w / (bl[0] + br[0]) || 1, h / (tl[1] + bl[1]) || 1, h / (tr[1] + br[1]) || 1);
        [tl, tr, br, bl] = [tl, tr, br, bl].map(([a, b]) => [a * f, b * f]);
        p.move(m, x + tl[0], y);
        p.line(m, x + w - tr[0], y); if (tr[0] && tr[1]) p.ellipse(m, x + w - tr[0], y + tr[1], tr[0], tr[1], 0, -Math.PI / 2, Math.PI / 2);
        p.line(m, x + w, y + h - br[1]); if (br[0] && br[1]) p.ellipse(m, x + w - br[0], y + h - br[1], br[0], br[1], 0, 0, Math.PI / 2);
        p.line(m, x + bl[0], y + h); if (bl[0] && bl[1]) p.ellipse(m, x + bl[0], y + h - bl[1], bl[0], bl[1], 0, Math.PI / 2, Math.PI / 2);
        p.line(m, x, y + tl[1]); if (tl[0] && tl[1]) p.ellipse(m, x + tl[0], y + tl[1], tl[0], tl[1], 0, Math.PI, Math.PI / 2);
        p.close();
        p.move(m, x, y);
    },
};
const ID = [1, 0, 0, 1, 0, 0];
/* a DOMMatrix2DInit dictionary -> [a, b, c, d, e, f] (TypeError when a and m11 etc. disagree) */
const c2dMatrixInit = (o = {}) => {
    if (o === null || (typeof o !== 'object' && typeof o !== 'function')) throw new TypeError("The provided value is not of type 'DOMMatrix2DInit'.");
    const pick = (a, m, d) => {
        const va = o[a] === undefined ? undefined : Number(o[a]), vm = o[m] === undefined ? undefined : Number(o[m]);
        if (va !== undefined && vm !== undefined && !(va === vm || (Number.isNaN(va) && Number.isNaN(vm)))) throw new TypeError(`The '${a}' property should equal the '${m}' property.`);
        return vm !== undefined ? vm : va !== undefined ? va : d;
    };
    return [pick('a', 'm11', 1), pick('b', 'm12', 0), pick('c', 'm21', 0), pick('d', 'm22', 1), pick('e', 'm41', 0), pick('f', 'm42', 0)];
};


/* SVG path data into a Path2D */
function pathFromSvg(P, d) {
    const re = /([MmLlHhVvCcSsQqTtAaZz])|([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)/g, tok = [];
    let t; while ((t = re.exec(d))) tok.push(t[1] || +t[2]);
    let i = 0, cmd = '', cx = 0, cy = 0, sx = 0, sy = 0, kx = 0, ky = 0, prev = '';
    const num = () => typeof tok[i] === 'number' ? tok[i++] : NaN;
    while (i < tok.length) {
        if (typeof tok[i] === 'string') cmd = tok[i++]; else if (!cmd) return;
        const rel = cmd === cmd.toLowerCase() && cmd !== 'Z', C = cmd.toUpperCase(), ox = rel ? cx : 0, oy = rel ? cy : 0;
        if (C === 'Z') { P.closePath(); cx = sx; cy = sy; prev = 'Z'; cmd = ''; continue; }
        const need = { M: 2, L: 2, T: 2, H: 1, V: 1, C: 6, S: 4, Q: 4, A: 7 }[C], a = [];
        for (let k = 0; k < need; k++) a.push(num());
        if (a.some(Number.isNaN)) return;
        switch (C) {
        case 'M': cx = a[0] + ox; cy = a[1] + oy; P.moveTo(cx, cy); sx = cx; sy = cy; cmd = rel ? 'l' : 'L'; break;
        case 'L': cx = a[0] + ox; cy = a[1] + oy; P.lineTo(cx, cy); break;
        case 'H': cx = a[0] + ox; P.lineTo(cx, cy); break;
        case 'V': cy = a[0] + oy; P.lineTo(cx, cy); break;
        case 'C': P.bezierCurveTo(a[0] + ox, a[1] + oy, kx = a[2] + ox, ky = a[3] + oy, cx = a[4] + ox, cy = a[5] + oy); break;
        case 'S': { const x1 = 'CS'.includes(prev) ? 2 * cx - kx : cx, y1 = 'CS'.includes(prev) ? 2 * cy - ky : cy; P.bezierCurveTo(x1, y1, kx = a[0] + ox, ky = a[1] + oy, cx = a[2] + ox, cy = a[3] + oy); break; }
        case 'Q': P.quadraticCurveTo(kx = a[0] + ox, ky = a[1] + oy, cx = a[2] + ox, cy = a[3] + oy); break;
        case 'T': kx = 'QT'.includes(prev) ? 2 * cx - kx : cx; ky = 'QT'.includes(prev) ? 2 * cy - ky : cy; P.quadraticCurveTo(kx, ky, cx = a[0] + ox, cy = a[1] + oy); break;
        case 'A': {
            const x2 = a[5] + ox, y2 = a[6] + oy;
            let rx = Math.abs(a[0]), ry = Math.abs(a[1]);
            if (!rx || !ry) { P.lineTo(x2, y2); } else if (cx !== x2 || cy !== y2) {
                const ph = a[2] * Math.PI / 180, cp = Math.cos(ph), sp = Math.sin(ph), dx = (cx - x2) / 2, dy = (cy - y2) / 2;
                const xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy, lam = xp * xp / (rx * rx) + yp * yp / (ry * ry);
                if (lam > 1) { rx *= Math.sqrt(lam); ry *= Math.sqrt(lam); }
                const nu = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp, de = rx * rx * yp * yp + ry * ry * xp * xp;
                let co = de > 0 ? Math.sqrt(Math.max(0, nu / de)) : 0; if (!!a[3] === !!a[4]) co = -co;
                const cxp = co * rx * yp / ry, cyp = -co * ry * xp / rx, ccx = cp * cxp - sp * cyp + (cx + x2) / 2, ccy = sp * cxp + cp * cyp + (cy + y2) / 2;
                const t1 = Math.atan2((yp - cyp) / ry, (xp - cxp) / rx); let dt = Math.atan2((-yp - cyp) / ry, (-xp - cxp) / rx) - t1;
                if (a[4] && dt < 0) dt += TAU; else if (!a[4] && dt > 0) dt -= TAU;
                P._p.ellipse(P._m(), ccx, ccy, rx, ry, ph, t1, dt);
            }
            cx = x2; cy = y2; break;
        }
        }
        prev = C;
    }
}

class Path2D {
    constructor(src) {
        def(this, '_p', new PathData());
        if (src instanceof Path2D) def(this, '_p', src._p.clone());
        else if (src !== undefined) pathFromSvg(this, String(src));
    }
    _m() { return ID; }
    addPath(path, t) {
        if (!(path instanceof Path2D)) throw new TypeError("Failed to execute 'addPath' on 'Path2D': parameter 1 is not of type 'Path2D'.");
        let m = ID;
        if (t !== undefined && t !== null) { m = c2dMatrixInit(t); if (!fin(...m)) return; }
        this._p.add(path._p, m);
    }
}
for (const [k, f] of Object.entries(PATH_METHODS)) Object.defineProperty(Path2D.prototype, k, { value: f, writable: true, configurable: true });

/* ---- paints ---- */
const premul = (r, g, b, a) => { const A = Math.round(a * 255), k = A / 255; return ((A << 24) | (Math.round(r * 255 * k) << 16) | (Math.round(g * 255 * k) << 8) | Math.round(b * 255 * k)) >>> 0; };
const colorNum3 = v => { const r = +v.toFixed(6); return String(Object.is(r, -0) ? 0 : r); };
const clamp01 = v => Math.max(0, Math.min(1, v));
const C2D_SPACES = ['srgb', 'display-p3', 'srgb-linear'];
/* 'srgb' <-> 'display-p3' for 8-bit values: same transfer curve, different primaries (linear-light matrices) */
const C2D_TOP3 = [0.8224621, 0.177538, 0, 0.0331941, 0.9668058, 0, 0.0170827, 0.0723974, 0.9105199];
const C2D_TOSRGB = [1.2249401, -0.2249404, 0, -0.0420569, 1.0420571, 0, -0.0196376, -0.0786361, 1.0982735];
let c2dLin = null, c2dEnc = null;
/* a function (r, g, b) -> 0xRRGGBB converting 8-bit values from colour space `from` into `to` */
function c2dXf(to, from) {
    if (!c2dLin) {
        c2dLin = new Float32Array(256);
        for (let i = 0; i < 256; i++) { const v = i / 255; c2dLin[i] = v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; }
        c2dEnc = new Uint8Array(16385);
        for (let i = 0; i <= 16384; i++) { const v = i / 16384; c2dEnc[i] = Math.round(255 * (v <= 0.0031308 ? v * 12.92 : 1.055 * v ** (1 / 2.4) - 0.055)); }
    }
    const p3 = c => c === 'display-p3', M = p3(to) === p3(from) ? [1, 0, 0, 0, 1, 0, 0, 0, 1] : p3(to) ? C2D_TOP3 : C2D_TOSRGB, memo = new Map();
    const lin = from === 'srgb-linear' ? i => i / 255 : i => c2dLin[i], enc = to === 'srgb-linear' ? v => Math.round(clamp01(v) * 255) : v => c2dEnc[Math.round(clamp01(v) * 16384)];
    return (r, g, b) => {
        const k = (r << 16) | (g << 8) | b;
        let q = memo.get(k);
        if (q === undefined) {
            const R = lin(r), G = lin(g), B = lin(b);
            q = (enc(M[0] * R + M[1] * G + M[2] * B) << 16) | (enc(M[3] * R + M[4] * G + M[5] * B) << 8) | enc(M[6] * R + M[7] * G + M[8] * B);
            if (memo.size > 65536) memo.clear();
            memo.set(k, q);
        }
        return q;
    };
}
/* premultiplied ARGB pixels from colour space `from` into `to` (a new array, or px itself when they match) */
function c2dConvert(px, from, to) {
    if ((from || 'srgb') === (to || 'srgb') || !px.length) return px;
    const f = c2dXf(to, from || 'srgb'), out = new Uint32Array(px.length);
    for (let i = 0; i < px.length; i++) {
        const p = px[i], a = p >>> 24;
        if (!a) continue;
        const un = c => Math.min(255, Math.round(c * 255 / a)), q = f(un((p >>> 16) & 255), un((p >>> 8) & 255), un(p & 255)), k = a / 255;
        out[i] = ((a << 24) | (Math.round(((q >>> 16) & 255) * k) << 16) | (Math.round(((q >>> 8) & 255) * k) << 8) | Math.round((q & 255) * k)) >>> 0;
    }
    return out;
}
/* straight-alpha RGBA bytes converted in place */
function c2dConvertRGBA(d, from, to) {
    if ((from || 'srgb') === (to || 'srgb')) return;
    const f = c2dXf(to, from || 'srgb');
    for (let j = 0; j < d.length; j += 4) { if (!d[j + 3]) continue; const q = f(d[j], d[j + 1], d[j + 2]); d[j] = q >>> 16; d[j + 1] = (q >>> 8) & 255; d[j + 2] = q & 255; }
}
/* a colour string -> { s: serialization, v: [r, g, b, a] 0..1 in colour space cs } or null */
function c2dColor(text, cv, cs = 'srgb') {
    const c = cssColor(String(text));
    if (!c) return null;
    let col = c;
    if (c.current) {
        let cur = null;
        try { if (cv && cv.isConnected) cur = cssColor(getComputedStyle(cv).color); } catch (e) {}
        if (!cur || cur.current) return { s: '#000000', v: [0, 0, 0, 1] };
        col = cur;
    }
    const rgb = colorTo(col, 'srgb').map(clamp01), a = clamp01(col.alpha ?? 1);
    const b8 = rgb.map(v => Math.round(v * 255));
    const a8 = Math.round(a * 255);
    let as = colorNum3(a);
    for (let d = 1; d <= 6; d++) { const t = +a.toFixed(d); if (Math.round(t * 255) === a8) { as = String(t); break; } }
    let s = a >= 1 ? '#' + b8.map(v => v.toString(16).padStart(2, '0')).join('') : `rgba(${b8.join(', ')}, ${as})`;
    if (!c.current && c.legacy === false) {   /* modern colours keep their own space, as Chrome serialises them */
        const body = c.c.map(colorNum3).join(' ') + (a < 1 ? ' / ' + colorNum3(a) : '');
        s = c.space === 'lab' || c.space === 'oklab' ? `${c.space}(${body})` : `color(${c.space} ${body})`;
    }
    const v = cs === 'srgb' ? b8.map(x => x / 255) : colorTo(col, cs === 'srgb-linear' ? 'srgb-linear' : cs).map(clamp01);
    return { s, v: [v[0], v[1], v[2], a] };
}
class CanvasGradient {
    constructor(k, kind, g, cs) { if (k !== C2D_KEY) c2dIllegal(); def(this, '_', { kind, g, stops: [], lut: null, cs: cs || 'srgb' }); }
    addColorStop(offset, color) {
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'addColorStop' on 'CanvasGradient': 2 arguments required, but only ${arguments.length} present.`);
        const o = toDouble(offset, 'offset');
        if (o < 0 || o > 1) throw c2dIndex(`The provided value (${o}) is outside the range (0.0, 1.0).`);
        const c = c2dColor(color, null, this._.cs);
        if (!c) throw new DOMException(`The value provided ('${color}') could not be parsed as a color.`, 'SyntaxError');
        const st = this._.stops; let i = st.length; while (i > 0 && st[i - 1][0] > o) i--;
        st.splice(i, 0, [o, c.v]); this._.lut = null;
    }
    _table() {
        const s = this._;
        if (s.lut) return s.lut;
        const lut = new Uint32Array(1024), st = s.stops;
        const pm = v => [v[0] * v[3], v[1] * v[3], v[2] * v[3], v[3]];
        for (let i = 0; i < 1024; i++) {
            const t = i / 1023;
            let j = 0; while (j < st.length && st[j][0] <= t) j++;
            let c;
            if (j === 0) c = pm(st[0][1]); else if (j === st.length) c = pm(st[st.length - 1][1]);
            else {
                const [o0, c0] = st[j - 1], [o1, c1] = st[j], f = o1 > o0 ? (t - o0) / (o1 - o0) : 1, p0 = pm(c0), p1 = pm(c1);
                c = p0.map((v, k) => v + (p1[k] - v) * f);
            }
            const A = Math.round(c[3] * 255);
            lut[i] = ((A << 24) | (Math.min(A, Math.round(c[0] * 255)) << 16) | (Math.min(A, Math.round(c[1] * 255)) << 8) | Math.min(A, Math.round(c[2] * 255))) >>> 0;
        }
        return (s.lut = lut);
    }
}
class CanvasPattern {
    constructor(k, src, rep) { if (k !== C2D_KEY) c2dIllegal(); def(this, '_', { ...src, rep, m: ID }); }
    setTransform(t) {
        const m = c2dMatrixInit(t === undefined ? {} : t);
        if (fin(...m)) this._.m = m;
    }
}
const c2dRectOf = (x, y, w, h) => typeof DOMRect !== 'undefined' ? new DOMRect(x, y, w, h) : { x, y, width: w, height: h, left: Math.min(x, x + w), top: Math.min(y, y + h), right: Math.max(x, x + w), bottom: Math.max(y, y + h) };
class TextMetrics {
    constructor(k, o) { if (k !== C2D_KEY) c2dIllegal(); def(this, '_', o); }
    /* x of the caret before UTF-16 index i, relative to the alignment point */
    _x(i) { const o = this._, f = o.font; return (i ? C2N.c2dMeasure(o.text.slice(0, i), f.family, f.weight, f.italic, f.size)[0] : 0) - o.actualBoundingBoxLeft; }
    _range(start, end, fn, strict) {
        const n = this._.text.length;
        start = erULong(start); end = erULong(end);
        if ((strict ? start >= n : start > n) || end > n) throw c2dIndex(`Failed to execute '${fn}' on 'TextMetrics': The ${start > n || (strict && start >= n) ? 'start' : 'end'} index is out of range.`);
        return start <= end ? [start, end] : [end, start];
    }
    getIndexFromOffset(offset) {
        const x = toDouble(offset), t = this._.text;
        let best = 0, bd = Infinity;
        for (let i = 0; i <= t.length; i++) {
            if (i > 0 && i < t.length && (t.charCodeAt(i) & 0xfc00) === 0xdc00) continue;   /* inside a surrogate pair */
            const d = Math.abs(this._x(i) - x);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }
    getSelectionRects(start, end) {
        const [a, b] = this._range(start, end, 'getSelectionRects');
        if (a === b) return [];
        const x0 = this._x(a), x1 = this._x(b), o = this._;
        return [c2dRectOf(x0, -o.fontBoundingBoxAscent, x1 - x0, o.fontBoundingBoxAscent + o.fontBoundingBoxDescent)];
    }
    getActualBoundingBox(start, end) {
        const [a, b] = this._range(start, end, 'getActualBoundingBox', true);
        const x0 = this._x(a), x1 = this._x(b), o = this._;
        return c2dRectOf(x0, -o.actualBoundingBoxAscent, x1 - x0, o.actualBoundingBoxAscent + o.actualBoundingBoxDescent);
    }
}
for (const k of ['width', 'actualBoundingBoxLeft', 'actualBoundingBoxRight', 'fontBoundingBoxAscent', 'fontBoundingBoxDescent', 'actualBoundingBoxAscent',
    'actualBoundingBoxDescent', 'emHeightAscent', 'emHeightDescent', 'hangingBaseline', 'alphabeticBaseline', 'ideographicBaseline'])
    Object.defineProperty(TextMetrics.prototype, k, { get() { return this._[k]; }, configurable: true, enumerable: true });

/* image sources -> { px: Uint32Array premultiplied ARGB, w, h, sw, sh (CSS size of the source), cs (colour space) } | null (not ready) */
function c2dImage(img, fn) {
    const bad = () => { throw new TypeError(`Failed to execute '${fn}' on 'CanvasRenderingContext2D': The provided value is not of type '(CSSImageValue or HTMLCanvasElement or HTMLImageElement or HTMLVideoElement or ImageBitmap or OffscreenCanvas or SVGImageElement or VideoFrame)'.`); };
    if (img === null || typeof img !== 'object') bad();
    const ctxOf = cv => cv.__ck === '2d' ? cv.__cx._ : null;
    if (c2dIs(img, 'HTMLCanvasElement') || (typeof OffscreenCanvas !== 'undefined' && img instanceof OffscreenCanvas)) {
        const w = img.width, h = img.height;
        if (!w || !h) throw new DOMException('The image argument is a canvas element with a width or height of 0.', 'InvalidStateError');
        const s = c2dIs(img, 'HTMLCanvasElement') ? ctxOf(img) : img._ && img._.cx && img._.cx._;
        if (s) return { px: s.px, w: s.w, h: s.h, sw: s.w, sh: s.h, cv: img, cs: s.cs };
        const r = c2dIs(img, 'HTMLCanvasElement') && (img.__ck || img.__offscreen) ? C2N.c2dSource(img) : null;   /* no context yet: transparent black */
        return r ? { px: new Uint32Array(r[2]), w: r[0], h: r[1], sw: w, sh: h } : { px: new Uint32Array(w * h), w, h, sw: w, sh: h };
    }
    if (img instanceof C2IB) {
        const b = img._;
        if (!b.src) throw new DOMException('The image source is detached.', 'InvalidStateError');
        if (b.src instanceof ImageData) return imageDataSrc(b.src);
        const r = c2dImage(b.src, fn);
        return r && { ...r, sw: b.w || r.sw, sh: b.h || r.sh };
    }
    if (img instanceof ImageData) return imageDataSrc(img);
    const isImg = c2dIs(img, 'HTMLImageElement'), isVid = c2dIs(img, 'HTMLVideoElement'), isSvg = c2dIs(img, 'SVGImageElement');
    if (!isImg && !isVid && !isSvg) bad();
    if (isImg && img.complete && img.src && !img.naturalWidth && img.__broken) throw new DOMException('The HTMLImageElement provided is in the \'broken\' state.', 'InvalidStateError');
    if (isVid && img.readyState < 2) return null;
    const r = C2N.c2dSource(img);
    if (!r) return null;
    const sw = isImg ? img.naturalWidth || r[0] : isVid ? img.videoWidth || r[0] : r[0], sh = isImg ? img.naturalHeight || r[1] : isVid ? img.videoHeight || r[1] : r[1];
    if (!sw || !sh || !r[0] || !r[1]) return null;   /* zero-size source: createPattern gives null, drawImage nothing */
    return { px: new Uint32Array(r[2]), w: r[0], h: r[1], sw, sh };
}
function imageDataSrc(d) {
    const w = d.width, h = d.height, px = new Uint32Array(w * h), b = d.data;
    for (let i = 0, j = 0; i < px.length; i++, j += 4) { const a = b[j + 3]; px[i] = ((a << 24) | (Math.round(b[j] * a / 255) << 16) | (Math.round(b[j + 1] * a / 255) << 8) | Math.round(b[j + 2] * a / 255)) >>> 0; }
    return { px, w, h, sw: w, sh: h, cs: d.colorSpace };
}

/* createImageBitmap(image, [sx, sy, sw, sh,] options) for pages and workers: the source is snapshotted into an
   ImageData now (crop, resize, orientation applied); decode(ArrayBuffer) -> Promise<[w, h, RGBA ArrayBuffer] | null> */
const C2IB_ENUMS = { colorSpaceConversion: ['none', 'default'], imageOrientation: ['from-image', 'flipY', 'none'], premultiplyAlpha: ['none', 'premultiply', 'default'], resizeQuality: ['pixelated', 'low', 'medium', 'high'] };
function c2dBitmap(src, args, decode) {
    try {
        const fail = m => new DOMException(m, 'InvalidStateError');
        if (args.length === 0 && src === undefined) throw new TypeError("Failed to execute 'createImageBitmap': 1 argument required, but only 0 present.");
        let crop = null, opts;
        if (args.length >= 4) {
            crop = args.slice(0, 4).map(v => { const x = Number(v); return Number.isFinite(x) ? x | 0 : 0; });
            if (!crop[2] || !crop[3]) throw new RangeError(`The crop rect ${crop[2] ? 'height' : 'width'} is 0.`);
            opts = args[4];
        } else if (args.length > 0 && args[0] !== undefined && args[0] !== null && typeof args[0] !== 'object') throw new TypeError("Failed to execute 'createImageBitmap': Overload resolution failed.");
        else opts = args[0];
        if (opts !== undefined && opts !== null && typeof opts !== 'object' && typeof opts !== 'function') throw new TypeError("Failed to execute 'createImageBitmap': The provided value is not of type 'ImageBitmapOptions'.");
        const o = {};
        for (const k of ['colorSpaceConversion', 'imageOrientation', 'premultiplyAlpha', 'resizeHeight', 'resizeQuality', 'resizeWidth']) {
            const v = opts == null ? undefined : opts[k];
            if (v === undefined) continue;
            if (k === 'resizeWidth' || k === 'resizeHeight') o[k] = erULong(v);
            else if (!C2IB_ENUMS[k].includes(String(v))) throw new TypeError(`The provided value '${v}' is not a valid enum value of type ${k === 'resizeQuality' ? 'ResizeQuality' : k[0].toUpperCase() + k.slice(1)}.`);
            else o[k] = String(v);
        }
        if (o.resizeWidth === 0 || o.resizeHeight === 0) throw fail('The resizeWidth or/and resizeHeight is equal to 0.');
        const build = from => {   /* from: ImageData (exact copy path) or a c2dImage source */
            const isD = from instanceof ImageData, fw = isD ? from.width : from.sw, fh = isD ? from.height : from.sh, cs = isD ? from.colorSpace : from.cs || 'srgb';
            let [cx, cy, cw, ch] = crop || [0, 0, fw, fh];
            if (cw < 0) { cx += cw; cw = -cw; } if (ch < 0) { cy += ch; ch = -ch; }
            const ow = o.resizeWidth ?? (o.resizeHeight !== undefined ? Math.ceil(cw * o.resizeHeight / ch) : cw);
            const oh = o.resizeHeight ?? (o.resizeWidth !== undefined ? Math.ceil(ch * o.resizeWidth / cw) : ch);
            if (ow * oh > 268435456) {   /* too large to allocate: an empty bitmap that keeps its size */
                const b = Object.create(C2IB.prototype);
                def(b, '_', { src: new ImageData(1, 1, { colorSpace: cs }), w: ow, h: oh, flipY: false, premul: false });
                return b;
            }
            let d;
            if (isD && ow === cw && oh === ch) {
                d = new ImageData(ow, oh, { colorSpace: cs });
                const od = d.data, id = from.data;
                for (let y = 0; y < oh; y++) {
                    const fy = cy + y; if (fy < 0 || fy >= fh) continue;
                    const x0 = Math.max(0, cx), x1 = Math.min(fw, cx + cw);
                    if (x1 > x0) od.set(id.subarray((fy * fw + x0) * 4, (fy * fw + x1) * 4), (y * ow + x0 - cx) * 4);
                }
            } else {
                const s = c2dState(null, ow, oh, { colorSpace: cs });
                s.st.smooth = o.resizeQuality !== 'pixelated';
                c2dBlit(s, isD ? imageDataSrc(from) : from, cx, cy, cw, ch, 0, 0, ow, oh);
                d = C2D_METHODS.getImageData.call({ _: s }, 0, 0, Math.max(1, s.w), Math.max(1, s.h));
            }
            if (o.imageOrientation === 'flipY') {
                const row = d.width * 4, t = new Uint8ClampedArray(row), a = d.data;
                for (let y = 0, z = d.height - 1; y < z; y++, z--) { t.set(a.subarray(y * row, y * row + row)); a.copyWithin(y * row, z * row, z * row + row); a.set(t, z * row); }
            }
            const b = Object.create(C2IB.prototype);
            def(b, '_', { src: d, w: d.width, h: d.height, flipY: false, premul: o.premultiplyAlpha === 'premultiply' });
            return b;
        };
        if (typeof Blob !== 'undefined' && src instanceof Blob)
            return src.arrayBuffer().then(decode).then(r => {
                if (!r) throw fail('The source image could not be decoded.');
                return build(new ImageData(new Uint8ClampedArray(r[2]), r[0], r[1]));
            });
        if (src instanceof ImageData) {
            if (src.data.buffer.detached) throw fail('The image data has been detached.');
            return Promise.resolve(build(src));
        }
        if (src instanceof C2IB) {
            if (!src._.src) throw fail('The image source is detached.');
            return Promise.resolve(build(src._.src instanceof ImageData ? src._.src : c2dImage(src, 'createImageBitmap')));
        }
        if (typeof OffscreenCanvas !== 'undefined' && src instanceof OffscreenCanvas && src._.detached) throw fail('The OffscreenCanvas is detached.');
        if (c2dIs(src, 'HTMLImageElement') && !src.getAttribute('src') && !src.getAttribute('srcset')) throw fail('The image element contains no source.');
        const r = c2dImage(src, 'createImageBitmap');
        if (!r) throw fail(c2dIs(src, 'HTMLVideoElement') ? 'The provided element has not retrieved data.' : 'The source image could not be decoded.');
        if (r.cv) r.px = r.px.slice();
        return Promise.resolve(build(r));
    } catch (e) { return Promise.reject(e); }
}

/* ---- font shorthand ---- */
const FONT_SIZES = { 'xx-small': 9, 'x-small': 10, small: 13, medium: 16, large: 18, 'x-large': 24, 'xx-large': 32, 'xxx-large': 48 };
const FONT_STRETCH = ['ultra-condensed', 'extra-condensed', 'condensed', 'semi-condensed', 'normal', 'semi-expanded', 'expanded', 'extra-expanded', 'ultra-expanded'];
function c2dFont(text, cv) {
    const s = String(text).trim();
    const m = /^((?:(?:normal|italic|oblique|small-caps|bold|bolder|lighter|[1-9]00|[\w-]*condensed|[\w-]*expanded)\s+)*)((?:[\d.]+(?:px|pt|pc|in|cm|mm|q|em|rem|ex|ch|%|vw|vh))|xx-small|x-small|small|medium|large|x-large|xx-large|xxx-large|larger|smaller)(?:\s*\/\s*(?:normal|[\d.]+[a-z%]*))?\s+(.+)$/i.exec(s);
    if (!m) return null;
    let style = 'normal', variant = 'normal', weight = 'normal', stretch = 'normal';
    for (const t of m[1].trim().split(/\s+/).filter(Boolean)) {
        const l = t.toLowerCase();
        if (l === 'italic' || l === 'oblique') style = l; else if (l === 'small-caps') variant = l; else if (/^(bold|bolder|lighter|[1-9]00)$/.test(l)) weight = l;
        else if (FONT_STRETCH.includes(l)) stretch = l; else if (l !== 'normal') return null;
    }
    let base = 10;
    try { if (cv && cv.isConnected) base = parseFloat(getComputedStyle(cv).fontSize) || 10; } catch (e) {}
    const z = m[2].toLowerCase();
    let px;
    if (FONT_SIZES[z]) px = FONT_SIZES[z]; else if (z === 'larger') px = base * 1.2; else if (z === 'smaller') px = base / 1.2;
    else {
        const v = parseFloat(z), u = z.replace(/^[\d.]+/, '');
        px = { px: v, pt: v * 4 / 3, pc: v * 16, in: v * 96, cm: v * 96 / 2.54, mm: v * 96 / 25.4, q: v * 96 / 101.6, em: v * base, rem: v * 16, ex: v * base / 2, ch: v * base / 2, '%': v * base / 100, vw: v * (globalThis.innerWidth || 0) / 100, vh: v * (globalThis.innerHeight || 0) / 100 }[u];
    }
    if (!(px >= 0)) return null;
    const fams = [];
    for (const f of m[3].split(',')) {
        const t = f.trim(), q = /^(['"])(.*)\1$/.exec(t);
        if (q) fams.push(q[2]); else if (/^[a-z_-][\w -]*$/i.test(t)) fams.push(t.replace(/\s+/g, ' ')); else return null;
    }
    const GEN = /^(serif|sans-serif|monospace|cursive|fantasy|system-ui|math|emoji|ui-\w+)$/;
    const famStr = fams.map(f => GEN.test(f) || /^[\w-]+$/.test(f) ? f : `"${f}"`).join(', ');
    const wnum = weight === 'bold' ? 700 : weight === 'normal' ? 400 : weight === 'bolder' ? 700 : weight === 'lighter' ? 100 : +weight;
    const pxs = colorNum3(px);
    const ser = [style !== 'normal' && style, variant !== 'normal' && variant, weight !== 'normal' && weight !== '400' && weight, stretch !== 'normal' && stretch, pxs + 'px', famStr].filter(Boolean).join(' ');
    return { s: ser, family: fams.join(','), weight: wnum, italic: style !== 'normal', size: px };
}

/* ---- the context ---- */
const c2dDefaults = () => ({
    fill: '#000000', fillV: [0, 0, 0, 1], stroke: '#000000', strokeV: [0, 0, 0, 1], lw: 1, cap: 'butt', join: 'miter', miter: 10, dash: [], dashOff: 0,
    font: '10px sans-serif', fontD: { family: 'sans-serif', weight: 400, italic: false, size: 10 }, align: 'start', base: 'alphabetic', dir: 'inherit',
    alpha: 1, op: 0, sBlur: 0, sColor: 'rgba(0, 0, 0, 0)', sColorV: [0, 0, 0, 0], sx: 0, sy: 0, smooth: true, smoothQ: 'low', filter: 'none',
    m: ID, clip: null, letterSpacing: '0px', wordSpacing: '0px', kerning: 'auto', stretch: 'normal', caps: 'normal', rendering: 'auto', lang: 'inherit',
});
/* a bitmap Chrome could not allocate (over 2^28 pixels or out of memory) is left empty: drawing is a no-op */
function c2dAlloc(s, w, h) {
    s.w = w; s.h = h;
    try { if (w * h > 268435456) throw new RangeError(); s.px = new Uint32Array(Math.max(0, w * h)); } catch (e) { s.px = new Uint32Array(0); s.w = s.h = 0; }
    if (!s.alpha) s.px.fill(0xff000000);
}
function c2dState(target, w, h, opts) {
    const alpha = !(opts && opts.alpha === false);
    const s = { target, w, h, alpha, opts: { alpha, colorSpace: opts && C2D_SPACES.includes(opts.colorSpace) ? opts.colorSpace : 'srgb', desynchronized: !!(opts && opts.desynchronized), willReadFrequently: !!(opts && opts.willReadFrequently) },
        st: c2dDefaults(), stack: [], path: new PathData(), dirty: false };
    s.cs = s.opts.colorSpace;
    c2dAlloc(s, w, h);
    return s;
}
/* ImageDataSettings with the canvas's colour space as the default */
const c2dDataSettings = (settings, cs) => settings != null && typeof settings === 'object' && settings.colorSpace !== undefined ? settings : { colorSpace: cs };
/* draw source rect (sx, sy, sw, sh) of an image source into (dx, dy, dw, dh), converting its colour space */
function c2dBlit(s, src, sx, sy, sw, sh, dx, dy, dw, dh) {
    if (!fin(sx, sy, sw, sh, dx, dy, dw, dh)) return;
    if (sw < 0) { sx += sw; sw = -sw; } if (sh < 0) { sy += sh; sh = -sh; }
    if (dw < 0) { dx += dw; dw = -dw; } if (dh < 0) { dy += dh; dh = -dh; }
    if (!sw || !sh || !dw || !dh) return;
    const fx = dw / sw, fy = dh / sh;
    const x0 = Math.max(sx, 0), y0 = Math.max(sy, 0), x1 = Math.min(sx + sw, src.sw), y1 = Math.min(sy + sh, src.sh);
    if (x1 <= x0 || y1 <= y0) return;
    dx += (x0 - sx) * fx; dy += (y0 - sy) * fy; dw = (x1 - x0) * fx; dh = (y1 - y0) * fy; sx = x0; sy = y0; sw = x1 - x0; sh = y1 - y0;
    const kx = src.w / src.sw, ky = src.h / src.sh;
    let px = c2dConvert(src.px, src.cs, s.cs);
    if (src.cv && px === s.px) px = px.slice();
    const inv = mInv(mMul(s.st.m, [fx / kx, 0, 0, fy / ky, dx - sx * fx, dy - sy * fy]));
    if (!inv) return;
    const pr = new Float32Array(12); pr.set(inv, 0);
    c2dDraw(s, pathShape(c2dRect(s.st.m, dx, dy, dw, dh), false), [4, 0, pr, null, px, src.w, src.h, s.st.smooth ? 4 : 0]);
}
const c2dPresent = s => {
    const node = s.target instanceof C2Node ? s.target : s.target && s.target._ && s.target._.ph;   /* ph: transferControlToOffscreen placeholder */
    if (s.dirty || !node || !C2N.c2dPresent) return;
    s.dirty = true;
    queueMicrotask(() => { s.dirty = false; C2N.c2dPresent(node, s.px, s.w, s.h); });
};
function c2dPaintOf(s, which) {
    const st = s.st, v = st[which];
    if (typeof v === 'string') { const c = st[which + 'V']; return [0, premul(c[0], c[1], c[2], c[3]), null, null, null, 0, 0, 0]; }
    if (v instanceof CanvasGradient) {
        const g = v._;
        if (!g.stops.length) return null;
        const inv = mInv(st.m);
        if (!inv) return null;
        const pr = new Float32Array(12); pr.set(inv, 0); pr.set(g.g.slice(0, 6), 6);
        return [g.kind, 0, pr, v._table(), null, 0, 0, 0];
    }
    const p = v._, inv = mInv(mMul(st.m, mMul(p.m, [p.sw / p.w || 1, 0, 0, p.sh / p.h || 1, 0, 0])));
    if (!inv) return null;
    const pr = new Float32Array(12); pr.set(inv, 0);
    return [4, 0, pr, null, p.px, p.w, p.h, (p.rep === 'repeat' || p.rep === 'repeat-x' ? 1 : 0) | (p.rep === 'repeat' || p.rep === 'repeat-y' ? 2 : 0) | (st.smooth ? 4 : 0)];
}
const pathShape = (p, eo) => {
    let n = 0; const subs = p.sub.filter(x => x.pts.length >= 6);
    for (const x of subs) n += x.pts.length;
    if (!subs.length) return null;
    const xy = new Float32Array(n), sub = new Int32Array(subs.length);
    let o = 0; subs.forEach((x, k) => { sub[k] = o / 2; xy.set(x.pts, o); o += x.pts.length; });
    return [xy, sub, eo];
};
const polysShape = polys => {
    if (!polys.length) return null;
    let n = 0; for (const q of polys) n += q.length;
    const xy = new Float32Array(n), sub = new Int32Array(polys.length);
    let o = 0; polys.forEach((q, k) => { sub[k] = o / 2; xy.set(q, o); o += q.length; });
    return [xy, sub, false];
};
/* stroke outline of a device-space path as device polygons, all positively wound (filled nonzero) */
function strokePolys(path, st) {
    const m = st.m, inv = mInv(m);
    if (!inv || !(st.lw > 0)) return [];
    const hw = st.lw / 2, out = [], sc = mScale(m);
    const emit = q => {
        const d = []; let ar = 0;
        for (let i = 0; i < q.length; i += 2) d.push(...mApply(m, q[i], q[i + 1]));
        for (let i = 0; i < d.length; i += 2) { const j = (i + 2) % d.length; ar += d[i] * d[j + 1] - d[j] * d[i + 1]; }
        if (ar < 0) { const r = []; for (let i = d.length - 2; i >= 0; i -= 2) r.push(d[i], d[i + 1]); out.push(r); } else out.push(d);
    };
    const circle = (x, y) => { const n = Math.max(8, Math.min(256, Math.ceil(hw * sc * 1.5))), q = []; for (let i = 0; i < n; i++) q.push(x + hw * Math.cos(TAU * i / n), y + hw * Math.sin(TAU * i / n)); emit(q); };
    let subs = [];
    for (const s of path.sub) {
        const pts = [];
        for (let i = 0; i < s.pts.length; i += 2) pts.push(mApply(inv, s.pts[i], s.pts[i + 1]));
        subs.push({ pts, closed: s.closed });
    }
    const dsum = st.dash.reduce((a, b) => a + b, 0);
    if (st.dash.length && dsum > 0) subs = dashSubs(subs, st.dash, st.dashOff);
    for (const s of subs) {
        const p = [];
        for (const q of s.pts) if (!p.length || Math.hypot(q[0] - p[p.length - 1][0], q[1] - p[p.length - 1][1]) > 1e-9) p.push(q);
        let closed = s.closed;
        if (closed && p.length > 1 && Math.hypot(p[0][0] - p[p.length - 1][0], p[0][1] - p[p.length - 1][1]) < 1e-9) p.pop();
        if (p.length === 1) {
            if (s.pts.length < 2 && !s.zero) continue;
            if (st.cap === 'round') circle(p[0][0], p[0][1]);
            else if (st.cap === 'square') emit([p[0][0] - hw, p[0][1] - hw, p[0][0] + hw, p[0][1] - hw, p[0][0] + hw, p[0][1] + hw, p[0][0] - hw, p[0][1] + hw]);
            continue;
        }
        if (p.length < 2) continue;
        if (p.length === 2) closed = false;
        const nseg = closed ? p.length : p.length - 1;
        const dir = i => { const a = p[i], b = p[(i + 1) % p.length], l = Math.hypot(b[0] - a[0], b[1] - a[1]); return [(b[0] - a[0]) / l, (b[1] - a[1]) / l]; };
        for (let i = 0; i < nseg; i++) {
            const a = p[i], b = p[(i + 1) % p.length], [dx, dy] = dir(i), nx = -dy * hw, ny = dx * hw;
            let ax = a[0], ay = a[1], bx = b[0], by = b[1];
            if (!closed && st.cap === 'square') { if (i === 0) { ax -= dx * hw; ay -= dy * hw; } if (i === nseg - 1) { bx += dx * hw; by += dy * hw; } }
            emit([ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny]);
        }
        const joins = closed ? p.length : p.length - 2;
        for (let k = 0; k < joins; k++) {
            const i = closed ? k : k + 1, i0 = (i - 1 + p.length) % p.length, v = p[i];
            const d0 = dir(i0), d1 = dir(i), cr = d0[0] * d1[1] - d0[1] * d1[0], dt = d0[0] * d1[0] + d0[1] * d1[1];
            if (Math.abs(cr) < 1e-9 && dt > 0) continue;
            if (st.join === 'round') { circle(v[0], v[1]); continue; }
            const sgn = cr > 0 ? -1 : 1;
            const o0 = [v[0] - d0[1] * hw * sgn, v[1] + d0[0] * hw * sgn], o1 = [v[0] - d1[1] * hw * sgn, v[1] + d1[0] * hw * sgn];
            const theta = Math.acos(Math.max(-1, Math.min(1, -dt)));
            if (st.join === 'miter' && theta > 1e-9 && 1 / Math.sin(theta / 2) <= st.miter) {
                const ml = hw / Math.sin(theta / 2), bx = (o0[0] + o1[0]) / 2 - v[0], by = (o0[1] + o1[1]) / 2 - v[1], bl = Math.hypot(bx, by);
                if (bl > 1e-12) { emit([v[0], v[1], o0[0], o0[1], v[0] + bx / bl * ml, v[1] + by / bl * ml, o1[0], o1[1]]); continue; }
            }
            emit([v[0], v[1], o0[0], o0[1], o1[0], o1[1]]);
        }
        if (!closed && st.cap === 'round') { circle(p[0][0], p[0][1]); circle(p[p.length - 1][0], p[p.length - 1][1]); }
    }
    return out;
}
function dashSubs(subs, dash, off) {
    const pat = dash.length % 2 ? dash.concat(dash) : dash, total = pat.reduce((a, b) => a + b, 0), out = [];
    for (const s of subs) {
        const pts = s.closed && s.pts.length > 1 ? s.pts.concat([s.pts[0]]) : s.pts;
        let pos = ((off % total) + total) % total, k = 0;
        while (pos >= pat[k]) { pos -= pat[k]; k = (k + 1) % pat.length; }
        let left = pat[k] - pos, on = k % 2 === 0, cur = on ? { pts: [pts[0]], closed: false, zero: true } : null;
        for (let i = 1; i < pts.length; i++) {
            let [ax, ay] = pts[i - 1]; const [bx, by] = pts[i];
            let seg = Math.hypot(bx - ax, by - ay);
            while (seg > 0) {
                const step = Math.min(seg, left), f = step / seg, nx = ax + (bx - ax) * f, ny = ay + (by - ay) * f;
                if (on) cur.pts.push([nx, ny]);
                ax = nx; ay = ny; seg -= step; left -= step;
                if (left <= 1e-12) {
                    if (on) out.push(cur);
                    k = (k + 1) % pat.length; left = pat[k]; on = !on;
                    cur = on ? { pts: [[ax, ay]], closed: false, zero: true } : null;
                }
            }
        }
        if (on && cur && cur.pts.length > 1) out.push(cur);
    }
    return out;
}
function windingAt(polys, x, y, eo) {
    let w = 0;
    for (const q of polys) {
        const n = q.length / 2;
        for (let i = 0; i < n; i++) {
            const j = (i + 1) % n, x0 = q[2 * i], y0 = q[2 * i + 1], x1 = q[2 * j], y1 = q[2 * j + 1];
            const cr = (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0);
            if (Math.abs(cr) < 1e-9 && x >= Math.min(x0, x1) - 1e-9 && x <= Math.max(x0, x1) + 1e-9 && y >= Math.min(y0, y1) - 1e-9 && y <= Math.max(y0, y1) + 1e-9) return true;
            if (y0 <= y) { if (y1 > y && cr > 0) w++; } else if (y1 <= y && cr < 0) w--;
        }
    }
    return eo ? w % 2 !== 0 : w !== 0;
}
const fillRule = r => { if (r === undefined) return false; r = String(r); if (r === 'nonzero') return false; if (r === 'evenodd') return true; throw new TypeError(`The provided value '${r}' is not a valid enum value of type CanvasFillRule.`); };
/* paint a shape with shadow, alpha, composite operation and clip */
function c2dDraw(s, shape, paint) {
    if (!paint || !s.w || !s.h) return;
    const st = s.st;
    if (st.sColorV[3] > 0 && (st.sBlur > 0 || st.sx || st.sy)) {
        /* the shape and its paint moved by the shadow offset (the shape itself may lie off the canvas) */
        const ox = st.sx, oy = st.sy, tmp = new Uint32Array(s.w * s.h);
        let sh;
        if (typeof shape[0] === 'string') { sh = shape.slice(); sh[5] += ox; sh[6] += oy; }
        else { const xy = shape[0].slice(); for (let i = 0; i < xy.length; i += 2) { xy[i] += ox; xy[i + 1] += oy; } sh = [xy, shape[1], shape[2]]; }
        let pt = paint;
        if (paint[2]) { const pr = paint[2].slice(); pr[4] -= pr[0] * ox + pr[2] * oy; pr[5] -= pr[1] * ox + pr[3] * oy; pt = paint.slice(); pt[2] = pr; }
        C2N.c2dFill(tmp, s.w, s.h, sh, 1, 0, null, pt);
        const cov = new Float32Array(s.w * s.h);
        for (let i = 0; i < cov.length; i++) cov[i] = (tmp[i] >>> 24) / 255;
        if (st.sBlur > 0) boxBlur(cov, s.w, s.h, st.sBlur / 2);
        const c = st.sColorV;
        C2N.c2dFill(s.px, s.w, s.h, cov, st.alpha, st.op, st.clip, [0, premul(c[0], c[1], c[2], c[3]), null, null, null, 0, 0, 0]);
    }
    C2N.c2dFill(s.px, s.w, s.h, shape, st.alpha, st.op, st.clip, paint);
    c2dPresent(s);
}
function boxBlur(a, w, h, sigma) {
    const r = Math.max(1, Math.round(sigma * Math.sqrt(3) * 0.8)), tmp = new Float32Array(Math.max(w, h));
    for (let pass = 0; pass < 3; pass++) {
        for (let y = 0; y < h; y++) {
            let acc = 0; const o = y * w;
            for (let x = -r; x <= r; x++) acc += x >= 0 && x < w ? a[o + x] : 0;
            for (let x = 0; x < w; x++) { tmp[x] = acc / (2 * r + 1); const add = x + r + 1, sub = x - r; acc += (add < w ? a[o + add] : 0) - (sub >= 0 ? a[o + sub] : 0); }
            a.set(tmp.subarray(0, w), o);
        }
        for (let x = 0; x < w; x++) {
            let acc = 0;
            for (let y = -r; y <= r; y++) acc += y >= 0 && y < h ? a[y * w + x] : 0;
            for (let y = 0; y < h; y++) { tmp[y] = acc / (2 * r + 1); const add = y + r + 1, sub = y - r; acc += (add < h ? a[add * w + x] : 0) - (sub >= 0 ? a[sub * w + x] : 0); }
            for (let y = 0; y < h; y++) a[y * w + x] = tmp[y];
        }
    }
}
const ENUMS = { cap: ['butt', 'round', 'square'], join: ['round', 'bevel', 'miter'], align: ['start', 'end', 'left', 'right', 'center'],
    base: ['top', 'hanging', 'middle', 'alphabetic', 'ideographic', 'bottom'], dir: ['ltr', 'rtl', 'inherit'], smoothQ: ['low', 'medium', 'high'],
    kerning: ['auto', 'normal', 'none'], stretch: FONT_STRETCH, caps: ['normal', 'small-caps', 'all-small-caps', 'petite-caps', 'all-petite-caps', 'unicase', 'titling-caps'],
    rendering: ['auto', 'optimizeSpeed', 'optimizeLegibility', 'geometricPrecision'] };
const C2D_PROPS = {
    lineWidth: ['lw', v => { v = +v; return v > 0 && Number.isFinite(v) ? v : undefined; }],
    miterLimit: ['miter', v => { v = +v; return v > 0 && Number.isFinite(v) ? v : undefined; }],
    lineDashOffset: ['dashOff', v => { v = +v; return Number.isFinite(v) ? v : undefined; }],
    globalAlpha: ['alpha', v => { v = +v; return v >= 0 && v <= 1 ? v : undefined; }],
    shadowBlur: ['sBlur', v => { v = +v; return v >= 0 && Number.isFinite(v) ? v : undefined; }],
    shadowOffsetX: ['sx', v => { v = +v; return Number.isFinite(v) ? v : undefined; }],
    shadowOffsetY: ['sy', v => { v = +v; return Number.isFinite(v) ? v : undefined; }],
    imageSmoothingEnabled: ['smooth', v => !!v],
    filter: ['filter', v => String(v)],
    lang: ['lang', v => String(v)],
    lineCap: ['cap'], lineJoin: ['join'], textAlign: ['align'], textBaseline: ['base'], direction: ['dir'], imageSmoothingQuality: ['smoothQ'],
    fontKerning: ['kerning'], fontStretch: ['stretch'], fontVariantCaps: ['caps'], textRendering: ['rendering'],
    letterSpacing: ['letterSpacing', v => { const m = /^\s*(-?[\d.]+)(px|em|rem|pt)?\s*$/.exec(String(v)); return m && (m[2] || +m[1] === 0) ? colorNum3(+m[1]) + (m[2] || 'px') : undefined; }],
    wordSpacing: ['wordSpacing', v => { const m = /^\s*(-?[\d.]+)(px|em|rem|pt)?\s*$/.exec(String(v)); return m && (m[2] || +m[1] === 0) ? colorNum3(+m[1]) + (m[2] || 'px') : undefined; }],
};
const c2dRect = (m, x, y, w, h) => { const p = new PathData(); p.rect(m, x, y, w, h); return p; };
const c2dText = (s, text, x, y, maxW, which) => {
    const st = s.st, f = st.fontD;
    const t = String(text).replace(/[\t\n\f\r]/g, ' ').replace(/\0/g, '');
    if (maxW !== undefined && !(+maxW > 0)) return;
    if (!fin(+x, +y)) return;
    const [w, asc, desc] = C2N.c2dMeasure(t, f.family, f.weight, f.italic, f.size);
    const rtl = st.dir === 'rtl';
    let ax = +x;
    const al = st.align === 'start' ? (rtl ? 'right' : 'left') : st.align === 'end' ? (rtl ? 'left' : 'right') : st.align;
    const k = maxW !== undefined && w > +maxW ? +maxW / w : 1;
    if (al === 'right') ax -= w * k; else if (al === 'center') ax -= w * k / 2;
    let by = +y;
    switch (st.base) {
    case 'top': by += asc; break;
    case 'hanging': by += asc * 0.8; break;
    case 'middle': by += (asc - desc) / 2; break;
    case 'ideographic': case 'bottom': by -= desc; break;
    }
    const m = st.m, [dx, dy] = mApply(m, ax, by), sx = Math.hypot(m[0], m[1]) * k, sy = Math.hypot(m[2], m[3]);
    c2dDraw(s, [t, f.family, f.weight, f.italic, f.size, dx, dy, sx, sy], c2dPaintOf(s, which));
};
const C2D_METHODS = {
    get canvas() { return this._.target; },
    getContextAttributes() { return { ...this._.opts }; },
    isContextLost() { return false; },
    save() { const s = this._; s.stack.push({ ...s.st, dash: s.st.dash.slice() }); },
    restore() { const s = this._, L = s.layers && s.layers[s.layers.length - 1]; if (s.stack.length > (L ? L.depth : 0)) s.st = s.stack.pop(); },
    /* layers: drawing goes to a transparent bitmap that endLayer composites with the alpha, operation, shadow
       and clip in effect at beginLayer */
    beginLayer(opts) {
        const s = this._;
        if (opts != null && typeof opts !== 'object') throw new TypeError("Failed to execute 'beginLayer': The provided value is not of type 'CanvasLayerOptions'.");
        for (const f of [].concat(opts && opts.filter != null && typeof opts.filter === 'object' ? opts.filter : [])) {
            if (!f || f.name !== 'gaussianBlur') continue;   /* stdDeviation: a finite number or a pair of them */
            const d = f.stdDeviation, ok = x => typeof x === 'number' ? Number.isFinite(x) : false;
            if (!(ok(d) || (Array.isArray(d) && d.length === 2 && d.every(ok)))) throw new TypeError("Failed to execute 'beginLayer': Failed to construct the filter: 'stdDeviation' must be a finite number or a pair of them.");
        }
        C2D_METHODS.save.call(this);
        (s.layers || (s.layers = [])).push({ px: s.px, st: s.st, depth: s.stack.length });
        s.px = new Uint32Array(s.w * s.h);
        s.st = { ...s.st, alpha: 1, op: 0, sColor: 'rgba(0, 0, 0, 0)', sColorV: [0, 0, 0, 0], sBlur: 0, sx: 0, sy: 0, clip: null, filter: 'none', dash: s.st.dash.slice() };
    },
    endLayer() {
        const s = this._, L = s.layers && s.layers.pop();
        if (!L) throw new DOMException("Failed to execute 'endLayer' on 'CanvasRenderingContext2D': No active layer.", 'InvalidStateError');
        const layer = s.px;
        s.stack.length = L.depth;
        s.px = L.px; s.st = { ...L.st, m: ID };
        const pr = new Float32Array(12); pr.set(ID, 0);
        c2dDraw(s, pathShape(c2dRect(ID, 0, 0, s.w, s.h), false), [4, 0, pr, null, layer, s.w, s.h, 0]);
        C2D_METHODS.restore.call(this);
    },
    reset() { const s = this._; if (s.layers && s.layers.length) { s.px = s.layers[0].px; s.layers = []; } s.px.fill(s.alpha ? 0 : 0xff000000); s.st = c2dDefaults(); s.stack = []; s.path = new PathData(); c2dPresent(s); },
    _reset(w, h) { const s = this._; s.layers = []; c2dAlloc(s, w, h); s.st = c2dDefaults(); s.stack = []; s.path = new PathData(); c2dPresent(s); },
    _m() { return this._.st.m; },
    get _p() { return this._.path; },
    scale(x, y) { x = +x; y = +y; if (fin(x, y)) this._.st.m = mMul(this._.st.m, [x, 0, 0, y, 0, 0]); },
    rotate(a) { a = +a; if (fin(a)) this._.st.m = mMul(this._.st.m, [Math.cos(a), Math.sin(a), -Math.sin(a), Math.cos(a), 0, 0]); },
    translate(x, y) { x = +x; y = +y; if (fin(x, y)) this._.st.m = mMul(this._.st.m, [1, 0, 0, 1, x, y]); },
    transform(a, b, c, d, e, f) { const v = [a, b, c, d, e, f].map(Number); if (fin(...v)) this._.st.m = mMul(this._.st.m, v); },
    setTransform(a, b, c, d, e, f) {
        let v;
        if (arguments.length <= 1) v = c2dMatrixInit(a === undefined ? {} : a);
        else v = [a, b, c, d, e, f].map(Number);
        if (fin(...v)) this._.st.m = v;
    },
    resetTransform() { this._.st.m = ID; },
    getTransform() { const m = this._.st.m; return typeof DOMMatrix !== 'undefined' ? new DOMMatrix(m) : { a: m[0], b: m[1], c: m[2], d: m[3], e: m[4], f: m[5], is2D: true }; },
    get fillStyle() { return this._.st.fill; },
    set fillStyle(v) { this._setStyle('fill', v); },
    get strokeStyle() { return this._.st.stroke; },
    set strokeStyle(v) { this._setStyle('stroke', v); },
    get shadowColor() { return this._.st.sColor; },
    set shadowColor(v) { const c = c2dColor(v, this._.target instanceof C2Node ? this._.target : null, this._.cs); if (c) { this._.st.sColor = c.s; this._.st.sColorV = c.v; } },
    _setStyle(k, v) {
        const st = this._.st;
        if (v instanceof CanvasGradient || v instanceof CanvasPattern) { st[k] = v; return; }
        const c = c2dColor(v, this._.target instanceof C2Node ? this._.target : null, this._.cs);
        if (c) { st[k] = c.s; st[k + 'V'] = c.v; }
    },
    get globalCompositeOperation() { return C2D_OPS[this._.st.op]; },
    set globalCompositeOperation(v) { const i = C2D_OPS.indexOf(String(v)); if (i >= 0) this._.st.op = i; },
    get font() { return this._.st.font; },
    set font(v) { const f = c2dFont(v, this._.target instanceof C2Node ? this._.target : null); if (f) { this._.st.font = f.s; this._.st.fontD = f; } },
    getLineDash() { return this._.st.dash.slice(); },
    setLineDash(d) {
        if (d === null || typeof d !== 'object' || typeof d[Symbol.iterator] !== 'function') throw new TypeError("Failed to execute 'setLineDash' on 'CanvasRenderingContext2D': The provided value cannot be converted to a sequence.");
        const a = Array.from(d, Number);
        if (a.some(x => !Number.isFinite(x) || x < 0)) return;
        this._.st.dash = a.length % 2 ? a.concat(a) : a;
    },
    beginPath() { this._.path = new PathData(); },
    fill(a, b) {
        const s = this._;
        const path = a instanceof Path2D ? a._p.transformed(s.st.m) : s.path, eo = fillRule(a instanceof Path2D ? b : a);
        const sh = pathShape(path, eo);
        if (sh || [1, 2, 5, 7, 9].includes(s.st.op)) c2dDraw(s, sh || [new Float32Array(0), new Int32Array(1), false], c2dPaintOf(s, 'fill'));
    },
    stroke(a) {
        const s = this._, path = a instanceof Path2D ? a._p.transformed(s.st.m) : s.path;
        const sh = polysShape(strokePolys(path, s.st));
        if (sh) c2dDraw(s, sh, c2dPaintOf(s, 'stroke'));
    },
    clip(a, b) {
        const s = this._, path = a instanceof Path2D ? a._p.transformed(s.st.m) : s.path, eo = fillRule(a instanceof Path2D ? b : a);
        const mask = s.st.clip ? s.st.clip.slice() : new Float32Array(s.w * s.h).fill(1);
        const sh = pathShape(path, eo);
        if (sh) C2N.c2dClip(mask, s.w, s.h, sh); else mask.fill(0);
        s.st.clip = mask;
    },
    isPointInPath(a, b, c, d) {
        const s = this._, isP = a instanceof Path2D, path = isP ? a._p.transformed(s.st.m) : s.path, x = +(isP ? b : a), y = +(isP ? c : b);
        const eo = fillRule(isP ? d : c);
        if (!fin(x, y)) return false;
        return windingAt(path.sub.filter(q => q.pts.length >= 6).map(q => q.pts), x, y, eo);
    },
    isPointInStroke(a, b, c) {
        const s = this._, isP = a instanceof Path2D, path = isP ? a._p.transformed(s.st.m) : s.path, x = +(isP ? b : a), y = +(isP ? c : b);
        if (!fin(x, y)) return false;
        return windingAt(strokePolys(path, s.st), x, y, false);
    },
    fillRect(x, y, w, h) {
        const v = [x, y, w, h].map(Number); if (!fin(...v) || (!v[2] && !v[3])) return;
        const s = this._; c2dDraw(s, pathShape(c2dRect(s.st.m, ...v), false) || [new Float32Array(0), new Int32Array(1), false], c2dPaintOf(s, 'fill'));
    },
    strokeRect(x, y, w, h) {
        const v = [x, y, w, h].map(Number); if (!fin(...v)) return;
        const s = this._, p = new PathData();
        if (!v[2] && !v[3]) { if (s.st.join !== 'round') return; p.move(s.st.m, v[0], v[1]); p.line(s.st.m, v[0], v[1]); }
        else if (!v[2] || !v[3]) { p.move(s.st.m, v[0], v[1]); p.line(s.st.m, v[0] + v[2], v[1] + v[3]); }
        else p.rect(s.st.m, ...v);
        const sh = polysShape(strokePolys(p, s.st));
        if (sh) c2dDraw(s, sh, c2dPaintOf(s, 'stroke'));
    },
    clearRect(x, y, w, h) {
        const v = [x, y, w, h].map(Number); if (!fin(...v)) return;
        const s = this._, sh = pathShape(c2dRect(s.st.m, ...v), false);
        if (!sh || !s.w || !s.h) return;
        C2N.c2dFill(s.px, s.w, s.h, sh, 1, s.alpha ? 6 : 0, s.st.clip, [0, s.alpha ? 0xff000000 : 0xff000000, null, null, null, 0, 0, 0]);
        c2dPresent(s);
    },
    fillText(t, x, y, maxW) { c2dText(this._, t, x, y, maxW, 'fill'); },
    strokeText(t, x, y, maxW) { c2dText(this._, t, x, y, maxW, 'stroke'); },
    measureText(text) {
        const f = this._.st.fontD, t = String(text).replace(/[\t\n\f\r]/g, ' ').replace(/\0/g, '');
        const [w, asc, desc] = C2N.c2dMeasure(t, f.family, f.weight, f.italic, f.size);
        const al = this._.st.align, left = al === 'center' ? w / 2 : al === 'right' || (al === 'end' && this._.st.dir !== 'rtl') ? w : 0;
        let base = 0;
        switch (this._.st.base) { case 'top': base = asc; break; case 'hanging': base = asc * 0.8; break; case 'middle': base = (asc - desc) / 2; break; case 'ideographic': case 'bottom': base = -desc; break; }
        return new TextMetrics(C2D_KEY, { text: t, font: f, width: w, actualBoundingBoxLeft: left, actualBoundingBoxRight: w - left, fontBoundingBoxAscent: asc - base, fontBoundingBoxDescent: desc + base,
            actualBoundingBoxAscent: (t.trim() ? asc * 0.9 : 0) - base, actualBoundingBoxDescent: (t.trim() ? desc * 0.5 : 0) + base, emHeightAscent: f.size * asc / (asc + desc) - base,
            emHeightDescent: f.size * desc / (asc + desc) + base, hangingBaseline: asc * 0.8 - base, alphabeticBaseline: -base, ideographicBaseline: -desc - base });
    },
    drawImage(img, ...a) {
        if (![2, 4, 8].includes(a.length)) throw new TypeError(`Failed to execute 'drawImage' on 'CanvasRenderingContext2D': Valid arities are: [3, 5, 9], but ${a.length + 1} arguments provided.`);
        const src = c2dImage(img, 'drawImage');
        if (!src) return;
        const v = a.length === 8 ? a.map(Number) : [0, 0, src.sw, src.sh, +a[0], +a[1], a.length === 4 ? +a[2] : src.sw, a.length === 4 ? +a[3] : src.sh];
        c2dBlit(this._, src, ...v);
    },
    createLinearGradient(x0, y0, x1, y1) {
        const v = [x0, y0, x1, y1].map((x, i) => toDouble(x, `x${i}`));
        return new CanvasGradient(C2D_KEY, 1, v.concat([0, 0]), this._.cs);
    },
    createRadialGradient(x0, y0, r0, x1, y1, r1) {
        const v = [x0, y0, r0, x1, y1, r1].map(x => toDouble(x));
        if (v[2] < 0 || v[5] < 0) throw c2dIndex(`The ${v[2] < 0 ? 'r0' : 'r1'} provided is less than 0.`);
        return new CanvasGradient(C2D_KEY, 2, v, this._.cs);
    },
    createConicGradient(a, x, y) { const v = [a, x, y].map(z => toDouble(z)); return new CanvasGradient(C2D_KEY, 3, [v[1], v[2], v[0], 0, 0, 0], this._.cs); },
    createPattern(img, rep) {
        const src = c2dImage(img, 'createPattern');
        rep = rep === null ? '' : String(rep);
        if (!['', 'repeat', 'repeat-x', 'repeat-y', 'no-repeat'].includes(rep)) throw new DOMException(`The provided type ('${rep}') is not one of 'repeat', 'no-repeat', 'repeat-x', or 'repeat-y'.`, 'SyntaxError');
        if (!src) return null;
        const px = c2dConvert(src.px, src.cs, this._.cs);
        return new CanvasPattern(C2D_KEY, { px: px === src.px ? px.slice() : px, w: src.w, h: src.h, sw: src.sw, sh: src.sh }, rep || 'repeat');
    },
    createImageData(a, b, c) {
        if (a instanceof ImageData) return new ImageData(a.width, a.height, { colorSpace: a.colorSpace });
        if (arguments.length < 2) throw new TypeError("Failed to execute 'createImageData': 2 arguments required.");
        const w = erLong(a), h = erLong(b);
        if (!w || !h) throw c2dIndex(`The source ${w ? 'height' : 'width'} is 0.`);
        return new ImageData(Math.abs(w), Math.abs(h), c2dDataSettings(c, this._.cs));
    },
    getImageData(x, y, w, h, settings) {
        if (arguments.length < 4) throw new TypeError(`Failed to execute 'getImageData': 4 arguments required, but only ${arguments.length} present.`);
        x = erLong(x); y = erLong(y); w = erLong(w); h = erLong(h);
        if (!w || !h) throw c2dIndex(`The source ${w ? 'height' : 'width'} is 0.`);
        if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
        const s = this._;
        if (s.tainted) throw new DOMException('The canvas has been tainted by cross-origin data.', 'SecurityError');
        const o = new ImageData(w, h, c2dDataSettings(settings, s.cs)), d = o.data;
        for (let yy = 0; yy < h; yy++) {
            const sy = y + yy; if (sy < 0 || sy >= s.h) continue;
            for (let xx = 0; xx < w; xx++) {
                const sx = x + xx; if (sx < 0 || sx >= s.w) continue;
                const v = s.px[sy * s.w + sx], a = v >>> 24, j = (yy * w + xx) * 4;
                if (!a) continue;
                d[j] = Math.round(((v >>> 16) & 255) * 255 / a); d[j + 1] = Math.round(((v >>> 8) & 255) * 255 / a); d[j + 2] = Math.round((v & 255) * 255 / a); d[j + 3] = a;
            }
        }
        c2dConvertRGBA(d, s.cs, o.colorSpace);
        return o;
    },
    putImageData(img, dx, dy, dirtyX, dirtyY, dirtyW, dirtyH) {
        if (!(img instanceof ImageData)) throw new TypeError("Failed to execute 'putImageData' on 'CanvasRenderingContext2D': parameter 1 is not of type 'ImageData'.");
        dx = erLong(dx); dy = erLong(dy);
        const d = img.data;
        if (d.buffer.detached || (!d.length && img.width)) throw new DOMException('The source data has been detached.', 'InvalidStateError');
        let x0 = 0, y0 = 0, w = img.width, h = img.height;
        if (arguments.length >= 7) {
            x0 = erLong(dirtyX); y0 = erLong(dirtyY); w = erLong(dirtyW); h = erLong(dirtyH);
            if (w < 0) { x0 += w; w = -w; } if (h < 0) { y0 += h; h = -h; }
            if (x0 < 0) { w += x0; x0 = 0; } if (y0 < 0) { h += y0; y0 = 0; }
            w = Math.min(w, img.width - x0); h = Math.min(h, img.height - y0);
        }
        const s = this._, f = (img.colorSpace || 'srgb') !== s.cs ? c2dXf(s.cs, img.colorSpace || 'srgb') : null;
        for (let yy = y0; yy < y0 + h; yy++) {
            const ty = dy + yy; if (ty < 0 || ty >= s.h) continue;
            for (let xx = x0; xx < x0 + w; xx++) {
                const tx = dx + xx; if (tx < 0 || tx >= s.w) continue;
                const j = (yy * img.width + xx) * 4, a = s.alpha ? d[j + 3] : 255;
                let r = d[j], g = d[j + 1], b = d[j + 2];
                if (f && a) { const q = f(r, g, b); r = q >>> 16; g = (q >>> 8) & 255; b = q & 255; }
                s.px[ty * s.w + tx] = ((a << 24) | (Math.round(r * a / 255) << 16) | (Math.round(g * a / 255) << 8) | Math.round(b * a / 255)) >>> 0;
            }
        }
        c2dPresent(s);
    },
    drawFocusIfNeeded() {},
    scrollPathIntoView() {},
};
for (const [k, [f, conv]] of Object.entries(C2D_PROPS))
    Object.defineProperty(C2D_METHODS, k, {
        get() { return this._.st[f]; },
        set(v) { const x = conv ? conv(v) : ENUMS[f].includes(String(v)) ? String(v) : undefined; if (x !== undefined) this._.st[f] = x; },
        enumerable: true, configurable: true,
    });
Object.assign(C2D_METHODS, PATH_METHODS);
const c2dInstall = (C, skip = []) => {
    for (const k of Reflect.ownKeys(C2D_METHODS)) {
        if (skip.includes(k)) continue;
        const d = Object.getOwnPropertyDescriptor(C2D_METHODS, k);
        if (k[0] === '_') d.enumerable = false;
        else if ('value' in d) { d.enumerable = true; d.writable = true; }
        d.configurable = true;
        Object.defineProperty(C.prototype, k, d);
    }
};
class CanvasRenderingContext2D { constructor(k, cv, o) { if (k !== C2D_KEY) c2dIllegal(); def(this, '_', c2dState(cv, cv.width, cv.height, o)); } }
c2dInstall(CanvasRenderingContext2D);
class OffscreenCanvasRenderingContext2D { constructor(k, cv, o) { if (k !== C2D_KEY) c2dIllegal(); def(this, '_', c2dState(cv, cv.width, cv.height, o)); } }
c2dInstall(OffscreenCanvasRenderingContext2D, ['drawFocusIfNeeded', 'scrollPathIntoView']);
const c2dCreate = (cv, o) => new CanvasRenderingContext2D(C2D_KEY, cv, o);

/* ---- PNG encoding (stored deflate blocks) for toDataURL / toBlob / convertToBlob ---- */
const CRC_T = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
const crc32 = (b, s = 0, e = b.length) => { let c = ~0; for (let i = s; i < e; i++) c = CRC_T[(c ^ b[i]) & 255] ^ (c >>> 8); return ~c >>> 0; };
function c2dPng(px, w, h) {
    const raw = new Uint8Array((w * 4 + 1) * h);
    for (let y = 0, o = 0; y < h; y++) {
        raw[o++] = 0;
        for (let x = 0; x < w; x++) {
            const v = px[y * w + x], a = v >>> 24;
            raw[o++] = a ? Math.round(((v >>> 16) & 255) * 255 / a) : 0; raw[o++] = a ? Math.round(((v >>> 8) & 255) * 255 / a) : 0; raw[o++] = a ? Math.round((v & 255) * 255 / a) : 0; raw[o++] = a;
        }
    }
    const nb = Math.max(1, Math.ceil(raw.length / 65535)), z = new Uint8Array(2 + raw.length + nb * 5 + 4);
    z[0] = 0x78; z[1] = 0x01;
    let o = 2, a1 = 1, a2 = 0;
    for (let i = 0; i < raw.length; i++) { a1 = (a1 + raw[i]) % 65521; a2 = (a2 + a1) % 65521; }
    for (let b = 0; b < nb; b++) {
        const s = b * 65535, n = Math.min(65535, raw.length - s);
        z[o++] = b === nb - 1 ? 1 : 0; z[o++] = n & 255; z[o++] = n >> 8; z[o++] = ~n & 255; z[o++] = (~n >> 8) & 255;
        z.set(raw.subarray(s, s + n), o); o += n;
    }
    const ad = ((a2 << 16) | a1) >>> 0; z[o++] = ad >>> 24; z[o++] = (ad >>> 16) & 255; z[o++] = (ad >>> 8) & 255; z[o++] = ad & 255;
    const chunks = [];
    const chunk = (type, data) => {
        const c = new Uint8Array(12 + data.length), dv = new DataView(c.buffer);
        dv.setUint32(0, data.length); for (let i = 0; i < 4; i++) c[4 + i] = type.charCodeAt(i);
        c.set(data, 8); dv.setUint32(8 + data.length, crc32(c, 4, 8 + data.length)); chunks.push(c);
    };
    const ih = new Uint8Array(13), dv = new DataView(ih.buffer); dv.setUint32(0, w); dv.setUint32(4, h); ih[8] = 8; ih[9] = 6;
    chunk('IHDR', ih); chunk('IDAT', z.subarray(0, o)); chunk('IEND', new Uint8Array(0));
    const sig = [137, 80, 78, 71, 13, 10, 26, 10], tot = 8 + chunks.reduce((n, c) => n + c.length, 0), out = new Uint8Array(tot);
    out.set(sig, 0); let p = 8; for (const c of chunks) { out.set(c, p); p += c.length; }
    return out;
}
const c2dPixels = cv => {
    const s = cv.__ck === '2d' ? cv.__cx._ : null;
    if (s) return { px: s.px, w: s.w, h: s.h, tainted: s.tainted };
    const r = cv instanceof C2Node ? C2N.c2dSource(cv) : null;
    return r ? { px: new Uint32Array(r[2]), w: r[0], h: r[1] } : { px: new Uint32Array(cv.width * cv.height), w: cv.width, h: cv.height };
};
function canvasDataURL(cv) {
    if (!cv.width || !cv.height) return 'data:,';
    const p = c2dPixels(cv);
    if (p.tainted) throw new DOMException('Tainted canvases may not be exported.', 'SecurityError');
    const b = c2dPng(p.px, p.w, p.h);
    let s = ''; for (let i = 0; i < b.length; i += 32768) s += String.fromCharCode.apply(null, b.subarray(i, i + 32768));
    return 'data:image/png;base64,' + btoa(s);
}
function canvasBlob(cv) {
    if (!cv.width || !cv.height) return null;
    const p = c2dPixels(cv);
    if (p.tainted) throw new DOMException('Tainted canvases may not be exported.', 'SecurityError');
    return new Blob([c2dPng(p.px, p.w, p.h)], { type: 'image/png' });
}

/* ---- OffscreenCanvas ---- */
class OffscreenCanvas extends EventTarget {
    constructor(w, h) {
        super();
        if (arguments.length < 2) throw new TypeError(`Failed to construct 'OffscreenCanvas': 2 arguments required, but only ${arguments.length} present.`);
        def(this, '_', { w: erLong(w, 0, Number.MAX_SAFE_INTEGER), h: erLong(h, 0, Number.MAX_SAFE_INTEGER), kind: null, cx: null });
    }
    get width() { return this._.w; }
    set width(v) { this._.w = erLong(v, 0, Number.MAX_SAFE_INTEGER); if (this._.kind === '2d') this._.cx._reset(this._.w, this._.h); }
    get height() { return this._.h; }
    set height(v) { this._.h = erLong(v, 0, Number.MAX_SAFE_INTEGER); if (this._.kind === '2d') this._.cx._reset(this._.w, this._.h); }
    getContext(t, o) {
        if (arguments.length < 1) throw new TypeError("Failed to execute 'getContext' on 'OffscreenCanvas': 1 argument required, but only 0 present.");
        t = String(t);
        if (!['2d', 'webgl', 'webgl2', 'webgpu', 'bitmaprenderer'].includes(t)) throw new TypeError(`The provided value '${t}' is not a valid enum value of type OffscreenRenderingContextId.`);
        if (this._.detached) throw new DOMException('OffscreenCanvas object is detached.', 'InvalidStateError');
        if (this._.kind) return this._.kind === t ? this._.cx : null;
        if (t !== '2d') return null;
        this._.kind = '2d'; this._.cx = new OffscreenCanvasRenderingContext2D(C2D_KEY, this, o);
        return this._.cx;
    }
    get __ck() { return this._.kind; }
    get __cx() { return this._.cx; }
    transferToImageBitmap() {
        if (this._.detached) throw new DOMException('OffscreenCanvas object is detached.', 'InvalidStateError');
        if (!this._.kind) throw new DOMException('Cannot transfer an ImageBitmap from an OffscreenCanvas with no context', 'InvalidStateError');
        const d = this._.w && this._.h ? this._.cx.getImageData(0, 0, this._.w, this._.h) : null;
        const o = Object.create(C2IB.prototype);
        def(o, '_', { src: d, w: this._.w, h: this._.h });
        if (this._.kind === '2d') this._.cx._reset(this._.w, this._.h);
        return o;
    }
    convertToBlob(opts) {
        try {
            if (this._.detached) throw new DOMException('OffscreenCanvas object is detached.', 'InvalidStateError');
            if (!this._.w || !this._.h) throw new DOMException('The size of the OffscreenCanvas is zero.', 'IndexSizeError');
            return Promise.resolve(canvasBlob(this));
        } catch (e) { return Promise.reject(e); }
    }
}
