/* XML parser for XML, XHTML and SVG documents (XML 1.0 with Namespaces): elements, attributes, text,
   CDATA sections, comments, processing instructions and the doctype with the internal entities of its
   subset. A well-formedness error replaces the document's content with a parsererror element. */
#include "../dom/dom.h"
#include <ctype.h>

#define XHTML_NS "http://www.w3.org/1999/xhtml"
#define SVG_NS "http://www.w3.org/2000/svg"
#define MATHML_NS "http://www.w3.org/1998/Math/MathML"
#define XML_NS "http://www.w3.org/XML/1998/namespace"
#define XMLNS_NS "http://www.w3.org/2000/xmlns/"

typedef struct { const char *pfx, *uri; } XBind;   /* atoms; pfx "" is the default namespace, uri NULL unbinds it */
typedef struct { const char *name; char *value; } XEnt;
typedef struct {
    Document *d;
    const char *s; size_t n, i;
    const char *err;
    bool html_ents;   /* an XHTML doctype: HTML's named character references */
    VEC(XBind) binds;
    VEC(Node *) open; VEC(int) marks;   /* open elements, and the binding count before each */
    VEC(XEnt) ents;
    SB tmp;
} XP;

bool xml_mime(const char *t) {
    if (!t) return false;
    while (*t == ' ' || *t == '\t') t++;
    size_t n = strcspn(t, "; \t");
    if ((n == 8 && !strncasecmp(t, "text/xml", 8)) || (n == 15 && !strncasecmp(t, "application/xml", 15))) return true;
    return n > 4 && !strncasecmp(t + n - 4, "+xml", 4) && memchr(t, '/', n);
}

char *mime_essence(const char *t) {
    while (*t == ' ' || *t == '\t') t++;
    size_t n = strcspn(t, "; \t");
    char *m = xstrndup(t, n);
    for (char *c = m; *c; c++) *c = (char)tolower((unsigned char)*c);
    return m;
}

static bool name_start(unsigned char c) { return isalpha(c) || c == '_' || c == ':' || c >= 0x80; }
static bool name_char(unsigned char c) { return name_start(c) || isdigit(c) || c == '-' || c == '.'; }
static bool ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static void skip_ws(XP *p) { while (p->i < p->n && ws(p->s[p->i])) p->i++; }
static bool at(XP *p, const char *lit) { size_t k = strlen(lit); return p->n - p->i >= k && !memcmp(p->s + p->i, lit, k); }
static bool fail(XP *p, const char *e) { if (!p->err) p->err = e; return false; }
static size_t name_len(XP *p) {
    size_t j = p->i;
    if (j >= p->n || !name_start((unsigned char)p->s[j])) return 0;
    while (j < p->n && name_char((unsigned char)p->s[j])) j++;
    return j - p->i;
}
static bool valid_char(uint32_t c) { return c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0x10FFFF); }

/* Character and entity references in s[0..n), appended to out; entity values are expanded once more
   for their own character references (depth bounds recursion) */
static bool decode(XP *p, const char *s, size_t n, SB *out, bool attr, int depth) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '<' && attr) return fail(p, "'<' in an attribute value");
        if (c != '&') { sb_putc(out, attr && (c == '\t' || c == '\n' || c == '\r') ? ' ' : c); continue; }
        size_t j = i + 1;
        while (j < n && s[j] != ';' && j - i < 64) j++;
        if (j >= n || s[j] != ';') return fail(p, "unterminated entity reference");
        const char *e = s + i + 1; size_t en = j - i - 1;
        if (en > 1 && e[0] == '#') {
            uint32_t v = 0; bool hex = e[1] == 'x'; size_t k = hex ? 2 : 1;
            if (k >= en) return fail(p, "bad character reference");
            for (; k < en; k++) {
                int d = isdigit((unsigned char)e[k]) ? e[k] - '0' : hex && isxdigit((unsigned char)e[k]) ? (tolower((unsigned char)e[k]) - 'a' + 10) : -1;
                if (d < 0) return fail(p, "bad character reference");
                v = v * (hex ? 16 : 10) + (uint32_t)d; if (v > 0x10FFFF) return fail(p, "bad character reference");
            }
            if (!valid_char(v)) return fail(p, "character reference to an invalid character");
            sb_utf8(out, v);
        } else {
            static const char *pre[][2] = { { "lt", "<" }, { "gt", ">" }, { "amp", "&" }, { "quot", "\"" }, { "apos", "'" } };
            const char *val = NULL;
            for (size_t k = 0; k < ARRLEN(pre) && !val; k++) if (strlen(pre[k][0]) == en && !memcmp(e, pre[k][0], en)) val = pre[k][1];
            if (val) sb_puts(out, val);
            else {
                for (int k = 0; k < p->ents.n && !val; k++) if (strlen(p->ents.v[k].name) == en && !memcmp(p->ents.v[k].name, e, en)) val = p->ents.v[k].value;
                if (val) {
                    if (depth > 8) return fail(p, "recursive entity reference");
                    if (!decode(p, val, strlen(val), out, attr, depth + 1)) return false;
                } else {
                    size_t used = 0; uint32_t second = 0, cp = p->html_ents ? html_entity_lookup(e, en + 1, &second, &used) : 0;
                    if (!cp || used != en + 1) return fail(p, "undefined entity");
                    sb_utf8(out, cp); if (second) sb_utf8(out, second);
                }
            }
        }
        i = j;
    }
    return true;
}

static const char *lookup(XP *p, const char *pfx) {
    for (int k = p->binds.n - 1; k >= 0; k--) if (p->binds.v[k].pfx == pfx) return p->binds.v[k].uri;
    if (pfx == atom("xml")) return XML_NS;
    if (pfx == atom("xmlns")) return XMLNS_NS;
    return *pfx ? (const char *)-1 : NULL;   /* an unbound prefix */
}

static Node *top(XP *p) { return p->open.n ? p->open.v[p->open.n - 1] : &p->d->node; }

static void add_text(XP *p, const char *s, size_t n) {
    Node *t = top(p);
    if (t->last && t->last->type == NODE_TEXT) node_text_append(t->last, s, n);
    else node_append(t, node_new_text(p->d, s, n));
}

static bool doctype(XP *p) {
    p->i += 9;
    if (p->d->html || (p->d->node.first && p->d->node.last->type == NODE_ELEMENT)) return fail(p, "misplaced doctype");
    skip_ws(p);
    size_t nl = name_len(p); if (!nl) return fail(p, "bad doctype");
    char *name = xstrndup(p->s + p->i, nl); p->i += nl;
    char *ids[2] = { NULL, NULL };
    skip_ws(p);
    int nid = at(p, "PUBLIC") ? 2 : at(p, "SYSTEM") ? 1 : 0;
    if (nid) {
        p->i += 6;
        for (int k = 2 - nid; k < 2; k++) {
            skip_ws(p);
            char q = p->i < p->n ? p->s[p->i] : 0;
            if (q != '"' && q != '\'') { if (k == 1 && nid == 2) break; free(name); free(ids[0]); return fail(p, "bad doctype identifier"); }
            const char *e = memchr(p->s + p->i + 1, q, p->n - p->i - 1);
            if (!e) { free(name); free(ids[0]); return fail(p, "bad doctype identifier"); }
            ids[k] = xstrndup(p->s + p->i + 1, (size_t)(e - p->s - p->i - 1)); p->i = (size_t)(e - p->s) + 1;
        }
    }
    skip_ws(p);
    if (p->i < p->n && p->s[p->i] == '[') {   /* the internal subset: general entities are kept, other declarations skipped */
        p->i++;
        while (p->i < p->n && p->s[p->i] != ']') {
            if (at(p, "<!ENTITY")) {
                p->i += 8; skip_ws(p);
                bool pe = p->i < p->n && p->s[p->i] == '%'; if (pe) { p->i++; skip_ws(p); }
                size_t en = name_len(p); const char *ename = p->s + p->i; p->i += en; skip_ws(p);
                char q = p->i < p->n ? p->s[p->i] : 0;
                if (en && (q == '"' || q == '\'')) {
                    const char *e = memchr(p->s + p->i + 1, q, p->n - p->i - 1);
                    if (!e) break;
                    if (!pe) { XEnt x = { atomn(ename, en), xstrndup(p->s + p->i + 1, (size_t)(e - p->s - p->i - 1)) }; vec_push(p->ents, x); }
                    p->i = (size_t)(e - p->s) + 1;
                }
            }
            if (at(p, "<!--")) { const char *e = strstr(p->s + p->i + 4, "-->"); p->i = e ? (size_t)(e - p->s) + 3 : p->n; continue; }
            /* skips a declaration or PI, minding quoted strings */
            char q = 0;
            while (p->i < p->n) {
                char c = p->s[p->i++];
                if (q) { if (c == q) q = 0; }
                else if (c == '"' || c == '\'') q = c;
                else if (c == '>' || c == ']') { if (c == ']') p->i--; break; }
            }
        }
        if (p->i >= p->n) { free(name); free(ids[0]); free(ids[1]); return fail(p, "unterminated doctype"); }
        p->i++; skip_ws(p);
    }
    if (p->i >= p->n || p->s[p->i] != '>') { free(name); free(ids[0]); free(ids[1]); return fail(p, "bad doctype"); }
    p->i++;
    Node *dt = node_new_doctype(p->d, name);
    node_set_attr(dt, atom("publicId"), ids[0] ? ids[0] : ""); node_set_attr(dt, atom("systemId"), ids[1] ? ids[1] : "");
    node_append(&p->d->node, dt);
    static const char *const ent_ids[] = {   /* HTML "Parsing XHTML documents": these public ids get HTML's entities */
        "-//W3C//DTD XHTML 1.0 Transitional//EN", "-//W3C//DTD XHTML 1.1//EN", "-//W3C//DTD XHTML 1.0 Strict//EN",
        "-//W3C//DTD XHTML 1.0 Frameset//EN", "-//W3C//DTD XHTML Basic 1.0//EN", "-//W3C//DTD XHTML 1.1 plus MathML 2.0//EN",
        "-//W3C//DTD XHTML 1.1 plus MathML 2.0 plus SVG 1.1//EN", "-//W3C//DTD MathML 2.0//EN", "-//WAPFORUM//DTD XHTML Mobile 1.0//EN" };
    if (ids[0]) {
        for (size_t k = 0; k < sizeof ent_ids / sizeof *ent_ids; k++) if (!strcmp(ids[0], ent_ids[k])) p->html_ents = true;
        if (strstr(ids[0], "XHTML")) p->html_ents = true;   /* Chrome: any XHTML public id (Mobile 1.1/1.2 too) */
    }
    free(name); free(ids[0]); free(ids[1]);
    return true;
}

typedef struct { const char *q; size_t qn; size_t vstart; } RawAttr;

static bool start_tag(XP *p) {
    p->i++;
    size_t qn = name_len(p); if (!qn) return fail(p, "bad start tag");
    const char *q = p->s + p->i; p->i += qn;
    VEC(RawAttr) ra = { 0 }; VEC(char *) vals = { 0 };
    bool empty = false, ok = true;
    int mark = p->binds.n;
    for (;;) {
        size_t w = p->i; skip_ws(p);
        if (p->i >= p->n) { ok = fail(p, "unterminated start tag"); break; }
        if (p->s[p->i] == '>') { p->i++; break; }
        if (at(p, "/>")) { p->i += 2; empty = true; break; }
        size_t an = name_len(p);
        if (!an || w == p->i) { ok = fail(p, "bad attribute"); break; }
        RawAttr r = { p->s + p->i, an, 0 }; p->i += an; skip_ws(p);
        if (p->i >= p->n || p->s[p->i] != '=') { ok = fail(p, "attribute without a value"); break; }
        p->i++; skip_ws(p);
        char qc = p->i < p->n ? p->s[p->i] : 0;
        const char *e = qc == '"' || qc == '\'' ? memchr(p->s + p->i + 1, qc, p->n - p->i - 1) : NULL;
        if (!e) { ok = fail(p, "unquoted attribute value"); break; }
        sb_clear(&p->tmp);
        if (!decode(p, p->s + p->i + 1, (size_t)(e - p->s - p->i - 1), &p->tmp, true, 0)) { ok = false; break; }
        p->i = (size_t)(e - p->s) + 1;
        for (int k = 0; k < ra.n; k++) if (ra.v[k].qn == an && !memcmp(ra.v[k].q, r.q, an)) ok = fail(p, "duplicate attribute");
        if (!ok) break;
        vec_push(ra, r); vec_push(vals, xstrdup(p->tmp.s ? p->tmp.s : ""));
        if (an == 5 && !memcmp(r.q, "xmlns", 5)) { XBind b = { atom(""), *vals.v[vals.n - 1] ? atom(vals.v[vals.n - 1]) : NULL }; vec_push(p->binds, b); }
        else if (an > 6 && !memcmp(r.q, "xmlns:", 6)) {
            if (!*vals.v[vals.n - 1]) { ok = fail(p, "empty namespace declaration"); break; }
            XBind b = { atomn(r.q + 6, an - 6), atom(vals.v[vals.n - 1]) }; vec_push(p->binds, b);
        }
    }
    Node *el = NULL;
    if (ok) {
        const char *colon = memchr(q, ':', qn);
        const char *pfx = colon ? atomn(q, (size_t)(colon - q)) : atom(""), *local = colon ? colon + 1 : q;
        size_t ln = qn - (size_t)(local - q);
        const char *uri = lookup(p, pfx);
        if (uri == (const char *)-1 || !ln) ok = fail(p, "unbound namespace prefix");
        else if (!p->open.n && p->d->html) ok = fail(p, "extra content at the end of the document");
        else {
            int ns = !uri ? NS_NONE : !strcmp(uri, XHTML_NS) ? NS_HTML : !strcmp(uri, SVG_NS) ? NS_SVG : !strcmp(uri, MATHML_NS) ? NS_MATHML : -1;
            /* SVG and MathML tags are lower-case atoms, as from createElementNS */
            const char *tag = ns == NS_SVG || ns == NS_MATHML ? atom_lower(local, ln) : atomn(local, ln);
            el = node_new_element(p->d, tag, ns < 0 ? NS_NONE : ns);
            if (ns < 0) el->xns = uri;
            if (*pfx) el->xpfx = pfx;
            if (tag != atomn(local, ln)) el->xln = atomn(local, ln);
            for (int k = 0; k < ra.n && ok; k++) {   /* attribute prefixes must be bound; unprefixed ones have no namespace */
                const char *qa = atomn(ra.v[k].q, ra.v[k].qn), *ac = memchr(ra.v[k].q, ':', ra.v[k].qn), *apfx = NULL, *auri = NULL;
                if (ac) {
                    apfx = atomn(ra.v[k].q, (size_t)(ac - ra.v[k].q));
                    if ((auri = lookup(p, apfx)) == (const char *)-1) { ok = fail(p, "unbound attribute prefix"); break; }
                    auri = auri ? atom(auri) : NULL;
                } else if (qa == atom("xmlns")) auri = atom(XMLNS_NS);
                node_set_attr_at(el, -1, qa, vals.v[k], auri, apfx);
            }
            node_append(top(p), el);
            if (!p->open.n) p->d->html = el;
            if (!empty) { vec_push(p->open, el); vec_push(p->marks, mark); }
        }
    }
    if (!ok || empty) p->binds.n = mark;
    for (int k = 0; k < vals.n; k++) free(vals.v[k]);
    vec_free(ra); vec_free(vals);
    return ok;
}

static bool end_tag(XP *p) {
    p->i += 2;
    size_t qn = name_len(p);
    const char *q = p->s + p->i; p->i += qn; skip_ws(p);
    if (!qn || p->i >= p->n || p->s[p->i] != '>') return fail(p, "bad end tag");
    p->i++;
    if (!p->open.n) return fail(p, "end tag without a start tag");
    Node *el = p->open.v[p->open.n - 1];
    const char *ln = el->xln ? el->xln : el->tag;
    size_t pl = el->xpfx ? strlen(el->xpfx) + 1 : 0;
    if (qn != pl + strlen(ln) || (pl && (memcmp(q, el->xpfx, pl - 1) || q[pl - 1] != ':')) || memcmp(q + pl, ln, qn - pl)) return fail(p, "mismatched end tag");
    p->open.n--; p->binds.n = p->marks.v[--p->marks.n];
    return true;
}

static void parse(XP *p) {
    if (at(p, "\xEF\xBB\xBF")) p->i = 3;
    size_t start = p->i;
    while (p->i < p->n && !p->err) {
        if (p->s[p->i] != '<') {
            const char *e = memchr(p->s + p->i, '<', p->n - p->i);
            size_t j = e ? (size_t)(e - p->s) : p->n;
            if (!p->open.n) {
                for (size_t k = p->i; k < j; k++) if (!ws(p->s[k])) { fail(p, "content outside the root element"); break; }
            } else {
                sb_clear(&p->tmp);
                if (decode(p, p->s + p->i, j - p->i, &p->tmp, false, 0) && p->tmp.n) add_text(p, p->tmp.s, p->tmp.n);
            }
            p->i = j; continue;
        }
        if (at(p, "<!--")) {
            const char *e = strstr(p->s + p->i + 4, "-->");
            if (!e) { fail(p, "unterminated comment"); break; }
            node_append(top(p), node_new_comment(p->d, p->s + p->i + 4, (size_t)(e - p->s - p->i - 4)));
            p->i = (size_t)(e - p->s) + 3;
        } else if (at(p, "<![CDATA[")) {
            const char *e = strstr(p->s + p->i + 9, "]]>");
            if (!e || !p->open.n) { fail(p, "bad CDATA section"); break; }
            node_append(top(p), node_new_cdata(p->d, p->s + p->i + 9, (size_t)(e - p->s - p->i - 9)));
            p->i = (size_t)(e - p->s) + 3;
        } else if (at(p, "<?")) {
            p->i += 2;
            size_t tn = name_len(p); const char *t = p->s + p->i;
            const char *e = strstr(p->s + p->i, "?>");
            if (!tn || !e) { fail(p, "bad processing instruction"); break; }
            p->i += tn;
            if (tn == 3 && !strncasecmp(t, "xml", 3)) { if (t - 2 != p->s + start) { fail(p, "XML declaration not at the start"); break; } }
            else {
                if (p->i < (size_t)(e - p->s) && !ws(p->s[p->i])) { fail(p, "bad processing instruction"); break; }
                skip_ws(p);
                size_t dl = (size_t)(e - p->s) > p->i ? (size_t)(e - p->s) - p->i : 0;
                char *tg = xstrndup(t, tn);
                node_append(top(p), node_new_pi(p->d, atom(tg), p->s + p->i, dl));
                free(tg);
            }
            p->i = (size_t)(e - p->s) + 2;
        } else if (at(p, "<!DOCTYPE")) { if (!doctype(p)) break; }
        else if (at(p, "</")) { if (!end_tag(p)) break; }
        else if (!start_tag(p)) break;
    }
    if (!p->err && p->open.n) fail(p, "unclosed element");
    if (!p->err && !p->d->html) fail(p, "no root element");
}

void xml_parse(Document *d, const char *src, size_t len) {
    d->node.ns = NS_NONE;
    /* line ends become LF before parsing */
    char *buf = xmalloc(len + 1); size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\r') { buf[n++] = '\n'; if (i + 1 < len && src[i + 1] == '\n') i++; }
        else buf[n++] = src[i];
    }
    buf[n] = 0;
    XP p = { .d = d, .s = buf, .n = n };
    sb_init(&p.tmp);
    parse(&p);
    if (p.err) {
        while (d->node.first) { Node *c = d->node.first; node_remove(c); node_free_tree(c); }
        d->html = d->head = d->body = NULL;
        Node *pe = node_new_element(d, atom("parsererror"), NS_NONE);
        pe->xns = atom("http://www.mozilla.org/newlayout/xml/parsererror.xml");
        char msg[160]; snprintf(msg, sizeof msg, "XML Parsing Error: %s", p.err);
        node_append(pe, node_new_text(d, msg, strlen(msg)));
        node_append(&d->node, pe);
        d->html = pe;
    } else if (d->html->ns == NS_HTML && d->html->tag == A_html && !d->html->xpfx) {
        for (Node *c = d->html->first; c; c = c->next) {
            if (c->type != NODE_ELEMENT || c->ns != NS_HTML) continue;
            if (c->tag == A_head && !d->head) d->head = c;
            else if (c->tag == A_body && !d->body) d->body = c;
        }
    }
    for (int k = 0; k < p.ents.n; k++) free(p.ents.v[k].value);
    vec_free(p.ents); vec_free(p.binds); vec_free(p.open); vec_free(p.marks);
    sb_free(&p.tmp); free(buf);
}
