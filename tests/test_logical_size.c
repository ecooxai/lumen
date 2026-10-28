/* Logical sizes act as the physical ones: chatgpt.com Pages gives every paragraph of a document
   flex:none and inline-size:100% inside a flex row, which must fill the row and wrap its text. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static Document *d;
static Box *box(const char *id) { Node *n = doc_get_element_by_id(d, id); assert(n && n->box); return n->box; }


/* The mapping basis is a cascaded prerequisite, not declaration-order state. */
static void cascade_case(const char *label, const char *parent, const char *inl,
                         const char *sheet, bool pseudo, int mode,
                         float min_width, float min_height) {
    Document *c = doc_new("https://logical-size.test/");
    SB html = {0};
    sb_printf(&html, "<html><body style='%s'><div id=x style='%s'></div></body></html>", parent, inl);
    html_parse(c, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(c);
    style_engine_add_sheet(e, css_parse_sheet(sheet, strlen(sheet), c->url, 1, &e->media));
    style_recalc(e, &c->node, true);
    Node *x = doc_get_element_by_id(c, "x");
    ComputedStyle *st = x && x->style ? (pseudo ? x->style->before : x->style) : NULL;
    bool ok = st && st->writing_mode == mode &&
        st->min_width.kind == LK_LEN && fabsf(st->min_width.px - min_width) < .01f &&
        st->min_height.kind == LK_LEN && fabsf(st->min_height.px - min_height) < .01f;
    if (!ok) fprintf(stderr, "%s: mode %d minimums %g/%g expected %d %g/%g\n", label,
        st ? st->writing_mode : -1, st ? st->min_width.px : -1, st ? st->min_height.px : -1,
        mode, min_width, min_height);
    assert(ok);
    style_engine_free(e); doc_free(c);
}
static void cascade_controls(void) {
    const char *dims = "#x{min-inline-size:31px;min-block-size:37px}";
    cascade_case("inline late mode", "", "min-inline-size:31px;min-block-size:37px;writing-mode:vertical-rl", "", false, 2, 37, 31);
    cascade_case("inline early mode", "", "writing-mode:vertical-rl;min-inline-size:31px;min-block-size:37px", "", false, 2, 37, 31);
    cascade_case("sheet before inline mode", "", "writing-mode:vertical-rl", dims, false, 2, 37, 31);
    cascade_case("sheet late mode", "", "", "#x{min-inline-size:31px;min-block-size:37px;writing-mode:vertical-lr}", false, 1, 37, 31);
    cascade_case("inherited vertical mode", "writing-mode:vertical-rl", "", dims, false, 2, 37, 31);
    cascade_case("explicit inherit wins", "writing-mode:vertical-rl", "writing-mode:inherit", "#x{writing-mode:horizontal-tb;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("unset wins", "writing-mode:vertical-rl", "writing-mode:unset", "#x{writing-mode:horizontal-tb;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("initial wins", "writing-mode:vertical-rl", "writing-mode:initial", dims, false, 0, 31, 37);
    cascade_case("important sheet mode", "", "writing-mode:horizontal-tb", "#x{min-inline-size:31px;min-block-size:37px;writing-mode:vertical-rl!important}", false, 2, 37, 31);
    cascade_case("variable mode", "", "--wm:vertical-rl;writing-mode:var(--wm)", dims, false, 2, 37, 31);
    cascade_case("physical later", "", "writing-mode:vertical-rl", "#x{min-inline-size:31px;min-block-size:37px;min-height:23px}", false, 2, 37, 23);
    cascade_case("logical later", "", "writing-mode:vertical-rl", "#x{min-height:23px;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("important logical", "", "writing-mode:vertical-rl;min-height:23px", "#x{min-inline-size:31px!important;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("important physical", "", "writing-mode:vertical-rl;min-inline-size:31px", "#x{min-height:23px!important;min-block-size:37px}", false, 2, 37, 23);
    cascade_case("pseudo own mode", "", "", "#x::before{content:'';min-inline-size:31px;min-block-size:37px;writing-mode:vertical-rl}", true, 2, 37, 31);
    cascade_case("pseudo inherited mode", "", "writing-mode:vertical-rl", "#x::before{content:'';min-inline-size:31px;min-block-size:37px}", true, 2, 37, 31);
    cascade_case("pseudo mode reset", "", "writing-mode:vertical-rl", "#x::before{content:'';min-inline-size:31px;min-block-size:37px;writing-mode:initial}", true, 0, 31, 37);
    cascade_case("invalid literal ignored", "", "writing-mode:bogus", "#x{writing-mode:vertical-rl;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("invalid vertical prefix ignored", "", "writing-mode:vertical-bogus", "#x{writing-mode:vertical-rl;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("invalid variable unsets", "writing-mode:vertical-rl", "--wm:bogus;writing-mode:var(--wm)", "#x{writing-mode:horizontal-tb;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("missing variable unsets", "writing-mode:vertical-rl", "writing-mode:var(--missing)", "#x{writing-mode:horizontal-tb;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("mixed case keyword", "", "writing-mode:VeRtIcAl-rL", dims, false, 2, 37, 31);
    puts("22 logical cascade controls passed");
}

int main(void) {
    dom_init(); font_init();
    cascade_controls();
    d = doc_new("https://logical-size.test/");
    const char *html = "<html><body>"
        "<div class=row><p id=p>A long paragraph of words that needs several lines inside a narrow column of text here.</p></div>"
        "<div id=b style='block-size:40px;inline-size:50%;min-block-size:60px;max-inline-size:100px'>x</div>"
        "</body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0;font:16px sans-serif}.row{display:flex;align-items:flex-start;width:200px}"
        ".row>*{flex:none;inline-size:100%;min-inline-size:0;margin-block:0}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 1080, 540);
    Box *p = box("p"), *b = box("b");
    fprintf(stderr, "p: w %g h %g; b: w %g h %g\n", p->w, p->h, b->w, b->h);
    assert(fabsf(p->w - 200) < .5f && p->h > 40);
    assert(fabsf(b->w - 100) < .5f && fabsf(b->h - 60) < .5f);
    char *w = css_get_computed_value(doc_get_element_by_id(d, "b"), "width");
    assert(!strcmp(w, "100px"));
    free(w);
    return 0;
}
