/* <canvas> natives: 2D raster ops on the element's CanvasBuf (premultiplied ARGB) */
#include "js_int.h"
#include <cmath>
#include <vector>
extern "C" {
struct CanvasBuf;
CanvasBuf *cvbuf_for(Node *n, bool create);
void cvbuf_resize(CanvasBuf *c, int w, int h);
void cvbuf_touch(CanvasBuf *c);
uint32_t *cvbuf_px(CanvasBuf *c, int *w, int *h);
const uint32_t *cvbuf_src(Node *n, int *w, int *h);
}
using FCI = v8::FunctionCallbackInfo<v8::Value>;
#define FN(nm) static void n_##nm(const FCI &a)
#define CTX JsCtx *c = jctx(a.GetIsolate()); v8::Isolate *iso = c->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext(); (void)ctx
#define NUM(i) a[i]->NumberValue(ctx).FromMaybe(0)
#define CV(i) CanvasBuf *cb = cvbuf_for(junwrap(a[i]), true); if (!cb) return; int W, H; uint32_t *PX = cvbuf_px(cb, &W, &H); (void)PX

static inline uint32_t prem(int r, int g, int b, int al) { return ((uint32_t)al << 24) | ((uint32_t)(r * al / 255) << 16) | ((uint32_t)(g * al / 255) << 8) | (uint32_t)(b * al / 255); }
static inline uint32_t over(uint32_t d, uint32_t s, int cov) {   /* s premultiplied, cov 0..256 */
    if (cov <= 0) return d;
    uint32_t sa = ((s >> 24) * (uint32_t)cov) >> 8; if (!sa) return d;
    uint32_t ia = 255 - sa, o = 0;
    for (int sh = 0; sh < 32; sh += 8) { uint32_t sc = (((s >> sh) & 255) * (uint32_t)cov) >> 8, dc = (d >> sh) & 255; o |= ((sc + (dc * ia + 127) / 255) & 255) << sh; }
    return o;
}
static uint32_t rgba_arg(const FCI &a, v8::Local<v8::Context> ctx, int i, int *al) {
    int r = (int)NUM(i), g = (int)NUM(i + 1), b = (int)NUM(i + 2); *al = (int)NUM(i + 3);
    return prem(r & 255, g & 255, b & 255, *al < 0 ? 0 : *al > 255 ? 255 : *al);
}
FN(cvSize) { CTX; CV(0); cvbuf_resize(cb, (int)NUM(1), (int)NUM(2)); }
/* fillRect(el, x, y, w, h, r, g, b, a, mode): mode 0 = source-over, 1 = copy (clearRect passes a=0) */
FN(cvRect) {
    CTX; CV(0);
    double x = NUM(1), y = NUM(2), w = NUM(3), h = NUM(4); int al; uint32_t s = rgba_arg(a, ctx, 5, &al); int mode = (int)NUM(9);
    if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
    int x0 = std::max(0, (int)floor(x)), y0 = std::max(0, (int)floor(y)), x1 = std::min(W, (int)ceil(x + w)), y1 = std::min(H, (int)ceil(y + h));
    for (int yy = y0; yy < y1; yy++) {
        double cy = std::min(y + h, yy + 1.0) - std::max(y, (double)yy);
        uint32_t *row = PX + (size_t)yy * W;
        for (int xx = x0; xx < x1; xx++) {
            double cx = std::min(x + w, xx + 1.0) - std::max(x, (double)xx); int cov = (int)(cx * cy * 256 + 0.5);
            if (mode == 1) row[xx] = cov >= 255 ? s : over(row[xx] & 0, s, cov) | 0;
            else row[xx] = over(row[xx], s, cov);
        }
    }
    cvbuf_touch(cb);
}
/* fillPath(el, Float64Array pts(x,y pairs), Int32Array counts, r, g, b, a, evenodd, mode): 4x4 supersampled scanline fill */
FN(cvPath) {
    CTX; CV(0);
    if (!a[1]->IsFloat64Array() || !a[2]->IsInt32Array()) return;
    auto pa = a[1].As<v8::Float64Array>(); auto ca = a[2].As<v8::Int32Array>();
    std::vector<double> P(pa->Length()); pa->CopyContents(P.data(), P.size() * 8);
    std::vector<int> C(ca->Length()); ca->CopyContents(C.data(), C.size() * 4);
    int al; uint32_t s = rgba_arg(a, ctx, 3, &al); bool eo = a[7]->BooleanValue(iso); int mode = (int)NUM(8);
    struct E { double x0, y0, x1, y1; int dir; };
    std::vector<E> es; double miny = 1e30, maxy = -1e30;
    size_t off = 0;
    for (int n : C) {
        if (n < 2 || off + (size_t)n * 2 > P.size()) { off += (size_t)std::max(n, 0) * 2; continue; }
        for (int i = 0; i < n; i++) {
            double ax = P[off + i * 2], ay = P[off + i * 2 + 1], bx = P[off + ((i + 1) % n) * 2], by = P[off + ((i + 1) % n) * 2 + 1];
            if (ay == by) continue;
            es.push_back(ay < by ? E{ ax, ay, bx, by, 1 } : E{ bx, by, ax, ay, -1 });
            miny = std::min(miny, std::min(ay, by)); maxy = std::max(maxy, std::max(ay, by));
        }
        off += (size_t)n * 2;
    }
    if (es.empty()) return;
    int Y0 = std::max(0, (int)floor(miny)), Y1 = std::min(H, (int)ceil(maxy));
    std::vector<int> cov(W + 2);
    std::vector<std::pair<double, int>> xs;
    for (int y = Y0; y < Y1; y++) {
        std::fill(cov.begin(), cov.end(), 0); int lo = W, hi = -1;
        for (int k = 0; k < 4; k++) {
            double sy = y + (k + 0.5) / 4; xs.clear();
            for (auto &e : es) if (sy >= e.y0 && sy < e.y1) xs.push_back({ e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir });
            std::sort(xs.begin(), xs.end());
            int wnd = 0;
            for (size_t i = 0; i + 1 < xs.size() + 1 && i < xs.size(); i++) {
                wnd = eo ? wnd ^ 1 : wnd + xs[i].second;
                if (!wnd || i + 1 >= xs.size()) continue;
                double xa = std::max(0.0, xs[i].first), xb = std::min((double)W, xs[i + 1].first);
                if (xb <= xa) continue;
                int ia = (int)xa, ib = (int)xb; lo = std::min(lo, ia); hi = std::max(hi, ib);
                if (ia == ib) { cov[ia] += (int)((xb - xa) * 64); continue; }
                cov[ia] += (int)((ia + 1 - xa) * 64);
                for (int xx = ia + 1; xx < ib; xx++) cov[xx] += 64;
                if (ib < W) cov[ib] += (int)((xb - ib) * 64);
            }
        }
        uint32_t *row = PX + (size_t)y * W;
        for (int xx = std::max(lo, 0); xx <= std::min(hi, W - 1); xx++) {
            int cv = std::min(cov[xx], 256); if (!cv) continue;
            row[xx] = mode == 1 ? over(0, s, cv) : over(row[xx], s, cv);
        }
    }
    cvbuf_touch(cb);
}
/* putImageData(el, rgbaBytes, sw, sh, dx, dy) */
FN(cvPut) {
    CTX; CV(0);
    if (!a[1]->IsArrayBufferView()) return;
    auto v = a[1].As<v8::ArrayBufferView>(); const uint8_t *p = (const uint8_t *)v->Buffer()->Data() + v->ByteOffset();
    int sw = (int)NUM(2), sh = (int)NUM(3), dx = (int)NUM(4), dy = (int)NUM(5);
    if ((size_t)sw * sh * 4 > v->ByteLength()) return;
    for (int y = 0; y < sh; y++) { int ty = dy + y; if (ty < 0 || ty >= H) continue;
        for (int x = 0; x < sw; x++) { int tx = dx + x; if (tx < 0 || tx >= W) continue; const uint8_t *q = p + ((size_t)y * sw + x) * 4; PX[(size_t)ty * W + tx] = prem(q[0], q[1], q[2], q[3]); } }
    cvbuf_touch(cb);
}
/* getImageData(el, x, y, w, h) -> ArrayBuffer of un-premultiplied RGBA */
FN(cvGet) {
    CTX; CV(0);
    int sx = (int)NUM(1), sy = (int)NUM(2), w = std::max(0, (int)NUM(3)), h = std::max(0, (int)NUM(4));
    auto ab = v8::ArrayBuffer::New(iso, (size_t)w * h * 4); uint8_t *o = (uint8_t *)ab->Data(); memset(o, 0, (size_t)w * h * 4);
    for (int y = 0; y < h; y++) { int yy = sy + y; if (yy < 0 || yy >= H) continue;
        for (int x = 0; x < w; x++) { int xx = sx + x; if (xx < 0 || xx >= W) continue; uint32_t p = PX[(size_t)yy * W + xx]; uint32_t al = p >> 24; uint8_t *q = o + ((size_t)y * w + x) * 4;
            if (al) { q[0] = (uint8_t)std::min(255u, ((p >> 16) & 255) * 255 / al); q[1] = (uint8_t)std::min(255u, ((p >> 8) & 255) * 255 / al); q[2] = (uint8_t)std::min(255u, (p & 255) * 255 / al); } q[3] = (uint8_t)al; } }
    a.GetReturnValue().Set(ab);
}
/* drawImage(el, srcNode, sx, sy, sw, sh, dx, dy, dw, dh, alpha): bilinear when scaling */
FN(cvDraw) {
    CTX; CV(0);
    Node *sn = junwrap(a[1]); if (!sn) return;
    struct { const uint32_t *px; int w, h; } I, *im = &I;
    I.px = cvbuf_src(sn, &I.w, &I.h);
    if (!I.px || I.w <= 0 || I.h <= 0) { a.GetReturnValue().Set(false); return; }
    double sx = NUM(2), sy = NUM(3), sw = NUM(4), sh = NUM(5), dx = NUM(6), dy = NUM(7), dw = NUM(8), dh = NUM(9); int ga = (int)(NUM(10) * 256);
    if (sw <= 0) { sx = 0; sy = 0; sw = im->w; sh = im->h; }
    if (dw == 0 || dh == 0) return;
    int x0 = std::max(0, (int)floor(std::min(dx, dx + dw))), x1 = std::min(W, (int)ceil(std::max(dx, dx + dw)));
    int y0 = std::max(0, (int)floor(std::min(dy, dy + dh))), y1 = std::min(H, (int)ceil(std::max(dy, dy + dh)));
    for (int y = y0; y < y1; y++) { double v = sy + ((y + 0.5 - dy) / dh) * sh - 0.5; int iy = (int)floor(v); double fy = v - iy;
        if (v < sy - 0.5 || v > sy + sh - 0.5) continue;
        int ya = std::clamp(iy, 0, im->h - 1), yb = std::clamp(iy + 1, 0, im->h - 1);
        uint32_t *row = PX + (size_t)y * W;
        for (int x = x0; x < x1; x++) { double u = sx + ((x + 0.5 - dx) / dw) * sw - 0.5; if (u < sx - 0.5 || u > sx + sw - 0.5) continue; int ix = (int)floor(u); double fx = u - ix;
            int xa = std::clamp(ix, 0, im->w - 1), xb = std::clamp(ix + 1, 0, im->w - 1);
            uint32_t p00 = im->px[(size_t)ya * im->w + xa], p01 = im->px[(size_t)ya * im->w + xb], p10 = im->px[(size_t)yb * im->w + xa], p11 = im->px[(size_t)yb * im->w + xb], s = 0;
            for (int shf = 0; shf < 32; shf += 8) { double c0 = ((p00 >> shf) & 255) * (1 - fx) + ((p01 >> shf) & 255) * fx, c1 = ((p10 >> shf) & 255) * (1 - fx) + ((p11 >> shf) & 255) * fx; s |= (uint32_t)(c0 * (1 - fy) + c1 * fy + 0.5) << shf; }
            row[x] = over(row[x], s, ga);
        } }
    cvbuf_touch(cb); a.GetReturnValue().Set(true);
}
FN(cvW) { CTX; CanvasBuf *cb = cvbuf_for(junwrap(a[0]), false); int W = -1, H; if (cb) cvbuf_px(cb, &W, &H); a.GetReturnValue().Set(W); }
void js_install_canvas(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(cvSize); REG(cvRect); REG(cvPath); REG(cvPut); REG(cvGet); REG(cvDraw); REG(cvW);
#undef REG
}
