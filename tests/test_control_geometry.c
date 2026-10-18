/* Portable projection and shaped-fragment geometry; real control state, with
 * deterministic ten-pixel glyph advances instead of a platform font. */
#include "../src/paint/control.h"
#include <math.h>
#include <stdio.h>

static int checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < .001f; }
typedef struct {
    Document *doc; Node *node; ComputedStyle style; Box control, text;
    GlyphPos glyphs[80]; TextFrag frags[30]; Line lines[30]; char *prepared;
} Fixture;
static void line(Fixture *f, int first, int end) {
    Box *b = &f->control; float y = 50 + b->nlines * 30.f;
    Line *l = &f->lines[b->nlines++]; *l = (Line){y, 30, y + 20, b->nfrags, 0};
    if (end > first) { l->nf = 1; f->frags[b->nfrags++] = (TextFrag){&f->text, 40, y + 5, (end - first) * 10.f, 20, y + 20, first, end}; }
}
static void init(Fixture *f, const char *value, const char *prepared, bool password, int white_space, int wrap) {
    memset(f, 0, sizeof *f); f->doc = doc_new("https://geometry.test/");
    f->node = node_new_element(f->doc, password ? "input" : "textarea", NS_HTML); node_append(&f->doc->node, f->node);
    if (password) node_set_attr(f->node, "type", "password");
    control_set_value_utf8(f->node, value); f->node->box = &f->control;
    f->style.font_size = 16; f->style.white_space = (uint8_t)white_space;
    f->control = (Box){.node=f->node, .st=&f->style, .ctl=password ? CTL_TEXT : CTL_TEXTAREA, .x=30, .y=40, .w=240, .h=180,
                      .first=&f->text, .frags=f->frags, .lines=f->lines};
    f->control.p[0] = f->control.p[3] = 10;
    f->prepared = xstrdup(prepared);
    f->text = (Box){.kind=BX_TEXT, .parent=&f->control, .st=&f->style, .text=f->prepared, .text_len=(int)strlen(prepared)};
    f->text.sh.g = f->glyphs;
    int start = 0, count = 0;
    for (size_t byte = 0; prepared[byte];) {
        uint32_t cp; int length = utf8_decode(prepared + byte, strlen(prepared + byte), &cp);
        int g = f->text.sh.n++; f->glyphs[g] = (GlyphPos){.gid=cp, .x=g * 10.f, .adv=10, .cluster=(uint32_t)byte};
        if (cp == '\n') { line(f, start, g); start = g + 1; count = 0; }
        else { if (wrap && count == wrap) { line(f, start, g); start = g; count = 0; } count++; }
        byte += (size_t)length;
    }
    if (start < f->text.sh.n) line(f, start, f->text.sh.n);
}
static void finish(Fixture *f) { f->node->box = NULL; doc_free(f->doc); free(f->prepared); }
static void caret(Fixture *f, uint32_t index, float x, float y, float height) {
    ControlCaret c; CHECK(control_paint_caret(&f->control, index, &c)); CHECK(near(c.x, x)); CHECK(near(c.y, y)); CHECK(near(c.h, height));
}
int main(void) {
    dom_init(); Fixture f;
    init(&f, "abcd", "abcd", false, WS_PRE_WRAP, 0);
    caret(&f, 0, 40, 55, 20); caret(&f, 1, 50, 55, 20); caret(&f, 4, 80, 55, 20);
    CHECK(control_paint_hit(&f.control, 51, 63) == 1); CHECK(control_paint_hit(&f.control, -100, 63) == 0); CHECK(control_paint_hit(&f.control, 500, 63) == 4);
    finish(&f);
    init(&f, "abcdefgh", "abcdefgh", false, WS_PRE_WRAP, 4);
    caret(&f, 4, 40, 85, 20); caret(&f, 8, 80, 85, 20);
    CHECK(control_paint_hit(&f.control, 60, 95) == 6); CHECK(control_paint_hit(&f.control, 50, -100) == 1);
    finish(&f);
    init(&f, "a\n\nb\n", "a\n\nb\n", false, WS_PRE_WRAP, 0);
    caret(&f, 1, 50, 55, 20); caret(&f, 2, 40, 80, 30); caret(&f, 3, 40, 115, 20); caret(&f, 5, 40, 140, 30);
    CHECK(control_paint_hit(&f.control, 40, 90) == 2); CHECK(control_paint_hit(&f.control, 40, 150) == 5);
    finish(&f);
    init(&f, "ab\xf0\x9f\x99\x82" "c", "ab\xf0\x9f\x99\x82" "c", false, WS_PRE_WRAP, 0);
    CHECK(control_paint_byte(&f.control, 2) == 2); CHECK(control_paint_byte(&f.control, 3) == 2); CHECK(control_paint_byte(&f.control, 4) == 6);
    CHECK(control_paint_index(&f.control, 6) == 4); caret(&f, 4, 70, 55, 20); CHECK(control_paint_hit(&f.control, 70, 60) == 4);
    finish(&f);
    init(&f, "ab\xf0\x9f\x99\x82" "c", "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2", true, WS_PRE, 0);
    CHECK(control_paint_byte(&f.control, 2) == 6); CHECK(control_paint_byte(&f.control, 4) == 9); CHECK(control_paint_index(&f.control, 9) == 4);
    caret(&f, 4, 70, 55, 20); CHECK(control_paint_hit(&f.control, 70, 60) == 4);
    finish(&f);
    init(&f, "a\tb", "a    b", false, WS_PRE_WRAP, 0);
    CHECK(control_paint_byte(&f.control, 1) == 1); CHECK(control_paint_byte(&f.control, 2) == 5);
    caret(&f, 2, 90, 55, 20); CHECK(control_paint_hit(&f.control, 60, 60) == 1); CHECK(control_paint_hit(&f.control, 80, 60) == 2);
    finish(&f);
    init(&f, "a  b", "a b", false, WS_NORMAL, 0);
    CHECK(control_paint_byte(&f.control, 1) == 1); CHECK(control_paint_byte(&f.control, 3) == 2); CHECK(control_paint_byte(&f.control, 4) == 3);
    finish(&f);
    init(&f, "a  \nb", "a\nb", false, WS_PRE_LINE, 0);
    CHECK(control_paint_byte(&f.control, 1) == 1); CHECK(control_paint_byte(&f.control, 3) == 1); CHECK(control_paint_byte(&f.control, 4) == 2);
    caret(&f, 4, 40, 85, 20); finish(&f);
    init(&f, "", "placeholder", false, WS_PRE_WRAP, 0); f.control.placeholder = true;
    CHECK(control_paint_byte(&f.control, 10) == 0); CHECK(control_paint_hit(&f.control, 80, 60) == 0); caret(&f, 0, 40, 55, 20); finish(&f);
    printf("RESULTS control-geometry %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
