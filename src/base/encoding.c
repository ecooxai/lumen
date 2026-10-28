/* The WHATWG Encoding Standard (https://encoding.spec.whatwg.org/): the decoders and encoders follow its algorithms step
   by step; the indexes come from its indexes.json through tools/gen_encoding_tables.py. */
#include "encoding.h"
#include "encoding_tables.h"
#include <ctype.h>
#include <pthread.h>
#include <strings.h>

_Static_assert(ENC_UTF_8 == ENCODING_UTF8 && ENC_WINDOWS_1252 == ENCODING_WINDOWS_1252 && ENC_REPLACEMENT == ENCODING_REPLACEMENT &&
               ENC_UTF_16BE == ENCODING_UTF16BE && ENC_UTF_16LE == ENCODING_UTF16LE && ENC_X_USER_DEFINED == ENCODING_X_USER_DEFINED,
               "encoding ids");

static bool ascii_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }

int enc_label(const char *s, size_t n) {
    while (n && ascii_ws((unsigned char)*s)) { s++; n--; }
    while (n && ascii_ws((unsigned char)s[n - 1])) n--;
    if (!n || n > 32) return -1;
    char lo[33];
    for (size_t i = 0; i < n; i++) {
        /* Labels are exact byte sequences; an embedded NUL must not make the
         * C-string search accept a valid prefix. Case folding is ASCII-only. */
        if (!s[i]) return -1;
        lo[i] = (char)lc((unsigned char)s[i]);
    }
    lo[n] = 0;
    int a = 0, b = (int)ARRLEN(enc_labels) - 1;
    while (a <= b) {
        int m = (a + b) / 2, c = strcmp(enc_labels[m].label, lo);
        if (!c) return enc_labels[m].enc;
        if (c < 0) a = m + 1; else b = m - 1;
    }
    return -1;
}
const char *enc_name(int e) { return e >= 0 && e < ENC_COUNT ? enc_names[e] : "UTF-8"; }
int enc_output(int e) { return e == ENC_REPLACEMENT || e == ENC_UTF_16BE || e == ENC_UTF_16LE ? ENC_UTF_8 : e; }

static const uint16_t *single_index(int e) { return enc_single + (size_t)(e - ENC_IBM866) * 128; }

/* ---- index lookups ---- */

static uint32_t gb_ranges_cp(uint32_t p) {
    if ((p > 39419 && p < 189000) || p > 1237575) return 0;
    if (p == 7457) return 0xE7C7;
    size_t n = ARRLEN(idx_gb_ranges) / 2, best = 0;
    for (size_t i = 0; i < n && idx_gb_ranges[i * 2] <= p; i++) best = i;
    return idx_gb_ranges[best * 2 + 1] + p - idx_gb_ranges[best * 2];
}
static uint32_t gb_ranges_pointer(uint32_t cp) {
    if (cp == 0xE7C7) return 7457;
    size_t n = ARRLEN(idx_gb_ranges) / 2, best = 0;
    for (size_t i = 0; i < n && idx_gb_ranges[i * 2 + 1] <= cp; i++) best = i;
    return idx_gb_ranges[best * 2] + cp - idx_gb_ranges[best * 2 + 1];
}

/* reverse indexes: (code point, pointer) pairs sorted by code point, then pointer */
typedef struct { uint32_t cp, ptr; } Rev;
typedef struct { Rev *v; size_t n; } RevIndex;
enum { RI_JIS0208, RI_EUCKR, RI_GB18030, RI_BIG5, RI_COUNT };
static RevIndex revs[RI_COUNT];
static pthread_once_t revs_once = PTHREAD_ONCE_INIT;
static int rev_cmp(const void *a, const void *b) {
    const Rev *x = a, *y = b;
    return x->cp != y->cp ? (x->cp < y->cp ? -1 : 1) : (x->ptr < y->ptr ? -1 : x->ptr > y->ptr);
}
static void rev_build16(RevIndex *r, const uint16_t *t, size_t n) {
    r->v = xmalloc(sizeof *r->v * n);
    for (size_t i = 0; i < n; i++) if (t[i]) r->v[r->n++] = (Rev){ t[i], (uint32_t)i };
    qsort(r->v, r->n, sizeof *r->v, rev_cmp);
}
static void revs_build(void) {
    rev_build16(&revs[RI_JIS0208], idx_jis0208, ARRLEN(idx_jis0208));
    rev_build16(&revs[RI_EUCKR], idx_euckr, ARRLEN(idx_euckr));
    rev_build16(&revs[RI_GB18030], idx_gb18030, ARRLEN(idx_gb18030));
    RevIndex *r = &revs[RI_BIG5];
    r->v = xmalloc(sizeof *r->v * ARRLEN(idx_big5));
    for (size_t i = 0; i < ARRLEN(idx_big5); i++) if (idx_big5[i]) r->v[r->n++] = (Rev){ idx_big5[i], (uint32_t)i };
    qsort(r->v, r->n, sizeof *r->v, rev_cmp);
}
/* the first (or last) pointer for cp whose pointer lies outside [skip_lo, skip_hi]; -1 if none */
static int32_t rev_find(int which, uint32_t cp, bool last, uint32_t skip_lo, uint32_t skip_hi) {
    pthread_once(&revs_once, revs_build);
    const RevIndex *r = &revs[which];
    size_t a = 0, b = r->n;
    while (a < b) { size_t m = (a + b) / 2; if (r->v[m].cp < cp) a = m + 1; else b = m; }
    int32_t found = -1;
    for (size_t i = a; i < r->n && r->v[i].cp == cp; i++) {
        uint32_t p = r->v[i].ptr;
        if (p >= skip_lo && p <= skip_hi) continue;
        found = (int32_t)p;
        if (!last) break;
    }
    return found;
}
#define NO_SKIP 1, 0

/* ---- decoders ---- */

enum { R_OK, R_ERR, R_FIN };
enum { J_ASCII, J_ROMAN, J_KATAKANA, J_LEAD, J_TRAIL, J_ESC_START, J_ESC };
typedef struct { int v[4]; int n; } Queue;   /* bytes prepended to the input; v[n - 1] comes next; -1 is end-of-queue */
static void prepend(Queue *q, int b) { q->v[q->n++] = b; }
static void prepend2(Queue *q, int a, int b) { prepend(q, b); prepend(q, a); }   /* the input continues a, b, ... */
static bool is_ascii(int b) { return b >= 0 && b < 0x80; }

static int step(EncDecoder *d, int b, Queue *q, SB *out) {
#define EMIT(c) do { sb_utf8(out, (uint32_t)(c)); return R_OK; } while (0)
    switch (d->enc) {
    case ENC_UTF_8:
        if (b < 0) { if (d->needed) { d->needed = 0; return R_ERR; } return R_FIN; }
        if (!d->needed) {
            if (b < 0x80) EMIT(b);
            if (b >= 0xC2 && b <= 0xDF) { d->needed = 1; d->cp = b & 0x1F; }
            else if (b >= 0xE0 && b <= 0xEF) { if (b == 0xE0) d->lower = 0xA0; if (b == 0xED) d->upper = 0x9F; d->needed = 2; d->cp = b & 0xF; }
            else if (b >= 0xF0 && b <= 0xF4) { if (b == 0xF0) d->lower = 0x90; if (b == 0xF4) d->upper = 0x8F; d->needed = 3; d->cp = b & 7; }
            else return R_ERR;
            return R_OK;
        }
        if ((uint32_t)b < d->lower || (uint32_t)b > d->upper) {
            d->cp = d->needed = d->seen = 0; d->lower = 0x80; d->upper = 0xBF;
            prepend(q, b); return R_ERR;
        }
        d->lower = 0x80; d->upper = 0xBF;
        d->cp = d->cp << 6 | (uint32_t)(b & 0x3F);
        if (++d->seen != d->needed) return R_OK;
        { uint32_t c = d->cp; d->cp = d->needed = d->seen = 0; EMIT(c); }
    case ENC_GBK: case ENC_GB18030:
        if (b < 0) { if (d->b1 || d->b2 || d->b3) { d->b1 = d->b2 = d->b3 = 0; return R_ERR; } return R_FIN; }
        if (d->b3) {
            if (b < 0x30 || b > 0x39) { prepend(q, b); prepend2(q, d->b2, d->b3); d->b1 = d->b2 = d->b3 = 0; return R_ERR; }
            uint32_t c = gb_ranges_cp((uint32_t)(d->b1 - 0x81) * 12600 + (uint32_t)(d->b2 - 0x30) * 1260 + (uint32_t)(d->b3 - 0x81) * 10 + (uint32_t)(b - 0x30));
            d->b1 = d->b2 = d->b3 = 0;
            if (!c) return R_ERR;
            EMIT(c);
        }
        if (d->b2) {
            if (b >= 0x81 && b <= 0xFE) { d->b3 = (uint8_t)b; return R_OK; }
            prepend2(q, d->b2, b); d->b1 = d->b2 = 0; return R_ERR;
        }
        if (d->b1) {
            if (b >= 0x30 && b <= 0x39) { d->b2 = (uint8_t)b; return R_OK; }
            int lead = d->b1, off = b < 0x7F ? 0x40 : 0x41;
            d->b1 = 0;
            uint32_t c = 0;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFE)) c = idx_gb18030[(lead - 0x81) * 190 + (b - off)];
            if (c) EMIT(c);
            if (is_ascii(b)) prepend(q, b);
            return R_ERR;
        }
        if (b < 0x80) EMIT(b);
        if (b == 0x80) EMIT(0x20AC);
        if (b >= 0x81 && b <= 0xFE) { d->b1 = (uint8_t)b; return R_OK; }
        return R_ERR;
    case ENC_BIG5:
        if (b < 0) { if (d->lead) { d->lead = 0; return R_ERR; } return R_FIN; }
        if (d->lead) {
            int l = d->lead, off = b < 0x7F ? 0x40 : 0x62, p = -1;
            d->lead = 0;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0xA1 && b <= 0xFE)) p = (l - 0x81) * 157 + (b - off);
            switch (p) {
            case 1133: sb_utf8(out, 0xCA); EMIT(0x304);
            case 1135: sb_utf8(out, 0xCA); EMIT(0x30C);
            case 1164: sb_utf8(out, 0xEA); EMIT(0x304);
            case 1166: sb_utf8(out, 0xEA); EMIT(0x30C);
            }
            uint32_t c = p >= 0 && (size_t)p < ARRLEN(idx_big5) ? idx_big5[p] : 0;
            if (c) EMIT(c);
            if (is_ascii(b)) prepend(q, b);
            return R_ERR;
        }
        if (b < 0x80) EMIT(b);
        if (b >= 0x81 && b <= 0xFE) { d->lead = (uint8_t)b; return R_OK; }
        return R_ERR;
    case ENC_EUC_JP:
        if (b < 0) { if (d->lead) { d->lead = 0; return R_ERR; } return R_FIN; }
        if (d->lead == 0x8E && b >= 0xA1 && b <= 0xDF) { d->lead = 0; EMIT(0xFF61 - 0xA1 + b); }
        if (d->lead == 0x8F && b >= 0xA1 && b <= 0xFE) { d->flag = 1; d->lead = (uint8_t)b; return R_OK; }
        if (d->lead) {
            int l = d->lead;
            uint32_t c = 0;
            d->lead = 0;
            if (l >= 0xA1 && l <= 0xFE && b >= 0xA1 && b <= 0xFE) {
                size_t p = (size_t)(l - 0xA1) * 94 + (size_t)(b - 0xA1);
                c = d->flag ? (p < ARRLEN(idx_jis0212) ? idx_jis0212[p] : 0) : (p < ARRLEN(idx_jis0208) ? idx_jis0208[p] : 0);
            }
            d->flag = 0;
            if (c) EMIT(c);
            if (is_ascii(b)) prepend(q, b);
            return R_ERR;
        }
        if (b < 0x80) EMIT(b);
        if (b == 0x8E || b == 0x8F || (b >= 0xA1 && b <= 0xFE)) { d->lead = (uint8_t)b; return R_OK; }
        return R_ERR;
    case ENC_ISO_2022_JP:
        switch (d->st) {
        case J_ASCII:
            if (b == 0x1B) { d->st = J_ESC_START; return R_OK; }
            if (b >= 0 && b <= 0x7F && b != 0x0E && b != 0x0F) { d->flag = 0; EMIT(b); }
            if (b < 0) return R_FIN;
            d->flag = 0; return R_ERR;
        case J_ROMAN:
            if (b == 0x1B) { d->st = J_ESC_START; return R_OK; }
            if (b == 0x5C) { d->flag = 0; EMIT(0xA5); }
            if (b == 0x7E) { d->flag = 0; EMIT(0x203E); }
            if (b >= 0 && b <= 0x7F && b != 0x0E && b != 0x0F) { d->flag = 0; EMIT(b); }
            if (b < 0) return R_FIN;
            d->flag = 0; return R_ERR;
        case J_KATAKANA:
            if (b == 0x1B) { d->st = J_ESC_START; return R_OK; }
            if (b >= 0x21 && b <= 0x5F) { d->flag = 0; EMIT(0xFF61 - 0x21 + b); }
            if (b < 0) return R_FIN;
            d->flag = 0; return R_ERR;
        case J_LEAD:
            if (b == 0x1B) { d->st = J_ESC_START; return R_OK; }
            if (b >= 0x21 && b <= 0x7E) { d->flag = 0; d->lead = (uint8_t)b; d->st = J_TRAIL; return R_OK; }
            if (b < 0) return R_FIN;
            d->flag = 0; return R_ERR;
        case J_TRAIL:
            if (b == 0x1B) { d->st = J_ESC_START; return R_ERR; }
            if (b >= 0x21 && b <= 0x7E) {
                d->st = J_LEAD;
                uint32_t c = idx_jis0208[(d->lead - 0x21) * 94 + b - 0x21];
                if (!c) return R_ERR;
                EMIT(c);
            }
            if (b < 0) { d->st = J_LEAD; prepend(q, b); return R_ERR; }
            d->st = J_LEAD; return R_ERR;
        case J_ESC_START:
            if (b == 0x24 || b == 0x28) { d->lead = (uint8_t)b; d->st = J_ESC; return R_OK; }
            prepend(q, b); d->flag = 0; d->st = d->ost; return R_ERR;
        case J_ESC: {
            int l = d->lead, s = -1;
            d->lead = 0;
            if (l == 0x28 && b == 0x42) s = J_ASCII;
            else if (l == 0x28 && b == 0x4A) s = J_ROMAN;
            else if (l == 0x28 && b == 0x49) s = J_KATAKANA;
            else if (l == 0x24 && (b == 0x40 || b == 0x42)) s = J_LEAD;
            if (s >= 0) {
                d->st = d->ost = (uint8_t)s;
                bool o = d->flag;
                d->flag = 1;
                return o ? R_ERR : R_OK;
            }
            prepend2(q, l, b); d->flag = 0; d->st = d->ost; return R_ERR;
        }
        }
        return R_ERR;
    case ENC_SHIFT_JIS:
        if (b < 0) { if (d->lead) { d->lead = 0; return R_ERR; } return R_FIN; }
        if (d->lead) {
            int l = d->lead, off = b < 0x7F ? 0x40 : 0x41, loff = l < 0xA0 ? 0x81 : 0xC1, p = -1;
            d->lead = 0;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC)) p = (l - loff) * 188 + b - off;
            if (p >= 8836 && p <= 10715) EMIT(0xE000 - 8836 + p);
            uint32_t c = p >= 0 && (size_t)p < ARRLEN(idx_jis0208) ? idx_jis0208[p] : 0;
            if (c) EMIT(c);
            if (is_ascii(b)) prepend(q, b);
            return R_ERR;
        }
        if (b <= 0x80) EMIT(b);
        if (b >= 0xA1 && b <= 0xDF) EMIT(0xFF61 - 0xA1 + b);
        if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) { d->lead = (uint8_t)b; return R_OK; }
        return R_ERR;
    case ENC_EUC_KR:
        if (b < 0) { if (d->lead) { d->lead = 0; return R_ERR; } return R_FIN; }
        if (d->lead) {
            int l = d->lead;
            uint32_t c = 0;
            d->lead = 0;
            if (b >= 0x41 && b <= 0xFE) c = idx_euckr[(l - 0x81) * 190 + (b - 0x41)];
            if (c) EMIT(c);
            if (is_ascii(b)) prepend(q, b);
            return R_ERR;
        }
        if (b < 0x80) EMIT(b);
        if (b >= 0x81 && b <= 0xFE) { d->lead = (uint8_t)b; return R_OK; }
        return R_ERR;
    case ENC_REPLACEMENT:
        if (b < 0) return R_FIN;
        if (!d->flag) { d->flag = 1; return R_ERR; }
        return R_FIN;
    case ENC_UTF_16BE: case ENC_UTF_16LE: {
        bool be = d->enc == ENC_UTF_16BE;
        if (b < 0) { if (d->flag || d->cp) { d->flag = 0; d->cp = 0; return R_ERR; } return R_FIN; }
        if (!d->flag) { d->flag = 1; d->lead = (uint8_t)b; return R_OK; }
        uint32_t cu = be ? (uint32_t)d->lead << 8 | (uint32_t)b : (uint32_t)b << 8 | d->lead;
        d->flag = 0;
        if (d->cp) {
            uint32_t ls = d->cp;
            d->cp = 0;
            if (cu >= 0xDC00 && cu <= 0xDFFF) EMIT(0x10000 + ((ls - 0xD800) << 10) + (cu - 0xDC00));
            int b1 = (int)(cu >> 8), b2 = (int)(cu & 0xFF);
            if (be) prepend2(q, b1, b2); else prepend2(q, b2, b1);
            return R_ERR;
        }
        if (cu >= 0xD800 && cu <= 0xDBFF) { d->cp = cu; return R_OK; }
        if (cu >= 0xDC00 && cu <= 0xDFFF) return R_ERR;
        EMIT(cu);
    }
    case ENC_X_USER_DEFINED:
        if (b < 0) return R_FIN;
        if (b < 0x80) EMIT(b);
        EMIT(0xF780 + b - 0x80);
    default:   /* single-byte */
        if (b < 0) return R_FIN;
        if (b < 0x80) EMIT(b);
        { uint32_t c = single_index(d->enc)[b - 0x80]; if (!c) return R_ERR; EMIT(c); }
    }
#undef EMIT
}

void enc_decoder_init(EncDecoder *d, int enc, bool fatal, bool strip_bom) {
    memset(d, 0, sizeof *d);
    d->enc = (uint8_t)(enc >= 0 && enc < ENC_COUNT ? enc : ENC_UTF_8);
    d->fatal = fatal;
    d->lower = 0x80; d->upper = 0xBF;
    d->bom = strip_bom && (d->enc == ENC_UTF_8 || d->enc == ENC_UTF_16BE || d->enc == ENC_UTF_16LE);
}

static bool run(EncDecoder *d, const uint8_t *in, size_t n, bool last, SB *out) {
    Queue q = { .n = 0 };
    size_t i = 0;
    for (;;) {
        int b;
        if (q.n) b = q.v[--q.n];
        else if (i < n) b = in[i++];
        else if (last) b = -1;
        else return true;
        int r = step(d, b, &q, out);
        if (r == R_ERR) { if (d->fatal) return false; sb_utf8(out, 0xFFFD); }
        else if (r == R_FIN) return true;
    }
}

bool enc_decode(EncDecoder *d, const uint8_t *in, size_t n, bool last, SB *out) {
    if (d->bom) {   /* hold the first bytes until a BOM of this encoding is ruled in or out */
        static const uint8_t bom8[] = { 0xEF, 0xBB, 0xBF }, bombe[] = { 0xFE, 0xFF }, bomle[] = { 0xFF, 0xFE };
        const uint8_t *bom = d->enc == ENC_UTF_8 ? bom8 : d->enc == ENC_UTF_16BE ? bombe : bomle;
        size_t bl = d->enc == ENC_UTF_8 ? 3 : 2;
        while (d->pn < bl && n) { d->pend[d->pn++] = *in++; n--; }
        bool match = !memcmp(d->pend, bom, d->pn);
        if (match && d->pn < bl && !last) return true;
        d->bom = 0;
        uint8_t held[3];
        size_t hn = match && d->pn == bl ? 0 : d->pn;
        memcpy(held, d->pend, hn);
        d->pn = 0;
        if (hn && !run(d, held, hn, last && !n, out)) return false;
        if (!n && hn) return true;
    }
    return run(d, in, n, last, out);
}

char *enc_decode_all(int enc, const uint8_t *in, size_t n, size_t *out_len) {
    EncDecoder d;
    enc_decoder_init(&d, enc, false, false);
    SB o; sb_init(&o);
    sb_put(&o, "", 0);
    enc_decode(&d, in, n, true, &o);
    if (out_len) *out_len = o.n;
    return sb_take(&o);
}

/* ---- encoders ---- */

enum { E_ASCII, E_ROMAN, E_JIS0208 };
static const uint32_t gb18030_pua[][2] = {
    { 0xE81E, 0xFE59 }, { 0xE826, 0xFE61 }, { 0xE82B, 0xFE66 }, { 0xE82C, 0xFE67 }, { 0xE832, 0xFE6D }, { 0xE843, 0xFE7E },
    { 0xE854, 0xFE90 }, { 0xE864, 0xFEA0 }, { 0xE78D, 0xA6D9 }, { 0xE78F, 0xA6DB }, { 0xE78E, 0xA6DA }, { 0xE790, 0xA6DC },
    { 0xE791, 0xA6DD }, { 0xE792, 0xA6DE }, { 0xE793, 0xA6DF }, { 0xE794, 0xA6EC }, { 0xE795, 0xA6ED }, { 0xE796, 0xA6F3 },
};

int32_t enc_encode_cp(EncEncoder *e, uint32_t cp, uint8_t o[8], int *n) {
    *n = 0;
#define B(x) (o[(*n)++] = (uint8_t)(x))
    switch (e->enc) {
    case ENC_UTF_8: case ENC_REPLACEMENT: case ENC_UTF_16BE: case ENC_UTF_16LE: {
        char t[4];
        int k = utf8_encode(cp, t);
        for (int i = 0; i < k; i++) B(t[i]);
        return -1;
    }
    case ENC_GBK: case ENC_GB18030: {
        bool gbk = e->enc == ENC_GBK;
        if (cp < 0x80) { B(cp); return -1; }
        if (cp == 0xE5E5) return (int32_t)cp;
        if (gbk && cp == 0x20AC) { B(0x80); return -1; }
        for (size_t i = 0; i < ARRLEN(gb18030_pua); i++)
            if (gb18030_pua[i][0] == cp) { B(gb18030_pua[i][1] >> 8); B(gb18030_pua[i][1] & 0xFF); return -1; }
        int32_t p = rev_find(RI_GB18030, cp, false, NO_SKIP);
        if (p >= 0) { uint32_t t = (uint32_t)p % 190; B((uint32_t)p / 190 + 0x81); B(t + (t < 0x3F ? 0x40 : 0x41)); return -1; }
        if (gbk) return (int32_t)cp;
        uint32_t q = gb_ranges_pointer(cp);
        B(q / 12600 + 0x81); q %= 12600; B(q / 1260 + 0x30); q %= 1260; B(q / 10 + 0x81); B(q % 10 + 0x30);
        return -1;
    }
    case ENC_BIG5: {
        if (cp < 0x80) { B(cp); return -1; }
        bool last = cp == 0x2550 || cp == 0x255E || cp == 0x2561 || cp == 0x256A || cp == 0x5341 || cp == 0x5345;
        int32_t p = rev_find(RI_BIG5, cp, last, 0, (0xA1 - 0x81) * 157 - 1);
        if (p < 0) return (int32_t)cp;
        uint32_t t = (uint32_t)p % 157;
        B((uint32_t)p / 157 + 0x81); B(t + (t < 0x3F ? 0x40 : 0x62));
        return -1;
    }
    case ENC_EUC_JP: {
        if (cp < 0x80) { B(cp); return -1; }
        if (cp == 0xA5) { B(0x5C); return -1; }
        if (cp == 0x203E) { B(0x7E); return -1; }
        if (cp >= 0xFF61 && cp <= 0xFF9F) { B(0x8E); B(cp - 0xFF61 + 0xA1); return -1; }
        if (cp == 0x2212) cp = 0xFF0D;
        int32_t p = rev_find(RI_JIS0208, cp, false, NO_SKIP);
        if (p < 0) return (int32_t)cp;
        B((uint32_t)p / 94 + 0xA1); B((uint32_t)p % 94 + 0xA1);
        return -1;
    }
    case ENC_ISO_2022_JP:
        for (;;) {
            if ((e->st == E_ASCII || e->st == E_ROMAN) && (cp == 0x0E || cp == 0x0F || cp == 0x1B)) return 0xFFFD;
            if (e->st == E_ASCII && cp < 0x80) { B(cp); return -1; }
            if (e->st == E_ROMAN && ((cp < 0x80 && cp != 0x5C && cp != 0x7E) || cp == 0xA5 || cp == 0x203E)) {
                B(cp < 0x80 ? cp : cp == 0xA5 ? 0x5C : 0x7E); return -1;
            }
            if (cp < 0x80) { e->st = E_ASCII; B(0x1B); B(0x28); B(0x42); continue; }
            if (cp == 0xA5 || cp == 0x203E) { e->st = E_ROMAN; B(0x1B); B(0x28); B(0x4A); continue; }
            uint32_t c = cp;
            if (c == 0x2212) c = 0xFF0D;
            if (c >= 0xFF61 && c <= 0xFF9F) c = idx_katakana[c - 0xFF61];
            int32_t p = rev_find(RI_JIS0208, c, false, NO_SKIP);
            if (p < 0) {
                if (e->st == E_JIS0208) { e->st = E_ASCII; B(0x1B); B(0x28); B(0x42); }
                return (int32_t)c;
            }
            if (e->st != E_JIS0208) { e->st = E_JIS0208; B(0x1B); B(0x24); B(0x42); }
            B((uint32_t)p / 94 + 0x21); B((uint32_t)p % 94 + 0x21);
            return -1;
        }
    case ENC_SHIFT_JIS: {
        if (cp <= 0x80) { B(cp); return -1; }
        if (cp == 0xA5) { B(0x5C); return -1; }
        if (cp == 0x203E) { B(0x7E); return -1; }
        if (cp >= 0xFF61 && cp <= 0xFF9F) { B(cp - 0xFF61 + 0xA1); return -1; }
        if (cp == 0x2212) cp = 0xFF0D;
        int32_t p = rev_find(RI_JIS0208, cp, false, 8272, 8835);
        if (p < 0) return (int32_t)cp;
        uint32_t lead = (uint32_t)p / 188, t = (uint32_t)p % 188;
        B(lead + (lead < 0x1F ? 0x81 : 0xC1)); B(t + (t < 0x3F ? 0x40 : 0x41));
        return -1;
    }
    case ENC_EUC_KR: {
        if (cp < 0x80) { B(cp); return -1; }
        int32_t p = rev_find(RI_EUCKR, cp, false, NO_SKIP);
        if (p < 0) return (int32_t)cp;
        B((uint32_t)p / 190 + 0x81); B((uint32_t)p % 190 + 0x41);
        return -1;
    }
    case ENC_X_USER_DEFINED:
        if (cp < 0x80) { B(cp); return -1; }
        if (cp >= 0xF780 && cp <= 0xF7FF) { B(cp - 0xF780 + 0x80); return -1; }
        return (int32_t)cp;
    default: {
        if (cp < 0x80) { B(cp); return -1; }
        const uint16_t *t = single_index(e->enc);
        for (int i = 0; i < 128; i++) if (t[i] == cp) { B(0x80 + i); return -1; }
        return (int32_t)cp;
    }
    }
#undef B
}

int enc_encode_end(EncEncoder *e, uint8_t o[8]) {
    if (e->enc != ENC_ISO_2022_JP || e->st == E_ASCII) return 0;
    e->st = E_ASCII;
    o[0] = 0x1B; o[1] = 0x28; o[2] = 0x42;
    return 3;
}

/* calls put for each run of output bytes, err for each unmappable code point */
static void encode_text(int enc, const char *s, size_t n, void (*put)(void *, const uint8_t *, int), void (*err)(void *, uint32_t), void *ctx) {
    EncEncoder e = { (uint8_t)enc_output(enc), 0 };
    uint8_t o[8];
    int k;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += (size_t)utf8_decode(s + i, n - i, &cp);
        int32_t bad = enc_encode_cp(&e, cp, o, &k);
        if (k) put(ctx, o, k);
        if (bad >= 0) err(ctx, (uint32_t)bad);
    }
    if ((k = enc_encode_end(&e, o))) put(ctx, o, k);
}

static void html_put(void *c, const uint8_t *b, int k) { sb_put(c, (const char *)b, (size_t)k); }
static void html_err(void *c, uint32_t cp) { sb_printf(c, "&#%u;", cp); }
char *enc_encode_html(int enc, const char *s, size_t n, size_t *out_len) {
    SB o; sb_init(&o);
    sb_put(&o, "", 0);
    encode_text(enc, s, n, html_put, html_err, &o);
    if (out_len) *out_len = o.n;
    return sb_take(&o);
}

typedef struct { SB *out; bool (*set)(unsigned char); bool plus; } PctCtx;
static void pct_put(void *c, const uint8_t *b, int k) {
    PctCtx *p = c;
    for (int i = 0; i < k; i++) {
        if (p->plus && b[i] == ' ') sb_putc(p->out, '+');
        else if (p->set(b[i])) sb_printf(p->out, "%%%02X", b[i]);
        else sb_putc(p->out, (char)b[i]);
    }
}
static void pct_err(void *c, uint32_t cp) { sb_printf(((PctCtx *)c)->out, "%%26%%23%u%%3B", cp); }
void enc_percent_encode(int enc, const char *s, size_t n, bool (*set)(unsigned char), bool space_plus, SB *out) {
    PctCtx p = { out, set, space_plus };
    encode_text(enc, s, n, pct_put, pct_err, &p);
}

/* ---- document encoding ---- */

bool enc_valid_utf8(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        if (c < 0x80) { i++; continue; }
        int need;
        uint8_t lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else return false;
        for (int k = 1; k <= need; k++) {
            if (i + (size_t)k >= n) return true;   /* a sequence the transfer cut short */
            if (s[i + k] < (k == 1 ? lo : 0x80) || s[i + k] > (k == 1 ? hi : 0xBF)) return false;
        }
        i += (size_t)need + 1;
    }
    return true;
}

/* HTML's "extracting a character encoding from a meta element" on a content attribute or a Content-Type value */
static int charset_in(const char *s, size_t n) {
    size_t i = 0;
    for (;;) {
        for (; i + 7 <= n; i++) if (!strncasecmp(s + i, "charset", 7)) break;
        if (i + 7 > n) return -1;
        i += 7;
        while (i < n && ascii_ws((unsigned char)s[i])) i++;
        if (i < n && s[i] == '=') break;
    }
    i++;
    while (i < n && ascii_ws((unsigned char)s[i])) i++;
    if (i >= n) return -1;
    if (s[i] == '"' || s[i] == '\'') {
        char qc = s[i++];
        size_t a = i;
        while (i < n && s[i] != qc) i++;
        return i < n ? enc_label(s + a, i - a) : -1;
    }
    size_t a = i;
    while (i < n && !ascii_ws((unsigned char)s[i]) && s[i] != ';') i++;
    return enc_label(s + a, i - a);
}

int enc_from_content_type(const char *ct) {
    if (!ct) return -1;
    const char *semi = strchr(ct, ';');
    if (!semi) return -1;
    for (const char *p = semi; *p;) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        const char *eq = p;
        while (*eq && *eq != '=' && *eq != ';') eq++;
        if (*eq != '=') { p = eq; if (!*p) break; continue; }
        bool cs = eq - p == 7 && !strncasecmp(p, "charset", 7);
        const char *v = eq + 1, *ve;
        char buf[64];
        size_t bn = 0;
        if (*v == '"') {
            for (ve = v + 1; *ve && *ve != '"'; ve++) { if (*ve == '\\' && ve[1]) ve++; if (bn < sizeof buf - 1) buf[bn++] = *ve; }
            if (*ve) ve++;
            while (*ve && *ve != ';') ve++;
        } else {
            for (ve = v; *ve && *ve != ';'; ve++) if (bn < sizeof buf - 1) buf[bn++] = *ve;
            while (bn && (buf[bn - 1] == ' ' || buf[bn - 1] == '\t')) bn--;
        }
        if (cs) return enc_label(buf, bn);
        p = ve;
        if (!*p) break;
    }
    return -1;
}

/* the prescan's "get an attribute": returns false at the end of the tag; name and value are lower-cased copies */
static bool get_attr(const uint8_t *s, size_t n, size_t *ip, char *name, size_t nmax, char *val, size_t vmax) {
    size_t i = *ip, nn = 0, vn = 0;
    while (i < n && (ascii_ws(s[i]) || s[i] == '/')) i++;
    if (i >= n || s[i] == '>') { *ip = i; return false; }
    for (;; i++) {
        if (i >= n) { *ip = i; return false; }
        uint8_t c = s[i];
        if (c == '=' && nn) { i++; goto value; }
        if (ascii_ws(c)) break;
        if (c == '/' || c == '>') { name[nn] = 0; val[0] = 0; *ip = i; return true; }
        if (nn < nmax - 1) name[nn++] = (char)tolower(c);
    }
    while (i < n && ascii_ws(s[i])) i++;
    if (i >= n || s[i] != '=') { name[nn] = 0; val[0] = 0; *ip = i; return true; }
    i++;
value:
    name[nn] = 0;
    while (i < n && ascii_ws(s[i])) i++;
    if (i < n && (s[i] == '"' || s[i] == '\'')) {
        uint8_t q = s[i++];
        while (i < n && s[i] != q) { if (vn < vmax - 1) val[vn++] = (char)tolower(s[i]); i++; }
        if (i < n) i++;
    } else {
        if (i < n && s[i] == '>') { val[0] = 0; *ip = i; return true; }
        while (i < n && !ascii_ws(s[i]) && s[i] != '>') { if (vn < vmax - 1) val[vn++] = (char)tolower(s[i]); i++; }
    }
    val[vn] = 0;
    *ip = i;
    return true;
}

int enc_prescan(const uint8_t *s, size_t n) {
    if (n > 1024) n = 1024;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != '<') continue;
        if (i + 4 <= n && !memcmp(s + i, "<!--", 4)) {
            size_t j = i + 2;
            while (j + 3 <= n && memcmp(s + j, "-->", 3)) j++;
            i = j + 2;
            continue;
        }
        if (i + 6 <= n && !strncasecmp((const char *)s + i, "<meta", 5) && (ascii_ws(s[i + 5]) || s[i + 5] == '/')) {
            size_t j = i + 6;
            bool got_pragma = false, seen_he = false, seen_ct = false, seen_cs = false;
            int need_pragma = 0, charset = -1;   /* need_pragma: 0 null, 1 false, 2 true */
            char name[32], val[256];
            while (get_attr(s, n, &j, name, sizeof name, val, sizeof val)) {
                if (!strcmp(name, "http-equiv") && !seen_he) { seen_he = true; if (!strcmp(val, "content-type")) got_pragma = true; }
                else if (!strcmp(name, "content") && !seen_ct) {
                    seen_ct = true;
                    if (charset < 0) { int c = charset_in(val, strlen(val)); if (c >= 0) { charset = c; need_pragma = 2; } }
                } else if (!strcmp(name, "charset") && !seen_cs) { seen_cs = true; charset = enc_label(val, strlen(val)); need_pragma = 1; }
            }
            if (j >= n) return -1;
            if (need_pragma && !(need_pragma == 2 && !got_pragma) && charset >= 0) {
                if (charset == ENC_UTF_16BE || charset == ENC_UTF_16LE) charset = ENC_UTF_8;
                if (charset == ENC_X_USER_DEFINED) charset = ENC_WINDOWS_1252;
                return charset;
            }
            i = j;
            continue;
        }
        if (i + 2 < n && (isalpha(s[i + 1]) || (s[i + 1] == '/' && isalpha(s[i + 2])))) {
            size_t j = i + 1;
            while (j < n && !ascii_ws(s[j]) && s[j] != '>') j++;
            char name[32], val[256];
            while (get_attr(s, n, &j, name, sizeof name, val, sizeof val)) {}
            i = j;
            continue;
        }
        if (i + 1 < n && (s[i + 1] == '!' || s[i + 1] == '/' || s[i + 1] == '?')) {
            size_t j = i + 1;
            while (j < n && s[j] != '>') j++;
            i = j;
        }
    }
    return -1;
}

int enc_sniff(const uint8_t *s, size_t n, const char *content_type, int *bom_len) {
    *bom_len = 0;
    if (n >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) { *bom_len = 3; return ENC_UTF_8; }
    if (n >= 2 && s[0] == 0xFE && s[1] == 0xFF) { *bom_len = 2; return ENC_UTF_16BE; }
    if (n >= 2 && s[0] == 0xFF && s[1] == 0xFE) { *bom_len = 2; return ENC_UTF_16LE; }
    int e = enc_from_content_type(content_type);
    if (e >= 0) return e;
    if ((e = enc_prescan(s, n)) >= 0) return e;
    return enc_valid_utf8(s, n) ? ENC_UTF_8 : ENC_WINDOWS_1252;
}

/* the XML declaration's encoding="..." */
static int xml_decl_encoding(const uint8_t *s, size_t n) {
    if (n < 6 || memcmp(s, "<?xml", 5) || !ascii_ws(s[5])) return -1;
    size_t end = 6;
    while (end + 1 < n && end < 1024 && !(s[end] == '?' && s[end + 1] == '>')) end++;
    for (size_t i = 6; i + 8 < end; i++) {
        if (memcmp(s + i, "encoding", 8)) continue;
        size_t j = i + 8;
        while (j < end && ascii_ws(s[j])) j++;
        if (j >= end || s[j] != '=') return -1;
        j++;
        while (j < end && ascii_ws(s[j])) j++;
        if (j >= end || (s[j] != '"' && s[j] != '\'')) return -1;
        uint8_t q = s[j++];
        size_t a = j;
        while (j < end && s[j] != q) j++;
        return enc_label((const char *)s + a, j - a);
    }
    return -1;
}

char *enc_document(const uint8_t *s, size_t n, const char *content_type, bool xml, int *enc, size_t *out_len) {
    int bom = 0, e;
    if (xml) {
        if (n >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) { bom = 3; e = ENC_UTF_8; }
        else if (n >= 2 && s[0] == 0xFE && s[1] == 0xFF) { bom = 2; e = ENC_UTF_16BE; }
        else if (n >= 2 && s[0] == 0xFF && s[1] == 0xFE) { bom = 2; e = ENC_UTF_16LE; }
        else if ((e = enc_from_content_type(content_type)) < 0 && (e = xml_decl_encoding(s, n)) < 0) e = ENC_UTF_8;
    } else e = enc_sniff(s, n, content_type, &bom);
    *enc = e;
    if (e == ENC_UTF_8 && !bom) return NULL;
    return enc_decode_all(e, s + bom, n - (size_t)bom, out_len);
}
