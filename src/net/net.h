#ifndef LUMEN_NET_H
#define LUMEN_NET_H
#include "../base/util.h"
#include "../base/url.h"

typedef struct { char *name, *value; } Header;
typedef VEC(Header) Headers;

void headers_add(Headers *h, const char *name, const char *value);
const char *headers_get(const Headers *h, const char *name);
void headers_set(Headers *h, const char *name, const char *value);
void headers_free(Headers *h);

typedef struct NetResponse {
    int status;            /* 0 = network error */
    char *status_text;
    char *url;             /* final url after redirects */
    Headers headers;
    char *body; size_t body_len;
    char *error;
    double t_start, t_end;
} NetResponse;

typedef struct NetRequest NetRequest;
typedef void (*NetDoneFn)(NetRequest *req, NetResponse *resp, void *ud);
/* streaming callback; called on the main thread with sequential body chunks if set */
typedef void (*NetChunkFn)(NetRequest *req, const char *data, size_t n, void *ud);

struct NetRequest {
    char *method;
    char *url;
    Headers headers;
    char *body; size_t body_len;
    int max_redirects;
    bool no_cookies;
    bool cancelled;
    int priority;          /* lower = sooner */
    NetDoneFn done;
    NetDoneFn head;        /* streaming: response head (no body) before the body arrives */
    NetChunkFn chunk;      /* streaming: decoded body pieces; done then gets an empty body */
    void *ud;
    uint64_t id;
    char *site_for_cookies;   /* top-level document URL; NULL = same-site (top-level navigation) */
    char *credentials_origin; /* Fetch/XHR same-origin credentials: canonical initiating origin, NULL = include */
    char *no_cors_origin;     /* initiating document URL of a no-cors subresource load: enables CORP checks */
    bool coep_require_corp;   /* initiator has COEP require-corp: cross-origin responses need CORP */
    char *csp, *csp_doc;      /* enforced CSP of the initiating document, checked on every hop against csp_dir */
    const char *csp_dir;
    int active_socket;       /* private to net: protected by the I/O ownership mutex; -1 when detached */
};

void net_init(int threads);
void net_shutdown(void);
NetRequest *net_request_new(const char *method, const char *url);
/* request made on behalf of a page whose top-level document is top_url (drives SameSite cookies) */
NetRequest *net_request_ctx(const char *method, const char *url, const char *top_url);
void net_request_free(NetRequest *req); /* only for requests not transferred to net_fetch */
/* Thread-safe cancellation of a caller-owned request, including blocked socket
   I/O. Keep req alive until both cancellation and net_fetch_sync have returned.
   The system DNS resolver itself is not cancellable. Use net_cancel for async. */
void net_request_cancel(NetRequest *req);
/* registrable-domain ("site") comparison of two hosts; approximate eTLD+1 without the full public suffix list */
bool net_same_site(const char *host_a, const char *host_b);
bool net_url_same_site(const char *url_a, const char *url_b);
/* Asynchronous: callbacks fire on the thread calling net_poll(). Ownership of req moves to net. */
uint64_t net_fetch(NetRequest *req);
void net_cancel(uint64_t id);
/* Synchronous fetch (blocking). Caller retains req; frees both request and response. */
NetResponse *net_fetch_sync(NetRequest *req);
void net_response_free(NetResponse *r);
/* Dispatch completed requests. Returns number dispatched. */
int net_poll(void);
int net_pending(void);
/* Called from worker threads when a result is ready, to wake the UI loop. */
extern void (*net_wakeup)(void);

/* cookies */
/* WebSocket (RFC 6455): callbacks run on the main thread from net_poll */
enum { NET_WS_OPEN, NET_WS_TEXT, NET_WS_BINARY, NET_WS_ERROR, NET_WS_CLOSE };
typedef struct NetWs NetWs;
typedef void (*NetWsFn)(NetWs *ws, int type, const char *data, size_t n, int code, void *ud);
NetWs *net_ws_open(const char *url, const char *protocols, const char *origin, NetWsFn cb, void *ud);
void net_ws_send(NetWs *ws, int opcode, const char *data, size_t n);
void net_ws_close(NetWs *ws, int code, const char *reason);
void net_ws_release(NetWs *ws); /* drop the owner reference; no further callbacks */

void cookies_set_from_header(const URL *u, const char *set_cookie, bool cross_site);
char *cookies_get(const URL *u, bool for_http, bool cross_site); /* "a=b; c=d" or NULL */
/* top_url: the top-level document (NULL = same as url); cross-site frames only see SameSite=None cookies */
void cookies_set_document(const char *url, const char *top_url, const char *cookie_str);
char *cookies_get_document(const char *url, const char *top_url);
/* HSTS (RFC 6797): known hosts are upgraded from http to https */
void hsts_note(const URL *u, const char *sts_header);
char *hsts_upgrade(const char *url);   /* https URL if the host is HSTS-known, else NULL */
/* https URL for an http(s)/ws(s) url that must not load insecurely: HSTS host, or a subresource of an https
   top-level page (mixed content is upgraded, like Chrome's autoupgrade); NULL if no change. ws: maps to wss:. */
char *net_secure_upgrade(const char *top_url, const char *url);

/* Referer value doc_url may send to target under a Referrer-Policy (NULL/unknown = strict-origin-when-cross-origin);
   NULL = send none. Caller frees. */
char *net_referrer(const char *doc_url, const char *target, const char *policy);

/* Content-Security-Policy: csp = enforced policies, one per line. csp_append adds a header value (realloc). */
char *csp_append(char *csp, const char *policy);
/* Cross-Origin-Resource-Policy: would a no-cors response from resp_url be blocked for initiator? */
bool net_corp_blocked(const char *initiator, const char *resp_url, const char *corp, bool require_corp);
/* may this <script> run? src_url NULL = inline (body/n hashed). */
bool csp_allows_url(const char *csp, const char *doc_url, const char *url, const char *dir);   /* img-src, style-src, connect-src, frame-src, ... */
bool csp_allows_style(const char *csp, const char *nonce, const char *body, size_t n);   /* inline <style> */
bool csp_allows_style_attr(const char *csp, const char *body, size_t n);   /* style= attribute */
bool csp_allows_attr(const char *csp, const char *body, size_t n);   /* inline event handler (script-src-attr) */
bool csp_allows_eval(const char *csp, bool wasm);                     /* eval/new Function, or WebAssembly compile */
bool csp_allows_script(const char *csp, const char *doc_url, const char *src_url, const char *nonce,
                       const char *body, size_t n, bool parser_inserted);
void hsts_load(const char *path);
bool hsts_save(const char *path);
void cookies_load(const char *path);
bool cookies_save(const char *path); /* writes only when the jar changed */

extern const char *g_user_agent;
enum { UA_CHROME, UA_FIREFOX, UA_LUMEN, UA_COUNT };
void net_set_user_agent(int which);   /* UA_CHROME (default), UA_FIREFOX or UA_LUMEN */
const char *net_user_agent_name(int which);
#endif
