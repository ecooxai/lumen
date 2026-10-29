/* An anonymous block wrapping inline content does not take its parent's size (google.com's
   search overlay: a fixed height:100% box whose empty focus-sentinel span pushed the search
   field a whole viewport down). */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static Document *d;
static Box *box(const char *id) { Node *n = doc_get_element_by_id(d, id); assert(n && n->box); return n->box; }

int main(void) {
    dom_init(); font_init();
    d = doc_new("https://anon-block.test/");
    const char *html = "<html><body>"
        "<div class=ov id=ov><span tabindex=0></span><div class=row id=r1>field</div></div>"
        "<div class=box id=b2>text<div class=row id=r2>row</div>more</div>"
        "<div class=sq id=b3>text<div class=row id=r3>row</div></div>"
        "</body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0;font:16px/20px sans-serif}"
        ".ov{position:fixed;top:0;left:0;right:0;height:100%;overflow:auto}"
        ".row{display:flex;margin-top:4px;height:50px}"
        ".box{height:300px;min-height:200px;width:50%}"
        ".sq{width:100px;aspect-ratio:1/2;margin-top:400px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 1080, 414);
    Box *ov = box("ov"), *r1 = box("r1"), *b2 = box("b2"), *r2 = box("r2"), *b3 = box("b3"), *r3 = box("r3");
    fprintf(stderr, "overlay y %g h %g, row y %g\n", ov->y, ov->h, r1->y);
    fprintf(stderr, "box y %g h %g w %g, row y %g\n", b2->y, b2->h, b2->w, r2->y);
    fprintf(stderr, "square y %g h %g, row y %g\n", b3->y, b3->h, r3->y);
    assert(fabsf(ov->h - 414) < .5f);
    assert(r1->y - ov->y < 30);              /* the empty span's wrapper has no height of its own */
    assert(fabsf(b2->h - 300) < .5f && fabsf(b2->w - 540) < .5f);
    assert(r2->y - b2->y < 40);              /* one line of text, then the row */
    assert(fabsf(b3->h - 200) < .5f);
    assert(r3->y - b3->y < 40);
    for (Box *c = b2->first; c; c = c->next)
        if (c->anon) assert(c->h < 40 && fabsf(c->w - 540) < .5f);   /* fills the parent's width, one line tall */
    return 0;
}
