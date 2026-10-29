/* Accessibility: computed roles (HTML-AAM / WAI-ARIA) and accessible names (accname 1.2), for the WebDriver
   computed label/role queries and later the platform accessibility tree. */
{
const AX_ROLES = new SSet('alert alertdialog application article banner blockquote button caption cell checkbox code columnheader combobox comment complementary contentinfo definition deletion dialog directory document emphasis feed figure form generic grid gridcell group heading img image insertion link list listbox listitem log main mark marquee math menu menubar menuitem menuitemcheckbox menuitemradio meter navigation none note option paragraph presentation progressbar radio radiogroup region row rowgroup rowheader scrollbar search searchbox sectionfooter sectionheader separator slider spinbutton status strong subscript suggestion superscript switch tab table tablist tabpanel term textbox time timer toolbar tooltip tree treegrid treeitem'.split(' '));
const AX_ABSTRACT = new SSet('command composite input landmark range roletype section sectionhead select structure widget window'.split(' '));
/* roles whose name may come from their content */
const AX_FROM_CONTENT = new SSet('button cell checkbox columnheader comment gridcell heading link menuitem menuitemcheckbox menuitemradio option radio row rowheader sectionheader switch tab tooltip treeitem caption code deletion emphasis insertion mark strong subscript superscript term time definition'.split(' '));
const axHtml = el => el.namespaceURI === 'http://www.w3.org/1999/xhtml';
const axLandmarkScoped = el => { for (let p = el.parentElement; p; p = p.parentElement) if (axHtml(p) && /^(article|aside|main|nav|section)$/.test(p.localName)) return true; return false; };
const axImplicit = el => {
    if (!axHtml(el)) return el.namespaceURI === 'http://www.w3.org/2000/svg' && el.localName === 'svg' ? 'graphics-document' : '';
    const t = el.localName, has = a => el.hasAttribute(a);
    switch (t) {
    case 'a': case 'area': return has('href') ? 'link' : 'generic';
    case 'article': return 'article'; case 'aside': return 'complementary'; case 'blockquote': return 'blockquote';
    case 'button': return 'button'; case 'caption': return 'caption'; case 'code': return 'code';
    case 'datalist': return 'listbox'; case 'dd': return 'definition'; case 'del': case 's': return 'deletion';
    case 'details': return 'group'; case 'dfn': case 'dt': return 'term'; case 'dialog': return 'dialog';
    case 'em': return 'emphasis'; case 'fieldset': case 'optgroup': case 'address': return 'group';
    case 'figure': return 'figure'; case 'footer': return axLandmarkScoped(el) ? 'generic' : 'contentinfo';
    case 'header': return axLandmarkScoped(el) ? 'generic' : 'banner';
    case 'form': return axName(el) ? 'form' : '';
    case 'h1': case 'h2': case 'h3': case 'h4': case 'h5': case 'h6': return 'heading';
    case 'hr': return 'separator'; case 'html': return 'document';
    case 'img': return el.getAttribute('alt') === '' && !has('title') && !has('aria-label') && !has('aria-labelledby') ? 'none' : 'img';
    case 'input': {
        const ty = (el.getAttribute('type') || 'text').toLowerCase();
        if (/^(button|image|reset|submit)$/.test(ty)) return 'button';
        if (ty === 'checkbox') return 'checkbox'; if (ty === 'radio') return 'radio'; if (ty === 'range') return 'slider';
        if (ty === 'number') return 'spinbutton'; if (ty === 'search') return has('list') ? 'combobox' : 'searchbox';
        if (/^(email|tel|text|url)$/.test(ty) || !/^(color|date|datetime-local|file|hidden|month|password|time|week)$/.test(ty)) return has('list') ? 'combobox' : 'textbox';
        return '';
    }
    case 'ins': return 'insertion'; case 'li': return 'listitem'; case 'main': return 'main'; case 'mark': return 'mark';
    case 'math': return 'math'; case 'menu': case 'ol': case 'ul': return 'list'; case 'meter': return 'meter';
    case 'nav': return 'navigation'; case 'option': return 'option'; case 'output': return 'status'; case 'p': return 'paragraph';
    case 'progress': return 'progressbar'; case 'search': return 'search';
    case 'section': return axName(el) ? 'region' : 'generic';
    case 'select': return el.multiple || el.size > 1 ? 'listbox' : 'combobox';
    case 'strong': return 'strong'; case 'sub': return 'subscript'; case 'sup': return 'superscript';
    case 'table': return 'table'; case 'tbody': case 'thead': case 'tfoot': return 'rowgroup';
    case 'td': return 'cell'; case 'th': return el.closest('thead') || el.getAttribute('scope') === 'col' ? 'columnheader' : 'rowheader';
    case 'textarea': return 'textbox'; case 'time': return 'time'; case 'tr': return 'row';
    case 'b': case 'bdi': case 'bdo': case 'body': case 'data': case 'div': case 'i': case 'pre': case 'q': case 'samp': case 'small': case 'span': case 'u': case 'hgroup': return 'generic';
    }
    return '';
};
const axRole = el => {
    const r = (el.getAttribute('role') || '').trim().toLowerCase();
    if (r) for (const t of r.split(/\s+/)) if (AX_ROLES.has(t) && !AX_ABSTRACT.has(t)) return t === 'presentation' ? 'none' : t === 'image' ? 'img' : t;
    return axImplicit(el);
};
const axHidden = el => {
    for (let e = el; e && e.nodeType === 1; e = e.parentElement || (e.getRootNode && e.getRootNode().host) || null) {
        if (e.hidden && axHtml(e)) return true;
        if (e.getAttribute('aria-hidden') === 'true') return true;
    }
    if (!el.isConnected) return true;
    const cs = getComputedStyle(el);
    return cs.display === 'none' || cs.visibility === 'hidden' || cs.visibility === 'collapse';
};
const axTrim = s => s.replace(/[ \t\n\r\f]+/g, ' ').replace(/^ | $/g, '');
const axControlValue = (el, role) => {
    if (role === 'textbox' || role === 'searchbox') return el.value !== undefined && axHtml(el) && /^(input|textarea)$/.test(el.localName) ? el.value : el.textContent;
    if (role === 'combobox' || role === 'listbox') {
        if (axHtml(el) && el.localName === 'select') return Array.from(el.selectedOptions || []).map(o => o.textContent).join(' ');
        if (axHtml(el) && el.localName === 'input') return el.value;
        const sel = el.querySelector('[role=option][aria-selected=true]'); return sel ? axText(sel, { rec: true, vis: new Set() }) : '';
    }
    if (/^(range|slider|spinbutton|scrollbar|progressbar|meter)$/.test(role)) {
        const vt = el.getAttribute('aria-valuetext'); if (vt) return vt;
        const vn = el.getAttribute('aria-valuenow'); if (vn) return vn;
        return el.value !== undefined ? String(el.value) : '';
    }
    return null;
};
const axPseudo = (el, which) => {
    let c = getComputedStyle(el, which).content;
    if (!c || c === 'none' || c === 'normal') return '';
    let out = '', m; const re = /"((?:[^"\\]|\\.)*)"|'((?:[^'\\]|\\.)*)'|\/\s*("(?:[^"\\]|\\.)*")/g;
    const slash = c.indexOf(' / ');
    if (slash >= 0) { const alt = c.slice(slash + 3); out = ''; while ((m = re.exec(alt))) out += (m[1] ?? m[2] ?? '').replace(/\\(.)/g, '$1'); return out; }
    while ((m = re.exec(c))) out += (m[1] ?? m[2] ?? '').replace(/\\(.)/g, '$1');
    return out;
};
const axBlock = el => { const d = getComputedStyle(el).display; return d && !/^inline/.test(d) && d !== 'contents' && d !== 'none'; };
const axFromContent = (el, cx) => {
    let s = axPseudo(el, '::before');
    const kids = el.shadowRoot ? el.shadowRoot.childNodes : (axHtml(el) && el.localName === 'slot' && el.assignedNodes().length ? el.assignedNodes() : el.childNodes);
    for (const k of kids) {
        if (k.nodeType === 3) s += k.data;
        else if (k.nodeType === 1) { const t = axText(k, { ...cx, rec: true, selfCtl: false }); s += axBlock(k) ? ' ' + t + ' ' : t; }
    }
    return s + axPseudo(el, '::after');
};
const axHostLabel = el => {
    if (axHtml(el)) {
        const t = el.localName, ty = (el.getAttribute('type') || '').toLowerCase();
        if (t === 'input' && /^(button|submit|reset)$/.test(ty)) return el.hasAttribute('value') ? el.getAttribute('value') : ty === 'submit' ? 'Submit' : ty === 'reset' ? 'Reset' : '';
        if (t === 'input' && ty === 'image') return el.getAttribute('alt') || el.getAttribute('value') || el.getAttribute('title') || 'Submit';
        if (el.labels && el.labels.length && !(t === 'input' && ty === 'hidden')) return Array.from(el.labels).map(l => axText(l, { rec: true, vis: new Set([el]), label: true })).join(' ');
        if (t === 'img' || t === 'area') return el.getAttribute('alt');
        if (t === 'fieldset') { const lg = Array.from(el.children).find(c => c.localName === 'legend'); return lg ? axText(lg, { rec: true, vis: new Set() }) : null; }
        if (t === 'figure') { const fc = Array.from(el.children).find(c => c.localName === 'figcaption'); return fc ? axText(fc, { rec: true, vis: new Set() }) : null; }
        if (t === 'table') { const cp = el.caption; return cp ? axText(cp, { rec: true, vis: new Set() }) : null; }
        if (t === 'optgroup') return el.getAttribute('label');
    } else if (el.namespaceURI === 'http://www.w3.org/2000/svg') {
        const ti = Array.from(el.children).find(c => c.localName === 'title'); if (ti) return ti.textContent;
    }
    return null;
};
function axText(el, cx) {
    if (cx.vis.has(el)) return '';
    cx.vis.add(el);
    if (!cx.ref && axHidden(el)) return '';
    const role = axRole(el);
    /* aria-labelledby, unless already following one */
    if (!cx.lby) {
        const ids = (el.getAttribute('aria-labelledby') || '').trim();
        if (ids) {
            const root = el.getRootNode();
            const parts = ids.split(/\s+/).map(id => root.getElementById ? root.getElementById(id) : document.getElementById(id)).filter(Boolean)
                .map(r => axText(r, { vis: new Set(r === el ? [] : [el]), lby: true, ref: true, rec: r !== el, selfCtl: r === el }));
            const s = axTrim(parts.join(' '));
            if (s) return s;
        }
    }
    /* an embedded control inside a name being built contributes its value */
    if (cx.rec && !cx.selfCtl) { const v = axControlValue(el, role); if (v !== null && (cx.label || cx.lby || cx.rec)) return v; }
    const al = (el.getAttribute('aria-label') || '').trim();
    if (al && !(cx.rec && role === 'none')) return al;
    if (role !== 'none' || !cx.rec) {
        const h = axHostLabel(el);
        if (h && axTrim(h)) return h;
    }
    let ws = '';
    if (cx.rec || AX_FROM_CONTENT.has(role) || cx.lby) {
        const s = axFromContent(el, cx);
        if (axTrim(s)) return s;
        ws = s;   /* whitespace-only content still separates its neighbours */
    }
    const ti = el.getAttribute('title'); if (ti && axTrim(ti)) return ti;
    if (axHtml(el) && /^(input|textarea)$/.test(el.localName)) { const ph = el.getAttribute('placeholder'); if (ph) return ph; }
    return cx.rec ? ws : '';
}
function axName(el) {
    if (!el || el.nodeType !== 1) return '';
    const r = axRole(el);
    if (/^(none|generic|paragraph|caption|code|deletion|emphasis|insertion|mark|strong|subscript|superscript|time|term|definition)$/.test(r) && !el.hasAttribute('aria-label') && !el.hasAttribute('aria-labelledby') && !axHostLabel(el) && !el.hasAttribute('title')) return '';
    return axTrim(axText(el, { vis: new Set(), selfCtl: true }));
}
def(G, '__lumenAx', Object.freeze({ role: el => (el && el.nodeType === 1 ? axRole(el) : ''), label: axName }));
}
