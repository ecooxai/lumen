/* Image decoding (PNG, JPEG, WebP, GIF first frame) to premultiplied ARGB32 */
#include "paint.h"
#include <setjmp.h>
#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>
#include <gif_lib.h>

static void premultiply(uint32_t *px, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint32_t p = px[i], a = p >> 24;
        if (a == 255) continue;
        if (!a) { px[i] = 0; continue; }
        px[i] = (a << 24) | ((((p >> 16) & 255) * a / 255) << 16) | ((((p >> 8) & 255) * a / 255) << 8) | ((p & 255) * a / 255);
    }
}
static Image *img_new(int w, int h) {
    if (w <= 0 || h <= 0 || (size_t)w * (size_t)h > 64u * 1024 * 1024) return NULL;
    Image *im = xcalloc(1, sizeof *im); im->w = w; im->h = h; im->refs = 1; im->px = xmalloc((size_t)w * (size_t)h * 4); return im;
}
void image_unref(Image *im) { if (im && --im->refs <= 0) { free(im->px); free(im); } }

static Image *dec_png(const uint8_t *d, size_t n) {
    png_image p; memset(&p, 0, sizeof p); p.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&p, d, n)) return NULL;
    p.format = PNG_FORMAT_BGRA;
    Image *im = img_new((int)p.width, (int)p.height);
    if (!im) { png_image_free(&p); return NULL; }
    if (!png_image_finish_read(&p, NULL, im->px, 0, NULL)) { image_unref(im); return NULL; }
    premultiply(im->px, (size_t)im->w * (size_t)im->h);
    return im;
}

struct jerr { struct jpeg_error_mgr pub; jmp_buf jb; };
static void jexit(j_common_ptr c) { longjmp(((struct jerr *)c->err)->jb, 1); }
static Image *dec_jpeg(const uint8_t *d, size_t n) {
    struct jpeg_decompress_struct c; struct jerr e; Image *volatile im = NULL;
    c.err = jpeg_std_error(&e.pub); e.pub.error_exit = jexit;
    if (setjmp(e.jb)) { jpeg_destroy_decompress(&c); image_unref(im); return NULL; }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, d, (unsigned long)n);
    jpeg_read_header(&c, TRUE);
    c.out_color_space = JCS_EXT_BGRA;
    jpeg_start_decompress(&c);
    im = img_new((int)c.output_width, (int)c.output_height);
    if (!im) { jpeg_destroy_decompress(&c); return NULL; }
    while (c.output_scanline < c.output_height) { JSAMPROW row = (JSAMPROW)(im->px + (size_t)c.output_scanline * (size_t)im->w); jpeg_read_scanlines(&c, &row, 1); }
    jpeg_finish_decompress(&c); jpeg_destroy_decompress(&c);
    return im;
}

static Image *dec_webp(const uint8_t *d, size_t n) {
    int w, h; if (!WebPGetInfo(d, n, &w, &h)) return NULL;
    Image *im = img_new(w, h); if (!im) return NULL;
    if (!WebPDecodeBGRAInto(d, n, (uint8_t *)im->px, (size_t)w * (size_t)h * 4, w * 4)) { image_unref(im); return NULL; }
    premultiply(im->px, (size_t)w * (size_t)h);
    return im;
}

typedef struct { const uint8_t *d; size_t n, pos; } GifMem;
static int gread(GifFileType *g, GifByteType *buf, int len) { GifMem *m = g->UserData; size_t k = LMIN((size_t)len, m->n - m->pos); memcpy(buf, m->d + m->pos, k); m->pos += k; return (int)k; }
static Image *dec_gif(const uint8_t *d, size_t n) {
    GifMem m = { d, n, 0 }; int err;
    GifFileType *g = DGifOpen(&m, gread, &err); if (!g) return NULL;
    if (DGifSlurp(g) != GIF_OK || g->ImageCount < 1) { DGifCloseFile(g, &err); return NULL; }
    Image *im = img_new(g->SWidth, g->SHeight);
    if (!im) { DGifCloseFile(g, &err); return NULL; }
    memset(im->px, 0, (size_t)im->w * (size_t)im->h * 4);
    SavedImage *f = &g->SavedImages[0];
    ColorMapObject *cm = f->ImageDesc.ColorMap ? f->ImageDesc.ColorMap : g->SColorMap;
    int trans = -1;
    for (int i = 0; i < f->ExtensionBlockCount; i++) { ExtensionBlock *e = &f->ExtensionBlocks[i]; if (e->Function == GRAPHICS_EXT_FUNC_CODE && e->ByteCount >= 4 && (e->Bytes[0] & 1)) trans = (unsigned char)e->Bytes[3]; }
    int fx = f->ImageDesc.Left, fy = f->ImageDesc.Top, fw = f->ImageDesc.Width, fh = f->ImageDesc.Height;
    for (int y = 0; y < fh; y++) for (int x = 0; x < fw; x++) {
        int X = fx + x, Y = fy + y; if (X >= im->w || Y >= im->h || !cm) continue;
        int ci = f->RasterBits[y * fw + x]; if (ci == trans || ci >= cm->ColorCount) continue;
        GifColorType c = cm->Colors[ci];
        im->px[(size_t)Y * (size_t)im->w + (size_t)X] = 0xff000000u | ((uint32_t)c.Red << 16) | ((uint32_t)c.Green << 8) | c.Blue;
    }
    DGifCloseFile(g, &err);
    return im;
}

Image *image_decode(const uint8_t *d, size_t n) {
    if (!d || n < 8) return NULL;
    if (!memcmp(d, "\x89PNG", 4)) return dec_png(d, n);
    if (d[0] == 0xFF && d[1] == 0xD8) return dec_jpeg(d, n);
    if (n > 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return dec_webp(d, n);
    if (!memcmp(d, "GIF8", 4)) return dec_gif(d, n);
    return NULL;
}
