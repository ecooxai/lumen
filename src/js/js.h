/* JavaScript (V8) integration: per-page context, DOM bindings, timers, fetch/XHR */
#ifndef LUMEN_JS_H
#define LUMEN_JS_H
#include "../dom/dom.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct JsCtx JsCtx;
/* Resolve a Blob URL from this document or a same-origin frame realm. No script-visible
   properties are consulted. Optional bytes/type outputs are malloc-owned by the caller. */
bool js_read_blob(JsCtx *ctx, const char *url, unsigned char **bytes, size_t *len, char **type);
/* system clipboard, set by the embedder (navigator.clipboard, execCommand copy); get returns malloc'd text */
extern void (*js_clip_set)(const char *text);
extern char *(*js_clip_get)(void);
/* the clipboard's image (malloc'd bytes and MIME type), else NULL */
extern unsigned char *(*js_clip_image)(char **mime, size_t *len);
/* modifier keys held for the next dispatched event: 1 shift, 2 ctrl, 4 alt, 8 meta */
extern int js_key_mods;

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
    void (*sync)(void *ud, Document *d, bool layout);  /* flush style (and layout) before geometry/style queries */
    /* nested browsing context for <iframe>: fill *child (its ud is the frame's host data) */
    void (*frame_open)(void *ud, Node *iframe, Document *d, struct JsHost *child);
    void (*frame_close)(void *child_ud);
    /* Lumen's own pages only: a command of lumenInternal(), answered with a malloc'd string or NULL */
    char *(*internal)(void *ud, const char *cmd);
    /* the Fullscreen API: the page is to fill the window (on) or go back to its place (off) */
    void (*fullscreen)(void *ud, bool on);
    /* window.open: a new tab (or the tab the page named name) loading url; opener is the calling context's
       js_ctx_id, 0 for noopener. Returns the tab's window id, 0 when none opened. */
    uint32_t (*open_window)(void *ud, const char *url, const char *name, uint32_t opener);
    /* on window wid: 0 is it closed, 1 close it (a tab a script opened), 2 load arg in it, 3 show it;
       4 window.close() of the calling tab (wid unused) */
    int (*window_op)(void *ud, uint32_t wid, int op, const char *arg);
    /* the text node and UTF-16 offset nearest viewport point (x, y), false when there is none */
    bool (*caret)(void *ud, float x, float y, Node **n, int *off);
} JsHost;

void js_global_init(const char *argv0);
JsCtx *js_new(Document *d, const JsHost *host);
/* the tab a page's context shows (its window id) and the context that opened that tab with window.open (0: none) */
void js_set_window(JsCtx *c, uint32_t wid, uint32_t opener);
void js_mem_stats(size_t *heap, size_t *external);
void js_set_background(JsCtx *c, bool bg);
bool js_busy(JsCtx *c);   /* fetches in flight */
void js_free(JsCtx *c);
void js_debug_profile(JsCtx *c);   /* debug Android: restart the armed JS profile (no-op otherwise) */
/* run a parser-inserted script (sets document.currentScript) */
void js_run_script(JsCtx *c, Node *script, const char *src, size_t n, const char *name);
/* Prepare once at parser encounter or dynamic insertion. False means already
   started or ineligible; valid scripts are marked started before fetching. */
bool js_prepare_script(Node *script);
uint64_t js_parser_generation(JsCtx *c);
/* These accept a just-prepared node. ordered is only for dynamic async=false. */
void js_queue_classic_script(JsCtx *c, Node *script, const char *url, bool ordered);
void js_run_deferred_script(JsCtx *c, Node *script, const char *src, size_t n, const char *url);
/* Execute using the classification captured at preparation, not live attributes. */
void js_run_prepared_classic(JsCtx *c, Node *script, const char *src, size_t n, const char *url, bool external, bool parser_point);
void js_eval(JsCtx *c, const char *src, const char *name);
/* Runs src as a classic script; returns its completion value as text (NULL for undefined), or the
   exception message with *failed set. Caller frees. */
char *js_eval_result(JsCtx *c, const char *src, const char *name, bool *failed);
/* run a <script type=module>; src NULL fetches url. Inline modules pass the document url */
void js_run_module(JsCtx *c, Node *script, const char *src, size_t n, const char *url);
/* A parser-fetched root, including a failed fetch (src == NULL); never refetch it. */
void js_run_module_fetched(JsCtx *c, Node *script, const char *src, size_t n, const char *url, const char *ctype);
/* Fetches module scripts and their static import graphs in parallel (blocking, any thread)
   so that later module loads find the sources ready. */
void js_module_prefetch(const char *const *urls, int n, const char *referrer);
/* Keeps compiled bytecode of large external scripts in dir, bounded to max_bytes. */
void js_code_cache_init(const char *dir, size_t max_bytes);
/* A consumed module response; NULL on failure. Caller frees source and *ctype. */
char *js_module_take(const char *url, const char *referrer, size_t *len, char **ctype);
/* dispatch a trusted event; kind: "Event", "MouseEvent", "KeyboardEvent", "FocusEvent", "WheelEvent".
   returns false when default was prevented */
bool js_dispatch(JsCtx *c, Node *target, const char *type, const char *kind, bool bubbles, bool cancelable,
                 double x, double y, int button, const char *key);
/* a trusted beforeinput (cancelable; false when cancelled) or input event of a text edit; for input, lo16..hi16 is
   the replaced UTF-16 span of the old value and n16 the inserted length (lo16 < 0: unknown) */
bool js_dispatch_input(JsCtx *c, Node *target, const char *type, const char *input_type, const char *data, int lo16, int hi16, int n16);
bool js_dispatch_window(JsCtx *c, const char *type);
unsigned js_touch(JsCtx *c, Node *target, const char *phase, uint64_t device, uint64_t finger,
                  double x, double y, double pressure);
bool js_touch_compat(JsCtx *c, Node *target, const char *type, const char *kind, double x, double y);
void js_release_pins(void);   /* end of a host event batch: unpin js_dispatch targets */
void js_set_ready_state(JsCtx *c, int state);
/* the window load event (readyState complete), or later once the document's iframes have loaded */
void js_fire_load(JsCtx *c);
/* host hook: true while the page's own subresources (images) still delay its load event */
extern bool (*js_load_blocked)(JsCtx *c);
/* fire a load event deferred by js_load_blocked or by frames, once nothing delays it any more */
void js_load_recheck(JsCtx *c);
/* ms until the next timer / animation frame is due; <0 if none */
double js_next_deadline(JsCtx *c);
/* run due timers, animation frames and microtasks */
void js_tick(JsCtx *c);
bool js_wants_frame(JsCtx *c);
/* the live child browsing context's document for an <iframe>, or NULL */
Document *js_frame_doc(Node *iframe);
JsCtx *js_frame_ctx(Node *iframe);
extern void (*js_wakeup)(void);
/* <input type=file>: host opens a picker and reports the chosen paths with js_files_picked (any thread; n<0 = cancelled) */
extern void (*js_pick_files)(bool multiple, const char *accept);
void js_files_picked(const char *const *paths, int n);
void js_pick_begin(JsCtx *c, bool multiple, const char *accept);   /* called from worker threads when messages are queued */
void js_frame_navigate(Node *iframe, const char *url);
/* the browser leaves fullscreen (Back, Escape): every document with a fullscreen element exits it */
void js_fullscreen_exit(void);
/* A link or form target ("_top", "_parent", a frame's name) naming another browsing context than c:
   navigates it (POST when ctype is set) and returns true; false leaves the navigation to c. */
bool js_navigate_target(JsCtx *c, const char *target, const char *url, const char *body, size_t n, const char *ctype);

#ifdef __cplusplus
}
#endif
#endif
