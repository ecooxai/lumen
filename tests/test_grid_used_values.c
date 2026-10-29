/* Public resolved grid values and private animation values have distinct lifetimes. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
static void expect(Node *n, const char *property, const char *wanted, bool animation) {
    char *value = animation ? css_get_animation_value(n, property) : css_get_computed_value(n, property);
    if (strcmp(value, wanted)) fprintf(stderr, "%s: expected '%s', got '%s'\n", property, wanted, value);
    assert(!strcmp(value, wanted)); free(value); checks++;
}
int main(void) {
    dom_init(); font_init();
    Document *d = doc_new("https://grid-used-values.test/");
    const char *html = "<html><body>"
      "<div id='grid' style='display:grid;width:200px;height:80px;grid-template-columns:repeat(2, 1fr);grid-template-rows:20px 60px'></div>"
      "<div id='implicit' style='display:grid;width:200px;height:80px;grid-auto-columns:20px;grid-auto-rows:10px'><div style='grid-column:3;grid-row:2'></div></div>"
      "<div id='empty' style='display:grid;width:200px;height:80px'></div>"
      "<div id='hidden' style='display:none;grid-template-columns:repeat(2, 10px)'></div>"
      "<div id='block' style='display:block;grid-template-columns:repeat(2, 10px)'></div>"
      "<div id='named' style='display:grid;grid-template-columns:[a] repeat(2, [b] 10px [c]) [d]'></div>"
      "<div id='leading' style='display:grid;grid-template-columns:[a] 10px [b];grid-auto-columns:20px'><div style='grid-column:-3 / -2'></div></div>"
      "<div id='areas' style='display:grid;grid-template-columns:[a] 10px [b];grid-template-areas:&quot;one two three&quot;;grid-auto-columns:20px'></div>"
      "<div id='pseudo' style='display:grid;width:200px;grid-template-columns:200px'></div>"
      "<div id='esc-plus'></div><div id='esc-close'></div><div id='esc-hex'></div></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0}#pseudo::before{content:'';display:grid;width:70px;grid-template-columns:1fr 1fr}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    const char *escaped = "#esc-plus,#esc-close,#esc-hex{display:grid}"
        "#esc-plus{grid-template-columns:[a\\+b] 10px [end]}"
        "#esc-close{grid-template-columns:[x\\]y] 10px [end]}"
        "#esc-hex{grid-template-columns:[\\31 a] 10px [end]}";
    style_engine_add_sheet(e, css_parse_sheet(escaped, strlen(escaped), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 800, 1200);
    Node *grid = doc_get_element_by_id(d, "grid"), *implicit = doc_get_element_by_id(d, "implicit");
    expect(grid, "grid-template-columns", "100px 100px", false);
    expect(grid, "grid-template-rows", "20px 60px", false);
    expect(grid, "grid-template-columns", "repeat(2, 1fr)", true);
    expect(implicit, "grid-template-columns", "20px 20px 20px", false);
    expect(implicit, "grid-template-rows", "10px 10px", false);
    expect(implicit, "grid-template-columns", "none", true);
    expect(doc_get_element_by_id(d, "empty"), "grid-template-columns", "none", false);
    expect(doc_get_element_by_id(d, "empty"), "grid-template-rows", "none", false);
    expect(doc_get_element_by_id(d, "hidden"), "grid-template-columns", "repeat(2, 10px)", false);
    expect(doc_get_element_by_id(d, "block"), "grid-template-columns", "repeat(2, 10px)", false);
    expect(doc_get_element_by_id(d, "named"), "grid-template-columns", "[a b] 10px [c b] 10px [c d]", false);
    expect(doc_get_element_by_id(d, "leading"), "grid-template-columns", "20px [a] 10px [b]", false);
    expect(doc_get_element_by_id(d, "areas"), "grid-template-columns", "[a] 10px [b] 20px 20px", false);
    char *before = css_get_computed_pseudo(doc_get_element_by_id(d, "pseudo"), 1, "grid-template-columns");
    assert(!strcmp(before, "35px 35px")); free(before); checks++;
    expect(doc_get_element_by_id(d, "esc-plus"), "grid-template-columns", "[a\\+b] 10px [end]", false);
    expect(doc_get_element_by_id(d, "esc-close"), "grid-template-columns", "[x\\]y] 10px [end]", false);
    expect(doc_get_element_by_id(d, "esc-hex"), "grid-template-columns", "[\\31 a] 10px [end]", false);
    int ordinary_checks = checks;
    for (int i = 0; i < 120; i++) {
        int width = 200 + (i % 3) * 20; char style[160], wanted[64];
        snprintf(style, sizeof style, "display:grid;width:%dpx;height:80px;grid-template-columns:repeat(2, 1fr)", width);
        snprintf(wanted, sizeof wanted, "%dpx %dpx", width / 2, width / 2);
        node_set_attr(grid, "style", style);
        style_recalc(e, &d->node, true); layout_run(l, d, 800, 1200);
        expect(grid, "grid-template-columns", wanted, false);
        expect(grid, "grid-template-columns", "repeat(2, 1fr)", true);
    }
    layout_free(l); style_engine_free(e); doc_free(d);
    printf("grid used values: %d ordinary checks and120 layout/animation replacement cycles passed\n", ordinary_checks);
    return 0;
}
