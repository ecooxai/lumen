/* Geometry Interfaces (DOMPoint, DOMRect, DOMQuad, DOMMatrix) for pages and workers. Matrices are stored as
   [m11, m12, m13, m14, m21, ..., m44]: element (row r, column c) of the math matrix is m[c * 4 + r]. */
const geoNum = v => Number(v);
const geoIllegal = n => { throw new TypeError(`Failed to construct '${n}': Illegal constructor`); };
const geoDict = (o, n) => {
    if (o === undefined || o === null) return {};
    if (typeof o !== 'object' && typeof o !== 'function') throw new TypeError(`The provided value is not of type '${n}'.`);
    return o;
};
let geoP, geoR, geoM;   /* private-field accessors (brand checks) */

class DOMPointReadOnly {
    #p;
    constructor(x = 0, y = 0, z = 0, w = 1) { this.#p = [geoNum(x), geoNum(y), geoNum(z), geoNum(w)]; }
    static { geoP = o => o.#p; }
    static fromPoint(o) { o = geoDict(o, 'DOMPointInit'); return new this(o.x ?? 0, o.y ?? 0, o.z ?? 0, o.w ?? 1); }
    get x() { return geoP(this)[0]; } get y() { return geoP(this)[1]; } get z() { return geoP(this)[2]; } get w() { return geoP(this)[3]; }
    matrixTransform(m) { return DOMMatrixReadOnly.fromMatrix(m).transformPoint(this); }
    toJSON() { const [x, y, z, w] = geoP(this); return { x, y, z, w }; }
}
class DOMPoint extends DOMPointReadOnly {
    get x() { return geoP(this)[0]; } set x(v) { geoP(this)[0] = geoNum(v); }
    get y() { return geoP(this)[1]; } set y(v) { geoP(this)[1] = geoNum(v); }
    get z() { return geoP(this)[2]; } set z(v) { geoP(this)[2] = geoNum(v); }
    get w() { return geoP(this)[3]; } set w(v) { geoP(this)[3] = geoNum(v); }
}

const geoMin = (a, b) => Number.isNaN(a) || Number.isNaN(b) ? NaN : Math.min(a, b);
const geoMax = (a, b) => Number.isNaN(a) || Number.isNaN(b) ? NaN : Math.max(a, b);
class DOMRectReadOnly {
    #r;
    constructor(x = 0, y = 0, w = 0, h = 0) { this.#r = [geoNum(x), geoNum(y), geoNum(w), geoNum(h)]; }
    static { geoR = o => o.#r; }
    static fromRect(o) { o = geoDict(o, 'DOMRectInit'); return new this(o.x ?? 0, o.y ?? 0, o.width ?? 0, o.height ?? 0); }
    get x() { return geoR(this)[0]; } get y() { return geoR(this)[1]; } get width() { return geoR(this)[2]; } get height() { return geoR(this)[3]; }
    get top() { const r = geoR(this); return geoMin(r[1], r[1] + r[3]); }
    get right() { const r = geoR(this); return geoMax(r[0], r[0] + r[2]); }
    get bottom() { const r = geoR(this); return geoMax(r[1], r[1] + r[3]); }
    get left() { const r = geoR(this); return geoMin(r[0], r[0] + r[2]); }
    toJSON() { const { x, y, width, height, top, right, bottom, left } = this; return { x, y, width, height, top, right, bottom, left }; }
}
class DOMRect extends DOMRectReadOnly {
    get x() { return geoR(this)[0]; } set x(v) { geoR(this)[0] = geoNum(v); }
    get y() { return geoR(this)[1]; } set y(v) { geoR(this)[1] = geoNum(v); }
    get width() { return geoR(this)[2]; } set width(v) { geoR(this)[2] = geoNum(v); }
    get height() { return geoR(this)[3]; } set height(v) { geoR(this)[3] = geoNum(v); }
}

class DOMQuad {
    #q;
    constructor(p1, p2, p3, p4) { this.#q = [p1, p2, p3, p4].map(p => DOMPoint.fromPoint(p)); }
    static fromRect(o) { o = geoDict(o, 'DOMRectInit'); const x = geoNum(o.x ?? 0), y = geoNum(o.y ?? 0), w = geoNum(o.width ?? 0), h = geoNum(o.height ?? 0); return new DOMQuad({ x, y }, { x: x + w, y }, { x: x + w, y: y + h }, { x, y: y + h }); }
    static fromQuad(o) { o = geoDict(o, 'DOMQuadInit'); return new DOMQuad(o.p1, o.p2, o.p3, o.p4); }
    get p1() { return this.#q[0]; } get p2() { return this.#q[1]; } get p3() { return this.#q[2]; } get p4() { return this.#q[3]; }
    getBounds() {
        const xs = this.#q.map(p => p.x), ys = this.#q.map(p => p.y);
        const l = xs.reduce(geoMin), t = ys.reduce(geoMin);
        return new DOMRect(l, t, xs.reduce(geoMax) - l, ys.reduce(geoMax) - t);
    }
    toJSON() { const [p1, p2, p3, p4] = this.#q; return { p1, p2, p3, p4 }; }
}

const GEO_ID = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const GEO_NAMES = ['m11', 'm12', 'm13', 'm14', 'm21', 'm22', 'm23', 'm24', 'm31', 'm32', 'm33', 'm34', 'm41', 'm42', 'm43', 'm44'];
const GEO_ALIAS = { a: 0, b: 1, c: 4, d: 5, e: 12, f: 13 };
const GEO_3D = [2, 3, 6, 7, 8, 9, 11, 14];   /* indices that must be 0 (and m33, m44 = 1) for a 2D matrix */
const geoMul = (A, B) => { const C = new Array(16); for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) { let s = 0; for (let k = 0; k < 4; k++) s += A[k * 4 + r] * B[c * 4 + k]; C[c * 4 + r] = s; } return C; };
const geoIs2D = m => GEO_3D.every(i => m[i] === 0) && m[10] === 1 && m[15] === 1;
const geoRad = d => d * Math.PI / 180;
const geoTrans = (x, y, z) => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1];
const geoScale = (x, y, z) => [x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1];
function geoRot(x, y, z, deg) {   /* rotation by deg degrees about the axis (x, y, z) */
    const len = Math.hypot(x, y, z);
    if (!len) return GEO_ID.slice();
    x /= len; y /= len; z /= len;
    if (x === 0 && y === 0) {
        const a = geoRad(deg) * Math.sign(z), c = Math.cos(a), s = Math.sin(a);
        return [c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    }
    const h = geoRad(deg) / 2, sc = Math.sin(h) * Math.cos(h), sq = Math.sin(h) ** 2;
    return [1 - 2 * (y * y + z * z) * sq, 2 * (x * y * sq + z * sc), 2 * (x * z * sq - y * sc), 0,
        2 * (x * y * sq - z * sc), 1 - 2 * (x * x + z * z) * sq, 2 * (y * z * sq + x * sc), 0,
        2 * (x * z * sq + y * sc), 2 * (y * z * sq - x * sc), 1 - 2 * (x * x + y * y) * sq, 0, 0, 0, 0, 1];
}
function geoInv(m) {
    const [a00, a01, a02, a03, a10, a11, a12, a13, a20, a21, a22, a23, a30, a31, a32, a33] = m;
    const b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11,
        b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12, b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30,
        b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    const det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (!det || !Number.isFinite(det)) return null;
    const k = 1 / det;
    return [(a11 * b11 - a12 * b10 + a13 * b09) * k, (a02 * b10 - a01 * b11 - a03 * b09) * k, (a31 * b05 - a32 * b04 + a33 * b03) * k, (a22 * b04 - a21 * b05 - a23 * b03) * k,
        (a12 * b08 - a10 * b11 - a13 * b07) * k, (a00 * b11 - a02 * b08 + a03 * b07) * k, (a32 * b02 - a30 * b05 - a33 * b01) * k, (a20 * b05 - a22 * b02 + a23 * b01) * k,
        (a10 * b10 - a11 * b08 + a13 * b06) * k, (a01 * b08 - a00 * b10 - a03 * b06) * k, (a30 * b04 - a31 * b02 + a33 * b00) * k, (a21 * b02 - a20 * b04 - a23 * b00) * k,
        (a11 * b07 - a10 * b09 - a12 * b06) * k, (a00 * b09 - a01 * b07 + a02 * b06) * k, (a31 * b01 - a30 * b03 - a32 * b00) * k, (a20 * b03 - a21 * b01 + a22 * b00) * k];
}
/* DOMMatrix2DInit / DOMMatrixInit "validate and fixup" -> [m (16), is2D] */
function geoFixup(o, only2D) {
    o = geoDict(o, only2D ? 'DOMMatrix2DInit' : 'DOMMatrixInit');
    const v = {};
    for (const k of ['a', 'b', 'c', 'd', 'e', 'f', ...GEO_NAMES, 'is2D']) { const x = o[k]; if (x !== undefined) v[k] = k === 'is2D' ? !!x : geoNum(x); }
    for (const [k, i] of Object.entries(GEO_ALIAS)) {
        const n = GEO_NAMES[i];
        if (k in v && n in v && !(v[k] === v[n] || (Number.isNaN(v[k]) && Number.isNaN(v[n])))) throw new TypeError(`Failed to execute 'fromMatrix': The '${k}' property should equal the '${n}' property.`);
    }
    const m = GEO_ID.slice();
    for (const [k, i] of Object.entries(GEO_ALIAS)) m[i] = v[GEO_NAMES[i]] ?? v[k] ?? m[i];
    if (only2D) return [m, true];
    for (const i of [...GEO_3D, 10, 15]) if (GEO_NAMES[i] in v) m[i] = v[GEO_NAMES[i]];
    const flat = geoIs2D(m);
    if (v.is2D === true && !flat) throw new TypeError("Failed to execute 'fromMatrix': The matrix is not 2D but 'is2D' is true.");
    return [m, v.is2D ?? flat];
}

/* a CSS <transform-list> (absolute lengths, angles) -> [m, is2D]; throws SyntaxError */
function geoParse(str) {
    const bad = () => { throw new DOMException(`Failed to parse '${str}'.`, 'SyntaxError'); };
    const s = String(str).trim();
    if (s === '' || /^none$/i.test(s)) return [GEO_ID.slice(), true];
    const LEN = { px: 1, cm: 96 / 2.54, mm: 96 / 25.4, q: 96 / 101.6, in: 96, pt: 4 / 3, pc: 16 };
    const ANG = { deg: 1, rad: 180 / Math.PI, grad: 0.9, turn: 360 };
    const num = /^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?/i;
    const val = (t, kind) => {
        t = t.trim(); const m = num.exec(t); if (!m) bad();
        const n = parseFloat(m[0]), u = t.slice(m[0].length).toLowerCase();
        if (!u) { if (kind === 'n' || n === 0) return n; bad(); }
        const f = (kind === 'l' ? LEN : kind === 'a' ? ANG : {})[u];
        return f === undefined ? bad() : n * f;
    };
    let m = GEO_ID.slice(), is2D = true, rest = s;
    const re = /^\s*([a-z0-9]+)\(([^()]*)\)\s*/i;
    while (rest) {
        const f = re.exec(rest); if (!f) bad();
        rest = rest.slice(f[0].length);
        const name = f[1].toLowerCase(), args = f[2].trim() === '' ? [] : f[2].split(',');
        const need = (lo, hi = lo) => { if (args.length < lo || args.length > hi) bad(); };
        const L = i => val(args[i], 'l'), A = i => val(args[i], 'a'), Nn = i => val(args[i], 'n');
        let t, d3 = false;
        switch (name) {
        case 'matrix': need(6); { const a = args.map((_, i) => Nn(i)); t = [a[0], a[1], 0, 0, a[2], a[3], 0, 0, 0, 0, 1, 0, a[4], a[5], 0, 1]; } break;
        case 'matrix3d': need(16); t = args.map((_, i) => Nn(i)); d3 = true; break;
        case 'translate': need(1, 2); t = geoTrans(L(0), args.length > 1 ? L(1) : 0, 0); break;
        case 'translatex': need(1); t = geoTrans(L(0), 0, 0); break;
        case 'translatey': need(1); t = geoTrans(0, L(0), 0); break;
        case 'translatez': need(1); t = geoTrans(0, 0, L(0)); d3 = true; break;
        case 'translate3d': need(3); t = geoTrans(L(0), L(1), L(2)); d3 = true; break;
        case 'scale': need(1, 2); t = geoScale(Nn(0), args.length > 1 ? Nn(1) : Nn(0), 1); break;
        case 'scalex': need(1); t = geoScale(Nn(0), 1, 1); break;
        case 'scaley': need(1); t = geoScale(1, Nn(0), 1); break;
        case 'scalez': need(1); t = geoScale(1, 1, Nn(0)); d3 = true; break;
        case 'scale3d': need(3); t = geoScale(Nn(0), Nn(1), Nn(2)); d3 = true; break;
        case 'rotate': case 'rotatez': need(1); t = geoRot(0, 0, 1, A(0)); d3 = name === 'rotatez'; break;
        case 'rotatex': need(1); t = geoRot(1, 0, 0, A(0)); d3 = true; break;
        case 'rotatey': need(1); t = geoRot(0, 1, 0, A(0)); d3 = true; break;
        case 'rotate3d': need(4); t = geoRot(Nn(0), Nn(1), Nn(2), A(3)); d3 = true; break;
        case 'skew': need(1, 2); t = [1, Math.tan(geoRad(args.length > 1 ? A(1) : 0)), 0, 0, Math.tan(geoRad(A(0))), 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; break;
        case 'skewx': need(1); t = [1, 0, 0, 0, Math.tan(geoRad(A(0))), 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; break;
        case 'skewy': need(1); t = [1, Math.tan(geoRad(A(0))), 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; break;
        case 'perspective': need(1); { const p = /^\s*none\s*$/i.test(args[0]) ? 0 : L(0); if (p < 0) bad(); t = GEO_ID.slice(); if (p) t[11] = -1 / p; d3 = true; } break;
        default: bad();
        }
        m = geoMul(m, t); if (d3) is2D = false;
    }
    return [m, is2D];
}

class DOMMatrixReadOnly {
    #m; #d2;
    constructor(init) {
        let m = GEO_ID.slice(), d2 = true;
        if (init === GEO_KEY) { this.#m = arguments[1]; this.#d2 = arguments[2]; return; }
        if (typeof init === 'string') {
            if (typeof document === 'undefined') throw new TypeError(`Failed to construct '${new.target.name}': Construction from a string is only supported in a window.`);
            [m, d2] = geoParse(init);
        } else if (init !== undefined) {
            if (init === null || typeof init !== 'object' || typeof init[Symbol.iterator] !== 'function') throw new TypeError(`Failed to construct '${new.target.name}': The provided value cannot be converted to a sequence.`);
            const a = Array.from(init, geoNum);
            if (a.length === 6) m = [a[0], a[1], 0, 0, a[2], a[3], 0, 0, 0, 0, 1, 0, a[4], a[5], 0, 1];
            else if (a.length === 16) { m = a; d2 = false; }
            else throw new TypeError(`Failed to construct '${new.target.name}': The sequence must contain 6 elements for a 2D matrix or 16 elements for a 3D matrix.`);
        }
        this.#m = m; this.#d2 = d2;
    }
    static { geoM = { get: o => o.#m, d2: o => o.#d2, set: (o, m, d2) => { o.#m = m; o.#d2 = d2; } }; }
    static fromMatrix(o) { const [m, d2] = geoFixup(o, false); return new this(GEO_KEY, m, d2); }
    static fromFloat32Array(a) { return geoFromArray(this, a, Float32Array); }
    static fromFloat64Array(a) { return geoFromArray(this, a, Float64Array); }
    get is2D() { return geoM.d2(this); }
    get isIdentity() { const m = geoM.get(this); return m.every((x, i) => x === GEO_ID[i]); }
    translate(x, y, z) { return geoCopy(this).translateSelf(x, y, z); }
    scale(sx, sy, sz, ox, oy, oz) { return geoCopy(this).scaleSelf(sx, sy, sz, ox, oy, oz); }
    scaleNonUniform(sx = 1, sy = 1) { return geoCopy(this).scaleSelf(sx, sy, 1, 0, 0, 0); }
    scale3d(s, ox, oy, oz) { return geoCopy(this).scale3dSelf(s, ox, oy, oz); }
    rotate(x, y, z) { return geoCopy(this).rotateSelf(x, y, z); }
    rotateFromVector(x, y) { return geoCopy(this).rotateFromVectorSelf(x, y); }
    rotateAxisAngle(x, y, z, a) { return geoCopy(this).rotateAxisAngleSelf(x, y, z, a); }
    skewX(a) { return geoCopy(this).skewXSelf(a); }
    skewY(a) { return geoCopy(this).skewYSelf(a); }
    multiply(o) { return geoCopy(this).multiplySelf(o); }
    flipX() { return geoCopy(this).multiplySelf({ a: -1 }); }
    flipY() { return geoCopy(this).multiplySelf({ d: -1 }); }
    inverse() { return geoCopy(this).invertSelf(); }
    transformPoint(p) {
        const { x, y, z, w } = DOMPointReadOnly.fromPoint(p), m = geoM.get(this);
        return new DOMPoint(m[0] * x + m[4] * y + m[8] * z + m[12] * w, m[1] * x + m[5] * y + m[9] * z + m[13] * w,
            m[2] * x + m[6] * y + m[10] * z + m[14] * w, m[3] * x + m[7] * y + m[11] * z + m[15] * w);
    }
    toFloat32Array() { return new Float32Array(geoM.get(this)); }
    toFloat64Array() { return new Float64Array(geoM.get(this)); }
    toJSON() { const o = {}; for (const k of ['a', 'b', 'c', 'd', 'e', 'f', ...GEO_NAMES, 'is2D', 'isIdentity']) o[k] = this[k]; return o; }
    toString() {
        const m = geoM.get(this);
        if (!m.every(Number.isFinite)) throw new DOMException('Cannot be serialized with NaN or Infinity values.', 'InvalidStateError');
        return geoM.d2(this) ? `matrix(${[0, 1, 4, 5, 12, 13].map(i => m[i]).join(', ')})` : `matrix3d(${m.join(', ')})`;
    }
}
const GEO_KEY = Symbol('geo');
const geoPost = (o, t, d3) => { geoM.set(o, geoMul(geoM.get(o), t), geoM.d2(o) && !d3); return o; };
const geoCopy = o => new DOMMatrix(GEO_KEY, geoM.get(o).slice(), geoM.d2(o));
function geoFromArray(C, a, T) {
    if (!(a instanceof T)) throw new TypeError(`The provided value is not of type '${T.name}'.`);
    if (a.length === 6) return new C([...a]);
    if (a.length === 16) return new C(GEO_KEY, [...a], false);
    throw new TypeError('The sequence must contain 6 elements for a 2D matrix or 16 elements for a 3D matrix.');
}
for (const [k, i] of [...Object.entries(GEO_ALIAS), ...GEO_NAMES.map((n, i) => [n, i])])
    Object.defineProperty(DOMMatrixReadOnly.prototype, k, { get() { return geoM.get(this)[i]; }, configurable: true, enumerable: true });

class DOMMatrix extends DOMMatrixReadOnly {
    static fromMatrix(o) { const [m, d2] = geoFixup(o, false); return new DOMMatrix(GEO_KEY, m, d2); }
    multiplySelf(o) { const other = DOMMatrix.fromMatrix(o); return geoPost(this, geoM.get(other), !other.is2D); }
    preMultiplySelf(o) { const other = DOMMatrix.fromMatrix(o); geoM.set(this, geoMul(geoM.get(other), geoM.get(this)), this.is2D && other.is2D); return this; }
    translateSelf(x = 0, y = 0, z = 0) { x = geoNum(x); y = geoNum(y); z = geoNum(z); return geoPost(this, geoTrans(x, y, z), z !== 0); }
    scaleSelf(sx = 1, sy, sz = 1, ox = 0, oy = 0, oz = 0) {
        sx = geoNum(sx); sy = sy === undefined ? sx : geoNum(sy); sz = geoNum(sz); ox = geoNum(ox); oy = geoNum(oy); oz = geoNum(oz);
        this.translateSelf(ox, oy, oz); geoPost(this, geoScale(sx, sy, sz), sz !== 1); return this.translateSelf(-ox, -oy, -oz);
    }
    scale3dSelf(s = 1, ox = 0, oy = 0, oz = 0) { s = geoNum(s); return this.scaleSelf(s, s, s, ox, oy, oz); }
    rotateSelf(x = 0, y, z) {
        x = geoNum(x);
        if (y === undefined && z === undefined) { z = x; x = 0; y = 0; } else { y = geoNum(y ?? 0); z = geoNum(z ?? 0); }
        if (z) geoPost(this, geoRot(0, 0, 1, z), false);
        if (y) geoPost(this, geoRot(0, 1, 0, y), true);
        if (x) geoPost(this, geoRot(1, 0, 0, x), true);
        return this;
    }
    rotateFromVectorSelf(x = 0, y = 0) { x = geoNum(x); y = geoNum(y); return x === 0 && y === 0 ? this : geoPost(this, geoRot(0, 0, 1, Math.atan2(y, x) * 180 / Math.PI), false); }
    rotateAxisAngleSelf(x = 0, y = 0, z = 0, a = 0) { x = geoNum(x); y = geoNum(y); z = geoNum(z); a = geoNum(a); return geoPost(this, geoRot(x, y, z, a), x !== 0 || y !== 0); }
    skewXSelf(a = 0) { const t = GEO_ID.slice(); t[4] = Math.tan(geoRad(geoNum(a))); return geoPost(this, t, false); }
    skewYSelf(a = 0) { const t = GEO_ID.slice(); t[1] = Math.tan(geoRad(geoNum(a))); return geoPost(this, t, false); }
    invertSelf() { const r = geoInv(geoM.get(this)); geoM.set(this, r || new Array(16).fill(NaN), r ? geoM.d2(this) : false); return this; }
    setMatrixValue(str) { const [m, d2] = geoParse(str); geoM.set(this, m, d2); return this; }
}
for (const [k, i] of [...Object.entries(GEO_ALIAS), ...GEO_NAMES.map((n, i) => [n, i])])
    Object.defineProperty(DOMMatrix.prototype, k, {
        get() { return geoM.get(this)[i]; },
        set(v) { const m = geoM.get(this); m[i] = geoNum(v); if (geoM.d2(this) && (GEO_3D.includes(i) ? m[i] !== 0 : (i === 10 || i === 15) && m[i] !== 1)) geoM.set(this, m, false); },
        configurable: true, enumerable: true });
