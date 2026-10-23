/* Content Security Policy as scripts see it (violation events, string compilation, connect-src) and Trusted Types.
   The policies come from the response headers and the <meta http-equiv> elements of the parsed head; documents with
   a local scheme (about:, blob:, data:) inherit their parent's. */
const TTError = Error, XLINK_NS = 'http://www.w3.org/1999/xlink', CSP_KEY = Symbol.for('lumen.csp');

const SPVE_KEYS = [['documentURI', ''], ['referrer', ''], ['blockedURI', ''], ['violatedDirective', ''], ['effectiveDirective', ''], ['originalPolicy', ''],
    ['sourceFile', ''], ['sample', ''], ['disposition', 'enforce'], ['statusCode', 0], ['lineNumber', 0], ['columnNumber', 0]];
const spveData = new WeakMap();
class SecurityPolicyViolationEvent extends Event {
    constructor(type, init = undefined) {
        if (!arguments.length) throw new TypeError("Failed to construct 'SecurityPolicyViolationEvent': 1 argument required, but only 0 present.");
        super(type, init);
        const i = init == null ? {} : init, r = {};
        for (const [k, d] of SPVE_KEYS) { const v = i[k]; r[k] = v === undefined ? d : typeof d === 'number' ? (k === 'statusCode' ? v & 0xffff : v >>> 0) : String(v); }
        if (r.disposition !== 'enforce' && r.disposition !== 'report')
            throw new TypeError(`Failed to construct 'SecurityPolicyViolationEvent': The provided value '${r.disposition}' is not a valid enum value of type SecurityPolicyViolationEventDisposition.`);
        spveData.set(this, r);
    }
}
for (const [k] of SPVE_KEYS) acc(SecurityPolicyViolationEvent.prototype, k, function () { const r = spveData.get(this); if (!r) throw new TypeError('Illegal invocation'); return r[k]; });

/* a policy: { d: directive name -> value tokens, lc: the same lowercased, report, src } */
function cspParse(text, report, out, meta) {
    const d = new Map(), lc = new Map();
    for (const part of text.split(';')) {
        const t = part.trim().split(/[\t\n\f\r ]+/);
        const name = t[0].toLowerCase();
        if (!name || !/^[a-z0-9-]+$/.test(name) || d.has(name)) continue;
        if (meta && (name === 'report-uri' || name === 'frame-ancestors' || name === 'sandbox')) continue;
        d.set(name, t.slice(1)); lc.set(name, t.slice(1).map(x => x.toLowerCase()));
    }
    if (d.size) out.push({ d, lc, report, src: text.trim() });
}
let cspPolicies = null;
function cspList() {
    if (cspPolicies) return cspPolicies;
    const out = [];
    try {
        if (/^(about|blob|data):/i.test(N.url()) && G.parent !== G) { const p = G.parent[CSP_KEY]; if (typeof p === 'function') out.push(...p()); }
    } catch (e) {}
    const [h, ro] = N.csp(document);
    if (h) for (const t of h.split(',')) cspParse(t, false, out, false);
    if (ro) for (const t of ro.split(',')) cspParse(t, true, out, false);
    const head = document.head;
    if (head) for (let n = N.first(head); n; n = N.next(n))
        if (N.type(n) === 1 && N.name(n) === 'meta' && (N.attr(n, 'http-equiv') || '').trim().toLowerCase() === 'content-security-policy') {
            const c = N.attr(n, 'content');
            if (c) cspParse(c, false, out, true);
        }
    return cspPolicies = out;
}
def(G, CSP_KEY, () => cspList());
/* the calling script's position: the first frame from a script URL */
function cspLoc() {
    const lim = TTError.stackTraceLimit; TTError.stackTraceLimit = 30;
    const st = new TTError().stack || ''; TTError.stackTraceLimit = lim;
    for (const ln of st.split('\n')) {
        const m = /((?:https?|file|blob):[^\s()]+?):(\d+):(\d+)\)?$/.exec(ln);
        if (m) return [m[1], +m[2], +m[3]];
    }
    return ['', 0, 0];
}
/* "report a violation" of policy p's directive: an event at the target (if connected) or the document, in a task */
function cspReport(p, directive, blockedURI, sample, target = null) {
    const [sourceFile, lineNumber, columnNumber] = cspLoc();
    const init = { bubbles: true, composed: true, documentURI: N.url().split('#')[0], referrer: document.referrer, blockedURI, violatedDirective: directive,
        effectiveDirective: directive, originalPolicy: p.src, sourceFile, sample, disposition: p.report ? 'report' : 'enforce', statusCode: 200, lineNumber, columnNumber };
    setTimeout(() => {
        const t = target && N.isNode(target) && N.connected(target) ? target : document;
        dispatch(t, new SecurityPolicyViolationEvent('securitypolicyviolation', init), true);
    });
}
/* the source list a fetch directive falls back to */
const cspSources = (p, dir) => p.lc.get(dir) || p.lc.get('default-src');
/* string compilation (eval, Function, string timers): blocked without 'unsafe-eval' (or 'trusted-types-eval' with
   Trusted Types enforced) in script-src */
function cspEval(src) {
    let ok = true;
    for (const p of cspList()) {
        const v = cspSources(p, 'script-src');
        if (!v || v.includes("'unsafe-eval'") || (v.includes("'trusted-types-eval'") && ttEnforced())) continue;
        cspReport(p, 'script-src', 'eval', v.includes("'report-sample'") ? src.slice(0, 40) : '');
        if (!p.report) ok = false;
    }
    return ok;
}
/* connect-src for WebSocket and friends: false when blocked (violations reported) */
function cspConnect(url) {
    let ok = true;
    for (const p of cspList()) {
        const v = cspSources(p, 'connect-src');
        if (!v || cspMatch(v, url)) continue;
        cspReport(p, 'connect-src', url, '');
        if (!p.report) ok = false;
    }
    return ok;
}
/* whether a source list allows url: '*', 'self', schemes and hosts (with *. wildcards), without paths */
function cspMatch(list, url) {
    if (list.length === 1 && list[0] === "'none'") return false;
    let u; try { u = new URL(url); } catch (e) { return false; }
    const self = new URL(N.url()), scheme = u.protocol.slice(0, -1), wsSelf = { ws: 'http', wss: 'https' }[scheme];
    for (const s of list) {
        if (s === '*') { if (!/^(data|blob|filesystem)$/.test(scheme)) return true; continue; }
        if (s === "'self'") { if (u.host === self.host && (u.protocol === self.protocol || wsSelf + ':' === self.protocol || (self.protocol === 'http:' && /^(https|wss):$/.test(u.protocol)))) return true; continue; }
        if (s[0] === "'") continue;
        const m = /^(?:([a-z][a-z0-9+.-]*):)?(?:\/\/)?([^/:]*)?(?::(\d+|\*))?/.exec(s);
        if (!m) continue;
        if (/^[a-z][a-z0-9+.-]*:$/.test(s)) { if (s.slice(0, -1) === scheme || (s === 'http:' && /^(https|ws|wss)$/.test(scheme)) || (s === 'https:' && scheme === 'wss')) return true; continue; }
        const [, sch, host, port] = m;
        if (sch && sch !== scheme && !(sch === 'http' && /^(https|ws|wss)$/.test(scheme)) && !(sch === 'https' && scheme === 'wss')) continue;
        if (!host) continue;
        const h = u.hostname;
        if (host.startsWith('*.') ? !h.endsWith(host.slice(1)) : host !== '*' && host !== h) continue;
        if (port && port !== '*' && port !== (u.port || { http: '80', https: '443', ws: '80', wss: '443' }[scheme])) continue;
        return true;
    }
    return false;
}

/* Trusted Types */
const ttRequiredBy = p => { const v = p.lc.get('require-trusted-types-for'); return !!v && v.includes("'script'"); };
const ttEnforced = () => cspList().some(p => !p.report && ttRequiredBy(p));
const ttData = new WeakMap();   /* trusted value -> [its interface, its data] */
const ttOwn = (o, C) => { const d = ttData.get(o); if (!d || d[0] !== C) throw new TypeError('Illegal invocation'); return d[1]; };
function ttIsHTML(v) { const d = ttData.get(v); return !!d && d[0] === TrustedHTML; }
/* fromLiteral: a template literal without substitutions */
function ttLiteral(args, C) {
    const t = args[0];
    if (args.length !== 1 || !Array.isArray(t) || !Object.isFrozen(t) || t.length !== 1 || !Object.prototype.hasOwnProperty.call(t, 'raw') || !Array.isArray(t.raw) || !Object.isFrozen(t.raw))
        throw new TypeError(`Failed to execute 'fromLiteral' on '${C.name}': Can't fromLiteral a non-literal.`);
    return String(t[0]);
}
class TrustedHTML {
    constructor() { illegal(); }
    toString() { return ttOwn(this, TrustedHTML); } toJSON() { return ttOwn(this, TrustedHTML); }
    static fromLiteral(templateStringsArray) {
        const s = ttLiteral(arguments, TrustedHTML), t = document.createElement('template');
        N.setHTML(t, s);
        return ttMake(TrustedHTML, N.html(N.templateContent(t), false));
    }
}
class TrustedScript {
    constructor() { illegal(); }
    toString() { return ttOwn(this, TrustedScript); } toJSON() { return ttOwn(this, TrustedScript); }
    static fromLiteral(templateStringsArray) { return ttMake(TrustedScript, ttLiteral(arguments, TrustedScript)); }
}
class TrustedScriptURL {
    constructor() { illegal(); }
    toString() { return ttOwn(this, TrustedScriptURL); } toJSON() { return ttOwn(this, TrustedScriptURL); }
    static fromLiteral(templateStringsArray) { return ttMake(TrustedScriptURL, ttLiteral(arguments, TrustedScriptURL).toWellFormed()); }
}
/* a TrustedScript is code-like: eval() and Function() take it as code */
function ttMake(C, s) {
    let o;
    if (C === TrustedScript) { o = N.codeLike(); Object.setPrototypeOf(o, TrustedScript.prototype); }
    else o = Object.create(C.prototype);
    ttData.set(o, [C, s]);
    return o;
}
const ttOptsOf = new WeakMap();   /* TrustedHTMLParserOptions -> { runScripts, sanitizer } */
class TrustedHTMLParserOptions { constructor() { illegal(); } }
function ttParserOpts(v) { return v !== null && typeof v === 'object' && ttOptsOf.get(v) || null; }

const ttPolicies = new WeakMap();   /* TrustedTypePolicy -> { name, fns } */
const ttPolOf = p => { const r = ttPolicies.get(p); if (!r) throw new TypeError('Illegal invocation'); return r; };
const TT_FN = new Map([[TrustedHTML, 'createHTML'], [TrustedScript, 'createScript'], [TrustedScriptURL, 'createScriptURL']]);
const ttDOMString = (v, m) => { if (typeof v === 'symbol') throw new TypeError(`Failed to execute '${m}': Cannot convert a Symbol value to a string.`); return String(v); };
function ttCreate(pol, C, args) {
    const p = ttPolOf(pol), fn = TT_FN.get(C);
    if (!args.length) throw new TypeError(`Failed to execute '${fn}' on 'TrustedTypePolicy': 1 argument required, but only 0 present.`);
    const s = ttDOMString(args[0], fn + "' on 'TrustedTypePolicy");
    const f = p.fns[fn];
    if (!f) throw new TypeError(`Failed to execute '${fn}' on 'TrustedTypePolicy': Policy ${p.name}'s TrustedTypePolicyOptions did not specify a '${fn}' member.`);
    const r = f.call(undefined, s, ...Array.prototype.slice.call(args, 1));
    const d = r == null ? '' : ttDOMString(r, fn + "' on 'TrustedTypePolicy");
    return ttMake(C, C === TrustedScriptURL ? d.toWellFormed() : d);
}
class TrustedTypePolicy {
    constructor() { illegal(); }
    get name() { return ttPolOf(this).name; }
    createHTML(input, ...args) { return ttCreate(this, TrustedHTML, arguments); }
    createScript(input, ...args) { return ttCreate(this, TrustedScript, arguments); }
    createScriptURL(input, ...args) { return ttCreate(this, TrustedScriptURL, arguments); }
    createParserOptions(options = undefined, ...args) {
        const p = ttPolOf(this), m = "execute 'createParserOptions' on 'TrustedTypePolicy'";
        if (!p.fns.createParserOptions) throw new TypeError(`Failed to ${m}: Policy ${p.name}'s TrustedTypePolicyOptions did not specify a 'createParserOptions' member.`);
        const o = sanOptions(options, m, true), arg = { runScripts: !!o.runScripts };
        if (o.sanitizer != null) arg.sanitizer = o.sanitizer;
        const r = p.fns.createParserOptions.call(undefined, arg, ...args);
        if (r == null) throw new TypeError(`Failed to ${m}: The policy's createParserOptions returned null.`);
        const po = Object.create(TrustedHTMLParserOptions.prototype);
        ttOptsOf.set(po, sanOptions(r, m, true));
        return po;
    }
}
const ttNames = [];
let ttDefault = null, ttEmptyHTML = null, ttEmptyScript = null;
const TT_PROPS = new Map([['iframe', [['srcdoc', 'TrustedHTML']]], ['script', [['innerText', 'TrustedScript'], ['textContent', 'TrustedScript'], ['text', 'TrustedScript'], ['src', 'TrustedScriptURL']]]]);
class TrustedTypePolicyFactory {
    constructor() { illegal(); }
    createPolicy(policyName, policyOptions = undefined) {
        const m = "execute 'createPolicy' on 'TrustedTypePolicyFactory'";
        if (!arguments.length) throw new TypeError(`Failed to ${m}: 1 argument required, but only 0 present.`);
        const name = ttDOMString(policyName, m), o = policyOptions;
        if (o != null && typeof o !== 'object' && typeof o !== 'function') throw new TypeError(`Failed to ${m}: The provided value is not of type 'TrustedTypePolicyOptions'.`);
        const fns = {};
        for (const k of ['createHTML', 'createParserOptions', 'createScript', 'createScriptURL']) {
            const v = o == null ? undefined : o[k];
            if (v !== undefined && typeof v !== 'function') throw new TypeError(`Failed to ${m}: Failed to read the '${k}' property from 'TrustedTypePolicyOptions': The provided callback is not a function.`);
            fns[k] = v || null;
        }
        let blocked = false;
        for (const p of cspList()) {
            const v = p.d.get('trusted-types'), lv = p.lc.get('trusted-types');
            if (!v) continue;
            const bad = (lv.length === 1 && lv[0] === "'none'") || (ttNames.includes(name) && !lv.includes("'allow-duplicates'")) || (!v.includes(name) && !v.includes('*'));
            if (!bad) continue;
            cspReport(p, 'trusted-types', 'trusted-types-policy', name.slice(0, 40));
            if (!p.report) blocked = true;
        }
        if (blocked) throw new TypeError(`Failed to ${m}: Policy "${name}" disallowed.`);
        if (name === 'default' && ttDefault) throw new TypeError(`Failed to ${m}: Policy "default" already exists.`);
        const pol = Object.create(TrustedTypePolicy.prototype);
        ttPolicies.set(pol, { name, fns });
        if (name === 'default') ttDefault = pol;
        ttNames.push(name);
        return pol;
    }
    isHTML(value) { return ttIsHTML(value); }
    isScript(value) { const d = ttData.get(value); return !!d && d[0] === TrustedScript; }
    isScriptURL(value) { const d = ttData.get(value); return !!d && d[0] === TrustedScriptURL; }
    get emptyHTML() { return ttEmptyHTML || (ttEmptyHTML = ttMake(TrustedHTML, '')); }
    get emptyScript() { return ttEmptyScript || (ttEmptyScript = ttMake(TrustedScript, '')); }
    getAttributeType(tagName, attribute, elementNs = '', attrNs = '') {
        const m = "execute 'getAttributeType' on 'TrustedTypePolicyFactory'";
        if (arguments.length < 2) throw new TypeError(`Failed to ${m}: 2 arguments required, but only ${arguments.length} present.`);
        const ln = ttDOMString(tagName, m).replace(/[A-Z]+/g, c => c.toLowerCase()), a = ttDOMString(attribute, m).replace(/[A-Z]+/g, c => c.toLowerCase());
        const ens = elementNs == null || elementNs === '' ? SAN_HTML : ttDOMString(elementNs, m), ans = attrNs == null || attrNs === '' ? null : ttDOMString(attrNs, m);
        const t = ttAttrData(ln, ens, a, ans);
        return t ? t[0].name : null;
    }
    getPropertyType(tagName, property, elementNs = '') {
        const m = "execute 'getPropertyType' on 'TrustedTypePolicyFactory'";
        if (arguments.length < 2) throw new TypeError(`Failed to ${m}: 2 arguments required, but only ${arguments.length} present.`);
        const ln = ttDOMString(tagName, m).replace(/[A-Z]+/g, c => c.toLowerCase()), prop = ttDOMString(property, m);
        const ens = elementNs == null || elementNs === '' ? SAN_HTML : ttDOMString(elementNs, m);
        if (ens === SAN_HTML) for (const [k, t] of TT_PROPS.get(ln) || []) if (k === prop) return t;
        return prop === 'innerHTML' || prop === 'outerHTML' ? 'TrustedHTML' : null;
    }
    get defaultPolicy() { return ttDefault; }
}
for (const C of [TrustedHTML, TrustedScript, TrustedScriptURL, TrustedTypePolicy, TrustedTypePolicyFactory])
    for (const k of Object.getOwnPropertyNames(C.prototype)) if (k !== 'constructor') Object.defineProperty(C.prototype, k, { enumerable: true });
for (const C of [TrustedHTML, TrustedScript, TrustedScriptURL]) Object.defineProperty(C, 'fromLiteral', { enumerable: true });
const ttFactory = Object.create(TrustedTypePolicyFactory.prototype);

/* the event handler content attributes, as the sanitizer knows them, and the media element's encrypted-media ones */
let ttEventNames = null;
const ttEvents = () => ttEventNames || (ttEventNames = new Set([...SAN_EVENT_ATTRS.map(a => a.name), 'onencrypted', 'onwaitingforkey']));
/* "get Trusted Type data for attribute": [the expected type, the sink name], or null */
function ttAttrData(ln, ns, a, ans) {
    if (ans === null && (ns === SAN_HTML || ns === SAN_SVG || ns === SAN_MATHML) && a.charCodeAt(0) === 111 && ttEvents().has(a)) return [TrustedScript, 'Element ' + a];
    if (ns === SAN_HTML && ans === null) {
        if (ln === 'iframe' && a === 'srcdoc') return [TrustedHTML, 'HTMLIFrameElement srcdoc'];
        if (ln === 'script' && a === 'src') return [TrustedScriptURL, 'HTMLScriptElement src'];
    }
    if (ns === SAN_SVG && ln === 'script' && a === 'href' && (ans === null || ans === XLINK_NS)) return [TrustedScriptURL, 'SVGScriptElement href'];
    return null;
}
/* "get Trusted Type compliant string": v's data if it is a C, else (when required) what the default policy makes of
   it; a rejected value is reported, and throws unless every requiring policy is report-only */
function ttString(C, v, sink) {
    const d = ttData.get(v);
    if (d && d[0] === C) return d[1];
    const s = `${v}`;
    if (!ttReq) return s;
    let r = null;
    const f = ttDefault && ttPolicies.get(ttDefault).fns[TT_FN.get(C)];
    if (f) r = f.call(undefined, s, C.name, sink);
    if (r != null) { const o = `${r}`; return C === TrustedScriptURL ? o.toWellFormed() : o; }
    if (ttMismatch(sink, s)) throw new TypeError(`Failed to set '${sink}': This document requires '${C.name}' assignment.`);
    return s;
}
/* "should sink type mismatch violation be blocked": reports one violation per requiring policy */
function ttMismatch(sink, source) {
    const sample = sink === 'Function' ? source.replace(/^(?:async )?function\*? anonymous/, '') : source;
    let blocked = false;
    for (const p of cspList()) if (ttRequiredBy(p)) {
        cspReport(p, 'require-trusted-types-for', 'trusted-types-sink', sink + '|' + sample.slice(0, 40));
        if (!p.report) blocked = true;
    }
    return blocked;
}
/* "get Trusted Types-compliant attribute value" for el's attribute (attribute namespace ans, local name ln) */
function ttAttr(el, ans, ln, v) {
    if (!ttReq) return typeof v === 'string' ? v : `${v}`;
    const t = ttAttrData(el.localName, el.namespaceURI, ln, ans === '' ? null : ans);
    if (t) return ttString(t[0], v, t[1]);
    const d = ttData.get(v);
    return d ? d[1] : `${v}`;
}
/* document.write(): the texts joined, TrustedHTML only when they all are */
function ttWriteText(texts, ln) {
    const all = texts.length > 0 && texts.every(ttIsHTML);
    let s = texts.map(t => all ? ttData.get(t)[1] : `${t}`).join('');
    if (ln) s += '\n';
    return all ? s : ttString(TrustedHTML, s, ln ? 'Document writeln' : 'Document write');
}

/* a script element's "script text": what the parser or a trusted setter put there; an inline script whose child text
   differs runs only what the default policy makes of it */
const ttSlot = new WeakMap();
function ttSlotsFrom(root) { for (const s of N.query(root, 'script', true)) ttSlot.set(s, N.text(s)); }
{
    const nodeText = Object.getOwnPropertyDescriptor(Node.prototype, 'textContent');
    const setScript = (el, v, sink) => { const s = ttString(TrustedScript, v, sink); ttSlot.set(el, s); nodeText.set.call(el, s); };
    const sp = H.HTMLScriptElement.prototype;
    acc(sp, 'text', function () { return N.text(this); }, function (v) { setScript(this, v, 'HTMLScriptElement text'); });
    acc(sp, 'textContent', function () { return nodeText.get.call(this); }, function (v) { setScript(this, v == null ? '' : v, 'HTMLScriptElement textContent'); });
    acc(sp, 'innerText', function () { return innerTextOf(this); }, function (v) { setScript(this, v == null ? '' : v, 'HTMLScriptElement innerText'); });
}
/* the native side's hook for an inline script about to run: the text to run, or null when blocked */
function ttScript(el, text) {
    if (ttSlot.get(el) === text) return text;
    try {
        const s = ttString(TrustedScript, text, el.namespaceURI === SAN_SVG ? 'SVGScriptElement text' : 'HTMLScriptElement text');
        ttSlot.set(el, s);
        return s;
    } catch (e) { return null; }
}
/* the native side's hook for string compilation (eval, Function and string timers once evalGuard is on; a
   TrustedScript always): the source to compile, undefined for as is, or false to throw an EvalError */
let ttTimerSrc = null;
const TT_FN_FRAME = /^[^\n]*\n[^\n]*\n\s+at (?:new )?(?:Async)?(?:Generator)?Function \(/;
function ttEval(source, isCodeLike) {
    if (typeof source !== 'string') {
        const d = ttData.get(source);
        if (!d || d[0] !== TrustedScript) return undefined;
        if (ttInternal) return d[1];
        /* an argument of the Function constructor: the whole function's text comes next */
        if (TT_FN_FRAME.test(new TTError().stack || '')) return d[1];
        return cspEval(d[1]) ? d[1] : false;
    }
    if (ttInternal) return undefined;
    const timer = source === ttTimerSrc;
    ttTimerSrc = null;
    if (ttReq && !isCodeLike && !timer) {
        const fn = /^\((?:async )?function\*? anonymous\(/.test(source), s = fn ? source.slice(1, -1) : source;
        let r;
        try { r = ttString(TrustedScript, s, fn ? 'Function' : 'eval'); } catch (e) { return false; }
        if (r !== s) return false;
    }
    return cspEval(source) ? undefined : false;
}
/* a javascript: URL navigation (links, location): the code runs in a later task, after the pre-navigation check */
function runJsURL(href) {
    let code = href.slice(11);
    if (ttReq) {
        let r = null;
        const f = ttDefault && ttPolicies.get(ttDefault).fns.createScript;
        try { if (f) r = f.call(undefined, code, 'TrustedScript', 'Location href'); } catch (e) { r = null; }
        if (r == null) { if (ttMismatch('Location href', code)) return; }
        else { const u = URL.parse('javascript:' + r); if (!u) return; code = u.href.slice(11); }
    }
    setTimeout(() => {
        ttInternal++;
        try { (0, eval)(decodeURIComponent(code)); } catch (e) { report(e); } finally { ttInternal--; }
    });
}
/* once the globals are in place: switch on the native checks this document's policies need */
function ttInit() {
    const list = cspList();
    ttReq = list.some(ttRequiredBy);
    if (ttReq) { N.ttOn(true); ttSlotsFrom(document); }
    if (ttReq || list.some(p => { const v = cspSources(p, 'script-src'); return v && !v.includes("'unsafe-eval'"); })) N.evalGuard(true);
}
