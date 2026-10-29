/* A WOFF2 web font loads and shapes (FreeType built with brotli): Google's Material Icons come only
   as WOFF2, and without it their ligature names showed as text ("search"). */
#include "../src/text/font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/usr/share/doc/appstream/html/compose-api/RedHatText-Bold.woff2";
    FILE *f = fopen(path, "rb");
    if (!f) { printf("no WOFF2 font at %s: skipped\n", path); return 77; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *d = malloc((size_t)n); assert(fread(d, 1, (size_t)n, f) == (size_t)n); fclose(f);
    assert(!memcmp(d, "wOF2", 4));
    font_init();
    const char *url = "https://woff2.test/a.woff2";
    font_declare("Woff2 Test", 700, false, url);
    assert(font_loaded(url, d, (size_t)n));
    free(d);
    Font *ft = font_get("'Woff2 Test'", 700, false, 20);
    assert(ft && ft->face);
    ShapedRun r; text_shape(ft, "Hello", 5, 0, &r);
    assert(r.n == 5 && r.width > 20 && r.g[0].font == ft && r.g[0].gid);
    const Glyph *g = font_glyph(ft, r.g[0].gid, 2);
    assert(g && g->w > 0 && g->alpha);
    fprintf(stderr, "woff2 face: 5 glyphs, %.1f px wide, H %dx%d\n", r.width, g->w, g->h);
    shaped_free(&r);
    puts("WOFF2 web font loaded");
}
