/* Minimal RFC 6265 cookie jar (in-memory, thread-safe). */
#include "net.h"
#include <pthread.h>
#include <time.h>
#include <ctype.h>

enum { SS_UNSET, SS_NONE, SS_LAX, SS_STRICT };
typedef struct { char *name, *value, *domain, *path; bool host_only, secure, http_only; double expires; int same_site; } Cookie;
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
/* Approximate registrable domain: last two labels, or three under common two-level public suffixes. */
static const char *const k_suffix2[] = {
    "co.uk", "org.uk", "ac.uk", "gov.uk", "me.uk", "ltd.uk", "plc.uk", "net.uk", "sch.uk",
    "com.au", "net.au", "org.au", "edu.au", "gov.au", "co.nz", "org.nz", "net.nz", "co.jp", "ne.jp", "or.jp", "ac.jp", "go.jp",
    "co.kr", "or.kr", "com.br", "net.br", "org.br", "com.cn", "net.cn", "org.cn", "com.vn", "net.vn", "org.vn", "edu.vn", "gov.vn",
    "co.in", "net.in", "org.in", "com.mx", "com.tr", "com.tw", "com.hk", "com.sg", "com.my", "co.id", "co.th", "com.ar", "com.co",
    "co.za", "com.ua", "com.pl", "com.ph", "com.pk", "com.sa", "com.eg", "co.il",
    "github.io", "gitlab.io", "vercel.app", "netlify.app", "pages.dev", "workers.dev", "herokuapp.com", "appspot.com",
    "blogspot.com", "web.app", "firebaseapp.com", "azurewebsites.net", "cloudfront.net", "amazonaws.com", "fly.dev", "onrender.com",
};
static bool host_is_ip(const char *h) { return strchr(h, ':') || (h[0] && strspn(h, "0123456789.") == strlen(h)); }
static const char *site_of(const char *h) {
    if (!h || host_is_ip(h)) return h ? h : "";
    size_t n = strlen(h); while (n && h[n - 1] == '.') n--;
    int dots = 0; const char *cut[3] = {0};
    for (size_t i = n; i-- > 0; ) if (h[i] == '.') { cut[dots] = h + i + 1; if (++dots == 3) break; }
    if (dots < 1) return h;
    if (dots >= 2) for (size_t k = 0; k < ARRLEN(k_suffix2); k++) {
        size_t sl = strlen(k_suffix2[k]);
        if ((size_t)(h + n - cut[1]) == sl && !strncmp(cut[1], k_suffix2[k], sl)) return dots >= 3 ? cut[2] : h;
    }
    return dots >= 2 ? cut[1] : h;
}
bool net_same_site(const char *a, const char *b) {
    if (!a || !b) return true;
    const char *x = site_of(a), *y = site_of(b);
    size_t xl = strlen(x), yl = strlen(y);
    while (xl && x[xl - 1] == '.') xl--;
    while (yl && y[yl - 1] == '.') yl--;
    return xl == yl && !strncasecmp(x, y, xl);
}
bool net_url_same_site(const char *ua, const char *ub) {
    if (!ua || !ub) return true;
    URL a, b; bool pa = url_parse(ua, &a), pb = url_parse(ub, &b), r = true;
    if (pa && pb && a.host && b.host) r = net_same_site(a.host, b.host);
    else if (pa && pb && (a.host != NULL) != (b.host != NULL)) r = false;   /* e.g. file:// or about: vs https */
    if (pa) url_free(&a); if (pb) url_free(&b);
    return r;
}

static void cookie_free(Cookie *c) { free(c->name); free(c->value); free(c->domain); free(c->path); }

/* Keep request-header order in the jar: longest path first, then oldest cookie.
   Insertion/deletion is uncommon compared with reads, so avoid sorting every request. */
static void cookie_insert(Cookie c) {
    int at = 0, n = g_jar.n; size_t len = strlen(c.path);
    while (at < n && strlen(g_jar.v[at].path) >= len) at++;
    vec_push(g_jar, c);
    memmove(g_jar.v + at + 1, g_jar.v + at, (size_t)(n - at) * sizeof c);
    g_jar.v[at] = c;
}
static void cookies_prune(double now) {
    int n = 0;
    for (int i = 0; i < g_jar.n; i++) {
        Cookie *c = &g_jar.v[i];
        if (c->expires >= 0 && c->expires <= now) { cookie_free(c); g_dirty = true; }
        else g_jar.v[n++] = *c;
    }
    g_jar.n = n;
}

static bool parse_max_age(const char *s, double now, double *expires) {
    const char *p = s;
    if (*p == '-') p++;
    if (!*p) return false;
    for (; *p; p++) if (*p < '0' || *p > '9') return false;
    /* strtoll saturates on overflow; unlike atof it cannot turn malformed data
       into Max-Age=0 and discard an otherwise valid session cookie. */
    long long age = strtoll(s, NULL, 10);
    *expires = age <= 0 ? 0 : now + (double)age;
    return true;
}

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

static void set_cookie(const char *host, const char *rpath, const char *sc, bool from_http, bool cross_site) {
    double now = (double)time(NULL);
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
        else if (str_ieq(an, "samesite")) c.same_site = str_ieq(av, "none") ? SS_NONE : str_ieq(av, "strict") ? SS_STRICT : str_ieq(av, "lax") ? SS_LAX : SS_UNSET;
        else if (str_ieq(an, "max-age")) parse_max_age(av, now, &c.expires);
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
    /* Chrome rules: SameSite=None needs Secure; a cross-site context may only set SameSite=None cookies;
       __Secure-/__Host- prefixes need Secure (and __Host- a host-only, Path=/ cookie). */
    if (c.same_site == SS_NONE && !c.secure) { cookie_free(&c); return; }
    if (cross_site && c.same_site != SS_NONE) { cookie_free(&c); return; }
    if (!strncmp(c.name, "__Secure-", 9) && !c.secure) { cookie_free(&c); return; }
    if (!strncmp(c.name, "__Host-", 7) && (!c.secure || !c.host_only || strcmp(c.path, "/"))) { cookie_free(&c); return; }
    pthread_mutex_lock(&g_mu);
    cookies_prune(now);
    for (int i = 0; i < g_jar.n; i++) {
        Cookie *o = &g_jar.v[i];
        if (!strcmp(o->name, c.name) && !strcmp(o->domain, c.domain) && !strcmp(o->path, c.path)) {
            if (!from_http && o->http_only) { pthread_mutex_unlock(&g_mu); cookie_free(&c); return; }
            cookie_free(o);
            if (c.expires < 0 || c.expires > now) *o = c;   /* retain creation order on replacement */
            else {
                cookie_free(&c);
                memmove(o, o + 1, (size_t)(g_jar.n - i - 1) * sizeof *o);
                g_jar.n--;
            }
            g_dirty = true;
            pthread_mutex_unlock(&g_mu);
            return;
        }
    }
    if (c.expires < 0 || c.expires > now) cookie_insert(c); else cookie_free(&c);
    g_dirty = true;
    pthread_mutex_unlock(&g_mu);
}

void cookies_set_from_header(const URL *u, const char *sc, bool cross_site) { set_cookie(u->host, u->path ? u->path : "/", sc, true, cross_site); }

char *cookies_get(const URL *u, bool for_http, bool cross_site) {
    SB b; sb_init(&b);
    double now = (double)time(NULL);
    bool secure = !strcmp(u->scheme, "https") || !strcmp(u->scheme, "wss");
    pthread_mutex_lock(&g_mu);
    cookies_prune(now);
    for (int i = 0; i < g_jar.n; i++) {
        Cookie *c = &g_jar.v[i];
        if (c->host_only ? strcmp(u->host, c->domain) : !domain_match(u->host, c->domain)) continue;
        if (!path_match(u->path ? u->path : "/", c->path)) continue;
        if (c->secure && !secure) continue;
        if (c->http_only && !for_http) continue;
        if (cross_site && c->same_site != SS_NONE) continue;   /* unset = Lax by default, like Chrome */
        if (b.n) sb_puts(&b, "; ");
        if (c->name[0]) { sb_puts(&b, c->name); sb_putc(&b, '='); }
        sb_puts(&b, c->value);
    }
    pthread_mutex_unlock(&g_mu);
    if (!b.n) { sb_free(&b); return NULL; }
    return sb_take(&b);
}
void cookies_set_document(const char *url, const char *top, const char *str) {
    URL u; if (!url_parse(url, &u)) return;
    set_cookie(u.host ? u.host : "", u.path ? u.path : "/", str, false, top && !net_url_same_site(url, top));
    url_free(&u);
}
char *cookies_get_document(const char *url, const char *top) {
    URL u; if (!url_parse(url, &u) || !u.host) return xstrdup("");
    char *r = cookies_get(&u, false, top && !net_url_same_site(url, top));
    url_free(&u);
    return r ? r : xstrdup("");
}

/* Persistence: one tab-separated cookie per line (domain host_only path secure http_only expires name value [same_site]). */
void cookies_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[16384]; double now = (double)time(NULL);
    pthread_mutex_lock(&g_mu);
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *fld[9]; int n = 0;
        for (char *p = line; n < 9; n++) { fld[n] = p; char *t = strchr(p, '\t'); if (!t) { n++; break; } *t = 0; p = t + 1; }
        if (n != 8 && n != 9) continue;
        Cookie c = { xstrdup(fld[6]), xstrdup(fld[7]), xstrdup(fld[0]), xstrdup(fld[2]), fld[1][0] == '1', fld[3][0] == '1', fld[4][0] == '1', atof(fld[5]), n == 9 ? atoi(fld[8]) : SS_UNSET };
        if (!c.path[0] || c.path[0] != '/' || (c.expires >= 0 && c.expires <= now)) { cookie_free(&c); continue; }
        cookie_insert(c);
    }
    pthread_mutex_unlock(&g_mu);
    fclose(f);
}
bool cookies_save(const char *path) {
    pthread_mutex_lock(&g_mu);
    double now = (double)time(NULL);
    cookies_prune(now);
    if (!g_dirty) { pthread_mutex_unlock(&g_mu); return true; }
    char tmp[4096]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { pthread_mutex_unlock(&g_mu); return false; }
    for (int i = 0; i < g_jar.n; i++) {
        Cookie *c = &g_jar.v[i];
        if (strpbrk(c->name, "\t\n") || strpbrk(c->value, "\t\n")) continue;
        fprintf(f, "%s\t%d\t%s\t%d\t%d\t%.0f\t%s\t%s\t%d\n", c->domain, c->host_only, c->path, c->secure, c->http_only, c->expires, c->name, c->value, c->same_site);
    }
    bool ok = fclose(f) == 0 && rename(tmp, path) == 0;
    if (ok) g_dirty = false;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
