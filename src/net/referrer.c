/* Referrer-Policy: the Referer value a document may send to a target URL */
#include "net.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { RP_NONE, RP_NWD, RP_SAME, RP_ORIGIN, RP_STRICT_ORIGIN, RP_OWCO, RP_SOWCO, RP_UNSAFE };
static const char *const k_rp[] = { "no-referrer", "no-referrer-when-downgrade", "same-origin", "origin",
                                    "strict-origin", "origin-when-cross-origin", "strict-origin-when-cross-origin", "unsafe-url" };

/* last recognised token of a comma-separated policy list; default strict-origin-when-cross-origin */
static int rp_parse(const char *p) {
    int r = RP_SOWCO;
    while (p && *p) {
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        const char *e = p; while (*e && *e != ',') e++;
        const char *t = e; while (t > p && isspace((unsigned char)t[-1])) t--;
        for (int i = 0; i < (int)(sizeof k_rp / sizeof *k_rp); i++)
            if ((size_t)(t - p) == strlen(k_rp[i]) && !strncasecmp(p, k_rp[i], (size_t)(t - p))) r = i;
        p = e;
    }
    return r;
}

static bool http_url(const char *u) { return u && (!strncasecmp(u, "http://", 7) || !strncasecmp(u, "https://", 8)); }

/* "scheme://host[:port]" lowercased, default port and userinfo dropped */
static char *origin_str(const char *u) {
    const char *s = strstr(u, "://"); if (!s) return NULL;
    size_t sl = (size_t)(s - u); s += 3;
    const char *e = s + strcspn(s, "/?#");
    const char *at = memchr(s, '@', (size_t)(e - s)); if (at) s = at + 1;
    size_t hl = (size_t)(e - s);
    const char *dp = sl == 5 ? ":443" : ":80";
    if (hl > 4 && hl >= strlen(dp) && !strncmp(e - strlen(dp), dp, strlen(dp))) hl -= strlen(dp);
    char *o = malloc(sl + 3 + hl + 1); if (!o) return NULL;
    for (size_t i = 0; i < sl; i++) o[i] = (char)tolower((unsigned char)u[i]);
    memcpy(o + sl, "://", 3);
    for (size_t i = 0; i < hl; i++) o[sl + 3 + i] = (char)tolower((unsigned char)s[i]);
    o[sl + 3 + hl] = 0;
    return o;
}

/* full referrer: fragment and userinfo stripped */
static char *full_str(const char *u) {
    char *o = origin_str(u); if (!o) return NULL;
    const char *s = strstr(u, "://") + 3, *path = s + strcspn(s, "/?#");
    size_t pl = strcspn(path, "#");
    char *r = malloc(strlen(o) + pl + 2); if (!r) { free(o); return NULL; }
    strcpy(r, o); free(o);
    if (!pl || *path != '/') strcat(r, "/");
    strncat(r, path, pl);
    return r;
}

char *net_referrer(const char *doc_url, const char *target, const char *policy) {
    if (!http_url(doc_url) || !http_url(target)) return NULL;
    int p = rp_parse(policy);
    if (p == RP_NONE) return NULL;
    char *a = origin_str(doc_url), *b = origin_str(target);
    bool same = a && b && !strcmp(a, b);
    bool downgrade = !strncasecmp(doc_url, "https:", 6) && strncasecmp(target, "https:", 6);
    free(b);
    bool full = false, origin = false;
    switch (p) {
    case RP_NWD: full = !downgrade; break;
    case RP_SAME: full = same; break;
    case RP_ORIGIN: origin = true; break;
    case RP_STRICT_ORIGIN: origin = !downgrade; break;
    case RP_OWCO: full = same; origin = !same; break;
    case RP_SOWCO: full = same; origin = !same && !downgrade; break;
    case RP_UNSAFE: full = true; break;
    }
    if (full) { free(a); return full_str(doc_url); }
    if (origin && a) { char *r = malloc(strlen(a) + 2); if (r) { strcpy(r, a); strcat(r, "/"); } free(a); return r; }
    free(a);
    return NULL;
}
