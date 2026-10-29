/* Independent Chromium150 geometry, measured2026-10-29 before applying the
   flex-line alignment repair. Horizontal writing modes; both axes are checked. */
#include "../src/layout/layout.h"
#include <math.h>
#include <stdio.h>
static unsigned checks, failures;
static void same(const char *id, const char *component, float actual, float expected) {
    ++checks;
    if (!(fabsf(actual - expected) <= .05f)) {
        ++failures; fprintf(stderr, "%s %s: expected %g, got %g\n", id, component, expected, actual);
    }
}
static void size(Document *d, const char *id, float w, float h) {
    Node *n = doc_get_element_by_id(d, id);
    if (!n || !n->box) { checks += 2; failures += 2; fprintf(stderr, "missing box %s\n", id); return; }
    same(id, "width", n->box->w, w); same(id, "height", n->box->h, h);
}
static void geometry(Document *d, const char *parent, const char *id, float x, float y, float w, float h) {
    Node *p = doc_get_element_by_id(d, parent), *n = doc_get_element_by_id(d, id);
    if (!p || !p->box || !n || !n->box) { checks += 4; failures += 4; fprintf(stderr, "missing box %s\n", id); return; }
    same(id, "x", n->box->x - p->box->x, x); same(id, "y", n->box->y - p->box->y, y);
    same(id, "width", n->box->w, w); same(id, "height", n->box->h, h);
}
int main(void) {
    dom_init(); font_init();
    Document *d = doc_new("https://flex-alignment.test/");
    const char *html =
        "<html><body>"
        "<div class=\"flex\" id=\"row-flex-start\" style=\"align-content:flex-start\"><div class=\"item\" id=\"row-flex-start-item0\" style=\"\"></div><div class=\"item\" id=\"row-flex-start-item1\" style=\"\"></div><div class=\"item\" id=\"row-flex-start-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-start\" style=\"align-content:start\"><div class=\"item\" id=\"row-start-item0\" style=\"\"></div><div class=\"item\" id=\"row-start-item1\" style=\"\"></div><div class=\"item\" id=\"row-start-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-flex-end\" style=\"align-content:flex-end\"><div class=\"item\" id=\"row-flex-end-item0\" style=\"\"></div><div class=\"item\" id=\"row-flex-end-item1\" style=\"\"></div><div class=\"item\" id=\"row-flex-end-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-end\" style=\"align-content:end\"><div class=\"item\" id=\"row-end-item0\" style=\"\"></div><div class=\"item\" id=\"row-end-item1\" style=\"\"></div><div class=\"item\" id=\"row-end-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-center\" style=\"align-content:center\"><div class=\"item\" id=\"row-center-item0\" style=\"\"></div><div class=\"item\" id=\"row-center-item1\" style=\"\"></div><div class=\"item\" id=\"row-center-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-between\" style=\"align-content:space-between\"><div class=\"item\" id=\"row-between-item0\" style=\"\"></div><div class=\"item\" id=\"row-between-item1\" style=\"\"></div><div class=\"item\" id=\"row-between-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-around\" style=\"align-content:space-around\"><div class=\"item\" id=\"row-around-item0\" style=\"\"></div><div class=\"item\" id=\"row-around-item1\" style=\"\"></div><div class=\"item\" id=\"row-around-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-evenly\" style=\"align-content:space-evenly\"><div class=\"item\" id=\"row-evenly-item0\" style=\"\"></div><div class=\"item\" id=\"row-evenly-item1\" style=\"\"></div><div class=\"item\" id=\"row-evenly-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-stretch-auto\" style=\"align-content:stretch;align-items:stretch\"><div class=\"item\" id=\"row-stretch-auto-item0\" style=\"height:auto\"></div><div class=\"item\" id=\"row-stretch-auto-item1\" style=\"height:auto\"></div><div class=\"item\" id=\"row-stretch-auto-item2\" style=\"height:auto\"></div></div>"
        "<div class=\"flex\" id=\"row-stretch-limits\" style=\"align-content:stretch;align-items:stretch\"><div class=\"item\" id=\"row-stretch-limits-item0\" style=\"height:auto;min-height:30px;max-height:60px\"></div><div class=\"item\" id=\"row-stretch-limits-item1\" style=\"height:auto;max-height:40px\"></div><div class=\"item\" id=\"row-stretch-limits-item2\" style=\"height:auto;min-height:25px;max-height:70px\"></div></div>"
        "<div class=\"flex\" id=\"row-single-wrapped-line\" style=\"align-content:center\"><div class=\"item\" id=\"row-single-wrapped-line-item0\" style=\"\"></div><div class=\"item\" id=\"row-single-wrapped-line-item1\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"row-nowrap\" style=\"flex-wrap:nowrap;align-content:end\"><div class=\"item\" id=\"row-nowrap-item0\" style=\"\"></div><div class=\"item\" id=\"row-nowrap-item1\" style=\"\"></div><div class=\"item\" id=\"row-nowrap-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"reverse-flex-start\" style=\"flex-wrap:wrap-reverse;align-content:flex-start\"><div class=\"item\" id=\"reverse-flex-start-item0\" style=\"\"></div><div class=\"item\" id=\"reverse-flex-start-item1\" style=\"\"></div><div class=\"item\" id=\"reverse-flex-start-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"reverse-start\" style=\"flex-wrap:wrap-reverse;align-content:start\"><div class=\"item\" id=\"reverse-start-item0\" style=\"\"></div><div class=\"item\" id=\"reverse-start-item1\" style=\"\"></div><div class=\"item\" id=\"reverse-start-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"reverse-flex-end\" style=\"flex-wrap:wrap-reverse;align-content:flex-end\"><div class=\"item\" id=\"reverse-flex-end-item0\" style=\"\"></div><div class=\"item\" id=\"reverse-flex-end-item1\" style=\"\"></div><div class=\"item\" id=\"reverse-flex-end-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"reverse-end\" style=\"flex-wrap:wrap-reverse;align-content:end\"><div class=\"item\" id=\"reverse-end-item0\" style=\"\"></div><div class=\"item\" id=\"reverse-end-item1\" style=\"\"></div><div class=\"item\" id=\"reverse-end-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"overflow-center\" style=\"height:30px;align-content:center\"><div class=\"item\" id=\"overflow-center-item0\" style=\"\"></div><div class=\"item\" id=\"overflow-center-item1\" style=\"\"></div><div class=\"item\" id=\"overflow-center-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"overflow-between\" style=\"height:30px;align-content:space-between\"><div class=\"item\" id=\"overflow-between-item0\" style=\"\"></div><div class=\"item\" id=\"overflow-between-item1\" style=\"\"></div><div class=\"item\" id=\"overflow-between-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"overflow-safe-center\" style=\"height:30px;align-content:safe center\"><div class=\"item\" id=\"overflow-safe-center-item0\" style=\"\"></div><div class=\"item\" id=\"overflow-safe-center-item1\" style=\"\"></div><div class=\"item\" id=\"overflow-safe-center-item2\" style=\"\"></div></div>"
        "<div class=\"flex\" id=\"column-center\" style=\"flex-direction:column;width:200px;height:120px;align-content:center\"><div class=\"item\" id=\"column-center-item0\" style=\"width:20px;height:50px\"></div><div class=\"item\" id=\"column-center-item1\" style=\"width:20px;height:50px\"></div><div class=\"item\" id=\"column-center-item2\" style=\"width:20px;height:50px\"></div></div>"
        "<div class=\"flex\" id=\"column-rtl-flex-start\" style=\"flex-direction:column;width:200px;height:120px;direction:rtl;align-content:flex-start\"><div class=\"item\" id=\"column-rtl-flex-start-item0\" style=\"width:20px;height:50px\"></div><div class=\"item\" id=\"column-rtl-flex-start-item1\" style=\"width:20px;height:50px\"></div><div class=\"item\" id=\"column-rtl-flex-start-item2\" style=\"width:20px;height:50px\"></div></div>"
        "<div class=\"flex\" id=\"column-rtl-start\" style=\"flex-direction:column;width:200px;height:120px;direction:rtl;align-content:start\"><div class=\"item\" id=\"column-rtl-start-item0\" style=\"width:20px;height:50px\"></div><div class=\"item\" id=\"column-rtl-start-item1\" style=\"width:20px;height:50px\"></div><div class=\"item\" id=\"column-rtl-start-item2\" style=\"width:20px;height:50px\"></div></div>"
        "<div class=\"flex\" id=\"column-rtl-reverse-end\" style=\"flex-direction:column;width:200px;height:120px;direction:rtl;flex-wrap:wrap-reverse;align-content:end\"><div class=\"item\" id=\"column-rtl-reverse-end-item0\" style=\"width:20px;height:50px\"></div><div class=\"item\" id=\"column-rtl-reverse-end-item1\" style=\"width:20px;height:50px\"></div><div class=\"item\" id=\"column-rtl-reverse-end-item2\" style=\"width:20px;height:50px\"></div></div>"
        "<div class=\"flex\" id=\"row-cross-auto-margins\" style=\"align-content:stretch;align-items:stretch\"><div class=\"item\" id=\"row-cross-auto-margins-item0\" style=\"height:60px\"></div><div class=\"item\" id=\"row-cross-auto-margins-item1\" style=\"height:20px;margin-top:auto\"></div><div class=\"item\" id=\"row-cross-auto-margins-item2\" style=\"height:auto\"></div></div>"
        "<div class=\"flex\" id=\"stretch-equal-natural-nowrap\" style=\"height:100px;flex-wrap:nowrap;align-content:stretch;align-items:stretch\"><div class=\"item\" id=\"stretch-equal-natural-nowrap-item0\" style=\"height:auto\"><div style=\"height:100px\"></div><div id=\"stretch-equal-natural-nowrap-percent\" style=\"height:50%;width:20px\"></div></div><div class=\"item\" id=\"stretch-equal-natural-nowrap-item1\" style=\"height:auto\"><div style=\"height:100px\"></div></div></div>"
        "<div class=\"flex\" id=\"stretch-equal-natural-wrapped\" style=\"height:100px;flex-wrap:wrap;align-content:stretch;align-items:stretch\"><div class=\"item\" id=\"stretch-equal-natural-wrapped-item0\" style=\"height:auto\"><div style=\"height:100px\"></div><div id=\"stretch-equal-natural-wrapped-percent\" style=\"height:50%;width:20px\"></div></div><div class=\"item\" id=\"stretch-equal-natural-wrapped-item1\" style=\"height:auto\"><div style=\"height:100px\"></div></div></div>"
        "<div class=\"flex\" id=\"stretch-equal-natural-auto-parent\" style=\"height:auto;flex-wrap:wrap;align-content:stretch;align-items:stretch\"><div class=\"item\" id=\"stretch-equal-natural-auto-parent-item0\" style=\"height:auto\"><div style=\"height:100px\"></div><div id=\"stretch-equal-natural-auto-parent-percent\" style=\"height:50%;width:20px\"></div></div><div class=\"item\" id=\"stretch-equal-natural-auto-parent-item1\" style=\"height:auto\"><div style=\"height:100px\"></div></div></div>"
        "<div class=\"flex\" id=\"relative-and-nested-abs\" style=\"align-content:center\"><div class=\"item\" id=\"relative-and-nested-abs-item0\" style=\"position:relative;left:7px;top:9px\"><div id=\"nested-absolute\" style=\"position:absolute;left:3px;top:4px;width:9px;height:11px\"></div></div><div class=\"item\" id=\"relative-and-nested-abs-item1\" style=\"\"></div><div class=\"item\" id=\"relative-and-nested-abs-item2\" style=\"\"></div></div>"
        "<div id=\"baseline-overflow-target\" style=\"display:flex;position:relative;line-height:0;font-size:20px;width:300px;margin:10px 0;padding:10px;border:3px solid;align-items:last baseline\"><div id=\"baseline-overflow-item\"><span style=\"display:inline-block;width:1em;height:1em\"></span><br><span style=\"display:inline-block;width:1em;height:1em\"></span></div><div style=\"overflow:hidden;width:100px;height:80px;margin:10px;border:5px solid;padding:10px;font-size:30px\"><div style=\"margin-top:-200px\"><span style=\"display:inline-block;width:1em;height:1em\"></span><br><span style=\"display:inline-block;width:1em;height:1em\"></span></div></div></div>"
        "</body></html>";
    const char *css = "body{margin:0}.flex{display:flex;position:relative;box-sizing:content-box;width:120px;height:200px;padding:7px 11px 13px 17px;border:3px solid;flex-wrap:wrap;flex-direction:row;gap:10px;align-items:flex-start;align-content:flex-start}.item{flex:0 0 50px;width:50px;height:20px;box-sizing:content-box}";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 800, 2000);
    size(d, "row-flex-start", 154, 226);
    geometry(d, "row-flex-start", "row-flex-start-item0", 20, 10, 50, 20);
    geometry(d, "row-flex-start", "row-flex-start-item1", 80, 10, 50, 20);
    geometry(d, "row-flex-start", "row-flex-start-item2", 20, 40, 50, 20);
    size(d, "row-start", 154, 226);
    geometry(d, "row-start", "row-start-item0", 20, 10, 50, 20);
    geometry(d, "row-start", "row-start-item1", 80, 10, 50, 20);
    geometry(d, "row-start", "row-start-item2", 20, 40, 50, 20);
    size(d, "row-flex-end", 154, 226);
    geometry(d, "row-flex-end", "row-flex-end-item0", 20, 160, 50, 20);
    geometry(d, "row-flex-end", "row-flex-end-item1", 80, 160, 50, 20);
    geometry(d, "row-flex-end", "row-flex-end-item2", 20, 190, 50, 20);
    size(d, "row-end", 154, 226);
    geometry(d, "row-end", "row-end-item0", 20, 160, 50, 20);
    geometry(d, "row-end", "row-end-item1", 80, 160, 50, 20);
    geometry(d, "row-end", "row-end-item2", 20, 190, 50, 20);
    size(d, "row-center", 154, 226);
    geometry(d, "row-center", "row-center-item0", 20, 85, 50, 20);
    geometry(d, "row-center", "row-center-item1", 80, 85, 50, 20);
    geometry(d, "row-center", "row-center-item2", 20, 115, 50, 20);
    size(d, "row-between", 154, 226);
    geometry(d, "row-between", "row-between-item0", 20, 10, 50, 20);
    geometry(d, "row-between", "row-between-item1", 80, 10, 50, 20);
    geometry(d, "row-between", "row-between-item2", 20, 190, 50, 20);
    size(d, "row-around", 154, 226);
    geometry(d, "row-around", "row-around-item0", 20, 47.5, 50, 20);
    geometry(d, "row-around", "row-around-item1", 80, 47.5, 50, 20);
    geometry(d, "row-around", "row-around-item2", 20, 152.5, 50, 20);
    size(d, "row-evenly", 154, 226);
    geometry(d, "row-evenly", "row-evenly-item0", 20, 60, 50, 20);
    geometry(d, "row-evenly", "row-evenly-item1", 80, 60, 50, 20);
    geometry(d, "row-evenly", "row-evenly-item2", 20, 140, 50, 20);
    size(d, "row-stretch-auto", 154, 226);
    geometry(d, "row-stretch-auto", "row-stretch-auto-item0", 20, 10, 50, 95);
    geometry(d, "row-stretch-auto", "row-stretch-auto-item1", 80, 10, 50, 95);
    geometry(d, "row-stretch-auto", "row-stretch-auto-item2", 20, 115, 50, 95);
    size(d, "row-stretch-limits", 154, 226);
    geometry(d, "row-stretch-limits", "row-stretch-limits-item0", 20, 10, 50, 60);
    geometry(d, "row-stretch-limits", "row-stretch-limits-item1", 80, 10, 50, 40);
    geometry(d, "row-stretch-limits", "row-stretch-limits-item2", 20, 117.5, 50, 70);
    size(d, "row-single-wrapped-line", 154, 226);
    geometry(d, "row-single-wrapped-line", "row-single-wrapped-line-item0", 20, 100, 50, 20);
    geometry(d, "row-single-wrapped-line", "row-single-wrapped-line-item1", 80, 100, 50, 20);
    size(d, "row-nowrap", 154, 226);
    geometry(d, "row-nowrap", "row-nowrap-item0", 20, 10, 50, 20);
    geometry(d, "row-nowrap", "row-nowrap-item1", 80, 10, 50, 20);
    geometry(d, "row-nowrap", "row-nowrap-item2", 140, 10, 50, 20);
    size(d, "reverse-flex-start", 154, 226);
    geometry(d, "reverse-flex-start", "reverse-flex-start-item0", 20, 190, 50, 20);
    geometry(d, "reverse-flex-start", "reverse-flex-start-item1", 80, 190, 50, 20);
    geometry(d, "reverse-flex-start", "reverse-flex-start-item2", 20, 160, 50, 20);
    size(d, "reverse-start", 154, 226);
    geometry(d, "reverse-start", "reverse-start-item0", 20, 40, 50, 20);
    geometry(d, "reverse-start", "reverse-start-item1", 80, 40, 50, 20);
    geometry(d, "reverse-start", "reverse-start-item2", 20, 10, 50, 20);
    size(d, "reverse-flex-end", 154, 226);
    geometry(d, "reverse-flex-end", "reverse-flex-end-item0", 20, 40, 50, 20);
    geometry(d, "reverse-flex-end", "reverse-flex-end-item1", 80, 40, 50, 20);
    geometry(d, "reverse-flex-end", "reverse-flex-end-item2", 20, 10, 50, 20);
    size(d, "reverse-end", 154, 226);
    geometry(d, "reverse-end", "reverse-end-item0", 20, 190, 50, 20);
    geometry(d, "reverse-end", "reverse-end-item1", 80, 190, 50, 20);
    geometry(d, "reverse-end", "reverse-end-item2", 20, 160, 50, 20);
    size(d, "overflow-center", 154, 56);
    geometry(d, "overflow-center", "overflow-center-item0", 20, 0, 50, 20);
    geometry(d, "overflow-center", "overflow-center-item1", 80, 0, 50, 20);
    geometry(d, "overflow-center", "overflow-center-item2", 20, 30, 50, 20);
    size(d, "overflow-between", 154, 56);
    geometry(d, "overflow-between", "overflow-between-item0", 20, 10, 50, 20);
    geometry(d, "overflow-between", "overflow-between-item1", 80, 10, 50, 20);
    geometry(d, "overflow-between", "overflow-between-item2", 20, 40, 50, 20);
    size(d, "overflow-safe-center", 154, 56);
    geometry(d, "overflow-safe-center", "overflow-safe-center-item0", 20, 10, 50, 20);
    geometry(d, "overflow-safe-center", "overflow-safe-center-item1", 80, 10, 50, 20);
    geometry(d, "overflow-safe-center", "overflow-safe-center-item2", 20, 40, 50, 20);
    size(d, "column-center", 234, 146);
    geometry(d, "column-center", "column-center-item0", 95, 10, 20, 50);
    geometry(d, "column-center", "column-center-item1", 95, 70, 20, 50);
    geometry(d, "column-center", "column-center-item2", 125, 10, 20, 50);
    size(d, "column-rtl-flex-start", 234, 146);
    geometry(d, "column-rtl-flex-start", "column-rtl-flex-start-item0", 200, 10, 20, 50);
    geometry(d, "column-rtl-flex-start", "column-rtl-flex-start-item1", 200, 70, 20, 50);
    geometry(d, "column-rtl-flex-start", "column-rtl-flex-start-item2", 170, 10, 20, 50);
    size(d, "column-rtl-start", 234, 146);
    geometry(d, "column-rtl-start", "column-rtl-start-item0", 200, 10, 20, 50);
    geometry(d, "column-rtl-start", "column-rtl-start-item1", 200, 70, 20, 50);
    geometry(d, "column-rtl-start", "column-rtl-start-item2", 170, 10, 20, 50);
    size(d, "column-rtl-reverse-end", 234, 146);
    geometry(d, "column-rtl-reverse-end", "column-rtl-reverse-end-item0", 20, 10, 20, 50);
    geometry(d, "column-rtl-reverse-end", "column-rtl-reverse-end-item1", 20, 70, 20, 50);
    geometry(d, "column-rtl-reverse-end", "column-rtl-reverse-end-item2", 50, 10, 20, 50);
    size(d, "row-cross-auto-margins", 154, 226);
    geometry(d, "row-cross-auto-margins", "row-cross-auto-margins-item0", 20, 10, 50, 60);
    geometry(d, "row-cross-auto-margins", "row-cross-auto-margins-item1", 80, 115, 50, 20);
    geometry(d, "row-cross-auto-margins", "row-cross-auto-margins-item2", 20, 145, 50, 65);
    size(d, "stretch-equal-natural-nowrap", 154, 126);
    geometry(d, "stretch-equal-natural-nowrap", "stretch-equal-natural-nowrap-item0", 20, 10, 50, 100);
    geometry(d, "stretch-equal-natural-nowrap", "stretch-equal-natural-nowrap-item1", 80, 10, 50, 100);
    geometry(d, "stretch-equal-natural-nowrap", "stretch-equal-natural-nowrap-percent", 20, 110, 20, 50);
    size(d, "stretch-equal-natural-wrapped", 154, 126);
    geometry(d, "stretch-equal-natural-wrapped", "stretch-equal-natural-wrapped-item0", 20, 10, 50, 100);
    geometry(d, "stretch-equal-natural-wrapped", "stretch-equal-natural-wrapped-item1", 80, 10, 50, 100);
    geometry(d, "stretch-equal-natural-wrapped", "stretch-equal-natural-wrapped-percent", 20, 110, 20, 50);
    size(d, "stretch-equal-natural-auto-parent", 154, 126);
    geometry(d, "stretch-equal-natural-auto-parent", "stretch-equal-natural-auto-parent-item0", 20, 10, 50, 100);
    geometry(d, "stretch-equal-natural-auto-parent", "stretch-equal-natural-auto-parent-item1", 80, 10, 50, 100);
    geometry(d, "stretch-equal-natural-auto-parent", "stretch-equal-natural-auto-parent-percent", 20, 110, 20, 50);
    size(d, "relative-and-nested-abs", 154, 226);
    geometry(d, "relative-and-nested-abs", "relative-and-nested-abs-item0", 27, 94, 50, 20);
    geometry(d, "relative-and-nested-abs", "relative-and-nested-abs-item1", 80, 85, 50, 20);
    geometry(d, "relative-and-nested-abs", "relative-and-nested-abs-item2", 20, 115, 50, 20);
    geometry(d, "relative-and-nested-abs", "nested-absolute", 30, 98, 9, 11);
    /* Regression from the independently passing WPT baseline-overflow case:
       last-baseline participants must not be replaced with end alignment. */
    Node *baseline_target = doc_get_element_by_id(d, "baseline-overflow-target");
    Node *baseline_item = doc_get_element_by_id(d, "baseline-overflow-item");
    same("last-baseline-overflow", "offsetTop",
         baseline_target && baseline_target->box && baseline_item && baseline_item->box
             ? baseline_item->box->y - baseline_target->box->y - baseline_target->box->b[0] : NAN, 10);
    layout_free(l); style_engine_free(e); doc_free(d);
    printf("Flex alignment geometry: %u/%u passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
