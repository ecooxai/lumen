#ifndef LUMEN_WEBGL_H
#define LUMEN_WEBGL_H
/* WebGL contexts: one OpenGL ES 3 context per canvas (EGL, offscreen) on the JS thread. The page draws
   into an FBO standing in for the default framebuffer; each present reads it back asynchronously
   through a pixel buffer and hands the frame to the canvas's media player, which the compositor shows
   like a video frame. The bindings (src/js/webgl.cc) call GLES directly once gl_get made it current. */
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct Node;
struct Image;

enum { GL_ATTR_ALPHA = 1, GL_ATTR_DEPTH = 2, GL_ATTR_STENCIL = 4, GL_ATTR_ANTIALIAS = 8, GL_ATTR_PREMULTIPLIED = 16, GL_ATTR_PRESERVE = 32 };

typedef struct GlCtx {
    const void *owner;
    void *egl;                      /* EGLContext */
    struct Node *canvas;
    void *player;                   /* MediaPlayer the canvas shows */
    int version, attrs, w, h, samples;
    unsigned fb, color, depth;      /* the default framebuffer: multisampled when samples > 0 */
    unsigned rfb, rcolor;           /* its single-sample resolve target (samples > 0) */
    bool read_default, draw_default;   /* the page has the default framebuffer bound */
    unsigned pbo[2]; void *fence[2]; int pw[2], ph[2]; int next;   /* readbacks in flight */
    struct Image *pool[6];
    int capture;                    /* live source of captureStream(), 0 if none */
    char *exts;                     /* the driver's extension string */
} GlCtx;

bool gl_available(void);
int gl_create(const void *owner, struct Node *canvas, int version, int attrs, int w, int h);   /* -1 if unsupported */
GlCtx *gl_get(const void *owner, int id);   /* the context, made current; NULL if gone */
void gl_resize(GlCtx *g, int w, int h);
void gl_present(GlCtx *g);                  /* after the page drew to the default framebuffer */
void gl_resolve(GlCtx *g);                  /* multisampled default framebuffer to rfb, for reading */
unsigned gl_read_fb(GlCtx *g);              /* what to bind for reading the default framebuffer */
int gl_capture(GlCtx *g);                   /* live source with this canvas's frames */
void gl_close(const void *owner, int id);
void gl_close_owner(const void *owner);
void gl_tick(void);                         /* main thread: finished readbacks to the canvases */
int gl_timeout_ms(void);
const char *gl_renderer(void);
uint8_t *gl_node_rgba(struct Node *n, int *w, int *h, bool premul);   /* an element's pixels as RGBA (malloc) */
#ifdef __cplusplus
}
#endif
#endif
