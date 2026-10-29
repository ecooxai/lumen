/* WebGL 1 and 2 natives (the `N.gl` object) over OpenGL ES 3: the prelude's WebGLRenderingContext keeps
   the object wrappers and WebGL rules, these pass calls through after gl_get made the canvas's context
   current. The default framebuffer is an FBO (src/gpu/webgl.c), so framebuffer 0 maps to it here. */
#include "js_int.h"
extern "C" {
#include "../gpu/webgl.h"
}
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <cstring>
#include <vector>

using FCI = v8::FunctionCallbackInfo<v8::Value>;

namespace {

inline double an(const FCI &a, int i) {
    if (i >= a.Length()) return 0;
    v8::Local<v8::Value> v = a[i];
    if (v->IsNumber()) return v.As<v8::Number>()->Value();
    if (v->IsBoolean()) return v->IsTrue() ? 1 : 0;
    if (v->IsNullOrUndefined()) return 0;
    return v->NumberValue(a.GetIsolate()->GetCurrentContext()).FromMaybe(0);
}
inline GLint ai(const FCI &a, int i) { double d = an(a, i); return std::isfinite(d) ? (GLint)(int64_t)d : 0; }
#define E(i) ((GLenum)(uint32_t)(int64_t)an(a, i))
#define I(i) ai(a, i)
#define U(i) ((GLuint)(int64_t)an(a, i))
#define F(i) ((GLfloat)an(a, i))
#define B(i) ((GLboolean)(i < a.Length() && a[i]->BooleanValue(a.GetIsolate())))
#define P(i) ((const void *)(intptr_t)(int64_t)an(a, i))
#define GLC                                          \
    JsCtx *c = jctx(a.GetIsolate());                 \
    v8::Isolate *iso = c->iso; (void)iso;            \
    GlCtx *g = gl_get(c, ai(a, 0));                  \
    if (!g) return
#define RET(...) a.GetReturnValue().Set(__VA_ARGS__)
#define FN(nm) void g_##nm(const FCI &a)

bool view_of(v8::Local<v8::Value> v, char **p, size_t *n, size_t *elem) {
    if (v->IsArrayBufferView()) {
        auto vw = v.As<v8::ArrayBufferView>();
        *p = (char *)vw->Buffer()->Data() + vw->ByteOffset(); *n = vw->ByteLength();
        *elem = v->IsFloat64Array() ? 8 : v->IsFloat32Array() || v->IsInt32Array() || v->IsUint32Array() ? 4 : v->IsInt16Array() || v->IsUint16Array() ? 2 : 1;
        return true;
    }
    if (v->IsArrayBuffer()) { auto ab = v.As<v8::ArrayBuffer>(); *p = (char *)ab->Data(); *n = ab->ByteLength(); *elem = 1; return true; }
    return false;
}
/* the [srcOffset, srcOffset + length) elements of a view, as bytes */
bool view_range(const FCI &a, int vi, int oi, int li, char **p, size_t *n) {
    size_t elem;
    if (vi >= a.Length() || !view_of(a[vi], p, n, &elem)) return false;
    size_t off = oi < a.Length() ? (size_t)ai(a, oi) : 0, cnt = *n / elem;
    if (off > cnt) return false;
    size_t len = li < a.Length() && !a[li]->IsUndefined() && ai(a, li) > 0 ? (size_t)ai(a, li) : cnt - off;
    if (off + len > cnt) return false;
    *p += off * elem; *n = len * elem;
    return true;
}
std::vector<std::string> strs(const FCI &a, int i) {
    std::vector<std::string> out;
    if (i >= a.Length() || !a[i]->IsArray()) return out;
    auto arr = a[i].As<v8::Array>(); auto ctx = a.GetIsolate()->GetCurrentContext();
    for (uint32_t k = 0; k < arr->Length(); k++) { v8::Local<v8::Value> v; if (arr->Get(ctx, k).ToLocal(&v)) out.push_back(jcstr(a.GetIsolate(), v)); }
    return out;
}
std::vector<GLuint> uints(const FCI &a, int i) {
    std::vector<GLuint> out;
    if (i >= a.Length() || !a[i]->IsArray()) return out;
    auto arr = a[i].As<v8::Array>(); auto ctx = a.GetIsolate()->GetCurrentContext();
    for (uint32_t k = 0; k < arr->Length(); k++) { v8::Local<v8::Value> v; out.push_back(arr->Get(ctx, k).ToLocal(&v) ? (GLuint)(int64_t)v->NumberValue(ctx).FromMaybe(0) : 0); }
    return out;
}
v8::Local<v8::Value> nums(v8::Isolate *iso, const double *v, int n) {
    std::vector<v8::Local<v8::Value>> e((size_t)n);
    for (int i = 0; i < n; i++) e[(size_t)i] = v8::Number::New(iso, v[i]);
    return v8::Array::New(iso, e.data(), (size_t)n);
}
template <class T, class A> v8::Local<v8::Value> typed(v8::Isolate *iso, const T *v, int n) {
    auto ab = v8::ArrayBuffer::New(iso, sizeof(T) * (size_t)n);
    memcpy(ab->Data(), v, sizeof(T) * (size_t)n);
    return A::New(ab, 0, (size_t)n);
}

/* syncs are pointers: handed to JS as small ids */
std::map<uint32_t, GLsync> g_syncs;
uint32_t g_next_sync = 1;

/* WebGL 1 unsized formats and OES/EXT enums to what ES 3 takes */
void fix_format(GlCtx *g, GLint *ifmt, GLenum *fmt, GLenum *type) {
    if (*type == 0x8D61) *type = GL_HALF_FLOAT;   /* HALF_FLOAT_OES */
    if (*fmt == 0x8C40) *fmt = GL_RGB;            /* SRGB_EXT */
    if (*fmt == 0x8C42) *fmt = GL_RGBA;           /* SRGB_ALPHA_EXT */
    GLint f = *ifmt;
    if (f == 0x8C40) *ifmt = GL_SRGB8;
    else if (f == 0x8C42) *ifmt = GL_SRGB8_ALPHA8;
    else if (f == GL_RGBA && *type == GL_FLOAT) *ifmt = GL_RGBA32F;
    else if (f == GL_RGB && *type == GL_FLOAT) *ifmt = GL_RGB32F;
    else if (f == GL_RGBA && *type == GL_HALF_FLOAT) *ifmt = GL_RGBA16F;
    else if (f == GL_RGB && *type == GL_HALF_FLOAT) *ifmt = GL_RGB16F;
    else if (f == GL_DEPTH_COMPONENT && *type == GL_UNSIGNED_SHORT) *ifmt = GL_DEPTH_COMPONENT16;
    else if (f == GL_DEPTH_COMPONENT && *type == GL_UNSIGNED_INT) *ifmt = GL_DEPTH_COMPONENT24;
    else if (f == GL_DEPTH_STENCIL && *type == GL_UNSIGNED_INT_24_8) *ifmt = GL_DEPTH24_STENCIL8;
    (void)g;
}
int channels(GLenum fmt) {
    switch (fmt) {
    case GL_RGBA: case GL_RGBA_INTEGER: return 4;
    case GL_RGB: case GL_RGB_INTEGER: return 3;
    case GL_RG: case GL_RG_INTEGER: case GL_LUMINANCE_ALPHA: return 2;
    default: return 1;
    }
}
int type_size(GLenum type) {
    switch (type) {
    case GL_FLOAT: case GL_UNSIGNED_INT: case GL_INT: return 4;
    case GL_HALF_FLOAT: case GL_UNSIGNED_SHORT: case GL_SHORT: return 2;
    default: return 1;
    }
}
/* packed types hold a whole pixel */
bool packed(GLenum type) {
    return type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1 ||
           type == GL_UNSIGNED_INT_2_10_10_10_REV || type == GL_UNSIGNED_INT_10F_11F_11F_REV || type == GL_UNSIGNED_INT_5_9_9_9_REV || type == GL_UNSIGNED_INT_24_8 || type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
}
size_t pixel_size(GLenum fmt, GLenum type) {
    if (type == GL_FLOAT_32_UNSIGNED_INT_24_8_REV) return 8;
    if (packed(type)) return type == GL_UNSIGNED_INT_2_10_10_10_REV || type == GL_UNSIGNED_INT_10F_11F_11F_REV || type == GL_UNSIGNED_INT_5_9_9_9_REV || type == GL_UNSIGNED_INT_24_8 ? 4 : 2;
    return (size_t)channels(fmt) * (size_t)type_size(type);
}
uint16_t half(float f) {
    uint32_t x; memcpy(&x, &f, 4);
    uint32_t s = (x >> 16) & 0x8000, e = (x >> 23) & 0xff, m = x & 0x7fffff;
    if (e < 113) return (uint16_t)s;
    if (e > 142) return (uint16_t)(s | 0x7c00);
    return (uint16_t)(s | (e - 112) << 10 | m >> 13);
}

struct Unpack { GLint align, row, height, skip_rows, skip_px, skip_img, pbo; };
Unpack unpack_plain() {
    Unpack u;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &u.align); glGetIntegerv(GL_UNPACK_ROW_LENGTH, &u.row); glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &u.height);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &u.skip_rows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &u.skip_px); glGetIntegerv(GL_UNPACK_SKIP_IMAGES, &u.skip_img);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &u.pbo);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
    if (u.pbo) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    return u;
}
void unpack_restore(const Unpack &u) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, u.align); glPixelStorei(GL_UNPACK_ROW_LENGTH, u.row); glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, u.height);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, u.skip_rows); glPixelStorei(GL_UNPACK_SKIP_PIXELS, u.skip_px); glPixelStorei(GL_UNPACK_SKIP_IMAGES, u.skip_img);
    if (u.pbo) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)u.pbo);
}

/* RGBA8 rows top down (an element, ImageData) to a texture in the format and type the page asked for */
void upload_rgba(GlCtx *g, bool sub, GLenum target, GLint level, GLint ifmt, GLint x, GLint y, GLint z, int w, int h,
                 GLenum fmt, GLenum type, uint8_t *rgba, bool flip, bool three) {
    fix_format(g, &ifmt, &fmt, &type);
    if (flip) for (int r = 0; r < h / 2; r++) {
        uint8_t *p = rgba + (size_t)r * w * 4, *q = rgba + (size_t)(h - 1 - r) * w * 4;
        for (int k = 0; k < w * 4; k++) { uint8_t t = p[k]; p[k] = q[k]; q[k] = t; }
    }
    size_t np = (size_t)w * h;
    std::vector<uint8_t> buf;
    const void *data = rgba;
    int ch = channels(fmt);
    /* which source channel each output channel takes */
    int map[4] = { 0, 1, 2, 3 };
    if (fmt == GL_ALPHA) map[0] = 3;
    else if (fmt == GL_LUMINANCE_ALPHA) map[1] = 3;
    if (type == GL_UNSIGNED_BYTE && fmt != GL_RGBA) {
        buf.resize(np * (size_t)ch);
        for (size_t i = 0; i < np; i++) for (int k = 0; k < ch; k++) buf[i * ch + k] = rgba[i * 4 + map[k]];
        data = buf.data();
    } else if (type == GL_FLOAT || type == GL_HALF_FLOAT) {
        size_t ts = type == GL_FLOAT ? 4 : 2;
        buf.resize(np * (size_t)ch * ts);
        for (size_t i = 0; i < np; i++) for (int k = 0; k < ch; k++) {
            float f = rgba[i * 4 + map[k]] / 255.0f;
            if (ts == 4) memcpy(&buf[(i * ch + k) * 4], &f, 4);
            else { uint16_t hv = half(f); memcpy(&buf[(i * ch + k) * 2], &hv, 2); }
        }
        data = buf.data();
    } else if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1) {
        buf.resize(np * 2);
        for (size_t i = 0; i < np; i++) {
            const uint8_t *s = rgba + i * 4; uint16_t v;
            if (type == GL_UNSIGNED_SHORT_5_6_5) v = (uint16_t)((s[0] >> 3) << 11 | (s[1] >> 2) << 5 | s[2] >> 3);
            else if (type == GL_UNSIGNED_SHORT_4_4_4_4) v = (uint16_t)((s[0] >> 4) << 12 | (s[1] >> 4) << 8 | (s[2] >> 4) << 4 | s[3] >> 4);
            else v = (uint16_t)((s[0] >> 3) << 11 | (s[1] >> 3) << 6 | (s[2] >> 3) << 1 | s[3] >> 7);
            memcpy(&buf[i * 2], &v, 2);
        }
        data = buf.data();
    } else if (type != GL_UNSIGNED_BYTE) { fmt = GL_RGBA; type = GL_UNSIGNED_BYTE; if (!sub) ifmt = GL_RGBA8; }
    Unpack u = unpack_plain();
    if (three) {
        if (sub) glTexSubImage3D(target, level, x, y, z, w, h, 1, fmt, type, data);
        else glTexImage3D(target, level, ifmt, w, h, 1, 0, fmt, type, data);
    } else if (sub) glTexSubImage2D(target, level, x, y, w, h, fmt, type, data);
    else glTexImage2D(target, level, ifmt, w, h, 0, fmt, type, data);
    unpack_restore(u);
}

/* the bytes a w x h x d upload reads, by the current unpack state; flips rows when asked */
size_t upload_bytes(GLenum fmt, GLenum type, int w, int h, int d) {
    GLint align = 4, rowlen = 0, imgh = 0; glGetIntegerv(GL_UNPACK_ALIGNMENT, &align); glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowlen); glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &imgh);
    size_t ps = pixel_size(fmt, type), row = (size_t)(rowlen > 0 ? rowlen : w) * ps;
    if (align > 1) row = (row + (size_t)align - 1) / (size_t)align * (size_t)align;
    size_t rows = (size_t)(imgh > 0 ? imgh : h);
    if (w <= 0 || h <= 0 || d <= 0) return 0;
    return row * (rows * (size_t)(d - 1) + (size_t)(h - 1)) + (size_t)w * ps;
}

FN(create) {
    JsCtx *c = jctx(a.GetIsolate());
    Node *n = junwrap(a[0]);
    if (!n) { RET(-1); return; }
    RET(gl_create(c, n, ai(a, 1), ai(a, 2), ai(a, 3), ai(a, 4)));
}
FN(available) { RET(gl_available()); }
FN(renderer) { RET(jstr(a.GetIsolate(), gl_renderer())); }
FN(exts) { GLC; RET(jstr(iso, g->exts ? g->exts : "")); }
FN(size) { GLC; double v[3] = { (double)g->w, (double)g->h, (double)g->samples }; RET(nums(iso, v, 3)); }
FN(resize) { GLC; gl_resize(g, I(1), I(2)); }
FN(present) { GLC; gl_present(g); }
FN(capture) { GLC; RET(gl_capture(g)); }
FN(close) { JsCtx *c = jctx(a.GetIsolate()); gl_close(c, ai(a, 0)); }

/* objects: 0 buffer, 1 framebuffer, 2 renderbuffer, 3 texture, 4 program, 5 shader (type), 6 vertex array, 7 query, 8 sampler, 9 transform feedback */
FN(gen) {
    GLC; GLuint n = 0;
    switch (I(1)) {
    case 0: glGenBuffers(1, &n); break;
    case 1: glGenFramebuffers(1, &n); break;
    case 2: glGenRenderbuffers(1, &n); break;
    case 3: glGenTextures(1, &n); break;
    case 4: n = glCreateProgram(); break;
    case 5: n = glCreateShader(E(2)); break;
    case 6: glGenVertexArrays(1, &n); break;
    case 7: glGenQueries(1, &n); break;
    case 8: glGenSamplers(1, &n); break;
    case 9: glGenTransformFeedbacks(1, &n); break;
    }
    RET((double)n);
}
FN(del) {
    GLC; GLuint n = U(2);
    switch (I(1)) {
    case 0: glDeleteBuffers(1, &n); break;
    case 1: glDeleteFramebuffers(1, &n); break;
    case 2: glDeleteRenderbuffers(1, &n); break;
    case 3: glDeleteTextures(1, &n); break;
    case 4: glDeleteProgram(n); break;
    case 5: glDeleteShader(n); break;
    case 6: glDeleteVertexArrays(1, &n); break;
    case 7: glDeleteQueries(1, &n); break;
    case 8: glDeleteSamplers(1, &n); break;
    case 9: glDeleteTransformFeedbacks(1, &n); break;
    }
}
FN(bindFramebuffer) {
    GLC; GLenum t = E(1); GLuint n = U(2);
    if (t == GL_FRAMEBUFFER || t == GL_DRAW_FRAMEBUFFER) g->draw_default = !n;
    if (t == GL_FRAMEBUFFER || t == GL_READ_FRAMEBUFFER) g->read_default = !n;
    glBindFramebuffer(t, n ? n : g->fb);
}
/* reading the default framebuffer: from its resolved copy when multisampled */
struct ReadDefault {
    GlCtx *g; bool on;
    explicit ReadDefault(GlCtx *gc) : g(gc), on(gc->read_default && gc->samples) { if (on) { gl_resolve(g); glBindFramebuffer(GL_READ_FRAMEBUFFER, g->rfb); } }
    ~ReadDefault() { if (on) glBindFramebuffer(GL_READ_FRAMEBUFFER, g->fb); }
};
FN(copyTexImage2D) { GLC; ReadDefault r(g); glCopyTexImage2D(E(1), I(2), E(3), I(4), I(5), I(6), I(7), I(8)); }
FN(copyTexSubImage2D) { GLC; ReadDefault r(g); glCopyTexSubImage2D(E(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8)); }
FN(copyTexSubImage3D) { GLC; ReadDefault r(g); glCopyTexSubImage3D(E(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), I(9)); }
FN(readBuffer) { GLC; GLenum b = E(1); glReadBuffer(b == GL_BACK && g->read_default ? GL_COLOR_ATTACHMENT0 : b); }
FN(drawBuffers) {
    GLC; std::vector<GLuint> v = uints(a, 1);
    std::vector<GLenum> e(v.begin(), v.end());
    if (g->draw_default) for (auto &x : e) if (x == GL_BACK) x = GL_COLOR_ATTACHMENT0;
    glDrawBuffers((GLsizei)e.size(), e.data());
}
void invalidate(const FCI &a, bool sub) {
    GLC; std::vector<GLuint> v = uints(a, 2);
    std::vector<GLenum> e(v.begin(), v.end());
    bool def = E(1) == GL_READ_FRAMEBUFFER ? g->read_default : g->draw_default;
    if (def) for (auto &x : e) x = x == GL_COLOR ? GL_COLOR_ATTACHMENT0 : x == GL_DEPTH ? GL_DEPTH_ATTACHMENT : x == GL_STENCIL ? GL_STENCIL_ATTACHMENT : x;
    if (sub) glInvalidateSubFramebuffer(E(1), (GLsizei)e.size(), e.data(), I(3), I(4), I(5), I(6));
    else glInvalidateFramebuffer(E(1), (GLsizei)e.size(), e.data());
}
FN(invalidateFramebuffer) { invalidate(a, false); }
FN(invalidateSubFramebuffer) { invalidate(a, true); }
FN(renderbufferStorage) { GLC; GLenum f = E(2); glRenderbufferStorage(E(1), f == GL_DEPTH_STENCIL ? GL_DEPTH24_STENCIL8 : f, I(3), I(4)); }
FN(renderbufferStorageMultisample) { GLC; GLenum f = E(3); glRenderbufferStorageMultisample(E(1), I(2), f == GL_DEPTH_STENCIL ? GL_DEPTH24_STENCIL8 : f, I(4), I(5)); }

FN(shaderSource) { GLC; std::string s = jcstr(iso, a[2]); const char *p = s.c_str(); GLint n = (GLint)s.size(); glShaderSource(U(1), 1, &p, &n); }
FN(bindAttribLocation) { GLC; glBindAttribLocation(U(1), U(2), jcstr(iso, a[3]).c_str()); }
FN(getAttribLocation) { GLC; RET(glGetAttribLocation(U(1), jcstr(iso, a[2]).c_str())); }
FN(getUniformLocation) { GLC; RET(glGetUniformLocation(U(1), jcstr(iso, a[2]).c_str())); }
FN(getFragDataLocation) { GLC; RET(glGetFragDataLocation(U(1), jcstr(iso, a[2]).c_str())); }
FN(getUniformBlockIndex) { GLC; RET((double)glGetUniformBlockIndex(U(1), jcstr(iso, a[2]).c_str())); }
std::string info_log(GLuint o, bool prog) {
    GLint n = 0;
    if (prog) glGetProgramiv(o, GL_INFO_LOG_LENGTH, &n); else glGetShaderiv(o, GL_INFO_LOG_LENGTH, &n);
    std::string s(n > 0 ? (size_t)n : 0, '\0');
    GLsizei got = 0;
    if (n > 0) { if (prog) glGetProgramInfoLog(o, n, &got, &s[0]); else glGetShaderInfoLog(o, n, &got, &s[0]); }
    s.resize((size_t)got);
    return s;
}
FN(getShaderInfoLog) { GLC; RET(jstr(iso, info_log(U(1), false).c_str())); }
FN(getProgramInfoLog) { GLC; RET(jstr(iso, info_log(U(1), true).c_str())); }
FN(getShaderParameter) { GLC; GLint v = 0; glGetShaderiv(U(1), E(2), &v); RET(v); }
FN(getProgramParameter) { GLC; GLint v = 0; glGetProgramiv(U(1), E(2), &v); RET(v); }
FN(getAttachedShaders) {
    GLC; GLuint s[8]; GLsizei n = 0; glGetAttachedShaders(U(1), 8, &n, s);
    double v[8]; for (int i = 0; i < n; i++) v[i] = s[i];
    RET(nums(iso, v, n));
}
v8::Local<v8::Value> active_info(v8::Isolate *iso, GLint size, GLenum type, const char *name) {
    v8::Local<v8::Value> e[3] = { v8::Number::New(iso, size), v8::Number::New(iso, type), jstr(iso, name) };
    return v8::Array::New(iso, e, 3);
}
FN(getActive) {   /* (id, program, index, 0 attribute / 1 uniform / 2 transform feedback varying) */
    GLC; GLuint p = U(1); GLuint i = U(2); int k = I(3);
    GLint maxlen = 0; glGetProgramiv(p, k == 0 ? GL_ACTIVE_ATTRIBUTE_MAX_LENGTH : k == 1 ? GL_ACTIVE_UNIFORM_MAX_LENGTH : GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH, &maxlen);
    std::vector<char> name((size_t)(maxlen > 0 ? maxlen : 1) + 1, 0);
    GLsizei len = 0; GLint size = 0; GLenum type = 0;
    if (k == 0) glGetActiveAttrib(p, i, (GLsizei)name.size(), &len, &size, &type, name.data());
    else if (k == 1) glGetActiveUniform(p, i, (GLsizei)name.size(), &len, &size, &type, name.data());
    else glGetTransformFeedbackVarying(p, i, (GLsizei)name.size(), &len, &size, &type, name.data());
    if (!type) { RET(v8::Null(iso)); return; }
    RET(active_info(iso, size, type, name.data()));
}
FN(getShaderPrecisionFormat) {
    GLC; GLint range[2] = { 0, 0 }, prec = 0;
    glGetShaderPrecisionFormat(E(1), E(2), range, &prec);
    double v[3] = { (double)range[0], (double)range[1], (double)prec };
    RET(nums(iso, v, 3));
}
FN(getShaderSource) {
    GLC; GLint n = 0; glGetShaderiv(U(1), GL_SHADER_SOURCE_LENGTH, &n);
    std::string s(n > 0 ? (size_t)n : 0, '\0'); GLsizei got = 0;
    if (n > 0) glGetShaderSource(U(1), n, &got, &s[0]);
    s.resize((size_t)got);
    RET(jstr(iso, s.c_str()));
}
FN(getError) { GLC; RET((double)glGetError()); }
FN(isEnabled) { GLC; RET((bool)glIsEnabled(E(1))); }
FN(checkFramebufferStatus) { GLC; RET((double)glCheckFramebufferStatus(E(1))); }

FN(getParameter) {
    GLC; GLenum p = E(1);
    switch (p) {
    case GL_ALIASED_LINE_WIDTH_RANGE: case GL_ALIASED_POINT_SIZE_RANGE: case GL_DEPTH_RANGE: {
        GLfloat v[2] = { 0, 0 }; glGetFloatv(p, v); RET(typed<GLfloat, v8::Float32Array>(iso, v, 2)); return; }
    case GL_COLOR_CLEAR_VALUE: case GL_BLEND_COLOR: { GLfloat v[4] = { 0 }; glGetFloatv(p, v); RET(typed<GLfloat, v8::Float32Array>(iso, v, 4)); return; }
    case GL_MAX_VIEWPORT_DIMS: { GLint v[2] = { 0 }; glGetIntegerv(p, v); RET(typed<GLint, v8::Int32Array>(iso, v, 2)); return; }
    case GL_SCISSOR_BOX: case GL_VIEWPORT: { GLint v[4] = { 0 }; glGetIntegerv(p, v); RET(typed<GLint, v8::Int32Array>(iso, v, 4)); return; }
    case GL_COLOR_WRITEMASK: {
        GLboolean v[4] = { 0 }; glGetBooleanv(p, v);
        v8::Local<v8::Value> e[4]; for (int i = 0; i < 4; i++) e[i] = v8::Boolean::New(iso, v[i]);
        RET(v8::Array::New(iso, e, 4)); return; }
    case GL_BLEND: case GL_CULL_FACE: case GL_DEPTH_TEST: case GL_DEPTH_WRITEMASK: case GL_DITHER: case GL_POLYGON_OFFSET_FILL:
    case GL_SAMPLE_ALPHA_TO_COVERAGE: case GL_SAMPLE_COVERAGE: case GL_SAMPLE_COVERAGE_INVERT: case GL_SCISSOR_TEST: case GL_STENCIL_TEST:
    case GL_RASTERIZER_DISCARD: case GL_TRANSFORM_FEEDBACK_ACTIVE: case GL_TRANSFORM_FEEDBACK_PAUSED: {
        GLboolean v = 0; glGetBooleanv(p, &v); RET((bool)v); return; }
    case GL_DEPTH_CLEAR_VALUE: case GL_LINE_WIDTH: case GL_POLYGON_OFFSET_FACTOR: case GL_POLYGON_OFFSET_UNITS: case GL_SAMPLE_COVERAGE_VALUE:
    case GL_MAX_TEXTURE_LOD_BIAS: case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: {
        GLfloat v = 0; glGetFloatv(p, &v); RET((double)v); return; }
    case GL_MAX_ELEMENT_INDEX: case GL_MAX_SERVER_WAIT_TIMEOUT: case GL_MAX_UNIFORM_BLOCK_SIZE:
    case GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS: case GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS: {
        GLint64 v = 0; glGetInteger64v(p, &v); RET((double)v); return; }
    default: {
        GLint v[4] = { 0 }; glGetIntegerv(p, v);
        if (p == GL_STENCIL_WRITEMASK || p == GL_STENCIL_BACK_WRITEMASK || p == GL_STENCIL_VALUE_MASK || p == GL_STENCIL_BACK_VALUE_MASK) RET((double)(GLuint)v[0]);
        else RET(v[0]);
    }
    }
}
FN(getIndexedParameter) {
    GLC; GLenum p = E(1);
    if (p == GL_TRANSFORM_FEEDBACK_BUFFER_START || p == GL_TRANSFORM_FEEDBACK_BUFFER_SIZE || p == GL_UNIFORM_BUFFER_START || p == GL_UNIFORM_BUFFER_SIZE) {
        GLint64 v = 0; glGetInteger64i_v(p, U(2), &v); RET((double)v); return;
    }
    GLint v = 0; glGetIntegeri_v(p, U(2), &v); RET(v);
}
FN(getBufferParameter) { GLC; GLint v = 0; glGetBufferParameteriv(E(1), E(2), &v); RET(v); }
FN(getRenderbufferParameter) { GLC; GLint v = 0; glGetRenderbufferParameteriv(E(1), E(2), &v); RET(v); }
FN(getTexParameter) {
    GLC; GLenum p = E(2);
    if (p == GL_TEXTURE_MIN_LOD || p == GL_TEXTURE_MAX_LOD || p == GL_TEXTURE_MAX_ANISOTROPY_EXT) { GLfloat v = 0; glGetTexParameterfv(E(1), p, &v); RET((double)v); return; }
    GLint v = 0; glGetTexParameteriv(E(1), p, &v); RET(v);
}
FN(getSamplerParameter) {
    GLC; GLenum p = E(2);
    if (p == GL_TEXTURE_MIN_LOD || p == GL_TEXTURE_MAX_LOD) { GLfloat v = 0; glGetSamplerParameterfv(U(1), p, &v); RET((double)v); return; }
    GLint v = 0; glGetSamplerParameteriv(U(1), p, &v); RET(v);
}
FN(getFramebufferAttachmentParameter) {
    GLC; GLenum t = E(1), at = E(2);
    bool def = t == GL_READ_FRAMEBUFFER ? g->read_default : g->draw_default;
    if (def) at = at == GL_BACK ? GL_COLOR_ATTACHMENT0 : at == GL_DEPTH ? GL_DEPTH_ATTACHMENT : at == GL_STENCIL ? GL_STENCIL_ATTACHMENT : at;
    GLint v = 0; glGetFramebufferAttachmentParameteriv(t, at, E(3), &v); RET(v);
}
FN(getVertexAttrib) {
    GLC; GLenum p = E(2);
    if (p == GL_CURRENT_VERTEX_ATTRIB) { GLfloat v[4] = { 0 }; glGetVertexAttribfv(U(1), p, v); RET(typed<GLfloat, v8::Float32Array>(iso, v, 4)); return; }
    GLint v = 0; glGetVertexAttribiv(U(1), p, &v); RET(v);
}
FN(getVertexAttribOffset) { GLC; void *p = nullptr; glGetVertexAttribPointerv(U(1), E(2), &p); RET((double)(intptr_t)p); }
FN(getQuery) { GLC; GLint v = 0; glGetQueryiv(E(1), E(2), &v); RET(v); }
FN(getQueryParameter) { GLC; GLuint v = 0; glGetQueryObjectuiv(U(1), E(2), &v); RET((double)v); }
FN(getInternalformatParameter) {
    GLC; GLint n = 0; glGetInternalformativ(E(1), E(2), GL_NUM_SAMPLE_COUNTS, 1, &n);
    std::vector<GLint> v((size_t)(n > 0 ? n : 0));
    if (n > 0) glGetInternalformativ(E(1), E(2), E(3), n, v.data());
    RET(typed<GLint, v8::Int32Array>(iso, v.data(), n > 0 ? n : 0));
}
FN(getUniform) {   /* by the uniform's type, found through the active uniforms */
    GLC; GLuint p = U(1); GLint loc = I(2);
    GLint count = 0, maxlen = 0; glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &count); glGetProgramiv(p, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxlen);
    std::vector<char> name((size_t)maxlen + 16);
    for (GLint i = 0; i < count; i++) {
        GLint size = 0; GLenum type = 0; GLsizei len = 0;
        glGetActiveUniform(p, (GLuint)i, (GLsizei)name.size() - 8, &len, &size, &type, name.data());
        std::string base(name.data(), (size_t)len);
        if (base.size() > 3 && base.compare(base.size() - 3, 3, "[0]") == 0) base.resize(base.size() - 3);
        for (GLint k = 0; k < size; k++) {
            std::string nm = size > 1 ? base + "[" + std::to_string(k) + "]" : base;
            if (glGetUniformLocation(p, nm.c_str()) != loc) continue;
            int n = 1; char kind = 'f';
            switch (type) {
            case GL_FLOAT_VEC2: n = 2; break; case GL_FLOAT_VEC3: n = 3; break; case GL_FLOAT_VEC4: case GL_FLOAT_MAT2: n = 4; break;
            case GL_FLOAT_MAT3: n = 9; break; case GL_FLOAT_MAT4: n = 16; break;
            case GL_FLOAT_MAT2x3: case GL_FLOAT_MAT3x2: n = 6; break; case GL_FLOAT_MAT2x4: case GL_FLOAT_MAT4x2: n = 8; break; case GL_FLOAT_MAT3x4: case GL_FLOAT_MAT4x3: n = 12; break;
            case GL_INT: case GL_SAMPLER_2D: case GL_SAMPLER_CUBE: case GL_SAMPLER_3D: case GL_SAMPLER_2D_SHADOW: case GL_SAMPLER_2D_ARRAY: case GL_SAMPLER_2D_ARRAY_SHADOW: case GL_SAMPLER_CUBE_SHADOW:
            case GL_INT_SAMPLER_2D: case GL_INT_SAMPLER_3D: case GL_INT_SAMPLER_CUBE: case GL_INT_SAMPLER_2D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_2D: case GL_UNSIGNED_INT_SAMPLER_3D:
            case GL_UNSIGNED_INT_SAMPLER_CUBE: case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY: kind = 'i'; break;
            case GL_INT_VEC2: kind = 'i'; n = 2; break; case GL_INT_VEC3: kind = 'i'; n = 3; break; case GL_INT_VEC4: kind = 'i'; n = 4; break;
            case GL_BOOL: kind = 'b'; break; case GL_BOOL_VEC2: kind = 'b'; n = 2; break; case GL_BOOL_VEC3: kind = 'b'; n = 3; break; case GL_BOOL_VEC4: kind = 'b'; n = 4; break;
            case GL_UNSIGNED_INT: kind = 'u'; break; case GL_UNSIGNED_INT_VEC2: kind = 'u'; n = 2; break; case GL_UNSIGNED_INT_VEC3: kind = 'u'; n = 3; break; case GL_UNSIGNED_INT_VEC4: kind = 'u'; n = 4; break;
            }
            if (kind == 'f') {
                GLfloat v[16] = { 0 }; glGetUniformfv(p, loc, v);
                if (n == 1) RET((double)v[0]); else RET(typed<GLfloat, v8::Float32Array>(iso, v, n));
            } else if (kind == 'u') {
                GLuint v[4] = { 0 }; glGetUniformuiv(p, loc, v);
                if (n == 1) RET((double)v[0]); else RET(typed<GLuint, v8::Uint32Array>(iso, v, n));
            } else {
                GLint v[4] = { 0 }; glGetUniformiv(p, loc, v);
                if (kind == 'b') {
                    if (n == 1) { RET((bool)v[0]); return; }
                    v8::Local<v8::Value> e[4]; for (int j = 0; j < n; j++) e[j] = v8::Boolean::New(iso, v[j] != 0);
                    RET(v8::Array::New(iso, e, (size_t)n));
                } else if (n == 1) RET(v[0]); else RET(typed<GLint, v8::Int32Array>(iso, v, n));
            }
            return;
        }
    }
    RET(v8::Null(iso));
}
FN(getActiveUniformBlockName) {
    GLC; GLint n = 0; glGetActiveUniformBlockiv(U(1), U(2), GL_UNIFORM_BLOCK_NAME_LENGTH, &n);
    std::vector<char> s((size_t)(n > 0 ? n : 1) + 1, 0); GLsizei got = 0;
    glGetActiveUniformBlockName(U(1), U(2), (GLsizei)s.size(), &got, s.data());
    RET(jstr(iso, s.data()));
}
FN(getActiveUniformBlockParameter) {
    GLC; GLenum p = E(3);
    if (p == GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES) {
        GLint n = 0; glGetActiveUniformBlockiv(U(1), U(2), GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &n);
        std::vector<GLint> v((size_t)(n > 0 ? n : 0));
        if (n > 0) glGetActiveUniformBlockiv(U(1), U(2), p, v.data());
        std::vector<GLuint> u(v.begin(), v.end());
        RET(typed<GLuint, v8::Uint32Array>(iso, u.data(), (int)u.size())); return;
    }
    GLint v = 0; glGetActiveUniformBlockiv(U(1), U(2), p, &v);
    if (p == GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER || p == GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER) RET(v != 0); else RET(v);
}
FN(getUniformIndices) {
    GLC; std::vector<std::string> n = strs(a, 2);
    std::vector<const char *> p; for (auto &s : n) p.push_back(s.c_str());
    std::vector<GLuint> idx(n.size());
    if (!n.empty()) glGetUniformIndices(U(1), (GLsizei)n.size(), p.data(), idx.data());
    std::vector<double> d(idx.begin(), idx.end());
    RET(nums(iso, d.data(), (int)d.size()));
}
FN(getActiveUniforms) {
    GLC; std::vector<GLuint> idx = uints(a, 2); GLenum p = E(3);
    std::vector<GLint> v(idx.size());
    if (!idx.empty()) glGetActiveUniformsiv(U(1), (GLsizei)idx.size(), idx.data(), p, v.data());
    std::vector<v8::Local<v8::Value>> e;
    for (GLint x : v) e.push_back(p == GL_UNIFORM_IS_ROW_MAJOR ? v8::Local<v8::Value>(v8::Boolean::New(iso, x != 0)) : v8::Local<v8::Value>(v8::Number::New(iso, p == GL_UNIFORM_TYPE ? (double)(GLuint)x : x)));
    RET(v8::Array::New(iso, e.data(), e.size()));
}
FN(transformFeedbackVaryings) {
    GLC; std::vector<std::string> n = strs(a, 2);
    std::vector<const char *> p; for (auto &s : n) p.push_back(s.c_str());
    glTransformFeedbackVaryings(U(1), (GLsizei)n.size(), p.data(), E(3));
}

FN(bufferData) {   /* (id, target, size | data, usage, srcOffset, length) */
    GLC; char *p; size_t n;
    if (a[2]->IsNumber()) { glBufferData(E(1), (GLsizeiptr)an(a, 2), nullptr, E(3)); return; }
    if (!view_range(a, 2, 4, 5, &p, &n)) return;
    glBufferData(E(1), (GLsizeiptr)n, p, E(3));
}
FN(bufferSubData) {   /* (id, target, dstOffset, data, srcOffset, length) */
    GLC; char *p; size_t n;
    if (!view_range(a, 3, 4, 5, &p, &n)) return;
    glBufferSubData(E(1), (GLintptr)an(a, 2), (GLsizeiptr)n, p);
}
FN(getBufferSubData) {   /* (id, target, srcOffset, dst, dstOffset, length) */
    GLC; char *p; size_t n;
    if (!view_range(a, 3, 4, 5, &p, &n) || !n) return;
    void *m = glMapBufferRange(E(1), (GLintptr)an(a, 2), (GLsizeiptr)n, GL_MAP_READ_BIT);
    if (m) { memcpy(p, m, n); glUnmapBuffer(E(1)); }
}
FN(copyBufferSubData) { GLC; glCopyBufferSubData(E(1), E(2), (GLintptr)an(a, 3), (GLintptr)an(a, 4), (GLsizeiptr)an(a, 5)); }

/* texImage2D/texSubImage2D/texImage3D/texSubImage3D from a view, a pixel buffer offset or nothing:
   (id, sub, three, target, level, ifmt, x, y, z, w, h, d, fmt, type, src, srcOffset, flipY) */
FN(texImage) {
    GLC; bool sub = B(1), three = B(2); GLenum target = E(3); GLint level = I(4), ifmt = I(5), x = I(6), y = I(7), z = I(8), w = I(9), h = I(10), d = I(11);
    GLenum fmt = E(12), type = E(13); bool flip = B(16);
    if (!sub) fix_format(g, &ifmt, &fmt, &type); else { GLint dummy = 0; fix_format(g, &dummy, &fmt, &type); }
    const void *data = nullptr;
    std::vector<uint8_t> tmp;
    GLint pbo = 0; glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);
    if (pbo) data = P(14);
    else if (a.Length() > 14 && !a[14]->IsNullOrUndefined()) {
        char *p; size_t n, elem;
        if (!view_of(a[14], &p, &n, &elem)) return;
        size_t off = (size_t)I(15) * elem;
        if (off > n) { glTexImage2D(target, level, ifmt, -1, -1, 0, fmt, type, nullptr); return; }   /* INVALID_VALUE */
        size_t need = upload_bytes(fmt, type, w, h, three ? d : 1);
        if (n - off < need) { glTexImage2D(target, level, ifmt, -1, -1, 0, fmt, type, nullptr); return; }   /* too small: INVALID_OPERATION in WebGL, an error here too */
        data = p + off;
        if (flip && h > 1) {
            GLint align = 4; glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
            size_t row = (size_t)w * pixel_size(fmt, type); if (align > 1) row = (row + (size_t)align - 1) / (size_t)align * (size_t)align;
            int layers = three ? d : 1;
            tmp.assign((const uint8_t *)data, (const uint8_t *)data + need);
            for (int l = 0; l < layers; l++) for (int r = 0; r < h / 2; r++) {
                uint8_t *pa = tmp.data() + ((size_t)l * h + (size_t)r) * row, *pb = tmp.data() + ((size_t)l * h + (size_t)(h - 1 - r)) * row;
                size_t len = std::min(row, need - (size_t)(pb - tmp.data()));
                for (size_t k = 0; k < len; k++) std::swap(pa[k], pb[k]);
            }
            data = tmp.data();
        }
    }
    if (three) {
        if (sub) glTexSubImage3D(target, level, x, y, z, w, h, d, fmt, type, data);
        else glTexImage3D(target, level, ifmt, w, h, d, 0, fmt, type, data);
    } else if (sub) glTexSubImage2D(target, level, x, y, w, h, fmt, type, data);
    else glTexImage2D(target, level, ifmt, w, h, 0, fmt, type, data);
}
/* from an element or RGBA pixels: (id, sub, three, target, level, ifmt, x, y, z, fmt, type, source, flipY, premultiply, w, h)
   source: an element (img, video, canvas) or a Uint8ClampedArray of w x h RGBA (ImageData) */
FN(texImageSource) {
    GLC; bool sub = B(1), three = B(2); GLenum target = E(3); GLint level = I(4), ifmt = I(5), x = I(6), y = I(7), z = I(8);
    GLenum fmt = E(9), type = E(10); bool flip = B(12), premul = B(13);
    int w = 0, h = 0; uint8_t *rgba = nullptr;
    if (Node *n = junwrap(a[11])) rgba = gl_node_rgba(n, &w, &h, premul);
    else {
        char *p; size_t nb, elem;
        w = I(14); h = I(15);
        if (view_of(a[11], &p, &nb, &elem) && w > 0 && h > 0 && nb >= (size_t)w * h * 4) {
            rgba = (uint8_t *)malloc((size_t)w * h * 4); memcpy(rgba, p, (size_t)w * h * 4);
            if (premul) for (size_t i = 0; i < (size_t)w * h; i++) { uint8_t *q = rgba + i * 4; q[0] = (uint8_t)(q[0] * q[3] / 255); q[1] = (uint8_t)(q[1] * q[3] / 255); q[2] = (uint8_t)(q[2] * q[3] / 255); }
        }
    }
    if (!rgba) {   /* nothing decoded yet: a 1x1 transparent texture, as browsers give for an incomplete image */
        if (sub) return;
        w = h = 1; rgba = (uint8_t *)calloc(1, 4);
    }
    upload_rgba(g, sub, target, level, ifmt, x, y, z, w, h, fmt, type, rgba, flip, three);
    free(rgba);
}
FN(compressedTexImage) {   /* (id, sub, target, level, ifmt|fmt, x, y, w, h, data, srcOffset, length) */
    GLC; bool sub = B(1); char *p; size_t n;
    if (!view_range(a, 9, 10, 11, &p, &n)) return;
    if (sub) glCompressedTexSubImage2D(E(2), I(3), I(5), I(6), I(7), I(8), E(4), (GLsizei)n, p);
    else glCompressedTexImage2D(E(2), I(3), E(4), I(7), I(8), 0, (GLsizei)n, p);
}
FN(readPixels) {   /* (id, x, y, w, h, fmt, type, dst | offset, dstOffset) */
    GLC; ReadDefault r(g);
    GLint pbo = 0; glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pbo);
    if (pbo) { glReadPixels(I(1), I(2), I(3), I(4), E(5), E(6), (void *)P(7)); return; }
    char *p; size_t n, elem;
    if (!view_of(a[7], &p, &n, &elem)) return;
    size_t off = (size_t)I(8) * elem;
    GLint align = 4, rowlen = 0; glGetIntegerv(GL_PACK_ALIGNMENT, &align); glGetIntegerv(GL_PACK_ROW_LENGTH, &rowlen);
    int w = I(3), h = I(4);
    size_t ps = pixel_size(E(5), E(6)), row = (size_t)(rowlen > 0 ? rowlen : w) * ps;
    if (align > 1) row = (row + (size_t)align - 1) / (size_t)align * (size_t)align;
    size_t need = w > 0 && h > 0 ? row * (size_t)(h - 1) + (size_t)w * ps : 0;
    if (off > n || n - off < need) { glReadPixels(0, 0, -1, -1, E(5), E(6), nullptr); return; }   /* INVALID_VALUE */
    glReadPixels(I(1), I(2), w, h, E(5), E(6), p + off);
}
FN(pixelStorei) { GLC; glPixelStorei(E(1), I(2)); }

/* uniform{1,2,3,4}{f,i,ui}v: (id, kind, loc, data, srcOffset, srcLength); kind 0-3 float, 4-7 int, 8-11 uint */
FN(uniformv) {
    GLC; int k = I(1); GLint loc = I(2); char *p; size_t n;
    if (!view_range(a, 3, 4, 5, &p, &n)) return;
    int comps = k % 4 + 1; GLsizei cnt = (GLsizei)(n / 4 / (size_t)comps);
    if (!cnt) { glUniform1fv(loc, -1, nullptr); return; }   /* INVALID_VALUE for an empty array */
    switch (k) {
    case 0: glUniform1fv(loc, cnt, (const GLfloat *)p); break; case 1: glUniform2fv(loc, cnt, (const GLfloat *)p); break;
    case 2: glUniform3fv(loc, cnt, (const GLfloat *)p); break; case 3: glUniform4fv(loc, cnt, (const GLfloat *)p); break;
    case 4: glUniform1iv(loc, cnt, (const GLint *)p); break; case 5: glUniform2iv(loc, cnt, (const GLint *)p); break;
    case 6: glUniform3iv(loc, cnt, (const GLint *)p); break; case 7: glUniform4iv(loc, cnt, (const GLint *)p); break;
    case 8: glUniform1uiv(loc, cnt, (const GLuint *)p); break; case 9: glUniform2uiv(loc, cnt, (const GLuint *)p); break;
    case 10: glUniform3uiv(loc, cnt, (const GLuint *)p); break; case 11: glUniform4uiv(loc, cnt, (const GLuint *)p); break;
    }
}
/* uniformMatrix{2,3,4,2x3,3x2,2x4,4x2,3x4,4x3}fv: (id, kind, loc, transpose, data, srcOffset, srcLength) */
FN(uniformMatrix) {
    GLC; int k = I(1); GLint loc = I(2); GLboolean tr = B(3); char *p; size_t n;
    if (!view_range(a, 4, 5, 6, &p, &n)) return;
    static const int sz[] = { 4, 9, 16, 6, 6, 8, 8, 12, 12 };
    if (k < 0 || k > 8) return;
    GLsizei cnt = (GLsizei)(n / 4 / (size_t)sz[k]);
    const GLfloat *f = (const GLfloat *)p;
    if (!cnt) { glUniformMatrix4fv(loc, -1, tr, nullptr); return; }
    switch (k) {
    case 0: glUniformMatrix2fv(loc, cnt, tr, f); break; case 1: glUniformMatrix3fv(loc, cnt, tr, f); break; case 2: glUniformMatrix4fv(loc, cnt, tr, f); break;
    case 3: glUniformMatrix2x3fv(loc, cnt, tr, f); break; case 4: glUniformMatrix3x2fv(loc, cnt, tr, f); break;
    case 5: glUniformMatrix2x4fv(loc, cnt, tr, f); break; case 6: glUniformMatrix4x2fv(loc, cnt, tr, f); break;
    case 7: glUniformMatrix3x4fv(loc, cnt, tr, f); break; case 8: glUniformMatrix4x3fv(loc, cnt, tr, f); break;
    }
}
FN(clearBuffer) {   /* (id, kind 0 f / 1 i / 2 ui, buffer, drawbuffer, data, srcOffset) */
    GLC; char *p; size_t n, elem;
    if (!view_of(a[4], &p, &n, &elem)) return;
    size_t off = (size_t)I(5) * 4;
    GLenum b = E(2);
    size_t need = b == GL_COLOR ? 16 : 4;
    if (off > n || n - off < need) return;
    p += off;
    if (I(1) == 0) glClearBufferfv(b, I(3), (const GLfloat *)p);
    else if (I(1) == 1) glClearBufferiv(b, I(3), (const GLint *)p);
    else glClearBufferuiv(b, I(3), (const GLuint *)p);
}

FN(fenceSync) {
    GLC; GLsync s = glFenceSync(E(1), 0);
    if (!s) { RET(0); return; }
    uint32_t id = g_next_sync++; g_syncs[id] = s; RET((double)id);
}
GLsync sync_of(const FCI &a, int i) { auto it = g_syncs.find(U(i)); return it == g_syncs.end() ? nullptr : it->second; }
FN(deleteSync) { GLC; auto it = g_syncs.find(U(1)); if (it != g_syncs.end()) { glDeleteSync(it->second); g_syncs.erase(it); } }
FN(isSync) { GLC; GLsync s = sync_of(a, 1); RET(s && glIsSync(s)); }
FN(clientWaitSync) { GLC; GLsync s = sync_of(a, 1); RET(s ? (double)glClientWaitSync(s, U(2), 0) : (double)GL_WAIT_FAILED); }   /* WebGL forbids blocking */
FN(waitSync) { GLC; GLsync s = sync_of(a, 1); if (s) glWaitSync(s, 0, GL_TIMEOUT_IGNORED); }
FN(getSyncParameter) { GLC; GLsync s = sync_of(a, 1); GLint v = 0; GLsizei n = 0; if (s) glGetSynciv(s, E(2), 1, &n, &v); RET(v); }

#define GLV(nm, call) FN(nm) { GLC; call; }
GLV(activeTexture, glActiveTexture(E(1)))
GLV(attachShader, glAttachShader(U(1), U(2)))
GLV(detachShader, glDetachShader(U(1), U(2)))
GLV(bindBuffer, glBindBuffer(E(1), U(2)))
GLV(bindBufferBase, glBindBufferBase(E(1), U(2), U(3)))
GLV(bindBufferRange, glBindBufferRange(E(1), U(2), U(3), (GLintptr)an(a, 4), (GLsizeiptr)an(a, 5)))
GLV(bindRenderbuffer, glBindRenderbuffer(E(1), U(2)))
GLV(bindTexture, glBindTexture(E(1), U(2)))
GLV(bindVertexArray, glBindVertexArray(U(1)))
GLV(bindSampler, glBindSampler(U(1), U(2)))
GLV(bindTransformFeedback, glBindTransformFeedback(E(1), U(2)))
GLV(beginTransformFeedback, glBeginTransformFeedback(E(1)))
GLV(endTransformFeedback, glEndTransformFeedback())
GLV(pauseTransformFeedback, glPauseTransformFeedback())
GLV(resumeTransformFeedback, glResumeTransformFeedback())
GLV(beginQuery, glBeginQuery(E(1), U(2)))
GLV(endQuery, glEndQuery(E(1)))
GLV(blendColor, glBlendColor(F(1), F(2), F(3), F(4)))
GLV(blendEquation, glBlendEquation(E(1)))
GLV(blendEquationSeparate, glBlendEquationSeparate(E(1), E(2)))
GLV(blendFunc, glBlendFunc(E(1), E(2)))
GLV(blendFuncSeparate, glBlendFuncSeparate(E(1), E(2), E(3), E(4)))
GLV(blitFramebuffer, glBlitFramebuffer(I(1), I(2), I(3), I(4), I(5), I(6), I(7), I(8), U(9), E(10)))
GLV(clear, glClear(U(1)))
GLV(clearColor, glClearColor(F(1), F(2), F(3), F(4)))
GLV(clearDepth, glClearDepthf(F(1)))
GLV(clearStencil, glClearStencil(I(1)))
GLV(clearBufferfi, glClearBufferfi(E(1), I(2), F(3), I(4)))
GLV(colorMask, glColorMask(B(1), B(2), B(3), B(4)))
GLV(compileShader, glCompileShader(U(1)))
GLV(cullFace, glCullFace(E(1)))
GLV(depthFunc, glDepthFunc(E(1)))
GLV(depthMask, glDepthMask(B(1)))
GLV(depthRange, glDepthRangef(F(1), F(2)))
GLV(disable, glDisable(E(1)))
GLV(enable, glEnable(E(1)))
GLV(disableVertexAttribArray, glDisableVertexAttribArray(U(1)))
GLV(enableVertexAttribArray, glEnableVertexAttribArray(U(1)))
GLV(drawArrays, glDrawArrays(E(1), I(2), I(3)))
GLV(drawElements, glDrawElements(E(1), I(2), E(3), P(4)))
GLV(drawArraysInstanced, glDrawArraysInstanced(E(1), I(2), I(3), I(4)))
GLV(drawElementsInstanced, glDrawElementsInstanced(E(1), I(2), E(3), P(4), I(5)))
GLV(drawRangeElements, glDrawRangeElements(E(1), U(2), U(3), I(4), E(5), P(6)))
GLV(finish, glFinish())
GLV(flush, glFlush())
GLV(framebufferRenderbuffer, glFramebufferRenderbuffer(E(1), E(2), E(3), U(4)))
GLV(framebufferTexture2D, glFramebufferTexture2D(E(1), E(2), E(3), U(4), I(5)))
GLV(framebufferTextureLayer, glFramebufferTextureLayer(E(1), E(2), U(3), I(4), I(5)))
GLV(frontFace, glFrontFace(E(1)))
GLV(generateMipmap, glGenerateMipmap(E(1)))
GLV(hint, glHint(E(1), E(2)))
GLV(lineWidth, glLineWidth(F(1)))
GLV(linkProgram, glLinkProgram(U(1)))
GLV(polygonOffset, glPolygonOffset(F(1), F(2)))
GLV(sampleCoverage, glSampleCoverage(F(1), B(2)))
GLV(samplerParameteri, glSamplerParameteri(U(1), E(2), I(3)))
GLV(samplerParameterf, glSamplerParameterf(U(1), E(2), F(3)))
GLV(scissor, glScissor(I(1), I(2), I(3), I(4)))
GLV(stencilFunc, glStencilFunc(E(1), I(2), U(3)))
GLV(stencilFuncSeparate, glStencilFuncSeparate(E(1), E(2), I(3), U(4)))
GLV(stencilMask, glStencilMask(U(1)))
GLV(stencilMaskSeparate, glStencilMaskSeparate(E(1), U(2)))
GLV(stencilOp, glStencilOp(E(1), E(2), E(3)))
GLV(stencilOpSeparate, glStencilOpSeparate(E(1), E(2), E(3), E(4)))
GLV(texParameterf, glTexParameterf(E(1), E(2), F(3)))
GLV(texParameteri, glTexParameteri(E(1), E(2), I(3)))
GLV(texStorage2D, glTexStorage2D(E(1), I(2), E(3), I(4), I(5)))
GLV(texStorage3D, glTexStorage3D(E(1), I(2), E(3), I(4), I(5), I(6)))
GLV(uniform1f, glUniform1f(I(1), F(2)))
GLV(uniform2f, glUniform2f(I(1), F(2), F(3)))
GLV(uniform3f, glUniform3f(I(1), F(2), F(3), F(4)))
GLV(uniform4f, glUniform4f(I(1), F(2), F(3), F(4), F(5)))
GLV(uniform1i, glUniform1i(I(1), I(2)))
GLV(uniform2i, glUniform2i(I(1), I(2), I(3)))
GLV(uniform3i, glUniform3i(I(1), I(2), I(3), I(4)))
GLV(uniform4i, glUniform4i(I(1), I(2), I(3), I(4), I(5)))
GLV(uniform1ui, glUniform1ui(I(1), U(2)))
GLV(uniform2ui, glUniform2ui(I(1), U(2), U(3)))
GLV(uniform3ui, glUniform3ui(I(1), U(2), U(3), U(4)))
GLV(uniform4ui, glUniform4ui(I(1), U(2), U(3), U(4), U(5)))
GLV(uniformBlockBinding, glUniformBlockBinding(U(1), U(2), U(3)))
GLV(useProgram, glUseProgram(U(1)))
GLV(validateProgram, glValidateProgram(U(1)))
GLV(vertexAttrib4f, glVertexAttrib4f(U(1), F(2), F(3), F(4), F(5)))
GLV(vertexAttribI4i, glVertexAttribI4i(U(1), I(2), I(3), I(4), I(5)))
GLV(vertexAttribI4ui, glVertexAttribI4ui(U(1), U(2), U(3), U(4), U(5)))
GLV(vertexAttribPointer, glVertexAttribPointer(U(1), I(2), E(3), B(4), I(5), P(6)))
GLV(vertexAttribIPointer, glVertexAttribIPointer(U(1), I(2), E(3), I(4), P(5)))
GLV(vertexAttribDivisor, glVertexAttribDivisor(U(1), U(2)))
GLV(viewport, glViewport(I(1), I(2), I(3), I(4)))
#undef GLV

}  // namespace

void js_install_webgl(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso;
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Object> G = v8::Object::New(iso);
#define REG(nm) (void)G->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, g_##nm).ToLocalChecked())
    REG(create); REG(available); REG(renderer); REG(exts); REG(size); REG(resize); REG(present); REG(capture); REG(close);
    REG(gen); REG(del); REG(bindFramebuffer); REG(copyTexImage2D); REG(copyTexSubImage2D); REG(copyTexSubImage3D); REG(readBuffer); REG(drawBuffers);
    REG(invalidateFramebuffer); REG(invalidateSubFramebuffer); REG(renderbufferStorage); REG(renderbufferStorageMultisample);
    REG(shaderSource); REG(bindAttribLocation); REG(getAttribLocation); REG(getUniformLocation); REG(getFragDataLocation); REG(getUniformBlockIndex);
    REG(getShaderInfoLog); REG(getProgramInfoLog); REG(getShaderParameter); REG(getProgramParameter); REG(getAttachedShaders); REG(getActive);
    REG(getShaderPrecisionFormat); REG(getShaderSource); REG(getError); REG(isEnabled); REG(checkFramebufferStatus); REG(getParameter); REG(getIndexedParameter);
    REG(getBufferParameter); REG(getRenderbufferParameter); REG(getTexParameter); REG(getSamplerParameter); REG(getFramebufferAttachmentParameter);
    REG(getVertexAttrib); REG(getVertexAttribOffset); REG(getQuery); REG(getQueryParameter); REG(getInternalformatParameter); REG(getUniform);
    REG(getActiveUniformBlockName); REG(getActiveUniformBlockParameter); REG(getUniformIndices); REG(getActiveUniforms); REG(transformFeedbackVaryings);
    REG(bufferData); REG(bufferSubData); REG(getBufferSubData); REG(copyBufferSubData); REG(texImage); REG(texImageSource); REG(compressedTexImage);
    REG(readPixels); REG(pixelStorei); REG(uniformv); REG(uniformMatrix); REG(clearBuffer);
    REG(fenceSync); REG(deleteSync); REG(isSync); REG(clientWaitSync); REG(waitSync); REG(getSyncParameter);
    REG(activeTexture); REG(attachShader); REG(detachShader); REG(bindBuffer); REG(bindBufferBase); REG(bindBufferRange); REG(bindRenderbuffer); REG(bindTexture);
    REG(bindVertexArray); REG(bindSampler); REG(bindTransformFeedback); REG(beginTransformFeedback); REG(endTransformFeedback); REG(pauseTransformFeedback);
    REG(resumeTransformFeedback); REG(beginQuery); REG(endQuery); REG(blendColor); REG(blendEquation); REG(blendEquationSeparate); REG(blendFunc);
    REG(blendFuncSeparate); REG(blitFramebuffer); REG(clear); REG(clearColor); REG(clearDepth); REG(clearStencil); REG(clearBufferfi); REG(colorMask);
    REG(compileShader); REG(cullFace); REG(depthFunc); REG(depthMask); REG(depthRange); REG(disable); REG(enable); REG(disableVertexAttribArray);
    REG(enableVertexAttribArray); REG(drawArrays); REG(drawElements); REG(drawArraysInstanced); REG(drawElementsInstanced); REG(drawRangeElements);
    REG(finish); REG(flush); REG(framebufferRenderbuffer); REG(framebufferTexture2D); REG(framebufferTextureLayer); REG(frontFace); REG(generateMipmap);
    REG(hint); REG(lineWidth); REG(linkProgram); REG(polygonOffset); REG(sampleCoverage); REG(samplerParameteri); REG(samplerParameterf); REG(scissor);
    REG(stencilFunc); REG(stencilFuncSeparate); REG(stencilMask); REG(stencilMaskSeparate); REG(stencilOp); REG(stencilOpSeparate); REG(texParameterf);
    REG(texParameteri); REG(texStorage2D); REG(texStorage3D); REG(uniform1f); REG(uniform2f); REG(uniform3f); REG(uniform4f); REG(uniform1i); REG(uniform2i);
    REG(uniform3i); REG(uniform4i); REG(uniform1ui); REG(uniform2ui); REG(uniform3ui); REG(uniform4ui); REG(uniformBlockBinding); REG(useProgram);
    REG(validateProgram); REG(vertexAttrib4f); REG(vertexAttribI4i); REG(vertexAttribI4ui); REG(vertexAttribPointer); REG(vertexAttribIPointer);
    REG(vertexAttribDivisor); REG(viewport);
#undef REG
    (void)N->Set(ctx, jstr(iso, "gl"), G);
}
