/* border-radius: inherit / initial, for the shorthand and each corner (google.com's search box is a
   white inner box with border-radius: inherit inside a 26px-rounded one; ignored, it painted square). */
#include "../src/css/css.h"
#include <assert.h>
#include <stdio.h>

static Document *d;
static const ComputedStyle *st(const char *id) { Node *n = doc_get_element_by_id(d, id); assert(n && n->style); return n->style; }

int main(void) {
    dom_init();
    d = doc_new("https://radius-inherit.test/");
    const char *html = "<html><body><div id=p><div id=a><div id=c></div></div><div id=b></div><div id=i></div></div></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "#p{border-radius:26px 26px 0 0}"
        "#a{border-radius:inherit}"
        "#c{border-radius:4px;border-top-right-radius:inherit}"
        "#b{border-radius:9px;border-bottom-left-radius:inherit}"
        "#i{border-radius:9px;border-radius:initial}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    const ComputedStyle *a = st("a"), *c = st("c"), *b = st("b"), *i = st("i");
    fprintf(stderr, "a %g %g %g %g, c %g %g, b %g %g, i %g\n", a->border_radius[0].px, a->border_radius[1].px, a->border_radius[2].px,
            a->border_radius[3].px, c->border_radius[0].px, c->border_radius[1].px, b->border_radius[0].px, b->border_radius[3].px, i->border_radius[0].px);
    assert(a->border_radius[0].px == 26 && a->border_radius[1].px == 26 && a->border_radius[2].px == 0 && a->border_radius[3].px == 0);
    assert(c->border_radius[0].px == 4 && c->border_radius[1].px == 26 && c->border_radius[2].px == 4);
    assert(b->border_radius[0].px == 9 && b->border_radius[3].px == 0);
    assert(i->border_radius[0].px == 0 && i->border_radius[2].px == 0);
    return 0;
}
