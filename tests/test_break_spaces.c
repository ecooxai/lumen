/* Preserved spaces must participate in line fitting, including the first
   trailing space attached to a word. Expected line contents are fixed from
   CSS Text's break-spaces rules, not from the fitter's implementation. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static void check_case(const char *label, const char *ws, const char *overflow,
                       int cells, const char *text, int lines, const char *first) {
    Document *d = doc_new("https://preserved-space.test/");
    SB html = {0};
    sb_printf(&html, "<!doctype html><html><body><div id=x style='font:16px/1 monospace;"
        "white-space:%s;overflow-wrap:%s;width:%dch'>%s</div></body></html>",
        ws, overflow, cells, text);
    html_parse(d, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(d); style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 800, 600);
    Node *n = doc_get_element_by_id(d, "x"); Box *box = n ? n->box : NULL;
    if (!box || box->nlines != lines) {
        fprintf(stderr, "%s: expected%d lines, got%d\n", label, lines, box ? box->nlines : -1);
        abort();
    }
    checks++;
    char actual[64] = {0}; size_t used = 0;
    Line *line = &box->lines[0];
    for (int i = line->f0; i < line->f0 + line->nf; i++) {
        TextFrag *f = &box->frags[i];
        if (f->box->kind != BX_TEXT) continue;
        for (int g = f->g0; g < f->g1; g++) {
            assert(used + 1 < sizeof actual);
            actual[used++] = f->box->text[f->box->sh.g[g].cluster];
        }
    }
    if (strcmp(actual, first)) {
        fprintf(stderr, "%s: expected first line[%s], got[%s]\n", label, first, actual);
        abort();
    }
    checks++;
    layout_free(l); style_engine_free(e); doc_free(d);
}

int main(void) {
    dom_init(); font_init();
    check_case("earlier break anywhere", "break-spaces", "anywhere", 5, " FAIL ", 2, " ");
    check_case("earlier break break-word", "break-spaces", "break-word", 5, " FAIL ", 2, " ");
    check_case("earlier ordinary break", "break-spaces", "normal", 5, " FAIL ", 2, " ");
    check_case("pre-wrap hanging preserved", "pre-wrap", "anywhere", 5, " FAIL ", 1, " FAIL ");
    check_case("normal collapsing preserved", "normal", "anywhere", 5, " FAIL ", 1, "FAIL");
    check_case("emergency break without earlier opportunity", "break-spaces", "anywhere", 4, "FAIL ", 2, "FAIL");
    check_case("normal overflow without earlier opportunity", "break-spaces", "normal", 4, "FAIL ", 1, "FAIL ");
    check_case("consecutive spaces anywhere", "break-spaces", "anywhere", 2, "X   ", 2, "X ");
    check_case("consecutive spaces normal", "break-spaces", "normal", 2, "X   ", 2, "X ");
    check_case("forced newline preserved", "break-spaces", "anywhere", 5, " \nFAIL ", 2, " ");
    check_case("no unnecessary earlier break", "break-spaces", "anywhere", 5, " FAIL", 1, " FAIL");
    printf("%d preserved-space line-break controls passed\n", checks);
    return 0;
}
