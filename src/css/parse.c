/* CSS Syntax Level 3 tokenizer, stylesheet & selector parser */
#include "css.h"
#include "../base/url.h"
#include <ctype.h>
#include <math.h>

void clex_init(CLexer *l, const char *s, size_t n) { l->s = s; l->n = n; l->i = 0; }
static bool name_start(int c) { return isalpha(c) || c == '_' || c >= 0x80; }
static bool name_char(int c) { return name_start(c) || isdigit(c) || c == '-'; }
static bool valid_esc(CLexer *l, size_t i) { return i + 1 < l->n + 1 && i < l->n && l->s[i] == '\\' && (i + 1 >= l->n || l->s[i + 1] != '\n'); }
static bool starts_ident(CLexer *l, size_t i) {
    if (i >= l->n) return false;
    int c = (unsigned char)l->s[i];
    if (c == '-') { if (i + 1 >= l->n) return false; int d = (unsigned char)l->s[i + 1]; return name_start(d) || d == '-' || valid_esc(l, i + 1); }
    if (name_start(c)) return true;
    if (c == '\\') return valid_esc(l, i);
    return false;
}
static void consume_escape(CLexer *l, SB *b) {
    /* l->s[l->i] is after the backslash */
    if (l->i >= l->n) { sb_utf8(b, 0xFFFD); return; }
    if (isxdigit((unsigned char)l->s[l->i])) {
        uint32_t cp = 0; int k = 0;
        while (k < 6 && l->i < l->n && isxdigit((unsigned char)l->s[l->i])) { char c = l->s[l->i++]; cp = cp * 16 + (uint32_t)(isdigit((unsigned char)c) ? c - '0' : lc(c) - 'a' + 10); k++; }
        if (l->i < l->n && is_ws((unsigned char)l->s[l->i])) l->i++;
        if (!cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        sb_utf8(b, cp);
    } else sb_putc(b, l->s[l->i++]);
}
static char *consume_name(CLexer *l) {
    SB b; sb_init(&b);
    while (l->i < l->n) {
        int c = (unsigned char)l->s[l->i];
        if (name_char(c)) { sb_putc(&b, (char)c); l->i++; }
        else if (c == '\\' && valid_esc(l, l->i)) { l->i++; consume_escape(l, &b); }
        else break;
    }
    return sb_take(&b);
}
static double consume_number(CLexer *l, bool *is_int) {
    size_t st = l->i; *is_int = true;
    if (l->i < l->n && (l->s[l->i] == '+' || l->s[l->i] == '-')) l->i++;
    while (l->i < l->n && isdigit((unsigned char)l->s[l->i])) l->i++;
    if (l->i + 1 < l->n && l->s[l->i] == '.' && isdigit((unsigned char)l->s[l->i + 1])) { *is_int = false; l->i++; while (l->i < l->n && isdigit((unsigned char)l->s[l->i])) l->i++; }
    if (l->i + 1 < l->n && (l->s[l->i] == 'e' || l->s[l->i] == 'E')) {
        size_t j = l->i + 1; if (j < l->n && (l->s[j] == '+' || l->s[j] == '-')) j++;
        if (j < l->n && isdigit((unsigned char)l->s[j])) { *is_int = false; l->i = j; while (l->i < l->n && isdigit((unsigned char)l->s[l->i])) l->i++; }
    }
    char buf[64]; size_t n = LMIN(l->i - st, sizeof buf - 1); memcpy(buf, l->s + st, n); buf[n] = 0;
    return strtod(buf, NULL);
}
static bool starts_number(CLexer *l, size_t i) {
    if (i >= l->n) return false;
    char c = l->s[i];
    if (c == '+' || c == '-') { i++; if (i >= l->n) return false; c = l->s[i]; }
    if (isdigit((unsigned char)c)) return true;
    if (c == '.' && i + 1 < l->n && isdigit((unsigned char)l->s[i + 1])) return true;
    return false;
}

CTok clex_next(CLexer *l) {
    CTok t; memset(&t, 0, sizeof t);
    /* skip comments */
    while (l->i + 1 < l->n && l->s[l->i] == '/' && l->s[l->i + 1] == '*') {
        const char *e = NULL;
        for (size_t k = l->i + 2; k + 1 < l->n; k++) if (l->s[k] == '*' && l->s[k + 1] == '/') { e = l->s + k; break; }
        l->i = e ? (size_t)(e - l->s) + 2 : l->n;
    }
    t.start = l->s + l->i;
    if (l->i >= l->n) { t.type = CT_EOF; return t; }
    int c = (unsigned char)l->s[l->i];
    if (is_ws(c)) { while (l->i < l->n && is_ws((unsigned char)l->s[l->i])) l->i++; t.type = CT_WS; goto done; }
    if (c == '"' || c == '\'') {
        l->i++; SB b; sb_init(&b);
        while (l->i < l->n) {
            char d = l->s[l->i];
            if (d == c) { l->i++; break; }
            if (d == '\n') { break; }
            if (d == '\\') { l->i++; if (l->i < l->n && l->s[l->i] == '\n') { l->i++; continue; } consume_escape(l, &b); continue; }
            sb_putc(&b, d); l->i++;
        }
        t.type = CT_STRING; t.str = sb_take(&b); goto done;
    }
    if (c == '#') {
        if (l->i + 1 < l->n && (name_char((unsigned char)l->s[l->i + 1]) || valid_esc(l, l->i + 1))) { l->i++; t.type = CT_HASH; t.str = consume_name(l); goto done; }
        l->i++; t.type = CT_DELIM; goto done;
    }
    if (starts_number(l, l->i)) {
        t.num = consume_number(l, &t.is_int);
        if (starts_ident(l, l->i)) { char *u = consume_name(l); snprintf(t.unit, sizeof t.unit, "%s", u); for (char *p = t.unit; *p; p++) *p = (char)lc(*p); free(u); t.type = CT_DIM; }
        else if (l->i < l->n && l->s[l->i] == '%') { l->i++; t.type = CT_PERCENT; }
        else t.type = CT_NUMBER;
        goto done;
    }
    if (c == '<' && l->i + 3 < l->n && !memcmp(l->s + l->i, "<!--", 4)) { l->i += 4; t.type = CT_CDO; goto done; }
    if (c == '-' && l->i + 2 < l->n && !memcmp(l->s + l->i, "-->", 3)) { l->i += 3; t.type = CT_CDC; goto done; }
    if (starts_ident(l, l->i)) {
        char *name = consume_name(l);
        if (l->i < l->n && l->s[l->i] == '(') {
            l->i++;
            if (str_ieq(name, "url")) {
                size_t j = l->i; while (j < l->n && is_ws((unsigned char)l->s[j])) j++;
                if (j < l->n && l->s[j] != '"' && l->s[j] != '\'') {
                    l->i = j; SB b; sb_init(&b);
                    while (l->i < l->n && l->s[l->i] != ')') { if (l->s[l->i] == '\\') { l->i++; consume_escape(l, &b); continue; } sb_putc(&b, l->s[l->i++]); }
                    if (l->i < l->n) l->i++;
                    free(name); t.type = CT_URL; t.str = sb_take(&b);
                    char *tr = str_trim(t.str); memmove(t.str, tr, strlen(tr) + 1);
                    goto done;
                }
            }
            t.type = CT_FUNC; t.str = name; goto done;
        }
        t.type = CT_IDENT; t.str = name; goto done;
    }
    if (c == '@') { l->i++; if (starts_ident(l, l->i)) { t.type = CT_AT; t.str = consume_name(l); } else t.type = CT_DELIM; goto done; }
    if (c == '\\') { if (valid_esc(l, l->i)) { char *name = consume_name(l); t.type = CT_IDENT; t.str = name; goto done; } }
    l->i++;
    switch (c) {
    case ':': t.type = CT_COLON; break; case ';': t.type = CT_SEMI; break; case ',': t.type = CT_COMMA; break;
    case '[': t.type = CT_LBRACK; break; case ']': t.type = CT_RBRACK; break; case '(': t.type = CT_LPAREN; break;
    case ')': t.type = CT_RPAREN; break; case '{': t.type = CT_LBRACE; break; case '}': t.type = CT_RBRACE; break;
    default: t.type = CT_DELIM;
    }
done:
    t.len = (size_t)((l->s + l->i) - t.start);
    return t;
}

/* ---------------- selectors ---------------- */
typedef struct { const char *s; size_t i, n; } SP;
static void sp_ws(SP *p) { while (p->i < p->n) { if (is_ws((unsigned char)p->s[p->i])) p->i++; else if (p->i + 1 < p->n && p->s[p->i] == '/' && p->s[p->i + 1] == '*') { const char *e = strstr(p->s + p->i + 2, "*/"); p->i = e ? (size_t)(e - p->s) + 2 : p->n; } else break; } }
static char *sp_ident(SP *p) {
    CLexer l; clex_init(&l, p->s, p->n); l.i = p->i;
    if (!starts_ident(&l, l.i)) {
        /* allow names starting with a digit? no. */
        return NULL;
    }
    char *r = consume_name(&l); p->i = l.i; return r;
}
static bool parse_sellist(SP *p, SelList *out, bool relative, char endc);

static bool parse_nth(const char *s, int *a, int *b, const char **rest) {
    while (is_ws((unsigned char)*s)) s++;
    if (str_istarts(s, "odd")) { *a = 2; *b = 1; s += 3; }
    else if (str_istarts(s, "even")) { *a = 2; *b = 0; s += 4; }
    else {
        int sign = 1; const char *q = s;
        if (*q == '+') q++; else if (*q == '-') { sign = -1; q++; }
        if (isdigit((unsigned char)*q) || lc(*q) == 'n') {
            int num = 0; bool has_num = false;
            while (isdigit((unsigned char)*q)) { num = num * 10 + (*q - '0'); q++; has_num = true; }
            if (lc(*q) == 'n') {
                *a = sign * (has_num ? num : 1); q++;
                while (is_ws((unsigned char)*q)) q++;
                if (*q == '+' || *q == '-') { int bs = *q == '-' ? -1 : 1; q++; while (is_ws((unsigned char)*q)) q++; if (!isdigit((unsigned char)*q)) return false; int bn = 0; while (isdigit((unsigned char)*q)) { bn = bn * 10 + (*q - '0'); q++; } *b = bs * bn; }
                else *b = 0;
            } else { *a = 0; *b = sign * num; }
            s = q;
        } else return false;
    }
    while (is_ws((unsigned char)*s)) s++;
    if (rest) *rest = s;
    return true;
}

static size_t find_close_paren(const char *s, size_t i, size_t n) {
    int depth = 1; char q = 0;
    for (; i < n; i++) {
        char c = s[i];
        if (q) { if (c == '\\') i++; else if (c == q) q = 0; continue; }
        if (c == '\\') { i++; continue; }
        if (c == '"' || c == '\'') q = c;
        else if (c == '(') depth++;
        else if (c == ')') { if (--depth == 0) return i; }
    }
    return n;
}

static bool parse_compound(SP *p, Compound *c, Selector *sel) {
    VEC(SimpleSel) v = {0};
    bool any = false;
    while (p->i < p->n) {
        char ch = p->s[p->i];
        SimpleSel s; memset(&s, 0, sizeof s);
        if (ch == '*') { p->i++; s.kind = SK_UNIVERSAL; if (p->i < p->n && p->s[p->i] == '|') { p->i++; if (p->s[p->i] == '*') p->i++; else { char *x = sp_ident(p); free(x); } } }
        else if (ch == '#') { p->i++; char *id = sp_ident(p); if (!id) { CLexer l; clex_init(&l, p->s, p->n); l.i = p->i; id = consume_name(&l); p->i = l.i; if (!id[0]) { free(id); goto fail; } } s.kind = SK_ID; s.name = atom(id); free(id); }
        else if (ch == '.') { p->i++; char *cl = sp_ident(p); if (!cl) goto fail; s.kind = SK_CLASS; s.name = atom(cl); free(cl); }
        else if (ch == '[') {
            p->i++; sp_ws(p);
            char *an = sp_ident(p); if (!an) { if (p->i < p->n && p->s[p->i] == '*') { p->i++; if (p->s[p->i] == '|') p->i++; an = sp_ident(p); } if (!an) goto fail; }
            if (p->i < p->n && p->s[p->i] == '|' && p->i + 1 < p->n && p->s[p->i + 1] != '=') { p->i++; free(an); an = sp_ident(p); if (!an) goto fail; }
            s.kind = SK_ATTR; s.name = atom_lower(an, strlen(an)); free(an);
            sp_ws(p);
            if (p->i < p->n && p->s[p->i] != ']') {
                char op = p->s[p->i];
                if (op == '=') { s.op = '='; p->i++; }
                else if (strchr("~|^$*", op) && p->i + 1 < p->n && p->s[p->i + 1] == '=') { s.op = (uint8_t)op; p->i += 2; }
                else goto fail;
                sp_ws(p);
                if (p->i < p->n && (p->s[p->i] == '"' || p->s[p->i] == '\'')) {
                    CLexer l; clex_init(&l, p->s, p->n); l.i = p->i; CTok t = clex_next(&l); p->i = l.i; s.value = t.str ? t.str : xstrdup("");
                } else { char *v = sp_ident(p); if (!v) { size_t st = p->i; while (p->i < p->n && p->s[p->i] != ']' && !is_ws((unsigned char)p->s[p->i])) p->i++; v = xstrndup(p->s + st, p->i - st); } s.value = v; }
                sp_ws(p);
                if (p->i < p->n && (p->s[p->i] == 'i' || p->s[p->i] == 'I')) { s.ci = 1; p->i++; sp_ws(p); }
                else if (p->i < p->n && (p->s[p->i] == 's' || p->s[p->i] == 'S')) { s.ci = 2; p->i++; sp_ws(p); }
            }
            if (p->i >= p->n || p->s[p->i] != ']') { free(s.value); goto fail; }
            p->i++;
        }
        else if (ch == ':') {
            p->i++; bool el = false;
            if (p->i < p->n && p->s[p->i] == ':') { el = true; p->i++; }
            char *nm = sp_ident(p); if (!nm) goto fail;
            for (char *q = nm; *q; q++) *q = (char)lc(*q);
            s.name = atom(nm); free(nm);
            if (!el && (s.name == atom("before") || s.name == atom("after") || s.name == atom("first-line") || s.name == atom("first-letter"))) el = true;
            s.kind = el ? SK_PSEUDO_EL : SK_PSEUDO;
            if (el) sel->pseudo_el = s.name == atom("before") ? 1 : s.name == atom("after") ? 2 : 3;
            if ((p->i >= p->n || p->s[p->i] != '(') && (s.name == atom("not") || s.name == atom("is") || s.name == atom("where") || s.name == atom("has") || str_starts(s.name, "nth-"))) goto fail;
            if (p->i < p->n && p->s[p->i] == '(') {
                p->i++;
                size_t close = find_close_paren(p->s, p->i, p->n);
                char *arg = xstrndup(p->s + p->i, close - p->i);
                p->i = close < p->n ? close + 1 : p->n;
                const char *nmx = s.name;
                if (nmx == atom("not") || nmx == atom("is") || nmx == atom("where") || nmx == atom("matches") || nmx == atom("-webkit-any") || nmx == atom("has") || nmx == atom("host") || nmx == atom("host-context") || nmx == atom("slotted") || nmx == atom("-moz-any")) {
                    s.sub = xcalloc(1, sizeof(SelList));
                    SP q = { arg, 0, strlen(arg) };
                    if (!parse_sellist(&q, s.sub, nmx == atom("has"), 0)) {
                        /* forgiving selector list for :is/:where */
                        if (nmx == atom("not") || nmx == atom("has")) { free(arg); css_sellist_free(s.sub); free(s.sub); goto fail; }
                    }
                } else if (str_starts(nmx, "nth-")) {
                    const char *rest = NULL;
                    if (!parse_nth(arg, &s.a, &s.b, &rest)) { free(arg); goto fail; }
                    if (rest && *rest && !str_istarts(rest, "of ")) { free(arg); goto fail; }
                    if (rest && str_istarts(rest, "of ")) { s.sub = xcalloc(1, sizeof(SelList)); SP q = { rest + 3, 0, strlen(rest + 3) }; parse_sellist(&q, s.sub, false, 0); }
                } else s.value = xstrdup(str_trim(arg));
                free(arg);
            }
        }
        else if (name_start((unsigned char)ch) || ch == '\\' || (ch == '-' )) {
            char *t = sp_ident(p); if (!t) goto fail;
            if (p->i < p->n && p->s[p->i] == '|') { p->i++; free(t); if (p->i < p->n && p->s[p->i] == '*') { p->i++; s.kind = SK_UNIVERSAL; vec_push(v, s); any = true; continue; } t = sp_ident(p); if (!t) goto fail; }
            s.kind = SK_TYPE; s.name = atom(t); s.value = (char *)atom_lower(t, strlen(t)); free(t);
            s.op = 1; /* marks value as atom (not owned) */
        }
        else break;
        vec_push(v, s); any = true;
    }
    if (!any) goto fail;
    if (v.n < v.cap) v.v = xrealloc(v.v, sizeof *v.v * (size_t)v.n);
    c->s = v.v; c->n = v.n;
    return true;
fail:
    for (int i = 0; i < v.n; i++) { if (v.v[i].kind != SK_TYPE) free(v.v[i].value); if (v.v[i].sub) { css_sellist_free(v.v[i].sub); free(v.v[i].sub); } }
    vec_free(v);
    return false;
}

static uint32_t spec_of_list(const SelList *l);
static uint32_t compute_spec(const Selector *s) {
    uint32_t a = 0, b = 0, c = 0;
    for (int i = 0; i < s->n; i++) for (int k = 0; k < s->c[i].n; k++) {
        SimpleSel *x = &s->c[i].s[k];
        switch (x->kind) {
        case SK_ID: a++; break;
        case SK_CLASS: case SK_ATTR: b++; break;
        case SK_TYPE: case SK_PSEUDO_EL: c++; break;
        case SK_PSEUDO:
            if (x->name == atom("where")) break;
            if (x->sub && (x->name == atom("not") || x->name == atom("is") || x->name == atom("has") || x->name == atom("matches"))) { uint32_t sp = spec_of_list(x->sub); a += sp >> 20; b += (sp >> 10) & 1023; c += sp & 1023; }
            else b++;
            break;
        }
    }
    return (LMIN(a, 1023u) << 20) | (LMIN(b, 1023u) << 10) | LMIN(c, 1023u);
}
static uint32_t spec_of_list(const SelList *l) { uint32_t m = 0; for (int i = 0; i < l->n; i++) if (l->v[i].spec > m) m = l->v[i].spec; return m; }

static void selector_free(Selector *s) {
    for (int i = 0; i < s->n; i++) {
        for (int k = 0; k < s->c[i].n; k++) { SimpleSel *x = &s->c[i].s[k]; if (x->kind != SK_TYPE) free(x->value); if (x->sub) { css_sellist_free(x->sub); free(x->sub); } }
        free(s->c[i].s);
    }
    free(s->c);
}
void css_sellist_free(SelList *l) { for (int i = 0; i < l->n; i++) selector_free(&l->v[i]); free(l->v); l->v = NULL; l->n = 0; }

static bool parse_selector(SP *p, Selector *sel, bool relative) {
    VEC(Compound) cs = {0};
    memset(sel, 0, sizeof *sel);
    sp_ws(p);
    uint8_t comb = CB_NONE;
    if (relative) {
        if (p->i < p->n && strchr(">+~", p->s[p->i])) { char c = p->s[p->i++]; comb = c == '>' ? CB_CHILD : c == '+' ? CB_ADJ : CB_SIB; sp_ws(p); }
        else comb = CB_DESC;
        /* relative selectors: represent leading combinator with a :scope compound */
        Compound sc = {0}; SimpleSel s = {0}; s.kind = SK_PSEUDO; s.name = atom("scope"); s.op = 2; /* implicit relative anchor */
        sc.s = xmalloc(sizeof s); sc.s[0] = s; sc.n = 1; sc.comb = CB_NONE;
        vec_push(cs, sc);
    }
    for (;;) {
        Compound c = {0};
        if (!parse_compound(p, &c, sel)) goto fail;
        c.comb = cs.n ? comb : CB_NONE;
        vec_push(cs, c);
        size_t save = p->i;
        sp_ws(p);
        if (p->i >= p->n || p->s[p->i] == ',' || p->s[p->i] == ')' || p->s[p->i] == '{') break;
        char ch = p->s[p->i];
        if (ch == '>') { comb = CB_CHILD; p->i++; if (p->i < p->n && p->s[p->i] == '>') p->i++; sp_ws(p); }
        else if (ch == '+') { comb = CB_ADJ; p->i++; sp_ws(p); }
        else if (ch == '~') { comb = CB_SIB; p->i++; sp_ws(p); }
        else if (p->i > save) comb = CB_DESC;
        else goto fail;
    }
    if (cs.n && cs.n < cs.cap) cs.v = xrealloc(cs.v, sizeof *cs.v * (size_t)cs.n);
    sel->c = cs.v; sel->n = cs.n;
    sel->spec = compute_spec(sel);
    return true;
fail:
    { Selector t = { cs.v, cs.n, 0, 0 }; selector_free(&t); }
    return false;
}

static bool parse_sellist(SP *p, SelList *out, bool relative, char endc) {
    VEC(Selector) v = {0};
    bool ok = true;
    for (;;) {
        Selector s;
        sp_ws(p);
        if (parse_selector(p, &s, relative)) vec_push(v, s);
        else {
            ok = false;
            /* skip to next comma at depth 0 */
            int depth = 0;
            while (p->i < p->n) { char c = p->s[p->i]; if (c == '(') depth++; else if (c == ')') { if (!depth) break; depth--; } else if (c == ',' && !depth) break; p->i++; }
        }
        sp_ws(p);
        if (p->i < p->n && p->s[p->i] == ',') { p->i++; continue; }
        break;
    }
    if (v.n && v.n < v.cap) v.v = xrealloc(v.v, sizeof *v.v * (size_t)v.n);
    out->v = v.v; out->n = v.n;
    return ok && p->i >= p->n;
}

bool css_parse_selector_list(const char *src, SelList *out) {
    SP p = { src, 0, strlen(src) };
    bool ok = parse_sellist(&p, out, false, 0);
    if (!ok) { css_sellist_free(out); return false; }
    return true;
}

/* ---------------- declarations ---------------- */
DeclList *css_parse_decls(const char *s, size_t n) {
    DeclList *d = xcalloc(1, sizeof *d);
    size_t i = 0;
    while (i < n) {
        while (i < n && (is_ws((unsigned char)s[i]) || s[i] == ';')) i++;
        if (i >= n) break;
        if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') { const char *e = NULL; for (size_t k = i + 2; k + 1 < n; k++) if (s[k] == '*' && s[k + 1] == '/') { e = s + k; break; } i = e ? (size_t)(e - s) + 2 : n; continue; }
        size_t ns = i;
        while (i < n && s[i] != ':' && s[i] != ';' && s[i] != '{' && s[i] != '}') i++;
        if (i >= n || s[i] != ':') {
            /* skip invalid (incl nested blocks) */
            int depth = 0;
            while (i < n) { if (s[i] == '{') depth++; else if (s[i] == '}') { if (depth) depth--; if (!depth) { i++; break; } } else if (s[i] == ';' && !depth) break; i++; }
            continue;
        }
        size_t ne = i; while (ne > ns && is_ws((unsigned char)s[ne - 1])) ne--;
        i++;
        size_t vs = i; int depth = 0; char q = 0;
        while (i < n) {
            char c = s[i];
            if (q) { if (c == '\\') i++; else if (c == q) q = 0; }
            else if (c == '\\') i++;
            else if (c == '"' || c == '\'') q = c;
            else if (c == '(' || c == '[' || c == '{') depth++;
            else if (c == ')' || c == ']' || c == '}') { if (!depth) break; depth--; }
            else if (c == ';' && !depth) break;
            i++;
        }
        char *val = xstrndup(s + vs, i - vs);
        /* strip comments in value */
        char *cm; while ((cm = strstr(val, "/*"))) { char *e = strstr(cm + 2, "*/"); if (!e) { *cm = 0; break; } memmove(cm, e + 2, strlen(e + 2) + 1); }
        char *v = str_trim(val);
        bool imp = false;
        size_t vl = strlen(v);
        if (vl >= 10) {
            char *bang = strrchr(v, '!');
            if (bang) { char *k = bang + 1; while (is_ws((unsigned char)*k)) k++; if (str_ieq(k, "important")) { imp = true; *bang = 0; v = str_trim(v); } }
        }
        bool custom = ne - ns > 2 && s[ns] == '-' && s[ns + 1] == '-';
        Decl dc; dc.prop = custom ? atomn(s + ns, ne - ns) : atom_lower(s + ns, ne - ns); dc.value = xstrdup(v); dc.important = imp;
        if (custom || v[0]) vec_push(*d, dc); else free(dc.value);
        free(val);
    }
    return d;
}
void css_decls_free(DeclList *d) { if (!d) return; for (int i = 0; i < d->n; i++) free(d->v[i].value); vec_free(*d); free(d); }

/* ---------------- media queries ---------------- */
static bool media_feature(const char *f, const MediaCtx *mc) {
    char buf[256]; snprintf(buf, sizeof buf, "%s", f);
    char *s = str_trim(buf);
    char *colon = strchr(s, ':');
    float em = 16;
    /* range syntax: (width >= 600px), (400px <= width <= 700px) */
    const char *ops[] = { ">=", "<=", ">", "<", "=" };
    if (!colon) {
        for (int oi = 0; oi < 5; oi++) {
            char *o = strstr(s, ops[oi]);
            if (o) {
                char lhs[128], rhs[128]; snprintf(lhs, sizeof lhs, "%.*s", (int)(o - s), s); snprintf(rhs, sizeof rhs, "%s", o + strlen(ops[oi]));
                char *L = str_trim(lhs), *R = str_trim(rhs);
                /* handle double ranges */
                char *o2 = NULL; int oi2 = -1; for (int k = 0; k < 4; k++) { o2 = strstr(R, ops[k]); if (o2) { oi2 = k; break; } }
                if (o2) {
                    char mid[128]; snprintf(mid, sizeof mid, "%.*s", (int)(o2 - R), R);
                    char a[200], b[200]; snprintf(a, sizeof a, "%s %s %s", L, ops[oi], str_trim(mid)); snprintf(b, sizeof b, "%s %s %s", str_trim(mid), ops[oi2], o2 + strlen(ops[oi2]));
                    return media_feature(a, mc) && media_feature(b, mc);
                }
                bool feat_left = isalpha((unsigned char)L[0]);
                const char *feat = feat_left ? L : R; const char *val = feat_left ? R : L;
                float fv = (!strcmp(feat, "width")) ? mc->vw : (!strcmp(feat, "height")) ? mc->vh : (!strcmp(feat, "aspect-ratio")) ? mc->vw / mc->vh : -1;
                if (fv < 0) return false;
                Length l; float v;
                if (strchr(val, '/')) { float a = 0, b = 1; sscanf(val, "%f / %f", &a, &b); v = a / b; }
                else if (css_parse_length(val, &l, em, em, mc)) v = l.px; else return false;
                int op = oi; if (!feat_left) { if (op == 0) op = 1; else if (op == 1) op = 0; else if (op == 2) op = 3; else if (op == 3) op = 2; }
                switch (op) { case 0: return fv >= v; case 1: return fv <= v; case 2: return fv > v; case 3: return fv < v; default: return fabsf(fv - v) < 0.5f; }
            }
        }
        if (!strcmp(s, "color") || !strcmp(s, "hover") || !strcmp(s, "pointer")) return true;
        if (!strcmp(s, "grid")) return false;
        return true;
    }
    *colon = 0; char *name = str_trim(s), *val = str_trim(colon + 1);
    Length l;
    if (!strcmp(name, "min-width")) return css_parse_length(val, &l, em, em, mc) && mc->vw >= l.px;
    if (!strcmp(name, "max-width")) return css_parse_length(val, &l, em, em, mc) && mc->vw <= l.px;
    if (!strcmp(name, "min-height")) return css_parse_length(val, &l, em, em, mc) && mc->vh >= l.px;
    if (!strcmp(name, "max-height")) return css_parse_length(val, &l, em, em, mc) && mc->vh <= l.px;
    if (!strcmp(name, "width")) return css_parse_length(val, &l, em, em, mc) && fabsf(mc->vw - l.px) < 0.5f;
    if (!strcmp(name, "orientation")) return !strcmp(val, mc->vw >= mc->vh ? "landscape" : "portrait");
    if (!strcmp(name, "prefers-color-scheme")) return !strcmp(val, mc->dark ? "dark" : "light");
    if (!strcmp(name, "prefers-reduced-motion")) return !strcmp(val, "no-preference");
    if (!strcmp(name, "prefers-contrast")) return !strcmp(val, "no-preference");
    if (!strcmp(name, "forced-colors")) return !strcmp(val, "none");
    if (!strcmp(name, "hover") || !strcmp(name, "any-hover")) return !strcmp(val, "hover");
    if (!strcmp(name, "pointer") || !strcmp(name, "any-pointer")) return !strcmp(val, "fine");
    if (!strcmp(name, "display-mode")) return !strcmp(val, "browser");
    if (strstr(name, "device-pixel-ratio") || !strcmp(name, "min-resolution") || !strcmp(name, "max-resolution") || !strcmp(name, "resolution")) {
        float v = (float)atof(val); if (strstr(val, "dpi")) v /= 96.f; else if (strstr(val, "x") || strstr(val, "dppx")) {}
        if (str_starts(name, "min") || strstr(name, "min-")) return mc->dpr >= v;
        if (str_starts(name, "max") || strstr(name, "max-")) return mc->dpr <= v;
        return fabsf(mc->dpr - v) < 0.01f;
    }
    if (!strcmp(name, "min-aspect-ratio") || !strcmp(name, "max-aspect-ratio")) { float a = 0, b = 1; sscanf(val, "%f / %f", &a, &b); float r = a / b, cur = mc->vw / mc->vh; return name[1] == 'i' ? cur >= r : cur <= r; }
    if (!strcmp(name, "scripting")) return !strcmp(val, "enabled");
    return false;
}

static bool media_single(const char *q, const MediaCtx *mc) {
    char *s = xstrdup(q); char *t = str_trim(s);
    bool neg = false, result = true;
    if (str_istarts(t, "not ")) { neg = true; t += 4; }
    else if (str_istarts(t, "only ")) t += 5;
    t = str_trim(t);
    /* media type */
    if (t[0] && t[0] != '(') {
        char *e = t; while (*e && !is_ws((unsigned char)*e)) e++;
        char save = *e; *e = 0;
        if (!str_ieq(t, "all") && !str_ieq(t, "screen")) result = false;
        *e = save; t = str_trim(e);
        if (str_istarts(t, "and")) t = str_trim(t + 3);
    }
    /* features joined by and / or */
    bool use_or = false;
    while (*t && result != (use_or)) {
        if (*t == '(') {
            size_t close = find_close_paren(t, 1, strlen(t));
            char *inner = xstrndup(t + 1, close - 1);
            bool r;
            if (inner[0] == '(' || str_istarts(str_trim(inner), "not ")) {
                char *in2 = str_trim(inner);
                if (str_istarts(in2, "not ")) r = !media_single(in2 + 4, mc);
                else r = media_single(in2, mc);
            } else r = media_feature(inner, mc);
            free(inner);
            if (use_or) result = result || r; else result = result && r;
            t = str_trim(t + (close < strlen(t) ? close + 1 : strlen(t)));
            if (str_istarts(t, "and")) { t = str_trim(t + 3); use_or = false; }
            else if (str_istarts(t, "or")) { t = str_trim(t + 2); use_or = true; }
            else break;
        } else break;
    }
    free(s);
    return neg ? !result : result;
}
bool css_media_matches(const char *q, const MediaCtx *mc) {
    if (!q) return true;
    char *s = xstrdup(q); char *t = str_trim(s);
    if (!*t) { free(s); return true; }
    bool any = false;
    int depth = 0; char *st = t;
    for (char *c = t;; c++) {
        if (*c == '(') depth++; else if (*c == ')') depth--;
        if ((*c == ',' && !depth) || !*c) { char sv = *c; *c = 0; if (media_single(st, mc)) any = true; if (!sv) break; st = c + 1; }
    }
    free(s);
    return any;
}

bool css_container_eval(const char *q, const CQEnv *env) {
    char *s = xstrdup(q), *t = str_trim(s); size_t n = strlen(t); bool r = false;
    if (str_istarts(t, "not ")) { r = !css_container_eval(t + 4, env); free(s); return r; }
    int depth = 0;
    for (size_t i = 0; i < n; i++) {
        if (t[i] == '(') depth++; else if (t[i] == ')') depth--;
        else if (!depth && (str_ieqn(t + i, " and ", 5) || str_ieqn(t + i, " or ", 4))) {
            bool isand = str_ieqn(t + i, " and ", 5); t[i] = 0;
            bool a = css_container_eval(t, env), b = css_container_eval(t + i + (isand ? 5 : 4), env);
            free(s); return isand ? a && b : a || b;
        }
    }
    if (n && t[n - 1] == ')' && str_istarts(t, "style(")) {
        t[n - 1] = 0; char *in = t + 6, *colon = strchr(in, ':');
        if (colon) *colon = 0;
        const char *v = env->var ? env->var(env->ud, str_trim(in)) : NULL;
        char vb[256]; snprintf(vb, sizeof vb, "%s", v ? v : ""); char *vt = str_trim(vb);
        r = colon ? !strcmp(vt, str_trim(colon + 1)) : *vt != 0;
    } else if (n >= 2 && t[0] == '(' && t[n - 1] == ')') {
        t[n - 1] = 0; char *in = str_trim(t + 1);
        if (in[0] == '(' || str_istarts(in, "not ") || str_istarts(in, "style(")) r = css_container_eval(in, env);
        else if (env->size) {
            SB b; sb_init(&b);
            for (const char *p = in; *p; ) {
                if (!strncmp(p, "inline-size", 11)) { sb_puts(&b, "width"); p += 11; }
                else if (!strncmp(p, "block-size", 10)) { sb_puts(&b, "height"); p += 10; }
                else sb_putc(&b, *p++);
            }
            bool vert = strstr(b.s, "height") || strstr(b.s, "aspect") || strstr(b.s, "orientation");
            MediaCtx mc = { env->w, env->h, 1, false };
            r = (!vert || env->block) && media_feature(b.s, &mc);
            sb_free(&b);
        }
    }
    free(s);
    return r;
}

extern bool css_property_known(const char *prop);
bool css_supports(const char *cond) {
    char *s = xstrdup(cond); char *t = str_trim(s);
    bool r = true;
    if (str_istarts(t, "not ")) { r = !css_supports(t + 4); free(s); return r; }
    if (str_istarts(t, "selector(")) { free(s); return true; }
    /* split and/or at depth 0 */
    int depth = 0; size_t n = strlen(t);
    for (size_t i = 0; i < n; i++) {
        if (t[i] == '(') depth++; else if (t[i] == ')') depth--;
        else if (!depth && (str_ieqn(t + i, " and ", 5) || str_ieqn(t + i, " or ", 4))) {
            bool isand = str_ieqn(t + i, " and ", 5);
            t[i] = 0; bool a = css_supports(t), b = css_supports(t + i + (isand ? 5 : 4));
            free(s); return isand ? a && b : a || b;
        }
    }
    if (t[0] == '(' && t[n - 1] == ')') {
        t[n - 1] = 0; char *in = str_trim(t + 1);
        char *colon = strchr(in, ':');
        if (colon && in[0] != '(' && !str_istarts(in, "not ")) {
            *colon = 0; char *p = str_trim(in); char *v = str_trim(colon + 1);
            r = (p[0] == '-' && p[1] == '-') || (css_property_known(p) && !strstr(v, "-webkit-") && !strstr(p, "-webkit-"));
            if (!strcmp(p, "display") && (strstr(v, "contents") || strstr(v, "subgrid"))) r = !strstr(v, "subgrid");
            if (strstr(p, "backdrop-filter") || strstr(p, "anchor")) r = false;
        } else r = css_supports(in);
    }
    free(s);
    return r;
}

/* ---------------- stylesheets ---------------- */
typedef struct { StyleSheet *sh; const MediaCtx *mc; uint32_t order; ContainerCond *cq; } PCtx;

static size_t skip_block(const char *s, size_t i, size_t n) {
    /* s[i] == '{' ; returns index after matching '}' */
    int depth = 0; char q = 0;
    for (; i < n; i++) {
        char c = s[i];
        if (q) { if (c == '\\') i++; else if (c == q) q = 0; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { const char *e = NULL; for (size_t k = i + 2; k + 1 < n; k++) if (s[k] == '*' && s[k + 1] == '/') { e = s + k; break; } i = e ? (size_t)(e - s) + 1 : n; continue; }
        if (c == '\\') { i++; continue; }
        if (c == '"' || c == '\'') q = c;
        else if (c == '{') depth++;
        else if (c == '}') { if (--depth == 0) return i + 1; }
    }
    return n;
}

static void add_rules(PCtx *c, const char *prelude, DeclList *decls) {
    SelList sl = {0};
    SP p = { prelude, 0, strlen(prelude) };
    parse_sellist(&p, &sl, false, 0);
    if (!sl.n) { free(sl.v); css_decls_free(decls); return; }
    vec_push(c->sh->decl_lists, decls);
    for (int i = 0; i < sl.n; i++) {
        Rule r; memset(&r, 0, sizeof r);
        r.sel = sl.v[i]; r.decls = decls; r.order = c->order++; r.origin = (uint8_t)c->sh->origin; r.cq = c->cq;
        vec_push(c->sh->rules, r);
    }
    free(sl.v);
}

static void parse_rules(PCtx *c, const char *s, size_t n, const char *parent_sel);

static char *nest_selector(const char *parent, const char *child) {
    /* CSS nesting: replace & with :is(parent) or prepend parent */
    SB b; sb_init(&b);
    char *ch = xstrdup(child);
    char *save = NULL; bool first = true;
    for (char *part = strtok_r(ch, ",", &save); part; part = strtok_r(NULL, ",", &save)) {
        char *pt = str_trim(part);
        if (!first) sb_puts(&b, ", ");
        first = false;
        if (strchr(pt, '&')) {
            for (char *q = pt; *q; q++) { if (*q == '&') { sb_puts(&b, ":is("); sb_puts(&b, parent); sb_putc(&b, ')'); } else sb_putc(&b, *q); }
        } else { sb_puts(&b, ":is("); sb_puts(&b, parent); sb_puts(&b, ") "); sb_puts(&b, pt); }
    }
    free(ch);
    return sb_take(&b);
}

static void parse_style_block(PCtx *c, const char *sel, const char *body, size_t bn) {
    /* detect nested rules */
    bool nested = false;
    for (size_t i = 0; i < bn; i++) if (body[i] == '{') { nested = true; break; }
    if (!nested) { add_rules(c, sel, css_parse_decls(body, bn)); return; }
    /* split: declarations (outside nested blocks) and nested rules */
    SB decls; sb_init(&decls);
    size_t i = 0, st = 0;
    while (i < bn) {
        char ch = body[i];
        if (ch == '\\') { i += 2; continue; }
        if (ch == '"' || ch == '\'') { char q = ch; i++; while (i < bn && body[i] != q) { if (body[i] == '\\') i++; i++; } i++; continue; }
        if (ch == '(') { i = find_close_paren(body, i + 1, bn) + 1; continue; }
        if (ch == ';') { sb_put(&decls, body + st, i - st + 1); st = i + 1; i++; continue; }
        if (ch == '{') {
            char *pre = xstrndup(body + st, i - st); char *pt = str_trim(pre);
            size_t e = skip_block(body, i, bn);
            if (pt[0] == '@') {
                /* nested at-rule (e.g. @media) inside style rule: parse body as nested of same selector */
                SB w; sb_init(&w); sb_puts(&w, pt); sb_puts(&w, "{"); sb_puts(&w, sel); sb_puts(&w, "{"); sb_put(&w, body + i + 1, e - i - 2); sb_puts(&w, "}}");
                parse_rules(c, w.s, w.n, NULL); sb_free(&w);
            } else {
                char *ns = nest_selector(sel, pt);
                parse_style_block(c, ns, body + i + 1, e - i - 2);
                free(ns);
            }
            free(pre);
            i = e; st = e; continue;
        }
        i++;
    }
    if (st < bn) sb_put(&decls, body + st, bn - st);
    add_rules(c, sel, css_parse_decls(decls.s ? decls.s : "", decls.n));
    sb_free(&decls);
}

static void parse_rules(PCtx *c, const char *s, size_t n, const char *parent_sel) {
    size_t i = 0;
    while (i < n) {
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i >= n) break;
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') { const char *e = NULL; for (size_t k = i + 2; k + 1 < n; k++) if (s[k] == '*' && s[k + 1] == '/') { e = s + k; break; } i = e ? (size_t)(e - s) + 2 : n; continue; }
        if (!strncmp(s + i, "<!--", 4)) { i += 4; continue; }
        if (!strncmp(s + i, "-->", 3)) { i += 3; continue; }
        /* find prelude end: '{' or ';' at depth 0 */
        size_t ps = i; int depth = 0; char q = 0;
        while (i < n) {
            char ch = s[i];
            if (q) { if (ch == '\\') i++; else if (ch == q) q = 0; i++; continue; }
            if (ch == '\\') { i += 2; continue; }
            if (ch == '"' || ch == '\'') q = ch;
            else if (ch == '(' || ch == '[') depth++;
            else if (ch == ')' || ch == ']') depth--;
            else if (!depth && (ch == '{' || ch == ';')) break;
            else if (ch == '}' && !depth) break;
            i++;
        }
        if (i >= n) break;
        char *pre = xstrndup(s + ps, i - ps);
        char *pt = str_trim(pre);
        if (s[i] == '}') { i++; free(pre); continue; }
        if (s[i] == ';') {
            if (str_istarts(pt, "@import")) {
                CLexer l; clex_init(&l, pt + 7, strlen(pt + 7)); CTok t;
                do { t = clex_next(&l); if (t.type == CT_WS) continue; break; } while (1);
                char *url = NULL;
                if (t.type == CT_STRING || t.type == CT_URL) url = t.str;
                else if (t.type == CT_FUNC) { free(t.str); CTok u; do { u = clex_next(&l); } while (u.type == CT_WS); if (u.type == CT_STRING) url = u.str; else free(u.str); }
                else free(t.str);
                const char *rest = l.s + l.i;
                if (url && css_media_matches(rest, c->mc)) { char *abs = url_join(c->sh->base_url, url); if (abs) vec_push(c->sh->imports, abs); }
                free(url);
            }
            i++; free(pre); continue;
        }
        /* block */
        size_t be = skip_block(s, i, n);
        const char *body = s + i + 1; size_t bn = be > i + 1 ? be - i - 2 : 0;
        if (be >= n && (be == 0 || s[be - 1] != '}')) bn = n - i - 1;
        if (pt[0] == '@') {
            if (str_istarts(pt, "@media")) { if (css_media_matches(pt + 6, c->mc)) parse_rules(c, body, bn, parent_sel); }
            else if (str_istarts(pt, "@supports")) { if (css_supports(pt + 9)) parse_rules(c, body, bn, parent_sel); }
            else if (str_istarts(pt, "@container")) {
                const char *q = pt + 10; while (is_ws((unsigned char)*q)) q++;
                ContainerCond *cc = xcalloc(1, sizeof *cc);
                if (*q && *q != '(' && !str_istarts(q, "not ") && !str_istarts(q, "style(")) {
                    const char *ne = q; while (*ne && !is_ws((unsigned char)*ne) && *ne != '(') ne++;
                    cc->name = xstrndup(q, (size_t)(ne - q)); q = ne; while (is_ws((unsigned char)*q)) q++;
                }
                cc->query = xstrdup(q); cc->outer = c->cq; vec_push(c->sh->conds, cc);
                ContainerCond *sv = c->cq; c->cq = cc; parse_rules(c, body, bn, parent_sel); c->cq = sv;
            }
            else if (str_istarts(pt, "@layer") || str_istarts(pt, "@document") || str_istarts(pt, "@-moz-document") || str_istarts(pt, "@scope") || str_istarts(pt, "@starting-style")) {
                parse_rules(c, body, bn, parent_sel);
            }
            else if (str_istarts(pt, "@font-face")) {
                SB ff; sb_init(&ff); sb_put(&ff, body, bn); vec_push(c->sh->font_faces, sb_take(&ff));
            }
            /* @keyframes, @page, @font-feature-values, @property, @counter-style: ignored */
        } else if (parent_sel) {
            char *ns = nest_selector(parent_sel, pt); parse_style_block(c, ns, body, bn); free(ns);
        } else parse_style_block(c, pt, body, bn);
        free(pre);
        i = be;
    }
}

size_t g_css_parsed_bytes;
StyleSheet *css_parse_sheet(const char *src, size_t n, const char *base_url, int origin, const MediaCtx *mc) {
    StyleSheet *sh = xcalloc(1, sizeof *sh); g_css_parsed_bytes += n;
    sh->base_url = xstrdup(base_url ? base_url : "about:blank");
    sh->origin = origin;
    PCtx c = { sh, mc, 0 };
    parse_rules(&c, src, n, NULL);
    return sh;
}
void css_sheet_free(StyleSheet *s) {
    if (!s) return;
    for (int i = 0; i < s->rules.n; i++) selector_free(&s->rules.v[i].sel);
    vec_free(s->rules);
    for (int i = 0; i < s->decl_lists.n; i++) css_decls_free(s->decl_lists.v[i]);
    vec_free(s->decl_lists);
    for (int i = 0; i < s->imports.n; i++) free(s->imports.v[i]);
    vec_free(s->imports);
    for (int i = 0; i < s->conds.n; i++) { free(s->conds.v[i]->name); free(s->conds.v[i]->query); free(s->conds.v[i]); }
    vec_free(s->conds);
    for (int i = 0; i < s->font_faces.n; i++) free(s->font_faces.v[i]);
    vec_free(s->font_faces);
    free(s->base_url); free(s);
}

/* ---------------- selector serialization (CSSOM) ---------------- */
static void ser_ident(SB *b, const char *s) {
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] == '-' && !u[1]) { sb_puts(b, "\\-"); return; }
    for (size_t i = 0; u[i]; i++) {
        unsigned c = u[i];
        if (c < 0x20 || c == 0x7f || (i == 0 && isdigit(c)) || (i == 1 && isdigit(c) && u[0] == '-')) sb_printf(b, "\\%x ", c);
        else if (c >= 0x80 || c == '-' || c == '_' || isalnum(c)) sb_putc(b, (char)c);
        else { sb_putc(b, '\\'); sb_putc(b, (char)c); }
    }
}
static void ser_string(SB *b, const char *s) {
    sb_putc(b, '"');
    for (const unsigned char *u = (const unsigned char *)s; *u; u++) {
        if (*u < 0x20 || *u == 0x7f) sb_printf(b, "\\%x ", *u);
        else { if (*u == '"' || *u == '\\') sb_putc(b, '\\'); sb_putc(b, (char)*u); }
    }
    sb_putc(b, '"');
}
static void ser_list(SB *b, const SelList *l);
static void ser_anb(SB *b, int a, int bb) {
    if (!a) { sb_printf(b, "%d", bb); return; }
    if (a == 1) sb_puts(b, "n"); else if (a == -1) sb_puts(b, "-n"); else sb_printf(b, "%dn", a);
    if (bb > 0) sb_printf(b, "+%d", bb); else if (bb < 0) sb_printf(b, "%d", bb);
}
static void ser_selector(SB *b, const Selector *s) {
    static const char *combs[] = { "", " ", " > ", " + ", " ~ " };
    bool first = true;
    for (int i = 0; i < s->n; i++) {
        const Compound *c = &s->c[i];
        if (!i && c->n == 1 && c->s[0].kind == SK_PSEUDO && c->s[0].op == 2) continue;
        if (c->comb) { const char *k = combs[c->comb]; sb_puts(b, !first ? k : (c->comb == CB_DESC ? "" : k + 1)); }
        first = false;
        for (int j = 0; j < c->n; j++) {
            const SimpleSel *x = &c->s[j];
            switch (x->kind) {
            case SK_UNIVERSAL: if (c->n == 1) sb_putc(b, '*'); break;
            case SK_TYPE: ser_ident(b, x->name); break;
            case SK_ID: sb_putc(b, '#'); ser_ident(b, x->name); break;
            case SK_CLASS: sb_putc(b, '.'); ser_ident(b, x->name); break;
            case SK_ATTR:
                sb_putc(b, '['); ser_ident(b, x->name);
                if (x->op) { if (x->op != '=') sb_putc(b, (char)x->op); sb_putc(b, '='); ser_string(b, x->value ? x->value : ""); if (x->ci) sb_puts(b, x->ci == 1 ? " i" : " s"); }
                sb_putc(b, ']'); break;
            case SK_PSEUDO: case SK_PSEUDO_EL:
                sb_puts(b, x->kind == SK_PSEUDO_EL ? "::" : ":"); ser_ident(b, x->name);
                if (str_starts(x->name, "nth-")) { sb_putc(b, '('); ser_anb(b, x->a, x->b); if (x->sub && x->sub->n) { sb_puts(b, " of "); ser_list(b, x->sub); } sb_putc(b, ')'); }
                else if (x->sub) { sb_putc(b, '('); ser_list(b, x->sub); sb_putc(b, ')'); }
                else if (x->value) { sb_putc(b, '('); sb_puts(b, x->value); sb_putc(b, ')'); }
                break;
            }
        }
    }
}
static void ser_list(SB *b, const SelList *l) { for (int i = 0; i < l->n; i++) { if (i) sb_puts(b, ", "); ser_selector(b, &l->v[i]); } }
char *css_selector_text(const char *src) {
    SelList l = {0};
    if (!css_parse_selector_list(src, &l) || !l.n) { css_sellist_free(&l); return NULL; }
    SB b; sb_init(&b); ser_list(&b, &l); css_sellist_free(&l);
    return b.s ? b.s : xstrdup("");
}
