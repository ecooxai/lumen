/* Auxiliary browsing contexts. All registry/context calls run on the browser
 * main thread unless marked pure. Separate V8 isolates exchange owned bytes. */
#ifndef LUMEN_JS_WINDOWS_H
#define LUMEN_JS_WINDOWS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Document Document;
typedef struct JsCtx JsCtx;
typedef uint64_t JsWindowId;
#define JS_WINDOW_MESSAGE_MAX_BYTES (8u * 1024u * 1024u)
#define JS_WINDOW_PENDING_MAX_BYTES (32u * 1024u * 1024u)
#define JS_WINDOW_PENDING_MAX_COUNT 1024u
#define JS_WINDOW_TARGET_MAX_COUNT 256u

typedef enum {
    JS_COOP_UNSAFE_NONE = 0,
    JS_COOP_SAME_ORIGIN_ALLOW_POPUPS,
    JS_COOP_SAME_ORIGIN,
    JS_COOP_SAME_ORIGIN_PLUS_COEP,
    JS_COOP_NOOPENER_ALLOW_POPUPS
} JsWindowCoop;

typedef struct {
    JsWindowId id;             /* registry allocated; never reused */
    JsWindowId source;         /* actual caller (may be an iframe) */
    JsWindowId source_top;     /* native top tab containing that caller */
    JsWindowId opener;         /* zero for noopener / noreferrer */
    const char *name;          /* already resolved name, empty for _blank */
    const char *url;           /* absolute requested destination, or about:blank */
    const char *referrer;      /* already policy-filtered; NULL means omit */
    bool noreferrer;
} JsWindowOpen;

typedef struct {
    JsWindowId id;             /* native TOP window to navigate */
    const char *url;           /* absolute, validated URL */
    const char *referrer;      /* already policy-filtered; NULL means omit */
    bool replace;
} JsWindowNavigate;

typedef struct {
    void *ud;                 /* App/world, NEVER a Page or JsCtx */
    /* Reserve native capacity synchronously (pending reservations count).
     * Copy borrowed strings before returning. No JS, navigation commit, Tab
     * destruction, visible-tab mutation, or arbitrary script inside callbacks.
     * The registry already contains the initial blank record when open runs.
     * Native drain installs a real about:blank Page before requested navigation.
     */
    bool (*open)(void *ud, const JsWindowOpen *request);
    bool (*navigate)(void *ud, const JsWindowNavigate *request);
    bool (*close)(void *ud, JsWindowId id);
    bool (*focus)(void *ud, JsWindowId id);
} JsWindowHost;

/* Create a user-created top context. A copied host table outlives all pages.
 * Append `JsWindowId window_id;` to JsHost, and set it for every page in that tab.
 * Hosts that leave it zero get an isolated context with no auxiliary-tab ops.
 */
JsWindowId js_window_new(const JsWindowHost *host);

/* At an ACTUAL new-document installation, before js_new and old Page free:
 * - applies reserved initial-blank base URL / CSP / origin inheritance;
 * - snapshots the immutable document principal and active sandbox flags;
 * - advances the document epoch and purges only old DESTINATION messages;
 * - enforces COOP and returns a NEW ID if a browsing-group split is required.
 * Save the returned ID in Tab and pass it in JsHost.window_id.
 * A return of zero means the context was closed; discard the incoming page.
 * Do not call for a started-but-uncommitted load, pushState, hash change, or wake.
 */
JsWindowId js_window_commit(JsWindowId id, Document *document, JsWindowCoop coop);

/* Invalidates a native tab (including a pending tab) BEFORE freeing it.
 * Idempotent. Also retires descendant frame records. No host callbacks.
 * Ordinary page navigation uses commit + js_free, not this function.
 */
void js_window_destroy(JsWindowId id);

/* Native trusted activation, before a genuine qualifying mouse/key/touch event.
 * Synthetic DOM events and HTMLElement.click MUST NOT call this function.
 * New auxiliary creation consumes a transient activation once.
 */
void js_window_activate(JsCtx *context);

JsWindowId js_window_id(const JsCtx *context);
/* False while opener/auxiliary/message relationships require a live destination.
 * Add this gate to the existing background-tab js_free sleep condition.
 */
bool js_window_can_sleep(JsWindowId id);
/* Native admission/queue validation, with no URL or realm access. */
bool js_window_is_closed(JsWindowId id);

/* Exact owned directed-message queue counters for lifecycle/memory tests. */
void js_windows_pending(size_t *messages, size_t *bytes);
/* Pure / loader-thread-safe: strict header policy parse + trustworthy URL gate. */
JsWindowCoop js_window_coop(const char *url, const char *coop, const char *coep);
#ifdef __cplusplus
}
#endif
#endif
