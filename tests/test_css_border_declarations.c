/* Shared physical border grammar, atomic application, and reset-only
 * border-image semantics. Geometry is checked through the real layout tree. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void require(bool condition, const char *name) {
    checks++; if (!condition) { fprintf(stderr, "%s\n", name); abort(); }
}
static void near(float actual, float expected, const char *name) {
    checks++; if (!isfinite(actual) || fabsf(actual - expected) > .02f) {
        fprintf(stderr, "%s: %g != %g\n", name, actual, expected); abort();
    }
}
static void computed(Node *node, const char *property, const char *expected) {
    char *value = css_get_computed_value(node, property);
    if (!value || strcmp(value, expected)) fprintf(stderr, "%s: %s != %s\n", property, value ? value : "(null)", expected);
    require(value && !strcmp(value, expected), "computed border value"); free(value);
}
static void refresh(Document *doc, StyleEngine *engine, Layout *layout) {
    style_recalc(engine, &doc->node, false);
    layout_run(layout, doc, engine->media.vw, engine->media.vh);
}
static void set_style(Node *node, const char *value, Document *doc, StyleEngine *engine, Layout *layout) {
    node_set_attr(node, "style", value); refresh(doc, engine, layout);
}
int main(void) {
    dom_init(); font_init();
    static const struct { const char *property, *value; } valid[] = {
        {"border", "5px dotted blue"}, {"border", "green double thin"},
        {"border-top", "thin"}, {"border-right", "double"}, {"border-bottom", "green"},
        {"border-left", "1px dotted red"}, {"border-width", "thin medium thick"},
        {"border-width", "1px 2px 3px 4px"}, {"border-width", "0"},
        {"border-style", "none hidden dotted dashed"}, {"border-style", "solid double groove ridge"},
        {"border-style", "inset outset"}, {"border-color", "red yellow green blue"},
        {"border-color", "currentColor"}, {"border-color", "rgb(1 2 3) #00f"},
        {"border", "1p\\78  so\\6c id r\\65 d"}, {"border", "1px/**/solid/**/red"},
        {"border", "calc(1px + 2px) solid red"}, {"border", "min(1px, 2px) dotted blue"},
        {"border-left-width", "2em"}, {"border-left-style", "DOTTED"},
        {"border-left-color", "color-mix(in srgb, red, blue)"},
        {"border", "initial"}, {"border-width", "inherit"}, {"border-top", "unset"}
    };
    for (unsigned i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        char *value = css_border_parse(valid[i].property, valid[i].value);
        require(value != NULL, valid[i].value); free(value);
        char condition[1024]; snprintf(condition, sizeof condition, "(%s:%s)", valid[i].property, valid[i].value);
        require(css_supports(condition), "valid border supports condition");
    }
    static const struct { const char *property, *value; } invalid[] = {
        {"border", "auto"}, {"border", "thin double green 1px"}, {"border", "-2em"},
        {"border", "2px solid color-mix(42deg)"}, {"border", "2px solid color-contrast(42deg)"},
        {"border", "inherit red"}, {"border", "1px initial"}, {"border", "1px red solid blue"},
        {"border-width", "none"}, {"border-width", "1px 2px 3px 4px 5px"},
        {"border-width", "10%"}, {"border-width", "calc(1px + 0%)"},
        {"border-width", "calc(1px/**/+/**/2px)"}, {"border-width", "-1px"},
        {"border-style", "auto"}, {"border-style", "solid double groove ridge none"},
        {"border-color", "auto"}, {"border-color", "black, white"},
        {"border-color", "black white red green blue"}, {"border-left", "1px solid red blue"},
        {"border-left-style", "solid dotted"}, {"border-left-width", "1px 2px"},
        {"border-left-color", "red blue"}, {"border", "var()"},
        {"border", "1p\\78 so\\6c id r\\65 d"}
    };
    ComputedStyle *style = style_new_default();
    css_apply_decl(style, NULL, "border", "5px solid blue", NULL, NULL);
    ComputedStyle before = *style;
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        char *value = css_border_parse(invalid[i].property, invalid[i].value);
        require(value == NULL, invalid[i].value); free(value);
        char condition[1024]; snprintf(condition, sizeof condition, "(%s:%s)", invalid[i].property, invalid[i].value);
        require(!css_supports(condition), "invalid border supports condition");
        css_apply_decl(style, NULL, invalid[i].property, invalid[i].value, NULL, NULL);
        require(!memcmp(style->border_width, before.border_width, sizeof style->border_width) &&
                !memcmp(style->border_style, before.border_style, sizeof style->border_style) &&
                !memcmp(style->border_color, before.border_color, sizeof style->border_color), "invalid application retains all physical components");
        char text[2048]; snprintf(text, sizeof text, "border:5px solid blue;color:green;%s:%s", invalid[i].property, invalid[i].value);
        DeclList *decls = css_parse_decls(text, strlen(text));
        require(decls && decls->n == 2, "invalid later declaration is discarded atomically"); css_decls_free(decls);
    }
    static const char *const pending[] = {"var(--B)", "env(border, 1px solid red)",
        "attr(data-border type(<length>), 1px) solid red", "if(else: 1px solid red)", "--Border()"};
    for (unsigned i = 0; i < sizeof pending / sizeof pending[0]; i++) {
        char *value = css_border_parse("border", pending[i]);
        require(value && !strcmp(value, pending[i]), "pending border token identity"); free(value);
    }
    style_free(style);

    Document *doc = doc_new("https://border-declarations.test/");
    const char *html = "<!doctype html><html><body><div id='parent'><div id='target'></div></div></body></html>";
    html_parse(doc, html, strlen(html)); StyleEngine *engine = style_engine_new(doc);
    engine->media = (MediaCtx){400, 300, 1, false};
    const char *sheet = "html,body{margin:0;padding:0}#target{width:100px;height:20px}";
    style_engine_add_sheet(engine, css_parse_sheet(sheet, strlen(sheet), doc->url, 1, &engine->media));
    Layout *layout = layout_new(); refresh(doc, engine, layout);
    Node *target = doc_get_element_by_id(doc, "target"), *parent = doc_get_element_by_id(doc, "parent");
    assert(target && parent && target->box);
    set_style(target, "border-image-source:linear-gradient(red,blue);border-image-slice:30%;border-image-width:2;border-image-outset:4;border-image-repeat:round;border:5px solid blue", doc, engine, layout);
    static const struct { const char *property, *initial; } image[] = {
        {"border-image-source", "none"}, {"border-image-slice", "100%"}, {"border-image-width", "1"},
        {"border-image-outset", "0"}, {"border-image-repeat", "stretch"}
    };
    for (unsigned i = 0; i < 5; i++) computed(target, image[i].property, image[i].initial);
    near(((Box *)target->box)->w, 110, "real border box width");
    near(((Box *)target->box)->h, 30, "real border box height");
    set_style(target, "border:5px solid blue;border:-2em", doc, engine, layout);
    near(((Box *)target->box)->w, 110, "invalid source retains earlier border");
    set_style(target, "border:5px solid blue;border-image-source:linear-gradient(red,blue);border:unset", doc, engine, layout);
    near(((Box *)target->box)->w, 100, "unset border uses initial physical values");
    for (unsigned i = 0; i < 5; i++) computed(target, image[i].property, image[i].initial);
    set_style(parent, "border:3px solid green;border-image-source:linear-gradient(red,blue);border-image-slice:30%", doc, engine, layout);
    set_style(target, "border:inherit", doc, engine, layout);
    near(((Box *)target->box)->w, 106, "inherited computed physical border");
    char *source = css_get_computed_value(parent, "border-image-source");
    computed(target, "border-image-source", source); free(source);
    computed(target, "border-image-slice", "30%");
    set_style(target, "--B:5px solid blue;border:var(--B)", doc, engine, layout);
    near(((Box *)target->box)->w, 110, "valid substituted border reaches layout");
    set_style(target, "border:5px solid blue;--B:-2em;border:var(--B)", doc, engine, layout);
    near(((Box *)target->box)->w, 100, "invalid substitution does not resurrect earlier border");
    for (unsigned i = 0; i < 5; i++) computed(target, image[i].property, image[i].initial);
    require(css_border_parse("border", NULL) == NULL, "null value");
    require(css_border_valid("box-shadow", "unrelated-validator-input"), "unrelated properties retain their own parser");
    /* The original181 checks above remain unchanged. Exercise the actual
       noninitial image value and each newly reached all reset path. */
    assert(checks == 181);
    set_style(target, "border-image-width:7", doc, engine, layout);
    computed(target, "border-image-width", "7");
    set_style(parent, "border:2px dotted green;border-image-width:9", doc, engine, layout);
    set_style(target, "border:8px solid blue;border-image-width:7;all:initial;border-style:solid;width:100px;height:20px;display:block", doc, engine, layout);
    near(((Box *)target->box)->w, 106, "all initial restores real medium side widths");
    near(((Box *)target->box)->h, 26, "all initial restores top and bottom widths");
    computed(target, "border-image-width", "1");
    set_style(target, "border:8px solid blue;border-image-width:7;all:unset;border-style:solid;width:100px;height:20px;display:block", doc, engine, layout);
    near(((Box *)target->box)->w, 106, "all unset does not inherit noninherited border widths");
    near(((Box *)target->box)->h, 26, "all unset restores top and bottom widths");
    computed(target, "border-image-width", "1");
    set_style(target, "border:8px solid blue;border-image-width:7;all:inherit;width:100px;height:20px;display:block", doc, engine, layout);
    near(((Box *)target->box)->w, 104, "all inherit copies real parent side widths");
    near(((Box *)target->box)->h, 24, "all inherit copies real parent top and bottom widths");
    computed(target, "border-image-width", "9");
    assert(checks == 191);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS css_border_declarations %u native checks\n", checks);
    return 0;
}
