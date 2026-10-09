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
    void *ud;
    uint64_t id;
};

void net_init(int threads);
void net_shutdown(void);
NetRequest *net_request_new(const char *method, const char *url);
/* Asynchronous: callbacks fire on the thread calling net_poll(). Ownership of req moves to net. */
uint64_t net_fetch(NetRequest *req);
void net_cancel(uint64_t id);
/* Synchronous fetch (blocking). Caller frees response with net_response_free. */
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

void cookies_set_from_header(const URL *u, const char *set_cookie);
char *cookies_get(const URL *u, bool for_http); /* "a=b; c=d" or NULL */
void cookies_set_document(const char *url, const char *cookie_str);
char *cookies_get_document(const char *url);
void cookies_load(const char *path);
bool cookies_save(const char *path); /* writes only when the jar changed */

extern const char *g_user_agent;
#endif
