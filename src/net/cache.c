/* Private HTTP cache on disk: one file per URL, written atomically and evicted
 * least recently used first. Freshness follows RFC 9111 sections 4.2 and 4.3. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cache.h"
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CACHE_MAX_BODY (32u << 20)

static char *g_dir;
static size_t g_max;
static int64_t g_total = -1;   /* bytes on disk; -1 until the startup scan finishes */
static bool g_evicting;
static unsigned g_seq;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

bool http_cache_enabled(void) { return g_dir != NULL; }

/* Entries are named by one hash of the URL and verified by a second, so URLs
 * themselves (which can carry tokens) are never written to disk. */
static uint64_t url_hash(const char *url, uint64_t h) {
    for (const char *s = url; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h;
}
static uint64_t url_check(const char *url) { return url_hash(url, 0x84222325cbf29ce4ull) ^ 0x9e3779b97f4a7c15ull; }
static void entry_path(char *out, size_t n, const char *url) {
    snprintf(out, n, "%s/%016llx", g_dir, (unsigned long long)url_hash(url, 1469598103934665603ull));
}

typedef struct { char name[24]; time_t mtime; off_t size; } DirEnt;
/* Sizes every entry, removes abandoned temporary files and, when over the
 * limit, the least recently used entries down to 80% of it. */
static int64_t sweep(size_t limit) {
    DIR *d = opendir(g_dir);
    if (!d) return 0;
    VEC(DirEnt) v = {0};
    int64_t total = 0; time_t now = time(NULL);
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        char p[1024]; snprintf(p, sizeof p, "%s/%s", g_dir, e->d_name);
        struct stat st;
        if (stat(p, &st) || !S_ISREG(st.st_mode)) continue;
        if (strchr(e->d_name, '~')) { if (now - st.st_mtime > 600) unlink(p); continue; }
        if (strlen(e->d_name) >= sizeof v.v->name) continue;
        DirEnt de = { "", st.st_mtime, st.st_size }; strcpy(de.name, e->d_name);
        vec_push(v, de); total += st.st_size;
    }
    closedir(d);
    if (limit && total > (int64_t)limit) {
        for (int i = 1; i < v.n; i++) for (int j = i; j > 0 && v.v[j].mtime < v.v[j - 1].mtime; j--) { DirEnt t = v.v[j]; v.v[j] = v.v[j - 1]; v.v[j - 1] = t; }
        for (int i = 0; i < v.n && total > (int64_t)(limit / 10 * 8); i++) {
            char p[1024]; snprintf(p, sizeof p, "%s/%s", g_dir, v.v[i].name);
            if (!unlink(p)) total -= v.v[i].size;
        }
    }
    vec_free(v);
    return total;
}
static void *scan_thread(void *arg) {
    (void)arg;
    int64_t t = sweep(g_max);
    pthread_mutex_lock(&g_mu); g_total = t; pthread_mutex_unlock(&g_mu);
    return NULL;
}

void net_cache_init(const char *dir, size_t max_bytes) {
    if (g_dir || !dir) return;
    mkdir(dir, 0700);
    g_dir = xstrdup(dir); g_max = max_bytes;
    pthread_t th;
    if (!pthread_create(&th, NULL, scan_thread, NULL)) pthread_detach(th);
}

/* ---- header semantics ---- */
static double http_date(const char *s) {
    struct tm tm; memset(&tm, 0, sizeof tm);
    if (!s || !strptime(s, "%a, %d %b %Y %H:%M:%S", &tm)) return -1;
    return (double)timegm(&tm);
}
typedef struct { bool no_store, no_cache; double max_age; } CacheControl;
static CacheControl cache_control(const Headers *h) {
    CacheControl c = { false, false, -1 };
    for (int i = 0; i < h->n; i++) {
        if (str_ieq(h->v[i].name, "pragma") && strstr(h->v[i].value, "no-cache")) c.no_cache = true;
        if (!str_ieq(h->v[i].name, "cache-control")) continue;
        for (const char *p = h->v[i].value; *p;) {
            while (*p == ' ' || *p == ',') p++;
            size_t n = strcspn(p, ",= ");
            if (n == 8 && str_ieqn(p, "no-store", 8)) c.no_store = true;
            else if (n == 8 && str_ieqn(p, "no-cache", 8)) c.no_cache = true;
            else if (n == 7 && str_ieqn(p, "max-age", 7) && p[7] == '=') c.max_age = atof(p + 8 + (p[8] == '"'));
            p += n; p += strcspn(p, ",");
        }
    }
    return c;
}
/* Freshness lifetime in seconds and the age when received. */
static void freshness(const Headers *h, double now, double *lifetime, double *age) {
    CacheControl c = cache_control(h);
    double date = http_date(headers_get(h, "date"));
    if (date < 0 || date > now) date = now;
    const char *a = headers_get(h, "age");
    *age = fmax(a ? atof(a) : 0, now - date);
    double expires = http_date(headers_get(h, "expires")), modified = http_date(headers_get(h, "last-modified"));
    if (c.no_cache) *lifetime = 0;
    else if (c.max_age >= 0) *lifetime = c.max_age;
    else if (headers_get(h, "expires")) *lifetime = expires > date ? expires - date : 0;
    else if (modified > 0 && modified < date) *lifetime = fmin((date - modified) / 10, 7 * 86400.0);   /* heuristic, 4.2.2 */
    else *lifetime = 0;
}
static bool request_uncacheable(const Headers *req) {
    return headers_get(req, "authorization") || headers_get(req, "range") ||
        headers_get(req, "if-none-match") || headers_get(req, "if-modified-since") || cache_control(req).no_store;
}
static bool storable_header(const Header *h) {
    return !str_ieq(h->name, "set-cookie") && !str_ieq(h->name, "set-cookie2") && !strpbrk(h->name, "\t\r\n") && !strpbrk(h->value, "\r\n");
}

/* ---- entry file: text head, then the body ----
 * LUMENCACHE 1 / stored lifetime age / status / url check / vary lines / header lines / body length */
typedef struct {
    double stored, lifetime, age; int status;
    Headers vary;    /* request header values this response was selected by */
    NetResponse *r;
} Entry;

static bool write_entry(const char *url, const Entry *e) {
    char path[1024], tmp[1100]; entry_path(path, sizeof path, url);
    pthread_mutex_lock(&g_mu); unsigned seq = ++g_seq; pthread_mutex_unlock(&g_mu);
    snprintf(tmp, sizeof tmp, "%s~%u.%u", path, (unsigned)getpid(), seq);
    SB b; sb_init(&b);
    sb_printf(&b, "LUMENCACHE 1\n%.0f %.0f %.0f\n%d\n%016llx\n%d\n", e->stored, e->lifetime, e->age, e->status,
        (unsigned long long)url_check(url), e->vary.n);
    for (int i = 0; i < e->vary.n; i++) sb_printf(&b, "%s\t%s\n", e->vary.v[i].name, e->vary.v[i].value);
    int nh = 0; for (int i = 0; i < e->r->headers.n; i++) nh += storable_header(&e->r->headers.v[i]);
    sb_printf(&b, "%d\n", nh);
    for (int i = 0; i < e->r->headers.n; i++) if (storable_header(&e->r->headers.v[i])) sb_printf(&b, "%s\t%s\n", e->r->headers.v[i].name, e->r->headers.v[i].value);
    sb_printf(&b, "%zu\n", e->r->body_len);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(b.s, 1, b.n, f) == b.n && (!e->r->body_len || fwrite(e->r->body, 1, e->r->body_len, f) == e->r->body_len);
    if (f && fclose(f)) ok = false;
    int64_t size = (int64_t)b.n + (int64_t)e->r->body_len;
    sb_free(&b);
    struct stat old; int64_t replaced = stat(path, &old) ? 0 : old.st_size;
    if (!ok || rename(tmp, path)) { unlink(tmp); return false; }
    pthread_mutex_lock(&g_mu);
    bool evict = false;
    if (g_total >= 0) { g_total += size - replaced; evict = g_total > (int64_t)g_max && !g_evicting; if (evict) g_evicting = true; }
    pthread_mutex_unlock(&g_mu);
    if (evict) {
        int64_t t = sweep(g_max);
        pthread_mutex_lock(&g_mu); g_total = t; g_evicting = false; pthread_mutex_unlock(&g_mu);
    }
    return true;
}

static char *line(char **p, char *end) {
    char *s = *p, *nl = memchr(s, '\n', (size_t)(end - s));
    if (!nl) return NULL;
    *nl = 0; *p = nl + 1;
    return s;
}
static bool read_pair(char **p, char *end, Headers *h) {
    char *s = line(p, end), *tab = s ? strchr(s, '\t') : NULL;
    if (!tab) return false;
    *tab = 0; headers_add(h, s, tab + 1);
    return true;
}
static void entry_free(Entry *e) { headers_free(&e->vary); net_response_free(e->r); }
static bool read_entry(const char *url, Entry *e) {
    memset(e, 0, sizeof *e);
    char path[1024]; entry_path(path, sizeof path, url);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    struct stat st;
    if (fstat(fileno(f), &st) || st.st_size <= 0 || st.st_size > (off_t)CACHE_MAX_BODY + (1 << 20)) { fclose(f); return false; }
    size_t n = (size_t)st.st_size;
    char *buf = xmalloc(n + 1), *p = buf, *end = buf + n, *s;
    bool ok = fread(buf, 1, n, f) == n;
    fclose(f);
    buf[n] = 0;
    int nv = 0, nh = 0; size_t blen = 0;
    char check[24]; snprintf(check, sizeof check, "%016llx", (unsigned long long)url_check(url));
    ok = ok && (s = line(&p, end)) && !strcmp(s, "LUMENCACHE 1") &&
        (s = line(&p, end)) && sscanf(s, "%lf %lf %lf", &e->stored, &e->lifetime, &e->age) == 3 &&
        (s = line(&p, end)) && sscanf(s, "%d", &e->status) == 1 &&
        (s = line(&p, end)) && !strcmp(s, check) &&
        (s = line(&p, end)) && sscanf(s, "%d", &nv) == 1;
    for (int i = 0; ok && i < nv; i++) ok = read_pair(&p, end, &e->vary);
    if (ok) { e->r = xcalloc(1, sizeof *e->r); ok = (s = line(&p, end)) && sscanf(s, "%d", &nh) == 1; }
    for (int i = 0; ok && i < nh; i++) ok = read_pair(&p, end, &e->r->headers);
    ok = ok && (s = line(&p, end)) && sscanf(s, "%zu", &blen) == 1 && blen == (size_t)(end - p);
    if (ok) {
        e->r->status = e->status; e->r->status_text = xstrdup("OK"); e->r->url = xstrdup(url);
        e->r->body = xmalloc(blen + 1); memcpy(e->r->body, p, blen); e->r->body[blen] = 0; e->r->body_len = blen;
        e->r->t_start = e->r->t_end = now_ms();
    }
    free(buf);
    if (!ok) { entry_free(e); unlink(path); }
    return ok;
}

/* Vary records a hash of each selecting request header (it may be a Cookie), never the value. */
static void vary_value(char out[24], const Headers *req, const char *name) {
    const char *v = headers_get(req, name);
    snprintf(out, 24, "%016llx", (unsigned long long)url_check(v ? v : ""));
}
static bool vary_matches(const Headers *vary, const Headers *req) {
    for (int i = 0; i < vary->n; i++) {
        char v[24]; vary_value(v, req, vary->v[i].name);
        if (strcmp(v, vary->v[i].value)) return false;
    }
    return true;
}

NetResponse *http_cache_lookup(const char *url, const Headers *req, bool *fresh, Headers *cond) {
    *fresh = false;
    if (!g_dir || request_uncacheable(req)) return NULL;
    Entry e;
    if (!read_entry(url, &e)) return NULL;
    if (!vary_matches(&e.vary, req)) { entry_free(&e); return NULL; }
    double now = (double)time(NULL), age = e.age + fmax(0, now - e.stored);
    CacheControl rc = cache_control(req);
    *fresh = !rc.no_cache && rc.max_age != 0 && age < e.lifetime;
    if (!*fresh) {
        const char *etag = headers_get(&e.r->headers, "etag"), *modified = headers_get(&e.r->headers, "last-modified");
        if (!etag && !modified) { entry_free(&e); return NULL; }
        for (int i = 0; i < req->n; i++) headers_add(cond, req->v[i].name, req->v[i].value);
        if (etag) headers_set(cond, "If-None-Match", etag);
        if (modified) headers_set(cond, "If-Modified-Since", modified);
    }
    char path[1024]; entry_path(path, sizeof path, url);
    utimensat(AT_FDCWD, path, NULL, 0);   /* recently used */
    NetResponse *r = e.r; e.r = NULL;
    entry_free(&e);
    return r;
}

static void store(const char *url, const Headers *req, const NetResponse *r, double now) {
    if (cache_control(&r->headers).no_store) return;
    const char *vary = headers_get(&r->headers, "vary");
    if (vary && strchr(vary, '*')) return;
    Entry e = { .stored = now, .status = r->status, .r = (NetResponse *)r };
    freshness(&r->headers, now, &e.lifetime, &e.age);
    if (e.lifetime <= e.age && !headers_get(&r->headers, "etag") && !headers_get(&r->headers, "last-modified")) return;
    for (const char *p = vary; p && *p;) {
        while (*p == ' ' || *p == ',') p++;
        size_t n = strcspn(p, ", ");
        if (n) { char *name = xstrndup(p, n), v[24]; vary_value(v, req, name); headers_add(&e.vary, name, v); free(name); }
        p += n;
    }
    write_entry(url, &e);
    headers_free(&e.vary);
}

void http_cache_store(const char *url, const Headers *req, const NetResponse *r) {
    if (!g_dir || r->status != 200 || r->error || r->body_len > CACHE_MAX_BODY || request_uncacheable(req)) return;
    store(url, req, r, (double)time(NULL));
}

NetResponse *http_cache_revalidated(const char *url, const Headers *req, NetResponse *cached, const NetResponse *r304) {
    static const char *const updated[] = { "cache-control", "date", "expires", "etag", "last-modified", "age", "vary" };
    for (size_t i = 0; i < sizeof updated / sizeof *updated; i++) {
        const char *v = headers_get(&r304->headers, updated[i]);
        if (v) headers_set(&cached->headers, updated[i], v);
    }
    double now = (double)time(NULL), lifetime, age;
    freshness(&cached->headers, now, &lifetime, &age);
    if (lifetime > age) store(url, req, cached, now);   /* fresh again: skip the next round trip */
    else { char path[1024]; entry_path(path, sizeof path, url); utimensat(AT_FDCWD, path, NULL, 0); }
    cached->t_start = r304->t_start; cached->t_end = r304->t_end;
    return cached;
}

void http_cache_invalidate(const char *url) {
    if (!g_dir) return;
    char path[1024]; entry_path(path, sizeof path, url);
    unlink(path);
}
