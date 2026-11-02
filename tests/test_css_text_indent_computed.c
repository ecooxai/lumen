/* Computed text-indent resolves relative lengths in the declaration's own
 * style context; inheritance copies that computed value to other fonts. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void computed(Node *node, const char *expected) {
    char *actual = css_get_computed_value(node, "text-indent");
    if (strcmp(actual, expected)) fprintf(stderr, "%s computed text-indent '%s', expected '%s'\n", node_attr(node, "id"), actual, expected);
    assert(!strcmp(actual, expected)); free(actual); checks++;
    actual = css_get_animation_value(node, "text-indent");
    assert(!strcmp(actual, expected)); free(actual); checks++;
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://text-indent-computed.test/");
    const char *html = "<html><body><div id='probe'></div><div id='parent'><div id='child'></div></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc);
    const char *css = "html{font-size:16px}body{margin:0}#probe{font-size:16px}"
        "#parent{font-size:20px;text-indent:2em}#child{font-size:10px}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    style_recalc(engine, &doc->node, true);
    Layout *layout = layout_new(); layout_run(layout, doc, 640, 480);
    Node *probe = doc_get_element_by_id(doc, "probe"), *parent = doc_get_element_by_id(doc, "parent"), *child = doc_get_element_by_id(doc, "child");
    computed(parent, "40px"); computed(child, "40px");
    const char *const cases[][2] = {
        {"0", "0px"}, {"1em", "16px"}, {"min(1in)", "96px"},
        {"max(1px, 2px)", "2px"}, {"min(1px, 1em)", "1px"},
        {"calc(1px + max(1in, 100px))", "101px"},
        {"calc(min(1px, 1in) + max(100px + 1em, 10px + 1in) + 1px)", "118px"},
        {"calc(2 * (.2 * min(1em, 1px)) + 1px)", "1.4px"},
        {"calc(-2em)", "-32px"}, {"calc(1rem + 1em)", "32px"},
        {"25%", "25%"}, {"calc(0% + 10px)", "calc(0% + 10px)"},
        {"10px hanging", "10px hanging"}, {"10px each-line", "10px each-line"}
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        char declaration[512]; snprintf(declaration, sizeof declaration, "text-indent:%s", cases[i][0]);
        node_set_attr(probe, "style", declaration);
        style_recalc(engine, &doc->node, false);
        computed(probe, cases[i][1]);
    }
    node_set_attr(parent, "style", "font-size:30px");
    style_recalc(engine, &doc->node, false);
    computed(parent, "60px"); computed(child, "60px");
    node_set_attr(child, "style", "font-size:50px;text-indent:inherit");
    style_recalc(engine, &doc->node, false); computed(child, "60px");
    node_set_attr(child, "style", "font-size:50px;text-indent:unset");
    style_recalc(engine, &doc->node, false); computed(child, "60px");
    node_set_attr(child, "style", "font-size:50px;text-indent:initial");
    style_recalc(engine, &doc->node, false); computed(child, "0px");
    node_set_attr(child, "style", "font-size:50px;text-indent:1em");
    style_recalc(engine, &doc->node, false); computed(child, "50px");
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS css_text_indent_computed %u native checks\n", checks);
    return 0;
}
