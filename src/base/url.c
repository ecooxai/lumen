#include "url.h"
#include <ctype.h>

int url_default_port(const char *s) {
    if (!strcmp(s, "http") || !strcmp(s, "ws")) return 80;
    if (!strcmp(s, "https") || !strcmp(s, "wss")) return 443;
    if (!strcmp(s, "ftp")) return 21;
    return 0;
}
static bool special(const char *s) { return url_default_port(s) || !strcmp(s, "file"); }

void url_free(URL *u) { free(u->scheme); free(u->host); free(u->path); free(u->query); free(u->fragment); free(u->userinfo); memset(u, 0, sizeof *u); }

static char *normalize_path(const char *p) {
    /* remove dot segments */
    VEC(char *) segs = {0};
    const char *s = p; if (*s == '/') s++;
    bool trailing = false;
    while (1) {
        const char *e = s; while (*e && *e != '/') e++;
        size_t n = (size_t)(e - s);
        trailing = false;
        if ((n == 1 && s[0] == '.') || (n == 3 && str_ieqn(s, "%2e", 3))) { trailing = true; }
        else if ((n == 2 && s[0] == '.' && s[1] == '.') || (n == 4 && (str_ieqn(s, ".%2e", 4) || str_ieqn(s, "%2e.", 4))) || (n == 6 && str_ieqn(s, "%2e%2e", 6))) { if (segs.n) free(vec_pop(segs)); trailing = true; }
        else vec_push(segs, xstrndup(s, n));
        if (!*e) break;
        s = e + 1;
    }
    SB b; sb_init(&b);
    for (int i = 0; i < segs.n; i++) { sb_putc(&b, '/'); sb_puts(&b, segs.v[i]); free(segs.v[i]); }
    if (trailing || !b.n) sb_putc(&b, '/');
    vec_free(segs);
    return sb_take(&b);
}

static char *clean(const char *s) {
    /* strip leading/trailing C0+space, remove tab/newline */
    while (*s && (unsigned char)*s <= ' ') s++;
    size_t n = strlen(s); while (n && (unsigned char)s[n - 1] <= ' ') n--;
    SB b; sb_init(&b);
    for (size_t i = 0; i < n; i++) { if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r') continue; sb_putc(&b, s[i]); }
    return sb_take(&b);
}

static bool parse_authority(const char *a, size_t n, URL *u) {
    const char *at = NULL;
    for (size_t i = 0; i < n; i++) if (a[i] == '@') at = a + i;
    if (at) { u->userinfo = xstrndup(a, (size_t)(at - a)); n -= (size_t)(at + 1 - a); a = at + 1; }
    const char *colon = NULL;
    if (n && a[0] == '[') { const char *rb = memchr(a, ']', n); if (!rb) return false; if (rb + 1 < a + n && rb[1] == ':') colon = rb + 1; }
    else for (size_t i = 0; i < n; i++) if (a[i] == ':') colon = a + i;
    size_t hn = colon ? (size_t)(colon - a) : n;
    u->host = xmalloc(hn + 1);
    for (size_t i = 0; i < hn; i++) u->host[i] = (char)tolower((unsigned char)a[i]);
    u->host[hn] = 0;
    u->port = url_default_port(u->scheme);
    if (colon && colon + 1 < a + n) {
        int p = 0; for (const char *c = colon + 1; c < a + n; c++) { if (!isdigit((unsigned char)*c)) return false; p = p * 10 + (*c - '0'); if (p > 65535) return false; }
        u->port = p;
    }
    return true;
}

static void split_rest(const char *s, URL *u, bool set_path) {
    const char *h = strchr(s, '#');
    if (h) { u->fragment = xstrdup(h + 1); }
    size_t n = h ? (size_t)(h - s) : strlen(s);
    const char *q = memchr(s, '?', n);
    if (q) { u->query = xstrndup(q + 1, n - (size_t)(q + 1 - s)); n = (size_t)(q - s); }
    if (set_path) {
        char *p = xstrndup(s, n);
        for (char *c = p; *c; c++) if (*c == '\\' && special(u->scheme)) *c = '/';
        if (u->opaque) u->path = p; else { u->path = normalize_path(p[0] ? p : "/"); free(p); }
    }
}

bool url_parse(const char *in, URL *u) {
    memset(u, 0, sizeof *u);
    char *s = clean(in);
    const char *c = s;
    if (!isalpha((unsigned char)*c)) { free(s); return false; }
    while (isalnum((unsigned char)*c) || *c == '+' || *c == '-' || *c == '.') c++;
    if (*c != ':') { free(s); return false; }
    u->scheme = xstrndup(s, (size_t)(c - s));
    for (char *p = u->scheme; *p; p++) *p = (char)tolower((unsigned char)*p);
    c++;
    if (special(u->scheme)) {
        if (!strcmp(u->scheme, "file")) { if ((c[0] == '/' || c[0] == '\\') && (c[1] == '/' || c[1] == '\\')) c += 2; }
        else while (*c == '/' || *c == '\\') c++;
        const char *e = c; while (*e && *e != '/' && *e != '\\' && *e != '?' && *e != '#') e++;
        if (!parse_authority(c, (size_t)(e - c), u)) { free(s); url_free(u); return false; }
        if (!u->host[0] && strcmp(u->scheme, "file")) { free(s); url_free(u); return false; }
        split_rest(e, u, true);
    } else if (c[0] == '/' && c[1] == '/') {
        c += 2; const char *e = c; while (*e && *e != '/' && *e != '?' && *e != '#') e++;
        parse_authority(c, (size_t)(e - c), u);
        split_rest(e, u, true);
    } else {
        u->opaque = true;
        split_rest(c, u, true);
    }
    free(s);
    return true;
}

static void dup_into(URL *o, const URL *b) {
    o->scheme = xstrdup(b->scheme); o->host = b->host ? xstrdup(b->host) : NULL; o->port = b->port;
    o->userinfo = b->userinfo ? xstrdup(b->userinfo) : NULL; o->opaque = b->opaque;
}

bool url_resolve(const URL *base, const char *rel_in, URL *o) {
    memset(o, 0, sizeof *o);
    if (url_parse(rel_in, o)) {
        /* "http:foo" relative with same special scheme */
        return true;
    }
    if (!base || base->opaque) return false;
    char *rel = clean(rel_in);
    const char *r = rel;
    if (special(base->scheme)) for (char *p = rel; *p && *p != '?' && *p != '#'; p++) if (*p == '\\') *p = '/';
    if (r[0] == '/' && r[1] == '/') {
        SB b; sb_init(&b); sb_puts(&b, base->scheme); sb_putc(&b, ':'); sb_puts(&b, r);
        char *t = sb_take(&b); bool ok = url_parse(t, o); free(t); free(rel); return ok;
    }
    dup_into(o, base);
    if (r[0] == '/') { split_rest(r, o, true); }
    else if (r[0] == '?') { o->path = xstrdup(base->path); split_rest(r, o, false); }
    else if (r[0] == '#') { o->path = xstrdup(base->path); o->query = base->query ? xstrdup(base->query) : NULL; o->fragment = xstrdup(r + 1); }
    else if (!r[0]) { o->path = xstrdup(base->path); o->query = base->query ? xstrdup(base->query) : NULL; }
    else {
        const char *slash = strrchr(base->path ? base->path : "/", '/');
        SB b; sb_init(&b);
        if (base->path && slash) sb_put(&b, base->path, (size_t)(slash - base->path) + 1); else sb_putc(&b, '/');
        sb_puts(&b, r);
        char *t = sb_take(&b); split_rest(t, o, true); free(t);
    }
    free(rel);
    return true;
}

char *url_origin(const URL *u) {
    SB b; sb_init(&b);
    if (u->opaque || !u->host) { sb_puts(&b, "null"); return sb_take(&b); }
    sb_printf(&b, "%s://%s", u->scheme, u->host);
    if (u->port && u->port != url_default_port(u->scheme)) sb_printf(&b, ":%d", u->port);
    return sb_take(&b);
}
char *url_path_query(const URL *u) {
    SB b; sb_init(&b);
    sb_puts(&b, u->path && u->path[0] ? u->path : "/");
    if (u->query) { sb_putc(&b, '?'); sb_puts(&b, u->query); }
    /* percent-encode spaces and non-ascii */
    SB o; sb_init(&o);
    for (size_t i = 0; i < b.n; i++) { unsigned char ch = (unsigned char)b.s[i]; if (ch <= 0x20 || ch >= 0x7f || ch == '"' || ch == '<' || ch == '>') sb_printf(&o, "%%%02X", ch); else sb_putc(&o, (char)ch); }
    sb_free(&b);
    return sb_take(&o);
}
char *url_to_string(const URL *u) {
    SB b; sb_init(&b);
    sb_puts(&b, u->scheme); sb_putc(&b, ':');
    if (!u->opaque) {
        sb_puts(&b, "//");
        if (u->userinfo && u->userinfo[0]) { sb_puts(&b, u->userinfo); sb_putc(&b, '@'); }
        if (u->host) sb_puts(&b, u->host);
        if (u->port && u->port != url_default_port(u->scheme)) sb_printf(&b, ":%d", u->port);
    }
    if (u->path) sb_puts(&b, u->path);
    if (u->query) { sb_putc(&b, '?'); sb_puts(&b, u->query); }
    if (u->fragment) { sb_putc(&b, '#'); sb_puts(&b, u->fragment); }
    return sb_take(&b);
}
char *url_join(const char *base, const char *rel) {
    URL b, o; bool hb = base && url_parse(base, &b);
    if (!url_resolve(hb ? &b : NULL, rel, &o)) { if (hb) url_free(&b); return NULL; }
    char *r = url_to_string(&o); url_free(&o); if (hb) url_free(&b); return r;
}
static int hexv(int c) { if (c >= '0' && c <= '9') return c - '0'; c = lc(c); if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; }
char *url_decode(const char *s, size_t n) {
    SB b; sb_init(&b);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && hexv((unsigned char)s[i + 1]) >= 0 && hexv((unsigned char)s[i + 2]) >= 0) { sb_putc(&b, (char)(hexv((unsigned char)s[i + 1]) * 16 + hexv((unsigned char)s[i + 2]))); i += 2; }
        else if (s[i] == '+') sb_putc(&b, ' ');
        else sb_putc(&b, s[i]);
    }
    return sb_take(&b);
}
char *url_encode_component(const char *s) {
    SB b; sb_init(&b);
    for (; *s; s++) { unsigned char c = (unsigned char)*s; if (isalnum(c) || strchr("-_.!~*'()", c)) sb_putc(&b, (char)c); else sb_printf(&b, "%%%02X", c); }
    return sb_take(&b);
}
