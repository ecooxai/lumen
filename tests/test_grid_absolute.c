/* CSS Grid 9.1 numeric-line containing blocks. Expected geometry was
   independently measured in Chromium 150 on 2026-10-29, before this repair. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static unsigned checks;
static void check(Document *d, const char *id, float x, float y, float w, float h) {
    char parent_id[96]; snprintf(parent_id, sizeof parent_id, "cb-%s", id);
    Node *n = doc_get_element_by_id(d, id), *p = doc_get_element_by_id(d, parent_id);
    assert(n && n->box && p && p->box);
    float actual[] = {n->box->x - p->box->x, n->box->y - p->box->y, n->box->w, n->box->h};
    float expected[] = {x, y, w, h};
    for (int i = 0; i < 4; ++i) {
        ++checks;
        if (fabsf(actual[i] - expected[i]) > .05f)
            fprintf(stderr, "%s component %d: expected %g, got %g\n", id, i, expected[i], actual[i]);
        assert(fabsf(actual[i] - expected[i]) <= .05f);
    }
}
int main(void) {
    dom_init(); font_init();
    Document *d = doc_new("https://grid-absolute.test/");
    const char *html =
        "<html><body>"
        "<div class=\"grid\" id=\"cb-ordinary\" style=\"\"><div id=\"ordinary\" class=\"abs\" style=\"grid-column:2 / 3;grid-row:2 / 3\"></div></div>"
        "<div class=\"grid\" id=\"cb-nested\" style=\"\"><div><div id=\"nested\" class=\"abs\" style=\"grid-column:2 / 3;grid-row:2 / 3\"></div></div></div>"
        "<div class=\"grid\" id=\"cb-all-auto\" style=\"\"><div id=\"all-auto\" class=\"abs\" style=\"grid-column:auto;grid-row:auto\"></div></div>"
        "<div class=\"grid\" id=\"cb-first-track\" style=\"\"><div id=\"first-track\" class=\"abs\" style=\"grid-column:1 / 2;grid-row:1 / 2\"></div></div>"
        "<div class=\"grid\" id=\"cb-auto-start\" style=\"\"><div id=\"auto-start\" class=\"abs\" style=\"grid-column:auto / 2;grid-row:auto / 2\"></div></div>"
        "<div class=\"grid\" id=\"cb-auto-end\" style=\"\"><div id=\"auto-end\" class=\"abs\" style=\"grid-column:2 / auto;grid-row:2 / auto\"></div></div>"
        "<div class=\"grid\" id=\"cb-missing-line\" style=\"\"><div id=\"missing-line\" class=\"abs\" style=\"grid-column:10 / auto;grid-row:10 / auto\"></div></div>"
        "<div class=\"grid\" id=\"cb-reversed\" style=\"\"><div id=\"reversed\" class=\"abs\" style=\"grid-column:3 / 1;grid-row:3 / 1\"></div></div>"
        "<div class=\"grid\" id=\"cb-equal\" style=\"\"><div id=\"equal\" class=\"abs\" style=\"grid-column:2 / 2;grid-row:2 / 2\"></div></div>"
        "<div class=\"grid\" id=\"cb-negative\" style=\"\"><div id=\"negative\" class=\"abs\" style=\"grid-column:-3 / -1;grid-row:-3 / -1\"></div></div>"
        "<div class=\"grid\" id=\"cb-start-span\" style=\"\"><div id=\"start-span\" class=\"abs\" style=\"grid-column:span 2 / 3;grid-row:span 2 / 3\"></div></div>"
        "<div class=\"grid\" id=\"cb-end-span\" style=\"\"><div id=\"end-span\" class=\"abs\" style=\"grid-column:1 / span 2;grid-row:1 / span 2\"></div></div>"
        "<div class=\"grid\" id=\"cb-span-outside\" style=\"\"><div id=\"span-outside\" class=\"abs\" style=\"grid-column:2 / span 2;grid-row:2 / span 2\"></div></div>"
        "<div class=\"grid\" id=\"cb-lone-span\" style=\"\"><div id=\"lone-span\" class=\"abs\" style=\"grid-column:span 2 / auto;grid-row:span 2 / auto\"></div></div>"
        "<div class=\"grid\" id=\"cb-missing-span\" style=\"\"><div id=\"missing-span\" class=\"abs\" style=\"grid-column:span 2 / 10;grid-row:span 2 / 10\"></div></div>"
        "<div class=\"grid\" id=\"cb-reversed-missing\" style=\"\"><div id=\"reversed-missing\" class=\"abs\" style=\"grid-column:10 / 2;grid-row:10 / 2\"></div></div>"
        "<div class=\"grid\" id=\"cb-rtl\" style=\"direction:rtl;\"><div id=\"rtl\" class=\"abs\" style=\"grid-column:2 / 3;grid-row:2 / 3\"></div></div>"
        "<div class=\"grid\" id=\"cb-rtl-auto-start\" style=\"direction:rtl;\"><div id=\"rtl-auto-start\" class=\"abs\" style=\"grid-column:auto / 2;grid-row:auto / 2\"></div></div>"
        "<div class=\"grid\" id=\"cb-center-end\" style=\"justify-content:center;align-content:end;\"><div id=\"center-end\" class=\"abs\" style=\"grid-column:2 / 3;grid-row:2 / 3\"></div></div>"
        "<div class=\"grid\" id=\"cb-distributed\" style=\"justify-content:space-between;align-content:space-around;\"><div id=\"distributed\" class=\"abs\" style=\"grid-column:2 / 3;grid-row:2 / 3\"></div></div>"
        "<div class=\"grid\" id=\"cb-implicit-after\" style=\"grid-auto-columns:50px;grid-auto-rows:30px;\"><i style=\"grid-column:4;grid-row:4\"></i><div id=\"implicit-after\" class=\"abs\" style=\"grid-column:3 / 4;grid-row:3 / 4\"></div></div>"
        "<div class=\"grid\" id=\"cb-implicit-before\" style=\"grid-auto-columns:50px;grid-auto-rows:30px;\"><i style=\"grid-column:-4;grid-row:-4\"></i><div id=\"implicit-before\" class=\"abs\" style=\"grid-column:1 / -1;grid-row:1 / -1\"></div></div>"
        "<div class=\"grid\" id=\"cb-overflow-auto\" style=\"grid-template-columns:350px 250px;grid-template-rows:250px 200px;\"><div id=\"overflow-auto\" class=\"abs\" style=\"grid-column:3 / auto;grid-row:3 / auto\"></div></div>"
        "<div class=\"grid\" id=\"cb-non-grid\" style=\"display:block;\"><div id=\"non-grid\" class=\"abs\" style=\"grid-column:2 / 3;grid-row:2 / 3\"></div></div>"
        "</body></html>";
    const char *css = "body{margin:0}.grid{display:grid;position:relative;width:400px;height:300px;border-style:solid;border-width:11px 13px 17px 7px;padding:20px 30px 40px 10px;grid-template-columns:100px 120px;grid-template-rows:60px 80px;column-gap:15px;row-gap:10px;margin:0}.abs{position:absolute;left:0;top:0;right:0;bottom:0}";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 800, 2000);
    check(d, "ordinary", 132, 101, 120, 80);
    check(d, "nested", 132, 101, 120, 80);
    check(d, "all-auto", 7, 11, 440, 360);
    check(d, "first-track", 17, 31, 100, 60);
    check(d, "auto-start", 7, 11, 110, 80);
    check(d, "auto-end", 132, 101, 315, 270);
    check(d, "missing-line", 7, 11, 440, 360);
    check(d, "reversed", 17, 31, 235, 150);
    check(d, "equal", 132, 101, 120, 80);
    check(d, "negative", 17, 31, 235, 150);
    check(d, "start-span", 17, 31, 235, 150);
    check(d, "end-span", 17, 31, 235, 150);
    check(d, "span-outside", 132, 101, 315, 270);
    check(d, "lone-span", 7, 11, 440, 360);
    check(d, "missing-span", 7, 11, 440, 360);
    check(d, "reversed-missing", 132, 101, 315, 270);
    check(d, "rtl", 182, 101, 120, 80);
    check(d, "rtl-auto-start", 317, 11, 130, 80);
    check(d, "center-end", 214.5, 251, 120, 80);
    check(d, "distributed", 297, 213.5, 120, 80);
    check(d, "implicit-after", 267, 191, 50, 30);
    check(d, "implicit-before", 82, 71, 235, 150);
    check(d, "overflow-auto", 632, 491, 0, 0);
    check(d, "non-grid", 7, 11, 440, 360);
    layout_free(l); style_engine_free(e); doc_free(d);
    printf("Grid absolute containing-block controls: %u/96 passed\n", checks);
    return 0;
}
