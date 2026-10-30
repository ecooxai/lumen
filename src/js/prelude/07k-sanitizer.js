/* The HTML Sanitizer API, the HTML insertion methods (setHTML() and its kin, with their Unsafe counterparts),
   Document.parseHTML(Unsafe)() and getHTML(). The parser sanitizes while it builds the tree (parseFragEx and
   parseDocEx take the canonical configuration flattened). */
const SAN_HTML = NSURI[0], SAN_SVG = NSURI[1], SAN_MATHML = NSURI[2];
const sanStringValue = String;
const sanErr = (m, what) => new TypeError(`Failed to ${m}: ${what}`);
const sanStr = (v, m) => { if (typeof v === 'symbol') throw sanErr(m, 'Cannot convert a Symbol value to a string.'); return sanStringValue(v); };
const sanIsObj = v => v !== null && (typeof v === 'object' || typeof v === 'function');
const sanObjectCreate = Object.create, sanObjectKeys = Object.keys, sanOwnDescriptor = Object.getOwnPropertyDescriptor;
const sanDefine = Object.defineProperty, sanArrayIsArray = Array.isArray;
const sanApply = Reflect.apply, sanArraySort = Array.prototype.sort, sanRegexExec = RegExp.prototype.exec;
const sanStartsWith = String.prototype.startsWith;
/* Only author sequences use their iterator. Private canonical records, lists and
   descriptors must not invoke mutable page prototypes while being consumed. */
function sanData(object, key, value) {
    const descriptor = sanObjectCreate(null);
    descriptor.value = value; descriptor.writable = true;
    descriptor.enumerable = true; descriptor.configurable = true;
    sanDefine(object, key, descriptor);
}
function sanAppend(list, value) { sanData(list, list.length, value); }
function sanRecord(name, namespace) {
    const record = sanObjectCreate(null); record.name = name; record.namespace = namespace; return record;
}
function sanSome(list, predicate) {
    if (list) for (let i = 0; i < list.length; i++) if (predicate(list[i])) return true;
    return false;
}
function sanFind(list, predicate) {
    for (let i = 0; i < list.length; i++) if (predicate(list[i])) return list[i];
    return undefined;
}
function sanFilter(list, predicate) {
    const out = [];
    for (let i = 0; i < list.length; i++) if (predicate(list[i])) sanAppend(out, list[i]);
    return out;
}
function sanSort(list, compare) { return sanApply(sanArraySort, list, [compare]); }
function sanSeq(v, m, name, each) {
    if (!sanIsObj(v) || typeof v[Symbol.iterator] !== 'function') throw sanErr(m, `The provided value cannot be converted to a sequence (${name}).`);
    const out = [];
    for (const x of v) sanAppend(out, each(x));
    return out;
}
/* a SanitizerElement / SanitizerAttribute, canonical: a string gets the default namespace, '' is null */
function sanName(v, defNs, m, withAttrs) {
    if (v !== undefined && v !== null && !sanIsObj(v)) return sanRecord(sanStr(v, m), defNs);
    const d = v == null ? sanObjectCreate(null) : v;
    const name = d.name;
    if (name === undefined) throw sanErr(m, "Failed to read the 'name' property: Required member is undefined.");
    const r = sanRecord(sanStr(name, m), null);
    const ns = d.namespace;
    r.namespace = ns === undefined ? defNs : ns === null ? null : sanStr(ns, m);
    if (r.namespace === '') r.namespace = null;
    if (withAttrs) {
        const a = d.attributes;
        if (a !== undefined) r.attributes = sanSeq(a, m, 'attributes', x => sanName(x, null, m));
        const ra = d.removeAttributes;
        if (ra !== undefined) r.removeAttributes = sanSeq(ra, m, 'removeAttributes', x => sanName(x, null, m));
    }
    return r;
}
function sanPI(v, m) {
    if (v !== undefined && v !== null && !sanIsObj(v)) { const r = sanObjectCreate(null); r.target = sanStr(v, m); return r; }
    const t = v == null ? undefined : v.target;
    if (t === undefined) throw sanErr(m, "Failed to read the 'target' property: Required member is undefined.");
    const r = sanObjectCreate(null); r.target = sanStr(t, m); return r;
}
/* the SanitizerConfig dictionary, its members read in order */
function sanDict(v, m) {
    if (v == null) v = sanObjectCreate(null);
    else if (!sanIsObj(v)) throw sanErr(m, "The provided value is not of type 'SanitizerConfig'.");
    const c = sanObjectCreate(null), rd = k => v[k];
    let x;
    if ((x = rd('attributes')) !== undefined) c.attributes = sanSeq(x, m, 'attributes', y => sanName(y, null, m));
    if ((x = rd('comments')) !== undefined) c.comments = !!x;
    if ((x = rd('dataAttributes')) !== undefined) c.dataAttributes = !!x;
    if ((x = rd('elements')) !== undefined) c.elements = sanSeq(x, m, 'elements', y => sanElemWithAttrs(y, m));
    if ((x = rd('javascriptURLs')) !== undefined) c.javascriptURLs = !!x;
    if ((x = rd('processingInstructions')) !== undefined) c.processingInstructions = sanSeq(x, m, 'processingInstructions', y => sanPI(y, m));
    if ((x = rd('removeAttributes')) !== undefined) c.removeAttributes = sanSeq(x, m, 'removeAttributes', y => sanName(y, null, m));
    if ((x = rd('removeElements')) !== undefined) c.removeElements = sanSeq(x, m, 'removeElements', y => sanName(y, SAN_HTML, m));
    if ((x = rd('removeProcessingInstructions')) !== undefined) c.removeProcessingInstructions = sanSeq(x, m, 'removeProcessingInstructions', y => sanPI(y, m));
    if ((x = rd('replaceWithChildrenElements')) !== undefined) c.replaceWithChildrenElements = sanSeq(x, m, 'replaceWithChildrenElements', y => sanName(y, SAN_HTML, m));
    return c;
}
/* canonicalizing a SanitizerElementWithAttributes */
function sanElemWithAttrs(v, m) {
    const r = sanName(v, SAN_HTML, m, true);
    if (!r.attributes && !r.removeAttributes) r.removeAttributes = [];
    return r;
}
const sanSame = (a, b) => a.name === b.name && a.namespace === b.namespace;
const sanHas = (l, x) => sanSome(l, y => sanSame(x, y));
const sanPIHas = (l, x) => sanSome(l, y => y.target === x.target);
function sanDrop(l, x, same = sanSame) {
    if (!l) return false;
    let r = false;
    for (let i = l.length - 1; i >= 0; i--) if (same(l[i], x)) {
        for (let j = i; j + 1 < l.length; j++) l[j] = l[j + 1];
        l.length--; r = true;
    }
    return r;
}
const sanSet = l => { const o = []; for (let i = 0; i < l.length; i++) if (!sanHas(o, l[i])) sanAppend(o, l[i]); return o; };
const sanDups = l => { for (let i = 0; i < l.length; i++) for (let j = i + 1; j < l.length; j++) if (sanSame(l[i], l[j])) return true; return false; };
const sanPIDups = l => { for (let i = 0; i < l.length; i++) for (let j = i + 1; j < l.length; j++) if (l[i].target === l[j].target) return true; return false; };
const sanMeets = (a, b) => !!b && sanSome(a, x => sanHas(b, x));
const sanIsData = a => a.namespace === null && sanApply(sanStartsWith, a.name, ['data-']);
const SAN_NON_REPLACEABLE = [{ name: 'html', namespace: SAN_HTML }, { name: 'svg', namespace: SAN_SVG }, { name: 'math', namespace: SAN_MATHML }];
/* Configurations here contain only converted own data. Never serialize through
   author-replaceable JSON methods or inherited toJSON: either could rewrite or
   disclose a policy's already-vetted private sanitizer configuration. */
function sanClone(value, publicObjects = false) {
    if (value === null || typeof value !== 'object') return value;
    const out = sanArrayIsArray(value) ? [] : publicObjects ? {} : sanObjectCreate(null);
    const keys = sanObjectKeys(value);
    for (let i = 0; i < keys.length; i++) {
        const key = keys[i], desc = sanOwnDescriptor(value, key);
        if (desc && sanOwnDescriptor(desc, 'value')) sanData(out, key, sanClone(desc.value, publicObjects));
    }
    return out;
}

/* canonicalize the configuration (its items already are, from sanDict or a canonical config) */
function sanCanon(c, permissive) {
    if (!c.elements && !c.removeElements) c.removeElements = [];
    if (!c.attributes && !c.removeAttributes) c.removeAttributes = [];
    if (!c.processingInstructions && !c.removeProcessingInstructions) {
        if (permissive) c.removeProcessingInstructions = []; else c.processingInstructions = [];
    }
    if (c.elements) for (let i = 0; i < c.elements.length; i++) { const e = c.elements[i]; if (!e.attributes && !e.removeAttributes) e.removeAttributes = []; }
    if (c.comments === undefined) c.comments = permissive;
    if (c.attributes && c.dataAttributes === undefined) c.dataAttributes = permissive;
    if (c.javascriptURLs === undefined) c.javascriptURLs = permissive;
    return c;
}
function sanValid(c) {
    if (c.elements && c.removeElements) return false;
    if (c.processingInstructions && c.removeProcessingInstructions) return false;
    if (c.attributes && c.removeAttributes) return false;
    if (sanDups(c.elements || c.removeElements)) return false;
    if (c.replaceWithChildrenElements && sanDups(c.replaceWithChildrenElements)) return false;
    if (sanPIDups(c.processingInstructions || c.removeProcessingInstructions)) return false;
    if (sanDups(c.attributes || c.removeAttributes)) return false;
    if (c.replaceWithChildrenElements) {
        for (let i = 0; i < c.replaceWithChildrenElements.length; i++) if (sanHas(SAN_NON_REPLACEABLE, c.replaceWithChildrenElements[i])) return false;
        if (sanMeets(c.elements || c.removeElements, c.replaceWithChildrenElements)) return false;
    }
    if (c.attributes) {
        for (let i = 0; c.elements && i < c.elements.length; i++) { const e = c.elements[i];
            if (e.attributes && sanDups(e.attributes)) return false;
            if (e.removeAttributes && sanDups(e.removeAttributes)) return false;
            if (sanMeets(c.attributes, e.attributes || [])) return false;
            if (sanSome(e.removeAttributes, a => !sanHas(c.attributes, a))) return false;
            if (c.dataAttributes === true && sanSome(e.attributes, sanIsData)) return false;
        }
        if (c.dataAttributes === true && sanSome(c.attributes, sanIsData)) return false;
    } else {
        for (let i = 0; c.elements && i < c.elements.length; i++) { const e = c.elements[i];
            if (e.attributes && e.removeAttributes) return false;
            if (e.attributes && sanDups(e.attributes)) return false;
            if (e.removeAttributes && sanDups(e.removeAttributes)) return false;
            if (sanMeets(c.removeAttributes, e.attributes || [])) return false;
            if (sanMeets(c.removeAttributes, e.removeAttributes || [])) return false;
        }
        if (c.dataAttributes !== undefined) return false;
    }
    return true;
}
/* remove an element / an attribute from a configuration */
function sanRemoveElement(c, e) {
    const modified = sanDrop(c.replaceWithChildrenElements, e);
    if (c.elements) return sanDrop(c.elements, e) || modified;
    if (sanHas(c.removeElements, e)) return modified;
    sanAppend(c.removeElements, sanRecord(e.name, e.namespace));
    return true;
}
function sanRemoveAttribute(c, a) {
    if (c.attributes) {
        let modified = sanDrop(c.attributes, a);
        for (let i = 0; c.elements && i < c.elements.length; i++) { const e = c.elements[i];
            if (sanDrop(e.attributes, a)) modified = true;
            sanDrop(e.removeAttributes, a);
        }
        return modified;
    }
    if (sanHas(c.removeAttributes, a)) return false;
    for (let i = 0; c.elements && i < c.elements.length; i++) { const e = c.elements[i]; sanDrop(e.attributes, a); sanDrop(e.removeAttributes, a); }
    sanAppend(c.removeAttributes, sanRecord(a.name, a.namespace));
    return true;
}
const SAN_BASELINE = [['base', SAN_HTML], ['embed', SAN_HTML], ['frame', SAN_HTML], ['iframe', SAN_HTML], ['object', SAN_HTML], ['script', SAN_HTML], ['script', SAN_SVG], ['use', SAN_SVG]].map(([name, namespace]) => ({ name, namespace }));
/* the event handler content attributes */
const SAN_EVENT_ATTRS = ('abort afterprint animationcancel animationend animationiteration animationstart auxclick beforecopy beforecut beforeinput beforematch beforepaste beforeprint beforetoggle beforeunload beforexrselect begin blur cancel canplay canplaythrough change click close command contentvisibilityautostatechange contextlost contextmenu contextrestored copy cuechange cut dblclick drag dragend dragenter dragleave dragover dragstart drop durationchange emptied end ended error focus focusin focusout formdata gotpointercapture hashchange input invalid keydown keypress keyup languagechange load loadeddata loadedmetadata loadstart lostpointercapture message messageerror mousedown mouseenter mouseleave mousemove mouseout mouseover mouseup mousewheel offline online overscroll pagehide pagereveal pageshow pageswap paste pause play playing pointercancel pointerdown pointerenter pointerleave pointermove pointerout pointerover pointerrawupdate pointerup popstate progress ratechange rejectionhandled repeat reset resize scroll scrollend scrollsnapchange scrollsnapchanging search securitypolicyviolation seeked seeking select selectionchange selectstart slotchange stalled storage submit suspend timeupdate toggle touchcancel touchend touchmove touchstart transitioncancel transitionend transitionrun transitionstart unhandledrejection unload volumechange waiting webkitanimationend webkitanimationiteration webkitanimationstart webkitfullscreenchange webkitfullscreenerror webkittransitionend wheel').split(' ').map(n => ({ name: 'on' + n, namespace: null }));
function sanRemoveUnsafe(c) {
    let r = false;
    for (let i = 0; i < SAN_BASELINE.length; i++) if (sanRemoveElement(c, SAN_BASELINE[i])) r = true;
    for (let i = 0; i < SAN_EVENT_ATTRS.length; i++) if (sanRemoveAttribute(c, SAN_EVENT_ATTRS[i])) r = true;
    if (c.javascriptURLs === true) { r = true; c.javascriptURLs = false; }
    return r;
}
/* the built-in safe default configuration: elements as nsName[:attr,attr], with ns h(tml), s(vg) or m(athml) */
const SAN_DEFAULT_ELS = 'mmath mmerror mmfrac mmi mmmultiscripts mmn mmo:fence,form,largeop,lspace,maxsize,minsize,movablelimits,rspace,separator,stretchy,symmetric mmover:accent mmpadded:depth,height,lspace,voffset,width mmphantom mmprescripts mmroot mmrow mms mmspace:depth,height,width mmsqrt mmstyle mmsub mmsubsup mmsup mmtable mmtd:columnspan,rowspan mmtext mmtr mmunder:accentunder mmunderover:accent,accentunder msemantics ha:href,hreflang,type habbr haddress harticle haside hb hbdi hbdo hblockquote:cite hbody hbr hcaption hcite hcode hcol:span hcolgroup:span hdata:value hdd hdel:cite,datetime hdfn hdiv hdl hdt hem hfigcaption hfigure hfooter hh1 hh2 hh3 hh4 hh5 hh6 hhead hheader hhgroup hhr hhtml hi hins:cite,datetime hkbd hli:value hmain hmark hmenu hnav hol:reversed,start,type hp hpre hq hrp hrt hruby hs hsamp hsearch hsection hsmall hspan hstrong hsub hsup htable htbody htd:colspan,headers,rowspan htfoot hth:abbr,colspan,headers,rowspan,scope hthead htime:datetime htitle htr hu hul hvar hwbr sa:href,hreflang,type scircle:cx,cy,pathLength,r sdefs sdesc sellipse:cx,cy,pathLength,rx,ry sforeignObject:height,width,x,y sg sline:pathLength,x1,x2,y1,y2 smarker:markerHeight,markerUnits,markerWidth,orient,preserveAspectRatio,refX,refY,viewBox smetadata spath:d,pathLength spolygon:pathLength,points spolyline:pathLength,points srect:height,pathLength,rx,ry,width,x,y ssvg:height,preserveAspectRatio,viewBox,width,x,y stext:dx,dy,lengthAdjust,rotate,textLength,x,y stextPath:lengthAdjust,method,path,side,spacing,startOffset,textLength stitle stspan:dx,dy,lengthAdjust,rotate,textLength,x,y';
const SAN_DEFAULT_ATTRS = 'alignment-baseline baseline-shift clip-path clip-rule color color-interpolation cursor dir direction display displaystyle dominant-baseline fill fill-opacity fill-rule font-family font-size font-size-adjust font-stretch font-style font-variant font-weight lang letter-spacing marker-end marker-mid marker-start mathbackground mathcolor mathsize opacity paint-order pointer-events scriptlevel shape-rendering stop-color stop-opacity stroke stroke-dasharray stroke-dashoffset stroke-linecap stroke-linejoin stroke-miterlimit stroke-opacity stroke-width text-anchor text-decoration text-overflow text-rendering title transform transform-origin unicode-bidi vector-effect visibility white-space word-spacing writing-mode';
function sanMakeDefault() {
    const ns = { h: SAN_HTML, s: SAN_SVG, m: SAN_MATHML };
    const attr = name => ({ name, namespace: null });
    return {
        elements: SAN_DEFAULT_ELS.split(' ').map(s => { const [n, a] = s.slice(1).split(':'); return { name: n, namespace: ns[s[0]], attributes: a ? a.split(',').map(attr) : [] }; }),
        processingInstructions: [], attributes: SAN_DEFAULT_ATTRS.split(' ').map(attr),
        comments: false, dataAttributes: false, javascriptURLs: false,
    };
}
/* Materialize the browser's default before author code can replace String or
   Array methods. Subsequent users receive an intrinsic-safe canonical copy. */
const SAN_DEFAULT_CONFIG = sanClone(sanMakeDefault());
function sanDefault() { return sanClone(SAN_DEFAULT_CONFIG); }
const sanCompare = (a, b) => {
    if (a.namespace !== b.namespace) {
        if (a.namespace === null) return -1;
        if (b.namespace === null) return 1;
        return a.namespace < b.namespace ? -1 : 1;
    }
    return a.name < b.name ? -1 : a.name > b.name ? 1 : 0;
};
const sanPICompare = (a, b) => a.target < b.target ? -1 : a.target > b.target ? 1 : 0;

const SAN_KEY = Symbol('sanitizer');
const sanCfgs = new WeakMap();
function sanConfigOf(s) { return ttRead(sanCfgs, s) || N.trustedData(s, 5) || null; }
function sanStoreConfig(s, c) { ttStore(sanCfgs, s, c); N.trustedBrand(s, 5, c); }
function sanOf(s, m) { const c = sanConfigOf(s); if (!c) throw new TypeError(`Failed to execute '${m}' on 'Sanitizer': Illegal invocation`); return c; }
/* configure: a fresh canonical copy, or a TypeError */
function sanConfigure(input, permissive, m) {
    const c = sanCanon(sanClone(input), permissive);
    if (!sanValid(c)) throw sanErr(m, 'The configuration is invalid.');
    return c;
}
class Sanitizer {
    constructor(configuration = 'default') {
        const m = "construct 'Sanitizer'";
        let input;
        if (configuration !== null && !sanIsObj(configuration)) {
            const s = sanStr(configuration, m);
            if (s !== 'default') throw sanErr(m, `The provided value '${s}' is not a valid enum value of type SanitizerPresets.`);
            input = sanDefault();
        } else input = sanDict(configuration, m);
        sanStoreConfig(this, sanConfigure(input, true, m));
    }
    get() {
        const c = sanClone(sanOf(this, 'get'));
        if (c.elements) {
            for (let i = 0; i < c.elements.length; i++) {
                const e = c.elements[i];
                if (e.attributes) sanSort(e.attributes, sanCompare);
                if (e.removeAttributes) sanSort(e.removeAttributes, sanCompare);
            }
            sanSort(c.elements, sanCompare);
        } else sanSort(c.removeElements, sanCompare);
        if (c.replaceWithChildrenElements) sanSort(c.replaceWithChildrenElements, sanCompare);
        if (c.processingInstructions) sanSort(c.processingInstructions, sanPICompare); else sanSort(c.removeProcessingInstructions, sanPICompare);
        if (c.attributes) sanSort(c.attributes, sanCompare); else sanSort(c.removeAttributes, sanCompare);
        return sanClone(c, true);
    }
    allowElement(element) {
        const c = sanOf(this, 'allowElement'), m = "execute 'allowElement' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        const e = sanName(element, SAN_HTML, m, true);
        if (c.elements) {
            const modified = sanDrop(c.replaceWithChildrenElements, e);
            if (c.attributes) {
                if (e.attributes) {
                    e.attributes = sanFilter(sanSet(e.attributes), a => !sanHas(c.attributes, a));
                    if (c.dataAttributes === true) e.attributes = sanFilter(e.attributes, a => !sanIsData(a));
                }
                if (e.removeAttributes) e.removeAttributes = sanFilter(sanSet(e.removeAttributes), a => sanHas(c.attributes, a));
            } else {
                if (e.attributes) {
                    e.attributes = sanFilter(sanSet(e.attributes), a => !sanHas(e.removeAttributes, a));
                    delete e.removeAttributes;
                    e.attributes = sanFilter(e.attributes, a => !sanHas(c.removeAttributes, a));
                }
                if (e.removeAttributes) e.removeAttributes = sanFilter(sanSet(e.removeAttributes), a => !sanHas(c.removeAttributes, a));
            }
            if (!e.attributes && !e.removeAttributes) e.removeAttributes = [];
            const cur = sanFind(c.elements, x => sanSame(x, e));
            if (!cur) { sanAppend(c.elements, e); return true; }
            if (sanEqualElement(cur, e)) return modified;
            sanDrop(c.elements, e);
            sanAppend(c.elements, e);
            return true;
        }
        if (e.attributes || (e.removeAttributes || []).length) return false;
        const modified = sanDrop(c.replaceWithChildrenElements, e);
        if (!sanHas(c.removeElements, e)) return modified;
        sanDrop(c.removeElements, e);
        return true;
    }
    removeElement(element) {
        const c = sanOf(this, 'removeElement'), m = "execute 'removeElement' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        return sanRemoveElement(c, sanName(element, SAN_HTML, m));
    }
    replaceElementWithChildren(element) {
        const c = sanOf(this, 'replaceElementWithChildren'), m = "execute 'replaceElementWithChildren' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        const e = sanName(element, SAN_HTML, m);
        if (sanHas(SAN_NON_REPLACEABLE, e)) return false;
        let modified = sanDrop(c.elements, e);
        if (sanDrop(c.removeElements, e)) modified = true;
        if (!c.replaceWithChildrenElements) c.replaceWithChildrenElements = [];
        if (!sanHas(c.replaceWithChildrenElements, e)) { sanAppend(c.replaceWithChildrenElements, e); return true; }
        return modified;
    }
    allowProcessingInstruction(pi) {
        const c = sanOf(this, 'allowProcessingInstruction'), m = "execute 'allowProcessingInstruction' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        const p = sanPI(pi, m);
        if (c.processingInstructions) {
            if (sanPIHas(c.processingInstructions, p)) return false;
            sanAppend(c.processingInstructions, p);
            return true;
        }
        return sanDrop(c.removeProcessingInstructions, p, (a, b) => a.target === b.target);
    }
    removeProcessingInstruction(pi) {
        const c = sanOf(this, 'removeProcessingInstruction'), m = "execute 'removeProcessingInstruction' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        const p = sanPI(pi, m);
        if (c.processingInstructions) return sanDrop(c.processingInstructions, p, (a, b) => a.target === b.target);
        if (sanPIHas(c.removeProcessingInstructions, p)) return false;
        sanAppend(c.removeProcessingInstructions, p);
        return true;
    }
    allowAttribute(attribute) {
        const c = sanOf(this, 'allowAttribute'), m = "execute 'allowAttribute' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        const a = sanName(attribute, null, m);
        if (c.attributes) {
            if (c.dataAttributes === true && sanIsData(a)) return false;
            if (sanHas(c.attributes, a)) return false;
            for (let i = 0; c.elements && i < c.elements.length; i++) sanDrop(c.elements[i].attributes, a);
            sanAppend(c.attributes, a);
            return true;
        }
        return sanDrop(c.removeAttributes, a);
    }
    removeAttribute(attribute) {
        const c = sanOf(this, 'removeAttribute'), m = "execute 'removeAttribute' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        return sanRemoveAttribute(c, sanName(attribute, null, m));
    }
    setComments(allow) {
        const c = sanOf(this, 'setComments'); allow = !!allow;
        if (c.comments === allow) return false;
        c.comments = allow;
        return true;
    }
    setDataAttributes(allow) {
        const c = sanOf(this, 'setDataAttributes'); allow = !!allow;
        if (!c.attributes) return false;
        if (c.dataAttributes === allow) return false;
        if (allow) {
            for (let i = 0; c.elements && i < c.elements.length; i++) { const e = c.elements[i]; if (e.attributes) e.attributes = sanFilter(e.attributes, a => !sanIsData(a)); }
            c.attributes = sanFilter(c.attributes, a => !sanIsData(a));
        }
        c.dataAttributes = allow;
        return true;
    }
    setJavascriptURLs(allow) {
        const c = sanOf(this, 'setJavascriptURLs'); allow = !!allow;
        if (c.javascriptURLs === allow) return false;
        c.javascriptURLs = allow;
        return true;
    }
    removeUnsafe() { return sanRemoveUnsafe(sanOf(this, 'removeUnsafe')); }
}
function sanEqualNames(a, b) {
    if (!a || !b) return a === b;
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (!sanHas(b, a[i])) return false;
    return true;
}
function sanEqualElement(a, b) {
    return sanSame(a, b) && sanEqualNames(a.attributes, b.attributes) && sanEqualNames(a.removeAttributes, b.removeAttributes);
}
for (const k of Object.getOwnPropertyNames(Sanitizer.prototype)) if (k !== 'constructor') Object.defineProperty(Sanitizer.prototype, k, { enumerable: true });
Object.defineProperty(Sanitizer.prototype, Symbol.toStringTag, { value: 'Sanitizer', configurable: true });

/* SetHTMLOptions / SetHTMLUnsafeOptions: { sanitizer, runScripts } */
function sanOptions(o, m, unsafe) {
    if (o == null) return sanObjectCreate(null);
    if (!sanIsObj(o)) throw sanErr(m, "The provided value is not of type 'SetHTMLOptions'.");
    const r = sanObjectCreate(null);
    if (unsafe) r.runScripts = !!o.runScripts;
    const s = o.sanitizer;
    if (s === undefined) return r;
    if (sanConfigOf(s)) r.sanitizer = s;
    else if (s === null || sanIsObj(s)) r.sanitizer = sanDict(s, m);
    else {
        const v = sanStr(s, m);
        if (v !== 'default') throw sanErr(m, `The provided value '${v}' is not a valid enum value of type SanitizerPresets.`);
        r.sanitizer = 'default';
    }
    return r;
}
let sanSafeDefault = null;
/* get a sanitizer config from options: null when unsafe without a sanitizer */
function sanFromOptions(o, safe, m) {
    /* Only internal dictionaries carry cfg; author dictionary conversion never
       reads that member. Trusted options retain the flat snapshot made at mint. */
    if (o.cfg !== undefined) return o.cfg;
    let input = o.sanitizer;
    if (input === undefined) { if (!safe) return null; input = 'default'; }
    if (input === 'default' && safe) {
        if (!sanSafeDefault) { const c = sanConfigure(sanDefault(), false, m); sanRemoveUnsafe(c); sanSafeDefault = sanFlat(c); }
        return sanSafeDefault;
    }
    if (input === 'default') input = sanDefault();
    else { const known = sanConfigOf(input); if (known) input = known; }
    const c = sanConfigure(input, !safe, m);
    if (safe) sanRemoveUnsafe(c);
    return sanFlat(c);
}
/* the flat form parseFragEx and parseDocEx read */
function sanFlat(c) {
    const names = list => {
        if (!list) return undefined;
        const out = [];
        for (let i = 0; i < list.length; i++) { sanAppend(out, list[i].namespace); sanAppend(out, list[i].name); }
        return out;
    };
    const r = sanObjectCreate(null);
    r.comments = !!c.comments; r.dataAttributes = !!c.dataAttributes; r.javascriptURLs = !!c.javascriptURLs;
    if (c.elements) {
        r.elements = [];
        for (let i = 0; i < c.elements.length; i++) {
            const e = c.elements[i];
            sanAppend(r.elements, e.namespace); sanAppend(r.elements, e.name);
            sanAppend(r.elements, names(e.attributes) || null); sanAppend(r.elements, names(e.removeAttributes) || null);
        }
    }
    const nameKeys = ['removeElements', 'replaceWithChildrenElements', 'attributes', 'removeAttributes'];
    for (let i = 0; i < nameKeys.length; i++) { const k = nameKeys[i]; if (c[k]) r[k] = names(c[k]); }
    const piKeys = ['processingInstructions', 'removeProcessingInstructions'];
    for (let i = 0; i < piKeys.length; i++) {
        const k = piKeys[i];
        if (c[k]) { const list = []; for (let j = 0; j < c[k].length; j++) sanAppend(list, c[k][j].target); r[k] = list; }
    }
    return r;
}
/* parse HTML for insertion: the fragment, or null for a script context in Safe mode */
function htmlForInsertion(target, html, o, safe, m) {
    const cfg = sanFromOptions(o, safe, m);
    const cx = N.type(target) === 1 ? target : N.host(target) || ttRead(tplOwner, target) || null;
    if (safe && cx && N.type(cx) === 1 && cx.localName === 'script' && (cx.namespaceURI === SAN_HTML || cx.namespaceURI === SAN_SVG)) return null;
    const f = N.parseFragEx(cx, html, cfg, true, !!o.runScripts);
    /* Parsing establishes each new script's trusted text before insertion can
       prepare it. Later author text mutations still pass through ttScript. */
    if (f && ttReq && o.runScripts) ttSlotsFrom(f);
    return f;
}
/* a template's contents stand for it as the parser context */
const tplOwner = new WeakMap();
function htmlTarget(el) {
    if (el.localName === 'template' && el.namespaceURI === SAN_HTML) { const c = N.templateContent(el); ttStore(tplOwner, c, el); return c; }
    return el;
}
function htmlParent(node) {
    const p = N.parent(node);
    if (!p) return null;
    if (N.type(p) === 1 && p.localName === 'template' && p.namespaceURI === SAN_HTML) throw hre('The parent is a template element.');
    if (N.type(p) === 1 || (N.type(p) === 11 && (N.host(p) || N.tplHost(p)))) return p;
    throw hre('The parent is not an element or a shadow root.');
}
const isHtmlRoot = p => isHtmlEl(p, 'html');
/* The private argument record never goes through an author-controlled array
   iterator: doing so would disclose the vetted options to page code. */
function htmlResult(html, options) { const r = sanObjectCreate(null); r.html = html; r.options = options; return r; }
function htmlSinkOptions(runScripts) { const r = sanObjectCreate(null); r.runScripts = runScripts; return r; }
function htmlArgs(args, safe, m, tt = !safe) {
    if (!args.length) throw sanErr(m, '1 argument required, but only 0 present.');
    if (!tt) return htmlResult(sanStr(args[0], m), sanOptions(args[1], m, !safe));
    /* Convert the markup and dictionary once, then vet options before markup.
       A branded options object is recognized by its private slot, not prototype. */
    const html = ttIsHTML(args[0]) ? args[0] : sanStr(args[0], m);
    const options = ttParserOpts(args[1]) ? args[1] : sanOptions(args[1], m, !safe);
    const match = sanApply(sanRegexExec, /'(\w+)' on '(\w+)'/, [m]), fn = match[1], on = match[2];
    const sink = ((fn === 'beforeHTMLUnsafe' || fn === 'afterHTMLUnsafe' || fn === 'replaceWithHTMLUnsafe') ? 'Node' : on) + ' ' + fn;
    return ttCompliantHTML(html, options, sink, false, true);
}
function htmlSet(target, a, safe, m) {
    const f = htmlForInsertion(target, a.html, a.options, safe, m);
    if (f) ParentNode.replaceChildren.call(target, f);
}
function htmlInsert(parent, child, a, safe, m) {
    if (isHtmlRoot(parent)) throw hre('The parent is an html element.');
    const f = htmlForInsertion(parent, a.html, a.options, safe, m);
    if (f) insertNode(parent, f, child);
}
function htmlReplace(parent, node, a, safe, m) {
    if (isHtmlRoot(parent)) throw hre('The parent is an html element.');
    const f = htmlForInsertion(parent, a.html, a.options, safe, m);
    if (f) parent.replaceChild(f, node);
}
/* GetHTMLOptions */
function getHTMLOf(node, o, m) {
    let ser = false, roots = [];
    if (o != null) {
        if (!sanIsObj(o)) throw sanErr(m, "The provided value is not of type 'GetHTMLOptions'.");
        ser = !!o.serializableShadowRoots;
        if (o.shadowRoots !== undefined) roots = sanSeq(o.shadowRoots, m, 'shadowRoots', r => { if (!(r instanceof ShadowRoot)) throw sanErr(m, "Failed to convert value to 'ShadowRoot'."); return r; });
    }
    return N.getHTML(node, ser, roots);
}
const elM = k => `execute '${k}' on 'Element'`, srM = k => `execute '${k}' on 'ShadowRoot'`;
/* setHTML, appendHTML and prependHTML with their Unsafe counterparts, for an Element (via its target) or a ShadowRoot */
function htmlParentMethods(proto, target, msg) {
    for (const u of ['', 'Unsafe']) {
        const safe = !u;
        methods(proto, {
            [`setHTML${u}`](html, options = undefined) { const m = msg(`setHTML${u}`); htmlSet(target(this), htmlArgs(arguments, safe, m), safe, m); },
            [`appendHTML${u}`](html, options = undefined) { const m = msg(`appendHTML${u}`); htmlInsert(target(this), null, htmlArgs(arguments, safe, m), safe, m); },
            [`prependHTML${u}`](html, options = undefined) { const m = msg(`prependHTML${u}`), a = htmlArgs(arguments, safe, m), t = target(this); htmlInsert(t, N.first(t), a, safe, m); },
        });
    }
    methods(proto, { getHTML(options = undefined) { return getHTMLOf(this, options, msg('getHTML')); } });
}
htmlParentMethods(Element.prototype, htmlTarget, elM);
htmlParentMethods(ShadowRoot.prototype, x => x, srM);
/* beforeHTML, afterHTML and replaceWithHTML (NonDocumentTypeChildNode: Element and CharacterData) */
for (const proto of [Element.prototype, CharacterData.prototype]) for (const u of ['', 'Unsafe']) {
    const safe = !u, msg = k => `execute '${k}' on '${proto === Element.prototype ? 'Element' : 'CharacterData'}'`;
    methods(proto, {
        [`beforeHTML${u}`](html, options = undefined) {
            const m = msg(`beforeHTML${u}`), a = htmlArgs(arguments, safe, m);
            const p = htmlParent(this); if (p) htmlInsert(p, this, a, safe, m);
        },
        [`afterHTML${u}`](html, options = undefined) {
            const m = msg(`afterHTML${u}`), a = htmlArgs(arguments, safe, m);
            const p = htmlParent(this); if (p) htmlInsert(p, N.next(this), a, safe, m);
        },
        [`replaceWithHTML${u}`](html, options = undefined) {
            const m = msg(`replaceWithHTML${u}`), a = htmlArgs(arguments, safe, m);
            const p = htmlParent(this); if (p) htmlReplace(p, this, a, safe, m);
        },
    });
}
/* stream*HTML(Unsafe)(options): a WritableStream of strings whose markup is applied, with the matching
   non-streaming method, once the stream closes */
function htmlStream(self, name, safe, options, apply) {
    const positional = name === 'streamBeforeHTML' || name === 'streamBeforeHTMLUnsafe' || name === 'streamAfterHTML' || name === 'streamAfterHTMLUnsafe' || name === 'streamReplaceWithHTML' || name === 'streamReplaceWithHTMLUnsafe';
    const on = positional ? 'Node' : N.type(self) === 11 ? 'ShadowRoot' : 'Element';
    const m = `execute '${name}' on '${on}'`;
    let o;
    if (safe) o = sanOptions(options, m, false);
    else {
        const converted = ttParserOpts(options) ? options : sanOptions(options, m, true);
        const compliant = ttCompliantParserOptions(converted, on + ' ' + name, true);
        o = ttParserOpts(compliant) || compliant;
    }
    /* Streaming cannot vet future chunks with createHTML. The parser policy and
       configuration are settled now, before any tree changes or writer exists. */
    const a = sanObjectCreate(null); a.runScripts = !!o.runScripts; a.cfg = sanFromOptions(o, safe, m);
    let buf = '';
    return new WritableStream({ write(c) { buf += String(c); }, close() { apply(htmlResult(buf, a), m); } });
}
for (const u of ['', 'Unsafe']) {
    const safe = !u, st = x => N.type(x) === 1 ? htmlTarget(x) : x;
    for (const proto of [Element.prototype, ShadowRoot.prototype]) methods(proto, {
        [`streamHTML${u}`](options = undefined) { return htmlStream(this, `streamHTML${u}`, safe, options, (a, m) => htmlSet(st(this), a, safe, m)); },
        [`streamAppendHTML${u}`](options = undefined) { return htmlStream(this, `streamAppendHTML${u}`, safe, options, (a, m) => htmlInsert(st(this), null, a, safe, m)); },
        [`streamPrependHTML${u}`](options = undefined) { return htmlStream(this, `streamPrependHTML${u}`, safe, options, (a, m) => { const t = st(this); htmlInsert(t, N.first(t), a, safe, m); }); },
    });
    for (const proto of [Element.prototype, CharacterData.prototype]) methods(proto, {
        [`streamBeforeHTML${u}`](options = undefined) { return htmlStream(this, `streamBeforeHTML${u}`, safe, options, (a, m) => { const p = htmlParent(this); if (p) htmlInsert(p, this, a, safe, m); }); },
        [`streamAfterHTML${u}`](options = undefined) { return htmlStream(this, `streamAfterHTML${u}`, safe, options, (a, m) => { const p = htmlParent(this); if (p) htmlInsert(p, N.next(this), a, safe, m); }); },
        [`streamReplaceWithHTML${u}`](options = undefined) { return htmlStream(this, `streamReplaceWithHTML${u}`, safe, options, (a, m) => { const p = htmlParent(this); if (p) htmlReplace(p, this, a, safe, m); }); },
    });
}
/* Document.parseHTML(Unsafe)(): a new document, scripting disabled, declarative shadow roots allowed */
function parseHTMLDoc(args, safe, m) {
    const a = htmlArgs(args, true, m, !safe);
    return N.parseDocEx(a.html, sanFromOptions(a.options, safe, m));
}
Object.defineProperty(Document, 'parseHTML', { value: function parseHTML(html, options = undefined) { return parseHTMLDoc(arguments, true, "execute 'parseHTML' on 'Document'"); }, writable: true, configurable: true, enumerable: true });
Object.defineProperty(Document, 'parseHTMLUnsafe', { value: function parseHTMLUnsafe(html, options = undefined) { return parseHTMLDoc(arguments, false, "execute 'parseHTMLUnsafe' on 'Document'"); }, writable: true, configurable: true, enumerable: true });

/* HTMLTemplateElement's declarative shadow root attributes */
{
    const tp = TAGS.template.prototype;
    acc(tp, 'shadowRootMode', function () { const v = (N.attr(this, 'shadowrootmode') ?? '').toLowerCase(); return v === 'open' || v === 'closed' ? v : ''; }, function (v) { this.setAttribute('shadowrootmode', v); });
    reflectBool(tp, 'shadowRootDelegatesFocus', 'shadowRootClonable', 'shadowRootSerializable', 'shadowRootCustomElementRegistry');
    acc(tp, 'shadowRootSlotAssignment', function () { return (N.attr(this, 'shadowrootslotassignment') ?? '').toLowerCase() === 'manual' ? 'manual' : 'named'; }, function (v) { this.setAttribute('shadowrootslotassignment', v); });
}
