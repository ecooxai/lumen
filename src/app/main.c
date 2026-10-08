/* Lumen: SDL3 window, minimalist chrome, navigation, wgpu/CPU presentation */
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <SDL3/SDL.h>
#include "../paint/paint.h"
#include "../media/media.h"
#include "../net/net.h"
#include "../base/url.h"
#include "../gpu/gpu.h"
#include "../js/js.h"
#include "../js/jsglue.h"

#define TABH 30.f
#define TB 44.f
static float g_info_h;
#define INFOH 36.f
#define BAR (TABH + TB + g_info_h)
#ifdef __APPLE__
#include "macui.h"
#define TLW 78.f
#else
#define TLW 0.f
#endif
#define SIDEW 84.f
#define WSY0 (TABH + 8)
#define WSRH 40.f

typedef struct { Node *n; char *src; size_t len; char *name; } PScript;
typedef struct { Node *n; uint64_t h; } SheetRef;
typedef struct Page {
    char *url; Document *d; StyleEngine *e; Layout *L; uint64_t gen; double load_ms;
    JsCtx *js; PScript *scripts; int nscripts; uint64_t seen_ver;
    SheetRef *sref; int nsref;
    HMap img_fired; bool img_check;
    /* <iframe> child browsing context (document owned by the JS layer) */
    Node *frame_el; float fw, fh, fsy; Canvas cv; Image img; DisplayList fdl;
    uint64_t drawn_ver; int drawn_epoch; bool frelayout; struct Page *fnext;
} Page;
static Page *g_frames;
static Page *frame_find(Node *f) { for (Page *p = g_frames; p; p = p->fnext) if (p->frame_el == f) return p; return NULL; }
typedef struct { Image *im; bool done, evicted, refetch; double used; float ew, eh; } ImgSlot;

static SDL_Mutex *icache_mu;
static HMap icache;
static size_t g_icache_bytes; static int g_icache_n;
static int g_img_epoch;
static Uint32 EV_LOADED, EV_NET, EV_MENU;
static uint64_t load_gen;
static bool g_lowmem;
static double g_full_paint; static bool g_in_layout;
#define MAX_TABS 32
static volatile uint64_t g_tab_gen[MAX_TABS];
static bool gen_live(uint64_t g) { for (int i = 0; i < MAX_TABS; i++) if (g_tab_gen[i] == g) return true; return false; }

static void img_refetch(const char *u);
static Image *cache_get(const char *u) {
    SDL_LockMutex(icache_mu); ImgSlot *s = hm_get(&icache, u); Image *im = s ? s->im : NULL; bool want = false;
    if (s && !g_in_layout) { s->used = (double)SDL_GetTicks(); if (s->evicted && !s->refetch) s->refetch = want = true; }
    SDL_UnlockMutex(icache_mu);
    if (want) img_refetch(u);
    return im;
}
static void cache_fetch(const char *u) {
    if (!u || !*u || !strncmp(u, "blob:", 5)) return;
    SDL_LockMutex(icache_mu); bool have = hm_get(&icache, u) != NULL; SDL_UnlockMutex(icache_mu);
    if (have) return;
    NetResponse *r = net_fetch_sync(net_request_new("GET", u));
    Image *im = r && r->status == 200 ? image_decode((const uint8_t *)r->body, r->body_len) : NULL;
    if (r) net_response_free(r);
    ImgSlot *slot = xcalloc(1, sizeof *slot); slot->im = im; slot->done = true;
    SDL_LockMutex(icache_mu); if (!hm_get(&icache, u)) { hm_put(&icache, u, slot); if (im) { g_icache_bytes += (size_t)im->w * (size_t)im->h * 4; g_icache_n++; } } else { image_unref(im); free(slot); } SDL_UnlockMutex(icache_mu);
}
static Image *node_img(Node *n) {
    if (n->tag == A_iframe) { Page *fp = frame_find(n); return fp && fp->cv.px ? &fp->img : NULL; }
    if (n->tag == A_video) { Image *f = media_frame_for(n); if (f) return f; }
    const char *s = node_attr(n, n->tag == A_video ? "poster" : "src"); if (!s) return NULL;
    char *u = url_join(n->doc->url, s); Image *im = cache_get(u); free(u); return im;
}
static Image *url_img(const char *u) { return u ? cache_get(u) : NULL; }
static bool img_size(Node *n, float *w, float *h) {
    if (n->tag == A_iframe) return false;
    g_in_layout = true; Image *im = node_img(n); g_in_layout = false;
    if (!im) {
        const char *s = n->tag == A_img && g_lowmem ? node_attr(n, "src") : NULL; char *u = s ? url_join(n->doc->url, s) : NULL;
        SDL_LockMutex(icache_mu); ImgSlot *sl = u ? hm_get(&icache, u) : NULL; bool ok = sl && sl->evicted; if (ok) { *w = sl->ew; *h = sl->eh; } SDL_UnlockMutex(icache_mu);
        free(u); return ok;
    }
    *w = image_css_w(im); *h = image_css_h(im); return true; }

typedef struct { char *url; uint64_t gen; float vw, vh; char *body; size_t blen; char *ctype; } LoadReq;

static int loader(void *arg) {
    LoadReq *rq = arg; double t0 = now_ms();
    NetRequest *nr = net_request_new(rq->body ? "POST" : "GET", rq->url);
    if (rq->body) {
        nr->body = rq->body; nr->body_len = rq->blen; rq->body = NULL;
        headers_set(&nr->headers, "Content-Type", rq->ctype && *rq->ctype ? rq->ctype : "application/x-www-form-urlencoded");
    }
    NetResponse *r = net_fetch_sync(nr);
    Page *p = xcalloc(1, sizeof *p); p->gen = rq->gen;
    const char *body = r && r->body ? r->body : "";
    size_t blen = r && r->body ? r->body_len : 0;
    char *errbuf = NULL;
    if (!r || r->status == 0) { errbuf = xmalloc(512); snprintf(errbuf, 512, "<title>Error</title><body style='font:15px system-ui;padding:40px;color:#444'><h2>Can't open this page</h2><p>%s</p>", rq->url); body = errbuf; blen = strlen(errbuf); }
    p->url = xstrdup(r && r->url ? r->url : rq->url);
    p->d = doc_new(p->url);
    html_parse(p->d, body, blen);
    if (!getenv("LUMEN_NO_JS")) for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (!gen_live(rq->gen)) break;
        if (n->type != NODE_ELEMENT || n->tag != A_script || n->ns != NS_HTML || !jsg_classic_script(n)) continue;
        PScript sc = { n, NULL, 0, NULL };
        const char *src = node_attr(n, "src");
        if (src) {
            sc.name = url_join(p->d->url, src);
            if (!sc.name) continue;
            NetResponse *sr = net_fetch_sync(net_request_new("GET", sc.name));
            if (sr && sr->status >= 200 && sr->status < 300) { sc.src = xstrndup(sr->body ? sr->body : "", sr->body_len); sc.len = sr->body_len; }
            if (sr) net_response_free(sr);
        } else { sc.src = node_text_content(n); sc.len = strlen(sc.src); sc.name = xstrdup(p->url); }
        p->scripts = xrealloc(p->scripts, sizeof *p->scripts * (size_t)(p->nscripts + 1)); p->scripts[p->nscripts++] = sc;
    }
    p->e = style_engine_new(p->d); p->e->media.vw = rq->vw; p->e->media.vh = rq->vh;
    for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (!gen_live(rq->gen)) break;
        if (n->type != NODE_ELEMENT) continue;
        if (n->tag == A_style) {
            char *t = node_text_content(n); StyleSheet *s = css_parse_sheet(t, strlen(t), p->d->url, 1, &p->e->media); s->owner = n; style_engine_add_sheet(p->e, s); free(t);
        } else if (n->tag == A_link && node_attr(n, "rel") && strstr(node_attr(n, "rel"), "stylesheet") && node_attr(n, "href")) {
            char *u = url_join(p->d->url, node_attr(n, "href"));
            NetResponse *cr = net_fetch_sync(net_request_new("GET", u));
            if (cr && cr->status == 200) { StyleSheet *s = css_parse_sheet(cr->body, cr->body_len, cr->url, 1, &p->e->media); s->owner = n; style_engine_add_sheet(p->e, s); }
            if (cr) net_response_free(cr); free(u);
        } else if (n->tag == A_img && node_attr(n, "src")) {
            char *u = url_join(p->d->url, node_attr(n, "src")); cache_fetch(u); free(u);
        }
    }
    style_recalc(p->e, &p->d->node, true);
    p->seen_ver = p->d->dom_version;
    p->load_ms = now_ms() - t0;
    if (r) net_response_free(r);
    free(errbuf); free(rq->url); free(rq->body); free(rq->ctype); free(rq);
    SDL_Event ev; SDL_zero(ev); ev.type = EV_LOADED; ev.user.data1 = p; SDL_PushEvent(&ev);
    return 0;
}

static void page_free(Page *p) {
    if (!p) return;
    js_free(p->js);
    hm_free(&p->img_fired, NULL);
    free(p->sref);
    for (int i = 0; i < p->nscripts; i++) { free(p->scripts[i].src); free(p->scripts[i].name); }
    free(p->scripts);
    if (p->L) layout_free(p->L);
    if (p->e) style_engine_free(p->e);
    if (p->d) doc_free(p->d);
    free(p->url); free(p);
}

typedef struct Tab { Page *cur; bool loading, relayout; char *hist[256]; uint64_t hgen[256]; int nhist, hpos; char url[2048]; float sy; uint64_t lgen; int ws;
    double cpu_ms, media_ms0, budget, bud_t, unlimit_until; float pct, cpuhist[60]; int ncpu, cpui; bool limited, info; } Tab;
typedef struct App {
    SDL_Window *win; SDL_MetalView mview; Gpu *gpu;
    int pw, ph; float scale, vw, vh;
    Canvas frame, page; DisplayList pdl, cdl;
    Tab *t, *tabs[MAX_TABS]; int ntabs, ti;
    char wsname[16][64]; Tab *wslast[16]; int nws, wi; float side;
    int wsicon[16]; Image *icimg[16][2];
    int tip_tab; double tip_until;     /* CPU-limit dot tooltip pinned by a click */
    bool editing; int sel_all;
    double caret_t;
    bool dirty;
    Font *ui;
    int hover; double frame_ms;
    bool vonly; float last_sy; uint64_t last_ver; Page *last_page; bool gvid_ok;
} App;
static void publish_gens(App *a) { for (int i = 0; i < MAX_TABS; i++) g_tab_gen[i] = i < a->ntabs ? a->tabs[i]->lgen : 0; }
static Tab *tab_new(App *a) {
    if (a->ntabs == MAX_TABS) return NULL;
    Tab *t = xcalloc(1, sizeof *t); t->hpos = -1; t->ws = a->wi; a->tabs[a->ntabs++] = t;
    if (!a->t) a->t = t;
    return t;
}
#define TAB_CALL(ud, expr) do { Tab *o_ = g_app->t; g_app->t = (Tab *)(ud); expr; g_app->t = o_; } while (0)


static char *normalize_url(const char *in) {
    while (*in == ' ') in++;
    if (strstr(in, "://") || !strncmp(in, "about:", 6) || !strncmp(in, "data:", 5)) return xstrdup(in);
    if (!strchr(in, ' ') && strchr(in, '.')) { char *o = xmalloc(strlen(in) + 9); sprintf(o, "https://%s", in); return o; }
    size_t n = strlen(in); char *o = xmalloc(n * 3 + 40), *w = o + sprintf(o, "https://www.google.com/search?q=");
    for (const unsigned char *s = (const unsigned char *)in; *s; s++) {
        if (isalnum(*s) || strchr("-_.~", *s)) *w++ = (char)*s; else if (*s == ' ') *w++ = '+'; else w += sprintf(w, "%%%02X", *s);
    }
    *w = 0; return o;
}

static void navigate_ex(App *a, const char *url, bool push, const char *body, size_t blen, const char *ctype) {
    char *u = normalize_url(url);
    if (push) {
        for (int i = a->t->hpos + 1; i < a->t->nhist; i++) free(a->t->hist[i]);
        a->t->nhist = a->t->hpos + 1;
        if (a->t->nhist == 256) { free(a->t->hist[0]); memmove(a->t->hist, a->t->hist + 1, sizeof(char *) * 255); memmove(a->t->hgen, a->t->hgen + 1, sizeof(uint64_t) * 255); a->t->nhist--; }
        a->t->hist[a->t->nhist++] = xstrdup(u); a->t->hpos = a->t->nhist - 1;
    }
    snprintf(a->t->url, sizeof a->t->url, "%s", u);
    a->editing = false; a->t->loading = true; a->dirty = true;
    LoadReq *rq = xcalloc(1, sizeof *rq); rq->url = u; rq->gen = ++load_gen; rq->vw = a->vw - a->side; rq->vh = a->vh - BAR; a->t->lgen = rq->gen; publish_gens(a);
    if (push) a->t->hgen[a->t->hpos] = rq->gen;
    if (body) { rq->body = xmalloc(blen + 1); memcpy(rq->body, body, blen); rq->body[blen] = 0; rq->blen = blen; rq->ctype = xstrdup(ctype ? ctype : ""); }
    SDL_Thread *t = SDL_CreateThread(loader, "loader", rq); SDL_DetachThread(t);
}
static void navigate(App *a, const char *url, bool push) { navigate_ex(a, url, push, NULL, 0, NULL); }

static void push_rect(DisplayList *dl, float x, float y, float w, float h, float r, Color c) {
    DItem it; memset(&it, 0, sizeof it); it.op = DO_RECT; it.x = x; it.y = y; it.w = w; it.h = h;
    for (int i = 0; i < 4; i++) it.r[i] = r;
    it.color = c; vec_push(dl->items, it);
}
static float push_text(DisplayList *dl, Font *f, const char *s, float x, float base, float maxw, Color c) {
    ShapedRun sr; memset(&sr, 0, sizeof sr); text_shape(f, s, strlen(s), 0, &sr);
    DItem it; memset(&it, 0, sizeof it); it.op = DO_TEXT; it.color = c;
    it.g = arena_alloc(&dl->arena, sizeof(DGlyph) * (size_t)(sr.n + 1));
    float w = 0;
    for (int i = 0; i < sr.n; i++) {
        if (sr.g[i].x + sr.g[i].adv > maxw) break;
        DGlyph *g = &it.g[it.ng++]; g->gid = sr.g[i].gid; g->font = sr.g[i].font; g->x = x + sr.g[i].x; g->y = base + sr.g[i].y;
        w = sr.g[i].x + sr.g[i].adv;
    }
    it.x = x; it.y = base - f->ascent; it.w = w; it.h = f->ascent + f->descent;
    vec_push(dl->items, it); shaped_free(&sr);
    return w;
}

enum { HB_NONE, HB_BACK, HB_FWD, HB_RELOAD, HB_URL, HB_NEWTAB, HB_WSNEW, HB_TAB = 100, HB_TABX = 200, HB_WS = 300, HB_INFO = 400, HB_TABDOT = 500 };
static void info_btn(App *a, int k, float *x, float *w) {
    static const float W[4] = { 70, 76, 82, 28 }; float r = a->vw - 12;
    for (int i = 3; i >= k; i--) r -= W[i] + (i < 3 ? 6 : 0);
    *x = r; *w = W[k];
}
static int utf8_len(const char *q) { unsigned char c = (unsigned char)*q; int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4; for (int k = 1; k < n; k++) if (!q[k]) return k; return n; }
static Image *ws_icon(App *a, int ic, bool on) {
#ifdef __APPLE__
    if (ic < 0 || ic >= MAC_NICONS) return NULL;
    Image **slot = &a->icimg[ic][on];
    if (!*slot) {
        int px = (int)(18 * a->scale + 0.5f); Image *im = xcalloc(1, sizeof *im);
        im->w = im->h = px; im->refs = 1; im->scale = a->scale; im->px = xcalloc((size_t)px * (size_t)px, 4);
        if (!mac_icon_rgba(ic, px, on ? 0x141414 : 0x5a5e64, im->px)) { free(im->px); free(im); return NULL; }
        *slot = im;
    }
    return *slot;
#else
    (void)a; (void)ic; (void)on; return NULL;
#endif
}
static int ws_tabs(App *a, int w, int *out) { int n = 0; for (int i = 0; i < a->ntabs; i++) if (a->tabs[i]->ws == w) out[n++] = i; return n; }
static float tab_w(App *a) { int v[MAX_TABS], n = ws_tabs(a, a->wi, v); float w = (a->vw - TLW - 48) / (n ? n : 1); return w > 220 ? 220 : w; }
static int bar_hit(App *a, float x, float y) {
    if (y < TABH) {
        int v[MAX_TABS], n = ws_tabs(a, a->wi, v), k; float tw = tab_w(a), x0 = TLW + 6; k = (int)((x - x0) / tw);
        if (x >= x0 && k < n) { float lx = x - x0 - k * tw; return lx > tw - 28 ? HB_TABX + v[k] : a->tabs[v[k]]->limited && lx < 26 ? HB_TABDOT + v[k] : HB_TAB + v[k]; }
        if (x >= x0 + n * tw && x < x0 + n * tw + 32) return HB_NEWTAB;
        return HB_NONE;
    }
    if (x < a->side) {
        int r = (int)floorf((y - WSY0) / WSRH);
        if (y >= WSY0 && r < a->nws) return HB_WS + r;
        return y >= WSY0 && r == a->nws ? HB_WSNEW : HB_NONE;
    }
    if (y > BAR) return HB_NONE;
    if (y >= TABH + TB) { for (int k = 0; k < 4; k++) { float bx, bw; info_btn(a, k, &bx, &bw); if (x >= bx && x < bx + bw) return HB_INFO + k; } return HB_NONE; }
    x -= a->side;
    if (x < 40) return HB_BACK; if (x < 72) return HB_FWD; if (x < 104) return HB_RELOAD;
    return HB_URL;
}

static void draw_icon(DisplayList *dl, int kind, float cx, float cy, Color c) {
    if (kind == HB_BACK || kind == HB_FWD) {
        float d = kind == HB_BACK ? -1 : 1;
        for (int i = 0; i < 6; i++) { push_rect(dl, cx - d * (i * 0.9f) - 1, cy - i - 1, 2, 2, 1, c); push_rect(dl, cx - d * (i * 0.9f) - 1, cy + i - 1, 2, 2, 1, c); }
    } else {
        for (int i = 0; i < 28; i++) { float t = (float)i / 28 * 5.4f + 0.6f; push_rect(dl, cx + 6 * sinf(t) - 1, cy - 6 * cosf(t) - 1, 2, 2, 1, c); }
        push_rect(dl, cx - 1, cy - 8, 4, 2, 1, c); push_rect(dl, cx + 1, cy - 9, 2, 4, 1, c);
    }
}

static void chrome_tip(App *a) {   /* tooltip for a tab's CPU-limit dot (hover or click) */
    int i = a->hover >= HB_TABDOT ? a->hover - HB_TABDOT : a->tip_until ? a->tip_tab : -1;
    if (i < 0 || i >= a->ntabs || !a->tabs[i]->limited || a->tabs[i]->ws != a->wi) return;
    int vt[MAX_TABS], n = ws_tabs(a, a->wi, vt), k = 0; while (k < n && vt[k] != i) k++;
    const char *msg = "CPU use of this tab is limited to 40%";
    float tw = tab_w(a), w = text_width(a->ui, msg, strlen(msg), 0) + 20, x = TLW + 6 + k * tw + 15 - w / 2, y = TABH + 3;
    if (x + w > a->vw - 6) x = a->vw - 6 - w;
    if (x < 6) x = 6;
    push_rect(&a->cdl, x, y, w, 26, 6, RGBA(45, 45, 48, 240));
    push_text(&a->cdl, a->ui, msg, x + 10, y + 13 + (a->ui->ascent - a->ui->descent) / 2, w, RGBA(255, 255, 255, 255));
}
static void build_chrome(App *a) {
    DisplayList *dl = &a->cdl; dl_clear(dl);
    push_rect(dl, 0, 0, a->vw, TABH, 0, RGBA(222, 225, 230, 255));
    int vt[MAX_TABS], nvt = ws_tabs(a, a->wi, vt);
    float tabw = tab_w(a), tx0 = TLW + 6, tbase = 4 + (TABH - 4) / 2 + (a->ui->ascent - a->ui->descent) / 2;
    for (int k = 0; k < nvt; k++) {
        int i = vt[k]; Tab *t = a->tabs[i]; float x = tx0 + k * tabw;
        if (i == a->ti) push_rect(dl, x, 4, tabw - 2, TABH - 4, 7, RGBA(250, 250, 250, 255));
        else if (a->hover == HB_TAB + i || a->hover == HB_TABX + i) push_rect(dl, x, 6, tabw - 2, TABH - 10, 6, RGBA(235, 237, 240, 255));
        else if (k + 1 < nvt && vt[k + 1] != a->ti) push_rect(dl, x + tabw - 2, 9, 1, TABH - 16, 0, RGBA(170, 175, 180, 255));
        const char *title = t->cur && t->cur->d && t->cur->d->title && *t->cur->d->title ? t->cur->d->title : *t->url ? t->url : "New Tab";
        if (t->limited) push_rect(dl, x + 10, (4 + TABH) / 2 - 5, 10, 10, 5, RGBA(52, 199, 89, 255));
        push_text(dl, a->ui, title, x + (t->limited ? 26 : 12), tbase, tabw - (t->limited ? 54 : 40), RGBA(40, 40, 40, 255));
        push_text(dl, a->ui, "\xC3\x97", x + tabw - 22, tbase, 16, a->hover == HB_TABX + i ? RGBA(20, 20, 20, 255) : RGBA(110, 110, 110, 255));
        if (t->loading) push_rect(dl, x + 8, TABH - 3, (tabw - 18) * 0.35f, 2, 1, RGBA(66, 133, 244, 255));
    }
    float npx = tx0 + nvt * tabw + 8;
    if (a->hover == HB_NEWTAB) push_rect(dl, npx - 4, 6, 24, 20, 10, RGBA(235, 237, 240, 255));
    push_text(dl, a->ui, "+", npx + 4, tbase, 16, RGBA(60, 60, 60, 255));
    if (a->side > 0) {
        float rb = (a->ui->ascent - a->ui->descent) / 2, bw = a->side - 20;
        push_rect(dl, 0, TABH, a->side, a->vh - TABH, 0, RGBA(236, 238, 241, 255));
        push_rect(dl, a->side - 1, TABH, 1, a->vh - TABH, 0, RGBA(214, 217, 222, 255));
        for (int w = 0; w <= a->nws; w++) {
            float y = WSY0 + w * WSRH;
            bool on = w == a->wi, hov = w < a->nws ? a->hover == HB_WS + w : a->hover == HB_WSNEW;
            char lab[16] = "+"; int ln = 1;
            if (w < a->nws) {
                const char *nm = a->wsname[w], *sp = strchr(nm, ' '); ln = 0;
                int c1 = utf8_len(nm); memcpy(lab, nm, (size_t)c1); ln = c1; lab[0] = (char)toupper((unsigned char)lab[0]);
                const char *q2 = sp && sp[1] ? sp + 1 : nm[c1] ? nm + c1 : NULL;
                if (q2) { int c2 = utf8_len(q2); memcpy(lab + ln, q2, (size_t)c2); ln += c2; }
                lab[ln] = 0;
            }
            float lw = text_width(a->ui, lab, strlen(lab), 0);
            if (on || hov || w == a->nws) push_rect(dl, 10, y + 3, bw, WSRH - 6, 8, on ? RGBA(255, 255, 255, 255) : hov ? RGBA(222, 225, 230, 255) : RGBA(236, 238, 241, 255));
            if (on) push_rect(dl, 4, y + 12, 3, WSRH - 24, 1, RGBA(66, 133, 244, 255));
            Image *ici = w < a->nws ? ws_icon(a, a->wsicon[w], on) : NULL;
            if (ici) { DItem it; memset(&it, 0, sizeof it); it.op = DO_IMAGE; it.x = 10 + (bw - 18) / 2; it.y = y + (WSRH - 18) / 2; it.w = it.h = 18; it.img = ici; it.alpha = 1; vec_push(dl->items, it); }
            else push_text(dl, a->ui, lab, 10 + (bw - lw) / 2, y + WSRH / 2 + rb, bw, on ? RGBA(20, 20, 20, 255) : RGBA(90, 94, 100, 255));
        }
    }
    if (g_info_h > 0) {
        float y0 = TABH + TB, rb = (a->ui->ascent - a->ui->descent) / 2, cy = y0 + g_info_h / 2 + rb;
        push_rect(dl, a->side, y0, a->vw - a->side, g_info_h, 0, RGBA(254, 247, 224, 255));
        push_rect(dl, a->side, y0 + g_info_h - 1, a->vw - a->side, 1, 0, RGBA(230, 214, 160, 255));
        push_text(dl, a->ui, "This tab used over 80% CPU for 60 s, so Lumen limited it to 40%. Remove the limit for:", a->side + 14, cy, a->vw - a->side - 310, RGBA(60, 50, 20, 255));
        static const char *lb[4] = { "1 hour", "4 hours", "10 hours", "\xC3\x97" };
        for (int k = 0; k < 4; k++) {
            float bx, bwid; info_btn(a, k, &bx, &bwid);
            if (k < 3 || a->hover == HB_INFO + k) push_rect(dl, bx, y0 + 6, bwid, g_info_h - 12, 6, a->hover == HB_INFO + k ? RGBA(240, 228, 190, 255) : RGBA(255, 255, 255, 255));
            float lw = text_width(a->ui, lb[k], strlen(lb[k]), 0);
            push_text(dl, a->ui, lb[k], bx + (bwid - lw) / 2, cy, bwid, RGBA(60, 50, 20, 255));
        }
    }
    int n0 = dl->items.n;
    push_rect(dl, 0, 0, (a->vw - a->side), TB, 0, RGBA(250, 250, 250, 255));
    push_rect(dl, 0, TB - 1, (a->vw - a->side), 1, 0, RGBA(226, 226, 226, 255));
    Color on = RGBA(60, 60, 60, 255), off = RGBA(190, 190, 190, 255);
    int hk[3] = { HB_BACK, HB_FWD, HB_RELOAD };
    for (int i = 0; i < 3; i++) {
        float cx = 22.f + i * 32.f;
        if (a->hover == hk[i]) push_rect(dl, cx - 13, TB / 2 - 13, 26, 26, 13, RGBA(232, 232, 232, 255));
        bool en = hk[i] == HB_BACK ? a->t->hpos > 0 : hk[i] == HB_FWD ? a->t->hpos < a->t->nhist - 1 : true;
        draw_icon(dl, hk[i], cx, TB / 2, en ? on : off);
    }
    float ux = 112, uw = (a->vw - a->side) - ux - 12;
    push_rect(dl, ux, 7, uw, TB - 14, (TB - 14) / 2, a->editing ? RGBA(255, 255, 255, 255) : RGBA(238, 238, 238, 255));
    if (a->editing) {
        DItem it; memset(&it, 0, sizeof it); it.op = DO_BORDER; it.x = ux; it.y = 7; it.w = uw; it.h = TB - 14;
        for (int i = 0; i < 4; i++) { it.r[i] = (TB - 14) / 2; it.bw[i] = 1.5f; it.bc[i] = RGBA(66, 133, 244, 255); it.bs[i] = 1; }
        vec_push(dl->items, it);
    }
    const char *shown = a->t->url;
    if (!a->editing) { const char *p = strstr(shown, "://"); if (p) shown = p + 3; }
    float base = TB / 2 + (a->ui->ascent - a->ui->descent) / 2;
    if (a->editing && a->sel_all && *shown) {
        float w = text_width(a->ui, shown, strlen(shown), 0);
        push_rect(dl, ux + 16, 12, LMIN(w, uw - 32), TB - 24, 2, RGBA(200, 220, 255, 255));
    }
    float tw = push_text(dl, a->ui, shown, ux + 16, base, uw - 32, a->editing ? RGBA(20, 20, 20, 255) : RGBA(90, 90, 90, 255));
    if (a->editing && !a->sel_all) push_rect(dl, ux + 16 + tw + 1, 13, 1.5f, TB - 26, 0, RGBA(20, 20, 20, 255));
    if (a->t->loading) push_rect(dl, 0, TB - 2, (a->vw - a->side) * 0.35f, 2, 0, RGBA(66, 133, 244, 255));
    for (int i = n0; i < dl->items.n; i++) { DItem *it = &dl->items.v[i]; it->y += TABH; it->x += a->side; if (it->op == DO_TEXT) for (int g = 0; g < it->ng; g++) { it->g[g].y += TABH; it->g[g].x += a->side; } }
}

static float max_scroll(App *a) { return a->t->cur && a->t->cur->L ? LMAX(0, a->t->cur->L->doc_h - (a->vh - BAR)) : 0; }

static void render(App *a) {
    double t0 = now_ms();
    int bar_px = (int)(BAR * a->scale);
    if (a->frame.w != a->pw || a->frame.h != a->ph) { canvas_free(&a->frame); canvas_init(&a->frame, a->pw, a->ph, a->scale); }
    int ph = a->ph - bar_px; if (ph < 1) ph = 1;
    int side_px = (int)(a->side * a->scale), pwp = a->pw - side_px; if (pwp < 1) pwp = 1;
    int why = !a->vonly ? 1 : !a->t->cur || a->t->cur != a->last_page || !a->t->cur->L ? 2 : a->t->relayout ? 3 : a->t->cur->d->dom_version != a->last_ver ? 4 : 0;
    bool part = !why;
    int rx0 = pwp, ry0 = ph, rx1 = 0, ry1 = 0;
    if (a->page.w != pwp || a->page.h != ph) { canvas_free(&a->page); canvas_init(&a->page, pwp, ph, a->scale); part = false; }
    bool gskip = false; GpuVideo gvd, *gvp = NULL;
    if (a->t->cur) {
        if (a->t->relayout || !a->t->cur->L) {
            if (!a->t->cur->L) a->t->cur->L = layout_new();
            a->t->cur->e->media.vw = a->vw - a->side; a->t->cur->e->media.vh = a->vh - BAR;
            layout_run(a->t->cur->L, a->t->cur->d, a->vw - a->side, a->vh - BAR);
            a->t->relayout = false;
        }
        a->t->sy = LCLAMP(a->t->sy, 0, max_scroll(a));
        if (a->t->sy != a->last_sy && part) { part = false; why = 5; }
        g_full_paint = (double)SDL_GetTicks(); dl_clear(&a->pdl); dl_build(&a->pdl, a->t->cur->L, 0, a->t->sy, a->vw - a->side, a->vh - BAR);
        const DItem *vit = NULL; int nv = 0;
        for (int i = 0; i < a->pdl.items.n; i++) {
            const DItem *it = &a->pdl.items.v[i];
            if (it->op != DO_IMAGE || !media_is_frame(it->img)) continue;
            if (!nv++) vit = it;
            float s = a->page.scale;
            rx0 = LMIN(rx0, (int)floorf(it->x * s)); ry0 = LMIN(ry0, (int)floorf(it->y * s));
            rx1 = LMAX(rx1, (int)ceilf((it->x + it->w) * s)); ry1 = LMAX(ry1, (int)ceilf((it->y + it->h) * s));
        }
        rx0 = LMAX(rx0, 0); ry0 = LMAX(ry0, 0); rx1 = LMIN(rx1, pwp); ry1 = LMIN(ry1, ph);
        bool gv = a->gpu && nv == 1;
        if (part && (nv == 0 || (gv && a->gvid_ok))) gskip = true;
        else if (part && rx1 > rx0 && ry1 > ry0) { a->gvid_ok = false; raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), rx0, ry0, rx1, ry1); }
        else {
            part = false; a->page.punch = gv ? vit->img : NULL; a->page.punched = false;
            raster(&a->page, &a->pdl, RGBA(255, 255, 255, 255));
            a->gvid_ok = gv && a->page.punched; a->page.punch = NULL;
        }
        if (gv && a->gvid_ok) {
            const Image *im = vit->img; float s = a->page.scale;
            gvd = (GpuVideo){ im->px, im->w, im->h, im->w, floorf(vit->x * s + 0.5f) + side_px, floorf(vit->y * s + 0.5f) + bar_px, floorf((vit->x + vit->w) * s + 0.5f) + side_px, floorf((vit->y + vit->h) * s + 0.5f) + bar_px };
            gvp = &gvd;
        }
    } else { a->gvid_ok = false; raster(&a->page, &(DisplayList){0}, RGBA(255, 255, 255, 255)); }
    if (!part) { build_chrome(a); chrome_tip(a); raster(&a->frame, &a->cdl, RGBA(255, 255, 255, 255)); }
    if (!gskip) for (int y = part ? ry0 : 0; y < (part ? ry1 : ph) && y + bar_px < a->ph; y++)
        memcpy(a->frame.px + (size_t)(y + bar_px) * (size_t)a->frame.stride + side_px, a->page.px + (size_t)y * (size_t)a->page.stride, (size_t)pwp * 4);
    if (!(a->gpu && gpu_present_frame(a->gpu, a->frame.px, a->frame.w, a->frame.h, a->frame.stride, gskip ? 0 : part ? bar_px + ry0 : 0, gskip ? 0 : part ? bar_px + ry1 : a->frame.h, gvp))) {
        SDL_Surface *ws = SDL_GetWindowSurface(a->win);
        if (ws) {
            SDL_Surface *src = SDL_CreateSurfaceFrom(a->frame.w, a->frame.h, SDL_PIXELFORMAT_ARGB8888, a->frame.px, a->frame.stride * 4);
            SDL_BlitSurfaceScaled(src, NULL, ws, NULL, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(src); SDL_UpdateWindowSurface(a->win);
        }
    }
    a->frame_ms = now_ms() - t0;
    if (getenv("LUMEN_SHOT")) {   /* write the presented frame to a PNG after LUMEN_SHOT_MS and exit */
        static double ts; if (!ts) ts = t0;
        const char *d = getenv("LUMEN_SHOT_MS");
        if (t0 - ts >= (d ? atof(d) : 1500)) { png_write(getenv("LUMEN_SHOT"), a->frame.px, a->frame.w, a->frame.h, a->frame.stride); exit(0); }
    }
    if (getenv("LUMEN_DEBUG_PAINT")) {
        static int np, nf, nw[6]; static double tp, tf, t_last;
        if (part) { np++; tp += a->frame_ms; } else { nf++; tf += a->frame_ms; nw[why]++; }
        if (t0 - t_last > 1000) {
            fprintf(stderr, "lumen: paint partial=%d (%.1fms avg) full=%d (%.1fms avg) why: novonly=%d page=%d relayout=%d dom=%d scroll=%d\n", np, np ? tp / np : 0, nf, nf ? tf / nf : 0, nw[1], nw[2], nw[3], nw[4], nw[5]);
            np = nf = 0; tp = tf = 0; memset(nw, 0, sizeof nw); t_last = t0;
        }
    }
    a->dirty = a->vonly = false;
    a->last_page = a->t->cur; a->last_sy = a->t->sy; a->last_ver = a->t->cur ? a->t->cur->d->dom_version : 0;
}

static void update_size(App *a) {
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
    int w, h; SDL_GetWindowSize(a->win, &w, &h); SDL_GetWindowSizeInPixels(a->win, &a->pw, &a->ph);
    a->vw = (float)w; a->vh = (float)h; a->scale = w ? (float)a->pw / (float)w : 1;
    if (a->gpu) gpu_resize(a->gpu, a->pw, a->ph);
    a->t->relayout = a->dirty = true;
}

static App *g_app;
static uint64_t fnv(const char *s) { uint64_t h = 1469598103934665603ull; for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull; return h; }
static bool is_sheet_link(Node *n) { const char *r = node_attr(n, "rel"); return n->tag == A_link && r && strstr(r, "stylesheet") && node_attr(n, "href"); }
typedef struct { uint64_t gen; Node *n; } LinkLoad;
static void link_done(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq; LinkLoad *l = ud; Page *p = g_app->t->cur;
    if (p && p->gen == l->gen && r && r->status == 200 && r->body) {
        StyleSheet *s = css_parse_sheet(r->body, r->body_len, r->url, 1, &p->e->media); s->owner = l->n; style_engine_add_sheet(p->e, s);
        style_recalc(p->e, &p->d->node, true); g_app->t->relayout = g_app->dirty = true;
    }
    if (p && p->gen == l->gen && p->js)
        js_dispatch(p->js, l->n, r && r->status == 200 && r->body ? "load" : "error", "Event", false, false, 0, 0, 0, NULL);
    node_release(l->n);
    free(l);
}
typedef struct { uint64_t gen; char *u; } ImgLoad;
static void img_done(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq; ImgLoad *l = ud;
    g_img_epoch++;
    Image *im = r && r->status == 200 && r->body ? image_decode((const uint8_t *)r->body, r->body_len) : NULL;
    if (getenv("LUMEN_DEBUG_IMG")) fprintf(stderr, "lumen: img decode -> %s\n", im ? "ok" : "NULL");
    SDL_LockMutex(icache_mu);
    ImgSlot *slot = hm_get(&icache, l->u);
    if (slot && !slot->im && im) { g_icache_bytes += (size_t)im->w * (size_t)im->h * 4; g_icache_n++; }
    if (slot && !slot->im) { slot->im = im; im = NULL; }
    if (slot) { slot->done = true; slot->evicted = slot->refetch = false; }
    SDL_UnlockMutex(icache_mu);
    if (getenv("LUMEN_DEBUG_IMG")) fprintf(stderr, "lumen: img done status=%d len=%zu decoded=%d slot=%d %.80s\n", r ? r->status : -1, r && r->body ? r->body_len : 0, slot && slot->im ? 1 : 0, slot ? 1 : 0, l->u);
    if (im) image_unref(im);
    if (g_app->t->cur && g_app->t->cur->gen == l->gen) { g_app->t->cur->img_check = true; g_app->t->relayout = g_app->dirty = true; }
    else if (!l->gen) { g_app->dirty = true; g_app->vonly = false; }
    free(l->u); free(l);
}
static void img_refetch(const char *u) {
    NetRequest *rq = net_request_new("GET", u); ImgLoad *l = xmalloc(sizeof *l); l->gen = 0; l->u = xstrdup(u);
    rq->done = img_done; rq->ud = l; rq->priority = 2; net_fetch(rq);
}
/* Low-memory mode: drop decoded pixels of images not painted in the last full paint for 3 s; refetched on demand. */
static void img_evict(void) {
    double now = (double)SDL_GetTicks();
    SDL_LockMutex(icache_mu);
    hm_foreach(&icache, ent) {
        ImgSlot *s = ent->val;
        if (!s->im || s->im->refs != 1 || s->used >= g_full_paint || now - s->used < 3000) continue;
        g_icache_bytes -= (size_t)s->im->w * (size_t)s->im->h * 4; g_icache_n--;
        s->ew = image_css_w(s->im); s->eh = image_css_h(s->im);
        image_unref(s->im); s->im = NULL; s->evicted = true; s->refetch = false;
    }
    SDL_UnlockMutex(icache_mu);
}
static bool vis_doc(Node *n, void *ud) {
    if (!n) return false;
    if ((void *)n->doc == ud) return true;
    for (Page *p = g_frames; p; p = p->fnext) if (p->d == n->doc && p->frame_el && (void *)p->frame_el->doc == ud) return true;
    return false;
}
/* Start async loads for <img> sources and CSS background images that appeared after the initial load. */
static void sync_images(Page *p) {
    for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (n->type != NODE_ELEMENT) continue;
        char *u = NULL;
        if (n->style && (n->style->mask_image || n->style->bg_image) && n->style->display != D_NONE) u = xstrdup(n->style->mask_image ? n->style->mask_image : n->style->bg_image);
        else if (n->ns == NS_HTML && n->tag == A_img && node_attr(n, "src")) u = url_join(p->d->url, node_attr(n, "src"));
        else continue;
        if (!u || !*u || !strncmp(u, "blob:", 5)) { free(u); continue; }
        SDL_LockMutex(icache_mu);
        bool have = hm_get(&icache, u) != NULL;
        if (!have) { ImgSlot *slot = xcalloc(1, sizeof *slot); hm_put(&icache, u, slot); }
        SDL_UnlockMutex(icache_mu);
        if (!have) {
            if (getenv("LUMEN_DEBUG_IMG")) fprintf(stderr, "lumen: img fetch %.80s\n", u);
            NetRequest *rq = net_request_new("GET", u); ImgLoad *l = xmalloc(sizeof *l); l->gen = p->gen; l->u = u;
            rq->done = img_done; rq->ud = l; rq->priority = 2; net_fetch(rq);
        } else free(u);
    }
}
/* Fire load/error once per (img node, resolved src) after its fetch completes. */
static void fire_img_events(Page *p) {
    if (!p || !p->js || !p->img_check) return;
    p->img_check = false;
    Node **ev = NULL; bool *ok = NULL; int ne = 0, cap = 0;
    for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML || n->tag != A_img || !node_attr(n, "src")) continue;
        char *u = url_join(p->d->url, node_attr(n, "src")); if (!u) continue;
        char key[2048]; snprintf(key, sizeof key, "%p %s", (void *)n, u);
        if (!hm_get(&p->img_fired, key)) {
            SDL_LockMutex(icache_mu); ImgSlot *s = hm_get(&icache, u); bool done = s && s->done, good = done && s->im; SDL_UnlockMutex(icache_mu);
            if (done) {
                hm_put(&p->img_fired, key, (void *)1);
                if (ne == cap) { cap = cap ? cap * 2 : 16; ev = xrealloc(ev, (size_t)cap * sizeof *ev); ok = xrealloc(ok, (size_t)cap * sizeof *ok); }
                n->refcount++; ev[ne] = n; ok[ne++] = good;
            }
        }
        free(u);
    }
    for (int i = 0; i < ne; i++) { js_dispatch(p->js, ev[i], ok[i] ? "load" : "error", "Event", false, false, 0, 0, 0, NULL); node_release(ev[i]); }
    free(ev); free(ok);
}
/* Reconcile engine sheets with <style>/<link rel=stylesheet> currently in the tree; seed only records them. */
static bool sync_sheets(Page *p, bool seed) {
    SheetRef *cur = NULL; int nc = 0, cap = 0; bool changed = false;
    for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML || (n->tag != A_style && !is_sheet_link(n))) continue;
        char *t = n->tag == A_style ? node_text_content(n) : NULL;
        uint64_t h = t ? fnv(t) : fnv(node_attr(n, "href"));
        int k = 0; while (k < p->nsref && p->sref[k].n != n) k++;
        bool known = k < p->nsref;
        if (!seed && (!known || p->sref[k].h != h)) {
            if (known) { style_engine_remove_owner(p->e, n); changed = true; }
            if (t) { StyleSheet *sh = css_parse_sheet(t, strlen(t), p->d->url, 1, &p->e->media); sh->owner = n; style_engine_add_sheet(p->e, sh); changed = true; }
            else {
                char *u = url_join(p->d->url, node_attr(n, "href"));
                if (u) { NetRequest *rq = net_request_new("GET", u); LinkLoad *l = xmalloc(sizeof *l); l->gen = p->gen; l->n = n; n->refcount++; rq->done = link_done; rq->ud = l; net_fetch(rq); free(u); }
            }
        }
        free(t);
        if (nc == cap) { cap = cap ? cap * 2 : 16; cur = xrealloc(cur, sizeof *cur * (size_t)cap); }
        cur[nc++] = (SheetRef){ n, h };
    }
    if (!seed) for (int i = 0; i < p->nsref; i++) {
        int k = 0; while (k < nc && cur[k].n != p->sref[i].n) k++;
        if (k == nc) { style_engine_remove_owner(p->e, p->sref[i].n); changed = true; }
    }
    free(p->sref); p->sref = cur; p->nsref = nc;
    return changed;
}
static bool frame_restyle(Page *p) {
    if (p->d->dom_version == p->seen_ver) return false;
    p->seen_ver = p->d->dom_version;
    bool sheets = sync_sheets(p, false);
    style_recalc(p->e, &p->d->node, sheets);
    sync_images(p);
    p->img_check = p->frelayout = true;
    return true;
}
static void frame_layout(Page *p) {
    if (!p->frelayout && p->L) return;
    if (!p->L) p->L = layout_new();
    p->e->media.vw = p->fw; p->e->media.vh = p->fh;
    layout_run(p->L, p->d, p->fw, p->fh);
    p->frelayout = false; p->drawn_ver = ~0ull;
}
/* Restyle, lay out and raster an iframe's document into its cached image; true if it changed. */
static bool frame_update(Page *p) {
    Box *b = p->frame_el->box;
    if (!b || !(p->frame_el->flags & NF_CONNECTED)) return false;
    float w = b->w - (b->b[1] + b->b[3] + b->p[1] + b->p[3]), h = b->h - (b->b[0] + b->b[2] + b->p[0] + b->p[2]);
    if (w < 1 || h < 1) return false;
    frame_restyle(p);
    if (w != p->fw || h != p->fh) { p->fw = w; p->fh = h; p->frelayout = true; }
    frame_layout(p);
    float s = g_app->scale; int pw = (int)ceilf(w * s), ph = (int)ceilf(h * s);
    if (p->cv.w != pw || p->cv.h != ph || !p->cv.px) { canvas_free(&p->cv); canvas_init(&p->cv, pw, ph, s); p->drawn_ver = ~0ull; }
    if (p->drawn_ver == p->d->dom_version && p->drawn_epoch == g_img_epoch) return false;
    dl_clear(&p->fdl); dl_build(&p->fdl, p->L, 0, p->fsy, w, h);
    raster(&p->cv, &p->fdl, RGBA(255, 255, 255, 255));
    p->drawn_ver = p->d->dom_version; p->drawn_epoch = g_img_epoch;
    p->img = (Image){ .w = pw, .h = ph, .refs = 1 << 30, .px = p->cv.px, .scale = s };
    return true;
}
static void frames_tick(App *a) {
    for (Page *p = g_frames; p; p = p->fnext) {
        p->js = js_frame_ctx(p->frame_el);
        if (frame_update(p)) { a->dirty = true; a->vonly = false; }
        fire_img_events(p);
    }
}
static void hf_sync(void *ud, Document *d, bool layout) {
    Page *p = ud; if (p->d != d) return;
    frame_restyle(p);
    if (layout && p->fw > 0) frame_layout(p);
}
static void hf_viewport(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { Page *p = ud; *w = p->fw; *h = p->fh; *sx = 0; *sy = p->fsy; *dpr = g_app->scale; }
static void hf_scroll_to(void *ud, float x, float y) { (void)x; Page *p = ud; p->fsy = y > 0 ? y : 0; p->drawn_ver = ~0ull; }
static Node *hf_hit(void *ud, float x, float y) { Page *p = ud; if (!p->L) return NULL; Box *b = layout_hit(p->L, x, y + p->fsy); return b ? b->node : NULL; }
static void h_frame_close(void *ud);
static void h_frame_open(void *ud, Node *f, Document *d, JsHost *ch) {
    (void)ud;
    Page *p = xcalloc(1, sizeof *p);
    p->url = xstrdup(d->url ? d->url : "about:blank"); p->d = d; p->frame_el = f;
    Box *b = f->box;
    p->fw = b ? b->w - (b->b[1] + b->b[3] + b->p[1] + b->p[3]) : 300; p->fh = b ? b->h - (b->b[0] + b->b[2] + b->p[0] + b->p[2]) : 150;
    if (p->fw < 1) p->fw = 300;
    if (p->fh < 1) p->fh = 150;
    p->e = style_engine_new(d); p->e->media.vw = p->fw; p->e->media.vh = p->fh;
    for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML) continue;
        if (n->tag == A_style) {
            char *t = node_text_content(n); StyleSheet *sh = css_parse_sheet(t, strlen(t), d->url, 1, &p->e->media); sh->owner = n; style_engine_add_sheet(p->e, sh); free(t);
        } else if (is_sheet_link(n)) {
            char *u = url_join(d->url, node_attr(n, "href"));
            NetResponse *cr = u ? net_fetch_sync(net_request_new("GET", u)) : NULL;
            if (cr && cr->status == 200 && cr->body) { StyleSheet *sh = css_parse_sheet(cr->body, cr->body_len, cr->url, 1, &p->e->media); sh->owner = n; style_engine_add_sheet(p->e, sh); }
            if (cr) net_response_free(cr);
            free(u);
        }
    }
    sync_sheets(p, true);
    style_recalc(p->e, &d->node, true);
    sync_images(p);
    p->seen_ver = d->dom_version; p->frelayout = true;
    p->fnext = g_frames; g_frames = p;
    *ch = (JsHost){ .ud = p, .media = &p->e->media, .viewport = hf_viewport, .scroll_to = hf_scroll_to, .hit = hf_hit, .sync = hf_sync, .frame_open = h_frame_open, .frame_close = h_frame_close };
}
static void h_frame_close(void *ud) {
    Page *p = ud;
    for (Page **pp = &g_frames; *pp; pp = &(*pp)->fnext) if (*pp == p) { *pp = p->fnext; break; }
    if (p->L) layout_free(p->L);
    if (p->e) style_engine_free(p->e);
    for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) { n->box = NULL; n->style = NULL; }
    hm_free(&p->img_fired, NULL); free(p->sref);
    canvas_free(&p->cv); dl_clear(&p->fdl); free(p->fdl.items.v);
    free(p->url); free(p);
    if (g_app) { g_app->dirty = true; g_app->vonly = false; }
}
static void click_frame(App *a, Page *fp, float x, float y) {
    Box *b = layout_hit(fp->L, x, y + fp->fsy);
    Node *t = b ? b->node : NULL;
    while (t && t->type != NODE_ELEMENT) t = t->parent;
    JsCtx *js = js_frame_ctx(fp->frame_el);
    if (!t || !js) return;
    js_dispatch(js, t, "mousedown", "MouseEvent", true, true, x, y, 0, NULL);
    js_dispatch(js, t, "mouseup", "MouseEvent", true, true, x, y, 0, NULL);
    bool ok = js_dispatch(js, t, "click", "MouseEvent", true, true, x, y, 0, NULL);
    a->dirty = true; a->vonly = false;
    if (!ok) return;
    for (Node *n = t; n; n = n->parent)
        if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) {
            const char *h = node_attr(n, "href"), *tg = node_attr(n, "target");
            if (!strncmp(h, "javascript:", 11) || h[0] == '#') return;
            char *u = url_join(fp->d->url, h);
            if (u && tg && (!strcmp(tg, "_top") || !strcmp(tg, "_blank"))) navigate(a, u, true);
            else if (u) js_frame_navigate(fp->frame_el, u);
            free(u); return;
        }
}
static void restyle(App *a) {
    Page *p = a->t->cur;
    if (!p || p->d->dom_version == p->seen_ver) return;
    p->seen_ver = p->d->dom_version;
    bool sheets = sync_sheets(p, false);
    style_recalc(p->e, &p->d->node, sheets);
    sync_images(p);
    p->img_check = true;
    a->t->relayout = true; if (a->t == a->tabs[a->ti]) a->dirty = true;
}
static void history_go(App *a, int d);
static void h_sync_in(void *ud, Document *d, bool layout) {
    App *a = ud; Page *p = a->t->cur;
    if (!p || p->d != d) return;
    restyle(a);
    if (!layout || !a->t->relayout) return;
    if (!p->L) p->L = layout_new();
    p->e->media.vw = a->vw - a->side; p->e->media.vh = a->vh - BAR;
    layout_run(p->L, p->d, a->vw - a->side, a->vh - BAR);
    a->t->relayout = false;
}
static void h_navigate_in(void *ud, const char *u) { (void)ud; navigate(g_app, u, true); }
static void h_set_url_in(void *ud, const char *u, bool push) {
    (void)ud; App *a = g_app;
    if (push && a->t->nhist < 256) { for (int i = a->t->hpos + 1; i < a->t->nhist; i++) free(a->t->hist[i]); a->t->nhist = a->t->hpos + 1; a->t->hist[a->t->nhist++] = xstrdup(u); a->t->hpos = a->t->nhist - 1; a->t->hgen[a->t->hpos] = a->t->cur ? a->t->cur->gen : 0; }
    else if (a->t->hpos >= 0) { free(a->t->hist[a->t->hpos]); a->t->hist[a->t->hpos] = xstrdup(u); }
    if (a->t->cur) { free(a->t->cur->url); a->t->cur->url = xstrdup(u); }
    if (!a->editing) snprintf(a->t->url, sizeof a->t->url, "%s", u);
    a->dirty = true;
}
static void h_navigate_post_in(void *ud, const char *u, const char *body, size_t len, const char *ctype) { (void)ud; navigate_ex(g_app, u, true, body, len, ctype); }
static void h_history_go_in(void *ud, int d) { (void)ud; history_go(g_app, d); }
static int h_history_len_in(void *ud) { (void)ud; return g_app->t->nhist; }
static void h_viewport_in(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { (void)ud; *w = g_app->vw - g_app->side; *h = g_app->vh - BAR; *sx = 0; *sy = g_app->t->sy; *dpr = g_app->scale; }
static void h_scroll_to_in(void *ud, float x, float y) { (void)ud; (void)x; g_app->t->sy = y; g_app->dirty = true; }
static Node *h_hit_in(void *ud, float x, float y) { (void)ud; Page *p = g_app->t->cur; if (!p || !p->L) return NULL; Box *b = layout_hit(p->L, x, y + g_app->t->sy); return b ? b->node : NULL; }
static void h_sync(void *ud, Document *d, bool layout) { TAB_CALL(ud, h_sync_in(g_app, d, layout)); }
static void h_navigate(void *ud, const char *u) { TAB_CALL(ud, h_navigate_in(ud, u)); }
static void h_set_url(void *ud, const char *u, bool push) { TAB_CALL(ud, h_set_url_in(ud, u, push)); }
static void h_navigate_post(void *ud, const char *u, const char *body, size_t len, const char *ctype) { TAB_CALL(ud, h_navigate_post_in(ud, u, body, len, ctype)); }
static void h_history_go(void *ud, int d) { TAB_CALL(ud, h_history_go_in(ud, d)); }
static int h_history_len(void *ud) { int r = 0; TAB_CALL(ud, r = h_history_len_in(ud)); return r; }
static void h_viewport(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { TAB_CALL(ud, h_viewport_in(ud, w, h, sx, sy, dpr)); }
static void h_scroll_to(void *ud, float x, float y) { TAB_CALL(ud, h_scroll_to_in(ud, x, y)); }
static Node *h_hit(void *ud, float x, float y) { Node *r = NULL; TAB_CALL(ud, r = h_hit_in(ud, x, y)); return r; }
static void page_start_js(App *a, Page *p) {
    if (getenv("LUMEN_NO_JS")) return;
    JsHost h = { a->t, &p->e->media, h_navigate, h_set_url, h_history_go, h_viewport, h_scroll_to, h_hit, h_history_len, h_navigate_post, h_sync, h_frame_open, h_frame_close };
    double t0 = now_ms();
    sync_sheets(p, true);
    p->js = js_new(p->d, &h);
    const char *pre = getenv("LUMEN_PRE_FILE");
    FILE *pf = pre ? fopen(pre, "rb") : NULL;
    if (pf) {
        fseek(pf, 0, SEEK_END); long n = ftell(pf); fseek(pf, 0, SEEK_SET);
        char *src = xmalloc((size_t)n + 1); size_t got = fread(src, 1, (size_t)n, pf); src[got] = 0; fclose(pf);
        js_eval(p->js, src, "lumen:pre"); free(src);
    }
    for (int i = 0; i < p->nscripts; i++) {
        PScript *sc = &p->scripts[i];
        if ((sc->n->flags & NF_SCRIPT_STARTED) || !(sc->n->flags & NF_CONNECTED)) continue;
        if (sc->src) { js_run_script(p->js, sc->n, sc->src, sc->len, sc->name); free(sc->src); sc->src = NULL; sc->len = 0; }
        else { sc->n->flags |= NF_SCRIPT_STARTED; js_dispatch(p->js, sc->n, "error", "Event", false, false, 0, 0, 0, NULL); }
    }
    js_set_ready_state(p->js, 1);
    js_dispatch(p->js, &p->d->node, "DOMContentLoaded", "Event", true, false, 0, 0, 0, NULL);
    js_set_ready_state(p->js, 2);
    js_dispatch_window(p->js, "load");
    fprintf(stderr, "lumen: ran %d scripts in %.0fms\n", p->nscripts, now_ms() - t0);
    restyle(a);
}
static void wake(void) { SDL_Event e; SDL_zero(e); e.type = EV_NET; SDL_PushEvent(&e); }

static bool is_text_ctl(Node *n) {
    if (!n || n->type != NODE_ELEMENT || n->ns != NS_HTML) return false;
    if (n->tag == A_textarea) return true;
    if (n->tag != A_input) return false;
    const char *t = node_attr(n, "type");
    if (!t || !*t) return true;
    static const char *const ok[] = { "text", "email", "password", "search", "tel", "url", "number", NULL };
    for (int i = 0; ok[i]; i++) if (str_ieq(t, ok[i])) return true;
    return false;
}
static Node *page_focus(App *a) { Node *f = a->t->cur && a->t->cur->d ? a->t->cur->d->focus : NULL; return is_text_ctl(f) ? f : NULL; }
static char *ctl_value(Node *n) {
    if (n->value_override) return xstrdup(n->value_override);
    if (n->tag == A_textarea) return node_text_content(n);
    const char *v = node_attr(n, "value"); return xstrdup(v ? v : "");
}
static void ctl_set(App *a, Node *n, char *v) {
    free(n->value_override); n->value_override = v; doc_mark_dirty(n->doc, n);
    if (a->t->cur->js) js_dispatch(a->t->cur->js, n, "input", "InputEvent", true, false, 0, 0, 0, NULL);
    a->t->relayout = true; a->dirty = true;
}
static void focus_node(App *a, Node *n) {
    Document *d = a->t->cur->d; Node *old = d->focus; JsCtx *js = a->t->cur->js;
    if (old == n) return;
    if (old) { old->flags &= ~(uint32_t)NF_FOCUS; doc_mark_dirty(d, old); }
    d->focus = n;
    if (n) { n->flags |= NF_FOCUS; doc_mark_dirty(d, n); }
    if (js && old) { js_dispatch(js, old, "blur", "FocusEvent", false, false, 0, 0, 0, NULL); js_dispatch(js, old, "focusout", "FocusEvent", true, false, 0, 0, 0, NULL); }
    if (js && n) { js_dispatch(js, n, "focus", "FocusEvent", false, false, 0, 0, 0, NULL); js_dispatch(js, n, "focusin", "FocusEvent", true, false, 0, 0, 0, NULL); }
    if (is_text_ctl(n)) SDL_StartTextInput(a->win);
    a->caret_t = now_ms();
    a->t->relayout = true; a->dirty = true;
}
static void url_enc(SB *b, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (isalnum(*p) || strchr("-_.*", *p)) sb_putc(b, (char)*p);
        else if (*p == ' ') sb_putc(b, '+');
        else { char h[4]; snprintf(h, sizeof h, "%%%02X", *p); sb_puts(b, h); }
    }
}
static void submit_form(App *a, Node *ctl) {
    Node *f = ctl;
    while (f && !(f->type == NODE_ELEMENT && f->tag == A_form)) f = f->parent;
    if (!f) return;
    if (a->t->cur->js && !js_dispatch(a->t->cur->js, f, "submit", "Event", true, true, 0, 0, 0, NULL)) return;
    const char *m = node_attr(f, "method");
    if (m && str_ieq(m, "post")) { fprintf(stderr, "lumen: native POST form submission not supported yet\n"); return; }
    SB q; sb_init(&q);
    for (Node *n = f->first; n; n = node_next_in_tree(n, f)) {
        if (n->type != NODE_ELEMENT || !(n->tag == A_input || n->tag == A_textarea || n->tag == A_select)) continue;
        const char *name = node_attr(n, "name"), *ty = node_attr(n, "type");
        if (!name || !*name || node_has_attr(n, "disabled")) continue;
        if (ty && (str_ieq(ty, "submit") || str_ieq(ty, "button") || str_ieq(ty, "reset") || str_ieq(ty, "image") || str_ieq(ty, "file"))) continue;
        if (ty && (str_ieq(ty, "checkbox") || str_ieq(ty, "radio")) && !(n->checked_override ? n->checked_override > 0 : node_has_attr(n, "checked"))) continue;
        char *v = ctl_value(n);
        if (q.n) sb_putc(&q, '&');
        url_enc(&q, name); sb_putc(&q, '='); url_enc(&q, v); free(v);
    }
    const char *act = node_attr(f, "action");
    char *u = url_join(a->t->cur->d->url, act && *act ? act : a->t->cur->d->url);
    char *qm = strchr(u, '?'); if (qm) *qm = 0;
    char *hm = strchr(u, '#'); if (hm) *hm = 0;
    char *qs = q.n ? sb_take(&q) : NULL; if (!qs) sb_free(&q);
    SB full; sb_init(&full); sb_puts(&full, u); sb_putc(&full, '?'); if (qs) sb_puts(&full, qs);
    free(u); free(qs);
    char *dest = sb_take(&full); navigate(a, dest, true); free(dest);
}

static void click_page(App *a, float x, float y) {
    x -= a->side;
    if (!a->t->cur || !a->t->cur->L) return;
    Box *b = layout_hit(a->t->cur->L, x, y - BAR + a->t->sy);
    Node *t = b ? b->node : NULL;
    if (getenv("LUMEN_DEBUG_CLICK")) {
        fprintf(stderr, "lumen: click %.0f,%.0f box[%.0f,%.0f %.0fx%.0f] ->", x, y, b ? b->x : 0, b ? b->y : 0, b ? b->w : 0, b ? b->h : 0);
        Node *n = t, *root = t; int i = 0;
        for (; n; root = n, n = n->parent) if (i++ < 8) fprintf(stderr, " %s%s%s", n->type == NODE_ELEMENT ? n->tag : "#", n->type == NODE_ELEMENT && node_attr(n, "id") ? "#" : "", n->type == NODE_ELEMENT && node_attr(n, "id") ? node_attr(n, "id") : "");
        fprintf(stderr, " connected=%d\n", root == &a->t->cur->d->node);
    }
    while (t && t->type != NODE_ELEMENT) t = t->parent;
    Page *fp = t && t->tag == A_iframe ? frame_find(t) : NULL;
    if (fp && fp->L && t->box) {
        Box *fb = t->box;
        click_frame(a, fp, x - (fb->x + fb->b[3] + fb->p[3]), y - BAR + a->t->sy - (fb->y + fb->b[0] + fb->p[0]));
        return;
    }
    if (t && a->t->cur->js) {
        JsCtx *js = a->t->cur->js; float cy = y - BAR;
        Node *before = a->t->cur->d->focus;
        js_dispatch(js, t, "mousedown", "MouseEvent", true, true, x, cy, 0, NULL);
        bool js_focused = a->t->cur->d->focus != before;
        Node *ctl = t;
        for (Node *l = t; l; l = l->parent) if (l->type == NODE_ELEMENT && l->tag && !strcmp(l->tag, "label")) {
            const char *fo = node_attr(l, "for");
            Node *c = fo ? doc_get_element_by_id(a->t->cur->d, fo) : NULL;
            for (Node *k = l->first; !c && k; k = node_next_in_tree(k, l)) if (is_text_ctl(k)) c = k;
            if (c) ctl = c;
            break;
        }
        if (!is_text_ctl(ctl) && !js_focused) {   /* overlays (floating labels etc.) above a text field */
            float px = x, py = y - BAR + a->t->sy;
            for (Node *n = a->t->cur->d->node.first; n; n = node_next_in_tree(n, &a->t->cur->d->node))
                if (is_text_ctl(n) && n->box && px >= n->box->x && px < n->box->x + n->box->w && py >= n->box->y && py < n->box->y + n->box->h) { ctl = n; break; }
        }
        if (is_text_ctl(ctl)) focus_node(a, ctl); else if (!js_focused && page_focus(a)) focus_node(a, NULL);
        js_dispatch(js, t, "mouseup", "MouseEvent", true, true, x, cy, 0, NULL);
        bool ok = js_dispatch(js, t, "click", "MouseEvent", true, true, x, cy, 0, NULL);
        restyle(a);
        if (!ok) return;
    }
    for (Node *n = b ? b->node : NULL; n; n = n->parent)
        if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) {
            const char *h = node_attr(n, "href");
            if (!strncmp(h, "javascript:", 11)) return;
            if (h[0] == '#') { Node *t = doc_get_element_by_id(a->t->cur->d, h + 1); if (t && t->box) { a->t->sy = t->box->y; a->dirty = true; } return; }
            char *u = url_join(a->t->cur->d->url, h); navigate(a, u, true); free(u); return;
        }
}

static bool over_link(App *a, float x, float y) {
    x -= a->side;
    if (!a->t->cur || !a->t->cur->L || y < BAR) return false;
    Box *b = layout_hit(a->t->cur->L, x, y - BAR + a->t->sy);
    for (Node *n = b ? b->node : NULL; n; n = n->parent) if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) return true;
    return false;
}

static void tab_select(App *a, int i) {
    if (i < 0 || i >= a->ntabs) return;
    a->ti = i; a->t = a->tabs[i]; a->wi = a->t->ws; a->wslast[a->wi] = a->t; a->editing = false; a->t->relayout = true; a->dirty = true; a->vonly = false;
    Page *p = a->t->cur;
    SDL_SetWindowTitle(a->win, p && p->d->title && *p->d->title ? p->d->title : "Lumen");
}
static void tab_open(App *a) {
    if (!tab_new(a)) return;
    tab_select(a, a->ntabs - 1);
    a->editing = true; a->sel_all = 1; a->t->url[0] = 0; SDL_StartTextInput(a->win);
}
static void tab_free(Tab *t) { if (t->cur) page_free(t->cur); for (int k = 0; k < t->nhist; k++) free(t->hist[k]); free(t); }
static void tab_close(App *a, int i, bool *quit) {
    if (a->ntabs == 1) { *quit = true; return; }
    Tab *t = a->tabs[i]; int w = t->ws; bool act = i == a->ti;
    for (int k = 0; k < 16; k++) if (a->wslast[k] == t) a->wslast[k] = NULL;
    tab_free(t);
    memmove(a->tabs + i, a->tabs + i + 1, sizeof(Tab *) * (size_t)(a->ntabs - i - 1)); a->ntabs--;
    publish_gens(a);
    if (!act) { if (a->ti > i) a->ti--; a->t = a->tabs[a->ti]; a->dirty = true; return; }
    int j = -1;
    for (int k = i; k < a->ntabs && j < 0; k++) if (a->tabs[k]->ws == w) j = k;
    for (int k = i - 1; k >= 0 && j < 0; k--) if (a->tabs[k]->ws == w) j = k;
    a->ti = 0; a->t = a->tabs[0];
    if (j >= 0) tab_select(a, j); else { a->wi = w; tab_open(a); }
}
static void tab_cycle(App *a, int d) {
    int v[MAX_TABS], n = ws_tabs(a, a->wi, v), cur = 0;
    for (int k = 0; k < n; k++) if (v[k] == a->ti) cur = k;
    if (n) tab_select(a, v[(cur + d + n) % n]);
}
static void tab_nth(App *a, int k) { int v[MAX_TABS], n = ws_tabs(a, a->wi, v); if (n) tab_select(a, v[k < 0 || k >= n ? n - 1 : k]); }
static void ws_select(App *a, int w) {
    if (w < 0 || w >= a->nws) return;
    int v[MAX_TABS], n = ws_tabs(a, w, v);
    for (int k = 0; k < n; k++) if (a->tabs[v[k]] == a->wslast[w]) { tab_select(a, v[k]); return; }
    if (n) { tab_select(a, v[0]); return; }
    a->wi = w; tab_open(a);
}
static void ws_new(App *a, const char *name) {
    if (a->nws == 16) return;
    snprintf(a->wsname[a->nws], sizeof a->wsname[0], "%s", name); a->wsicon[a->nws] = -1; a->wslast[a->nws++] = NULL;
    ws_select(a, a->nws - 1);
}
static void ws_close(App *a, int w) {
    if (a->nws == 1) return;
    int n = 0;
    for (int i = 0; i < a->ntabs; i++) { Tab *t = a->tabs[i]; if (t->ws == w) tab_free(t); else { if (t->ws > w) t->ws--; a->tabs[n++] = t; } }
    a->ntabs = n;
    memmove(a->wsname + w, a->wsname + w + 1, sizeof a->wsname[0] * (size_t)(a->nws - w - 1));
    memmove(a->wsicon + w, a->wsicon + w + 1, sizeof(int) * (size_t)(a->nws - w - 1));
    memmove(a->wslast + w, a->wslast + w + 1, sizeof(Tab *) * (size_t)(a->nws - w - 1)); a->nws--;
    publish_gens(a);
    a->t = n ? a->tabs[0] : NULL; a->ti = 0;
    ws_select(a, w > 0 ? w - 1 : 0);
}
static bool ui_prompt(const char *title, const char *init, char *out, size_t n) {
#ifdef __APPLE__
    return mac_prompt(title, init, out, n);
#else
    (void)title; snprintf(out, n, "%s", init); return true;
#endif
}
static void navigate(App *a, const char *url, bool push);
static void menu_cmd(App *a, int c) {
    char nm[64], def[64];
    switch (c) {
    case MENU_WS_NEW: snprintf(def, sizeof def, "Workspace %d", a->nws + 1); ws_new(a, def); break;
    case MENU_WS_RENAME: if (ui_prompt("Rename workspace", a->wsname[a->wi], nm, sizeof nm)) snprintf(a->wsname[a->wi], sizeof a->wsname[0], "%s", nm); break;
    case MENU_WS_CLOSE: ws_close(a, a->wi); break;
    case MENU_WS_REFRESH: { Tab *act = a->t; for (int i = 0; i < a->ntabs; i++) { Tab *t = a->tabs[i]; if (t->ws != a->wi || t->hpos < 0) continue; a->t = t; navigate(a, t->hist[t->hpos], false); } a->t = act; break; }
    case MENU_WS_NEXT: ws_select(a, (a->wi + 1) % a->nws); break;
    case MENU_WS_PREV: ws_select(a, (a->wi + a->nws - 1) % a->nws); break;
    case MENU_WS_SIDEBAR: a->side = a->side > 0 ? 0 : SIDEW; for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true; break;
    }
    a->dirty = true; a->vonly = false;
}
static void info_click(App *a, int k) {
    Tab *t = a->t;
    if (k < 3) { static const int H[3] = { 1, 4, 10 }; t->unlimit_until = (double)time(NULL) + H[k] * 3600.0; t->limited = false; t->ncpu = 0; if (t->cur) media_set_limit(t->cur->d, 0); }
    t->info = false; a->dirty = true; a->vonly = false;
}
static void cpu_monitor(App *a) {   /* a tab over 80% of a core for 60 s is limited to 40% until the user lifts it */
    static double last; double now = now_ms();
    if (!last) { last = now; return; }
    double dt = now - last; if (dt < 1000) return; last = now;
    for (int i = 0; i < a->ntabs; i++) {
        Tab *t = a->tabs[i];
        double mc = t->cur ? media_cpu_ms(t->cur->d) : 0, used = t->cpu_ms + LMAX(0, mc - t->media_ms0);
        t->media_ms0 = mc; t->cpu_ms = 0; t->pct = (float)(used / dt);
        if (t->limited) { if (t->cur) media_set_limit(t->cur->d, 0.4f); continue; }
        if ((double)time(NULL) < t->unlimit_until) continue;
        t->cpuhist[t->cpui] = t->pct; t->cpui = (t->cpui + 1) % 60; if (t->ncpu < 60) t->ncpu++;
        float sum = 0; for (int k = 0; k < t->ncpu; k++) sum += t->cpuhist[k];
        if (t->ncpu == 60 && sum / 60 > 0.8f) {
            t->limited = t->info = true; t->budget = 0; t->bud_t = now;
            if (t->cur) media_set_limit(t->cur->d, 0.4f);
            fprintf(stderr, "lumen: tab %d limited to 40%% CPU (was %.0f%%)\n", i, t->pct * 100);
        }
    }
    if (getenv("LUMEN_CPU_DEBUG")) for (int i = 0; i < a->ntabs; i++) fprintf(stderr, "lumen-cpu: tab %d %.0f%%%s\n", i, a->tabs[i]->pct * 100, a->tabs[i]->limited ? " limited" : "");
}
static void ws_popup(App *a, int w, float x, float y) {
#ifdef __APPLE__
    int c = mac_ws_menu(a->win, x, y);
    if (c >= MENU_WS_ICON) { int ic = c - MENU_WS_ICON; a->wsicon[w] = ic >= MAC_NICONS ? -1 : ic; a->dirty = true; a->vonly = false; }
    else if (c) menu_cmd(a, c);
#else
    (void)w; (void)x; (void)y; menu_cmd(a, MENU_WS_RENAME);
#endif
}
static SDL_HitTestResult win_hit(SDL_Window *w, const SDL_Point *pt, void *ud) {
    (void)w;
    return pt->y < TABH && bar_hit(ud, (float)pt->x, (float)pt->y) == HB_NONE ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}
static void history_go(App *a, int d) {
    int np = a->t->hpos + d; if (np < 0 || np >= a->t->nhist) return;
    Page *p = a->t->cur;
    if (p && p->js && a->t->hgen[np] == p->gen && a->t->hgen[a->t->hpos] == p->gen) {
        a->t->hpos = np;
        free(p->d->url); p->d->url = xstrdup(a->t->hist[np]);
        free(p->url); p->url = xstrdup(a->t->hist[np]);
        if (!a->editing) snprintf(a->t->url, sizeof a->t->url, "%s", a->t->hist[np]);
        char js[64]; snprintf(js, sizeof js, "__lumenPopState(%d)", d);
        js_eval(p->js, js, "lumen:popstate");
        a->dirty = true;
        return;
    }
    a->t->hpos = np; navigate(a, a->t->hist[np], false); a->t->hgen[np] = load_gen;
}

#include <execinfo.h>
#include <unistd.h>
#include <signal.h>
#include <mach-o/dyld.h>
static void crash_handler(int sig) {
    void *bt[64]; int n = backtrace(bt, 64);
    char buf[96]; int k = snprintf(buf, sizeof buf, "lumen: fatal signal %d, load address 0x%lx\n", sig, (unsigned long)(0x100000000UL + (unsigned long)_dyld_get_image_vmaddr_slide(0)));
    write(2, buf, (size_t)k);
    backtrace_symbols_fd(bt, n, 2);
    signal(sig, SIG_DFL); raise(sig);
}

int main(int argc, char **argv) {
    signal(SIGSEGV, crash_handler); signal(SIGBUS, crash_handler); signal(SIGABRT, crash_handler);
    const char *start = argc > 1 ? argv[1] : "https://en.wikipedia.org/wiki/Web_browser";
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    dom_init(); net_init(6); font_init();
    icache_mu = SDL_CreateMutex(); EV_LOADED = SDL_RegisterEvents(1); EV_NET = SDL_RegisterEvents(1); EV_MENU = SDL_RegisterEvents(1);
    net_wakeup = wake; media_wakeup = wake; js_wakeup = wake; js_global_init(argv[0]);
    paint_image_hook = node_img; paint_url_image_hook = url_img; layout_image_size_hook = img_size;
    App a; memset(&a, 0, sizeof a); g_app = &a; a.nws = 1; a.wsicon[0] = -1; snprintf(a.wsname[0], sizeof a.wsname[0], "Personal"); a.side = getenv("LUMEN_NO_SIDEBAR") ? 0 : SIDEW; tab_new(&a);
    bool want_gpu = !getenv("LUMEN_NO_GPU");
    a.win = SDL_CreateWindow("Lumen", 1280, 840, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (want_gpu && !strcmp(SDL_GetPlatform(), "macOS") ? SDL_WINDOW_METAL : 0));
    if (!a.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
#ifdef __APPLE__
    mac_style_window(a.win); mac_install_menu(EV_MENU);
#endif
    SDL_SetWindowHitTest(a.win, win_hit, &a);
    update_size(&a);
    if (want_gpu) {
        GpuSurfSrc src = { 0 }; bool ok = false;
        SDL_PropertiesID pr = SDL_GetWindowProperties(a.win);
        if (!strcmp(SDL_GetPlatform(), "macOS")) { a.mview = SDL_Metal_CreateView(a.win); if (a.mview) { src.kind = GPU_SURF_METAL; src.a = SDL_Metal_GetLayer(a.mview); ok = src.a != NULL; } }
        else if (SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL)) { src.kind = GPU_SURF_WAYLAND; src.a = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL); src.b = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL); ok = true; }
        else if (SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL)) { src.kind = GPU_SURF_XLIB; src.a = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL); src.win = (uint64_t)SDL_GetNumberProperty(pr, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0); ok = true; }
        else if (SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL)) { src.kind = GPU_SURF_ANDROID; src.a = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL); ok = true; }
        if (ok) a.gpu = gpu_create(&src, a.pw, a.ph, getenv("LUMEN_VULKAN") != NULL || strcmp(SDL_GetPlatform(), "macOS"));
        if (!a.gpu && a.mview) { SDL_Metal_DestroyView(a.mview); a.mview = NULL; }
    }
    fprintf(stderr, "lumen: presenting with %s\n", gpu_backend_name(a.gpu));
    a.ui = font_get("system-ui", 400, false, 13.5f);
    char cookie_path[1024] = "";
    { char *pref = SDL_GetPrefPath("lumen", "Lumen"); if (pref) { snprintf(cookie_path, sizeof cookie_path, "%scookies.txt", pref); SDL_free(pref); cookies_load(cookie_path); } }
    double cookies_saved_at = now_ms();
    {
        char *pref = SDL_GetPrefPath("lumen", "Lumen"); char sp[1024] = "";
        if (pref) { snprintf(sp, sizeof sp, "%ssettings.txt", pref); SDL_free(pref); }
        FILE *sf = *sp ? fopen(sp, "r") : NULL; char line[256];
        while (sf && fgets(line, sizeof line, sf)) if (!strncmp(line, "offscreen_media_eviction=", 25)) g_lowmem = atoi(line + 25) != 0;
        if (sf) fclose(sf);
        const char *lm = getenv("LUMEN_LOWMEM"); if (lm) g_lowmem = atoi(lm) != 0;
        fprintf(stderr, "lumen: offscreen media eviction %s\n", g_lowmem ? "on" : "off");
        media_lowmem = g_lowmem;
    }
    navigate(&a, start, true);
    for (int i = 2; i < argc; i++) { Tab *nt = tab_new(&a); if (!nt) break; a.t = nt; navigate(&a, argv[i], true); }
    a.t = a.tabs[0]; a.ti = 0;
    bool quit = false, cmd = false;
    while (!quit) {
        SDL_Event ev;
        int to = a.t->loading ? 120 : 1000;
        for (int i = 0; i < a.ntabs; i++) { Tab *tb = a.tabs[i]; if (tb->cur && tb->cur->js) { double dl = js_next_deadline(tb->cur->js) - now_ms(); if (tb->limited && tb->budget < 0) dl = LMAX(dl, -tb->budget / 0.4 - (now_ms() - tb->bud_t)); if (dl < to) to = dl < 0 ? 0 : (int)dl; } }
        if (net_pending() && to > 50) to = 50;
        { int mf = media_tick(); if (mf & 1) { if (!a.dirty) a.vonly = true; a.dirty = true; } if (mf & 2) a.t->relayout = true; }
        if (!a.editing && page_focus(&a) && !SDL_TextInputActive(a.win)) SDL_StartTextInput(a.win);
        if (getenv("LUMEN_MEM_STATS")) {
            static double last_stats; double tn = now_ms();
            if (tn - last_stats > 10000) { last_stats = tn; size_t fr, seg = media_mem_bytes(&fr); size_t jh = 0, je = 0; js_mem_stats(&jh, &je); fprintf(stderr, "lumen-mem: mse=%.1fMB vframes=%.1fMB images=%.1fMB/%d js_heap=%.1fMB js_external=%.1fMB canvases=%.1fMB\n", seg / 1048576.0, fr / 1048576.0, g_icache_bytes / 1048576.0, g_icache_n, jh / 1048576.0, je / 1048576.0, ((double)a.frame.w * a.frame.h + (double)a.page.w * a.page.h) * 4 / 1048576.0); }
        }
        if (g_lowmem) {
            static double last_evict; double tn = now_ms();
            if (a.t->cur) media_mark_visible(vis_doc, a.t->cur->d);
            if (tn - last_evict > 2000) { last_evict = tn; img_evict(); }
        }
        { int mt = media_timeout_ms(); if (mt >= 0 && mt < to) to = mt; }
        if (a.tip_until) { double r = a.tip_until - now_ms(); if (r <= 0) { a.tip_until = 0; a.dirty = true; a.vonly = false; } else if (r + 1 < to) to = (int)r + 1; }
        {
            bool want = false;
            if (!a.editing && page_focus(&a)) {
                double ph = fmod(now_ms() - a.caret_t, 1060);
                want = ph < 530;
                int nx = (int)((want ? 530 : 1060) - ph) + 1; if (nx < to) to = nx;
            }
            if (want != dl_caret_on) { dl_caret_on = want; a.vonly = false; a.dirty = true; }
        }
        { double now = now_ms(), gn; if (image_anim_tick(now, &gn)) { a.vonly = false; a.dirty = true; } if (gn > 0 && gn - now < to) to = gn - now < 1 ? 1 : (int)(gn - now); }
        if (!SDL_WaitEventTimeout(&ev, to)) { if (a.t->loading) a.dirty = true; }
        else do {
            if (ev.type != EV_NET) a.vonly = false;
            switch (ev.type) {
            case SDL_EVENT_QUIT: quit = true; break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESIZED: update_size(&a); break;
            case SDL_EVENT_WINDOW_EXPOSED: a.dirty = true; break;
            case SDL_EVENT_MOUSE_WHEEL: a.t->sy -= ev.wheel.y * 40; a.dirty = true; break;
            case SDL_EVENT_MOUSE_MOTION: {
                int h = bar_hit(&a, ev.motion.x, ev.motion.y); if (h != a.hover) { a.hover = h; a.dirty = true; }
                SDL_SetCursor(SDL_CreateSystemCursor(h == HB_URL ? SDL_SYSTEM_CURSOR_TEXT : over_link(&a, ev.motion.x, ev.motion.y) || (h && h != HB_URL) ? SDL_SYSTEM_CURSOR_POINTER : SDL_SYSTEM_CURSOR_DEFAULT));
                break; }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button != SDL_BUTTON_LEFT) break;
                { int bh = bar_hit(&a, ev.button.x, ev.button.y);
                  if (bh >= HB_TABDOT) { int i = bh - HB_TABDOT; if (i != a.ti) tab_select(&a, i); a.t->info = true; a.dirty = true; a.vonly = false; break; }
                  if (bh >= HB_INFO) { info_click(&a, bh - HB_INFO); break; }
                  if (bh >= HB_WS) { int w = bh - HB_WS; if (w != a.wi) ws_select(&a, w); else ws_popup(&a, w, ev.button.x, ev.button.y); break; }
                  if (bh == HB_WSNEW) { menu_cmd(&a, MENU_WS_NEW); break; }
                  if (bh >= HB_TABX) { tab_close(&a, bh - HB_TABX, &quit); break; }
                  if (bh >= HB_TAB) { tab_select(&a, bh - HB_TAB); break; }
                  if (bh == HB_NEWTAB) { tab_open(&a); break; } }
                switch (bar_hit(&a, ev.button.x, ev.button.y)) {
                case HB_BACK: history_go(&a, -1); break;
                case HB_FWD: history_go(&a, 1); break;
                case HB_RELOAD: if (a.t->hpos >= 0) navigate(&a, a.t->hist[a.t->hpos], false); break;
                case HB_URL: a.editing = true; a.sel_all = 1; SDL_StartTextInput(a.win); a.dirty = true; break;
                default: if (a.editing) { a.editing = false; SDL_StopTextInput(a.win); a.dirty = true; } click_page(&a, ev.button.x, ev.button.y);
                }
                break;
            case SDL_EVENT_TEXT_INPUT: a.caret_t = now_ms();
                if (!a.editing && page_focus(&a)) {
                    Node *f = page_focus(&a);
                    if (!a.t->cur->js || js_dispatch(a.t->cur->js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text)) {
                        char *v = ctl_value(f); SB b; sb_init(&b); sb_puts(&b, v); sb_puts(&b, ev.text.text); free(v);
                        ctl_set(&a, f, sb_take(&b));
                    }
                    if (a.t->cur->js) js_dispatch(a.t->cur->js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text);
                    restyle(&a);
                } else if (a.editing) { if (a.sel_all) { a.t->url[0] = 0; a.sel_all = 0; } strncat(a.t->url, ev.text.text, sizeof a.t->url - strlen(a.t->url) - 1); a.dirty = true; }
                break;
            case SDL_EVENT_KEY_UP: if (ev.key.key == SDLK_LGUI || ev.key.key == SDLK_RGUI || ev.key.key == SDLK_LCTRL || ev.key.key == SDLK_RCTRL) cmd = false; break;
            case SDL_EVENT_KEY_DOWN: { a.caret_t = now_ms();
                SDL_Keycode k = ev.key.key; float page = a.vh - BAR - 40;
                if (k == SDLK_LGUI || k == SDLK_RGUI || k == SDLK_LCTRL || k == SDLK_RCTRL) { cmd = true; break; }
                if (cmd || (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
                    if (k == SDLK_L) { a.editing = true; a.sel_all = 1; SDL_StartTextInput(a.win); }
                    else if (k == SDLK_R && a.t->hpos >= 0) navigate(&a, a.t->hist[a.t->hpos], false);
                    else if (k == SDLK_LEFTBRACKET) history_go(&a, -1);
                    else if (k == SDLK_RIGHTBRACKET) history_go(&a, 1);
                    else if (k == SDLK_Q) quit = true;
                    else if (k == SDLK_W) tab_close(&a, a.ti, &quit);
                    else if (k == SDLK_T) tab_open(&a);
                    else if (k == SDLK_TAB) tab_cycle(&a, (ev.key.mod & SDL_KMOD_SHIFT) ? -1 : 1);
                    else if (k >= SDLK_1 && k <= SDLK_9) tab_nth(&a, k == SDLK_9 ? -1 : (int)(k - SDLK_1));
                    a.dirty = true; break;
                }
                if (a.editing) {
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { SDL_StopTextInput(a.win); navigate(&a, a.t->url, true); }
                    else if (k == SDLK_ESCAPE) { a.editing = false; SDL_StopTextInput(a.win); if (a.t->hpos >= 0) snprintf(a.t->url, sizeof a.t->url, "%s", a.t->cur ? a.t->cur->url : a.t->hist[a.t->hpos]); }
                    else if (k == SDLK_BACKSPACE) { if (a.sel_all) { a.t->url[0] = 0; a.sel_all = 0; } else { size_t n = strlen(a.t->url); while (n && (a.t->url[n - 1] & 0xC0) == 0x80) n--; if (n) n--; a.t->url[n] = 0; } }
                    else if (k == SDLK_LEFT || k == SDLK_RIGHT) a.sel_all = 0;
                    a.dirty = true; break;
                }
                if (page_focus(&a)) {
                    Node *f = page_focus(&a); const char *kn = k == SDLK_RETURN || k == SDLK_KP_ENTER ? "Enter" : k == SDLK_BACKSPACE ? "Backspace" : k == SDLK_ESCAPE ? "Escape" : k == SDLK_TAB ? "Tab" : k == SDLK_LEFT ? "ArrowLeft" : k == SDLK_RIGHT ? "ArrowRight" : k == SDLK_UP ? "ArrowUp" : k == SDLK_DOWN ? "ArrowDown" : NULL;
                    if (kn) {
                        bool ok = !a.t->cur->js || js_dispatch(a.t->cur->js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        if (ok && !strcmp(kn, "Backspace")) { char *v = ctl_value(f); size_t n = strlen(v); while (n && (v[n - 1] & 0xC0) == 0x80) n--; if (n) n--; v[n] = 0; ctl_set(&a, f, v); }
                        else if (ok && !strcmp(kn, "Enter") && f->tag == A_input) submit_form(&a, f);
                        else if (ok && !strcmp(kn, "Escape")) focus_node(&a, NULL);
                        if (a.t->cur && a.t->cur->js && page_focus(&a) == f) js_dispatch(a.t->cur->js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        restyle(&a); a.dirty = true;
                    }
                    break;
                }
                if (k == SDLK_DOWN) a.t->sy += 40; else if (k == SDLK_UP) a.t->sy -= 40;
                else if (k == SDLK_PAGEDOWN || k == SDLK_SPACE) a.t->sy += (ev.key.mod & SDL_KMOD_SHIFT) ? -page : page;
                else if (k == SDLK_PAGEUP) a.t->sy -= page;
                else if (k == SDLK_HOME) a.t->sy = 0; else if (k == SDLK_END) a.t->sy = max_scroll(&a);
                else if (k == SDLK_BACKSPACE) history_go(&a, (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : -1);
                else break;
                a.dirty = true; break; }
            default:
                if (ev.type == EV_MENU) menu_cmd(&a, ev.user.code);
                if (ev.type == EV_LOADED) {
                    Page *p = ev.user.data1;
                    Tab *lt = NULL; for (int i = 0; i < a.ntabs; i++) if (a.tabs[i]->lgen == p->gen) lt = a.tabs[i];
        {   /* LUMEN_TAB_TOUR=ms: benchmark aid, activates each tab in turn once like a user would */
            static int tour_i = -1; static double tour_next, tour_ms;
            if (tour_i < 0) { const char *e = getenv("LUMEN_TAB_TOUR"); tour_ms = e ? atof(e) : 0; tour_i = 0; tour_next = SDL_GetTicks() + tour_ms; }
            if (tour_ms > 0 && tour_i < a.ntabs && SDL_GetTicks() >= tour_next) { tab_select(&a, tour_i++); tour_next = SDL_GetTicks() + tour_ms; }
        }
                    if (!lt) { page_free(p); break; }
                    Tab *act = a.t; bool fg = lt == act; a.t = lt;
                    page_free(a.t->cur); a.t->cur = p; a.t->loading = false; a.t->sy = 0; a.t->relayout = true; a.dirty = true;
                    if (!a.editing) snprintf(a.t->url, sizeof a.t->url, "%s", p->url);
                    if (a.t->hpos >= 0) { free(a.t->hist[a.t->hpos]); a.t->hist[a.t->hpos] = xstrdup(p->url); }
                    char title[512]; snprintf(title, sizeof title, "%s", p->d->title && *p->d->title ? p->d->title : p->url);
                    if (fg) SDL_SetWindowTitle(a.win, title);
                    sync_images(p);
                    page_start_js(&a, p);
                    a.t = act; double t0 = now_ms(); if (fg) render(&a); else a.dirty = true;
                    fprintf(stderr, "lumen: %s loaded in %.0fms, first frame %.1fms (layout+paint+present), %d boxes\n", p->url, p->load_ms, now_ms() - t0, p->L ? p->L->nboxes : 0);
                }
            }
        } while (SDL_PollEvent(&ev));
        net_poll();
        { Tab *act = a.t; double tn = now_ms(); for (int i = 0; i < a.ntabs; i++) { a.t = a.tabs[i]; if (a.t->limited) { a.t->budget = LMIN(a.t->budget + 0.4 * (tn - a.t->bud_t), 100); a.t->bud_t = tn; if (a.t->budget < 0) continue; } if (a.t->cur && a.t->cur->js) { double c0 = now_ms(); js_tick(a.t->cur->js); restyle(&a); fire_img_events(a.t->cur); double c = now_ms() - c0; a.t->cpu_ms += c; if (a.t->limited) a.t->budget -= c; } } a.t = act; if (a.t->cur && a.t->cur->js) frames_tick(&a); }
        if (a.dirty) { double r0 = now_ms(); render(&a); a.t->cpu_ms += now_ms() - r0; }
        cpu_monitor(&a);
        { float want = a.t->info ? INFOH : 0; if (want != g_info_h) { g_info_h = want; for (int i = 0; i < a.ntabs; i++) a.tabs[i]->relayout = true; a.dirty = true; a.vonly = false; } }
        if (*cookie_path && now_ms() - cookies_saved_at > 5000) { cookies_save(cookie_path); cookies_saved_at = now_ms(); }
    }
    if (*cookie_path) cookies_save(cookie_path);
    for (int i = 0; i < a.ntabs; i++) if (a.tabs[i]->cur) page_free(a.tabs[i]->cur);
    gpu_destroy(a.gpu);
    if (a.mview) SDL_Metal_DestroyView(a.mview);
    SDL_DestroyWindow(a.win); SDL_Quit();
    return 0;
}
