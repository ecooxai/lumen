/* Runs html5lib-tests tree construction .dat files through the HTML parser and compares the
 * serialized trees the way WPT's html/syntax/parsing/resources/test.js does.
 * test_html5lib [-o FAILURES] [-min PASSED] FILE.dat... */
#include "../src/dom/dom.h"

typedef struct { char *key; SB val; } Sec;
typedef struct { VEC(Sec) s; } Case;
typedef VEC(Case) Cases;

static SB *sec(Case *c, const char *k) { for (int i = 0; i < c->s.n; i++) if (!strcmp(c->s.v[i].key, k)) return &c->s.v[i].val; return NULL; }
static const char *secs(Case *c, const char *k) { SB *b = sec(c, k); return b ? (b->s ? b->s : "") : NULL; }

static void attrs_sorted(const Node *n, SB *out, const char *pad) {
    int na = n->nattrs; char **v = xcalloc((size_t)na + 1, sizeof *v);
    for (int i = 0; i < na; i++) {
        const Attr *a = &n->attrs[i]; SB b; sb_init(&b);
        if (a->ns) {
            const char *pre = !strcmp(a->ns, "http://www.w3.org/1999/xlink") ? "xlink" : !strcmp(a->ns, "http://www.w3.org/XML/1998/namespace") ? "xml" : !strcmp(a->ns, "http://www.w3.org/2000/xmlns/") ? "xmlns" : "?";
            const char *colon = a->pfx ? strchr(a->name, ':') : NULL;
            sb_put(&b, pre, strlen(pre)); sb_putc(&b, ' ');
            const char *ln = colon ? colon + 1 : a->name; sb_put(&b, ln, strlen(ln));
        } else sb_put(&b, a->name, strlen(a->name));
        sb_putc(&b, '\x01'); sb_put(&b, a->value, strlen(a->value));
        v[i] = sb_take(&b);
    }
    for (int i = 1; i < na; i++) for (int j = i; j > 0 && strcmp(v[j - 1], v[j]) > 0; j--) { char *t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    for (int i = 0; i < na; i++) {
        char *sep = strchr(v[i], '\x01'); *sep = 0;
        sb_putc(out, '\n'); sb_putc(out, '|'); sb_put(out, pad, strlen(pad)); sb_put(out, v[i], strlen(v[i]));
        sb_put(out, "=\"", 2); sb_put(out, sep + 1, strlen(sep + 1)); sb_putc(out, '"');
        free(v[i]);
    }
    free(v);
}
static void walk(const Node *n, int depth, SB *out);
static void walk_children(const Node *n, int depth, SB *out) { for (const Node *k = n->first; k; k = k->next) walk(k, depth, out); }
static void walk(const Node *n, int depth, SB *out) {
    char pad[512], ipad[512];
    int pl = depth > 0 ? 2 * depth - 1 : 0; if (pl > 500) pl = 500;
    memset(pad, ' ', (size_t)pl); pad[pl] = 0;
    int il = 2 * depth + 1; if (il > 500) il = 500;
    memset(ipad, ' ', (size_t)il); ipad[il] = 0;
#define LINE() do { if (out->n) sb_putc(out, '\n'); sb_putc(out, '|'); sb_put(out, pad, strlen(pad)); } while (0)
    switch (n->type) {
    case NODE_DOCTYPE: {
        const char *pub = node_attr(n, "publicId"), *sys = node_attr(n, "systemId"), *name = n->text ? n->text : "";
        LINE();
        if (*name) {
            sb_put(out, "<!DOCTYPE ", 10); sb_put(out, name, strlen(name));
            if ((pub && *pub) || (sys && *sys)) {
                sb_put(out, " \"", 2); if (pub) sb_put(out, pub, strlen(pub)); sb_put(out, "\" \"", 3); if (sys) sb_put(out, sys, strlen(sys)); sb_putc(out, '"');
            }
            sb_putc(out, '>');
        } else sb_put(out, "<!DOCTYPE >", 11);
        break;
    }
    case NODE_COMMENT: LINE(); sb_put(out, "<!-- ", 5); sb_put(out, n->text ? n->text : "", n->text_len); sb_put(out, " -->", 4); break;
    case NODE_PI: LINE(); sb_put(out, "<?", 2); sb_put(out, n->tag, strlen(n->tag)); sb_putc(out, ' '); sb_put(out, n->text ? n->text : "", n->text_len); sb_put(out, "?>", 2); break;
    case NODE_TEXT: LINE(); sb_putc(out, '"'); sb_put(out, n->text ? n->text : "", n->text_len); sb_putc(out, '"'); break;
    case NODE_ELEMENT: {
        if (node_attr(n, "data-skip")) return;
        LINE(); sb_putc(out, '<');
        if (n->ns == NS_SVG) sb_put(out, "svg ", 4); else if (n->ns == NS_MATHML) sb_put(out, "math ", 5);
        const char *ln = n->ns == NS_HTML ? n->tag : n->xln ? n->xln : n->tag;
        sb_put(out, ln, strlen(ln)); sb_putc(out, '>');
        attrs_sorted(n, out, ipad);
        if (n->ns == NS_HTML && n->tag == A_template) {
            sb_putc(out, '\n'); sb_putc(out, '|'); sb_put(out, ipad, strlen(ipad)); sb_put(out, "content", 7);
            if (n->template_content) walk_children(n->template_content, depth + 2, out);
        }
        break;
    }
    }
#undef LINE
    walk_children(n, depth + 1, out);
}

static Cases parse_dat(char *text, size_t len) {
    Cases cases = { 0 };
    if (len && text[len - 1] == '\n') text[--len] = 0;
    Case *data = NULL; Case cur = { 0 }; const char *key = NULL;
    char *end = text + len;
    for (char *line = text; line;) {
        char *nl = memchr(line, '\n', (size_t)(end - line)); size_t ll = nl ? (size_t)(nl - line + 1) : (size_t)(end - line);
        char head[128] = { 0 };
        if (line[0] == '#') {
            size_t hl = ll - 1; const char *h = line + 1;
            while (hl && (h[hl - 1] == '\n' || h[hl - 1] == ' ' || h[hl - 1] == '\t' || h[hl - 1] == '\r')) hl--;
            while (hl && (*h == ' ' || *h == '\t')) { h++; hl--; }
            if (hl && hl < sizeof head) memcpy(head, h, hl);
        }
        if (head[0]) {
            if (data && (!strcmp(head, "description") || (!strcmp(head, "data") && strcmp(key, "description")))) {
                SB *b = sec(data, key); if (b && b->n) { b->n--; b->s[b->n] = 0; }
                vec_push(cases, cur); memset(&cur, 0, sizeof cur); data = NULL;
            }
            if (!data) data = &cur;
            Sec s = { xstrdup(head), { 0 } }; sb_init(&s.val);
            vec_push(cur.s, s); key = cur.s.v[cur.s.n - 1].key;
        } else if (data) sb_put(sec(data, key), line, ll);
        line = nl ? nl + 1 : NULL;
    }
    if (data) vec_push(cases, cur);
    for (int i = 0; i < cases.n; i++) for (int j = 0; j < cases.v[i].s.n; j++) {
        SB *b = &cases.v[i].s.v[j].val;
        if (b->n && b->s[b->n - 1] == '\n') b->s[--b->n] = 0;
    }
    return cases;
}

int main(int argc, char **argv) {
    dom_init();
    FILE *fail = NULL; int min = -1, total = 0, passed = 0;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "-o") && a + 1 < argc) { fail = fopen(argv[++a], "w"); continue; }
        if (!strcmp(argv[a], "-min") && a + 1 < argc) { min = atoi(argv[++a]); continue; }
        size_t flen; char *text = read_file(argv[a], &flen);
        if (!text) { fprintf(stderr, "cannot read %s\n", argv[a]); return 2; }
        Cases cases = parse_dat(text, flen);
        int fp = 0, ft = 0;
        for (int i = 0; i < cases.n; i++) {
            Case *c = &cases.v[i];
            if (sec(c, "script-off")) continue;
            SB *ib = sec(c, "data"), *eb = sec(c, "document");
            const char *input = ib && ib->s ? ib->s : "", *expected = eb && eb->s ? eb->s : "", *frag = secs(c, "document-fragment");
            size_t ilen = ib ? ib->n : 0, elen = eb ? eb->n : 0;
            Document *d = doc_new("about:blank");
            SB out; sb_init(&out);
            if (frag) {
                const char *sp = strchr(frag, ' ');
                Node *ctx;
                if (sp) {
                    int ns = !strncmp(frag, "svg", 3) ? NS_SVG : NS_MATHML;
                    ctx = node_new_element(d, atom_lower(sp + 1, strlen(sp + 1)), ns);
                    ctx->xln = atom(sp + 1);
                } else ctx = node_new_element(d, atom_lower(frag, strlen(frag)), NS_HTML);
                Node *f = html_parse_fragment(d, ctx, input, ilen);
                walk_children(f, 1, &out);
                node_free_tree(f); node_free_tree(ctx);
            } else {
                html_parse(d, input, ilen);
                walk_children(&d->node, 1, &out);
            }
            ft++;
            if (out.n == elen && !memcmp(out.s ? out.s : "", expected, elen)) fp++;
            else if (fail) fprintf(fail, "=== %s #%d%s%s\n--- input\n%s\n--- expected\n%s\n--- actual\n%s\n\n", argv[a], i + 1,
                                  frag ? " fragment " : "", frag ? frag : "", input, expected, out.s);
            sb_free(&out);
            doc_free(d);
        }
        const char *base = strrchr(argv[a], '/');
        printf("%-48s %4d/%-4d\n", base ? base + 1 : argv[a], fp, ft);
        total += ft; passed += fp;
        free(text);
    }
    printf("TOTAL %d/%d\n", passed, total);
    if (fail) fclose(fail);
    return min >= 0 && passed < min ? 1 : 0;
}
