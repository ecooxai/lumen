#ifndef LUMEN_NET_H
#define LUMEN_NET_H
#include "../base/util.h"
#include "../base/url.h"

typedef struct { char *name, *value; } Header;
typedef VEC(Header) Headers;

void headers_add(Headers *h, const char *name, const char *value);
const char *headers_get(const Headers *h, const char *name);
char *headers_join(const Headers *h, const char *name);   /* every value of name joined with ", "; NULL if absent */
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
    struct NetFetchMetadata *fetch_metadata; /* owned native subresource request context */
    char *body; size_t body_len;
    NetResponse *prepared_response; /* owned local response; uses the same asynchronous work/done queue */
    int max_redirects;     /* 0 = the default (20); -1 = none: a redirect is the response */
    bool no_cookies;
    bool cancelled;
    bool paused;           /* streaming: script's stream is full; the fetching thread stops reading (net_pause) */
    size_t queued;         /* streaming: body bytes handed to net_poll but not yet delivered */
    int priority;          /* lower = sooner */
    NetDoneFn done;
    NetDoneFn head;        /* streaming: response head (no body) before the body arrives */
    NetChunkFn chunk;      /* streaming: decoded body pieces; done then gets an empty body */
    /* Downloads, on the fetching thread: on_head sees the final (non-redirect) response head and
       returns true to pass the decoded body to sink instead of keeping it; sink returns false to stop. */
    bool (*on_head)(NetRequest *req, const NetResponse *head, void *ud);
    bool (*sink)(NetRequest *req, const char *data, size_t n, void *ud);
    void *sink_ud;
    /* on the fetching thread once the whole response is in, before done runs on the main thread */
    void (*work)(NetRequest *req, NetResponse *resp, void *ud);
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
/* file: URLs never read inside dir (the app's private data: cookies, caches), except below the
   allowed folders or files (NULL-terminated). Call once, before any fetch. */
void net_file_protect(const char *dir, const char *const *allowed);
/* Private disk cache for GET responses in dir (created if missing), bounded to max_bytes. */
void net_cache_init(const char *dir, size_t max_bytes);
void net_shutdown(void);
/* Debug Android builds: log numeric timings of each request for the next seconds (0 disarms). Call on the main thread. */
void net_trace_load(unsigned seconds);
NetRequest *net_request_new(const char *method, const char *url);
/* Native subresources supply their initiator origin, destination and request mode.
   The transport recomputes metadata before every HTTP hop, retaining redirect taint. */
void net_request_set_fetch_metadata(NetRequest *req, const char *origin, const char *destination, const char *mode);
void net_request_update_fetch_metadata(NetRequest *req, const URL *current_url);
void net_request_free_fetch_metadata(NetRequest *req);
/* fetch()'s control pseudo-headers from script, never sent: ":redirect: manual" returns a redirect as the response,
   ":cookies: omit" neither sends nor stores cookies. Returns false for an ordinary header. */
bool net_request_control(NetRequest *rq, const char *name, const char *value);
/* Asynchronous: callbacks fire on the thread calling net_poll(). Ownership of req moves to net. */
uint64_t net_fetch(NetRequest *req);
/* Takes ownership of both objects; local bytes run work() off-thread and done() in net_poll(). */
uint64_t net_fetch_prepared(NetRequest *req, NetResponse *response);
void net_cancel(uint64_t id);
/* streaming flow control: a paused request reads no more of its body until resumed */
void net_pause(uint64_t id, bool on);
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
/* the site of a host (its registrable domain, approximated: example.co.uk for www.example.co.uk) */
void net_site_of(const char *host, char *out, size_t n);
/* the cookies of a site and its subdomains: counts them, or deletes them and returns how many went */
int cookies_site(const char *site, bool remove);
void cookies_clear(void);   /* every cookie (WebDriver Delete All Cookies, for WPT) */

extern const char *g_user_agent;
/* Authentication providers must see Lumen's own identity, including after redirects. */
const char *net_user_agent_for_url(const char *url);
bool net_login_host(const char *host);   /* a Google/Apple/Microsoft sign-in host: Lumen is Firefox there */
enum { UA_CHROME, UA_FIREFOX, UA_LUMEN, UA_COUNT };
void net_set_user_agent(int which);   /* UA_CHROME (default), UA_FIREFOX or UA_LUMEN */
const char *net_user_agent_name(int which);
/* "Desktop site" per site (host and its subdomains, www. ignored): desktop UA and client hints. */
bool net_desktop_host(const char *host);
void net_set_desktop_host(const char *host, bool on);
void net_desktop_hosts(char *out, size_t n);   /* space-separated, for saving */
void net_desktop_restore(const char *entry);    /* one saved entry, as net_desktop_hosts wrote it */
#endif
