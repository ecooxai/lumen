const SVG_UNITS = ['', '', '%', 'em', 'ex', 'px', 'cm', 'mm', 'in', 'pt', 'pc'];
const SVG_PX = [0, 1, 0, 16, 8, 1, 96 / 2.54, 96 / 25.4, 96, 4 / 3, 16];
const SVG_NUM = '[+-]?(?:\\d+(?:\\.\\d+)?|\\.\\d+)(?:[eE][+-]?\\d+)?';
const SVG_NUM_RE = new RegExp(`^\\s*${SVG_NUM}\\s*$`);
function svgPair(s, i, re) { if (s == null) return null; const p = s.trim().split(/\s*,\s*|\s+/); if (p.length < 1 || p.length > 2 || !p.every(x => re.test(x))) return null; return +(p[i] !== undefined ? p[i] : p[0]); }
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
    get value() { if (!this._el) return this._v; const s = N.attr(this._el, this._attr); if (this._i >= 0) { const v = svgPair(s, this._i, SVG_NUM_RE); return v === null ? this._d : v; } return s != null && SVG_NUM_RE.test(s) ? +s : this._d; }
    set value(v) { if (this._ro) svgRO(); v = +v; if (!isFinite(v)) throw new TypeError('The provided float value is non-finite.'); if (!this._el) { this._v = v; return; } if (this._i >= 0) { const o = mkNum(this._el, this._attr, this._d, false, 1 - this._i).value; this._el.setAttribute(this._attr, this._i ? o + ' ' + v : v + ' ' + o); } else this._el.setAttribute(this._attr, String(v)); }
}
function mkNum(el, attr, d, ro, i = -1) { const n = Object.create(SVGNumber.prototype); def(n, '_el', el); def(n, '_attr', attr); def(n, '_d', d); def(n, '_ro', ro); def(n, '_v', 0); def(n, '_i', i); return n; }
class SVGAnimatedLength { constructor() { illegal(); } get baseVal() { return this._b; } get animVal() { return this._a; } }
class SVGAnimatedNumber { constructor() { illegal(); } get baseVal() { return this._n.value; } set baseVal(v) { this._n.value = v; } get animVal() { return this._n.value; } }
class SVGAnimatedInteger { constructor() { illegal(); } get baseVal() { const s = N.attr(this._el, this._attr); if (this._i >= 0) { const v = svgPair(s, this._i, /^[+-]?\d+$/); return v === null ? this._d : v; } return s != null && /^\s*[+-]?\d+\s*$/.test(s) ? parseInt(s, 10) : this._d; } set baseVal(v) { v = String(Math.trunc(+v) | 0); if (this._i >= 0) { const o = svgMk(SVGAnimatedInteger, { _el: this._el, _attr: this._attr, _d: this._d, _i: 1 - this._i }).baseVal; v = this._i ? o + ' ' + v : v + ' ' + o; } this._el.setAttribute(this._attr, v); } get animVal() { return this.baseVal; } }
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
function svgCache(el, k, f) { let c = el.__svg; if (!c) { c = new SMap(); def(el, '__svg', c); } let v = c.get(k); if (!v) { v = f(); c.set(k, v); } return v; }
function svgMk(C, props) { const o = Object.create(C.prototype); for (const k in props) def(o, k, props[k]); return o; }
function svgProps(C, spec) {
    for (const [prop, kind, attr = prop, d = 0, map] of spec) Object.defineProperty(C.prototype, prop, {
        get() {
            if (!N.isNode(this)) throw new TypeError('Illegal invocation');
            return svgCache(this, prop, () => {
                switch (kind) {
                case 'len': return svgMk(SVGAnimatedLength, { _b: mkLen(this, attr, false, d), _a: mkLen(this, attr, true, d) });
                case 'num': return svgMk(SVGAnimatedNumber, { _n: mkNum(this, attr, d, false) });
                case 'num0': case 'num1': return svgMk(SVGAnimatedNumber, { _n: mkNum(this, attr, d, false, +kind[3]) });
                case 'int': return svgMk(SVGAnimatedInteger, { _el: this, _attr: attr, _d: d, _i: -1 });
                case 'int0': case 'int1': return svgMk(SVGAnimatedInteger, { _el: this, _attr: attr, _d: d, _i: +kind[3] });
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
svgProps(SVGSVGElement, [['x', 'len'], ['y', 'len'], ['width', 'len', 'width', '100%'], ['height', 'len', 'height', '100%'], ['viewBox', 'rect']]);
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
const SVGLinearGradientElement = svgClass('SVGLinearGradientElement', SVGGradientElement, ['linearGradient'], [['x1', 'len', 'x1', '0%'], ['y1', 'len', 'y1', '0%'], ['x2', 'len', 'x2', '100%'], ['y2', 'len', 'y2', '0%']]);
const SVGRadialGradientElement = svgClass('SVGRadialGradientElement', SVGGradientElement, ['radialGradient'], [['cx', 'len', 'cx', '50%'], ['cy', 'len', 'cy', '50%'], ['r', 'len', 'r', '50%'], ['fx', 'len', 'fx', '50%'], ['fy', 'len', 'fy', '50%'], ['fr', 'len', 'fr', '0%']]);
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
const FE_STD = [['x', 'len', 'x', '0%'], ['y', 'len', 'y', '0%'], ['width', 'len', 'width', '100%'], ['height', 'len', 'height', '100%'], ['result', 'str']];
const IN1 = ['in1', 'str', 'in'], IN2 = ['in2', 'str', 'in2'];
const fe = (name, tag, spec, consts) => { const C = svgClass(name, SVGElement, [tag], FE_STD.concat(spec)); if (consts) svgConsts(C, consts); return C; };
const pair = (x, y, attr, kind, d = 0) => [[x, kind + '0', attr, d], [y, kind + '1', attr, d]];
const enumConsts = (prefix, kws) => { const o = { [prefix + 'UNKNOWN']: 0 }; kws.forEach((k, i) => { o[prefix + k] = i + 1; }); return o; };
const EDGE = ['', 'duplicate', 'wrap', 'none'], EDGE_C = enumConsts('SVG_EDGEMODE_', ['DUPLICATE', 'WRAP', 'NONE']);
const BLEND = ['normal', 'multiply', 'screen', 'darken', 'lighten', 'overlay', 'color-dodge', 'color-burn', 'hard-light', 'soft-light', 'difference', 'exclusion', 'hue', 'saturation', 'color', 'luminosity'];
const SVGFEBlendElement = fe('SVGFEBlendElement', 'feBlend', [['mode', 'enum', 'mode', 1, [''].concat(BLEND)], IN1, IN2], enumConsts('SVG_FEBLEND_MODE_', BLEND.map(k => k.toUpperCase().replace('-', '_'))));
const SVGFEColorMatrixElement = fe('SVGFEColorMatrixElement', 'feColorMatrix', [['type', 'enum', 'type', 1, ['', 'matrix', 'saturate', 'hueRotate', 'luminanceToAlpha']], IN1], enumConsts('SVG_FECOLORMATRIX_TYPE_', ['MATRIX', 'SATURATE', 'HUEROTATE', 'LUMINANCETOALPHA']));
const SVGFEComponentTransferElement = fe('SVGFEComponentTransferElement', 'feComponentTransfer', [IN1]);
const SVGFECompositeElement = fe('SVGFECompositeElement', 'feComposite', [['operator', 'enum', 'operator', 1, ['', 'over', 'in', 'out', 'atop', 'xor', 'arithmetic']], IN1, IN2, ['k1', 'num'], ['k2', 'num'], ['k3', 'num'], ['k4', 'num']], enumConsts('SVG_FECOMPOSITE_OPERATOR_', ['OVER', 'IN', 'OUT', 'ATOP', 'XOR', 'ARITHMETIC']));
const SVGFEConvolveMatrixElement = fe('SVGFEConvolveMatrixElement', 'feConvolveMatrix', [['edgeMode', 'enum', 'edgeMode', 1, EDGE], ['preserveAlpha', 'bool'], ['divisor', 'num', 'divisor', 1], ['bias', 'num'], IN1, ['targetX', 'int'], ['targetY', 'int']].concat(pair('orderX', 'orderY', 'order', 'int', 3), pair('kernelUnitLengthX', 'kernelUnitLengthY', 'kernelUnitLength', 'num')), EDGE_C);
const SVGFEDiffuseLightingElement = fe('SVGFEDiffuseLightingElement', 'feDiffuseLighting', [IN1, ['surfaceScale', 'num', 'surfaceScale', 1], ['diffuseConstant', 'num', 'diffuseConstant', 1]].concat(pair('kernelUnitLengthX', 'kernelUnitLengthY', 'kernelUnitLength', 'num')));
const SVGFESpecularLightingElement = fe('SVGFESpecularLightingElement', 'feSpecularLighting', [IN1, ['surfaceScale', 'num', 'surfaceScale', 1], ['specularConstant', 'num', 'specularConstant', 1], ['specularExponent', 'num', 'specularExponent', 1]].concat(pair('kernelUnitLengthX', 'kernelUnitLengthY', 'kernelUnitLength', 'num')));
const SVGFEDisplacementMapElement = fe('SVGFEDisplacementMapElement', 'feDisplacementMap', [['xChannelSelector', 'enum', 'xChannelSelector', 4, ['', 'R', 'G', 'B', 'A']], ['yChannelSelector', 'enum', 'yChannelSelector', 4, ['', 'R', 'G', 'B', 'A']], ['scale', 'num'], IN1, IN2], enumConsts('SVG_CHANNEL_', ['R', 'G', 'B', 'A']));
const SVGFEDropShadowElement = fe('SVGFEDropShadowElement', 'feDropShadow', [IN1, ['dx', 'num', 'dx', 2], ['dy', 'num', 'dy', 2]].concat(pair('stdDeviationX', 'stdDeviationY', 'stdDeviation', 'num', 2)));
const SVGFEFloodElement = fe('SVGFEFloodElement', 'feFlood', []);
const SVGFEGaussianBlurElement = fe('SVGFEGaussianBlurElement', 'feGaussianBlur', [IN1, ['edgeMode', 'enum', 'edgeMode', 3, EDGE]].concat(pair('stdDeviationX', 'stdDeviationY', 'stdDeviation', 'num')), EDGE_C);
const SVGFEImageElement = fe('SVGFEImageElement', 'feImage', [['href', 'str']]);
const SVGFEMergeElement = fe('SVGFEMergeElement', 'feMerge', []);
const SVGFEMergeNodeElement = svgClass('SVGFEMergeNodeElement', SVGElement, ['feMergeNode'], [IN1]);
const SVGFEMorphologyElement = fe('SVGFEMorphologyElement', 'feMorphology', [['operator', 'enum', 'operator', 1, ['', 'erode', 'dilate']], IN1].concat(pair('radiusX', 'radiusY', 'radius', 'num')), enumConsts('SVG_MORPHOLOGY_OPERATOR_', ['ERODE', 'DILATE']));
const SVGFEOffsetElement = fe('SVGFEOffsetElement', 'feOffset', [IN1, ['dx', 'num'], ['dy', 'num']]);
const SVGFETileElement = fe('SVGFETileElement', 'feTile', [IN1]);
const SVGFETurbulenceElement = fe('SVGFETurbulenceElement', 'feTurbulence', [['type', 'enum', 'type', 2, ['', 'fractalNoise', 'turbulence']], ['stitchTiles', 'enum', 'stitchTiles', 2, ['', 'stitch', 'noStitch']], ['numOctaves', 'int', 'numOctaves', 1], ['seed', 'num']].concat(pair('baseFrequencyX', 'baseFrequencyY', 'baseFrequency', 'num')), Object.assign(enumConsts('SVG_TURBULENCE_TYPE_', ['FRACTALNOISE', 'TURBULENCE']), enumConsts('SVG_STITCHTYPE_', ['STITCH', 'NOSTITCH'])));
const SVGFEDistantLightElement = svgClass('SVGFEDistantLightElement', SVGElement, ['feDistantLight'], [['azimuth', 'num'], ['elevation', 'num']]);
const SVGFEPointLightElement = svgClass('SVGFEPointLightElement', SVGElement, ['fePointLight'], [['x', 'num'], ['y', 'num'], ['z', 'num']]);
const SVGFESpotLightElement = svgClass('SVGFESpotLightElement', SVGElement, ['feSpotLight'], [['x', 'num'], ['y', 'num'], ['z', 'num'], ['pointsAtX', 'num'], ['pointsAtY', 'num'], ['pointsAtZ', 'num'], ['specularExponent', 'num', 'specularExponent', 1], ['limitingConeAngle', 'num']]);
const SVGComponentTransferFunctionElement = svgClass('SVGComponentTransferFunctionElement', SVGElement, [], [['type', 'enum', 'type', 1, ['', 'identity', 'table', 'discrete', 'linear', 'gamma']], ['slope', 'num', 'slope', 1], ['intercept', 'num'], ['amplitude', 'num', 'amplitude', 1], ['exponent', 'num', 'exponent', 1], ['offset', 'num']]);
svgConsts(SVGComponentTransferFunctionElement, enumConsts('SVG_FECOMPONENTTRANSFER_TYPE_', ['IDENTITY', 'TABLE', 'DISCRETE', 'LINEAR', 'GAMMA']));
const SVGFEFuncRElement = svgClass('SVGFEFuncRElement', SVGComponentTransferFunctionElement, ['feFuncR']);
const SVGFEFuncGElement = svgClass('SVGFEFuncGElement', SVGComponentTransferFunctionElement, ['feFuncG']);
const SVGFEFuncBElement = svgClass('SVGFEFuncBElement', SVGComponentTransferFunctionElement, ['feFuncB']);
const SVGFEFuncAElement = svgClass('SVGFEFuncAElement', SVGComponentTransferFunctionElement, ['feFuncA']);
const svgProto = (tag) => (svgClasses[tag.toLowerCase()] || (/^(g|path|rect|circle|ellipse|line|polyline|polygon|use|text|image)$/.test(tag) ? SVGGraphicsElement : SVGElement)).prototype;
/* ---- lists, transforms, angles, preserveAspectRatio, SVGTests and the animation elements ---- */
const svgIdx = () => { throw new DOMException('The index provided is larger than the maximum index.', 'IndexSizeError'); };
function svgAcc(C, name, f) { Object.defineProperty(C.prototype, name, { get() { if (!N.isNode(this)) throw new TypeError('Illegal invocation'); return svgCache(this, name, () => f(this)); }, enumerable: true, configurable: true }); }
function svgListClass(name) {
    const C = { [name]: class {
        constructor() { illegal(); }
        _items() { return this._el ? this._parse(N.attr(this._el, this._attr) || '') : this._v; }
        _set(a) { if (this._ro) svgRO(); if (this._el) this._el.setAttribute(this._attr, a.map(this._ser).join(this._sep)); else this._v = a; }
        get length() { return this._items().length; } get numberOfItems() { return this._items().length; }
        clear() { this._set([]); }
        initialize(it) { this._set([it]); return it; }
        getItem(i) { const a = this._items(); i >>>= 0; if (i >= a.length) svgIdx(); return a[i]; }
        insertItemBefore(it, i) { const a = this._items(); a.splice(Math.min(i >>> 0, a.length), 0, it); this._set(a); return it; }
        replaceItem(it, i) { const a = this._items(); i >>>= 0; if (i >= a.length) svgIdx(); a[i] = it; this._set(a); return it; }
        removeItem(i) { const a = this._items(); i >>>= 0; if (i >= a.length) svgIdx(); const [r] = a.splice(i, 1); this._set(a); return r; }
        appendItem(it) { const a = this._items(); a.push(it); this._set(a); return it; }
    } }[name];
    Object.defineProperty(C.prototype, Symbol.iterator, { value: function* () { yield* this._items(); }, configurable: true, writable: true });
    return C;
}
const mkList = (C, el, attr, ro, parse, ser, sep = ' ') => svgMk(C, { _el: el, _attr: attr, _ro: ro, _parse: parse, _ser: ser, _sep: sep, _v: [] });
const svgNums = s => (s.match(new RegExp(SVG_NUM, 'g')) || []).map(Number);
const SVGStringList = svgListClass('SVGStringList'), SVGNumberList = svgListClass('SVGNumberList'), SVGLengthList = svgListClass('SVGLengthList');
const SVGPointList = svgListClass('SVGPointList'), SVGTransformList = svgListClass('SVGTransformList');
const numItem = v => { const n = mkNum(null, null, 0, false); n._v = v; return n; };
const lenItem = s => { const l = mkLen(null, null, false); l._v = svgParseLen(s) || [1, 0]; return l; };
const strList = (el, attr) => mkList(SVGStringList, el, attr, false, s => s.trim() ? s.trim().split(/\s+/) : [], String);
const numList = (el, attr, ro) => mkList(SVGNumberList, el, attr, ro, s => svgNums(s).map(numItem), n => String(n.value));
const lenList = (el, attr, ro) => mkList(SVGLengthList, el, attr, ro, s => (s.trim() ? s.trim().split(/[\s,]+/) : []).map(lenItem), l => l.valueAsString);
const ptList = (el, attr, ro) => mkList(SVGPointList, el, attr, ro, s => { const n = svgNums(s), r = []; for (let i = 0; i + 1 < n.length; i += 2) r.push(new DOMPoint(n[i], n[i + 1])); return r; }, p => p.x + ',' + p.y);
class SVGTransform {
    constructor() { illegal(); }
    get type() { return this._t; } get angle() { return this._a; } get matrix() { return this._m; }
    _s(t, m, a = 0) { if (this._ro) svgRO(); this._t = t; this._m = m; this._a = a; if (this._list) this._list._sync(); }
    setMatrix(m) { this._s(1, Object.assign(new DOMMatrix(), { a: m.a, b: m.b, c: m.c, d: m.d, e: m.e, f: m.f })); }
    setTranslate(x, y) { this._s(2, Object.assign(new DOMMatrix(), { e: +x, f: +y })); }
    setScale(x, y) { this._s(3, Object.assign(new DOMMatrix(), { a: +x, d: +y })); }
    setRotate(a, cx, cy) { const r = a * Math.PI / 180; this._s(4, Object.assign(new DOMMatrix(), { a: Math.cos(r), b: Math.sin(r), c: -Math.sin(r), d: Math.cos(r) }), +a); }
    setSkewX(a) { this._s(5, Object.assign(new DOMMatrix(), { c: Math.tan(a * Math.PI / 180) }), +a); }
    setSkewY(a) { this._s(6, Object.assign(new DOMMatrix(), { b: Math.tan(a * Math.PI / 180) }), +a); }
}
svgConsts(SVGTransform, { SVG_TRANSFORM_UNKNOWN: 0, SVG_TRANSFORM_MATRIX: 1, SVG_TRANSFORM_TRANSLATE: 2, SVG_TRANSFORM_SCALE: 3, SVG_TRANSFORM_ROTATE: 4, SVG_TRANSFORM_SKEWX: 5, SVG_TRANSFORM_SKEWY: 6 });
const mkTransform = () => svgMk(SVGTransform, { _t: 1, _a: 0, _m: new DOMMatrix(), _ro: false, _list: null });
function parseTransforms(s) {
    const out = [], re = /(matrix|translate|scale|rotate|skewX|skewY)\s*\(([^)]*)\)/g; let m;
    while ((m = re.exec(s))) {
        const t = mkTransform(), n = svgNums(m[2]);
        if (m[1] === 'matrix' && n.length === 6) t.setMatrix({ a: n[0], b: n[1], c: n[2], d: n[3], e: n[4], f: n[5] });
        else if (m[1] === 'translate') t.setTranslate(n[0] || 0, n[1] || 0);
        else if (m[1] === 'scale') t.setScale(n[0] ?? 1, n[1] ?? n[0] ?? 1);
        else if (m[1] === 'rotate') t.setRotate(n[0] || 0, n[1] || 0, n[2] || 0);
        else if (m[1] === 'skewX') t.setSkewX(n[0] || 0); else if (m[1] === 'skewY') t.setSkewY(n[0] || 0);
        out.push(t);
    }
    return out;
}
const serTransform = t => { const m = t.matrix; return t.type === 2 ? `translate(${m.e} ${m.f})` : t.type === 3 ? `scale(${m.a} ${m.d})` : t.type === 4 ? `rotate(${t.angle})` : t.type === 5 ? `skewX(${t.angle})` : t.type === 6 ? `skewY(${t.angle})` : `matrix(${m.a} ${m.b} ${m.c} ${m.d} ${m.e} ${m.f})`; };
Object.assign(SVGTransformList.prototype, {
    createSVGTransformFromMatrix(m) { const t = mkTransform(); t.setMatrix(m); return t; },
    consolidate() { const a = this._items(); if (!a.length) return null; const t = mkTransform(); t.setMatrix(a.reduce((acc, x) => acc.multiply ? acc : x.matrix, a[0].matrix)); this._set([t]); return t; },
});
const tList = (el, attr, ro) => mkList(SVGTransformList, el, attr, ro, parseTransforms, serTransform);
class SVGAnimatedTransformList { constructor() { illegal(); } get baseVal() { return this._b; } get animVal() { return this._a; } }
class SVGAnimatedNumberList { constructor() { illegal(); } get baseVal() { return this._b; } get animVal() { return this._a; } }
class SVGAnimatedLengthList { constructor() { illegal(); } get baseVal() { return this._b; } get animVal() { return this._a; } }
const PAR_ALIGN = ['', 'none', 'xMinYMin', 'xMidYMin', 'xMaxYMin', 'xMinYMid', 'xMidYMid', 'xMaxYMid', 'xMinYMax', 'xMidYMax', 'xMaxYMax'];
class SVGPreserveAspectRatio {
    constructor() { illegal(); }
    _p() { const s = (this._el && N.attr(this._el, this._attr) || '').trim().split(/\s+/); const a = PAR_ALIGN.indexOf(s[0]); return [a > 0 ? a : 6, s[1] === 'slice' ? 2 : 1]; }
    _w(a, m) { if (this._ro) svgRO(); this._el.setAttribute(this._attr, PAR_ALIGN[a] + (m === 2 ? ' slice' : '')); }
    get align() { return this._p()[0]; } set align(v) { v = +v; if (!(v >= 1 && v <= 10)) throw new TypeError('Invalid align'); this._w(v, this._p()[1]); }
    get meetOrSlice() { return this._p()[1]; } set meetOrSlice(v) { v = +v; if (v !== 1 && v !== 2) throw new TypeError('Invalid meetOrSlice'); this._w(this._p()[0], v); }
}
svgConsts(SVGPreserveAspectRatio, { SVG_PRESERVEASPECTRATIO_UNKNOWN: 0, SVG_PRESERVEASPECTRATIO_NONE: 1, SVG_PRESERVEASPECTRATIO_XMINYMIN: 2, SVG_PRESERVEASPECTRATIO_XMIDYMIN: 3, SVG_PRESERVEASPECTRATIO_XMAXYMIN: 4, SVG_PRESERVEASPECTRATIO_XMINYMID: 5, SVG_PRESERVEASPECTRATIO_XMIDYMID: 6, SVG_PRESERVEASPECTRATIO_XMAXYMID: 7, SVG_PRESERVEASPECTRATIO_XMINYMAX: 8, SVG_PRESERVEASPECTRATIO_XMIDYMAX: 9, SVG_PRESERVEASPECTRATIO_XMAXYMAX: 10, SVG_MEETORSLICE_UNKNOWN: 0, SVG_MEETORSLICE_MEET: 1, SVG_MEETORSLICE_SLICE: 2 });
class SVGAnimatedPreserveAspectRatio { constructor() { illegal(); } get baseVal() { return this._b; } get animVal() { return this._a; } }
const ANG_UNITS = ['', '', 'deg', 'rad', 'grad'], ANG_DEG = [0, 1, 1, 180 / Math.PI, 0.9];
class SVGAngle {
    constructor() { illegal(); }
    _g() { if (!this._el) return this._v; const m = /^\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?)(deg|rad|grad)?\s*$/i.exec(N.attr(this._el, this._attr) || ''); return m ? [m[2] ? ANG_UNITS.indexOf(m[2].toLowerCase()) : 1, +m[1]] : [1, 0]; }
    _s(t, v) { if (this._ro) svgRO(); if (this._el) this._el.setAttribute(this._attr, v + ANG_UNITS[t]); else this._v = [t, v]; }
    get unitType() { return this._g()[0]; }
    get value() { const [t, v] = this._g(); return v * ANG_DEG[t]; } set value(v) { const t = this.unitType; this._s(t, +v / ANG_DEG[t]); }
    get valueInSpecifiedUnits() { return this._g()[1]; } set valueInSpecifiedUnits(v) { this._s(this.unitType, +v); }
    get valueAsString() { const [t, v] = this._g(); return v + ANG_UNITS[t]; }
    set valueAsString(s) { const m = /^\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?)(deg|rad|grad)?\s*$/i.exec(String(s)); if (!m) throw new DOMException('The value provided is invalid.', 'SyntaxError'); this._s(m[2] ? ANG_UNITS.indexOf(m[2].toLowerCase()) : 1, +m[1]); }
    newValueSpecifiedUnits(t, v) { t = +t; if (!(t >= 1 && t <= 4)) throw new DOMException('Invalid unit type.', 'NotSupportedError'); this._s(t, +v); }
    convertToSpecifiedUnits(t) { t = +t; if (!(t >= 1 && t <= 4)) throw new DOMException('Invalid unit type.', 'NotSupportedError'); this._s(t, this.value / ANG_DEG[t]); }
}
svgConsts(SVGAngle, { SVG_ANGLETYPE_UNKNOWN: 0, SVG_ANGLETYPE_UNSPECIFIED: 1, SVG_ANGLETYPE_DEG: 2, SVG_ANGLETYPE_RAD: 3, SVG_ANGLETYPE_GRAD: 4 });
class SVGAnimatedAngle { constructor() { illegal(); } get baseVal() { return this._b; } get animVal() { return this._a; } }
const mkAngle = (el, attr, ro) => svgMk(SVGAngle, { _el: el, _attr: attr, _ro: ro, _v: [1, 0] });
const parAcc = (C) => svgAcc(C, 'preserveAspectRatio', el => svgMk(SVGAnimatedPreserveAspectRatio, { _b: svgMk(SVGPreserveAspectRatio, { _el: el, _attr: 'preserveAspectRatio', _ro: false }), _a: svgMk(SVGPreserveAspectRatio, { _el: el, _attr: 'preserveAspectRatio', _ro: true }) }));
const tAcc = (C, name, attr = name) => svgAcc(C, name, el => svgMk(SVGAnimatedTransformList, { _b: tList(el, attr, false), _a: tList(el, attr, true) }));
const testsAcc = (C) => { svgAcc(C, 'requiredExtensions', el => strList(el, 'requiredExtensions')); svgAcc(C, 'systemLanguage', el => strList(el, 'systemLanguage')); };
const svgCTM = (el) => { const r = N.rect(el); const m = new DOMMatrix(); if (r) { m.e = r[0]; m.f = r[1]; } return m; };

Object.defineProperty(SVGElement.prototype, 'viewportElement', { get() { if (!N.isNode(this)) throw new TypeError('Illegal invocation'); for (let p = N.parent(this); p && N.type(p) === 1; p = N.parent(p)) if (N.name(p) === 'svg' || N.name(p) === 'symbol') return p; return null; }, enumerable: true, configurable: true });
delete SVGElement.prototype.getBBox;
tAcc(SVGGraphicsElement, 'transform'); testsAcc(SVGGraphicsElement);
methods(SVGGraphicsElement.prototype, {
    getBBox() { const r = N.rect(this), o = this.ownerSVGElement && N.rect(this.ownerSVGElement); return new DOMRect(r && o ? r[0] - o[0] : 0, r && o ? r[1] - o[1] : 0, r ? r[2] : 0, r ? r[3] : 0); },
    getCTM() { return svgCTM(this); }, getScreenCTM() { return svgCTM(this); },
});
parAcc(SVGSVGElement); svgConsts(SVGSVGElement, { SVG_ZOOMANDPAN_UNKNOWN: 0, SVG_ZOOMANDPAN_DISABLE: 1, SVG_ZOOMANDPAN_MAGNIFY: 2 });
methods(SVGSVGElement.prototype, {
    get currentScale() { return this.__cs ?? 1; }, set currentScale(v) { def(this, '__cs', +v); },
    get currentTranslate() { if (!this.__ct) def(this, '__ct', new DOMPoint(0, 0)); return this.__ct; },
    getIntersectionList() { return nodeList([]); }, getEnclosureList() { return nodeList([]); }, checkIntersection() { return false; }, checkEnclosure() { return false; }, deselectAll() { selection.removeAllRanges(); },
    createSVGAngle() { return mkAngle(null, null, false); }, createSVGTransform() { return mkTransform(); }, createSVGTransformFromMatrix(m) { const t = mkTransform(); t.setMatrix(m); return t; },
    getElementById(id) { return this.querySelector('#' + CSS.escape(String(id))); },
    suspendRedraw() { return 1; }, unsuspendRedraw() {}, unsuspendRedrawAll() {}, forceRedraw() {},
    pauseAnimations() {}, unpauseAnimations() {}, animationsPaused() { return false; }, getCurrentTime() { return 0; }, setCurrentTime() {},
});
methods(SVGTextContentElement.prototype, {
    getSubStringLength(c, n) { return 0; }, getStartPositionOfChar() { return new DOMPoint(); }, getEndPositionOfChar() { return new DOMPoint(); },
    getExtentOfChar() { return new DOMRect(); }, getRotationOfChar() { return 0; }, getCharNumAtPosition() { return -1; }, selectSubString() {},
});
for (const k of ['x', 'y', 'dx', 'dy']) svgAcc(SVGTextPositioningElement, k, el => svgMk(SVGAnimatedLengthList, { _b: lenList(el, k, false), _a: lenList(el, k, true) }));
svgAcc(SVGTextPositioningElement, 'rotate', el => svgMk(SVGAnimatedNumberList, { _b: numList(el, 'rotate', false), _a: numList(el, 'rotate', true) }));
for (const C of [SVGPolylineElement, SVGPolygonElement]) { svgAcc(C, 'points', el => ptList(el, 'points', false)); svgAcc(C, 'animatedPoints', el => ptList(el, 'points', true)); }
tAcc(SVGGradientElement, 'gradientTransform'); tAcc(SVGPatternElement, 'patternTransform'); parAcc(SVGPatternElement); testsAcc(SVGPatternElement);
parAcc(SVGImageElement); parAcc(SVGMarkerElement); parAcc(SVGSymbolElement); parAcc(SVGFEImageElement);
svgAcc(SVGMarkerElement, 'orientType', el => svgMk(SVGAnimatedEnumeration, { _el: el, _attr: 'orient', _d: 2, _map: ['', 'auto', ''] }));
svgAcc(SVGMarkerElement, 'orientAngle', el => svgMk(SVGAnimatedAngle, { _b: mkAngle(el, 'orient', false), _a: mkAngle(el, 'orient', true) }));
methods(SVGMarkerElement.prototype, { get orient() { return N.attr(this, 'orient') || '0'; }, set orient(v) { this.setAttribute('orient', String(v)); }, setOrientToAuto() { this.setAttribute('orient', 'auto'); }, setOrientToAngle(a) { this.setAttribute('orient', a.valueAsString); } });
methods(SVGImageElement.prototype, { get crossOrigin() { return N.attr(this, 'crossorigin'); }, set crossOrigin(v) { if (v === null) this.removeAttribute('crossorigin'); else this.setAttribute('crossorigin', String(v)); }, decode() { return Promise.resolve(); } });
methods(SVGScriptElement.prototype, { get type() { return N.attr(this, 'type') || ''; }, set type(v) { this.setAttribute('type', String(v)); }, get crossOrigin() { return N.attr(this, 'crossorigin'); }, set crossOrigin(v) { if (v === null) this.removeAttribute('crossorigin'); else this.setAttribute('crossorigin', String(v)); } });
methods(SVGStyleElement.prototype, { get type() { return N.attr(this, 'type') || ''; }, set type(v) { this.setAttribute('type', String(v)); }, get media() { return N.attr(this, 'media') || ''; }, set media(v) { this.setAttribute('media', String(v)); }, get title() { return N.attr(this, 'title') || ''; }, set title(v) { this.setAttribute('title', String(v)); }, get disabled() { return !!this.__dis; }, set disabled(v) { def(this, '__dis', !!v); } });
const SVGViewElement = svgClass('SVGViewElement', SVGElement, ['view'], [['viewBox', 'rect']]); parAcc(SVGViewElement);
const SVGMPathElement = svgClass('SVGMPathElement', SVGElement, ['mpath'], [['href', 'str']]);
const SVGAnimationElement = svgClass('SVGAnimationElement', SVGElement, []); testsAcc(SVGAnimationElement);
methods(SVGAnimationElement.prototype, {
    get targetElement() { const h = N.attr(this, 'href') || this.getAttributeNS('http://www.w3.org/1999/xlink', 'href'); return h && h[0] === '#' ? (N.doc(this).getElementById(h.slice(1))) : this.parentElement; },
    getStartTime() { throw new DOMException('No current interval.', 'InvalidStateError'); }, getCurrentTime() { return 0; },
    getSimpleDuration() { const d = parseFloat(N.attr(this, 'dur')); if (!(d >= 0)) throw new DOMException('No simple duration.', 'NotSupportedError'); return d; },
    beginElement() {}, beginElementAt() {}, endElement() {}, endElementAt() {},
});
for (const k of ['onbegin', 'onend', 'onrepeat']) Object.defineProperty(SVGAnimationElement.prototype, k, { get() { return this['__' + k] ?? null; }, set(v) { if (this['__' + k]) this.removeEventListener(k.slice(2), this['__' + k]); def(this, '__' + k, typeof v === 'function' ? v : null); if (typeof v === 'function') this.addEventListener(k.slice(2), v); }, enumerable: true, configurable: true });
const SVGAnimateElement = svgClass('SVGAnimateElement', SVGAnimationElement, ['animate']);
const SVGSetElement = svgClass('SVGSetElement', SVGAnimationElement, ['set']);
const SVGAnimateMotionElement = svgClass('SVGAnimateMotionElement', SVGAnimationElement, ['animateMotion']);
const SVGAnimateTransformElement = svgClass('SVGAnimateTransformElement', SVGAnimationElement, ['animateTransform']);
class TimeEvent extends Event {
    constructor() { illegal(); }
    get view() { return this._view ?? null; } get detail() { return this._detail ?? 0; }
    initTimeEvent(type, view, detail) { this.initEvent(type, false, false); def(this, '_view', view); def(this, '_detail', detail | 0); }
}
