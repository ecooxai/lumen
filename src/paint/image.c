/* Image decoding (PNG, JPEG, WebP, animated GIF, SVG) to premultiplied ARGB32 */
#include "paint.h"
#include "../gpu/webgpu.h"
#include "../dom/dom.h"
#include "../base/util.h"
#include <ctype.h>
#include <setjmp.h>
#include <png.h>
#include <jpeglib.h>
#include <webp/decode.h>
#include <gif_lib.h>
#include <zlib.h>
#include <math.h>
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
    free(im->yuv);
    if (im->gpu) im->gpu->release(im->gpu);
    free(im);
}

Image *image_yuv420p(int w, int h, const uint8_t *const planes[3], const int strides[3], int mat) {
    if (w <= 0 || h <= 0 || (size_t)w > (64u * 1024 * 1024) / (size_t)h || !planes || !strides) return NULL;
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    for (int i = 0; i < 3; i++) {
        int row = i ? cw : w;
        if (!planes[i] || (strides[i] < row && strides[i] > -row)) return NULL;
    }
    Image *im = xcalloc(1, sizeof *im);
    im->w = w; im->h = h; im->refs = 1; im->yuv_mat = mat;
    size_t luma = (size_t)w * (size_t)h;
    im->yuv = xmalloc(luma + (size_t)cw * 2 * (size_t)ch);
    for (int y = 0; y < h; y++)
        memcpy(im->yuv + (size_t)y * w, planes[0] + (ptrdiff_t)y * strides[0], (size_t)w);
    for (int y = 0; y < ch; y++) {
        const uint8_t *u = planes[1] + (ptrdiff_t)y * strides[1];
        const uint8_t *v = planes[2] + (ptrdiff_t)y * strides[2];
        uint8_t *uv = im->yuv + luma + (size_t)y * cw * 2;
        for (int x = 0; x < cw; x++) { uv[2 * x] = u[x]; uv[2 * x + 1] = v[x]; }
    }
    return im;
}

static inline uint32_t c8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint32_t)v; }
void image_yuv_materialize(Image *im) {
    if (!im->px && !im->yuv && im->gpu) {
        uint32_t *px = xmalloc((size_t)im->w * (size_t)im->h * 4);
        if (im->gpu->read(im->gpu, px, im->w, im->h)) im->px = px; else free(px);
        return;
    }
    if (im->px || !im->yuv) return;
    int w = im->w, h = im->h, cw = (w + 1) / 2; const uint8_t *Y = im->yuv, *UV = im->yuv + (size_t)w * (size_t)h;
    bool b709 = im->yuv_mat & 1, full = im->yuv_mat & 2;
    float kr = b709 ? 1.5748f : 1.402f, kgu = b709 ? 0.1873f : 0.344136f, kgv = b709 ? 0.4681f : 0.714136f, kb = b709 ? 1.8556f : 1.772f;
    float ys = full ? 1.f : 255.f / 219.f, yo = full ? 0 : 16, cs = full ? 1.f : 255.f / 224.f;
    uint32_t *px = xmalloc((size_t)w * (size_t)h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const uint8_t *c = UV + (size_t)(y / 2) * (size_t)cw * 2 + (size_t)(x / 2) * 2;
        float l = (Y[(size_t)y * (size_t)w + x] - yo) * ys, u = (c[0] - 128) * cs, v = (c[1] - 128) * cs;
        px[(size_t)y * (size_t)w + x] = 0xff000000u | c8(l + kr * v) << 16 | c8(l - kgu * u - kgv * v) << 8 | c8(l + kb * u);
    }
    im->px = px;
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

/* ---- embedded ICC profiles: RGB matrix/TRC profiles are converted to sRGB (out-of-gamut colours clip) ---- */
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static float s15(const uint8_t *p) { return (float)(int32_t)be32(p) / 65536.f; }
static const uint8_t *icc_tag(const uint8_t *d, size_t n, const char *sig, uint32_t *len) {
    uint32_t cnt = be32(d + 128);
    for (uint32_t i = 0; i < cnt && 132 + 12 * ((size_t)i + 1) <= n; i++) {
        const uint8_t *e = d + 132 + 12 * (size_t)i; uint32_t off = be32(e + 4), sz = be32(e + 8);
        if (!memcmp(e, sig, 4) && off < n && sz <= n - off) { *len = sz; return d + off; }
    }
    return NULL;
}
static bool icc_curve(const uint8_t *t, uint32_t n, float *lut) {
    if (n >= 12 && !memcmp(t, "curv", 4)) {
        uint32_t c = be32(t + 8);
        if (c == 0) { for (int i = 0; i < 256; i++) lut[i] = i / 255.f; return true; }
        if (c == 1) { if (n < 14) return false; float g = (float)((t[12] << 8) | t[13]) / 256.f; for (int i = 0; i < 256; i++) lut[i] = powf(i / 255.f, g); return true; }
        if ((size_t)n < 12 + 2 * (size_t)c) return false;
        for (int i = 0; i < 256; i++) {
            float x = i / 255.f * (float)(c - 1); uint32_t k = (uint32_t)x; if (k > c - 2) k = c - 2;
            const uint8_t *q = t + 12 + 2 * (size_t)k;
            float a = (float)((q[0] << 8) | q[1]) / 65535.f, b = (float)((q[2] << 8) | q[3]) / 65535.f;
            lut[i] = a + (b - a) * (x - (float)k);
        }
        return true;
    }
    if (n >= 12 && !memcmp(t, "para", 4)) {
        static const int np[] = { 1, 3, 4, 5, 7 };
        int ty = (t[8] << 8) | t[9];
        if (ty > 4 || n < 12 + 4u * (unsigned)np[ty]) return false;
        float p[7] = { 0 }; for (int i = 0; i < np[ty]; i++) p[i] = s15(t + 12 + 4 * i);
        float g = p[0], a = p[1], b = p[2], c = p[3], dd = ty == 1 || ty == 2 ? (a ? -b / a : 0) : p[4];
        for (int i = 0; i < 256; i++) {
            float x = i / 255.f, base = a * x + b, y;
            if (ty == 0) y = powf(x, g);
            else if (x >= dd) y = powf(base > 0 ? base : 0, g) + (ty == 2 ? c : ty == 4 ? p[5] : 0);
            else y = ty == 1 ? 0 : ty == 2 ? c : ty == 3 ? c * x : c * x + p[6];
            lut[i] = y;
        }
        return true;
    }
    return false;
}
typedef struct { float lut[3][256], m[9]; } IccXf;
/* false when the profile can't be used or is (close enough to) sRGB already */
static bool icc_parse(const uint8_t *d, size_t n, IccXf *x) {
    if (!d || n < 132 || memcmp(d + 16, "RGB ", 4)) return false;
    static const char *const xyz[3] = { "rXYZ", "gXYZ", "bXYZ" }, *const trc[3] = { "rTRC", "gTRC", "bTRC" };
    float P[9];
    for (int k = 0; k < 3; k++) {
        uint32_t l; const uint8_t *t = icc_tag(d, n, xyz[k], &l);
        if (!t || l < 20 || memcmp(t, "XYZ ", 4)) return false;
        for (int r = 0; r < 3; r++) P[r * 3 + k] = s15(t + 8 + 4 * r);
        if (!(t = icc_tag(d, n, trc[k], &l)) || !icc_curve(t, l, x->lut[k])) return false;
    }
    static const float S[9] = { 3.1338561f, -1.6168667f, -0.4906146f, -0.9787684f, 1.9161415f, 0.0334540f, 0.0719453f, -0.2289914f, 1.4052427f };
    float dm = 0, dc = 0;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
        float v = 0; for (int k = 0; k < 3; k++) v += S[r * 3 + k] * P[k * 3 + c];
        x->m[r * 3 + c] = v; dm = fmaxf(dm, fabsf(v - (r == c)));
    }
    for (int i = 0; i < 256; i++) {
        float v = i / 255.f, s = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
        for (int k = 0; k < 3; k++) dc = fmaxf(dc, fabsf(x->lut[k][i] - s));
    }
    return dm > 0.01f || dc > 0.004f;
}
/* straight-alpha ARGB pixels (before premultiplication) from the profile's space into sRGB */
static void icc_apply(uint32_t *px, size_t n, const IccXf *x) {
    static uint8_t enc[4097]; static bool init;
    if (!init) { for (int i = 0; i <= 4096; i++) { float v = i / 4096.f; enc[i] = (uint8_t)lroundf(255.f * (v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1 / 2.4f) - 0.055f)); } init = true; }
    uint32_t last = 0, lastv = 0; bool have = false;
    for (size_t i = 0; i < n; i++) {
        uint32_t p = px[i], rgb = p & 0xffffff;
        if (have && rgb == last) { px[i] = (p & 0xff000000u) | lastv; continue; }
        float r = x->lut[0][(p >> 16) & 255], g = x->lut[1][(p >> 8) & 255], b = x->lut[2][p & 255];
        uint32_t o = 0;
        for (int k = 0; k < 3; k++) {
            float v = x->m[k * 3] * r + x->m[k * 3 + 1] * g + x->m[k * 3 + 2] * b;
            o = o << 8 | enc[(int)lroundf((v < 0 ? 0 : v > 1 ? 1 : v) * 4096.f)];
        }
        last = rgb; lastv = o; have = true;
        px[i] = (p & 0xff000000u) | o;
    }
}
static void icc_convert(Image *im, const uint8_t *icc, size_t n) {
    IccXf x;
    if (im && icc_parse(icc, n, &x)) icc_apply(im->px, (size_t)im->w * (size_t)im->h, &x);
}
/* the zlib-compressed profile of a PNG iCCP chunk (malloc'd), or NULL */
static uint8_t *png_icc(const uint8_t *d, size_t n, size_t *len) {
    for (size_t p = 8; p + 12 <= n;) {
        uint32_t L = be32(d + p); const uint8_t *t = d + p + 4;
        if (L > n - p - 12 || !memcmp(t, "IDAT", 4)) break;
        if (!memcmp(t, "iCCP", 4)) {
            const uint8_t *c = t + 4; size_t k = 0;
            while (k < L && c[k]) k++;
            if (k + 2 >= L) return NULL;
            for (uLongf cap = 1 << 16; cap <= (1u << 22); cap *= 4) {
                uint8_t *out = xmalloc(cap); uLongf got = cap;
                int r = uncompress(out, &got, c + k + 2, (uLong)(L - k - 2));
                if (r == Z_OK) { *len = got; return out; }
                free(out);
                if (r != Z_BUF_ERROR) return NULL;
            }
            return NULL;
        }
        p += 12 + (size_t)L;
    }
    return NULL;
}

static Image *dec_png(const uint8_t *d, size_t n) {
    png_image p; memset(&p, 0, sizeof p); p.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&p, d, n)) return NULL;
    p.format = PNG_FORMAT_BGRA;
    Image *im = img_new((int)p.width, (int)p.height);
    if (!im) { png_image_free(&p); return NULL; }
    if (!png_image_finish_read(&p, NULL, im->px, 0, NULL)) { image_unref(im); return NULL; }
    size_t il = 0; uint8_t *icc = png_icc(d, n, &il);
    if (icc) { icc_convert(im, icc, il); free(icc); }
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
    jpeg_save_markers(&c, JPEG_APP0 + 2, 0xFFFF);
    jpeg_read_header(&c, TRUE);
    c.out_color_space = JCS_EXT_BGRA;
    jpeg_start_decompress(&c);
    im = img_new((int)c.output_width, (int)c.output_height);
    if (!im) { jpeg_destroy_decompress(&c); return NULL; }
    while (c.output_scanline < c.output_height) { JSAMPROW row = (JSAMPROW)(im->px + (size_t)c.output_scanline * (size_t)im->w); jpeg_read_scanlines(&c, &row, 1); }
    JOCTET *icc = NULL; unsigned il = 0;
    if (jpeg_read_icc_profile(&c, &icc, &il) && icc) { icc_convert(im, icc, il); free(icc); }
    jpeg_finish_decompress(&c); jpeg_destroy_decompress(&c);
    return im;
}

static Image *dec_webp(const uint8_t *d, size_t n) {
    int w, h; if (!WebPGetInfo(d, n, &w, &h)) return NULL;
    Image *im = img_new(w, h); if (!im) return NULL;
    if (!WebPDecodeBGRAInto(d, n, (uint8_t *)im->px, (size_t)w * (size_t)h * 4, w * 4)) { image_unref(im); return NULL; }
    for (size_t p = 12; p + 8 <= n;) {   /* an extended (VP8X) file's ICCP chunk */
        uint32_t L = (uint32_t)d[p + 4] | (uint32_t)d[p + 5] << 8 | (uint32_t)d[p + 6] << 16 | (uint32_t)d[p + 7] << 24;
        if (L > n - p - 8) break;
        if (!memcmp(d + p, "ICCP", 4)) { icc_convert(im, d + p + 8, L); break; }
        p += 8 + (size_t)L + (L & 1);
    }
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

Image *image_decode_bitmap(const uint8_t *d, size_t n) {
    if (!d || n < 8) return NULL;
    if (!memcmp(d, "\x89PNG", 4)) return dec_png(d, n);
    if (d[0] == 0xFF && d[1] == 0xD8) return dec_jpeg(d, n);
    if (n > 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return dec_webp(d, n);
    if (!memcmp(d, "GIF8", 4)) return dec_gif(d, n);
    return NULL;
}
Image *image_decode(const uint8_t *d, size_t n) {
    Image *im = image_decode_bitmap(d, n);
    return im || !d || n < 8 || !is_svg(d, n) ? im : dec_svg(d, n);
}
