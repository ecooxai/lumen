const SVG_UNITS = ['', '', '%', 'em', 'ex', 'px', 'cm', 'mm', 'in', 'pt', 'pc'];
const SVG_PX = [0, 1, 0, 16, 8, 1, 96 / 2.54, 96 / 25.4, 96, 4 / 3, 16];
const SVG_NUM = '[+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?';
const SVG_LEN_RE = new RegExp(`^\\s*(${SVG_NUM})(%|em|ex|px|cm|mm|in|pt|pc)?\\s*$`, 'i');
function svgParseLen(s) { const m = s == null ? null : SVG_LEN_RE.exec(s); if (!m) return null; return [Math.max(1, SVG_UNITS.indexOf((m[2] || '').toLowerCase())), +m[1]]; }
function svgConsts(C, o) { for (const [k, v] of Object.entries(o)) { Object.defineProperty(C, k, { value: v, enumerable: true }); Object.defineProperty(C.prototype, k, { value: v, enumerable: true }); } }
const svgRO = () => { throw new DOMException('The object is read-only.', 'NoModificationAllowedError'); };
class SVGLength {
    constructor() { illegal(); }
    _get() { if (!this._el) return this._v; const p = svgParseLen(N.attr(this._el, this._attr)); return p || this._d || [1, 0]; }
    _put(t, v) { if (this._ro) svgRO(); if (!this._el) { this._v = [t, v]; return; } this._el.setAttribute(this._attr, String(v) + SVG_UNITS[t]); }
    get unitType() { return this._get()[0]; }
    get valueInSpecifiedUnits() { return this._get()[1]; }
    set valueInSpecifiedUnits(v) { v = +v; if (!isFinite(v)) throw new TypeError('The provided float value is non-finite.'); this._put(this.unitType, v); }
    get value() { const [t, v] = this._get(); return t === 2 ? 0 : v * SVG_PX[t]; }
    set value(v) { v = +v; if (!isFinite(v)) throw new TypeError('The provided float value is non-finite.'); const t = this.unitType; this._put(t, t === 2 ? v : v / SVG_PX[t]); }
    get valueAsString() { const [t, v] = this._get(); return String(v) + SVG_UNITS[t]; }
    set valueAsString(s) { const p = svgParseLen(String(s)); if (!p) throw new DOMException(`The value provided ('${s}') is invalid.`, 'SyntaxError'); this._put(p[0], p[1]); }
    newValueSpecifiedUnits(t, v) { t = +t; if (!(t >= 1 && t <= 10)) throw new DOMException('Cannot set value with unknown or invalid units (' + t + ').', 'NotSupportedError'); this._put(t, +v); }
    convertToSpecifiedUnits(t) { t = +t; if (!(t >= 1 && t <= 10)) throw new DOMException('Cannot convert to unknown or invalid units (' + t + ').', 'NotSupportedError'); const px = this.value; this._put(t, t === 2 ? px : px / SVG_PX[t]); }
}
svgConsts(SVGLength, { SVG_LENGTHTYPE_UNKNOWN: 0, SVG_LENGTHTYPE_NUMBER: 1, SVG_LENGTHTYPE_PERCENTAGE: 2, SVG_LENGTHTYPE_EMS: 3, SVG_LENGTHTYPE_EXS: 4, SVG_LENGTHTYPE_PX: 5, SVG_LENGTHTYPE_CM: 6, SVG_LENGTHTYPE_MM: 7, SVG_LENGTHTYPE_IN: 8, SVG_LENGTHTYPE_PT: 9, SVG_LENGTHTYPE_PC: 10 });
function mkLen(el, attr, ro, d) { const l = Object.create(SVGLength.prototype); def(l, '_el', el); def(l, '_attr', attr); def(l, '_ro', ro); def(l, '_d', d ? svgParseLen(d) : null); def(l, '_v', [1, 0]); return l; }
class SVGNumber {
    constructor() { illegal(); }
    get value() { if (!this._el) return this._v; const s = N.attr(this._el, this._attr); return s != null && new RegExp(`^\\s*${SVG_NUM}\\s*$`).test(s) ? +s : this._d; }
    set value(v) { if (this._ro) svgRO(); v = +v; if (!isFinite(v)) throw new TypeError('The provided float value is non-finite.'); if (this._el) this._el.setAttribute(this._attr, String(v)); else this._v = v; }
}
function mkNum(el, attr, d, ro) { const n = Object.create(SVGNumber.prototype); def(n, '_el', el); def(n, '_attr', attr); def(n, '_d', d); def(n, '_ro', ro); def(n, '_v', 0); return n; }
class SVGAnimatedLength { constructor() { illegal(); } get baseVal() { return this._b; } get animVal() { return this._a; } }
class SVGAnimatedNumber { constructor() { illegal(); } get baseVal() { return this._n.value; } set baseVal(v) { this._n.value = v; } get animVal() { return this._n.value; } }
class SVGAnimatedInteger { constructor() { illegal(); } get baseVal() { const s = N.attr(this._el, this._attr); return s != null && /^\s*[+-]?\d+\s*$/.test(s) ? parseInt(s, 10) : this._d; } set baseVal(v) { this._el.setAttribute(this._attr, String(Math.trunc(+v) | 0)); } get animVal() { return this.baseVal; } }
class SVGAnimatedBoolean { constructor() { illegal(); } get baseVal() { return N.attr(this._el, this._attr) === 'true'; } set baseVal(v) { this._el.setAttribute(this._attr, v ? 'true' : 'false'); } get animVal() { return this.baseVal; } }
class SVGAnimatedString {
    constructor() { illegal(); }
    get baseVal() { let v = N.attr(this._el, this._attr); if (v == null && this._attr === 'href') v = this._el.getAttributeNS('http://www.w3.org/1999/xlink', 'href'); return v == null ? '' : v; }
    set baseVal(v) { this._el.setAttribute(this._attr, String(v)); } get animVal() { return this.baseVal; }
}
class SVGAnimatedEnumeration {
    constructor() { illegal(); }
    get baseVal() { const s = N.attr(this._el, this._attr); const i = s == null ? -1 : this._map.indexOf(s); return i > 0 ? i : this._d; }
    set baseVal(v) { v = toU32(v) & 0xFFFF; if (!v || v >= this._map.length || !this._map[v]) throw new TypeError(`The enumeration value provided (${v}) is larger than the largest allowed value (${this._map.length - 1}).`); this._el.setAttribute(this._attr, this._map[v]); }
    get animVal() { return this.baseVal; }
}
class SVGRect { constructor() { illegal(); } }
for (const [i, k] of ['x', 'y', 'width', 'height'].entries()) Object.defineProperty(SVGRect.prototype, k, { get() { return this._get()[i]; }, set(v) { if (this._ro) svgRO(); const r = this._get(); r[i] = +v; if (this._el) this._el.setAttribute(this._attr, r.join(' ')); else this._v = r; }, enumerable: true, configurable: true });
SVGRect.prototype._get = function () { if (!this._el) return this._v.slice(); const s = N.attr(this._el, this._attr); const p = s ? s.trim().split(/[\s,]+/).map(Number) : []; return p.length === 4 && p.every(isFinite) && p[2] >= 0 && p[3] >= 0 ? p : [0, 0, 0, 0]; };
function mkRect(el, attr, ro) { const r = Object.create(SVGRect.prototype); def(r, '_el', el); def(r, '_attr', attr); def(r, '_ro', ro); def(r, '_v', [0, 0, 0, 0]); return r; }
class SVGAnimatedRect { constructor() { illegal(); } get baseVal() { return this._b; } get animVal() { return this._a; } }
const SVGUnitTypes = { SVG_UNIT_TYPE_UNKNOWN: 0, SVG_UNIT_TYPE_USERSPACEONUSE: 1, SVG_UNIT_TYPE_OBJECTBOUNDINGBOX: 2 };
function svgCache(el, k, f) { let c = el.__svg; if (!c) { c = new Map(); def(el, '__svg', c); } let v = c.get(k); if (!v) { v = f(); c.set(k, v); } return v; }
function svgMk(C, props) { const o = Object.create(C.prototype); for (const k in props) def(o, k, props[k]); return o; }
function svgProps(C, spec) {
    for (const [prop, kind, attr = prop, d = 0, map] of spec) Object.defineProperty(C.prototype, prop, {
        get() {
            return svgCache(this, prop, () => {
                switch (kind) {
                case 'len': return svgMk(SVGAnimatedLength, { _b: mkLen(this, attr, false, d), _a: mkLen(this, attr, true, d) });
                case 'num': return svgMk(SVGAnimatedNumber, { _n: mkNum(this, attr, d, false) });
                case 'int': return svgMk(SVGAnimatedInteger, { _el: this, _attr: attr, _d: d });
                case 'bool': return svgMk(SVGAnimatedBoolean, { _el: this, _attr: attr });
                case 'str': return svgMk(SVGAnimatedString, { _el: this, _attr: attr });
                case 'enum': return svgMk(SVGAnimatedEnumeration, { _el: this, _attr: attr, _d: d, _map: map });
                case 'rect': return svgMk(SVGAnimatedRect, { _b: mkRect(this, attr, false), _a: mkRect(this, attr, true) });
                }
            });
        }, enumerable: true, configurable: true,
    });
}
const UNITS = ['', 'userSpaceOnUse', 'objectBoundingBox'];
const XYWH = [['x', 'len'], ['y', 'len'], ['width', 'len'], ['height', 'len']];
const XYWH10 = [['x', 'len', 'x', '-10%'], ['y', 'len', 'y', '-10%'], ['width', 'len', 'width', '120%'], ['height', 'len', 'height', '120%']];
const svgClasses = {};
function svgClass(name, base, tags, spec) { const C = { [name]: class extends base {} }[name]; if (spec) svgProps(C, spec); for (const t of tags) svgClasses[t.toLowerCase()] = C; return C; }
svgProps(SVGElement, [['className', 'str', 'class']]);
svgProps(SVGSVGElement, XYWH.concat([['viewBox', 'rect']]));
svgClasses.svg = SVGSVGElement;
methods(SVGSVGElement.prototype, {
    createSVGLength() { return mkLen(null, null, false); },
    createSVGNumber() { return mkNum(null, null, 0, false); },
    createSVGRect() { return mkRect(null, null, false); },
});
const SVGGeometryElement = svgClass('SVGGeometryElement', SVGGraphicsElement, [], [['pathLength', 'num']]);
methods(SVGGeometryElement.prototype, { getTotalLength() { return 0; }, getPointAtLength() { return new DOMPoint(0, 0); }, isPointInFill() { return false; }, isPointInStroke() { return false; } });
const SVGRectElement = svgClass('SVGRectElement', SVGGeometryElement, ['rect'], XYWH.concat([['rx', 'len'], ['ry', 'len']]));
const SVGCircleElement = svgClass('SVGCircleElement', SVGGeometryElement, ['circle'], [['cx', 'len'], ['cy', 'len'], ['r', 'len']]);
const SVGEllipseElement = svgClass('SVGEllipseElement', SVGGeometryElement, ['ellipse'], [['cx', 'len'], ['cy', 'len'], ['rx', 'len'], ['ry', 'len']]);
const SVGLineElement = svgClass('SVGLineElement', SVGGeometryElement, ['line'], [['x1', 'len'], ['y1', 'len'], ['x2', 'len'], ['y2', 'len']]);
const SVGPathElement = svgClass('SVGPathElement', SVGGeometryElement, ['path']);
const SVGPolylineElement = svgClass('SVGPolylineElement', SVGGeometryElement, ['polyline']);
const SVGPolygonElement = svgClass('SVGPolygonElement', SVGGeometryElement, ['polygon']);
const SVGGElement = svgClass('SVGGElement', SVGGraphicsElement, ['g']);
const SVGDefsElement = svgClass('SVGDefsElement', SVGGraphicsElement, ['defs']);
const SVGUseElement = svgClass('SVGUseElement', SVGGraphicsElement, ['use'], XYWH.concat([['href', 'str']]));
const SVGImageElement = svgClass('SVGImageElement', SVGGraphicsElement, ['image'], XYWH.concat([['href', 'str']]));
const SVGForeignObjectElement = svgClass('SVGForeignObjectElement', SVGGraphicsElement, ['foreignObject'], XYWH);
const SVGAElement = svgClass('SVGAElement', SVGGraphicsElement, ['a'], [['href', 'str'], ['target', 'str']]);
const SVGSwitchElement = svgClass('SVGSwitchElement', SVGGraphicsElement, ['switch']);
const SVGTextContentElement = svgClass('SVGTextContentElement', SVGGraphicsElement, [], [['textLength', 'len'], ['lengthAdjust', 'enum', 'lengthAdjust', 1, ['', 'spacing', 'spacingAndGlyphs']]]);
svgConsts(SVGTextContentElement, { LENGTHADJUST_UNKNOWN: 0, LENGTHADJUST_SPACING: 1, LENGTHADJUST_SPACINGANDGLYPHS: 2 });
methods(SVGTextContentElement.prototype, { getNumberOfChars() { return (this.textContent || '').length; }, getComputedTextLength() { const r = N.rect(this); return r ? r[2] : 0; } });
const SVGTextPositioningElement = svgClass('SVGTextPositioningElement', SVGTextContentElement, []);
const SVGTextElement = svgClass('SVGTextElement', SVGTextPositioningElement, ['text']);
const SVGTSpanElement = svgClass('SVGTSpanElement', SVGTextPositioningElement, ['tspan']);
const SVGTextPathElement = svgClass('SVGTextPathElement', SVGTextContentElement, ['textPath'], [['startOffset', 'len'], ['href', 'str'], ['method', 'enum', 'method', 1, ['', 'align', 'stretch']], ['spacing', 'enum', 'spacing', 2, ['', 'auto', 'exact']]]);
svgConsts(SVGTextPathElement, { TEXTPATH_METHODTYPE_UNKNOWN: 0, TEXTPATH_METHODTYPE_ALIGN: 1, TEXTPATH_METHODTYPE_STRETCH: 2, TEXTPATH_SPACINGTYPE_UNKNOWN: 0, TEXTPATH_SPACINGTYPE_AUTO: 1, TEXTPATH_SPACINGTYPE_EXACT: 2 });
const SVGGradientElement = svgClass('SVGGradientElement', SVGElement, [], [['gradientUnits', 'enum', 'gradientUnits', 2, UNITS], ['spreadMethod', 'enum', 'spreadMethod', 1, ['', 'pad', 'reflect', 'repeat']], ['href', 'str']]);
svgConsts(SVGGradientElement, { SVG_SPREADMETHOD_UNKNOWN: 0, SVG_SPREADMETHOD_PAD: 1, SVG_SPREADMETHOD_REFLECT: 2, SVG_SPREADMETHOD_REPEAT: 3 });
const SVGLinearGradientElement = svgClass('SVGLinearGradientElement', SVGGradientElement, ['linearGradient'], [['x1', 'len'], ['y1', 'len'], ['x2', 'len', 'x2', '100%'], ['y2', 'len']]);
const SVGRadialGradientElement = svgClass('SVGRadialGradientElement', SVGGradientElement, ['radialGradient'], [['cx', 'len', 'cx', '50%'], ['cy', 'len', 'cy', '50%'], ['r', 'len', 'r', '50%'], ['fx', 'len', 'fx', '50%'], ['fy', 'len', 'fy', '50%'], ['fr', 'len']]);
const SVGStopElement = svgClass('SVGStopElement', SVGElement, ['stop'], [['offset', 'num']]);
const SVGClipPathElement = svgClass('SVGClipPathElement', SVGElement, ['clipPath'], [['clipPathUnits', 'enum', 'clipPathUnits', 1, UNITS]]);
const SVGMaskElement = svgClass('SVGMaskElement', SVGElement, ['mask'], [['maskUnits', 'enum', 'maskUnits', 2, UNITS], ['maskContentUnits', 'enum', 'maskContentUnits', 1, UNITS]].concat(XYWH10));
const SVGPatternElement = svgClass('SVGPatternElement', SVGElement, ['pattern'], [['patternUnits', 'enum', 'patternUnits', 2, UNITS], ['patternContentUnits', 'enum', 'patternContentUnits', 1, UNITS], ['viewBox', 'rect'], ['href', 'str']].concat(XYWH));
const SVGFilterElement = svgClass('SVGFilterElement', SVGElement, ['filter'], [['filterUnits', 'enum', 'filterUnits', 2, UNITS], ['primitiveUnits', 'enum', 'primitiveUnits', 1, UNITS], ['href', 'str']].concat(XYWH10));
const SVGMarkerElement = svgClass('SVGMarkerElement', SVGElement, ['marker'], [['refX', 'len'], ['refY', 'len'], ['markerWidth', 'len', 'markerWidth', '3'], ['markerHeight', 'len', 'markerHeight', '3'], ['markerUnits', 'enum', 'markerUnits', 2, ['', 'userSpaceOnUse', 'strokeWidth']], ['viewBox', 'rect']]);
svgConsts(SVGMarkerElement, { SVG_MARKERUNITS_UNKNOWN: 0, SVG_MARKERUNITS_USERSPACEONUSE: 1, SVG_MARKERUNITS_STROKEWIDTH: 2, SVG_MARKER_ORIENT_UNKNOWN: 0, SVG_MARKER_ORIENT_AUTO: 1, SVG_MARKER_ORIENT_ANGLE: 2 });
const SVGSymbolElement = svgClass('SVGSymbolElement', SVGElement, ['symbol'], [['viewBox', 'rect']]);
const SVGTitleElement = svgClass('SVGTitleElement', SVGElement, ['title']);
const SVGDescElement = svgClass('SVGDescElement', SVGElement, ['desc']);
const SVGMetadataElement = svgClass('SVGMetadataElement', SVGElement, ['metadata']);
const SVGStyleElement = svgClass('SVGStyleElement', SVGElement, ['style']);
const SVGScriptElement = svgClass('SVGScriptElement', SVGElement, ['script'], [['href', 'str']]);
const SVGFEColorMatrixElement = svgClass('SVGFEColorMatrixElement', SVGElement, ['feColorMatrix'], [['type', 'enum', 'type', 1, ['', 'matrix', 'saturate', 'hueRotate', 'luminanceToAlpha']], ['in1', 'str', 'in']]);
const SVGFECompositeElement = svgClass('SVGFECompositeElement', SVGElement, ['feComposite'], [['operator', 'enum', 'operator', 1, ['', 'over', 'in', 'out', 'atop', 'xor', 'arithmetic']], ['in1', 'str', 'in'], ['in2', 'str', 'in2'], ['k1', 'num'], ['k2', 'num'], ['k3', 'num'], ['k4', 'num']]);
const SVGFEMorphologyElement = svgClass('SVGFEMorphologyElement', SVGElement, ['feMorphology'], [['operator', 'enum', 'operator', 1, ['', 'erode', 'dilate']], ['in1', 'str', 'in']]);
const SVGFETurbulenceElement = svgClass('SVGFETurbulenceElement', SVGElement, ['feTurbulence'], [['type', 'enum', 'type', 2, ['', 'fractalNoise', 'turbulence']], ['stitchTiles', 'enum', 'stitchTiles', 2, ['', 'stitch', 'noStitch']], ['numOctaves', 'int', 'numOctaves', 1], ['seed', 'num']]);
const SVGFEDisplacementMapElement = svgClass('SVGFEDisplacementMapElement', SVGElement, ['feDisplacementMap'], [['xChannelSelector', 'enum', 'xChannelSelector', 4, ['', 'R', 'G', 'B', 'A']], ['yChannelSelector', 'enum', 'yChannelSelector', 4, ['', 'R', 'G', 'B', 'A']], ['scale', 'num'], ['in1', 'str', 'in'], ['in2', 'str', 'in2']]);
const SVGFEConvolveMatrixElement = svgClass('SVGFEConvolveMatrixElement', SVGElement, ['feConvolveMatrix'], [['edgeMode', 'enum', 'edgeMode', 1, ['', 'duplicate', 'wrap', 'none']], ['preserveAlpha', 'bool'], ['divisor', 'num'], ['bias', 'num'], ['in1', 'str', 'in']]);
const SVGComponentTransferFunctionElement = svgClass('SVGComponentTransferFunctionElement', SVGElement, [], [['type', 'enum', 'type', 1, ['', 'identity', 'table', 'discrete', 'linear', 'gamma']], ['slope', 'num', 'slope', 1], ['intercept', 'num'], ['amplitude', 'num', 'amplitude', 1], ['exponent', 'num', 'exponent', 1], ['offset', 'num']]);
for (const c of 'RGBA') svgClass('SVGFEFunc' + c + 'Element', SVGComponentTransferFunctionElement, ['feFunc' + c]);
const svgProto = (tag) => (svgClasses[tag.toLowerCase()] || (/^(g|path|rect|circle|ellipse|line|polyline|polygon|use|text|image)$/.test(tag) ? SVGGraphicsElement : SVGElement)).prototype;
