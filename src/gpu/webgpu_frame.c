/* WebGPU canvas frames: the rows of a texture read back by src/js/webgpu.cc become premultiplied 0xAARRGGBB
   Images, reused once the compositor let go of them, the way WebGL frames are (src/gpu/webgl.c). */
#include "gpu/webgpu.h"
#include "media/media.h"
#include "paint/paint.h"
#include "base/util.h"
#include <stdlib.h>
#include <string.h>

#define WGF_POOL 8
struct WgFrames { Image *pool[WGF_POOL]; };

WgFrames *wgf_new(void) { return xcalloc(1, sizeof(WgFrames)); }
void wgf_free(WgFrames *f) {
    if (!f) return;
    for (int i = 0; i < WGF_POOL; i++) image_unref(f->pool[i]);
    free(f);
}

static Image *frame_image(WgFrames *f, int w, int h) {
    for (int i = 0; i < WGF_POOL; i++) {
        Image *im = f->pool[i];
        if (im && im->refs == 1 && im->w == w && im->h == h) { im->refs++; im->content_version++; return im; }
    }
    Image *im = xcalloc(1, sizeof *im); im->w = w; im->h = h; im->refs = 1;
    im->px = xmalloc((size_t)w * h * 4);
    for (int i = 0; i < WGF_POOL; i++) {
        if (f->pool[i] && f->pool[i]->refs == 1) { image_unref(f->pool[i]); f->pool[i] = NULL; }
        if (!f->pool[i]) { f->pool[i] = im; im->refs++; break; }
    }
    return im;
}

static uint32_t unit8(uint16_t hv) {   /* a half float in [0, 1] to 0..255 */
    uint32_t e = (hv >> 10) & 0x1f, m = hv & 0x3ff;
    if (hv & 0x8000) return 0;
    if (e >= 15) return 255;   /* 1.0 and up, inf, nan */
    float v = e ? (float)(1024 + m) / (float)(1 << (25 - e)) : (float)m / 16777216.0f;
    return (uint32_t)(v * 255.0f + 0.5f);
}

void wgf_push(WgFrames *f, MediaPlayer *m, int capture, const uint8_t *rows, size_t stride, int w, int h, int fmt, bool opaque) {
    if (!f || !m || !rows || w <= 0 || h <= 0) return;
    Image *im = frame_image(f, w, h);
    uint32_t all = 0xffffffffu, force = opaque ? 0xff000000u : 0;
    size_t row = (size_t)w * 4;
    if (fmt != WGF_RGBA16F) {   /* mapped readback memory is often uncached: copy it out in bulk, convert in cached memory */
        if (stride == row) memcpy(im->px, rows, row * (size_t)h);
        else for (int y = 0; y < h; y++) memcpy(im->px + (size_t)y * w, rows + (size_t)y * stride, row);
    }
    for (int y = 0; y < h; y++) {
        const uint8_t *s = rows + (size_t)y * stride;
        uint32_t *o = im->px + (size_t)y * w;
        if (fmt == WGF_BGRA8) {   /* already 0xAARRGGBB in memory */
            if (opaque) for (int x = 0; x < w; x++) o[x] |= force;
            else for (int x = 0; x < w; x++) all &= o[x];
        } else if (fmt == WGF_RGBA8) {
            for (int x = 0; x < w; x++) {
                uint32_t v = o[x];
                v = ((v & 0xff00ff00u) | (v & 0xffu) << 16 | (v >> 16 & 0xffu)) | force;
                o[x] = v; all &= v;
            }
        } else {
            const uint16_t *p = (const uint16_t *)s;
            for (int x = 0; x < w; x++, p += 4) {
                uint32_t v = unit8(p[3]) << 24 | unit8(p[0]) << 16 | unit8(p[1]) << 8 | unit8(p[2]);
                v |= force; o[x] = v; all &= v;
            }
        }
    }
    im->translucent = !opaque && (all >> 24) != 0xff;
    if (capture > 0) { im->refs++; live_push(capture, im); }
    mp_push(m, im);
}
