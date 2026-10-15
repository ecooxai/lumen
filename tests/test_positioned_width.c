#include "layout/layout.h"
#include <math.h>

static int failures;
static void expect(Document *d, const char *id, float x, float width) {
    Node *n = doc_get_element_by_id(d, id);
    Box *b = n ? n->box : NULL;
    if (!b || fabsf(b->x - x) > .02f || fabsf(b->w - width) > .02f) {
        fprintf(stderr, "%s: expected x=%.2f width=%.2f, got %.2f %.2f\n", id, x, width, b ? b->x : -999, b ? b->w : -999);
        failures++;
    }
}
int main(void) {
    dom_init(); font_init();
    Document *d = doc_new("http://fixture.test/");
    const char *html = "<body><div id=drawer></div><div id=positive></div><div id=right></div>"
        "<div id=both></div><div id=padded></div><div id=auto></div><div id=container><div id=nested></div></div></body>";
    html_parse(d, html, strlen(html));
    const char *css = "body{margin:0}body>div{position:fixed;top:0;height:10px}"
        "#drawer{width:70%;left:-100%}#positive{width:70%;left:20%}#right{width:70%;right:20%}"
        "#both{width:70%;left:10%;right:10%}#padded{width:50%;left:20%;padding:0 5%;margin-left:5%;max-width:60%}"
        "#auto{left:10%;right:20%}#container{position:relative;width:400px;height:20px}"
        "#nested{position:absolute;left:-100%;width:70%;height:10px}";
    StyleEngine *engine = style_engine_new(d);
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), d->url, 1, &engine->media));
    style_recalc(engine, &d->node, true);
    Layout *layout = layout_new();
    for (int i = 0; i < 2; i++) {
        float vw = i ? 800 : 540;
        layout_run(layout, d, vw, 984);
        expect(d, "drawer", -vw, .7f * vw);
        expect(d, "positive", .2f * vw, .7f * vw);
        expect(d, "right", .1f * vw, .7f * vw);
        expect(d, "both", .1f * vw, .7f * vw);
        expect(d, "padded", .25f * vw, .6f * vw);
        expect(d, "auto", .1f * vw, .7f * vw);
        expect(d, "nested", -400, 280);
    }
    layout_free(layout); style_engine_free(engine); doc_free(d);
    if (!failures) puts("positioned percentage widths pass at both viewport sizes");
    return failures ? 1 : 0;
}
