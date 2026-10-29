/* WebGL contexts over EGL and OpenGL ES 3 (see webgl.h). Frames leave the GPU through two pixel buffers:
   a present starts glReadPixels into one and fences it, and gl_tick copies it out once the fence has
   signalled, so neither the page nor the compositor waits for the GPU. */
#include "gpu/webgl.h"
#include "media/media.h"
#include "paint/paint.h"
#include "base/util.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define GLCTX_MAX 16
static GlCtx *g_gl[GLCTX_MAX];
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLConfig g_cfg;
static EGLSurface g_pb = EGL_NO_SURFACE;   /* 1x1 when the driver cannot go surfaceless */
static bool g_tried, g_robust;
static char g_renderer[128];

static bool egl_init(void) {
    if (g_tried) return g_dpy != EGL_NO_DISPLAY;
    g_tried = true;
    EGLDisplay d = EGL_NO_DISPLAY;
#ifndef __ANDROID__
    /* no window system needed: Mesa's surfaceless platform, else the default display */
    const char *ce = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (gpd && ce && strstr(ce, "EGL_MESA_platform_surfaceless")) d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (d != EGL_NO_DISPLAY && !eglInitialize(d, NULL, NULL)) d = EGL_NO_DISPLAY;
#endif
    if (d == EGL_NO_DISPLAY) {
        d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (d != EGL_NO_DISPLAY && !eglInitialize(d, NULL, NULL)) d = EGL_NO_DISPLAY;
    }
    if (d == EGL_NO_DISPLAY || !eglBindAPI(EGL_OPENGL_ES_API)) { SDL_Log("lumen: WebGL unavailable (no EGL display)"); return false; }
    const char *ext = eglQueryString(d, EGL_EXTENSIONS);
    bool surfaceless = ext && strstr(ext, "EGL_KHR_surfaceless_context");
    g_robust = ext && strstr(ext, "EGL_EXT_create_context_robustness");
    EGLint at[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLint n = 0;
    if ((!eglChooseConfig(d, at, &g_cfg, 1, &n) || n < 1) && surfaceless) { at[3] = EGL_DONT_CARE; eglChooseConfig(d, at, &g_cfg, 1, &n); }
    if (n < 1) { SDL_Log("lumen: WebGL unavailable (no ES 3 config)"); eglTerminate(d); return false; }
    if (!surfaceless) {
        EGLint pa[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        g_pb = eglCreatePbufferSurface(d, g_cfg, pa);
        if (g_pb == EGL_NO_SURFACE) { SDL_Log("lumen: WebGL unavailable (no pbuffer)"); eglTerminate(d); return false; }
    }
    g_dpy = d;
    return true;
}

bool gl_available(void) { return egl_init(); }
const char *gl_renderer(void) { return g_renderer[0] ? g_renderer : "OpenGL ES"; }

/* Only this file changes the thread's EGL context, so it remembers which one is current: every binding
   call passes here, and Android's eglGetCurrentContext costs a lock and TLS lookups (~13% of a core with
   WebGL Aquarium's 10000 fish). */
static _Thread_local EGLContext g_current = EGL_NO_CONTEXT;
static bool make_current(GlCtx *g) {
    if (g_current == (EGLContext)g->egl) return true;
    if (!eglMakeCurrent(g_dpy, g_pb, g_pb, (EGLContext)g->egl)) { g_current = eglGetCurrentContext(); return false; }
    g_current = (EGLContext)g->egl;
    return true;
}

static void fb_free(GlCtx *g) {
    GLuint rb[3] = { g->color, g->depth, g->rcolor }, fb[2] = { g->fb, g->rfb };
    glDeleteRenderbuffers(3, rb); glDeleteFramebuffers(2, fb);
    g->color = g->depth = g->rcolor = g->fb = g->rfb = 0;
}
/* the default framebuffer at w x h, cleared */
static void fb_build(GlCtx *g) {
    GLint old_draw = 0, old_read = 0, old_rb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &old_rb);
    fb_free(g);
    GLenum cf = g->attrs & GL_ATTR_ALPHA ? GL_RGBA8 : GL_RGB8;
    GLenum df = g->attrs & GL_ATTR_STENCIL ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
    GLenum da = g->attrs & GL_ATTR_STENCIL ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    glGenFramebuffers(1, &g->fb); glBindFramebuffer(GL_FRAMEBUFFER, g->fb);
    glGenRenderbuffers(1, &g->color); glBindRenderbuffer(GL_RENDERBUFFER, g->color);
    if (g->samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, g->samples, cf, g->w, g->h);
    else glRenderbufferStorage(GL_RENDERBUFFER, cf, g->w, g->h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g->color);
    if (g->attrs & (GL_ATTR_DEPTH | GL_ATTR_STENCIL)) {
        glGenRenderbuffers(1, &g->depth); glBindRenderbuffer(GL_RENDERBUFFER, g->depth);
        if (g->samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, g->samples, df, g->w, g->h);
        else glRenderbufferStorage(GL_RENDERBUFFER, df, g->w, g->h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, da, GL_RENDERBUFFER, g->depth);
    }
    if (g->samples) {
        glGenFramebuffers(1, &g->rfb); glBindFramebuffer(GL_FRAMEBUFFER, g->rfb);
        glGenRenderbuffers(1, &g->rcolor); glBindRenderbuffer(GL_RENDERBUFFER, g->rcolor);
        glRenderbufferStorage(GL_RENDERBUFFER, cf, g->w, g->h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g->rcolor);
        glBindFramebuffer(GL_FRAMEBUFFER, g->fb);
    }
    /* fresh contents are transparent black, whatever clear state the page left */
    GLfloat cc[4]; GLboolean cm[4], dm; GLint sm, ss, sb; GLfloat dc;
    glGetFloatv(GL_COLOR_CLEAR_VALUE, cc); glGetBooleanv(GL_COLOR_WRITEMASK, cm); glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &sm); glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &sb); glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &ss); glGetFloatv(GL_DEPTH_CLEAR_VALUE, &dc);
    GLboolean sc = glIsEnabled(GL_SCISSOR_TEST), rd = glIsEnabled(GL_RASTERIZER_DISCARD);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_RASTERIZER_DISCARD);
    glClearColor(0, 0, 0, 0); glColorMask(1, 1, 1, 1); glDepthMask(1); glStencilMask(0xffffffffu); glClearDepthf(1); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glClearColor(cc[0], cc[1], cc[2], cc[3]); glColorMask(cm[0], cm[1], cm[2], cm[3]); glDepthMask(dm);
    glStencilMaskSeparate(GL_FRONT, (GLuint)sm); glStencilMaskSeparate(GL_BACK, (GLuint)sb); glClearStencil(ss); glClearDepthf(dc);
    if (sc) glEnable(GL_SCISSOR_TEST);
    if (rd) glEnable(GL_RASTERIZER_DISCARD);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)old_rb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g->draw_default ? g->fb : (GLuint)old_draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g->read_default ? g->fb : (GLuint)old_read);
}

int gl_create(const void *owner, Node *canvas, int version, int attrs, int w, int h) {
    if (!egl_init()) return -1;
    int id = -1;
    for (int i = 0; i < GLCTX_MAX && id < 0; i++) if (!g_gl[i]) id = i;
    if (id < 0) return -1;
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT, EGL_TRUE, EGL_NONE };
    if (!g_robust) ca[2] = EGL_NONE;
    EGLContext ec = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ca);
    if (ec == EGL_NO_CONTEXT && g_robust) { ca[2] = EGL_NONE; ec = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ca); }
    if (ec == EGL_NO_CONTEXT) { SDL_Log("lumen: WebGL context creation failed (0x%x)", eglGetError()); return -1; }
    GlCtx *g = xcalloc(1, sizeof *g);
    g->owner = owner; g->egl = ec; g->canvas = canvas; g->version = version; g->attrs = attrs;
    g->read_default = g->draw_default = true;
    if (!make_current(g)) { eglDestroyContext(g_dpy, ec); free(g); return -1; }
    if (!g_renderer[0]) { const char *r = (const char *)glGetString(GL_RENDERER); SDL_strlcpy(g_renderer, r ? r : "", sizeof g_renderer); }
    const char *e = (const char *)glGetString(GL_EXTENSIONS);
    g->exts = xstrdup(e ? e : "");
    GLint maxs = 0, maxr = 0; glGetIntegerv(GL_MAX_SAMPLES, &maxs); glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxr);
    g->samples = attrs & GL_ATTR_ANTIALIAS ? (maxs >= 4 ? 4 : maxs > 1 ? maxs : 0) : 0;
    if (maxr <= 0) maxr = 4096;
    g->w = w < 1 ? 1 : w > maxr ? maxr : w; g->h = h < 1 ? 1 : h > maxr ? maxr : h;
    fb_build(g);
    glViewport(0, 0, g->w, g->h); glScissor(0, 0, g->w, g->h);
    g->player = mp_new(canvas);
    g_gl[id] = g;
    SDL_Log("lumen: WebGL%d context %dx%d samples=%d", version, g->w, g->h, g->samples);
    return id;
}

GlCtx *gl_get(const void *owner, int id) {
    GlCtx *g = id >= 0 && id < GLCTX_MAX ? g_gl[id] : NULL;
    if (!g || g->owner != owner || !make_current(g)) return NULL;
    return g;
}

void gl_resize(GlCtx *g, int w, int h) {
    GLint maxr = 4096; glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxr);
    w = w < 1 ? 1 : w > maxr ? maxr : w; h = h < 1 ? 1 : h > maxr ? maxr : h;
    if (w == g->w && h == g->h) return;
    g->w = w; g->h = h;
    fb_build(g);
}

unsigned gl_read_fb(GlCtx *g) { return g->samples ? g->rfb : g->fb; }
void gl_resolve(GlCtx *g) {
    if (!g->samples) return;
    GLint dr = 0, rd = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &dr); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rd);
    GLboolean sc = glIsEnabled(GL_SCISSOR_TEST), rz = glIsEnabled(GL_RASTERIZER_DISCARD);
    if (sc) glDisable(GL_SCISSOR_TEST);
    if (rz) glDisable(GL_RASTERIZER_DISCARD);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g->fb); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g->rfb);
    glBlitFramebuffer(0, 0, g->w, g->h, 0, 0, g->w, g->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)rd); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)dr);
    if (sc) glEnable(GL_SCISSOR_TEST);
    if (rz) glEnable(GL_RASTERIZER_DISCARD);
}

/* a frame buffer we can write: one of ours nobody else holds, else a new one */
static Image *frame_image(GlCtx *g, int w, int h) {
    for (int i = 0; i < 6; i++) {
        Image *im = g->pool[i];
        if (im && im->refs == 1 && im->w == w && im->h == h) { im->refs++; im->content_version++; return im; }
    }
    Image *im = xcalloc(1, sizeof *im); im->w = w; im->h = h; im->refs = 1;
    im->px = xmalloc((size_t)w * h * 4);
    for (int i = 0; i < 6; i++) {
        if (g->pool[i] && g->pool[i]->refs == 1) { image_unref(g->pool[i]); g->pool[i] = NULL; }
        if (!g->pool[i]) { g->pool[i] = im; im->refs++; break; }
    }
    return im;
}

/* the readback in pbo k to the canvas: RGBA rows bottom up become premultiplied 0xAARRGGBB top down */
static void finish(GlCtx *g, int k, bool wait) {
    GLsync f = (GLsync)g->fence[k];
    if (!f) return;
    GLenum r = glClientWaitSync(f, wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0, wait ? 1000000000ull : 0);
    if (r == GL_TIMEOUT_EXPIRED) return;
    glDeleteSync(f); g->fence[k] = NULL;
    if (r == GL_WAIT_FAILED) return;
    int w = g->pw[k], h = g->ph[k];
    GLint old = 0; glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, g->pbo[k]);
    const uint32_t *p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)w * h * 4, GL_MAP_READ_BIT);
    if (p) {
        Image *im = frame_image(g, w, h);
        bool alpha = g->attrs & GL_ATTR_ALPHA, premul = g->attrs & GL_ATTR_PREMULTIPLIED;
        uint32_t all = 0xffffffffu;
        for (int y = 0; y < h; y++) {   /* the mapped buffer is often uncached: copy each row out in bulk, convert in place */
            uint32_t *o = im->px + (size_t)y * w;
            memcpy(o, p + (size_t)(h - 1 - y) * w, (size_t)w * 4);
            const uint32_t *s = o;
            if (!alpha) for (int x = 0; x < w; x++) { uint32_t v = s[x]; o[x] = 0xff000000u | (v & 0xff00u) | ((v & 0xffu) << 16) | ((v >> 16) & 0xffu); }
            else if (premul) for (int x = 0; x < w; x++) { uint32_t v = s[x]; all &= v; o[x] = (v & 0xff00ff00u) | ((v & 0xffu) << 16) | ((v >> 16) & 0xffu); }
            else for (int x = 0; x < w; x++) {
                uint32_t v = s[x], a = v >> 24; all &= v;
                uint32_t rr = (v & 0xff) * a / 255, gg = ((v >> 8) & 0xff) * a / 255, bb = ((v >> 16) & 0xff) * a / 255;
                o[x] = a << 24 | rr << 16 | gg << 8 | bb;
            }
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        im->translucent = alpha && (all >> 24) != 0xff;
        if (g->capture > 0) { im->refs++; live_push(g->capture, im); }
        mp_push((MediaPlayer *)g->player, im);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)old);
}

void gl_present(GlCtx *g) {
    int k = g->next;
    if (g->fence[k]) finish(g, k, true);   /* two frames already in flight */
    gl_resolve(g);
    GLint rd = 0, pk = 0, al = 4, rl = 0, sr = 0, sp = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rd); glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pk);
    glGetIntegerv(GL_PACK_ALIGNMENT, &al); glGetIntegerv(GL_PACK_ROW_LENGTH, &rl); glGetIntegerv(GL_PACK_SKIP_ROWS, &sr); glGetIntegerv(GL_PACK_SKIP_PIXELS, &sp);
    if (!g->pbo[k]) glGenBuffers(1, &g->pbo[k]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, g->pbo[k]);
    if (g->pw[k] != g->w || g->ph[k] != g->h) { glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)g->w * g->h * 4, NULL, GL_STREAM_READ); g->pw[k] = g->w; g->ph[k] = g->h; }
    glPixelStorei(GL_PACK_ALIGNMENT, 4); glPixelStorei(GL_PACK_ROW_LENGTH, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_read_fb(g));
    glReadPixels(0, 0, g->w, g->h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    g->fence[k] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    glPixelStorei(GL_PACK_ALIGNMENT, al); glPixelStorei(GL_PACK_ROW_LENGTH, rl); glPixelStorei(GL_PACK_SKIP_ROWS, sr); glPixelStorei(GL_PACK_SKIP_PIXELS, sp);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pk);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)rd);
    g->next = k ^ 1;
    if (!(g->attrs & GL_ATTR_PRESERVE)) {
        /* preserveDrawingBuffer false: the next frame starts cleared */
        GLfloat cc[4], dc; GLboolean cm[4], dm; GLint sm, sb, ss, dr = 0;
        glGetFloatv(GL_COLOR_CLEAR_VALUE, cc); glGetBooleanv(GL_COLOR_WRITEMASK, cm); glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);
        glGetIntegerv(GL_STENCIL_WRITEMASK, &sm); glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &sb); glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &ss);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &dc); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &dr);
        GLboolean sc = glIsEnabled(GL_SCISSOR_TEST), rz = glIsEnabled(GL_RASTERIZER_DISCARD);
        if (sc) glDisable(GL_SCISSOR_TEST);
        if (rz) glDisable(GL_RASTERIZER_DISCARD);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g->fb);
        glClearColor(0, 0, 0, 0); glColorMask(1, 1, 1, 1); glDepthMask(1); glStencilMask(0xffffffffu); glClearDepthf(1); glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | (g->depth ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT : 0));
        glClearColor(cc[0], cc[1], cc[2], cc[3]); glColorMask(cm[0], cm[1], cm[2], cm[3]); glDepthMask(dm);
        glStencilMaskSeparate(GL_FRONT, (GLuint)sm); glStencilMaskSeparate(GL_BACK, (GLuint)sb); glClearStencil(ss); glClearDepthf(dc);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)dr);
        if (sc) glEnable(GL_SCISSOR_TEST);
        if (rz) glEnable(GL_RASTERIZER_DISCARD);
    }
    if (media_wakeup) media_wakeup();
}

int gl_capture(GlCtx *g) {
    if (g->capture <= 0) g->capture = live_open();
    return g->capture;
}

void gl_tick(void) {
    for (int i = 0; i < GLCTX_MAX; i++) {
        GlCtx *g = g_gl[i];
        if (!g || (!g->fence[0] && !g->fence[1]) || !make_current(g)) continue;
        int k = g->next;   /* the older readback first */
        finish(g, k, false); finish(g, k ^ 1, false);
    }
}
int gl_timeout_ms(void) {
    for (int i = 0; i < GLCTX_MAX; i++) if (g_gl[i] && (g_gl[i]->fence[0] || g_gl[i]->fence[1])) return 2;
    return -1;
}

void gl_close(const void *owner, int id) {
    GlCtx *g = id >= 0 && id < GLCTX_MAX ? g_gl[id] : NULL;
    if (!g || g->owner != owner) return;
    g_gl[id] = NULL;
    if (make_current(g)) {
        for (int k = 0; k < 2; k++) if (g->fence[k]) glDeleteSync((GLsync)g->fence[k]);
        glDeleteBuffers(2, g->pbo);
        fb_free(g);
    }
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); g_current = EGL_NO_CONTEXT;
    eglDestroyContext(g_dpy, (EGLContext)g->egl);
    mp_free((MediaPlayer *)g->player);
    if (g->capture > 0) live_close(g->capture);
    for (int i = 0; i < 6; i++) image_unref(g->pool[i]);
    free(g->exts); free(g);
}
void gl_close_owner(const void *owner) { for (int i = 0; i < GLCTX_MAX; i++) if (g_gl[i] && g_gl[i]->owner == owner) gl_close(owner, i); }

/* an <img>, <video> or canvas frame as RGBA rows top down (malloc), straight or premultiplied alpha */
uint8_t *gl_node_rgba(Node *n, int *w, int *h, bool premul) {
    Image *im = paint_image_hook ? paint_image_hook(n) : NULL;
    if (im && !im->px && (im->yuv || im->gpu)) image_yuv_materialize(im);
    if (!im || !im->px || im->w <= 0 || im->h <= 0) return NULL;
    *w = im->w; *h = im->h;
    size_t np = (size_t)im->w * im->h;
    uint8_t *o = xmalloc(np * 4);
    for (size_t i = 0; i < np; i++) {
        uint32_t v = im->px[i], a = v >> 24, r = (v >> 16) & 0xff, g = (v >> 8) & 0xff, b = v & 0xff;
        if (!premul && a && a < 255) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; }
        o[i * 4] = (uint8_t)r; o[i * 4 + 1] = (uint8_t)g; o[i * 4 + 2] = (uint8_t)b; o[i * 4 + 3] = (uint8_t)a;
    }
    return o;
}
