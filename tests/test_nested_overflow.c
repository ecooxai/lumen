#include "../src/layout/layout.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>

int main(void) {
    dom_init();
    Document *d = doc_new("https://overflow.test/");
    const char *html = "<html><body><div id='scroll'><div id='wrapper'><div id='content'></div></div></div></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0}#scroll{height:200px;width:300px;overflow:auto;display:flex;flex-direction:column}"
        "#wrapper{height:100px;min-height:0;flex:1}#content{height:1200px;width:600px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 540, 800);
    Node *scroll = doc_get_element_by_id(d, "scroll"), *wrapper = doc_get_element_by_id(d, "wrapper"), *content = doc_get_element_by_id(d, "content");
    assert(fabsf(scroll->box->h - 200) < .1f);
    assert(scroll->box->scroll_h >= 1200 && scroll->box->scroll_w >= 600);
    uint64_t dom = d->dom_version, sheets = d->sheet_version, layout = d->layout_version;
    uint64_t paint = d->paint_version, visual = doc_visual_generation();
    uint32_t flags = scroll->flags;
    assert(jsg_set_scroll(scroll, 0, 900));
    assert(d->dom_version == dom && d->sheet_version == sheets && d->layout_version == layout);
    assert(d->paint_version == paint + 1 && doc_visual_generation() == visual + 1);
    assert(scroll->flags == flags);
    float rect[4]; assert(jsg_vrect(content, rect, 0, 0));
    assert(fabsf(rect[1] - (content->box->y - 900)) < .1f);
    assert(!jsg_set_scroll(scroll, NAN, 900));
    assert(d->paint_version == paint + 1);
    layout_run(l, d, 540, 400);
    assert(scroll->scroll_y == 900);
    node_set_attr(wrapper, "style", "overflow:hidden");
    style_recalc(e, &d->node, false); layout_run(l, d, 540, 400);
    assert(scroll->box->scroll_h == 200 && scroll->scroll_y == 0);
    node_set_attr(wrapper, "style", "overflow:clip");
    style_recalc(e, &d->node, false); layout_run(l, d, 540, 400);
    assert(scroll->box->scroll_h == 200);
    node_remove_attr(wrapper, "style");
    node_set_attr(content, "style", "position:fixed;top:1000px");
    style_recalc(e, &d->node, false); layout_run(l, d, 540, 400);
    assert(scroll->box->scroll_h == 200);
    node_set_attr(content, "style", "height:1600px");
    style_recalc(e, &d->node, false); layout_run(l, d, 540, 400);
    assert(scroll->box->scroll_h >= 1600);
    node_set_attr(wrapper, "style", "padding-bottom:12px;height:100px;flex:none");
    node_set_attr(content, "style", "height:10px;width:10px");
    style_recalc(e, &d->node, false); layout_run(l, d, 540, 400);
    assert(scroll->box->scroll_h == 200); /* no phantom overflow from padding */
    /* a clip axis beside a scrollable axis computes to hidden, so touch cannot pan it */
    node_set_attr(wrapper, "style", "overflow-x:clip;overflow-y:auto");
    node_set_attr(content, "style", "height:10px;width:900px");
    style_recalc(e, &d->node, false); layout_run(l, d, 540, 400);
    assert(wrapper->style->overflow_x == OV_HIDDEN && wrapper->style->overflow_y == OV_AUTO);
    node_set_attr(wrapper, "style", "overflow-y:auto");
    style_recalc(e, &d->node, false);
    assert(wrapper->style->overflow_x == OV_AUTO);
    node_set_attr(wrapper, "style", "overflow:clip");
    style_recalc(e, &d->node, false);
    assert(wrapper->style->overflow_x == OV_CLIP && wrapper->style->overflow_y == OV_CLIP);
    layout_free(l); style_engine_free(e); doc_free(d);
    puts("Nested visible overflow, clipping, resizing and fixed descendants passed");
}
