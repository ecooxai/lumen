/* Lumen: SDL3 window, minimalist chrome, navigation, wgpu/CPU presentation */
#include <math.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#define MALLOC_IN_USE(v) malloc_statistics_t ms_ = {0}; malloc_zone_statistics(NULL, &ms_); size_t v = ms_.size_in_use
#else
#include <malloc.h>
#define MALLOC_IN_USE(v) struct mallinfo2 mi_ = mallinfo2(); size_t v = mi_.uordblks
#endif
#include <sys/resource.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define LUMEN_IOS 1   /* iOS / iPadOS / Mac Catalyst: UIKit shell, touch input */
#endif
#include <SDL3/SDL.h>
#include "viewport.h"
#include "present_retry.h"
#ifdef LUMEN_IOS
#include <SDL3/SDL_main.h>
#include "touch_contacts.h"
#endif
#include "../paint/paint.h"
#include "../media/media.h"
#include "../net/net.h"
#include "../base/url.h"
#include "../gpu/gpu.h"
#include "../js/js.h"
#include "../js/jsglue.h"

static bool g_compact;   /* one tab in the workspace: toolbar lives in the title bar */
#define TABH (g_compact ? 0.f : 30.f)
#define TB 44.f
static float g_info_h;
static char g_notice[256];   /* dismissable error notice shown in the info bar */
#define INFOH 36.f
#define BAR (TABH + TB + g_info_h)
#include "uicmd.h"
#include "../paint/cvbuf.h"
#ifdef __APPLE__
#include "macui.h"
#ifdef LUMEN_IOS
void ios_wait_init(void);
bool ios_wait_event(SDL_Event *ev, int to_ms);
#endif
/* no auto-capitalisation/autocorrect: iOS keyboards otherwise rewrite typed URLs and queries */
static void text_input(SDL_Window *w, bool url) {
#ifdef LUMEN_IOS
    SDL_PropertiesID p = SDL_CreateProperties();
    (void)url;
    SDL_SetNumberProperty(p, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
    SDL_SetBooleanProperty(p, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
    SDL_StartTextInputWithProperties(w, p);
    SDL_DestroyProperties(p);
#else
    (void)url;
    SDL_StartTextInput(w);
#endif
}
#ifdef LUMEN_IOS
#define TLW 0.f
#else
#define TLW 78.f
#endif
#else
#define TLW 0.f
#endif
#define SIDEW 84.f
#define SIDEY (g_compact ? TB : TABH)
#define WSY0 (SIDEY + 8)
#define WSRH 40.f

typedef struct { Node *n; char *src; size_t len; char *name; bool module; } PScript;
typedef struct { Node *n; uint64_t h; } SheetRef;
typedef struct Page {
    char *url; Document *d; StyleEngine *e; Layout *L; uint64_t gen; double load_ms, scroll_due;
    JsCtx *js; PScript *scripts; int nscripts; uint64_t seen_ver, lay_ver;
    SheetRef *sref; int nsref;
    Node **vids; int nvids, vidcap; uint64_t vids_ver; /* <video> elements, rebuilt when the tree changes */
    HMap img_fired; bool img_check, scroll_pending;
    /* <iframe> child browsing context (document owned by the JS layer) */
    Node *frame_el; float fw, fh, fsy; Canvas cv; Image img; DisplayList fdl;
    uint64_t drawn_ver; int drawn_epoch; bool frelayout; struct Page *fnext;
} Page;
static Page *g_frames;
static Page *frame_find(Node *f) { for (Page *p = g_frames; p; p = p->fnext) if (p->frame_el == f) return p; return NULL; }
/* URL of the top-level document a frame element belongs to (SameSite / mixed-content context) */
static const char *frame_top_url(Node *f) {
    Document *d = f ? f->doc : NULL;
    for (int guard = 0; d && guard < 64; guard++) {
        Page *fp = NULL; for (Page *q = g_frames; q; q = q->fnext) if (q->d == d) { fp = q; break; }
        if (!fp || !fp->frame_el) break;
        d = fp->frame_el->doc;
    }
    return d ? d->url : NULL;
}
static const char *page_top_url(Page *p) { return p->frame_el ? frame_top_url(p->frame_el) : p->url; }
typedef struct { Image *im; bool done, evicted, refetch; double used; float ew, eh; uint8_t *enc; size_t enclen; } ImgSlot;   /* enc: compressed bytes kept in Lite mode for local re-decode */
static bool g_lite_active;

static SDL_Mutex *icache_mu;
static HMap icache;
static size_t g_icache_bytes; static int g_icache_n;
static int g_img_epoch;
static Uint32 EV_LOADED, EV_NET, EV_MENU;
static Uint32 EV_DL;   /* a navigation turned out to be a download: data1 = (void*)gen */
static uint64_t load_gen;
static bool g_lowmem;
static int g_cpu_on = 1, g_cpu_pct = 80, g_cpu_secs = 60, g_cpu_lim = 40;
static bool g_vctl;
static bool g_tab_sleep;
static bool g_smooth;       /* Smooth scrolling (Settings, off by default): animate wheel notches instead of jumping */
static float g_sm_dx, g_sm_dy, g_sm_x, g_sm_y;
#define WHEEL_STEP 120.f
#ifdef NDEBUG
static bool g_hud;          /* CPU/RAM readout at the bottom of the workspace sidebar (Settings; on by default in debug builds) */
#else
static bool g_hud = true;
#endif
static bool g_hud_pop;      /* host CPU/RAM popup opened by clicking the readout */
static float g_hud_cpu, g_hud_ram, g_host_cpu, g_host_ram, g_pop[4];
#define HUDH 48.f
static int g_ua;   /* UA_CHROME / UA_FIREFOX / UA_LUMEN */
static int g_gpu_backend, g_gpu_adapter = -1;   /* Settings > Graphics (GpuBackend, adapter index); applied at startup */
static const char *g_gpu_active = "CPU";
static char g_pref[1024];
/* One profile root for cookies, HSTS, storage, history and settings. An invalid
   explicit test profile must fail instead of silently reading the user's data. */
static bool profile_init(void) {
    const char *override = getenv("LUMEN_PROFILE_DIR");
    char *pref = override ? NULL : SDL_GetPrefPath("lumen", "Lumen");
    const char *path = override ? override : pref;
    size_t len = path ? strlen(path) : 0;
    if (override && (!len || len + 2 > sizeof g_pref || !SDL_CreateDirectory(path))) {
        fprintf(stderr, "lumen: cannot use LUMEN_PROFILE_DIR: %s\n", !len ? "empty path" : len + 2 > sizeof g_pref ? "path too long" : SDL_GetError());
        SDL_free(pref); return false;
    }
    if (len && len + 2 <= sizeof g_pref) {
        memcpy(g_pref, path, len);
        if (g_pref[len - 1] != '/') g_pref[len++] = '/';
        g_pref[len] = 0;
    }
    SDL_free(pref);
    return true;
}
typedef struct { char *url, *title; } Link;
#define MAX_BM 512
#define MAX_HV 1000
static Link g_bms[MAX_BM], g_hv[MAX_HV]; static int g_nbm, g_nhv;
static void pref_path(char *out, size_t n, const char *f) { snprintf(out, n, "%s%s", g_pref, f); }
static void settings_save(void) {
    char p[1200]; pref_path(p, sizeof p, "settings.txt"); FILE *f = *g_pref ? fopen(p, "w") : NULL; if (!f) return;
    fprintf(f, "offscreen_media_eviction=%d\ncpu_limit=%d\ncpu_pct=%d\ncpu_secs=%d\ncpu_lim=%d\nvideo_controls=%d\ntab_sleep=%d\nuser_agent=%d\ncpu_hud=%d\nsmooth_scroll=%d\ngpu_backend=%d\ngpu_adapter=%d\n", g_lowmem, g_cpu_on, g_cpu_pct, g_cpu_secs, g_cpu_lim, g_vctl, g_tab_sleep, g_ua, g_hud, g_smooth, g_gpu_backend, g_gpu_adapter);
    fclose(f);
}
static void link_put(Link *v, int *n, int max, const char *url, const char *title) {
    if (*n == max) { free(v[0].url); free(v[0].title); memmove(v, v + 1, sizeof *v * (size_t)(max - 1)); (*n)--; }
    char *t = xstrdup(title && *title ? title : url); for (char *c = t; *c; c++) if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' ';
    v[*n].url = xstrdup(url); v[*n].title = t; (*n)++;
}
static void link_del(Link *v, int *n, int i) { free(v[i].url); free(v[i].title); memmove(v + i, v + i + 1, sizeof *v * (size_t)(*n - i - 1)); (*n)--; }
static void links_save(const char *file, Link *v, int n) {
    char p[1200]; pref_path(p, sizeof p, file); FILE *f = *g_pref ? fopen(p, "w") : NULL; if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s\t%s\n", v[i].url, v[i].title);
    fclose(f);
}
static void links_load(const char *file, Link *v, int *n, int max) {
    char p[1200]; pref_path(p, sizeof p, file); FILE *f = *g_pref ? fopen(p, "r") : NULL; if (!f) return;
    char line[4096]; int total = 0;
    while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; char *tab = strchr(line, '\t'); if (!tab || tab == line) continue; *tab = 0; link_put(v, n, max, line, tab + 1); total++; }
    fclose(f);
    if (total > *n) links_save(file, v, *n);
}
static int bm_find(const char *url) { for (int i = 0; i < g_nbm; i++) if (!strcmp(g_bms[i].url, url)) return i; return -1; }
static void hist_add(const char *url, const char *title) {
    if (g_nhv && !strcmp(g_hv[g_nhv - 1].url, url)) return;
    link_put(g_hv, &g_nhv, MAX_HV, url, title);
    char p[1200]; pref_path(p, sizeof p, "history.txt"); FILE *f = *g_pref ? fopen(p, "a") : NULL;
    if (f) { fprintf(f, "%s\t%s\n", g_hv[g_nhv - 1].url, g_hv[g_nhv - 1].title); fclose(f); }
}
static double g_full_paint; static bool g_in_layout;
#define MAX_TABS 32
static volatile uint64_t g_tab_gen[MAX_TABS];
static bool gen_live(uint64_t g) { for (int i = 0; i < MAX_TABS; i++) if (g_tab_gen[i] == g) return true; return false; }

static void img_refetch(const char *u);
/* subresource request from document d: SameSite/mixed-content context + Referer under d's Referrer-Policy */
static bool style_csp_ok(Document *d, Node *n, const char *t) {
    if (!d || !d->csp) return true;
    if (!(n->flags & NF_CSP_STYLE_CHECKED)) {
        if (!*t) return true;   /* script-created and still empty: decide once it has text */
        n->flags |= NF_CSP_STYLE_CHECKED;
        if (!csp_allows_style(d->csp, node_attr(n, "nonce"), t, strlen(t))) { n->flags |= NF_CSP_STYLE_BLOCKED; fprintf(stderr, "lumen: CSP style-src blocked inline <style>\n"); }
    }
    return !(n->flags & NF_CSP_STYLE_BLOCKED);
}
static NetRequest *doc_req(Document *d, const char *u, const char *top, const char *csp_dir) {
    NetRequest *rq = net_request_ctx("GET", u, top);
    char *ref = d ? net_referrer(d->url, u, d->referrer_policy) : NULL;
    if (ref) { headers_add(&rq->headers, "Referer", ref); free(ref); }
    if (d && d->url) { rq->no_cors_origin = xstrdup(d->url); rq->coep_require_corp = d->coep_require_corp; }
    if (d && d->csp && csp_dir) { rq->csp = xstrdup(d->csp); rq->csp_doc = xstrdup(d->url ? d->url : ""); rq->csp_dir = csp_dir; }
    return rq;
}
static NetResponse *doc_fetch_sync(Document *d, const char *u, const char *top, const char *csp_dir) {
    NetRequest *rq = doc_req(d, u, top, csp_dir);
    NetResponse *r = net_fetch_sync(rq);
    net_request_free(rq);
    return r;
}
static Image *cache_get(const char *u) {
    SDL_LockMutex(icache_mu); ImgSlot *s = hm_get(&icache, u); Image *im = s ? s->im : NULL; bool want = false; const uint8_t *enc = NULL; size_t en = 0;
    if (s && !g_in_layout) { s->used = (double)SDL_GetTicks(); if (s->evicted && !s->refetch) { s->refetch = true; if (s->enc) { enc = s->enc; en = s->enclen; } else want = true; } }
    SDL_UnlockMutex(icache_mu);
    if (enc) {   /* evicted in Lite mode: decode again from the kept compressed bytes, no network round trip */
        Image *d = image_decode(enc, en);
        SDL_LockMutex(icache_mu);
        if (d && !s->im) { s->im = d; d = NULL; g_icache_bytes += (size_t)s->im->w * (size_t)s->im->h * 4; g_icache_n++; }
        if (s->im) s->evicted = s->refetch = false; else want = true;
        im = s->im;
        SDL_UnlockMutex(icache_mu);
        if (d) image_unref(d);
    }
    if (want) img_refetch(u);
    return im;
}
static const char *page_top_url(Page *p);
static void cache_fetch(Page *p, const char *u) {
    if (!u || !*u || !strncmp(u, "blob:", 5)) return;
    SDL_LockMutex(icache_mu); bool have = hm_get(&icache, u) != NULL; SDL_UnlockMutex(icache_mu);
    if (have) return;
    NetResponse *r = doc_fetch_sync(p->d, u, page_top_url(p), "img-src");
    Image *im = r && r->status == 200 ? image_decode((const uint8_t *)r->body, r->body_len) : NULL;
    if (r) net_response_free(r);
    ImgSlot *slot = xcalloc(1, sizeof *slot); slot->im = im; slot->done = true;
    SDL_LockMutex(icache_mu); if (!hm_get(&icache, u)) { hm_put(&icache, u, slot); if (im) { g_icache_bytes += (size_t)im->w * (size_t)im->h * 4; g_icache_n++; } } else { image_unref(im); free(slot); } SDL_UnlockMutex(icache_mu);
}
Image *cap_frame_for(Node *n); bool cap_screen_active(void); void cap_rec_tick(void); void cap_screen_push(const uint32_t *px, int w, int h, int stride);
static Image *node_img(Node *n) {
    if (n->tag == A_iframe) { Page *fp = frame_find(n); return fp && fp->cv.px ? &fp->img : NULL; }
    if (n->tag == A_video) { Image *f = cap_frame_for(n); if (!f) f = media_frame_for(n); if (f) return f; }
    if (n->tag == A_canvas) return cvbuf_image(n);
    const char *s = node_attr(n, n->tag == A_video ? "poster" : "src"); if (!s) return NULL;
    char *u = url_join(n->doc->url, s); Image *im = u ? cache_get(u) : NULL; free(u); return im;
}
static Image *url_img(const char *u) { return u ? cache_get(u) : NULL; }
static bool img_size(Node *n, float *w, float *h) {
    if (n->tag == A_iframe) return false;
    MpState ms;
    if (n->tag == A_video && media_state_for(n, &ms) && ms.w > 0 && ms.h > 0) { *w = (float)ms.w; *h = (float)ms.h; return true; }
    g_in_layout = true; Image *im = node_img(n); g_in_layout = false;
    if (!im) {
        const char *s = n->tag == A_img ? node_attr(n, "src") : NULL; char *u = s ? url_join(n->doc->url, s) : NULL;
        SDL_LockMutex(icache_mu); ImgSlot *sl = u ? hm_get(&icache, u) : NULL; bool ok = sl && sl->evicted; if (ok) { *w = sl->ew; *h = sl->eh; } SDL_UnlockMutex(icache_mu);
        free(u); return ok;
    }
    *w = image_css_w(im); *h = image_css_h(im); return true; }

typedef struct { char *url; uint64_t gen; float vw, vh; char *body; size_t blen; char *ctype; char *html; char *ref; } LoadReq;

/* Downloads go to $LUMEN_DOWNLOAD_DIR, $XDG_DOWNLOAD_DIR or ~/Downloads; existing files are never overwritten */
static char g_last_download[1024];
static void download_save(const char *name, const void *data, size_t n) {
    char dir[768], nm[256], path[1024];
    const char *d = getenv("LUMEN_DOWNLOAD_DIR"); if (!d) d = getenv("XDG_DOWNLOAD_DIR");
    if (d) snprintf(dir, sizeof dir, "%s", d); else snprintf(dir, sizeof dir, "%s/Downloads", getenv("HOME") ? getenv("HOME") : ".");
    SDL_CreateDirectory(dir);
    const char *b = name ? strrchr(name, '/') : NULL; b = b ? b + 1 : name;
    size_t k = 0; for (const char *c = b ? b : ""; *c && k + 1 < sizeof nm; c++) nm[k++] = *c == '\\' || *c == ':' || (unsigned char)*c < 32 ? '_' : *c;
    nm[k] = 0; while (nm[0] == '.') memmove(nm, nm + 1, strlen(nm));
    if (!nm[0]) snprintf(nm, sizeof nm, "download");
    char *dot = strrchr(nm, '.'); int stem = dot && dot != nm ? (int)(dot - nm) : (int)strlen(nm);
    snprintf(path, sizeof path, "%s/%s", dir, nm);
    for (int i = 1; access(path, F_OK) == 0 && i < 1000; i++) snprintf(path, sizeof path, "%s/%.*s (%d)%s", dir, stem, nm, i, nm + stem);
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(data, 1, n, f) == n; if (f) fclose(f);
    if (ok) { snprintf(g_last_download, sizeof g_last_download, "%s", path); snprintf(g_notice, sizeof g_notice, "Downloaded %s (%.1f KB) to %s", nm, n / 1024.0, dir); }
    else snprintf(g_notice, sizeof g_notice, "Download of %s failed: could not write to %s", nm, dir);
    fprintf(stderr, "lumen: download %s %s (%zu bytes)\n", ok ? "saved" : "FAILED", path, n);
    if (net_wakeup) net_wakeup();
}
/* filename from Content-Disposition (filename*=UTF-8''x or filename="x"), else the URL's last path segment */
static void download_name(const NetResponse *r, const char *url, char *out, size_t n) {
    const char *cd = headers_get(&r->headers, "content-disposition"), *f; out[0] = 0;
    if (cd && (f = strstr(cd, "filename*="))) { f += 10; const char *q = strstr(f, "''"); if (q) f = q + 2; size_t k = 0; for (; *f && *f != ';' && k + 1 < n; f++) { if (*f == '%' && isxdigit((unsigned char)f[1]) && isxdigit((unsigned char)f[2])) { char h[3] = { f[1], f[2], 0 }; out[k++] = (char)strtol(h, NULL, 16); f += 2; } else out[k++] = *f; } out[k] = 0; }
    else if (cd && (f = strstr(cd, "filename="))) { f += 9; bool q = *f == '"'; if (q) f++; size_t k = 0; for (; *f && (q ? *f != '"' : *f != ';') && k + 1 < n; f++) out[k++] = *f; out[k] = 0; }
    if (!out[0]) { const char *e = strpbrk(url, "?#"), *sl = url; for (const char *c = url; *c && (!e || c < e); c++) if (*c == '/') sl = c + 1; snprintf(out, n, "%.*s", (int)((e ? e : url + strlen(url)) - sl), sl); }
}
static bool is_download(const NetResponse *r) {
    const char *cd = headers_get(&r->headers, "content-disposition");
    if (cd && !strncasecmp(cd + strspn(cd, " "), "attachment", 10)) return true;
    const char *ct = headers_get(&r->headers, "content-type"); if (!ct || !*ct) return false;
    static const char *show[] = { "text/", "image/", "application/xhtml", "application/xml", "application/json", "application/javascript", "application/ld+json", "application/rss", "application/atom", "multipart/", NULL };
    for (int i = 0; show[i]; i++) if (!strncasecmp(ct, show[i], strlen(show[i]))) return false;
    return true;
}
static int loader(void *arg) {
    LoadReq *rq = arg; double t0 = now_ms();
    NetRequest *nr = rq->html ? NULL : net_request_new(rq->body ? "POST" : "GET", rq->url);
    if (rq->body) {
        nr->body = rq->body; nr->body_len = rq->blen; rq->body = NULL;
        headers_set(&nr->headers, "Content-Type", rq->ctype && *rq->ctype ? rq->ctype : "application/x-www-form-urlencoded");
    }
    if (nr) {   /* top-level document navigation headers, as Chrome sends them */
        headers_set(&nr->headers, "Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8");
        headers_set(&nr->headers, "Upgrade-Insecure-Requests", "1");
        headers_set(&nr->headers, "Sec-Fetch-Dest", "document");
        headers_set(&nr->headers, "Sec-Fetch-Mode", "navigate");
        headers_set(&nr->headers, "Sec-Fetch-Site", "none");
        headers_set(&nr->headers, "Sec-Fetch-User", "?1");
        if (rq->ref) headers_set(&nr->headers, "Referer", rq->ref);
    }
    NetResponse *r = rq->html ? NULL : net_fetch_sync(nr);
    net_request_free(nr);
    if (r && r->status >= 200 && r->status < 300 && is_download(r)) {
        char nm[256]; download_name(r, r->url ? r->url : rq->url, nm, sizeof nm);
        download_save(nm, r->body ? r->body : "", r->body ? r->body_len : 0);
        SDL_Event ev; SDL_zero(ev); ev.type = EV_DL; ev.user.data1 = (void *)(uintptr_t)rq->gen; SDL_PushEvent(&ev);
        net_response_free(r); free(rq->url); free(rq->body); free(rq->ctype); free(rq->html); free(rq->ref); free(rq);
        return 0;
    }
    Page *p = xcalloc(1, sizeof *p); p->gen = rq->gen;
    char *wrapbuf = NULL;   /* a bare image URL shows the image, like other browsers */
    if (r && r->status >= 200 && r->status < 300 && headers_get(&r->headers, "content-type") && !strncasecmp(headers_get(&r->headers, "content-type"), "image/", 6) && strncasecmp(headers_get(&r->headers, "content-type"), "image/svg", 9)) {
        wrapbuf = xmalloc(strlen(r->url ? r->url : rq->url) * 6 + 256); char *w = wrapbuf + sprintf(wrapbuf, "<body style='margin:0;background:#0e0e0e;display:flex;align-items:center;justify-content:center;min-height:100vh'><img style='max-width:100%%' src=\"");
        for (const char *c = r->url ? r->url : rq->url; *c; c++) w += *c == '"' ? sprintf(w, "&quot;") : *c == '&' ? sprintf(w, "&amp;") : sprintf(w, "%c", *c);
        strcpy(w, "\"></body>");
    }
    const char *body = r && r->body ? r->body : "";
    size_t blen = r && r->body ? r->body_len : 0;
    char *errbuf = NULL;
    if (rq->html) { body = rq->html; blen = strlen(body); }
    else if (wrapbuf) { body = wrapbuf; blen = strlen(body); }
    else if (!r || r->status == 0) { errbuf = xmalloc(512); snprintf(errbuf, 512, "<title>Error</title><body style='font:15px system-ui;padding:40px;color:#444'><h2>Can't open this page</h2><p>%s</p>", rq->url); body = errbuf; blen = strlen(errbuf); }
    p->url = xstrdup(r && r->url ? r->url : rq->url);
    p->d = doc_new(p->url); p->d->referrer = rq->ref; rq->ref = NULL;
    if (r && !rq->html) {
        const char *coop = headers_get(&r->headers, "cross-origin-opener-policy"), *coep = headers_get(&r->headers, "cross-origin-embedder-policy");
        bool secure = !strncmp(p->url, "https:", 6) || !strncmp(p->url, "http://localhost", 16) || !strncmp(p->url, "http://127.0.0.1", 16);
        p->d->coep_require_corp = secure && coep && !strncasecmp(coep, "require-corp", 12);
        p->d->cross_origin_isolated = secure && coop && coep && !strncasecmp(coop, "same-origin", 11) && strncasecmp(coop, "same-origin-allow-popups", 24)
            && (!strncasecmp(coep, "require-corp", 12) || !strncasecmp(coep, "credentialless", 14));
        const char *rp = headers_get(&r->headers, "referrer-policy");
        if (rp) snprintf(p->d->referrer_policy, sizeof p->d->referrer_policy, "%s", rp);
        for (int i = 0; i < r->headers.n; i++)
            if (!strcasecmp(r->headers.v[i].name, "content-security-policy")) p->d->csp = csp_append(p->d->csp, r->headers.v[i].value);
    }
    html_parse(p->d, body, blen);
    for (Node *m = p->d->node.first; m; m = node_next_in_tree(m, &p->d->node)) {  /* <meta name=referrer> overrides the header */
        if (m->type != NODE_ELEMENT || m->tag != A_meta || !node_attr(m, "content")) continue;
        const char *nm = node_attr(m, "name"), *he = node_attr(m, "http-equiv");
        if (nm && !strcasecmp(nm, "referrer")) snprintf(p->d->referrer_policy, sizeof p->d->referrer_policy, "%s", node_attr(m, "content"));
        if (he && !strcasecmp(he, "content-security-policy")) p->d->csp = csp_append(p->d->csp, node_attr(m, "content"));
    }
    if (!getenv("LUMEN_NO_JS")) for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (!gen_live(rq->gen)) break;
        if (n->type != NODE_ELEMENT || n->tag != A_script || n->ns != NS_HTML) continue;
        bool mod = jsg_module_script(n);
        if (!mod && !jsg_classic_script(n)) continue;
        PScript sc = { n, NULL, 0, NULL, mod };
        const char *src = node_attr(n, "src");
        if (src) {
            sc.name = url_join(p->d->url, src);
            if (!sc.name) continue;
            NetResponse *sr = doc_fetch_sync(p->d, sc.name, page_top_url(p), NULL);
            if (sr && sr->status >= 200 && sr->status < 300 && !js_script_mime_blocked(headers_get(&sr->headers, "content-type"))) { sc.src = xstrndup(sr->body ? sr->body : "", sr->body_len); sc.len = sr->body_len; }
            if (sr) net_response_free(sr);
        } else { sc.src = node_text_content(n); sc.len = strlen(sc.src); sc.name = xstrdup(p->url); }
        p->scripts = xrealloc(p->scripts, sizeof *p->scripts * (size_t)(p->nscripts + 1)); p->scripts[p->nscripts++] = sc;
    }
    p->e = style_engine_new(p->d); p->e->media.vw = rq->vw; p->e->media.vh = rq->vh;
    for (Node *n = p->d->node.first; n; n = node_next_deep(n, &p->d->node)) {
        if (!gen_live(rq->gen)) break;
        if (n->type != NODE_ELEMENT) continue;
        if (n->tag == A_style) {
            char *t = node_text_content(n); if (style_csp_ok(p->d, n, t)) { StyleSheet *s = css_parse_sheet(t, strlen(t), p->d->url, 1, &p->e->media); s->owner = n; style_engine_add_sheet(p->e, s); } free(t);
        } else if (n->tag == A_link && node_attr(n, "rel") && strstr(node_attr(n, "rel"), "stylesheet") && node_attr(n, "href")) {
            char *u = url_join(p->d->url, node_attr(n, "href"));
            NetResponse *cr = doc_fetch_sync(p->d, u, page_top_url(p), "style-src");
            if (cr && cr->status == 200) { StyleSheet *s = css_parse_sheet(cr->body, cr->body_len, cr->url, 1, &p->e->media); s->owner = n; style_engine_add_sheet(p->e, s); }
            if (cr) net_response_free(cr); free(u);
        } else if (n->tag == A_img && node_attr(n, "src")) {
            char *u = url_join(p->d->url, node_attr(n, "src")); cache_fetch(p, u); free(u);
        }
    }
    style_recalc(p->e, &p->d->node, true);
    p->seen_ver = p->d->dom_version;
    p->load_ms = now_ms() - t0;
    if (r) net_response_free(r);
    free(errbuf); free(wrapbuf); free(rq->url); free(rq->body); free(rq->ctype); free(rq->html); free(rq->ref); free(rq);
    SDL_Event ev; SDL_zero(ev); ev.type = EV_LOADED; ev.user.data1 = p; SDL_PushEvent(&ev);
    return 0;
}

static void page_free(Page *p) {
    if (!p) return;
    js_free(p->js);
    hm_free(&p->img_fired, NULL);
    free(p->sref); free(p->vids);
    for (int i = 0; i < p->nscripts; i++) { free(p->scripts[i].src); free(p->scripts[i].name); }
    free(p->scripts);
    if (p->L) layout_free(p->L);
    if (p->e) style_engine_free(p->e);
    if (p->d) doc_free(p->d);
    free(p->url); free(p);
}

typedef struct Tab { Page *cur; bool loading, relayout; char *hist[256]; uint64_t hgen[256]; int nhist, hpos; char url[2048]; float sy; uint64_t lgen; int ws;
    double cpu_ms, media_ms0, budget, bud_t, unlimit_until; float pct, lim, cpuhist[600]; int ncpu, cpui, cpumode, vctl, vap, lite; uint64_t vgen; bool limited, info, asleep; double bg_since; int crashes; } Tab;
typedef struct App {
    SDL_Window *win; SDL_MetalView mview; Gpu *gpu;
    int pw, ph; float scale, vw, vh;
    UiViewport view;   /* safe content area within the full-size SDL window */
    PresentRetry present_retry;
    SDL_Cursor *cursors[3]; bool cursor_tried[3];
    Canvas frame, page; DisplayList pdl, cdl, hdl;
    struct DSig *osig, *nsig; int nosig, cosig, cnsig; uint64_t chash; bool osig_ok;   /* last page display list, to raster only what changed */
    Tab *t, *tabs[MAX_TABS]; int ntabs, ti;
    char wsname[16][64]; Tab *wslast[16]; int nws, wi; float side; bool side_set;
    int wsicon[16]; Image *icimg[16][2];
    Image *sym[8][4]; bool vbars;
    int tip_tab; double tip_until;     /* CPU-limit dot tooltip pinned by a click */
    bool editing; int sel_all, page_sel;
    double caret_t;
    bool dirty;
    Font *ui;
    int hover; double frame_ms;
    bool vonly, deferred, vframe; double last_input, last_full; float last_sy; uint64_t last_ver; Page *last_page; bool gvid_ok; const void *vown; float vrect[4]; float srect[4]; bool sdirty;   /* srect: viewport rect of element scrollers to repaint */
} App;
static bool g_bg;   /* UIKit disallows GPU work while the app is backgrounded. */
static void text_area(App *a, Node *n);
#ifdef LUMEN_IOS
static void touch_cancel(void);
#endif
static void set_cursor(App *a, int kind) {
    static const SDL_SystemCursor ids[] = { SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_TEXT, SDL_SYSTEM_CURSOR_POINTER };
    if (!a->cursor_tried[kind]) { a->cursor_tried[kind] = true; a->cursors[kind] = SDL_CreateSystemCursor(ids[kind]); }
    if (a->cursors[kind] && SDL_GetCursor() != a->cursors[kind]) SDL_SetCursor(a->cursors[kind]);
}
static void publish_gens(App *a) { for (int i = 0; i < MAX_TABS; i++) g_tab_gen[i] = i < a->ntabs ? a->tabs[i]->lgen : 0; }
static Tab *tab_new(App *a) {
    if (a->ntabs == MAX_TABS) return NULL;
    Tab *t = xcalloc(1, sizeof *t); t->hpos = -1; t->ws = a->wi; t->vctl = -1; t->lite = -1; t->lim = 0.4f; a->tabs[a->ntabs++] = t;
    if (!a->t) a->t = t;
    return t;
}
static bool is_youtube(const char *u) { const char *h = strstr(u, "://"); if (!h) return false; h += 3; size_t n = strcspn(h, "/?#:"); return n >= 11 && !strncmp(h + n - 11, "youtube.com", 11); }
static bool vctl_on(const Tab *t) { return t->vctl >= 0 ? t->vctl : g_vctl; }
static bool lite_on(const Tab *t) { return t->lite >= 0 ? t->lite : g_lowmem; }
static bool video_bars(App *a, DisplayList *dl);
static void tab_unlimit(Tab *t) { t->limited = t->info = false; t->ncpu = t->cpui = 0; if (t->cur) media_set_limit(t->cur->d, 0); }
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

static bool qparam(const char *q, const char *k, char *out, size_t n) {
    size_t kl = strlen(k);
    for (const char *p = q; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL)
        if (!strncmp(p, k, kl) && p[kl] == '=') { size_t l = strcspn(p + kl + 1, "&#"); if (l >= n) l = n - 1; memcpy(out, p + kl + 1, l); out[l] = 0; return true; }
    return false;
}
static void sb_esc(SB *b, const char *s) {
    for (; *s; s++) switch (*s) {
    case '<': sb_puts(b, "&lt;"); break; case '>': sb_puts(b, "&gt;"); break; case '&': sb_puts(b, "&amp;"); break;
    case '"': sb_puts(b, "&quot;"); break; case '\'': sb_puts(b, "&#39;"); break; default: sb_putc(b, *s);
    }
}
static void ip_toggle(SB *b, const char *key, bool on) { sb_printf(b, "<a class=\"btn%s\" href=\"lumen://set?s=settings&%s=%d\">%s</a>", on ? " on" : "", key, !on, on ? "On" : "Off"); }
static char *internal_page(const char *u) {   /* lumen://newtab?s=bookmarks|history|settings */
    const char *q = strchr(u, '?'); char sec[16] = "bookmarks"; if (q) qparam(q + 1, "s", sec, sizeof sec);
    if (strcmp(sec, "history") && strcmp(sec, "settings")) strcpy(sec, "bookmarks");
    SB b; sb_init(&b);
    sb_puts(&b, "<!doctype html><html><head><meta charset=utf-8><title>New Tab</title><style>"
        "body{margin:0;font:14px -apple-system,system-ui,sans-serif;color:#202124;background:#f6f7f9;display:flex;min-height:100vh}"
        "nav{width:190px;flex:none;min-height:100vh;background:#fff;border-right:1px solid #e3e5e8;padding:24px 12px;box-sizing:border-box}"
        "nav h1{font-size:20px;font-weight:600;margin:0 12px 20px}"
        "nav a{display:block;padding:9px 12px;margin:2px 0;border-radius:8px;color:#3c4043;text-decoration:none}"
        "nav a.on{background:#e8f0fe;color:#1967d2;font-weight:600}"
        "main{flex:1;padding:32px 40px;max-width:820px}"
        "h2{font-size:22px;font-weight:600;margin:0 0 18px}"
        ".card{background:#fff;border:1px solid #e3e5e8;border-radius:12px;padding:4px 18px;margin-bottom:16px}"
        ".row{display:flex;align-items:center;padding:12px 0;border-bottom:1px solid #f0f1f3}"
        ".row:last-child{border-bottom:0}"
        ".grow{flex:1;min-width:0;margin-right:12px}"
        ".t{font-weight:500;overflow:hidden;white-space:nowrap;text-overflow:ellipsis}"
        ".u{color:#5f6368;font-size:12px;margin-top:2px;overflow:hidden;white-space:nowrap;text-overflow:ellipsis}"
        "a.l{color:inherit;text-decoration:none;display:block}"
        ".x{color:#80868b;text-decoration:none;padding:4px 10px;border-radius:6px;font-size:18px}"
        ".btn{display:inline-block;padding:6px 16px;border-radius:16px;background:#e8eaed;color:#202124;text-decoration:none;font:500 14px -apple-system,system-ui,sans-serif;border:0}"
        ".btn.on{background:#1a73e8;color:#fff}"
        ".note{background:#e8f0fe;color:#174ea6;border-radius:8px;padding:10px 12px;margin:0 0 14px;line-height:1.45}"
        ".sub{color:#5f6368;font-size:13px;margin-top:3px;line-height:1.4}"
        "input{width:48px;padding:4px 6px;border:1px solid #dadce0;border-radius:6px;font:14px -apple-system,system-ui,sans-serif;margin:0 4px}"
        ".empty{color:#5f6368;padding:18px 0}"
        "</style></head><body><nav><h1>Lumen</h1>");
    static const char *S[3][2] = { { "bookmarks", "Bookmarks" }, { "history", "History" }, { "settings", "Settings" } };
    for (int i = 0; i < 3; i++) sb_printf(&b, "<a href=\"lumen://newtab?s=%s\"%s>%s</a>", S[i][0], strcmp(sec, S[i][0]) ? "" : " class=on", S[i][1]);
    sb_puts(&b, "</nav><main>");
    if (!strcmp(sec, "settings")) {
        sb_puts(&b, "<h2>Settings</h2><div class=card><div class=row><div class=grow><div class=t>Lite mode</div><div class=sub>Uses less memory on heavy pages such as YouTube.</div></div>");
        ip_toggle(&b, "lite", g_lowmem);
        sb_puts(&b, "</div>");
        if (g_lowmem) sb_puts(&b, "<p class=note>Lite mode is on. Images, video pictures and other media outside the visible part of a page are removed from memory; only the text and layout are kept. They load again when you scroll back to them, so they may appear a moment later. Audio keeps playing.</p>");
        sb_puts(&b, "</div><div class=card><div class=row><div class=grow><div class=t>CPU limit</div><div class=sub>Slows down a tab that keeps the processor busy for a long time.</div></div>");
        ip_toggle(&b, "cpu", g_cpu_on);
        sb_printf(&b, "</div><div class=row><div class=grow>When a tab uses more than <input id=n value=%d>%% CPU for <input id=m value=%d> seconds, limit it to <input id=t value=%d>%%</div><button class=btn onclick=\"save()\">Save</button></div>"
            "<div class=sub style=\"padding:0 0 12px\">To set the limit for one tab only, click the tab, then click it again.</div></div>", g_cpu_pct, g_cpu_secs, g_cpu_lim);
        sb_puts(&b, "<div class=card><div class=row><div class=grow><div class=t>Always show video controls</div><div class=sub>Keeps the play controls visible under every video, including YouTube, so they never hide. To change it for one tab only, click the tab, then click it again.</div></div>");
        ip_toggle(&b, "vctl", g_vctl);
        sb_puts(&b, "</div></div><div class=card><div class=row><div class=grow><div class=t>Sleep background ChatGPT tabs</div><div class=sub>A ChatGPT tab left in the background for a minute stops running and frees most of its memory. It keeps showing its last screen and reloads when you click it. Text you typed but did not send in that tab may be lost.</div></div>");
        ip_toggle(&b, "sleep", g_tab_sleep);
        sb_puts(&b, "</div></div><div class=card><div class=row><div class=grow><div class=t>CPU usage</div><div class=sub>Shows how much CPU and memory (GB) Lumen uses at the bottom of the workspace sidebar, updated every 2 seconds. Click it to see the whole computer's CPU and memory use.</div></div>");
        ip_toggle(&b, "hud", g_hud);
        sb_puts(&b, "</div></div><div class=card><div class=row><div class=grow><div class=t>Smooth scrolling</div><div class=sub>Animates each mouse-wheel step. When off, each wheel step jumps a fixed, larger distance, which uses less CPU. Trackpad scrolling is not affected.</div></div>");
        ip_toggle(&b, "smooth", g_smooth);
        sb_puts(&b, "</div></div><div class=card><div class=row><div class=grow><div class=t>User agent</div><div class=sub>How Lumen identifies itself to websites. Chrome works best on most sites. Reload open pages to apply.</div></div>");
        for (int i = 0; i < UA_COUNT; i++) sb_printf(&b, "<a class=\"btn%s\" href=\"lumen://set?s=settings&ua=%d\">%s</a>", i == g_ua ? " on" : "", i, net_user_agent_name(i));
        {
            static char an[16][128]; static int na = -1, nab = -1;
            if (nab != g_gpu_backend) { na = gpu_adapters(g_gpu_backend, an, 16); nab = g_gpu_backend; }
            sb_puts(&b, "</div></div><div class=card><div class=row><div class=grow><div class=t>Graphics</div><div class=sub>How Lumen draws pages and WebGL/WebGPU content. Now using: ");
            sb_esc(&b, g_gpu_active);
            sb_puts(&b, ". CPU only never uses a software 3D renderer; Lumen's own rasterizer draws everything. Restart Lumen to apply.</div></div></div><div class=row>");
#ifdef __APPLE__
            static const int bk[] = { GPU_BACKEND_AUTO, GPU_BACKEND_METAL, GPU_BACKEND_VULKAN, GPU_BACKEND_CPU }; static const char *bn[] = { "Auto", "Metal", "Vulkan (MoltenVK)", "CPU only" };
#else
            static const int bk[] = { GPU_BACKEND_AUTO, GPU_BACKEND_VULKAN, GPU_BACKEND_GL, GPU_BACKEND_CPU }; static const char *bn[] = { "Auto", "Vulkan", "OpenGL", "CPU only" };
#endif
            for (int i = 0; i < 4; i++) sb_printf(&b, "<a class=\"btn%s\" href=\"lumen://set?s=settings&gpub=%d\">%s</a>", bk[i] == g_gpu_backend ? " on" : "", bk[i], bn[i]);
            if (g_gpu_backend != GPU_BACKEND_CPU) {
                sb_printf(&b, "</div><div class=row><div class=sub>GPU:</div><a class=\"btn%s\" href=\"lumen://set?s=settings&gpua=-1\">Best available</a>", g_gpu_adapter < 0 ? " on" : "");
                for (int i = 0; i < na; i++) { sb_printf(&b, "<a class=\"btn%s\" href=\"lumen://set?s=settings&gpua=%d\">", i == g_gpu_adapter ? " on" : "", i); sb_esc(&b, an[i]); sb_puts(&b, "</a>"); }
                if (!na) sb_puts(&b, "<div class=sub>No hardware GPU found for this backend.</div>");
            }
        }
        sb_puts(&b, "</div></div><script>function save(){var g=function(i){return parseInt(document.getElementById(i).value,10)||0};location.href='lumen://set?s=settings&cpu_pct='+g('n')+'&cpu_secs='+g('m')+'&cpu_lim='+g('t')}</script>");
    } else {
        bool bm = sec[0] == 'b'; Link *v = bm ? g_bms : g_hv; int n = bm ? g_nbm : g_nhv;
        sb_printf(&b, "<h2>%s</h2>", bm ? "Bookmarks" : "History");
        if (!bm && n) sb_puts(&b, "<p><a class=btn href=\"lumen://set?s=history&clearhist=1\">Clear history</a></p>");
        sb_puts(&b, "<div class=card>");
        if (!n) sb_printf(&b, "<div class=empty>%s</div>", bm ? "No bookmarks yet. Click the star next to the address bar to bookmark a page." : "No pages visited yet.");
        for (int i = n - 1, k = 0; i >= 0 && k < 300; i--, k++) {
            sb_puts(&b, "<div class=row><a class=\"l grow\" href=\""); sb_esc(&b, v[i].url); sb_puts(&b, "\"><div class=t>"); sb_esc(&b, v[i].title);
            sb_puts(&b, "</div><div class=u>"); sb_esc(&b, v[i].url); sb_puts(&b, "</div></a>");
            if (bm) sb_printf(&b, "<a class=x title=Remove href=\"lumen://set?s=bookmarks&unbm=%d\">\xC3\x97</a>", i);
            sb_puts(&b, "</div>");
        }
        sb_puts(&b, "</div>");
    }
    sb_puts(&b, "</main></body></html>");
    return sb_take(&b);
}
static void apply_set(App *a, const char *q) {
    char v[32];
    if (qparam(q, "lite", v, sizeof v)) { g_lowmem = atoi(v) != 0; media_lowmem = g_lowmem; }
    if (qparam(q, "cpu", v, sizeof v)) g_cpu_on = atoi(v) != 0;
    if (qparam(q, "vctl", v, sizeof v)) g_vctl = atoi(v) != 0;
    if (qparam(q, "sleep", v, sizeof v)) g_tab_sleep = atoi(v) != 0;
    if (qparam(q, "smooth", v, sizeof v)) { g_smooth = atoi(v) != 0; g_sm_dx = g_sm_dy = 0; }
    if (qparam(q, "hud", v, sizeof v)) { g_hud = atoi(v) != 0; g_hud_pop = false; }
    if (qparam(q, "gpub", v, sizeof v)) { g_gpu_backend = LCLAMP(atoi(v), 0, GPU_BACKEND_METAL); g_gpu_adapter = -1; }
    if (qparam(q, "gpua", v, sizeof v)) g_gpu_adapter = LCLAMP(atoi(v), -1, 15);
    if (qparam(q, "ua", v, sizeof v)) { g_ua = LCLAMP(atoi(v), 0, UA_COUNT - 1); net_set_user_agent(g_ua); }
    if (qparam(q, "cpu_pct", v, sizeof v)) g_cpu_pct = LCLAMP(atoi(v), 10, 100);
    if (qparam(q, "cpu_secs", v, sizeof v)) g_cpu_secs = LCLAMP(atoi(v), 5, 600);
    if (qparam(q, "cpu_lim", v, sizeof v)) g_cpu_lim = LCLAMP(atoi(v), 5, 95);
    for (int i = 0; i < a->ntabs; i++) { Tab *t = a->tabs[i]; t->ncpu = t->cpui = 0; if (!g_cpu_on && !t->cpumode && t->limited) tab_unlimit(t); }
    if (qparam(q, "unbm", v, sizeof v)) { int i = atoi(v); if (i >= 0 && i < g_nbm) { link_del(g_bms, &g_nbm, i); links_save("bookmarks.txt", g_bms, g_nbm); } }
    if (qparam(q, "clearhist", v, sizeof v)) { while (g_nhv) link_del(g_hv, &g_nhv, g_nhv - 1); links_save("history.txt", g_hv, 0); }
    settings_save();
}

static const char *file_kind(const char *name) {
    static const char *const K[][2] = { { ".png", "image" }, { ".jpg", "image" }, { ".jpeg", "image" }, { ".gif", "image" }, { ".webp", "image" },
        { ".bmp", "image" }, { ".svg", "image" }, { ".avif", "image" }, { ".mp4", "video" }, { ".m4v", "video" }, { ".mov", "video" },
        { ".webm", "video" }, { ".mkv", "video" }, { ".mp3", "audio" }, { ".m4a", "audio" }, { ".aac", "audio" }, { ".wav", "audio" },
        { ".ogg", "audio" }, { ".opus", "audio" }, { ".flac", "audio" }, { ".obj", "model" } };
    const char *e = strrchr(name, '.');
    if (e) for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) if (!strcasecmp(e, K[i][0])) return K[i][1];
    return NULL;
}
static int dirent_cmp(const void *x, const void *y) {
    const char *const *a = x, *const *b = y;
    if ((*a)[0] != (*b)[0]) return (*a)[0] == 'd' ? -1 : 1;
    return strcasecmp(*a + 1, *b + 1);
}
static void sb_size(SB *b, double n) {
    static const char *u[] = { "B", "KB", "MB", "GB", "TB" }; int k = 0;
    while (n >= 1024 && k < 4) { n /= 1024; k++; }
    sb_printf(b, k ? "%.1f %s" : "%.0f %s", n, u[k]);
}
#define FV_CSS "<style>*{box-sizing:border-box}body{margin:0;font:13px -apple-system,system-ui,sans-serif;background:#1c1c1e;color:#e5e5e7}" \
    ".bar{position:fixed;top:0;left:0;right:0;height:40px;display:flex;align-items:center;gap:6px;padding:0 12px;background:#2c2c2e;border-bottom:1px solid #3a3a3c;z-index:2}" \
    ".bar .t{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-weight:600}.bar .s{color:#98989d;margin-right:8px}" \
    ".z{border:0;border-radius:6px;padding:4px 8px;background:#3a3a3c;color:#e5e5e7;font:12px system-ui;cursor:pointer}.z.on{background:#0a84ff;color:#fff}" \
    ".stage{position:absolute;top:40px;left:0;right:0;bottom:0;overflow:auto;display:flex}.stage>*{margin:auto;flex:none}" \
    "img,video{display:block;image-rendering:auto}.aud{padding:40px;border-radius:14px;background:#2c2c2e;text-align:center;min-width:360px}.aud b{display:block;margin-bottom:18px;font-size:15px}</style>"
static char *file_page(const char *u) {
    if (strncmp(u, "file://", 7)) return NULL;
    const char *rp = u + 7, *qs = strpbrk(rp, "?#");
    char *path = url_decode(rp, qs ? (size_t)(qs - rp) : strlen(rp));
    struct stat st; char *html = NULL;
    if (stat(path, &st)) { free(path); return NULL; }
    const char *base = strrchr(path, '/'); base = base && base[1] ? base + 1 : path;
    SB b; sb_init(&b);
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        char **ents = NULL; int ne = 0;
        for (struct dirent *de; d && (de = readdir(d));) {
            if (de->d_name[0] == '.') continue;
            char full[2048]; struct stat es; snprintf(full, sizeof full, "%s/%s", path, de->d_name);
            bool dir = !stat(full, &es) && S_ISDIR(es.st_mode);
            ents = xrealloc(ents, sizeof *ents * (size_t)(ne + 1));
            size_t nl = strlen(de->d_name); char *e = xmalloc(nl + 40);
            snprintf(e, nl + 40, "%c%s\t%lld\t%lld", dir ? 'd' : 'f', de->d_name, dir ? -1LL : (long long)es.st_size, (long long)es.st_mtime);
            ents[ne++] = e;
        }
        if (d) closedir(d);
        qsort(ents, (size_t)ne, sizeof *ents, dirent_cmp);
        sb_puts(&b, "<!doctype html><meta charset=utf-8><title>"); sb_esc(&b, path); sb_puts(&b, "</title>" FV_CSS
            "<style>.list{position:absolute;top:40px;left:0;right:0;padding:6px 10px}.r{display:flex;align-items:center;gap:8px;padding:3px 8px;border-radius:5px;color:#e5e5e7;text-decoration:none;line-height:20px}"
            ".r:hover{background:#2c2c2e}.r .n{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.r .m{color:#98989d;font-size:12px;width:90px;text-align:right}"
            ".i{width:16px;height:16px;border-radius:3px;flex:none;font-size:9px;line-height:16px;text-align:center;color:#fff;font-weight:700}"
            ".i.d{background:#0a84ff}.i.image{background:#30d158}.i.video{background:#ff375f}.i.audio{background:#ff9f0a}.i.model{background:#bf5af2}.i.f{background:#636366}</style>");
        sb_puts(&b, "<div class=bar><span class=t>"); sb_esc(&b, path); sb_printf(&b, "</span><span class=s>%d items</span></div><div class=list>", ne);
        if (strcmp(path, "/")) sb_puts(&b, "<a class=r href=\"..\"><span class=\"i d\">\xe2\x86\x91</span><span class=n>..</span></a>");
        for (int i = 0; i < ne; i++) {
            char *nm = ents[i] + 1, *t1 = strchr(nm, '\t'); *t1 = 0;
            long long sz = atoll(t1 + 1); time_t mt = (time_t)atoll(strchr(t1 + 1, '\t') + 1);
            bool dir = ents[i][0] == 'd'; const char *k = dir ? "d" : file_kind(nm);
            sb_puts(&b, "<a class=r href=\"");
            for (const unsigned char *c = (const unsigned char *)nm; *c; c++)
                if (isalnum(*c) || strchr("-_.~", *c)) sb_putc(&b, (char)*c); else sb_printf(&b, "%%%02X", *c);
            sb_printf(&b, "%s\"><span class=\"i %s\"></span><span class=n>", dir ? "/" : "", k ? k : "f");
            sb_esc(&b, nm); sb_puts(&b, "</span><span class=m>");
            if (!dir) sb_size(&b, (double)sz);
            char tb[32]; struct tm tmv; localtime_r(&mt, &tmv); strftime(tb, sizeof tb, "%Y-%m-%d %H:%M", &tmv);
            sb_printf(&b, "</span><span class=m style=width:120px>%s</span></a>", tb);
            free(ents[i]);
        }
        free(ents);
        sb_puts(&b, "</div>");
        html = sb_take(&b);
    } else if (file_kind(base)) {
        const char *k = file_kind(base);
        sb_puts(&b, "<!doctype html><meta charset=utf-8><title>"); sb_esc(&b, base); sb_puts(&b, "</title>" FV_CSS "<div class=bar><span class=t>");
        sb_esc(&b, base); sb_puts(&b, "</span><span class=s id=info>"); sb_size(&b, (double)st.st_size); sb_puts(&b, "</span><span id=zb></span></div><div class=stage id=st>");
        if (!strcmp(k, "image")) { sb_puts(&b, "<img id=m src=\""); sb_esc(&b, base); sb_puts(&b, "\">"); }
        else if (!strcmp(k, "video")) { sb_puts(&b, "<video id=m controls autoplay src=\""); sb_esc(&b, base); sb_puts(&b, "\"></video>"); }
        else if (!strcmp(k, "audio")) { sb_puts(&b, "<div class=aud><b>"); sb_esc(&b, base); sb_puts(&b, "</b><audio controls autoplay src=\""); sb_esc(&b, base); sb_puts(&b, "\"></audio></div>"); }
        else { sb_puts(&b, "<svg id=m xmlns=\"http://www.w3.org/2000/svg\"></svg>"); }
        sb_printf(&b, "</div><script>var KIND='%s',SRC=", k);
        sb_putc(&b, '\''); sb_esc(&b, base); sb_putc(&b, '\'');
        sb_puts(&b, ";\n"
            "var Z=[10,25,50,75,100,150,200,300],z=100,m=document.getElementById('m'),zb=document.getElementById('zb'),dpr=window.devicePixelRatio||1,nw=0,nh=0;\n"
            "function draw(){if(KIND=='model')return obj.render();if(!nw)return;m.style.width=(nw/dpr*z/100)+'px';m.style.height=(nh/dpr*z/100)+'px'}\n"
            "function setZ(v){z=v;[].forEach.call(zb.children,function(b){b.className='z'+(+b.dataset.z==z?' on':'')});draw()}\n"
            "function step(d){var i=Z.indexOf(z)+d;if(i>=0&&i<Z.length)setZ(Z[i])}\n"
            "if(KIND!='audio')Z.forEach(function(v){var b=document.createElement('button');b.className='z';b.dataset.z=v;b.textContent=v+'%';b.onclick=function(){setZ(v)};zb.appendChild(b)});\n"
            "document.addEventListener('keydown',function(e){if(e.key=='+'||e.key=='=')step(1);else if(e.key=='-')step(-1);else if(e.key=='0')setZ(100)});\n"
            "document.addEventListener('wheel',function(e){if(e.ctrlKey||e.metaKey){e.preventDefault();step(e.deltaY<0?1:-1)}},{passive:false});\n"
            "function info(t){document.getElementById('info').textContent+=' \\u00b7 '+t}\n"
            "if(KIND=='image')m.onload=function(){nw=m.naturalWidth;nh=m.naturalHeight;info(nw+'\\u00d7'+nh);setZ(100)};\n"
            "if(KIND=='video')m.addEventListener('loadedmetadata',function(){nw=m.videoWidth;nh=m.videoHeight;info(nw+'\\u00d7'+nh);setZ(100)});\n"
            "var obj={v:[],f:[],ry:0.6,rx:-0.4,render:function(){var W=Math.max(200,innerWidth*z/100),H=Math.max(200,(innerHeight-40)*z/100),s=Math.min(W,H)*0.4/this.r,cy=Math.cos(this.ry),sy=Math.sin(this.ry),cx=Math.cos(this.rx),sx=Math.sin(this.rx),P=[],self=this;\n"
            " m.setAttribute('width',W);m.setAttribute('height',H);m.setAttribute('viewBox','0 0 '+W+' '+H);\n"
            " this.v.forEach(function(p){var x=p[0]-self.c[0],y=p[1]-self.c[1],q=p[2]-self.c[2],X=x*cy+q*sy,Q=-x*sy+q*cy,Y=y*cx-Q*sx;Q=y*sx+Q*cx;P.push([W/2+X*s,H/2-Y*s,Q])});\n"
            " var L=[];this.f.forEach(function(f){var a=P[f[0]],b=P[f[1]],c=P[f[2]];if(!a||!b||!c)return;var n=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);var z0=0;f.forEach(function(i){z0+=P[i]?P[i][2]:0});L.push([z0/f.length,f,n])});\n"
            " L.sort(function(a,b){return a[0]-b[0]});var o=[];L.forEach(function(e){var ux=0,uy=0,uz=0,f=e[1],a=P[f[0]],b=P[f[1]],c=P[f[2]];var v1=[b[0]-a[0],b[1]-a[1],b[2]-a[2]],v2=[c[0]-a[0],c[1]-a[1],c[2]-a[2]];ux=v1[1]*v2[2]-v1[2]*v2[1];uy=v1[2]*v2[0]-v1[0]*v2[2];uz=v1[0]*v2[1]-v1[1]*v2[0];var l=Math.sqrt(ux*ux+uy*uy+uz*uz)||1,sh=Math.abs(uz/l)*0.75+0.2,g=Math.round(70+150*sh);\n"
            "  o.push('<polygon points=\"'+f.map(function(i){return P[i][0].toFixed(1)+','+P[i][1].toFixed(1)}).join(' ')+'\" fill=\"rgb('+g+','+g+','+Math.round(g*1.08)+')\" stroke=\"rgba(0,0,0,.15)\" stroke-width=\".5\"/>')});m.innerHTML=o.join('')}};\n"
            "if(KIND=='model'){fetch(SRC).then(function(r){return r.text()}).then(function(t){var mn=[1e9,1e9,1e9],mx=[-1e9,-1e9,-1e9];t.split('\\n').forEach(function(l){var p=l.trim().split(/\\s+/);if(p[0]=='v'){var v=[+p[1],+p[2],+p[3]];for(var k=0;k<3;k++){mn[k]=Math.min(mn[k],v[k]);mx[k]=Math.max(mx[k],v[k])}obj.v.push(v)}else if(p[0]=='f'){var f=p.slice(1).map(function(s){var i=parseInt(s,10);return i<0?obj.v.length+i:i-1});for(var k=1;k+1<f.length;k++)obj.f.push([f[0],f[k],f[k+1]])}});\n"
            " obj.c=[0,1,2].map(function(k){return (mn[k]+mx[k])/2});obj.r=Math.max(mx[0]-mn[0],mx[1]-mn[1],mx[2]-mn[2])/2||1;info(obj.v.length+' vertices, '+obj.f.length+' triangles');setZ(100);\n"
            " var dg=null,pend=false;m.addEventListener('pointerdown',function(e){dg=[e.clientX,e.clientY]});addEventListener('pointerup',function(){dg=null});addEventListener('pointermove',function(e){if(!dg)return;obj.ry+=(e.clientX-dg[0])*0.01;obj.rx+=(e.clientY-dg[1])*0.01;dg=[e.clientX,e.clientY];if(!pend){pend=true;requestAnimationFrame(function(){pend=false;obj.render()})}})})}\n"
            "</script>");
        html = sb_take(&b);
    } else sb_free(&b);
    free(path);
    return html;
}

static char *g_nav_ref;   /* Referer for the next navigate_ex, set by page-initiated navigations */
static void nav_from(Document *d, const char *u) { free(g_nav_ref); g_nav_ref = d && d->url && u ? net_referrer(d->url, u, d->referrer_policy) : NULL; }
static void navigate_ex(App *a, const char *url, bool push, const char *body, size_t blen, const char *ctype) {
    char *u = normalize_url(url), *html = NULL, *ref = g_nav_ref; g_nav_ref = NULL;
    if (!strncmp(u, "lumen://", 8)) {
        if (!strncmp(u, "lumen://set?", 12)) {   /* only Lumen's own pages may change settings */
            if (strncmp(a->t->url, "lumen://", 8)) { free(u); free(ref); return; }
            apply_set(a, u + 12);
            char sec[16] = "settings"; qparam(u + 12, "s", sec, sizeof sec);
            free(u); u = xmalloc(64); snprintf(u, 64, "lumen://newtab?s=%s", sec); push = false;
        }
        html = internal_page(u);
    } else if (!body && (!strcmp(u, "about:blank") || !strncmp(u, "about:blank#", 12) || !strncmp(u, "about:blank?", 12))) {
        html = xstrdup("<!doctype html><html><head></head><body></body></html>");
    } else if (!body) html = file_page(u);
    if (push) {
        for (int i = a->t->hpos + 1; i < a->t->nhist; i++) free(a->t->hist[i]);
        a->t->nhist = a->t->hpos + 1;
        if (a->t->nhist == 256) { free(a->t->hist[0]); memmove(a->t->hist, a->t->hist + 1, sizeof(char *) * 255); memmove(a->t->hgen, a->t->hgen + 1, sizeof(uint64_t) * 255); a->t->nhist--; }
        a->t->hist[a->t->nhist++] = xstrdup(u); a->t->hpos = a->t->nhist - 1;
    }
    snprintf(a->t->url, sizeof a->t->url, "%s", u);
    a->editing = false; a->t->loading = true; a->dirty = true;
    LoadReq *rq = xcalloc(1, sizeof *rq); rq->url = u; rq->ref = ref; rq->html = html; rq->gen = ++load_gen; rq->vw = a->vw - a->side; rq->vh = a->vh - BAR; a->t->lgen = rq->gen; publish_gens(a);
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

enum { HB_NONE, HB_BACK, HB_FWD, HB_RELOAD, HB_URL, HB_NEWTAB, HB_WSNEW, HB_STAR, HB_HUD, HB_TAB = 100, HB_TABX = 200, HB_WS = 300, HB_INFO = 400, HB_TABDOT = 500 };
static void info_btn(App *a, int k, float *x, float *w) {
    static const float W[4] = { 70, 76, 82, 28 }; float r = a->vw - 12;
    for (int i = 3; i >= k; i--) r -= W[i] + (i < 3 ? 6 : 0);
    *x = r; *w = W[k];
}
static int utf8_len(const char *q) { unsigned char c = (unsigned char)*q; int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4; for (int k = 1; k < n; k++) if (!q[k]) return k; return n; }
static const uint8_t app_icon_png[] = {
#include "icon.inc"
};
static void set_window_icon(SDL_Window *w) {
    Image *im = image_decode(app_icon_png, sizeof app_icon_png);
    if (!im) return;
    SDL_Surface *s = SDL_CreateSurface(im->w, im->h, SDL_PIXELFORMAT_ARGB8888);
    if (s) {
        for (int y = 0; y < im->h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch);
            for (int x = 0; x < im->w; x++) {
                uint32_t p = im->px[(size_t)y * (size_t)im->w + (size_t)x], al = p >> 24;
                row[x] = al ? (al << 24) | (((p >> 16) & 255) * 255 / al) << 16 | (((p >> 8) & 255) * 255 / al) << 8 | ((p & 255) * 255 / al) : 0;
            }
        }
        SDL_SetWindowIcon(w, s);
        SDL_DestroySurface(s);
    }
    image_unref(im);
}
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
static float bar_x0(App *a) { return g_compact ? TLW : a->side; }
static float url_w(App *a) { return a->vw - bar_x0(a) - 144 - (g_compact ? 92 : 12); }
static int bar_hit(App *a, float x, float y) {
    if (x < 0 || y < 0 || x >= a->vw || y >= a->vh) return HB_NONE;
    if (g_compact && y < TB) {
        x -= TLW;
        if (x < 0) return HB_NONE;
        if (x < 40) return HB_BACK; if (x < 72) return HB_FWD; if (x < 104) return HB_RELOAD; if (x < 136) return HB_STAR;
        float ue = 144 + url_w(a);
        return x < 144 ? HB_NONE : x < ue ? HB_URL : x >= ue + 4 && x < ue + 32 ? HB_NEWTAB : HB_NONE;
    }
    if (y < TABH) {
        int v[MAX_TABS], n = ws_tabs(a, a->wi, v), k; float tw = tab_w(a), x0 = TLW + 6; k = (int)((x - x0) / tw);
        if (x >= x0 && k < n) { float lx = x - x0 - k * tw; return lx > tw - 28 ? HB_TABX + v[k] : a->tabs[v[k]]->limited && lx < 26 ? HB_TABDOT + v[k] : HB_TAB + v[k]; }
        if (x >= x0 + n * tw && x < x0 + n * tw + 32) return HB_NEWTAB;
        return HB_NONE;
    }
    if (x < a->side) {
        if (g_hud && y >= a->vh - HUDH) return HB_HUD;
        int r = (int)floorf((y - WSY0) / WSRH);
        if (y >= WSY0 && r < a->nws) return HB_WS + r;
        return y >= WSY0 && r == a->nws ? HB_WSNEW : HB_NONE;
    }
    if (y > BAR) return HB_NONE;
    if (y >= TABH + TB) { for (int k = 0; k < 4; k++) { float bx, bw; info_btn(a, k, &bx, &bw); if (x >= bx && x < bx + bw) return HB_INFO + k; } return HB_NONE; }
    x -= a->side;
    if (x < 40) return HB_BACK; if (x < 72) return HB_FWD; if (x < 104) return HB_RELOAD; if (x < 136) return HB_STAR;
    return HB_URL;
}

static void push_img(DisplayList *dl, Image *im, float x, float y, float w, float h) { DItem it; memset(&it, 0, sizeof it); it.op = DO_IMAGE; it.x = x; it.y = y; it.w = w; it.h = h; it.img = im; it.alpha = 1; vec_push(dl->items, it); }
static Image *sym_icon(App *a, int k, int c) {   /* k: 0 back 1 forward 2 reload 3 star 4 star.fill 5 play 6 pause 7 plus; c: 0 dark 1 grey 2 blue 3 white */
#ifdef __APPLE__
    static const char *N[8] = { "chevron.left", "chevron.right", "arrow.clockwise", "star", "star.fill", "play.fill", "pause.fill", "plus" };
    static const uint32_t C[4] = { 0x3c4043, 0xbdc1c6, 0x1a73e8, 0xffffff };
    Image **slot = &a->sym[k][c];
    if (!*slot) {
        int px = (int)(16 * a->scale + 0.5f); Image *im = xcalloc(1, sizeof *im);
        im->w = im->h = px; im->refs = 1; im->scale = a->scale; im->px = xcalloc((size_t)px * (size_t)px, 4);
        if (!mac_symbol_rgba(N[k], px, C[c], im->px)) { free(im->px); free(im); return NULL; }
        *slot = im;
    }
    return *slot;
#else
    (void)a; (void)k; (void)c; return NULL;
#endif
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
    char msg[64]; snprintf(msg, sizeof msg, "CPU use of this tab is limited to %d%%", (int)(a->tabs[i]->lim * 100 + 0.5f));
    float tw = tab_w(a), w = text_width(a->ui, msg, strlen(msg), 0) + 20, x = TLW + 6 + k * tw + 15 - w / 2, y = TABH + 3;
    if (x + w > a->vw - 6) x = a->vw - 6 - w;
    if (x < 6) x = 6;
    push_rect(&a->cdl, x, y, w, 26, 6, RGBA(45, 45, 48, 240));
    push_text(&a->cdl, a->ui, msg, x + 10, y + 13 + (a->ui->ascent - a->ui->descent) / 2, w, RGBA(255, 255, 255, 255));
}
static void build_hud_pop(App *a) {
    DisplayList *dl = &a->hdl; dl_clear(dl);
    if (!g_hud || !g_hud_pop || a->side <= 0) return;
    float w = 150, h = 62, x = 8, y = a->vh - HUDH - h - 2, rb = (a->ui->ascent - a->ui->descent) / 2;
    g_pop[0] = x; g_pop[1] = y; g_pop[2] = x + w; g_pop[3] = y + h;
    push_rect(dl, x, y, w, h, 0, RGBA(208, 212, 218, 255));
    push_rect(dl, x + 1, y + 1, w - 2, h - 2, 0, RGBA(255, 255, 255, 255));
    static const char *lb[2] = { "Host CPU", "Host RAM" };
    for (int k = 0; k < 2; k++) {
        char v[16]; float cy = y + 19 + k * 24 + rb;
        snprintf(v, sizeof v, k ? "%.1f" : "%.0f", k ? g_host_ram : g_host_cpu);
        push_text(dl, a->ui, lb[k], x + 12, cy, 80, RGBA(95, 99, 104, 255));
        float tw = text_width(a->ui, v, strlen(v), 0);
        push_text(dl, a->ui, v, x + w - 12 - tw, cy, 60, RGBA(32, 33, 36, 255));
    }
}
/* this process's CPU (% of one core since the last call) and memory footprint (GB), plus the host's CPU (%) and used memory (GB) */
static void hud_sample(void) {
    static double lt; static uint64_t lu, hb, ht; double now = now_ms();
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    uint64_t us = (uint64_t)ru.ru_utime.tv_sec * 1000000 + (uint64_t)ru.ru_utime.tv_usec + (uint64_t)ru.ru_stime.tv_sec * 1000000 + (uint64_t)ru.ru_stime.tv_usec;
    if (lt && now > lt) g_hud_cpu = (float)((double)(us - lu) / ((now - lt) * 10.0));
    lt = now; lu = us;
#ifdef __APPLE__
    task_vm_info_data_t vi; mach_msg_type_number_t c = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &c) == KERN_SUCCESS) g_hud_ram = (float)(vi.phys_footprint / 1073741824.0);
    host_cpu_load_info_data_t cl; c = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cl, &c) == KERN_SUCCESS) {
        uint64_t busy = (uint64_t)cl.cpu_ticks[CPU_STATE_USER] + cl.cpu_ticks[CPU_STATE_SYSTEM] + cl.cpu_ticks[CPU_STATE_NICE], tot = busy + cl.cpu_ticks[CPU_STATE_IDLE];
        if (ht && tot > ht) g_host_cpu = (float)(100.0 * (double)(busy - hb) / (double)(tot - ht));
        hb = busy; ht = tot;
    }
    vm_statistics64_data_t vs; c = HOST_VM_INFO64_COUNT; vm_size_t pg = 0;
    if (host_page_size(mach_host_self(), &pg) == KERN_SUCCESS && host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &c) == KERN_SUCCESS)   /* Activity Monitor's "Memory Used" */
        g_host_ram = (float)(((double)vs.internal_page_count - vs.purgeable_count + vs.wire_count + vs.compressor_page_count) * (double)pg / 1073741824.0);
#else
    FILE *f = fopen("/proc/self/statm", "r");
    if (f) { unsigned long sz = 0, rss = 0, shr = 0; if (fscanf(f, "%lu %lu %lu", &sz, &rss, &shr) == 3) g_hud_ram = (float)((double)(rss - shr) * (double)sysconf(_SC_PAGESIZE) / 1073741824.0); fclose(f); }
    if ((f = fopen("/proc/stat", "r"))) {
        unsigned long long u, ni, sy, id, io, ir, so, st;
        if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &ni, &sy, &id, &io, &ir, &so, &st) == 8) {
            uint64_t busy = u + ni + sy + ir + so + st, tot = busy + id + io;
            if (ht && tot > ht) g_host_cpu = (float)(100.0 * (double)(busy - hb) / (double)(tot - ht));
            hb = busy; ht = tot;
        }
        fclose(f);
    }
    if ((f = fopen("/proc/meminfo", "r"))) {
        char ln[128]; unsigned long long tot = 0, avail = 0, v;
        while (fgets(ln, sizeof ln, f)) { if (sscanf(ln, "MemTotal: %llu", &v) == 1) tot = v; else if (sscanf(ln, "MemAvailable: %llu", &v) == 1) avail = v; }
        if (tot) g_host_ram = (float)((double)(tot - avail) / 1048576.0);
        fclose(f);
    }
#endif
}
static void build_chrome(App *a) {
    build_hud_pop(a);
    DisplayList *dl = &a->cdl; dl_clear(dl);
    float x0 = bar_x0(a), tbw = a->vw - x0;
    if (g_compact) { push_rect(dl, 0, 0, a->vw, TB, 0, RGBA(250, 250, 250, 255)); push_rect(dl, 0, TB - 1, a->vw, 1, 0, RGBA(226, 226, 226, 255)); }
    else push_rect(dl, 0, 0, a->vw, TABH, 0, RGBA(222, 225, 230, 255));
    int vt[MAX_TABS], nvt = g_compact ? 0 : ws_tabs(a, a->wi, vt);
    float tabw = tab_w(a), tx0 = TLW + 6, tbase = 4 + (TABH - 4) / 2 + (a->ui->ascent - a->ui->descent) / 2;
    for (int k = 0; k < nvt; k++) {
        int i = vt[k]; Tab *t = a->tabs[i]; float x = tx0 + k * tabw;
        if (i == a->ti) push_rect(dl, x, 4, tabw - 2, TABH - 4, 7, RGBA(250, 250, 250, 255));
        else if (a->hover == HB_TAB + i || a->hover == HB_TABX + i) push_rect(dl, x, 6, tabw - 2, TABH - 10, 6, RGBA(235, 237, 240, 255));
        else if (k + 1 < nvt && vt[k + 1] != a->ti) push_rect(dl, x + tabw - 2, 9, 1, TABH - 16, 0, RGBA(170, 175, 180, 255));
        const char *title = t->cur && t->cur->d && t->cur->d->title && *t->cur->d->title ? t->cur->d->title : *t->url ? t->url : "New Tab";
        if (t->limited) push_rect(dl, x + 10, (4 + TABH) / 2 - 5, 10, 10, 5, RGBA(52, 199, 89, 255));
        push_text(dl, a->ui, title, x + (t->limited ? 26 : 12), tbase, tabw - (t->limited ? 54 : 40), lite_on(t) ? RGBA(0, 100, 0, 255) : RGBA(40, 40, 40, 255));
        push_text(dl, a->ui, "\xC3\x97", x + tabw - 22, tbase, 16, a->hover == HB_TABX + i ? RGBA(20, 20, 20, 255) : RGBA(110, 110, 110, 255));
        if (t->loading) push_rect(dl, x + 8, TABH - 3, (tabw - 18) * 0.35f, 2, 1, RGBA(66, 133, 244, 255));
    }
    if (!g_compact) {
        float npx = tx0 + nvt * tabw + 8;
        if (a->hover == HB_NEWTAB) push_rect(dl, npx - 4, 6, 24, 20, 10, RGBA(235, 237, 240, 255));
        push_text(dl, a->ui, "+", npx + 4, tbase, 16, RGBA(60, 60, 60, 255));
    }
    if (a->side > 0) {
        float rb = (a->ui->ascent - a->ui->descent) / 2, bw = a->side - 20;
        push_rect(dl, 0, SIDEY, a->side, a->vh - SIDEY, 0, RGBA(236, 238, 241, 255));
        push_rect(dl, a->side - 1, SIDEY, 1, a->vh - SIDEY, 0, RGBA(214, 217, 222, 255));
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
        if (g_hud) {
            static Font *sf; if (!sf) sf = font_get("system-ui", 400, false, 11.f);
            float hy = a->vh - HUDH, sb = (sf->ascent - sf->descent) / 2;
            if (a->hover == HB_HUD || g_hud_pop) push_rect(dl, 6, hy + 4, a->side - 12, HUDH - 10, 8, RGBA(222, 225, 230, 255));
            static const char *lb[2] = { "CPU", "RAM" };
            for (int k = 0; k < 2; k++) {
                char v[16]; float cy = hy + 15 + k * 17 + sb;
                snprintf(v, sizeof v, k ? "%.1f" : "%.0f", k ? g_hud_ram : g_hud_cpu);
                push_text(dl, sf, lb[k], 14, cy, 30, RGBA(110, 114, 120, 255));
                float tw = text_width(sf, v, strlen(v), 0);
                push_text(dl, sf, v, a->side - 14 - tw, cy, 44, RGBA(32, 33, 36, 255));
            }
        }
    }
    if (g_info_h > 0) {
        float y0 = TABH + TB, rb = (a->ui->ascent - a->ui->descent) / 2, cy = y0 + g_info_h / 2 + rb;
        push_rect(dl, a->side, y0, a->vw - a->side, g_info_h, 0, RGBA(254, 247, 224, 255));
        push_rect(dl, a->side, y0 + g_info_h - 1, a->vw - a->side, 1, 0, RGBA(230, 214, 160, 255));
        if (*g_notice) {
            push_rect(dl, a->side, y0, a->vw - a->side, g_info_h, 0, RGBA(253, 236, 234, 255));
            push_rect(dl, a->side, y0 + g_info_h - 1, a->vw - a->side, 1, 0, RGBA(232, 180, 172, 255));
            push_text(dl, a->ui, g_notice, a->side + 14, cy, a->vw - a->side - 80, RGBA(110, 30, 20, 255));
            float bx, bwid; info_btn(a, 3, &bx, &bwid);
            if (a->hover == HB_INFO + 3) push_rect(dl, bx, y0 + 6, bwid, g_info_h - 12, 6, RGBA(245, 210, 204, 255));
            float lw = text_width(a->ui, "\xC3\x97", 2, 0);
            push_text(dl, a->ui, "\xC3\x97", bx + (bwid - lw) / 2, cy, bwid, RGBA(110, 30, 20, 255));
        } else {
        char msg[200]; int lp = (int)(a->t->lim * 100 + 0.5f);
        if (a->t->cpumode == 2) snprintf(msg, sizeof msg, "You limited this tab to %d%% CPU. Remove the limit for:", lp);
        else snprintf(msg, sizeof msg, "This tab used over %d%% CPU for %d s, so Lumen limited it to %d%%. Remove the limit for:", g_cpu_pct, g_cpu_secs, lp);
        push_text(dl, a->ui, msg, a->side + 14, cy, a->vw - a->side - 310, RGBA(60, 50, 20, 255));
        static const char *lb[4] = { "1 hour", "4 hours", "10 hours", "\xC3\x97" };
        for (int k = 0; k < 4; k++) {
            float bx, bwid; info_btn(a, k, &bx, &bwid);
            if (k < 3 || a->hover == HB_INFO + k) push_rect(dl, bx, y0 + 6, bwid, g_info_h - 12, 6, a->hover == HB_INFO + k ? RGBA(240, 228, 190, 255) : RGBA(255, 255, 255, 255));
            float lw = text_width(a->ui, lb[k], strlen(lb[k]), 0);
            push_text(dl, a->ui, lb[k], bx + (bwid - lw) / 2, cy, bwid, RGBA(60, 50, 20, 255));
        }
        }
    }
    int n0 = dl->items.n;
    push_rect(dl, 0, 0, tbw, TB, 0, RGBA(250, 250, 250, 255));
    push_rect(dl, 0, TB - 1, tbw, 1, 0, RGBA(226, 226, 226, 255));
    Color on = RGBA(60, 60, 60, 255), off = RGBA(190, 190, 190, 255);
    int hk[3] = { HB_BACK, HB_FWD, HB_RELOAD };
    for (int i = 0; i < 3; i++) {
        float cx = 22.f + i * 32.f;
        if (a->hover == hk[i]) push_rect(dl, cx - 13, TB / 2 - 13, 26, 26, 13, RGBA(232, 232, 232, 255));
        bool en = hk[i] == HB_BACK ? a->t->hpos > 0 : hk[i] == HB_FWD ? a->t->hpos < a->t->nhist - 1 : true;
        Image *si = sym_icon(a, i, en ? 0 : 1); if (si) push_img(dl, si, cx - 8, TB / 2 - 8, 16, 16); else draw_icon(dl, hk[i], cx, TB / 2, en ? on : off);
    }
    {
        bool bmd = !a->editing && bm_find(a->t->url) >= 0; float cx = 22.f + 3 * 32.f;
        if (a->hover == HB_STAR) push_rect(dl, cx - 13, TB / 2 - 13, 26, 26, 13, RGBA(232, 232, 232, 255));
        Image *si = sym_icon(a, bmd ? 4 : 3, bmd ? 2 : 0);
        if (si) push_img(dl, si, cx - 8, TB / 2 - 8, 16, 16); else push_text(dl, a->ui, bmd ? "\xE2\x98\x85" : "\xE2\x98\x86", cx - 7, TB / 2 + (a->ui->ascent - a->ui->descent) / 2, 16, on);
    }
    float ux = 144, uw = url_w(a);
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
    if (g_compact) {
        float cx = ux + uw + 18;
        if (a->hover == HB_NEWTAB) push_rect(dl, cx - 13, TB / 2 - 13, 26, 26, 13, RGBA(232, 232, 232, 255));
        Image *si = sym_icon(a, 7, 0);
        if (si) push_img(dl, si, cx - 8, TB / 2 - 8, 16, 16); else { push_rect(dl, cx - 6, TB / 2 - 1, 12, 2, 1, on); push_rect(dl, cx - 1, TB / 2 - 6, 2, 12, 1, on); }
    }
    if (a->t->loading) push_rect(dl, 0, TB - 2, tbw * 0.35f, 2, 0, RGBA(66, 133, 244, 255));
    for (int i = n0; i < dl->items.n; i++) { DItem *it = &dl->items.v[i]; it->y += TABH; it->x += x0; if (it->op == DO_TEXT) for (int g = 0; g < it->ng; g++) { it->g[g].y += TABH; it->g[g].x += x0; } }
}

static float max_scroll(App *a) { return a->t->cur && a->t->cur->L ? LMAX(0, a->t->cur->L->doc_h - (a->vh - BAR)) : 0; }
/* Viewport scrolling is synchronous; its document event is queued/coalesced
   for the host turn. This also covers keyboard, anchors and relayout. */
static bool page_scroll(App *a, float y) {
    Page *p = a->t->cur; if (!p) return false;
    y = isfinite(y) ? LCLAMP(y, 0, max_scroll(a)) : 0;
    /* layout_hit maps fixed descendants using the layout's viewport offset;
       keep it current before input/elementFromPoint, even before painting. */
    if (p->L) { p->L->scroll_x = 0; p->L->scroll_y = y; }
    if (y == a->t->sy) return false;
    a->t->sy = y; p->scroll_pending = true; a->dirty = true;
    return true;
}

static double g_tr, g_tl, g_td, g_tx;
static bool g_no_paint_only;
static void update_compact(App *a) {
    int v[MAX_TABS]; bool c = ws_tabs(a, a->wi, v) <= 1;
    if (c == g_compact) return;
    g_compact = c; a->hover = HB_NONE; a->vonly = false;
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
}
/* per display item: content hash + device-independent bounds, so a DOM change only rasters the area whose items changed */
struct DSig { uint64_t h; float b[4]; bool vid; };
static uint64_t hmix(uint64_t h, const void *p, size_t n) { const uint8_t *c = p; for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull; return h; }
#define HMIX(h, v) h = hmix(h, &(v), sizeof(v))
static uint64_t ditem_hash(const DItem *it) {
    uint64_t h = 1469598103934665603ull;
    HMIX(h, it->op); HMIX(h, it->x); HMIX(h, it->y); HMIX(h, it->w); HMIX(h, it->h); HMIX(h, it->r); HMIX(h, it->color);
    HMIX(h, it->bw); HMIX(h, it->bc); HMIX(h, it->bs); HMIX(h, it->img); if (it->img && it->img->anim) HMIX(h, it->img->px); if (it->img) HMIX(h, it->img->ver); HMIX(h, it->blur); HMIX(h, it->spread); HMIX(h, it->alpha); HMIX(h, it->inset);
    if (it->grad) { const Gradient *g = it->grad; HMIX(h, g->type); HMIX(h, g->angle); HMIX(h, g->nstops); HMIX(h, g->repeating); h = hmix(h, g->stops, sizeof g->stops[0] * (size_t)LCLAMP(g->nstops, 0, 8)); }
    for (int i = 0; i < it->ng; i++) { const DGlyph *g = &it->g[i]; HMIX(h, g->gid); HMIX(h, g->x); HMIX(h, g->y); HMIX(h, g->font); }
    HMIX(h, it->ts);
    return h;
}
static void bunion(float *a, const float *b) { a[0] = LMIN(a[0], b[0]); a[1] = LMIN(a[1], b[1]); a[2] = LMAX(a[2], b[2]); a[3] = LMAX(a[3], b[3]); }
/* union of the bounds of items present in only one of the two display-list signatures: common prefix/suffix are skipped and
   the middle is compared as a multiset, so inserting one item (e.g. a typed glyph) doesn't dirty everything painted after it.
   Video frames are left to the caller's per-video rects; only a video that moved or went away adds its old box. */
/* union only the part of b inside the viewport: off-screen carousel slides and clips must not widen the damage */
static void vunion(float *dm, float vw, float vh, const float *b) {
    float c[4] = { LMAX(b[0], 0), LMAX(b[1], 0), LMIN(b[2], vw), LMIN(b[3], vh) };
    if (c[2] > c[0] && c[3] > c[1]) bunion(dm, c);
}
static const DisplayList *g_dbg_dmg;
static void dsig_damage(const struct DSig *o, int no, const struct DSig *q, int nq, float vw, float vh, float *dm) {
    int pre = 0, suf = 0;
    while (pre < no && pre < nq && o[pre].h == q[pre].h) pre++;
    while (suf < no - pre && suf < nq - pre && o[no - 1 - suf].h == q[nq - 1 - suf].h) suf++;
    int mo = no - pre - suf, mq = nq - pre - suf;
    if (!mo && !mq) return;
    int cap = 16; while (cap < (mo + mq) * 2) cap <<= 1;
    struct { uint64_t h; int c; } *t = calloc((size_t)cap, sizeof *t);
    if (!t) { for (int i = pre; i < no - suf; i++) vunion(dm, vw, vh, o[i].b); for (int i = pre; i < nq - suf; i++) vunion(dm, vw, vh, q[i].b); return; }
    for (int i = pre; i < no - suf; i++) {
        if (o[i].vid) continue;
        uint64_t h = o[i].h | 1; size_t k = (size_t)(h * 0x9E3779B97F4A7C15ull) & (size_t)(cap - 1);
        while (t[k].h && t[k].h != h) k = (k + 1) & (size_t)(cap - 1);
        t[k].h = h; t[k].c++;
    }
    for (int i = pre; i < nq - suf; i++) {
        if (q[i].vid) continue;
        uint64_t h = q[i].h | 1; size_t k = (size_t)(h * 0x9E3779B97F4A7C15ull) & (size_t)(cap - 1);
        while (t[k].h && t[k].h != h) k = (k + 1) & (size_t)(cap - 1);
        if (t[k].h && t[k].c > 0) t[k].c--;
        else {
            vunion(dm, vw, vh, q[i].b);
            if (g_dbg_dmg && q[i].b[2] > q[i].b[0] && q[i].b[3] > q[i].b[1] && (q[i].b[2] - q[i].b[0]) * (q[i].b[3] - q[i].b[1]) > 3000) fprintf(stderr, "lumen: dmg new #%d op=%d a=%.3f %.0f,%.0f-%.0f,%.0f\n", i, g_dbg_dmg->items.v[i].op, g_dbg_dmg->items.v[i].alpha, q[i].b[0], q[i].b[1], q[i].b[2], q[i].b[3]);
        }
    }
    for (int i = pre; i < no - suf; i++) {   /* old items left unmatched were removed or changed */
        if (o[i].vid) continue;
        uint64_t h = o[i].h | 1; size_t k = (size_t)(h * 0x9E3779B97F4A7C15ull) & (size_t)(cap - 1);
        while (t[k].h != h) k = (k + 1) & (size_t)(cap - 1);
        if (t[k].c > 0) {
            t[k].c--; vunion(dm, vw, vh, o[i].b);
            if (g_dbg_dmg && o[i].b[2] > o[i].b[0] && o[i].b[3] > o[i].b[1] && (o[i].b[2] - o[i].b[0]) * (o[i].b[3] - o[i].b[1]) > 3000) fprintf(stderr, "lumen: dmg old #%d %.0f,%.0f-%.0f,%.0f\n", i, o[i].b[0], o[i].b[1], o[i].b[2], o[i].b[3]);
        }
    }
    free(t);
    /* videos are matched by stacking rank (stacked carousel slides share a box): the k-th old video that isn't
       at the same place as the k-th new one moved or went away, so its old box needs repainting */
    for (int i = 0, j = 0; i < no; i++) {
        if (!o[i].vid) continue;
        while (j < nq && !q[j].vid) j++;
        if (j >= nq || memcmp(q[j].b, o[i].b, sizeof o[i].b)) vunion(dm, vw, vh, o[i].b);
        if (j < nq) j++;
    }
}
static void ditem_box(const DItem *it, float *b) {
    float x0 = it->x, y0 = it->y, x1 = it->x + it->w, y1 = it->y + it->h, e = 2;
    if (it->op == DO_SHADOW) e += fabsf(it->spread) + fabsf(it->blur);
    if (it->op == DO_IMAGE) e = 0;   /* images draw exactly within floor/ceil of their rect */
    if (it->op == DO_TEXT) {
        x0 = y0 = 1e30f; x1 = y1 = -1e30f;
        for (int i = 0; i < it->ng; i++) { const DGlyph *g = &it->g[i]; float em = g->font ? g->font->size : 16; x0 = LMIN(x0, g->x - em); x1 = LMAX(x1, g->x + 2 * em); y0 = LMIN(y0, g->y - 1.5f * em); y1 = LMAX(y1, g->y + em); }
    }
    if (it->op == DO_POP_CLIP || it->op == DO_POP_LAYER || it->op == DO_PUSH_LAYER || x1 <= x0 || y1 <= y0) { b[0] = b[1] = 1e30f; b[2] = b[3] = -1e30f; return; }
    b[0] = x0 - e; b[1] = y0 - e; b[2] = x1 + e; b[3] = y1 + e;
}
static int dl_sigs(const DisplayList *dl, struct DSig **v, int *cap) {
    int n = dl->items.n, st[64], ns = 0;
    if (n > *cap) { *cap = n + n / 2 + 16; *v = xrealloc(*v, sizeof **v * (size_t)*cap); }
    for (int i = 0; i < n; i++) {
        const DItem *it = &dl->items.v[i]; struct DSig *q = &(*v)[i];
        q->h = ditem_hash(it); ditem_box(it, q->b); q->vid = it->op == DO_IMAGE && media_is_frame(it->img);
        if (it->op == DO_PUSH_LAYER) { if (ns < 64) st[ns++] = i; }   /* a layer covers its contents: an opacity change damages all of them */
        else if (it->op == DO_POP_LAYER) { if (ns) { int t = st[--ns]; if (ns) bunion((*v)[st[ns - 1]].b, (*v)[t].b); } }
        else if (ns) bunion((*v)[st[ns - 1]].b, q->b);
    }
    return n;
}
static uint64_t dl_hash(const DisplayList *dl) { uint64_t h = (uint64_t)dl->items.n; for (int i = 0; i < dl->items.n; i++) h = h * 31 + ditem_hash(&dl->items.v[i]); return h; }

static void check_damage(App *a, int pwp, int ph, int x0, int y0, int x1, int y1, const char *what) {
    Canvas t; canvas_init(&t, pwp, ph, a->page.scale); raster(&t, &a->pdl, RGBA(255, 255, 255, 255));
    long bad = 0; int b0 = pwp, b1 = ph, b2 = 0, b3 = 0;
    for (int y = 0; y < ph; y++) for (int x = 0; x < pwp; x++)
        if (t.px[(size_t)y * (size_t)t.stride + (size_t)x] != a->page.px[(size_t)y * (size_t)a->page.stride + (size_t)x]) { bad++; b0 = LMIN(b0, x); b1 = LMIN(b1, y); b2 = LMAX(b2, x + 1); b3 = LMAX(b3, y + 1); }
    if (bad) fprintf(stderr, "lumen: %s %dx%d at %d,%d mismatched px=%ld in %d,%d-%d,%d\n", what, x1 - x0, y1 - y0, x0, y0, bad, b0, b1, b2, b3);
    if (bad) { float s = a->page.scale;
        for (int i = 0; i < a->pdl.items.n; i++) { const DItem *it = &a->pdl.items.v[i]; float b[4]; ditem_box(it, b);
            if (b[2] * s < b0 || b[0] * s > b2 || b[3] * s < b1 || b[1] * s > b3) continue;
            fprintf(stderr, "  item %d op=%d %.1f,%.1f %.1fx%.1f img=%p anim=%d frame=%d alpha=%.2f\n", i, it->op, it->x, it->y, it->w, it->h, (void *)it->img, it->img && it->img->anim ? 1 : 0, it->img ? media_is_frame(it->img) : 0, it->alpha); } }
    else fprintf(stderr, "lumen: %s %dx%d at %d,%d mismatched px=0\n", what, x1 - x0, y1 - y0, x0, y0);
    canvas_free(&t);
}

static double g_shot_t0;   /* first frame time, for the LUMEN_SHOT test hook */
static double shot_left(void) { const char *d = getenv("LUMEN_SHOT_MS"); return g_shot_t0 ? g_shot_t0 + (d ? atof(d) : 1500) - now_ms() : 0; }
static void render(App *a) {
    if (g_bg) { a->dirty = true; return; }
    update_compact(a);
    double t0 = now_ms();
    int toolbar_px = LMIN((int)(BAR * a->scale), a->view.px_h - 1);
    int bar_px = a->view.px_y + toolbar_px;
    if (a->frame.w != a->pw || a->frame.h != a->ph) { canvas_free(&a->frame); canvas_init(&a->frame, a->pw, a->ph, a->scale); }
    a->frame.scale = a->scale;
    /* Paint chrome directly into a framebuffer subview: safe-area padding adds
       no full-screen allocation or copy, and the page/GPU share these offsets. */
    Canvas chrome = a->frame;
    chrome.px += (size_t)a->view.px_y * (size_t)chrome.stride + a->view.px_x;
    chrome.w = a->view.px_w; chrome.h = a->view.px_h;
    int ph = a->view.px_h - toolbar_px; if (ph < 1) ph = 1;
    int sidebar_px = LMIN((int)(a->side * a->scale), a->view.px_w - 1);
    int side_px = a->view.px_x + sidebar_px, pwp = a->view.px_w - sidebar_px; if (pwp < 1) pwp = 1;
    int why = !a->vonly ? 1 : !a->t->cur || a->t->cur != a->last_page || !a->t->cur->L ? 2 : a->t->relayout ? 3 : a->t->cur->d->dom_version != a->last_ver ? 4 : 0;
    bool defer = !a->present_retry.delay_ms && why && why != 2 && !a->t->loading && a->t->cur && a->t->cur->L && t0 - a->last_input > 1000 && t0 - a->last_full < 250 && media_timeout_ms() >= 0 && a->page.w == pwp && a->page.h == ph;
    a->deferred = defer; if (defer) why = 0;
    if (defer && !a->vframe) { a->dirty = false; return; }
    bool part = !why;
    int rx0 = pwp, ry0 = ph, rx1 = 0, ry1 = 0;
    if (getenv("LUMEN_DEBUG_PAINT") && (a->page.w != pwp || a->page.h != ph)) fprintf(stderr, "lumen: page canvas resize %dx%d -> %dx%d\n", a->page.w, a->page.h, pwp, ph);
    if (a->page.w != pwp || a->page.h != ph || a->page.scale != a->scale) { canvas_free(&a->page); canvas_init(&a->page, pwp, ph, a->scale); part = false; a->osig_ok = false; }
    bool gskip = false; GpuVideo gvd, *gvp = NULL;
    Image *fast = part && !a->sdirty && a->t->cur && a->gpu && a->gvid_ok && a->t->sy == a->last_sy && a->page.w == pwp && a->page.h == ph ? media_owner_frame(a->vown) : NULL;
    if (a->t->cur && (defer || fast)) {   /* nothing but the video changed, or layout may be stale: only swap the video texture */
        Image *im = fast;
        if (!im) { a->dirty = a->vframe = false; return; }
        float s = a->page.scale; const float *r = a->vrect;
        gvd = (GpuVideo){ im->px, im->w, im->h, im->w, floorf(r[0] * s + 0.5f) + side_px, floorf(r[1] * s + 0.5f) + bar_px, floorf((r[0] + r[2]) * s + 0.5f) + side_px, floorf((r[1] + r[3]) * s + 0.5f) + bar_px };
        gvd.yuv = im->yuv; gvd.mat = im->yuv_mat; gvp = &gvd; gskip = true;
    } else if (a->t->cur) {
        if (a->t->relayout || !a->t->cur->L) {
            if (!a->t->cur->L) a->t->cur->L = layout_new();
            { MediaCtx *m = &a->t->cur->e->media; float nw = a->vw - a->side, nh = a->vh - BAR;
              if (m->vw != nw || m->vh != nh) { m->vw = nw; m->vh = nh; style_recalc(a->t->cur->e, &a->t->cur->d->node, true); if (a->t->cur->js) js_viewport_changed(a->t->cur->js); } }
            { double q = now_ms(); layout_run(a->t->cur->L, a->t->cur->d, a->vw - a->side, a->vh - BAR); g_tl += now_ms() - q; }
            a->t->relayout = false;
        }
        page_scroll(a, a->t->sy);
        int blit = 0;   /* page scrolled by whole device pixels: shift the old pixels, raster only the exposed strip */
        if (a->t->sy != a->last_sy && part) {
            float sdy = (a->t->sy - a->last_sy) * a->page.scale;
            if (!a->sdirty && fabsf(sdy - roundf(sdy)) < 0.01f && fabsf(sdy) < ph / 2) blit = (int)roundf(sdy);
            else { part = false; why = 5; }
        }
        g_full_paint = (double)SDL_GetTicks(); { double q = now_ms(); dl_clear(&a->pdl); dl_build(&a->pdl, a->t->cur->L, 0, a->t->sy, a->vw - a->side, a->vh - BAR); image_anim_seen(&a->pdl, q); g_td += now_ms() - q; } a->vbars = video_bars(a, &a->pdl);
        int nn = dl_sigs(&a->pdl, &a->nsig, &a->cnsig);
        const DItem *vit = NULL; int nv = 0, nvr = 0, vr[16][4];
        int ovi = 0;   /* next old video sig, by stacking rank */
        for (int i = 0; i < a->pdl.items.n; i++) {
            const DItem *it = &a->pdl.items.v[i];
            if (it->op != DO_IMAGE || !media_is_frame(it->img)) continue;
            if (!nv++) vit = it;
            if (a->osig_ok) {   /* paused video: the same-rank video last frame showed the same frame in the same place */
                while (ovi < a->nosig && !a->osig[ovi].vid) ovi++;
                bool same = ovi < a->nosig && a->osig[ovi].h == a->nsig[i].h && !memcmp(a->osig[ovi].b, a->nsig[i].b, sizeof a->nsig[i].b);
                if (ovi < a->nosig) ovi++;
                if (same) continue;
            }
            float s = a->page.scale;
            int q[4] = { (int)floorf(it->x * s), (int)floorf(it->y * s), (int)ceilf((it->x + it->w) * s), (int)ceilf((it->y + it->h) * s) };
            rx0 = LMIN(rx0, q[0]); ry0 = LMIN(ry0, q[1]); rx1 = LMAX(rx1, q[2]); ry1 = LMAX(ry1, q[3]);
            if (nvr < 16) memcpy(vr[nvr], q, sizeof q);
            nvr++;
        }
        rx0 = LMAX(rx0, 0); ry0 = LMAX(ry0, 0); rx1 = LMIN(rx1, pwp); ry1 = LMIN(ry1, ph);
        bool gv = a->gpu && nv == 1;
        const float *fb = a->pdl.fix; float s = a->page.scale;
        int fx0 = LMAX(0, (int)floorf(fb[0] * s)), fx1 = LMIN(pwp, (int)ceilf(fb[2] * s)), fy0 = LMAX(0, (int)floorf(fb[1] * s) - LMAX(blit, 0)), fy1 = LMIN(ph, (int)ceilf(fb[3] * s) - LMIN(blit, 0));
        bool fixed_area = a->pdl.has_fixed && fx1 > fx0 && fy1 > fy0;
        if (blit && (nv || (fixed_area && (fy1 - fy0) * 2 > ph))) { blit = 0; part = false; why = 5; }
        if (blit) {
            int d = blit, n = ph - abs(d); size_t row = (size_t)a->page.stride;
            if (d > 0) memmove(a->page.px, a->page.px + (size_t)d * row, (size_t)n * row * 4);
            else memmove(a->page.px + (size_t)(-d) * row, a->page.px, (size_t)n * row * 4);
            raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), 0, d > 0 ? n : 0, pwp, d > 0 ? ph : -d);
            if (fixed_area) raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), fx0, fy0, fx1, fy1);   /* fixed boxes moved with the shift: repaint where they were and are */
            rx0 = 0; ry0 = 0; rx1 = pwp; ry1 = ph; a->gvid_ok = false;
        } else if (part && a->sdirty) {
            float s = a->page.scale;
            int x0 = (int)floorf(a->srect[0] * s), y0 = (int)floorf(a->srect[1] * s), x1 = (int)ceilf(a->srect[2] * s), y1 = (int)ceilf(a->srect[3] * s);
            if (nv) { x0 = LMIN(x0, rx0); y0 = LMIN(y0, ry0); x1 = LMAX(x1, rx1); y1 = LMAX(y1, ry1); }
            x0 = LMAX(x0, 0); y0 = LMAX(y0, 0); x1 = LMIN(x1, pwp); y1 = LMIN(y1, ph);
            if (x1 > x0 && y1 > y0) raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), x0, y0, x1, y1);
            rx0 = x0; ry0 = y0; rx1 = LMAX(x1, x0); ry1 = LMAX(y1, y0); a->gvid_ok = false;
        } else if (part && (nv == 0 || (gv && a->gvid_ok) || rx1 <= rx0 || ry1 <= ry0)) gskip = true;   /* off-screen video: nothing visible changed */
        else if (part && rx1 > rx0 && ry1 > ry0) {   /* only video frames changed: raster each changed video's rect */
            a->gvid_ok = false;
            { double q = now_ms();
              if (nvr > 16) raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), rx0, ry0, rx1, ry1);
              else for (int k = 0; k < nvr; k++) raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), vr[k][0], vr[k][1], vr[k][2], vr[k][3]);
              g_tx += now_ms() - q; }
            if (getenv("LUMEN_CHECK_DAMAGE")) check_damage(a, pwp, ph, rx0, ry0, rx1, ry1, "vdamage");
        }
        else {
            bool dmg = (why == 1 || why == 3 || why == 4) && !gv && a->osig_ok && a->t->sy == a->last_sy && !a->t->loading;
            if (dmg) {   /* only DOM/layout changed: raster the union of the items that differ from the last frame */
                build_chrome(a); chrome_tip(a);
                if (dl_hash(&a->cdl) * 31 + dl_hash(&a->hdl) != a->chash) dmg = false;
                float dm[4] = { 1e30f, 1e30f, -1e30f, -1e30f };
                g_dbg_dmg = getenv("LUMEN_DEBUG_DAMAGE") ? &a->pdl : NULL;
                if (dmg) dsig_damage(a->osig, a->nosig, a->nsig, nn, a->vw - a->side, a->vh - BAR, dm);
                float s = a->page.scale;
                int x0 = LMAX(0, (int)floorf(dm[0] * s)), y0 = LMAX(0, (int)floorf(dm[1] * s)), x1 = LMIN(pwp, (int)ceilf(dm[2] * s)), y1 = LMIN(ph, (int)ceilf(dm[3] * s));
                bool dom = x1 > x0 && y1 > y0, vid = rx1 > rx0 && ry1 > ry0;
                long area = dom ? (long)(x1 - x0) * (y1 - y0) : 0;
                if (vid && nvr > 16) area += (long)(rx1 - rx0) * (ry1 - ry0);
                else if (vid) for (int k = 0; k < nvr; k++) area += (long)LMAX(0, LMIN(vr[k][2], pwp) - LMAX(vr[k][0], 0)) * LMAX(0, LMIN(vr[k][3], ph) - LMAX(vr[k][1], 0));
                if (dmg && !dom && !vid) { part = true; gskip = true; }
                else if (dmg && area * 2 < (long)pwp * ph) {   /* DOM damage and each changed video frame are rastered as separate rects */
                    { double q = now_ms();
                      if (dom) raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), x0, y0, x1, y1);
                      if (vid && nvr > 16) raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), rx0, ry0, rx1, ry1);
                      else if (vid) for (int k = 0; k < nvr; k++) raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), LMAX(vr[k][0], 0), LMAX(vr[k][1], 0), LMIN(vr[k][2], pwp), LMIN(vr[k][3], ph));
                      g_tx += now_ms() - q; }
                    if (!dom) { x0 = rx0; y0 = ry0; x1 = rx1; y1 = ry1; }
                    else if (vid) { x0 = LMIN(x0, rx0); y0 = LMIN(y0, ry0); x1 = LMAX(x1, rx1); y1 = LMAX(y1, ry1); }
                    part = true; rx0 = x0; ry0 = y0; rx1 = x1; ry1 = y1; a->gvid_ok = false;
                    if (getenv("LUMEN_CHECK_DAMAGE")) check_damage(a, pwp, ph, x0, y0, x1, y1, "damage");
                }
                else {
                    if (dmg && getenv("LUMEN_DEBUG_PAINT")) fprintf(stderr, "lumen: damage too big dom=%d,%d-%d,%d vid=%d,%d-%d,%d nvr=%d area=%ld page=%ld\n", x0, y0, x1, y1, rx0, ry0, rx1, ry1, nvr, area, (long)pwp * ph);
                    dmg = false;
                }
            }
            if (!dmg) {
                if (getenv("LUMEN_DEBUG_PAINT")) fprintf(stderr, "lumen: full raster why=%d nv=%d osig_ok=%d sy_same=%d loading=%d chash_same=%d\n", why, nv, a->osig_ok, a->t->sy == a->last_sy, a->t->loading, dl_hash(&a->cdl) * 31 + dl_hash(&a->hdl) == a->chash);
                part = false; a->page.punch = gv ? vit->img : NULL; a->page.punched = false;
                { double q = now_ms(); raster(&a->page, &a->pdl, RGBA(255, 255, 255, 255)); g_tx += now_ms() - q; }
                a->gvid_ok = gv && a->page.punched; a->page.punch = NULL;
            }
        }
        { struct DSig *t = a->osig; a->osig = a->nsig; a->nsig = t; int c = a->cosig; a->cosig = a->cnsig; a->cnsig = c; a->nosig = nn; a->osig_ok = true; }
        if (gv && a->gvid_ok) {
            const Image *im = vit->img; float s = a->page.scale;
            gvd = (GpuVideo){ im->px, im->w, im->h, im->w, floorf(vit->x * s + 0.5f) + side_px, floorf(vit->y * s + 0.5f) + bar_px, floorf((vit->x + vit->w) * s + 0.5f) + side_px, floorf((vit->y + vit->h) * s + 0.5f) + bar_px };
            gvd.yuv = im->yuv; gvd.mat = im->yuv_mat; gvp = &gvd;
            a->vown = media_owner_of(im); a->vrect[0] = vit->x; a->vrect[1] = vit->y; a->vrect[2] = vit->w; a->vrect[3] = vit->h;
        }
    } else { a->gvid_ok = false; a->osig_ok = false; raster(&a->page, &(DisplayList){0}, RGBA(255, 255, 255, 255)); }
    if (!part) {
        build_chrome(a); chrome_tip(a); a->chash = dl_hash(&a->cdl) * 31 + dl_hash(&a->hdl);
        if (chrome.w != a->frame.w || chrome.h != a->frame.h) {
            DisplayList empty = {0}; Color bg = RGBA(250, 250, 250, 255);
            int x = a->view.px_x, y = a->view.px_y, ex = x + chrome.w, ey = y + chrome.h;
            raster_rect(&a->frame, &empty, bg, 0, 0, a->pw, y);
            raster_rect(&a->frame, &empty, bg, 0, ey, a->pw, a->ph);
            raster_rect(&a->frame, &empty, bg, 0, y, x, ey);
            raster_rect(&a->frame, &empty, bg, ex, y, a->pw, ey);
        }
        raster(&chrome, &a->cdl, RGBA(255, 255, 255, 255));
    }
    if (!gskip) for (int y = part ? ry0 : 0; y < (part ? ry1 : ph) && y + bar_px < a->ph; y++)
        memcpy(a->frame.px + (size_t)(y + bar_px) * (size_t)a->frame.stride + side_px, a->page.px + (size_t)y * (size_t)a->page.stride, (size_t)pwp * 4);
    if (!gskip && a->hdl.items.n) { float s = chrome.scale; raster_rect(&chrome, &a->hdl, RGBA(255, 255, 255, 255), (int)floorf(g_pop[0] * s), (int)floorf(g_pop[1] * s), (int)ceilf(g_pop[2] * s), (int)ceilf(g_pop[3] * s)); }   /* floats over the page */
    bool presented = a->gpu && gpu_present_frame(a->gpu, a->frame.px, a->frame.w, a->frame.h, a->frame.stride, gskip ? 0 : part ? bar_px + ry0 : 0, gskip ? 0 : part ? bar_px + ry1 : a->frame.h, gvp);
    bool retry = gpu_present_pending(a->gpu);
    if (!presented && !retry) {
        SDL_Surface *ws = SDL_GetWindowSurface(a->win);
        int y0 = gskip ? 0 : part ? bar_px + ry0 : 0, y1 = gskip ? 0 : part ? bar_px + ry1 : a->frame.h;
        if (ws && ws->w == a->frame.w && ws->h == a->frame.h && (ws->format == SDL_PIXELFORMAT_XRGB8888 || ws->format == SDL_PIXELFORMAT_ARGB8888)) {
            /* CPU path: copy only the changed rows straight into the window's shared-memory surface */
            if (y1 > ws->h) y1 = ws->h;
            if (y1 > y0) {
                for (int y = y0; y < y1; y++) memcpy((uint8_t *)ws->pixels + (size_t)y * (size_t)ws->pitch, a->frame.px + (size_t)y * (size_t)a->frame.stride, (size_t)a->frame.w * 4);
                SDL_Rect r = { 0, y0, ws->w, y1 - y0 }; SDL_UpdateWindowSurfaceRects(a->win, &r, 1);
            }
        } else if (ws) {
            SDL_Surface *src = SDL_CreateSurfaceFrom(a->frame.w, a->frame.h, SDL_PIXELFORMAT_ARGB8888, a->frame.px, a->frame.stride * 4);
            SDL_BlitSurfaceScaled(src, NULL, ws, NULL, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(src); SDL_UpdateWindowSurface(a->win);
        }
    }
    if (retry) present_retry_fail(&a->present_retry, now_ms()); else present_retry_clear(&a->present_retry);
    if (!retry && cap_screen_active() && a->frame.h >= bar_px + ph && a->frame.w >= side_px + pwp)
        cap_screen_push(a->frame.px + (size_t)bar_px * (size_t)a->frame.stride + side_px, pwp, ph, a->frame.stride);
#ifdef LUMEN_IOS
    ios_debug_geometry(a->win, a->mview, retry ? "present-retry" : presented ? "presented" : "cpu-fallback");
    if (SDL_TextInputActive(a->win)) text_area(a, a->editing || !a->t->cur ? NULL : a->t->cur->d->focus);
#endif
    a->frame_ms = now_ms() - t0;
    if (!retry && getenv("LUMEN_SHOT")) {   /* write the presented frame to a PNG after LUMEN_SHOT_MS and exit */
        if (!g_shot_t0) g_shot_t0 = t0;
        if (shot_left() <= 0) { png_write(getenv("LUMEN_SHOT"), a->frame.px, a->frame.w, a->frame.h, a->frame.stride); exit(0); }
    }
    if (getenv("LUMEN_DEBUG_PAINT")) {
        static int np, nf, nw[6]; static double tp, tf, t_last;
        if (part) { np++; tp += a->frame_ms; } else { nf++; tf += a->frame_ms; nw[why]++; }
        if (t0 - t_last > 1000) {
            fprintf(stderr, "lumen: paint partial=%d (%.1fms avg) full=%d (%.1fms avg) why: novonly=%d page=%d relayout=%d dom=%d scroll=%d | ms/s style=%.0f layout=%.0f dl=%.0f raster=%.0f\n", np, np ? tp / np : 0, nf, nf ? tf / nf : 0, nw[1], nw[2], nw[3], nw[4], nw[5], g_tr, g_tl, g_td, g_tx); g_tr = g_tl = g_td = g_tx = 0;
            np = nf = 0; tp = tf = 0; memset(nw, 0, sizeof nw); t_last = t0;
        }
    }
    if (!part) a->last_full = t0;
    a->vframe = false;
    a->dirty = a->vonly = a->sdirty = false;
    a->last_page = a->t->cur; a->last_sy = a->t->sy; if (!defer) a->last_ver = a->t->cur ? a->t->cur->d->dom_version : 0;
}

/* scroll the innermost scrollable box under (x,y) that can still move; false = let the page scroll */
static bool wheel_scroll(App *a, float x, float y, float dx, float dy) {
    Page *p = a->t->cur; if (!p || !p->L || a->t->asleep) return false;
    for (Box *b = layout_hit(p->L, x, y + a->t->sy); b; b = b->parent) {
        if (!b->scroller || !b->node || !b->st) continue;
        bool sx = b->st->overflow_x == OV_AUTO || b->st->overflow_x == OV_SCROLL, sy = b->st->overflow_y == OV_AUTO || b->st->overflow_y == OV_SCROLL;
        float maxx = LMAX(0, b->scroll_w - b->w), maxy = LMAX(0, b->scroll_h - b->h);
        float lo = box_scroll_from_end(b) ? -maxy : 0, hi = box_scroll_from_end(b) ? 0 : maxy;
        float nx = sx ? LCLAMP(b->node->scroll_x + dx, 0, maxx) : b->node->scroll_x, ny = sy ? LCLAMP(b->node->scroll_y + dy, lo, hi) : b->node->scroll_y;
        if (nx == b->node->scroll_x && ny == b->node->scroll_y) continue;
        b->node->scroll_x = nx; b->node->scroll_y = ny;
        float rx = b->x, ry = b->y; bool fx = b->fixed;
        for (Box *q = b->parent; q; q = q->parent) { fx |= q->fixed; if (q->scroller && q->node) { rx -= q->node->scroll_x; ry -= q->node->scroll_y; } }
        if (fx || (a->dirty && !a->vonly)) { a->vonly = a->sdirty = false; }
        else {
            float r[4] = { rx, ry - a->t->sy, rx + b->w, ry - a->t->sy + b->h };
            if (a->sdirty) { r[0] = LMIN(r[0], a->srect[0]); r[1] = LMIN(r[1], a->srect[1]); r[2] = LMAX(r[2], a->srect[2]); r[3] = LMAX(r[3], a->srect[3]); }
            memcpy(a->srect, r, sizeof r); a->sdirty = a->vonly = true;
        }
        a->dirty = true;
        if (p->js) js_dispatch(p->js, b->node, "scroll", "Event", false, false, 0, 0, 0, NULL);
        return true;
    }
    return false;
}
static bool wheel_apply(App *a, float mx, float my, float dx, float dy) {
    if (mx < a->side || mx >= a->vw || my <= BAR || my >= a->vh) return false;
    if (wheel_scroll(a, mx - a->side, my - BAR, dx, dy)) return true;
    bool full = a->dirty && !a->vonly;
    if (!page_scroll(a, a->t->sy + dy)) return false;
    if (!full) a->vonly = true;
    return true;
}
static Tab *tab_of_doc(App *a, Document *d) {
    for (int depth = 0; d && depth < 16; depth++) {
        for (int i = 0; i < a->ntabs; i++) if (a->tabs[i]->cur && a->tabs[i]->cur->d == d) return a->tabs[i];
        Page *f = NULL; for (Page *p = g_frames; p; p = p->fnext) if (p->d == d) { f = p; break; }
        d = f && f->frame_el ? f->frame_el->doc : NULL;
    }
    return NULL;
}
static void update_size(App *a) {
    present_retry_clear(&a->present_retry);
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
    int w, h; SDL_GetWindowSize(a->win, &w, &h); SDL_GetWindowSizeInPixels(a->win, &a->pw, &a->ph);
    SDL_Rect safe = { 0, 0, w, h };
#ifdef LUMEN_IOS
    SDL_GetWindowSafeArea(a->win, &safe);
    touch_cancel();
#endif
    a->view = ui_viewport(w, h, a->pw, a->ph, safe.x, safe.y, safe.w, safe.h);
    a->vw = a->view.w; a->vh = a->view.h; a->scale = a->view.scale;
#ifdef LUMEN_IOS
    if (!a->side_set) a->side = a->vw >= 1000 ? SIDEW : 0;   /* iPad portrait: give pages the full tablet width */
#endif
    g_sm_dx = g_sm_dy = 0;
    a->vonly = a->gvid_ok = a->osig_ok = false;
    if (a->gpu && !g_bg) gpu_resize(a->gpu, a->pw, a->ph);
    a->t->relayout = a->dirty = true;
#ifdef LUMEN_IOS
    ios_debug_geometry(a->win, a->mview, g_bg ? "resize-background" : "resize-foreground");
#endif
}

static App *g_app;
static uint64_t fnv(const char *s) { uint64_t h = 1469598103934665603ull; for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull; return h; }
static bool is_sheet_link(Node *n) { const char *r = node_attr(n, "rel"); return n->tag == A_link && r && strstr(r, "stylesheet") && node_attr(n, "href"); }
typedef struct { uint64_t gen; Node *n; } LinkLoad;
static bool is_preload_link(Node *n) {
    const char *r = node_attr(n, "rel");
    return n->tag == A_link && r && strstr(r, "preload") && node_attr(n, "href");
}
static void preload_done(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq; LinkLoad *l = ud; Page *p = g_app->t->cur;
    if (p && p->gen == l->gen && p->js)
        js_dispatch(p->js, l->n, r && r->status >= 200 && r->status < 300 ? "load" : "error", "Event", false, false, 0, 0, 0, NULL);
    node_release(l->n);
    free(l);
}
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
    if (slot && slot->im && !slot->enc && g_lite_active && r->body_len < (8u << 20)) { slot->enc = xmalloc(r->body_len); memcpy(slot->enc, r->body, r->body_len); slot->enclen = r->body_len; }
    if (slot) { slot->done = true; slot->evicted = slot->refetch = false; }
    SDL_UnlockMutex(icache_mu);
    if (getenv("LUMEN_DEBUG_IMG")) fprintf(stderr, "lumen: img done status=%d len=%zu decoded=%d slot=%d %.80s\n", r ? r->status : -1, r && r->body ? r->body_len : 0, slot && slot->im ? 1 : 0, slot ? 1 : 0, l->u);
    if (getenv("LUMEN_DEBUG_IMG") && r && r->body && r->body_len >= 12 && !(slot && slot->im)) { fprintf(stderr, "lumen: img magic"); for (int k = 0; k < 12; k++) fprintf(stderr, " %02x", (unsigned char)r->body[k]); fprintf(stderr, " ct=%s\n", headers_get(&r->headers, "content-type") ? headers_get(&r->headers, "content-type") : "?"); }
    if (im) image_unref(im);
    if (g_app->t->cur && g_app->t->cur->gen == l->gen) { g_app->t->cur->img_check = true; g_app->t->relayout = g_app->dirty = true; }
    else if (!l->gen) { g_app->dirty = true; g_app->vonly = false; }
    free(l->u); free(l);
}
static void img_refetch(const char *u) {
    NetRequest *rq = net_request_new("GET", u); ImgLoad *l = xmalloc(sizeof *l); l->gen = 0; l->u = xstrdup(u);
    rq->done = img_done; rq->ud = l; rq->priority = 2; net_fetch(rq);
}
/* external SVG documents referenced by <use href="file.svg#id">, parsed once per URL */
typedef struct { Document *d; } SvgDoc;
static HMap g_svgdocs;
static void svgdoc_done(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq; char *u = ud; Document *d = NULL;
    if (r && r->status == 200 && r->body) { d = doc_new(u); html_parse(d, r->body, r->body_len); }
    SvgDoc *s = hm_get(&g_svgdocs, u); if (s && !s->d) { s->d = d; d = NULL; }
    if (d) doc_free(d);
    g_img_epoch++;
    if (g_app->t->cur) { g_app->t->cur->img_check = true; g_app->t->relayout = true; }
    g_app->dirty = true; g_app->vonly = false;
    free(u);
}
static const Node *svg_ext_ref(const char *u, const char *id) {
    if (!strncmp(u, "blob:", 5) || !strncmp(u, "data:", 5) || !strncmp(u, "javascript:", 11)) return NULL;
    SvgDoc *s = hm_get(&g_svgdocs, u);
    if (!s) {
        s = xcalloc(1, sizeof *s); hm_put(&g_svgdocs, u, s);
        NetRequest *rq = net_request_new("GET", u); rq->done = svgdoc_done; rq->ud = xstrdup(u); rq->priority = 2; net_fetch(rq);
    }
    return s->d ? doc_get_element_by_id(s->d, id) : NULL;
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
/* a player is seen when its element's box meets the scrolled viewport (with a margin); frames, fixed boxes count as seen */
static bool vis_view(Node *n, void *ud) {
    App *a = ud;
    if (!n || !a->t || !a->t->cur) return false;
    if (n->doc != a->t->cur->d) return vis_doc(n, a->t->cur->d);
    Box *b = n->box;
    if (!b) return false;
    for (Box *q = b; q; q = q->parent) if (q->fixed) return true;
    float s = a->page.scale > 0 ? a->page.scale : 1, vh = a->page.h / s, vw = a->page.w / s, m = 200;
    return b->y + b->h >= a->t->sy - m && b->y <= a->t->sy + vh + m && b->x + b->w >= -m && b->x <= vw + m;
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
            NetRequest *rq = doc_req(p->d, u, page_top_url(p), "img-src"); ImgLoad *l = xmalloc(sizeof *l); l->gen = p->gen; l->u = u;
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
    SheetRef *cur = NULL; int nc = 0, cap = 0, hint = 0; bool changed = false;
    for (Node *n = p->d->node.first; n; n = node_next_deep(n, &p->d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML || (n->tag != A_style && !is_sheet_link(n) && !is_preload_link(n))) continue;
        int k = hint < p->nsref && p->sref[hint].n == n ? hint : 0;
        if (p->sref[k < p->nsref ? k : 0].n != n || k >= p->nsref) { k = 0; while (k < p->nsref && p->sref[k].n != n) k++; }
        bool known = k < p->nsref;
        hint = k + 1;
        /* a <style>'s text only changes through its children, which flag it dirty */
        bool fresh = !known || n->tag != A_style || (n->flags & (NF_STYLE_DIRTY | NF_CHILD_STYLE_DIRTY | NF_STRUCT_DIRTY));
        if (n->tag == A_style) n->flags &= ~(uint32_t)(NF_STYLE_DIRTY | NF_CHILD_STYLE_DIRTY | NF_STRUCT_DIRTY);
        char *t = fresh && n->tag == A_style ? node_text_content(n) : NULL;
        /* rel=preload -> stylesheet on the same href must still load and fire "load" */
        uint64_t h = !fresh ? p->sref[k].h : t ? fnv(t) : fnv(node_attr(n, "href")) ^ (is_sheet_link(n) ? 0x9e3779b97f4a7c15ull : 0);
        if (!seed && (!known || p->sref[k].h != h)) {
            if (known) { style_engine_remove_owner(p->e, n); changed = true; }
            if (t) { if (style_csp_ok(p->d, n, t)) { StyleSheet *sh = css_parse_sheet(t, strlen(t), p->d->url, 1, &p->e->media); sh->owner = n; style_engine_add_sheet(p->e, sh); changed = true; } }
            else {
                char *u = url_join(p->d->url, node_attr(n, "href"));
                if (u) { NetRequest *rq = doc_req(p->d, u, page_top_url(p), is_sheet_link(n) ? "style-src" : NULL); LinkLoad *l = xmalloc(sizeof *l); l->gen = p->gen; l->n = n; n->refcount++; rq->done = is_sheet_link(n) ? link_done : preload_done; rq->ud = l; net_fetch(rq); free(u); }
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
    p->fsy = isfinite(p->fsy) ? LCLAMP(p->fsy, 0, LMAX(0, p->L->doc_h - p->fh)) : 0;
    p->L->scroll_x = 0; p->L->scroll_y = p->fsy;
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
    dl_clear(&p->fdl); dl_build(&p->fdl, p->L, 0, p->fsy, w, h); image_anim_seen(&p->fdl, now_ms());
    raster(&p->cv, &p->fdl, 0);   /* iframes are transparent unless their document paints a background */
    p->drawn_ver = p->d->dom_version; p->drawn_epoch = g_img_epoch;
    static uint32_t fver;
    p->img = (Image){ .w = pw, .h = ph, .refs = 1 << 30, .px = p->cv.px, .scale = s, .ver = ++fver };
    return true;
}
/* @font-face: declare faces as sheets arrive; fetch a face only once text asks for it */
static char *ff_prop(const char *body, const char *name) {
    size_t nl = strlen(name);
    for (const char *p = body; (p = strcasestr(p, name)); p += nl) {
        const char *q = p + nl;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q != ':' || (p != body && !strchr("{; \t\r\n", p[-1]))) continue;
        const char *e = ++q; int par = 0; char qt = 0;
        for (; *e; e++) {
            if (qt) { if (*e == qt) qt = 0; continue; }
            if (*e == '"' || *e == '\'') qt = *e;
            else if (*e == '(') par++;
            else if (*e == ')') par--;
            else if ((*e == ';' || *e == '}') && !par) break;
        }
        while (q < e && isspace((unsigned char)*q)) q++;
        while (e > q && isspace((unsigned char)e[-1])) e--;
        if (e - q >= 2 && (*q == '"' || *q == '\'') && e[-1] == *q) { q++; e--; }
        char *r = xmalloc((size_t)(e - q) + 1); memcpy(r, q, (size_t)(e - q)); r[e - q] = 0;
        return r;
    }
    return NULL;
}
/* first src entry FreeType can read: TrueType/OpenType/WOFF (no brotli for WOFF2) */
static char *ff_pick_src(const char *src) {
    for (const char *p = src; (p = strstr(p, "url(")); ) {
        p += 4; while (*p == ' ') p++;
        char qt = *p == '"' || *p == '\'' ? *p++ : 0;
        const char *e = p; while (*e && (qt ? *e != qt : *e != ')')) e++;
        size_t n = (size_t)(e - p);
        const char *nx = e; while (*nx && *nx != ',') nx++;
        char fmt[32] = ""; const char *f = strstr(e, "format(");
        if (f && f < nx) { f += 7; size_t k = 0; while (*f && *f != ')' && k < sizeof fmt - 1) { if (*f != '"' && *f != '\'' && *f != ' ') fmt[k++] = (char)tolower((unsigned char)*f); f++; } fmt[k] = 0; }
        char *u = xmalloc(n + 1); memcpy(u, p, n); u[n] = 0;
        char *qm = strpbrk(u, "?#"); size_t ul = qm ? (size_t)(qm - u) : n;
        bool woff2 = strstr(fmt, "woff2") || (!*fmt && ul >= 6 && !strncasecmp(u + ul - 6, ".woff2", 6));
        bool ok = *fmt ? !woff2 && (strstr(fmt, "truetype") || strstr(fmt, "opentype") || strstr(fmt, "woff")) : !woff2;
        if (ok && n) return u;
        free(u); p = nx;
    }
    return NULL;
}
typedef struct { char *csp, *doc; } FontCsp;
static HMap g_fontcsp;   /* font url -> CSP of the first document that declared it (rechecked on redirects) */
static void sheet_fonts(StyleSheet *s, Document *d) {
    for (int i = 0; i < s->font_faces.n; i++) {
        const char *b = s->font_faces.v[i];
        char *fam = ff_prop(b, "font-family"), *src = ff_prop(b, "src"), *wt = ff_prop(b, "font-weight"), *sty = ff_prop(b, "font-style");
        char *u = src ? ff_pick_src(src) : NULL;
        char *abs = u && fam && *fam ? url_join(s->base_url, u) : NULL;
        if (abs && d && d->csp && !csp_allows_url(d->csp, d->url ? d->url : "", abs, "font-src")) {
            fprintf(stderr, "lumen: CSP font-src blocked %.120s\n", abs);
            free(abs); abs = NULL;
        }
        if (abs) {
            int w = 400;
            if (wt && str_ieq(wt, "bold")) w = 700;
            else if (wt && !strchr(wt, ' ') && atoi(wt) > 0) w = atoi(wt);
            bool it = sty && (!strncasecmp(sty, "italic", 6) || !strncasecmp(sty, "oblique", 7));
            if (d && d->csp && !hm_get(&g_fontcsp, abs)) {
                FontCsp *fc = xcalloc(1, sizeof *fc); fc->csp = xstrdup(d->csp); fc->doc = xstrdup(d->url ? d->url : "");
                hm_put(&g_fontcsp, abs, fc);
            }
            font_declare(fam, w, it, abs);
        }
        free(abs); free(u); free(fam); free(src); free(wt); free(sty);
    }
}
static HMap g_fontfetch;
static void font_done(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq; char *u = ud;
    bool got = r && r->status == 200 && r->body && r->body_len;
    if (font_loaded(u, got ? r->body : NULL, got ? r->body_len : 0)) {
        g_img_epoch++;
        for (Page *f = g_frames; f; f = f->fnext) f->frelayout = true;
        g_app->t->relayout = g_app->dirty = true; g_app->vonly = false;
    }
    free(u);
}
static void font_pump(void) {
    for (char *u; (u = font_next_request()); ) {
        if (hm_get(&g_fontfetch, u)) { free(u); continue; }
        hm_put(&g_fontfetch, u, (void *)1);
        NetRequest *rq = net_request_new("GET", u); rq->done = font_done; rq->ud = u;
        FontCsp *fc = hm_get(&g_fontcsp, u);
        if (fc) { rq->csp = xstrdup(fc->csp); rq->csp_doc = xstrdup(fc->doc); rq->csp_dir = "font-src"; }
        net_fetch(rq);
    }
}
static void frames_tick(App *a) {
    font_pump();
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
static void hf_scroll_to(void *ud, float x, float y) {
    (void)x; Page *p = ud; frame_restyle(p); frame_layout(p);
    y = isfinite(y) ? LCLAMP(y, 0, LMAX(0, p->L->doc_h - p->fh)) : 0;
    p->L->scroll_x = 0; p->L->scroll_y = y;
    if (y != p->fsy) { p->fsy = y; p->drawn_ver = ~0ull; }
}
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
    for (Node *n = d->node.first; n; n = node_next_deep(n, &d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML) continue;
        if (n->tag == A_style) {
            char *t = node_text_content(n); if (style_csp_ok(d, n, t)) { StyleSheet *sh = css_parse_sheet(t, strlen(t), d->url, 1, &p->e->media); sh->owner = n; style_engine_add_sheet(p->e, sh); } free(t);
        } else if (is_sheet_link(n)) {
            char *u = url_join(d->url, node_attr(n, "href"));
            NetResponse *cr = u ? doc_fetch_sync(f->doc, u, frame_top_url(f), "style-src") : NULL;
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
    hm_free(&p->img_fired, NULL); free(p->sref); free(p->vids);
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
            if (u && tg && (!strcmp(tg, "_top") || !strcmp(tg, "_blank"))) { nav_from(fp->d, u); navigate(a, u, true); }
            else if (u) js_frame_navigate(fp->frame_el, u);
            free(u); return;
        }
}
static void restyle(App *a) {
    Page *p = a->t->cur;
    if (!p || p->d->dom_version == p->seen_ver) return;
    p->seen_ver = p->d->dom_version;
    bool sheets = sync_sheets(p, false);
    static int dbg = -1; if (dbg < 0) dbg = getenv("LUMEN_DEBUG_STYLE") != NULL;
    bool idx = p->e->idx_dirty;
    { double q = now_ms(); style_recalc(p->e, &p->d->node, sheets); g_tr += now_ms() - q;
      if (dbg) fprintf(stderr, "lumen-style: sheets=%d idx=%d matched=%d paint=%d %.2fms\n", sheets, idx, (int)p->e->stats_matched, (int)p->e->stats_paint, now_ms() - q); }
    sync_images(p);
    p->img_check = true;
    if (sheets || p->e->layout_dirty || p->d->layout_version != p->lay_ver || g_no_paint_only) a->t->relayout = true;
    p->e->layout_dirty = false; p->lay_ver = p->d->layout_version;
    if (a->t == a->tabs[a->ti]) a->dirty = true;
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
    page_scroll(a, a->t->sy);   /* a forced layout can shrink the scroll range before the next paint */
}
static void h_navigate_in(void *ud, const char *u) { (void)ud; nav_from(g_app->t->cur ? g_app->t->cur->d : NULL, u); navigate(g_app, u, true); }
static void h_set_url_in(void *ud, const char *u, bool push) {
    (void)ud; App *a = g_app;
    if (push && a->t->nhist < 256) { for (int i = a->t->hpos + 1; i < a->t->nhist; i++) free(a->t->hist[i]); a->t->nhist = a->t->hpos + 1; a->t->hist[a->t->nhist++] = xstrdup(u); a->t->hpos = a->t->nhist - 1; a->t->hgen[a->t->hpos] = a->t->cur ? a->t->cur->gen : 0; }
    else if (a->t->hpos >= 0) { free(a->t->hist[a->t->hpos]); a->t->hist[a->t->hpos] = xstrdup(u); }
    if (a->t->cur) { free(a->t->cur->url); a->t->cur->url = xstrdup(u); }
    if (!a->editing) snprintf(a->t->url, sizeof a->t->url, "%s", u);
    a->dirty = true;
}
static void h_navigate_post_in(void *ud, const char *u, const char *body, size_t len, const char *ctype) { (void)ud; nav_from(g_app->t->cur ? g_app->t->cur->d : NULL, u); navigate_ex(g_app, u, true, body, len, ctype); }
static void h_history_go_in(void *ud, int d) { (void)ud; history_go(g_app, d); }
static int h_history_len_in(void *ud) { (void)ud; return g_app->t->nhist; }
static void h_viewport_in(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { (void)ud; *w = g_app->vw - g_app->side; *h = g_app->vh - BAR; *sx = 0; *sy = g_app->t->sy; *dpr = g_app->scale; }
static void h_scroll_to_in(void *ud, float x, float y) {
    (void)ud; (void)x; App *a = g_app; Page *p = a->t->cur; if (!p) return;
    h_sync_in(a, p->d, true);   /* clamp against current layout before returning to script */
    page_scroll(a, y);
}
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
    Node **sl = NULL; int nsl = 0;   /* parser-inserted stylesheets were fetched before JS existed: fire their load after DOMContentLoaded */
    for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node))
        if (n->type == NODE_ELEMENT && is_sheet_link(n)) { sl = xrealloc(sl, sizeof *sl * (size_t)(nsl + 1)); n->refcount++; sl[nsl++] = n; }
    p->js = js_new(p->d, &h);
    for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {   /* parser-inserted rel=preload: fetch so "load"/"error" fire */
        if (n->type != NODE_ELEMENT || !is_preload_link(n) || is_sheet_link(n)) continue;
        char *u = url_join(p->d->url, node_attr(n, "href"));
        if (u) { NetRequest *rq = doc_req(p->d, u, page_top_url(p), NULL); LinkLoad *l = xmalloc(sizeof *l); l->gen = p->gen; l->n = n; n->refcount++; rq->done = preload_done; rq->ud = l; net_fetch(rq); free(u); }
    }
    js_eval(p->js, "document.addEventListener('lumenmediactl',function(e){var v=e.target;if(!(v instanceof HTMLMediaElement))return;e.stopImmediatePropagation();"
        "if(e.key==='toggle'){if(v.paused)v.play();else v.pause()}else if(e.key.indexOf('seek:')===0&&v.duration>0)v.currentTime=v.duration*parseFloat(e.key.slice(5))},true)", "lumen:ctl");
    const char *pre = getenv("LUMEN_PRE_FILE");
    FILE *pf = pre ? fopen(pre, "rb") : NULL;
    if (pf) {
        fseek(pf, 0, SEEK_END); long n = ftell(pf); fseek(pf, 0, SEEK_SET);
        char *src = xmalloc((size_t)n + 1); size_t got = fread(src, 1, (size_t)n, pf); src[got] = 0; fclose(pf);
        js_eval(p->js, src, "lumen:pre"); free(src);
    }
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < p->nscripts; i++) {   /* module scripts are deferred */
        PScript *sc = &p->scripts[i];
        if (sc->module != (pass == 1) || (sc->n->flags & NF_SCRIPT_STARTED) || !(sc->n->flags & NF_CONNECTED)) continue;
        if (sc->src) {
            if (sc->module) js_run_module(p->js, sc->n, sc->src, sc->len, sc->name); else js_run_script(p->js, sc->n, sc->src, sc->len, sc->name);
            free(sc->src); sc->src = NULL; sc->len = 0;
        }
        else { sc->n->flags |= NF_SCRIPT_STARTED; js_dispatch(p->js, sc->n, "error", "Event", false, false, 0, 0, 0, NULL); }
    }
    js_set_ready_state(p->js, 1);
    js_dispatch(p->js, &p->d->node, "DOMContentLoaded", "Event", true, false, 0, 0, 0, NULL);
    for (int i = 0; i < nsl; i++) {
        if (sl[i]->flags & NF_CONNECTED) js_dispatch(p->js, sl[i], "load", "Event", false, false, 0, 0, 0, NULL);
        node_release(sl[i]);
    }
    free(sl);
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
static Node *edit_host(Node *n) {
    for (; n; n = n->parent) if (n->type == NODE_ELEMENT) {
        const char *v = node_attr(n, "contenteditable");
        if (v && (!*v || !strcmp(v, "true") || !strcmp(v, "plaintext-only"))) return n;
        if (v && !strcmp(v, "false")) return NULL;
    }
    return NULL;
}
static const char *key_name(SDL_Keycode k, bool shift, char *b) {
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return "Enter"; case SDLK_ESCAPE: return "Escape"; case SDLK_BACKSPACE: return "Backspace";
    case SDLK_TAB: return "Tab"; case SDLK_DELETE: return "Delete"; case SDLK_SPACE: return " ";
    case SDLK_LEFT: return "ArrowLeft"; case SDLK_RIGHT: return "ArrowRight"; case SDLK_UP: return "ArrowUp"; case SDLK_DOWN: return "ArrowDown";
    case SDLK_HOME: return "Home"; case SDLK_END: return "End"; case SDLK_PAGEUP: return "PageUp"; case SDLK_PAGEDOWN: return "PageDown";
    default: break;
    }
    if (k >= SDLK_F1 && k <= SDLK_F12) { snprintf(b, 8, "F%d", (int)(k - SDLK_F1) + 1); return b; }
    if (k < 33 || k >= 127) return NULL;
    static const char *plain = "1234567890-=[]\\;',./`", *shifted = "!@#$%^&*()_+{}|:\"<>?~";
    char c = (char)k; const char *p = strchr(plain, c);
    if (shift) c = c >= 'a' && c <= 'z' ? (char)(c - 32) : p ? shifted[p - plain] : c;
    b[0] = c; b[1] = 0; return b;
}
static int key_mods(SDL_Keymod m) { return (m & SDL_KMOD_SHIFT ? 1 : 0) | (m & SDL_KMOD_CTRL ? 2 : 0) | (m & SDL_KMOD_ALT ? 4 : 0) | (m & SDL_KMOD_GUI ? 8 : 0); }
/* keydown+keyup to the focused element (or body) for a key the page may handle; false if the page cancelled it */
static bool page_key(App *a, SDL_Keycode k, SDL_Keymod mod) {
    Page *p = a->t->cur; char b[8]; const char *kn = key_name(k, mod & SDL_KMOD_SHIFT, b);
    Node *f = p && p->d ? (p->d->focus ? p->d->focus : p->d->body) : NULL;
    if (a->editing || !kn || !f || !p->js) return true;
    bool ok = js_dispatch(p->js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, kn);
    if (a->t->cur == p) js_dispatch(p->js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, kn);
    return ok;
}
static Node *edit_focus(App *a) { Node *f = a->t->cur && a->t->cur->d && a->t->cur->js ? a->t->cur->d->focus : NULL; return edit_host(f) ? f : NULL; }
static Node *page_focus(App *a) { Node *f = a->t->cur && a->t->cur->d ? a->t->cur->d->focus : NULL; return is_text_ctl(f) ? f : NULL; }
static char *ctl_value(Node *n) {
    if (n->value_override) return xstrdup(n->value_override);
    if (n->tag == A_textarea) return node_text_content(n);
    if (n->tag == A_select) {   /* selectedness: script/user choice, else [selected], else the first option */
        Node *pick = NULL, *attr = NULL, *first = NULL;
        for (Node *o = n->first; o; o = node_next_in_tree(o, n)) {
            if (o->type != NODE_ELEMENT || o->tag != A_option || node_has_attr(o, "disabled")) continue;
            if (!first) first = o;
            if (o->checked_override > 0) { pick = o; break; }
            if (!attr && o->checked_override == 0 && node_has_attr(o, "selected")) attr = o;
        }
        Node *o = pick ? pick : attr ? attr : first; if (!o) return xstrdup("");
        const char *ov = node_attr(o, "value"); return ov ? xstrdup(ov) : node_text_content(o);
    }
    const char *v = node_attr(n, "value"); return xstrdup(v ? v : "");
}
static void ctl_set(App *a, Node *n, char *v) {
    free(n->value_override); n->value_override = v; doc_mark_dirty(n->doc, n);
    if (a->t->cur->js) js_dispatch(a->t->cur->js, n, "input", "InputEvent", true, false, 0, 0, 0, NULL);
    a->t->relayout = true; a->dirty = true;
}
/* where the edited field is on screen, so the on-screen keyboard is kept clear of it */
static void text_area(App *a, Node *n) {
#ifdef LUMEN_IOS
    SDL_Rect r = { 0, 0, (int)a->vw, (int)BAR };
    float box[4];
    if (n && jsg_vrect(n, box)) {
        bool fixed = false;
        for (Box *b = n->box; b; b = b->abs && b->cb ? b->cb : b->parent) if (b->fixed) { fixed = true; break; }
        r = (SDL_Rect){ (int)(box[0] + a->side), (int)(box[1] - (fixed ? 0 : a->t->sy) + BAR), (int)LMAX(1, box[2]), (int)LMAX(1, box[3]) };
    }
    r.x += (int)a->view.x; r.y += (int)a->view.y;
    SDL_SetTextInputArea(a->win, &r, 0);
#else
    (void)a; (void)n;
#endif
}
static void focus_node(App *a, Node *n) {
    Document *d = a->t->cur->d; Node *old = d->focus; JsCtx *js = a->t->cur->js;
    if (old == n) return;
    if (old) { old->flags &= ~(uint32_t)NF_FOCUS; doc_mark_dirty(d, old); }
    d->focus = n;
    if (n) { n->flags |= NF_FOCUS; doc_mark_dirty(d, n); }
    if (js && old) { js_dispatch(js, old, "blur", "FocusEvent", false, false, 0, 0, 0, NULL); js_dispatch(js, old, "focusout", "FocusEvent", true, false, 0, 0, 0, NULL); }
    if (js && n) { js_dispatch(js, n, "focus", "FocusEvent", false, false, 0, 0, 0, NULL); js_dispatch(js, n, "focusin", "FocusEvent", true, false, 0, 0, 0, NULL); }
    if (is_text_ctl(n) || edit_host(n)) { text_area(a, n); text_input(a->win, false); }
    else if (!a->editing) SDL_StopTextInput(a->win);
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

static double g_mv_t, g_mv_rep; static float g_mv_x, g_mv_y; static Page *g_mv_p;
/* f: 1 restyle n, 2 its descendants, 4 its parent's descendants; key/which select the rules that set f */
static bool hover_mark(Page *p, Node *n, int f, Node *key, int which) {
    if (!(f & 7)) return false;
    if (f & 1) doc_mark_style_dirty(p->d, n);
    if (f & 6) style_hover_desc(p->e, p->d, key, (f & 4) && n->parent && n->parent->type == NODE_ELEMENT ? n->parent : n, which);
    return true;
}
static bool hover_dirty(Page *p, Node *n) { return hover_mark(p, n, style_hover_affects(p->e, n), n, 0); }
static bool hover_has_dirty(Page *p, Node *q) {   /* :has(...:hover...) subjects: q itself, or q's previous siblings for :has(+ ...) / :has(~ ...) */
    Node *par = q->parent && q->parent->type == NODE_ELEMENT ? q->parent : NULL;
    int pg = par ? style_hover_has(p->e, par, true) : 0;
    bool ch = hover_mark(p, q, style_hover_has(p->e, q, false), q, 1);
    if (!(pg & 48)) ch |= hover_mark(p, q, pg, par, 1);
    if (p->e->idx.hov_sib)
        for (Node *s = q->prev, *adj = NULL; s; s = s->prev) {
            if (s->type != NODE_ELEMENT) continue;
            int g = style_hover_has(p->e, s, false);
            if ((g & 32) || (!adj && (g & 16))) ch |= hover_mark(p, s, g, s, 1);
            if ((pg & 32) || (!adj && (pg & 16))) ch |= hover_mark(p, s, pg, par, 1);
            adj = s;
        }
    return ch;
}
static bool page_hover(Page *p, Node *n) {   /* :hover = the hit element and its ancestors; restyle only the nodes that changed */
    static Node *last; static Page *lastp;
    if (n == last && p == lastp) return false;
    last = n; lastp = p;
    Document *d = p->d; bool ch = false, flip = false;
    for (Node *c = d->node.first; c; c = node_next_in_tree(c, &d->node)) {
        if (!(c->flags & NF_HOVER)) continue;
        Node *q = n; while (q && q != c) q = q->parent ? q->parent : q->host;
        if (!q) { c->flags &= ~(uint32_t)NF_HOVER; flip = true; ch |= hover_dirty(p, c); ch |= hover_has_dirty(p, c); }
    }
    for (Node *q = n; q; q = q->parent ? q->parent : q->host)
        if (q->type == NODE_ELEMENT) {
            if (!(q->flags & NF_HOVER)) { q->flags |= NF_HOVER; flip = true; ch |= hover_dirty(p, q); }
            if (flip) ch |= hover_has_dirty(p, q);
        }
    return ch;
}
static void page_move(App *a, float x, float y) {   /* pointer moves for page scripts (e.g. YouTube shows its controls on mousemove) */
    static double last; static Node *prev; double tn = now_ms();
    Page *p = a->t->cur; if (!p || !p->L) return;
    Box *b = layout_hit(p->L, x, y + a->t->sy); Node *n = b ? b->node : NULL;
    while (n && n->type != NODE_ELEMENT) n = n->parent;
    if (!n) n = p->d->html;
    if (page_hover(p, n)) restyle(a);
    if (!n || !p->js || tn - last < 30) return;
    last = tn; g_mv_t = tn; g_mv_x = x; g_mv_y = y; g_mv_p = p;
    if (n != prev) { prev = n; js_dispatch(p->js, n, "pointerover", "PointerEvent", true, true, x, y, 0, NULL); js_dispatch(p->js, n, "mouseover", "MouseEvent", true, true, x, y, 0, NULL); }
    js_dispatch(p->js, n, "pointermove", "PointerEvent", true, true, x, y, 0, NULL);
    js_dispatch(p->js, n, "mousemove", "MouseEvent", true, true, x, y, 0, NULL);
}
static void page_move_keep(App *a) {   /* YouTube hides its controls ~3 s after the pointer stops; keep them up for 8 s */
    double tn = now_ms(); Page *p = g_mv_p;
    if (!p || tn - g_mv_t > 8000) { g_mv_p = NULL; return; }
    if (tn - g_mv_rep < 1000) return;
    g_mv_rep = tn;
    if (a->t->cur != p || !p->js || !p->L) { g_mv_p = NULL; return; }
    Box *b = layout_hit(p->L, g_mv_x, g_mv_y + a->t->sy); Node *n = b ? b->node : NULL;
    while (n && n->type != NODE_ELEMENT) n = n->parent;
    float j = ((long)(tn / 1000) & 1) ? 1 : 0;   /* YouTube ignores moves to the same spot */
    if (n) js_dispatch(p->js, n, "mousemove", "MouseEvent", true, true, g_mv_x + j, g_mv_y, 0, NULL);
}
#define VBH 34.f
static bool vbar_on(App *a, Node *n) { return n->type == NODE_ELEMENT && n->tag == A_video && n->box && n->box->w >= 120 && (node_attr(n, "controls") || (vctl_on(a->t) && !is_youtube(a->t->url))); }
static Node **page_videos(Page *p, int *n) {
    if (p->vids_ver != p->d->layout_version + 1) {
        p->nvids = 0;
        for (Node *x = p->d->node.first; x; x = node_next_in_tree(x, &p->d->node)) {
            if (x->type != NODE_ELEMENT || x->tag != A_video) continue;
            if (p->nvids == p->vidcap) { p->vidcap = p->vidcap ? p->vidcap * 2 : 4; p->vids = xrealloc(p->vids, sizeof *p->vids * (size_t)p->vidcap); }
            p->vids[p->nvids++] = x;
        }
        p->vids_ver = p->d->layout_version + 1;
    }
    *n = p->nvids; return p->vids;
}
static float vbar_y(App *a, const Box *b) { return vctl_on(a->t) ? b->y + b->h : b->y + b->h - VBH; }   /* below the video when "always show", else inside its bottom edge */
static void fmt_t(char *o, size_t n, double s) { int t = s >= 0 && s < 1e7 ? (int)s : 0; if (t >= 3600) snprintf(o, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60); else snprintf(o, n, "%d:%02d", t / 60, t % 60); }
static void vbar_layout(App *a, const Box *b, const MpState *s, char *tm, size_t n, float *tx0, float *tx1) {
    char c[16], d[16]; fmt_t(c, sizeof c, s->time); fmt_t(d, sizeof d, s->duration); snprintf(tm, n, "%s / %s", c, d);
    *tx0 = b->x + 44; *tx1 = b->x + b->w - text_width(a->ui, tm, strlen(tm), 0) - 26;
}
static bool video_bars(App *a, DisplayList *dl) {   /* control bar under each <video controls> (or every video when "always show video controls" is on) */
    bool playing = false; Document *d = a->t->cur->d; float rb = (a->ui->ascent - a->ui->descent) / 2;
    (void)d; int nv; Node **vs = page_videos(a->t->cur, &nv);
    for (int vi = 0; vi < nv; vi++) { Node *n = vs[vi];
        if (!vbar_on(a, n)) continue;
        Box *b = n->box; float y = vbar_y(a, b) - a->t->sy;
        if (y < -VBH || y > a->vh) continue;
        MpState s; memset(&s, 0, sizeof s); if (!media_state_for(n, &s)) s.paused = true;
        playing |= !s.paused;
        char tm[48]; float tx0, tx1; vbar_layout(a, b, &s, tm, sizeof tm, &tx0, &tx1);
        push_rect(dl, b->x, y, b->w, VBH, 0, RGBA(32, 33, 36, 240));
        Image *ic = sym_icon(a, s.paused ? 5 : 6, 3); if (ic) push_img(dl, ic, b->x + 14, y + (VBH - 16) / 2, 16, 16);
        float f = s.duration > 0 ? LCLAMP(s.time / s.duration, 0, 1) : 0, cy = y + VBH / 2;
        push_rect(dl, tx0, cy - 2, tx1 - tx0, 4, 2, RGBA(255, 255, 255, 90));
        push_rect(dl, tx0, cy - 2, (tx1 - tx0) * f, 4, 2, RGBA(255, 48, 48, 255));
        push_rect(dl, tx0 + (tx1 - tx0) * f - 6, cy - 6, 12, 12, 6, RGBA(255, 255, 255, 255));
        push_text(dl, a->ui, tm, tx1 + 14, cy + rb, b->x + b->w - tx1, RGBA(255, 255, 255, 255));
    }
    return playing;
}
static bool vbar_click(App *a, float x, float y) {   /* document coordinates */
    Page *p = a->t->cur; if (!p->js) return false;
    int nv; Node **vs = page_videos(p, &nv);
    for (int vi = 0; vi < nv; vi++) { Node *n = vs[vi];
        if (!vbar_on(a, n)) continue;
        Box *b = n->box; float by = vbar_y(a, b);
        if (x < b->x || x >= b->x + b->w || y < by || y >= by + VBH) continue;
        MpState s; memset(&s, 0, sizeof s); bool ok = media_state_for(n, &s);
        char tm[48], key[32] = "toggle"; float tx0, tx1; vbar_layout(a, b, &s, tm, sizeof tm, &tx0, &tx1);
        if (ok && s.duration > 0 && x >= tx0 - 8 && x <= tx1 + 8) snprintf(key, sizeof key, "seek:%.4f", LCLAMP((x - tx0) / (tx1 - tx0), 0, 1));
        else if (x >= b->x + 44) return true;
        js_dispatch(p->js, n, "lumenmediactl", "KeyboardEvent", false, false, 0, 0, 0, key);
        a->dirty = true; a->vonly = false;
        return true;
    }
    return false;
}
static float g_px, g_py; static bool g_press, g_drag;   /* text selection by mouse */
static void sel_dirty(App *a) { a->dirty = true; a->vonly = false; }
static Box *sel_scope(Layout *L, float x, float y) {
    Box *b = layout_hit(L, x, y);
    while (b && (b->kind == BX_INLINE || b->kind == BX_TEXT)) b = b->parent;
    return b;
}
static void sel_press(App *a, float x, float y, int clicks) {
    bool had = g_tsel.on; g_tsel.on = false; g_press = g_drag = false;
    Page *p = a->t->cur;
    if (p && p->L && !a->t->asleep) {
        float px = x - a->side, py = y - BAR + a->t->sy;
        Box *b = layout_hit(p->L, px, py);
        bool ok = !(b && b->st && b->st->user_select);
        for (Node *k = b ? b->node : NULL; ok && k; k = k->parent) if (is_text_ctl(k)) ok = false;
        if (ok && b && edit_host(b->node)) ok = false;
        if (ok) {
            g_press = true; g_px = px; g_py = py;
            Node *tn; int off;
            if (clicks >= 2 && tsel_point(p->L, px, py, a->t->sy, sel_scope(p->L, px, py), &tn, &off)) {
                if (clicks == 2) tsel_word(p->L, tn, off); else tsel_block(p->L, tn);
                g_press = false;
            }
        }
    }
    if (had || g_tsel.on) sel_dirty(a);
}
static void sel_drag(App *a, float x, float y) {
    Page *p = a->t->cur;
    if (!g_press || !p || !p->L) return;
    float px = x - a->side, py = y - BAR + a->t->sy;
    if (!g_drag) {
        if (fabsf(px - g_px) + fabsf(py - g_py) < 4) return;
        Node *an; int ao;
        if (!tsel_point(p->L, g_px, g_py, a->t->sy, sel_scope(p->L, g_px, g_py), &an, &ao)) { g_press = false; return; }
        g_drag = true; g_tsel = (TextSel){ false, p->L, an, an, ao, ao };
    }
    Node *fn; int fo;
    if (!tsel_point(p->L, px, py, a->t->sy, NULL, &fn, &fo)) return;
    if (fn != g_tsel.fn || fo != g_tsel.fo || !g_tsel.on) { g_tsel.fn = fn; g_tsel.fo = fo; g_tsel.on = fn != g_tsel.an || fo != g_tsel.ao; sel_dirty(a); }
}
static bool g_epress;
static bool edit_text_at(App *a, Node *h, float px, float py, Node **tn, int *off) {
    Page *p = a->t->cur;
    return h->box && tsel_point(p->L, px, py, a->t->sy, h->box, tn, off) && node_within(*tn, h);
}
static void edit_press(App *a, float x, float y, int clicks) {
    g_epress = false;
    Node *h = edit_focus(a); Page *p = a->t->cur;
    if (!h || !p || !p->L || a->t->asleep) return;
    float px = x - a->side, py = y - BAR + a->t->sy;
    Box *b = layout_hit(p->L, px, py); Node *tn; int off;
    if (!b || edit_host(b->node) != edit_host(h) || !edit_text_at(a, h, px, py, &tn, &off)) return;
    js_dispatch(p->js, tn, clicks >= 3 ? "lumensel-all" : clicks == 2 ? "lumensel-word" : "lumensel-collapse", "MouseEvent", false, false, off, 0, 0, NULL);
    g_epress = clicks == 1;
    sel_dirty(a);
}
static void edit_drag(App *a, float x, float y) {
    static Node *ln; static int lo;
    Node *h = edit_focus(a), *tn; int off;
    if (!h || !a->t->cur || !a->t->cur->L || !edit_text_at(a, h, x - a->side, y - BAR + a->t->sy, &tn, &off) || (tn == ln && off == lo)) return;
    ln = tn; lo = off;
    js_dispatch(a->t->cur->js, tn, "lumensel-extend", "MouseEvent", false, false, off, 0, 0, NULL);
    sel_dirty(a);
}
/* mirror a ranged selection inside the focused contenteditable into the native highlight (and Cmd+C) */
static void edit_sel_sync(App *a) {
    static uint64_t ver; static const Document *dd; static bool mine;
    Page *p = a->t->cur; Document *d = p ? p->d : NULL;
    if (!d || !p->L || (d == dd && d->sel_ver == ver)) return;
    dd = d; ver = d->sel_ver;
    Node *h = edit_focus(a), *an, *fn; int ao, fo;
    bool on = h && d->sel[0] && (d->sel[0] != d->sel[1] || d->selo[0] != d->selo[1]) && node_within(d->sel[0], h) && node_within(d->sel[1], h) &&
              tsel_dom_point(d->sel[0], d->selo[0], false, &an, &ao) && tsel_dom_point(d->sel[1], d->selo[1], true, &fn, &fo);
    if (on) { g_tsel = (TextSel){ true, p->L, an, fn, ao, fo }; mine = true; }
    else if (mine) { mine = false; g_tsel.on = false; }
    if (h || on) sel_dirty(a);
}
static void h_clip_set(const char *t) { SDL_SetClipboardText(t); }
static char *h_clip_get(void) { char *t = SDL_GetClipboardText(); char *r = strdup(t ? t : ""); SDL_free(t); return r; }
static void sel_copy(App *a) {
    Node *f = !a->editing ? page_focus(a) : NULL;
    if (a->editing) { if (a->sel_all) SDL_SetClipboardText(a->t->url); return; }
    if (f) { if (a->page_sel) { char *v = ctl_value(f); SDL_SetClipboardText(v); free(v); } return; }
    if (g_tsel.on && a->t->cur && a->t->cur->L == g_tsel.L) { char *s = tsel_text(a->t->cur->L); if (*s) SDL_SetClipboardText(s); free(s); }
}
static Node *g_down; static Page *g_down_p;   /* element under the pointer at mousedown, until mouseup */
static void anchor_follow(App *a, Node *t);
static void click_release(App *a, float x, float y) {
    Node *d = g_down; Page *p = g_down_p; g_down = NULL; g_down_p = NULL;
    if (!d) return;
    Node *root = d; while (root->parent) root = root->parent;
    if (p != a->t->cur || !p->js || !p->L || d->doc != p->d || root != &p->d->node) { node_release(d); return; }
    x -= a->side; float cy = y - BAR;
    h_sync_in(a, p->d, true);
    Box *b = layout_hit(p->L, x, cy + a->t->sy); Node *u = b ? b->node : NULL;
    while (u && u->type != NODE_ELEMENT) u = u->parent;
    if (!u) u = d;
    js_dispatch(p->js, u, "pointerup", "PointerEvent", true, true, x, cy, 0, NULL);
    js_dispatch(p->js, u, "mouseup", "MouseEvent", true, true, x, cy, 0, NULL);
    Node *c = d;   /* click goes to the nearest common ancestor of the press and release targets */
    for (; c; c = c->parent) { Node *k = u; while (k && k != c) k = k->parent; if (k) break; }
    for (root = d; root->parent; root = root->parent) {}
    if (c && c->type == NODE_ELEMENT && a->t->cur == p && root == &p->d->node) {
        bool ok = js_dispatch(p->js, c, "click", "PointerEvent", true, true, x, cy, 0, NULL);
        restyle(a);
        if (ok && a->t->cur == p) anchor_follow(a, c);
    } else restyle(a);
    node_release(d);
}
static void click_page(App *a, float x, float y) {
    if (g_down) { node_release(g_down); g_down = NULL; g_down_p = NULL; }
    x -= a->side;
    if (!a->t->cur || !a->t->cur->L || a->t->asleep) return;   /* a sleeping page is only a picture until its reload lands */
    h_sync_in(a, a->t->cur->d, true);   /* paint may be deferred; hit-test the current DOM, not detached boxes */
    if (vbar_click(a, x, y - BAR + a->t->sy)) return;
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
        js_dispatch(js, t, "pointerdown", "PointerEvent", true, true, x, cy, 0, NULL);
        bool md_ok = js_dispatch(js, t, "mousedown", "MouseEvent", true, true, x, cy, 0, NULL);
        if (!md_ok) g_press = g_drag = false;   /* a cancelled mousedown starts no text selection */
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
        if (is_text_ctl(ctl)) focus_node(a, ctl);
        else if (edit_host(t)) { if (!js_focused && md_ok) focus_node(a, edit_host(t)); }
        else if (!js_focused && md_ok && page_focus(a)) focus_node(a, NULL);
        restyle(a);
        node_retain(t); g_down = t; g_down_p = a->t->cur;   /* pointerup/mouseup/click wait for the real button release */
        return;
    }
    anchor_follow(a, t);
}
static void anchor_follow(App *a, Node *t) {
    for (Node *n = t; n; n = n->parent)   /* not b: handlers and restyle() may have rebuilt the layout and freed it */
        if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) {
            const char *h = node_attr(n, "href");
            if (!strncmp(h, "javascript:", 11)) return;
            if (h[0] == '#') { Node *t = doc_get_element_by_id(a->t->cur->d, h + 1); if (t && t->box) page_scroll(a, t->box->y); return; }
            char *u = url_join(a->t->cur->d->url, h); nav_from(a->t->cur->d, u); navigate(a, u, true); free(u); return;
        }
}

static bool over_link(App *a, float x, float y) {
    x -= a->side;
    if (!a->t->cur || !a->t->cur->L || a->t->asleep || y < BAR) return false;
    Box *b = layout_hit(a->t->cur->L, x, y - BAR + a->t->sy);
    for (Node *n = b ? b->node : NULL; n; n = n->parent) if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) return true;
    return false;
}

static void tab_select(App *a, int i) {
    if (i < 0 || i >= a->ntabs) return;
    a->ti = i; a->t = a->tabs[i]; a->wi = a->t->ws; a->wslast[a->wi] = a->t; a->editing = false; a->t->relayout = true; a->dirty = true; a->vonly = false;
    if (a->t->asleep && !a->t->loading) navigate(a, a->t->url, false);
    Page *p = a->t->cur;
    SDL_SetWindowTitle(a->win, p && p->d->title && *p->d->title ? p->d->title : "Lumen");
}
static void tab_open(App *a) {
    if (!tab_new(a)) return;
    tab_select(a, a->ntabs - 1);
    navigate(a, "lumen://newtab", true);
    a->editing = true; a->sel_all = 1; a->t->url[0] = 0; text_area(a, NULL); text_input(a->win, true);
}
static void tab_free(Tab *t) { js_session_forget(t); if (t->cur) page_free(t->cur); for (int k = 0; k < t->nhist; k++) free(t->hist[k]); free(t); }
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
    static const int order[MAC_NICONS] = { 0, 9, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 1, 2 };
    int ic = order[a->nws % MAC_NICONS];
    for (int k = 0; k < MAC_NICONS; k++) {
        bool used = false;
        for (int w = 0; w < a->nws; w++) used |= a->wsicon[w] == order[k];
        if (!used) { ic = order[k]; break; }
    }
    snprintf(a->wsname[a->nws], sizeof a->wsname[0], "%s", name); a->wsicon[a->nws] = ic; a->wslast[a->nws++] = NULL;
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
    case MENU_WS_SIDEBAR: a->side_set = true; a->side = a->side > 0 ? 0 : SIDEW; for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true; break;
    }
    a->dirty = true; a->vonly = false;
}
static void info_click(App *a, int k) {
    Tab *t = a->t;
    if (*g_notice) { if (k == 3) { g_notice[0] = 0; a->dirty = true; a->vonly = false; } return; }
    if (k < 3) { static const int H[3] = { 1, 4, 10 }; t->unlimit_until = (double)time(NULL) + H[k] * 3600.0; t->limited = false; t->ncpu = t->cpui = 0; t->cpumode = 0; if (t->cur) media_set_limit(t->cur->d, 0); }
    t->info = false; a->dirty = true; a->vonly = false;
}
static const char YT_VCTL[] = "(function(on){var s=document.getElementById('__lumen_vctl');if(on&&!s){s=document.createElement('style');s.id='__lumen_vctl';"
    "s.textContent='.ytp-autohide .ytp-chrome-bottom,.ytp-autohide .ytp-gradient-bottom{opacity:1!important;visibility:visible!important}';"
    "(document.head||document.documentElement).appendChild(s)}else if(!on&&s)s.remove()})(%d)";
static void cpu_monitor(App *a) {   /* a tab averaging over g_cpu_pct% of a core for g_cpu_secs is limited to g_cpu_lim% until the user lifts it */
    static double last; double now = now_ms();
    if (!last) { last = now; return; }
    double dt = now - last; if (dt < 1000) return; last = now;
    for (int i = 0; i < a->ntabs; i++) {
        Tab *t = a->tabs[i];
        if (t->cur && t->cur->js && is_youtube(t->cur->url) && (t->vgen != t->cur->gen || t->vap != vctl_on(t))) {
            char js[600]; t->vgen = t->cur->gen; t->vap = vctl_on(t); snprintf(js, sizeof js, YT_VCTL, t->vap);
            TAB_CALL(t, js_eval(t->cur->js, js, "lumen:vctl"));
        }
        double mc = t->cur ? media_cpu_ms(t->cur->d) : 0, used = t->cpu_ms + LMAX(0, mc - t->media_ms0);
        t->media_ms0 = mc; t->cpu_ms = 0; t->pct = (float)(used / dt);
        if (t->limited) { if (t->cur) media_set_limit(t->cur->d, t->lim); continue; }
        if (!g_cpu_on || t->cpumode || (double)time(NULL) < t->unlimit_until) continue;
        int win = g_cpu_secs;
        t->cpuhist[t->cpui] = t->pct; t->cpui = (t->cpui + 1) % win; if (t->ncpu < win) t->ncpu++;
        float sum = 0; for (int k = 0; k < t->ncpu; k++) sum += t->cpuhist[k];
        if (t->ncpu == win && sum / win > g_cpu_pct / 100.f) {
            t->limited = t->info = true; t->lim = g_cpu_lim / 100.f; t->budget = 0; t->bud_t = now;
            if (t->cur) media_set_limit(t->cur->d, t->lim);
            fprintf(stderr, "lumen: tab %d limited to %d%% CPU (was %.0f%%)\n", i, g_cpu_lim, t->pct * 100);
        }
    }
    if (getenv("LUMEN_CPU_DEBUG")) for (int i = 0; i < a->ntabs; i++) fprintf(stderr, "lumen-cpu: tab %d %.0f%%%s\n", i, a->tabs[i]->pct * 100, a->tabs[i]->limited ? " limited" : "");
}
static void bm_toggle(App *a) {
    if (!a->t->cur || !*a->t->url || !strncmp(a->t->url, "lumen:", 6)) return;
    int i = bm_find(a->t->url);
    if (i >= 0) link_del(g_bms, &g_nbm, i); else link_put(g_bms, &g_nbm, MAX_BM, a->t->url, a->t->cur->d->title);
    links_save("bookmarks.txt", g_bms, g_nbm);
}
static void tab_popup(App *a, int i, float x, float y) {
#ifdef __APPLE__
    Tab *t = a->tabs[i]; char def[96];
    if (g_cpu_on) snprintf(def, sizeof def, "Default (over %d%% for %d s \xE2\x86\x92 %d%%)", g_cpu_pct, g_cpu_secs, g_cpu_lim); else snprintf(def, sizeof def, "Default (limit off)");
    int c = mac_tab_menu(a->win, x + a->view.x, y + a->view.y, t->cpumode, (int)(t->lim * 100 + 0.5f), vctl_on(t), lite_on(t), def);
    if (c == MENU_TAB_VCTL) t->vctl = !vctl_on(t);
    else if (c == MENU_TAB_LITE) t->lite = !lite_on(t);
    else if (c == MENU_TAB_CPU_DEFAULT || c == MENU_TAB_CPU_NEVER) { t->cpumode = c == MENU_TAB_CPU_NEVER; tab_unlimit(t); t->unlimit_until = 0; }
    else if (c > MENU_TAB_CPU_LIM) { t->cpumode = 2; t->limited = true; t->info = false; t->lim = (c - MENU_TAB_CPU_LIM) / 100.f; t->budget = 0; t->bud_t = now_ms(); }
    a->dirty = true; a->vonly = false;
#else
    (void)a; (void)i; (void)x; (void)y;
#endif
}
static void ws_popup(App *a, int w, float x, float y) {
#ifdef __APPLE__
    int c = mac_ws_menu(a->win, x + a->view.x, y + a->view.y);
    if (c >= MENU_WS_ICON) { int ic = c - MENU_WS_ICON; a->wsicon[w] = ic >= MAC_NICONS ? -1 : ic; a->dirty = true; a->vonly = false; }
    else if (c) menu_cmd(a, c);
#else
    (void)w; (void)x; (void)y; menu_cmd(a, MENU_WS_RENAME);
#endif
}
static SDL_Window *g_dialog_win;
static void pick_done(void *ud, const char *const *list, int filter) {
    (void)ud; (void)filter;
    int n = 0;
    if (list) while (list[n]) n++;
    js_files_picked(list, list && n ? n : -1);
}
/* accept="image/png,.pdf" -> SDL extension filter "png;jpg;...;pdf" */
static void pick_files(bool multiple, const char *accept) {
    static char pat[512]; static SDL_DialogFileFilter flt;
    pat[0] = 0;
    for (const char *p = accept; p && *p;) {
        const char *e = strchr(p, ','); size_t n = e ? (size_t)(e - p) : strlen(p);
        char tok[64]; snprintf(tok, sizeof tok, "%.*s", (int)n, p);
        char *t = str_trim(tok); const char *ext = NULL;
        if (t[0] == '.') ext = t + 1;
        else if (!strcmp(t, "image/*")) ext = "png;jpg;jpeg;gif;webp;heic;bmp;svg";
        else if (!strcmp(t, "audio/*")) ext = "mp3;wav;ogg;m4a;flac;opus";
        else if (!strcmp(t, "video/*")) ext = "mp4;webm;mov;mkv";
        else if (!strcmp(t, "application/pdf")) ext = "pdf";
        else if (!strcmp(t, "text/plain")) ext = "txt";
        else if (strchr(t, '/')) { pat[0] = 0; break; }   /* unknown MIME type: don't filter */
        if (ext && strlen(pat) + strlen(ext) + 2 < sizeof pat) { if (pat[0]) strcat(pat, ";"); strcat(pat, ext); }
        p = e ? e + 1 : NULL;
    }
    flt.name = "Allowed files"; flt.pattern = pat;
    SDL_ShowOpenFileDialog(pick_done, NULL, g_dialog_win, pat[0] ? &flt : NULL, pat[0] ? 1 : 0, NULL, multiple);
}
static SDL_HitTestResult win_hit(SDL_Window *w, const SDL_Point *pt, void *ud) {
    (void)w;
    App *a = ud; float x, y;
    bool inside = ui_viewport_point(&a->view, (float)pt->x, (float)pt->y, &x, &y);
    return inside && y < (g_compact ? TB : TABH) && bar_hit(a, x, y) == HB_NONE ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
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
#include <setjmp.h>
#include <pthread.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#define LOAD_ADDR() (0x100000000UL + (unsigned long)_dyld_get_image_vmaddr_slide(0))
#else
#include <link.h>
#include <ucontext.h>
static unsigned long LOAD_ADDR(void) { return _r_debug.r_map ? (unsigned long)_r_debug.r_map->l_addr : 0UL; }
#endif
/* A memory fault on the main thread while handling a tab jumps back to the event loop instead of killing
   Lumen: that tab's page is abandoned (leaked, never touched again), the tab reloads, and a notice is shown. */
static sigjmp_buf g_recover;
static volatile sig_atomic_t g_guard;
static pthread_t g_main_thr;
static void crash_handler(int sig, siginfo_t *si, void *ucv) {
    void *bt[64]; int n = backtrace(bt, 64);
    unsigned long pc = 0, lr = 0;
#if defined(__APPLE__) && defined(__aarch64__)
    ucontext_t *uc = ucv; if (uc && uc->uc_mcontext) { pc = (unsigned long)uc->uc_mcontext->__ss.__pc; lr = (unsigned long)uc->uc_mcontext->__ss.__lr; }
#elif defined(__APPLE__) && defined(__x86_64__)
    ucontext_t *uc = ucv; if (uc && uc->uc_mcontext) pc = (unsigned long)uc->uc_mcontext->__ss.__rip;
#elif defined(__linux__) && defined(__x86_64__)
    ucontext_t *uc = ucv; if (uc) pc = (unsigned long)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__linux__) && defined(__aarch64__)
    ucontext_t *uc = ucv; if (uc) { pc = (unsigned long)uc->uc_mcontext.pc; lr = (unsigned long)uc->uc_mcontext.regs[30]; }
#else
    (void)ucv;
#endif
    char buf[192]; int k = snprintf(buf, sizeof buf, "lumen: fatal signal %d at pc 0x%lx lr 0x%lx addr %p, load address 0x%lx\n", sig, pc, lr, si ? si->si_addr : NULL, LOAD_ADDR());
    write(2, buf, (size_t)k);
    backtrace_symbols_fd(bt, n, 2);
    if (g_guard && (sig == SIGSEGV || sig == SIGBUS) && pthread_equal(pthread_self(), g_main_thr)) { g_guard = 0; siglongjmp(g_recover, sig); }
    signal(sig, SIG_DFL); raise(sig);
}
static void install_crash_handler(void) {
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = crash_handler; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL); sigaction(SIGABRT, &sa, NULL);
}
static void recover_from_crash(App *a, int sig) {
    Tab *t = a->t, *act = a->ti >= 0 && a->ti < a->ntabs ? a->tabs[a->ti] : t;
    fprintf(stderr, "lumen: recovered from signal %d in %s\n", sig, *t->url ? t->url : "(blank tab)");
    js_release_pins();
    if (t->cur) {
        Page *drop[256]; int nd = 0;
        for (Page *p = g_frames; p && nd < 256; p = p->fnext) if (tab_of_doc(a, p->d) == t) drop[nd++] = p;
        for (int i = 0; i < nd; i++) for (Page **pp = &g_frames; *pp; pp = &(*pp)->fnext) if (*pp == drop[i]) { *pp = drop[i]->fnext; break; }
        t->cur = NULL;
    }
    t->loading = t->asleep = false; t->crashes++;
    a->last_page = NULL; a->dirty = true; a->vonly = false; a->sdirty = false;
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
    if (t->crashes <= 3 && *t->url) {
        snprintf(g_notice, sizeof g_notice, "Something went wrong on this page, so Lumen reloaded the tab instead of quitting.");
        a->t = t; navigate(a, t->url, false);
    } else snprintf(g_notice, sizeof g_notice, "This page keeps hitting an error, so Lumen stopped reloading it. Other tabs are not affected.");
    a->t = act;
}

#ifdef LUMEN_IOS
/* Touch: one finger pans (with momentum), a short touch is a click, two quick taps a double click. */
static TouchContacts g_contacts;
static struct { float x0, y0, lx, ly, vx, vy; double t0, lt; bool pan; double tap_t; float tap_x, tap_y; } g_tc;
static struct { float x, y, vx, vy; double t; bool on; } g_fl;
static void touch_cancel(void) {
    touch_contacts_cancel(&g_contacts); memset(&g_tc, 0, sizeof g_tc); g_fl.on = false;
    g_sm_dx = g_sm_dy = 0;
    g_press = g_drag = g_epress = false;
    if (g_down) { node_release(g_down); g_down = NULL; g_down_p = NULL; }
}
static void push_mouse(App *a, Uint32 type, float x, float y, int clicks, Uint8 btn) {
    /* Synthetic mouse events follow SDL's window-coordinate convention. */
    x += a->view.x; y += a->view.y;
    SDL_Event e; SDL_zero(e); e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) { e.motion.windowID = SDL_GetWindowID(a->win); e.motion.which = SDL_TOUCH_MOUSEID; e.motion.x = x; e.motion.y = y; }
    else { e.button.windowID = SDL_GetWindowID(a->win); e.button.which = SDL_TOUCH_MOUSEID; e.button.button = btn; e.button.x = x; e.button.y = y; e.button.clicks = (Uint8)clicks; e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN; }
    SDL_PushEvent(&e);
}
static void touch_event(App *a, const SDL_Event *ev) {
    float x, y; double t = now_ms();
    bool inside = ui_viewport_point(&a->view, ev->tfinger.x * a->view.window_w, ev->tfinger.y * a->view.window_h, &x, &y);
    uint64_t id = (uint64_t)ev->tfinger.fingerID;
    a->last_input = t;
    if (ev->type == SDL_EVENT_FINGER_CANCELED) { touch_cancel(); return; }
    if (ev->type == SDL_EVENT_FINGER_DOWN) {
        g_fl.on = false;
        if (touch_contacts_down(&g_contacts, id)) {
            g_tc.x0 = g_tc.lx = x; g_tc.y0 = g_tc.ly = y; g_tc.t0 = g_tc.lt = t; g_tc.pan = false; g_tc.vx = g_tc.vy = 0;
        }
        if (!inside) g_contacts.blocked = true;
        if (g_contacts.blocked) g_tc.tap_t = 0;
        return;
    }
    if (ev->type == SDL_EVENT_FINGER_MOTION) {
        if (id != g_contacts.primary || !g_contacts.n || g_contacts.blocked) return;
        if (!g_tc.pan && hypotf(x - g_tc.x0, y - g_tc.y0) > 8) { g_tc.pan = true; g_tc.tap_t = 0; g_tc.lx = g_tc.x0; g_tc.ly = g_tc.y0; }
        if (g_tc.pan) {
            float dx = x - g_tc.lx, dy = y - g_tc.ly; double dt = t - g_tc.lt;
            if (dx || dy) wheel_apply(a, g_tc.x0, g_tc.y0, -dx, -dy);
            if (dt > 0) { float k = (float)LMIN(dt / 50.0, 1.0); g_tc.vx += (float)(-dx / dt * 1000 - g_tc.vx) * k; g_tc.vy += (float)(-dy / dt * 1000 - g_tc.vy) * k; }
            g_tc.lx = x; g_tc.ly = y; g_tc.lt = t;
        }
        return;
    }
    if (!touch_contacts_up(&g_contacts, id) || !inside) return;
    if (g_tc.pan) {
        if (t - g_tc.lt < 80 && hypotf(g_tc.vx, g_tc.vy) > 120) { g_fl.on = true; g_fl.x = g_tc.x0; g_fl.y = g_tc.y0; g_fl.vx = g_tc.vx; g_fl.vy = g_tc.vy; g_fl.t = t; }
        return;
    }
    if (t - g_tc.t0 > 500) {   /* long press = right click (context menu) */
        g_tc.tap_t = 0;
        push_mouse(a, SDL_EVENT_MOUSE_MOTION, x, y, 0, 0); push_mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, 1, SDL_BUTTON_RIGHT); push_mouse(a, SDL_EVENT_MOUSE_BUTTON_UP, x, y, 1, SDL_BUTTON_RIGHT);
        return;
    }
    int clicks = g_tc.tap_t > 0 && t - g_tc.tap_t < 350 && hypotf(x - g_tc.tap_x, y - g_tc.tap_y) < 24 ? 2 : 1;
    g_tc.tap_t = clicks == 2 ? 0 : t; g_tc.tap_x = x; g_tc.tap_y = y;
    push_mouse(a, SDL_EVENT_MOUSE_MOTION, x, y, 0, 0); push_mouse(a, SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, clicks, SDL_BUTTON_LEFT); push_mouse(a, SDL_EVENT_MOUSE_BUTTON_UP, x, y, clicks, SDL_BUTTON_LEFT);
}
/* Momentum scrolling after a flick, UIScrollView-like deceleration; true while still moving. */
static bool touch_fling(App *a) {
    if (!g_fl.on) return false;
    double t = now_ms(), dt = t - g_fl.t; g_fl.t = t;
    if (dt <= 0) return true;
    if (dt > 80) { g_fl.on = false; return false; }   /* a paused/resumed run loop must not jump the page */
    float dec = powf(0.998f, (float)dt);
    float dx = g_fl.vx * (float)dt / 1000, dy = g_fl.vy * (float)dt / 1000;
    g_fl.vx *= dec; g_fl.vy *= dec;
    if ((fabsf(dx) >= 0.01f || fabsf(dy) >= 0.01f) && !wheel_apply(a, g_fl.x, g_fl.y, dx, dy)) g_fl.on = false;
    if (hypotf(g_fl.vx, g_fl.vy) < 20) g_fl.on = false;
    return g_fl.on;
}
#endif

#ifndef LUMEN_IOS
/* SIGTERM/SIGINT/SIGHUP -> SDL quit event. SDL's own handler only queues the quit on the next event pump, which never
   comes while an idle page sleeps in SDL_WaitEventTimeout; pushing from a thread wakes the wait. A second signal exits. */
static void *quit_sig_thread(void *arg) {
    sigset_t *ss = arg; int sig, n = 0;
    while (!sigwait(ss, &sig)) {
        if (n++ || !SDL_WasInit(SDL_INIT_EVENTS)) _exit(128 + sig);
        SDL_Event e; SDL_zero(e); e.type = SDL_EVENT_QUIT; SDL_PushEvent(&e);
    }
    return NULL;
}
#endif
int main(int argc, char **argv) {
#ifndef LUMEN_IOS
    {
        static sigset_t ss; sigemptyset(&ss); sigaddset(&ss, SIGTERM); sigaddset(&ss, SIGINT); sigaddset(&ss, SIGHUP);
        pthread_sigmask(SIG_BLOCK, &ss, NULL);   /* before any thread starts, so only quit_sig_thread receives them */
        pthread_t th; if (!pthread_create(&th, NULL, quit_sig_thread, &ss)) pthread_detach(th);
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    }
#endif
    g_no_paint_only = getenv("LUMEN_NO_PAINT_ONLY") != NULL;
    g_main_thr = pthread_self();
    install_crash_handler();
    const char *start = argc > 1 ? argv[1] : "https://en.wikipedia.org/wiki/Web_browser";
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#ifdef LUMEN_IOS
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   /* fingers are turned into taps / scrolls below */
    ios_wait_init();
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "0");
    { const char *bp = SDL_GetBasePath(); char cp[1024];   /* app bundle ships cacert.pem + icudtl.dat */
      if (bp) { snprintf(cp, sizeof cp, "%scacert.pem", bp); if (access(cp, R_OK) == 0) setenv("SSL_CERT_FILE", cp, 0); } }
#endif
#ifdef __linux__
    /* CPU frames go straight to the X11 window surface (XShm); otherwise SDL backs the surface with a GL
       renderer, which on a GPU-less host is llvmpipe burning every core per present */
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    if (!profile_init()) { SDL_Quit(); return 1; }
    dom_init(); net_init(6); font_init(); css_sheet_added_hook = sheet_fonts;
    icache_mu = SDL_CreateMutex(); EV_LOADED = SDL_RegisterEvents(1); EV_DL = SDL_RegisterEvents(1); js_save_file = download_save; EV_NET = SDL_RegisterEvents(1); EV_MENU = SDL_RegisterEvents(1);
    net_wakeup = wake; media_wakeup = wake; js_wakeup = wake; js_clip_set = h_clip_set; js_clip_get = h_clip_get; js_global_init(argv[0]);
    paint_image_hook = node_img; paint_url_image_hook = url_img; svg_ext_ref_hook = svg_ext_ref; layout_image_size_hook = img_size;
    App a; memset(&a, 0, sizeof a); g_app = &a; a.nws = 1; a.wsicon[0] = 2; snprintf(a.wsname[0], sizeof a.wsname[0], "Personal"); a.side = getenv("LUMEN_NO_SIDEBAR") ? 0 : SIDEW; tab_new(&a);
    {
        char sp[1200] = "";
        if (*g_pref) { js_set_profile_dir(g_pref); pref_path(sp, sizeof sp, "settings.txt"); }
        FILE *sf = *sp ? fopen(sp, "r") : NULL; char line[256];
        while (sf && fgets(line, sizeof line, sf)) {
            char k[64]; int v; if (sscanf(line, "%63[^=]=%d", k, &v) != 2) continue;
            if (!strcmp(k, "offscreen_media_eviction")) g_lowmem = v != 0; else if (!strcmp(k, "cpu_limit")) g_cpu_on = v != 0;
            else if (!strcmp(k, "cpu_pct")) g_cpu_pct = LCLAMP(v, 10, 100); else if (!strcmp(k, "cpu_secs")) g_cpu_secs = LCLAMP(v, 5, 600);
            else if (!strcmp(k, "cpu_lim")) g_cpu_lim = LCLAMP(v, 5, 95); else if (!strcmp(k, "video_controls")) g_vctl = v != 0;
            else if (!strcmp(k, "tab_sleep")) g_tab_sleep = v != 0; else if (!strcmp(k, "cpu_hud")) g_hud = v != 0; else if (!strcmp(k, "smooth_scroll")) g_smooth = v != 0; else if (!strcmp(k, "user_agent")) g_ua = LCLAMP(v, 0, UA_COUNT - 1);
            else if (!strcmp(k, "gpu_backend")) g_gpu_backend = LCLAMP(v, 0, GPU_BACKEND_METAL); else if (!strcmp(k, "gpu_adapter")) g_gpu_adapter = LCLAMP(v, -1, 15);
        }
        if (sf) fclose(sf);
        net_set_user_agent(g_ua);
        links_load("bookmarks.txt", g_bms, &g_nbm, MAX_BM); links_load("history.txt", g_hv, &g_nhv, MAX_HV);
        const char *lm = getenv("LUMEN_LOWMEM"); if (lm) g_lowmem = atoi(lm) != 0;
        fprintf(stderr, "lumen: lite mode %s\n", g_lowmem ? "on" : "off");
        media_lowmem = g_lowmem;
    }
    bool want_gpu = !getenv("LUMEN_NO_GPU") && g_gpu_backend != GPU_BACKEND_CPU;
    int ww = 1280, wh = 840; { const char *e = getenv("LUMEN_WINDOW"); if (e) sscanf(e, "%dx%d", &ww, &wh); }   /* e.g. LUMEN_WINDOW=800x600 */
    a.win = SDL_CreateWindow("Lumen", ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (want_gpu && (!strcmp(SDL_GetPlatform(), "macOS") || !strcmp(SDL_GetPlatform(), "iOS")) ? SDL_WINDOW_METAL : 0)
#ifdef LUMEN_IOS
        | SDL_WINDOW_BORDERLESS   /* full screen without the status bar */
#endif
        );
    if (!a.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    g_dialog_win = a.win; js_pick_files = pick_files;
    set_window_icon(a.win);
#ifdef __APPLE__
    mac_style_window(a.win); mac_install_menu(EV_MENU);
#endif
    SDL_SetWindowHitTest(a.win, win_hit, &a);
    update_size(&a);
    if (want_gpu) {
        GpuSurfSrc src = { 0 }; bool ok = false;
        SDL_PropertiesID pr = SDL_GetWindowProperties(a.win);
        if (!strcmp(SDL_GetPlatform(), "macOS") || !strcmp(SDL_GetPlatform(), "iOS")) { a.mview = SDL_Metal_CreateView(a.win); if (a.mview) { src.kind = GPU_SURF_METAL; src.a = SDL_Metal_GetLayer(a.mview); ok = src.a != NULL; } }
        else if (SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL)) { src.kind = GPU_SURF_WAYLAND; src.a = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL); src.b = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL); ok = true; }
        else if (SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL)) { src.kind = GPU_SURF_XLIB; src.a = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL); src.win = (uint64_t)SDL_GetNumberProperty(pr, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0); ok = true; }
        else if (SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL)) { src.kind = GPU_SURF_ANDROID; src.a = SDL_GetPointerProperty(pr, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL); ok = true; }
        if (ok) {
            GpuOpts go = { g_gpu_backend, g_gpu_adapter, false };
            if (getenv("LUMEN_VULKAN")) go.backend = GPU_BACKEND_VULKAN;
            const char *eb = getenv("LUMEN_GPU_BACKEND");   /* vulkan | gl | metal | cpu | auto */
            if (eb) go.backend = !strcmp(eb, "vulkan") ? GPU_BACKEND_VULKAN : !strcmp(eb, "gl") || !strcmp(eb, "opengl") ? GPU_BACKEND_GL : !strcmp(eb, "metal") ? GPU_BACKEND_METAL : !strcmp(eb, "cpu") ? GPU_BACKEND_CPU : GPU_BACKEND_AUTO;
            if (getenv("LUMEN_GPU_ADAPTER")) go.adapter = atoi(getenv("LUMEN_GPU_ADAPTER"));
            a.gpu = gpu_create(&src, a.pw, a.ph, &go);
        }
        g_gpu_active = a.gpu ? gpu_adapter_name(a.gpu) : "CPU";
        media_yuv = gpu_yuv_ok(a.gpu) && !getenv("LUMEN_NO_YUV");
        if (!a.gpu && a.mview) { SDL_Metal_DestroyView(a.mview); a.mview = NULL; }
    }
    fprintf(stderr, "lumen: presenting with %s\n", gpu_backend_name(a.gpu));
    a.ui = font_get("system-ui", 400, false, 13.5f);
    char cookie_path[1200] = "", hsts_path[1200] = "";
    if (*g_pref) { pref_path(cookie_path, sizeof cookie_path, "cookies.txt"); pref_path(hsts_path, sizeof hsts_path, "hsts.txt"); cookies_load(cookie_path); hsts_load(hsts_path); }
    double cookies_saved_at = now_ms();
    navigate(&a, start, true);
    for (int i = 2; i < argc; i++) { Tab *nt = tab_new(&a); if (!nt) break; a.t = nt; navigate(&a, argv[i], true); }
    a.t = a.tabs[0]; a.ti = 0;
    bool quit = false, cmd = false;
    while (!quit) {
        { int sig = sigsetjmp(g_recover, 1); if (sig) recover_from_crash(&a, sig); }
        g_guard = 1;
        if (getenv("LUMEN_CRASH_TEST")) {   /* test hook: fault once on the main thread after N ms */
            static double t0; static bool done; if (!t0) t0 = now_ms();
            if (!done && now_ms() - t0 > atof(getenv("LUMEN_CRASH_TEST"))) { done = true; *(volatile int *)(uintptr_t)8 = 1; }
        }
        SDL_Event ev;
        int to = a.t->loading ? 120 : 1000;
        for (int i = 0; i < a.ntabs; i++) { Tab *tb = a.tabs[i]; if (tb->cur && tb->cur->js) { double now = now_ms(), dl = js_next_deadline(tb->cur->js) - now; if (tb->cur->scroll_pending) dl = LMIN(dl, tb->cur->scroll_due - now); if (tb->limited && tb->budget < 0) dl = LMAX(dl, -tb->budget / tb->lim - (now - tb->bud_t)); if (dl < to) to = dl <= 0 ? 0 : (int)ceil(dl); } }
        if (net_pending() && to > 50) to = 50;
        if (g_hud) {
            static double lh; double th = now_ms();
            if (th - lh >= 2000) { lh = th; hud_sample(); a.dirty = true; a.vonly = false; }
            int nx = (int)(2000 - (th - lh)) + 1; if (nx < to) to = nx;
        }
        if (cvbuf_changed) { cvbuf_changed = 0; a.dirty = true; a.vonly = false; }
        cap_rec_tick();
        { int mf = media_tick(); if (mf & 1) { if (!a.dirty && !a.deferred) a.vonly = true; a.dirty = a.vframe = true; } if (mf & 2) a.t->relayout = true; }
        if (!a.editing && (page_focus(&a) || edit_focus(&a)) && !SDL_TextInputActive(a.win)) text_input(a.win, false);
        {
            static double last_bg; double tb = now_ms();
            if (tb - last_bg > 2000) {
                last_bg = tb;
                for (int i = 0; i < a.ntabs; i++) if (a.tabs[i]->cur && a.tabs[i]->cur->js) js_set_background(a.tabs[i]->cur->js, a.tabs[i] != a.t);
                /* tab sleep (Settings, off by default): an idle background chatgpt.com tab drops its JS (the bulk of its RAM) and keeps
                   its DOM/layout on screen; activating it reloads. LUMEN_TAB_SLEEP_MS: delay (overrides the setting), 0 = off; LUMEN_TAB_SLEEP=all: any site */
                const char *sm = getenv("LUMEN_TAB_SLEEP_MS"); double sleep_ms = sm ? atof(sm) : g_tab_sleep ? 60000 : 0; bool sleep_all = getenv("LUMEN_TAB_SLEEP") && !strcmp(getenv("LUMEN_TAB_SLEEP"), "all");
                for (int i = 0; i < a.ntabs; i++) {
                    Tab *st = a.tabs[i]; Page *sp = st->cur;
                    if (st == a.t || st->loading || !sp || !sp->js || !sp->url) { st->bg_since = 0; continue; }
                    if (!st->bg_since) st->bg_since = tb;
                    bool site = sleep_all || !strncmp(sp->url, "https://chatgpt.com/", 20);
                    if (sleep_ms > 0 && site && tb - st->bg_since > sleep_ms && !js_busy(sp->js)) { js_free(sp->js); sp->js = NULL; st->asleep = true; }
                }
                if (a.t->asleep && !a.t->loading) navigate(&a, a.t->url, false);
                for (Page *p = g_frames; p; p = p->fnext) if (p->js) { Tab *ft = tab_of_doc(&a, p->d); js_set_background(p->js, ft && ft != a.t); }
            }
        }
        if (getenv("LUMEN_MEM_STATS")) {
            static double last_stats; double tn = now_ms();
            if (tn - last_stats > 10000) { last_stats = tn; size_t fr, seg = media_mem_bytes(&fr); size_t jh = 0, je = 0; js_mem_stats(&jh, &je); fprintf(stderr, "lumen-mem: mse=%.1fMB vframes=%.1fMB images=%.1fMB/%d js_heap=%.1fMB js_external=%.1fMB canvases=%.1fMB\n", seg / 1048576.0, fr / 1048576.0, g_icache_bytes / 1048576.0, g_icache_n, jh / 1048576.0, je / 1048576.0, ((double)a.frame.w * a.frame.h + (double)a.page.w * a.page.h) * 4 / 1048576.0); }
            if (last_stats == tn) { extern long g_styles_live, g_nodes_live; extern size_t g_css_parsed_bytes; extern void worker_mem_stats(size_t *, size_t *, int *); size_t wh, wx; int wn; worker_mem_stats(&wh, &wx, &wn); MALLOC_IN_USE(ms);
                fprintf(stderr, "lumen-mem2: malloc_in_use=%.1fMB nodes=%ld (%zuB) styles=%ld (%zuB) css_parsed=%.1fMB workers=%d heap=%.1fMB ext=%.1fMB\n", ms / 1048576.0, g_nodes_live, sizeof(Node), g_styles_live, sizeof(ComputedStyle), g_css_parsed_bytes / 1048576.0, wn, wh / 1048576.0, wx / 1048576.0); }
        }
        g_lite_active = a.t && lite_on(a.t); media_lowmem = g_lite_active;
        if (a.t && a.t->cur) { if (g_lite_active) media_mark_visible(vis_doc, a.t->cur->d); else media_mark_visible(vis_view, &a); }
        if (g_lite_active) {
            static double last_evict; double tn = now_ms();
            if (tn - last_evict > 2000) { last_evict = tn; img_evict(); }
        }
        { int mt = media_timeout_ms(); if (mt >= 0 && mt < to) to = mt; }
        if (g_sm_dx || g_sm_dy) {   /* smooth scrolling: move 30% of what is left each 16ms frame */
            float sx = fabsf(g_sm_dx) < 2 ? g_sm_dx : g_sm_dx * .3f, sy = fabsf(g_sm_dy) < 2 ? g_sm_dy : g_sm_dy * .3f;
            g_sm_dx -= sx; g_sm_dy -= sy; wheel_apply(&a, g_sm_x, g_sm_y, sx, sy);
            if (to > 16) to = 16;
        }
        page_move_keep(&a); if (g_mv_p && to > 1000) to = 1000;
        if (a.vbars) { static double lb; double tn = now_ms(); if (tn - lb >= 500) { lb = tn; a.dirty = true; a.vonly = false; } if (to > 500) to = 500; }
        if (a.deferred) { double r = 250 - (now_ms() - a.last_full); if (r <= 0) a.dirty = true; else if (r + 1 < to) to = (int)r + 1; }
        if (a.tip_until) { double r = a.tip_until - now_ms(); if (r <= 0) { a.tip_until = 0; a.dirty = true; a.vonly = false; } else if (r + 1 < to) to = (int)r + 1; }
        if (!g_bg) to = present_retry_wait(&a.present_retry, now_ms(), to);
        {
            bool want = false;
            if (!a.editing && (page_focus(&a) || edit_focus(&a))) {
                double ph = fmod(now_ms() - a.caret_t, 1060);
                want = ph < 530;
                int nx = (int)((want ? 530 : 1060) - ph) + 1; if (nx < to) to = nx;
            }
            if (want != dl_caret_on) { dl_caret_on = want; a.vonly = false; a.dirty = true; }
        }
        { double now = now_ms(), gn; if (image_anim_tick(now, &gn)) { a.vonly = false; a.dirty = true; } if (gn > 0 && gn - now < to) to = gn - now < 1 ? 1 : (int)(gn - now); }
        if (getenv("LUMEN_AUTOSCROLL")) {
            /* test hook: "x,y,dy,start_ms,count" pushes one wheel tick every 16ms */
            static double t0; static int sent; float wx = 0, wy = 0, wdy = 0; double st = 0; int cnt = 0;
            sscanf(getenv("LUMEN_AUTOSCROLL"), "%f,%f,%f,%lf,%d", &wx, &wy, &wdy, &st, &cnt);
            if (!t0) t0 = now_ms();
            if (sent < cnt && now_ms() - t0 > st + sent * 16.0) { SDL_Event we = {0}; we.type = SDL_EVENT_MOUSE_WHEEL; we.wheel.y = wdy; we.wheel.mouse_x = wx; we.wheel.mouse_y = wy; SDL_PushEvent(&we); sent++; }
            if (sent < cnt && to > 16) to = 16;
        }
#ifdef LUMEN_IOS
        if (touch_fling(&a) && to > 8) to = 8;
#endif
        if (getenv("LUMEN_SHOT") && !a.present_retry.delay_ms) { double r = shot_left(); if (r <= 0) { a.dirty = true; to = 0; } else if (r + 1 < to) to = (int)r + 1; }
#ifdef LUMEN_IOS
        if (!ios_wait_event(&ev, to)) {
#else
        if (!SDL_WaitEventTimeout(&ev, to)) {
#endif
            if (a.t->loading) a.dirty = true; }
        else do {
            bool pointer_inside = true;
            if (ev.type == SDL_EVENT_MOUSE_MOTION) pointer_inside = ui_viewport_point(&a.view, ev.motion.x, ev.motion.y, &ev.motion.x, &ev.motion.y);
            else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) pointer_inside = ui_viewport_point(&a.view, ev.button.x, ev.button.y, &ev.button.x, &ev.button.y);
            else if (ev.type == SDL_EVENT_MOUSE_WHEEL) pointer_inside = ui_viewport_point(&a.view, ev.wheel.mouse_x, ev.wheel.mouse_y, &ev.wheel.mouse_x, &ev.wheel.mouse_y);
            bool wheel0 = ev.type == SDL_EVENT_MOUSE_WHEEL && !ev.wheel.x && !ev.wheel.y;
            if ((ev.type >= SDL_EVENT_KEY_DOWN && ev.type <= SDL_EVENT_TEXT_INPUT) || (ev.type >= SDL_EVENT_MOUSE_MOTION && ev.type <= SDL_EVENT_MOUSE_WHEEL && !wheel0)) a.last_input = now_ms();
            if (ev.type != EV_NET && ev.type != SDL_EVENT_MOUSE_WHEEL && !(ev.type >= SDL_EVENT_FINGER_DOWN && ev.type <= SDL_EVENT_FINGER_CANCELED)) a.vonly = false;
            js_key_mods = key_mods(SDL_GetModState());
            { SDL_MouseButtonFlags mb = SDL_GetMouseState(NULL, NULL); js_buttons = (mb & SDL_BUTTON_LMASK ? 1 : 0) | (mb & SDL_BUTTON_RMASK ? 2 : 0) | (mb & SDL_BUTTON_MMASK ? 4 : 0); }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                int b = ev.button.button == SDL_BUTTON_LEFT ? 1 : ev.button.button == SDL_BUTTON_RIGHT ? 2 : ev.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) js_buttons |= b; else js_buttons &= ~b;
            }
            switch (ev.type) {
            case SDL_EVENT_QUIT: quit = true; break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED: case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: update_size(&a); break;
            case SDL_EVENT_WINDOW_EXPOSED: a.dirty = true; break;
#ifdef LUMEN_IOS
            case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION: case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED: touch_event(&a, &ev); break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: touch_cancel(); cmd = false; break;
            case SDL_EVENT_WILL_ENTER_BACKGROUND: g_bg = true; present_retry_clear(&a.present_retry); touch_cancel(); js_ls_flush(true); if (*cookie_path) { cookies_save(cookie_path); hsts_save(hsts_path); cookies_saved_at = now_ms(); } break;
            case SDL_EVENT_DID_ENTER_BACKGROUND: g_bg = true; break;
            case SDL_EVENT_WILL_ENTER_FOREGROUND: g_bg = true; update_size(&a); break;
            case SDL_EVENT_DID_ENTER_FOREGROUND: g_bg = false; update_size(&a); a.dirty = true; a.vonly = false; break;
            case SDL_EVENT_LOW_MEMORY: g_lowmem = media_lowmem = true; a.dirty = true; break;   /* Lite mode drops off-screen images/media */
#endif
            case SDL_EVENT_MOUSE_WHEEL: {
                if (!pointer_inside) break;
                /* whole-number deltas come from a mouse wheel: one fixed step per notch; fractional ones from a trackpad */
                bool notch = ev.wheel.x == roundf(ev.wheel.x) && ev.wheel.y == roundf(ev.wheel.y);
                float k = notch ? WHEEL_STEP : 40, wdx = ev.wheel.x * k, wdy = -ev.wheel.y * k;
                if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { wdx = -wdx; wdy = -wdy; }
                if (notch && g_smooth && (wdx || wdy)) {
                    if (ev.wheel.mouse_x != g_sm_x || ev.wheel.mouse_y != g_sm_y || (g_sm_dy && (g_sm_dy > 0) != (wdy > 0))) g_sm_dx = g_sm_dy = 0;
                    g_sm_x = ev.wheel.mouse_x; g_sm_y = ev.wheel.mouse_y; g_sm_dx += wdx; g_sm_dy += wdy; break;
                }
                wheel_apply(&a, ev.wheel.mouse_x, ev.wheel.mouse_y, wdx, wdy); break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                int h = bar_hit(&a, ev.motion.x, ev.motion.y); if (h != a.hover) { a.hover = h; a.dirty = true; }
                if (pointer_inside && !h && ev.motion.y > BAR && ev.motion.x > a.side) page_move(&a, ev.motion.x - a.side, ev.motion.y - BAR);
                else if (a.t->cur && page_hover(a.t->cur, NULL)) restyle(&a);
                if (g_press && (ev.motion.state & SDL_BUTTON_LMASK)) sel_drag(&a, ev.motion.x, ev.motion.y);
                else if (g_epress && (ev.motion.state & SDL_BUTTON_LMASK)) edit_drag(&a, ev.motion.x, ev.motion.y);
                set_cursor(&a, h == HB_URL ? 1 : pointer_inside && (over_link(&a, ev.motion.x, ev.motion.y) || h) ? 2 : 0);
                break; }
            case SDL_EVENT_WINDOW_MOUSE_LEAVE: if (a.t->cur && page_hover(a.t->cur, NULL)) restyle(&a); break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    g_press = g_drag = g_epress = false;
                    if (pointer_inside) click_release(&a, ev.button.x, ev.button.y);
                    else if (g_down) { node_release(g_down); g_down = NULL; g_down_p = NULL; }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!pointer_inside) break;
                if (ev.button.button == SDL_BUTTON_RIGHT && bar_hit(&a, ev.button.x, ev.button.y) == HB_NEWTAB) { ws_popup(&a, a.wi, ev.button.x, ev.button.y); break; }   /* long-press "+": workspace menu (bar may be hidden) */
                if (ev.button.button != SDL_BUTTON_LEFT) break;
                { int bh = bar_hit(&a, ev.button.x, ev.button.y);
                  if (g_hud_pop) { float x = ev.button.x, y = ev.button.y; bool in = x >= g_pop[0] && x < g_pop[2] && y >= g_pop[1] && y < g_pop[3]; g_hud_pop = false; a.dirty = true; a.vonly = false; if (in || bh == HB_HUD) break; }
                  if (bh == HB_HUD) { g_hud_pop = true; a.dirty = true; a.vonly = false; break; }
                  if (bh >= HB_TABDOT) { int i = bh - HB_TABDOT; if (i != a.ti) tab_select(&a, i); a.t->info = true; a.dirty = true; a.vonly = false; break; }
                  if (bh >= HB_INFO) { info_click(&a, bh - HB_INFO); break; }
                  if (bh >= HB_WS) { int w = bh - HB_WS; if (w != a.wi) ws_select(&a, w); else ws_popup(&a, w, ev.button.x, ev.button.y); break; }
                  if (bh == HB_WSNEW) { menu_cmd(&a, MENU_WS_NEW); break; }
                  if (bh >= HB_TABX) { tab_close(&a, bh - HB_TABX, &quit); break; }
                  if (bh >= HB_TAB) { int i = bh - HB_TAB; if (i == a.ti) tab_popup(&a, i, ev.button.x, ev.button.y); else tab_select(&a, i); break; }
                  if (bh == HB_NEWTAB) { tab_open(&a); break; } }
                switch (bar_hit(&a, ev.button.x, ev.button.y)) {
                case HB_BACK: history_go(&a, -1); break;
                case HB_FWD: history_go(&a, 1); break;
                case HB_STAR: bm_toggle(&a); a.dirty = true; a.vonly = false; break;
                case HB_RELOAD: if (a.t->hpos >= 0) navigate(&a, a.t->hist[a.t->hpos], false); break;
                case HB_URL: a.editing = true; a.sel_all = 1; text_area(&a, NULL); text_input(a.win, true); a.dirty = true; break;
                default: if (a.editing) { a.editing = false; SDL_StopTextInput(a.win); a.dirty = true; } sel_press(&a, ev.button.x, ev.button.y, ev.button.clicks); click_page(&a, ev.button.x, ev.button.y); edit_press(&a, ev.button.x, ev.button.y, ev.button.clicks);
                }
                break;
            case SDL_EVENT_TEXT_INPUT: a.caret_t = now_ms();
                if (!a.editing && page_focus(&a)) {
                    Node *f = page_focus(&a);
                    if (!a.t->cur->js || js_dispatch(a.t->cur->js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text)) {
                        char *v = ctl_value(f); SB b; sb_init(&b); if (!a.page_sel) sb_puts(&b, v); a.page_sel = 0; sb_puts(&b, ev.text.text); free(v);
                        ctl_set(&a, f, sb_take(&b));
                    }
                    if (a.t->cur->js) js_dispatch(a.t->cur->js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text);
                    restyle(&a);
                } else if (!a.editing && edit_focus(&a)) {
                    Node *f = edit_focus(&a); JsCtx *js = a.t->cur->js;
                    if (js_dispatch(js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text))
                        js_dispatch(js, f, "lumenedit", "KeyboardEvent", false, false, 0, 0, 0, ev.text.text);
                    js_dispatch(js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text);
                    restyle(&a); a.dirty = true;
                } else if (a.editing) { if (a.sel_all) { a.t->url[0] = 0; a.sel_all = 0; } strncat(a.t->url, ev.text.text, sizeof a.t->url - strlen(a.t->url) - 1); a.dirty = true; }
                break;
            case SDL_EVENT_KEY_UP: if (ev.key.key == SDLK_LGUI || ev.key.key == SDLK_RGUI || ev.key.key == SDLK_LCTRL || ev.key.key == SDLK_RCTRL) cmd = false; break;
            case SDL_EVENT_KEY_DOWN: { a.caret_t = now_ms();
                js_key_mods = key_mods(ev.key.mod);
                SDL_Keycode k = ev.key.key; float page = a.vh - BAR - 40;
                if (k == SDLK_LGUI || k == SDLK_RGUI || k == SDLK_LCTRL || k == SDLK_RCTRL) { cmd = true; break; }
                if (!a.editing && (k == SDLK_AC_BACK || k == SDLK_AC_FORWARD ||
                    ((k == SDLK_LEFT || k == SDLK_RIGHT) && (ev.key.mod & SDL_KMOD_ALT) && !(ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_SHIFT))))) {
                    history_go(&a, k == SDLK_LEFT || k == SDLK_AC_BACK ? -1 : 1); break;
                }
                if (cmd || (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
                    bool reserved = k == SDLK_Q || k == SDLK_W || k == SDLK_T || k == SDLK_N || k == SDLK_L || k == SDLK_TAB || (k >= SDLK_1 && k <= SDLK_9);
                    if (cmd) js_key_mods |= (ev.key.mod & SDL_KMOD_CTRL) ? 2 : 8;
                    if (!reserved && !page_key(&a, k, ev.key.mod)) { restyle(&a); a.dirty = true; a.vonly = false; break; }
                    if (k == SDLK_A && !a.editing && page_focus(&a)) a.page_sel = 1;
                    else if (k == SDLK_A && !a.editing && !edit_focus(&a) && a.t->cur && a.t->cur->L) { tsel_all(a.t->cur->L); g_tsel.L = a.t->cur->L; a.vonly = false; }
                    else if (k == SDLK_A && !a.editing && edit_focus(&a)) js_dispatch(a.t->cur->js, edit_focus(&a), "lumensel-all", "MouseEvent", false, false, 0, 0, 0, NULL);
                    else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && !a.editing && edit_focus(&a))
                        js_dispatch(a.t->cur->js, edit_focus(&a), k == SDLK_LEFT ? "lumensel-home" : "lumensel-end", "MouseEvent", false, false, 0, (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : 0, 0, NULL);
                    else if (k == SDLK_C) sel_copy(&a);
                    else if (k == SDLK_L) { a.editing = true; a.sel_all = 1; text_area(&a, NULL); text_input(a.win, true); }
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
                        if (strcmp(kn, "Backspace")) a.page_sel = 0;
                        bool ok = !a.t->cur->js || js_dispatch(a.t->cur->js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        if (ok && !strcmp(kn, "Backspace")) { char *v = ctl_value(f); size_t n = a.page_sel ? 1 : strlen(v); if (a.page_sel) v[0] = 0, n = 0, a.page_sel = 0; while (n && (v[n - 1] & 0xC0) == 0x80) n--; if (n) n--; v[n] = 0; ctl_set(&a, f, v); }
                        else if (ok && !strcmp(kn, "Enter") && f->tag == A_input) submit_form(&a, f);
                        else if (ok && !strcmp(kn, "Escape")) focus_node(&a, NULL);
                        if (a.t->cur && a.t->cur->js && page_focus(&a) == f) js_dispatch(a.t->cur->js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        restyle(&a); a.dirty = true;
                    } else if (k != SDLK_SPACE && (k < 32 || k >= 127) && !page_key(&a, k, ev.key.mod)) { restyle(&a); a.dirty = true; }
                    break;
                }
                if (edit_focus(&a)) {
                    Node *f = edit_focus(&a); JsCtx *js = a.t->cur->js;
                    const char *kn = k == SDLK_RETURN || k == SDLK_KP_ENTER ? "Enter" : k == SDLK_BACKSPACE ? "Backspace" : k == SDLK_ESCAPE ? "Escape" : k == SDLK_TAB ? "Tab" :
                        k == SDLK_LEFT ? "ArrowLeft" : k == SDLK_RIGHT ? "ArrowRight" : k == SDLK_UP ? "ArrowUp" : k == SDLK_DOWN ? "ArrowDown" : k == SDLK_HOME ? "Home" : k == SDLK_END ? "End" : NULL;
                    if (kn) {
                        bool ok = js_dispatch(js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        if (ok && !strcmp(kn, "Backspace")) js_dispatch(js, f, "lumeneditdel", "KeyboardEvent", false, false, 0, 0, 0, kn);
                        else if (ok && (!strcmp(kn, "ArrowLeft") || !strcmp(kn, "ArrowRight") || !strcmp(kn, "Home") || !strcmp(kn, "End"))) {
                            static const char *mv[] = { "lumensel-left", "lumensel-right", "lumensel-home", "lumensel-end" };
                            js_dispatch(js, f, mv[kn[0] == 'H' ? 2 : kn[0] == 'E' ? 3 : kn[5] == 'L' ? 0 : 1], "MouseEvent", false, false, 0, (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : 0, 0, NULL);
                        }
                        else if (ok && !strcmp(kn, "Escape")) focus_node(&a, NULL);
                        if (a.t->cur && a.t->cur->js == js) js_dispatch(js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        restyle(&a); a.dirty = true;
                    } else if (k != SDLK_SPACE && (k < 32 || k >= 127) && !page_key(&a, k, ev.key.mod)) { restyle(&a); a.dirty = true; }
                    break;
                }
                if (!page_key(&a, k, ev.key.mod)) { restyle(&a); a.dirty = true; a.vonly = false; break; }
                if (k == SDLK_DOWN) page_scroll(&a, a.t->sy + 40); else if (k == SDLK_UP) page_scroll(&a, a.t->sy - 40);
                else if (k == SDLK_PAGEDOWN || k == SDLK_SPACE) page_scroll(&a, a.t->sy + ((ev.key.mod & SDL_KMOD_SHIFT) ? -page : page));
                else if (k == SDLK_PAGEUP) page_scroll(&a, a.t->sy - page);
                else if (k == SDLK_HOME) page_scroll(&a, 0); else if (k == SDLK_END) page_scroll(&a, max_scroll(&a));
                else if (k == SDLK_BACKSPACE) history_go(&a, (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : -1);
                else break;
                a.dirty = true; break; }
            default:
                if (ev.type == EV_MENU) menu_cmd(&a, ev.user.code);
                if (ev.type == EV_DL) {   /* the page stays; drop the history entry the download created */
                    uint64_t g = (uint64_t)(uintptr_t)ev.user.data1;
                    for (int i = 0; i < a.ntabs; i++) { Tab *t = a.tabs[i]; if (t->lgen != g) continue;
                        t->loading = false;
                        if (t->cur && t->hpos > 0 && t->hgen[t->hpos] == g) { free(t->hist[t->hpos]); t->nhist = t->hpos; t->hpos--; }
                        if (t->cur && !a.editing) snprintf(t->url, sizeof t->url, "%s", t->cur->url);
                    }
                    a.dirty = true; a.vonly = false;
                }
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
                    page_free(a.t->cur); a.t->cur = p; a.t->asleep = false; a.t->loading = false; a.t->sy = 0; a.t->relayout = true; a.dirty = true;
                    if (!a.editing) snprintf(a.t->url, sizeof a.t->url, "%s", p->url);
                    if (a.t->hpos >= 0) { free(a.t->hist[a.t->hpos]); a.t->hist[a.t->hpos] = xstrdup(p->url); }
                    char title[512]; snprintf(title, sizeof title, "%s", p->d->title && *p->d->title ? p->d->title : p->url);
                    if (strncmp(p->url, "lumen:", 6) && strncmp(p->url, "about:", 6) && strncmp(p->url, "data:", 5)) hist_add(p->url, title);
                    if (fg) SDL_SetWindowTitle(a.win, title);
                    sync_images(p);
                    page_start_js(&a, p);
                    a.t = act; double t0 = now_ms(); if (fg) render(&a); else a.dirty = true;
                    fprintf(stderr, "lumen: %s loaded in %.0fms, first frame %.1fms (layout+paint+present), %d boxes\n", p->url, p->load_ms, now_ms() - t0, p->L ? p->L->nboxes : 0);
                }
            }
        } while (SDL_PollEvent(&ev));
        js_release_pins();
        net_poll();
        { Tab *act = a.t; double tn = now_ms(); for (int i = 0; i < a.ntabs; i++) { a.t = a.tabs[i]; if (a.t->limited) { a.t->budget = LMIN(a.t->budget + a.t->lim * (tn - a.t->bud_t), 100); a.t->bud_t = tn; if (a.t->budget < 0) continue; } if (a.t->cur && a.t->cur->js) { double c0 = now_ms();
            Page *p = a.t->cur;
            if (p->scroll_pending && c0 >= p->scroll_due) {
                p->scroll_pending = false;   /* a listener may queue another scroll for the next turn */
                p->scroll_due = c0 + 16;     /* bound scroll callbacks even when a listener scrolls again */
                js_dispatch(p->js, &p->d->node, "scroll", "Event", true, false, 0, 0, 0, NULL);
            }
            js_tick(a.t->cur->js); restyle(&a); fire_img_events(a.t->cur); double c = now_ms() - c0; a.t->cpu_ms += c; if (a.t->limited) a.t->budget -= c; } } a.t = act; if (a.t->cur && a.t->cur->js) frames_tick(&a); }
        edit_sel_sync(&a);
        if (!g_bg && present_retry_due(&a.present_retry, now_ms())) { if (!a.dirty) a.vonly = true; a.dirty = true; }
        if (a.dirty && !g_bg) { double r0 = now_ms(); render(&a); a.t->cpu_ms += now_ms() - r0; }
        cpu_monitor(&a);
        { float want = a.t->info || *g_notice ? INFOH : 0; if (want != g_info_h) { g_info_h = want; for (int i = 0; i < a.ntabs; i++) a.tabs[i]->relayout = true; a.dirty = true; a.vonly = false; } }
        if (*cookie_path && now_ms() - cookies_saved_at > 5000) { cookies_save(cookie_path); hsts_save(hsts_path); cookies_saved_at = now_ms(); }
    }
    if (*cookie_path) { cookies_save(cookie_path); hsts_save(hsts_path); }
    for (int i = 0; i < a.ntabs; i++) if (a.tabs[i]->cur) page_free(a.tabs[i]->cur);
    if (!js_workers_shutdown(5000)) {
        fprintf(stderr, "lumen: worker cleanup exceeded 5 seconds during shutdown\n");
        fflush(stderr);
        _Exit(EXIT_FAILURE);
    }
    gpu_destroy(a.gpu);
    if (a.mview) SDL_Metal_DestroyView(a.mview);
    for (int i = 0; i < 3; i++) SDL_DestroyCursor(a.cursors[i]);
    SDL_DestroyWindow(a.win); SDL_Quit();
    return 0;
}
