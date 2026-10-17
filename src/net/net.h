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
#if defined(__ANDROID__) && !defined(NDEBUG)
    bool debug_stream;
    size_t debug_chunks, debug_bytes;
    double debug_started, debug_max_queue_ms;
#endif
};

void net_init(int threads);
/* HTTP proxy for all connections: "host:port", "http://[user:pass@]host:port", or NULL/"" for direct.
   net_init reads LUMEN_PROXY, then https_proxy/http_proxy (and NO_PROXY) from the environment. */
void net_set_proxy(const char *spec);
/* Private disk cache for GET responses in dir (created if missing), bounded to max_bytes. */
void net_cache_init(const char *dir, size_t max_bytes);
void net_shutdown(void);
/* Debug Android builds: log numeric timings of each request for the next seconds (0 disarms). Call on the main thread. */
void net_trace_load(unsigned seconds);
NetRequest *net_request_new(const char *method, const char *url);
/* Asynchronous: callbacks fire on the thread calling net_poll(). Ownership of req moves to net. */
uint64_t net_fetch(NetRequest *req);
void net_cancel(uint64_t id);
/* Synchronous fetch consumes req. Caller frees response with net_response_free. */
NetResponse *net_fetch_sync(NetRequest *req);
void net_response_free(NetResponse *r);
/* Dispatch completed requests. Returns number dispatched. */
int net_poll(void);
int net_pending(void);
/* Bounded debug transport counters; never logs URLs, headers or payloads. */
void net_debug_streams(unsigned seconds);
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

void cookies_set_from_header(const URL *u, const char *set_cookie);
char *cookies_get(const URL *u, bool for_http); /* "a=b; c=d" or NULL */
void cookies_set_document(const char *url, const char *cookie_str);
char *cookies_get_document(const char *url);
void cookies_load(const char *path);
bool cookies_save(const char *path); /* writes only when the jar changed */

extern const char *g_user_agent;
/* Authentication providers must see Lumen's own identity, including after redirects. */
const char *net_user_agent_for_url(const char *url);
enum { UA_CHROME, UA_FIREFOX, UA_LUMEN, UA_COUNT };
void net_set_user_agent(int which);   /* UA_CHROME (default), UA_FIREFOX or UA_LUMEN */
const char *net_user_agent_name(int which);
#endif
