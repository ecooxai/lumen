/* Deferred native auxiliary-window requests. Slots include real tabs and
 * reservations being drained, so callbacks cannot overbook native capacity. */
#ifndef LUMEN_APP_WINDOW_QUEUE_H
#define LUMEN_APP_WINDOW_QUEUE_H
#include "../js/windows.h"
#include <stdlib.h>
#include <string.h>

#define APP_WINDOW_LIMIT 32
typedef struct {
    JsWindowId id, source_top;
    char *url, *referrer;
    bool reserved, closing;
    bool create, navigate, replace, close;
} AppWindowRequest;
typedef struct {
    AppWindowRequest slots[APP_WINDOW_LIMIT];
    JsWindowId focus;
} AppWindowQueue;
typedef struct {
    AppWindowRequest items[APP_WINDOW_LIMIT];
    unsigned count;
    JsWindowId focus;
} AppWindowBatch;

static inline AppWindowRequest *app_windows_find(AppWindowQueue *q, JsWindowId id) {
    if (id) for (unsigned i = 0; i < APP_WINDOW_LIMIT; i++) if (q->slots[i].id == id) return &q->slots[i];
    return NULL;
}
static inline unsigned app_windows_count(const AppWindowQueue *q) {
    unsigned n = 0;
    for (unsigned i = 0; i < APP_WINDOW_LIMIT; i++) n += q->slots[i].id != 0;
    return n;
}
static inline bool app_windows_track(AppWindowQueue *q, JsWindowId id) {
    if (!id || app_windows_find(q, id)) return false;
    for (unsigned i = 0; i < APP_WINDOW_LIMIT; i++) if (!q->slots[i].id) {
        q->slots[i].id = id;
        return true;
    }
    return false;
}
static inline char *app_windows_copy(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}
static inline bool app_windows_destination(AppWindowRequest *slot, const char *url, const char *referrer, bool replace) {
    if (!url) return false;
    char *u = app_windows_copy(url), *r = app_windows_copy(referrer);
    if (!u || (referrer && !r)) { free(u); free(r); return false; }
    free(slot->url); free(slot->referrer);
    slot->url = u; slot->referrer = r; slot->replace = replace; slot->navigate = true;
    return true;
}
static inline bool app_windows_open(AppWindowQueue *q, const JsWindowOpen *r) {
    if (!r || !app_windows_track(q, r->id)) return false;
    AppWindowRequest *slot = app_windows_find(q, r->id);
    if (!app_windows_destination(slot, r->url ? r->url : "about:blank", r->referrer, true)) {
        memset(slot, 0, sizeof *slot); return false;
    }
    slot->source_top = r->source_top; slot->reserved = slot->create = true;
    q->focus = r->id;
    return true;
}
static inline bool app_windows_navigate(AppWindowQueue *q, const JsWindowNavigate *r) {
    AppWindowRequest *slot = r ? app_windows_find(q, r->id) : NULL;
    return slot && !slot->closing && app_windows_destination(slot, r->url, r->referrer, r->replace);
}
static inline bool app_windows_close(AppWindowQueue *q, JsWindowId id) {
    AppWindowRequest *slot = app_windows_find(q, id);
    if (!slot) return false;
    slot->closing = slot->close = true;
    slot->navigate = slot->replace = false;
    free(slot->url); free(slot->referrer); slot->url = slot->referrer = NULL;
    if (q->focus == id) q->focus = 0;
    return true;
}
static inline bool app_windows_focus(AppWindowQueue *q, JsWindowId id) {
    AppWindowRequest *slot = app_windows_find(q, id);
    if (!slot || slot->closing) return false;
    q->focus = id; return true;
}
static inline bool app_windows_realize(AppWindowQueue *q, JsWindowId id) {
    AppWindowRequest *slot = app_windows_find(q, id);
    if (!slot || !slot->reserved || slot->closing) return false;
    slot->reserved = false; return true;
}
static inline void app_windows_forget(AppWindowQueue *q, JsWindowId id) {
    AppWindowRequest *slot = app_windows_find(q, id);
    if (!slot) return;
    free(slot->url); free(slot->referrer); memset(slot, 0, sizeof *slot);
    if (q->focus == id) q->focus = 0;
}
static inline bool app_windows_rebind(AppWindowQueue *q, JsWindowId old, JsWindowId id) {
    AppWindowRequest *slot = app_windows_find(q, old);
    if (!slot || !id) return false;
    if (old == id) return true;
    if (app_windows_find(q, id)) return false;
    app_windows_forget(q, old);
    slot->id = id;
    return true;
}
static inline bool app_windows_pending(const AppWindowQueue *q) {
    if (q->focus) return true;
    for (unsigned i = 0; i < APP_WINDOW_LIMIT; i++) {
        const AppWindowRequest *slot = &q->slots[i];
        if (slot->create || slot->navigate || slot->close) return true;
    }
    return false;
}
static inline void app_windows_take(AppWindowQueue *q, AppWindowBatch *batch) {
    memset(batch, 0, sizeof *batch);
    batch->focus = q->focus; q->focus = 0;
    for (unsigned i = 0; i < APP_WINDOW_LIMIT; i++) {
        AppWindowRequest *slot = &q->slots[i];
        if (!slot->create && !slot->navigate && !slot->close) continue;
        batch->items[batch->count++] = *slot;
        slot->url = slot->referrer = NULL;
        slot->create = slot->navigate = slot->replace = slot->close = false;
        /* id/reserved/closing remain until actual materialization or teardown.
         * Requests queued by that work therefore still see the occupied slot. */
    }
}
static inline void app_windows_batch_free(AppWindowBatch *batch) {
    for (unsigned i = 0; i < batch->count; i++) { free(batch->items[i].url); free(batch->items[i].referrer); }
    memset(batch, 0, sizeof *batch);
}
static inline void app_windows_clear(AppWindowQueue *q) {
    for (unsigned i = 0; i < APP_WINDOW_LIMIT; i++) app_windows_forget(q, q->slots[i].id);
    q->focus = 0;
}
#endif
