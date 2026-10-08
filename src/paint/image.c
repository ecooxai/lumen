/* Image decoding (PNG, JPEG, WebP, animated GIF, SVG) to premultiplied ARGB32 */
#include "paint.h"
#include "../dom/dom.h"
#include "../base/util.h"
#include <ctype.h>
#include <setjmp.h>
#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>
#include <gif_lib.h>
#include <pthread.h>

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
/* Animated GIFs keep every composited frame; px points at the current one */
typedef struct ImgAnim { int n, cur, loops, played; uint32_t **fr; int *delay; double next; } ImgAnim;
static pthread_mutex_t anim_mu = PTHREAD_MUTEX_INITIALIZER;
static Image **anims; static int nanims, canims;

void image_unref(Image *im) {
    if (!im || --im->refs > 0) return;
    if (im->anim) {
        pthread_mutex_lock(&anim_mu);
        for (int i = 0; i < nanims; i++) if (anims[i] == im) { anims[i] = anims[--nanims]; break; }
        pthread_mutex_unlock(&anim_mu);
        for (int i = 0; i < im->anim->n; i++) free(im->anim->fr[i]);
        free(im->anim->fr); free(im->anim->delay); free(im->anim);
    } else free(im->px);
    free(im);
}

bool image_anim_tick(double now, double *next) {
    bool changed = false; *next = 0;
    pthread_mutex_lock(&anim_mu);
    for (int i = 0; i < nanims; i++) {
        Image *im = anims[i]; ImgAnim *a = im->anim;
        if (a->loops >= 0 && a->played > a->loops && a->cur == a->n - 1) continue;
        if (!a->next) a->next = now + a->delay[a->cur];
        int steps = 0;
        while (now >= a->next && steps++ < a->n) {
            if (++a->cur == a->n) { a->cur = 0; a->played++; }
            im->px = a->fr[a->cur]; changed = true;
            if (a->loops >= 0 && a->played > a->loops) { a->cur = a->n - 1; im->px = a->fr[a->cur]; break; }
            a->next += a->delay[a->cur];
            if (now - a->next > 1000) a->next = now + a->delay[a->cur];
        }
        if (!(a->loops >= 0 && a->played > a->loops) && (!*next || a->next < *next)) *next = a->next;
    }
    pthread_mutex_unlock(&anim_mu);
    return changed;
}

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
    int W = g->SWidth, H = g->SHeight; size_t np = (size_t)W * (size_t)H;
    if (W <= 0 || H <= 0) { DGifCloseFile(g, &err); return NULL; }
    int nf = g->ImageCount;
    if (np * 4 * (size_t)nf > ((size_t)64 << 20)) nf = 1;   /* too large to keep every frame: show the first */
    uint32_t *canvas = xcalloc(np, 4), *prev = NULL, **fr = xcalloc((size_t)nf, sizeof *fr);
    int *delay = xcalloc((size_t)nf, sizeof *delay), loops = -1;
    for (int k = 0; k < nf; k++) {
        SavedImage *f = &g->SavedImages[k];
        ColorMapObject *cm = f->ImageDesc.ColorMap ? f->ImageDesc.ColorMap : g->SColorMap;
        int trans = -1, disp = 0, dl = 0;
        for (int i = 0; i < f->ExtensionBlockCount; i++) {
            ExtensionBlock *e = &f->ExtensionBlocks[i];
            if (e->Function == GRAPHICS_EXT_FUNC_CODE && e->ByteCount >= 4) {
                disp = (e->Bytes[0] >> 2) & 7; dl = e->Bytes[1] | e->Bytes[2] << 8;
                if (e->Bytes[0] & 1) trans = (unsigned char)e->Bytes[3];
            } else if (e->Function == APPLICATION_EXT_FUNC_CODE && e->ByteCount == 11 && !memcmp(e->Bytes, "NETSCAPE2.0", 11) &&
                       i + 1 < f->ExtensionBlockCount && f->ExtensionBlocks[i + 1].ByteCount >= 3 && f->ExtensionBlocks[i + 1].Bytes[0] == 1) {
                int c = f->ExtensionBlocks[i + 1].Bytes[1] | f->ExtensionBlocks[i + 1].Bytes[2] << 8;
                loops = c ? c : -2;   /* -2: forever */
            }
        }
        if (disp == 3) { if (!prev) prev = xcalloc(np, 4); memcpy(prev, canvas, np * 4); }
        int fx = f->ImageDesc.Left, fy = f->ImageDesc.Top, fw = f->ImageDesc.Width, fh = f->ImageDesc.Height;
        for (int y = 0; cm && f->RasterBits && y < fh; y++) for (int x = 0; x < fw; x++) {
            int X = fx + x, Y = fy + y; if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
            int ci = f->RasterBits[y * fw + x]; if (ci == trans || ci >= cm->ColorCount) continue;
            GifColorType c = cm->Colors[ci];
            canvas[(size_t)Y * (size_t)W + (size_t)X] = 0xff000000u | ((uint32_t)c.Red << 16) | ((uint32_t)c.Green << 8) | c.Blue;
        }
        fr[k] = xmalloc(np * 4); memcpy(fr[k], canvas, np * 4);
        delay[k] = dl <= 1 ? 100 : dl * 10;   /* browsers treat 0-10 ms as 100 ms */
        if (disp == 2) for (int y = LMAX(fy, 0); y < LMIN(fy + fh, H); y++) for (int x = LMAX(fx, 0); x < LMIN(fx + fw, W); x++) canvas[(size_t)y * (size_t)W + (size_t)x] = 0;
        else if (disp == 3 && prev) memcpy(canvas, prev, np * 4);
    }
    DGifCloseFile(g, &err); free(canvas); free(prev);
    Image *im = xcalloc(1, sizeof *im); im->w = W; im->h = H; im->refs = 1; im->px = fr[0];
    if (nf > 1) {
        ImgAnim *a = xcalloc(1, sizeof *a); a->n = nf; a->fr = fr; a->delay = delay; a->loops = loops == -2 ? -1 : loops < 0 ? 0 : loops;
        im->anim = a;
        pthread_mutex_lock(&anim_mu);
        if (nanims == canims) { canims = canims ? canims * 2 : 16; anims = xrealloc(anims, (size_t)canims * sizeof *anims); }
        anims[nanims++] = im;
        pthread_mutex_unlock(&anim_mu);
    } else { free(fr); free(delay); }
    return im;
}

static bool is_svg(const uint8_t *d, size_t n) {
    size_t i = n >= 3 && !memcmp(d, "\xEF\xBB\xBF", 3) ? 3 : 0;
    while (i < n && isspace(d[i])) i++;
    if (i >= n || d[i] != '<') return false;
    for (size_t j = i, lim = n < 4096 ? n : 4096; j + 4 <= lim; j++) if (!memcmp(d + j, "<svg", 4)) return true;
    return false;
}
/* Rasterised once at >=2x (small icons at >=128 px) since it may be scaled up; scale records the density */
static Image *dec_svg(const uint8_t *d, size_t n) {
    Document *doc = doc_new("about:blank");
    Node *ctx = node_new_element(doc, "div", NS_HTML);
    Node *frag = html_parse_fragment(doc, ctx, (const char *)d, n), *s = NULL;
    for (Node *k = frag ? frag->first : NULL; k && !s; k = node_next_in_tree(k, frag))
        if (k->type == NODE_ELEMENT && k->ns == NS_SVG && !strcmp(k->tag, "svg")) s = k;
    Image *im = NULL;
    if (s) {
        float vb[4] = { 0 }, w = 0, h = 0; const char *v;
        bool hv = (v = node_attr(s, "viewBox")) && sscanf(v, "%f%*[ ,]%f%*[ ,]%f%*[ ,]%f", &vb[0], &vb[1], &vb[2], &vb[3]) == 4 && vb[2] > 0 && vb[3] > 0;
        if ((v = node_attr(s, "width")) && !strchr(v, '%')) w = (float)atof(v);
        if ((v = node_attr(s, "height")) && !strchr(v, '%')) h = (float)atof(v);
        if (w <= 0) w = hv ? (h > 0 ? h * vb[2] / vb[3] : vb[2]) : 300;
        if (h <= 0) h = hv ? w * vb[3] / vb[2] : 150;
        im = svg_image(s, w, h, LMAX(2, 128 / LMAX(w, h)));
        if (im) { im->refs++; im->scale = (float)im->w / w; }
    }
    if (frag) node_free_tree(frag);
    node_free_tree(ctx);
    doc_free(doc);
    return im;
}

Image *image_decode(const uint8_t *d, size_t n) {
    if (!d || n < 8) return NULL;
    if (!memcmp(d, "\x89PNG", 4)) return dec_png(d, n);
    if (d[0] == 0xFF && d[1] == 0xD8) return dec_jpeg(d, n);
    if (n > 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return dec_webp(d, n);
    if (!memcmp(d, "GIF8", 4)) return dec_gif(d, n);
    if (is_svg(d, n)) return dec_svg(d, n);
    return NULL;
}
