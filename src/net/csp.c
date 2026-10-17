/* Content-Security-Policy: script-src / script-src-elem / default-src for <script> elements */
#include "net.h"
#include "../base/url.h"
#include <ctype.h>
#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *csp_append(char *csp, const char *policy) {
    if (!policy || !*policy) return csp;
    size_t a = csp ? strlen(csp) : 0, b = strlen(policy);
    char *r = realloc(csp, a + b + 2);
    if (!r) return csp;
    if (a) r[a++] = '\n';
    for (size_t i = 0; i < b; i++) r[a + i] = policy[i] == ',' || policy[i] == '\n' ? '\n' : policy[i];  /* "," separates policies */
    r[a + b] = 0;
    return r;
}

static bool tok_eq(const char *p, size_t n, const char *s) { return strlen(s) == n && !strncasecmp(p, s, n); }
static bool tok_pre(const char *p, size_t n, const char *s) { size_t l = strlen(s); return n > l && !strncasecmp(p, s, l); }

/* source list of the first directive called name in policy [p,e) */
static const char *find_dir(const char *p, const char *e, const char *name, const char **ve) {
    while (p < e) {
        const char *de = memchr(p, ';', (size_t)(e - p)); if (!de) de = e;
        const char *q = p; while (q < de && isspace((unsigned char)*q)) q++;
        const char *ne = q; while (ne < de && !isspace((unsigned char)*ne)) ne++;
        if (tok_eq(q, (size_t)(ne - q), name)) { *ve = de; return ne; }
        p = de + 1;
    }
    return NULL;
}

static bool web_scheme(const char *s) { return s && (!strcmp(s, "http") || !strcmp(s, "https") || !strcmp(s, "ws") || !strcmp(s, "wss")); }

static bool src_match(const char *t, size_t n, const URL *a, const char *doc_url) {
    if (tok_eq(t, n, "*")) return web_scheme(a->scheme);
    if (tok_eq(t, n, "'self'")) {
        URL d; if (!url_parse(doc_url, &d)) return false;
        bool ok = d.host && a->host && !strcmp(d.host, a->host) && d.port == a->port && d.scheme && a->scheme
                  && (!strcmp(d.scheme, a->scheme) || (!strcmp(d.scheme, "http") && !strcmp(a->scheme, "https")));
        if (!ok && d.host && a->host && !strcmp(d.host, a->host) && d.scheme && !strcmp(d.scheme, "http") && !strcmp(a->scheme, "https")
            && d.port == 80 && a->port == 443) ok = true;
        url_free(&d); return ok;
    }
    if (n && t[0] == '\'') return false;
    if (n > 1 && t[n - 1] == ':' && !memchr(t, '/', n)) {
        size_t sl = n - 1;
        return a->scheme && ((strlen(a->scheme) == sl && !strncasecmp(t, a->scheme, sl)) || (tok_eq(t, sl, "http") && !strcmp(a->scheme, "https"))
                             || (tok_eq(t, sl, "ws") && !strcmp(a->scheme, "wss")));
    }
    if (!a->host) return false;
    const char *s = t, *e = t + n, *ss = NULL;
    for (const char *q = s; q + 2 < e; q++) if (q[0] == ':' && q[1] == '/' && q[2] == '/') { ss = q; break; }
    char sch[16] = "";
    if (ss) { size_t l = (size_t)(ss - s); if (l >= sizeof sch) return false; for (size_t i = 0; i < l; i++) sch[i] = (char)tolower((unsigned char)s[i]); sch[l] = 0; s = ss + 3; }
    const char *he = s; while (he < e && *he != '/') he++;
    const char *co = NULL; for (const char *q = s; q < he; q++) if (*q == ':') co = q;
    const char *hend = co ? co : he;
    bool sch_ok = !*sch ? (web_scheme(a->scheme)) : (!strcmp(sch, a->scheme) || (!strcmp(sch, "http") && !strcmp(a->scheme, "https")) || (!strcmp(sch, "ws") && !strcmp(a->scheme, "wss")));
    size_t hl = (size_t)(hend - s), al = strlen(a->host);
    bool host_ok;
    if (hl == 1 && *s == '*') host_ok = true;
    else if (hl > 2 && s[0] == '*' && s[1] == '.') host_ok = al > hl - 1 && !strncasecmp(a->host + al - (hl - 1), s + 1, hl - 1);
    else host_ok = al == hl && !strncasecmp(a->host, s, hl);
    bool port_ok;
    if (co) port_ok = (he - co == 2 && co[1] == '*') || atoi(co + 1) == a->port;
    else port_ok = !a->has_port || a->port == url_default_port(a->scheme) ||
                   (!*sch && 0);
    if (!co && !strcmp(a->scheme, "https") && a->port == 443) port_ok = true;
    return sch_ok && host_ok && port_ok;
}

static bool hash_match(const char *t, size_t n, const char *body, size_t bn) {
    const EVP_MD *md = tok_pre(t, n, "'sha256-") ? EVP_sha256() : tok_pre(t, n, "'sha384-") ? EVP_sha384() : tok_pre(t, n, "'sha512-") ? EVP_sha512() : NULL;
    if (!md || !body || t[n - 1] != '\'') return false;
    unsigned char dg[EVP_MAX_MD_SIZE]; unsigned dl = 0;
    if (!EVP_Digest(body, bn, dg, &dl, md, NULL)) return false;
    unsigned char b64[EVP_MAX_MD_SIZE * 2]; int bl = EVP_EncodeBlock(b64, dg, (int)dl);
    const char *v = t + 8; size_t vl = n - 9;
    if ((size_t)bl != vl) return false;
    for (size_t i = 0; i < vl; i++) {
        char c = v[i] == '-' ? '+' : v[i] == '_' ? '/' : v[i];   /* base64url tolerated */
        if (c != (char)b64[i]) return false;
    }
    return true;
}

static bool policy_allows(const char *p, const char *e, const char *doc_url, const char *src_url, const URL *su,
                          const char *nonce, const char *body, size_t bn, bool parser, int kind) {
    /* kind: 0 <script>, 1 inline event handler attribute, 2 eval/new Function, 3 WebAssembly compile */
    const char *ve, *v = kind == 0 ? find_dir(p, e, "script-src-elem", &ve) : kind == 1 ? find_dir(p, e, "script-src-attr", &ve) : NULL;
    if (!v) v = find_dir(p, e, "script-src", &ve);
    if (!v) v = find_dir(p, e, "default-src", &ve);
    if (!v) return true;
    bool ui = false, sd = false, nh = false, host = false, uh = false, ue = false, we = false, hm = false;
    for (const char *q = v; q < ve;) {
        while (q < ve && isspace((unsigned char)*q)) q++;
        const char *te = q; while (te < ve && !isspace((unsigned char)*te)) te++;
        size_t n = (size_t)(te - q);
        if (!n) break;
        if (tok_eq(q, n, "'unsafe-inline'")) ui = true;
        else if (tok_eq(q, n, "'strict-dynamic'")) sd = true;
        else if (tok_eq(q, n, "'unsafe-hashes'")) uh = true;
        else if (tok_eq(q, n, "'unsafe-eval'")) ue = true;
        else if (tok_eq(q, n, "'wasm-unsafe-eval'")) we = true;
        else if (tok_pre(q, n, "'nonce-") && q[n - 1] == '\'') {
            nh = true;
            if (!kind && nonce && *nonce && strlen(nonce) == n - 8 && !strncmp(nonce, q + 7, n - 8)) return true;
        } else if (tok_pre(q, n, "'sha256-") || tok_pre(q, n, "'sha384-") || tok_pre(q, n, "'sha512-")) {
            nh = true;
            if (!src_url && body && hash_match(q, n, body, bn)) { if (!kind) return true; hm = true; }
        } else if (!kind && src_url && su && src_match(q, n, su, doc_url)) host = true;
        q = te;
    }
    if (kind == 2) return ue;
    if (kind == 3) return ue || we;
    if (kind == 1) return (hm && uh) || (ui && !nh && !sd);
    if (!src_url) return ui && !nh && !sd;
    if (sd) return !parser;
    return host;
}

bool csp_allows_script(const char *csp, const char *doc_url, const char *src_url, const char *nonce,
                       const char *body, size_t n, bool parser_inserted) {
    if (!csp || !*csp) return true;
    URL su; bool have = src_url && url_parse(src_url, &su);
    bool ok = true;
    for (const char *p = csp; ok && *p;) {
        const char *e = strchr(p, '\n'); if (!e) e = p + strlen(p);
        ok = policy_allows(p, e, doc_url ? doc_url : "", src_url, have ? &su : NULL, nonce, body, n, parser_inserted, 0);
        p = *e ? e + 1 : e;
    }
    if (have) url_free(&su);
    return ok;
}

static bool csp_all(const char *csp, const char *body, size_t n, int kind) {
    bool ok = true;
    for (const char *p = csp ? csp : ""; ok && *p;) {
        const char *e = strchr(p, '\n'); if (!e) e = p + strlen(p);
        ok = policy_allows(p, e, "", NULL, NULL, NULL, body, n, false, kind);
        p = *e ? e + 1 : e;
    }
    return ok;
}
bool csp_allows_attr(const char *csp, const char *body, size_t n) { return csp_all(csp, body, n, 1); }
bool csp_allows_eval(const char *csp, bool wasm) { return csp_all(csp, NULL, 0, wasm ? 3 : 2); }

bool net_corp_blocked(const char *initiator, const char *resp_url, const char *corp, bool require_corp) {
    URL a, b;
    if (!initiator || !resp_url || !url_parse(initiator, &a)) return false;
    if (!url_parse(resp_url, &b)) { url_free(&a); return false; }
    bool web = a.scheme && (!strcmp(a.scheme, "http") || !strcmp(a.scheme, "https"));
    char *oa = url_origin(&a), *ob = url_origin(&b);
    bool same_origin = oa && ob && !strcmp(oa, ob), a_secure = a.scheme && !strcmp(a.scheme, "https"), b_secure = b.scheme && !strcmp(b.scheme, "https");
    free(oa); free(ob); url_free(&a); url_free(&b);
    if (!web || same_origin) return false;
    while (corp && isspace((unsigned char)*corp)) corp++;
    size_t n = corp ? strlen(corp) : 0;
    while (n && isspace((unsigned char)corp[n - 1])) n--;
    if (n && tok_eq(corp, n, "same-origin")) return true;
    if (n && tok_eq(corp, n, "same-site")) return !net_url_same_site(initiator, resp_url) || (a_secure && !b_secure);
    if (n && tok_eq(corp, n, "cross-origin")) return false;
    return require_corp;
}
