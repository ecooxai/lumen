/* CSS Grid 10.2-10.5: content alignment, auto margins and item constraints. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void check(Document *d, const char *id, float x, float y, float w, float h) {
    Node *n = doc_get_element_by_id(d, id);
    assert(n && n->box && n->box->parent);
    Box *b = n->box, *p = b->parent;
    float actual[] = {b->x - p->x, b->y - p->y, b->w, b->h};
    float expected[] = {x, y, w, h};
    for (int i = 0; i < 4; ++i) {
        if (fabsf(actual[i] - expected[i]) > .05f)
            fprintf(stderr, "%s component %d: expected %g, got %g\n", id, i, expected[i], actual[i]);
        assert(fabsf(actual[i] - expected[i]) <= .05f);
    }
}
int main(void) {
    dom_init(); font_init();
    Document *d = doc_new("https://grid-alignment.test/");
    const char *html = "<html><body>"
        "<div class='g' style='justify-content:center;align-content:end'>"
        "<div id='center' class='item' style='justify-self:center;align-self:center'></div>"
        "<div id='stretch' style='grid-column:2;grid-row:2'></div></div>"
        "<div class='g' style='direction:rtl'>"
        "<div id='rtl' class='item'></div>"
        "<div id='self-ltr' class='item' style='grid-row:2;direction:ltr;justify-self:self-start'></div></div>"
        "<div class='g' style='justify-content:space-between;align-content:space-around'>"
        "<div id='distributed' style='grid-column:2;grid-row:2'></div></div>"
        "<div class='one'><div id='auto' class='item' style='margin:auto;justify-self:end;align-self:end'></div></div>"
        "<div class='one'><div id='auto-one' class='item' style='margin:auto 5px 3px auto'></div></div>"
        "<div class='one'><div id='max' style='max-width:50px;max-height:30px'></div></div>"
        "<div class='one'><div id='min' style='min-width:120px;min-height:80px'></div></div>"
        "<div class='one'><div id='safe' style='width:140px;height:80px;justify-self:safe center;align-self:safe center'></div></div>"
        "<div class='one'><div id='unsafe' style='width:140px;height:80px;justify-self:unsafe center;align-self:unsafe center'></div></div>"
        "<div class='one'><div id='self-start' class='item' style='writing-mode:vertical-rl;direction:rtl;justify-self:self-start;align-self:self-start'></div></div>"
        "<div class='one'><div id='self-end' class='item' style='writing-mode:vertical-rl;direction:rtl;justify-self:self-end;align-self:self-end'></div></div>"
        "<div class='one'><div><div id='percent' style='height:50%'></div></div></div>"
        "<div class='one'><div id='overflow-auto' style='width:140px;height:80px;margin:auto;justify-self:center;align-self:end'></div></div>"
        "<div class='one' style='direction:rtl'><div id='overflow-rtl' style='width:140px;height:80px;margin:auto;justify-self:center;align-self:end'></div></div>"
        "<div class='one'><div id='overflow-one' style='width:140px;height:80px;margin-left:auto;justify-self:end;align-self:end'></div></div>"
        "<div class='one'><div id='stretch-fixed-width' style='width:20px;justify-self:center;align-self:stretch'></div></div>"
        "<div class='g' style='align-content:center;align-content:right'><div id='invalid-content' class='item'></div></div>"
        "<div class='one' style='align-items:end;align-items:safe right'><div id='invalid-items' class='item'></div></div>"
        "<div class='one'><div id='invalid-self' class='item' style='align-self:end;align-self:LEFT'></div></div>"
        "<div class='g' style='justify-content:right'><div id='valid-right' class='item'></div></div>"
        "</body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0}.g{display:grid;position:relative;width:300px;height:200px;grid-template-columns:100px 100px;grid-template-rows:50px 50px;gap:10px}"
        ".one{display:grid;position:relative;width:100px;height:60px;grid-template-columns:100px;grid-template-rows:60px}.item{width:20px;height:10px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 800, 2000);
    check(d, "center", 85, 110, 20, 10);
    check(d, "stretch", 155, 150, 100, 50);
    check(d, "rtl", 280, 0, 20, 10);
    check(d, "self-ltr", 200, 60, 20, 10);
    check(d, "distributed", 200, 127.5f, 100, 50);
    check(d, "auto", 40, 25, 20, 10);
    check(d, "auto-one", 75, 47, 20, 10);
    check(d, "max", 0, 0, 50, 30);
    check(d, "min", 0, 0, 120, 80);
    check(d, "safe", 0, 0, 140, 80);
    check(d, "unsafe", -20, -10, 140, 80);
    check(d, "self-start", 80, 50, 20, 10);
    check(d, "self-end", 0, 0, 20, 10);
    check(d, "percent", 0, 0, 100, 30);
    check(d, "overflow-auto", 0, 0, 140, 80);
    check(d, "overflow-rtl", -40, 0, 140, 80);
    check(d, "overflow-one", 0, -20, 140, 80);
    check(d, "stretch-fixed-width", 40, 0, 20, 60);
    check(d, "invalid-content", 0, 45, 20, 10);
    check(d, "invalid-items", 0, 50, 20, 10);
    check(d, "invalid-self", 0, 50, 20, 10);
    check(d, "valid-right", 90, 0, 20, 10);
    puts("grid alignment: 22 geometry cases passed");
    return 0;
}
