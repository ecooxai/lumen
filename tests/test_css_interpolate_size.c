/* Native policy grammar, inherited snapshots and the actual preferred-size
 * sample path. The original257/264 sizing controls remain separate targets. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, changed_policy, changed_width;
static void yes(bool value, const char *label) {
    checks++;
    if (!value) { fprintf(stderr, "%s\n", label); abort(); }
}
static void value(ComputedStyle *style, const char *expected) {
    Node node = {0}; node.type = NODE_ELEMENT; node.style = style;
    char *actual = css_get_computed_value(&node, "interpolate-size");
    if (!actual || strcmp(actual, expected))
        fprintf(stderr, "interpolate-size:%s expected %s\n", actual ? actual : "(null)", expected);
    yes(actual && !strcmp(actual, expected), "computed policy keyword"); free(actual);
}
static void change(Node *node, const char *property, const char *from, const char *to) {
    (void)node; (void)from; (void)to;
    if (!strcmp(property, "interpolate-size")) changed_policy++;
    if (!strcmp(property, "width")) changed_width++;
}
static void dimension(Node *node, bool height, float expected) {
    const char *property = height ? "height" : "width";
    char *computed = css_get_computed_value(node, property), *end = NULL;
    float px = computed ? strtof(computed, &end) : NAN;
    yes(computed && end != computed && !strcmp(end, "px"), "actual resolved sample is pixels");
    yes(isfinite(px) && fabsf(px - expected) < .02f, "actual resolved sample size"); free(computed);
    float actual = height ? node->box->h : node->box->w;
    yes(isfinite(actual) && fabsf(actual - expected) < .02f, "actual native sample geometry");
}

int main(void) {
    dom_init(); font_init();
    ComputedStyle *style = style_new_default(), *parent = style_new_default();
    value(style, "numeric-only");
    static const char *const valid[][2] = {
        {"numeric-only", "numeric-only"}, {"allow-keywords", "allow-keywords"},
        {"ALLOW-KEYWORDS", "allow-keywords"}, {"NuMeRiC-OnLy", "numeric-only"},
        {"allow-\\6b eywords", "allow-keywords"}, {"/**/allow-keywords/**/", "allow-keywords"}
    };
    char condition[512], declarations[1024];
    for (unsigned i = 0; i < sizeof valid / sizeof *valid; i++) {
        snprintf(condition, sizeof condition, "(interpolate-size:%s)", valid[i][0]);
        yes(css_supports(condition), "valid native supports condition");
        css_apply_decl(style, parent, "interpolate-size", valid[i][0], NULL, NULL);
        value(style, valid[i][1]);
        snprintf(declarations, sizeof declarations, "interpolate-size:%s;color:red", valid[i][0]);
        DeclList *list = css_parse_decls(declarations, strlen(declarations));
        yes(list && list->n == 2, "valid raw declaration remains present"); css_decls_free(list);
    }
    assert(checks == 19);
    static const char *const invalid[] = {
        "", " ", "auto", "none", "allow", "allow-keywords numeric-only", "allow-keywords, numeric-only",
        "allow/**/-keywords", "0", "\"allow-keywords\"", "calc(1)", "var()", "numeric-only)"
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        css_apply_decl(style, parent, "interpolate-size", "allow-keywords", NULL, NULL);
        snprintf(condition, sizeof condition, "(interpolate-size:%s)", invalid[i]);
        yes(!css_supports(condition), "invalid native supports condition");
        css_apply_decl(style, parent, "interpolate-size", invalid[i], NULL, NULL);
        value(style, "allow-keywords");
        snprintf(declarations, sizeof declarations, "interpolate-size:allow-keywords;color:red;interpolate-size:%s", invalid[i]);
        DeclList *list = css_parse_decls(declarations, strlen(declarations));
        yes(list && list->n == 2, "invalid raw declaration retains prior declaration"); css_decls_free(list);
    }
    assert(checks == 58);
    css_apply_decl(parent, NULL, "interpolate-size", "allow-keywords", NULL, NULL);
    ComputedStyle *child = style_inherit(parent); value(child, "allow-keywords"); style_free(child);
    css_apply_decl(style, parent, "interpolate-size", "initial", NULL, NULL); value(style, "numeric-only");
    css_apply_decl(style, parent, "interpolate-size", "inherit", NULL, NULL); value(style, "allow-keywords");
    css_apply_decl(style, parent, "interpolate-size", "unset", NULL, NULL); value(style, "allow-keywords");
    css_apply_decl(style, parent, "--Policy", "numeric-only", NULL, NULL);
    css_apply_decl(style, parent, "interpolate-size", "var(--Policy)", NULL, NULL); value(style, "numeric-only");
    css_apply_decl(style, parent, "--Policy", "bad-policy", NULL, NULL);
    css_apply_decl(style, parent, "interpolate-size", "var(--Policy)", NULL, NULL); value(style, "allow-keywords");
    css_apply_decl(style, parent, "interpolate-size", "var(--Missing, numeric-only)", NULL, NULL); value(style, "numeric-only");
    css_apply_decl(style, parent, "interpolate-size", "var(--Missing)", NULL, NULL); value(style, "allow-keywords");
    css_apply_decl(style, parent, "--Policy", "INITIAL", NULL, NULL);
    /* CSS-wide initial on an unregistered custom property is guaranteed-invalid;
       its failed substitution takes this inherited property's unset value. */
    css_apply_decl(style, parent, "interpolate-size", "var(--Policy)", NULL, NULL); value(style, "allow-keywords");
    assert(checks == 67);
    ComputedStyle *before = style_new_default(), *after = style_new_default();
    css_apply_decl(before, NULL, "width", "10px", NULL, NULL);
    css_apply_decl(after, NULL, "width", "20px", NULL, NULL);
    css_apply_decl(after, NULL, "interpolate-size", "allow-keywords", NULL, NULL);
    Node node = {0}; node.type = NODE_ELEMENT; node.style = after;
    css_apply_decl(after, NULL, "transition-property", "all", NULL, NULL);
    css_transition_scan(&node, before, after, change);
    yes(changed_width == 1 && changed_policy == 0, "transition all excludes nonanimatable policy but includes width");
    changed_width = changed_policy = 0;
    css_apply_decl(after, NULL, "transition-property", "interpolate-size, width", NULL, NULL);
    css_transition_scan(&node, before, after, change);
    yes(changed_width == 1 && changed_policy == 0, "explicit transition policy remains nonanimatable");
    style_free(before); style_free(after); style_free(style); style_free(parent);
    assert(checks == 69);

    Document *doc = doc_new("https://interpolate-size.test/");
    const char *html = "<!doctype html><html><body><div id=parent><div id=target><div id=content></div></div></div></body></html>";
    html_parse(doc, html, strlen(html)); StyleEngine *engine = style_engine_new(doc);
    engine->media = (MediaCtx){800,600,1,false};
    const char *css = "html,body{margin:0;padding:0}#parent{width:200px;height:160px;interpolate-size:allow-keywords}"
        "#target{margin:0;padding:0;border:0;width:60px;height:40px}#content{width:80px;height:80px}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    Node *p = doc_get_element_by_id(doc,"parent"), *target = doc_get_element_by_id(doc,"target"), *content = doc_get_element_by_id(doc,"content");
    Layout *layout = layout_new(); style_recalc(engine, &doc->node, false); layout_run(layout,doc,800,600);
    value(p->style,"allow-keywords"); value(target->style,"allow-keywords"); value(content->style,"allow-keywords");
    node_set_attr(p,"style","interpolate-size:numeric-only"); style_recalc(engine,&doc->node,false);
    value(target->style,"numeric-only"); value(content->style,"numeric-only");
    node_set_attr(target,"style","interpolate-size:allow-keywords"); style_recalc(engine,&doc->node,false);
    value(target->style,"allow-keywords"); value(content->style,"allow-keywords");
    node_set_attr(target,"style","interpolate-size:var(--Policy);--Policy:bad"); style_recalc(engine,&doc->node,false);
    value(target->style,"numeric-only"); value(content->style,"numeric-only");
    node_set_attr(p,"style","interpolate-size:allow-keywords"); style_recalc(engine,&doc->node,false);
    value(target->style,"allow-keywords"); value(content->style,"allow-keywords");
    assert(checks == 80);
    static const struct { const char *declaration; bool height; float expected; } samples[] = {
        {"width:calc-size(auto,size * .75 + 10px)",false,160},
        {"width:calc-size(auto,size * -.5 + 60px)",false,0},
        {"height:calc-size(auto,size * .5 + 10px)",true,50},
        {"height:calc-size(auto,size * 1.3 - 6px)",true,98},
        {"width:calc-size(min-content,size * .5 + 20px)",false,60},
        {"width:calc-size(fit-content,size * .5 + 20px)",false,60},
    };
    for (unsigned i = 0; i < sizeof samples / sizeof *samples; i++) {
        node_set_attr(target,"style",samples[i].declaration); style_recalc(engine,&doc->node,false); layout_run(layout,doc,800,600);
        dimension(target,samples[i].height,samples[i].expected);
    }
    assert(checks == 98);
    node_set_attr(target,"style","height:calc-size(auto,size * .5 + 10px)");
    node_set_attr(content,"style","height:120px"); style_recalc(engine,&doc->node,false); layout_run(layout,doc,800,600);
    dimension(target,true,70);
    node_set_attr(target,"style","width:calc-size(auto,size * .5 + 20px)");
    node_set_attr(p,"style","width:300px"); style_recalc(engine,&doc->node,false); layout_run(layout,doc,800,600);
    dimension(target,false,170);
    assert(checks == 104);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS css_interpolate_size %u native checks\n",checks); return 0;
}
