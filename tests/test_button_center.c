/* A <button>'s content is centred vertically when the button is taller than it, as in Chrome
   (google.com's mic button: an inline-block svg in a 48px display:block button). */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static Document *d;
static Box *box(const char *id) { Node *n = doc_get_element_by_id(d, id); assert(n && n->box); return n->box; }

int main(void) {
    dom_init(); font_init();
    d = doc_new("https://button-center.test/");
    const char *html = "<html><body>"
        "<button id=b1><svg id=s1 width=24 height=24></svg></button>"
        "<button id=b2><span id=t2>text</span></button>"
        "<button id=b3><svg id=s3 width=24 height=24></svg></button>"
        "<div id=d4><svg id=s4 width=24 height=24></svg></div>"
        "</body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0;font:16px/20px sans-serif}"
        "button{display:block;height:48px;width:48px;padding:0;border:0;margin:0}"
        "svg{display:inline-block;vertical-align:middle}"
        "#b2{height:auto;min-height:60px;width:100px;line-height:20px}"
        "#b3{display:flex}"
        "#d4{height:48px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 400, 400);
    Box *b1 = box("b1"), *s1 = box("s1"), *b2 = box("b2"), *t2 = box("t2"), *b3 = box("b3"), *s3 = box("s3"), *d4 = box("d4"), *s4 = box("s4");
    fprintf(stderr, "b1 y %g h %g, svg y %g h %g\n", b1->y, b1->h, s1->y, s1->h);
    fprintf(stderr, "b2 y %g h %g, span y %g h %g\n", b2->y, b2->h, t2->y, t2->h);
    fprintf(stderr, "b3 y %g, svg y %g; div y %g, svg y %g\n", b3->y, s3->y, d4->y, s4->y);
    assert(fabsf(b1->h - 48) < .5f);
    float mid1 = s1->y + s1->h / 2 - b1->y;
    assert(fabsf(mid1 - 24) < 3);           /* the svg's line is centred, not at the top */
    assert(fabsf(b2->h - 60) < .5f);
    float mid2 = t2->y + t2->h / 2 - b2->y;
    assert(fabsf(mid2 - 30) < 3);           /* min-height too */
    assert(s3->y - b3->y < 1);              /* a flex button follows flex alignment (stretch, top) */
    assert(s4->y - d4->y < 12);             /* a plain block is not centred */
    return 0;
}
