/* HSTS (RFC 6797): hosts that sent Strict-Transport-Security over https are only ever fetched over https. */
#include "net.h"
#include <pthread.h>
#include <time.h>
#include <ctype.h>

typedef struct { char *host; double expires; bool subs; } Hsts;
static VEC(Hsts) g_hsts;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static bool g_dirty;

static bool hsts_host_ok(const char *h) {
    return h && *h && !strchr(h, ':') && strspn(h, "0123456789.") != strlen(h) && strcmp(h, "localhost");
}

void hsts_note(const URL *u, const char *v) {
    if (!v || !u->host || strcmp(u->scheme, "https") || !hsts_host_ok(u->host)) return;
    double max_age = -1; bool subs = false;
    for (const char *p = v; *p; ) {
        while (*p == ' ' || *p == '\t' || *p == ';') p++;
        const char *e = p; while (*e && *e != ';') e++;
        if (!strncasecmp(p, "max-age", 7)) {
            const char *q = p + 7; while (*q == ' ') q++;
            if (*q == '=') { q++; while (*q == ' ' || *q == '"') q++; if (isdigit((unsigned char)*q)) max_age = atof(q); }
        } else if (!strncasecmp(p, "includesubdomains", 17) && (p + 17 == e || p[17] == ' ')) subs = true;
        p = e;
    }
    if (max_age < 0) return;
    pthread_mutex_lock(&g_mu);
    int i = 0;
    for (; i < g_hsts.n; i++) if (!strcmp(g_hsts.v[i].host, u->host)) break;
    if (max_age == 0) {
        if (i < g_hsts.n) { free(g_hsts.v[i].host); g_hsts.v[i] = g_hsts.v[--g_hsts.n]; g_dirty = true; }
    } else {
        double exp = (double)time(NULL) + max_age;
        if (i == g_hsts.n) { Hsts h = { xstrdup(u->host), exp, subs }; vec_push(g_hsts, h); g_dirty = true; }
        else if (g_hsts.v[i].subs != subs || exp - g_hsts.v[i].expires > 3600) { g_hsts.v[i].expires = exp; g_hsts.v[i].subs = subs; g_dirty = true; }
    }
    pthread_mutex_unlock(&g_mu);
}

char *hsts_upgrade(const char *url) {
    if (!url || strncasecmp(url, "http:", 5)) return NULL;
    URL u; if (!url_parse(url, &u)) return NULL;
    char *out = NULL;
    if (u.host && hsts_host_ok(u.host)) {
        double now = (double)time(NULL); bool hit = false; size_t hl = strlen(u.host);
        pthread_mutex_lock(&g_mu);
        for (int i = 0; i < g_hsts.n && !hit; i++) {
            const Hsts *h = &g_hsts.v[i]; size_t l = strlen(h->host);
            if (h->expires < now) continue;
            hit = !strcmp(u.host, h->host) || (h->subs && hl > l && u.host[hl - l - 1] == '.' && !strcmp(u.host + hl - l, h->host));
        }
        pthread_mutex_unlock(&g_mu);
        if (hit) {
            free(u.scheme); u.scheme = xstrdup("https");
            if (u.port == 80) u.port = 443;
            out = url_to_string(&u);
        }
    }
    url_free(&u);
    return out;
}

/* one host per line: host expires include_subdomains */
void hsts_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024]; double now = (double)time(NULL);
    pthread_mutex_lock(&g_mu);
    while (fgets(line, sizeof line, f)) {
        char host[512]; double exp; int subs;
        if (sscanf(line, "%511s %lf %d", host, &exp, &subs) != 3 || exp < now || !hsts_host_ok(host)) continue;
        Hsts h = { xstrdup(host), exp, subs != 0 }; vec_push(g_hsts, h);
    }
    pthread_mutex_unlock(&g_mu);
    fclose(f);
}
bool hsts_save(const char *path) {
    pthread_mutex_lock(&g_mu);
    if (!g_dirty) { pthread_mutex_unlock(&g_mu); return true; }
    char tmp[4096]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { pthread_mutex_unlock(&g_mu); return false; }
    double now = (double)time(NULL);
    for (int i = 0; i < g_hsts.n; i++) if (g_hsts.v[i].expires >= now) fprintf(f, "%s %.0f %d\n", g_hsts.v[i].host, g_hsts.v[i].expires, g_hsts.v[i].subs);
    bool ok = fclose(f) == 0 && rename(tmp, path) == 0;
    if (ok) g_dirty = false;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
