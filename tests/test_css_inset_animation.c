/* Animation and transition snapshots retain computed inset values, while
 * public CSSOM obtains resolved values from the current native layout. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, callbacks;
static const char *transition_from, *transition_to;
static void equal_value(Node *node, const char *prop, const char *expected, bool animation) {
    char *actual = animation ? css_get_animation_value(node, prop) : css_get_computed_value(node, prop);
    if (strcmp(actual, expected)) fprintf(stderr, "%s %s: got '%s', expected '%s'\n", animation ? "animation" : "CSSOM", prop, actual, expected);
    assert(!strcmp(actual, expected)); free(actual); checks++;
}
static void transition(Node *node, const char *prop, const char *from, const char *to) {
    assert(node && !strcmp(prop, "left"));
    if (strcmp(from, transition_from) || strcmp(to, transition_to))
        fprintf(stderr, "transition left: '%s' -> '%s', expected '%s' -> '%s'\n", from, to, transition_from, transition_to);
    assert(!strcmp(from, transition_from) && !strcmp(to, transition_to)); callbacks++; checks++;
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://inset-animation.test/");
    const char *html = "<html><body><div id='cb'><div id='target'></div></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc);
    const char *css = "body{margin:0}#cb{position:relative;width:200px;height:100px}"
        "#target{position:absolute;width:20px;height:10px;left:25%;right:auto;top:auto;bottom:8px}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    style_recalc(engine, &doc->node, true);
    Layout *layout = layout_new(); layout_run(layout, doc, 640, 480);
    Node *target = doc_get_element_by_id(doc, "target");
    assert(target && target->box);
    equal_value(target, "left", "50px", false);
    equal_value(target, "right", "130px", false);
    equal_value(target, "top", "82px", false);
    equal_value(target, "left", "25%", true);
    equal_value(target, "right", "auto", true);
    equal_value(target, "top", "auto", true);
    equal_value(target, "inset-inline-start", "25%", true);
    equal_value(target, "inset-inline-end", "auto", true);
    equal_value(target, "inset-inline", "25% auto", true);
    equal_value(target, "inset-block", "auto 8px", true);
    equal_value(target, "inset", "auto auto 8px 25%", true);

    /* A different containing block size changes public used pixels, never the
       computed percentages or automatic values consumed by animation. */
    node_set_attr(doc_get_element_by_id(doc, "cb"), "style", "width:400px;height:160px");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, 640, 480);
    equal_value(target, "left", "100px", false);
    equal_value(target, "right", "280px", false);
    equal_value(target, "top", "142px", false);
    equal_value(target, "left", "25%", true);
    equal_value(target, "top", "auto", true);
    equal_value(target, "inset-inline", "25% auto", true);

    node_set_attr(target, "style", "writing-mode:vertical-rl;direction:rtl");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, 640, 480);
    equal_value(target, "inset-block-start", "auto", true);
    equal_value(target, "inset-block-end", "25%", true);
    equal_value(target, "inset-inline-start", "8px", true);
    equal_value(target, "inset-inline-end", "auto", true);
    equal_value(target, "inset-block", "auto 25%", true);
    equal_value(target, "inset-inline", "8px auto", true);

    /* Transition snapshots do not necessarily match the node's current style.
       Their values must remain independent of its current box and layout. */
    ComputedStyle *old = style_new_default(), *next = style_new_default();
    css_apply_decl(old, NULL, "position", "absolute", NULL, NULL);
    css_apply_decl(next, NULL, "position", "absolute", NULL, NULL);
    css_apply_decl(old, NULL, "left", "25%", NULL, NULL);
    css_apply_decl(next, NULL, "left", "40%", NULL, NULL);
    css_apply_decl(next, NULL, "transition-property", "left", NULL, NULL);
    transition_from = "25%"; transition_to = "40%";
    css_transition_scan(target, old, next, transition); assert(callbacks == 1);
    css_apply_decl(old, NULL, "left", "auto", NULL, NULL);
    css_apply_decl(next, NULL, "left", "20px", NULL, NULL);
    transition_from = "auto"; transition_to = "20px";
    css_transition_scan(target, old, next, transition); assert(callbacks == 2);
    style_free(old); style_free(next);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS css_inset_animation %u native checks\n", checks);
    return 0;
}
