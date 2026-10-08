#ifndef LUMEN_UTIL_H
#define LUMEN_UTIL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define LMIN(a,b) ((a)<(b)?(a):(b))
#define LMAX(a,b) ((a)>(b)?(a):(b))
#define LCLAMP(v,lo,hi) LMIN(LMAX(v,lo),hi)
#define ARRLEN(a) (sizeof(a)/sizeof((a)[0]))

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* Generic growable vector: struct { T *v; int n, cap; } */
#define VEC(T) struct { T *v; int n, cap; }
#define vec_push(a, x) do { if ((a).n >= (a).cap) { (a).cap = (a).cap ? (a).cap * 2 : 8; \
    (a).v = xrealloc((a).v, sizeof(*(a).v) * (size_t)(a).cap); } (a).v[(a).n++] = (x); } while (0)
#define vec_reserve(a, c) do { if ((a).cap < (c)) { (a).cap = (c); (a).v = xrealloc((a).v, sizeof(*(a).v) * (size_t)(a).cap); } } while (0)
#define vec_free(a) do { free((a).v); (a).v = NULL; (a).n = (a).cap = 0; } while (0)
#define vec_pop(a) ((a).v[--(a).n])
#define vec_last(a) ((a).v[(a).n - 1])

/* String builder */
typedef struct { char *s; size_t n, cap; } SB;
void sb_init(SB *b);
void sb_putc(SB *b, char c);
void sb_put(SB *b, const char *s, size_t n);
void sb_puts(SB *b, const char *s);
void sb_printf(SB *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sb_utf8(SB *b, uint32_t cp);
char *sb_take(SB *b); /* returns NUL terminated, resets builder */
void sb_free(SB *b);
static inline void sb_clear(SB *b) { b->n = 0; if (b->s) b->s[0] = 0; }

/* String hash map (string -> void*) */
typedef struct { char *key; void *val; uint32_t hash; } HMEntry;
typedef struct { HMEntry *e; int cap, n; } HMap;
uint32_t hash_str(const char *s, size_t n);
void *hm_get(HMap *m, const char *key);
void *hm_getn(HMap *m, const char *key, size_t n);
void hm_put(HMap *m, const char *key, void *val);
void hm_free(HMap *m, void (*freeval)(void *));
#define hm_foreach(m, ent) for (HMEntry *ent = (m)->e; (m)->e && ent < (m)->e + (m)->cap; ent++) if (ent->key)

/* Atoms: interned lower-case strings for fast tag/attr comparison */
const char *atom(const char *s);
const char *atomn(const char *s, size_t n);
const char *atom_lower(const char *s, size_t n);

/* UTF-8 */
int utf8_decode(const char *s, size_t n, uint32_t *cp); /* returns bytes consumed (>=1) */
int utf8_encode(uint32_t cp, char out[4]);

/* misc string helpers */
bool str_ieq(const char *a, const char *b);
bool str_ieqn(const char *a, const char *b, size_t n);
bool str_starts(const char *s, const char *prefix);
bool str_istarts(const char *s, const char *prefix);
char *str_trim(char *s);
static inline bool is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static inline int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

double now_ms(void);
char *read_file(const char *path, size_t *len);

#define LOG(...) do { fprintf(stderr, "[lumen] " __VA_ARGS__); fputc('\n', stderr); } while (0)
extern int g_verbose;
#define DLOG(...) do { if (g_verbose) LOG(__VA_ARGS__); } while (0)

#endif
