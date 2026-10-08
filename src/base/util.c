#include "util.h"
#include <stdarg.h>
#include <pthread.h>
#include <time.h>

int g_verbose = 0;

void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) { fprintf(stderr, "OOM\n"); abort(); } return p; }
void *xcalloc(size_t n, size_t sz) { void *p = calloc(n ? n : 1, sz ? sz : 1); if (!p) { fprintf(stderr, "OOM\n"); abort(); } return p; }
void *xrealloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) { fprintf(stderr, "OOM\n"); abort(); } return p; }
char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }
char *xstrndup(const char *s, size_t n) { char *r = xmalloc(n + 1); memcpy(r, s, n); r[n] = 0; return r; }

void sb_init(SB *b) { b->s = NULL; b->n = b->cap = 0; }
static void sb_grow(SB *b, size_t need) {
    if (b->n + need + 1 <= b->cap) return;
    size_t c = b->cap ? b->cap : 64;
    while (c < b->n + need + 1) c *= 2;
    b->s = xrealloc(b->s, c); b->cap = c;
}
void sb_putc(SB *b, char c) { sb_grow(b, 1); b->s[b->n++] = c; b->s[b->n] = 0; }
void sb_put(SB *b, const char *s, size_t n) { if (!n) { sb_grow(b, 0); b->s[b->n] = 0; return; } sb_grow(b, n); memcpy(b->s + b->n, s, n); b->n += n; b->s[b->n] = 0; }
void sb_puts(SB *b, const char *s) { sb_put(b, s, strlen(s)); }
void sb_printf(SB *b, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char tmp[512]; int k = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (k < (int)sizeof tmp) { sb_put(b, tmp, (size_t)k); return; }
    sb_grow(b, (size_t)k); va_start(ap, fmt); vsnprintf(b->s + b->n, (size_t)k + 1, fmt, ap); va_end(ap); b->n += (size_t)k;
}
void sb_utf8(SB *b, uint32_t cp) { char o[4]; int k = utf8_encode(cp, o); sb_put(b, o, (size_t)k); }
char *sb_take(SB *b) { if (!b->s) { sb_grow(b, 0); b->s[0] = 0; } char *r = b->s; b->s = NULL; b->n = b->cap = 0; return r; }
void sb_free(SB *b) { free(b->s); b->s = NULL; b->n = b->cap = 0; }

uint32_t hash_str(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h ? h : 1;
}
static HMEntry *hm_find(HMap *m, const char *key, size_t n, uint32_t h) {
    if (!m->cap) return NULL;
    uint32_t mask = (uint32_t)m->cap - 1;
    for (uint32_t i = h & mask;; i = (i + 1) & mask) {
        HMEntry *e = &m->e[i];
        if (!e->key) return e;
        if (e->hash == h && strlen(e->key) == n && memcmp(e->key, key, n) == 0) return e;
    }
}
void *hm_getn(HMap *m, const char *key, size_t n) {
    HMEntry *e = hm_find(m, key, n, hash_str(key, n));
    return e && e->key ? e->val : NULL;
}
void *hm_get(HMap *m, const char *key) { return hm_getn(m, key, strlen(key)); }
void hm_put(HMap *m, const char *key, void *val) {
    if ((m->n + 1) * 4 >= m->cap * 3) {
        int oc = m->cap; HMEntry *oe = m->e;
        m->cap = oc ? oc * 2 : 16; m->e = xcalloc((size_t)m->cap, sizeof(HMEntry)); m->n = 0;
        for (int i = 0; i < oc; i++) if (oe[i].key) { HMEntry *e = hm_find(m, oe[i].key, strlen(oe[i].key), oe[i].hash); *e = oe[i]; m->n++; }
        free(oe);
    }
    size_t n = strlen(key); uint32_t h = hash_str(key, n);
    HMEntry *e = hm_find(m, key, n, h);
    if (!e->key) { e->key = xstrndup(key, n); e->hash = h; m->n++; }
    e->val = val;
}
void hm_free(HMap *m, void (*freeval)(void *)) {
    for (int i = 0; i < m->cap; i++) if (m->e[i].key) { free(m->e[i].key); if (freeval) freeval(m->e[i].val); }
    free(m->e); m->e = NULL; m->n = m->cap = 0;
}

static HMap g_atoms; static pthread_mutex_t g_atom_mu = PTHREAD_MUTEX_INITIALIZER;
const char *atomn(const char *s, size_t n) {
    pthread_mutex_lock(&g_atom_mu);
    HMEntry *e = hm_find(&g_atoms, s, n, hash_str(s, n));
    const char *r;
    if (e && e->key) r = e->key;
    else { char *k = xstrndup(s, n); hm_put(&g_atoms, k, NULL); r = hm_find(&g_atoms, s, n, hash_str(s, n))->key; free(k); }
    pthread_mutex_unlock(&g_atom_mu);
    return r;
}
const char *atom(const char *s) { return atomn(s, strlen(s)); }
const char *atom_lower(const char *s, size_t n) {
    char buf[128]; char *b = n < sizeof buf ? buf : xmalloc(n + 1);
    for (size_t i = 0; i < n; i++) b[i] = (char)lc((unsigned char)s[i]);
    const char *r = atomn(b, n); if (b != buf) free(b); return r;
}

int utf8_decode(const char *s, size_t n, uint32_t *cp) {
    const uint8_t *u = (const uint8_t *)s;
    if (!n) { *cp = 0; return 0; }
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xE0) == 0xC0 && n >= 2 && (u[1] & 0xC0) == 0x80) { *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F); return 2; }
    if ((u[0] & 0xF0) == 0xE0 && n >= 3 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) { *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F); return 3; }
    if ((u[0] & 0xF8) == 0xF0 && n >= 4 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) { *cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) | ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F); return 4; }
    *cp = 0xFFFD; return 1;
}
int utf8_encode(uint32_t cp, char o[4]) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
    if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    if (cp > 0x10FFFF) cp = 0xFFFD;
    if (cp < 0x10000) return utf8_encode(cp, o);
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

bool str_ieq(const char *a, const char *b) { while (*a && *b) { if (lc((unsigned char)*a) != lc((unsigned char)*b)) return false; a++; b++; } return *a == *b; }
bool str_ieqn(const char *a, const char *b, size_t n) { for (size_t i = 0; i < n; i++) { if (lc((unsigned char)a[i]) != lc((unsigned char)b[i])) return false; if (!a[i]) return true; } return true; }
bool str_starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
bool str_istarts(const char *s, const char *p) { return str_ieqn(s, p, strlen(p)); }
char *str_trim(char *s) { while (is_ws((unsigned char)*s)) s++; size_t n = strlen(s); while (n && is_ws((unsigned char)s[n - 1])) s[--n] = 0; return s; }

double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = xmalloc((size_t)n + 1); size_t r = fread(b, 1, (size_t)n, f); fclose(f); b[r] = 0;
    if (len) *len = r; return b;
}
