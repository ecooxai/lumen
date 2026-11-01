/* Viewport dimensions and flow axes are independent inputs. These controls
 * exercise computed style, actual boxes, inheritance and incremental resize. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void near(const char *name, float actual, float expected) {
    checks++;
    if (!isfinite(actual) || fabsf(actual - expected) > .02f) {
        fprintf(stderr, "%s: %g, expected %g\n", name, actual, expected); abort();
    }
}
static void yes(const char *name, bool actual) {
    checks++; if (!actual) { fprintf(stderr, "%s\n", name); abort(); }
}
static void serialized(Node *node, const char *prop, const char *expected) {
    char *value = css_get_computed_value(node, prop);
    if (!value || strcmp(value, expected)) fprintf(stderr, "%s: %s, expected %s\n", prop, value ? value : "(null)", expected);
    yes(prop, value && !strcmp(value, expected)); free(value);
}
static void refresh(StyleEngine *engine, Layout *layout, Document *doc) {
    style_recalc(engine, &doc->node, false);
    layout_run(layout, doc, engine->media.vw, engine->media.vh);
}
static void set_style(Node *node, const char *style, StyleEngine *engine, Layout *layout, Document *doc) {
    node_set_attr(node, "style", style); refresh(engine, layout, doc);
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://viewport-units.test/");
    const char *html = "<!doctype html><html><body><div id='target'></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc);
    engine->media = (MediaCtx){400, 200, 1, false};
    const char *sheet = "html,body{margin:0;padding:0}#target{position:absolute;left:0;top:0;width:1px;height:10px}";
    style_engine_add_sheet(engine, css_parse_sheet(sheet, strlen(sheet), doc->url, 1, &engine->media));
    Layout *layout = layout_new(); refresh(engine, layout, doc);
    Node *target = doc_get_element_by_id(doc, "target"); assert(target && target->style && target->box);
    static const char *prefixes[] = {"", "s", "l", "d"};
    static const struct { const char *unit; float horizontal, vertical; } units[] = {
        {"vw",400,400}, {"vh",200,200}, {"vi",400,200},
        {"vb",200,400}, {"vmin",200,200}, {"vmax",400,400}
    };
    char text[192];
    for (unsigned p = 0; p < 4; p++) for (unsigned u = 0; u < 6; u++) for (unsigned vertical = 0; vertical < 2; vertical++) {
        snprintf(text, sizeof text, "width:25%s%s;writing-mode:%s", prefixes[p], units[u].unit, vertical ? "vertical-rl" : "horizontal-tb");
        set_style(target, text, engine, layout, doc);
        float expected = .25f * (vertical ? units[u].vertical : units[u].horizontal);
        near(text, target->style->width.px, expected);
        near("actual laid-out viewport width", ((Box *)target->box)->w, expected);
    }
    /* Uppercase spelling and strict unit boundaries use the same native parser. */
    Length parsed;
    yes("uppercase viewport unit", css_parse_length("25DvMiN", &parsed, 16, 16, &engine->media));
    near("uppercase computed dimension", parsed.px, 50);
    static const char *invalid[] = {"10xvi", "10ssvw", "10dvminimum", "10v", "10vii"};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++)
        yes(invalid[i], !css_parse_length(invalid[i], &parsed, 16, 16, &engine->media));

    /* Height-only resize must invalidate the logical height axis even if the
       property is physical width, without inventing a DOM mutation. */
    set_style(target, "width:calc(25dvi + 10px);writing-mode:vertical-rl", engine, layout, doc);
    near("vertical inline axis before resize", ((Box *)target->box)->w, 60);
    yes("vertical vi records height dependency", target->style->uses_vh);
    uint64_t version = doc->dom_version;
    engine->media.vh = 300; refresh(engine, layout, doc);
    yes("resize leaves DOM version unchanged", doc->dom_version == version);
    near("vertical inline axis after height-only resize", ((Box *)target->box)->w, 85);
    set_style(target, "width:25svb;writing-mode:horizontal-tb", engine, layout, doc);
    near("horizontal block axis", ((Box *)target->box)->w, 75);
    engine->media.vh = 200; refresh(engine, layout, doc);
    near("horizontal vb height-only resize", ((Box *)target->box)->w, 50);
    set_style(target, "width:25lvb;writing-mode:vertical-lr", engine, layout, doc);
    near("vertical block axis is viewport width", ((Box *)target->box)->w, 100);
    yes("vertical vb has no height dependency", !target->style->uses_vh);
    engine->media.vh = 300; refresh(engine, layout, doc);
    near("vertical vb stays fixed on height-only resize", ((Box *)target->box)->w, 100);

    /* min/max must track both axes across the point where the smaller side changes. */
    set_style(target, "width:100svmin;height:100dvmax;writing-mode:horizontal-tb", engine, layout, doc);
    near("minimum before crossover", ((Box *)target->box)->w, 300);
    near("maximum before crossover", ((Box *)target->box)->h, 400);
    engine->media.vh = 500; refresh(engine, layout, doc);
    near("minimum after crossover", ((Box *)target->box)->w, 400);
    near("maximum after crossover", ((Box *)target->box)->h, 500);
    engine->media.vh = 200; refresh(engine, layout, doc);

    /* A root mode does not override the target's explicit mode; an inherited
       change does update a target that has no mode declaration. */
    set_style(target, "width:25vi;writing-mode:initial", engine, layout, doc);
    set_style(doc->html, "writing-mode:vertical-rl", engine, layout, doc);
    near("own initial mode ignores vertical root", ((Box *)target->box)->w, 100);
    set_style(doc->html, "writing-mode:horizontal-tb", engine, layout, doc);
    set_style(target, "width:25vi", engine, layout, doc);
    set_style(doc->body, "writing-mode:vertical-lr", engine, layout, doc);
    near("inherited vertical mode", ((Box *)target->box)->w, 50);
    set_style(doc->body, "writing-mode:horizontal-tb", engine, layout, doc);
    near("inherited horizontal mode update", ((Box *)target->box)->w, 100);

    /* Font metrics use the parent, but viewport axes still use the element. */
    set_style(doc->body, "font-size:20px;writing-mode:horizontal-tb", engine, layout, doc);
    set_style(target, "font-size:calc(10vi);width:2em;writing-mode:vertical-rl", engine, layout, doc);
    near("vertical own font vi", target->style->font_size, 20);
    near("length follows computed viewport font", ((Box *)target->box)->w, 40);
    set_style(target, "font-size:10vb;width:2em;writing-mode:vertical-lr", engine, layout, doc);
    near("vertical own font vb", target->style->font_size, 40);
    set_style(doc->body, "font-size:20px;writing-mode:vertical-rl", engine, layout, doc);
    set_style(target, "font-size:calc(1em + 10vi);width:2em;writing-mode:horizontal-tb", engine, layout, doc);
    near("parent em plus own horizontal viewport axis", target->style->font_size, 60);
    near("combined font basis actual geometry", ((Box *)target->box)->w, 120);

    /* Context-free media queries keep initial horizontal axes even while the
       document and target use vertical modes. */
    set_style(doc->html, "writing-mode:vertical-rl", engine, layout, doc);
    yes("media inline axis is width", css_media_matches("(width:100vi)", &engine->media));
    yes("media block axis is height", css_media_matches("(height:100dvb)", &engine->media));
    yes("media query rejects wrong axis", !css_media_matches("(width:100svb)", &engine->media));
    yes("media min alias", css_media_matches("(height:100lvmin)", &engine->media));

    set_style(target, "background-position:10dvi 20svb;writing-mode:vertical-rl", engine, layout, doc);
    serialized(target, "background-position-x", "20px");
    serialized(target, "background-position-y", "80px");
    set_style(target, "width:10vi;writing-mode:sideways-rl", engine, layout, doc);
    near("sideways computed inline axis", target->style->width.px, 20);
    set_style(target, "width:10vb;writing-mode:sideways-lr", engine, layout, doc);
    near("sideways computed block axis", target->style->width.px, 40);

    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS css_viewport_units %u native checks\n", checks);
    return 0;
}
