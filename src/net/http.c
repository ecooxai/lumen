#include <strings.h>
/* HTTP/1.1 client with TLS (OpenSSL), keep-alive connection pooling, redirects, decompression. */
#include "net.h"
#include "trust.h"
#include "file.h"
#include "cache.h"
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <zlib.h>
#include <brotli/decode.h>
#include <zstd.h>
#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
#include <dlfcn.h>
static double g_debug_stream_until;
/* Page-load trace: one numeric line per request while armed; never URLs or headers. */
static double g_trace_until;
static pthread_t g_main_thread;
static int g_trace_seq;
static __thread struct { double connect_ms, tls_ms, ttfb_ms; int new_conns; } t_tr;
void net_trace_load(unsigned seconds) {
    g_main_thread = pthread_self();
    g_trace_until = seconds ? now_ms() + LMIN(seconds, 180u) * 1000.0 : 0;
}
static const char *trace_kind(const NetResponse *r) {
    const char *ct = r ? headers_get(&r->headers, "content-type") : NULL;
    if (!ct) return "none";
    static const char *const k[][2] = { { "javascript", "js" }, { "css", "css" }, { "json", "json" }, { "html", "html" },
        { "image/", "img" }, { "font", "font" }, { "event-stream", "sse" }, { "wasm", "wasm" } };
    for (size_t i = 0; i < ARRLEN(k); i++) if (strstr(ct, k[i][0])) return k[i][1];
    return "other";
}
#else
void net_trace_load(unsigned seconds) { (void)seconds; }
#endif

void net_debug_streams(unsigned seconds) {
#if defined(__ANDROID__) && !defined(NDEBUG)
    g_debug_stream_until = seconds ? now_ms() + LMIN(seconds, 120u) * 1000.0 : 0;
#else
    (void)seconds;
#endif
}

#ifdef __ANDROID__
/* Chromium's reduced Android UA freezes the OS/model tokens at 10/K. */
#define UA_OS "Linux; Android 10; K"
#define UA_FF_OS "Android 10; Mobile"
#define UA_CH_OS "Android"
#define UA_MOBILE " Mobile"
#define UA_CH_MOBILE "?1"
#elif defined(__APPLE__)
#define UA_OS "Macintosh; Intel Mac OS X 10_15_7"
#define UA_FF_OS "Macintosh; Intel Mac OS X 10.15"
#define UA_CH_OS "macOS"
#else
#define UA_OS "X11; Linux x86_64"
#define UA_FF_OS "X11; Linux x86_64"
#define UA_CH_OS "Linux"
#endif
#ifndef UA_MOBILE
#define UA_MOBILE ""
#define UA_CH_MOBILE "?0"
#endif
static const char *const ua_str[UA_COUNT] = {
    "Mozilla/5.0 (" UA_OS ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0" UA_MOBILE " Safari/537.36",
    "Mozilla/5.0 (" UA_FF_OS "; rv:150.0) Gecko/20100101 Firefox/150.0",
    "Mozilla/5.0 (" UA_OS ") AppleWebKit/537.36 (KHTML, like Gecko) Lumen/1.0" UA_MOBILE " Safari/537.36",
};
static const char *const ua_name[UA_COUNT] = { "Chrome 150", "Firefox 150", "Lumen" };
const char *g_user_agent = "Mozilla/5.0 (" UA_OS ") AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0" UA_MOBILE " Safari/537.36";
void net_set_user_agent(int which) { if (which >= 0 && which < UA_COUNT) g_user_agent = ua_str[which]; }
const char *net_user_agent_name(int which) { return which >= 0 && which < UA_COUNT ? ua_name[which] : ""; }
/* "Desktop site": these sites (and their subdomains) see a desktop Chrome on Linux; "*" is every site,
   and "-site" entries are the sites turned off under "*". */
#define UA_DESKTOP "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36"
#define MAX_DESKTOP 64
static char g_desktop[MAX_DESKTOP][128]; static int g_ndesktop;
static pthread_mutex_t g_desktop_mu = PTHREAD_MUTEX_INITIALIZER;
static const char *site_of(const char *host) { return !strncasecmp(host, "www.", 4) ? host + 4 : host; }
/* site is host or a parent site of it */
static bool site_covers(const char *site, const char *host) {
    size_t hn = strlen(host), n = strlen(site);
    return hn >= n && !strcasecmp(host + hn - n, site) && (hn == n || host[hn - n - 1] == '.');
}
static void desktop_del(int k) { memmove(g_desktop[k], g_desktop[k + 1], sizeof g_desktop[0] * (size_t)(g_ndesktop - k - 1)); g_ndesktop--; }
static bool desktop_on_locked(const char *host) {   /* host without "www." */
    bool all = false, on = false;
    for (int i = 0; i < g_ndesktop; i++) {
        const char *e = g_desktop[i];
        if (*e == '-') { if (strcmp(host, "*") && site_covers(e + 1, host)) return false; }
        else if (!strcmp(e, "*")) all = true;
        else if (strcmp(host, "*") && site_covers(e, host)) on = true;
    }
    return all || on;
}
bool net_desktop_host(const char *host) {
    if (!host || !*host) return false;
    pthread_mutex_lock(&g_desktop_mu);
    bool on = desktop_on_locked(site_of(host));
    pthread_mutex_unlock(&g_desktop_mu);
    return on;
}
void net_set_desktop_host(const char *host, bool on) {
    if (!host || !*host) return;
    host = site_of(host);
    if (strlen(host) + 1 >= sizeof g_desktop[0]) return;
    pthread_mutex_lock(&g_desktop_mu);
    bool all = !strcmp(host, "*");
    for (int k = 0; k < g_ndesktop;) {
        const char *e = g_desktop[k];
        bool drop = all ? !on && (*e == '-' || !strcmp(e, "*"))   /* "*" off: back to no sites */
                  : *e == '-' ? site_covers(e + 1, host)             /* on: lift the exclusions over host */
                  : !on && strcmp(e, "*") && site_covers(e, host);    /* off: the sites that cover host */
        if (drop) desktop_del(k); else k++;
    }
    if (desktop_on_locked(host) != on && g_ndesktop < MAX_DESKTOP)   /* still covered by "*" when off: exclude */
        snprintf(g_desktop[g_ndesktop++], sizeof g_desktop[0], "%s%s", on ? "" : "-", host);
    pthread_mutex_unlock(&g_desktop_mu);
}
void net_desktop_restore(const char *entry) {
    if (!entry || !*entry || strlen(entry) >= sizeof g_desktop[0]) return;
    pthread_mutex_lock(&g_desktop_mu);
    int i = 0; while (i < g_ndesktop && strcasecmp(g_desktop[i], entry)) i++;
    if (i == g_ndesktop && g_ndesktop < MAX_DESKTOP) snprintf(g_desktop[g_ndesktop++], sizeof g_desktop[0], "%s", entry);
    pthread_mutex_unlock(&g_desktop_mu);
}
void net_desktop_hosts(char *out, size_t n) {   /* space separated */
    size_t o = 0; if (n) out[0] = 0;
    pthread_mutex_lock(&g_desktop_mu);
    for (int i = 0; i < g_ndesktop && o + strlen(g_desktop[i]) + 2 < n; i++) o += (size_t)snprintf(out + o, n - o, "%s%s", o ? " " : "", g_desktop[i]);
    pthread_mutex_unlock(&g_desktop_mu);
}
/* The sign-in pages of Google, Apple and Microsoft turn away browsers they do not recognise ("This
   browser or app may not be secure"), Lumen's own identity and a Chrome without Chrome's internals
   alike: there Lumen is Firefox, a desktop one when the site is in Desktop site mode. */
static const char *const login_hosts[] = {
    "accounts.google.com", "accounts.youtube.com",
    "appleid.apple.com", "idmsa.apple.com", "account.apple.com",
    "login.microsoftonline.com", "login.microsoft.com", "login.live.com", "account.live.com",
};
bool net_login_host(const char *host) {
    if (!host) return false;
    for (size_t i = 0; i < sizeof login_hosts / sizeof *login_hosts; i++) if (str_ieq(host, login_hosts[i])) return true;
    return false;
}
#ifdef __ANDROID__
#define UA_FF_PHONE "Mozilla/5.0 (Android 10; Mobile; rv:150.0) Gecko/150.0 Firefox/150.0"
#define UA_FF_DESKTOP "Mozilla/5.0 (X11; Linux x86_64; rv:150.0) Gecko/20100101 Firefox/150.0"
#else
#define UA_FF_PHONE ua_str[UA_FIREFOX]
#define UA_FF_DESKTOP ua_str[UA_FIREFOX]
#endif
static const char *user_agent_for_host(const char *host) {
    if (net_login_host(host)) return net_desktop_host(host) ? UA_FF_DESKTOP : UA_FF_PHONE;
    return net_desktop_host(host) ? UA_DESKTOP : g_user_agent;
}
const char *net_user_agent_for_url(const char *url) {
    URL u;
    if (!url || !url_parse(url, &u)) return g_user_agent;
    const char *ua = user_agent_for_host(u.host);
    url_free(&u);
    return ua;
}
void (*net_wakeup)(void) = NULL;

/* ---------- headers ---------- */
void headers_add(Headers *h, const char *n, const char *v) { Header x = { xstrdup(n), xstrdup(v) }; vec_push(*h, x); }
const char *headers_get(const Headers *h, const char *n) { for (int i = 0; i < h->n; i++) if (str_ieq(h->v[i].name, n)) return h->v[i].value; return NULL; }
char *headers_join(const Headers *h, const char *n) {
    size_t len = 0; int k = 0;
    for (int i = 0; i < h->n; i++) if (str_ieq(h->v[i].name, n)) len += strlen(h->v[i].value) + 2, k++;
    if (!k) return NULL;
    char *r = xmalloc(len + 1); r[0] = 0;
    for (int i = 0; i < h->n; i++) if (str_ieq(h->v[i].name, n)) { if (r[0]) strcat(r, ", "); strcat(r, h->v[i].value); }
    return r;
}
void headers_set(Headers *h, const char *n, const char *v) {
    for (int i = 0; i < h->n; i++) if (str_ieq(h->v[i].name, n)) { free(h->v[i].value); h->v[i].value = xstrdup(v); return; }
    headers_add(h, n, v);
}
void headers_free(Headers *h) { for (int i = 0; i < h->n; i++) { free(h->v[i].name); free(h->v[i].value); } vec_free(*h); }

/* ---------- connections ---------- */
typedef struct Conn {
    int fd; SSL *ssl;
    bool proxied; /* plain http through the proxy: requests use absolute-form targets */
    char *key; /* scheme://host:port */
    char rbuf[16384]; size_t rpos, rlen;
    double last_used;
    struct Conn *next;
} Conn;

static SSL_CTX *g_ssl_ctx;
static pthread_mutex_t g_pool_mu = PTHREAD_MUTEX_INITIALIZER;
static Conn *g_pool;

static void conn_close(Conn *c) {
    if (!c) return;
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
    if (c->fd >= 0) close(c->fd);
    free(c->key); free(c);
}

static Conn *pool_take(const char *key) {
    pthread_mutex_lock(&g_pool_mu);
    Conn **pp = &g_pool, *c = NULL; double now = now_ms();
    while (*pp) {
        Conn *x = *pp;
        if (now - x->last_used > 60000) { *pp = x->next; conn_close(x); continue; }
        if (!c && !strcmp(x->key, key)) { *pp = x->next; c = x; continue; }
        pp = &x->next;
    }
    pthread_mutex_unlock(&g_pool_mu);
    if (c) {
        /* check that the peer hasn't closed it */
        struct pollfd pf = { c->fd, POLLIN, 0 };
        if (poll(&pf, 1, 0) > 0 && c->rpos == c->rlen) { char b; ssize_t r = recv(c->fd, &b, 1, MSG_PEEK); if (r <= 0) { conn_close(c); c = NULL; } }
    }
    return c;
}
static void pool_put(Conn *c) {
    c->last_used = now_ms();
    pthread_mutex_lock(&g_pool_mu);
    int n = 0; for (Conn *x = g_pool; x; x = x->next) n++;
    if (n > 32) { pthread_mutex_unlock(&g_pool_mu); conn_close(c); return; }
    c->next = g_pool; g_pool = c;
    pthread_mutex_unlock(&g_pool_mu);
}

static int connect_tcp(const char *host, int port, char **err) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    char ps[16]; snprintf(ps, sizeof ps, "%d", port);
    const char *h = host; char hb[256];
    if (host[0] == '[') { size_t n = strlen(host); snprintf(hb, sizeof hb, "%.*s", (int)(n - 2), host + 1); h = hb; }
    /* LUMEN_HOST_MAP="suffix=ip,..." resolves matching hosts to a fixed address (test harnesses only) */
    const char *map = getenv("LUMEN_HOST_MAP"); char mip[64];
    for (const char *m = map; m && *m;) {
        const char *eq = strchr(m, '='), *end = strchr(m, ','); if (!end) end = m + strlen(m);
        if (!eq || eq > end) break;
        size_t sl = (size_t)(eq - m), hl = strlen(h);
        if (sl && (hl == sl || (hl > sl && h[hl - sl - 1] == '.')) && !strncasecmp(h + hl - sl, m, sl)) { snprintf(mip, sizeof mip, "%.*s", (int)(end - eq - 1), eq + 1); h = mip; break; }
        m = *end ? end + 1 : end;
    }
    int gr = getaddrinfo(h, ps, &hints, &res);
    if (gr) { *err = xstrdup(gai_strerror(gr)); return -1; }
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        /* non-blocking connect with timeout */
        int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (r < 0 && errno == EINPROGRESS) {
            struct pollfd pf = { fd, POLLOUT, 0 };
            if (poll(&pf, 1, 10000) == 1) { int e = 0; socklen_t l = sizeof e; getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &l); r = e ? -1 : 0; }
            else r = -1;
        }
        if (r == 0) { fcntl(fd, F_SETFL, fl); break; }
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { *err = xstrdup("connection failed"); return -1; }
    int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    struct timeval tv = { 30, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    return fd;
}

/* ---------- proxy ---------- */
static pthread_mutex_t g_proxy_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_proxy_host[256], g_proxy_auth[384], g_no_proxy[1024];
static int g_proxy_port;
static void b64enc(const unsigned char *in, size_t n, char *out);

void net_set_proxy(const char *spec) {
    pthread_mutex_lock(&g_proxy_mu);
    g_proxy_host[0] = g_proxy_auth[0] = 0; g_proxy_port = 0;
    const char *s = spec ? spec : "";
    while (*s == ' ') s++;
    if (!strncasecmp(s, "http://", 7)) s += 7;
    char buf[512]; snprintf(buf, sizeof buf, "%s", s);
    char *slash = strchr(buf, '/'); if (slash) *slash = 0;
    char *hp = buf, *at = strrchr(buf, '@');
    if (at) {
        *at = 0; hp = at + 1;
        b64enc((const unsigned char *)buf, strlen(buf), g_proxy_auth);
    }
    char *colon = hp[0] == '[' ? strstr(hp, "]:") : strrchr(hp, ':');
    int port = 8080;
    if (colon) { if (*colon == ']') colon++; *colon = 0; port = atoi(colon + 1); }
    if (*hp && port > 0 && port < 65536) { snprintf(g_proxy_host, sizeof g_proxy_host, "%s", hp); g_proxy_port = port; }
    else g_proxy_auth[0] = 0;
    pthread_mutex_unlock(&g_proxy_mu);
}

/* Loopback and NO_PROXY hosts (comma-separated suffixes, "*" for all) go direct. */
static bool proxy_bypass(const char *host) {
    if (!strcasecmp(host, "localhost") || !strncmp(host, "127.", 4) || !strcmp(host, "[::1]")) return true;
    size_t hl = strlen(host);
    if (hl > 10 && !strcasecmp(host + hl - 10, ".localhost")) return true;
    for (const char *m = g_no_proxy; *m;) {
        while (*m == ',' || *m == ' ') m++;
        const char *end = m; while (*end && *end != ',' && *end != ' ') end++;
        const char *p = m; if (*p == '*' && end - p == 1) return true;
        if (*p == '*') p++;
        if (*p == '.') p++;
        size_t sl = (size_t)(end - p);
        if (sl && (hl == sl || (hl > sl && host[hl - sl - 1] == '.')) && !strncasecmp(host + hl - sl, p, sl)) return true;
        m = end;
    }
    return false;
}

/* Reads a proxy reply head on a raw socket without consuming bytes past it; returns the status code. */
static int proxy_read_head(int fd) {
    char buf[4096]; size_t n = 0;
    while (n < sizeof buf - 1) {
        ssize_t r = recv(fd, buf + n, 1, 0);
        if (r <= 0) return -1;
        n++;
        if (n >= 4 && !memcmp(buf + n - 4, "\r\n\r\n", 4)) break;
    }
    buf[n] = 0;
    return n > 12 && !strncmp(buf, "HTTP/1.", 7) ? atoi(buf + 9) : -1;
}

static int conn_proxy_connect(const URL *u, bool tunnel, bool *proxied, char **err) {
    *proxied = false;
    pthread_mutex_lock(&g_proxy_mu);
    char ph[256], auth[384]; int pp = g_proxy_port;
    snprintf(ph, sizeof ph, "%s", g_proxy_host); snprintf(auth, sizeof auth, "%s", g_proxy_auth);
    bool use = pp && !proxy_bypass(u->host);
    pthread_mutex_unlock(&g_proxy_mu);
    if (!use) return connect_tcp(u->host, u->port, err);
    char *perr = NULL;
    int fd = connect_tcp(ph, pp, &perr);
    if (fd < 0) {
        /* proxy unreachable: fall back to a direct connection, like a "PROXY x; DIRECT" list */
        static bool warned; if (!warned) { warned = true; fprintf(stderr, "lumen: proxy unreachable (%s); connecting directly\n", perr ? perr : "?"); }
        free(perr);
        return connect_tcp(u->host, u->port, err);
    }
    if (!tunnel) { *proxied = true; return fd; }
    SB req; sb_init(&req);
    sb_printf(&req, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n", u->host, u->port, u->host, u->port);
    if (*auth) sb_printf(&req, "Proxy-Authorization: Basic %s\r\n", auth);
    sb_puts(&req, "\r\n");
    bool ok = send(fd, req.s, req.n, 0) == (ssize_t)req.n; sb_free(&req);
    int st = ok ? proxy_read_head(fd) : -1;
    if (st / 100 != 2) {
        close(fd);
        SB b; sb_init(&b); sb_printf(&b, "proxy CONNECT failed (%d)", st); *err = sb_take(&b);
        return -1;
    }
    return fd;
}

static Conn *conn_open(const URL *u, char **err) {
    char key[512]; snprintf(key, sizeof key, "%s://%s:%d", u->scheme, u->host, u->port);
    Conn *c = pool_take(key);
    if (c) return c;
#if defined(__ANDROID__) && !defined(NDEBUG)
    double tc0 = now_ms();
#endif
    bool proxied = false;
    int fd = conn_proxy_connect(u, strcmp(u->scheme, "http") != 0, &proxied, err);
    if (fd < 0) return NULL;
#if defined(__ANDROID__) && !defined(NDEBUG)
    double tc1 = now_ms();
    t_tr.connect_ms += tc1 - tc0; t_tr.new_conns++;
#endif
    c = xcalloc(1, sizeof *c); c->fd = fd; c->key = xstrdup(key); c->proxied = proxied;
    if (!strcmp(u->scheme, "https") || !strcmp(u->scheme, "wss")) {
        c->ssl = SSL_new(g_ssl_ctx);
        SSL_set_fd(c->ssl, fd);
        SSL_set_tlsext_host_name(c->ssl, u->host);
        SSL_set1_host(c->ssl, u->host);
        static const unsigned char alpn[] = "\x08http/1.1";
        SSL_set_alpn_protos(c->ssl, alpn, sizeof alpn - 1);
        if (SSL_connect(c->ssl) != 1) {
            unsigned long e = ERR_get_error(); char eb[256]; ERR_error_string_n(e, eb, sizeof eb);
            long vr = SSL_get_verify_result(c->ssl);
            SB b; sb_init(&b); sb_printf(&b, "TLS handshake failed: %s (verify=%s)", eb, X509_verify_cert_error_string(vr));
            *err = sb_take(&b); conn_close(c); return NULL;
        }
#if defined(__ANDROID__) && !defined(NDEBUG)
        t_tr.tls_ms += now_ms() - tc1;
#endif
    }
    return c;
}

static ssize_t conn_write(Conn *c, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = c->ssl ? SSL_write(c->ssl, (const char *)buf + off, (int)(n - off)) : send(c->fd, (const char *)buf + off, n - off, 0);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return (ssize_t)n;
}
static ssize_t conn_fill(Conn *c) {
    if (c->rpos < c->rlen) return (ssize_t)(c->rlen - c->rpos);
    c->rpos = c->rlen = 0;
    ssize_t r = c->ssl ? SSL_read(c->ssl, c->rbuf, sizeof c->rbuf) : recv(c->fd, c->rbuf, sizeof c->rbuf, 0);
    if (r <= 0) return r;
    c->rlen = (size_t)r; return r;
}
static bool conn_readline(Conn *c, SB *line) {
    sb_clear(line);
    for (;;) {
        if (conn_fill(c) <= 0) return line->n > 0;
        char *s = c->rbuf + c->rpos; size_t n = c->rlen - c->rpos;
        char *nl = memchr(s, '\n', n);
        if (nl) { sb_put(line, s, (size_t)(nl - s)); c->rpos += (size_t)(nl - s) + 1; if (line->n && line->s[line->n - 1] == '\r') line->s[--line->n] = 0; return true; }
        sb_put(line, s, n); c->rpos = c->rlen;
        if (line->n > 1 << 20) return false;
    }
}
static bool conn_readn(Conn *c, SB *out, size_t n) {
    while (n) {
        if (conn_fill(c) <= 0) return false;
        size_t k = LMIN(n, c->rlen - c->rpos);
        sb_put(out, c->rbuf + c->rpos, k); c->rpos += k; n -= k;
    }
    return true;
}

/* ---------- decompression ---------- */
/* HTTP zstd uses the RFC 9659 maximum window, including for streamed bodies. */
static ZSTD_DStream *zstd_decoder_new(void) {
    ZSTD_DStream *ds = ZSTD_createDStream();
    if (!ds) return NULL;
    if (ZSTD_isError(ZSTD_initDStream(ds)) ||
        ZSTD_isError(ZSTD_DCtx_setParameter(ds, ZSTD_d_windowLogMax, 23))) {
        ZSTD_freeDStream(ds); return NULL;
    }
    return ds;
}
static bool zstd_decode(ZSTD_DStream *ds, const char *bytes, size_t length, SB *out, size_t *remaining) {
    if (!ds) return false;
    ZSTD_inBuffer in = { bytes, length, 0 }; char buf[65536];
    for (;;) {
        size_t before = in.pos;
        ZSTD_outBuffer o = { buf, sizeof buf, 0 };
        *remaining = ZSTD_decompressStream(ds, &o, &in);
        if (ZSTD_isError(*remaining)) return false;
        sb_put(out, buf, o.pos);
        /* A completed frame can be followed by another or a skippable frame.
           Drain buffered output, but do not start a new frame at clean EOF. */
        if (in.pos == in.size && (!*remaining || o.pos < sizeof buf)) return true;
        if (in.pos == before && !o.pos) return false;
    }
}
static bool decompress(const char *enc, SB *body) {
    if (!enc || !body->n) return true;
    SB out; sb_init(&out);
    if (str_ieq(enc, "gzip") || str_ieq(enc, "deflate") || str_ieq(enc, "x-gzip")) {
        z_stream z = {0};
        int wb = str_ieq(enc, "deflate") ? 15 : 15 + 16;
        if (str_ieq(enc, "deflate") && body->n >= 2 && ((unsigned char)body->s[0] & 0x0f) != 8) wb = -15;
        if (inflateInit2(&z, wb) != Z_OK) return false;
        z.next_in = (Bytef *)body->s; z.avail_in = (uInt)body->n;
        char buf[65536]; int r;
        do { z.next_out = (Bytef *)buf; z.avail_out = sizeof buf; r = inflate(&z, Z_NO_FLUSH); sb_put(&out, buf, sizeof buf - z.avail_out); }
        while (r == Z_OK);
        inflateEnd(&z);
        if (r != Z_STREAM_END && !out.n) { sb_free(&out); return false; }
    } else if (str_ieq(enc, "br")) {
        BrotliDecoderState *st = BrotliDecoderCreateInstance(NULL, NULL, NULL);
        size_t ain = body->n; const uint8_t *nin = (const uint8_t *)body->s;
        uint8_t buf[65536]; BrotliDecoderResult r;
        do { size_t aout = sizeof buf; uint8_t *nout = buf; r = BrotliDecoderDecompressStream(st, &ain, &nin, &aout, &nout, NULL); sb_put(&out, (char *)buf, sizeof buf - aout); }
        while (r == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);
        BrotliDecoderDestroyInstance(st);
        if (r != BROTLI_DECODER_RESULT_SUCCESS) { sb_free(&out); return false; }
    } else if (str_ieq(enc, "zstd")) {
        ZSTD_DStream *ds = zstd_decoder_new(); size_t remaining = 1;
        bool ok = zstd_decode(ds, body->s, body->n, &out, &remaining) && remaining == 0;
        ZSTD_freeDStream(ds);
        if (!ok) { sb_free(&out); return false; }
    } else return true;
    sb_free(body); *body = out;
    return true;
}

/* ---------- streamed responses ---------- */
typedef struct Ev { int type; NetRequest *req; NetResponse *resp; char *data; size_t n; struct Ev *next;
#if defined(__ANDROID__) && !defined(NDEBUG)
    double queued;
#endif
} Ev; /* 0 done 1 head 2 chunk */
static pthread_mutex_t g_ev_mu = PTHREAD_MUTEX_INITIALIZER;
static Ev *g_ev, *g_ev_tail;
static __thread NetRequest *t_stream;
static __thread SB *t_copy;           /* streamed cacheable GET: a copy of the decoded body for the HTTP cache */
static __thread bool t_copy_over;     /* the copy outgrew its limit */
static __thread NetRequest *t_sink;   /* fetch whose body goes to its download sink */
static __thread bool t_sink_stop;
static __thread const char *t_url;
static void ev_push(int type, NetRequest *req, NetResponse *resp, char *data, size_t n) {
    Ev *e = xcalloc(1, sizeof *e); e->type = type; e->req = req; e->resp = resp; e->data = data; e->n = n;
#if defined(__ANDROID__) && !defined(NDEBUG)
    e->queued = now_ms();
#endif
    pthread_mutex_lock(&g_ev_mu);
    if (g_ev_tail) g_ev_tail->next = e; else g_ev = e;
    g_ev_tail = e;
    pthread_mutex_unlock(&g_ev_mu);
    if (net_wakeup) net_wakeup();
}
typedef struct { int kind; bool started, failed; z_stream z; BrotliDecoderState *br; ZSTD_DStream *zs; size_t remaining; } Dec;
static int enc_kind(const char *enc) {
    if (!enc) return 0;
    if (str_ieq(enc, "gzip") || str_ieq(enc, "x-gzip")) return 1;
    if (str_ieq(enc, "deflate")) return 2;
    if (str_ieq(enc, "br")) return 3;
    if (str_ieq(enc, "zstd")) return 4;
    return 0;
}
static void dec_feed(Dec *d, const char *in, size_t n, SB *out) {
    char buf[65536];
    if (!n || d->failed) return;
    if (d->kind == 1 || d->kind == 2) {
        if (!d->started) { int wb = d->kind == 1 ? 15 + 16 : ((unsigned char)in[0] & 0x0f) != 8 ? -15 : 15; if (inflateInit2(&d->z, wb) != Z_OK) return; d->started = true; }
        d->z.next_in = (Bytef *)in; d->z.avail_in = (uInt)n;
        int r;
        do { d->z.next_out = (Bytef *)buf; d->z.avail_out = sizeof buf; r = inflate(&d->z, Z_NO_FLUSH); sb_put(out, buf, sizeof buf - d->z.avail_out); }
        while (r == Z_OK && (d->z.avail_in || !d->z.avail_out));
    } else if (d->kind == 3) {
        if (!d->br) d->br = BrotliDecoderCreateInstance(NULL, NULL, NULL);
        size_t ain = n; const uint8_t *nin = (const uint8_t *)in; BrotliDecoderResult r;
        do { size_t aout = sizeof buf; uint8_t *nout = (uint8_t *)buf; r = BrotliDecoderDecompressStream(d->br, &ain, &nin, &aout, &nout, NULL); sb_put(out, buf, sizeof buf - aout); }
        while (r == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);
    } else if (d->kind == 4) {
        if (!d->zs) d->zs = zstd_decoder_new();
        if (!zstd_decode(d->zs, in, n, out, &d->remaining)) d->failed = true;
    } else sb_put(out, in, n);
}
static void dec_free(Dec *d) {
    if (d->started) inflateEnd(&d->z);
    if (d->br) BrotliDecoderDestroyInstance(d->br);
    if (d->zs) ZSTD_freeDStream(d->zs);
}
static void stream_out(Dec *d, SB *raw) {
    if (!raw->n) return;
    SB o; sb_init(&o);
    dec_feed(d, raw->s, raw->n, &o);
    sb_clear(raw);
    if (!o.n) { sb_free(&o); return; }
    size_t n = o.n;
    if (t_copy && !t_copy_over) { if (t_copy->n + n > (8u << 20)) t_copy_over = true; else sb_put(t_copy, o.s, n); }
    __atomic_add_fetch(&t_stream->queued, n, __ATOMIC_RELAXED);
    ev_push(2, t_stream, NULL, o.s, n);
    /* flow control: wait while script's stream is full or 16 MB are still undelivered (the socket then backs up) */
    while ((__atomic_load_n(&t_stream->paused, __ATOMIC_RELAXED) || __atomic_load_n(&t_stream->queued, __ATOMIC_RELAXED) > (16u << 20))
           && !__atomic_load_n(&t_stream->cancelled, __ATOMIC_RELAXED)) usleep(5000);
}
static void sink_out(Dec *d, SB *raw) {
    if (!raw->n || t_sink_stop) { sb_clear(raw); return; }
    SB o; sb_init(&o);
    dec_feed(d, raw->s, raw->n, &o);
    sb_clear(raw);
    if (o.n && !t_sink->sink(t_sink, o.s, o.n, t_sink->sink_ud)) t_sink_stop = true;
    sb_free(&o);
}
static bool stream_cancelled(void) { return t_sink_stop || (t_stream && __atomic_load_n(&t_stream->cancelled, __ATOMIC_RELAXED)); }

/* ---------- single HTTP exchange ---------- */
static NetResponse *http_once(const char *method, const URL *u, const Headers *hdrs, const char *body, size_t body_len, bool cookies, bool *retryable) {
    const char *ua = user_agent_for_host(u->host);
    NetResponse *r = xcalloc(1, sizeof *r);
    r->t_start = now_ms();
    char *err = NULL;
    *retryable = false;
    Conn *c = conn_open(u, &err);
    if (!c) { r->error = err; return r; }
    bool reused = c->last_used > 0;
    SB req; sb_init(&req);
    char *pq = url_path_query(u);
    if (c->proxied) {
        if (u->port != url_default_port(u->scheme)) sb_printf(&req, "%s http://%s:%d%s HTTP/1.1\r\n", method, u->host, u->port, pq);
        else sb_printf(&req, "%s http://%s%s HTTP/1.1\r\n", method, u->host, pq);
        pthread_mutex_lock(&g_proxy_mu);
        if (*g_proxy_auth) sb_printf(&req, "Proxy-Authorization: Basic %s\r\n", g_proxy_auth);
        pthread_mutex_unlock(&g_proxy_mu);
    } else sb_printf(&req, "%s %s HTTP/1.1\r\n", method, pq);
    free(pq);
    if (u->port != url_default_port(u->scheme)) sb_printf(&req, "Host: %s:%d\r\n", u->host, u->port); else sb_printf(&req, "Host: %s\r\n", u->host);
    bool has_ua = false, has_accept = false, has_ae = false, has_al = false, has_ch = false;
    for (int i = 0; i < hdrs->n; i++) {
        const char *n = hdrs->v[i].name;
        if (str_ieq(n, "host") || str_ieq(n, "content-length") || str_ieq(n, "connection")) continue;
        if (str_ieq(n, "user-agent")) has_ua = true;
        if (str_ieq(n, "accept")) has_accept = true;
        if (str_ieq(n, "accept-encoding")) has_ae = true;
        if (str_ieq(n, "accept-language")) has_al = true;
        if (str_ieq(n, "sec-ch-ua")) has_ch = true;
        sb_printf(&req, "%s: %s\r\n", n, hdrs->v[i].value);
    }
    if (!has_ua) sb_printf(&req, "User-Agent: %s\r\n", ua);
    if (!has_accept) sb_puts(&req, "Accept: */*\r\n");
    /* Fetch appends identity for Range, including when an encoding was supplied. */
    if (headers_get(hdrs, "range") != NULL) sb_puts(&req, "Accept-Encoding: identity\r\n");
    else if (!has_ae) sb_puts(&req, "Accept-Encoding: gzip, deflate, br, zstd\r\n");
    if (!has_al) sb_puts(&req, "Accept-Language: en-US,en;q=0.9\r\n");
    if (!has_ch && !strcmp(u->scheme, "https") && strstr(ua, "Chrome/")) {
        bool desk = !strcmp(ua, UA_DESKTOP);
        sb_puts(&req, "sec-ch-ua: \"Google Chrome\";v=\"150\", \"Not?A_Brand\";v=\"8\", \"Chromium\";v=\"150\"\r\n");
        sb_puts(&req, desk ? "sec-ch-ua-mobile: ?0\r\nsec-ch-ua-platform: \"Linux\"\r\n" : "sec-ch-ua-mobile: " UA_CH_MOBILE "\r\nsec-ch-ua-platform: \"" UA_CH_OS "\"\r\n");
    }
    if (cookies) { char *ck = cookies_get(u, true); if (ck) { sb_printf(&req, "Cookie: %s\r\n", ck); free(ck); } }
    if (body || (strcmp(method, "GET") && strcmp(method, "HEAD"))) sb_printf(&req, "Content-Length: %zu\r\n", body_len);
    sb_puts(&req, "Connection: keep-alive\r\n\r\n");
    if (conn_write(c, req.s, req.n) < 0 || (body_len && conn_write(c, body, body_len) < 0)) {
        sb_free(&req); conn_close(c); r->error = xstrdup("write failed"); *retryable = reused; return r;
    }
    sb_free(&req);
#if defined(__ANDROID__) && !defined(NDEBUG)
    double tw = now_ms();
#endif

    SB line; sb_init(&line);
    int status = 0;
    do {
        if (!conn_readline(c, &line)) { sb_free(&line); conn_close(c); r->error = xstrdup("no response"); *retryable = reused; return r; }
        if (strncmp(line.s, "HTTP/", 5)) { sb_free(&line); conn_close(c); r->error = xstrdup("bad status line"); return r; }
        const char *sp = strchr(line.s, ' ');
        status = sp ? atoi(sp + 1) : 0;
        const char *sp2 = sp ? strchr(sp + 1, ' ') : NULL;
        free(r->status_text); r->status_text = xstrdup(sp2 ? sp2 + 1 : "");
        headers_free(&r->headers);
        while (conn_readline(c, &line) && line.n) {
            char *colon = strchr(line.s, ':'); if (!colon) continue;
            *colon = 0; char *v = colon + 1; while (*v == ' ' || *v == '\t') v++;
            for (char *e = v + strlen(v); e > v && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
            headers_add(&r->headers, line.s, v);
            if (cookies && str_ieq(line.s, "set-cookie")) cookies_set_from_header(u, v);
        }
    } while (status >= 100 && status < 200 && status != 101);
#if defined(__ANDROID__) && !defined(NDEBUG)
    t_tr.ttfb_ms += now_ms() - tw;
#endif
    r->status = status;
    bool redirect = status >= 300 && status < 400 && status != 304 && headers_get(&r->headers, "location");
    bool streaming = t_stream && !redirect;
    Dec dec = {0};
    NetRequest *sinkreq = t_sink;
    bool sinking = false;
    if (sinkreq && !redirect && !t_stream && strcmp(method, "HEAD")) {
        r->url = xstrdup(t_url ? t_url : "");
        sinking = sinkreq->on_head(sinkreq, r, sinkreq->ud);
        if (sinking) { dec.kind = enc_kind(headers_get(&r->headers, "content-encoding")); t_sink_stop = false; }
    }
    if (streaming) {
        NetResponse *h = xcalloc(1, sizeof *h);
        h->status = status; h->status_text = xstrdup(r->status_text ? r->status_text : ""); h->url = xstrdup(t_url ? t_url : "");
        for (int i = 0; i < r->headers.n; i++) headers_add(&h->headers, r->headers.v[i].name, r->headers.v[i].value);
        ev_push(1, t_stream, h, NULL, 0);
        dec.kind = enc_kind(headers_get(&r->headers, "content-encoding"));
    }

    SB b; sb_init(&b);
    bool keep = true;
    const char *conn_h = headers_get(&r->headers, "connection");
    if (conn_h && str_ieq(conn_h, "close")) keep = false;
    const char *te = headers_get(&r->headers, "transfer-encoding");
    const char *cl = headers_get(&r->headers, "content-length");
    bool ok = true;
    if (!strcmp(method, "HEAD") || status == 204 || status == 304) {
    } else if (te && strstr(te, "chunked")) {
        for (;;) {
            if (!conn_readline(c, &line)) { ok = false; break; }
            size_t n = strtoul(line.s, NULL, 16);
            if (!n) { while (conn_readline(c, &line) && line.n) {} break; }
            if (!conn_readn(c, &b, n)) { ok = false; break; }
            conn_readline(c, &line);
            if (streaming || sinking) { if (sinking) sink_out(&dec, &b); else stream_out(&dec, &b); if (stream_cancelled() || dec.failed) { ok = false; break; } }
        }
    } else if (cl) {
        size_t n = strtoull(cl, NULL, 10);
        if (!streaming && !sinking) ok = conn_readn(c, &b, n);
        else while (n) {
            if (conn_fill(c) <= 0 || stream_cancelled()) { ok = false; break; }
            size_t k = LMIN(n, c->rlen - c->rpos);
            sb_put(&b, c->rbuf + c->rpos, k); c->rpos += k; n -= k;
            if (sinking) sink_out(&dec, &b); else stream_out(&dec, &b);
            if (dec.failed) { ok = false; break; }
        }
    } else {
        keep = false;
        while (conn_fill(c) > 0) {
            sb_put(&b, c->rbuf + c->rpos, c->rlen - c->rpos); c->rpos = c->rlen;
            if (streaming || sinking) { if (sinking) sink_out(&dec, &b); else stream_out(&dec, &b); if (stream_cancelled() || dec.failed) { ok = !t_sink_stop && !dec.failed; break; } }
        }
    }
    sb_free(&line);
    /* Headers may already have resolved fetch(). Propagate a decoding failure
       through the final body callback, including a started frame cut off at EOF.
       No decoder is started for HEAD, null-body statuses or an empty body. */
    bool decoded = true;
    if (streaming || sinking) {
        decoded = !dec.failed && (!dec.zs || dec.remaining == 0);
        dec_free(&dec);
    } else decoded = decompress(headers_get(&r->headers, "content-encoding"), &b);
    if (!ok || !decoded) {
        keep = false; r->error = xstrdup(decoded ? "truncated body" : "content decoding failed"); ok = false;
    }
    if (keep) pool_put(c); else conn_close(c);
    if (streaming && t_copy && !t_copy_over && ok) { sb_free(&b); b = *t_copy; sb_init(t_copy); }   /* the whole body, for the cache */
    r->body_len = b.n; r->body = b.s ? b.s : xcalloc(1, 1);
    r->t_end = now_ms();
    return r;
}

/* Fetch data: URL processing uses byte lengths throughout: percent-decoding
 * may produce NUL, which is data for plain bodies and invalid base64 input. */
static int data_hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (unsigned char)lc(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static char *data_percent_decode(const char *s, size_t n, size_t *length) {
    char *out = xmalloc(n + 1);
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n) {
            int hi = data_hex((unsigned char)s[i + 1]);
            int lo = data_hex((unsigned char)s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[used++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out[used++] = s[i];
    }
    out[used] = 0;
    *length = used;
    return out;
}

static int data_base64_digit(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Infra's forgiving-base64 decoder. Whitespace and omitted final padding are
 * accepted; invalid alphabet, misplaced padding and remainder-one lengths fail.
 * The discarded low bits in the last digit need not be zero. */
static bool data_base64_decode(char *bytes, size_t *length) {
    size_t n = 0;
    for (size_t i = 0; i < *length; i++)
        if (!is_ws((unsigned char)bytes[i])) bytes[n++] = bytes[i];
    if (n && n % 4 == 0 && bytes[n - 1] == '=') {
        n--;
        if (n && bytes[n - 1] == '=') n--;
    }
    if (n % 4 == 1) return false;
    for (size_t i = 0; i < n; i++)
        if (data_base64_digit((unsigned char)bytes[i]) < 0) return false;
    size_t used = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        acc = (acc << 6) | (uint32_t)data_base64_digit((unsigned char)bytes[i]);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes[used++] = (char)((acc >> bits) & 255);
        }
    }
    bytes[used] = 0;
    *length = used;
    return true;
}

static bool data_mime_token(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || (c && strchr("!#$%&'*+-.^_`|~", c));
}

static bool data_http_ws(unsigned char c) {
    return c == '\t' || c == '\n' || c == '\r' || c == ' ';
}

/* Parse and serialize only the data URL's MIME metadata. This follows MIME
 * Sniffing's parameter rules without changing ordinary HTTP response headers. */
static char *data_mime_type(const char *input) {
    const char *p = input, *end = input + strlen(input);
    while (p < end && data_http_ws((unsigned char)*p)) p++;
    while (end > p && data_http_ws((unsigned char)end[-1])) end--;
    const char *type = p;
    while (p < end && *p != '/') p++;
    if (p == type || p == end) return NULL;
    for (const char *q = type; q < p; q++) if (!data_mime_token((unsigned char)*q)) return NULL;
    const char *slash = p++, *subtype = p;
    while (p < end && *p != ';') p++;
    const char *subend = p;
    while (subend > subtype && data_http_ws((unsigned char)subend[-1])) subend--;
    if (subend == subtype) return NULL;
    for (const char *q = subtype; q < subend; q++) if (!data_mime_token((unsigned char)*q)) return NULL;
    SB out; sb_init(&out);
    for (const char *q = type; q < slash; q++) sb_putc(&out, (char)lc((unsigned char)*q));
    sb_putc(&out, '/');
    for (const char *q = subtype; q < subend; q++) sb_putc(&out, (char)lc((unsigned char)*q));
    Headers parameters = {0};
    while (p < end) {
        p++; /* semicolon */
        while (p < end && data_http_ws((unsigned char)*p)) p++;
        const char *name_start = p;
        while (p < end && *p != ';' && *p != '=') p++;
        if (p == end) break;
        if (*p == ';') continue;
        size_t name_length = (size_t)(p - name_start);
        p++; /* equals */
        if (p == end) break;
        SB value; sb_init(&value);
        bool quoted = *p == '"';
        if (quoted) {
            p++;
            while (p < end) {
                char c = *p++;
                if (c == '"') break;
                if (c == '\\' && p < end) c = *p++;
                sb_putc(&value, c);
            }
            while (p < end && *p != ';') p++;
        } else {
            const char *start = p;
            while (p < end && *p != ';') p++;
            const char *stop = p;
            while (stop > start && data_http_ws((unsigned char)stop[-1])) stop--;
            sb_put(&value, start, (size_t)(stop - start));
        }
        bool valid = name_length && (quoted || value.n);
        for (size_t i = 0; valid && i < name_length; i++) valid = data_mime_token((unsigned char)name_start[i]);
        for (size_t i = 0; valid && i < value.n; i++) {
            unsigned char c = (unsigned char)value.s[i];
            valid = c == '\t' || (c >= 0x20 && c != 0x7f);
        }
        if (valid) {
            char *name = xstrndup(name_start, name_length);
            for (size_t i = 0; i < name_length; i++) name[i] = (char)lc((unsigned char)name[i]);
            if (!headers_get(&parameters, name)) headers_add(&parameters, name, value.s ? value.s : "");
            free(name);
        }
        sb_free(&value);
    }
    for (int i = 0; i < parameters.n; i++) {
        const Header *h = &parameters.v[i];
        sb_putc(&out, ';'); sb_puts(&out, h->name); sb_putc(&out, '=');
        bool quote = !h->value[0];
        for (const char *q = h->value; *q; q++) if (!data_mime_token((unsigned char)*q)) quote = true;
        if (quote) sb_putc(&out, '"');
        for (const char *q = h->value; *q; q++) {
            if (quote && (*q == '"' || *q == '\\')) sb_putc(&out, '\\');
            sb_putc(&out, *q);
        }
        if (quote) sb_putc(&out, '"');
    }
    headers_free(&parameters);
    return out.s;
}

static NetResponse *data_url(const char *url) {
    NetResponse *r = xcalloc(1, sizeof *r);
    URL parsed;
    if (!url_parse(url, &parsed)) { r->error = xstrdup("bad data url"); return r; }
    free(parsed.fragment); parsed.fragment = NULL;
    char *serialized = url_to_string(&parsed);
    url_free(&parsed);
    const char *p = serialized + 5, *comma = strchr(p, ',');
    if (!comma) { free(serialized); r->error = xstrdup("bad data url"); return r; }
    char *metadata = xstrndup(p, (size_t)(comma - p));
    char *meta = str_trim(metadata);
    size_t meta_length = strlen(meta);
    bool b64 = false;
    if (meta_length >= 6 && str_ieqn(meta + meta_length - 6, "base64", 6)) {
        size_t marker = meta_length - 6;
        while (marker && meta[marker - 1] == ' ') marker--;
        if (marker && meta[marker - 1] == ';') { b64 = true; meta[marker - 1] = 0; }
    }
    size_t length;
    char *body = data_percent_decode(comma + 1, strlen(comma + 1), &length);
    free(serialized);
    if (b64 && !data_base64_decode(body, &length)) {
        free(body); free(metadata);
        r->error = xstrdup("invalid data url base64");
        return r;
    }
    SB mime_input; sb_init(&mime_input);
    if (*meta == ';') sb_puts(&mime_input, "text/plain");
    sb_puts(&mime_input, meta);
    char *mime = data_mime_type(mime_input.s ? mime_input.s : "");
    headers_add(&r->headers, "content-type", mime ? mime : "text/plain;charset=US-ASCII");
    free(mime); sb_free(&mime_input); free(metadata);
    r->body = body; r->body_len = length;
    r->status = 200; r->status_text = xstrdup("OK"); r->url = xstrdup(url);
    return r;
}

/* Cacheable GETs for a URL already being downloaded wait for that download and
 * then read the stored copy (preloads and module graphs ask for the same files). */
static pthread_mutex_t g_fly_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_fly_cv = PTHREAD_COND_INITIALIZER;
static VEC(uint64_t) g_fly;
static uint64_t fly_hash(const char *url) { uint64_t h = 1469598103934665603ull; for (; *url; url++) h = (h ^ (uint8_t)*url) * 1099511628211ull; return h; }
static int fly_find(uint64_t h) { for (int i = 0; i < g_fly.n; i++) if (g_fly.v[i] == h) return i; return -1; }
/* Returns true once this thread owns the download; false after waiting for another one. */
static bool fly_begin(uint64_t h) {
    pthread_mutex_lock(&g_fly_mu);
    bool waited = false;
    while (fly_find(h) >= 0) { pthread_cond_wait(&g_fly_cv, &g_fly_mu); waited = true; }
    if (!waited) vec_push(g_fly, h);
    pthread_mutex_unlock(&g_fly_mu);
    return !waited;
}
static void fly_end(uint64_t h) {
    pthread_mutex_lock(&g_fly_mu);
    int i = fly_find(h);
    if (i >= 0) g_fly.v[i] = g_fly.v[--g_fly.n];
    pthread_cond_broadcast(&g_fly_cv);
    pthread_mutex_unlock(&g_fly_mu);
}

static NetResponse *net_fetch_impl2(NetRequest *req, char *cache_state);
static NetResponse *net_fetch_impl(NetRequest *req) {
#if defined(__ANDROID__) && !defined(NDEBUG)
    double t0 = now_ms();
    bool trace = t0 < g_trace_until;
    memset(&t_tr, 0, sizeof t_tr);
    char cs = '-';
    NetResponse *r = net_fetch_impl2(req, &cs);
    if (trace && r) {
        double ms = now_ms() - t0;
        bool main = pthread_equal(pthread_self(), g_main_thread);
        __android_log_print(ANDROID_LOG_INFO, "LumenNet", "req=%d %s %s status=%d cache=%c kind=%s bytes=%zu ms=%.0f conns=%d connect_ms=%.0f tls_ms=%.0f ttfb_ms=%.0f%s",
            __atomic_add_fetch(&g_trace_seq, 1, __ATOMIC_RELAXED), main ? "MAIN" : "bg", req->method ? req->method : "GET", r->status, cs, trace_kind(r),
            r->body_len, ms, t_tr.new_conns, t_tr.connect_ms, t_tr.tls_ms, t_tr.ttfb_ms, t_stream ? " stream" : "");
    }
    return r;
#else
    char cs;
    return net_fetch_impl2(req, &cs);
#endif
}
/* cache_state: F fresh hit, R revalidated, S stored, M miss not stored, - uncacheable */
static NetResponse *net_fetch_impl2(NetRequest *req, char *cache_state) {
    if (str_istarts(req->url, "data:")) return data_url(req->url);
    char *url = xstrdup(req->url);
    char *method = xstrdup(req->method ? req->method : "GET");
    const char *body = req->body; size_t blen = req->body_len;
    int redirects = req->max_redirects < 0 ? 0 : req->max_redirects ? req->max_redirects : 20;
    NetResponse *r = NULL;
    for (;;) {
        URL u;
        if (!url_parse(url, &u)) { r = xcalloc(1, sizeof *r); r->error = xstrdup("invalid url"); break; }
        if (!strcmp(u.scheme, "file")) { r = net_file_response(&u, url); url_free(&u); break; }
        if (strcmp(u.scheme, "http") && strcmp(u.scheme, "https")) { r = xcalloc(1, sizeof *r); r->error = xstrdup("unsupported scheme"); url_free(&u); break; }
        net_request_update_fetch_metadata(req, &u);
        /* Streamed fetches deliver the live response head and body to script as they arrive. */
        bool get = !strcmp(method, "GET") && !body;
        bool cacheable = get && !req->on_head && http_cache_enabled() && !headers_get(&req->headers, "range");
        Headers cond = {0}; bool fresh = false;
        NetResponse *cached = cacheable ? http_cache_lookup(url, &req->headers, &fresh, &cond) : NULL;
        /* a streamed fetch uses only fresh entries: a 304 would reach script as its response head */
        if (cached && !fresh && t_stream) { net_response_free(cached); cached = NULL; headers_free(&cond); }
        if (cached && fresh) { r = cached; url_free(&u); *cache_state = 'F'; break; }
        uint64_t fly = 0;
        if (cacheable && !t_stream && !fly_begin(fly = fly_hash(url))) {   /* a stream may never end: nobody waits for it */
            /* another thread just fetched it: use what it stored, else fetch as usual */
            fly = 0;
            headers_free(&cond); net_response_free(cached);
            cached = http_cache_lookup(url, &req->headers, &fresh, &cond);
            if (cached && fresh) { r = cached; url_free(&u); *cache_state = 'F'; break; }
            if (!fly_begin(fly = fly_hash(url))) fly = 0;
        }
        const Headers *hdrs = cached ? &cond : &req->headers;
        bool retry;
        SB copy; sb_init(&copy);
        if (t_stream && cacheable) { t_copy = &copy; t_copy_over = false; }
        t_url = url; t_sink = req->on_head && req->sink ? req : NULL; t_sink_stop = false;
        r = http_once(method, &u, hdrs, body, blen, !req->no_cookies, &retry);
        if (r->error && retry) { net_response_free(r); r = http_once(method, &u, hdrs, body, blen, !req->no_cookies, &retry); }
        t_sink = NULL; t_copy = NULL; sb_free(&copy);
        if (cached && r->status == 304) { NetResponse *m = http_cache_revalidated(url, &req->headers, cached, r); net_response_free(r); r = m; *cache_state = 'R'; }
        else {
            net_response_free(cached);
            if (cacheable) *cache_state = 'M';
            if (cacheable && r->status == 200) { http_cache_store(url, &req->headers, r); *cache_state = 'S'; }
            else if (!get && strcmp(method, "HEAD") && strcmp(method, "OPTIONS") && r->status >= 200 && r->status < 400) http_cache_invalidate(url);
            if (t_stream) { free(r->body); r->body = NULL; r->body_len = 0; }   /* script got the body as chunks */
        }
        if (fly) fly_end(fly);
        headers_free(&cond);
        const char *loc = headers_get(&r->headers, "location");
        if (r->status >= 300 && r->status < 400 && r->status != 304 && loc && redirects-- > 0) {
            char *nu = url_join(url, loc);
            if (r->status == 303 || ((r->status == 301 || r->status == 302) && !strcmp(method, "POST"))) { free(method); method = xstrdup("GET"); body = NULL; blen = 0; }
            net_response_free(r); r = NULL; url_free(&u);
            if (!nu) { r = xcalloc(1, sizeof *r); r->error = xstrdup("bad redirect"); break; }
            URL target;
            bool parsed = url_parse(nu, &target);
            bool allowed = parsed && (!strcmp(target.scheme, "http") || !strcmp(target.scheme, "https"));
            /* a Location without a fragment keeps the request's (Fetch: "set locationURL's fragment") */
            const char *hash = strchr(url, '#');
            if (allowed && hash && !target.fragment) { char *w = xmalloc(strlen(nu) + strlen(hash) + 1); strcpy(w, nu); strcat(w, hash); free(nu); nu = w; }
            if (parsed) url_free(&target);
            if (!allowed) {
                free(nu);
                r = xcalloc(1, sizeof *r);
                r->error = xstrdup("unsupported redirect scheme");
                break;
            }
            free(url); url = nu;
            if (req->cancelled) { r = xcalloc(1, sizeof *r); r->error = xstrdup("cancelled"); break; }
            continue;
        }
        url_free(&u);
        break;
    }
    if (!r->url) r->url = url; else free(url);
    free(method);
    if (!r->body) r->body = xcalloc(1, 1);
    return r;
}

void net_response_free(NetResponse *r) {
    if (!r) return;
    free(r->status_text); free(r->url); headers_free(&r->headers); free(r->body); free(r->error); free(r);
}

bool net_request_control(NetRequest *rq, const char *name, const char *value) {
    if (name[0] != ':') return false;
    if (!strcmp(name, ":redirect")) rq->max_redirects = !strcmp(value, "manual") ? -1 : 0;
    else if (!strcmp(name, ":cookies")) rq->no_cookies = !strcmp(value, "omit");
    return true;
}

NetRequest *net_request_new(const char *method, const char *url) {
    NetRequest *r = xcalloc(1, sizeof *r);
    r->method = xstrdup(method ? method : "GET"); r->url = xstrdup(url);
    return r;
}
static void net_request_free(NetRequest *r) { net_request_free_fetch_metadata(r); free(r->method); free(r->url); headers_free(&r->headers); free(r->body); if (r->prepared_response) net_response_free(r->prepared_response); free(r); }
static NetResponse *net_request_response(NetRequest *req) {
    if (req->prepared_response) { NetResponse *r = req->prepared_response; req->prepared_response = NULL; return r; }
    return net_fetch_impl(req);
}

NetResponse *net_fetch_sync(NetRequest *req) {
    NetResponse *response = net_request_response(req);
    net_request_free(req);
    return response;
}

/* ---------- async worker pool ---------- */
typedef struct Job { NetRequest *req; NetResponse *resp; struct Job *next, *rnext; } Job;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static Job *g_queue, *g_running;
static int g_pending, g_nthreads;
static bool g_quit;
static uint64_t g_next_id = 1;

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_mu);
        while (!g_queue && !g_quit) pthread_cond_wait(&g_cv, &g_mu);
        if (g_quit) { pthread_mutex_unlock(&g_mu); return NULL; }
        /* pick lowest priority value */
        Job **best = &g_queue;
        for (Job **pp = &g_queue; *pp; pp = &(*pp)->next) if ((*pp)->req->priority < (*best)->req->priority) best = pp;
        Job *j = *best; *best = j->next; j->next = NULL;
        j->rnext = g_running; g_running = j;
        pthread_mutex_unlock(&g_mu);
        if (j->req->cancelled) { j->resp = xcalloc(1, sizeof *j->resp); j->resp->error = xstrdup("cancelled"); }
        else { t_stream = j->req->chunk ? j->req : NULL; j->resp = net_request_response(j->req); t_stream = NULL; }
        if (j->req->work && !j->req->cancelled) j->req->work(j->req, j->resp, j->req->ud);
        pthread_mutex_lock(&g_mu);
        for (Job **pp = &g_running; *pp; pp = &(*pp)->rnext) if (*pp == j) { *pp = j->rnext; break; }
        pthread_mutex_unlock(&g_mu);
        ev_push(0, j->req, j->resp, NULL, 0);
        free(j);
    }
}

void net_init(int threads) {
    signal(SIGPIPE, SIG_IGN);
    g_ssl_ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_PEER, NULL);
#ifdef __ANDROID__
    if (!net_trust_roots_loader || !net_trust_roots_loader(g_ssl_ctx))
        fprintf(stderr, "lumen: Android trust store unavailable; HTTPS verification remains enabled\n");
#else
    const char *cands[] = { getenv("SSL_CERT_FILE"), "/opt/homebrew/etc/openssl@3/cert.pem", "/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt" };
    bool loaded = false;
    for (size_t i = 0; i < ARRLEN(cands) && !loaded; i++) if (cands[i] && access(cands[i], R_OK) == 0) loaded = SSL_CTX_load_verify_locations(g_ssl_ctx, cands[i], NULL) == 1;
    if (!loaded) SSL_CTX_set_default_verify_paths(g_ssl_ctx);
#endif
    SSL_CTX_set_session_cache_mode(g_ssl_ctx, SSL_SESS_CACHE_CLIENT);
    const char *px = NULL, *np = getenv("NO_PROXY");
    const char *pvars[] = { "LUMEN_PROXY", "https_proxy", "HTTPS_PROXY", "http_proxy", "HTTP_PROXY" };
    for (size_t i = 0; i < ARRLEN(pvars) && !px; i++) px = getenv(pvars[i]);
    if (!np) np = getenv("no_proxy");
    snprintf(g_no_proxy, sizeof g_no_proxy, "%s", np ? np : "");
    net_set_proxy(px);
    g_nthreads = threads;
    for (int i = 0; i < threads; i++) { pthread_t t; pthread_create(&t, NULL, worker, NULL); pthread_detach(t); }
}
void net_shutdown(void) { pthread_mutex_lock(&g_mu); g_quit = true; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_mu); }

uint64_t net_fetch(NetRequest *req) {
    Job *j = xcalloc(1, sizeof *j); j->req = req;
    pthread_mutex_lock(&g_mu);
    req->id = g_next_id++;
    j->next = g_queue; g_queue = j; g_pending++;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_mu);
    return req->id;
}
uint64_t net_fetch_prepared(NetRequest *req, NetResponse *response) {
    req->prepared_response = response;
    return net_fetch(req);
}
void net_cancel(uint64_t id) {
    pthread_mutex_lock(&g_mu);
    for (Job *j = g_queue; j; j = j->next) if (j->req->id == id) j->req->cancelled = true;
    for (Job *j = g_running; j; j = j->rnext) if (j->req->id == id) __atomic_store_n(&j->req->cancelled, true, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&g_mu);
}
void net_pause(uint64_t id, bool on) {
    pthread_mutex_lock(&g_mu);
    for (Job *j = g_queue; j; j = j->next) if (j->req->id == id) j->req->paused = on;
    for (Job *j = g_running; j; j = j->rnext) if (j->req->id == id) __atomic_store_n(&j->req->paused, on, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&g_mu);
}
int net_pending(void) { pthread_mutex_lock(&g_mu); int n = g_pending; pthread_mutex_unlock(&g_mu); return n; }
static void ws_poll(void);
#if defined(__ANDROID__) && !defined(NDEBUG)
/* Per-poll callback cost while a load trace is armed, keyed by callback address
   (library offset, symbolize with the unstripped libmain.so). */
typedef struct { void *fn; int type, n; double ms, max; size_t bytes; } PollCost;
static PollCost g_pc[16]; static int g_npc;
static void poll_cost(void *fn, int type, double ms, size_t bytes) {
    int i = 0; while (i < g_npc && (g_pc[i].fn != fn || g_pc[i].type != type)) i++;
    if (i == g_npc) { if (g_npc == ARRLEN(g_pc)) return; g_pc[g_npc++] = (PollCost){ fn, type, 0, 0, 0, 0 }; }
    g_pc[i].n++; g_pc[i].ms += ms; g_pc[i].bytes += bytes; if (ms > g_pc[i].max) g_pc[i].max = ms;
}
static void poll_cost_flush(double t0, double ws_ms) {
    double total = now_ms() - t0;
    if (total >= 100) {
        __android_log_print(ANDROID_LOG_INFO, "LumenNet", "poll_ms=%.0f ws_ms=%.0f groups=%d", total, ws_ms, g_npc);
        for (int i = 0; i < g_npc; i++) {
            Dl_info di; uintptr_t off = (uintptr_t)g_pc[i].fn;
            if (dladdr(g_pc[i].fn, &di) && di.dli_fbase) off -= (uintptr_t)di.dli_fbase;
            if (g_pc[i].ms >= 20) __android_log_print(ANDROID_LOG_INFO, "LumenNet", "poll cb=0x%zx type=%d n=%d ms=%.0f max_ms=%.0f bytes=%zu",
                (size_t)off, g_pc[i].type, g_pc[i].n, g_pc[i].ms, g_pc[i].max, g_pc[i].bytes);
        }
    }
    g_npc = 0;
}
#endif
int net_poll(void) {
#if defined(__ANDROID__) && !defined(NDEBUG)
    bool tr = now_ms() < g_trace_until; double pt0 = now_ms();
#endif
    ws_poll();
#if defined(__ANDROID__) && !defined(NDEBUG)
    double ws_ms = now_ms() - pt0;
#endif
    pthread_mutex_lock(&g_ev_mu);
    Ev *list = g_ev; g_ev = g_ev_tail = NULL;
    pthread_mutex_unlock(&g_ev_mu);
    int n = 0;
    while (list) {
        Ev *e = list; list = e->next;
        NetRequest *rq = e->req;
#if defined(__ANDROID__) && !defined(NDEBUG)
        double ct0 = tr ? now_ms() : 0;
        void *cfn = e->type == 0 ? (void *)rq->done : e->type == 1 ? (void *)rq->head : (void *)rq->chunk;
        size_t cbytes = e->type == 0 && e->resp ? e->resp->body_len : e->type == 2 ? e->n : 0;
        int e_type_saved = e->type;
#endif
        /* A busy UI can accumulate many chunks from one stream. Their byte
         * boundaries are transport details: deliver an already-queued run in
         * one callback instead of forcing a JS/microtask/layout cycle for each
         * socket read. Never wait for more data or cross another request/event.
         * Bound the combined allocation even when a long response backed up. */
        if (e->type == 2 && list && list->type == 2 && list->req == rq) {
            size_t total = e->n;
            Ev *end = list;
            while (end && end->type == 2 && end->req == rq &&
                    total <= 256 * 1024 && end->n <= 256 * 1024 - total) {
                total += end->n; end = end->next;
            }
            if (end != list) {
                e->data = xrealloc(e->data, total);
                while (list != end) {
                    Ev *part = list; list = part->next;
                    memcpy(e->data + e->n, part->data, part->n); e->n += part->n;
                    free(part->data); free(part);
                }
            }
        }
#if defined(__ANDROID__) && !defined(NDEBUG)
        if (e->type == 1 && now_ms() < g_debug_stream_until) {
            rq->debug_stream = true;
            rq->debug_started = e->queued;
            const char *ct = headers_get(&e->resp->headers, "content-type");
            __android_log_print(ANDROID_LOG_INFO, "LumenStream", "head id=%llu status=%d event_stream=%d encoding=%d",
                (unsigned long long)rq->id, e->resp->status, ct && strstr(ct, "text/event-stream") != NULL,
                enc_kind(headers_get(&e->resp->headers, "content-encoding")));
        }
        if (rq->debug_stream) {
            rq->debug_max_queue_ms = LMAX(rq->debug_max_queue_ms, now_ms() - e->queued);
            if (e->type == 2) { rq->debug_chunks++; rq->debug_bytes += e->n; }
            if (e->type == 0) __android_log_print(ANDROID_LOG_INFO, "LumenStream",
                "done id=%llu status=%d error=%d cancelled=%d chunks=%zu bytes=%zu elapsed_ms=%.0f max_queue_ms=%.0f",
                (unsigned long long)rq->id, e->resp->status, e->resp->error != NULL, rq->cancelled,
                rq->debug_chunks, rq->debug_bytes, e->queued - rq->debug_started, rq->debug_max_queue_ms);
        }
#endif
        if (e->type == 0) {
            pthread_mutex_lock(&g_mu); g_pending--; pthread_mutex_unlock(&g_mu);
            if (!rq->cancelled && rq->done) rq->done(rq, e->resp, rq->ud);
            net_response_free(e->resp); net_request_free(rq);
            n++;
        } else if (e->type == 1) {
            if (!rq->cancelled && rq->head) rq->head(rq, e->resp, rq->ud);
            net_response_free(e->resp);
        } else {
            __atomic_sub_fetch(&rq->queued, e->n, __ATOMIC_RELAXED);
            if (!rq->cancelled && rq->chunk) rq->chunk(rq, e->data, e->n, rq->ud);
            free(e->data);
        }
        free(e);
#if defined(__ANDROID__) && !defined(NDEBUG)
        if (tr) poll_cost(cfn, e_type_saved, now_ms() - ct0, cbytes);
#endif
    }
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (tr) poll_cost_flush(pt0, ws_ms);
#endif
    return n;
}

/* ---------- WebSocket ---------- */
typedef struct WsOut { unsigned char *buf; size_t n; struct WsOut *next; } WsOut;
struct NetWs {
    int refs; char *url, *protocols, *origin, *protocol;
    NetWsFn cb; void *ud;
    pthread_mutex_t mu; WsOut *out, *out_tail; int wake[2];
};
typedef struct WsEv { NetWs *ws; int type; char *data; size_t n; int code; struct WsEv *next; } WsEv;
static pthread_mutex_t g_ws_mu = PTHREAD_MUTEX_INITIALIZER;
static WsEv *g_ws_ev, *g_ws_ev_tail;

static void ws_unref(NetWs *w) {
    pthread_mutex_lock(&g_ws_mu); int r = --w->refs; pthread_mutex_unlock(&g_ws_mu);
    if (r) return;
    for (WsOut *o = w->out; o;) { WsOut *n = o->next; free(o->buf); free(o); o = n; }
    close(w->wake[0]); close(w->wake[1]); pthread_mutex_destroy(&w->mu);
    free(w->url); free(w->protocols); free(w->origin); free(w->protocol); free(w);
}
static void ws_emit(NetWs *w, int type, const char *d, size_t n, int code) {
    WsEv *e = xcalloc(1, sizeof *e); e->ws = w; e->type = type; e->code = code; e->n = n;
    if (n) { e->data = xmalloc(n); memcpy(e->data, d, n); }
    pthread_mutex_lock(&g_ws_mu); w->refs++;
    if (g_ws_ev_tail) g_ws_ev_tail->next = e; else g_ws_ev = e;
    g_ws_ev_tail = e; pthread_mutex_unlock(&g_ws_mu);
    if (net_wakeup) net_wakeup();
}
static void ws_poll(void) {
    pthread_mutex_lock(&g_ws_mu); WsEv *e = g_ws_ev; g_ws_ev = g_ws_ev_tail = NULL; pthread_mutex_unlock(&g_ws_mu);
    while (e) {
        WsEv *n = e->next;
        if (e->ws->cb) e->ws->cb(e->ws, e->type, e->data ? e->data : "", e->n, e->code, e->ws->ud);
        ws_unref(e->ws); free(e->data); free(e); e = n;
    }
}
static WsOut *ws_frame(int op, const char *d, size_t n) {
    WsOut *o = xcalloc(1, sizeof *o); o->buf = xmalloc(n + 14); size_t h = 0;
    o->buf[h++] = (unsigned char)(0x80 | op);
    if (n < 126) o->buf[h++] = (unsigned char)(0x80 | n);
    else if (n < 65536) { o->buf[h++] = 0x80 | 126; o->buf[h++] = (unsigned char)(n >> 8); o->buf[h++] = (unsigned char)n; }
    else { o->buf[h++] = 0x80 | 127; for (int i = 7; i >= 0; i--) o->buf[h++] = (unsigned char)((uint64_t)n >> (8 * i)); }
    unsigned char m[4]; RAND_bytes(m, 4); memcpy(o->buf + h, m, 4); h += 4;
    for (size_t i = 0; i < n; i++) o->buf[h + i] = (unsigned char)d[i] ^ m[i & 3];
    o->n = h + n; return o;
}
static void ws_queue(NetWs *w, WsOut *o) {
    pthread_mutex_lock(&w->mu);
    if (w->out_tail) w->out_tail->next = o; else w->out = o;
    w->out_tail = o; pthread_mutex_unlock(&w->mu);
    char b = 1; (void)!write(w->wake[1], &b, 1);
}
static void b64enc(const unsigned char *in, size_t n, char *out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63]; out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}
static void *ws_thread(void *arg) {
    NetWs *w = arg; URL u; char *err = NULL; Conn *c = NULL; int code = 1006; bool clean = false, sent_close = false;
    SB in, msg, line; sb_init(&in); sb_init(&msg); sb_init(&line); int msg_op = 1;
    bool have_u = url_parse(w->url, &u);
    if (!have_u || !(c = conn_open(&u, &err))) goto out;
    struct timeval tv = { 0, 0 };
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    unsigned char kr[16]; char key[32], want[32]; RAND_bytes(kr, sizeof kr); b64enc(kr, sizeof kr, key);
    {
        char cat[96]; unsigned char dg[SHA_DIGEST_LENGTH];
        snprintf(cat, sizeof cat, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
        SHA1((const unsigned char *)cat, strlen(cat), dg); b64enc(dg, sizeof dg, want);
        SB req; sb_init(&req); char *pq = url_path_query(&u);
        sb_printf(&req, "GET %s HTTP/1.1\r\n", pq); free(pq);
        if (u.port != url_default_port(u.scheme)) sb_printf(&req, "Host: %s:%d\r\n", u.host, u.port); else sb_printf(&req, "Host: %s\r\n", u.host);
        sb_printf(&req, "Connection: Upgrade\r\nPragma: no-cache\r\nCache-Control: no-cache\r\nUser-Agent: %s\r\nUpgrade: websocket\r\n", user_agent_for_host(u.host));
        if (*w->origin) sb_printf(&req, "Origin: %s\r\n", w->origin);
        sb_printf(&req, "Sec-WebSocket-Version: 13\r\nAccept-Encoding: gzip, deflate, br, zstd\r\nAccept-Language: en-US,en;q=0.9\r\n");
        char *ck = cookies_get(&u, true); if (ck) { sb_printf(&req, "Cookie: %s\r\n", ck); free(ck); }
        sb_printf(&req, "Sec-WebSocket-Key: %s\r\n", key);
        if (*w->protocols) sb_printf(&req, "Sec-WebSocket-Protocol: %s\r\n", w->protocols);
        sb_puts(&req, "\r\n");
        bool ok = conn_write(c, req.s, req.n) >= 0; sb_free(&req);
        if (!ok || !conn_readline(c, &line) || line.n < 12 || atoi(line.s + 9) != 101) goto out;
        bool accept_ok = false;
        while (conn_readline(c, &line) && line.n) {
            char *col = strchr(line.s, ':'); if (!col) continue;
            *col = 0; char *v = col + 1; while (*v == ' ' || *v == '\t') v++;
            if (str_ieq(line.s, "set-cookie")) cookies_set_from_header(&u, v);
            else if (str_ieq(line.s, "sec-websocket-accept")) accept_ok = !strcmp(v, want);
            else if (str_ieq(line.s, "sec-websocket-protocol")) { free(w->protocol); w->protocol = xstrdup(v); }
        }
        if (!accept_ok) goto out;
    }
    ws_emit(w, NET_WS_OPEN, w->protocol ? w->protocol : "", w->protocol ? strlen(w->protocol) : 0, 0);
    for (;;) {
        pthread_mutex_lock(&w->mu); WsOut *o = w->out; w->out = w->out_tail = NULL; pthread_mutex_unlock(&w->mu);
        bool wfail = false;
        while (o) { WsOut *n = o->next; if (!wfail && !(sent_close && (o->buf[0] & 15) != 8) && conn_write(c, o->buf, o->n) < 0) wfail = true; if ((o->buf[0] & 15) == 8) sent_close = true; free(o->buf); free(o); o = n; }
        if (wfail) break;
        if (!(c->rpos < c->rlen || (c->ssl && SSL_pending(c->ssl) > 0))) {
            struct pollfd pf[2] = { { c->fd, POLLIN, 0 }, { w->wake[0], POLLIN, 0 } };
            int pr = poll(pf, 2, sent_close ? 5000 : -1);
            if (pr == 0) break;
            if (pf[1].revents & POLLIN) { char b[64]; while (read(w->wake[0], b, sizeof b) > 0) {} }
            if (!(pf[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        }
        if (conn_fill(c) <= 0) break;
        sb_put(&in, c->rbuf + c->rpos, c->rlen - c->rpos); c->rpos = c->rlen;
        size_t p = 0; bool done = false;
        while (!done && in.n - p >= 2) {
            unsigned char *b = (unsigned char *)in.s + p;
            int fin = b[0] & 0x80, op = b[0] & 15; bool masked = b[1] & 0x80; uint64_t len = b[1] & 127; size_t h = 2;
            if (len == 126) { if (in.n - p < 4) break; len = (uint64_t)b[2] << 8 | b[3]; h = 4; }
            else if (len == 127) { if (in.n - p < 10) break; len = 0; for (int i = 0; i < 8; i++) len = len << 8 | b[2 + i]; h = 10; }
            unsigned char mk[4] = { 0 };
            if (masked) { if (in.n - p < h + 4) break; memcpy(mk, b + h, 4); h += 4; }
            if (in.n - p < h + len) break;
            char *pl = in.s + p + h;
            if (masked) for (uint64_t i = 0; i < len; i++) pl[i] ^= (char)mk[i & 3];
            p += h + (size_t)len;
            if (op == 9) { WsOut *pong = ws_frame(10, pl, (size_t)len); if (conn_write(c, pong->buf, pong->n) < 0) done = true; free(pong->buf); free(pong); }
            else if (op == 8) {
                code = len >= 2 ? ((unsigned char)pl[0] << 8 | (unsigned char)pl[1]) : 1005;
                if (len > 2) { sb_clear(&msg); sb_put(&msg, pl + 2, (size_t)len - 2); } else sb_clear(&msg);
                if (!sent_close) { WsOut *cf = ws_frame(8, pl, len >= 2 ? 2 : 0); (void)conn_write(c, cf->buf, cf->n); free(cf->buf); free(cf); }
                clean = done = true;
            } else if (op == 0 || op == 1 || op == 2) {
                if (op) { sb_clear(&msg); msg_op = op; }
                sb_put(&msg, pl, (size_t)len);
                if (fin) { ws_emit(w, msg_op == 2 ? NET_WS_BINARY : NET_WS_TEXT, msg.s, msg.n, 0); sb_clear(&msg); }
            }
        }
        if (p) { memmove(in.s, in.s + p, in.n - p); in.n -= p; }
        if (done) break;
    }
out:
    if (!clean) { sb_clear(&msg); ws_emit(w, NET_WS_ERROR, err ? err : "", err ? strlen(err) : 0, 0); code = 1006; }
    ws_emit(w, NET_WS_CLOSE, msg.s, msg.n, code);
    if (c) conn_close(c);
    if (have_u) url_free(&u);
    free(err); sb_free(&in); sb_free(&msg); sb_free(&line);
    ws_unref(w);
    return NULL;
}
NetWs *net_ws_open(const char *url, const char *protocols, const char *origin, NetWsFn cb, void *ud) {
    NetWs *w = xcalloc(1, sizeof *w);
    w->refs = 2; w->cb = cb; w->ud = ud;
    SB b; sb_init(&b);
    if (!strncasecmp(url, "wss:", 4)) { sb_puts(&b, "https:"); sb_puts(&b, url + 4); }
    else if (!strncasecmp(url, "ws:", 3)) { sb_puts(&b, "http:"); sb_puts(&b, url + 3); }
    else sb_puts(&b, url);
    w->url = sb_take(&b); w->protocols = xstrdup(protocols ? protocols : ""); w->origin = xstrdup(origin ? origin : "");
    pthread_mutex_init(&w->mu, NULL);
    if (pipe(w->wake)) w->wake[0] = w->wake[1] = -1;
    else fcntl(w->wake[0], F_SETFL, fcntl(w->wake[0], F_GETFL, 0) | O_NONBLOCK);
    pthread_t t; pthread_create(&t, NULL, ws_thread, w); pthread_detach(t);
    return w;
}
void net_ws_send(NetWs *w, int op, const char *d, size_t n) { ws_queue(w, ws_frame(op, d, n)); }
void net_ws_close(NetWs *w, int code, const char *reason) {
    char p[125]; size_t n = 0;
    if (code) { p[0] = (char)(code >> 8); p[1] = (char)code; n = 2; size_t r = strlen(reason); if (r > 123) r = 123; memcpy(p + 2, reason, r); n += r; }
    ws_queue(w, ws_frame(8, p, n));
}
void net_ws_release(NetWs *w) {
    if (!w) return;
    w->cb = NULL;
    net_ws_close(w, 1001, "");
    ws_unref(w);
}
