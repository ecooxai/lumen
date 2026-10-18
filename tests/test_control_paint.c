/* Public control state, real layout/font shaping, and the actual display list.
 * This fixture also runs before the caret renderer change to record RED. */
#include "../src/dom/control.h"
#include "../src/paint/paint.h"
#include <math.h>
#include <stdio.h>

static int checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)
static bool near(float x, float y) { return fabsf(x - y) < .04f; }
typedef struct { Document *doc; StyleEngine *style; Layout *layout; Node *field; DisplayList dl; } Fixture;
static void init(Fixture *f, const char *input_type) {
    memset(f, 0, sizeof *f); f->doc = doc_new("https://control-paint.test/");
    const char *html = input_type ? "<html><body><input id='field'></body></html>" : "<html><body><textarea id='field'></textarea></body></html>";
    html_parse(f->doc, html, strlen(html)); f->field = doc_get_element_by_id(f->doc, "field");
    if (input_type) node_set_attr(f->field, "type", input_type);
    f->style = style_engine_new(f->doc); f->style->media = (MediaCtx){402,734,1,false};
    const char *css = "html,body{margin:0;padding:0}#field{position:absolute;left:30px;top:40px;width:220px;height:140px;border:0;padding:10px;font:16px monospace;line-height:30px;white-space:pre-wrap;color:rgb(13,37,59);background:white}";
    style_engine_add_sheet(f->style, css_parse_sheet(css, strlen(css), f->doc->url, 1, &f->style->media));
    f->layout = layout_new(); f->doc->focus = f->field; f->field->flags |= NF_FOCUS;
}
static void reflow(Fixture *f) {
    style_recalc(f->style, &f->doc->node, true); layout_run(f->layout, f->doc, 402, 734);
    CHECK(f->field->box != NULL);
}
static void value(Fixture *f, const char *text) { control_set_value_utf8(f->field, text); reflow(f); }
static void paint(Fixture *f, uint32_t start, uint32_t end, bool blink) {
    control_set_selection(f->field, start, end, CONTROL_DIR_NONE);
    dl_caret_on = blink; dl_build(&f->dl, f->layout, 0, 0, 402, 734);
}
static const DItem *caret(Fixture *f) {
    for (int i = 0; i < f->dl.items.n; i++) { const DItem *d = &f->dl.items.v[i]; if (d->op == DO_RECT && near(d->w, 1) && d->color == RGBA(13,37,59,255)) return d; }
    return NULL;
}
static int highlights(Fixture *f, float *width) {
    int count = 0; *width = 0;
    for (int i = 0; i < f->dl.items.n; i++) { const DItem *d = &f->dl.items.v[i]; if (d->op == DO_RECT && d->color == RGBA(179,215,255,255)) { count++; *width += d->w; } }
    return count;
}
static float glyph_x(const TextFrag *f, uint32_t byte) {
    const Box *t = f->box;
    for (int i = f->g0; i < f->g1; i++) if (t->sh.g[i].cluster == byte) return f->x + t->sh.g[i].x - t->sh.g[f->g0].x;
    return f->x + f->w;
}
static void check_caret(Fixture *f, uint32_t index, float x, float y) {
    paint(f, index, index, true); const DItem *c = caret(f); CHECK(c);
    if (c) { if (!near(c->x, x) || !near(c->y, y)) fprintf(stderr, "caret[%u] actual %.2f,%.2f expected %.2f,%.2f\n", index, c->x, c->y, x, y); CHECK(near(c->x, x)); CHECK(near(c->y, y)); }
}
static void finish(Fixture *f) { dl_clear(&f->dl); layout_free(f->layout); style_engine_free(f->style); doc_free(f->doc); }
int main(void) {
    dom_init(); font_init(); Fixture f; init(&f, NULL); value(&f, "abcd");
    Box *b = f.field->box; CHECK(b->nfrags > 0);
    if (!b->nfrags) return 1;
    TextFrag *frag = &b->frags[0]; CHECK(frag->box->sh.n >= 4);
    check_caret(&f, 0, frag->x, frag->y);
    check_caret(&f, 1, glyph_x(frag, 1), frag->y);
    check_caret(&f, 4, frag->x + frag->w, frag->y);
    float width; paint(&f, 1, 3, true);
    CHECK(!caret(&f)); CHECK(highlights(&f, &width) == 1); CHECK(near(width, glyph_x(frag, 3) - glyph_x(frag, 1)));
    paint(&f, 1, 3, false); CHECK(!caret(&f)); CHECK(highlights(&f, &width) == 1);
    f.field->flags &= ~(uint32_t)NF_FOCUS; paint(&f, 1, 3, true); CHECK(!caret(&f)); CHECK(highlights(&f, &width) == 0); f.field->flags |= NF_FOCUS;
    value(&f, "A\xf0\x9f\x99\x82" "B"); frag = &f.field->box->frags[0];
    check_caret(&f, 1, glyph_x(frag, 1), frag->y); check_caret(&f, 3, glyph_x(frag, 5), frag->y);
    value(&f, "a\n\nb\n"); b = f.field->box; CHECK(b->nlines == 3);
    if (b->nlines == 3) {
        check_caret(&f, 2, b->x + b->b[3] + b->p[3], b->lines[1].y);
        check_caret(&f, 5, b->x + b->b[3] + b->p[3], b->lines[2].y + b->lines[2].h);
    }
    value(&f, ""); b = f.field->box;
    check_caret(&f, 0, b->x + b->b[3] + b->p[3], b->y + b->b[0] + b->p[0]);
    node_set_text_content(f.field, "A\rB"); control_set_value_utf8(f.field, NULL); reflow(&f);
    b = f.field->box; CHECK(b->nlines == 2); CHECK(f.field->value_override == NULL);
    CHECK(b->first && b->first->text && !strcmp(b->first->text, "A\nB"));
    if (b->nlines == 2) { TextFrag *second = &b->frags[b->lines[1].f0]; check_caret(&f, 2, second->x, second->y); }
    node_set_attr(f.field, "placeholder", "Type here"); value(&f, "");
    CHECK(f.field->box->placeholder); CHECK(f.field->box->first && !strcmp(f.field->box->first->text, "Type here"));
    finish(&f);
    init(&f, "password"); value(&f, "secret\xf0\x9f\x99\x82" "9"); frag = &f.field->box->frags[0];
    check_caret(&f, 6, glyph_x(frag, 18), frag->y); check_caret(&f, 8, glyph_x(frag, 21), frag->y);
    paint(&f, 6, 8, true); CHECK(!caret(&f)); CHECK(highlights(&f, &width) == 1); CHECK(near(width, glyph_x(frag, 21) - glyph_x(frag, 18)));
    finish(&f);
    init(&f, "email"); node_set_attr(f.field, "value", " \tAB\r\n@example.test \t"); reflow(&f);
    CHECK(f.field->box->first && !strcmp(f.field->box->first->text, "AB@example.test")); CHECK(f.field->value_override == NULL); finish(&f);
    init(&f, "url"); node_set_attr(f.field, "value", " \thttps://example.test/\r\npath \t"); reflow(&f);
    CHECK(f.field->box->first && !strcmp(f.field->box->first->text, "https://example.test/path")); CHECK(f.field->value_override == NULL); finish(&f);
    printf("RESULTS control-paint %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
