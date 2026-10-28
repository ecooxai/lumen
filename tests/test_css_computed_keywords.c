/* Declaration/cascade regressions for the CSSOM values used by responsive demos. */
#include "../src/css/css.h"
#include <stdio.h>

static int checks, failures;
static StyleEngine environment;

static void expect(Node *node, const char *property, const char *expected) {
    char *actual = css_get_computed_value(node, property);
    bool passed = actual && !strcmp(actual, expected);
    ++checks;
    if (!passed) ++failures;
    printf("%s %s expected=\"%s\" actual=\"%s\"\n",
           passed ? "PASS" : "FAIL", property, expected, actual ? actual : "<null>");
    free(actual);
}

int main(void) {
    dom_init(); environment.media = (MediaCtx){402, 734, 3, false};
    ComputedStyle *style = style_new_default();
    Node node = { .type = NODE_ELEMENT, .style = style };
    expect(&node, "object-fit", "fill");
    expect(&node, "flex-wrap", "nowrap");
    static const char *fits[] = {"fill", "contain", "cover", "none", "scale-down"};
    for (size_t index = 0; index < sizeof fits / sizeof *fits; ++index) {
        css_apply_decl(style, NULL, "object-fit", fits[index], &environment, NULL);
        expect(&node, "object-fit", fits[index]);
    }
    static const char *wraps[] = {"nowrap", "wrap", "wrap-reverse"};
    for (size_t index = 0; index < sizeof wraps / sizeof *wraps; ++index) {
        css_apply_decl(style, NULL, "flex-wrap", wraps[index], &environment, NULL);
        expect(&node, "flex-wrap", wraps[index]);
    }
    expect(&node, "lume-unknown-property", "");
    style_free(style);
    node.style = NULL;
    expect(&node, "object-fit", "");

    const char *html = "<!doctype html><html><body>"
        "<div id='fit' style='object-fit:cover'></div>"
        "<div id='wrap' style='display:flex;flex-wrap:nowrap'></div>"
        "<div id='mutable' style='object-fit:contain;flex-wrap:wrap'></div>"
        "</body></html>";
    const char *css = "#fit{object-fit:contain!important}"
        "#wrap{display:flex;flex-wrap:wrap-reverse!important}";
    Document *doc = doc_new("https://css-keywords.example/");
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc);
    engine->media = environment.media;
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    style_recalc(engine, &doc->node, true);
    Node *fit = doc_get_element_by_id(doc, "fit");
    Node *wrap = doc_get_element_by_id(doc, "wrap");
    Node *mutable = doc_get_element_by_id(doc, "mutable");
    if (!fit || !wrap || !mutable) {
        fprintf(stderr, "Actual parsed regression elements are missing\n");
        style_engine_free(engine); doc_free(doc);
        return 2;
    }
    /* These must reflect the computed cascade, not just the inline text. */
    expect(fit, "object-fit", "contain");
    expect(wrap, "flex-wrap", "wrap-reverse");
    expect(mutable, "object-fit", "contain");
    expect(mutable, "flex-wrap", "wrap");
    node_set_attr(mutable, "style", "object-fit:cover;flex-wrap:nowrap");
    style_recalc(engine, &doc->node, true);
    expect(mutable, "object-fit", "cover");
    expect(mutable, "flex-wrap", "nowrap");
    style_engine_free(engine); doc_free(doc);
    printf("RESULTS css-computed-keywords %s (%d/%d checks)\n",
           failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
