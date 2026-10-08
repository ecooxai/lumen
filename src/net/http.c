#include <strings.h>
/* HTTP/1.1 client with TLS (OpenSSL), keep-alive connection pooling, redirects, decompression. */
#include "net.h"
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
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <zlib.h>
#include <brotli/decode.h>
#include <zstd.h>

const char *g_user_agent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
void (*net_wakeup)(void) = NULL;

/* ---------- headers ---------- */
void headers_add(Headers *h, const char *n, const char *v) { Header x = { xstrdup(n), xstrdup(v) }; vec_push(*h, x); }
const char *headers_get(const Headers *h, const char *n) { for (int i = 0; i < h->n; i++) if (str_ieq(h->v[i].name, n)) return h->v[i].value; return NULL; }
void headers_set(Headers *h, const char *n, const char *v) {
    for (int i = 0; i < h->n; i++) if (str_ieq(h->v[i].name, n)) { free(h->v[i].value); h->v[i].value = xstrdup(v); return; }
    headers_add(h, n, v);
}
void headers_free(Headers *h) { for (int i = 0; i < h->n; i++) { free(h->v[i].name); free(h->v[i].value); } vec_free(*h); }

/* ---------- connections ---------- */
typedef struct Conn {
    int fd; SSL *ssl;
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

static Conn *conn_open(const URL *u, char **err) {
    char key[512]; snprintf(key, sizeof key, "%s://%s:%d", u->scheme, u->host, u->port);
    Conn *c = pool_take(key);
    if (c) return c;
    int fd = connect_tcp(u->host, u->port, err);
    if (fd < 0) return NULL;
    c = xcalloc(1, sizeof *c); c->fd = fd; c->key = xstrdup(key);
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
        ZSTD_DStream *ds = ZSTD_createDStream(); ZSTD_initDStream(ds);
        ZSTD_inBuffer in = { body->s, body->n, 0 }; char buf[65536]; size_t r = 0;
        while (in.pos < in.size) { ZSTD_outBuffer o = { buf, sizeof buf, 0 }; r = ZSTD_decompressStream(ds, &o, &in); if (ZSTD_isError(r)) break; sb_put(&out, buf, o.pos); }
        ZSTD_freeDStream(ds);
        if (ZSTD_isError(r)) { sb_free(&out); return false; }
    } else return true;
    sb_free(body); *body = out;
    return true;
}

/* ---------- single HTTP exchange ---------- */
static NetResponse *http_once(const char *method, const URL *u, const Headers *hdrs, const char *body, size_t body_len, bool cookies, bool *retryable) {
    NetResponse *r = xcalloc(1, sizeof *r);
    r->t_start = now_ms();
    char *err = NULL;
    *retryable = false;
    Conn *c = conn_open(u, &err);
    if (!c) { r->error = err; return r; }
    bool reused = c->last_used > 0;
    SB req; sb_init(&req);
    char *pq = url_path_query(u);
    sb_printf(&req, "%s %s HTTP/1.1\r\n", method, pq); free(pq);
    if (u->port != url_default_port(u->scheme)) sb_printf(&req, "Host: %s:%d\r\n", u->host, u->port); else sb_printf(&req, "Host: %s\r\n", u->host);
    bool has_ua = false, has_accept = false, has_ae = false, has_al = false;
    for (int i = 0; i < hdrs->n; i++) {
        const char *n = hdrs->v[i].name;
        if (str_ieq(n, "host") || str_ieq(n, "content-length") || str_ieq(n, "connection")) continue;
        if (str_ieq(n, "user-agent")) has_ua = true;
        if (str_ieq(n, "accept")) has_accept = true;
        if (str_ieq(n, "accept-encoding")) has_ae = true;
        if (str_ieq(n, "accept-language")) has_al = true;
        sb_printf(&req, "%s: %s\r\n", n, hdrs->v[i].value);
    }
    if (!has_ua) sb_printf(&req, "User-Agent: %s\r\n", g_user_agent);
    if (!has_accept) sb_puts(&req, "Accept: */*\r\n");
    if (!has_ae) sb_puts(&req, "Accept-Encoding: gzip, deflate, br, zstd\r\n");
    if (!has_al) sb_puts(&req, "Accept-Language: en-US,en;q=0.9\r\n");
    if (cookies) { char *ck = cookies_get(u, true); if (ck) { sb_printf(&req, "Cookie: %s\r\n", ck); free(ck); } }
    if (body || (strcmp(method, "GET") && strcmp(method, "HEAD"))) sb_printf(&req, "Content-Length: %zu\r\n", body_len);
    sb_puts(&req, "Connection: keep-alive\r\n\r\n");
    if (conn_write(c, req.s, req.n) < 0 || (body_len && conn_write(c, body, body_len) < 0)) {
        sb_free(&req); conn_close(c); r->error = xstrdup("write failed"); *retryable = reused; return r;
    }
    sb_free(&req);

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
    r->status = status;

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
        }
    } else if (cl) {
        size_t n = strtoull(cl, NULL, 10);
        ok = conn_readn(c, &b, n);
    } else {
        keep = false;
        while (conn_fill(c) > 0) { sb_put(&b, c->rbuf + c->rpos, c->rlen - c->rpos); c->rpos = c->rlen; }
    }
    sb_free(&line);
    if (!ok) { keep = false; if (!b.n) { r->error = xstrdup("truncated body"); } }
    if (keep) pool_put(c); else conn_close(c);
    if (!decompress(headers_get(&r->headers, "content-encoding"), &b)) DLOG("decompress failed for %s", u->host);
    r->body_len = b.n; r->body = b.s ? b.s : xcalloc(1, 1);
    r->t_end = now_ms();
    return r;
}

static NetResponse *data_url(const char *url) {
    NetResponse *r = xcalloc(1, sizeof *r);
    const char *p = url + 5, *comma = strchr(p, ',');
    if (!comma) { r->error = xstrdup("bad data url"); return r; }
    char *meta = xstrndup(p, (size_t)(comma - p));
    bool b64 = strstr(meta, ";base64") != NULL;
    char *semi = strstr(meta, ";base64"); if (semi) *semi = 0;
    headers_add(&r->headers, "content-type", meta[0] ? meta : "text/plain;charset=US-ASCII");
    char *dec = url_decode(comma + 1, strlen(comma + 1));
    if (b64) {
        size_t n = strlen(dec); char *out = xmalloc(n + 1); size_t o = 0; uint32_t acc = 0; int bits = 0;
        for (size_t i = 0; i < n; i++) {
            int c = (unsigned char)dec[i], v;
            if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+' || c == '-') v = 62; else if (c == '/' || c == '_') v = 63; else continue;
            acc = (acc << 6) | (uint32_t)v; bits += 6;
            if (bits >= 8) { bits -= 8; out[o++] = (char)((acc >> bits) & 0xff); }
        }
        free(dec); r->body = out; r->body_len = o;
    } else { r->body = dec; r->body_len = strlen(dec); }
    free(meta);
    r->status = 200; r->status_text = xstrdup("OK"); r->url = xstrdup(url);
    return r;
}

static NetResponse *file_url(const URL *u, const char *url) {
    NetResponse *r = xcalloc(1, sizeof *r);
    char *path = url_decode(u->path, strlen(u->path));
    size_t n; char *d = read_file(path, &n); free(path);
    if (!d) { r->status = 404; r->status_text = xstrdup("Not Found"); r->body = xcalloc(1, 1); }
    else { r->status = 200; r->status_text = xstrdup("OK"); r->body = d; r->body_len = n; }
    const char *ext = strrchr(u->path, '.');
    const char *ct = "application/octet-stream";
    if (ext) { if (!strcmp(ext, ".html") || !strcmp(ext, ".htm")) ct = "text/html"; else if (!strcmp(ext, ".css")) ct = "text/css"; else if (!strcmp(ext, ".js") || !strcmp(ext, ".mjs")) ct = "text/javascript";
        else if (!strcmp(ext, ".png")) ct = "image/png"; else if (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")) ct = "image/jpeg"; else if (!strcmp(ext, ".svg")) ct = "image/svg+xml"; else if (!strcmp(ext, ".json")) ct = "application/json"; else if (!strcmp(ext, ".txt")) ct = "text/plain"; }
    headers_add(&r->headers, "content-type", ct);
    r->url = xstrdup(url);
    return r;
}

NetResponse *net_fetch_sync(NetRequest *req) {
    if (str_istarts(req->url, "data:")) return data_url(req->url);
    char *url = xstrdup(req->url);
    char *method = xstrdup(req->method ? req->method : "GET");
    const char *body = req->body; size_t blen = req->body_len;
    int redirects = req->max_redirects ? req->max_redirects : 20;
    NetResponse *r = NULL;
    for (;;) {
        URL u;
        if (!url_parse(url, &u)) { r = xcalloc(1, sizeof *r); r->error = xstrdup("invalid url"); break; }
        if (!strcmp(u.scheme, "file")) { r = file_url(&u, url); url_free(&u); break; }
        if (strcmp(u.scheme, "http") && strcmp(u.scheme, "https")) { r = xcalloc(1, sizeof *r); r->error = xstrdup("unsupported scheme"); url_free(&u); break; }
        bool retry;
        r = http_once(method, &u, &req->headers, body, blen, !req->no_cookies, &retry);
        if (r->error && retry) { net_response_free(r); r = http_once(method, &u, &req->headers, body, blen, !req->no_cookies, &retry); }
        const char *loc = headers_get(&r->headers, "location");
        if (r->status >= 300 && r->status < 400 && r->status != 304 && loc && redirects-- > 0) {
            char *nu = url_join(url, loc);
            if (r->status == 303 || ((r->status == 301 || r->status == 302) && !strcmp(method, "POST"))) { free(method); method = xstrdup("GET"); body = NULL; blen = 0; }
            net_response_free(r); r = NULL; url_free(&u);
            if (!nu) { r = xcalloc(1, sizeof *r); r->error = xstrdup("bad redirect"); break; }
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

NetRequest *net_request_new(const char *method, const char *url) {
    NetRequest *r = xcalloc(1, sizeof *r);
    r->method = xstrdup(method ? method : "GET"); r->url = xstrdup(url);
    return r;
}
static void net_request_free(NetRequest *r) { free(r->method); free(r->url); headers_free(&r->headers); free(r->body); free(r); }

/* ---------- async worker pool ---------- */
typedef struct Job { NetRequest *req; NetResponse *resp; struct Job *next; } Job;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static Job *g_queue, *g_done, *g_done_tail;
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
        pthread_mutex_unlock(&g_mu);
        if (j->req->cancelled) { j->resp = xcalloc(1, sizeof *j->resp); j->resp->error = xstrdup("cancelled"); }
        else j->resp = net_fetch_sync(j->req);
        pthread_mutex_lock(&g_mu);
        if (g_done_tail) g_done_tail->next = j; else g_done = j;
        g_done_tail = j;
        pthread_mutex_unlock(&g_mu);
        if (net_wakeup) net_wakeup();
    }
}

void net_init(int threads) {
    signal(SIGPIPE, SIG_IGN);
    g_ssl_ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_PEER, NULL);
    const char *cands[] = { getenv("SSL_CERT_FILE"), "/opt/homebrew/etc/openssl@3/cert.pem", "/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/system/etc/security/cacerts" };
    bool loaded = false;
    for (size_t i = 0; i < ARRLEN(cands) && !loaded; i++) if (cands[i] && access(cands[i], R_OK) == 0) loaded = SSL_CTX_load_verify_locations(g_ssl_ctx, cands[i], NULL) == 1;
    if (!loaded) SSL_CTX_set_default_verify_paths(g_ssl_ctx);
    SSL_CTX_set_session_cache_mode(g_ssl_ctx, SSL_SESS_CACHE_CLIENT);
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
void net_cancel(uint64_t id) {
    pthread_mutex_lock(&g_mu);
    for (Job *j = g_queue; j; j = j->next) if (j->req->id == id) j->req->cancelled = true;
    pthread_mutex_unlock(&g_mu);
}
int net_pending(void) { pthread_mutex_lock(&g_mu); int n = g_pending; pthread_mutex_unlock(&g_mu); return n; }
int net_poll(void) {
    pthread_mutex_lock(&g_mu);
    Job *list = g_done; g_done = g_done_tail = NULL;
    pthread_mutex_unlock(&g_mu);
    int n = 0;
    while (list) {
        Job *j = list; list = j->next;
        pthread_mutex_lock(&g_mu); g_pending--; pthread_mutex_unlock(&g_mu);
        if (!j->req->cancelled && j->req->done) j->req->done(j->req, j->resp, j->req->ud);
        net_response_free(j->resp); net_request_free(j->req); free(j);
        n++;
    }
    return n;
}
