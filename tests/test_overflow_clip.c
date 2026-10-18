#include "../src/layout/layout.h"
#include "../src/js/jsglue.h"
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); failures++; } } while (0)

int main(void) {
    dom_init(); font_init();
    const char *html = "<html><body style='margin:0'>"
        "<div id='clip' style='width:100px;height:100px;overflow:clip'>"
        "<div id='child' style='width:400px;height:400px'></div></div>"
        "<div id='hidden' style='width:100px;height:100px;overflow:hidden'>"
        "<div style='width:400px;height:400px'></div></div>"
        "<div id='auto' style='width:100px;height:100px;overflow:auto'>"
        "<div style='width:400px;height:400px'></div></div>"
        "<div id='reverse' style='display:flex;flex-direction:column-reverse;"
        "width:100px;height:100px;overflow:clip'>"
        "<div id='reverse-child' style='flex:none;height:300px'></div></div>"
        "</body></html>";
    Document *d = doc_new("https://layout.example/");
    html_parse(d, html, strlen(html));
    StyleEngine *se = style_engine_new(d);
    se->media = (MediaCtx){430, 888, 1, false};
    style_recalc(se, &d->node, true);
    Layout *L = layout_new(); layout_run(L, d, 430, 888);
    Node *clip = doc_get_element_by_id(d, "clip"), *child = doc_get_element_by_id(d, "child");
    Node *hidden = doc_get_element_by_id(d, "hidden"), *auto_box = doc_get_element_by_id(d, "auto");
    Node *reverse = doc_get_element_by_id(d, "reverse");
    CHECK(clip && clip->box && clip->box->scroller); /* Painting must still clip. */
    CHECK(!clip->box->bfc); /* Clipping alone does not establish a formatting context. */
    CHECK(!jsg_set_scroll(clip, 80, 90));
    CHECK(clip->scroll_x == 0 && clip->scroll_y == 0);
    float r[4]; CHECK(jsg_vrect(child, r)); CHECK(r[0] == 0 && r[1] == 0);
    CHECK(jsg_set_scroll(hidden, 80, 90));
    CHECK(hidden->scroll_x == 80 && hidden->scroll_y == 90);
    CHECK(jsg_set_scroll(auto_box, 60, 70));
    CHECK(auto_box->scroll_x == 60 && auto_box->scroll_y == 70);
    CHECK(!jsg_set_scroll(reverse, NAN, -80));
    CHECK(reverse->scroll_y == 0 && box_scroll_y(reverse->box) == 0);

    /* A style change must discard an old scroll offset, not shift a clipped page. */
    node_set_attr(hidden, "style", "width:100px;height:100px;overflow:clip");
    style_recalc(se, &d->node, true); layout_run(L, d, 430, 888);
    CHECK(hidden->scroll_x == 0 && hidden->scroll_y == 0);
    CHECK(!jsg_set_scroll(hidden, 60, 70));
    layout_free(L); style_engine_free(se); doc_free(d);
    if (failures) { fprintf(stderr, "%d overflow clip failures\n", failures); return 1; }
    puts("overflow clip tests ok"); return 0;
}
