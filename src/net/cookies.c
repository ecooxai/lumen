/* Minimal RFC 6265 cookie jar (in-memory, thread-safe). */
#include "net.h"
#include <pthread.h>
#include <time.h>
#include <ctype.h>

typedef struct { char *name, *value, *domain, *path; bool host_only, secure, http_only; double expires; } Cookie;
static VEC(Cookie) g_jar;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static bool g_dirty;

static bool domain_match(const char *host, const char *dom) {
    size_t hl = strlen(host), dl = strlen(dom);
    if (hl == dl) return !strcmp(host, dom);
    return hl > dl && !strcmp(host + hl - dl, dom) && host[hl - dl - 1] == '.';
}
static bool path_match(const char *rp, const char *cp) {
    size_t n = strlen(cp);
    if (strncmp(rp, cp, n)) return false;
    return cp[n - 1] == '/' || rp[n] == 0 || rp[n] == '/';
}
static void cookie_free(Cookie *c) { free(c->name); free(c->value); free(c->domain); free(c->path); }

static double parse_http_date(const char *s) {
    struct tm tm = {0}; char mon[4] = {0}; int d, y, H, M, S;
    static const char *mons = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *p = strchr(s, ','); if (p) s = p + 1;
    while (*s == ' ') s++;
    if (sscanf(s, "%d%*[ -]%3s%*[ -]%d %d:%d:%d", &d, mon, &y, &H, &M, &S) != 6) return 0;
    const char *mp = strstr(mons, mon); if (!mp) return 0;
    if (y < 100) y += y < 70 ? 2000 : 1900;
    tm.tm_mday = d; tm.tm_mon = (int)(mp - mons) / 3; tm.tm_year = y - 1900; tm.tm_hour = H; tm.tm_min = M; tm.tm_sec = S;
    return (double)timegm(&tm);
}

static void set_cookie(const char *host, const char *rpath, const char *sc, bool from_http) {
    char *s = xstrdup(sc);
    char *semi = strchr(s, ';'); if (semi) *semi = 0;
    char *eq = strchr(s, '=');
    Cookie c = {0};
    if (eq) { *eq = 0; c.name = xstrdup(str_trim(s)); c.value = xstrdup(str_trim(eq + 1)); }
    else { c.name = xstrdup(""); c.value = xstrdup(str_trim(s)); }
    c.expires = -1;
    char *attrs = semi ? semi + 1 : NULL;
    while (attrs) {
        char *next = strchr(attrs, ';'); if (next) *next++ = 0;
        char *ae = strchr(attrs, '='); char *av = ae ? (*ae = 0, str_trim(ae + 1)) : (char *)"";
        char *an = str_trim(attrs);
        if (str_ieq(an, "domain") && av[0]) { if (av[0] == '.') av++; free(c.domain); c.domain = xstrdup(av); for (char *q = c.domain; *q; q++) *q = (char)tolower((unsigned char)*q); }
        else if (str_ieq(an, "path") && av[0] == '/') { free(c.path); c.path = xstrdup(av); }
        else if (str_ieq(an, "secure")) c.secure = true;
        else if (str_ieq(an, "httponly")) c.http_only = true;
        else if (str_ieq(an, "max-age")) c.expires = (double)time(NULL) + atof(av);
        else if (str_ieq(an, "expires") && c.expires < 0) { double t = parse_http_date(av); if (t) c.expires = t; }
        attrs = next;
    }
    free(s);
    if (c.domain) { if (!domain_match(host, c.domain)) { cookie_free(&c); return; } }
    else { c.domain = xstrdup(host); c.host_only = true; }
    if (!c.path) {
        const char *ls = strrchr(rpath, '/');
        c.path = (ls && ls != rpath) ? xstrndup(rpath, (size_t)(ls - rpath)) : xstrdup("/");
    }
    if (!from_http && c.http_only) { cookie_free(&c); return; }
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_jar.n; i++) {
        Cookie *o = &g_jar.v[i];
        if (!strcmp(o->name, c.name) && !strcmp(o->domain, c.domain) && !strcmp(o->path, c.path)) {
            if (!from_http && o->http_only) { pthread_mutex_unlock(&g_mu); cookie_free(&c); return; }
            cookie_free(o); g_jar.v[i] = g_jar.v[--g_jar.n]; break;
        }
    }
    if (c.expires < 0 || c.expires > (double)time(NULL)) vec_push(g_jar, c); else cookie_free(&c);
    g_dirty = true;
    pthread_mutex_unlock(&g_mu);
}

void cookies_set_from_header(const URL *u, const char *sc) { set_cookie(u->host, u->path ? u->path : "/", sc, true); }

char *cookies_get(const URL *u, bool for_http) {
    SB b; sb_init(&b);
    double now = (double)time(NULL);
    bool secure = !strcmp(u->scheme, "https") || !strcmp(u->scheme, "wss");
    pthread_mutex_lock(&g_mu);
    for (int i = 0; i < g_jar.n; i++) {
        Cookie *c = &g_jar.v[i];
        if (c->expires >= 0 && c->expires < now) continue;
        if (c->host_only ? strcmp(u->host, c->domain) : !domain_match(u->host, c->domain)) continue;
        if (!path_match(u->path ? u->path : "/", c->path)) continue;
        if (c->secure && !secure) continue;
        if (c->http_only && !for_http) continue;
        if (b.n) sb_puts(&b, "; ");
        if (c->name[0]) { sb_puts(&b, c->name); sb_putc(&b, '='); }
        sb_puts(&b, c->value);
    }
    pthread_mutex_unlock(&g_mu);
    if (!b.n) { sb_free(&b); return NULL; }
    return sb_take(&b);
}
void cookies_set_document(const char *url, const char *str) { URL u; if (!url_parse(url, &u)) return; set_cookie(u.host ? u.host : "", u.path ? u.path : "/", str, false); url_free(&u); }
char *cookies_get_document(const char *url) { URL u; if (!url_parse(url, &u) || !u.host) return xstrdup(""); char *r = cookies_get(&u, false); url_free(&u); return r ? r : xstrdup(""); }

/* Persistence: one tab-separated cookie per line (domain host_only path secure http_only expires name value). */
void cookies_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[16384]; double now = (double)time(NULL);
    pthread_mutex_lock(&g_mu);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *fld[8]; int n = 0;
        for (char *p = line; n < 8; n++) { fld[n] = p; char *t = strchr(p, '\t'); if (!t) { n++; break; } *t = 0; p = t + 1; }
        if (n != 8) continue;
        Cookie c = { xstrdup(fld[6]), xstrdup(fld[7]), xstrdup(fld[0]), xstrdup(fld[2]), fld[1][0] == '1', fld[3][0] == '1', fld[4][0] == '1', atof(fld[5]) };
        if (c.expires >= 0 && c.expires < now) { cookie_free(&c); continue; }
        vec_push(g_jar, c);
    }
    pthread_mutex_unlock(&g_mu);
    fclose(f);
}
bool cookies_save(const char *path) {
    pthread_mutex_lock(&g_mu);
    if (!g_dirty) { pthread_mutex_unlock(&g_mu); return true; }
    char tmp[4096]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { pthread_mutex_unlock(&g_mu); return false; }
    double now = (double)time(NULL);
    for (int i = 0; i < g_jar.n; i++) {
        Cookie *c = &g_jar.v[i];
        if ((c->expires >= 0 && c->expires < now) || strpbrk(c->name, "\t\n") || strpbrk(c->value, "\t\n")) continue;
        fprintf(f, "%s\t%d\t%s\t%d\t%d\t%.0f\t%s\t%s\n", c->domain, c->host_only, c->path, c->secure, c->http_only, c->expires, c->name, c->value);
    }
    bool ok = fclose(f) == 0 && rename(tmp, path) == 0;
    if (ok) g_dirty = false;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
