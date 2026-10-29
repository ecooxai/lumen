/* HTML attribute reflection (HTML "Reflecting content attributes in IDL attributes"): the IDL attributes below reflect
   their content attributes by type. An entry replaces an accessor the reflect* helpers made or adds a missing one; a
   hand-written accessor (one with behaviour beyond the attribute) stays. */
(() => {
    const MAXI = 2147483647, isWs = c => c === 32 || c === 9 || c === 10 || c === 12 || c === 13;
    /* the rules for parsing integers; NaN on failure */
    const pInt = (s) => {
        if (s == null) return NaN;
        let i = 0, sign = 1, v = 0; const n = s.length;
        while (i < n && isWs(s.charCodeAt(i))) i++;
        if (s[i] === '-') { sign = -1; i++; } else if (s[i] === '+') i++;
        const st = i;
        for (let c; i < n && (c = s.charCodeAt(i) - 48) >= 0 && c <= 9; i++) v = v * 10 + c;
        return i === st ? NaN : v === 0 ? 0 : sign * v;
    };
    /* the rules for parsing floating-point number values; NaN on failure */
    const FLOAT = /^[\t\n\f\r ]*([-+]?(?:\d+\.?\d*|\.\d+))(?:[eE]([-+]?\d+))?/;
    const pFloat = (s) => {
        const m = s == null ? null : FLOAT.exec(s);
        if (!m) return NaN;
        const v = Number(m[1].replace(/\.$/, '') + (m[2] ? 'e' + m[2] : ''));
        return !Number.isFinite(v) ? NaN : v === 0 ? 0 : v;
    };
    const idxErr = (v) => new DOMException(`The value provided (${v}) is outside the range.`, 'IndexSizeError');
    const finite = (v, n) => { v = +v; if (!Number.isFinite(v)) throw new TypeError(`Failed to set the '${n}' property: The provided double value is non-finite.`); return v; };
    const set = reflectSetAttr;
    const types = {
        s: (a, n, o) => [function () { return N.attr(this, a) ?? ''; }, function (v) { set(this, a, o.tn && v === null ? '' : v); }],
        u: (a, n, o) => [function () {
            const v = N.attr(this, a), doc = this.ownerDocument || document;
            if (o.doc && !v) return doc.URL;
            if (v == null) return '';
            const r = N.urlParse(v, doc.baseURI, docEnc(this)); return r ? r[0] : v;
        }, function (v) { set(this, a, v); }],
        b: (a) => [function () { return N.attr(this, a) != null; }, function (v) { if (v) set(this, a, ''); else reflectRemoveAttr(this, a); }],
        l: (a, n, o) => [function () { const v = pInt(N.attr(this, a)); return v >= -MAXI - 1 && v <= MAXI ? v : (o.d ?? 0); }, function (v) { set(this, a, String(v | 0)); }],
        ll: (a, n, o) => [function () { const v = pInt(N.attr(this, a)); return v >= 0 && v <= MAXI ? v : (o.d ?? -1); }, function (v) { v |= 0; if (v < 0) throw idxErr(v); set(this, a, String(v)); }],
        ul: (a, n, o) => [function () { const v = pInt(N.attr(this, a)); return v >= 0 && v <= MAXI ? v : (o.d ?? 0); }, function (v) { v >>>= 0; set(this, a, String(v > MAXI ? (o.d ?? 0) : v)); }],
        lul: (a, n, o) => [function () { const v = pInt(N.attr(this, a)); return v >= 1 && v <= MAXI ? v : (o.d ?? 1); },
            function (v) { v >>>= 0; if (v === 0 && !o.fb) throw idxErr(v); set(this, a, String(v === 0 || v > MAXI ? (o.d ?? 1) : v)); }],
        cul: (a, n, o) => [function () { const v = pInt(N.attr(this, a)); return !(v >= 0) ? o.d : Math.min(Math.max(v, o.min), o.max); },
            function (v) { v >>>= 0; set(this, a, String(v > MAXI ? o.d : v)); }],
        d: (a, n, o) => [function () { const v = pFloat(N.attr(this, a)); return v === v && !(o.pos && v <= 0) ? v : (o.d ?? 0); },
            function (v) { v = finite(v, n); if (o.pos && v <= 0) return; set(this, a, String(v)); }],
        e: (a, n, o) => {
            const kw = new SMap(); for (const k of o.k.split(' ')) kw.set(k.toLowerCase(), k);
            if (o.k0) kw.set('', o.k0);
            const miss = o.m ?? (o.nul ? null : ''), inv = o.i ?? miss;
            return [function () { const v = N.attr(this, a); if (v == null) return miss; const k = kw.get(asciiLower(v)); return k === undefined ? inv : k; },
                function (v) { if (o.nul && v == null) reflectRemoveAttr(this, a); else set(this, a, v); }];
        },
    };
    const RP = { k: 'no-referrer no-referrer-when-downgrade same-origin origin strict-origin origin-when-cross-origin strict-origin-when-cross-origin unsafe-url', k0: '' };
    const CORS = { k: 'anonymous use-credentials', k0: 'anonymous', nul: true, i: 'anonymous' };
    const ENC = { k: 'application/x-www-form-urlencoded multipart/form-data text/plain' };
    const PRELOAD = { k: 'none metadata auto', k0: 'auto', m: 'metadata', i: 'metadata' };
    const LOADING = { k: 'lazy eager', m: 'eager' };
    const POPACT = { k: 'toggle show hide', m: 'toggle' };
    const CELL = 'align ch/char chOff/charoff vAlign';
    /* interface: space-separated entries "prop[/attr]" of type t (s if none), as groups [type, entries, options] */
    const table = {
        HTMLElement: [['e', 'dir', { k: 'ltr rtl auto' }], ['e', 'enterKeyHint', { k: 'enter done go next previous search send' }],
            ['e', 'inputMode', { k: 'none text tel url email numeric decimal search' }],
            ['e', 'popover', { k: 'auto manual hint', k0: 'auto', nul: 1, i: 'manual' }]],
        HTMLHtmlElement: [['s', 'version']],
        HTMLScriptElement: [['u', 'src'], ['s', 'type charset integrity event htmlFor/for'], ['b', 'noModule defer'], ['e', 'crossOrigin', CORS]],
        HTMLModElement: [['u', 'cite'], ['s', 'dateTime']],
        HTMLQuoteElement: [['u', 'cite']],
        HTMLDetailsElement: [['b', 'open']], HTMLDialogElement: [['b', 'open']],
        HTMLMenuElement: [['b', 'compact']], HTMLDirectoryElement: [['b', 'compact']], HTMLDListElement: [['b', 'compact']],
        HTMLSlotElement: [['s', 'name']],
        HTMLBodyElement: [['s', 'text link vLink aLink bgColor', { tn: 1 }], ['s', 'background']],
        HTMLHeadingElement: [['s', 'align']], HTMLParagraphElement: [['s', 'align']], HTMLDivElement: [['s', 'align']],
        HTMLLegendElement: [['s', 'align']], HTMLTableCaptionElement: [['s', 'align']],
        HTMLHRElement: [['s', 'align color size width'], ['b', 'noShade']],
        HTMLPreElement: [['l', 'width']],
        HTMLOListElement: [['b', 'reversed compact'], ['l', 'start', { d: 1 }], ['s', 'type']],
        HTMLUListElement: [['b', 'compact'], ['s', 'type']],
        HTMLLIElement: [['l', 'value'], ['s', 'type']],
        HTMLAnchorElement: [['s', 'target download ping rel hreflang type coords charset name rev shape'], ['e', 'referrerPolicy', RP]],
        HTMLDataElement: [['s', 'value']], HTMLTimeElement: [['s', 'dateTime']], HTMLBRElement: [['s', 'clear']],
        HTMLFormElement: [['s', 'acceptCharset/accept-charset name target'], ['u', 'action', { doc: 1 }], ['b', 'noValidate'],
            ['e', 'autocomplete', { k: 'on off', m: 'on' }], ['e', 'enctype encoding/enctype', Object.assign({ m: 'application/x-www-form-urlencoded' }, ENC)],
            ['e', 'method', { k: 'get post dialog', m: 'get' }]],
        HTMLFieldSetElement: [['b', 'disabled'], ['s', 'name']],
        HTMLLabelElement: [['s', 'htmlFor/for']],
        HTMLInputElement: [['s', 'accept alt dirName formTarget max min name pattern placeholder step align useMap'],
            ['b', 'disabled formNoValidate multiple readOnly required'], ['u', 'formAction', { doc: 1 }], ['u', 'src'],
            ['e', 'formEnctype', Object.assign({ i: 'application/x-www-form-urlencoded' }, ENC)], ['e', 'formMethod', { k: 'get post dialog', i: 'get' }],
            ['ul', 'height width'], ['ll', 'maxLength minLength'], ['lul', 'size', { d: 20 }], ['e', 'popoverTargetAction', POPACT]],
        HTMLButtonElement: [['b', 'disabled formNoValidate'], ['u', 'formAction', { doc: 1 }], ['s', 'formTarget name value'],
            ['e', 'formEnctype', Object.assign({ i: 'application/x-www-form-urlencoded' }, ENC)], ['e', 'formMethod', { k: 'get post dialog', i: 'get' }],
            ['e', 'type', { k: 'submit reset button', m: 'submit' }], ['e', 'popoverTargetAction', POPACT]],
        HTMLSelectElement: [['b', 'disabled multiple required'], ['s', 'name'], ['ul', 'size']],
        HTMLOptGroupElement: [['b', 'disabled'], ['s', 'label']],
        HTMLOptionElement: [['b', 'disabled']],
        HTMLTextAreaElement: [['s', 'dirName name placeholder wrap'], ['b', 'disabled readOnly required'], ['ll', 'maxLength minLength'],
            ['lul', 'cols', { d: 20, fb: 1 }], ['lul', 'rows', { d: 2, fb: 1 }]],
        HTMLOutputElement: [['s', 'name']],
        HTMLProgressElement: [['d', 'max', { d: 1, pos: 1 }]],
        HTMLBaseElement: [['s', 'target']],
        HTMLLinkElement: [['u', 'href'], ['e', 'crossOrigin', CORS], ['s', 'rel media integrity hreflang type charset rev target'],
            ['e', 'as', { k: 'fetch audio document embed font image manifest object report script sharedworker style track video worker xslt' }],
            ['e', 'referrerPolicy', RP]],
        HTMLMetaElement: [['s', 'name httpEquiv/http-equiv content media scheme']],
        HTMLStyleElement: [['s', 'media type']],
        HTMLImageElement: [['s', 'alt srcset useMap name align'], ['u', 'src lowsrc longDesc'], ['e', 'crossOrigin', CORS], ['b', 'isMap'],
            ['e', 'referrerPolicy', RP], ['e', 'decoding', { k: 'async sync auto', m: 'auto' }], ['ul', 'hspace vspace'], ['s', 'border', { tn: 1 }],
            ['e', 'loading', LOADING]],
        HTMLIFrameElement: [['u', 'src longDesc'], ['s', 'srcdoc name width height align scrolling frameBorder'], ['b', 'allowFullscreen'],
            ['e', 'referrerPolicy', RP], ['s', 'marginHeight marginWidth', { tn: 1 }], ['e', 'loading', LOADING]],
        HTMLEmbedElement: [['u', 'src'], ['s', 'type width height align name']],
        HTMLObjectElement: [['u', 'data codeBase'], ['s', 'type name useMap width height align archive code standby codeType'], ['b', 'declare'],
            ['ul', 'hspace vspace'], ['s', 'border', { tn: 1 }]],
        HTMLParamElement: [['s', 'name value type valueType']],
        HTMLMediaElement: [['e', 'crossOrigin', CORS], ['e', 'preload', PRELOAD], ['b', 'autoplay loop controls defaultMuted/muted']],
        HTMLVideoElement: [['ul', 'width height'], ['u', 'poster'], ['b', 'playsInline']],
        HTMLSourceElement: [['u', 'src'], ['s', 'type srcset sizes media']],
        HTMLTrackElement: [['e', 'kind', { k: 'subtitles captions descriptions chapters metadata', m: 'subtitles', i: 'metadata' }], ['u', 'src'],
            ['s', 'srclang label'], ['b', 'default']],
        HTMLMapElement: [['s', 'name']],
        HTMLAreaElement: [['s', 'alt coords shape target download ping rel hreflang type'], ['e', 'referrerPolicy', RP], ['b', 'noHref']],
        HTMLTableElement: [['s', 'align border frame rules summary width'], ['s', 'bgColor cellPadding cellSpacing', { tn: 1 }]],
        HTMLTableColElement: [['cul', 'span', { d: 1, min: 1, max: 1000 }], ['s', CELL + ' width']],
        HTMLTableSectionElement: [['s', CELL]],
        HTMLTableRowElement: [['s', CELL], ['s', 'bgColor', { tn: 1 }]],
        HTMLTableCellElement: [['cul', 'colSpan', { d: 1, min: 1, max: 1000 }], ['cul', 'rowSpan', { d: 1, min: 0, max: 65534 }],
            ['s', 'headers abbr axis height width ' + CELL], ['e', 'scope', { k: 'row col rowgroup colgroup' }], ['b', 'noWrap'], ['s', 'bgColor', { tn: 1 }]],
        HTMLMarqueeElement: [['s', 'bgColor height width'], ['ul', 'hspace vspace'], ['ul', 'scrollAmount', { d: 6 }], ['ul', 'scrollDelay', { d: 85 }],
            ['b', 'trueSpeed'], ['e', 'behavior', { k: 'scroll slide alternate', m: 'scroll' }], ['e', 'direction', { k: 'up right down left', m: 'left' }]],
        HTMLFrameSetElement: [['s', 'cols rows']],
        HTMLFrameElement: [['s', 'name scrolling frameBorder'], ['u', 'src longDesc'], ['b', 'noResize'], ['s', 'marginHeight marginWidth', { tn: 1 }]],
        HTMLFontElement: [['s', 'color', { tn: 1 }], ['s', 'face size']],
    };
    for (const name in table) {
        const C = name === 'HTMLElement' ? HTMLElement : H[name];
        if (!C) continue;
        const P = C.prototype;
        for (const [t, list, o = {}] of table[name]) for (const ent of list.split(' ')) {
            const [n, a = n.toLowerCase()] = ent.split('/');
            const d = Object.getOwnPropertyDescriptor(P, n);
            if (d && !(d.get && REFLECTED.has(d.get))) continue;
            const [g, s] = types[t](a, n, o);
            racc(P, n, g, s);
        }
    }
    /* reflect*-made copies on subclasses would shadow the HTMLElement entries */
    for (const k in H) for (const n of ['dir', 'enterKeyHint', 'inputMode']) {
        const d = H[k] && Object.getOwnPropertyDescriptor(H[k].prototype, n);
        if (d && d.get && REFLECTED.has(d.get)) delete H[k].prototype[n];
    }
})();
