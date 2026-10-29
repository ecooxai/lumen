/* Fetch Metadata for native subresource requests. Script fetch owns its own
 * request context; do not infer context from a URL, Referer, or author headers.
 * https://www.w3.org/TR/fetch-metadata/#sec-fetch-site-header */
#include "net.h"
#include <arpa/inet.h>
#include <pthread.h>
#ifdef LUMEN_HAS_PSL
#include <libpsl.h>
static const char public_suffix_list[] =
#include "fetch_public_suffix_list.inc"
;
static pthread_once_t suffix_once = PTHREAD_ONCE_INIT;
static psl_ctx_t *suffix_context;
static void load_suffix_list(void) {
#ifdef __linux__
    FILE *input = fmemopen((void *)public_suffix_list, sizeof public_suffix_list - 1, "r");
#else
    FILE *input = tmpfile();
    if (input && fwrite(public_suffix_list, 1, sizeof public_suffix_list - 1, input) != sizeof public_suffix_list - 1) {
        fclose(input); input = NULL;
    }
    if (input) rewind(input);
#endif
    if (input) { suffix_context = psl_load_fp(input); fclose(input); }
}
#endif

struct NetFetchMetadata {
    URL origin;
    bool tuple_origin;
    char *destination, *mode;
    int site; /* 0 same-origin, 1 same-site, 2 cross-site; monotonically tainted */
};

static bool web_tuple(const URL *u) {
    return u->scheme && u->host && *u->host && (!strcmp(u->scheme, "http") || !strcmp(u->scheme, "https"));
}
static bool same_site_host(const char *a, const char *b) {
    if (!strcmp(a, b)) return true;
    /* IP hosts have no registrable domain. Do not ask PSL to split an IPv4
       address as though its final numbers were domain labels. */
    struct in_addr ip;
    if (a[0] == '[' || b[0] == '[' || inet_pton(AF_INET, a, &ip) == 1 || inet_pton(AF_INET, b, &ip) == 1) return false;
#ifdef LUMEN_HAS_PSL
    pthread_once(&suffix_once, load_suffix_list);
    if (suffix_context) {
        const char *da = psl_registrable_domain(suffix_context, a);
        const char *db = psl_registrable_domain(suffix_context, b);
        return da && db && !strcmp(da, db);
    }
#endif
    /* Without a full PSL, fail conservatively rather than guess eTLD+1. */
    return false;
}
static bool trustworthy_http(const URL *u) {
    if (!strcmp(u->scheme, "https")) return true;
    struct in_addr ipv4;
    if (inet_pton(AF_INET, u->host, &ipv4) == 1)
        return (ntohl(ipv4.s_addr) >> 24) == 127;
    if (!strcmp(u->host, "[::1]")) return true;
    /* Localhost names need a resolver guarantee that they never reach a
       non-loopback peer. This transport does not yet supply that guarantee. */
    return false;
}
static void remove_header(Headers *h, const char *name) {
    for (int i = 0; i < h->n;) {
        if (!str_ieq(h->v[i].name, name)) { i++; continue; }
        free(h->v[i].name); free(h->v[i].value);
        memmove(h->v + i, h->v + i + 1, (size_t)(h->n - i - 1) * sizeof *h->v);
        h->n--;
    }
}
void net_request_free_fetch_metadata(NetRequest *req) {
    struct NetFetchMetadata *m = req->fetch_metadata;
    if (!m) return;
    url_free(&m->origin); free(m->destination); free(m->mode); free(m);
    req->fetch_metadata = NULL;
}
void net_request_set_fetch_metadata(NetRequest *req, const char *origin, const char *destination, const char *mode) {
    net_request_free_fetch_metadata(req);
    struct NetFetchMetadata *m = xcalloc(1, sizeof *m);
    m->tuple_origin = origin && url_parse(origin, &m->origin) && web_tuple(&m->origin);
    m->destination = xstrdup(destination && *destination ? destination : "empty");
    m->mode = xstrdup(mode && *mode ? mode : "no-cors");
    m->site = m->tuple_origin ? 0 : 2;
    req->fetch_metadata = m;
}
void net_request_update_fetch_metadata(NetRequest *req, const URL *u) {
    struct NetFetchMetadata *m = req->fetch_metadata;
    if (!m) return;
    if (m->site < 2) {
        bool scheme = !strcmp(m->origin.scheme, u->scheme);
        bool host = !strcmp(m->origin.host, u->host);
        if (!(scheme && host && m->origin.port == u->port))
            m->site = scheme && same_site_host(m->origin.host, u->host) ? 1 : 2;
    }
    /* Insecure redirect hops must not inherit headers from a secure hop. */
    remove_header(&req->headers, "Sec-Fetch-Dest");
    remove_header(&req->headers, "Sec-Fetch-Mode");
    remove_header(&req->headers, "Sec-Fetch-Site");
    remove_header(&req->headers, "Sec-Fetch-User");
    if (!trustworthy_http(u)) return;
    static const char *const sites[] = {"same-origin", "same-site", "cross-site"};
    headers_add(&req->headers, "Sec-Fetch-Dest", m->destination);
    headers_add(&req->headers, "Sec-Fetch-Mode", m->mode);
    headers_add(&req->headers, "Sec-Fetch-Site", sites[m->site]);
    /* Stylesheets are subresources, never user-activated navigations. */
}
