/* Atomic Grid1/2 automatic-repeat rejection preserves arrays and metadata. */
#include "../src/css/css.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void fallback_preserved(const char *property, const char *value) {
    ComputedStyle *s = style_new_default();
    css_apply_decl(s, NULL, property, "25px", NULL, NULL);
    bool rows = !strcmp(property, "grid-template-rows");
    GridTrack *before = rows ? s->grid_rows : s->grid_cols;
    void *metadata = hm_get(&s->gen_n->map, property);
    assert(before && metadata && !strcmp(metadata, "25px"));
    css_apply_decl(s, NULL, property, value, NULL, NULL);
    assert((rows ? s->grid_rows : s->grid_cols) == before);
    assert((rows ? s->grid_nrows : s->grid_ncols) == 1);
    assert(fabsf(before[0].size.px - 25) < .001f);
    assert(hm_get(&s->gen_n->map, property) == metadata);
    assert(!strcmp(metadata, "25px"));
    style_free(s);
}
int main(void) {
    dom_init();
    const char *bad[] = {"repeat(auto-fill,min-content)","repeat(auto-fit,max-content)","repeat(auto-fill,fit-content(20px))","repeat(auto-fill,minmax(auto,auto))","1fr repeat(auto-fill,20px)","repeat(auto-fill,20px) repeat(auto-fit,30px)","repeat(auto-fill,-10px)","repeat(auto-fill,20deg)","repeat(auto-fill,minmax(-10px,20px))","repeat(auto-fill,minmax(20px,-10px))"};
    for (size_t i=0;i<sizeof bad/sizeof *bad;i++) {
        assert(!css_grid_repeat_valid("grid-template-columns",bad[i]));
        fallback_preserved("grid-template-columns",bad[i]);
        fallback_preserved("grid-template-rows",bad[i]);
    }
    const char *good[] = {"repeat(auto-fill,40px)","repeat(auto-fit,minmax(20px,1fr))","repeat(auto-fill,minmax(auto,30px))","10px repeat(auto-fill,20px) 30px","repeat(2,minmax(10px,auto)) repeat(auto-fill,20px)","[one two] repeat(auto-fill,[a b] minmax(20px,1fr) [c d]) [end]","repeat(auto-fill,minmax(min(10px,20px),1fr))","subgrid [a] repeat(auto-fill,[b c])"};
    for (size_t i=0;i<sizeof good/sizeof *good;i++) assert(css_grid_repeat_valid("grid-template-columns",good[i]));
    assert(css_grid_repeat_valid("grid-template-columns","repeat(auto-fill,20px/**/30px)"));
    ComputedStyle *s=style_new_default();
    css_apply_decl(s,NULL,"grid-template-rows","25px",NULL,NULL);
    css_apply_decl(s,NULL,"grid-template-columns","30px",NULL,NULL);
    GridTrack *rows=s->grid_rows,*cols=s->grid_cols;
    css_apply_decl(s,NULL,"grid-template","repeat(auto-fill,min-content) / 50px",NULL,NULL);
    assert(s->grid_rows==rows && s->grid_cols==cols);
    style_free(s);
    assert(css_grid_repeat_valid("grid-template-columns", "repeat(1001, 1px)"));
    s=style_new_default();
    css_apply_decl(s,NULL,"grid-template-columns","repeat(1001, 1px)",NULL,NULL);
    assert(s->grid_ncols==1000);
    assert(!strcmp(hm_get(&s->gen_n->map,"grid-template-columns"),"repeat(1001, 1px)"));
    style_free(s);
    /* Number-valued math uses computed integer rounding and range clamping;
       dimensions must cancel before a value can occupy the count position. */
    const struct { const char *text; int count; } calculated[] = {
        {"2", 2},
        {"calc(1 + 1)", 2},
        {"min(2, 3)", 2},
        {"max(1, 2)", 2},
        {"clamp(1, 2, 3)", 2},
        {"calc(2.4)", 2},
        {"calc(2.5)", 3},
        {"calc(0)", 1},
        {"calc(-2)", 1},
        {"calc(4 / 2)", 2},
        {"calc(20px / 10px)", 2},
        {"2.5", -1},
        {"calc(2px)", -1},
        {"calc(50%)", -1},
        {"calc(1 + 1px)", -1},
        {"calc()", -1},
        {"calc(250ms / .125s)", 2},
        {"calc((2 + 3) * 2 / 5)", 2},
        {"min(4, 3, 2, 5)", 2},
        {"sqrt(9)", 3},
        {"pow(2, 1)", 2},
        {"round(up, 2.1)", 3},
        {"mod(7, 3)", 1},
        {"rem(8, 3)", 2},
        {"calc(sin(90deg) + 1)", 2},
        {"calc(20px * 2px / 20px / 1px)", 2},
        {"min(2, 3px)", -1},
        {"max(2px, 3px)", -1},
        {"calc(1px * 2px)", -1},
        {"calc(50% / 10px)", -1},
        {"unknown(2)", -1},
        {"min()", -1},
    };
    for (size_t i = 0; i < sizeof calculated / sizeof *calculated; i++) {
        char value[256]; snprintf(value, sizeof value, "repeat(%s,10px)", calculated[i].text);
        assert(css_grid_repeat_valid("grid-template-columns", value) == (calculated[i].count > 0));
        for (int axis = 0; axis < 2; axis++) {
            const char *property = axis ? "grid-template-rows" : "grid-template-columns";
            if (calculated[i].count < 0) { fallback_preserved(property, value); continue; }
            s = style_new_default(); css_apply_decl(s, NULL, property, value, NULL, NULL);
            int count = axis ? s->grid_nrows : s->grid_ncols; GridTrack *tracks = axis ? s->grid_rows : s->grid_cols;
            assert(count == calculated[i].count);
            for (int j = 0; j < count; j++) assert(tracks[j].size.kind == LK_LEN && fabsf(tracks[j].size.px - 10) < .001f);
            style_free(s);
        }
    }
    puts("grid repeat validation: atomic arrays/metadata and fixed-size grammar PASS");
    return 0;
}
