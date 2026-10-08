/* Lumen: SDL3 window, minimalist chrome, navigation, wgpu/CPU presentation */
#include <math.h>
#include <SDL3/SDL.h>
#include "../paint/paint.h"
#include "../net/net.h"
#include "../base/url.h"
#include "../gpu/gpu.h"
#include "../js/js.h"
#include "../js/jsglue.h"

#define BAR 44.f

typedef struct { Node *n; char *src; size_t len; char *name; } PScript;
typedef struct Page {
    char *url; Document *d; StyleEngine *e; Layout *L; uint64_t gen; double load_ms;
    JsCtx *js; PScript *scripts; int nscripts; uint64_t seen_ver;
} Page;

static SDL_Mutex *icache_mu;
static HMap icache;
static Uint32 EV_LOADED, EV_NET;
static uint64_t load_gen;

static Image *cache_get(const char *u) {
    SDL_LockMutex(icache_mu); Image **c = (Image **)hm_get(&icache, u); Image *im = c ? *c : NULL; SDL_UnlockMutex(icache_mu); return im;
}
static void cache_fetch(const char *u) {
    if (!u || !*u || !strncmp(u, "data:", 5)) return;
    SDL_LockMutex(icache_mu); bool have = hm_get(&icache, u) != NULL; SDL_UnlockMutex(icache_mu);
    if (have) return;
    NetResponse *r = net_fetch_sync(net_request_new("GET", u));
    Image *im = r && r->status == 200 ? image_decode((const uint8_t *)r->body, r->body_len) : NULL;
    if (r) net_response_free(r);
    Image **slot = xmalloc(sizeof *slot); *slot = im;
    SDL_LockMutex(icache_mu); if (!hm_get(&icache, u)) hm_put(&icache, u, slot); else { image_unref(im); free(slot); } SDL_UnlockMutex(icache_mu);
}
static Image *node_img(Node *n) {
    const char *s = node_attr(n, n->tag == A_video ? "poster" : "src"); if (!s) return NULL;
    char *u = url_join(n->doc->url, s); Image *im = cache_get(u); free(u); return im;
}
static Image *url_img(const char *u) { return u ? cache_get(u) : NULL; }
static bool img_size(Node *n, float *w, float *h) { Image *im = node_img(n); if (!im) return false; *w = (float)im->w; *h = (float)im->h; return true; }

typedef struct { char *url; uint64_t gen; float vw, vh; } LoadReq;

static int loader(void *arg) {
    LoadReq *rq = arg; double t0 = now_ms();
    NetResponse *r = net_fetch_sync(net_request_new("GET", rq->url));
    Page *p = xcalloc(1, sizeof *p); p->gen = rq->gen;
    const char *body = r && r->body ? r->body : "";
    size_t blen = r && r->body ? r->body_len : 0;
    char *errbuf = NULL;
    if (!r || r->status == 0) { errbuf = xmalloc(512); snprintf(errbuf, 512, "<title>Error</title><body style='font:15px system-ui;padding:40px;color:#444'><h2>Can't open this page</h2><p>%s</p>", rq->url); body = errbuf; blen = strlen(errbuf); }
    p->url = xstrdup(r && r->url ? r->url : rq->url);
    p->d = doc_new(p->url);
    html_parse(p->d, body, blen);
    if (!getenv("LUMEN_NO_JS")) for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (rq->gen != load_gen) break;
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
        if (rq->gen != load_gen) break;
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
    free(errbuf); free(rq->url); free(rq);
    SDL_Event ev; SDL_zero(ev); ev.type = EV_LOADED; ev.user.data1 = p; SDL_PushEvent(&ev);
    return 0;
}

static void page_free(Page *p) {
    if (!p) return;
    js_free(p->js);
    for (int i = 0; i < p->nscripts; i++) { free(p->scripts[i].src); free(p->scripts[i].name); }
    free(p->scripts);
    if (p->L) layout_free(p->L);
    if (p->e) style_engine_free(p->e);
    if (p->d) doc_free(p->d);
    free(p->url); free(p);
}

typedef struct App {
    SDL_Window *win; SDL_MetalView mview; Gpu *gpu;
    int pw, ph; float scale, vw, vh;
    Canvas frame, page; DisplayList pdl, cdl;
    Page *cur; bool loading;
    char *hist[256]; int nhist, hpos;
    char url[2048]; bool editing; int sel_all;
    float sy; bool dirty, relayout;
    Font *ui;
    int hover; double frame_ms;
} App;

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

static void navigate(App *a, const char *url, bool push) {
    char *u = normalize_url(url);
    if (push) {
        for (int i = a->hpos + 1; i < a->nhist; i++) free(a->hist[i]);
        a->nhist = a->hpos + 1;
        if (a->nhist == 256) { free(a->hist[0]); memmove(a->hist, a->hist + 1, sizeof(char *) * 255); a->nhist--; }
        a->hist[a->nhist++] = xstrdup(u); a->hpos = a->nhist - 1;
    }
    snprintf(a->url, sizeof a->url, "%s", u);
    a->editing = false; a->loading = true; a->dirty = true;
    LoadReq *rq = xcalloc(1, sizeof *rq); rq->url = u; rq->gen = ++load_gen; rq->vw = a->vw; rq->vh = a->vh - BAR;
    SDL_Thread *t = SDL_CreateThread(loader, "loader", rq); SDL_DetachThread(t);
}

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

enum { HB_NONE, HB_BACK, HB_FWD, HB_RELOAD, HB_URL };
static int bar_hit(App *a, float x, float y) {
    if (y > BAR) return HB_NONE;
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

static void build_chrome(App *a) {
    DisplayList *dl = &a->cdl; dl_clear(dl);
    push_rect(dl, 0, 0, a->vw, BAR, 0, RGBA(250, 250, 250, 255));
    push_rect(dl, 0, BAR - 1, a->vw, 1, 0, RGBA(226, 226, 226, 255));
    Color on = RGBA(60, 60, 60, 255), off = RGBA(190, 190, 190, 255);
    int hk[3] = { HB_BACK, HB_FWD, HB_RELOAD };
    for (int i = 0; i < 3; i++) {
        float cx = 22.f + i * 32.f;
        if (a->hover == hk[i]) push_rect(dl, cx - 13, BAR / 2 - 13, 26, 26, 13, RGBA(232, 232, 232, 255));
        bool en = hk[i] == HB_BACK ? a->hpos > 0 : hk[i] == HB_FWD ? a->hpos < a->nhist - 1 : true;
        draw_icon(dl, hk[i], cx, BAR / 2, en ? on : off);
    }
    float ux = 112, uw = a->vw - ux - 12;
    push_rect(dl, ux, 7, uw, BAR - 14, (BAR - 14) / 2, a->editing ? RGBA(255, 255, 255, 255) : RGBA(238, 238, 238, 255));
    if (a->editing) {
        DItem it; memset(&it, 0, sizeof it); it.op = DO_BORDER; it.x = ux; it.y = 7; it.w = uw; it.h = BAR - 14;
        for (int i = 0; i < 4; i++) { it.r[i] = (BAR - 14) / 2; it.bw[i] = 1.5f; it.bc[i] = RGBA(66, 133, 244, 255); it.bs[i] = 1; }
        vec_push(dl->items, it);
    }
    const char *shown = a->url;
    if (!a->editing) { const char *p = strstr(shown, "://"); if (p) shown = p + 3; }
    float base = BAR / 2 + (a->ui->ascent - a->ui->descent) / 2;
    if (a->editing && a->sel_all && *shown) {
        float w = text_width(a->ui, shown, strlen(shown), 0);
        push_rect(dl, ux + 16, 12, LMIN(w, uw - 32), BAR - 24, 2, RGBA(200, 220, 255, 255));
    }
    float tw = push_text(dl, a->ui, shown, ux + 16, base, uw - 32, a->editing ? RGBA(20, 20, 20, 255) : RGBA(90, 90, 90, 255));
    if (a->editing && !a->sel_all) push_rect(dl, ux + 16 + tw + 1, 13, 1.5f, BAR - 26, 0, RGBA(20, 20, 20, 255));
    if (a->loading) push_rect(dl, 0, BAR - 2, a->vw * 0.35f, 2, 0, RGBA(66, 133, 244, 255));
}

static float max_scroll(App *a) { return a->cur && a->cur->L ? LMAX(0, a->cur->L->doc_h - (a->vh - BAR)) : 0; }

static void render(App *a) {
    double t0 = now_ms();
    int bar_px = (int)(BAR * a->scale);
    if (a->frame.w != a->pw || a->frame.h != a->ph) { canvas_free(&a->frame); canvas_init(&a->frame, a->pw, a->ph, a->scale); }
    int ph = a->ph - bar_px; if (ph < 1) ph = 1;
    if (a->page.w != a->pw || a->page.h != ph) { canvas_free(&a->page); canvas_init(&a->page, a->pw, ph, a->scale); }
    if (a->cur) {
        if (a->relayout || !a->cur->L) {
            if (!a->cur->L) a->cur->L = layout_new();
            a->cur->e->media.vw = a->vw; a->cur->e->media.vh = a->vh - BAR;
            layout_run(a->cur->L, a->cur->d, a->vw, a->vh - BAR);
            a->relayout = false;
        }
        a->sy = LCLAMP(a->sy, 0, max_scroll(a));
        dl_clear(&a->pdl); dl_build(&a->pdl, a->cur->L, 0, a->sy, a->vw, a->vh - BAR);
        raster(&a->page, &a->pdl, RGBA(255, 255, 255, 255));
    } else raster(&a->page, &(DisplayList){0}, RGBA(255, 255, 255, 255));
    build_chrome(a);
    raster(&a->frame, &a->cdl, RGBA(255, 255, 255, 255));
    for (int y = 0; y < ph && y + bar_px < a->ph; y++)
        memcpy(a->frame.px + (size_t)(y + bar_px) * (size_t)a->frame.stride, a->page.px + (size_t)y * (size_t)a->page.stride, (size_t)a->pw * 4);
    if (!(a->gpu && gpu_present(a->gpu, a->frame.px, a->frame.w, a->frame.h, a->frame.stride))) {
        SDL_Surface *ws = SDL_GetWindowSurface(a->win);
        if (ws) {
            SDL_Surface *src = SDL_CreateSurfaceFrom(a->frame.w, a->frame.h, SDL_PIXELFORMAT_ARGB8888, a->frame.px, a->frame.stride * 4);
            SDL_BlitSurfaceScaled(src, NULL, ws, NULL, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(src); SDL_UpdateWindowSurface(a->win);
        }
    }
    a->frame_ms = now_ms() - t0;
    a->dirty = false;
}

static void update_size(App *a) {
    int w, h; SDL_GetWindowSize(a->win, &w, &h); SDL_GetWindowSizeInPixels(a->win, &a->pw, &a->ph);
    a->vw = (float)w; a->vh = (float)h; a->scale = w ? (float)a->pw / (float)w : 1;
    if (a->gpu) gpu_resize(a->gpu, a->pw, a->ph);
    a->relayout = a->dirty = true;
}

static App *g_app;
static void restyle(App *a) {
    Page *p = a->cur;
    if (!p || p->d->dom_version == p->seen_ver) return;
    p->seen_ver = p->d->dom_version;
    style_recalc(p->e, &p->d->node, false);
    a->relayout = a->dirty = true;
}
static void history_go(App *a, int d);
static void h_navigate(void *ud, const char *u) { (void)ud; navigate(g_app, u, true); }
static void h_set_url(void *ud, const char *u, bool push) {
    (void)ud; App *a = g_app;
    if (push && a->nhist < 256) { for (int i = a->hpos + 1; i < a->nhist; i++) free(a->hist[i]); a->nhist = a->hpos + 1; a->hist[a->nhist++] = xstrdup(u); a->hpos = a->nhist - 1; }
    else if (a->hpos >= 0) { free(a->hist[a->hpos]); a->hist[a->hpos] = xstrdup(u); }
    if (a->cur) { free(a->cur->url); a->cur->url = xstrdup(u); }
    if (!a->editing) snprintf(a->url, sizeof a->url, "%s", u);
    a->dirty = true;
}
static void h_history_go(void *ud, int d) { (void)ud; history_go(g_app, d); }
static int h_history_len(void *ud) { (void)ud; return g_app->nhist; }
static void h_viewport(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { (void)ud; *w = g_app->vw; *h = g_app->vh - BAR; *sx = 0; *sy = g_app->sy; *dpr = g_app->scale; }
static void h_scroll_to(void *ud, float x, float y) { (void)ud; (void)x; g_app->sy = y; g_app->dirty = true; }
static Node *h_hit(void *ud, float x, float y) { (void)ud; Page *p = g_app->cur; if (!p || !p->L) return NULL; Box *b = layout_hit(p->L, x, y + g_app->sy); return b ? b->node : NULL; }
static void page_start_js(App *a, Page *p) {
    if (getenv("LUMEN_NO_JS")) return;
    JsHost h = { a, &p->e->media, h_navigate, h_set_url, h_history_go, h_viewport, h_scroll_to, h_hit, h_history_len };
    double t0 = now_ms();
    p->js = js_new(p->d, &h);
    for (int i = 0; i < p->nscripts; i++) {
        PScript *sc = &p->scripts[i];
        if ((sc->n->flags & NF_SCRIPT_STARTED) || !(sc->n->flags & NF_CONNECTED)) continue;
        if (sc->src) js_run_script(p->js, sc->n, sc->src, sc->len, sc->name);
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

static void click_page(App *a, float x, float y) {
    if (!a->cur || !a->cur->L) return;
    Box *b = layout_hit(a->cur->L, x, y - BAR + a->sy);
    Node *t = b ? b->node : NULL;
    while (t && t->type != NODE_ELEMENT) t = t->parent;
    if (t && a->cur->js) {
        JsCtx *js = a->cur->js; float cy = y - BAR;
        js_dispatch(js, t, "mousedown", "MouseEvent", true, true, x, cy, 0, NULL);
        js_dispatch(js, t, "mouseup", "MouseEvent", true, true, x, cy, 0, NULL);
        bool ok = js_dispatch(js, t, "click", "MouseEvent", true, true, x, cy, 0, NULL);
        restyle(a);
        if (!ok) return;
    }
    for (Node *n = b ? b->node : NULL; n; n = n->parent)
        if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) {
            const char *h = node_attr(n, "href");
            if (!strncmp(h, "javascript:", 11)) return;
            if (h[0] == '#') { Node *t = doc_get_element_by_id(a->cur->d, h + 1); if (t && t->box) { a->sy = t->box->y; a->dirty = true; } return; }
            char *u = url_join(a->cur->d->url, h); navigate(a, u, true); free(u); return;
        }
}

static bool over_link(App *a, float x, float y) {
    if (!a->cur || !a->cur->L || y < BAR) return false;
    Box *b = layout_hit(a->cur->L, x, y - BAR + a->sy);
    for (Node *n = b ? b->node : NULL; n; n = n->parent) if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) return true;
    return false;
}

static void history_go(App *a, int d) {
    int np = a->hpos + d; if (np < 0 || np >= a->nhist) return;
    a->hpos = np; navigate(a, a->hist[np], false);
}

int main(int argc, char **argv) {
    const char *start = argc > 1 ? argv[1] : "https://en.wikipedia.org/wiki/Web_browser";
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    dom_init(); net_init(6); font_init();
    icache_mu = SDL_CreateMutex(); EV_LOADED = SDL_RegisterEvents(1); EV_NET = SDL_RegisterEvents(1);
    net_wakeup = wake; js_global_init(argv[0]);
    paint_image_hook = node_img; paint_url_image_hook = url_img; layout_image_size_hook = img_size;
    App a; memset(&a, 0, sizeof a); a.hpos = -1; g_app = &a;
    bool want_gpu = !getenv("LUMEN_NO_GPU");
    a.win = SDL_CreateWindow("Lumen", 1280, 840, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (want_gpu && !strcmp(SDL_GetPlatform(), "macOS") ? SDL_WINDOW_METAL : 0));
    if (!a.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
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
    navigate(&a, start, true);
    bool quit = false, cmd = false;
    while (!quit) {
        SDL_Event ev;
        int to = a.loading ? 120 : 1000;
        if (a.cur && a.cur->js) { double dl = js_next_deadline(a.cur->js) - now_ms(); if (dl < to) to = dl < 0 ? 0 : (int)dl; }
        if (net_pending() && to > 50) to = 50;
        if (!SDL_WaitEventTimeout(&ev, to)) { if (a.loading) a.dirty = true; }
        else do {
            switch (ev.type) {
            case SDL_EVENT_QUIT: quit = true; break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESIZED: update_size(&a); break;
            case SDL_EVENT_WINDOW_EXPOSED: a.dirty = true; break;
            case SDL_EVENT_MOUSE_WHEEL: a.sy -= ev.wheel.y * 40; a.dirty = true; break;
            case SDL_EVENT_MOUSE_MOTION: {
                int h = bar_hit(&a, ev.motion.x, ev.motion.y); if (h != a.hover) { a.hover = h; a.dirty = true; }
                SDL_SetCursor(SDL_CreateSystemCursor(h == HB_URL ? SDL_SYSTEM_CURSOR_TEXT : over_link(&a, ev.motion.x, ev.motion.y) || (h && h != HB_URL) ? SDL_SYSTEM_CURSOR_POINTER : SDL_SYSTEM_CURSOR_DEFAULT));
                break; }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button != SDL_BUTTON_LEFT) break;
                switch (bar_hit(&a, ev.button.x, ev.button.y)) {
                case HB_BACK: history_go(&a, -1); break;
                case HB_FWD: history_go(&a, 1); break;
                case HB_RELOAD: if (a.hpos >= 0) navigate(&a, a.hist[a.hpos], false); break;
                case HB_URL: a.editing = true; a.sel_all = 1; SDL_StartTextInput(a.win); a.dirty = true; break;
                default: if (a.editing) { a.editing = false; SDL_StopTextInput(a.win); a.dirty = true; } click_page(&a, ev.button.x, ev.button.y);
                }
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (a.editing) { if (a.sel_all) { a.url[0] = 0; a.sel_all = 0; } strncat(a.url, ev.text.text, sizeof a.url - strlen(a.url) - 1); a.dirty = true; }
                break;
            case SDL_EVENT_KEY_UP: if (ev.key.key == SDLK_LGUI || ev.key.key == SDLK_RGUI || ev.key.key == SDLK_LCTRL || ev.key.key == SDLK_RCTRL) cmd = false; break;
            case SDL_EVENT_KEY_DOWN: {
                SDL_Keycode k = ev.key.key; float page = a.vh - BAR - 40;
                if (k == SDLK_LGUI || k == SDLK_RGUI || k == SDLK_LCTRL || k == SDLK_RCTRL) { cmd = true; break; }
                if (cmd || (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
                    if (k == SDLK_L) { a.editing = true; a.sel_all = 1; SDL_StartTextInput(a.win); }
                    else if (k == SDLK_R && a.hpos >= 0) navigate(&a, a.hist[a.hpos], false);
                    else if (k == SDLK_LEFTBRACKET) history_go(&a, -1);
                    else if (k == SDLK_RIGHTBRACKET) history_go(&a, 1);
                    else if (k == SDLK_Q || k == SDLK_W) quit = true;
                    a.dirty = true; break;
                }
                if (a.editing) {
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { SDL_StopTextInput(a.win); navigate(&a, a.url, true); }
                    else if (k == SDLK_ESCAPE) { a.editing = false; SDL_StopTextInput(a.win); if (a.hpos >= 0) snprintf(a.url, sizeof a.url, "%s", a.cur ? a.cur->url : a.hist[a.hpos]); }
                    else if (k == SDLK_BACKSPACE) { if (a.sel_all) { a.url[0] = 0; a.sel_all = 0; } else { size_t n = strlen(a.url); while (n && (a.url[n - 1] & 0xC0) == 0x80) n--; if (n) n--; a.url[n] = 0; } }
                    else if (k == SDLK_LEFT || k == SDLK_RIGHT) a.sel_all = 0;
                    a.dirty = true; break;
                }
                if (k == SDLK_DOWN) a.sy += 40; else if (k == SDLK_UP) a.sy -= 40;
                else if (k == SDLK_PAGEDOWN || k == SDLK_SPACE) a.sy += (ev.key.mod & SDL_KMOD_SHIFT) ? -page : page;
                else if (k == SDLK_PAGEUP) a.sy -= page;
                else if (k == SDLK_HOME) a.sy = 0; else if (k == SDLK_END) a.sy = max_scroll(&a);
                else if (k == SDLK_BACKSPACE) history_go(&a, (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : -1);
                else break;
                a.dirty = true; break; }
            default:
                if (ev.type == EV_LOADED) {
                    Page *p = ev.user.data1;
                    if (p->gen != load_gen) { page_free(p); break; }
                    page_free(a.cur); a.cur = p; a.loading = false; a.sy = 0; a.relayout = true; a.dirty = true;
                    if (!a.editing) snprintf(a.url, sizeof a.url, "%s", p->url);
                    if (a.hpos >= 0) { free(a.hist[a.hpos]); a.hist[a.hpos] = xstrdup(p->url); }
                    char title[512]; snprintf(title, sizeof title, "%s", p->d->title && *p->d->title ? p->d->title : p->url);
                    SDL_SetWindowTitle(a.win, title);
                    page_start_js(&a, p);
                    double t0 = now_ms(); render(&a);
                    fprintf(stderr, "lumen: %s loaded in %.0fms, first frame %.1fms (layout+paint+present), %d boxes\n", p->url, p->load_ms, now_ms() - t0, p->L ? p->L->nboxes : 0);
                }
            }
        } while (SDL_PollEvent(&ev));
        net_poll();
        if (a.cur && a.cur->js) { js_tick(a.cur->js); restyle(&a); }
        if (a.dirty) render(&a);
    }
    page_free(a.cur);
    gpu_destroy(a.gpu);
    if (a.mview) SDL_Metal_DestroyView(a.mview);
    SDL_DestroyWindow(a.win); SDL_Quit();
    return 0;
}
