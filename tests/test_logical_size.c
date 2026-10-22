/* Logical sizes act as the physical ones: chatgpt.com Pages gives every paragraph of a document
   flex:none and inline-size:100% inside a flex row, which must fill the row and wrap its text. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static Document *d;
static Box *box(const char *id) { Node *n = doc_get_element_by_id(d, id); assert(n && n->box); return n->box; }

int main(void) {
    dom_init(); font_init();
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
