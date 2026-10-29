/* A uniform scale transform scales the subtree's text with its boxes and icons: x.com's Google
   button (scale(1.15) from the left, a 1em svg then the label after an 8px gap) drew the scaled icon
   over the unscaled label. A non-uniform scale (progress bars' scaleX) leaves text alone. */
#include "../src/paint/paint.h"
#include "../src/text/font.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static const DItem *first(const DisplayList *dl, int op) {
    for (int i = 0; i < dl->items.n; i++) if (dl->items.v[i].op == op) return &dl->items.v[i];
    return NULL;
}

int main(void) {
    dom_init(); font_init();
    Document *d = doc_new("https://scale-text.test/");
    const char *html = "<html><body><div id=b><span id=s><i id=ic></i>Go</span></div>"
        "<div id=x><span>Go</span></div></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0;font:20px sans-serif}#b{width:200px;height:40px;display:flex;align-items:center;transform:scale(2);transform-origin:0 0}"
        "#s{display:flex;gap:8px;align-items:center}#ic{display:block;width:20px;height:20px;background:red}"
        "#x{margin-top:100px;transform:scaleX(.5);transform-origin:0 0}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 800, 600);
    DisplayList dl = {0}; dl_build(&dl, l, 0, 0, 800, 600);
    const DItem *icon = first(&dl, DO_RECT), *t = first(&dl, DO_TEXT);
    assert(icon && t && t->ng >= 2);
    /* icon 0..20 scales to 0..40; the label at 28 (20 + 8 gap) scales to 56, at twice the size */
    assert(fabsf(icon->x) < .5f && fabsf(icon->w - 40) < .5f);
    assert(fabsf(t->g[0].x - 56) < 1.f && t->g[0].font && fabsf(t->g[0].font->size - 40) < .01f);
    assert(t->g[1].x - t->g[0].x > 10);   /* advances doubled too */
    const DItem *t2 = NULL; for (int i = 0; i < dl.items.n; i++) if (dl.items.v[i].op == DO_TEXT && &dl.items.v[i] != t) t2 = &dl.items.v[i];
    assert(t2 && t2->g[0].font && fabsf(t2->g[0].font->size - 20) < .01f);
    dl_clear(&dl); free(dl.items.v); layout_free(l); style_engine_free(e); doc_free(d);
    puts("Uniform scale transforms scale text with the subtree passed");
}
