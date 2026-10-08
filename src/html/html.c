/* HTML5 tokenizer and (simplified but robust) tree builder. */
#include "../dom/dom.h"
#include <ctype.h>

void (*html_script_hook)(Document *d, Node *script, void *ud) = NULL;
void *html_script_hook_ud = NULL;

enum TokType { T_NONE, T_DOCTYPE, T_START, T_END, T_COMMENT, T_CHARS, T_EOF };
enum State { S_DATA, S_RCDATA, S_RAWTEXT, S_SCRIPT, S_PLAINTEXT };
enum Mode { M_INITIAL, M_BEFORE_HTML, M_BEFORE_HEAD, M_IN_HEAD, M_AFTER_HEAD, M_IN_BODY, M_TEXT, M_IN_TABLE, M_IN_SELECT, M_IN_FRAMESET, M_AFTER_BODY };

typedef struct { const char *name; char *value; } TAttr;

struct HtmlParser {
    Document *doc;
    SB in; size_t pos;          /* buffered input */
    bool finished;
    int state;
    const char *raw_end_tag;    /* for rawtext/rcdata/script */
    /* current token */
    int tt; SB tag; bool self_closing; VEC(TAttr) attrs; SB text;
    SB chars;                   /* pending character data */
    /* tree */
    VEC(Node *) open;           /* stack of open elements */
    VEC(Node *) fmt;            /* active formatting elements (simplified) */
    int mode, orig_mode;
    Node *head_el, *form_el;
    Node *frag_ctx; Node *frag_root;
    bool frameset_ok, ignore_lf;
    VEC(int) template_modes;
};

static Node *cur(HtmlParser *p) { return p->open.n ? p->open.v[p->open.n - 1] : (p->frag_root ? p->frag_root : &p->doc->node); }
static Node *insertion_parent(HtmlParser *p) {
    Node *c = cur(p);
    if (c->template_content) return c->template_content;
    return c;
}

HtmlParser *html_parser_new(Document *d, Node *ctx) {
    HtmlParser *p = xcalloc(1, sizeof *p);
    p->doc = d; p->mode = M_INITIAL; p->frameset_ok = true;
    if (ctx) {
        p->frag_ctx = ctx; p->frag_root = node_new_fragment(d);
        p->mode = M_IN_BODY;
        Node *h = node_new_element(d, "html", NS_HTML); node_append(p->frag_root, h); vec_push(p->open, h);
        const char *t = ctx->tag;
        if (t == A_title || t == A_textarea) { p->state = S_RCDATA; p->raw_end_tag = t; }
        else if (t == A_style || t == A_xmp || t == A_iframe || t == A_noscript) { p->state = S_RAWTEXT; p->raw_end_tag = t; }
        else if (t == A_script) { p->state = S_SCRIPT; p->raw_end_tag = t; }
        else if (t == A_table) p->mode = M_IN_TABLE;
        else if (t == A_select) p->mode = M_IN_SELECT;
    }
    return p;
}
void html_parser_free(HtmlParser *p) {
    sb_free(&p->in); sb_free(&p->tag); sb_free(&p->text); sb_free(&p->chars);
    for (int i = 0; i < p->attrs.n; i++) free(p->attrs.v[i].value);
    vec_free(p->attrs); vec_free(p->open); vec_free(p->fmt); vec_free(p->template_modes);
    free(p);
}

/* ---------------- tree construction ---------------- */
static bool in_list(const char *t, const char *const *l) { for (; *l; l++) if (!strcmp(t, *l)) return true; return false; }
static const char *const special_tags[] = { "address","applet","area","article","aside","base","basefont","bgsound","blockquote","body","br","button","caption","center","col","colgroup","dd","details","dir","div","dl","dt","embed","fieldset","figcaption","figure","footer","form","frame","frameset","h1","h2","h3","h4","h5","h6","head","header","hgroup","hr","html","iframe","img","input","keygen","li","link","listing","main","marquee","menu","meta","nav","noembed","noframes","noscript","object","ol","p","param","plaintext","pre","script","search","section","select","source","style","summary","table","tbody","td","template","textarea","tfoot","th","thead","title","tr","track","ul","wbr","xmp", NULL };
static const char *const closes_p[] = { "address","article","aside","blockquote","center","details","dialog","dir","div","dl","fieldset","figcaption","figure","footer","header","hgroup","main","menu","nav","ol","p","search","section","summary","ul","h1","h2","h3","h4","h5","h6","pre","listing","form","table","hr","xmp","plaintext","li","dd","dt", NULL };
static const char *const formatting[] = { "a","b","big","code","em","font","i","nobr","s","small","strike","strong","tt","u", NULL };
static const char *const head_tags[] = { "base","basefont","bgsound","link","meta","noframes","script","style","template","title","noscript", NULL };

static void flush_chars(HtmlParser *p);

static Node *insert_el(HtmlParser *p, const char *tag, int ns) {
    Node *e = node_new_element(p->doc, tag, ns);
    for (int i = 0; i < p->attrs.n; i++) if (!node_has_attr(e, p->attrs.v[i].name)) node_set_attr(e, p->attrs.v[i].name, p->attrs.v[i].value);
    e->flags |= NF_PARSER_INSERTED;
    if (tag == A_template && ns == NS_HTML) { e->template_content = node_new_fragment(p->doc); e->template_content->refcount = 1; e->template_content->flags |= NF_INERT; }
    node_append(insertion_parent(p), e);
    vec_push(p->open, e);
    return e;
}
static void pop(HtmlParser *p) { if (p->open.n) p->open.n--; }
static bool has_in_scope(HtmlParser *p, const char *tag, int kind) {
    static const char *const scope[] = { "applet","caption","html","table","td","th","marquee","object","template", NULL };
    for (int i = p->open.n - 1; i >= 0; i--) {
        Node *n = p->open.v[i];
        if (n->tag == tag && n->ns == NS_HTML) return true;
        if (kind == 3) { if (n->ns == NS_HTML && (n->tag == A_html || n->tag == A_table || n->tag == A_template)) return false; continue; }
        if (n->ns != NS_HTML) { if (n->tag == atom("foreignObject") || n->tag == atom("desc")) return false; continue; }
        if (in_list(n->tag, scope)) return false;
        if (kind == 1 && (n->tag == A_ul || n->tag == A_ol)) return false;        /* list item scope */
        if (kind == 2 && n->tag == A_button) return false;                       /* button scope */
    }
    return false;
}
static void pop_until(HtmlParser *p, const char *tag) {
    while (p->open.n) { Node *n = vec_pop(p->open); if (n->tag == tag && n->ns == NS_HTML) break; }
}
static void gen_implied_end(HtmlParser *p, const char *except) {
    static const char *const l[] = { "dd","dt","li","optgroup","option","p","rb","rp","rt","rtc", NULL };
    while (p->open.n) { Node *n = cur(p); if (n->ns == NS_HTML && in_list(n->tag, l) && n->tag != except) pop(p); else break; }
}
static void close_p(HtmlParser *p) { if (has_in_scope(p, A_p, 2)) { gen_implied_end(p, A_p); pop_until(p, A_p); } }

static void reconstruct_fmt(HtmlParser *p) {
    /* reopen formatting elements that were implicitly closed */
    int start = p->fmt.n;
    while (start > 0) {
        Node *f = p->fmt.v[start - 1];
        if (!f) break;
        bool open = false; for (int i = 0; i < p->open.n; i++) if (p->open.v[i] == f) { open = true; break; }
        if (open) break;
        start--;
    }
    for (int i = start; i < p->fmt.n; i++) {
        Node *f = p->fmt.v[i]; if (!f) continue;
        Node *e = node_new_element(p->doc, f->tag, NS_HTML);
        for (int k = 0; k < f->nattrs; k++) node_set_attr(e, f->attrs[k].name, f->attrs[k].value);
        node_append(insertion_parent(p), e); vec_push(p->open, e);
        p->fmt.v[i] = e;
    }
}
static void fmt_remove(HtmlParser *p, Node *n) { for (int i = 0; i < p->fmt.n; i++) if (p->fmt.v[i] == n) { memmove(&p->fmt.v[i], &p->fmt.v[i + 1], sizeof(Node *) * (size_t)(p->fmt.n - i - 1)); p->fmt.n--; return; } }

/* Simplified adoption agency: close the formatting element and reopen intervening ones as needed. */
static void adoption(HtmlParser *p, const char *tag) {
    Node *f = NULL; int fi = -1;
    for (int i = p->fmt.n - 1; i >= 0; i--) { if (!p->fmt.v[i]) break; if (p->fmt.v[i]->tag == tag) { f = p->fmt.v[i]; fi = i; break; } }
    if (!f) { /* any other end tag */
        for (int i = p->open.n - 1; i >= 0; i--) {
            Node *n = p->open.v[i];
            if (n->tag == tag && n->ns == NS_HTML) { gen_implied_end(p, tag); p->open.n = i; return; }
            if (n->ns == NS_HTML && in_list(n->tag, special_tags)) return;
        }
        return;
    }
    int oi = -1; for (int i = p->open.n - 1; i >= 0; i--) if (p->open.v[i] == f) { oi = i; break; }
    if (oi < 0) { fmt_remove(p, f); return; }
    /* find furthest block */
    int fb = -1; for (int i = oi + 1; i < p->open.n; i++) if (p->open.v[i]->ns == NS_HTML && in_list(p->open.v[i]->tag, special_tags)) { fb = i; break; }
    if (fb < 0) { p->open.n = oi; fmt_remove(p, f); return; }
    /* move furthest block's subtree: create clone of f, move children of furthest block into it */
    Node *block = p->open.v[fb];
    Node *clone = node_new_element(p->doc, f->tag, NS_HTML);
    for (int k = 0; k < f->nattrs; k++) node_set_attr(clone, f->attrs[k].name, f->attrs[k].value);
    while (block->first) node_append(clone, block->first);
    node_append(block, clone);
    /* block moves out of f to after f */
    Node *fp = f->parent; node_insert_before(fp ? fp : insertion_parent(p), block, f->next);
    p->fmt.v[fi] = clone;
    /* fix open stack: remove f, insert clone after block */
    memmove(&p->open.v[oi], &p->open.v[oi + 1], sizeof(Node *) * (size_t)(p->open.n - oi - 1)); p->open.n--;
    fb--;
    p->open.n = fb + 1;
    vec_push(p->open, clone);
    fmt_remove(p, clone);
    pop(p);
}

static bool is_ws_str(const char *s, size_t n) { for (size_t i = 0; i < n; i++) if (!is_ws((unsigned char)s[i])) return false; return true; }

static void insert_text(HtmlParser *p, const char *s, size_t n) {
    Node *par = insertion_parent(p);
    /* foster parenting for text in table context */
    if (par->ns == NS_HTML && (par->tag == A_table || par->tag == A_tbody || par->tag == A_thead || par->tag == A_tfoot || par->tag == A_tr) && !is_ws_str(s, n)) {
        Node *tbl = NULL; for (int i = p->open.n - 1; i >= 0; i--) if (p->open.v[i]->tag == A_table) { tbl = p->open.v[i]; break; }
        if (tbl && tbl->parent) { Node *prev = tbl->prev; if (prev && prev->type == NODE_TEXT) node_text_append(prev, s, n); else node_insert_before(tbl->parent, node_new_text(p->doc, s, n), tbl); return; }
    }
    if (par->last && par->last->type == NODE_TEXT) { node_text_append(par->last, s, n); doc_mark_dirty(p->doc, par->last); }
    else node_append(par, node_new_text(p->doc, s, n));
}

static void ensure_body(HtmlParser *p);
static void ensure_head(HtmlParser *p) {
    if (p->mode == M_INITIAL || p->mode == M_BEFORE_HTML) {
        if (!p->doc->html) { TAttr *sv = p->attrs.v; int sn = p->attrs.n; p->attrs.n = 0; p->doc->html = insert_el(p, "html", NS_HTML); p->attrs.v = sv; p->attrs.n = sn; }
        p->mode = M_BEFORE_HEAD;
    }
    if (p->mode == M_BEFORE_HEAD) {
        int sn = p->attrs.n; p->attrs.n = 0;
        p->head_el = insert_el(p, "head", NS_HTML); p->doc->head = p->head_el;
        p->attrs.n = sn;
        p->mode = M_IN_HEAD;
    }
}
static void ensure_body(HtmlParser *p) {
    if (p->frag_root) return;
    ensure_head(p);
    if (p->mode == M_IN_HEAD) { while (p->open.n > 1 && cur(p) != p->doc->html) pop(p); p->mode = M_AFTER_HEAD; }
    if (p->mode == M_AFTER_HEAD) {
        int sn = p->attrs.n; p->attrs.n = 0;
        p->doc->body = insert_el(p, "body", NS_HTML);
        p->attrs.n = sn;
        p->mode = M_IN_BODY;
    }
}

static const char *svg_fix_tag(const char *t) {
    static const char *const m[] = { "altglyph","altGlyph","altglyphdef","altGlyphDef","altglyphitem","altGlyphItem","animatecolor","animateColor","animatemotion","animateMotion","animatetransform","animateTransform","clippath","clipPath","feblend","feBlend","fecolormatrix","feColorMatrix","fecomponenttransfer","feComponentTransfer","fecomposite","feComposite","feconvolvematrix","feConvolveMatrix","fediffuselighting","feDiffuseLighting","fedisplacementmap","feDisplacementMap","fedistantlight","feDistantLight","fedropshadow","feDropShadow","feflood","feFlood","fefunca","feFuncA","fefuncb","feFuncB","fefuncg","feFuncG","fefuncr","feFuncR","fegaussianblur","feGaussianBlur","feimage","feImage","femerge","feMerge","femergenode","feMergeNode","femorphology","feMorphology","feoffset","feOffset","fepointlight","fePointLight","fespecularlighting","feSpecularLighting","fespotlight","feSpotLight","fetile","feTile","feturbulence","feTurbulence","foreignobject","foreignObject","glyphref","glyphRef","lineargradient","linearGradient","radialgradient","radialGradient","textpath","textPath", NULL };
    for (int i = 0; m[i]; i += 2) if (!strcmp(t, m[i])) return m[i + 1];
    return t;
}
static const char *svg_fix_attr(const char *a) {
    static const char *const m[] = { "attributename","attributeName","basefrequency","baseFrequency","clippathunits","clipPathUnits","diffuseconstant","diffuseConstant","gradienttransform","gradientTransform","gradientunits","gradientUnits","kernelmatrix","kernelMatrix","markerheight","markerHeight","markerunits","markerUnits","markerwidth","markerWidth","maskcontentunits","maskContentUnits","maskunits","maskUnits","numoctaves","numOctaves","pathlength","pathLength","patterncontentunits","patternContentUnits","patterntransform","patternTransform","patternunits","patternUnits","preserveaspectratio","preserveAspectRatio","refx","refX","refy","refY","repeatcount","repeatCount","spreadmethod","spreadMethod","stddeviation","stdDeviation","stitchtiles","stitchTiles","textlength","textLength","viewbox","viewBox","xchannelselector","xChannelSelector","ychannelselector","yChannelSelector", NULL };
    for (int i = 0; m[i]; i += 2) if (!strcmp(a, m[i])) return atom(m[i + 1]);
    return a;
}

static void tree_token(HtmlParser *p);

static void handle_start_body(HtmlParser *p, const char *t) {
    if (t == A_html) { if (p->doc->html) for (int i = 0; i < p->attrs.n; i++) if (!node_has_attr(p->doc->html, p->attrs.v[i].name)) node_set_attr(p->doc->html, p->attrs.v[i].name, p->attrs.v[i].value); return; }
    if (t == A_body) { if (p->doc->body) for (int i = 0; i < p->attrs.n; i++) if (!node_has_attr(p->doc->body, p->attrs.v[i].name)) node_set_attr(p->doc->body, p->attrs.v[i].name, p->attrs.v[i].value); return; }
    if (t == A_frameset || t == A_head) return;
    Node *c = cur(p);
    /* implicit closes */
    if (in_list(t, closes_p)) {
        if (t == A_li) { for (int i = p->open.n - 1; i >= 0; i--) { Node *n = p->open.v[i]; if (n->tag == A_li) { gen_implied_end(p, A_li); pop_until(p, A_li); break; } if (in_list(n->tag, special_tags) && n->tag != A_address && n->tag != A_div && n->tag != A_p) break; } }
        else if (t == A_dd || t == A_dt) { for (int i = p->open.n - 1; i >= 0; i--) { Node *n = p->open.v[i]; if (n->tag == A_dd || n->tag == A_dt) { gen_implied_end(p, n->tag); pop_until(p, n->tag); break; } if (in_list(n->tag, special_tags) && n->tag != A_address && n->tag != A_div && n->tag != A_p) break; } }
        if (t != A_table || !p->doc->quirks) close_p(p);
        c = cur(p);
        if ((t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2]) && c->tag[0] == 'h' && c->tag[1] >= '1' && c->tag[1] <= '6' && !c->tag[2]) pop(p);
        if (t == A_form && p->form_el) return;
    }
    if (t == A_button && has_in_scope(p, A_button, 0)) { gen_implied_end(p, NULL); pop_until(p, A_button); }
    if (t == A_a) { for (int i = p->fmt.n - 1; i >= 0 && p->fmt.v[i]; i--) if (p->fmt.v[i]->tag == A_a) { adoption(p, A_a); break; } }
    if (t == A_nobr && has_in_scope(p, A_nobr, 0)) adoption(p, A_nobr);
    if ((t == A_option) && cur(p)->tag == A_option) pop(p);
    if ((t == A_optgroup) && cur(p)->tag == A_option) pop(p);
    if (in_list(t, formatting) || !in_list(t, special_tags)) reconstruct_fmt(p);
    if (t == A_table) { p->mode = M_IN_TABLE; }
    if (t == A_svg || t == A_math) {
        insert_el(p, t, t == A_svg ? NS_SVG : NS_MATHML);
        if (p->self_closing) pop(p);
        return;
    }
    if (t == A_select) { insert_el(p, t, NS_HTML); p->frameset_ok = false; return; }
    Node *e = insert_el(p, t, NS_HTML);
    if (t == A_form) p->form_el = e;
    if (in_list(t, formatting)) { vec_push(p->fmt, e); }
    if (t == A_template) { vec_push(p->fmt, (Node *)NULL); }
    if (t == A_pre || t == A_listing || t == A_textarea) p->ignore_lf = true;
    if (html_is_void(t) || t == atom("image")) pop(p);
    else if (t == A_textarea || t == A_title) { p->state = S_RCDATA; p->raw_end_tag = t; p->orig_mode = p->mode; p->mode = M_TEXT; }
    else if (t == A_style || t == A_xmp || t == A_iframe || t == atom("noembed") || t == atom("noframes") || t == A_noscript) { p->state = S_RAWTEXT; p->raw_end_tag = t; p->orig_mode = p->mode; p->mode = M_TEXT; }
    else if (t == A_script) { p->state = S_SCRIPT; p->raw_end_tag = t; p->orig_mode = p->mode; p->mode = M_TEXT; }
    else if (t == A_plaintext) { p->state = S_PLAINTEXT; }
}

static void handle_end_body(HtmlParser *p, const char *t) {
    if (t == A_body || t == A_html) { if (has_in_scope(p, A_body, 0) || p->frag_root) p->mode = M_AFTER_BODY; return; }
    if (t == A_p) { if (!has_in_scope(p, A_p, 2)) { int sn = p->attrs.n; p->attrs.n = 0; insert_el(p, "p", NS_HTML); p->attrs.n = sn; } close_p(p); return; }
    if (t == A_li) { if (has_in_scope(p, A_li, 1)) { gen_implied_end(p, A_li); pop_until(p, A_li); } return; }
    if (t == A_br) { int sn = p->attrs.n; p->attrs.n = 0; reconstruct_fmt(p); insert_el(p, "br", NS_HTML); pop(p); p->attrs.n = sn; return; }
    if (t == A_form) { Node *f = p->form_el; p->form_el = NULL; if (f) { for (int i = p->open.n - 1; i >= 0; i--) if (p->open.v[i] == f) { memmove(&p->open.v[i], &p->open.v[i + 1], sizeof(Node *) * (size_t)(p->open.n - i - 1)); p->open.n--; break; } } return; }
    if (t == A_template) {
        if (has_in_scope(p, A_template, 0)) { gen_implied_end(p, NULL); pop_until(p, A_template); while (p->fmt.n) { Node *f = vec_pop(p->fmt); if (!f) break; } }
        return;
    }
    if (in_list(t, formatting)) { adoption(p, t); return; }
    if (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2]) {
        for (int i = p->open.n - 1; i >= 0; i--) { const char *x = p->open.v[i]->tag; if (x[0] == 'h' && x[1] >= '1' && x[1] <= '6' && !x[2]) { gen_implied_end(p, NULL); p->open.n = i; return; } if (in_list(x, special_tags)) break; }
        return;
    }
    if (t == A_table) { if (has_in_scope(p, A_table, 3)) { pop_until(p, A_table); } p->mode = M_IN_BODY; return; }
    if (in_list(t, special_tags)) {
        if (!has_in_scope(p, t, 0)) return;
        gen_implied_end(p, t); pop_until(p, t); return;
    }
    adoption(p, t);
}

/* table handling (simplified): auto-insert tbody/tr, close cells */
static bool table_start(HtmlParser *p, const char *t) {
    if (t == A_caption || t == A_colgroup || t == A_col || t == A_tbody || t == A_thead || t == A_tfoot || t == A_tr || t == A_td || t == A_th) {
        /* find nearest table context */
        int ti = -1; for (int i = p->open.n - 1; i >= 0; i--) if (p->open.v[i]->tag == A_table) { ti = i; break; }
        if (ti < 0) return true; /* ignore stray table parts in body */
        if (t == A_td || t == A_th) {
            /* close current cell */
            for (int i = p->open.n - 1; i > ti; i--) if (p->open.v[i]->tag == A_td || p->open.v[i]->tag == A_th) { p->open.n = i; break; }
            Node *c = cur(p);
            if (c->tag != A_tr) {
                while (cur(p)->tag != A_tbody && cur(p)->tag != A_thead && cur(p)->tag != A_tfoot && cur(p) != p->open.v[ti]) pop(p);
                if (cur(p) == p->open.v[ti]) { int sn = p->attrs.n; p->attrs.n = 0; insert_el(p, "tbody", NS_HTML); p->attrs.n = sn; }
                int sn = p->attrs.n; p->attrs.n = 0; insert_el(p, "tr", NS_HTML); p->attrs.n = sn;
            }
            insert_el(p, t, NS_HTML); vec_push(p->fmt, (Node *)NULL);
            return true;
        }
        if (t == A_tr) {
            p->open.n = LMAX(p->open.n, ti + 1);
            while (p->open.n > ti + 1 && cur(p)->tag != A_tbody && cur(p)->tag != A_thead && cur(p)->tag != A_tfoot) pop(p);
            if (cur(p) == p->open.v[ti]) { int sn = p->attrs.n; p->attrs.n = 0; insert_el(p, "tbody", NS_HTML); p->attrs.n = sn; }
            insert_el(p, t, NS_HTML); return true;
        }
        p->open.n = ti + 1;
        insert_el(p, t, NS_HTML);
        if (t == A_col) pop(p);
        return true;
    }
    return false;
}
static bool table_end(HtmlParser *p, const char *t) {
    if (t == A_td || t == A_th || t == A_tr || t == A_tbody || t == A_thead || t == A_tfoot || t == A_caption || t == A_colgroup) {
        for (int i = p->open.n - 1; i >= 0; i--) {
            Node *n = p->open.v[i];
            if (n->tag == t) { p->open.n = i; if (t == A_td || t == A_th || t == A_caption) { while (p->fmt.n) { Node *f = vec_pop(p->fmt); if (!f) break; } } return true; }
            if (n->tag == A_table) return true;
        }
        return true;
    }
    return false;
}

static bool in_foreign(HtmlParser *p) { Node *c = cur(p); return c && c->type == NODE_ELEMENT && c->ns != NS_HTML && c->tag != atom("foreignObject") && c->tag != atom("desc") && !(c->ns == NS_SVG && c->tag == A_title); }

static void tree_token(HtmlParser *p) {
    int tt = p->tt;
    if (tt == T_DOCTYPE) {
        if (p->mode == M_INITIAL) {
            char *name = p->tag.s ? p->tag.s : "html";
            node_append(&p->doc->node, node_new_doctype(p->doc, name));
            if (!str_ieq(name, "html")) p->doc->quirks = true;
            p->mode = M_BEFORE_HTML;
        }
        return;
    }
    if (tt == T_COMMENT) {
        Node *par = (p->mode == M_INITIAL || p->mode == M_BEFORE_HTML) && !p->frag_root ? &p->doc->node : insertion_parent(p);
        node_append(par, node_new_comment(p->doc, p->text.s ? p->text.s : "", p->text.n));
        return;
    }
    if (tt == T_START) {
        const char *t = atom_lower(p->tag.s, p->tag.n);
        if (in_foreign(p)) {
            static const char *const breakout[] = { "b","big","blockquote","body","br","center","code","dd","div","dl","dt","em","embed","h1","h2","h3","h4","h5","h6","head","hr","i","img","li","listing","menu","meta","nobr","ol","p","pre","ruby","s","small","span","strong","strike","sub","sup","table","tt","u","ul","var", NULL };
            if (in_list(t, breakout) || (t == A_font && false)) {
                while (p->open.n && in_foreign(p)) pop(p);
            } else {
                int ns = cur(p)->ns;
                const char *rt = ns == NS_SVG ? atom(svg_fix_tag(t)) : t;
                if (ns == NS_SVG) for (int i = 0; i < p->attrs.n; i++) p->attrs.v[i].name = svg_fix_attr(p->attrs.v[i].name);
                insert_el(p, rt, ns);
                if (p->self_closing) pop(p);
                return;
            }
        }
        if (t == A_svg || t == A_math) for (int i = 0; i < p->attrs.n; i++) p->attrs.v[i].name = svg_fix_attr(p->attrs.v[i].name);
        switch (p->mode) {
        case M_INITIAL: p->doc->quirks = !p->frag_root; /* fallthrough */
        case M_BEFORE_HTML:
            if (t == A_html) { p->doc->html = insert_el(p, "html", NS_HTML); p->mode = M_BEFORE_HEAD; return; }
            ensure_head(p); p->mode = M_IN_HEAD; /* fallthrough */
        case M_BEFORE_HEAD:
            if (p->mode == M_BEFORE_HEAD) {
                if (t == A_html) { handle_start_body(p, t); return; }
                if (t == A_head) { p->head_el = insert_el(p, "head", NS_HTML); p->doc->head = p->head_el; p->mode = M_IN_HEAD; return; }
                ensure_head(p);
            }
            /* fallthrough */
        case M_IN_HEAD:
            if (in_list(t, head_tags) && t != A_noscript) {
                Node *e = insert_el(p, t, NS_HTML);
                if (t == A_title) { p->state = S_RCDATA; p->raw_end_tag = t; p->orig_mode = M_IN_HEAD; p->mode = M_TEXT; }
                else if (t == A_style || t == atom("noframes")) { p->state = S_RAWTEXT; p->raw_end_tag = t; p->orig_mode = M_IN_HEAD; p->mode = M_TEXT; }
                else if (t == A_script) { p->state = S_SCRIPT; p->raw_end_tag = t; p->orig_mode = M_IN_HEAD; p->mode = M_TEXT; }
                else if (t == A_template) { vec_push(p->fmt, (Node *)NULL); }
                else pop(p);
                (void)e;
                return;
            }
            if (t == A_noscript && p->mode == M_IN_HEAD) {
                /* scripting enabled: treat as raw text */
                insert_el(p, t, NS_HTML); p->state = S_RAWTEXT; p->raw_end_tag = t; p->orig_mode = M_IN_HEAD; p->mode = M_TEXT; return;
            }
            if (t == A_head) return;
            if (p->mode == M_IN_HEAD) { while (p->open.n && cur(p) != p->doc->html) pop(p); p->mode = M_AFTER_HEAD; }
            /* fallthrough */
        case M_AFTER_HEAD:
            if (p->mode == M_AFTER_HEAD) {
                if (t == A_body) { p->doc->body = insert_el(p, "body", NS_HTML); p->mode = M_IN_BODY; p->frameset_ok = false; return; }
                if (t == A_frameset) { insert_el(p, t, NS_HTML); p->mode = M_IN_FRAMESET; return; }
                if (in_list(t, head_tags)) {
                    /* insert into head */
                    if (p->head_el) { vec_push(p->open, p->head_el); int m = p->mode; p->mode = M_IN_HEAD; tree_token(p); if (p->mode == M_IN_HEAD) { p->mode = m; for (int i = p->open.n - 1; i >= 0; i--) if (p->open.v[i] == p->head_el) { memmove(&p->open.v[i], &p->open.v[i + 1], sizeof(Node *) * (size_t)(p->open.n - i - 1)); p->open.n--; break; } } else if (p->mode == M_TEXT) p->orig_mode = M_AFTER_HEAD; return; }
                }
                ensure_body(p);
            }
            /* fallthrough */
        case M_IN_BODY: case M_IN_TABLE: case M_IN_SELECT:
            if (p->mode == M_IN_SELECT || (cur(p)->tag == A_select || cur(p)->tag == A_option || cur(p)->tag == A_optgroup)) {
                bool insel = false; for (int i = p->open.n - 1; i >= 0; i--) { if (p->open.v[i]->tag == A_select) { insel = true; break; } }
                if (insel) {
                    if (t == A_option) { if (cur(p)->tag == A_option) pop(p); insert_el(p, t, NS_HTML); return; }
                    if (t == A_optgroup) { if (cur(p)->tag == A_option) pop(p); if (cur(p)->tag == A_optgroup) pop(p); insert_el(p, t, NS_HTML); return; }
                    if (t == A_select || t == A_input || t == A_textarea) { pop_until(p, A_select); if (t == A_select) return; }
                    else if (t == A_hr) { if (cur(p)->tag == A_option) pop(p); if (cur(p)->tag == A_optgroup) pop(p); insert_el(p, t, NS_HTML); pop(p); return; }
                }
            }
            if (in_list(t, head_tags) && t != A_noscript) {
                Node *e = insert_el(p, t, NS_HTML);
                if (t == A_title) { p->state = S_RCDATA; p->raw_end_tag = t; p->orig_mode = p->mode; p->mode = M_TEXT; }
                else if (t == A_style || t == atom("noframes")) { p->state = S_RAWTEXT; p->raw_end_tag = t; p->orig_mode = p->mode; p->mode = M_TEXT; }
                else if (t == A_script) { p->state = S_SCRIPT; p->raw_end_tag = t; p->orig_mode = p->mode; p->mode = M_TEXT; }
                else if (t == A_template) { vec_push(p->fmt, (Node *)NULL); }
                else pop(p);
                (void)e; return;
            }
            if (table_start(p, t)) return;
            handle_start_body(p, t);
            return;
        case M_IN_FRAMESET: if (t == A_frame) { insert_el(p, t, NS_HTML); pop(p); } else if (t == A_frameset) insert_el(p, t, NS_HTML); return;
        case M_AFTER_BODY: p->mode = M_IN_BODY; tree_token(p); return;
        default: return;
        }
    }
    if (tt == T_END) {
        const char *t = atom_lower(p->tag.s, p->tag.n);
        if (p->mode == M_TEXT) {
            Node *c = cur(p); pop(p); p->mode = p->orig_mode;
            if (c && c->tag == A_script && c->ns == NS_HTML && html_script_hook && !p->frag_root) html_script_hook(p->doc, c, html_script_hook_ud);
            if (c && c->tag == A_title && !p->doc->title) { p->doc->title = node_text_content(c); }
            return;
        }
        if (in_foreign(p) || (p->open.n && cur(p)->ns != NS_HTML)) {
            for (int i = p->open.n - 1; i >= 0; i--) {
                Node *n = p->open.v[i];
                if (n->ns == NS_HTML) break;
                if (str_ieq(n->tag, t)) { p->open.n = i; return; }
            }
        }
        switch (p->mode) {
        case M_INITIAL: case M_BEFORE_HTML: case M_BEFORE_HEAD:
            if (t != A_head && t != A_body && t != A_html && t != A_br) return;
            ensure_head(p); /* fallthrough */
        case M_IN_HEAD:
            if (t == A_head) { pop(p); p->mode = M_AFTER_HEAD; return; }
            if (t == A_template) { handle_end_body(p, t); return; }
            if (t != A_body && t != A_html && t != A_br) return;
            while (p->open.n && cur(p) != p->doc->html) pop(p); p->mode = M_AFTER_HEAD; /* fallthrough */
        case M_AFTER_HEAD:
            if (t == A_template) { handle_end_body(p, t); return; }
            if (t != A_body && t != A_html && t != A_br) return;
            ensure_body(p); /* fallthrough */
        case M_IN_BODY: case M_IN_TABLE: case M_IN_SELECT:
            if (t == A_select) { pop_until(p, A_select); return; }
            if (t == A_option) { if (cur(p)->tag == A_option) pop(p); return; }
            if (t == A_optgroup) { if (cur(p)->tag == A_option) pop(p); if (cur(p)->tag == A_optgroup) pop(p); return; }
            if (table_end(p, t)) return;
            handle_end_body(p, t);
            return;
        case M_AFTER_BODY: return;
        case M_IN_FRAMESET: if (t == A_frameset) pop(p); return;
        default: return;
        }
    }
    if (tt == T_EOF) {
        while (p->open.n) pop(p);
        return;
    }
}

static void emit_chars(HtmlParser *p, const char *s, size_t n) {
    if (!n) return;
    if (p->ignore_lf) { p->ignore_lf = false; if (s[0] == '\n') { s++; n--; if (!n) return; } }
    if (p->mode == M_TEXT) { insert_text(p, s, n); return; }
    if (p->mode == M_INITIAL || p->mode == M_BEFORE_HTML || p->mode == M_BEFORE_HEAD || p->mode == M_IN_HEAD || p->mode == M_AFTER_HEAD) {
        size_t i = 0; while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i && (p->mode == M_IN_HEAD || p->mode == M_AFTER_HEAD)) insert_text(p, s, i);
        if (i == n) return;
        s += i; n -= i;
        if (p->mode == M_INITIAL && !p->frag_root) p->doc->quirks = true;
        ensure_body(p);
    }
    if (p->mode == M_IN_FRAMESET) return;
    if (p->mode == M_AFTER_BODY) { if (is_ws_str(s, n)) { insert_text(p, s, n); return; } p->mode = M_IN_BODY; }
    if (!is_ws_str(s, n)) { reconstruct_fmt(p); p->frameset_ok = false; }
    insert_text(p, s, n);
}
static void flush_chars(HtmlParser *p) { if (p->chars.n) { emit_chars(p, p->chars.s, p->chars.n); sb_clear(&p->chars); } }

static void emit(HtmlParser *p) {
    if (p->tt != T_CHARS) flush_chars(p);
    tree_token(p);
    if (p->tt == T_START && p->ignore_lf == false) {}
    for (int i = 0; i < p->attrs.n; i++) free(p->attrs.v[i].value);
    p->attrs.n = 0; sb_clear(&p->tag); sb_clear(&p->text); p->self_closing = false; p->tt = T_NONE;
}

/* ---------------- tokenizer ---------------- */
static void charref(HtmlParser *p, const char *s, size_t n, size_t *i, SB *out, bool in_attr) {
    /* s[*i] == '&' */
    size_t j = *i + 1;
    if (j < n && s[j] == '#') {
        j++; uint32_t cp = 0; bool hex = false; size_t st;
        if (j < n && (s[j] == 'x' || s[j] == 'X')) { hex = true; j++; }
        st = j;
        while (j < n && (hex ? isxdigit((unsigned char)s[j]) : isdigit((unsigned char)s[j]))) { int d = isdigit((unsigned char)s[j]) ? s[j] - '0' : lc((unsigned char)s[j]) - 'a' + 10; if (cp < 0x110000) cp = cp * (hex ? 16 : 10) + (uint32_t)d; j++; }
        if (j == st) { sb_putc(out, '&'); *i += 1; return; }
        if (j < n && s[j] == ';') j++;
        static const uint16_t win1252[32] = { 0x20AC,0x81,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0x8D,0x017D,0x8F,0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0x9D,0x017E,0x0178 };
        if (cp >= 0x80 && cp <= 0x9F) cp = win1252[cp - 0x80];
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        sb_utf8(out, cp); *i = j; return;
    }
    size_t used; uint32_t second = 0;
    uint32_t cp = html_entity_lookup(s + j, n - j, &second, &used);
    if (!cp) { sb_putc(out, '&'); *i += 1; return; }
    bool semi = s[j + used - 1] == ';';
    if (in_attr && !semi && j + used < n && (isalnum((unsigned char)s[j + used]) || s[j + used] == '=')) { sb_putc(out, '&'); *i += 1; return; }
    sb_utf8(out, cp); if (second) sb_utf8(out, second);
    *i = j + used;
}

/* Tokenize as much as possible from p->in. Returns false if more input is needed. */
static bool is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static void run(HtmlParser *p, bool final) {
    const char *s = p->in.s; size_t n = p->in.n;
    size_t i = p->pos;
    while (i < n) {
        if (p->state == S_PLAINTEXT) { sb_put(&p->chars, s + i, n - i); i = n; break; }
        if (p->state == S_RAWTEXT || p->state == S_RCDATA || p->state == S_SCRIPT) {
            /* scan for </raw_end_tag */
            size_t L = strlen(p->raw_end_tag);
            size_t k = i; bool found = false;
            bool in_comment = false;
            while (k < n) {
                if (p->state == S_SCRIPT && !in_comment && k + 4 <= n && !memcmp(s + k, "<!--", 4)) { in_comment = true; }
                else if (p->state == S_SCRIPT && in_comment && k + 3 <= n && !memcmp(s + k, "-->", 3)) { in_comment = false; }
                if (s[k] == '<' && k + 1 < n && s[k + 1] == '/') {
                    if (k + 2 + L > n) break;
                    if (str_ieqn(s + k + 2, p->raw_end_tag, L)) {
                        char e = k + 2 + L < n ? s[k + 2 + L] : 0;
                        if (k + 2 + L >= n && !final) break;
                        if (is_ws((unsigned char)e) || e == '/' || e == '>' || e == 0) { found = true; break; }
                    }
                }
                k++;
            }
            if (!found && !final) {
                /* emit safe portion, keep potential partial end tag */
                size_t safe = k; if (safe > i) {
                    if (p->state == S_RCDATA) { SB t; sb_init(&t); for (size_t q = i; q < safe;) { if (s[q] == '&') charref(p, s, safe, &q, &t, false); else sb_putc(&t, s[q++]); } sb_put(&p->chars, t.s ? t.s : "", t.n); sb_free(&t); }
                    else sb_put(&p->chars, s + i, safe - i);
                    i = safe;
                }
                break;
            }
            size_t end = found ? k : n;
            if (p->state == S_RCDATA) { SB t; sb_init(&t); for (size_t q = i; q < end;) { if (s[q] == '&') charref(p, s, end, &q, &t, false); else sb_putc(&t, s[q++]); } sb_put(&p->chars, t.s ? t.s : "", t.n); sb_free(&t); }
            else sb_put(&p->chars, s + i, end - i);
            flush_chars(p);
            i = end;
            if (found) {
                size_t q = k + 2 + L; while (q < n && s[q] != '>') q++;
                if (q >= n && !final) { break; }
                p->tt = T_END; sb_put(&p->tag, p->raw_end_tag, L);
                p->state = S_DATA;
                i = q < n ? q + 1 : n;
                emit(p);
            } else { p->state = S_DATA; }
            continue;
        }
        char c = s[i];
        if (c == '&') {
            size_t save = i;
            if (!final && n - i < 34 && !memchr(s + i, ';', n - i)) { /* might be partial */
                bool has_term = false; for (size_t q = i + 1; q < n; q++) if (!isalnum((unsigned char)s[q]) && s[q] != '#') { has_term = true; break; }
                if (!has_term) break;
            }
            charref(p, s, n, &i, &p->chars, false); (void)save; continue;
        }
        if (c != '<') {
            size_t k = i; while (k < n && s[k] != '<' && s[k] != '&') k++;
            sb_put(&p->chars, s + i, k - i);
            if (p->chars.n) {
                /* replace NUL */
                for (size_t q = p->chars.n - (k - i); q < p->chars.n; q++) if (!p->chars.s[q]) p->chars.s[q] = ' ';
            }
            i = k; continue;
        }
        /* tag-ish */
        if (i + 1 >= n) { if (final) { sb_putc(&p->chars, '<'); i++; } break; }
        char d = s[i + 1];
        if (d == '!') {
            if (i + 4 <= n && !memcmp(s + i, "<!--", 4)) {
                const char *e = NULL;
                for (size_t q = i + 4; q + 3 <= n; q++) if (s[q] == '-' && s[q + 1] == '-' && s[q + 2] == '>') { e = s + q; break; }
                if (i + 5 <= n && s[i + 4] == '>') e = s + i + 2; /* <!--> */
                if (!e && i + 6 <= n && !memcmp(s + i + 4, "->", 2)) e = s + i + 3;
                if (!e) { if (!final) break; e = s + n; }
                p->tt = T_COMMENT;
                if (e > s + i + 4) sb_put(&p->text, s + i + 4, (size_t)(e - (s + i + 4)));
                emit(p);
                i = (size_t)(e - s) + 3; if (i > n) i = n; continue;
            }
            if (i + 9 <= n && str_ieqn(s + i + 2, "doctype", 7)) {
                const char *e = memchr(s + i, '>', n - i);
                if (!e) { if (!final) break; e = s + n - 1; }
                size_t q = i + 9; while (q < (size_t)(e - s) && is_ws((unsigned char)s[q])) q++;
                size_t st = q; while (q < (size_t)(e - s) && !is_ws((unsigned char)s[q])) q++;
                p->tt = T_DOCTYPE; for (size_t z = st; z < q; z++) sb_putc(&p->tag, (char)lc((unsigned char)s[z]));
                emit(p); i = (size_t)(e - s) + 1; continue;
            }
            if (i + 9 <= n && !memcmp(s + i + 2, "[CDATA[", 7) && in_foreign(p)) {
                const char *e = NULL; for (size_t q = i + 9; q + 3 <= n; q++) if (!memcmp(s + q, "]]>", 3)) { e = s + q; break; }
                if (!e) { if (!final) break; e = s + n; }
                sb_put(&p->chars, s + i + 9, (size_t)(e - (s + i + 9))); i = (size_t)(e - s) + 3; if (i > n) i = n; continue;
            }
            /* bogus comment */
            const char *e = memchr(s + i, '>', n - i);
            if (!e) { if (!final) break; e = s + n - 1; }
            p->tt = T_COMMENT; sb_put(&p->text, s + i + 2, (size_t)(e - (s + i + 2))); emit(p); i = (size_t)(e - s) + 1; continue;
        }
        if (d == '?') { const char *e = memchr(s + i, '>', n - i); if (!e) { if (!final) break; e = s + n - 1; } p->tt = T_COMMENT; sb_put(&p->text, s + i + 1, (size_t)(e - (s + i + 1))); emit(p); i = (size_t)(e - s) + 1; continue; }
        bool end = d == '/';
        size_t q = i + (end ? 2 : 1);
        if (q >= n) { if (!final) break; sb_put(&p->chars, s + i, n - i); i = n; continue; }
        if (!is_alpha((unsigned char)s[q])) {
            if (end && s[q] == '>') { i = q + 1; continue; }
            if (end) { const char *e = memchr(s + q, '>', n - q); if (!e) { if (!final) break; e = s + n - 1; } p->tt = T_COMMENT; sb_put(&p->text, s + q, (size_t)(e - (s + q))); emit(p); i = (size_t)(e - s) + 1; continue; }
            sb_putc(&p->chars, '<'); i++; continue;
        }
        /* find the end of the tag, respecting quotes */
        size_t k = q; char quote = 0; bool complete = false;
        while (k < n) {
            char ch = s[k];
            if (quote) { if (ch == quote) quote = 0; }
            else if (ch == '>') { complete = true; break; }
            else if ((ch == '"' || ch == '\'') && k > q && (s[k - 1] == '=' || is_ws((unsigned char)s[k - 1]))) {
                /* quotes only start after '=' (possibly with ws) */
                size_t b = k - 1; while (b > q && is_ws((unsigned char)s[b])) b--;
                if (s[b] == '=') quote = ch;
            }
            k++;
        }
        if (!complete && !final) break;
        size_t tend = complete ? k : n;
        p->tt = end ? T_END : T_START;
        size_t a = q; while (a < tend && !is_ws((unsigned char)s[a]) && s[a] != '/' && s[a] != '>') a++;
        sb_put(&p->tag, s + q, a - q);
        for (size_t z = 0; z < p->tag.n; z++) if (!p->tag.s[z]) p->tag.s[z] = '?';
        /* attributes */
        while (a < tend) {
            while (a < tend && (is_ws((unsigned char)s[a]) || s[a] == '/')) { if (s[a] == '/' && a + 1 == tend) p->self_closing = true; a++; }
            if (a >= tend) break;
            size_t ns = a; a++;
            while (a < tend && !is_ws((unsigned char)s[a]) && s[a] != '/' && s[a] != '=' && s[a] != '>') a++;
            size_t ne = a;
            while (a < tend && is_ws((unsigned char)s[a])) a++;
            SB val; sb_init(&val);
            if (a < tend && s[a] == '=') {
                a++; while (a < tend && is_ws((unsigned char)s[a])) a++;
                if (a < tend && (s[a] == '"' || s[a] == '\'')) {
                    char qc = s[a++]; size_t vs = a; while (a < tend && s[a] != qc) a++;
                    for (size_t z = vs; z < a;) { if (s[z] == '&') charref(p, s, a, &z, &val, true); else sb_putc(&val, s[z++]); }
                    if (a < tend) a++;
                } else {
                    size_t vs = a; while (a < tend && !is_ws((unsigned char)s[a]) && s[a] != '>') a++;
                    for (size_t z = vs; z < a;) { if (s[z] == '&') charref(p, s, a, &z, &val, true); else sb_putc(&val, s[z++]); }
                }
            }
            if (!end) {
                const char *an = atom_lower(s + ns, ne - ns);
                bool dup = false; for (int z = 0; z < p->attrs.n; z++) if (p->attrs.v[z].name == an) dup = true;
                if (!dup) { TAttr ta = { an, sb_take(&val) }; vec_push(p->attrs, ta); } else sb_free(&val);
            } else sb_free(&val);
        }
        i = complete ? k + 1 : n;
        emit(p);
    }
    p->pos = i;
    /* compact consumed input */
    if (p->pos > 65536 || p->pos == p->in.n) { memmove(p->in.s, p->in.s + p->pos, p->in.n - p->pos); p->in.n -= p->pos; p->pos = 0; if (p->in.s) p->in.s[p->in.n] = 0; }
    if (final) flush_chars(p);
}

void html_parser_feed(HtmlParser *p, const char *src, size_t len) {
    sb_put(&p->in, src, len);
    run(p, false);
    flush_chars(p);
}
void html_parser_finish(HtmlParser *p) {
    run(p, true);
    flush_chars(p);
    if (!p->frag_root) { ensure_body(p); }
    p->tt = T_EOF; tree_token(p);
}

void html_parse(Document *d, const char *src, size_t len) {
    HtmlParser *p = html_parser_new(d, NULL);
    /* skip BOM */
    if (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) { src += 3; len -= 3; }
    html_parser_feed(p, src, len);
    html_parser_finish(p);
    html_parser_free(p);
}

Node *html_parse_fragment(Document *d, Node *ctx, const char *src, size_t len) {
    HtmlParser *p = html_parser_new(d, ctx);
    html_parser_feed(p, src, len);
    html_parser_finish(p);
    Node *root = p->frag_root;
    Node *h = root->first;
    Node *frag = node_new_fragment(d);
    if (h) while (h->first) node_append(frag, h->first);
    html_parser_free(p);
    node_free_tree(root);
    return frag;
}
