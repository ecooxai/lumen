/* Rounded rects and box shadows painted with constant-coverage spans give exactly the pixels of
   evaluating every pixel: pills, mixed and huge radii, fractional boxes, blur, spread, offsets,
   clipped and rounded-clip shadows, rounded borders of even, uneven and thick widths, at several
   scales and partial rects. Partial rects also check that culling items and clip subtrees outside
   them (text, layers, nested scroll clips) paints what replaying every item does. */
#include "../src/paint/paint.h"
#include <assert.h>
#include <stdio.h>

static void paint(const DisplayList *dl, Canvas *c, int x0, int y0, int x1, int y1) {
    memset(c->px, 0x5a, (size_t)c->w * c->h * 4);
    raster_rect(c, dl, RGBA(250, 250, 245, 255), x0, y0, x1, y1);
}

int main(void) {
    dom_init(); font_init();
    Document *d = doc_new("https://raster-spans.test/");
    SB h; sb_init(&h);
    sb_puts(&h, "<html><body>");
    char buf[640];
    for (int i = 0; i < 6; i++) {
        snprintf(buf, sizeof buf, "<div style='opacity:.%d;margin:6px;border-radius:%dpx;overflow:hidden;height:%dpx;background:#9c6'><p style='margin:3px;font:%dpx sans-serif;text-shadow:0 1px 4px #000a'>Lumen cull %d gy</p>"
                 "<div style='opacity:.7;overflow:hidden;height:30px;width:%dpx'><b style='display:block;box-shadow:0 0 9px 3px #f00;margin:8px;height:39px;width:140%%'></b></div></div>",
                 3 + i, i * 5, 40 + i * 3, 12 + i * 4, i, 200 + i * 60);
        sb_puts(&h, buf);
    }
    static const char *radius[] = { "0", "4px", "9999px", "50%", "12px 3px 30px 0", "0 0 18px 18px", "7.3px", "40px 2px" };
    static const char *shade[] = { "none", "0 1px 6px #0003", "0 4px 30px 4px #0006", "3px -5px 0 #f008", "-7px 9px 2px -3px #00f9", "0 0 0 6px #0a04", "12px 12px 40px -10px #000c", "0 2px 1px .5px #1238" };
    static const char *edge[] = { "", "border:1px solid #c33;", "border:3.4px solid #2a2;", "border-style:solid;border-color:#33c;border-width:1px 4px 9px 2px;",
                                  "border:14px solid #a0a8;", "border:.6px solid #000;", "border-style:solid;border-color:#0a08;border-width:0 0 2px 0;" };
    for (int i = 0; i < 96; i++) {
        snprintf(buf, sizeof buf, "<div style='display:inline-block;margin:%.2fpx %.2fpx;width:%.2fpx;height:%.2fpx;border-radius:%s;box-shadow:%s;background:%s;%s'></div>",
                 11 + (i * 7 % 13) * 1.37, 9 + (i % 5) * 2.21, 30 + (i * 37 % 300) + (i % 3) * .41, 14 + (i * 23 % 90) + (i % 4) * .33,
                 radius[i % 8], shade[(i / 8 + i) % 8], i % 3 ? "#3a7bd5" : "#fffc", i < 64 ? edge[i % 7 ? 0 : i / 7 % 7] : edge[i % 7]);
        sb_puts(&h, buf);
    }
    /* shadows and pills inside a rounded scroller clip */
    sb_puts(&h, "<div style='width:500px;height:260px;overflow:hidden;border-radius:36px;margin:20px'>");
    for (int i = 0; i < 12; i++) {
        snprintf(buf, sizeof buf, "<div style='margin:9px %dpx;width:%dpx;height:%dpx;border-radius:%s;box-shadow:%s;background:#e94'></div>", 5 + i * 13, 120 + i * 29, 10 + i * 3, radius[(i + 2) % 8], shade[(i + 1) % 8]);
        sb_puts(&h, buf);
    }
    sb_puts(&h, "</div>");
    sb_puts(&h, "<div style='width:880px;height:60px;border-radius:30px;box-shadow:0 1px 3px #0004,0 6px 24px 2px #0002;margin:30px 8px'></div></body></html>");
    html_parse(d, h.s, h.n);
    StyleEngine *e = style_engine_new(d);
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 900, 2400);
    static const float scales[] = { 1, 2, 1.75f, 2.625f };
    for (int k = 0; k < 4; k++) {
        float s = scales[k]; int w = (int)(900 * s), hh = (int)(1300 * s);
        DisplayList dl = {0}; dl_build(&dl, l, 0, 0, 900, 1300);
        int layers = 0, clips = 0;
        for (int i = 0; i < dl.items.n; i++) { const DItem *it = &dl.items.v[i]; if (it->y < 1300) { layers += it->op == DO_PUSH_LAYER; clips += it->op == DO_PUSH_CLIP; } }
        if (!k) fprintf(stderr, "%d items, %d layers, %d clips in view\n", dl.items.n, layers, clips);
        assert(layers >= 6 && clips >= 7);
        Canvas a, b; canvas_init(&a, w, hh, s); canvas_init(&b, w, hh, s);
        raster_set_threads(0);
        const int rects[][4] = { { 0, 0, w, hh }, { 37, 101, w - 61, hh - 233 }, { 0, hh / 3, w, hh / 3 + 97 }, { 0, hh * 2 / 3, w, hh * 2 / 3 + 23 }, { 211, hh - 400, w - 300, hh - 9 } };
        for (int q = 0; q < (int)(sizeof rects / sizeof *rects); q++) {
            const int *r = rects[q];
            raster_pixel_rows = false; double t0 = now_ms(); paint(&dl, &a, r[0], r[1], r[2], r[3]);
            double t1 = now_ms();
            raster_pixel_rows = true; paint(&dl, &b, r[0], r[1], r[2], r[3]);
            double t2 = now_ms();
            size_t diff = 0, first = 0; for (size_t i = 0; i < (size_t)w * hh; i++) if (a.px[i] != b.px[i] && !diff++) first = i;
            fprintf(stderr, "scale %.3g rect %d: spans %.1f ms, per pixel %.1f ms, %zu differing px", s, q, t1 - t0, t2 - t1, diff);
            if (diff) fprintf(stderr, " (first at %zu,%zu: %08x vs %08x)", first % (size_t)w, first / (size_t)w, a.px[first], b.px[first]);
            fputc('\n', stderr);
            assert(!diff);
        }
        canvas_free(&a); canvas_free(&b); dl_clear(&dl); free(dl.items.v);
    }
    layout_free(l); style_engine_free(e); doc_free(d); sb_free(&h);
    puts("Raster spans match per-pixel coverage");
}
