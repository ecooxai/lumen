/* Off-screen subtree culling in dl_build paints exactly what the full walk paints in the viewport */
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>

typedef VEC(DItem) Items;
static void seen(const DisplayList *dl, float vh, Items *out) {
    out->n = 0;
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_CLIP || it->op == DO_POP_CLIP || it->op == DO_PUSH_LAYER || it->op == DO_POP_LAYER) continue;
        if (it->y + it->h <= 0 || it->y >= vh) continue;
        vec_push(*out, *it);
    }
}
static int compare(Layout *l, float sy, const char *what) {
    Items a = {0}, b = {0};
    DisplayList full = {0}, cut = {0};
    dl_cull = false; dl_build(&full, l, 0, sy, l->vw, l->vh);
    dl_cull = true; dl_build(&cut, l, 0, sy, l->vw, l->vh);
    dl_build(&cut, l, 0, sy, l->vw, l->vh);   /* again, from the kept extents */
    seen(&full, l->vh, &a); seen(&cut, l->vh, &b);
    int bad = a.n != b.n;
    for (int i = 0; !bad && i < a.n; i++) {
        const DItem *x = &a.v[i], *y = &b.v[i];
        bad = x->op != y->op || x->x != y->x || x->y != y->y || x->w != y->w || x->h != y->h || x->color != y->color || x->ng != y->ng;
        for (int g = 0; !bad && g < x->ng; g++) bad = x->g[g].gid != y->g[g].gid || x->g[g].x != y->g[g].x || x->g[g].y != y->g[g].y;
    }
    if (bad) fprintf(stderr, "%s sy=%.0f: full walk %d items in view, culled %d (of %d / %d)\n", what, sy, a.n, b.n, full.items.n, cut.items.n);
    else fprintf(stderr, "%s sy=%.0f: %d items in view; built %d full, %d culled\n", what, sy, a.n, full.items.n, cut.items.n);
    dl_clear(&full); dl_clear(&cut); vec_free(full.items); vec_free(cut.items); vec_free(a); vec_free(b);
    return bad;
}
int main(void) {
    dom_init();
    Document *d = doc_new("https://cull.test/");
    char *html = malloc(400000); size_t n = 0;
    n += (size_t)sprintf(html + n, "<html><body>"
        "<div id='far' style='margin-top:6000px'><div class='abs'>abs from far</div><div class='tr'>moved up</div></div>"
        "<div class='short'><p>text that overflows a short box downwards, line one, line two, line three, line four, line five</p></div>"
        "<div id='sc' class='sc'>");
    for (int i = 0; i < 60; i++) n += (size_t)sprintf(html + n, "<p>scroller row %d</p>", i);
    n += (size_t)sprintf(html + n, "</div><ul>");
    for (int i = 0; i < 300; i++)
        n += (size_t)sprintf(html + n, "<li class='m'><div class='fl'>f%d</div><span class='ib'>ib %d</span> message %d with <b>bold</b> and <a href='#'>a link</a>"
                             " and enough words to wrap over a few lines in the column</li>%s", i, i, i, i % 50 == 7 ? "<div class='neg'>pulled up</div>" : "");
    n += (size_t)sprintf(html + n, "</ul><div class='rel'>relative tail</div></body></html>");
    html_parse(d, html, n); free(html);
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0;font:16px sans-serif}"
        ".abs{position:absolute;top:30px;left:20px;background:#123456}"
        ".tr{transform:translateY(-5900px);background:#345678;opacity:.9}"
        ".short{height:12px;background:#eee}"
        ".sc{height:200px;overflow:auto;background:#fafafa;border:2px solid #888}"
        ".m{box-shadow:0 0 30px #000;margin:6px 0;border-bottom:1px solid #ccc}"
        ".fl{float:left;width:30px;height:30px;background:#c00}"
        ".ib{display:inline-block;padding:4px;background:#0c0;outline:3px solid #00c}"
        ".neg{margin-top:-400px;height:20px;background:#990}"
        ".rel{position:relative;top:-3000px;background:#099}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 540, 800);
    jsg_set_scroll(doc_get_element_by_id(d, "sc"), 0, 300);
    int bad = 0;
    float ys[] = { 0, 150, 1000, 2600, 4000, 5900, 6100, l->doc_h - 800 };
    for (size_t i = 0; i < ARRLEN(ys); i++) bad += compare(l, ys[i], "page");
    /* a restyle invalidates the kept extents */
    node_set_attr(doc_get_element_by_id(d, "far"), "style", "margin-top:6000px;position:relative;top:-6000px");
    style_recalc(e, &d->node, false); layout_run(l, d, 540, 800);
    bad += compare(l, 0, "moved");
    assert(!bad);
    return 0;
}
