/* JavaScript (V8) integration: per-page context, DOM bindings, timers, fetch/XHR */
#ifndef LUMEN_JS_H
#define LUMEN_JS_H
#include "../dom/dom.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct JsCtx JsCtx;

typedef struct JsHost {
    void *ud;
    void *media;                                               /* MediaCtx* for matchMedia */
    void (*navigate)(void *ud, const char *url);
    void (*set_url)(void *ud, const char *url, bool push);     /* pushState/replaceState/hash change */
    void (*history_go)(void *ud, int delta);
    void (*viewport)(void *ud, float *w, float *h, float *sx, float *sy, float *dpr);
    void (*scroll_to)(void *ud, float x, float y);
    Node *(*hit)(void *ud, float x, float y);                  /* viewport coordinates */
    int (*history_len)(void *ud);
    void (*navigate_post)(void *ud, const char *url, const char *body, size_t len, const char *ctype);
} JsHost;

void js_global_init(const char *argv0);
JsCtx *js_new(Document *d, const JsHost *host);
void js_free(JsCtx *c);
/* run a parser-inserted script (sets document.currentScript) */
void js_run_script(JsCtx *c, Node *script, const char *src, size_t n, const char *name);
void js_eval(JsCtx *c, const char *src, const char *name);
/* dispatch a trusted event; kind: "Event", "MouseEvent", "KeyboardEvent", "FocusEvent", "WheelEvent".
   returns false when default was prevented */
bool js_dispatch(JsCtx *c, Node *target, const char *type, const char *kind, bool bubbles, bool cancelable,
                 double x, double y, int button, const char *key);
bool js_dispatch_window(JsCtx *c, const char *type);
void js_set_ready_state(JsCtx *c, int state);
/* ms until the next timer / animation frame is due; <0 if none */
double js_next_deadline(JsCtx *c);
/* run due timers, animation frames and microtasks */
void js_tick(JsCtx *c);
bool js_wants_frame(JsCtx *c);

#ifdef __cplusplus
}
#endif
#endif
