/* Flexible row sizing: auto headers/footers retain their intrinsic height,
 * flexible rows divide the remaining height and respect their minimums. */
#include "../src/layout/layout.h"
#include <math.h>
#include <stdio.h>

typedef struct {
    const char *name, *tracks, *extra;
    float height, gap;
    int count;
    float content[4], expected[4];
} Case;
static const Case cases[] = {
    { "app-shell", "auto minmax(0,1fr) auto", "", 600, 0, 3, {40,20,60}, {40,500,60} },
    { "bare-flex", "auto 1fr auto", "", 600, 0, 3, {40,20,60}, {40,500,60} },
    { "weighted", "auto 1fr 2fr auto", "", 700, 0, 4, {40,10,10,60}, {40,200,400,60} },
    { "minimum-freeze", "minmax(180px,1fr) 2fr", "", 300, 0, 2, {0,0}, {180,120} },
    { "fraction-below-one", ".25fr .25fr", "align-content:start;", 400, 0, 2, {0,0}, {100,100} },
    { "zero-minimum-overflow", "auto minmax(0,1fr) auto", "", 200, 0, 3, {40,500,60}, {40,100,60} },
    { "automatic-minimum", "auto 1fr auto", "", 200, 0, 3, {40,500,60}, {40,500,60} },
    { "indefinite-flex", "1fr 2fr", "", -1, 0, 2, {20,60}, {30,60} },
    { "true-auto-stretch", "auto auto auto", "", 600, 0, 3, {40,20,60}, {200,180,220} },
    { "max-content-not-auto", "max-content 1fr auto", "", 600, 0, 3, {40,20,60}, {40,500,60} },
    { "explicit-before-implicit", "auto 1fr auto", "grid-auto-rows:100px;", 600, 0, 3, {40,20,60}, {40,500,60} },
    { "row-gap", "auto minmax(0,1fr) auto", "", 600, 10, 3, {40,20,60}, {40,480,60} },
    { "empty-flex", "1fr 3fr", "", 400, 0, 2, {0,0}, {100,300} },
    { "nested-safe-area", "minmax(calc(4.5rem + env(safe-area-inset-top,0px)),1fr) minmax(0,auto) minmax(0,1fr)", "", 888, 0, 3, {0,92,0}, {398,92,398} },
    { "nested-safe-minimum", "minmax(calc(4.5rem + env(safe-area-inset-top,0px)),1fr) minmax(0,auto) minmax(0,1fr)", "", 200, 0, 3, {0,92,0}, {72,92,36} },
    { "nested-max-minimum", "minmax(max(30px,80px),1fr) minmax(0,1fr)", "", 120, 0, 2, {0,0}, {80,40} },
    { "nested-clamp-minimum", "minmax(clamp(20px,calc(4rem + 16px),100px),1fr) minmax(0,1fr)", "", 120, 0, 2, {0,0}, {80,40} },
    { "nested-repeat", "repeat(2,minmax(max(20px,60px),1fr))", "", 200, 0, 2, {0,0}, {100,100} },
    { "nested-env-fallback", "minmax(env(lumen-test-missing,max(60px,80px)),1fr) minmax(0,1fr)", "", 120, 0, 2, {0,0}, {80,40} },
};
static int checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); failures++; } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < .03f; }
static void run_case(const Case *t) {
    SB html, css; sb_init(&html); sb_init(&css);
    sb_puts(&html, "<html><body><div id='grid'>");
    for (int i = 0; i < t->count; i++)
        sb_printf(&html, "<div id='item%d'><div style='height:%gpx'></div></div>", i, t->content[i]);
    sb_puts(&html, "</div></body></html>");
    sb_printf(&css, "html,body{margin:0;padding:0}#grid{display:grid;width:240px;grid-template-rows:%s;row-gap:%gpx;%s", t->tracks, t->gap, t->extra);
    if (t->height >= 0) sb_printf(&css, "height:%gpx;", t->height);
    sb_putc(&css, '}');
    Document *d = doc_new("https://grid.example/rows"); html_parse(d, html.s, html.n);
    StyleEngine *se = style_engine_new(d); se->media = (MediaCtx){430,888,1,false};
    style_engine_add_sheet(se, css_parse_sheet(css.s, css.n, d->url, 1, &se->media));
    style_recalc(se, &d->node, true);
    Layout *L = layout_new(); layout_run(L, d, 430, 888);
    Node *grid = doc_get_element_by_id(d, "grid"); CHECK(grid && grid->box);
    float y = 0;
    for (int i = 0; i < t->count; i++) {
        char id[32]; snprintf(id, sizeof id, "item%d", i);
        Node *n = doc_get_element_by_id(d, id); CHECK(n && n->box);
        if (!n || !n->box || !grid || !grid->box) continue;
        float top = n->box->y - grid->box->y, height = n->box->h;
        if (!near(top, y) || !near(height, t->expected[i]))
            fprintf(stderr, "%s row%d got y=%g h=%g expected y=%g h=%g\n", t->name, i, top, height, y, t->expected[i]);
        CHECK(near(top, y)); CHECK(near(height, t->expected[i]));
        y += t->expected[i] + t->gap;
    }
    if (grid && grid->box) CHECK(near(grid->box->h, t->height >= 0 ? t->height : y - t->gap));
    layout_free(L); style_engine_free(se); doc_free(d); sb_free(&html); sb_free(&css);
}
static void check_env_lengths(void) {
    static const struct { const char *css; bool valid; float px; } env_cases[] = {
        { "env(safe-area-inset-top)", true, 0 },
        { "env(safe-area-inset-right,99px)", true, 0 },
        { "env(safe-area-inset-bottom,env(lumen-unknown))", true, 0 },
        { "ENV(safe-area-inset-left,5px)", true, 0 },
        { "calc(4.5rem + env(safe-area-inset-top,0px))", true, 72 },
        { "calc(2rem + env(lumen-unknown,12px))", true, 44 },
        { "env(lumen-unknown,max(8px,calc(1rem + 4px)))", true, 20 },
        { "env(lumen-unknown,env(lumen-other,24px))", true, 24 },
        { "env(SAFE-area-inset-top,6px)", true, 6 },
        { "env(lumen-unknown)", false, 0 },
        { "env(lumen-unknown,)", false, 0 },
        { "env(lumen-unknown,10px,20px)", false, 0 },
        { "env(safe-area-inset-top", false, 0 },
    };
    MediaCtx media = {430,888,1,false};
    for (size_t i = 0; i < ARRLEN(env_cases); i++) {
        Length value = L_auto();
        bool valid = css_parse_length(env_cases[i].css, &value, 16, 16, &media);
        if (valid != env_cases[i].valid || (valid && !near(value.px, env_cases[i].px)))
            fprintf(stderr, "env %s got valid=%d px=%g expected valid=%d px=%g\n", env_cases[i].css, valid, value.px, env_cases[i].valid, env_cases[i].px);
        CHECK(valid == env_cases[i].valid);
        if (valid && env_cases[i].valid) { CHECK(value.kind == LK_LEN); CHECK(near(value.px, env_cases[i].px)); }
    }
}
int main(void) {
    dom_init(); font_init(); check_env_lengths();
    for (size_t i = 0; i < ARRLEN(cases); i++) run_case(&cases[i]);
    printf("grid row tests: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
