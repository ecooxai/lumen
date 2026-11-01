/* A rendered element has one current box. Transition old/new size snapshots
 * must compare their own computed values and types, not that shared geometry. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
struct Expected { const char *name, *from, *to; unsigned seen; };
static struct Expected *expected;
static unsigned expected_count;
static void changed(Node *node, const char *property, const char *from, const char *to) {
    assert(node && node->box);
    for (unsigned i = 0; i < expected_count; i++) if (!strcmp(property, expected[i].name)) {
        if (strcmp(from, expected[i].from) || strcmp(to, expected[i].to))
            fprintf(stderr, "%s transition '%s' -> '%s', expected '%s' -> '%s'\n", property, from, to, expected[i].from, expected[i].to);
        assert(!strcmp(from, expected[i].from) && !strcmp(to, expected[i].to));
        expected[i].seen++; checks++; return;
    }
    fprintf(stderr, "unexpected transition %s: %s -> %s\n", property, from, to); assert(0);
}
static void scan(Node *node, ComputedStyle *before, ComputedStyle *after, struct Expected *items, unsigned count) {
    expected = items; expected_count = count;
    for (unsigned i = 0; i < count; i++) items[i].seen = 0;
    css_transition_scan(node, before, after, changed);
    for (unsigned i = 0; i < count; i++) {
        if (items[i].seen != 1) fprintf(stderr, "%s emitted %u transitions, expected one\n", items[i].name, items[i].seen);
        assert(items[i].seen == 1); checks++;
    }
}
static void value(Node *node, const char *property, const char *wanted, bool animation) {
    char *actual = animation ? css_get_animation_value(node, property) : css_get_computed_value(node, property);
    if (strcmp(actual, wanted)) fprintf(stderr, "%s %s: '%s', expected '%s'\n", animation ? "animation" : "CSSOM", property, actual, wanted);
    assert(!strcmp(actual, wanted)); free(actual); checks++;
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://transition-size-snapshot.test/");
    const char *html = "<html><body><div id='target'></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc);
    const char *css = "body{margin:0}#target{width:10px;height:12px;opacity:0.2}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    style_recalc(engine, &doc->node, true);
    Layout *layout = layout_new(); layout_run(layout, doc, 640, 480);
    Node *target = doc_get_element_by_id(doc, "target"); assert(target && target->box);
    value(target, "width", "10px", false); value(target, "height", "12px", false);
    ComputedStyle *before = style_new_default(), *after = style_new_default();
    css_apply_decl(before, NULL, "width", "10px", NULL, NULL);
    css_apply_decl(before, NULL, "height", "12px", NULL, NULL);
    css_apply_decl(before, NULL, "opacity", "0.2", NULL, NULL);
    css_apply_decl(after, NULL, "width", "20px", NULL, NULL);
    css_apply_decl(after, NULL, "height", "24px", NULL, NULL);
    css_apply_decl(after, NULL, "opacity", "0.4", NULL, NULL);
    css_apply_decl(after, NULL, "transition-property", "width, height, opacity", NULL, NULL);
    struct Expected physical[] = {{"width", "10px", "20px", 0}, {"height", "12px", "24px", 0}, {"opacity", "0.2", "0.4", 0}};
    scan(target, before, after, physical, 3);
    /* Identical snapshots must not invent a transition from box geometry. */
    scan(target, after, after, NULL, 0); checks++;

    node_set_attr(target, "style", "width:30px;height:36px");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, 640, 480);
    value(target, "width", "30px", false); value(target, "height", "36px", false);
    scan(target, before, after, physical, 3);

    /* Used min-size constraints change CSSOM geometry, not the computed
     * preferred size used by animation and transition snapshots. */
    node_set_attr(target, "style", "width:10px;height:12px;min-width:50px;min-height:60px");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, 640, 480);
    value(target, "width", "50px", false); value(target, "height", "60px", false);
    value(target, "width", "10px", true); value(target, "height", "12px", true);
    value(target, "inline-size", "10px", true); value(target, "block-size", "12px", true);
    scan(target, before, after, physical, 3);

    css_apply_decl(before, NULL, "writing-mode", "vertical-rl", NULL, NULL);
    css_apply_decl(after, NULL, "writing-mode", "vertical-rl", NULL, NULL);
    css_apply_decl(after, NULL, "transition-property", "inline-size, block-size", NULL, NULL);
    struct Expected logical[] = {{"inline-size", "12px", "24px", 0}, {"block-size", "10px", "20px", 0}};
    scan(target, before, after, logical, 2);

    /* Mixed rendered snapshots retain their own types. The animation layer
     * receives auto as a keyword and can reject a normal auto/length pair. */
    css_apply_decl(before, NULL, "writing-mode", "horizontal-tb", NULL, NULL);
    css_apply_decl(after, NULL, "writing-mode", "horizontal-tb", NULL, NULL);
    css_apply_decl(before, NULL, "transition-property", "width, height", NULL, NULL);
    css_apply_decl(after, NULL, "transition-property", "width, height", NULL, NULL);
    css_apply_decl(before, NULL, "width", "auto", NULL, NULL);
    css_apply_decl(after, NULL, "height", "auto", NULL, NULL);
    struct Expected keyword[] = {{"width", "auto", "20px", 0}, {"height", "12px", "auto", 0}};
    struct Expected keyword_reverse[] = {{"width", "20px", "auto", 0}, {"height", "auto", "12px", 0}};
    scan(target, before, after, keyword, 2); scan(target, after, before, keyword_reverse, 2);
    css_apply_decl(before, NULL, "width", "25%", NULL, NULL);
    css_apply_decl(before, NULL, "height", "50%", NULL, NULL);
    css_apply_decl(after, NULL, "height", "24px", NULL, NULL);
    struct Expected percentage[] = {{"width", "25%", "20px", 0}, {"height", "50%", "24px", 0}};
    struct Expected percentage_reverse[] = {{"width", "20px", "25%", 0}, {"height", "24px", "50%", 0}};
    scan(target, before, after, percentage, 2); scan(target, after, before, percentage_reverse, 2);
    css_apply_decl(before, NULL, "height", "24px", NULL, NULL);
    css_apply_decl(before, NULL, "width", "calc(0% + 10px)", NULL, NULL);
    struct Expected zero_basis[] = {{"width", "calc(0% + 10px)", "20px", 0}};
    scan(target, before, after, zero_basis, 1);
    css_apply_decl(before, NULL, "width", "min(10%, 20px)", NULL, NULL);
    struct Expected comparison[] = {{"width", "min(10%, 20px)", "20px", 0}};
    scan(target, before, after, comparison, 1);

    node_set_attr(target, "style", "width:25%;height:auto");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, 640, 480);
    value(target, "width", "160px", false); value(target, "height", "0px", false);
    value(target, "width", "25%", true); value(target, "height", "auto", true);
    node_set_attr(target, "style", "width:calc(-2px);height:calc(-0px)");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, 640, 480);
    value(target, "width", "0px", true); value(target, "height", "0px", true);
    css_apply_decl(before, NULL, "width", "calc(-2px)", NULL, NULL);
    struct Expected clamped[] = {{"width", "0px", "20px", 0}};
    scan(target, before, after, clamped, 1);

    /* Non-rendered values retain the same computed types. */
    node_set_attr(target, "style", "display:none;width:25%;height:auto");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, 640, 480);
    value(target, "width", "25%", true); value(target, "height", "auto", true);
    value(target, "inline-size", "25%", true); value(target, "block-size", "auto", true);
    style_free(before); style_free(after);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS css_transition_size_snapshot %u native checks\n", checks);
    return 0;
}
