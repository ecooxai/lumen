#include "css/css.h"
#include <math.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

typedef struct { Document *document; StyleEngine *engine; Node *target; } Fixture;

static Fixture fixture(const char *css) {
    Fixture fixture = {0};
    fixture.document = doc_new("about:blank");
    fixture.document->html = node_new_element(fixture.document, "html", 0);
    node_append(&fixture.document->node, fixture.document->html);
    fixture.target = node_new_element(fixture.document, "div", 0);
    node_set_attr(fixture.target, "id", "target");
    node_append(fixture.document->html, fixture.target);
    fixture.engine = style_engine_new(fixture.document);
    fixture.engine->media = (MediaCtx){300, 700, 1, false};
    style_engine_add_sheet(fixture.engine, css_parse_sheet(css, strlen(css), "about:blank", 1, &fixture.engine->media));
    style_recalc(fixture.engine, &fixture.document->node, false);
    return fixture;
}

static void resize(Fixture *fixture, float width, float height) {
    fixture->engine->media.vw = width;
    fixture->engine->media.vh = height;
    fixture->engine->layout_dirty = false;
    style_recalc(fixture->engine, &fixture->document->node, false);
}

static void release(Fixture *fixture) {
    style_engine_free(fixture->engine);
    doc_free(fixture->document);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    dom_init();
    const char *scenario = argv[1];
    Fixture fixture_value;
    if (!strcmp(scenario, "units")) {
        fixture_value = fixture("#target { min-height:100vh; width:10vw; height:20vmin; }");
        CHECK(fixture_value.target->style->min_height.px == 700);
        uint64_t version = fixture_value.document->dom_version;
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.document->dom_version == version);
        CHECK(fixture_value.target->style->min_height.px == 300);
        CHECK(fixture_value.target->style->width.px == 70);
        CHECK(fixture_value.target->style->height.px == 60);
        CHECK(fixture_value.engine->layout_dirty);
        resize(&fixture_value, 300, 700);
        CHECK(fixture_value.target->style->min_height.px == 700);
    } else if (!strcmp(scenario, "media")) {
        fixture_value = fixture("#target { width:11px; } @media (min-width:500px) { #target { width:22px; } }");
        CHECK(fixture_value.target->style->width.px == 11);
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.target->style->width.px == 22);
        resize(&fixture_value, 300, 700);
        CHECK(fixture_value.target->style->width.px == 11);
    } else if (!strcmp(scenario, "nested")) {
        fixture_value = fixture("#target { width:11px; } @media (min-width:500px) { @media (min-height:500px) { #target { width:33px; } } }");
        resize(&fixture_value, 700, 700);
        CHECK(fixture_value.target->style->width.px == 33);
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.target->style->width.px == 11);
        resize(&fixture_value, 300, 700);
        CHECK(fixture_value.target->style->width.px == 11);
    } else if (!strcmp(scenario, "pseudo")) {
        fixture_value = fixture("html { font-size:10vw; } #target { font-size:2em; } #target::before { content:'x'; width:25vw; }");
        CHECK(fixture_value.target->style->font_size == 60);
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.target->style->font_size == 140);
        CHECK(fixture_value.target->style->before && fixture_value.target->style->before->width.px == 175);
    } else if (!strcmp(scenario, "stable")) {
        fixture_value = fixture("#target { width:10vw; } @media (min-width:500px) { #target { width:20vw; } }");
        extern size_t g_css_parsed_bytes;
        size_t parsed = g_css_parsed_bytes;
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.target->style->width.px == 140);
        CHECK(g_css_parsed_bytes == parsed);
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.engine->stats_matched == 0);
        CHECK(!fixture_value.engine->layout_dirty);
        CHECK(g_css_parsed_bytes == parsed);
    } else if (!strcmp(scenario, "independent")) {
        fixture_value = fixture("#target { height:100vh; }");
        Fixture second = fixture("#target { height:100vh; }");
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.target->style->height.px == 300);
        CHECK(second.target->style->height.px == 700);
        resize(&second, 300, 500);
        CHECK(second.target->style->height.px == 500);
        CHECK(fixture_value.target->style->height.px == 300);
        release(&second);
    } else if (!strcmp(scenario, "environment")) {
        fixture_value = fixture("#target { width:11px; height:11px; } @media (min-resolution:2dppx) { #target { width:44px; } } @media (prefers-color-scheme:dark) { #target { height:55px; } }");
        fixture_value.engine->media.dpr = 2;
        fixture_value.engine->media.dark = true;
        resize(&fixture_value, 300, 700);
        CHECK(fixture_value.target->style->width.px == 44);
        CHECK(fixture_value.target->style->height.px == 55);
        fixture_value.engine->media.dpr = 1;
        fixture_value.engine->media.dark = false;
        resize(&fixture_value, 300, 700);
        CHECK(fixture_value.target->style->width.px == 11);
        CHECK(fixture_value.target->style->height.px == 11);
    } else if (!strcmp(scenario, "resources")) {
        fixture_value = fixture("@media print { @font-face { font-family:Hidden; src:url(hidden.woff); } @import 'hidden.css'; #target { width:99px; } } #target { width:11px; }");
        StyleSheet *sheet = fixture_value.engine->sheets.v[1];
        CHECK(sheet->font_faces.n == 0 && sheet->imports.n == 0);
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.target->style->width.px == 11);
    } else if (!strcmp(scenario, "late-sheet")) {
        fixture_value = fixture("#target { width:11px; }");
        resize(&fixture_value, 700, 300);
        const char *css = "@media (min-width:500px) { #target { width:77px; } }";
        style_engine_add_sheet(fixture_value.engine, css_parse_sheet(css, strlen(css), "about:blank", 1, &fixture_value.engine->media));
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.target->style->width.px == 77);
        resize(&fixture_value, 300, 700);
        CHECK(fixture_value.target->style->width.px == 11);
    } else if (!strcmp(scenario, "orientation")) {
        fixture_value = fixture("#target { width:11px; } @media (orientation:portrait) { #target { width:66px; } }");
        CHECK(fixture_value.target->style->width.px == 66);
        resize(&fixture_value, 700, 300);
        CHECK(fixture_value.target->style->width.px == 11);
        resize(&fixture_value, 300, 700);
        CHECK(fixture_value.target->style->width.px == 66);
    } else {
        CHECK(false);
    }
    release(&fixture_value);
    puts("PASS");
    return 0;
}
