#include <pthread.h>
#include <unicode/uidna.h>
#include "url.h"
#include <ctype.h>
#include <strings.h>

int url_default_port(const char *s) {
    if (!strcmp(s, "http") || !strcmp(s, "ws")) return 80;
    if (!strcmp(s, "https") || !strcmp(s, "wss")) return 443;
    if (!strcmp(s, "ftp")) return 21;
    return 0;
}
static bool special(const char *s) { return url_default_port(s) || !strcmp(s, "file"); }

void url_free(URL *u) { free(u->scheme); free(u->host); free(u->path); free(u->query); free(u->fragment); free(u->userinfo); memset(u, 0, sizeof *u); }

static char *sdup(const SB *b) { return xstrndup(b->s ? b->s : "", b->n); }

typedef struct { char **v; int n, cap; } Segs;
static void segs_push(Segs *p, char *s) { if (p->n == p->cap) { p->cap = p->cap ? p->cap * 2 : 8; p->v = xrealloc(p->v, sizeof *p->v * (size_t)p->cap); } p->v[p->n++] = s; }
static void segs_free(Segs *p) { for (int i = 0; i < p->n; i++) free(p->v[i]); free(p->v); memset(p, 0, sizeof *p); }
static void segs_from(Segs *p, const char *path) {
    if (!path || !*path) return;
    const char *s = path + (*path == '/');
    for (;;) { const char *e = strchr(s, '/'); size_t n = e ? (size_t)(e - s) : strlen(s); segs_push(p, xstrndup(s, n)); if (!e) break; s = e + 1; }
}
static bool is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool drive_letter(const char *s, size_t n, bool normalized) { return n == 2 && is_alpha((unsigned char)s[0]) && (s[1] == ':' || (!normalized && s[1] == '|')); }
static bool starts_drive(const char *s, size_t n) { return n >= 2 && drive_letter(s, 2, false) && (n == 2 || strchr("/\\?#", s[2])); }
static void shorten(Segs *p, const char *scheme) {
    if (!strcmp(scheme, "file") && p->n == 1 && drive_letter(p->v[0], strlen(p->v[0]), true)) return;
    if (p->n) free(p->v[--p->n]);
}

enum { PE_C0, PE_FRAG, PE_QUERY, PE_SQUERY, PE_PATH, PE_USER };
static bool in_set(unsigned char c, int set) {
    if (c < 0x20 || c > 0x7e) return true;
    if (set == PE_C0) return false;
    if (set == PE_FRAG) return strchr(" \"<>`", c) != NULL;
    if (strchr(" \"#<>", c)) return true;
    if (set == PE_QUERY) return false;
    if (set == PE_SQUERY) return c == '\'';
    if (strchr("?^`{}", c)) return true;
    if (set == PE_PATH) return false;
    return strchr("/:;=@[\\]|", c) != NULL;
}
static void pe(SB *b, unsigned char c, int set) { if (in_set(c, set)) sb_printf(b, "%%%02X", c); else sb_putc(b, (char)c); }
static int hexv(int c) { if (c >= '0' && c <= '9') return c - '0'; c = lc(c); if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; }

static bool ipv6_parse(const char *s, size_t n, uint16_t a[8]) {
    memset(a, 0, 16);
    int pi = 0, comp = -1; size_t p = 0;
    if (p < n && s[p] == ':') { if (p + 1 >= n || s[p + 1] != ':') return false; p += 2; comp = ++pi; }
    while (p < n) {
        if (pi == 8) return false;
        if (s[p] == ':') { if (comp != -1) return false; p++; comp = ++pi; continue; }
        unsigned v = 0; int len = 0;
        while (len < 4 && p < n && hexv((unsigned char)s[p]) >= 0) { v = v * 16 + (unsigned)hexv((unsigned char)s[p]); p++; len++; }
        if (p < n && s[p] == '.') {
            if (!len) return false;
            p -= (size_t)len;
            if (pi > 6) return false;
            int seen = 0;
            while (p < n) {
                int piece = -1;
                if (seen > 0) { if (s[p] == '.' && seen < 4) p++; else return false; }
                if (p >= n || !isdigit((unsigned char)s[p])) return false;
                while (p < n && isdigit((unsigned char)s[p])) {
                    int d = s[p] - '0';
                    if (piece == -1) piece = d; else if (piece == 0) return false; else piece = piece * 10 + d;
                    if (piece > 255) return false;
                    p++;
                }
                a[pi] = (uint16_t)(a[pi] * 0x100 + piece);
                if (++seen == 2 || seen == 4) pi++;
            }
            if (seen != 4) return false;
            break;
        } else if (p < n && s[p] == ':') { if (++p >= n) return false; }
        else if (p < n) return false;
        a[pi++] = (uint16_t)v;
    }
    if (comp != -1) { int sw = pi - comp; pi = 7; while (pi != 0 && sw > 0) { uint16_t t = a[pi]; a[pi] = a[comp + sw - 1]; a[comp + sw - 1] = t; pi--; sw--; } }
    else if (pi != 8) return false;
    return true;
}
static void ipv6_ser(SB *b, const uint16_t a[8]) {
    int best = -1, bl = 1;
    for (int i = 0; i < 8;) { if (a[i]) { i++; continue; } int j = i; while (j < 8 && !a[j]) j++; if (j - i > bl) { best = i; bl = j - i; } i = j; }
    sb_putc(b, '[');
    for (int i = 0; i < 8; i++) {
        if (i == best) { sb_puts(b, i ? ":" : "::"); i += bl - 1; if (i == 7) {} continue; }
        sb_printf(b, "%x", a[i]); if (i < 7) sb_putc(b, ':');
    }
    sb_putc(b, ']');
}
static bool ipv4_num(const char *s, size_t n, uint64_t *out) {
    if (!n) return false;
    int R = 10;
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; n -= 2; R = 16; }
    else if (n >= 2 && s[0] == '0') { s++; n--; R = 8; }
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        int d = hexv((unsigned char)s[i]);
        if (d < 0 || d >= R || (R != 16 && !isdigit((unsigned char)s[i]))) return false;
        if (v < ((uint64_t)1 << 40)) v = v * (uint64_t)R + (uint64_t)d;
    }
    *out = v; return true;
}
static bool ends_in_number(const char *h) {
    size_t e = strlen(h);
    if (e && h[e - 1] == '.') { if (!memchr(h, '.', e - 1)) { if (e == 1) return false; } e--; }
    size_t st = e; while (st > 0 && h[st - 1] != '.') st--;
    if (st == e) return false;
    bool dig = true; for (size_t i = st; i < e; i++) if (!isdigit((unsigned char)h[i])) dig = false;
    if (dig) return true;
    uint64_t v; return e - st >= 2 && h[st] == '0' && (h[st + 1] == 'x' || h[st + 1] == 'X') && ipv4_num(h + st, e - st, &v);
}
static bool ipv4_parse(const char *h, SB *o) {
    const char *parts[8]; size_t lens[8]; int np = 0; const char *s = h;
    for (;;) { const char *d = strchr(s, '.'); if (np == 8) return false; parts[np] = s; lens[np++] = d ? (size_t)(d - s) : strlen(s); if (!d) break; s = d + 1; }
    if (np > 1 && !lens[np - 1]) np--;
    if (np > 4) return false;
    uint64_t nums[4];
    for (int i = 0; i < np; i++) if (!ipv4_num(parts[i], lens[i], &nums[i])) return false;
    for (int i = 0; i < np - 1; i++) if (nums[i] > 255) return false;
    if (nums[np - 1] >= ((uint64_t)1 << (8 * (5 - np)))) return false;
    uint64_t v = nums[np - 1];
    for (int i = 0; i < np - 1; i++) v += nums[i] << (8 * (3 - i));
    sb_printf(o, "%u.%u.%u.%u", (unsigned)(v >> 24) & 255, (unsigned)(v >> 16) & 255, (unsigned)(v >> 8) & 255, (unsigned)v & 255);
    return true;
}
static int adapt(uint32_t d, uint32_t np, bool first) {
    d = first ? d / 700 : d / 2; d += d / np; int k = 0;
    while (d > ((36 - 1) * 26) / 2) { d /= 35; k += 36; }
    return k + (int)((36 * d) / (d + 38));
}
static void puny(SB *o, const uint32_t *cp, int n) {
    uint32_t h = 0, b, nn = 128, delta = 0; int bias = 72;
    for (int i = 0; i < n; i++) if (cp[i] < 128) { sb_putc(o, (char)cp[i]); h++; }
    b = h; if (b) sb_putc(o, '-');
    while (h < (uint32_t)n) {
        uint32_t m = 0xFFFFFFFF; for (int i = 0; i < n; i++) if (cp[i] >= nn && cp[i] < m) m = cp[i];
        delta += (m - nn) * (h + 1); nn = m;
        for (int i = 0; i < n; i++) {
            if (cp[i] < nn) delta++;
            if (cp[i] == nn) {
                uint32_t q = delta;
                for (int k = 36;; k += 36) {
                    int t = k <= bias ? 1 : k >= bias + 26 ? 26 : k - bias;
                    if (q < (uint32_t)t) break;
                    uint32_t dg = (uint32_t)t + (q - (uint32_t)t) % (36 - (uint32_t)t);
                    sb_putc(o, (char)(dg < 26 ? 'a' + dg : '0' + dg - 26));
                    q = (q - (uint32_t)t) / (36 - (uint32_t)t);
                }
                sb_putc(o, (char)(q < 26 ? 'a' + q : '0' + q - 26));
                bias = adapt(delta, h + 1, h == b); delta = 0; h++;
            }
        }
        delta++; nn++;
    }
}
static int utf8_dec(const unsigned char *s, size_t n, size_t *i, uint32_t *cp) {
    unsigned c = s[*i]; int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!len || *i + (size_t)len > n) return 0;
    uint32_t v = len == 1 ? c : len == 2 ? (c & 31) : len == 3 ? (c & 15) : (c & 7);
    for (int k = 1; k < len; k++) { if ((s[*i + (size_t)k] & 0xC0) != 0x80) return 0; v = (v << 6) | (s[*i + (size_t)k] & 63); }
    *i += (size_t)len; *cp = v; return 1;
}
/* UTS #46 domain-to-ASCII as the URL standard requires (CheckBidi, CheckJoiners, nontransitional, beStrict=false) */
static UIDNA *g_idna; static pthread_once_t g_idna_once = PTHREAD_ONCE_INIT;
static void idna_init(void) {
    UErrorCode e = U_ZERO_ERROR;
    g_idna = uidna_openUTS46(UIDNA_CHECK_BIDI | UIDNA_CHECK_CONTEXTJ | UIDNA_NONTRANSITIONAL_TO_ASCII | UIDNA_NONTRANSITIONAL_TO_UNICODE, &e);
    if (U_FAILURE(e)) g_idna = NULL;
}
static bool idna_to_ascii(const char *s, size_t n, SB *o) {
    pthread_once(&g_idna_once, idna_init);
    if (!g_idna) return false;
    char buf[512], *big = NULL; UIDNAInfo info = UIDNA_INFO_INITIALIZER; UErrorCode e = U_ZERO_ERROR;
    int32_t len = uidna_nameToASCII_UTF8(g_idna, s, (int32_t)n, buf, (int32_t)sizeof buf, &info, &e);
    if (e == U_BUFFER_OVERFLOW_ERROR) {
        UIDNAInfo i2 = UIDNA_INFO_INITIALIZER; info = i2; e = U_ZERO_ERROR; big = xmalloc((size_t)len + 1);
        len = uidna_nameToASCII_UTF8(g_idna, s, (int32_t)n, big, len + 1, &info, &e);
    }
    const uint32_t lenient = UIDNA_ERROR_EMPTY_LABEL | UIDNA_ERROR_LABEL_TOO_LONG | UIDNA_ERROR_DOMAIN_NAME_TOO_LONG |
                             UIDNA_ERROR_LEADING_HYPHEN | UIDNA_ERROR_TRAILING_HYPHEN | UIDNA_ERROR_HYPHEN_3_4;
    bool ok = U_SUCCESS(e) && !(info.errors & ~lenient);
    if (ok) sb_put(o, big ? big : buf, (size_t)len);
    free(big); return ok;
}
static bool forbidden_host(unsigned char c) { return c == 0 || strchr("\t\n\r #/:<>?@[\\]^|", c); }
static char *host_parse(const char *s, size_t n, bool opaque) {
    SB o; sb_init(&o);
    if (n && s[0] == '[') {
        uint16_t a[8];
        if (s[n - 1] != ']' || !ipv6_parse(s + 1, n - 2, a)) goto fail;
        ipv6_ser(&o, a); return sb_take(&o);
    }
    if (opaque) {
        for (size_t i = 0; i < n; i++) { if (forbidden_host((unsigned char)s[i])) goto fail; pe(&o, (unsigned char)s[i], PE_C0); }
        return sb_take(&o);
    }
    {
        char *dec = url_decode(s, n); size_t dn = strlen(dec); bool simple = true;
        for (size_t i = 0; i < dn; i++) { unsigned char c = (unsigned char)dec[i]; if (c >= 0x80) simple = false; dec[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c); }
        if (simple && strstr(dec, "xn--")) simple = false;
        bool ok = simple ? (sb_put(&o, dec, dn), true) : idna_to_ascii(dec, dn, &o);
        free(dec);
        if (!ok || !o.n) goto fail;
        for (size_t k = 0; k < o.n; k++) { unsigned char c = (unsigned char)o.s[k]; if (forbidden_host(c) || c < 0x20 || c == '%' || c == 0x7f) goto fail; }
        char *h = sb_take(&o);
        if (ends_in_number(h)) { SB v; sb_init(&v); bool r = ipv4_parse(h, &v); free(h); if (!r) { sb_free(&v); return NULL; } return sb_take(&v); }
        return h;
    }
fail:
    sb_free(&o); return NULL;
}

enum { S_SCHEME_START, S_SCHEME, S_NO_SCHEME, S_SPECIAL_REL_OR_AUTH, S_PATH_OR_AUTH, S_RELATIVE, S_REL_SLASH, S_SPECIAL_AUTH_SLASHES,
       S_SPECIAL_AUTH_IGNORE, S_AUTHORITY, S_HOST, S_PORT, S_FILE, S_FILE_SLASH, S_FILE_HOST, S_PATH_START, S_PATH, S_OPAQUE, S_QUERY, S_FRAGMENT };

static bool parse(const char *raw, const URL *base, URL *u) {
    memset(u, 0, sizeof *u);
    size_t a = 0, z = strlen(raw);
    while (a < z && (unsigned char)raw[a] <= 0x20) a++;
    while (z > a && (unsigned char)raw[z - 1] <= 0x20) z--;
    char *in = xmalloc(z - a + 1); size_t n = 0;
    for (size_t i = a; i < z; i++) if (raw[i] != '\t' && raw[i] != '\n' && raw[i] != '\r') in[n++] = raw[i];
    in[n] = 0;
    SB buf, user, pass, opath, q, f; sb_init(&buf); sb_init(&user); sb_init(&pass); sb_init(&opath); sb_init(&q); sb_init(&f);
    char *scheme = NULL, *host = NULL; int port = -1; bool opq = false, hq = false, hf = false, at = false, pw = false, br = false, ok = false, sp = false;
    Segs path = { 0 };
    int st = S_SCHEME_START;
#define REM1 (p + 1 < (long)n ? in[p + 1] : -1)
#define COPY_AUTH() do { if (base->userinfo) { const char *c = strchr(base->userinfo, ':'); sb_put(&user, base->userinfo, c ? (size_t)(c - base->userinfo) : strlen(base->userinfo)); if (c) sb_puts(&pass, c + 1); } \
                         free(host); host = base->host ? xstrdup(base->host) : NULL; port = base->has_port ? base->port : -1; } while (0)
#define COPY_PATH() do { segs_free(&path); segs_from(&path, base->path); } while (0)
#define COPY_QUERY() do { sb_clear(&q); hq = base->query != NULL; if (hq) sb_puts(&q, base->query); } while (0)
    for (long p = 0; p <= (long)n; p++) {
        int c = p < (long)n ? (unsigned char)in[p] : -1;
        switch (st) {
        case S_SCHEME_START:
            if (c >= 0 && is_alpha(c)) { sb_putc(&buf, (char)lc(c)); st = S_SCHEME; }
            else { st = S_NO_SCHEME; p--; }
            break;
        case S_SCHEME:
            if (c >= 0 && (isalnum(c) || c == '+' || c == '-' || c == '.')) sb_putc(&buf, (char)lc(c));
            else if (c == ':') {
                scheme = sdup(&buf); sb_clear(&buf); sp = special(scheme);
                if (!strcmp(scheme, "file")) st = S_FILE;
                else if (sp && base && base->scheme && !strcmp(base->scheme, scheme)) st = S_SPECIAL_REL_OR_AUTH;
                else if (sp) st = S_SPECIAL_AUTH_SLASHES;
                else if (REM1 == '/') { st = S_PATH_OR_AUTH; p++; }
                else { opq = true; st = S_OPAQUE; }
            } else { sb_clear(&buf); st = S_NO_SCHEME; p = -1; }
            break;
        case S_NO_SCHEME:
            if (!base || (base->opaque && c != '#')) goto done;
            free(scheme); scheme = xstrdup(base->scheme); sp = special(scheme);
            if (base->opaque && c == '#') { opq = true; sb_puts(&opath, base->path ? base->path : ""); COPY_QUERY(); hf = true; st = S_FRAGMENT; }
            else if (strcmp(base->scheme, "file")) { st = S_RELATIVE; p--; }
            else { st = S_FILE; p--; }
            break;
        case S_SPECIAL_REL_OR_AUTH:
            if (c == '/' && REM1 == '/') { st = S_SPECIAL_AUTH_IGNORE; p++; } else { st = S_RELATIVE; p--; }
            break;
        case S_PATH_OR_AUTH:
            if (c == '/') st = S_AUTHORITY; else { st = S_PATH; p--; }
            break;
        case S_RELATIVE:
            if (c == '/' || (sp && c == '\\')) st = S_REL_SLASH;
            else {
                COPY_AUTH(); COPY_PATH(); COPY_QUERY();
                if (c == '?') { sb_clear(&q); hq = true; st = S_QUERY; }
                else if (c == '#') { hf = true; st = S_FRAGMENT; }
                else if (c != -1) { hq = false; sb_clear(&q); shorten(&path, scheme); st = S_PATH; p--; }
            }
            break;
        case S_REL_SLASH:
            if (sp && (c == '/' || c == '\\')) st = S_SPECIAL_AUTH_IGNORE;
            else if (c == '/') st = S_AUTHORITY;
            else { COPY_AUTH(); st = S_PATH; p--; }
            break;
        case S_SPECIAL_AUTH_SLASHES:
            if (c == '/' && REM1 == '/') p++;
            st = S_SPECIAL_AUTH_IGNORE; if (!(c == '/' && in[p] == '/')) p--;
            break;
        case S_SPECIAL_AUTH_IGNORE:
            if (c != '/' && c != '\\') { st = S_AUTHORITY; p--; }
            break;
        case S_AUTHORITY:
            if (c == '@') {
                if (at) { SB t; sb_init(&t); sb_puts(&t, "%40"); sb_put(&t, buf.s ? buf.s : "", buf.n); sb_free(&buf); buf = t; }
                at = true;
                for (size_t k = 0; k < buf.n; k++) {
                    unsigned char ch = (unsigned char)buf.s[k];
                    if (ch == ':' && !pw) { pw = true; continue; }
                    pe(pw ? &pass : &user, ch, PE_USER);
                }
                sb_clear(&buf);
            } else if (c == -1 || c == '/' || c == '?' || c == '#' || (sp && c == '\\')) {
                if (at && !buf.n) goto done;
                p -= (long)buf.n + 1; sb_clear(&buf); st = S_HOST;
            } else sb_putc(&buf, (char)c);
            break;
        case S_HOST:
            if (c == ':' && !br) {
                if (!buf.n) goto done;
                free(host); host = host_parse(buf.s, buf.n, !sp); if (!host) goto done;
                sb_clear(&buf); st = S_PORT;
            } else if (c == -1 || c == '/' || c == '?' || c == '#' || (sp && c == '\\')) {
                p--;
                if (sp && !buf.n) goto done;
                if (!buf.n && (user.n || pass.n)) goto done;
                free(host); host = host_parse(buf.s ? buf.s : "", buf.n, !sp); if (!host) goto done;
                sb_clear(&buf); st = S_PATH_START;
            } else { if (c == '[') br = true; if (c == ']') br = false; sb_putc(&buf, (char)c); }
            break;
        case S_PORT:
            if (c >= '0' && c <= '9') { sb_putc(&buf, (char)c); if (buf.n > 9) { size_t k = 0; while (k < buf.n - 1 && buf.s[k] == '0') k++; if (buf.n - k > 5) goto done; } }
            else if (c == -1 || c == '/' || c == '?' || c == '#' || (sp && c == '\\')) {
                if (buf.n) { long v = 0; for (size_t k = 0; k < buf.n; k++) { v = v * 10 + (buf.s[k] - '0'); if (v > 65535) goto done; } port = v == url_default_port(scheme) && sp ? -1 : (int)v; sb_clear(&buf); }
                st = S_PATH_START; p--;
            } else goto done;
            break;
        case S_FILE:
            free(scheme); scheme = xstrdup("file"); sp = true; free(host); host = xstrdup("");
            if (c == '/' || c == '\\') st = S_FILE_SLASH;
            else if (base && base->scheme && !strcmp(base->scheme, "file")) {
                free(host); host = base->host ? xstrdup(base->host) : NULL; COPY_PATH(); COPY_QUERY();
                if (c == '?') { sb_clear(&q); hq = true; st = S_QUERY; }
                else if (c == '#') { hf = true; st = S_FRAGMENT; }
                else if (c != -1) {
                    hq = false; sb_clear(&q);
                    if (!starts_drive(in + p, n - (size_t)p)) shorten(&path, scheme); else segs_free(&path);
                    st = S_PATH; p--;
                }
            } else { st = S_PATH; p--; }
            break;
        case S_FILE_SLASH:
            if (c == '/' || c == '\\') st = S_FILE_HOST;
            else {
                if (base && base->scheme && !strcmp(base->scheme, "file")) {
                    free(host); host = base->host ? xstrdup(base->host) : NULL;
                    if (!starts_drive(in + p, n - (size_t)p)) { Segs bp = { 0 }; segs_from(&bp, base->path); if (bp.n && drive_letter(bp.v[0], strlen(bp.v[0]), true)) segs_push(&path, xstrdup(bp.v[0])); segs_free(&bp); }
                }
                st = S_PATH; p--;
            }
            break;
        case S_FILE_HOST:
            if (c == -1 || c == '/' || c == '\\' || c == '?' || c == '#') {
                p--;
                if (drive_letter(buf.s ? buf.s : "", buf.n, false)) st = S_PATH;
                else if (!buf.n) { free(host); host = xstrdup(""); st = S_PATH_START; }
                else {
                    free(host); host = host_parse(buf.s, buf.n, false); if (!host) goto done;
                    if (!strcmp(host, "localhost")) { free(host); host = xstrdup(""); }
                    sb_clear(&buf); st = S_PATH_START;
                }
            } else sb_putc(&buf, (char)c);
            break;
        case S_PATH_START:
            if (sp) { st = S_PATH; if (c != '/' && c != '\\') p--; }
            else if (c == '?') { sb_clear(&q); hq = true; st = S_QUERY; }
            else if (c == '#') { hf = true; st = S_FRAGMENT; }
            else if (c != -1) { st = S_PATH; if (c != '/') p--; }
            break;
        case S_PATH:
            if (c == -1 || c == '/' || (sp && c == '\\') || c == '?' || c == '#') {
                char *seg = sdup(&buf); bool slash = c == '/' || (sp && c == '\\');
                bool dd = !strcmp(seg, "..") || !strcasecmp(seg, ".%2e") || !strcasecmp(seg, "%2e.") || !strcasecmp(seg, "%2e%2e");
                bool sd = !strcmp(seg, ".") || !strcasecmp(seg, "%2e");
                if (dd) { shorten(&path, scheme); if (!slash) segs_push(&path, xstrdup("")); free(seg); }
                else if (sd) { if (!slash) segs_push(&path, xstrdup("")); free(seg); }
                else { if (!strcmp(scheme, "file") && !path.n && drive_letter(seg, strlen(seg), false)) seg[1] = ':'; segs_push(&path, seg); }
                sb_clear(&buf);
                if (c == '?') { sb_clear(&q); hq = true; st = S_QUERY; }
                if (c == '#') { hf = true; st = S_FRAGMENT; }
            } else pe(&buf, (unsigned char)c, PE_PATH);
            break;
        case S_OPAQUE:
            if (c == '?') { sb_clear(&q); hq = true; st = S_QUERY; }
            else if (c == '#') { hf = true; st = S_FRAGMENT; }
            else if (c == ' ') { int nx = REM1; if (nx == '?' || nx == '#') sb_puts(&opath, "%20"); else sb_putc(&opath, ' '); }
            else if (c != -1) pe(&opath, (unsigned char)c, PE_C0);
            break;
        case S_QUERY:
            if (c == '#' || c == -1) {
                for (size_t k = 0; k < buf.n; k++) pe(&q, (unsigned char)buf.s[k], sp ? PE_SQUERY : PE_QUERY);
                sb_clear(&buf);
                if (c == '#') { hf = true; st = S_FRAGMENT; }
            } else sb_putc(&buf, (char)c);
            break;
        case S_FRAGMENT:
            if (c != -1) pe(&f, (unsigned char)c, PE_FRAG);
            break;
        }
    }
    ok = scheme != NULL;
done:
    if (ok) {
        u->scheme = scheme; scheme = NULL;
        u->host = host; host = NULL;
        u->has_port = port >= 0; u->port = port >= 0 ? port : url_default_port(u->scheme);
        if (user.n || pass.n) { SB ui; sb_init(&ui); sb_put(&ui, user.s ? user.s : "", user.n); if (pass.n) { sb_putc(&ui, ':'); sb_put(&ui, pass.s, pass.n); } u->userinfo = sb_take(&ui); }
        u->opaque = opq;
        if (opq) u->path = sdup(&opath);
        else { SB pp; sb_init(&pp); for (int i = 0; i < path.n; i++) { sb_putc(&pp, '/'); sb_puts(&pp, path.v[i]); } u->path = sdup(&pp); sb_free(&pp); }
        if (hq) u->query = sdup(&q);
        if (hf) u->fragment = sdup(&f);
    }
    free(scheme); free(host); free(in); segs_free(&path);
    sb_free(&buf); sb_free(&user); sb_free(&pass); sb_free(&opath); sb_free(&q); sb_free(&f);
    return ok;
#undef REM1
#undef COPY_AUTH
#undef COPY_PATH
#undef COPY_QUERY
}
bool url_parse(const char *in, URL *u) { return parse(in, NULL, u); }
bool url_resolve(const URL *base, const char *rel, URL *o) { return parse(rel, base, o); }
char *url_origin(const URL *u) {
    SB b; sb_init(&b);
    if (u->opaque || !u->host) { sb_puts(&b, "null"); return sb_take(&b); }
    sb_printf(&b, "%s://%s", u->scheme, u->host);
    if (u->has_port) sb_printf(&b, ":%d", u->port);
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
    if (u->host) {
        sb_puts(&b, "//");
        if (u->userinfo && u->userinfo[0]) { sb_puts(&b, u->userinfo); sb_putc(&b, '@'); }
        sb_puts(&b, u->host);
        if (u->has_port) sb_printf(&b, ":%d", u->port);
    } else if (!u->opaque && u->path && u->path[0] == '/' && u->path[1] == '/') sb_puts(&b, "/.");
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
char *url_decode(const char *s, size_t n) {
    SB b; sb_init(&b);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && hexv((unsigned char)s[i + 1]) >= 0 && hexv((unsigned char)s[i + 2]) >= 0) { sb_putc(&b, (char)(hexv((unsigned char)s[i + 1]) * 16 + hexv((unsigned char)s[i + 2]))); i += 2; }
        else sb_putc(&b, s[i]);
    }
    return sb_take(&b);
}
char *url_encode_component(const char *s) {
    SB b; sb_init(&b);
    for (; *s; s++) { unsigned char c = (unsigned char)*s; if (isalnum(c) || strchr("-_.!~*'()", c)) sb_putc(&b, (char)c); else sb_printf(&b, "%%%02X", c); }
    return sb_take(&b);
}
