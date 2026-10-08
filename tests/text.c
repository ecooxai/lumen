#include "../src/text/font.h"
int main(void) {
    font_init();
    const char *fams[] = { "Arial, sans-serif", "\"Roboto\",\"Noto\",sans-serif", "system-ui", "monospace", "Georgia", "serif" };
    for (int i = 0; i < 6; i++) {
        Font *f = font_get(fams[i], 400, false, 16);
        Font *b = font_get(fams[i], 700, false, 16);
        printf("%-30s asc=%.2f desc=%.2f gap=%.2f space=%.2f w('Hello world')=%.2f bold=%.2f\n", fams[i], f->ascent, f->descent, f->line_gap, f->space_adv, text_width(f, "Hello world", 11, 0), text_width(b, "Hello world", 11, 0));
    }
    Font *f = font_get("Arial", 400, false, 16);
    ShapedRun r; const char *s = "hi 你好 😀 ok";
    text_shape(f, s, strlen(s), 0, &r);
    printf("mixed: %d glyphs width %.2f\n", r.n, r.width);
    for (int i = 0; i < r.n; i++) { const Glyph *g = font_glyph(r.g[i].font, r.g[i].gid, 2); printf(" [%u %dx%d c=%d]", r.g[i].gid, g->w, g->h, g->color); }
    printf("\n");
    double t0 = now_ms(); for (int i = 0; i < 10000; i++) text_width(f, "The quick brown fox jumps", 25, 0); printf("10k shapes: %.1fms\n", now_ms() - t0);
}
