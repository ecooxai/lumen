/* Shaped text is reused across layout passes and released with its boxes. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static Box *text_box(Box *b) {
    for (; b; b = b->next) {
        if (b->kind == BX_TEXT && b->shaped) return b;
        Box *t = text_box(b->first);
        if (t) return t;
    }
    return NULL;
}

static void relayout(StyleEngine *e, Document *d, Layout *l) {
    style_recalc(e, &d->node, false);
    layout_run(l, d, 400, 300);
}

int main(void) {
    dom_init(); font_init();
    Document *d = doc_new("https://shape-cache.test/");
    const char *html = "<html><body><p id='a'>Shaping cache keeps glyph runs</p><p id='b'>Shaping cache keeps glyph runs</p></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0;font:16px sans-serif}#b{letter-spacing:2px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 400, 300);
    Node *a = doc_get_element_by_id(d, "a"), *b = doc_get_element_by_id(d, "b");
    Box *ta = text_box(a->box->first), *tb = text_box(b->box->first);
    assert(ta && tb && ta->sh.n > 10 && tb->sh.n == ta->sh.n);
    assert(ta->sh.g != tb->sh.g);                        /* letter-spacing is part of the key */
    assert(tb->sh.width > ta->sh.width + ta->sh.n);
    GlyphPos *glyphs = ta->sh.g; int count = ta->sh.n; float width = ta->sh.width, height = a->box->h;
    GlyphPos copy[64]; assert(count <= 64); memcpy(copy, glyphs, sizeof copy[0] * (size_t)count);

    /* an unrelated layout mutation rebuilds the tree but reuses the shaped run */
    node_set_attr(b, "style", "margin-top:7px");
    relayout(e, d, l);
    ta = text_box(a->box->first);
    assert(ta->sh.g == glyphs && ta->sh.n == count && ta->sh.width == width && a->box->h == height);
    assert(!memcmp(ta->sh.g, copy, sizeof copy[0] * (size_t)count));

    /* changing the text shapes the new string */
    node_set_character_data(a->first, "Different words", 15);
    relayout(e, d, l);
    ta = text_box(a->box->first);
    assert(ta->sh.g != glyphs && ta->sh.width < width);

    /* changing the font shapes again; restoring it may share the cached run */
    node_set_attr(a, "style", "font-size:24px");
    relayout(e, d, l);
    float big = text_box(a->box->first)->sh.width;
    node_remove_attr(a, "style");
    relayout(e, d, l);
    assert(text_box(a->box->first)->sh.width < big);

    /* many passes with distinct texts stay correct while unused runs are swept */
    for (int i = 0; i < 200; i++) {
        char text[64]; int n = snprintf(text, sizeof text, "pass %d of the sweep", i);
        node_set_character_data(a->first, text, (size_t)n);
        relayout(e, d, l);
        ta = text_box(a->box->first);
        assert(ta->sh.n >= n - 4 && ta->sh.width > 0);
    }
    layout_free(l); style_engine_free(e); doc_free(d);
    text_shape_cache_sweep(); text_shape_cache_sweep();
    puts("shape cache: ok");
    return 0;
}
