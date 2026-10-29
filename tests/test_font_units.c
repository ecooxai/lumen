/* CSS ch uses a real zero glyph. The optional external Ahem file is supplied by
 * LUMEN_TEST_AHEM_FONT; its absence is an explicit CTest skip, not font coverage. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int checks;
static void near(const char *label, float actual, float expected) {
    checks++;
    if (!isfinite(actual) || fabsf(actual - expected) > .02f) {
        fprintf(stderr, "%s: %g expected %g\n", label, actual, expected);
        abort();
    }
}
static Document *document(const char *parent, const char *inl, const char *sheet, StyleEngine **engine) {
    Document *d = doc_new("https://font-units.test/");
    SB html = {0};
    sb_printf(&html, "<!doctype html><html><body style='%s'><div id=x style='%s'></div></body></html>", parent, inl);
    html_parse(d, html.s, html.n); sb_free(&html);
    *engine = style_engine_new(d);
    if (sheet && *sheet) style_engine_add_sheet(*engine, css_parse_sheet(sheet, strlen(sheet), d->url, 1, &(*engine)->media));
    style_recalc(*engine, &d->node, true);
    return d;
}
static void width_case(const char *label, const char *parent, const char *inl, const char *sheet, float expected) {
    StyleEngine *e; Document *d = document(parent, inl, sheet, &e);
    Node *n = doc_get_element_by_id(d, "x"); assert(n && n->style);
    near(label, n->style->width.px, expected);
    char *computed = css_get_computed_value(n, "width");
    near("matching computed width", strtof(computed, NULL), expected); free(computed);
    style_engine_free(e); doc_free(d);
}
static void line_case(const char *label, const char *inl, float expected) {
    StyleEngine *e; Document *d = document("", inl, "", &e);
    Node *n = doc_get_element_by_id(d, "x"); assert(n && n->style);
    near(label, n->style->line_height, expected);
    style_engine_free(e); doc_free(d);
}
int main(int argc, char **argv) {
    FILE *file = argc == 3 ? fopen(argv[1], "rb") : NULL;
    FILE *zero_file = argc == 3 ? fopen(argv[2], "rb") : NULL;
    if (!file || !zero_file) {
        if (file) fclose(file); if (zero_file) fclose(zero_file);
        puts("SKIP font_units: configure external LUMEN_TEST_AHEM_FONT and LUMEN_TEST_ZERO_FONT fixtures"); return 77;
    }
    assert(!fseek(file, 0, SEEK_END)); long bytes = ftell(file); assert(bytes > 0);
    rewind(file); void *data = malloc((size_t)bytes); assert(data);
    assert(fread(data, 1, (size_t)bytes, file) == (size_t)bytes); fclose(file);
    dom_init(); font_init();
    font_declare("AstraFontUnitsAhem", 400, false, "https://font-units.test/Ahem.ttf");
    assert(font_loaded("https://font-units.test/Ahem.ttf", data, (size_t)bytes));
    checks++;
    Font *font = font_get("AstraFontUnitsAhem", 400, false, 10);
    near("real zero horizontal", font_zero_advance(font, false), 10);
    near("real zero cached", font_zero_advance(font, false), 10);
    near("Ahem metric matches real shaped zero", font_zero_advance(font, false), text_width(font, "0", 1, 0));
    near("real zero vertical", font_zero_advance(font, true), 10);
    Length fallback;
    assert(css_parse_length("4ch", &fallback, 10, 16, NULL));
    near("context-free normative fallback", fallback.px, 20);
    const char *base = "font:10px AstraFontUnitsAhem";
    width_case("inherited font", base, "width:4ch", "", 40);
    width_case("late inline font", "", "width:4ch;font:10px AstraFontUnitsAhem", "", 40);
    width_case("late family", "", "font-size:10px;width:4ch;font-family:AstraFontUnitsAhem", "", 40);
    width_case("late size", "", "font-family:AstraFontUnitsAhem;width:4ch;font-size:10px", "", 40);
    width_case("shorthand resets earlier size", "", "font-size:20px;font:10px AstraFontUnitsAhem;width:4ch", "", 40);
    width_case("longhand overrides shorthand", "", "font:10px AstraFontUnitsAhem;font-size:20px;width:4ch", "", 80);
    width_case("important shorthand beats inline size", "", "font-size:20px", "#x{width:4ch;font:10px AstraFontUnitsAhem!important}", 40);

    width_case("sheet order", "", "", "#x{width:4ch;font:10px AstraFontUnitsAhem}", 40);
    width_case("important size", "", "font-size:18px", "#x{width:4ch;font-family:AstraFontUnitsAhem;font-size:10px!important}", 40);
    width_case("variable unit", base, "--w:4ch;width:var(--w)", "", 40);
    width_case("calc unit", base, "width:calc(3ch + 10px)", "", 40);
    width_case("min unit", base, "width:min(4ch, 50px)", "", 40);
    width_case("max unit", base, "width:max(4ch, 30px)", "", 40);
    width_case("clamp unit", base, "width:clamp(30px, 4ch, 50px)", "", 40);
    width_case("ordinary em control", base, "width:4em", "", 40);
    width_case("ordinary px control", base, "width:31px", "", 31);
    width_case("zero font size", "", "font:0px AstraFontUnitsAhem;width:4ch", "", 0);
    width_case("vertical upright", base, "writing-mode:vertical-rl;text-orientation:upright;width:4ch", "", 40);
    line_case("line height late font", "line-height:2ch;font-size:10px;font-family:AstraFontUnitsAhem", 20);
    line_case("line height shorthand", "font:10px/2ch AstraFontUnitsAhem", 20);
    line_case("line height later font-size", "font:10px/2ch AstraFontUnitsAhem;font-size:20px", 40);
    line_case("line height override", "font:10px/2ch AstraFontUnitsAhem;line-height:3ch", 30);
    line_case("line height invalid retains", "font:10px/2ch AstraFontUnitsAhem;line-height:bogus", 20);
    line_case("line height number", "line-height:2;font:10px/3 AstraFontUnitsAhem", 30);
    StyleEngine *e; Document *d = document("", "font:20px AstraFontUnitsAhem;line-height:3ch", "#x{line-height:2ch!important}", &e);
    near("important line-height uses final inline font", doc_get_element_by_id(d, "x")->style->line_height, 40);
    style_engine_free(e); doc_free(d);
    d = document("font:20px AstraFontUnitsAhem", "font-family:sans-serif;font-size:1ch", "", &e);
    Node *n = doc_get_element_by_id(d, "x");
    near("font size uses parent metric", n->style->font_size, 20);
    style_engine_free(e); doc_free(d);
    d = document("", "font:20px AstraFontUnitsAhem", "#x::before{content:'x';font-size:1ch;width:2ch}", &e);
    n = doc_get_element_by_id(d, "x"); assert(n->style->before);
    near("pseudo size uses originating font", n->style->before->font_size, 20);
    near("pseudo width uses own font", n->style->before->width.px, 40);
    style_engine_free(e); doc_free(d);
    d = document(base, "width:4ch;margin-left:-2ch;padding-left:1ch;left:3ch;display:grid;grid-template-columns:2ch 3ch", "", &e);
    n = doc_get_element_by_id(d, "x");
    near("negative margin", n->style->margin[3].px, -20);
    near("padding", n->style->padding[3].px, 10);
    near("inset", n->style->inset[3].px, 30);
    assert(n->style->grid_ncols == 2); checks++;
    near("grid first track", n->style->grid_cols[0].size.px, 20);
    near("grid second track", n->style->grid_cols[1].size.px, 30);
    style_engine_free(e); doc_free(d);
    d = document("font:10px AstraFontUnitsAhem", "width:4ch", "#x::before{content:'';width:2ch}", &e);
    n = doc_get_element_by_id(d, "x");
    near("parent font initial", n->style->width.px, 40);
    node_set_attr(d->body, "style", "font:20px AstraFontUnitsAhem");
    style_recalc(e, &d->node, false);
    near("parent font mutation", n->style->width.px, 80);
    near("parent font mutation pseudo", n->style->before->width.px, 40);
    style_engine_free(e); doc_free(d);
    /* A second family starts unresolved. Loading it must refresh clean styles,
       including pseudo values, without any DOM mutation or caller force flag. */
    const char *late = "https://font-units.test/late-Ahem.ttf";
    font_declare("AstraFontUnitsLate", 400, false, late);
    d = document("", "font:10px AstraFontUnitsLate;width:4ch", "#x::before{content:'';width:2ch}", &e);
    n = doc_get_element_by_id(d, "x");
    assert(e->uses_font_units && !style_font_metrics_dirty(e)); checks++;
    unsigned generation = font_generation(); uint64_t version = d->dom_version;
    assert(font_loaded(late, data, (size_t)bytes)); checks++;
    assert(font_generation() != generation && style_font_metrics_dirty(e)); checks++;
    char *computed = css_get_computed_value(n, "width");
    near("font load computed refresh", strtof(computed, NULL), 40); free(computed);
    near("font load own refresh", n->style->width.px, 40);
    assert(n->style->before); near("font load pseudo refresh", n->style->before->width.px, 20);
    assert(d->dom_version == version && !style_font_metrics_dirty(e)); checks++;
    style_engine_free(e); doc_free(d);
    free(data);
    assert(!fseek(zero_file, 0, SEEK_END)); long zero_bytes = ftell(zero_file); assert(zero_bytes > 0);
    rewind(zero_file); data = malloc((size_t)zero_bytes); assert(data);
    assert(fread(data, 1, (size_t)zero_bytes, zero_file) == (size_t)zero_bytes); fclose(zero_file);
    const char *zero_url = "https://font-units.test/zero.woff";
    font_declare("AstraFontUnitsZero", 400, false, zero_url);
    assert(font_loaded(zero_url, data, (size_t)zero_bytes)); checks++;
    Font *zero = font_get("AstraFontUnitsZero", 400, false, 20);
    near("actual zero advance", font_zero_advance(zero, false), 0);
    near("cached zero remains valid", font_zero_advance(zero, false), 0);
    near("zero metric matches real shaped zero", font_zero_advance(zero, false), text_width(zero, "0", 1, 0));
    width_case("zero metric ch width", "font:20px AstraFontUnitsZero", "width:4ch", "", 0);
    free(data);
    printf("%d font-unit controls passed; external Ahem loaded (%ld bytes), zero-advance font loaded (%ld bytes)\n", checks, bytes, zero_bytes);
    return 0;
}
