/* The HTML Sanitizer API, the HTML insertion methods (setHTML() and its kin, with their Unsafe counterparts),
   Document.parseHTML(Unsafe)() and getHTML(). The parser sanitizes while it builds the tree (parseFragEx and
   parseDocEx take the canonical configuration flattened). */
const SAN_HTML = NSURI[0], SAN_SVG = NSURI[1], SAN_MATHML = NSURI[2];
const sanErr = (m, what) => new TypeError(`Failed to ${m}: ${what}`);
const sanStr = (v, m) => { if (typeof v === 'symbol') throw sanErr(m, 'Cannot convert a Symbol value to a string.'); return String(v); };
const sanIsObj = v => v !== null && (typeof v === 'object' || typeof v === 'function');
function sanSeq(v, m, name, each) {
    if (!sanIsObj(v) || typeof v[Symbol.iterator] !== 'function') throw sanErr(m, `The provided value cannot be converted to a sequence (${name}).`);
    const out = [];
    for (const x of v) out.push(each(x));
    return out;
}
/* a SanitizerElement / SanitizerAttribute, canonical: a string gets the default namespace, '' is null */
function sanName(v, defNs, m, withAttrs) {
    if (v !== undefined && v !== null && !sanIsObj(v)) return { name: sanStr(v, m), namespace: defNs };
    const d = v == null ? {} : v;
    const name = d.name;
    if (name === undefined) throw sanErr(m, "Failed to read the 'name' property: Required member is undefined.");
    const r = { name: sanStr(name, m), namespace: null };
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
    if (v !== undefined && v !== null && !sanIsObj(v)) return { target: sanStr(v, m) };
    const t = v == null ? undefined : v.target;
    if (t === undefined) throw sanErr(m, "Failed to read the 'target' property: Required member is undefined.");
    return { target: sanStr(t, m) };
}
/* the SanitizerConfig dictionary, its members read in order */
function sanDict(v, m) {
    if (v == null) v = {};
    else if (!sanIsObj(v)) throw sanErr(m, "The provided value is not of type 'SanitizerConfig'.");
    const c = {}, rd = k => v[k];
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
const sanHas = (l, x) => !!l && l.some(y => sanSame(x, y));
const sanPIHas = (l, x) => !!l && l.some(y => y.target === x.target);
function sanDrop(l, x, same = sanSame) {
    if (!l) return false;
    let r = false;
    for (let i = l.length - 1; i >= 0; i--) if (same(l[i], x)) { l.splice(i, 1); r = true; }
    return r;
}
const sanSet = l => { const o = []; for (const x of l) if (!sanHas(o, x)) o.push(x); return o; };
const sanDups = l => { for (let i = 0; i < l.length; i++) for (let j = i + 1; j < l.length; j++) if (sanSame(l[i], l[j])) return true; return false; };
const sanPIDups = l => { for (let i = 0; i < l.length; i++) for (let j = i + 1; j < l.length; j++) if (l[i].target === l[j].target) return true; return false; };
const sanMeets = (a, b) => !!a && !!b && a.some(x => sanHas(b, x));
const sanIsData = a => a.namespace === null && a.name.startsWith('data-');
const SAN_NON_REPLACEABLE = [{ name: 'html', namespace: SAN_HTML }, { name: 'svg', namespace: SAN_SVG }, { name: 'math', namespace: SAN_MATHML }];
const sanClone = c => JSON.parse(JSON.stringify(c));

/* canonicalize the configuration (its items already are, from sanDict or a canonical config) */
function sanCanon(c, permissive) {
    if (!c.elements && !c.removeElements) c.removeElements = [];
    if (!c.attributes && !c.removeAttributes) c.removeAttributes = [];
    if (!c.processingInstructions && !c.removeProcessingInstructions) {
        if (permissive) c.removeProcessingInstructions = []; else c.processingInstructions = [];
    }
    if (c.elements) for (const e of c.elements) if (!e.attributes && !e.removeAttributes) e.removeAttributes = [];
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
        for (const e of c.replaceWithChildrenElements) if (sanHas(SAN_NON_REPLACEABLE, e)) return false;
        if (sanMeets(c.elements || c.removeElements, c.replaceWithChildrenElements)) return false;
    }
    if (c.attributes) {
        for (const e of c.elements || []) {
            if (e.attributes && sanDups(e.attributes)) return false;
            if (e.removeAttributes && sanDups(e.removeAttributes)) return false;
            if (sanMeets(c.attributes, e.attributes || [])) return false;
            if ((e.removeAttributes || []).some(a => !sanHas(c.attributes, a))) return false;
            if (c.dataAttributes === true && (e.attributes || []).some(sanIsData)) return false;
        }
        if (c.dataAttributes === true && c.attributes.some(sanIsData)) return false;
    } else {
        for (const e of c.elements || []) {
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
    c.removeElements.push({ name: e.name, namespace: e.namespace });
    return true;
}
function sanRemoveAttribute(c, a) {
    if (c.attributes) {
        let modified = sanDrop(c.attributes, a);
        for (const e of c.elements || []) {
            if (sanDrop(e.attributes, a)) modified = true;
            sanDrop(e.removeAttributes, a);
        }
        return modified;
    }
    if (sanHas(c.removeAttributes, a)) return false;
    for (const e of c.elements || []) { sanDrop(e.attributes, a); sanDrop(e.removeAttributes, a); }
    c.removeAttributes.push({ name: a.name, namespace: a.namespace });
    return true;
}
const SAN_BASELINE = [['base', SAN_HTML], ['embed', SAN_HTML], ['frame', SAN_HTML], ['iframe', SAN_HTML], ['object', SAN_HTML], ['script', SAN_HTML], ['script', SAN_SVG], ['use', SAN_SVG]].map(([name, namespace]) => ({ name, namespace }));
/* the event handler content attributes */
const SAN_EVENT_ATTRS = ('abort afterprint animationcancel animationend animationiteration animationstart auxclick beforecopy beforecut beforeinput beforematch beforepaste beforeprint beforetoggle beforeunload beforexrselect begin blur cancel canplay canplaythrough change click close command contentvisibilityautostatechange contextlost contextmenu contextrestored copy cuechange cut dblclick drag dragend dragenter dragleave dragover dragstart drop durationchange emptied end ended error focus focusin focusout formdata gotpointercapture hashchange input invalid keydown keypress keyup languagechange load loadeddata loadedmetadata loadstart lostpointercapture message messageerror mousedown mouseenter mouseleave mousemove mouseout mouseover mouseup mousewheel offline online overscroll pagehide pagereveal pageshow pageswap paste pause play playing pointercancel pointerdown pointerenter pointerleave pointermove pointerout pointerover pointerrawupdate pointerup popstate progress ratechange rejectionhandled repeat reset resize scroll scrollend scrollsnapchange scrollsnapchanging search securitypolicyviolation seeked seeking select selectionchange selectstart slotchange stalled storage submit suspend timeupdate toggle touchcancel touchend touchmove touchstart transitioncancel transitionend transitionrun transitionstart unhandledrejection unload volumechange waiting webkitanimationend webkitanimationiteration webkitanimationstart webkitfullscreenchange webkitfullscreenerror webkittransitionend wheel').split(' ').map(n => ({ name: 'on' + n, namespace: null }));
function sanRemoveUnsafe(c) {
    let r = false;
    for (const e of SAN_BASELINE) if (sanRemoveElement(c, e)) r = true;
    for (const a of SAN_EVENT_ATTRS) if (sanRemoveAttribute(c, a)) r = true;
    if (c.javascriptURLs === true) { r = true; c.javascriptURLs = false; }
    return r;
}
/* the built-in safe default configuration: elements as nsName[:attr,attr], with ns h(tml), s(vg) or m(athml) */
const SAN_DEFAULT_ELS = 'mmath mmerror mmfrac mmi mmmultiscripts mmn mmo:fence,form,largeop,lspace,maxsize,minsize,movablelimits,rspace,separator,stretchy,symmetric mmover:accent mmpadded:depth,height,lspace,voffset,width mmphantom mmprescripts mmroot mmrow mms mmspace:depth,height,width mmsqrt mmstyle mmsub mmsubsup mmsup mmtable mmtd:columnspan,rowspan mmtext mmtr mmunder:accentunder mmunderover:accent,accentunder msemantics ha:href,hreflang,type habbr haddress harticle haside hb hbdi hbdo hblockquote:cite hbody hbr hcaption hcite hcode hcol:span hcolgroup:span hdata:value hdd hdel:cite,datetime hdfn hdiv hdl hdt hem hfigcaption hfigure hfooter hh1 hh2 hh3 hh4 hh5 hh6 hhead hheader hhgroup hhr hhtml hi hins:cite,datetime hkbd hli:value hmain hmark hmenu hnav hol:reversed,start,type hp hpre hq hrp hrt hruby hs hsamp hsearch hsection hsmall hspan hstrong hsub hsup htable htbody htd:colspan,headers,rowspan htfoot hth:abbr,colspan,headers,rowspan,scope hthead htime:datetime htitle htr hu hul hvar hwbr sa:href,hreflang,type scircle:cx,cy,pathLength,r sdefs sdesc sellipse:cx,cy,pathLength,rx,ry sforeignObject:height,width,x,y sg sline:pathLength,x1,x2,y1,y2 smarker:markerHeight,markerUnits,markerWidth,orient,preserveAspectRatio,refX,refY,viewBox smetadata spath:d,pathLength spolygon:pathLength,points spolyline:pathLength,points srect:height,pathLength,rx,ry,width,x,y ssvg:height,preserveAspectRatio,viewBox,width,x,y stext:dx,dy,lengthAdjust,rotate,textLength,x,y stextPath:lengthAdjust,method,path,side,spacing,startOffset,textLength stitle stspan:dx,dy,lengthAdjust,rotate,textLength,x,y';
const SAN_DEFAULT_ATTRS = 'alignment-baseline baseline-shift clip-path clip-rule color color-interpolation cursor dir direction display displaystyle dominant-baseline fill fill-opacity fill-rule font-family font-size font-size-adjust font-stretch font-style font-variant font-weight lang letter-spacing marker-end marker-mid marker-start mathbackground mathcolor mathsize opacity paint-order pointer-events scriptlevel shape-rendering stop-color stop-opacity stroke stroke-dasharray stroke-dashoffset stroke-linecap stroke-linejoin stroke-miterlimit stroke-opacity stroke-width text-anchor text-decoration text-overflow text-rendering title transform transform-origin unicode-bidi vector-effect visibility white-space word-spacing writing-mode';
function sanDefault() {
    const ns = { h: SAN_HTML, s: SAN_SVG, m: SAN_MATHML };
    const attr = name => ({ name, namespace: null });
    return {
        elements: SAN_DEFAULT_ELS.split(' ').map(s => { const [n, a] = s.slice(1).split(':'); return { name: n, namespace: ns[s[0]], attributes: a ? a.split(',').map(attr) : [] }; }),
        processingInstructions: [], attributes: SAN_DEFAULT_ATTRS.split(' ').map(attr),
        comments: false, dataAttributes: false, javascriptURLs: false,
    };
}
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
function sanOf(s, m) { const c = sanCfgs.get(s); if (!c) throw new TypeError(`Failed to execute '${m}' on 'Sanitizer': Illegal invocation`); return c; }
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
        sanCfgs.set(this, sanConfigure(input, true, m));
    }
    get() {
        const c = sanClone(sanOf(this, 'get'));
        if (c.elements) {
            for (const e of c.elements) { if (e.attributes) e.attributes.sort(sanCompare); if (e.removeAttributes) e.removeAttributes.sort(sanCompare); }
            c.elements.sort(sanCompare);
        } else c.removeElements.sort(sanCompare);
        if (c.replaceWithChildrenElements) c.replaceWithChildrenElements.sort(sanCompare);
        if (c.processingInstructions) c.processingInstructions.sort(sanPICompare); else c.removeProcessingInstructions.sort(sanPICompare);
        if (c.attributes) c.attributes.sort(sanCompare); else c.removeAttributes.sort(sanCompare);
        return c;
    }
    allowElement(element) {
        const c = sanOf(this, 'allowElement'), m = "execute 'allowElement' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        const e = sanName(element, SAN_HTML, m, true);
        if (c.elements) {
            const modified = sanDrop(c.replaceWithChildrenElements, e);
            if (c.attributes) {
                if (e.attributes) {
                    e.attributes = sanSet(e.attributes).filter(a => !sanHas(c.attributes, a));
                    if (c.dataAttributes === true) e.attributes = e.attributes.filter(a => !sanIsData(a));
                }
                if (e.removeAttributes) e.removeAttributes = sanSet(e.removeAttributes).filter(a => sanHas(c.attributes, a));
            } else {
                if (e.attributes) {
                    e.attributes = sanSet(e.attributes).filter(a => !sanHas(e.removeAttributes || [], a));
                    delete e.removeAttributes;
                    e.attributes = e.attributes.filter(a => !sanHas(c.removeAttributes, a));
                }
                if (e.removeAttributes) e.removeAttributes = sanSet(e.removeAttributes).filter(a => !sanHas(c.removeAttributes, a));
            }
            if (!e.attributes && !e.removeAttributes) e.removeAttributes = [];
            const cur = c.elements.find(x => sanSame(x, e));
            if (!cur) { c.elements.push(e); return true; }
            if (JSON.stringify(sanSorted(cur)) === JSON.stringify(sanSorted(e))) return modified;
            sanDrop(c.elements, e);
            c.elements.push(e);
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
        if (!sanHas(c.replaceWithChildrenElements, e)) { c.replaceWithChildrenElements.push(e); return true; }
        return modified;
    }
    allowProcessingInstruction(pi) {
        const c = sanOf(this, 'allowProcessingInstruction'), m = "execute 'allowProcessingInstruction' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        const p = sanPI(pi, m);
        if (c.processingInstructions) {
            if (sanPIHas(c.processingInstructions, p)) return false;
            c.processingInstructions.push(p);
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
        c.removeProcessingInstructions.push(p);
        return true;
    }
    allowAttribute(attribute) {
        const c = sanOf(this, 'allowAttribute'), m = "execute 'allowAttribute' on 'Sanitizer'";
        if (!arguments.length) throw sanErr(m, '1 argument required, but only 0 present.');
        const a = sanName(attribute, null, m);
        if (c.attributes) {
            if (c.dataAttributes === true && sanIsData(a)) return false;
            if (sanHas(c.attributes, a)) return false;
            for (const e of c.elements || []) sanDrop(e.attributes, a);
            c.attributes.push(a);
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
            for (const e of c.elements || []) if (e.attributes) e.attributes = e.attributes.filter(a => !sanIsData(a));
            c.attributes = c.attributes.filter(a => !sanIsData(a));
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
function sanSorted(e) {
    const r = { name: e.name, namespace: e.namespace };
    if (e.attributes) r.attributes = [...e.attributes].sort(sanCompare);
    if (e.removeAttributes) r.removeAttributes = [...e.removeAttributes].sort(sanCompare);
    return r;
}
for (const k of Object.getOwnPropertyNames(Sanitizer.prototype)) if (k !== 'constructor') Object.defineProperty(Sanitizer.prototype, k, { enumerable: true });
Object.defineProperty(Sanitizer.prototype, Symbol.toStringTag, { value: 'Sanitizer', configurable: true });

/* SetHTMLOptions / SetHTMLUnsafeOptions: { sanitizer, runScripts } */
function sanOptions(o, m, unsafe) {
    if (o == null) return {};
    if (!sanIsObj(o)) throw sanErr(m, "The provided value is not of type 'SetHTMLOptions'.");
    const r = {};
    if (unsafe) r.runScripts = !!o.runScripts;
    const s = o.sanitizer;
    if (s === undefined) return r;
    if (sanCfgs.has(s)) r.sanitizer = s;
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
    let input = o.sanitizer;
    if (input === undefined) { if (!safe) return null; input = 'default'; }
    if (input === 'default' && safe) {
        if (!sanSafeDefault) { const c = sanConfigure(sanDefault(), false, m); sanRemoveUnsafe(c); sanSafeDefault = sanFlat(c); }
        return sanSafeDefault;
    }
    if (input === 'default') input = sanDefault();
    else if (sanCfgs.has(input)) input = sanCfgs.get(input);
    const c = sanConfigure(input, !safe, m);
    if (safe) sanRemoveUnsafe(c);
    return sanFlat(c);
}
/* the flat form parseFragEx and parseDocEx read */
function sanFlat(c) {
    const names = l => { if (!l) return undefined; const o = []; for (const x of l) o.push(x.namespace, x.name); return o; };
    const r = { comments: !!c.comments, dataAttributes: !!c.dataAttributes, javascriptURLs: !!c.javascriptURLs };
    if (c.elements) { r.elements = []; for (const e of c.elements) r.elements.push(e.namespace, e.name, names(e.attributes) || null, names(e.removeAttributes) || null); }
    for (const k of ['removeElements', 'replaceWithChildrenElements', 'attributes', 'removeAttributes']) if (c[k]) r[k] = names(c[k]);
    for (const k of ['processingInstructions', 'removeProcessingInstructions']) if (c[k]) r[k] = c[k].map(p => p.target);
    return r;
}
/* parse HTML for insertion: the fragment, or null for a script context in Safe mode */
function htmlForInsertion(target, html, o, safe, m) {
    const cfg = sanFromOptions(o, safe, m);
    const cx = N.type(target) === 1 ? target : N.host(target) || tplOwner.get(target) || null;
    if (safe && cx && N.type(cx) === 1 && cx.localName === 'script' && (cx.namespaceURI === SAN_HTML || cx.namespaceURI === SAN_SVG)) return null;
    return N.parseFragEx(cx, html, cfg, true, !!o.runScripts);
}
/* a template's contents stand for it as the parser context */
const tplOwner = new WeakMap();
function htmlTarget(el) {
    if (el.localName === 'template' && el.namespaceURI === SAN_HTML) { const c = N.templateContent(el); tplOwner.set(c, el); return c; }
    return el;
}
function htmlParent(node) {
    const p = N.parent(node);
    if (!p) return null;
    if (N.type(p) === 1 && p.localName === 'template' && p.namespaceURI === SAN_HTML) throw hre('The parent is a template element.');
    if (N.type(p) === 1 || (N.type(p) === 11 && N.host(p))) return p;
    throw hre('The parent is not an element or a shadow root.');
}
const isHtmlRoot = p => isHtmlEl(p, 'html');
/* the converted arguments: [html, options] */
function htmlArgs(args, safe, m, tt = !safe) {
    if (!args.length) throw sanErr(m, '1 argument required, but only 0 present.');
    if (!tt) return [sanStr(args[0], m), sanOptions(args[1], m, !safe)];
    /* the Unsafe sinks take TrustedHTML, and options a policy made (TrustedHTMLParserOptions) */
    const po = ttParserOpts(args[1]);
    if (po) return [sanStr(args[0], m), po];
    const html = ttIsHTML(args[0]) ? args[0] : sanStr(args[0], m), o = sanOptions(args[1], m, !safe);
    const [, fn, on] = /'(\w+)' on '(\w+)'/.exec(m);
    return [ttString(TrustedHTML, html, on + ' ' + fn), o];
}
function htmlSet(target, [html, o], safe, m) {
    const f = htmlForInsertion(target, html, o, safe, m);
    if (f) ParentNode.replaceChildren.call(target, f);
}
function htmlInsert(parent, child, [html, o], safe, m) {
    if (isHtmlRoot(parent)) throw hre('The parent is an html element.');
    const f = htmlForInsertion(parent, html, o, safe, m);
    if (f) insertNode(parent, f, child);
}
function htmlReplace(parent, node, [html, o], safe, m) {
    if (isHtmlRoot(parent)) throw hre('The parent is an html element.');
    const f = htmlForInsertion(parent, html, o, safe, m);
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
/* Document.parseHTML(Unsafe)(): a new document, scripting disabled, declarative shadow roots allowed */
function parseHTMLDoc(args, safe, m) {
    const [html, o] = htmlArgs(args, true, m, !safe);
    return N.parseDocEx(html, sanFromOptions(o, safe, m));
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
