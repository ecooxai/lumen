/* Beacon uses the ordinary HTTP pool, cookie jar, CSP and TLS verification.
 * Each network hop owns a reference to a job; the request finalizer releases
 * it even when cancellation suppresses the response callback. Nothing here
 * retains a JsCtx, an isolate, a DOM node, or a JavaScript callback.
 */
#include "beacon.h"
#include <pthread.h>
#include <strings.h>

#define KEEPALIVE_BODY_LIMIT (64u * 1024u)
#define BEACON_MEMORY_LIMIT (8u * 1024u * 1024u)
#define BEACON_JOB_LIMIT 256u
#define BEACON_DEADLINE_MS 60000.0

struct NetKeepaliveGroup {
    unsigned refs;
    size_t bytes;
};
static pthread_mutex_t budget_mu = PTHREAD_MUTEX_INITIALIZER;
static size_t queued_memory;
static unsigned queued_jobs;

NetKeepaliveGroup *net_keepalive_group_new(void) {
    NetKeepaliveGroup *g = xcalloc(1, sizeof *g);
    g->refs = 1;
    return g;
}
void net_keepalive_group_release(NetKeepaliveGroup *g) {
    if (g && __atomic_sub_fetch(&g->refs, 1, __ATOMIC_ACQ_REL) == 0) free(g);
}
bool net_keepalive_reserve(NetKeepaliveGroup *g, size_t n) {
    if (!g || n > KEEPALIVE_BODY_LIMIT) return false;
    pthread_mutex_lock(&budget_mu);
    bool ok = n <= KEEPALIVE_BODY_LIMIT - g->bytes;
    if (ok) { g->bytes += n; __atomic_add_fetch(&g->refs, 1, __ATOMIC_RELAXED); }
    pthread_mutex_unlock(&budget_mu);
    return ok;
}
void net_keepalive_unreserve(NetKeepaliveGroup *g, size_t n) {
    if (!g) return;
    pthread_mutex_lock(&budget_mu);
    g->bytes -= n;
    pthread_mutex_unlock(&budget_mu);
    net_keepalive_group_release(g);
}

typedef struct {
    unsigned refs, redirects;
    NetKeepaliveGroup *group;
    size_t body_bytes, reserved_body, reserved_memory;
    char *url, *origin, *document_url, *top_url, *policy, *csp, *type, *body;
    bool cors_mode, cors_tainted, origin_tainted, cross_site, get, preflight;
    bool coep_require_corp;
    double deadline;
} Beacon;

/* Account for both the retained job string and the current request's copy.
   Redirect locations must not grow the detached queue beyond its byte cap. */
static bool replace_string(Beacon *b, char **field, char *replacement) {
    size_t before = *field ? strlen(*field) + 1 : 0;
    size_t after = replacement ? strlen(replacement) + 1 : 0;
    pthread_mutex_lock(&budget_mu);
    bool ok = after <= before || (after - before <= BEACON_MEMORY_LIMIT / 2 &&
        2 * (after - before) <= BEACON_MEMORY_LIMIT - queued_memory);
    if (ok) {
        if (after >= before) { queued_memory += 2 * (after - before); b->reserved_memory += 2 * (after - before); }
        else { queued_memory -= 2 * (before - after); b->reserved_memory -= 2 * (before - after); }
    }
    pthread_mutex_unlock(&budget_mu);
    if (!ok) { free(replacement); return false; }
    free(*field); *field = replacement;
    return true;
}

static void beacon_release(void *ud) {
    Beacon *b = ud;
    if (__atomic_sub_fetch(&b->refs, 1, __ATOMIC_ACQ_REL) != 0) return;
    pthread_mutex_lock(&budget_mu);
    queued_memory -= b->reserved_memory;
    queued_jobs--;
    pthread_mutex_unlock(&budget_mu);
    net_keepalive_unreserve(b->group, b->reserved_body);
    free(b->url); free(b->origin); free(b->document_url); free(b->top_url);
    free(b->policy); free(b->csp); free(b->type); free(b->body); free(b);
}

static bool http_url(const URL *u) {
    return !strcmp(u->scheme, "http") || !strcmp(u->scheme, "https");
}
bool net_beacon_same_site(const char *first, const char *second) {
    URL a = {0}, b = {0};
    bool same = first && second && url_parse(first, &a) && url_parse(second, &b) &&
        http_url(&a) && http_url(&b) && a.host && b.host &&
        !strcmp(a.scheme, b.scheme) && net_same_site(a.host, b.host);
    url_free(&a); url_free(&b);
    return same;
}
static bool same_origin(const char *origin, const char *url) {
    URL u;
    if (!origin || !strcmp(origin, "null") || !url_parse(url, &u)) return false;
    char *other = url_origin(&u);
    bool same = other && strcmp(other, "null") && !strcmp(origin, other);
    free(other); url_free(&u);
    return same;
}
static const char *serialized_origin(const Beacon *b) {
    return b->origin_tainted ? "null" : b->origin;
}

/* Duplicate singleton CORS response fields are invalid, rather than accepting
   whichever value happens to be first in the native header array. */
static const char *singleton_header(const Headers *headers, const char *name) {
    const char *value = NULL;
    for (int i = 0; i < headers->n; i++) if (str_ieq(headers->v[i].name, name)) {
        if (value) return NULL;
        value = headers->v[i].value;
    }
    return value;
}
static bool cors_ok(const Beacon *b, const Headers *headers) {
    const char *origin = singleton_header(headers, "access-control-allow-origin");
    const char *credentials = singleton_header(headers, "access-control-allow-credentials");
    return origin && !strcmp(origin, serialized_origin(b)) &&
        credentials && !strcmp(credentials, "true");
}
static bool opaque_corp_blocked(const Beacon *b, const Headers *headers) {
    if (strcmp(b->origin, "null")) return false;
    const char *corp = singleton_header(headers, "cross-origin-resource-policy");
    if (corp && !strcmp(corp, "cross-origin")) return false;
    return b->coep_require_corp || (corp && (!strcmp(corp, "same-origin") || !strcmp(corp, "same-site")));
}
static bool token_char(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || (c && strchr("!#$%&'*+-.^_`|~", c));
}
/* Validate all list members; a matching early token must not hide a malformed
   suffix. Repeated list-valued fields are combined by iterating every field. */
static bool header_tokens(const Headers *headers, const char *name, const char *wanted,
                          bool *found) {
    *found = false;
    for (int i = 0; i < headers->n; i++) if (str_ieq(headers->v[i].name, name)) {
        const char *p = headers->v[i].value;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            if (!*p) break;
            const char *start = p;
            while (token_char((unsigned char)*p)) p++;
            if (p == start) return false;
            size_t n = (size_t)(p - start);
            if (wanted && n == strlen(wanted) && !strncasecmp(start, wanted, n)) *found = true;
            while (*p == ' ' || *p == '\t') p++;
            if (*p && *p != ',') return false;
        }
    }
    return true;
}
static bool type_safelisted(const char *value) {
    if (!value || !*value) return true;
    size_t n = strlen(value);
    if (n > 128) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if ((*p < 0x20 && *p != '\t') || *p == 0x7f || strchr("\"():<>?@[\\]{}", *p)) return false;
    }
    const char *end = strchr(value, ';');
    if (!end) end = value + n;
    while (value < end && (*value == ' ' || *value == '\t')) value++;
    while (end > value && (end[-1] == ' ' || end[-1] == '\t')) end--;
    n = (size_t)(end - value);
    const char *types[] = { "application/x-www-form-urlencoded", "multipart/form-data", "text/plain" };
    for (size_t i = 0; i < ARRLEN(types); i++)
        if (strlen(types[i]) == n && !strncasecmp(value, types[i], n)) return true;
    return false;
}
static bool valid_header_value(const char *value) {
    for (const unsigned char *p = (const unsigned char *)value; p && *p; p++)
        if ((*p < 0x20 && *p != '\t') || *p >= 0x7f) return false;
    return true;
}

static void beacon_start(Beacon *b, bool preflight_allowed);
static bool set_redirect_policy(Beacon *b, const Headers *headers) {
    const char *policies[] = { "no-referrer", "no-referrer-when-downgrade", "same-origin",
        "origin", "strict-origin", "origin-when-cross-origin", "strict-origin-when-cross-origin", "unsafe-url" };
    for (int i = 0; i < headers->n; i++) if (str_ieq(headers->v[i].name, "referrer-policy")) {
        const char *p = headers->v[i].value;
        while (*p) {
            const char *end = strchr(p, ',');
            if (!end) end = p + strlen(p);
            const char *start = p, *finish = end;
            while (start < finish && (*start == ' ' || *start == '\t')) start++;
            while (finish > start && (finish[-1] == ' ' || finish[-1] == '\t')) finish--;
            for (size_t j = 0; j < ARRLEN(policies); j++)
                if ((size_t)(finish - start) == strlen(policies[j]) && !memcmp(start, policies[j], (size_t)(finish - start))) {
                    if (!replace_string(b, &b->policy, xstrdup(policies[j]))) return false;
                    break;
                }
            p = *end ? end + 1 : end;
        }
    }
    return true;
}

static void beacon_done(NetRequest *request, NetResponse *response, void *ud) {
    (void)request;
    Beacon *b = ud;
    if (!response || response->error || !response->status || now_ms() >= b->deadline) return;
    if (b->preflight) {
        bool method_found, type_found;
        if (response->status < 200 || response->status > 299 || !cors_ok(b, &response->headers) ||
            !header_tokens(&response->headers, "access-control-allow-methods", NULL, &method_found) ||
            !header_tokens(&response->headers, "access-control-allow-headers", "content-type", &type_found) || !type_found) return;
        /* POST is CORS-safelisted; ACA-Methods may be absent. Credentials are
           included in the eventual POST, so ACA-Headers '*' is not a match. */
        beacon_start(b, true);
        return;
    }
    if (b->cors_tainted && !cors_ok(b, &response->headers)) return;
    if (!b->cors_mode && opaque_corp_blocked(b, &response->headers)) return;
    int status = response->status;
    if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) return;
    const char *location = singleton_header(&response->headers, "location");
    if (!location || b->redirects >= 20) return;
    char *next = url_join(b->url, location);
    URL target;
    if (!next || !url_parse(next, &target)) { free(next); return; }
    if (!http_url(&target) || ((b->cors_tainted || (b->cors_mode && !same_origin(b->origin, next))) &&
        target.userinfo && *target.userinfo)) { url_free(&target); free(next); return; }
    char *target_origin = url_origin(&target);
    URL current;
    if (!target_origin || !url_parse(b->url, &current)) { free(target_origin); url_free(&target); free(next); return; }
    char *current_origin = url_origin(&current);
    if (current_origin && strcmp(current_origin, target_origin) && strcmp(b->origin, current_origin)) b->origin_tainted = true;
    if (!net_beacon_same_site(b->url, next) && !net_beacon_same_site(b->origin, b->url)) b->cross_site = true;
    if (!target.fragment && current.fragment) {
        target.fragment = xstrdup(current.fragment);
        free(next); next = url_to_string(&target);
    }
    free(current_origin); free(target_origin); url_free(&current); url_free(&target);
    if (!next) return;
    if (status == 303 || ((status == 301 || status == 302) && !b->get)) {
        b->get = true;
        free(b->body); b->body = NULL; b->body_bytes = 0;
        free(b->type); b->type = NULL;
    }
    if (!set_redirect_policy(b, &response->headers)) { free(next); return; }
    if (!replace_string(b, &b->url, next)) return;
    b->redirects++;
    beacon_start(b, false);
}

static const char *post_origin(const Beacon *b) {
    const char *origin = serialized_origin(b);
    if (b->cors_mode) return origin;
    if (!strcmp(b->policy, "no-referrer")) return "null";
    if (!strcmp(b->policy, "same-origin") && !same_origin(b->origin, b->url)) return "null";
    bool strict = strcmp(b->policy, "unsafe-url") && strcmp(b->policy, "origin") &&
        strcmp(b->policy, "origin-when-cross-origin") && strcmp(b->policy, "same-origin");
    if (strict && str_istarts(b->origin, "https:") && !str_istarts(b->url, "https:")) return "null";
    return origin;
}

static void beacon_start(Beacon *b, bool preflight_allowed) {
    if (now_ms() >= b->deadline) return;
    char *upgraded = net_secure_upgrade(b->top_url, b->url);
    if (upgraded) {
        if (!replace_string(b, &b->url, upgraded)) return;
        preflight_allowed = false;
    }
    URL parsed;
    if (!url_parse(b->url, &parsed)) return;
    bool cross = !same_origin(b->origin, b->url);
    if (!http_url(&parsed) || ((b->cors_tainted || (b->cors_mode && cross)) && parsed.userinfo && *parsed.userinfo)) {
        url_free(&parsed); return;
    }
    url_free(&parsed);
    if (b->csp && !csp_allows_url(b->csp, b->document_url, b->url, "connect-src")) return;
    if (b->cors_mode && cross) b->cors_tainted = true;
    b->preflight = b->cors_tainted && !b->get && !type_safelisted(b->type) && !preflight_allowed;
    NetRequest *request = net_request_ctx(b->preflight ? "OPTIONS" : b->get ? "GET" : "POST", b->url, b->top_url);
    request->no_redirects = true;
    request->discard_response_body = true;
    request->force_cross_site = b->cross_site || !net_beacon_same_site(b->top_url, b->url);
    request->deadline_ms = b->deadline;
    request->priority = 3;
    request->no_cookies = b->preflight;
    request->csp = b->csp ? xstrdup(b->csp) : NULL;
    request->csp_doc = xstrdup(b->document_url);
    request->csp_dir = "connect-src";
    if (!b->cors_mode) {
        request->no_cors_origin = xstrdup(b->origin);
        request->coep_require_corp = b->coep_require_corp;
    }
    char *referrer = net_referrer(b->document_url, b->url, b->policy);
    if (referrer) { headers_add(&request->headers, "Referer", referrer); free(referrer); }
    if (b->preflight) {
        headers_add(&request->headers, "Origin", serialized_origin(b));
        headers_add(&request->headers, "Access-Control-Request-Method", "POST");
        headers_add(&request->headers, "Access-Control-Request-Headers", "content-type");
    } else {
        if (!b->get || b->cors_tainted) headers_add(&request->headers, "Origin", post_origin(b));
        if (b->type && *b->type) headers_add(&request->headers, "Content-Type", b->type);
        if (b->body_bytes) { request->body = xmalloc(b->body_bytes); memcpy(request->body, b->body, b->body_bytes); request->body_len = b->body_bytes; }
    }
    __atomic_add_fetch(&b->refs, 1, __ATOMIC_RELAXED);
    request->ud = b; request->done = beacon_done; request->release = beacon_release;
    net_fetch(request);
}

bool net_beacon_queue(NetKeepaliveGroup *group, const NetBeaconOptions *o,
                      const char *url, const char *type, const void *body, size_t n) {
    if (!group || !o || !url || (n && !body) || !valid_header_value(type)) return false;
    URL parsed;
    if (!url_parse(url, &parsed)) return false;
    bool valid = http_url(&parsed);
    url_free(&parsed);
    if (!valid || n > KEEPALIVE_BODY_LIMIT) return false;
    const char *strings[] = { url, o->origin, o->document_url, o->top_origin, o->referrer_policy, o->csp, type };
    size_t memory = sizeof(Beacon) + sizeof(NetRequest) + 2048 + 2 * n;
    for (size_t i = 0; i < ARRLEN(strings); i++) if (strings[i]) {
        size_t bytes = strlen(strings[i]) + 1;
        if (bytes > BEACON_MEMORY_LIMIT / 2 || memory > BEACON_MEMORY_LIMIT - 2 * bytes) return false;
        memory += 2 * bytes;
    }
    if (!net_keepalive_reserve(group, n)) return false;
    pthread_mutex_lock(&budget_mu);
    bool room = queued_jobs < BEACON_JOB_LIMIT && memory <= BEACON_MEMORY_LIMIT - queued_memory;
    if (room) { queued_memory += memory; queued_jobs++; }
    pthread_mutex_unlock(&budget_mu);
    if (!room) { net_keepalive_unreserve(group, n); return false; }
    Beacon *b = xcalloc(1, sizeof *b);
    b->refs = 1; b->group = group; b->reserved_body = b->body_bytes = n; b->reserved_memory = memory;
    b->url = xstrdup(url); b->origin = xstrdup(o->origin ? o->origin : "null");
    b->document_url = xstrdup(o->document_url ? o->document_url : "");
    b->top_url = xstrdup(o->top_origin ? o->top_origin : "null");
    b->policy = xstrdup(o->referrer_policy ? o->referrer_policy : "");
    b->csp = o->csp ? xstrdup(o->csp) : NULL;
    b->type = type && *type ? xstrdup(type) : NULL;
    b->cors_mode = !type_safelisted(type);
    b->coep_require_corp = o->coep_require_corp;
    b->cross_site = o->force_cross_site;
    b->deadline = now_ms() + BEACON_DEADLINE_MS;
    if (n) { b->body = xmalloc(n); memcpy(b->body, body, n); }
    beacon_start(b, false);
    beacon_release(b);
    return true;
}
