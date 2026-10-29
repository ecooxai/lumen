/* Download manager. A download is a set of byte ranges ("segments") of one output file; each
 * worker thread owns one segment at a time and fetches it with a Range request through the
 * shared HTTP stack, so cookies, proxy and user agent are the same as for the page. A worker
 * that finishes early takes half of the largest range still left. */
#include "download.h"
#include "../base/url.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DL_MAX_CONNS 6
#define DL_SPLIT_MIN (2 << 20)       /* parallel only above this size; also the smallest range given away */
#define DL_MAX_SEGS 64
#define DL_TRIES 8
#define DL_KEEP 200                  /* finished entries remembered */

typedef struct Seg { int64_t pos, end; bool owned; int tries; } Seg;   /* end -1: to the end of the body */
typedef struct Dl {
    uint64_t id;
    pthread_mutex_t mu;              /* guards everything below and serializes the writes */
    char *url, *referrer, *validator;
    int fd, state, workers;
    bool stop, cancel, resumable, single;   /* single: one connection reading the whole body */
    int64_t total, done;
    Seg seg[DL_MAX_SEGS]; int nseg;
    double win_t, bps; int64_t win_bytes; double notified;
    char name[256], mime[96], where[1024], error[96];
    struct Dl *next;
} Dl;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;   /* the list, ids, versions and the history file */
static Dl *g_list;
static uint64_t g_next_id = 1, g_ver, g_sver;
static char *g_path;
static DlStore g_store;
void (*dl_notify)(uint64_t id, bool started);

static void bump(Dl *d, bool state, bool started) {
    pthread_mutex_lock(&g_mu); g_ver++; if (state) g_sver++; pthread_mutex_unlock(&g_mu);
    if (dl_notify) dl_notify(d->id, started);
}
uint64_t dl_version(void) { pthread_mutex_lock(&g_mu); uint64_t v = g_ver; pthread_mutex_unlock(&g_mu); return v; }
uint64_t dl_state_version(void) { pthread_mutex_lock(&g_mu); uint64_t v = g_sver; pthread_mutex_unlock(&g_mu); return v; }
void dl_set_store(const DlStore *store) { if (store) g_store = *store; }

static Dl *find(uint64_t id) { for (Dl *d = g_list; d; d = d->next) if (d->id == id) return d; return NULL; }

/* ---------- history ---------- */
static void esc_field(FILE *f, const char *s) { for (; s && *s; s++) fputc(*s == '\t' || *s == '\n' || *s == '\r' ? ' ' : *s, f); }
/* lock order: g_mu before any d->mu */
static void save_list_locked(void) {
    if (!g_path) return;
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    int n = 0;
    for (Dl *d = g_list; d && n < DL_KEEP; d = d->next) {
        pthread_mutex_lock(&d->mu);
        if (d->state == DL_DONE) {
            n++;
            fprintf(f, "%lld\t", (long long)d->total);
            esc_field(f, d->name); fputc('\t', f); esc_field(f, d->mime); fputc('\t', f); esc_field(f, d->where); fputc('\t', f); esc_field(f, d->url); fputc('\n', f);
        }
        pthread_mutex_unlock(&d->mu);
    }
    if (fclose(f) == 0) rename(tmp, g_path); else unlink(tmp);
}
/* Referer under the default strict-origin-when-cross-origin policy: the whole page URL for the
   same origin, only its origin for another one, nothing from https to http. */
static char *referrer_for(const char *url, const char *ref) {
    URL u, r;
    if (!url || !ref || !*ref || !url_parse(url, &u)) return NULL;
    if (!url_parse(ref, &r)) { url_free(&u); return NULL; }
    char *out = NULL;
    bool http_ref = !strcmp(r.scheme, "http") || !strcmp(r.scheme, "https");
    if (http_ref && !(!strcmp(r.scheme, "https") && strcmp(u.scheme, "https"))) {
        if (!strcmp(u.scheme, r.scheme) && !strcmp(u.host, r.host) && u.port == r.port) {
            out = xstrdup(ref); char *h = strchr(out, '#'); if (h) *h = 0;
        } else {
            size_t n = strlen(r.scheme) + strlen(r.host) + 16; out = xmalloc(n);
            if (r.port != url_default_port(r.scheme)) snprintf(out, n, "%s://%s:%d/", r.scheme, r.host, r.port);
            else snprintf(out, n, "%s://%s/", r.scheme, r.host);
        }
    }
    url_free(&u); url_free(&r);
    return out;
}
static Dl *dl_new(const char *url, const char *referrer) {
    Dl *d = xcalloc(1, sizeof *d);
    pthread_mutex_init(&d->mu, NULL);
    d->url = xstrdup(url ? url : ""); d->referrer = referrer_for(url, referrer);
    d->fd = -1; d->total = -1; d->state = DL_RUNNING;
    return d;
}
static void list_add(Dl *d) {   /* newest first */
    pthread_mutex_lock(&g_mu); d->id = g_next_id++; d->next = g_list; g_list = d; g_sver++; g_ver++; pthread_mutex_unlock(&g_mu);
}
void dl_init(const char *path) {
    pthread_mutex_lock(&g_mu);
    free(g_path); g_path = path ? xstrdup(path) : NULL;
    pthread_mutex_unlock(&g_mu);
    size_t n; char *s = path ? read_file(path, &n) : NULL;
    if (!s) return;
    Dl *rev = NULL;   /* the file is newest first; build oldest first so list_add restores the order */
    for (char *line = s, *nl; line && *line; line = nl) {
        nl = strchr(line, '\n'); if (nl) *nl++ = 0;
        char *f[5] = {0}; int k = 0;
        for (char *p = line; k < 5; ) { f[k++] = p; char *t = strchr(p, '\t'); if (!t) break; *t = 0; p = t + 1; }
        if (k < 5) continue;
        Dl *d = dl_new(f[4], NULL);
        d->state = DL_DONE; d->total = d->done = strtoll(f[0], NULL, 10);
        snprintf(d->name, sizeof d->name, "%s", f[1]); snprintf(d->mime, sizeof d->mime, "%s", f[2]); snprintf(d->where, sizeof d->where, "%s", f[3]);
        d->next = rev; rev = d;
    }
    while (rev) { Dl *d = rev; rev = rev->next; list_add(d); }
    free(s);
}

/* ---------- names and types ---------- */
static const char *const EXT[][2] = {
    { "application/pdf", ".pdf" }, { "application/zip", ".zip" }, { "application/vnd.android.package-archive", ".apk" },
    { "application/x-7z-compressed", ".7z" }, { "application/gzip", ".gz" }, { "application/x-tar", ".tar" },
    { "application/json", ".json" }, { "text/plain", ".txt" }, { "text/csv", ".csv" }, { "text/html", ".html" },
    { "image/png", ".png" }, { "image/jpeg", ".jpg" }, { "image/gif", ".gif" }, { "image/webp", ".webp" }, { "image/svg+xml", ".svg" },
    { "audio/mpeg", ".mp3" }, { "audio/ogg", ".ogg" }, { "audio/wav", ".wav" }, { "audio/webm", ".weba" }, { "audio/mp4", ".m4a" }, { "audio/aac", ".aac" },
    { "video/mp4", ".mp4" }, { "video/webm", ".webm" }, { "application/msword", ".doc" },
    { "application/vnd.openxmlformats-officedocument.wordprocessingml.document", ".docx" },
    { "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", ".xlsx" },
    { "application/vnd.openxmlformats-officedocument.presentationml.presentation", ".pptx" },
};
static void base_type(const char *ct, char *out, size_t n) {
    size_t k = 0;
    if (ct) for (; *ct == ' '; ct++) {}
    for (; ct && *ct && *ct != ';' && *ct != ' ' && k + 1 < n; ct++) out[k++] = (char)lc(*ct);
    out[k] = 0;
}
int dl_classify(int status, const char *content_type, const char *disposition) {
    if (status < 200 || status >= 300 || status == 204 || status == 205) return 0;
    if (disposition) { const char *p = disposition; while (*p == ' ') p++; if (str_istarts(p, "attachment")) return 1; }
    char t[96]; base_type(content_type, t, sizeof t);
    if (!*t) return 0;
    if (str_starts(t, "audio/") || str_starts(t, "video/")) return 2;
    if (str_starts(t, "image/")) return !strcmp(t, "image/svg+xml") ? 0 : 2;
    if (str_starts(t, "text/") || strstr(t, "+xml") || strstr(t, "+json") || !strcmp(t, "application/xhtml+xml") || !strcmp(t, "application/xml") ||
        !strcmp(t, "application/json") || !strcmp(t, "application/javascript") || !strcmp(t, "application/x-javascript") || !strcmp(t, "multipart/x-mixed-replace"))
        return 0;
    return 1;
}
static void hexdec(char *s) {   /* %XX in place */
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && s[1] && s[2]) { char h[3] = { s[1], s[2], 0 }; char *e; long v = strtol(h, &e, 16); if (!*e) { *o++ = (char)v; s += 2; continue; } }
        *o++ = *s;
    }
    *o = 0;
}
static void sanitize(char *s) {
    size_t n = strlen(s), k = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c == 127 || strchr("/\\:*?\"<>|", c)) c = '_';
        s[k++] = (char)c;
    }
    s[k] = 0;
    while (k && (s[k - 1] == '.' || s[k - 1] == ' ')) s[--k] = 0;
    size_t lead = 0; while (s[lead] == '.' || s[lead] == ' ') lead++;
    if (lead) memmove(s, s + lead, strlen(s + lead) + 1);
    if (strlen(s) > 180) {   /* keep the extension; cut on a UTF-8 boundary */
        char *dot = strrchr(s, '.'); size_t el = dot && strlen(dot) <= 10 ? strlen(dot) : 0;
        size_t cut = 180 - el; while (cut && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
        memmove(s + cut, s + strlen(s) - el, el + 1);
    }
}
static bool disp_param(const char *disp, const char *key, char *out, size_t n) {   /* key=value or key="value" */
    size_t kl = strlen(key);
    for (const char *p = disp; p && *p; ) {
        while (*p == ';' || *p == ' ') p++;
        if (str_ieqn(p, key, kl) && p[kl] == '=') {
            p += kl + 1; size_t k = 0;
            if (*p == '"') { for (p++; *p && *p != '"' && k + 1 < n; p++) { if (*p == '\\' && p[1]) p++; out[k++] = *p; } }
            else for (; *p && *p != ';' && k + 1 < n; p++) out[k++] = *p;
            out[k] = 0; str_trim(out);
            return k > 0;
        }
        p = strchr(p, ';');
    }
    return false;
}
void dl_file_name(const char *disposition, const char *hint, const char *url, const char *mime, char *out, size_t n) {
    char buf[512] = "";
    if (disposition && disp_param(disposition, "filename*", buf, sizeof buf)) {   /* RFC 5987: charset'lang'value */
        char *q = strchr(buf, '\''); char *q2 = q ? strchr(q + 1, '\'') : NULL;
        if (q2) memmove(buf, q2 + 1, strlen(q2 + 1) + 1);
        hexdec(buf);
    } else if (disposition && disp_param(disposition, "filename", buf, sizeof buf)) {
        if (strchr(buf, '%')) hexdec(buf);
    }
    if (!*buf && hint && *hint) snprintf(buf, sizeof buf, "%s", hint);
    if (!*buf && url) {
        const char *s = strstr(url, "://"); s = s ? strchr(s + 3, '/') : NULL;
        if (s) {
            size_t e = strcspn(s, "?#");
            const char *b = s + e; while (b > s && b[-1] != '/') b--;
            snprintf(buf, sizeof buf, "%.*s", (int)(s + e - b), b);
            hexdec(buf);
        }
    }
    char *slash = strrchr(buf, '/'); if (slash) memmove(buf, slash + 1, strlen(slash + 1) + 1);
    slash = strrchr(buf, '\\'); if (slash) memmove(buf, slash + 1, strlen(slash + 1) + 1);
    sanitize(buf);
    if (!*buf) snprintf(buf, sizeof buf, "download");
    char t[96]; base_type(mime, t, sizeof t);
    if (!strrchr(buf, '.') && *t) for (size_t i = 0; i < sizeof EXT / sizeof EXT[0]; i++)
        if (!strcmp(t, EXT[i][0])) { size_t l = strlen(buf); snprintf(buf + l, sizeof buf - l, "%s", EXT[i][1]); break; }
    snprintf(out, n, "%s", buf);
}

/* ---------- transfer ---------- */
typedef struct Job { Dl *d; int seg; bool first; NetResponse *head_status; int status; bool ranged_ok; } Job;

static bool write_all(int fd, const char *p, size_t n, int64_t off) {
    while (n) {
        ssize_t w = pwrite(fd, p, n, (off_t)off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w; n -= (size_t)w; off += w;
    }
    return true;
}
static void fail_locked(Dl *d, const char *msg) {
    if (!*d->error) snprintf(d->error, sizeof d->error, "%s", msg);
    d->stop = true;
}
static bool seg_sink(NetRequest *req, const char *data, size_t n, void *ud) {
    (void)req;
    Job *j = ud; Dl *d = j->d;
    pthread_mutex_lock(&d->mu);
    Seg *s = &d->seg[j->seg];
    bool more = !d->stop;
    if (more) {
        int64_t room = s->end < 0 ? (int64_t)n : s->end - s->pos;
        size_t k = room < (int64_t)n ? (size_t)(room > 0 ? room : 0) : n;
        if (k && !write_all(d->fd, data, k, s->pos)) { fail_locked(d, errno == ENOSPC ? "Not enough storage" : "Could not write the file"); more = false; }
        else {
            s->pos += (int64_t)k; d->done += (int64_t)k; s->tries = 0;
            double t = now_ms();
            d->win_bytes += (int64_t)k;
            if (!d->win_t) d->win_t = t;
            if (t - d->win_t >= 1000) { d->bps = d->bps ? d->bps * 0.5 + d->win_bytes * 1000.0 / (t - d->win_t) * 0.5 : d->win_bytes * 1000.0 / (t - d->win_t); d->win_t = t; d->win_bytes = 0; }
            if (k < n || (s->end >= 0 && s->pos >= s->end)) more = false;
        }
    }
    bool tell = now_ms() - d->notified > 250;
    if (tell) d->notified = now_ms();
    pthread_mutex_unlock(&d->mu);
    if (tell) bump(d, false, false);
    return more;
}
/* The first response decides the layout: one connection, or several ranges when the server allows it. */
static void plan_locked(Dl *d, const NetResponse *h) {
    const char *cl = headers_get(&h->headers, "content-length"), *enc = headers_get(&h->headers, "content-encoding");
    const char *ar = headers_get(&h->headers, "accept-ranges");
    bool identity = !enc || str_ieq(enc, "identity");
    d->total = cl && identity ? strtoll(cl, NULL, 10) : -1;
    d->resumable = d->total > 0 && h->status == 200 && ar && strstr(ar, "bytes");
    const char *et = headers_get(&h->headers, "etag"), *lm = headers_get(&h->headers, "last-modified");
    free(d->validator); d->validator = et && strncmp(et, "W/", 2) ? xstrdup(et) : lm ? xstrdup(lm) : NULL;
    d->nseg = 1; d->seg[0] = (Seg){ 0, d->total, true, 0 };
    d->single = !d->resumable;
    if (d->resumable && d->total >= 2 * DL_SPLIT_MIN) {
        int n = (int)LMIN(DL_MAX_CONNS, d->total / DL_SPLIT_MIN);
        int64_t part = d->total / n;
        d->seg[0].end = part;
        for (int i = 1; i < n; i++) d->seg[d->nseg++] = (Seg){ part * i, i == n - 1 ? d->total : part * (i + 1), false, 0 };
    }
}
static void *worker(void *arg);
static void spawn_locked(Dl *d) {   /* one thread per unowned unfinished range, up to the connection limit */
    for (int i = 0; i < d->nseg && d->workers < DL_MAX_CONNS; i++) {
        Seg *s = &d->seg[i];
        if (s->owned || (s->end >= 0 && s->pos >= s->end)) continue;
        Job *j = xcalloc(1, sizeof *j); j->d = d; j->seg = i;
        s->owned = true; d->workers++;
        pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &at, worker, j)) { s->owned = false; d->workers--; free(j); }
        pthread_attr_destroy(&at);
        if (d->single) break;
    }
}
static bool create_output_locked(Dl *d, const char *disposition, const char *hint, const char *mime) {
    char t[96]; base_type(mime, t, sizeof t);
    snprintf(d->mime, sizeof d->mime, "%s", *t ? t : "application/octet-stream");
    char name[256]; dl_file_name(disposition, hint, d->url, d->mime, name, sizeof name);
    snprintf(d->name, sizeof d->name, "%s", name);
    d->fd = g_store.create ? g_store.create(name, d->mime, d->where, sizeof d->where, d->name, sizeof d->name) : -1;
    if (d->fd < 0) { fail_locked(d, "Could not create the file"); return false; }
    return true;
}
/* head of a range or restart request */
static bool range_head(NetRequest *req, const NetResponse *h, void *ud) {
    (void)req;
    Job *j = ud; Dl *d = j->d;
    j->status = h->status;
    pthread_mutex_lock(&d->mu);
    Seg *s = &d->seg[j->seg];
    bool ok = false;
    if (h->status == 206) {
        const char *cr = headers_get(&h->headers, "content-range");
        long long a = -1;
        if (cr && sscanf(cr, "bytes %lld-", &a) == 1 && a == s->pos) ok = true;
    } else if (h->status == 200 && d->single && s->pos == 0) ok = true;   /* restart from the beginning */
    else if (h->status == 200 && j->seg == 0 && s->pos == 0 && !d->single) {
        /* the server ignored the range: fall back to this one connection for the whole file */
        d->single = true; d->resumable = false; d->nseg = 1; s->end = d->total; ok = true;
    }
    if (ok) j->ranged_ok = true;
    pthread_mutex_unlock(&d->mu);
    return ok;
}
/* head of the first request of dl_start */
static bool first_head(NetRequest *req, const NetResponse *h, void *ud) {
    (void)req;
    Job *j = ud; Dl *d = j->d;
    j->status = h->status;
    if (h->status < 200 || h->status >= 300) return false;
    pthread_mutex_lock(&d->mu);
    plan_locked(d, h);
    bool ok = create_output_locked(d, headers_get(&h->headers, "content-disposition"), d->name[0] ? d->name : NULL, headers_get(&h->headers, "content-type"));
    if (ok) { spawn_locked(d); j->ranged_ok = true; }
    pthread_mutex_unlock(&d->mu);
    bump(d, true, true);
    return ok;
}
static void save_list(void) { pthread_mutex_lock(&g_mu); save_list_locked(); pthread_mutex_unlock(&g_mu); }
/* the last worker left; true when the download completed and the list needs saving (after d->mu is released) */
static bool finish_locked(Dl *d) {
    if (d->cancel) {
        if (g_store.finish && d->fd >= 0) g_store.finish(d->where, d->fd, false);
        d->fd = -1; d->state = DL_CANCELLED; return false;
    }
    bool complete = d->nseg > 0;
    for (int i = 0; i < d->nseg; i++) if (d->seg[i].end < 0 ? true : d->seg[i].pos < d->seg[i].end) complete = false;
    if (d->single && d->total < 0 && !*d->error && !d->stop && d->seg[0].end < 0 && d->seg[0].tries < 0) complete = true;   /* unknown length: ended cleanly */
    if (complete) {
        if (d->total < 0) d->total = d->done;
        if (d->fd >= 0) { if (ftruncate(d->fd, (off_t)d->total)) {} if (g_store.finish) g_store.finish(d->where, d->fd, true); }
        d->fd = -1; d->state = DL_DONE; d->bps = 0;
        return true;
    }
    d->state = *d->error ? DL_FAILED : DL_PAUSED;
    return false;
}
/* the largest range left, split in two; returns the new index or -1 */
static int steal_locked(Dl *d) {
    int best = -1; int64_t left = 0;
    for (int i = 0; i < d->nseg; i++) {
        Seg *s = &d->seg[i];
        if (!s->owned || s->end < 0) continue;
        if (s->end - s->pos > left) { left = s->end - s->pos; best = i; }
    }
    if (best < 0 || left < 2 * DL_SPLIT_MIN || d->nseg >= DL_MAX_SEGS) return -1;
    Seg *s = &d->seg[best];
    int64_t mid = s->pos + left / 2;
    d->seg[d->nseg] = (Seg){ mid, s->end, true, 0 };
    s->end = mid;
    return d->nseg++;
}
static int next_seg_locked(Dl *d, int cur) {   /* after finishing cur: an unowned range, else half of a big one */
    if (cur >= 0) d->seg[cur].owned = false;
    if (d->stop || d->single) return -1;
    for (int i = 0; i < d->nseg; i++) { Seg *s = &d->seg[i]; if (!s->owned && s->end >= 0 && s->pos < s->end) { s->owned = true; return i; } }
    return steal_locked(d);
}
static NetRequest *range_request(Dl *d, Job *j) {
    NetRequest *r = net_request_new("GET", d->url);
    if (d->referrer) headers_set(&r->headers, "Referer", d->referrer);
    headers_set(&r->headers, "Accept-Encoding", "identity");
    pthread_mutex_lock(&d->mu);
    Seg *s = &d->seg[j->seg];
    if (!(d->single && s->pos == 0)) {
        char rg[64];
        if (s->end >= 0) snprintf(rg, sizeof rg, "bytes=%lld-%lld", (long long)s->pos, (long long)s->end - 1);
        else snprintf(rg, sizeof rg, "bytes=%lld-", (long long)s->pos);
        headers_set(&r->headers, "Range", rg);
        if (d->validator) headers_set(&r->headers, "If-Range", d->validator);
    }
    pthread_mutex_unlock(&d->mu);
    r->on_head = range_head; r->sink = seg_sink; r->ud = r->sink_ud = j;
    return r;
}
/* One fetch has returned for j's range: decide whether it is done, retry it or give up. */
static bool after_fetch(Job *j, NetResponse *resp) {
    Dl *d = j->d;
    pthread_mutex_lock(&d->mu);
    Seg *s = &d->seg[j->seg];
    bool body_ok = resp && !resp->error && resp->status >= 200 && resp->status < 300;
    if (d->single && d->total < 0 && body_ok && j->ranged_ok) { s->tries = -1; pthread_mutex_unlock(&d->mu); return false; }  /* unknown length done */
    bool done = s->end >= 0 && s->pos >= s->end;
    bool retry = false;
    if (!done && !d->stop) {
        if (j->ranged_ok || !resp || resp->status == 0) {   /* connection lost mid-way or before the head */
            if (!d->resumable && !d->single) fail_locked(d, "Connection lost");
            else if (++s->tries > DL_TRIES) fail_locked(d, "Connection lost");
            else {
                if (d->single && !d->resumable) {   /* no ranges: start over */
                    d->done -= s->pos; s->pos = 0;
                }
                retry = true;
            }
        } else if (resp->status == 416 || resp->status == 200) fail_locked(d, "The file changed on the server");
        else { char m[64]; snprintf(m, sizeof m, "Server error %d", resp->status); fail_locked(d, m); }
    }
    int tries = s->tries;
    pthread_mutex_unlock(&d->mu);
    if (retry) {   /* back off 1, 2, 4 ... 16 s, stopping early on pause or cancel */
        for (int ms = 0; ms < 1000 * (1 << LMIN(tries - 1, 4)); ms += 100) {
            pthread_mutex_lock(&d->mu); bool stop = d->stop; pthread_mutex_unlock(&d->mu);
            if (stop) return false;
            usleep(100000);
        }
    }
    return retry;
}
static void worker_exit(Job *j) {
    Dl *d = j->d;
    pthread_mutex_lock(&d->mu);
    if (j->seg >= 0 && j->seg < d->nseg) d->seg[j->seg].owned = false;
    bool last = --d->workers == 0, save = last && finish_locked(d);
    pthread_mutex_unlock(&d->mu);
    if (save) save_list();
    free(j);
    bump(d, last, false);
}
/* Runs ranges until none is left; the first job of dl_start begins with the plain request. */
static void run_job(Job *j) {
    Dl *d = j->d;
    for (;;) {
        NetRequest *req;
        if (j->first) {
            j->first = false;
            req = net_request_new("GET", d->url);
            if (d->referrer) headers_set(&req->headers, "Referer", d->referrer);
            req->on_head = first_head; req->sink = seg_sink; req->ud = req->sink_ud = j;
        } else req = range_request(d, j);
        j->ranged_ok = false; j->status = 0;
        NetResponse *r = net_fetch_sync(req);
        if (j->d->fd < 0 && !j->ranged_ok) {   /* the first request never produced a file */
            pthread_mutex_lock(&d->mu);
            char m[64];
            if (r && r->status) snprintf(m, sizeof m, "Server error %d", r->status); else snprintf(m, sizeof m, "Could not connect");
            fail_locked(d, m);
            pthread_mutex_unlock(&d->mu);
            net_response_free(r);
            break;
        }
        bool again = after_fetch(j, r);
        net_response_free(r);
        if (again) continue;
        pthread_mutex_lock(&d->mu);
        int next = next_seg_locked(d, j->seg);
        j->seg = next;
        pthread_mutex_unlock(&d->mu);
        if (next < 0) break;
    }
    worker_exit(j);
}
static void *worker(void *arg) { run_job(arg); return NULL; }

uint64_t dl_start(const char *url, const char *referrer, const char *name_hint) {
    if (!url || !(str_istarts(url, "http://") || str_istarts(url, "https://"))) return 0;
    Dl *d = dl_new(url, referrer);
    if (name_hint && *name_hint) { snprintf(d->name, sizeof d->name, "%s", name_hint); sanitize(d->name); }
    else dl_file_name(NULL, NULL, url, NULL, d->name, sizeof d->name);
    list_add(d);
    Job *j = xcalloc(1, sizeof *j); j->d = d; j->seg = 0; j->first = true;
    d->nseg = 1; d->seg[0] = (Seg){ 0, -1, true, 0 }; d->single = true; d->workers = 1;
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, worker, j)) { pthread_mutex_lock(&d->mu); d->workers = 0; fail_locked(d, "Could not start"); d->state = DL_FAILED; pthread_mutex_unlock(&d->mu); free(j); }
    pthread_attr_destroy(&at);
    bump(d, true, true);
    return d->id;
}
uint64_t dl_save_bytes(const char *name, const char *mime, const void *data, size_t n) {
    Dl *d = dl_new("", NULL);
    pthread_mutex_lock(&d->mu);
    bool ok = create_output_locked(d, NULL, name && *name ? name : NULL, mime);
    if (ok && n && !write_all(d->fd, data, n, 0)) { fail_locked(d, "Could not write the file"); ok = false; }
    d->total = d->done = (int64_t)n; d->nseg = 1; d->seg[0] = (Seg){ (int64_t)n, (int64_t)n, false, 0 };
    if (ok) { if (g_store.finish) g_store.finish(d->where, d->fd, true); d->fd = -1; d->state = DL_DONE; }
    else { if (d->fd >= 0 && g_store.finish) g_store.finish(d->where, d->fd, false); d->fd = -1; d->state = DL_FAILED; }
    pthread_mutex_unlock(&d->mu);
    list_add(d);
    if (ok) save_list();
    bump(d, true, true);
    return d->id;
}

/* ---------- navigations ---------- */
struct DlProbe { Job *job; Dl *d; int kind; char *referrer; void (*started)(uint64_t, void *); void *ud; };
static bool probe_head(NetRequest *req, const NetResponse *h, void *ud) {
    DlProbe *p = ud;
    p->kind = dl_classify(h->status, headers_get(&h->headers, "content-type"), headers_get(&h->headers, "content-disposition"));
    if (p->kind == 2) return true;   /* the viewer page loads the media itself; stop this body */
    if (p->kind != 1) return false;
    Dl *d = dl_new(h->url && *h->url ? h->url : req->url, p->referrer);
    if (strcmp(req->method, "GET")) d->resumable = false;
    pthread_mutex_lock(&d->mu);
    plan_locked(d, h);
    if (strcmp(req->method, "GET")) { d->resumable = false; d->single = true; d->nseg = 1; d->seg[0].end = d->total; }
    bool ok = create_output_locked(d, headers_get(&h->headers, "content-disposition"), NULL, headers_get(&h->headers, "content-type"));
    d->workers = 1;   /* this fetching thread is the first worker */
    if (ok) spawn_locked(d);
    pthread_mutex_unlock(&d->mu);
    list_add(d);
    Job *j = xcalloc(1, sizeof *j); j->d = d; j->seg = 0; j->ranged_ok = ok;
    p->job = j; p->d = d;
    if (p->started) p->started(d->id, p->ud);
    bump(d, true, true);
    return ok;
}
static bool probe_sink(NetRequest *req, const char *data, size_t n, void *ud) {
    DlProbe *p = ud;
    if (!p->job) return false;   /* media viewer: nothing to keep */
    return seg_sink(req, data, n, p->job);
}
DlProbe *dl_probe(NetRequest *req, const char *referrer, void (*started)(uint64_t id, void *ud), void *ud) {
    DlProbe *p = xcalloc(1, sizeof *p);
    p->referrer = referrer && *referrer ? xstrdup(referrer) : NULL; p->started = started; p->ud = ud;
    req->on_head = probe_head; req->sink = probe_sink; req->ud = p; req->sink_ud = p;
    return p;
}
int dl_probe_finish(DlProbe *p, NetResponse *r) {
    int kind = p->kind;
    if (kind == 2 && r) { free(r->error); r->error = NULL; r->body_len = 0; if (r->body) r->body[0] = 0; }
    if (kind == 1 && p->job) {
        Job *j = p->job;
        if (!j->ranged_ok) worker_exit(j);
        else if (after_fetch(j, r)) run_job(j);   /* retries continue on this thread */
        else {
            pthread_mutex_lock(&j->d->mu); int next = next_seg_locked(j->d, j->seg); j->seg = next; pthread_mutex_unlock(&j->d->mu);
            if (next >= 0) run_job(j); else worker_exit(j);
        }
    }
    free(p->referrer); free(p);
    return kind == 1 ? 1 : kind == 2 ? 2 : 0;
}

/* ---------- control ---------- */
void dl_pause(uint64_t id) {
    pthread_mutex_lock(&g_mu); Dl *d = find(id); pthread_mutex_unlock(&g_mu);
    if (!d) return;
    pthread_mutex_lock(&d->mu);
    if (d->state == DL_RUNNING) { d->stop = true; if (!d->workers) d->state = DL_PAUSED; }
    pthread_mutex_unlock(&d->mu);
    bump(d, true, false);
}
void dl_resume(uint64_t id) {
    pthread_mutex_lock(&g_mu); Dl *d = find(id); pthread_mutex_unlock(&g_mu);
    if (!d) return;
    bool save = false;
    pthread_mutex_lock(&d->mu);
    if ((d->state == DL_PAUSED || d->state == DL_FAILED) && !d->workers && *d->url) {
        if (d->fd < 0) {   /* never created: start again from the request */
            pthread_mutex_unlock(&d->mu);
            char hint[256]; snprintf(hint, sizeof hint, "%s", d->name);
            uint64_t nid = dl_start(d->url, d->referrer, hint);
            if (nid) dl_forget(id);
            return;
        }
        d->stop = false; d->error[0] = 0; d->state = DL_RUNNING; d->bps = 0; d->win_t = 0; d->win_bytes = 0;
        if (!d->resumable) { d->single = true; d->nseg = 1; d->done = 0; d->seg[0] = (Seg){ 0, d->total, false, 0 }; if (ftruncate(d->fd, 0)) {} }
        for (int i = 0; i < d->nseg; i++) d->seg[i].tries = 0;
        spawn_locked(d);
        if (!d->workers) save = finish_locked(d);
    }
    pthread_mutex_unlock(&d->mu);
    if (save) save_list();
    bump(d, true, false);
}
void dl_cancel(uint64_t id) {
    pthread_mutex_lock(&g_mu); Dl *d = find(id); pthread_mutex_unlock(&g_mu);
    if (!d) return;
    pthread_mutex_lock(&d->mu);
    if (d->state == DL_RUNNING || d->state == DL_PAUSED || d->state == DL_FAILED) {
        d->stop = d->cancel = true;
        if (!d->workers) (void)finish_locked(d);
    }
    pthread_mutex_unlock(&d->mu);
    bump(d, true, false);
}
void dl_forget(uint64_t id) {
    pthread_mutex_lock(&g_mu);
    for (Dl **pp = &g_list; *pp; pp = &(*pp)->next) {
        Dl *d = *pp;
        if (d->id != id) continue;
        pthread_mutex_lock(&d->mu);
        bool idle = !d->workers && d->state != DL_RUNNING && d->state != DL_PAUSED;
        pthread_mutex_unlock(&d->mu);
        if (!idle) break;
        *pp = d->next;
        bool was_done = d->state == DL_DONE;
        if (d->fd >= 0 && g_store.finish) g_store.finish(d->where, d->fd, false);
        free(d->url); free(d->referrer); free(d->validator); pthread_mutex_destroy(&d->mu); free(d);
        g_sver++; g_ver++;
        if (was_done) save_list_locked();
        break;
    }
    pthread_mutex_unlock(&g_mu);
    if (dl_notify) dl_notify(id, false);
}
void dl_open(uint64_t id) {
    DlInfo i;
    if (dl_get(id, &i) && i.state == DL_DONE && g_store.open && *i.where) g_store.open(i.where, i.mime);
}
static void info_of(Dl *d, DlInfo *o) {
    pthread_mutex_lock(&d->mu);
    o->id = d->id; o->state = d->state; o->total = d->total; o->done = d->done; o->bps = d->state == DL_RUNNING ? d->bps : 0;
    o->conns = d->state == DL_RUNNING ? d->workers : 0; o->resumable = d->resumable;
    if (d->state == DL_RUNNING && d->stop && !d->cancel) o->state = DL_PAUSED;
    snprintf(o->name, sizeof o->name, "%s", d->name); snprintf(o->mime, sizeof o->mime, "%s", d->mime);
    snprintf(o->where, sizeof o->where, "%s", d->where); snprintf(o->error, sizeof o->error, "%s", d->error);
    pthread_mutex_unlock(&d->mu);
}
int dl_list(DlInfo *out, int max) {
    int n = 0;
    pthread_mutex_lock(&g_mu);
    for (Dl *d = g_list; d && n < max; d = d->next) if (d->state != DL_CANCELLED) info_of(d, &out[n++]);
    pthread_mutex_unlock(&g_mu);
    return n;
}
bool dl_get(uint64_t id, DlInfo *out) {
    pthread_mutex_lock(&g_mu); Dl *d = find(id); if (d) info_of(d, out); pthread_mutex_unlock(&g_mu);
    return d != NULL;
}
int dl_active(void) {
    int n = 0;
    pthread_mutex_lock(&g_mu);
    for (Dl *d = g_list; d; d = d->next) { pthread_mutex_lock(&d->mu); n += d->state == DL_RUNNING && !d->stop; pthread_mutex_unlock(&d->mu); }
    pthread_mutex_unlock(&g_mu);
    return n;
}
