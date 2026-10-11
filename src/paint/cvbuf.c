#include "cvbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

volatile int cvbuf_changed;
static void cvbuf_free(void *p) {
    CanvasBuf *c = p; if (!c) return;
    free(c->px); free(c->img[0].px); free(c->img[1].px); free(c);
}
void cvbuf_resize(CanvasBuf *c, int w, int h) {
    w = w < 0 ? 0 : w > 16384 ? 16384 : w; h = h < 0 ? 0 : h > 16384 ? 16384 : h;
    size_t n = (size_t)w * (size_t)h;
    if (w != c->w || h != c->h) {
        free(c->px); free(c->img[0].px); free(c->img[1].px);
        c->px = calloc(n ? n : 1, 4); c->img[0].px = calloc(n ? n : 1, 4); c->img[1].px = calloc(n ? n : 1, 4);
        c->w = w; c->h = h;
        for (int i = 0; i < 2; i++) { c->img[i].w = w; c->img[i].h = h; c->img[i].refs = 1; c->img[i].scale = 1; }
    } else memset(c->px, 0, n * 4);
    cvbuf_touch(c);
}
CanvasBuf *cvbuf_for(Node *n, bool create) {
    if (!n || n->tag != A_canvas) return NULL;
    if (n->ext && n->ext_free == cvbuf_free) return n->ext;
    if (!create || n->ext) return NULL;
    CanvasBuf *c = calloc(1, sizeof *c); n->ext = c; n->ext_free = cvbuf_free;
    const char *aw = node_attr(n, "width"), *ah = node_attr(n, "height");
    cvbuf_resize(c, aw ? atoi(aw) : 300, ah ? atoi(ah) : 150);
    return c;
}
void cvbuf_touch(CanvasBuf *c) { c->dirty = true; cvbuf_changed = 1; }
Image *cvbuf_image(Node *n) {
    CanvasBuf *c = cvbuf_for(n, false);
    if (!c || !c->w || !c->h) return NULL;
    if (c->dirty) { c->cur ^= 1; memcpy(c->img[c->cur].px, c->px, (size_t)c->w * (size_t)c->h * 4); c->dirty = false; }
    return &c->img[c->cur];
}
uint32_t *cvbuf_px(CanvasBuf *c, int *w, int *h) { *w = c->w; *h = c->h; return c->px; }
const uint32_t *cvbuf_src(Node *n, int *w, int *h) {
    Image *im = n && n->tag == A_canvas ? cvbuf_image(n) : paint_image_hook && n ? paint_image_hook(n) : NULL;
    if (!im || !im->px) return NULL;
    *w = im->w; *h = im->h; return im->px;
}
