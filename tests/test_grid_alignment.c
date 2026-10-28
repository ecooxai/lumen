/* CSS Grid 10.2-10.5: content alignment, auto margins and item constraints. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int geometry_checks;
static void check(Document *d, const char *id, float x, float y, float w, float h) {
    geometry_checks++;
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
        "<div style='display:grid;position:relative;width:300px;height:200px;grid-auto-columns:20px;grid-auto-rows:40px;justify-content:space-between'>"
        "<div id='implicit-left'></div><div id='implicit-right' style='grid-column:2;grid-row:2'></div></div>"
        "<div style='display:grid;position:relative;width:300px;height:100px;grid-auto-rows:40px'><div id='implicit-stretch' style='grid-column:2'></div></div>"
        "<div style='display:grid;position:relative;width:300px;height:200px;grid-template-columns:10px;grid-template-rows:20px;grid-auto-columns:50px;grid-auto-rows:30px'><div id='explicit-implicit' style='grid-column:2;grid-row:2'></div></div>"
        "<div style='display:grid;position:relative;width:300px;height:200px;grid-template-columns:10px;grid-template-rows:10px;grid-auto-columns:20px 40px;grid-auto-rows:10px 30px'><div id='cyclic' style='grid-column:4;grid-row:4'></div></div>"
        "<div style='display:grid;position:relative;width:1000px;height:1000px;grid-template-columns:10px;grid-template-rows:20px;grid-auto-columns:minmax(30%,100px);grid-auto-rows:minmax(10%,15px)'><div id='implicit-minmax' style='grid-column:2;grid-row:2'></div></div>"
        "<div style='display:grid;position:relative;width:100px;height:200px;grid-auto-columns:20px;grid-auto-rows:40px'><div style='height:80px'></div><div id='row-overflow' style='grid-row:2'></div></div>"
        "<div style='display:grid;position:relative;width:40px;height:20px'><div id='implicit-min'><span style='display:block;width:50px;height:10px'></span></div></div>"
        "<div style='display:grid;position:relative;width:200px;height:20px;grid-auto-columns:max-content'><div id='intrinsic-max'><span style='display:block;width:50px;height:10px'></span></div></div>"
        "<div style='display:grid;position:relative;width:300px;grid-template-columns:100px 20px;grid-template-rows:20px;grid-auto-columns:20px'><div id='negative-end' style='grid-column:1 / -1;grid-row:1'></div><div style='grid-column:4;grid-row:1'></div></div>"
        "<div style='display:grid;position:relative;width:100px;height:90px;grid-template-rows:1fr 2fr'><div id='row-fr-one'></div><div id='row-fr-two'></div></div>"
        "<div style='display:grid;position:relative;width:100px;height:90px;grid-template-rows:1fr 2fr'><div style='min-height:60px'></div><div id='row-fr-min'></div></div>"
        "<div style='display:grid;position:relative;width:100px;height:80px;grid-template-rows:.25fr .25fr'><div></div><div id='row-fr-small'></div></div>"
        "<div class='placement'><div id='span-first' style='grid-column-start:span 2;grid-column-end:3;grid-row-start:span 2;grid-row-end:3'></div></div>"
        "<div class='placement'><div id='end-first' style='grid-column-end:3;grid-column-start:span 2;grid-row-end:3;grid-row-start:span 2'></div></div>"
        "<div class='placement'><div id='span-shorthand' style='grid-column:span 2 / 3;grid-row:span 2 / 3'></div></div>"
        "<div class='placement'><div id='span-named-count' style='grid-column-start:span col 2;grid-column-end:3;grid-row-start:span row 2;grid-row-end:3'></div></div>"
        "<div class='placement'><div id='end-only' style='grid-column-end:3;grid-row-end:3'></div></div>"
        "<div class='placement'><div id='end-span' style='grid-column:2 / span 2;grid-row:2 / span 2'></div></div>"
        "<div class='placement'><div id='start-auto' style='grid-column:span 2 / 3;grid-column-start:auto;grid-row:span 2 / 3;grid-row-start:auto'></div></div>"
        "<div class='placement'><div id='end-auto' style='grid-column:2 / span 2;grid-column-end:auto;grid-row:2 / span 2;grid-row-end:auto'></div></div>"
        "<div class='placement'><div id='shorthand-reset' style='grid-column:1 / 3;grid-column:2;grid-row:1 / 3;grid-row:2'></div></div>"
        "<div class='placement'><div id='reversed-lines' style='grid-column:3 / 1;grid-row:3 / 1'></div></div>"
        "<div class='placement'><div id='double-span' style='grid-column:span 2 / span 3;grid-row:span 2 / span 3'></div></div>"
        "<div class='placement'><div id='wide-reset' style='grid-column:span 2 / 3;grid-column-start:initial;grid-row:span 2 / 3;grid-row-start:unset'></div></div>"
        "<div class='placement' style='grid-column:span 2 / 3;grid-row:span 2 / 3'><div id='wide-inherit' style='grid-column:inherit;grid-row:inherit'></div></div>"
        "<div style='display:grid;position:relative;width:400px;height:300px;justify-content:start;align-content:start;grid-template-columns:100px;grid-template-rows:50px;grid-auto-columns:20px;grid-auto-rows:30px;'><div id='leading' style='grid-column:-3;grid-row:-3'></div><div id='leading-anchor' style='grid-column:1;grid-row:1'></div><div id='leading-auto' style=''></div></div><div style='display:grid;position:relative;width:400px;height:300px;justify-content:start;align-content:start;grid-template-columns:100px;grid-template-rows:50px;grid-auto-columns:20px;grid-auto-rows:30px;grid-auto-columns:20px 40px 60px;grid-auto-rows:10px 30px 50px'><div id='cycle-first' style='grid-column:-5;grid-row:-5'></div><div id='cycle-middle' style='grid-column:-4;grid-row:-4'></div><div id='cycle-last' style='grid-column:-3;grid-row:-3'></div><div id='cycle-anchor' style='grid-column:1;grid-row:1'></div></div><div style='display:grid;position:relative;width:400px;height:300px;justify-content:start;align-content:start;grid-template-columns:100px;grid-template-rows:50px;grid-auto-columns:20px;grid-auto-rows:30px;grid-auto-columns:20px 40px 60px;grid-auto-rows:10px 30px 50px'><div id='leading-span' style='grid-column:span 2 / -2;grid-row:span 2 / -2'></div><div id='span-anchor' style='grid-column:1;grid-row:1'></div></div><div style='display:grid;position:relative;width:400px;height:300px;justify-content:start;align-content:start;grid-template-columns:100px;grid-template-rows:50px;grid-auto-columns:20px;grid-auto-rows:30px;grid-template-columns:none;grid-template-rows:none;justify-content:end;align-content:end'><div id='negative-only' style='grid-column:-2 / -1;grid-row:-2 / -1'></div></div><div style='display:grid;position:relative;width:400px;height:300px;justify-content:start;align-content:start;grid-template-columns:100px;grid-template-rows:50px;grid-auto-columns:20px;grid-auto-rows:30px;grid-template-areas:\"main\"'><div id='area-leading' style='grid-column:-3;grid-row:-3'></div><div id='area-anchor' style='grid-area:main'></div></div><div style='display:grid;position:relative;width:400px;height:300px;justify-content:start;align-content:start;grid-template-columns:100px;grid-template-rows:50px;grid-auto-columns:20px;grid-auto-rows:30px;grid-template-columns:none;grid-template-rows:none;grid-template-areas:\"a b\" \"c d\";grid-auto-columns:20px 40px;grid-auto-rows:30px 50px'><div id='unsized-leading' style='grid-column:-4;grid-row:-4'></div><div id='unsized-area' style='grid-area:d'></div><div id='unsized-trailing' style='grid-column:3;grid-row:3'></div></div><div style='display:grid;position:relative;width:400px;height:300px;justify-content:start;align-content:start;grid-template-columns:100px;grid-template-rows:50px;grid-auto-columns:20px;grid-auto-rows:30px;column-gap:5px;row-gap:7px'><div id='gap-leading' style='grid-column:-3;grid-row:-3'></div><div id='gap-anchor' style='grid-column:1;grid-row:1'></div></div><div style='display:grid;position:relative;width:400px;height:300px;justify-content:start;align-content:start;grid-template-columns:100px;grid-template-rows:50px;grid-auto-columns:20px;grid-auto-rows:30px;direction:rtl'><div id='rtl-leading' style='grid-column:-3;grid-row:-3'></div><div id='rtl-anchor' style='grid-column:1;grid-row:1'></div></div>"
        "</body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0}.g{display:grid;position:relative;width:300px;height:200px;grid-template-columns:100px 100px;grid-template-rows:50px 50px;gap:10px}"
        ".one{display:grid;position:relative;width:100px;height:60px;grid-template-columns:100px;grid-template-rows:60px}.item{width:20px;height:10px}"
        ".placement{display:grid;position:relative;width:300px;height:490px;grid-template-columns:[col] 50px [col] 100px [col] 150px [col];grid-template-rows:[row] 70px [row] 140px [row] 280px [row]}";
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
    /* Chromium150 compatibility controls, not normative WPT proof: current
       Grid1 10.2 / Grid2 11.2 differ for overflowing auto margins. */
    check(d, "overflow-auto", 0, 0, 140, 80);
    check(d, "overflow-rtl", -40, 0, 140, 80);
    check(d, "overflow-one", 0, -20, 140, 80);
    check(d, "stretch-fixed-width", 40, 0, 20, 60);
    check(d, "invalid-content", 0, 45, 20, 10);
    check(d, "invalid-items", 0, 50, 20, 10);
    check(d, "invalid-self", 0, 50, 20, 10);
    check(d, "valid-right", 90, 0, 20, 10);
    check(d, "implicit-left", 0, 0, 20, 40);
    check(d, "implicit-right", 280, 40, 20, 40);
    check(d, "implicit-stretch", 150, 0, 150, 40);
    check(d, "explicit-implicit", 10, 20, 50, 30);
    check(d, "cyclic", 70, 50, 20, 10);
    check(d, "implicit-minmax", 10, 20, 300, 100);
    check(d, "row-overflow", 0, 40, 20, 40);
    check(d, "implicit-min", 0, 0, 50, 20);
    check(d, "intrinsic-max", 0, 0, 50, 20);
    check(d, "negative-end", 0, 0, 120, 20);
    check(d, "row-fr-one", 0, 0, 100, 30);
    check(d, "row-fr-two", 0, 30, 100, 60);
    check(d, "row-fr-min", 0, 60, 100, 30);
    check(d, "row-fr-small", 0, 20, 100, 20);
    check(d, "span-first", 0, 0, 150, 210);
    check(d, "end-first", 0, 0, 150, 210);
    check(d, "span-shorthand", 0, 0, 150, 210);
    check(d, "span-named-count", 0, 0, 150, 210);
    check(d, "end-only", 50, 70, 100, 140);
    check(d, "end-span", 50, 70, 250, 420);
    check(d, "start-auto", 50, 70, 100, 140);
    check(d, "end-auto", 50, 70, 100, 140);
    check(d, "shorthand-reset", 50, 70, 100, 140);
    check(d, "reversed-lines", 0, 0, 150, 210);
    check(d, "double-span", 0, 0, 150, 210);
    check(d, "wide-reset", 50, 70, 100, 140);
    check(d, "wide-inherit", 0, 0, 150, 210);
    Node *changing = doc_get_element_by_id(d, "span-first");
    node_set_attr(changing, "style", "grid-column-end:3;grid-column-start:span 2;grid-row-end:3;grid-row-start:span 2");
    style_recalc(e, &d->node, true); layout_run(l, d, 800, 2000);
    check(d, "span-first", 0, 0, 150, 210);
    node_set_attr(changing, "style", "grid-column-start:2;grid-column-end:auto;grid-row-start:2;grid-row-end:auto");
    style_recalc(e, &d->node, true); layout_run(l, d, 800, 2000);
    check(d, "span-first", 50, 70, 100, 140);
    node_set_attr(changing, "style", "grid-column-end:3;grid-column-start:span 2;grid-row-end:3;grid-row-start:span 2");
    style_recalc(e, &d->node, true); layout_run(l, d, 800, 2000);
    check(d, "span-first", 0, 0, 150, 210);
    check(d, "leading", 0, 0, 20, 30);
    check(d, "leading-anchor", 20, 30, 100, 50);
    check(d, "leading-auto", 20, 0, 100, 30);
    check(d, "cycle-first", 0, 0, 20, 10);
    check(d, "cycle-middle", 20, 10, 40, 30);
    check(d, "cycle-last", 60, 40, 60, 50);
    check(d, "cycle-anchor", 120, 90, 100, 50);
    check(d, "leading-span", 0, 0, 100, 80);
    check(d, "span-anchor", 100, 80, 100, 50);
    check(d, "negative-only", 380, 270, 20, 30);
    check(d, "area-leading", 0, 0, 20, 30);
    check(d, "area-anchor", 20, 30, 100, 50);
    check(d, "unsized-leading", 0, 0, 40, 50);
    check(d, "unsized-area", 60, 80, 40, 50);
    check(d, "unsized-trailing", 100, 130, 20, 30);
    check(d, "gap-leading", 0, 0, 20, 30);
    check(d, "gap-anchor", 25, 37, 100, 50);
    check(d, "rtl-leading", 380, 0, 20, 30);
    check(d, "rtl-anchor", 280, 30, 100, 50);
    Node *leading = doc_get_element_by_id(d, "leading");
    node_set_attr(leading, "style", "grid-column:2;grid-row:2");
    style_recalc(e, &d->node, true); layout_run(l, d, 800, 2000);
    check(d, "leading", 100, 50, 20, 30);
    check(d, "leading-anchor", 0, 0, 100, 50);
    node_set_attr(leading, "style", "grid-column:-3;grid-row:-3");
    style_recalc(e, &d->node, true); layout_run(l, d, 800, 2000);
    check(d, "leading", 0, 0, 20, 30);
    check(d, "leading-anchor", 20, 30, 100, 50);
    printf("grid alignment: %d geometry cases passed\n", geometry_checks);
    return 0;
}
