/* Banded parallel raster: painting a large rect on several threads gives exactly the pixels of one
   pass (text, shadows, gradients, opacity layers across band edges, rounded clips, images, borders),
   for full and partial rects, and keeps the canvas' video punch result. */
#include "../src/paint/paint.h"
#include <assert.h>
#include <stdio.h>

static uint32_t px[64 * 48];
static Image img = { 64, 48, 1, px, 0 }, hole = { 4, 4, 1, px, 0 };
static Image *node_img(Node *n) { return n->tag != A_img ? NULL : node_attr(n, "data-hole") ? &hole : &img; }

static void paint(const DisplayList *dl, Canvas *c, int x0, int y0, int x1, int y1) {
    memset(c->px, 0x5a, (size_t)c->w * c->h * 4); c->punched = false;
    raster_rect(c, dl, RGBA(255, 255, 255, 255), x0, y0, x1, y1);
}

int main(void) {
    dom_init(); font_init();
    for (int i = 0; i < 64 * 48; i++) px[i] = 0xff000000u | (uint32_t)(i * 2654435761u >> 8);
    paint_image_hook = node_img;
    Document *d = doc_new("https://raster-bands.test/");
    SB h; sb_init(&h);
    sb_puts(&h, "<html><body><div class=o><div class=c>");
    for (int i = 0; i < 60; i++) sb_puts(&h, "<p style='color:hsl(20,70%,40%)'>Banded raster text, <b>bold</b> and <i>italic</i> glyphs across rows.</p>");
    sb_puts(&h, "</div></div><div class=g></div><div class=r></div><div class=s></div><img width=300 height=170><img width=33 height=29 style='border-radius:50%'>"
                "<img data-hole width=200 height=120><div class=f>faded group spanning many bands<div class=s></div></div></body></html>");
    html_parse(d, h.s, h.n);
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:7px;font:15px sans-serif}.o{width:420px;height:500px;overflow:hidden;border:5px solid #345;border-radius:40px}"
        ".c p{margin:3px}.g{height:150px;background:linear-gradient(33deg,red,blue 60%,#0f08)}.r{height:120px;width:300px;background:radial-gradient(circle,#ff0,#0a0);border-radius:30px 5px}"
        ".s{width:200px;height:90px;margin:20px;box-shadow:4px 6px 14px 3px #0008;border:3px dashed #a00}"
        ".f{opacity:.55;background:#39c;height:400px;font-size:30px;transform:none}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 900, 1400);
    DisplayList dl = {0}; dl_build(&dl, l, 0, 120, 900, 1400);
    Canvas a, b; canvas_init(&a, 1800, 2800, 2); canvas_init(&b, 1800, 2800, 2);
    a.punch = b.punch = &hole;
    static const int rects[][4] = { { 0, 0, 1800, 2800 }, { 37, 101, 1611, 2333 }, { 0, 1200, 1800, 1700 }, { 500, 0, 900, 2800 } };
    for (int k = 0; k < 4; k++) {
        const int *r = rects[k];
        raster_set_threads(0); double t0 = now_ms(); paint(&dl, &a, r[0], r[1], r[2], r[3]);
        double t1 = now_ms();
        raster_set_threads(5); double t2 = now_ms(); paint(&dl, &b, r[0], r[1], r[2], r[3]);
        double t3 = now_ms();
        size_t diff = 0; for (size_t i = 0; i < (size_t)a.w * a.h; i++) diff += a.px[i] != b.px[i];
        fprintf(stderr, "rect %d: one pass %.1f ms, bands %.1f ms, %zu differing px, punched %d/%d\n", k, t1 - t0, t3 - t2, diff, a.punched, b.punched);
        assert(!diff && a.punched == b.punched);
    }
    assert(a.punched);
    canvas_free(&a); canvas_free(&b); dl_clear(&dl); free(dl.items.v); layout_free(l); style_engine_free(e); doc_free(d); sb_free(&h);
    puts("Banded parallel raster matches one pass");
}
