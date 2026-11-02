/* Absolute text spacing is computed in the declaration's font/viewport
 * context, then inherited. Public letter-spacing retains its legacy zero
 * spelling while animation compares numeric computed snapshots. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, transitions;
static bool reverse_transition;
static void yes(const char *label, bool condition) {
    checks++;
    if (!condition) { fprintf(stderr, "%s\n", label); abort(); }
}
static void string_value(Node *node, const char *property, const char *expected, bool animation) {
    char *actual = animation ? css_get_animation_value(node, property) : css_get_computed_value(node, property);
    if (!actual || strcmp(actual, expected)) fprintf(stderr, "%s %s: %s, expected %s\n", property,
        animation ? "animation" : "CSSOM", actual ? actual : "(null)", expected);
    yes(property, actual && !strcmp(actual, expected)); free(actual);
}
static void value(Node *node, const char *property, float expected) {
    bool letter = !strcmp(property, "letter-spacing");
    float actual = letter ? node->style->letter_spacing : node->style->word_spacing;
    yes("native computed spacing", isfinite(actual) && fabsf(actual - expected) < .002f);
    char numeric[64]; snprintf(numeric, sizeof numeric, "%gpx", expected == 0 ? 0.0 : (double)expected);
    string_value(node, property, letter && expected == 0 ? "normal" : numeric, false);
    string_value(node, property, numeric, true);
}
static void refresh(StyleEngine *engine, Layout *layout, Document *doc) {
    style_recalc(engine, &doc->node, false);
    layout_run(layout, doc, engine->media.vw, engine->media.vh);
}
static void set_style(Node *node, const char *css, StyleEngine *engine, Layout *layout, Document *doc) {
    node_set_attr(node, "style", css); refresh(engine, layout, doc);
}
static void transition(Node *node, const char *property, const char *from, const char *to) {
    (void)node; transitions++;
    bool letter = !strcmp(property, "letter-spacing"), word = !strcmp(property, "word-spacing");
    yes("only requested spacing transitions", letter || word);
    const char *changed = letter ? "4px" : "6px";
    yes("old spacing snapshot", !strcmp(from, reverse_transition ? changed : "0px"));
    yes("new spacing snapshot", !strcmp(to, reverse_transition ? "0px" : changed));
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://text-spacing-computed.test/");
    const char *html = "<!doctype html><html><body><div id='probe'>A B</div><div id='parent'><div id='child'>A B</div></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc); engine->media = (MediaCtx){400,200,1,false};
    const char *css = "html{font-size:16px}body{margin:0}#probe{font-size:16px}"
        "#parent{font-size:20px;letter-spacing:2em;word-spacing:3em}#child{font-size:10px}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    Layout *layout = layout_new(); refresh(engine, layout, doc);
    Node *probe = doc_get_element_by_id(doc, "probe"), *parent = doc_get_element_by_id(doc, "parent"), *child = doc_get_element_by_id(doc, "child");
    assert(probe && parent && child && child->box);
    value(parent, "letter-spacing", 40); value(parent, "word-spacing", 60);
    value(child, "letter-spacing", 40); value(child, "word-spacing", 60);
    static const struct { const char *input; float expected; } cases[] = {
        {"normal",0}, {"0",0}, {"-0px",0}, {"1em",16}, {"-2px",-2},
        {"calc(1px + 2em)",33}, {"10vw",40}, {"10vh",20}, {"10vmin",20}, {"10vmax",40}
    };
    const char *properties[] = {"letter-spacing", "word-spacing"};
    char declaration[256];
    for (unsigned p = 0; p < 2; p++) for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        snprintf(declaration, sizeof declaration, "%s:%s", properties[p], cases[i].input);
        set_style(probe, declaration, engine, layout, doc); value(probe, properties[p], cases[i].expected);
    }
    set_style(parent, "font-size:30px", engine, layout, doc);
    value(parent, "letter-spacing", 60); value(parent, "word-spacing", 90);
    value(child, "letter-spacing", 60); value(child, "word-spacing", 90);
    set_style(child, "font-size:50px;letter-spacing:inherit;word-spacing:inherit", engine, layout, doc);
    value(child, "letter-spacing", 60); value(child, "word-spacing", 90);
    set_style(child, "font-size:50px;letter-spacing:unset;word-spacing:unset", engine, layout, doc);
    value(child, "letter-spacing", 60); value(child, "word-spacing", 90);
    set_style(child, "font-size:50px;letter-spacing:initial;word-spacing:initial", engine, layout, doc);
    value(child, "letter-spacing", 0); value(child, "word-spacing", 0);
    set_style(child, "font-size:50px;letter-spacing:normal;word-spacing:normal", engine, layout, doc);
    value(child, "letter-spacing", 0); value(child, "word-spacing", 0);
    set_style(child, "font-size:50px;letter-spacing:1em;word-spacing:2em", engine, layout, doc);
    value(child, "letter-spacing", 50); value(child, "word-spacing", 100);
    set_style(probe, "letter-spacing:10vh;word-spacing:10vb", engine, layout, doc);
    value(probe, "letter-spacing", 20); value(probe, "word-spacing", 20);
    uint64_t version = doc->dom_version;
    engine->media.vh = 300; refresh(engine, layout, doc);
    yes("viewport resize does not invent a DOM mutation", doc->dom_version == version);
    value(probe, "letter-spacing", 30); value(probe, "word-spacing", 30);
    /* Preserve existing unresolved representations; this does not claim
       percentage text layout support or reinterpret their percentage basis. */
    const char *fallbacks[] = {"25%", "calc(0% + 10px)"};
    for (unsigned p = 0; p < 2; p++) for (unsigned i = 0; i < 2; i++) {
        snprintf(declaration, sizeof declaration, "%s:%s", properties[p], fallbacks[i]);
        set_style(probe, declaration, engine, layout, doc);
        string_value(probe, properties[p], fallbacks[i], false);
        string_value(probe, properties[p], fallbacks[i], true);
    }
    /* The current rendered node is deliberately unrelated to either supplied
       snapshot. Public normal must not suppress the numeric zero endpoint. */
    ComputedStyle *before = style_new_default(), *after = style_new_default();
    css_apply_decl(before, NULL, "transition-property", "letter-spacing, word-spacing", NULL, NULL);
    css_apply_decl(after, NULL, "transition-property", "letter-spacing, word-spacing", NULL, NULL);
    css_apply_decl(after, NULL, "letter-spacing", "4px", NULL, NULL);
    css_apply_decl(after, NULL, "word-spacing", "6px", NULL, NULL);
    css_transition_scan(probe, before, after, transition);
    yes("both forward spacing transitions", transitions == 2);
    transitions = 0; css_transition_scan(probe, before, before, transition);
    yes("identical spacing snapshots do not transition", transitions == 0);
    reverse_transition = true; css_transition_scan(probe, after, before, transition);
    yes("both reverse spacing transitions", transitions == 2);
    style_free(before); style_free(after);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    assert(checks == 150);
    printf("PASS css_text_spacing_computed %u native checks\n", checks);
    return 0;
}
