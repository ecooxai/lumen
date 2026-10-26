/* Lumen: SDL3 window, minimalist chrome, navigation, wgpu/CPU presentation */
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#if defined(__ANDROID__) && !defined(NDEBUG)
#include <android/log.h>
#endif
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#include "android_platform.h"
#include "android_launch.h"
#include "android_back.h"
#include "android_touch.h"
#include "android_fling.h"
#include "android_focus.h"
#endif
#include "menu.h"
#include "workspace.h"
#include "local_viewer.h"
#include "download_store.h"
#include "android_ui.h"
#include "../net/download.h"
#include "media_icon.h"
#include "path_icon.h"
#include "paint_profile.h"
#include "page_zoom.h"
#include "../base/platform.h"
#include "../paint/paint.h"
#include "../paint/canvas2d.h"
#include "../media/media.h"
#include "../net/net.h"
#include "../base/url.h"
#include "../base/encoding.h"
#include "../gpu/gpu.h"
#include "../gpu/webgpu.h"
#include "../js/js.h"
#include "../js/jsglue.h"

static SDL_Window *g_dialog_win;
static bool g_compact;   /* one tab in the workspace: toolbar lives in the title bar */
static bool g_desktop_seeded;   /* Desktop site was turned on for every site once (saved as desktop_default); later choices stand */
static bool g_hide_tabs;   /* Hide tab bar (Settings, saved as hide_tabs): compact even with several tabs; the menu's Workspaces lists them */
static bool g_bare, g_bare_fs __attribute__((unused));   /* the Downloads viewer's full screen: no tab strip or toolbar, the panel fills the window; the system bars' state before */
/* test hooks read on every main-loop turn: looked up once (Android's getenv scans the environment) */
enum { ENV_SHOT, ENV_CRASH_TEST, ENV_TAB_SLEEP_MS, ENV_TAB_SLEEP, ENV_MEM_STATS, ENV_AUTOSCROLL, ENV_CHECK_DAMAGE, ENV_TESTDRIVER, ENV_N };
static const char *env_hook(int i) {
    static const char *const names[ENV_N] = { "LUMEN_SHOT", "LUMEN_CRASH_TEST", "LUMEN_TAB_SLEEP_MS", "LUMEN_TAB_SLEEP", "LUMEN_MEM_STATS", "LUMEN_AUTOSCROLL", "LUMEN_CHECK_DAMAGE", "LUMEN_TESTDRIVER" };
    static const char *v[ENV_N]; static bool got[ENV_N];
    if (!got[i]) { v[i] = getenv(names[i]); got[i] = true; }
    return v[i];
}
#define TABH (g_compact || g_bare ? 0.f : 30.f)
#define TB (g_bare ? 0.f : 44.f)
static float g_info_h;
static char g_notice[256];   /* dismissable error notice shown in the info bar */
#define INFOH 36.f
#define BAR (TABH + TB + g_info_h)
#ifdef __APPLE__
#include "macui.h"
#define TLW 78.f
#else
#define TLW 0.f
#endif
#define SIDEW 84.f
#define SIDEY (g_compact ? TB : TABH)
#define WSY0 (SIDEY + 8)
#define WSRH 40.f

typedef struct { Node *n; char *src; size_t len; char *name; bool module; } PScript;
typedef struct { Node *n; uint64_t h; size_t len; } SheetRef;   /* len: <style> text length, 0 for links */
typedef struct Page {
    char *url; Document *d; StyleEngine *e; Layout *L; uint64_t gen; double load_ms;
    JsCtx *js; PScript *scripts; int nscripts; uint64_t seen_ver, lay_ver, seen_sheet_ver, seen_paint_ver, seen_res; bool res_seen;
    bool sheets_pending; /* a fetched stylesheet was added: next restyle recomputes everything once */
    SheetRef *sref; int nsref;
    HMap img_fired; bool img_check, internal_ui;
    double load_t0;   /* when its load event first became due (images delay it up to 10 s) */
    /* <iframe> child browsing context (document owned by the JS layer) */
    Node *frame_el; float fw, fh, fsy; Canvas cv; Image img; DisplayList fdl;
    uint64_t drawn_ver, drawn_paint_ver; int drawn_epoch; bool frelayout; struct Page *fnext;
} Page;
static Page *g_frames;
static Page *frame_find(Node *f) { for (Page *p = g_frames; p; p = p->fnext) if (p->frame_el == f) return p; return NULL; }
#ifdef __ANDROID__
/* touch contacts that started inside an iframe: their frame, whose JS context gets the sequence */
static struct { uint64_t device, finger; Page *fp; } g_touch_in[10];
static int g_touch_nin;
static bool frame_alive(const Page *fp) { for (Page *p = g_frames; p; p = p->fnext) if (p == fp) return true; return false; }
static void touch_frames_reset(void) {
    for (int i = 0; i < g_touch_nin; i++) {
        Page *fp = g_touch_in[i].fp; bool dup = false;
        for (int j = 0; j < i; j++) dup |= g_touch_in[j].fp == fp;
        if (!dup && frame_alive(fp) && fp->frame_el) js_touch(js_frame_ctx(fp->frame_el), NULL, "reset", 0, 0, 0, 0, 0);
    }
    g_touch_nin = 0;
}
#endif
static bool page_viewport(Page *page, float width, float height, float scale) {
    if (page->e->media.vw == width && page->e->media.vh == height && page->e->media.dpr == scale) return false;
    page->e->media.vw = width; page->e->media.vh = height;
    page->e->media.dpr = scale;
    style_recalc(page->e, &page->d->node, false);
    page->img_check = true;
    return true;
}
typedef struct { Image *im; bool done, evicted, refetch; double used; float ew, eh; uint8_t *enc; size_t enclen; } ImgSlot;   /* enc: compressed bytes kept in Lite mode for local re-decode */
static bool g_lite_active;

static SDL_Mutex *icache_mu;
static HMap icache;
static size_t g_icache_bytes; static int g_icache_n;
static int g_img_epoch;
static Uint32 EV_LOADED, EV_NET, EV_MENU, EV_DL, EV_UI, EV_TD;
static uint64_t load_gen;
static bool g_lowmem;
static int g_cpu_on = 1, g_cpu_pct = 80, g_cpu_secs = 60, g_cpu_lim = 40;
static bool g_vctl;
static bool g_tab_sleep;   /* sleep background ChatGPT tabs; off by default, saved as chatgpt_sleep (an older tab_sleep is ignored) */
static bool g_smooth = true;   /* Smooth scrolling (Settings, on by default, saved as smooth_scrolling): animate wheel notches; off, touch scrolls in steps */
#ifdef __ANDROID__
static double g_kbd_show_at;   /* Show keyboard: when to raise it, once the menu's window has given focus back */
static bool g_kbd_sticky;   /* Show keyboard (Android menu): the keyboard stays up for the last focused text field until hidden */
#endif
static bool g_tap_kbd = true;   /* Keyboard on second tap (Settings, Android, on by default, saved as tap_keyboard): a first tap in a text field only places the caret */
static int g_scroll_px = 20;   /* the touch scroll step while smooth scrolling is off, in CSS px: a multiple of 10, at most the page's height */
static float g_sm_dx, g_sm_dy, g_sm_x, g_sm_y;
/* Android tabs show no close button (the touch indicator is drawn by BrowserToolbar, on the UI
   thread, from the raw touches) */
#ifdef __ANDROID__
#define ANDROID_UI 1
#else
#define ANDROID_UI 0
#endif
static int g_page_zoom_pct = 100;
#define WHEEL_STEP 120.f
#ifdef NDEBUG
static bool g_hud;          /* CPU/RAM readout at the bottom of the workspace sidebar (Settings; on by default in debug builds) */
#else
static bool g_hud = true;
#endif
/* Android's readout sits by the address field and has its own key: the sidebar one's was never shown there */
#ifdef __ANDROID__
#define HUD_KEY "cpu_meter"
#else
#define HUD_KEY "cpu_hud"
#endif
static bool g_touch_fb = true;   /* Touch feedback (Settings, Android): a faint circle where the screen is touched */
#ifdef __ANDROID__
/* its colour (0xRRGGBB), opacity (%), diameter (dp) and how long it stays after the finger lifts (ms) */
static int g_touch_fb_rgb = 0x90caf9, g_touch_fb_alpha = 10, g_touch_fb_size = 14, g_touch_fb_ms = 200;
static const int touch_fb_colors[] = {0x90caf9, 0x1a73e8, 0x9e9e9e, 0x81c784, 0xe57373, 0xba68c8, 0xffb74d, 0xfff176};
static const char *touch_fb_color_names[] = {"Light blue", "Blue", "Grey", "Green", "Red", "Purple", "Orange", "Yellow"};
static const int touch_fb_alphas[] = {5, 10, 15, 20, 30, 50}, touch_fb_sizes[] = {8, 10, 14, 20, 28, 40};
static const int touch_fb_times[] = {0, 100, 200, 300, 500, 1000};
#endif
static bool g_hud_pop;      /* host CPU/RAM popup opened by clicking the readout */
static float g_hud_cpu, g_hud_ram, g_host_cpu, g_host_ram, g_pop[4];
#define HUDH 48.f
static int g_ua;   /* UA_CHROME / UA_FIREFOX / UA_LUMEN */
static char g_pref[1024];
#ifdef __ANDROID__
static int g_pixel_scale_pct = 200;
static GpuBackend g_renderer = GPU_BACKEND_VULKAN;
static int renderer_parse(const char *value) {
    static const char *choices[] = {"auto", "vulkan", "opengl", "software"};
    for (int index = 0; index < 4; index++) if (!strcmp(value, choices[index])) return index;
    return -1;
}
static const char *renderer_key(GpuBackend backend) {
    static const char *choices[] = {"auto", "vulkan", "opengl", "software"};
    return backend >= GPU_BACKEND_AUTO && backend <= GPU_BACKEND_CPU ? choices[backend] : "vulkan";
}
static void renderer_load(const char *line) {
    if (strncmp(line, "renderer=", 9)) return;
    size_t length = strcspn(line + 9, "\r\n");
    if (length >= 16) return;
    char value[16]; memcpy(value, line + 9, length); value[length] = 0;
    int backend = renderer_parse(value);
    if (backend >= 0) g_renderer = (GpuBackend)backend;
}
static void renderer_read_preferences(void) {
    char *directory = SDL_GetPrefPath("lumen", "Lumen");
    if (!directory) return;
    char path[1200]; snprintf(path, sizeof path, "%ssettings.txt", directory); SDL_free(directory);
    FILE *settings = fopen(path, "r");
    if (!settings) return;
    char line[256];
    while (fgets(line, sizeof line, settings)) renderer_load(line);
    fclose(settings);
}
static int pixel_scale_parse(const char *value) {
    static const char *choices[] = {"100", "150", "200", "250", "300"};
    for (int index = 0; index < 5; index++) if (!strcmp(value, choices[index])) return 100 + index * 50;
    return 0;
}
static void pixel_scale_load(const char *line) {
    if (strncmp(line, "pixel_scale=", 12)) return;
    size_t length = strcspn(line + 12, "\r\n");
    if (length != 3) return;
    char value[4]; memcpy(value, line + 12, 3); value[3] = 0;
    int percent = pixel_scale_parse(value);
    if (percent) g_pixel_scale_pct = percent;
}
#endif
#ifdef __ANDROID__
/* the toolbar buttons left of the menu, two (four on a wide window): the most recently used menu actions */
#define NSLOT 4
static int g_slot[NSLOT] = { UI_BACK, UI_RELOAD, UI_FORWARD, UI_NEW_TAB }, g_slot_n = 2; static unsigned g_slot_seq[NSLOT], g_slot_clock;
static bool g_fullscreen;
static bool slot_action(int code) {
    return code == UI_BACK || code == UI_FORWARD || code == UI_RELOAD || code == UI_NEW_TAB || code == UI_BOOKMARKS || code == UI_HISTORY ||
        code == UI_DOWNLOADS || code == UI_WORKSPACES || code == UI_SETTINGS || code == UI_DESKTOP || code == UI_BM_TOGGLE || code == UI_FULLSCREEN;
}
#endif
typedef struct { char *url, *title; } Link;
#define MAX_BM 512
#define MAX_HV 1000
static Link g_bms[MAX_BM], g_hv[MAX_HV]; static int g_nbm, g_nhv;
static void pref_path(char *out, size_t n, const char *f) { snprintf(out, n, "%s%s", g_pref, f); }
#ifdef __ANDROID__
/* the touch selection and edit bars' actions; the edit bar's two slots and text snippets (defined with the bar) */
enum { TA_CUT, TA_COPY, TA_PASTE, TA_ALL, TA_KBD, TA_BKSP, TA_ENTER, TA_UNDO, TA_CLEAR, TA_HIDE, TA_MORE, TA_REDO, TA_NACT };
static int g_bar_slot[2];
#define MAX_SNIP 16
static char *g_snip[MAX_SNIP]; static int g_nsnip;
static void snip_encode(char *out, size_t n);
static void snip_decode(const char *v);
#endif
static void settings_save(void) {
    char p[1200]; pref_path(p, sizeof p, "settings.txt"); FILE *f = *g_pref ? fopen(p, "w") : NULL; if (!f) return;
    fprintf(f, "offscreen_media_eviction=%d\ncpu_limit=%d\ncpu_pct=%d\ncpu_secs=%d\ncpu_lim=%d\nvideo_controls=%d\nchatgpt_sleep=%d\nuser_agent=%d\n%s=%d\nsmooth_scrolling=%d\nscroll_px=%d\ntap_keyboard=%d\nhide_tabs=%d\ndesktop_default=%d\n", g_lowmem, g_cpu_on, g_cpu_pct, g_cpu_secs, g_cpu_lim, g_vctl, g_tab_sleep, g_ua, HUD_KEY, g_hud, g_smooth, g_scroll_px, g_tap_kbd, g_hide_tabs, g_desktop_seeded);
    fprintf(f, "page_zoom=%d\n", g_page_zoom_pct);
#ifdef __ANDROID__
    fprintf(f, "pixel_scale=%d\n", g_pixel_scale_pct);
    fprintf(f, "renderer=%s\n", renderer_key(g_renderer));
    for (int i = 0; i < NSLOT; i++) fprintf(f, "bar_slot%d=%d\n", i, g_slot[i]);
    { char sn[4096]; snip_encode(sn, sizeof sn); fprintf(f, "edit_slot0=%d\nedit_slot1=%d\nsnippets=%s\n", g_bar_slot[0], g_bar_slot[1], sn); }
    fprintf(f, "touch_feedback=%d\ntouch_feedback_color=%d\ntouch_feedback_opacity=%d\ntouch_feedback_size=%d\ntouch_feedback_ms=%d\n", g_touch_fb, g_touch_fb_rgb, g_touch_fb_alpha, g_touch_fb_size, g_touch_fb_ms);
#endif
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
/* a start argument "bm:TEXT" (tools/device_tests.py): the newest bookmark whose address, else title, contains
   TEXT, so test runs open personal pages without their addresses leaving the device */
static const char *start_url(const char *arg) {
    if (strncmp(arg, "bm:", 3) || !arg[3]) return arg;
    for (int k = 0; k < 2; k++)
        for (int i = g_nbm - 1; i >= 0; i--) if (strcasestr(k ? g_bms[i].title : g_bms[i].url, arg + 3)) return g_bms[i].url;
    return "lumen://newtab";
}
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
/* An element's src resolved against its document. Layout sizes every image on each pass (a dragged map's
   tiles every frame), and parsing the URLs again took a sixth of such frames: remembered per element. The
   result is borrowed until this thread's next call. */
static const char *node_src_url(Node *n, const char *s) {
    static _Thread_local struct { const Node *n; char *base, *src, *abs; } memo[1024];
    static _Thread_local char *scratch;
    const char *base = n->doc->url;
    if (strlen(s) > 262144) { free(scratch); scratch = url_join(base, s); return scratch; }   /* huge data: URLs are not copied */
    uint64_t k = (uint64_t)(uintptr_t)n * 0x9E3779B97F4A7C15ull;
    __typeof__(&memo[0]) m = &memo[k >> 54];
    if (m->n == n && m->src && !strcmp(m->src, s) && (base ? m->base && !strcmp(m->base, base) : !m->base)) return m->abs;
    free(m->base); free(m->src); free(m->abs);
    m->n = n; m->base = base ? xstrdup(base) : NULL; m->src = xstrdup(s); m->abs = url_join(base, s);
    return m->abs;
}
/* an <img>'s chosen source (srcset/sizes/<picture>), else its src (a video's poster); NULL for none */
static const char *img_src(Node *n, char *buf, size_t bn, float *dens) {
    *dens = 1;
    if (n->tag != A_img) return node_attr(n, n->tag == A_video ? "poster" : "src");
    if (!node_attr(n, "srcset") && !(n->parent && node_is(n->parent, atom("picture")))) return node_attr(n, "src");
    return css_img_source(n, buf, bn, dens) ? buf : NULL;
}
static Image *node_img(Node *n) {
    if (n->tag == A_iframe) { Page *fp = frame_find(n); return fp && fp->cv.px ? &fp->img : NULL; }
    if (n->tag == A_video) { Image *f = media_frame_for(n); if (f) return f; }
    if (n->tag == A_canvas) { Image *c = c2d_image_for(n); return c ? c : media_frame_for(n); }   /* 2D canvases present here; WebGL/WebGPU push frames to a live player */
    char b[2048]; float dn; const char *s = img_src(n, b, sizeof b, &dn); if (!s) return NULL;
    const char *u = node_src_url(n, s); return u ? cache_get(u) : NULL;
}
static Image *url_img(const char *u) { return u ? cache_get(u) : NULL; }
static bool img_size(Node *n, float *w, float *h) {
    if (n->tag == A_iframe) return false;
    g_in_layout = true; Image *im = node_img(n); g_in_layout = false;
    if (!im) {
        char b[2048]; float dn; const char *s = n->tag == A_img ? img_src(n, b, sizeof b, &dn) : NULL; const char *u = s ? node_src_url(n, s) : NULL;
        SDL_LockMutex(icache_mu); ImgSlot *sl = u ? hm_get(&icache, u) : NULL; bool ok = sl && sl->evicted; if (ok) { *w = sl->ew / dn; *h = sl->eh / dn; } SDL_UnlockMutex(icache_mu);
        return ok;
    }
    float dn = 1; char b[2048]; if (n->tag == A_img) img_src(n, b, sizeof b, &dn);
    *w = image_css_w(im) / dn; *h = image_css_h(im) / dn; return true; }

typedef struct { char *url; uint64_t gen; float vw, vh, dpr; char *body; size_t blen; char *ctype; char *html; char *ref; } LoadReq;

/* Page-load phase timings in debug Android builds (numbers only, never URLs). */
#if defined(__ANDROID__) && !defined(NDEBUG)
#define LOAD_TRACE(...) __android_log_print(ANDROID_LOG_INFO, "LumenLoad", __VA_ARGS__)
#else
#define LOAD_TRACE(...) do { if (0) fprintf(stderr, __VA_ARGS__); } while (0)
#endif
static double g_load_trace_until;   /* main-loop stalls are logged until then */

/* Downloads a page's blocking subresources concurrently, like a browser's preload scanner. */
typedef struct { char **urls; NetResponse **out; int n; SDL_AtomicInt next; } FetchBatch;
static int batch_worker(void *arg) {
    FetchBatch *b = arg;
    for (int i; (i = SDL_AddAtomicInt(&b->next, 1)) < b->n;)
        if (b->urls[i]) b->out[i] = net_fetch_sync(net_request_new("GET", b->urls[i]));
    return 0;
}
typedef struct { char **urls; int n; const char *ref; } ModBatch;
static int module_worker(void *arg) { ModBatch *m = arg; js_module_prefetch((const char *const *)m->urls, m->n, m->ref); return 0; }
static void fetch_batch(char **urls, NetResponse **out, int n) {
    FetchBatch b = { urls, out, n, { 0 } };
    SDL_Thread *th[12]; int nt = 0;
    for (; nt < LMIN(n - 1, 12); nt++) if (!(th[nt] = SDL_CreateThread(batch_worker, "lumen-fetch", &b))) break;
    batch_worker(&b);
    for (int i = 0; i < nt; i++) SDL_WaitThread(th[i], NULL);
}

/* a navigation's response turned out to be a file: the tab stops loading and stays on its page */
static void nav_download_started(uint64_t id, void *ud) {
    SDL_Event ev; SDL_zero(ev); ev.type = EV_DL; ev.user.code = 1; ev.user.data1 = ud; ev.user.data2 = (void *)(uintptr_t)id; SDL_PushEvent(&ev);
}
static SDL_AtomicInt g_dl_posted;
static void dl_changed(uint64_t id, bool started) {   /* download threads: one pending wakeup at a time */
    (void)id; (void)started;
    if (!SDL_CompareAndSwapAtomicInt(&g_dl_posted, 0, 1)) return;
    SDL_Event ev; SDL_zero(ev); ev.type = EV_DL; SDL_PushEvent(&ev);
}
static void loader_done(LoadReq *rq) { free(rq->url); free(rq->body); free(rq->ctype); free(rq->html); free(rq->ref); free(rq); }
static int loader(void *arg) {
    LoadReq *rq = arg; double t0 = now_ms();
    NetRequest *nr = rq->html ? NULL : net_request_new(rq->body ? "POST" : "GET", rq->url);
    if (rq->body) {
        nr->body = rq->body; nr->body_len = rq->blen; rq->body = NULL;
        headers_set(&nr->headers, "Content-Type", rq->ctype && *rq->ctype ? rq->ctype : "application/x-www-form-urlencoded");
    }
    DlProbe *probe = nr && !strncmp(rq->url, "http", 4) ? dl_probe(nr, rq->ref, nav_download_started, (void *)(uintptr_t)rq->gen) : NULL;
    NetResponse *r = rq->html ? NULL : net_fetch_sync(nr);
    int kind = probe ? dl_probe_finish(probe, r) : 0;
    if (kind == 1) { if (r) net_response_free(r); loader_done(rq); return 0; }   /* the download carries on by itself */
    if (r && (r->status == 204 || r->status == 205)) {   /* No Content: the tab stays on its page, as for a download */
        nav_download_started(0, (void *)(uintptr_t)rq->gen); net_response_free(r); loader_done(rq); return 0;
    }
    double t_html = now_ms();
    Page *p = xcalloc(1, sizeof *p); p->gen = rq->gen; p->internal_ui = rq->html != NULL;
    const char *body = r && r->body ? r->body : "";
    size_t blen = r && r->body ? r->body_len : 0;
    char *errbuf = NULL;
    if (rq->html) { body = rq->html; blen = strlen(body); }
    else if (!r || r->status == 0) { errbuf = xmalloc(512); snprintf(errbuf, 512, "<title>Error</title><body style='font:15px system-ui;padding:40px;color:#444'><h2>Can't open this page</h2><p>%s</p>", rq->url); body = errbuf; blen = strlen(errbuf); }
    const char *ctype = r && r->status > 0 && !rq->html ? headers_get(&r->headers, "content-type") : NULL;
    if (r && r->status == 200 && !rq->html) {
        char *viewer = local_viewer_html(r->url, ctype, kind == 2);
        if (viewer) { free(r->body); r->body = viewer; r->body_len = strlen(viewer); body = viewer; blen = r->body_len; ctype = NULL; }
    }
    p->url = xstrdup(r && r->url ? r->url : rq->url);
    p->d = doc_new(p->url);
    if (r && r->status > 0 && !rq->html) { p->d->csp = headers_join(&r->headers, "content-security-policy"); p->d->csp_ro = headers_join(&r->headers, "content-security-policy-report-only"); p->d->referrer_policy = headers_join(&r->headers, "referrer-policy"); }
    bool xml = xml_mime(ctype);
    char *decoded = NULL;
    if (!rq->html && !errbuf) {   /* the document's encoding; anything but UTF-8 is decoded to UTF-8 first */
        int enc; size_t dn = 0;
        if ((decoded = enc_document((const uint8_t *)body, blen, ctype, xml, &enc, &dn))) { body = decoded; blen = dn; }
        p->d->charset = enc;
    }
    if (xml) { xml_parse(p->d, body, blen); p->d->content_type = mime_essence(ctype); }
    else html_parse(p->d, body, blen);
    free(decoded);
    double t_parse = now_ms();
    /* Classic scripts and stylesheets in document order, fetched together; module scripts
     * and modulepreloads with their whole static import graphs alongside. */
    char **urls = NULL, **mods = NULL; NetResponse **got = NULL; int nurl = 0, ui = 0, nmod = 0;
    bool js = !getenv("LUMEN_NO_JS");
    for (Node *n = p->d->node.first; n; n = node_next_deep(n, &p->d->node)) {
        if (n->type != NODE_ELEMENT) continue;
        const char *rel = n->tag == A_link ? node_attr(n, "rel") : NULL, *href = NULL;
        bool module = false;
        if (jsg_is_script(n) && js && (module = jsg_module_script(n), module || jsg_classic_script(n))) href = jsg_script_src(n);
        else if (rel && strstr(rel, "modulepreload")) { href = js ? node_attr(n, "href") : NULL; module = true; }
        else if (rel && strstr(rel, "stylesheet")) href = node_attr(n, "href");
        char *u = href ? url_join(doc_base(p->d), href) : NULL;
        if (!u) continue;
        char ***v = module ? &mods : &urls; int *k = module ? &nmod : &nurl;
        *v = xrealloc(*v, sizeof **v * (size_t)(*k + 1)); (*v)[(*k)++] = u;
    }
    got = xcalloc((size_t)nurl + 1, sizeof *got);
    if (gen_live(rq->gen)) {
        ModBatch mb = { mods, nmod, p->url };
        SDL_Thread *mt = nmod ? SDL_CreateThread(module_worker, "lumen-modules", &mb) : NULL;
        if (nmod && !mt) module_worker(&mb);
        fetch_batch(urls, got, nurl);
        if (mt) SDL_WaitThread(mt, NULL);
    }
    double t_batch = now_ms();
    if (!getenv("LUMEN_NO_JS")) for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (!gen_live(rq->gen)) break;
        if (!jsg_is_script(n)) continue;
        bool mod = jsg_module_script(n);
        if (!mod && !jsg_classic_script(n)) continue;
        PScript sc = { n, NULL, 0, NULL, mod };
        const char *src = jsg_script_src(n);
        if (src) {
            sc.name = url_join(doc_base(p->d), src);
            if (!sc.name) continue;
            if (mod) {   /* prefetched above; js_run_module compiles it and its imports */
                sc.src = js_module_take(sc.name, p->url, &sc.len);
                p->scripts = xrealloc(p->scripts, sizeof *p->scripts * (size_t)(p->nscripts + 1)); p->scripts[p->nscripts++] = sc;
                continue;
            }
            NetResponse *sr = NULL;
            for (int i = ui; i < nurl; i++) if (got[i] && urls[i] && !strcmp(urls[i], sc.name)) { sr = got[i]; got[i] = NULL; ui = i + 1; break; }
            if (!sr) sr = net_fetch_sync(net_request_new("GET", sc.name));
            if (sr && sr->status >= 200 && sr->status < 300) { sc.src = sr->body; sc.len = sr->body_len; sr->body = NULL; }
            if (sr) net_response_free(sr);
        } else { sc.src = node_text_content(n); sc.len = strlen(sc.src); sc.name = xstrdup(p->url); }
        p->scripts = xrealloc(p->scripts, sizeof *p->scripts * (size_t)(p->nscripts + 1)); p->scripts[p->nscripts++] = sc;
    }
    double t_scripts = now_ms();
    p->e = style_engine_new(p->d); p->e->media.vw = rq->vw; p->e->media.vh = rq->vh; p->e->media.dpr = rq->dpr;
    for (Node *n = p->d->node.first; n; n = node_next_deep(n, &p->d->node)) {
        if (!gen_live(rq->gen)) break;
        if (n->type != NODE_ELEMENT) continue;
        if (n->tag == A_style) {
            char *t = node_text_content(n); StyleSheet *s = css_parse_sheet(t, strlen(t), p->d->url, 1, &p->e->media); s->owner = n; style_engine_add_sheet(p->e, s); free(t);
        } else if (n->tag == A_link && node_attr(n, "rel") && strstr(node_attr(n, "rel"), "stylesheet") && node_attr(n, "href")) {
            char *u = url_join(doc_base(p->d), node_attr(n, "href"));
            NetResponse *cr = NULL;
            for (int i = 0; u && i < nurl; i++) if (got[i] && urls[i] && !strcmp(urls[i], u)) { cr = got[i]; got[i] = NULL; break; }
            if (!cr && u) cr = net_fetch_sync(net_request_new("GET", u));
            if (cr && cr->status == 200) { StyleSheet *s = css_parse_sheet(cr->body, cr->body_len, cr->url, 1, &p->e->media); s->owner = n; style_engine_add_sheet(p->e, s); }
            if (cr) net_response_free(cr); free(u);
        }   /* images load asynchronously once the page is shown (sync_images) */
    }
    for (int i = 0; i < nurl; i++) { free(urls[i]); net_response_free(got[i]); }
    for (int i = 0; i < nmod; i++) free(mods[i]);
    free(urls); free(got); free(mods);
    double t_css = now_ms();
    style_recalc(p->e, &p->d->node, true);
    p->seen_ver = p->d->dom_version;
    p->load_ms = now_ms() - t0;
    LOAD_TRACE("loader html_ms=%.0f html_bytes=%zu parse_ms=%.0f batch_ms=%.0f classic_css=%d modules=%d scripts_ms=%.0f css_ms=%.0f style_ms=%.0f",
        t_html - t0, blen, t_parse - t_html, t_batch - t_parse, nurl, nmod, t_scripts - t_batch, t_css - t_scripts, now_ms() - t_css);
    if (r) net_response_free(r);
    free(errbuf); loader_done(rq);
    SDL_Event ev; SDL_zero(ev); ev.type = EV_LOADED; ev.user.data1 = p; SDL_PushEvent(&ev);
    return 0;
}

static Page *g_loading_page;   /* the page whose load event may still be waiting for its images */
static void page_free(Page *p) {
    if (!p) return;
    if (g_loading_page == p) g_loading_page = NULL;
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

typedef struct Tab { Page *cur; bool loading, relayout, desktop;   /* desktop: the site is in "Desktop site" mode */ char *hist[256]; uint64_t hgen[256]; int nhist, hpos; char url[2048]; float sy; uint64_t lgen; int ws;
    int zoom_pct; /* zero inherits the default */
    double cpu_ms, media_ms0, budget, bud_t, unlimit_until; float pct, lim, cpuhist[600]; int ncpu, cpui, cpumode, vctl, vap, lite; uint64_t vgen; bool limited, info, asleep; double bg_since; int crashes;
    char *stitle;   /* the title saved with a restored tab, until its page loads */
    uint32_t wid, opener, opener_wid; char *wname; bool scripted, close_req;   /* window.open: this tab's window id; the context and tab that opened it, its name; to close */ } Tab;
typedef struct App {
    SDL_Window *win; SDL_MetalView mview; Gpu *gpu;
#ifdef __ANDROID__
    bool android_suspended, focus_reveal;
    AndroidTouch touch;
    AndroidFling fling;
    float fling_x, fling_y;
    float step_rx, step_ry; bool step_started;   /* touch scrolling in steps: finger travel not yet used; the drag's first 5 px done */
    Tab *touch_tab;
    uint64_t touch_generation;
    unsigned touch_dom_flags;
    unsigned touch_block;   /* TOUCH_NO_X/Y: the touch-action of the contact's target forbids that panning */
#endif
    int pw, ph, screen_w, screen_h; float scale, vw, vh, input_scale;
    Canvas frame, page; DisplayList pdl, cdl, hdl;
    bool frame_page_stale;
    struct DSig *osig, *nsig, *ssig; int nosig, cosig, cnsig, cssig; uint64_t chash; bool osig_ok;   /* last page display list, to raster only what changed; ssig: this one moved back by a scroll */
    Tab *t, *tabs[MAX_TABS]; int ntabs, ti;
    char wsname[16][64]; Tab *wslast[16]; int nws, wi; float side;
    /* the menu's panel (panel_open): its tab, the tab it covers, one closed but not yet freed, a link it
       opened for the covered tab, a close asked for from the page, and the dimmed picture beside it */
    Tab *pan, *pan_under, *pan_dead; char *pan_go; bool pan_close; uint32_t *pan_bg; int pan_bgw, pan_bgh;
    int wsicon[16]; Image *icimg[16][2];
    Image *sym[8][4]; bool vbars, vbar_any, vbar_tick; uint64_t vbar_msv; float vbr[4];   /* vbar_tick: only the bars' clocks are due; vbr: their union in the viewport */
    int tip_tab; double tip_until;     /* CPU-limit dot tooltip pinned by a click */
    bool editing; int us[2]; float uscroll; bool umenu;   /* URL field: anchor/focus bytes of t->url, text scroll, Android Copy/Paste/Share menu */
    double caret_t;
    bool dirty;
    Font *ui;
    int hover; double frame_ms;
    bool vonly, deferred, vframe; double last_input, last_full; float last_sy; uint64_t last_ver; Page *last_page; bool gvid_ok; const void *vown; float vrect[4]; int nxown; const void *xown[GPU_EXTRA_LAYERS]; float xrect[GPU_EXTRA_LAYERS][4]; float srect[4]; bool sdirty; int last_nv;   /* last_nv: media frames in the last display list; srect: viewport rect of element scrollers to repaint */
    const Box *sbox; float sbox_x0, sbox_y0;   /* the one scroller moved since the last frame (NULL: several) and its painted offsets */
} App;
#ifdef __ANDROID__
static void bare_set(App *a, bool on);
#endif
static App *g_app;
/* "Desktop site" only changes the user agent; the layout width follows the window and the zoom */
static float tab_zoom(const Tab *tab) { return (tab && tab->zoom_pct ? tab->zoom_pct : g_page_zoom_pct) / 100.f; }
static bool url_desktop(const char *url) {
    URL u; if (!url || strncmp(url, "http", 4) || !url_parse(url, &u)) return false;
    bool on = net_desktop_host(u.host); url_free(&u); return on;
}
/* the panel takes the right of the window below the toolbar, most of a phone held upright */
static bool panel_shown(const App *a) { return a->pan && a->t == a->pan; }
/* a page in fullscreen (the Fullscreen API): the window is all page, with no tab strip, toolbar, side
   bar or system bars; the page it is, and the side bar's width before */
static Page *g_fs_page;
static float g_fs_side;
static float page_left(const App *a) {
    if (!panel_shown(a) || g_bare) return a->side;
    float w = (a->vw - a->side) * 0.6f;
    if (w < 400) w = a->vw - a->side - 32;
    if (w > 680) w = 680;
    return LMAX(a->side, a->vw - w);
}
/* the tab whose page the toolbar shows: the covered one while the panel is up */
static Tab *bar_tab(App *a) { return a->pan && a->pan_under ? a->pan_under : a->t; }
static float page_width(const App *a) { return (a->vw - page_left(a)) / tab_zoom(a->t); }
static float page_height(const App *a) { return (a->vh - BAR) / tab_zoom(a->t); }
/* the touch scroll step's largest setting: the page's visible height, in whole 10 px */
static int scroll_px_max(const App *a) { return LMAX(10, (int)(page_height(a) / 10) * 10); }
static float page_scale(const App *a) { return a->scale * tab_zoom(a->t); }
static float page_x(const App *a, float x) { return (x - page_left(a)) / tab_zoom(a->t); }
static float page_y(const App *a, float y) { return (y - BAR) / tab_zoom(a->t); }
static void page_input_reset(void);
#ifdef __ANDROID__
static bool tsel_ui(App *a);
static void tsel_overlay(App *a, DisplayList *dl);
static void kbm_overlay(App *a, DisplayList *dl);
static void pop_overlay(App *a, DisplayList *dl);
static bool pop_shown(void);
/* Keyboard on second tap: the field the last tap placed the caret in without the keyboard, that
   tap (window px), and its Paste | Select all bubble (anchored in page px, drawn in viewport px);
   tx, ty, tt: the last page tap, focused or not */
static struct {
    Node *n; float x, y, tx, ty; double tt;
    bool menu; float mx, my;
    float bar[4], bend[10]; int act[10], nb, hit;
    double hold_t, hold_next; bool held;   /* a bar button held down: when, its next repeat, whether it acted */
} g_kbm;
#endif
static void page_zoom_changed(App *a) {
    page_input_reset();
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
    a->dirty = true;
    a->vonly = a->sdirty = a->osig_ok = a->gvid_ok = false;
    g_sm_dx = g_sm_dy = 0;
#ifdef __ANDROID__
    if (a->t && a->t->cur && a->t->cur->js) js_touch(a->t->cur->js, NULL, "reset", 0, 0, 0, 0, 0);
    touch_frames_reset();
    android_touch_reset(&a->touch);
    android_fling_reset(&a->fling);
#endif
}
static void page_zoom_adjust(App *a, int direction) {
    if (!a->t) return;
    int current = a->t->zoom_pct ? a->t->zoom_pct : g_page_zoom_pct;
    int next = direction ? page_zoom_step(current, direction) : 0;
    if (a->t->zoom_pct == next) return;
    a->t->zoom_pct = next;
    page_zoom_changed(a);
}
#ifdef __ANDROID__
static Node *page_focus(App *app);
static Node *edit_focus(App *app);
#endif
static float sidebar_width(float requested) {
#ifdef __ANDROID__
    (void)requested;
    return 0;
#else
    return requested;
#endif
}
static bool workspace_action(App *app, const char *url);
static bool fragment_link(App *a, const char *h);
static void panel_close(App *a, bool keep_picture);
static void panel_backdrop(App *a, int bar_px, int width);
static void publish_gens(App *a) { for (int i = 0; i < MAX_TABS; i++) g_tab_gen[i] = i < a->ntabs ? a->tabs[i]->lgen : 0; }
static uint32_t g_next_wid = 1, g_show_wid;   /* g_show_wid: a tab window.open opened or focused, shown by popup_service */
static Tab *tab_new(App *a) {
    if (a->ntabs == MAX_TABS) return NULL;
    Tab *t = xcalloc(1, sizeof *t); t->wid = g_next_wid++; t->hpos = -1; t->ws = a->wi; t->vctl = -1; t->lite = -1; t->lim = 0.4f; a->tabs[a->ntabs++] = t;
    if (!a->t) a->t = t;
    return t;
}
static bool is_youtube(const char *u) { const char *h = u ? strstr(u, "://") : NULL; if (!h) return false; h += 3; size_t n = strcspn(h, "/?#:"); return n >= 11 && (n == 11 || h[n - 12] == '.') && !strncmp(h + n - 11, "youtube.com", 11); }
static bool vctl_on(const Tab *t) { return t->vctl >= 0 ? t->vctl : g_vctl; }
static bool lite_on(const Tab *t) { return t->lite >= 0 ? t->lite : g_lowmem; }
static bool video_bars(App *a, DisplayList *dl);
static void tab_unlimit(Tab *t) { t->limited = t->info = false; t->ncpu = t->cpui = 0; if (t->cur) media_set_limit(t->cur->d, 0); }
#define TAB_CALL(ud, expr) do { Tab *o_ = g_app->t; g_app->t = (Tab *)(ud); expr; g_app->t = o_; } while (0)


/* a typed localhost or IPv4 address opens over http, as in Chrome */
static bool local_host(const char *in) {
    size_t n = strcspn(in, ":/?#"), dots = 0;
    if (n == 9 && !strncasecmp(in, "localhost", 9)) return true;
    for (size_t i = 0; i < n; i++) if (in[i] == '.') dots++; else if (!isdigit((unsigned char)in[i])) return false;
    return n && dots == 3;
}
static char *normalize_url(const char *in) {
    while (*in == ' ') in++;
    if (strstr(in, "://") || !strncmp(in, "about:", 6) || !strncmp(in, "data:", 5)) return xstrdup(in);
    if (!strchr(in, ' ') && (strchr(in, '.') || local_host(in))) { char *o = xmalloc(strlen(in) + 9); sprintf(o, "%s://%s", local_host(in) ? "http" : "https", in); return o; }
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
/* s as a JSON string; control characters, < and the line separators escaped, other UTF-8 kept */
static void sb_json(SB *b, const char *s) {
    sb_putc(b, '"');
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        if (*p == '"' || *p == '\\') { sb_putc(b, '\\'); sb_putc(b, (char)*p); }
        else if (*p < 0x20 || *p == '<') sb_printf(b, "\\u%04x", *p);
        else if (*p == 0xe2 && p[1] == 0x80 && (p[2] == 0xa8 || p[2] == 0xa9)) { sb_printf(b, "\\u%04x", p[2] == 0xa8 ? 0x2028 : 0x2029); p += 2; }
        else sb_putc(b, (char)*p);
    }
    sb_putc(b, '"');
}
#ifdef __ANDROID__
#define HUD_TEXT "Shows the whole phone's CPU use (top) and Lumen's (bottom) next to the address bar, both as a share of all its cores, updated every 3 seconds. Tap it for the current tab's CPU and the memory use."
#else
#define HUD_TEXT "Shows how much CPU and memory (GB) Lumen uses at the bottom of the workspace sidebar, updated every 2 seconds. Click it to see the whole computer's CPU and memory use."
#endif
#ifdef __ANDROID__
static void renderer_text(App *app, char *text, size_t capacity) {
    snprintf(text, capacity, "Requested: %s. Active: %s.%s", renderer_key(g_renderer), gpu_backend_name(app->gpu),
        !app->gpu && g_renderer != GPU_BACKEND_CPU ? " GPU unavailable; software fallback." : "");
}
static void update_renderer_readout(App *app) {
    Page *page = app->t->cur;
    if (!page || !page->internal_ui || !page->d) return;
    Node *readout = doc_get_element_by_id(page->d, "lumen-renderer");
    if (!readout) return;
    char text[256]; renderer_text(app, text, sizeof text);
    if (!(readout->first && readout->first == readout->last && readout->first->type == NODE_TEXT && !strcmp(readout->first->text, text))) {
        node_set_text_content(readout, text);
        app->t->relayout = true;
    }
    for (int backend = GPU_BACKEND_AUTO; backend <= GPU_BACKEND_CPU; backend++) {
        char id[40]; snprintf(id, sizeof id, "lumen-renderer-%s", renderer_key((GpuBackend)backend));
        Node *button = doc_get_element_by_id(page->d, id);
        if (!button) continue;
        const char *classes = backend == (int)g_renderer ? "btn on" : "btn";
        const char *current = node_attr(button, "class");
        if (!current || strcmp(current, classes)) { node_set_attr(button, "class", classes); app->t->relayout = true; }
    }
}
static void resolution_text(App *app, char *text, size_t capacity) {
    char screen[64] = "unavailable";
    if (app->screen_w > 0 && app->screen_h > 0) snprintf(screen, sizeof screen, "%d x %d px", app->screen_w, app->screen_h);
    snprintf(text, capacity, "Screen: %s. App render: %d x %d px. Layout: %.1f x %.1f px.", screen, app->pw, app->ph, app->vw, app->vh);
}
static void update_resolution_readout(App *app) {
    Page *page = app->t->cur;
    if (!page || !page->internal_ui || !page->d) return;
    Node *readout = doc_get_element_by_id(page->d, "lumen-resolution");
    if (!readout) return;
    char text[256]; resolution_text(app, text, sizeof text);
    if (!(readout->first && readout->first == readout->last && readout->first->type == NODE_TEXT && !strcmp(readout->first->text, text))) {
        node_set_text_content(readout, text);
        app->t->relayout = true;
    }
    for (int percent = 100; percent <= 300; percent += 50) {
        char id[40]; snprintf(id, sizeof id, "lumen-pixel-scale-%d", percent);
        Node *button = doc_get_element_by_id(page->d, id);
        if (!button) continue;
        const char *classes = percent == g_pixel_scale_pct ? "btn on" : "btn";
        const char *current = node_attr(button, "class");
        if (!current || strcmp(current, classes)) { node_set_attr(button, "class", classes); app->t->relayout = true; }
    }
}
#endif
/* ---------- downloads page ---------- */
static void dl_size_text(int64_t n, char *out, size_t cap) {
    if (n < 1024) snprintf(out, cap, "%lld B", (long long)n);
    else if (n < 1024 * 1024) snprintf(out, cap, "%.0f KB", n / 1024.0);
    else if (n < 1024LL * 1024 * 1024) snprintf(out, cap, "%.1f MB", n / 1048576.0);
    else snprintf(out, cap, "%.2f GB", n / 1073741824.0);
}
static void dl_status_text(const DlInfo *d, char *out, size_t cap) {
    char done[24], total[24], rate[24];
    dl_size_text(d->done, done, sizeof done); dl_size_text(d->total, total, sizeof total); dl_size_text((int64_t)d->bps, rate, sizeof rate);
    switch (d->state) {
    case DL_RUNNING:
        if (!d->conns && !d->done) { snprintf(out, cap, "Starting\xE2\x80\xA6"); break; }
        if (d->total >= 0) snprintf(out, cap, "%s of %s \xC2\xB7 %s/s \xC2\xB7 %d connection%s", done, total, rate, d->conns, d->conns == 1 ? "" : "s");
        else snprintf(out, cap, "%s \xC2\xB7 %s/s", done, rate);
        break;
    case DL_PAUSED: if (d->total >= 0) snprintf(out, cap, "Paused \xC2\xB7 %s of %s", done, total); else snprintf(out, cap, "Paused \xC2\xB7 %s", done); break;
    case DL_DONE: snprintf(out, cap, "%s \xC2\xB7 Done", d->total >= 0 ? total : done); break;
    case DL_FAILED: snprintf(out, cap, "Failed: %s", *d->error ? d->error : "network error"); break;
    default: snprintf(out, cap, "Cancelled"); break;
    }
}
static int dl_percent(const DlInfo *d) { return d->state == DL_DONE ? 100 : d->total > 0 ? (int)(d->done * 100 / d->total) : 0; }
static uint64_t g_dl_page_sver;   /* the download list the open Downloads page shows */
/* the Downloads page follows progress in place and asks the page to refresh when the list or a state changes */
static void downloads_page_update(App *a) {
    Page *page = a->t->cur;
    if (!page || !page->internal_ui || !page->d) return;
    if (dl_state_version() != g_dl_page_sver) {
        g_dl_page_sver = dl_state_version();
        if (page->js) js_eval(page->js, "window.lumenRefresh && lumenRefresh()", "lumen:downloads");
        return;
    }
    DlInfo v[100]; int n = dl_list(v, 100);
    for (int i = 0; i < n; i++) {
        if (v[i].state != DL_RUNNING) continue;
        char id[48], st[160], w[32]; Node *e;
        snprintf(id, sizeof id, "dl-%llu-s", (unsigned long long)v[i].id); dl_status_text(&v[i], st, sizeof st);
        if ((e = doc_get_element_by_id(page->d, id))) { char *cur = node_text_content(e); if (strcmp(cur, st)) { node_set_text_content(e, st); a->t->relayout = true; } free(cur); }
        snprintf(id, sizeof id, "dl-%llu-b", (unsigned long long)v[i].id); snprintf(w, sizeof w, "width:%d%%", dl_percent(&v[i]));
        if ((e = doc_get_element_by_id(page->d, id)) && strcmp(node_attr(e, "style") ? node_attr(e, "style") : "", w)) { node_set_attr(e, "style", w); a->t->relayout = true; }
    }
    a->dirty = true; a->vonly = false;
}

/* ---------- Lumen's own pages: src/app/newtab.html, drawn from the JSON below ---------- */
static const unsigned char newtab_html[] = {
#include "newtab.inc"
};
static const char *const newtab_sections[] = {
#ifdef __ANDROID__
    "home",
#endif
    "bookmarks", "history", "downloads", "settings" };
#define NEWTAB_NSEC (int)(sizeof newtab_sections / sizeof *newtab_sections)
static char *internal_page(App *app, const char *u) { (void)app; (void)u; return xstrdup((const char *)newtab_html); }
/* a query value, %-decoded; NULL when absent */
static char *qparam_dup(const char *q, const char *k) {
    size_t kl = strlen(k);
    for (const char *p = q; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL)
        if (!strncmp(p, k, kl) && p[kl] == '=') return url_decode(p + kl + 1, strcspn(p + kl + 1, "&#"));
    return NULL;
}
static void sb_json_int_array(SB *b, const int *v, int n) {
    sb_putc(b, '[');
    for (int i = 0; i < n; i++) sb_printf(b, "%s%d", i ? "," : "", v[i]);
    sb_putc(b, ']');
}
/* files other apps open or share with Lumen, copied in by LumenActivity when they have no path of their own */
static const char *opened_dir(void) {
#ifdef __ANDROID__
    static char dir[1100];
    if (!*dir) { const char *files = SDL_GetAndroidInternalStoragePath(); if (files) snprintf(dir, sizeof dir, "%s/opened", files); }
    return dir;
#else
    return "";
#endif
}
/* path, resolved, when it is the file browser's top folder or inside it, or a file opened from another app */
static bool files_resolve(const char *path, char *out) {
    char top[PATH_MAX], opened[PATH_MAX];
    if (!path || path[0] != '/' || !realpath(path, out)) return false;
    if (*opened_dir() && realpath(opened_dir(), opened)) { size_t k = strlen(opened); if (!strncmp(out, opened, k) && out[k] == '/') return true; }
    if (!realpath(download_store_top(), top)) snprintf(top, sizeof top, "%s", download_store_top());
    size_t n = strlen(top);
    if (n == 1) return true;
    return !strncmp(out, top, n) && (out[n] == 0 || out[n] == '/');
}
typedef struct { char *name; bool dir; long long size; long long mtime; } FileEntry;
/* folder want, or folder def when want is not one the browser may show ("missing" then); a file
   given as want lists its folder and names it as "file" */
static void files_json(SB *b, const char *want, const char *def) {
    char dir[PATH_MAX], top[PATH_MAX], root[PATH_MAX];
    struct stat ws;
    if (!realpath(download_store_top(), top)) snprintf(top, sizeof top, "%s", download_store_top());
    if (!realpath(def, root)) snprintf(root, sizeof root, "%s", def);
    if (!files_resolve(want, dir) || stat(dir, &ws)) {
        if (want && *want) sb_puts(b, "\"missing\":true,");
        snprintf(dir, sizeof dir, "%s", root);
    } else if (S_ISREG(ws.st_mode)) {
        sb_puts(b, "\"file\":"); sb_json(b, dir); sb_putc(b, ',');
        char *slash = strrchr(dir, '/');
        if (slash == dir) slash[1] = 0; else if (slash) *slash = 0;
    }
    sb_puts(b, "\"dir\":"); sb_json(b, dir);
    sb_puts(b, ",\"root\":"); sb_json(b, root);
    if (strcmp(dir, top) && strcmp(dir, "/")) {
        char parent[PATH_MAX]; snprintf(parent, sizeof parent, "%s", dir);
        char *slash = strrchr(parent, '/');
        if (slash == parent) slash[1] = 0; else if (slash) *slash = 0;
        sb_puts(b, ",\"parent\":"); sb_json(b, parent);
    }
    sb_printf(b, ",\"grant\":%s,\"files\":[", download_store_all_files() ? "false" : "true");
    DIR *d = opendir(dir);
    const char *error = NULL;
    if (!d) error = "Lumen cannot open this folder.";
    else {
        int fd = dirfd(d), count = 0;
        struct dirent *e;
        while ((e = readdir(d)) && count < 5000) {
            if (e->d_name[0] == '.') continue;   /* ., .. and hidden files */
            struct stat st;
            if (fstatat(fd, e->d_name, &st, 0)) continue;
            bool folder = S_ISDIR(st.st_mode);
            if (!folder && !S_ISREG(st.st_mode)) continue;
            sb_puts(b, count++ ? ",{\"n\":" : "{\"n\":"); sb_json(b, e->d_name);
            sb_printf(b, ",\"d\":%d,\"s\":%lld,\"m\":%lld}", folder, (long long)st.st_size, (long long)st.st_mtime);
        }
        closedir(d);
    }
    sb_puts(b, "]");
    if (error) { sb_puts(b, ",\"error\":"); sb_json(b, error); }
}
static void downloads_json(SB *b, const char *want) {
    DlInfo v[100]; int n = dl_list(v, 100), k = 0;
    g_dl_page_sver = dl_state_version();
    sb_puts(b, "{\"active\":[");
    for (int i = 0; i < n; i++) {
        DlInfo *d = &v[i];
        const char *state = d->state == DL_RUNNING ? "running" : d->state == DL_PAUSED ? "paused" : d->state == DL_FAILED ? "failed" : NULL;
        if (!state) continue;   /* finished files are in the folder below */
        char st[160]; dl_status_text(d, st, sizeof st);
        sb_printf(b, "%s{\"id\":\"%llu\",\"state\":\"%s\",\"pct\":%d,\"resumable\":%s,\"name\":", k++ ? "," : "", (unsigned long long)d->id, state, dl_percent(d), d->resumable ? "true" : "false");
        sb_json(b, d->name); sb_puts(b, ",\"status\":"); sb_json(b, st); sb_putc(b, '}');
    }
    sb_puts(b, "],");
    files_json(b, want, download_store_dir());
    sb_putc(b, '}');
}
static void links_json(SB *b, const Link *v, int n) {
    sb_putc(b, '[');
    for (int i = n - 1; i >= 0; i--) {
        sb_printf(b, "%s{\"i\":%d,\"t\":", i == n - 1 ? "" : ",", i); sb_json(b, v[i].title);
        sb_puts(b, ",\"u\":"); sb_json(b, v[i].url); sb_putc(b, '}');
    }
    sb_putc(b, ']');
}
static void home_json(App *app, SB *b) {
    sb_puts(b, "\"ws\":[");
    for (int w = 0; w < app->nws; w++) {
        sb_puts(b, w ? ",{\"name\":" : "{\"name\":"); sb_json(b, app->wsname[w]);
        sb_printf(b, ",\"cur\":%s,\"tabs\":[", w == app->wi ? "true" : "false");
        for (int i = 0, k = 0; i < app->ntabs; i++) {
            Tab *t = app->tabs[i];
            if (t->ws != w) continue;
            bool own = t->cur && t->cur->internal_ui;
            const char *title = own ? "New Tab" : t->cur && t->cur->d && t->cur->d->title && *t->cur->d->title ? t->cur->d->title : t->stitle ? t->stitle : *t->url ? t->url : "New Tab";
            sb_printf(b, "%s{\"i\":%d,\"cur\":%s,\"title\":", k++ ? "," : "", i, t == bar_tab(app) ? "true" : "false"); sb_json(b, title);
            sb_puts(b, ",\"url\":"); sb_json(b, own ? "" : t->url); sb_putc(b, '}');
        }
        sb_puts(b, "]}");
    }
    sb_printf(b, "],\"canAddWs\":%s", app->nws < 16 && app->ntabs < MAX_TABS ? "true" : "false");
}
static void settings_json(App *app, SB *b) {
    static const int zooms[] = { 50, 100, 150, 200 };
    sb_puts(b, "{\"zooms\":"); sb_json_int_array(b, zooms, 4);
    sb_printf(b, ",\"zoom\":%d", g_page_zoom_pct);
#ifdef __ANDROID__
    static const int scales[] = { 100, 150, 200, 250, 300 };
    static const char *backend_labels[] = { "Auto", "Vulkan", "OpenGL ES", "Software" };
    char text[256];
    sb_puts(b, ",\"pixelScales\":"); sb_json_int_array(b, scales, 5);
    resolution_text(app, text, sizeof text);
    sb_printf(b, ",\"pixelScale\":%d,\"resolution\":", g_pixel_scale_pct); sb_json(b, text);
    sb_puts(b, ",\"renderers\":[");
    for (int backend = GPU_BACKEND_AUTO; backend <= GPU_BACKEND_CPU; backend++) sb_printf(b, "%s[\"%s\",\"%s\"]", backend ? "," : "", renderer_key((GpuBackend)backend), backend_labels[backend]);
    renderer_text(app, text, sizeof text);
    sb_printf(b, "],\"renderer\":\"%s\",\"rendererText\":", renderer_key(g_renderer)); sb_json(b, text);
    int ncolors = (int)(sizeof touch_fb_colors / sizeof *touch_fb_colors);
    sb_printf(b, ",\"touch\":{\"on\":%s,\"color\":%d,\"alpha\":%d,\"size\":%d,\"ms\":%d,\"colors\":", g_touch_fb ? "true" : "false", g_touch_fb_rgb, g_touch_fb_alpha, g_touch_fb_size, g_touch_fb_ms);
    sb_json_int_array(b, touch_fb_colors, ncolors);
    sb_puts(b, ",\"names\":[");
    for (int i = 0; i < ncolors; i++) { if (i) sb_putc(b, ','); sb_json(b, touch_fb_color_names[i]); }
    sb_puts(b, "],\"alphas\":"); sb_json_int_array(b, touch_fb_alphas, (int)(sizeof touch_fb_alphas / sizeof *touch_fb_alphas));
    sb_puts(b, ",\"sizes\":"); sb_json_int_array(b, touch_fb_sizes, (int)(sizeof touch_fb_sizes / sizeof *touch_fb_sizes));
    sb_puts(b, ",\"times\":"); sb_json_int_array(b, touch_fb_times, (int)(sizeof touch_fb_times / sizeof *touch_fb_times));
    sb_putc(b, '}');
    sb_printf(b, ",\"tapKbd\":%s", g_tap_kbd ? "true" : "false");
    sb_printf(b, ",\"editBar\":{\"slots\":[%d,%d],\"choices\":[[%d,\"Backspace\"],[%d,\"Enter\"],[%d,\"Undo\"],[%d,\"Clear\"],[%d,\"Hide menu\"]],\"snippets\":[",
              g_bar_slot[0], g_bar_slot[1], TA_BKSP, TA_ENTER, TA_UNDO, TA_CLEAR, TA_HIDE);
    for (int i = 0; i < g_nsnip; i++) { if (i) sb_putc(b, ','); sb_json(b, g_snip[i]); }
    sb_puts(b, "]}");
#else
    (void)app;
#endif
    sb_printf(b, ",\"lite\":%s,\"cpu\":%s,\"cpuPct\":%d,\"cpuSecs\":%d,\"cpuLim\":%d,\"vctl\":%s,\"sleep\":%s,\"hud\":%s,\"smooth\":%s,\"ua\":%d,\"hudText\":",
        g_lowmem ? "true" : "false", g_cpu_on ? "true" : "false", g_cpu_pct, g_cpu_secs, g_cpu_lim, g_vctl ? "true" : "false", g_tab_sleep ? "true" : "false", g_hud ? "true" : "false", g_smooth ? "true" : "false", g_ua);
    sb_json(b, HUD_TEXT);
    sb_printf(b, ",\"hideTabs\":%s", g_hide_tabs ? "true" : "false");
    sb_printf(b, ",\"scrollPx\":%d,\"scrollMax\":%d", LMIN(g_scroll_px, scroll_px_max(app)), scroll_px_max(app));
    sb_puts(b, ",\"uas\":[");
    for (int i = 0; i < UA_COUNT; i++) { if (i) sb_putc(b, ','); sb_json(b, net_user_agent_name(i)); }
    sb_puts(b, "]}");
}
static const char *g_newtab_msg;   /* a notice for the next state, from the last set? */
static const char *bm_add(Tab *t);
static const char *bm_store(int index, const char *title, const char *raw);
/* <input type=file> on Android: the file chooser (newtab's "pick", in the panel) while a page waits
   for it; several files or one, and the input's accept list */
static struct { bool wait, multi; char accept[512]; } g_pick;
static const char *files_home(void) {
#ifdef __ANDROID__
    return download_store_top();
#else
    const char *h = getenv("HOME");
    return h && *h ? h : "/";
#endif
}
/* the state of section s of q (and of folder dir on Downloads and the file chooser) */
static char *newtab_state(App *app, const char *q) {
    char sec[16] = "";
    qparam(q, "s", sec, sizeof sec);
    if (!strcmp(sec, "pick")) {   /* not one of the sections the others list */
        SB b; sb_init(&b);
        char *dir = qparam_dup(q, "dir");
        sb_printf(&b, "{\"sec\":\"pick\",\"sections\":[],\"panel\":%s,\"pk\":{\"home\":", panel_shown(app) ? "true" : "false");
        sb_json(&b, files_home());
        sb_printf(&b, ",\"multi\":%s,\"wait\":%s,\"accept\":", g_pick.multi ? "true" : "false", g_pick.wait ? "true" : "false");
        sb_json(&b, g_pick.accept);
        sb_putc(&b, ','); files_json(&b, dir, files_home()); free(dir);
        sb_puts(&b, "}}");
        return sb_take(&b);
    }
    if (!strcmp(sec, "workspaces")) snprintf(sec, sizeof sec, "home");
    int k = 0;
    while (k < NEWTAB_NSEC && strcmp(sec, newtab_sections[k])) k++;
    if (k == NEWTAB_NSEC) k = 0;
    const char *s = newtab_sections[k];
    SB b; sb_init(&b);
    sb_printf(&b, "{\"sec\":\"%s\",\"sections\":[", s);
    for (int i = 0; i < NEWTAB_NSEC; i++) sb_printf(&b, "%s\"%s\"", i ? "," : "", newtab_sections[i]);
    sb_printf(&b, "],\"panel\":%s", panel_shown(app) ? "true" : "false");
    if (!strcmp(s, "home")) { sb_putc(&b, ','); home_json(app, &b); }
    else if (!strcmp(s, "bookmarks")) {
#ifdef __ANDROID__
        const char *empty = "No bookmarks yet. Open the \xE2\x8B\xAE menu next to the address bar, then Bookmarks, to add the current page.";
#else
        const char *empty = "No bookmarks yet. Click the star next to the address bar to bookmark a page.";
#endif
        Tab *t = bar_tab(app);
        bool page = t->cur && *t->url && strncmp(t->url, "lumen:", 6);
        sb_puts(&b, ",\"bm\":"); links_json(&b, g_bms, g_nbm);
        sb_puts(&b, ",\"bmEmpty\":"); sb_json(&b, empty);
        sb_printf(&b, ",\"bmPage\":%d", !page ? 0 : bm_find(t->url) >= 0 ? 2 : 1);   /* none, can be added, already added */
        sb_printf(&b, ",\"canAddWs\":%s", app->nws < 16 && app->ntabs < MAX_TABS ? "true" : "false");   /* "Open in new workspace" */
    } else if (!strcmp(s, "history")) { sb_puts(&b, ",\"hist\":"); links_json(&b, g_hv, g_nhv); }
    else if (!strcmp(s, "downloads")) { char *dir = qparam_dup(q, "dir"); sb_puts(&b, ",\"dl\":"); downloads_json(&b, dir); free(dir); }
    else { sb_puts(&b, ",\"set\":"); settings_json(app, &b); }
    if (g_newtab_msg) { sb_puts(&b, ",\"msg\":"); sb_json(&b, g_newtab_msg); g_newtab_msg = NULL; }
    sb_putc(&b, '}');
    return sb_take(&b);
}
static void apply_set(App *a, const char *q);
static bool is_js_url(const char *u);
#ifdef __ANDROID__
static void show_bookmarks(App *a);
static void show_workspaces(App *a);
#endif
/* the Downloads viewer's facts about a file: "view?path=P[&page=N&w=PX]" gives {"size","probe":{media_probe_file},
   "pdf":{"pages","page","img" a file: URL of the page's picture,"w","h"}}; probe and pdf only when they apply */
static char *files_view(const char *q) {
    char *want = qparam_dup(q, "path"), path[PATH_MAX], v[16], probe[1024];
    struct stat st;
    bool ok = want && files_resolve(want, path) && !stat(path, &st) && S_ISREG(st.st_mode);
    free(want);
    if (!ok) return xstrdup("{\"error\":\"Lumen cannot open this file.\"}");
    SB b; sb_init(&b);
    sb_printf(&b, "{\"size\":%lld", (long long)st.st_size);
    const char *dot = strrchr(path, '.');
    if (dot && !strcasecmp(dot, ".pdf")) {
        int page = qparam(q, "page", v, sizeof v) ? atoi(v) : 0, width = qparam(q, "w", v, sizeof v) ? atoi(v) : 1280, pages = 0, w = 0, h = 0;
        char image[PATH_MAX];
        static unsigned serial;   /* each picture a new URL, since the image cache keys on it */
        if (download_store_pdf_page(path, page, width, &pages, image, sizeof image, &w, &h)) {
            sb_printf(&b, ",\"pdf\":{\"pages\":%d,\"page\":%d,\"w\":%d,\"h\":%d,\"img\":", pages, LMAX(0, LMIN(page, pages - 1)), w, h);
            SB u; sb_init(&u); sb_puts(&u, "file://");
            for (const unsigned char *c = (const unsigned char *)image; *c; c++)
                if (isalnum(*c) || strchr("/-._~", *c)) sb_printf(&u, "%c", *c); else sb_printf(&u, "%%%02X", *c);
            sb_printf(&u, "?v=%u", ++serial);
            sb_json(&b, u.s); sb_free(&u); sb_puts(&b, "}");
        }
    } else if (media_probe_file(path, probe, sizeof probe)) sb_printf(&b, ",\"probe\":{%s}", probe);
    sb_puts(&b, "}");
    return b.s;
}
/* LUMEN_TESTDRIVER (WPT runs only): WPT's testdriver drives real input through "td?OPS", one op per line in CSS
   viewport pixels: "move X Y", "down B", "up B" (WebDriver buttons), "key CP", "keyup CP" (WebDriver key code
   points). The ops become SDL events behind the queue, then an EV_TD event with the returned id calls
   __lumenTdDone(id) once they have been handled. */
static SDL_Keymod g_td_mod;
static SDL_Keycode td_keycode(uint32_t cp, SDL_Keymod *mod) {
    switch (cp) {
    case 0xE003: return SDLK_BACKSPACE; case 0xE004: return SDLK_TAB; case 0xE006: case 0xE007: return SDLK_RETURN;
    case 0xE00C: return SDLK_ESCAPE; case 0xE00D: return SDLK_SPACE; case 0xE00E: return SDLK_PAGEUP; case 0xE00F: return SDLK_PAGEDOWN;
    case 0xE010: return SDLK_END; case 0xE011: return SDLK_HOME; case 0xE012: return SDLK_LEFT; case 0xE013: return SDLK_UP;
    case 0xE014: return SDLK_RIGHT; case 0xE015: return SDLK_DOWN; case 0xE017: return SDLK_DELETE;
    case 0xE008: case 0xE050: *mod = SDL_KMOD_LSHIFT; return SDLK_LSHIFT;
    case 0xE009: case 0xE051: *mod = SDL_KMOD_LCTRL; return SDLK_LCTRL;
    case 0xE00A: case 0xE052: *mod = SDL_KMOD_LALT; return SDLK_LALT;
    case 0xE03D: case 0xE053: *mod = SDL_KMOD_LGUI; return SDLK_LGUI;
    default: break;
    }
    if (cp >= 0xE031 && cp <= 0xE03C) return SDLK_F1 + (cp - 0xE031);
    if (cp >= 'A' && cp <= 'Z') { *mod = SDL_KMOD_LSHIFT; return cp + 32; }
    return cp >= 0xE000 && cp <= 0xF8FF ? SDLK_UNKNOWN : cp;
}
static char *td_cmd(App *a, const char *q) {
    static float x, y; static Uint32 buttons; static double down_t; static int clicks; static int id;
    if (!strcmp(q, "ping")) return xstrdup("pong");
    float z = tab_zoom(a->t), s = a->input_scale > 0 ? a->input_scale : 1;
    for (const char *l = q; *l; ) {
        const char *e = strchr(l, '\n'); size_t n = e ? (size_t)(e - l) : strlen(l);
        char op[16] = ""; double v1 = 0, v2 = 0; char line[128];
        snprintf(line, sizeof line, "%.*s", (int)(n < sizeof line ? n : sizeof line - 1), l);
        int got = sscanf(line, "%15s %lf %lf", op, &v1, &v2);
        SDL_Event ev; SDL_zero(ev);
        Uint8 btn = v1 == 1 ? SDL_BUTTON_MIDDLE : v1 == 2 ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        if (!strcmp(op, "cookies-clear")) cookies_clear();
        else if (got == 3 && !strcmp(op, "move")) {
            x = (page_left(a) + (float)v1 * z) / s; y = (BAR + (float)v2 * z) / s;
            ev.type = SDL_EVENT_MOUSE_MOTION; ev.motion.x = x; ev.motion.y = y; ev.motion.state = buttons; SDL_PushEvent(&ev);
        } else if (got >= 1 && (!strcmp(op, "down") || !strcmp(op, "up"))) {
            bool down = op[0] == 'd';
            if (down) { double t = now_ms(); clicks = t - down_t < 500 ? clicks + 1 : 1; down_t = t; buttons |= SDL_BUTTON_MASK(btn); }
            else buttons &= ~SDL_BUTTON_MASK(btn);
            ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.button = btn; ev.button.down = down; ev.button.clicks = (Uint8)clicks; ev.button.x = x; ev.button.y = y;
            SDL_PushEvent(&ev);
        } else if (got >= 2 && (!strcmp(op, "key") || !strcmp(op, "keyup"))) {
            uint32_t cp = (uint32_t)v1; SDL_Keymod mod = 0; SDL_Keycode k = td_keycode(cp, &mod);
            bool modifier = k == SDLK_LSHIFT || k == SDLK_LCTRL || k == SDLK_LALT || k == SDLK_LGUI;
            if (modifier) { if (op[3]) g_td_mod &= ~mod; else g_td_mod |= mod; SDL_SetModState(g_td_mod); }
            if (k != SDLK_UNKNOWN && !(op[3] && !modifier)) {
                ev.type = op[3] ? SDL_EVENT_KEY_UP : SDL_EVENT_KEY_DOWN; ev.key.key = k; ev.key.down = !op[3]; ev.key.mod = g_td_mod | mod;
                /* the browser's own shortcuts stay out of tests: tab, window and history keys go to the page without Ctrl */
                if ((ev.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && !modifier && (k == SDLK_Q || k == SDLK_W || k == SDLK_T || k == SDLK_N || k == SDLK_L || k == SDLK_R || k == SDLK_TAB ||
                    k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET || (k >= SDLK_0 && k <= SDLK_9) || k == SDLK_EQUALS || k == SDLK_MINUS || k == SDLK_PLUS))
                    ev.key.mod &= ~(SDL_KMOD_CTRL | SDL_KMOD_GUI);
                SDL_PushEvent(&ev);
            }
            if (!op[3] && cp >= 32 && cp != 127 && !(cp >= 0xE000 && cp <= 0xF8FF) && !(g_td_mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT))) {
                static char *texts[64]; static int ti;   /* the text outlives the queued event; reused 64 events later */
                char *t = xmalloc(5); size_t tn = 0;
                if (cp < 0x80) t[tn++] = (char)cp;
                else if (cp < 0x800) { t[tn++] = (char)(0xC0 | cp >> 6); t[tn++] = (char)(0x80 | (cp & 63)); }
                else if (cp < 0x10000) { t[tn++] = (char)(0xE0 | cp >> 12); t[tn++] = (char)(0x80 | (cp >> 6 & 63)); t[tn++] = (char)(0x80 | (cp & 63)); }
                else { t[tn++] = (char)(0xF0 | cp >> 18); t[tn++] = (char)(0x80 | (cp >> 12 & 63)); t[tn++] = (char)(0x80 | (cp >> 6 & 63)); t[tn++] = (char)(0x80 | (cp & 63)); }
                t[tn] = 0; free(texts[ti]); texts[ti] = t; ti = (ti + 1) % 64;
                SDL_Event te; SDL_zero(te); te.type = SDL_EVENT_TEXT_INPUT; te.text.text = t; SDL_PushEvent(&te);
            }
        }
        l += n; if (*l) l++;
    }
    SDL_Event done; SDL_zero(done); done.type = EV_TD; done.user.code = ++id; SDL_PushEvent(&done);
    char b[16]; snprintf(b, sizeof b, "%d", id); return xstrdup(b);
}

/* lumenInternal(cmd) of Lumen's own pages: "state?s=SEC[&dir=PATH]", or "set?s=SEC&key=value..."
   which applies the settings and actions first; both return the section's state as JSON */
static char *h_internal_in(const char *cmd) {
    App *a = g_app;
    if (env_hook(ENV_TESTDRIVER) && !strncmp(cmd, "td?", 3)) return a->t ? td_cmd(a, cmd + 3) : NULL;
    if (!a->t || !a->t->cur || !a->t->cur->internal_ui) return NULL;
    if (!strcmp(cmd, "ping")) return xstrdup("pong");
    const char *q;
    char v[16];
    if (!strncmp(cmd, "view?", 5)) return files_view(cmd + 5);
    if (!strncmp(cmd, "state?", 6)) q = cmd + 6;
    else if (!strncmp(cmd, "set?", 4)) {
        q = cmd + 4;
        /* these recreate the window or resize the page, so they stay links (lumen://set?) */
        if (qparam(q, "renderer", v, sizeof v) || qparam(q, "pixel_scale", v, sizeof v)) return NULL;
        apply_set(a, q);
        if (qparam(q, "files_grant", v, sizeof v)) download_store_request_all_files();
        char *open = qparam_dup(q, "files_open"), path[PATH_MAX]; struct stat st;
        if (open && files_resolve(open, path) && !stat(path, &st) && S_ISREG(st.st_mode)) download_store_open_path(path);
        free(open);
        char *chosen = g_pick.wait ? qparam_dup(q, "pick_choose") : NULL;   /* the file chooser's files, one per line */
        if (chosen) {
            char *paths[64]; int n = 0;
            for (char *sv = NULL, *line = strtok_r(chosen, "\n", &sv); line && n < 64; line = strtok_r(NULL, "\n", &sv)) {
                char path[PATH_MAX]; struct stat st;
                if (files_resolve(line, path) && !stat(path, &st) && S_ISREG(st.st_mode)) paths[n++] = xstrdup(path);
            }
            if (n) { g_pick.wait = false; js_files_picked((const char *const *)paths, g_pick.multi ? n : 1); a->pan_close = true; }
            for (int i = 0; i < n; i++) free(paths[i]);
            free(chosen);
        }
#ifdef __ANDROID__
        if (g_pick.wait && qparam(q, "pick_system", v, sizeof v)) { g_pick.wait = false; a->pan_close = true; android_ui_pick_files(g_pick.accept, g_pick.multi); }   /* the system's picker answers instead */
#endif
        if (qparam(q, "pick_cancel", v, sizeof v)) a->pan_close = true;   /* panel_close tells the page */
        if (qparam(q, "bm_add", v, sizeof v)) g_newtab_msg = bm_add(bar_tab(a));   /* the page under the panel */
        if (qparam(q, "bm_save", v, sizeof v)) {   /* an edit of the Bookmarks page: index, bm_t title, bm_u URL */
            int i = atoi(v);
            char *title = qparam_dup(q, "bm_t"), *url = qparam_dup(q, "bm_u");
            if (i >= 0 && i < g_nbm) g_newtab_msg = bm_store(i, title, url);
            if (!g_newtab_msg && i >= 0 && i < g_nbm) g_newtab_msg = "A bookmark needs a URL";
            free(title); free(url);
        }
        if (qparam(q, "ws_rename", v, sizeof v)) {   /* Home's workspace menu: index, ws_name the new name */
            int i = atoi(v);
            char *name = qparam_dup(q, "ws_name");
            if (i >= 0 && i < a->nws && name && *name) snprintf(a->wsname[i], sizeof a->wsname[0], "%s", name);
            free(name);
        }
#ifdef __ANDROID__
        if (qparam(q, "ws_dialog", v, sizeof v)) show_workspaces(a);   /* rename one */
        if (qparam(q, "fullscreen", v, sizeof v) && (atoi(v) != 0) != g_bare) bare_set(a, atoi(v) != 0);   /* the Downloads viewer's */
        int run = qparam(q, "bmrun", v, sizeof v) ? atoi(v) : -1;   /* the panel's JavaScript bookmark, for the page it covers */
        if (panel_shown(a) && run >= 0 && run < g_nbm && is_js_url(g_bms[run].url)) { free(a->pan_go); a->pan_go = xstrdup(g_bms[run].url); }
#endif
    } else return NULL;
    return newtab_state(a, q);
}
static void update_size(App *app);
#ifdef __ANDROID__
static void renderer_apply(App *app, GpuBackend backend) {
    android_touch_reset(&app->touch);
    android_fling_reset(&app->fling);
    g_sm_dx = g_sm_dy = 0;
    gpu_destroy(app->gpu); app->gpu = NULL;
    SDL_DestroyWindowSurface(app->win);
    int width = 0, height = 0;
    SDL_GetWindowSize(app->win, &width, &height);
    SDL_DestroyWindow(app->win);
    app->win = g_dialog_win = NULL;
    g_renderer = backend;
    app->vonly = app->sdirty = app->gvid_ok = app->osig_ok = false;
    app->dirty = true;
    media_yuv = false;
    app->win = SDL_CreateWindow("Lumen", width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app->win) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Lumen: unable to recreate the graphics window: %s", SDL_GetError());
        SDL_Event quit = {0}; quit.type = SDL_EVENT_QUIT; SDL_PushEvent(&quit);
        return;
    }
    g_dialog_win = app->win;
    update_size(app);
    GpuSurfSrc source = { .kind = GPU_SURF_ANDROID,
        .a = SDL_GetPointerProperty(SDL_GetWindowProperties(app->win), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL) };
    if (source.a) app->gpu = gpu_create_backend(&source, app->pw, app->ph, g_renderer);
    media_yuv = gpu_yuv_ok(app->gpu) && !getenv("LUMEN_NO_YUV");
    SDL_Log("Lumen: requested renderer %s, active %s", renderer_key(g_renderer), gpu_backend_name(app->gpu));
}
#endif
static void apply_set(App *a, const char *q) {
    char v[32];
    if (qparam(q, "page_zoom", v, sizeof v)) {
        int percent = page_zoom_parse(v);
        if (percent && percent != g_page_zoom_pct) { g_page_zoom_pct = percent; page_zoom_changed(a); }
    }
#ifdef __ANDROID__
    if (qparam(q, "renderer", v, sizeof v)) {
        int backend = renderer_parse(v);
        if (backend >= 0 && (backend != (int)g_renderer || !a->gpu)) renderer_apply(a, (GpuBackend)backend);
    }
    if (qparam(q, "pixel_scale", v, sizeof v)) {
        int percent = pixel_scale_parse(v);
        if (percent && percent != g_pixel_scale_pct) {
            g_pixel_scale_pct = percent;
            android_touch_reset(&a->touch);
            android_fling_reset(&a->fling);
            g_sm_dx = g_sm_dy = 0;
            update_size(a);
        }
    }
#endif
    if (qparam(q, "lite", v, sizeof v)) { g_lowmem = atoi(v) != 0; media_lowmem = g_lowmem; }
    if (qparam(q, "cpu", v, sizeof v)) g_cpu_on = atoi(v) != 0;
    if (qparam(q, "vctl", v, sizeof v)) g_vctl = atoi(v) != 0;
    if (qparam(q, "sleep", v, sizeof v)) g_tab_sleep = atoi(v) != 0;
    if (qparam(q, "smooth", v, sizeof v)) { g_smooth = atoi(v) != 0; g_sm_dx = g_sm_dy = 0; }
    if (qparam(q, "tapkbd", v, sizeof v)) g_tap_kbd = atoi(v) != 0;
#ifdef __ANDROID__
    for (int k = 0; k < 2; k++) {
        char key[16]; snprintf(key, sizeof key, "edit_slot%d", k);
        if (!qparam(q, key, v, sizeof v)) continue;
        int x = atoi(v);
        if (x != TA_BKSP && x != TA_ENTER && x != TA_UNDO && x != TA_CLEAR && x != TA_HIDE) continue;
        if (g_bar_slot[!k] == x) g_bar_slot[!k] = g_bar_slot[k];   /* the two slots swap rather than repeat */
        g_bar_slot[k] = x;
    }
    { char sn[4096];   /* URL-encoded snip_encode text */
      if (qparam(q, "snippets", sn, sizeof sn)) {
          size_t o = 0; unsigned x;
          for (size_t i = 0; sn[i]; i++) sn[o++] = sn[i] == '%' && sscanf(sn + i + 1, "%2x", &x) == 1 ? (i += 2, (char)x) : sn[i] == '+' ? ' ' : sn[i];
          sn[o] = 0; snip_decode(sn);
      } }
#endif
    if (qparam(q, "hidetabs", v, sizeof v)) g_hide_tabs = atoi(v) != 0;
    if (qparam(q, "scroll_px", v, sizeof v)) g_scroll_px = LCLAMP((atoi(v) + 5) / 10 * 10, 10, scroll_px_max(a));
    if (qparam(q, "hud", v, sizeof v)) { g_hud = atoi(v) != 0; g_hud_pop = false; }
    if (qparam(q, "touchfb", v, sizeof v)) g_touch_fb = atoi(v) != 0;
#ifdef __ANDROID__
    if (qparam(q, "touchfb_color", v, sizeof v)) g_touch_fb_rgb = (int)(strtol(v, NULL, 10) & 0xffffff);
    if (qparam(q, "touchfb_hex", v, sizeof v)) { char *e; long c = strtol(v, &e, 16); if (e != v && !*e && strlen(v) == 6) g_touch_fb_rgb = (int)c; }
    if (qparam(q, "touchfb_alpha", v, sizeof v)) g_touch_fb_alpha = LCLAMP(atoi(v), 1, 100);
    if (qparam(q, "touchfb_size", v, sizeof v)) g_touch_fb_size = LCLAMP(atoi(v), 4, 120);
    if (qparam(q, "touchfb_ms", v, sizeof v)) g_touch_fb_ms = LCLAMP(atoi(v), 0, 5000);
#endif
    if (qparam(q, "ua", v, sizeof v)) { g_ua = LCLAMP(atoi(v), 0, UA_COUNT - 1); net_set_user_agent(g_ua); }
    if (qparam(q, "cpu_pct", v, sizeof v)) g_cpu_pct = LCLAMP(atoi(v), 10, 100);
    if (qparam(q, "cpu_secs", v, sizeof v)) g_cpu_secs = LCLAMP(atoi(v), 5, 600);
    if (qparam(q, "cpu_lim", v, sizeof v)) g_cpu_lim = LCLAMP(atoi(v), 5, 95);
    for (int i = 0; i < a->ntabs; i++) { Tab *t = a->tabs[i]; t->ncpu = t->cpui = 0; if (!g_cpu_on && !t->cpumode && t->limited) tab_unlimit(t); }
    if (qparam(q, "unbm", v, sizeof v)) { int i = atoi(v); if (i >= 0 && i < g_nbm) { link_del(g_bms, &g_nbm, i); links_save("bookmarks.txt", g_bms, g_nbm); } }
    if (qparam(q, "dl_pause", v, sizeof v)) dl_pause(strtoull(v, NULL, 10));
    if (qparam(q, "dl_resume", v, sizeof v)) dl_resume(strtoull(v, NULL, 10));
    if (qparam(q, "dl_cancel", v, sizeof v)) dl_cancel(strtoull(v, NULL, 10));
    if (qparam(q, "dl_open", v, sizeof v)) dl_open(strtoull(v, NULL, 10));
    if (qparam(q, "dl_forget", v, sizeof v)) dl_forget(strtoull(v, NULL, 10));
    if (qparam(q, "dl_clear", v, sizeof v)) { DlInfo d[100]; int n = dl_list(d, 100); for (int i = 0; i < n; i++) if (d[i].state != DL_RUNNING && d[i].state != DL_PAUSED) dl_forget(d[i].id); }
    if (qparam(q, "clearhist", v, sizeof v)) { while (g_nhv) link_del(g_hv, &g_nhv, g_nhv - 1); links_save("history.txt", g_hv, 0); }
    settings_save();
}

static bool is_js_url(const char *u);
static void navigate_ex(App *a, const char *url, bool push, const char *body, size_t blen, const char *ctype) {
    if (is_js_url(url)) return;   /* page scripts run their own; the address bar and bookmarks use run_js_url */
    if (panel_shown(a) && strncmp(url, "lumen://newtab", 14) && strncmp(url, "lumen://set?", 12)) {   /* for the covered tab, once the panel closes */
        free(a->pan_go); a->pan_go = xstrdup(url); return;
    }
    char *u = normalize_url(url), *html = NULL;
    if (!strncmp(u, "lumen://", 8)) {
        if (!strncmp(u, "lumen://set?", 12)) {   /* only Lumen's own pages may change settings */
            if (!a->t->cur || !a->t->cur->internal_ui) { free(u); return; }
            apply_set(a, u + 12);
            char sec[16] = "settings", open[48] = ""; qparam(u + 12, "s", sec, sizeof sec);
            if (qparam(u + 12, "open", open, sizeof open) && strspn(open, "abcdefghijklmnopqrstuvwxyz,") != strlen(open)) open[0] = 0;   /* the settings groups left open */
            free(u); u = xmalloc(128); snprintf(u, 128, "lumen://newtab?s=%s%s%s", sec, *open ? "&open=" : "", open); push = false;
        }
        html = internal_page(a, u);
    }
    if (push) {
        for (int i = a->t->hpos + 1; i < a->t->nhist; i++) free(a->t->hist[i]);
        a->t->nhist = a->t->hpos + 1;
        if (a->t->nhist == 256) { free(a->t->hist[0]); memmove(a->t->hist, a->t->hist + 1, sizeof(char *) * 255); memmove(a->t->hgen, a->t->hgen + 1, sizeof(uint64_t) * 255); a->t->nhist--; }
        a->t->hist[a->t->nhist++] = xstrdup(u); a->t->hpos = a->t->nhist - 1;
    }
    snprintf(a->t->url, sizeof a->t->url, "%s", u);
    a->editing = false; a->t->loading = true; a->dirty = true;
    LoadReq *rq = xcalloc(1, sizeof *rq); rq->url = u; rq->html = html; rq->gen = ++load_gen; a->t->desktop = !html && url_desktop(u); float zoom = tab_zoom(a->t); rq->vw = (a->vw - page_left(a)) / zoom; rq->vh = (a->vh - BAR) / zoom; rq->dpr = a->scale * zoom; a->t->lgen = rq->gen; publish_gens(a);
    if (push) a->t->hgen[a->t->hpos] = rq->gen;
    if (!html && a->t->cur && !a->t->cur->internal_ui && !strncmp(a->t->cur->url, "http", 4)) rq->ref = xstrdup(a->t->cur->url);
    if (body) { rq->body = xmalloc(blen + 1); memcpy(rq->body, body, blen); rq->body[blen] = 0; rq->blen = blen; rq->ctype = xstrdup(ctype ? ctype : ""); }
    if (!html) { net_trace_load(120); g_load_trace_until = now_ms() + 120000; }
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
#ifndef __ANDROID__   /* the address field's text; Android's is BrowserToolbar's */
/* the glyphs of s that fall in [skip, skip + w) of its run, drawn from x */
static void push_text_window(DisplayList *dl, Font *f, const char *s, float x, float base, float skip, float w, Color c) {
    ShapedRun sr; memset(&sr, 0, sizeof sr); text_shape(f, s, strlen(s), 0, &sr);
    DItem it; memset(&it, 0, sizeof it); it.op = DO_TEXT; it.color = c;
    it.g = arena_alloc(&dl->arena, sizeof(DGlyph) * (size_t)(sr.n + 1));
    float x1 = 0;
    for (int i = 0; i < sr.n; i++) {
        float gx = sr.g[i].x - skip;
        if (gx < -0.5f) continue;
        if (gx + sr.g[i].adv > w) break;
        DGlyph *g = &it.g[it.ng++]; g->gid = sr.g[i].gid; g->font = sr.g[i].font; g->x = x + gx; g->y = base + sr.g[i].y;
        x1 = gx + sr.g[i].adv;
    }
    it.x = x; it.y = base - f->ascent; it.w = x1; it.h = f->ascent + f->descent;
    vec_push(dl->items, it); shaped_free(&sr);
}
#endif
/* the byte offset of s nearest x px into its run */
static int text_hit_x(Font *f, const char *s, float x) {
    ShapedRun sr; memset(&sr, 0, sizeof sr); text_shape(f, s, strlen(s), 0, &sr);
    int off = (int)strlen(s);
    for (int i = 0; i < sr.n; i++) if (x < sr.g[i].x + sr.g[i].adv / 2) { off = (int)sr.g[i].cluster; break; }
    shaped_free(&sr);
    return off;
}
static void url_sel_clamp(App *a) { int n = (int)strlen(a->t->url); for (int k = 0; k < 2; k++) a->us[k] = LCLAMP(a->us[k], 0, n); }

enum { HB_NONE, HB_BACK, HB_FWD, HB_RELOAD, HB_URL, HB_NEWTAB, HB_WSNEW, HB_STAR, HB_HUD, HB_MENU, HB_TAB = 100, HB_TABX = 200, HB_WS = 300, HB_INFO = 400, HB_TABDOT = 500, HB_SLOT = 600 };
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
    if (ic < 0 || ic >= WORKSPACE_ICON_COUNT) return NULL;
    Image **slot = &a->icimg[ic][on];
    if (*slot && (*slot)->scale != a->scale) { image_unref(*slot); *slot = NULL; }
    if (!*slot) {
        int px = (int)(18 * a->scale + 0.5f); Image *im = xcalloc(1, sizeof *im);
        im->w = im->h = px; im->refs = 1; im->scale = a->scale; im->px = xcalloc((size_t)px * (size_t)px, 4);
        bool drawn = false;
#ifdef __APPLE__
        drawn = mac_icon_rgba(ic, px, on ? 0x141414 : 0x5a5e64, im->px);
#endif
        if (!drawn) drawn = workspace_icon_rgba(px, on ? 0x141414 : 0x5a5e64, im->px);
        if (!drawn) { free(im->px); free(im); return NULL; }
        *slot = im;
    }
    return *slot;
}
static int ws_tabs(App *a, int w, int *out) { int n = 0; for (int i = 0; i < a->ntabs; i++) if (a->tabs[i]->ws == w) out[n++] = i; return n; }
static float tab_w(App *a) { int v[MAX_TABS], n = ws_tabs(a, a->wi, v); float w = (a->vw - TLW - 48) / (n ? n : 1); return w > 220 ? 220 : w; }
static float bar_x0(App *a) { return g_compact ? TLW : a->side; }
/* Android toolbar: URL | back | reload | menu; elsewhere back, forward, reload and star sit left of the URL */
static float url_x(App *a) {
    (void)a;
#ifdef __ANDROID__
    return 10;
#else
    return 144;
#endif
}
static float url_w(App *a) {
#ifdef __ANDROID__
    return a->vw - bar_x0(a) - url_x(a) - 134;
#else
    return a->vw - bar_x0(a) - 144 - (g_compact ? 92 : 12);
#endif
}
#ifdef __ANDROID__
static int android_bar_hit(App *a, float x) {   /* the row is BrowserToolbar's: its touches never come here */
    (void)a; (void)x;
    return HB_NONE;
}
#endif
static int bar_hit(App *a, float x, float y) {
    if (g_compact && y < TB) {
        x -= TLW;
#ifdef __ANDROID__
        return android_bar_hit(a, x);
#endif
        if (x < 0) return HB_NONE;
        if (x < 40) return HB_BACK; if (x < 72) return HB_FWD; if (x < 104) return HB_RELOAD; if (x < 136) return HB_STAR;
        float ue = 144 + url_w(a);
        return x < 144 ? HB_NONE : x < ue ? HB_URL : x >= ue + 4 && x < ue +
#ifdef __ANDROID__
            48
#else
            32
#endif
            ? HB_NEWTAB : HB_NONE;
    }
    if (y < TABH) {
        int v[MAX_TABS], n = ws_tabs(a, a->wi, v), k; float tw = tab_w(a), x0 = TLW + 6; k = (int)((x - x0) / tw);
        if (x >= x0 && k < n) { float lx = x - x0 - k * tw; return lx > tw - 28 && !ANDROID_UI ? HB_TABX + v[k] : a->tabs[v[k]]->limited && lx < 26 ? HB_TABDOT + v[k] : HB_TAB + v[k]; }
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
#ifdef __ANDROID__
    return android_bar_hit(a, x);
#endif
    if (x < 40) return HB_BACK; if (x < 72) return HB_FWD; if (x < 104) return HB_RELOAD; if (x < 136) return HB_STAR;
    return HB_URL;
}

static void push_img(DisplayList *dl, Image *im, float x, float y, float w, float h) { DItem it; memset(&it, 0, sizeof it); it.op = DO_IMAGE; it.x = x; it.y = y; it.w = w; it.h = h; it.img = im; it.alpha = 1; vec_push(dl->items, it); }
static Image *sym_icon(App *a, int k, int c) {   /* k: 0 back 1 forward 2 reload 3 star 4 star.fill 5 play 6 pause 7 plus; c: 0 dark 1 grey 2 blue 3 white */
    if (k < 0 || k >= 8 || c < 0 || c >= 4) return NULL;
#ifdef __APPLE__
    static const char *N[8] = { "chevron.left", "chevron.right", "arrow.clockwise", "star", "star.fill", "play.fill", "pause.fill", "plus" };
#else
    if (k != 5 && k != 6) return NULL;
#endif
    static const uint32_t C[4] = { 0x3c4043, 0xbdc1c6, 0x1a73e8, 0xffffff };
    Image **slot = &a->sym[k][c];
    if (*slot && (*slot)->scale != a->scale) { image_unref(*slot); *slot = NULL; }
    if (!*slot) {
        int px = (int)(16 * a->scale + 0.5f); Image *im = xcalloc(1, sizeof *im);
        im->w = im->h = px; im->refs = 1; im->scale = a->scale; im->px = xcalloc((size_t)px * (size_t)px, 4);
        bool drawn = false;
#ifdef __APPLE__
        drawn = mac_symbol_rgba(N[k], px, C[c], im->px);
#endif
        if (!drawn && (k == 5 || k == 6)) drawn = media_icon_rgba(k == 5, px, C[c], im->px);
        if (!drawn) { free(im->px); free(im); return NULL; }
        *slot = im;
    }
    return *slot;
}
#ifndef __ANDROID__   /* Android: BrowserToolbar draws android_ui_icon's outlines */
static void draw_icon(DisplayList *dl, int kind, float cx, float cy, Color c) {
    if (kind == HB_BACK || kind == HB_FWD) {
        float d = kind == HB_BACK ? -1 : 1;
        for (int i = 0; i < 6; i++) { push_rect(dl, cx - d * (i * 0.9f) - 1, cy - i - 1, 2, 2, 1, c); push_rect(dl, cx - d * (i * 0.9f) - 1, cy + i - 1, 2, 2, 1, c); }
    } else {
        for (int i = 0; i < 28; i++) { float t = (float)i / 28 * 5.4f + 0.6f; push_rect(dl, cx + 6 * sinf(t) - 1, cy - 6 * cosf(t) - 1, 2, 2, 1, c); }
        push_rect(dl, cx - 1, cy - 8, 4, 2, 1, c); push_rect(dl, cx + 1, cy - 9, 2, 4, 1, c);
    }
}
#endif

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
#ifdef __ANDROID__
/* the URL field's touch menu, under the field: Copy | Paste | Share [| Select all] */
enum { UM_COPY, UM_PASTE, UM_SHARE, UM_ALL };
static float g_umenu[4], g_umenu_end[4]; static int g_umenu_n, g_umenu_act[4];
static void url_menu_build(App *a, DisplayList *dl) {
    g_umenu_n = 0;
    if (!a->editing || !a->umenu) return;
    static const char *label[4] = { "Copy", "Paste", "Share", "Select all" };
    url_sel_clamp(a);
    bool all = LMIN(a->us[0], a->us[1]) == 0 && LMAX(a->us[0], a->us[1]) == (int)strlen(a->t->url);
    int n = 0; g_umenu_act[n++] = UM_COPY; g_umenu_act[n++] = UM_PASTE; g_umenu_act[n++] = UM_SHARE;
    if (!all) g_umenu_act[n++] = UM_ALL;
    Font *f = a->ui; float pad = 16, h = 40, w = 0, bw[4];
    for (int i = 0; i < n; i++) { bw[i] = text_width(f, label[g_umenu_act[i]], strlen(label[g_umenu_act[i]]), 0) + 2 * pad; w += bw[i]; }
    float x = LCLAMP(url_x(a), 4, LMAX(4, a->vw - 4 - w)), y = TABH + TB + 4, base = y + h / 2 + (f->ascent - f->descent) / 2;
    push_rect(dl, x - 1, y, w + 2, h + 2, 9, RGBA(0, 0, 0, 38));
    push_rect(dl, x, y, w, h, 8, RGBA(255, 255, 255, 255));
    for (int i = 0, bx = 0; i < n; bx += (int)bw[i], i++) {
        if (i) push_rect(dl, x + bx, y + 10, 1, h - 20, 0, RGBA(218, 220, 224, 255));
        push_text(dl, f, label[g_umenu_act[i]], x + bx + pad, base, bw[i], RGBA(32, 33, 36, 255));
        g_umenu_end[i] = x + bx + bw[i];
    }
    g_umenu[0] = x; g_umenu[1] = y; g_umenu[2] = x + w; g_umenu[3] = y + h; g_umenu_n = n;
    g_pop[0] = x - 1; g_pop[1] = y; g_pop[2] = x + w + 1; g_pop[3] = y + h + 2;
}
static int url_menu_hit(float x, float y) {
    if (!g_umenu_n || x < g_umenu[0] || x >= g_umenu[2] || y < g_umenu[1] || y >= g_umenu[3]) return -1;
    for (int i = 0; i < g_umenu_n; i++) if (x < g_umenu_end[i]) return i;
    return g_umenu_n - 1;
}
#endif
static void build_hud_pop(App *a) {
    DisplayList *dl = &a->hdl; dl_clear(dl);
#ifdef __ANDROID__
    url_menu_build(a, dl);
#endif
    if (!g_hud || !g_hud_pop || a->side <= 0) return;
    float w = 150, h = 62, x = 8, y = a->vh - HUDH - h - 2, rb = (a->ui->ascent - a->ui->descent) / 2;
    if (dl->items.n) { g_pop[0] = LMIN(g_pop[0], x); g_pop[1] = LMIN(g_pop[1], y); g_pop[2] = LMAX(g_pop[2], x + w); g_pop[3] = LMAX(g_pop[3], y + h); }
    else { g_pop[0] = x; g_pop[1] = y; g_pop[2] = x + w; g_pop[3] = y + h; }
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
    PlatformStats stats;
    if (!platform_stats(&stats)) return;
    uint64_t us = stats.cpu_us;
    if (lt && now > lt) g_hud_cpu = (float)((double)(us - lu) / ((now - lt) * 10.0));
    lt = now; lu = us;
    g_hud_ram = (float)(stats.rss_bytes / 1073741824.0);
    g_host_ram = (float)(stats.host_used_bytes / 1073741824.0);
    if (ht && stats.host_total_ticks > ht && stats.host_busy_ticks >= hb)
        g_host_cpu = (float)(100.0 * (double)(stats.host_busy_ticks - hb) / (double)(stats.host_total_ticks - ht));
    hb = stats.host_busy_ticks; ht = stats.host_total_ticks;
}
static void build_chrome(App *a) {
    build_hud_pop(a);
    DisplayList *dl = &a->cdl; dl_clear(dl);
    if (g_bare) return;
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
        const char *title = t->cur && t->cur->d && t->cur->d->title && *t->cur->d->title ? t->cur->d->title : t->stitle ? t->stitle : *t->url ? t->url : "New Tab";
        if (t->limited) push_rect(dl, x + 10, (4 + TABH) / 2 - 5, 10, 10, 5, RGBA(52, 199, 89, 255));
        push_text(dl, a->ui, title, x + (t->limited ? 26 : 12), tbase, tabw - (t->limited ? 54 : 40) + (ANDROID_UI ? 18 : 0), lite_on(t) ? RGBA(0, 100, 0, 255) : RGBA(40, 40, 40, 255));
        if (!ANDROID_UI) push_text(dl, a->ui, "\xC3\x97", x + tabw - 22, tbase, 16, a->hover == HB_TABX + i ? RGBA(20, 20, 20, 255) : RGBA(110, 110, 110, 255));
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
#ifndef __ANDROID__   /* Android: the toolbar row is BrowserToolbar, fed by bar_push */
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
    float ux = url_x(a), uw = url_w(a);
    push_rect(dl, ux, 7, uw, TB - 14, (TB - 14) / 2, a->editing ? RGBA(255, 255, 255, 255) : RGBA(238, 238, 238, 255));
    if (a->editing) {
        DItem it; memset(&it, 0, sizeof it); it.op = DO_BORDER; it.x = ux; it.y = 7; it.w = uw; it.h = TB - 14;
        for (int i = 0; i < 4; i++) { it.r[i] = (TB - 14) / 2; it.bw[i] = 1.5f; it.bc[i] = RGBA(66, 133, 244, 255); it.bs[i] = 1; }
        vec_push(dl->items, it);
    }
    const char *shown = a->t->url;
    if (!a->editing) { const char *p = strstr(shown, "://"); if (p) shown = p + 3; }
    float base = TB / 2 + (a->ui->ascent - a->ui->descent) / 2, room = uw - 32;
    if (a->editing) {   /* scroll the text so the caret stays in the field */
        url_sel_clamp(a);
        float cx = text_width(a->ui, shown, (size_t)a->us[1], 0), tw = text_width(a->ui, shown, strlen(shown), 0);
        if (cx - a->uscroll > room) a->uscroll = cx - room;
        if (cx < a->uscroll) a->uscroll = cx;
        a->uscroll = LCLAMP(a->uscroll, 0, LMAX(0, tw - room));
        int lo = LMIN(a->us[0], a->us[1]), hi = LMAX(a->us[0], a->us[1]);
        if (lo < hi) {
            float x0 = LMAX(0, text_width(a->ui, shown, (size_t)lo, 0) - a->uscroll), x1 = LMIN(room, text_width(a->ui, shown, (size_t)hi, 0) - a->uscroll);
            if (x1 > x0) push_rect(dl, ux + 16 + x0, 12, x1 - x0, TB - 24, 2, RGBA(200, 220, 255, 255));
        }
    } else a->uscroll = 0;
    push_text_window(dl, a->ui, shown, ux + 16, base, a->uscroll, room, a->editing ? RGBA(20, 20, 20, 255) : RGBA(90, 90, 90, 255));
    if (a->editing && a->us[0] == a->us[1]) push_rect(dl, ux + 16 + text_width(a->ui, shown, (size_t)a->us[1], 0) - a->uscroll, 13, 1.5f, TB - 26, 0, RGBA(20, 20, 20, 255));
#ifndef __ANDROID__
    if (g_compact) {
        float cx = ux + uw + 18;
        if (a->hover == HB_NEWTAB) push_rect(dl, cx - 13, TB / 2 - 13, 26, 26, 13, RGBA(232, 232, 232, 255));
        Image *si = sym_icon(a, 7, 0);
        if (si) push_img(dl, si, cx - 8, TB / 2 - 8, 16, 16); else { push_rect(dl, cx - 6, TB / 2 - 1, 12, 2, 1, on); push_rect(dl, cx - 1, TB / 2 - 6, 2, 12, 1, on); }
    }
#endif
    if (a->t->loading) push_rect(dl, 0, TB - 2, tbw * 0.35f, 2, 0, RGBA(66, 133, 244, 255));
#endif
    for (int i = n0; i < dl->items.n; i++) { DItem *it = &dl->items.v[i]; it->y += TABH; it->x += x0; if (it->op == DO_TEXT) for (int g = 0; g < it->ng; g++) { it->g[g].y += TABH; it->g[g].x += x0; } }
}

static float max_scroll(App *a) { return a->t->cur && a->t->cur->L ? LMAX(0, a->t->cur->L->doc_h - page_height(a)) : 0; }

static double g_tr, g_tl, g_td, g_tx;
static int g_tr_n, g_tr_slow;   /* style passes, and those >= 15 ms (debug load trace) */
static bool g_no_paint_only;
#ifdef __ANDROID__
static void bar_push(App *a);
/* the Downloads viewer's full screen: hides the system bars too, and puts them back as they were */
static void bare_set(App *a, bool on) {
    if (on == g_bare) return;
    g_bare = on;
    if (on) g_bare_fs = g_fullscreen;
    bool fs = on || g_bare_fs;
    if (fs != g_fullscreen) { g_fullscreen = fs; android_ui_fullscreen(fs); }
    a->hover = HB_NONE; a->vonly = false; a->dirty = true;
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
    bar_push(a);
}
#endif
static void page_fs_set(App *a, Page *p) {
    if (p == g_fs_page) return;
    bool was = g_fs_page != NULL;
    g_fs_page = p;
    if (!!p == was) return;
    if (p) { g_fs_side = a->side; a->side = 0; } else a->side = g_fs_side;
#ifdef __ANDROID__
    bare_set(a, p != NULL);
#else
    g_bare = p != NULL;
    SDL_SetWindowFullscreen(a->win, g_bare);
    a->vonly = false; a->dirty = true;
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
#endif
}
/* the browser takes fullscreen back: the page hears it, and its bars return even if it does not answer */
static void page_fs_exit(App *a) {
    if (!g_fs_page) return;
    js_fullscreen_exit();
    page_fs_set(a, NULL);
}
static void update_compact(App *a) {
    int v[MAX_TABS]; bool c = g_hide_tabs || ws_tabs(a, a->wi, v) <= 1;
    if (c == g_compact) return;
    g_compact = c; a->hover = HB_NONE; a->vonly = false;
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
}
/* per display item: content hash + device-independent bounds, so a DOM change only rasters the area whose items changed */
struct DSig { uint64_t h; float b[4]; };
static uint64_t hmix(uint64_t h, const void *p, size_t n) { const uint8_t *c = p; for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull; return h; }
#define HMIX(h, v) h = hmix(h, &(v), sizeof(v))
/* vertical positions in 1/1024 CSS px, so an item moved back by a page scroll hashes as before it */
static int64_t hq(float v) { return isfinite(v) ? (int64_t)llrintf(LCLAMP(v, -1e9f, 1e9f) * 1024) : INT64_MIN; }
static uint64_t ditem_hash(const DItem *it, float dy) {
    uint64_t h = 1469598103934665603ull; int64_t y = hq(it->y + dy);
    HMIX(h, it->op); HMIX(h, it->x); HMIX(h, y); HMIX(h, it->w); HMIX(h, it->h); HMIX(h, it->r); HMIX(h, it->color);
    HMIX(h, it->bw); HMIX(h, it->bc); HMIX(h, it->bs); HMIX(h, it->img); HMIX(h, it->blur); HMIX(h, it->spread); HMIX(h, it->alpha); HMIX(h, it->inset);
    if (it->img) HMIX(h, it->img->content_version);
    if (it->grad) { const Gradient *g = it->grad; HMIX(h, g->type); HMIX(h, g->angle); HMIX(h, g->nstops); HMIX(h, g->repeating); h = hmix(h, g->stops, sizeof g->stops[0] * (size_t)LCLAMP(g->nstops, 0, 8)); }
    for (int i = 0; i < it->ng; i++) { const DGlyph *g = &it->g[i]; int64_t gy = hq(g->y + dy); HMIX(h, g->gid); HMIX(h, g->x); HMIX(h, gy); HMIX(h, g->font); }
    return h;
}
static void bunion(float *a, const float *b) { a[0] = LMIN(a[0], b[0]); a[1] = LMIN(a[1], b[1]); a[2] = LMAX(a[2], b[2]); a[3] = LMAX(a[3], b[3]); }
static void ditem_box(const DItem *it, float *b) {
    float x0 = it->x, y0 = it->y, x1 = it->x + it->w, y1 = it->y + it->h, e = 2;
    if (it->op == DO_SHADOW) e += fabsf(it->spread) + fabsf(it->blur);
    if (it->op == DO_TEXT) {
        x0 = y0 = 1e30f; x1 = y1 = -1e30f;
        for (int i = 0; i < it->ng; i++) { const DGlyph *g = &it->g[i]; float em = g->font ? g->font->size : 16; x0 = LMIN(x0, g->x - em); x1 = LMAX(x1, g->x + 2 * em); y0 = LMIN(y0, g->y - 1.5f * em); y1 = LMAX(y1, g->y + em); }
    }
    if (it->op == DO_POP_CLIP || it->op == DO_POP_LAYER || it->op == DO_PUSH_LAYER || x1 <= x0 || y1 <= y0) { b[0] = b[1] = 1e30f; b[2] = b[3] = -1e30f; return; }
    b[0] = x0 - e; b[1] = y0 - e; b[2] = x1 + e; b[3] = y1 + e;
}
/* dy: CSS px added to the items that scroll with the page (not fixed ones) before hashing */
static int dl_sigs(const DisplayList *dl, struct DSig **v, int *cap, float dy) {
    int n = dl->items.n, st[64], ns = 0, depth = 0;
    uint64_t context[65]; context[0] = 1469598103934665603ull;   /* per open clip/layer depth: the hash of the innermost one */
    if (n > *cap) { *cap = n + n / 2 + 16; *v = xrealloc(*v, sizeof **v * (size_t)*cap); }
    for (int i = 0; i < n; i++) {
        const DItem *it = &dl->items.v[i]; struct DSig *q = &(*v)[i];
        /* pops and layers carry no position: moving them by the scroll would unmatch them and, through the
           context, every item inside an opacity layer */
        bool still = (it->fixed && !it->sticky) || it->op == DO_POP_CLIP || it->op == DO_PUSH_LAYER || it->op == DO_POP_LAYER;
        q->h = ditem_hash(it, still ? 0 : dy); ditem_box(it, q->b);
        HMIX(q->h, context[LMIN(depth, 64)]);   /* only the open clips and layers: items culled before this one do not change it */
        if (it->op == DO_PUSH_CLIP || it->op == DO_PUSH_LAYER) { if (++depth <= 64) context[depth] = q->h; }
        else if ((it->op == DO_POP_CLIP || it->op == DO_POP_LAYER) && depth) depth--;
        if (it->op == DO_PUSH_LAYER) { if (ns < 64) st[ns++] = i; }   /* a layer covers its contents: an opacity change damages all of them */
        else if (it->op == DO_POP_LAYER) { if (ns) { int t = st[--ns]; if (ns) bunion((*v)[st[ns - 1]].b, (*v)[t].b); } }
        else if (ns) bunion((*v)[st[ns - 1]].b, q->b);
    }
    return n;
}
static uint64_t dl_hash(const DisplayList *dl) { uint64_t h = (uint64_t)dl->items.n; for (int i = 0; i < dl->items.n; i++) h = h * 31 + ditem_hash(&dl->items.v[i], 0); return h; }

static PaintProfile g_paint_profile;
/* paint profile: frames per render path, then why the scroller blit was not taken */
enum { PP_BLIT, PP_BDMG, PP_SBLIT, PP_SDIRTY, PP_SKIP, PP_IDLE, PP_RECT, PP_DMG, PP_FULL, PP_NOSBOX, PP_SB_UI, PP_SB_CLIP, PP_SB_X, PP_SB_D, PP_SB_AREA, PP_N };
static unsigned g_pp[PP_N], g_pp_why[6], g_pp_noblit[3], g_wake[4], g_sdmg_no[4];   /* g_sdmg_no: scroll+DOM frames not shifted: shift refused, damage too big, video, not eligible (signatures, loading) */
static unsigned g_pp_unused_ __attribute__((unused));   /* g_wake: frames asked for by the caret blink, image animations, video bars, defer expiry */   /* g_pp_noblit: page scrolls not shifted: sub-pixel/overlay/dirty scroller, fixed boxes, video */
static long g_sb_big[6], g_sb_dmg; static unsigned g_sb_rects;   /* scroller blit damage: largest unmoved item (area op w h mv fixed), total px, rects */   /* g_pp_why: frames that neither blitted nor skipped, by render()'s why */
#define SB_FAIL(k) do { g_pp[k]++; return false; } while (0)

static void paint_profile_configure(void) {
    const char *value = getenv("LUMEN_DEBUG_PAINT");
    g_paint_profile.enabled = value && !strcmp(value, "1");
#if defined(__ANDROID__) && !defined(NDEBUG)
    if (!g_paint_profile.enabled && *g_pref) {
        char path[1200]; pref_path(path, sizeof path, "paint-profile.enabled");
        FILE *marker = fopen(path, "r");
        if (marker) {
            int first = fgetc(marker), second = fgetc(marker);
            g_paint_profile.enabled = first == '1' && second == EOF && !ferror(marker);
            fclose(marker);
        }
    }
#endif
}

static double paint_profile_cpu_ms(void) {
#ifdef __ANDROID__
    struct timespec usage;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &usage) == 0 && usage.tv_sec >= 0 &&
        usage.tv_nsec >= 0 && usage.tv_nsec < 1000000000)
        return (double)usage.tv_sec * 1000.0 + (double)usage.tv_nsec / 1000000.0;
#endif
    return -1;
}

static PaintFrame paint_profile_begin(double start_ms) {
    PaintFrame frame = { .last_ms = start_ms };
    if (g_paint_profile.enabled) {
        frame.cpu_last_ms = paint_profile_cpu_ms();
        frame.cpu_valid = frame.cpu_last_ms >= 0;
    }
    return frame;
}

static void paint_profile_stage(PaintFrame *frame, PaintPhase phase) {
    if (!g_paint_profile.enabled) return;
    double stamp = now_ms();
    frame->phase_ms[phase] += stamp - frame->last_ms;
    frame->last_ms = stamp;
    if (frame->cpu_valid) {
        double cpu_stamp = paint_profile_cpu_ms();
        if (cpu_stamp < frame->cpu_last_ms) frame->cpu_valid = false;
        else {
            frame->cpu_phase_ms[phase] += cpu_stamp - frame->cpu_last_ms;
            frame->cpu_last_ms = cpu_stamp;
        }
    }
}

static void paint_profile_report(const PaintFrame *frame, double start_ms, bool partial) {
    if (!paint_profile_record(&g_paint_profile, frame, start_ms, partial)) return;
    char message[1024];
    paint_profile_format(&g_paint_profile, frame->last_ms, message, sizeof message);
#ifdef __ANDROID__
    SDL_Log("%s", message);
#else
    fprintf(stderr, "%s\n", message);
#endif
    paint_profile_cpu_format(&g_paint_profile, frame->last_ms, message, sizeof message);
#ifdef __ANDROID__
    SDL_Log("%s", message);
#else
    fprintf(stderr, "%s\n", message);
#endif
    snprintf(message, sizeof message, "lumen: paint paths blit=%u bdmg=%u (no %u/%u/%u/%u) sblit=%u sdirty=%u skip=%u idle=%u rect=%u dmg=%u full=%u sb_fail nosbox=%u ui=%u clip=%u x=%u d=%u area=%u why=%u/%u/%u/%u/%u/%u raster calls=%u kpx=%llu layers=%u layer_kpx=%llu sb_dmg kpx=%ld rects=%u big=%ld/op%ld/%ldx%ld/mv%ld/f%ld",
             g_pp[PP_BLIT], g_pp[PP_BDMG], g_sdmg_no[0], g_sdmg_no[1], g_sdmg_no[2], g_sdmg_no[3], g_pp[PP_SBLIT], g_pp[PP_SDIRTY], g_pp[PP_SKIP], g_pp[PP_IDLE], g_pp[PP_RECT], g_pp[PP_DMG], g_pp[PP_FULL], g_pp[PP_NOSBOX], g_pp[PP_SB_UI], g_pp[PP_SB_CLIP], g_pp[PP_SB_X], g_pp[PP_SB_D], g_pp[PP_SB_AREA], g_pp_why[0], g_pp_why[1], g_pp_why[2], g_pp_why[3], g_pp_why[4], g_pp_why[5], g_raster_calls, g_raster_px / 1000, g_layer_n, g_layer_px / 1000,
             g_sb_dmg / 1000, g_sb_rects, g_sb_big[0], g_sb_big[1], g_sb_big[2], g_sb_big[3], g_sb_big[4], g_sb_big[5]);
#ifdef __ANDROID__
    SDL_Log("%s", message);
#else
    fprintf(stderr, "%s\n", message);
#endif
    memset(g_pp, 0, sizeof g_pp); memset(g_pp_why, 0, sizeof g_pp_why); memset(g_sdmg_no, 0, sizeof g_sdmg_no); g_raster_calls = g_layer_n = 0; g_raster_px = g_layer_px = 0; memset(g_sb_big, 0, sizeof g_sb_big); g_sb_dmg = 0; g_sb_rects = 0;
    paint_profile_reset(&g_paint_profile, frame->last_ms);
}

static void compose_page(App *app, int first_row, int last_row, int toolbar, int sidebar) {
    last_row = LMIN(last_row, LMIN(app->page.h, app->frame.h - toolbar));
    if (last_row <= first_row) return;
    int width = app->page.w;
    if (!app->page.row_offset && !sidebar && app->frame.stride == width && app->page.stride == width)
        memcpy(app->frame.px + (size_t)(first_row + toolbar) * (size_t)app->frame.stride,
               app->page.px + (size_t)first_row * (size_t)app->page.stride,
               (size_t)(last_row - first_row) * (size_t)width * sizeof(*app->page.px));
    else for (int row = first_row; row < last_row; row++)
        memcpy(app->frame.px + (size_t)(row + toolbar) * (size_t)app->frame.stride + sidebar,
               canvas_row(&app->page, row),
               (size_t)width * sizeof(*app->page.px));
}

#ifdef __ANDROID__
static void android_reveal_focus(App *app) {
    if (!app->focus_reveal) return;
    app->focus_reveal = false;
    if (app->editing || !app->t->cur || !app->t->cur->L || app->t->asleep) return;
    Node *focus = page_focus(app);
    bool editable = !focus;
    if (!focus) focus = edit_focus(app);
    Box *target = focus ? focus->box : NULL;
    if (!target) return;
    if (editable) {
        Node *selection = app->t->cur->d->sel[1];
        for (Node *ancestor = selection; ancestor; ancestor = ancestor->parent) if (ancestor == focus) {
            if (selection->box && box_within(selection->box, target) &&
                    selection->box->w > 0 && selection->box->h > 0) target = selection->box;
            break;
        }
    }
    if (android_focus_scroll(app->t->cur->L, target, &app->t->sy)) {
        app->vonly = app->sdirty = app->osig_ok = app->gvid_ok = false;
        android_fling_reset(&app->fling);
    }
}
#endif

/* Reuse an existing video hole only when it follows the same pixel movement
   as the page (or is fixed and covered by fixed-region damage). */
/* GPU layer for media frame im shown at CSS rect r (x, y, w, h) of the page */
static GpuVideo media_layer(const Image *im, const float r[4], float s, int side_px, int bar_px) {
    GpuVideo v = { im->px, im->w, im->h, im->w, floorf(r[0] * s + 0.5f) + side_px, floorf(r[1] * s + 0.5f) + bar_px,
                   floorf((r[0] + r[2]) * s + 0.5f) + side_px, floorf((r[1] + r[3]) * s + 0.5f) + bar_px };
    v.yuv = im->yuv; v.mat = im->yuv_mat; v.serial = media_image_serial(im);
    if (im->gpu && !im->px) { v.view = im->gpu->view; v.view_dev = im->gpu->dev; v.view_flags = im->gpu->flags; }
    return v;
}
static bool video_scroll_matches(const DItem *it, const float previous[4], float scale, int delta) {
    float current[4] = {it->x, it->y, it->x + it->w, it->y + it->h};
    float old[4] = {previous[0], previous[1], previous[0] + previous[2], previous[1] + previous[3]};
    if (!(scale > 0) || !isfinite(scale) || !(it->w > 0) || !(it->h > 0)) return false;
    for (int i = 0; i < 4; i++) {
        float next = floorf(current[i] * scale + 0.5f), before = floorf(old[i] * scale + 0.5f);
        if (!isfinite(next) || !isfinite(before) ||
            next != before - ((i & 1) && !it->fixed ? delta : 0)) return false;
    }
    return true;
}

/* Only one element scroller moved, by whole device pixels: shift the pixels inside its clip, then raster the exposed
   strip and wherever something that does not move with it (overlays, sticky headers, backgrounds) was or is.
   False when that is not cheaper than rastering the scroller. Uses the signatures dl_sigs just computed. */
static bool scroller_blit(App *a, int pwp, int ph, int *ox0, int *oy0, int *ox1, int *oy1) {
    const DisplayList *dl = &a->pdl; const Box *sb = a->sbox; float s = a->page.scale;
    if (!sb) SB_FAIL(PP_NOSBOX);
#ifdef __ANDROID__
    if (tsel_ui(a)) SB_FAIL(PP_SB_UI);
#endif
    int cs[64][4]; bool cround[64]; int nc = 0, found = 0, R[4] = {0};
    cs[0][0] = 0; cs[0][1] = 0; cs[0][2] = pwp; cs[0][3] = ph; cround[0] = false; nc = 1;
    for (int i = 0; i < dl->items.n && found < 2; i++) {   /* the scroller's clip as rastered, inside its ancestors' */
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_POP_CLIP) { if (nc > 1) nc--; continue; }
        if (it->op != DO_PUSH_CLIP || nc >= 64) continue;
        const int *p = cs[nc - 1]; int *c = cs[nc];
        c[0] = LMAX(p[0], (int)floorf(it->x * s)); c[1] = LMAX(p[1], (int)floorf(it->y * s));
        c[2] = LMIN(p[2], (int)ceilf((it->x + it->w) * s)); c[3] = LMIN(p[3], (int)ceilf((it->y + it->h) * s));
        cround[nc] = cround[nc - 1] || it->r[0] > 0 || it->r[1] > 0 || it->r[2] > 0 || it->r[3] > 0;
        if (it->sc == sb) { found++; if (cround[nc]) SB_FAIL(PP_SB_CLIP); memcpy(R, c, sizeof R); }
        nc++;
    }
    if (found != 1 || R[2] <= R[0] || R[3] <= R[1]) SB_FAIL(PP_SB_CLIP);
    if (!sb->node || sb->node->scroll_x != a->sbox_x0) SB_FAIL(PP_SB_X);
    float fd = (box_scroll_y(sb) - a->sbox_y0) * s; int d = (int)roundf(fd);
    if (!d || fabsf(fd - d) > 0.01f || abs(d) * 2 >= R[3] - R[1]) SB_FAIL(PP_SB_D);
    /* Opaque rects that do not move with the scroller (a fixed composer bar) keep their pixels when only unmoved items
       paint over them: put those back after the shift instead of rastering them and whatever scrolls beneath. */
    int cov[4][4], ncov = 0, depth = 0;
    nc = 1; cround[0] = false; cs[0][0] = R[0]; cs[0][1] = R[1]; cs[0][2] = R[2]; cs[0][3] = R[3];
    for (int i = 0; i < dl->items.n && ncov < 4; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_CLIP) {
            if (nc < 64) {
                const int *p = cs[nc - 1]; int *c = cs[nc];
                c[0] = LMAX(p[0], (int)ceilf(it->x * s)); c[1] = LMAX(p[1], (int)ceilf(it->y * s));
                c[2] = LMIN(p[2], (int)floorf((it->x + it->w) * s)); c[3] = LMIN(p[3], (int)floorf((it->y + it->h) * s));
                cround[nc] = cround[nc - 1] || it->r[0] > 0 || it->r[1] > 0 || it->r[2] > 0 || it->r[3] > 0; nc++;
            }
            continue;
        }
        if (it->op == DO_POP_CLIP) { if (nc > 1) nc--; continue; }
        if (it->op == DO_PUSH_LAYER) { depth++; continue; }
        if (it->op == DO_POP_LAYER) { if (depth) depth--; continue; }
        if (it->op != DO_RECT || depth || nc >= 64 || cround[nc - 1] || (it->color >> 24) != 255 ||
            it->r[0] > 0 || it->r[1] > 0 || it->r[2] > 0 || it->r[3] > 0 || (it->mv && (it->mv == sb || box_within(it->mv, sb)))) continue;
        const int *c = cs[nc - 1];
        int x0 = LMAX(c[0], (int)ceilf(it->x * s)), y0 = LMAX(c[1], (int)ceilf(it->y * s));
        int x1 = LMIN(c[2], (int)floorf((it->x + it->w) * s)), y1 = LMIN(c[3], (int)floorf((it->y + it->h) * s));
        if (x1 <= x0 || y1 <= y0 || (long)(x1 - x0) * (y1 - y0) < 16384) continue;
        bool ok = true;   /* nothing that moves paints over it, before or after the shift */
        for (int j = i + 1; ok && j < dl->items.n; j++) {
            const DItem *o = &dl->items.v[j];
            if (o->op >= DO_PUSH_CLIP || !o->mv || (o->mv != sb && !box_within(o->mv, sb))) continue;
            const float *b = a->nsig[j].b;
            ok = !(floorf(b[0] * s) < x1 && ceilf(b[2] * s) > x0 && floorf(b[1] * s) - abs(d) < y1 && ceilf(b[3] * s) + abs(d) > y0);
        }
        if (ok) { int *v = cov[ncov++]; v[0] = x0; v[1] = y0; v[2] = x1; v[3] = y1; }
    }
    PaintRegions dmg = {0}; long area = 0, limit = (long)(R[2] - R[0]) * (R[3] - R[1]) / 2;
    nc = 1; cround[0] = false;
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_CLIP) { if (nc < 64) { cround[nc] = cround[nc - 1] || it->r[0] > 0 || it->r[1] > 0 || it->r[2] > 0 || it->r[3] > 0; nc++; } continue; }
        if (it->op == DO_POP_CLIP) { if (nc > 1) nc--; continue; }
        if (it->op == DO_PUSH_LAYER || it->op == DO_POP_LAYER) continue;
        const Box *mv = it->mv;
        if (mv && (mv == sb || box_within(mv, sb))) continue;   /* shifted with the pixels */
        const float *b = a->nsig[i].b;
        float x0 = LMAX(R[0], floorf(b[0] * s)), x1 = LMIN(R[2], ceilf(b[2] * s));
        float y0 = LMAX(R[1], floorf(b[1] * s) - (d > 0 ? d : 0)), y1 = LMIN(R[3], ceilf(b[3] * s) - (d < 0 ? d : 0));
        if (x1 <= x0 || y1 <= y0) continue;
        if (it->op == DO_RECT && !cround[nc - 1] && !it->r[0] && !it->r[1] && !it->r[2] && !it->r[3] &&
            it->y * s <= R[1] && (it->y + it->h) * s >= R[3]) continue;   /* the same in every row of the scroller */
        for (int k = 0; k < ncov; k++) {   /* restored, not rastered */
            const int *c = cov[k];
            if (x0 < c[0] || x1 > c[2]) continue;
            if (y0 >= c[1] && y1 <= c[3]) { y1 = y0; break; }
            if (y0 >= c[1] && y0 < c[3]) y0 = c[3];
            else if (y1 > c[1] && y1 <= c[3]) y1 = c[1];
        }
        if (y1 <= y0) continue;
        long ia = (long)((x1 - x0) * (y1 - y0)); area += ia;
        if (g_paint_profile.enabled && ia > g_sb_big[0]) {   /* the largest item that does not move with the scroller */
            g_sb_big[0] = ia; g_sb_big[1] = it->op; g_sb_big[2] = (long)it->w; g_sb_big[3] = (long)it->h;
            g_sb_big[4] = !mv ? 0 : box_within(sb, mv) ? 1 : 2; g_sb_big[5] = it->fixed;
        }
        if (area > limit) SB_FAIL(PP_SB_AREA);
        paint_regions_add(&dmg, (float[4]){ x0, y0, x1, y1 });
    }
    static uint32_t *saved; static size_t saved_cap; size_t need = 0;
    for (int k = 0; k < ncov; k++) need += (size_t)(cov[k][2] - cov[k][0]) * (size_t)(cov[k][3] - cov[k][1]);
    if (need > saved_cap) { saved = xrealloc(saved, need * 4); saved_cap = need; }
    uint32_t *sp = saved;
    for (int k = 0; k < ncov; k++) for (int y = cov[k][1]; y < cov[k][3]; y++, sp += cov[k][2] - cov[k][0])
        memcpy(sp, canvas_row(&a->page, y) + cov[k][0], (size_t)(cov[k][2] - cov[k][0]) * 4);
    int w = R[2] - R[0], n = R[3] - R[1] - abs(d);
    if (d > 0) for (int y = R[1]; y < R[1] + n; y++) memcpy(canvas_row(&a->page, y) + R[0], canvas_row(&a->page, y + d) + R[0], (size_t)w * 4);
    else for (int y = R[3] - 1; y >= R[3] - n; y--) memcpy(canvas_row(&a->page, y) + R[0], canvas_row(&a->page, y + d) + R[0], (size_t)w * 4);
    sp = saved;
    for (int k = 0; k < ncov; k++) for (int y = cov[k][1]; y < cov[k][3]; y++, sp += cov[k][2] - cov[k][0])
        memcpy(canvas_row(&a->page, y) + cov[k][0], sp, (size_t)(cov[k][2] - cov[k][0]) * 4);
    int e0 = d > 0 ? R[3] - d : R[1], e1 = d > 0 ? R[3] : R[1] - d;   /* the exposed strip, less full-width covers */
    for (int k = 0; k < ncov; k++) {
        const int *c = cov[k];
        if (c[0] > R[0] || c[2] < R[2]) continue;
        if (e0 >= c[1] && e1 <= c[3]) e1 = e0;
        else if (e0 >= c[1] && e0 < c[3]) e0 = c[3];
        else if (e1 > c[1] && e1 <= c[3]) e1 = c[1];
    }
    if (e1 > e0) raster_rect(&a->page, dl, RGBA(255, 255, 255, 255), R[0], e0, R[2], e1);
    for (int i = 0; i < dmg.count; i++) {
        const float *r = dmg.rect[i];
        raster_rect(&a->page, dl, RGBA(255, 255, 255, 255), (int)r[0], (int)r[1], (int)r[2], (int)r[3]);
    }
    if (env_hook(ENV_CHECK_DAMAGE)) {
        Canvas t; canvas_init(&t, pwp, ph, s); raster(&t, dl, RGBA(255, 255, 255, 255));
        long bad = 0; for (int y = 0; y < ph; y++) for (int x = 0; x < pwp; x++) bad += t.px[(size_t)y * (size_t)t.stride + (size_t)x] != canvas_row(&a->page, y)[x];
        fprintf(stderr, "lumen: scroller blit d=%d damage=%d px=%ld covers=%d mismatched px=%ld\n", d, dmg.count, area, ncov, bad); canvas_free(&t);
    }
    g_sb_dmg += area; g_sb_rects += (unsigned)dmg.count;
    *ox0 = R[0]; *oy0 = R[1]; *ox1 = R[2]; *oy1 = R[3];
    return true;
}
/* device-pixel regions of the items that differ between the last frame's signatures and cur, kept apart
   while that adds no area (paint_fixed_add); the last frame's boxes move up by shift CSS px, as its pixels
   were shifted. After trimming the unchanged head and tail, items are matched in order by signature (a
   scroll culls items at both ends, so indices do not line up): each new item takes the first unmatched
   old one with its signature after the last match. Unmatched items on either side are damage, so matched
   ones keep their relative paint order. Returns the regions' total area. */
static void sig_add(PaintRegions *out, const float *b, float shift, float s, int pwp, int ph) {
    if (!(b[2] > b[0] && b[3] > b[1])) return;
    float r[4] = { fminf(pwp, fmaxf(0, floorf(b[0] * s))), fminf(ph, fmaxf(0, floorf((b[1] - shift) * s))),
                   fminf(pwp, fmaxf(0, ceilf(b[2] * s))), fminf(ph, fmaxf(0, ceilf((b[3] - shift) * s))) };
    paint_fixed_add(out, r);
}
typedef struct { uint64_t h; int i; } SigAt;
static int sig_at_cmp(const void *x, const void *y) {
    const SigAt *a = x, *b = y;
    return a->h != b->h ? (a->h < b->h ? -1 : 1) : a->i - b->i;
}
static int g_wk_to[16], g_wk_ev[8][2];   /* main-loop wakes per 2 s: timeouts by the source that set the wait, events by type */
static int g_sig_changed[2], g_sig_first[3]; static float g_sig_firstA[3];   /* last sig_damage: unmatched old and new items, the largest new ones in the viewport (diagnostics) */
static double sig_damage(const App *a, const struct DSig *cur, int ncur, float shift, int pwp, int ph, PaintRegions *out) {
    static SigAt *olds; static int cap;
    int first = 0, old_end = a->nosig, new_end = ncur;
    float s = a->page.scale;
    memset(g_sig_changed, 0, sizeof g_sig_changed); g_sig_first[0] = g_sig_first[1] = g_sig_first[2] = -1; memset(g_sig_firstA, 0, sizeof g_sig_firstA);
    while (first < old_end && first < new_end && a->osig[first].h == cur[first].h) first++;
    while (old_end > first && new_end > first && a->osig[old_end - 1].h == cur[new_end - 1].h) { old_end--; new_end--; }
    int no = old_end - first;
    if (no > cap) { cap = no + no / 2 + 16; olds = xrealloc(olds, sizeof *olds * (size_t)cap); }
    for (int k = 0; k < no; k++) olds[k] = (SigAt){ a->osig[first + k].h, first + k };
    if (no > 1) qsort(olds, (size_t)no, sizeof *olds, sig_at_cmp);
    int last = first - 1;   /* the last matched old item */
    for (int index = first; index < new_end; index++) {
        uint64_t h = cur[index].h;
        int lo = 0, hi = no;   /* the first old entry not before (h, last + 1) */
        while (lo < hi) { int mid = (lo + hi) / 2; if (olds[mid].h < h || (olds[mid].h == h && olds[mid].i <= last)) lo = mid + 1; else hi = mid; }
        if (lo < no && olds[lo].h == h) {
            for (int k = last + 1; k < olds[lo].i; k++) { sig_add(out, a->osig[k].b, shift, s, pwp, ph); g_sig_changed[0]++; }
            last = olds[lo].i;
        } else {
            sig_add(out, cur[index].b, 0, s, pwp, ph);
            const float *b = cur[index].b;
            float ar = fmaxf(0, fminf(b[2] * s, pwp) - fmaxf(b[0] * s, 0)) * fmaxf(0, fminf(b[3] * s, ph) - fmaxf(b[1] * s, 0));
            for (int k = 0; k < 3; k++) if (g_sig_first[k] < 0 || ar > g_sig_firstA[k]) {
                for (int j = 2; j > k; j--) { g_sig_first[j] = g_sig_first[j - 1]; g_sig_firstA[j] = g_sig_firstA[j - 1]; }
                g_sig_first[k] = index; g_sig_firstA[k] = ar; break;
            }
            g_sig_changed[1]++;
        }
    }
    for (int k = last + 1; k < old_end; k++) { sig_add(out, a->osig[k].b, shift, s, pwp, ph); g_sig_changed[0]++; }
    double area = 0;
    for (int i = 0; i < out->count; i++) area += (double)paint_region_area(out->rect[i]);
    return area;
}
#ifdef __ANDROID__
static void bar_push(App *a);
#endif
/* Startup timeline, logged once as "lumen: boot ...": ms since main() per stage, the process age at
   main() (library loading and the Java activity), the first presented frame, the first page loaded and
   when the main thread settled (2 s without a task over 150 ms). */
#ifdef __linux__
#include <unistd.h>
#endif
static struct { double t0, proc, last_long, long_ms; char s[512]; int n, longs; bool presented, loaded, done; } g_boot;
static void boot_mark(const char *k) {
    if (g_boot.done || g_boot.n >= (int)sizeof g_boot.s - 40) return;
    g_boot.n += snprintf(g_boot.s + g_boot.n, sizeof g_boot.s - g_boot.n, " %s=%.0f", k, now_ms() - g_boot.t0);
}
static void boot_begin(void) {
    g_boot.t0 = now_ms(); g_boot.proc = -1;
#ifdef __linux__
    FILE *f = fopen("/proc/self/stat", "r"); char b[1024]; struct timespec ts;
    if (f) {
        size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f);
        char *p = strrchr(b, ')'); unsigned long long st = 0; int i = 0;
        for (p = p ? strtok(p + 2, " ") : NULL; p && i < 19; p = strtok(NULL, " ")) i++;   /* field 22: starttime in clock ticks since boot */
        if (p) st = strtoull(p, NULL, 10);
        if (st && !clock_gettime(CLOCK_BOOTTIME, &ts)) g_boot.proc = (ts.tv_sec + ts.tv_nsec / 1e9 - (double)st / sysconf(_SC_CLK_TCK)) * 1000;
    }
#endif
}
/* each main-loop turn: gap = time since the previous turn */
static void boot_tick(double gap) {
    if (g_boot.done || !g_boot.loaded) return;
    double t = now_ms();
    if (gap > 150) { g_boot.last_long = t; g_boot.longs++; g_boot.long_ms += gap; }
    if (t - g_boot.last_long < 2000 && t - g_boot.t0 < 120000) return;
    boot_mark("settled"); g_boot.done = true;
    SDL_Log("lumen: boot process_age_at_main=%.0f%s (settled = last task over 150 ms; %d such tasks, %.0f ms)", g_boot.proc, g_boot.s, g_boot.longs, g_boot.long_ms);
}
static void render(App *a) {
#ifdef __ANDROID__
    if (!a->win || a->android_suspended) return;
    update_compact(a);   /* before bar_push: the toolbar's top follows the tab bar */
    bar_push(a);
    update_resolution_readout(a);
    update_renderer_readout(a);
#else
    update_compact(a);
#endif
    double t0 = now_ms();
    PaintFrame profile_frame = paint_profile_begin(t0);
    int bar_px = (int)(BAR * a->scale);
    if (a->frame.w != a->pw || a->frame.h != a->ph || a->frame.scale != a->scale) { canvas_free(&a->frame); canvas_init(&a->frame, a->pw, a->ph, a->scale); }
    int ph = a->ph - bar_px; if (ph < 1) ph = 1;
    int side_px = (int)(page_left(a) * a->scale), pwp = a->pw - side_px; if (pwp < 1) pwp = 1;
    int why = !a->vonly ? 1 : !a->t->cur || a->t->cur != a->last_page || !a->t->cur->L ? 2 : a->t->relayout ? 3 : a->t->cur->d->dom_version != a->last_ver ? 4 : 0;
    bool defer = why && why != 2 && !a->t->loading && a->t->cur && a->t->cur->L && t0 - a->last_input > 1000 && t0 - a->last_full < 250 && media_timeout_ms() >= 0 && a->page.w == pwp && a->page.h == ph;
    a->deferred = defer; if (defer) why = 0;
    if (defer && !a->vframe) { a->dirty = false; return; }
    bool part = !why;
    int rx0 = pwp, ry0 = ph, rx1 = 0, ry1 = 0;
    int scroll_delta = 0;
    PaintRegions scroll_fixed = {0};
    if (a->page.w != pwp || a->page.h != ph || a->page.scale != page_scale(a)) { canvas_free(&a->page); canvas_init(&a->page, pwp, ph, page_scale(a)); part = false; a->osig_ok = false; }
    bool gskip = false; GpuVideo gvd, *gvp = NULL, xvd[GPU_EXTRA_LAYERS]; int nxvd = 0;
    Image *fast = part && !a->sdirty && !a->vbar_tick && a->t->cur && a->gpu && a->gvid_ok && a->t->sy == a->last_sy && a->page.w == pwp && a->page.h == ph ? media_owner_frame(a->vown) : NULL;
    if (fast && fast->translucent) fast = NULL;
    for (int i = 0; fast && i < a->nxown; i++) {   /* the extra layers need opaque RGBA frames too */
        const Image *x = media_owner_frame(a->xown[i]);
        if (!x || x->translucent || (!x->px && !x->gpu)) fast = NULL;
        else xvd[nxvd++] = media_layer(x, a->xrect[i], a->page.scale, side_px, bar_px);
    }
    paint_profile_stage(&profile_frame, PAINT_PREPARE);
    /* Unchanged page, no scroll, and the last display list showed no video frame (a video playing off-screen or
       hidden): the frame would be skipped anyway, so neither rebuild it nor present it. A first frame or a new
       size relayouts (why 3), so a video that turns visible is still drawn. */
    if (!fast && part && a->t->cur && a->osig_ok && !a->last_nv && !a->sdirty && !a->vbar_tick && !a->frame_page_stale &&
        a->t->sy == a->last_sy && a->page.w == pwp && a->page.h == ph && a->page.scale == page_scale(a)
#ifdef __ANDROID__
        && !a->focus_reveal
#endif
        ) { g_pp[PP_IDLE]++; a->dirty = a->vframe = a->vonly = false; return; }
    if (a->t->cur && (defer || fast)) {   /* nothing but the video changed, or layout may be stale: only swap the video texture */
        Image *im = fast;
        if (!im) { a->dirty = a->vframe = false; return; }
        gvd = media_layer(im, a->vrect, a->page.scale, side_px, bar_px); gvp = &gvd; gskip = true;
    } else if (a->t->cur) {
        if (a->t->relayout || !a->t->cur->L) {
            if (!a->t->cur->L) a->t->cur->L = layout_new();
            page_viewport(a->t->cur, page_width(a), page_height(a), page_scale(a));
            { double q = now_ms(); layout_run(a->t->cur->L, a->t->cur->d, page_width(a), page_height(a)); g_tl += now_ms() - q; }
            a->t->relayout = false;
        }
        a->t->sy = LCLAMP(a->t->sy, 0, max_scroll(a));
        bool sdmg = !part && (why == 1 || why == 3 || why == 4) && a->osig_ok && !a->t->loading;   /* also changed: shift, then damage */
#ifdef __ANDROID__
        if (a->focus_reveal) { android_reveal_focus(a); part = false; sdmg = false; }
#endif
        float scroll_css = a->t->sy - a->last_sy;
        int blit = 0;   /* page scrolled by whole device pixels: shift the old pixels, raster only the exposed strip */
        if (a->t->sy != a->last_sy && !part && !sdmg && why != 2) g_sdmg_no[3]++;
        if (a->t->sy != a->last_sy && (part || sdmg)) {
            float sdy = (a->t->sy - a->last_sy) * a->page.scale;
            bool overlay = false;
#ifdef __ANDROID__
            overlay = tsel_ui(a) || g_kbm.nb || pop_shown();   /* the selection bar and Paste bubble are clamped to the viewport, so old pixels cannot be shifted */
#endif
            if (!a->sdirty && !overlay && fabsf(sdy - roundf(sdy)) < 0.01f && fabsf(sdy) < ph / 2) {
                blit = (int)roundf(sdy); part = true;
                a->t->sy = a->last_sy + blit / a->page.scale; scroll_css = a->t->sy - a->last_sy;   /* whole pixels like the shift: a fraction off, the old pixels would not match */
            }
            else { part = false; if (!sdmg) why = 5; else g_sdmg_no[0]++; g_pp_noblit[0]++; }
        }
        if (!blit) sdmg = false;
        g_full_paint = (double)SDL_GetTicks(); { double q = now_ms(); dl_clear(&a->pdl); dl_build(&a->pdl, a->t->cur->L, 0, a->t->sy, page_width(a), page_height(a)); g_td += now_ms() - q; } a->vbars = video_bars(a, &a->pdl);
#ifdef __ANDROID__
        tsel_overlay(a, &a->pdl);
        kbm_overlay(a, &a->pdl);
        pop_overlay(a, &a->pdl);
#endif
        paint_profile_stage(&profile_frame, PAINT_LAYOUT_DL);
        int nn = dl_sigs(&a->pdl, &a->nsig, &a->cnsig, 0);
        const DItem *vit = NULL, *vits[1 + GPU_EXTRA_LAYERS]; int nv = 0, nvit = 0;
        bool xpx = true;   /* every frame after the first has RGBA pixels (only the first layer can be YUV) */
        for (int i = 0; i < a->pdl.items.n; i++) {
            const DItem *it = &a->pdl.items.v[i];
            if (it->op != DO_IMAGE || !media_is_frame(it->img)) continue;
            if (!nv++) vit = it;
            if (nvit && !it->img->px && !it->img->gpu) xpx = false;
            if (nvit < 1 + GPU_EXTRA_LAYERS) vits[nvit] = it;
            nvit++;
            if (it->img->translucent) nv++;   /* a see-through canvas is blended by the raster, not shown through a hole */
            float s = a->page.scale;
            rx0 = LMIN(rx0, (int)floorf(it->x * s)); ry0 = LMIN(ry0, (int)floorf(it->y * s));
            rx1 = LMAX(rx1, (int)ceilf((it->x + it->w) * s)); ry1 = LMAX(ry1, (int)ceilf((it->y + it->h) * s));
        }
        rx0 = LMAX(rx0, 0); ry0 = LMAX(ry0, 0); rx1 = LMIN(rx1, pwp); ry1 = LMIN(ry1, ph);
        a->last_nv = nvit;
        /* opaque media frames are GPU layers under the page, shown through alpha-0 holes; translucent ones count twice */
        bool gv = a->gpu && nv && nv == nvit && nvit <= 1 + GPU_EXTRA_LAYERS && xpx;
        bool same_layers = gv && a->gvid_ok && a->nxown == nvit - 1 && media_owner_of(vit->img) == a->vown;
        for (int i = 1; same_layers && i < nvit; i++) same_layers = media_owner_of(vits[i]->img) == a->xown[i - 1];
        bool scroll_video = gv && nv == 1 && a->gvid_ok && media_owner_of(vit->img) == a->vown &&
                            video_scroll_matches(vit, a->vrect, a->page.scale, blit);
        scroll_fixed = paint_scroll_regions(&a->pdl.fixed, a->page.scale, pwp, ph, blit);
        bool chrome_changed = false;
        if (sdmg && !nv) {   /* the items that differ from the last frame's once moved back by the scroll are repainted with the fixed boxes */
            dl_sigs(&a->pdl, &a->ssig, &a->cssig, scroll_css);
            PaintRegions changed = {0}; sig_damage(a, a->ssig, nn, scroll_css, pwp, ph, &changed);
            for (int i = 0; i < changed.count; i++) paint_fixed_add(&scroll_fixed, changed.rect[i]);
            if (env_hook(ENV_CHECK_DAMAGE)) {
                fprintf(stderr, "lumen: scroll sigs d=%.2f unmatched old=%d new=%d of %d/%d regions=%d:", scroll_css, g_sig_changed[0], g_sig_changed[1], a->nosig, nn, changed.count);
                for (int i = 0; i < changed.count; i++) fprintf(stderr, " %.0f,%.0f-%.0f,%.0f", changed.rect[i][0], changed.rect[i][1], changed.rect[i][2], changed.rect[i][3]);
                for (int k = 0; k < 3 && g_sig_first[k] >= 0; k++) { const DItem *it = &a->pdl.items.v[g_sig_first[k]]; fprintf(stderr, " [#%d op%d f%d %.2f,%.2f %.1fx%.1f]", g_sig_first[k], it->op, it->fixed, it->x, it->y, it->w, it->h); }
                fputc('\n', stderr);
            }
            build_chrome(a); chrome_tip(a);
            chrome_changed = dl_hash(&a->cdl) * 31 + dl_hash(&a->hdl) != a->chash;
        } else if (sdmg) { blit = 0; part = false; g_sdmg_no[2]++; }
        /* the exposed strip and the fixed boxes repainted after the shift (overlaps once: scrolling up, the strip lies
           in a header's shifted copy): past 4/5 of the page, a full raster is as cheap */
        float strip[4] = { 0, blit > 0 ? ph - blit : 0, pwp, blit > 0 ? ph : -blit };
        double fixed_area = paint_regions_union_area(&scroll_fixed, blit ? strip : NULL);
        if (blit && !nv && fixed_area * 5 > (double)pwp * ph * 4 && t0 < g_load_trace_until) {   /* geometry only */
            static double last; char m[900]; int o = 0;
            for (int i = 0; i < scroll_fixed.count && o < 300; i++) { const float *r = scroll_fixed.rect[i]; o += snprintf(m + o, sizeof m - (size_t)o, " %.0f,%.0f-%.0f,%.0f", r[0], r[1], r[2], r[3]); }
            int big[3] = { -1, -1, -1 }; float bigA[3] = { 0, 0, 0 }, vw = a->pdl.vw, vh = a->pdl.vh;   /* the largest fixed items, clipped to the viewport */
            for (int i = 0; i < a->pdl.items.n; i++) {
                const DItem *it = &a->pdl.items.v[i];
                if (!it->fixed || it->op == DO_PUSH_CLIP || it->op >= DO_POP_CLIP) continue;
                float ar = LMAX(0, LMIN(it->x + it->w, vw) - LMAX(it->x, 0)) * LMAX(0, LMIN(it->y + it->h, vh) - LMAX(it->y, 0));
                for (int k = 0; k < 3; k++) if (ar > bigA[k]) { for (int j = 2; j > k; j--) { big[j] = big[j - 1]; bigA[j] = bigA[j - 1]; } big[k] = i; bigA[k] = ar; break; }
            }
            for (int k = 0; k < 3 && sdmg && g_sig_first[k] >= 0 && g_sig_first[k] < a->pdl.items.n && o < 440; k++) { const DItem *it = &a->pdl.items.v[g_sig_first[k]]; o += snprintf(m + o, sizeof m - (size_t)o, " <#%d op%d f%d %.1f,%.2f %.1fx%.1f ng%d mv%d>", g_sig_first[k], it->op, it->fixed, it->x, it->y, it->w, it->h, it->ng, it->mv != NULL); }
            for (int k = 0; k < 3 && big[k] >= 0 && o < 560; k++) { const DItem *it = &a->pdl.items.v[big[k]]; o += snprintf(m + o, sizeof m - (size_t)o, " [op%d %c %.0f,%.0f %.0fx%.0f a%u mv%d]", it->op, it->sticky ? 's' : 'f', it->x, it->y, it->w, it->h, (unsigned)COLOR_A(it->color), it->mv != NULL); }
            if (t0 - last > 400) { last = t0; LOAD_TRACE("noblit fixed d=%d page=%dx%d area=%.0f%% sdmg=%d fixed=%d unmatched=%d/%d repaint px:%s", blit, pwp, ph, fixed_area * 100 / ((double)pwp * ph), sdmg, a->pdl.fixed.count, g_sig_changed[0], g_sig_changed[1], o ? m : " none"); }
        }
        if (blit && ((nv && !scroll_video) || fixed_area * 5 > (double)pwp * ph * 4)) { g_pp_noblit[nv && !scroll_video ? 2 : 1]++; blit = 0; part = false; if (!sdmg) why = 5; else g_sdmg_no[1]++; }
        bool bars_only = part && !blit && !a->sdirty && a->vbar_tick && (nv == 0 || (same_layers && nvit == 1));
        if (part && !blit && !a->sdirty && a->vbar_tick && !bars_only) { part = false; why = 1; }   /* bar clocks over several video layers: a full raster */
        paint_profile_stage(&profile_frame, PAINT_SIGNATURES);
        if (blit) {
            profile_frame.blit = true;
            scroll_delta = blit;
            int d = blit, n = ph - abs(d); size_t row = (size_t)a->page.stride;
#ifdef __ANDROID__
            if (a->gpu || a->page.row_offset) {
                int offset = a->page.row_offset + d;
                if (offset < 0) offset += ph;
                else if (offset >= ph) offset -= ph;
                a->page.row_offset = offset;
            } else
#endif
            if (d > 0) memmove(a->page.px, a->page.px + (size_t)d * row, (size_t)n * row * 4);
            else memmove(a->page.px + (size_t)(-d) * row, a->page.px, (size_t)n * row * 4);
            a->page.punch = scroll_video ? vit->img : NULL;
            raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), 0, d > 0 ? n : 0, pwp, d > 0 ? ph : -d);
            for (int i = 0; i < scroll_fixed.count; i++) {
                const float *box = scroll_fixed.rect[i];
                raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255),
                            (int)box[0], (int)box[1], (int)box[2], (int)box[3]);
            }   /* fixed boxes moved with the shift: repaint where they were and are */
            a->page.punch = NULL;
            rx0 = 0; ry0 = 0; rx1 = pwp; ry1 = ph; a->gvid_ok = scroll_video; g_pp[sdmg ? PP_BDMG : PP_BLIT]++;
            if (sdmg && env_hook(ENV_CHECK_DAMAGE)) {
                Canvas t; canvas_init(&t, pwp, ph, a->page.scale); raster(&t, &a->pdl, RGBA(255, 255, 255, 255));
                long bad = 0; for (int y = 0; y < ph; y++) for (int x = 0; x < pwp; x++) bad += t.px[(size_t)y * (size_t)t.stride + (size_t)x] != canvas_row(&a->page, y)[x];
                fprintf(stderr, "lumen: scroll damage d=%d regions=%d mismatched px=%ld\n", d, scroll_fixed.count, bad); canvas_free(&t);
            }
            if (chrome_changed) part = false;
        } else if (!sdmg && part && a->sdirty && !nv && scroller_blit(a, pwp, ph, &rx0, &ry0, &rx1, &ry1)) {
            a->gvid_ok = false; g_pp[PP_SBLIT]++;
        } else if (part && a->sdirty) {
            g_pp[PP_SDIRTY]++;
            float s = a->page.scale;
            int x0 = (int)floorf(a->srect[0] * s), y0 = (int)floorf(a->srect[1] * s), x1 = (int)ceilf(a->srect[2] * s), y1 = (int)ceilf(a->srect[3] * s);
            if (nv) { x0 = LMIN(x0, rx0); y0 = LMIN(y0, ry0); x1 = LMAX(x1, rx1); y1 = LMAX(y1, ry1); }
            x0 = LMAX(x0, 0); y0 = LMAX(y0, 0); x1 = LMIN(x1, pwp); y1 = LMIN(y1, ph);
            if (x1 > x0 && y1 > y0) raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), x0, y0, x1, y1);
            rx0 = x0; ry0 = y0; rx1 = LMAX(x1, x0); ry1 = LMAX(y1, y0); a->gvid_ok = false;
        } else if (bars_only) {   /* only the video bars' clocks moved: repaint them, punching the video layer's hole again */
            float s = a->page.scale;
            int x0 = LMAX((int)floorf(a->vbr[0] * s), 0), y0 = LMAX((int)floorf(a->vbr[1] * s), 0), x1 = LMIN((int)ceilf(a->vbr[2] * s), pwp), y1 = LMIN((int)ceilf(a->vbr[3] * s), ph);
            if (x1 > x0 && y1 > y0) {
                a->page.punch = nv ? vit->img : NULL;
                raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), x0, y0, x1, y1);
                a->page.punch = NULL; rx0 = x0; ry0 = y0; rx1 = x1; ry1 = y1; g_pp[PP_RECT]++;
                if (env_hook(ENV_CHECK_DAMAGE)) {
                    Canvas t; canvas_init(&t, pwp, ph, a->page.scale); t.punch = nv ? vit->img : NULL; raster(&t, &a->pdl, RGBA(255, 255, 255, 255)); t.punch = NULL;
                    long bad = 0; for (int y = 0; y < ph; y++) for (int x = 0; x < pwp; x++) bad += t.px[(size_t)y * (size_t)t.stride + (size_t)x] != canvas_row(&a->page, y)[x];
                    fprintf(stderr, "lumen: bar damage %dx%d at %d,%d mismatched px=%ld\n", x1 - x0, y1 - y0, x0, y0, bad); canvas_free(&t);
                }
            } else { gskip = true; g_pp[PP_SKIP]++; }
        } else if (part && (nv == 0 || same_layers)) { gskip = true; g_pp[PP_SKIP]++; }
        else if (part && rx1 > rx0 && ry1 > ry0) { a->gvid_ok = false; g_pp[PP_RECT]++; raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), rx0, ry0, rx1, ry1); }
        else {
            g_pp_why[LCLAMP(why, 0, 5)]++;
            bool dmg = (why == 1 || why == 3 || why == 4) && !nv && a->osig_ok && a->t->sy == a->last_sy && !a->t->loading;
            double dmg_area = -1; int dmg_n = -1;
            if (dmg) {   /* only DOM/layout changed: raster the union of the items that differ from the last frame */
                build_chrome(a); chrome_tip(a);
                bool chrome_changed = dl_hash(&a->cdl) * 31 + dl_hash(&a->hdl) != a->chash;
                PaintRegions changed = {0}; double area = sig_damage(a, a->nsig, nn, 0, pwp, ph, &changed);
                dmg_area = area; dmg_n = changed.count;
                int x0 = pwp, y0 = ph, x1 = 0, y1 = 0;   /* their union: the rows composed and uploaded */
                for (int i = 0; i < changed.count; i++) {
                    const float *c = changed.rect[i];
                    x0 = LMIN(x0, (int)c[0]); y0 = LMIN(y0, (int)c[1]); x1 = LMAX(x1, (int)c[2]); y1 = LMAX(y1, (int)c[3]);
                }
                if (dmg && !changed.count) { part = true; gskip = true; }
                else if (dmg && area * 2 < (double)pwp * ph) {
                    double q = now_ms();
                    for (int i = 0; i < changed.count; i++) { const float *c = changed.rect[i]; raster_rect(&a->page, &a->pdl, RGBA(255, 255, 255, 255), (int)c[0], (int)c[1], (int)c[2], (int)c[3]); }
                    g_tx += now_ms() - q;
                    part = true; rx0 = x0; ry0 = y0; rx1 = x1; ry1 = y1; a->gvid_ok = false; g_pp[PP_DMG]++;
                }
                else dmg = false;
                if (dmg && env_hook(ENV_CHECK_DAMAGE)) {
                    Canvas t; canvas_init(&t, pwp, ph, a->page.scale); raster(&t, &a->pdl, RGBA(255, 255, 255, 255));
                    long bad = 0; for (int y = 0; y < ph; y++) for (int x = 0; x < pwp; x++) bad += t.px[(size_t)y * (size_t)t.stride + (size_t)x] != canvas_row(&a->page, y)[x];
                    fprintf(stderr, "lumen: damage %dx%d at %d,%d regions=%d mismatched px=%ld\n", LMAX(0, x1 - x0), LMAX(0, y1 - y0), x0, y0, changed.count, bad); canvas_free(&t);
                }
                if (dmg && chrome_changed) { part = false; gskip = false; }
            }
            if (!dmg && why && now_ms() < g_load_trace_until) {   /* why a frame took the full repaint: the gates, or the damage's area and largest items */
                static double last_full_tr; double tn = now_ms();
                if (tn - last_full_tr > 500) {
                    last_full_tr = tn; char m[320]; int o = 0; m[0] = 0;
                    for (int k = 0; dmg_n >= 0 && k < 3 && g_sig_first[k] >= 0 && g_sig_first[k] < a->pdl.items.n && o < 260; k++) { const DItem *it = &a->pdl.items.v[g_sig_first[k]]; o += snprintf(m + o, sizeof m - (size_t)o, " <op%d f%d %.0f,%.0f %.0fx%.0f>", it->op, it->fixed, it->x, it->y, it->w, it->h); }
                    LOAD_TRACE("full why=%d nv=%d osig=%d sy_same=%d loading=%d regions=%d area=%.0f%% unmatched=%d/%d%s", why, nv, a->osig_ok, a->t->sy == a->last_sy, a->t->loading, dmg_n,
                               dmg_area < 0 ? -1.0 : dmg_area * 100 / ((double)pwp * ph), dmg_n >= 0 ? g_sig_changed[0] : -1, dmg_n >= 0 ? g_sig_changed[1] : -1, m);
                }
            }
            if (!dmg) {
                part = false; a->page.punch = gv ? vit->img : NULL; a->page.punched = false; g_pp[PP_FULL]++;
                for (int i = 1; gv && i < nvit; i++) a->page.punch_more[i - 1] = vits[i]->img;
                a->page.punched_more = 0;
                { double q = now_ms(); raster(&a->page, &a->pdl, RGBA(255, 255, 255, 255)); g_tx += now_ms() - q; }
                a->gvid_ok = gv && a->page.punched && a->page.punched_more == (gv ? (1u << (nvit - 1)) - 1 : 0); a->page.punch = NULL;
                memset(a->page.punch_more, 0, sizeof a->page.punch_more);
            }
        }
        { struct DSig *t = a->osig; a->osig = a->nsig; a->nsig = t; int c = a->cosig; a->cosig = a->cnsig; a->cnsig = c; a->nosig = nn; a->osig_ok = true; }
        if (gv && a->gvid_ok) {
            a->vown = media_owner_of(vit->img); a->vrect[0] = vit->x; a->vrect[1] = vit->y; a->vrect[2] = vit->w; a->vrect[3] = vit->h;
            gvd = media_layer(vit->img, a->vrect, a->page.scale, side_px, bar_px); gvp = &gvd;
            a->nxown = nvit - 1;
            for (int i = 1; i < nvit; i++) {
                const DItem *x = vits[i]; float *r = a->xrect[i - 1];
                a->xown[i - 1] = media_owner_of(x->img); r[0] = x->x; r[1] = x->y; r[2] = x->w; r[3] = x->h;
                xvd[nxvd++] = media_layer(x->img, r, a->page.scale, side_px, bar_px);
            }
        }
    } else { a->gvid_ok = false; a->osig_ok = false; raster(&a->page, &(DisplayList){0}, RGBA(255, 255, 255, 255)); }
    paint_profile_stage(&profile_frame, PAINT_RASTER);
    if (!part) { build_chrome(a); chrome_tip(a); a->chash = dl_hash(&a->cdl) * 31 + dl_hash(&a->hdl); raster(&a->frame, &a->cdl, RGBA(255, 255, 255, 255)); if (panel_shown(a)) panel_backdrop(a, bar_px, side_px - (int)(a->side * a->scale)); }
    bool direct_page = false;
#ifdef __ANDROID__
    direct_page = a->gpu && part && !gskip && profile_frame.blit && !side_px &&
                  !a->hdl.items.n && !env_hook(ENV_SHOT) &&
                  a->page.w == a->frame.w && a->page.h == a->frame.h - bar_px;
#endif
    if (!direct_page && a->frame_page_stale) { gskip = false; ry0 = 0; ry1 = ph; }
    if (!direct_page && !gskip) {
        compose_page(a, part ? ry0 : 0, part ? ry1 : ph, bar_px, side_px);
        a->frame_page_stale = false;
    } else if (direct_page) a->frame_page_stale = true;
    if (!gskip && a->hdl.items.n) { float s = a->frame.scale; raster_rect(&a->frame, &a->hdl, RGBA(255, 255, 255, 255), (int)floorf(g_pop[0] * s), (int)floorf(g_pop[1] * s), (int)ceilf(g_pop[2] * s), (int)ceilf(g_pop[3] * s)); }   /* floats over the page */
    paint_profile_stage(&profile_frame, PAINT_COMPOSE);
    bool page_uploaded = direct_page &&
        gpu_scroll_page_ring(a->gpu, a->page.px, a->frame.w, a->frame.h, a->page.stride,
                        bar_px, scroll_delta, 0, 0, a->page.row_offset);
    for (int i = 0; page_uploaded && i < scroll_fixed.count; i++)   /* only the fixed boxes' own columns */
        page_uploaded = gpu_upload_page_rect_ring(a->gpu, a->page.px, a->frame.w, a->frame.h, a->page.stride, bar_px,
                                        (int)scroll_fixed.rect[i][0], (int)scroll_fixed.rect[i][2],
                                        (int)scroll_fixed.rect[i][1], (int)scroll_fixed.rect[i][3], a->page.row_offset);
    if (direct_page && !page_uploaded)
        page_uploaded = gpu_upload_page_ring(a->gpu, a->page.px, a->frame.w, a->frame.h,
                                        a->page.stride, bar_px, ry0, ry1, a->page.row_offset);
    if (direct_page && !page_uploaded) {
        compose_page(a, 0, ph, bar_px, side_px);
        a->frame_page_stale = false;
        gskip = false; ry0 = 0; ry1 = ph;
    }
    if (gvp) gpu_set_extra_layers(a->gpu, xvd, nxvd);
    int up0 = (gskip || page_uploaded) ? 0 : part ? bar_px + ry0 : 0, up1 = (gskip || page_uploaded) ? 0 : part ? bar_px + ry1 : a->frame.h;
    bool shown = a->gpu && gpu_present_frame(a->gpu, a->frame.px, a->frame.w, a->frame.h, a->frame.stride, up0, up1, gvp);
    if (!g_boot.presented) { g_boot.presented = true; boot_mark("first_present"); }
#ifdef __ANDROID__
    /* a GPU without a surface is between windows (going to the background): a software window surface
       would take the window from Vulkan, so the frame waits for the next one */
    if (a->gpu) shown = true;
#endif
    if (!shown) {
        if (a->frame_page_stale) {
            compose_page(a, 0, ph, bar_px, side_px);
            a->frame_page_stale = false;
        }
        SDL_Surface *ws = SDL_GetWindowSurface(a->win);
        if (ws) {
            SDL_Surface *src = SDL_CreateSurfaceFrom(a->frame.w, a->frame.h, SDL_PIXELFORMAT_ARGB8888, a->frame.px, a->frame.stride * 4);
            SDL_BlitSurfaceScaled(src, NULL, ws, NULL, SDL_SCALEMODE_LINEAR);
            SDL_DestroySurface(src); SDL_UpdateWindowSurface(a->win);
        }
    }
    paint_profile_stage(&profile_frame, PAINT_PRESENT);
    if (!gskip || gvp) doc_activity();   /* page pixels changed: animation frames keep their full rate */
    a->frame_ms = now_ms() - t0;
    if (env_hook(ENV_SHOT)) {   /* write the presented frame to a PNG after LUMEN_SHOT_MS and exit */
        static double ts; if (!ts) ts = t0;
        const char *d = getenv("LUMEN_SHOT_MS");
        if (t0 - ts >= (d ? atof(d) : 1500)) { png_write(env_hook(ENV_SHOT), a->frame.px, a->frame.w, a->frame.h, a->frame.stride); exit(0); }
    }
    paint_profile_report(&profile_frame, t0, part);
    if (g_paint_profile.enabled) {
        static int np, nf, nw[6]; static double tp, tf, t_last;
        if (part) { np++; tp += a->frame_ms; } else { nf++; tf += a->frame_ms; nw[why]++; }
        if (t0 - t_last > 1000) {
            fprintf(stderr, "lumen: paint partial=%d (%.1fms avg) full=%d (%.1fms avg) why: novonly=%d page=%d relayout=%d dom=%d scroll=%d | ms/s style=%.0f layout=%.0f dl=%.0f raster=%.0f\n", np, np ? tp / np : 0, nf, nf ? tf / nf : 0, nw[1], nw[2], nw[3], nw[4], nw[5], g_tr, g_tl, g_td, g_tx); g_tr = g_tl = g_td = g_tx = 0;
            np = nf = 0; tp = tf = 0; memset(nw, 0, sizeof nw); t_last = t0;
        }
    }
    if (!part) a->last_full = t0;
    a->vframe = false;
    a->dirty = a->vonly = a->sdirty = a->vbar_tick = false;
    a->last_page = a->t->cur; a->last_sy = a->t->sy; if (!defer) a->last_ver = a->t->cur ? a->t->cur->d->dom_version : 0;
}

/* scroll the innermost scrollable box under (x,y) that can still move; false = let the page scroll */
static bool wheel_scroll(App *a, float x, float y, float dx, float dy) {
    Page *p = a->t->cur; if (!p || !p->L || a->t->asleep) return false;
    for (Box *b = layout_hit(p->L, x, y + a->t->sy, 0, a->t->sy); b; b = b->parent) {
        if (!b->scroller || !b->node || !b->st) continue;
        bool sx = b->st->overflow_x == OV_AUTO || b->st->overflow_x == OV_SCROLL, sy = b->st->overflow_y == OV_AUTO || b->st->overflow_y == OV_SCROLL;
        float maxx = LMAX(0, b->scroll_w - b->w), maxy = LMAX(0, b->scroll_h - b->h);
        float lo = box_scroll_from_end(b) ? -maxy : 0, hi = box_scroll_from_end(b) ? 0 : maxy;
        float nx = sx ? LCLAMP(b->node->scroll_x + dx, 0, maxx) : b->node->scroll_x, ny = sy ? LCLAMP(b->node->scroll_y + dy, lo, hi) : b->node->scroll_y;
        if (ny != b->node->scroll_y) {   /* whole device pixels, so the next frame can shift the scroller's pixels */
            float s = page_scale(a), end = box_scroll_from_end(b) ? maxy : 0, snapped = LCLAMP(roundf((ny + end) * s) / s - end, lo, hi);
            if (snapped != b->node->scroll_y) ny = snapped;
        }
        if (nx == b->node->scroll_x && ny == b->node->scroll_y) continue;
        float painted_x = b->node->scroll_x, painted_y = box_scroll_y(b);
        b->node->scroll_x = nx; b->node->scroll_y = ny;
        doc_visual_changed();
        float rx = b->x, ry = b->y; bool fx = b->fixed;
        for (Box *q = b->parent; q; q = q->parent) { fx |= q->fixed; if (q->scroller && q->node) { rx -= q->node->scroll_x; ry -= q->node->scroll_y; } }
        if (fx || (a->dirty && !a->vonly)) { a->vonly = a->sdirty = false; }
        else {
            float r[4] = { rx, ry - a->t->sy, rx + b->w, ry - a->t->sy + b->h };
            if (a->sdirty) { r[0] = LMIN(r[0], a->srect[0]); r[1] = LMIN(r[1], a->srect[1]); r[2] = LMAX(r[2], a->srect[2]); r[3] = LMAX(r[3], a->srect[3]); }
            if (!a->sdirty) { a->sbox = b; a->sbox_x0 = painted_x; a->sbox_y0 = painted_y; }
            else if (a->sbox != b) a->sbox = NULL;
            memcpy(a->srect, r, sizeof r); a->sdirty = a->vonly = true;
        }
        a->dirty = true;
        if (p->js) js_dispatch(p->js, b->node, "scroll", "Event", false, false, 0, 0, 0, NULL);
        return true;
    }
    return false;
}
static void wheel_apply(App *a, float mx, float my, float dx, float dy) {
    float zoom = tab_zoom(a->t); dx /= zoom; dy /= zoom;
    if (!(my > BAR && mx > page_left(a) && wheel_scroll(a, page_x(a, mx), page_y(a, my), dx, dy))) { if (!(a->dirty && !a->vonly)) a->vonly = true; a->t->sy += dy; }
    a->dirty = true;
}
#ifdef __ANDROID__
static bool android_scroll_apply(App *app, float anchor_x, float anchor_y, float dx, float dy) {
    if (!app->t->cur || app->t->asleep || app->t->info || !isfinite(dx) || !isfinite(dy) ||
            anchor_x <= page_left(app) || anchor_y <= BAR || (!dx && !dy)) return false;
    float zoom = tab_zoom(app->t); dx /= zoom; dy /= zoom;
    if (wheel_scroll(app, page_x(app, anchor_x), page_y(app, anchor_y), dx, dy)) return true;
    float scale = page_scale(app);
    float next = LCLAMP(roundf((app->t->sy + dy) * scale) / scale, 0, max_scroll(app));
    if (next == app->t->sy) return false;
    app->t->sy = next;
    if (!(app->dirty && !app->vonly)) app->vonly = true;
    app->dirty = true;
    return true;
}
/* smooth scrolling off: a drag moves the page in steps of the size set in Settings, the first
   after 5 px of finger travel, then one every further 10 px; no momentum after. */
#define STEP_START 5
#define STEP_TRAVEL 10
static bool android_step_scroll(App *app, float anchor_x, float anchor_y, float dx, float dy) {
    float zoom = tab_zoom(app->t), step = (float)LMIN(g_scroll_px, scroll_px_max(app)), out[2] = { 0, 0 };
    float *r[2] = { &app->step_rx, &app->step_ry };
    *r[0] += dx / zoom; *r[1] += dy / zoom;
    for (int k = 0; k < 2; k++)
        for (float need; fabsf(*r[k]) >= (need = app->step_started ? STEP_TRAVEL : STEP_START); ) {
            float sign = *r[k] > 0 ? 1 : -1;
            out[k] += sign * step;
            *r[k] -= sign * need; app->step_started = true;
        }
    if (!out[0] && !out[1]) return true;   /* not far enough yet: the drag goes on */
    return android_scroll_apply(app, anchor_x, anchor_y, out[0] * zoom, out[1] * zoom);
}
static void android_fling_tick(App *app) {
    if (!app->fling.active) return;
    if (app->android_suspended || app->touch_tab != app->t || app->touch_generation != app->t->lgen) {
        android_fling_reset(&app->fling);
        return;
    }
    float dx, dy;
    if (android_fling_step(&app->fling, now_ms(), app->scale, &dx, &dy) &&
            !android_scroll_apply(app, app->fling_x, app->fling_y, dx, dy)) android_fling_reset(&app->fling);
}
static unsigned android_dom_touch(App *app, const char *phase, const SDL_Event *event);
static void page_touch_controls(App *a, float x, float y);
static void kbd_tap(App *a, float x, float y);
static bool tsel_touch(App *a, AndroidTouchPhase phase, const SDL_Event *ev, float x, float y);
static void tsel_touch_reset(void);
/* the keyboard id of the keys the edit bar sends (a held Backspace must not end its own touch) */
#define BAR_KEY_ID 0x4c42u
static bool android_touch_input(App *app, SDL_Event *event) {
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.which == BAR_KEY_ID) return false;
    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION: return event->motion.which == SDL_TOUCH_MOUSEID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        return event->button.which == SDL_TOUCH_MOUSEID;
    case SDL_EVENT_WINDOW_FOCUS_LOST: case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        android_dom_touch(app, "reset", NULL);
        android_touch_reset(&app->touch);
        android_fling_reset(&app->fling);
        tsel_touch_reset();
        return false;
    case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED:
        break;
    default: return false;
    }
    if (event->tfinger.windowID != SDL_GetWindowID(app->win) ||
            event->tfinger.touchID == SDL_MOUSE_TOUCHID) return true;
    if (app->touch_tab != app->t || app->touch_generation != app->t->lgen) {
        android_dom_touch(app, "reset", NULL);
        android_touch_reset(&app->touch);
        android_fling_reset(&app->fling);
    }
    AndroidTouchPhase phase = event->type == SDL_EVENT_FINGER_DOWN ? ANDROID_TOUCH_DOWN
        : event->type == SDL_EVENT_FINGER_UP ? ANDROID_TOUCH_UP
        : event->type == SDL_EVENT_FINGER_MOTION ? ANDROID_TOUCH_MOVE : ANDROID_TOUCH_CANCEL;
    float pointer_x = event->tfinger.x * app->vw, pointer_y = event->tfinger.y * app->vh;
    if (!isfinite(pointer_x) || !isfinite(pointer_y)) {
        android_dom_touch(app, "reset", NULL);
        android_touch_reset(&app->touch);
        android_fling_reset(&app->fling);
        return true;
    }
    if (phase == ANDROID_TOUCH_DOWN) {
        android_fling_reset(&app->fling);
        if (pointer_x < 0 || pointer_y < 0 || pointer_x > app->vw || pointer_y > app->vh) return true;
        if (panel_shown(app) && pointer_x < page_left(app) && pointer_y > BAR) {   /* beside the panel: closes it */
            app->pan_close = true; android_touch_reset(&app->touch); return true;
        }
        app->touch_tab = app->t;
        app->touch_generation = app->t->lgen;
        g_sm_dx = g_sm_dy = 0;
    }
    app->last_input = now_ms();
    if (tsel_touch(app, phase, event, pointer_x, pointer_y)) return true;
    unsigned dom = android_dom_touch(app, phase == ANDROID_TOUCH_DOWN ? "start"
        : phase == ANDROID_TOUCH_MOVE ? "move" : phase == ANDROID_TOUCH_UP ? "end" : "cancel", event);
    app->touch_dom_flags = dom;
    if (app->touch_tab != app->t || app->touch_generation != app->t->lgen) {
        android_touch_reset(&app->touch);
        android_fling_reset(&app->fling);
        return true;
    }
    if (phase == ANDROID_TOUCH_DOWN && app->touch.count == 0 && (app->touch_block & 3) == 3) dom |= 1;   /* touch-action: none */
    if (dom & 1) app->touch.can_scroll = false;
    app->touch.slop = g_smooth ? 8 : STEP_START;   /* stepped scrolling starts after 5 px */
    AndroidTouchResult result = android_touch_update(&app->touch, phase,
        event->tfinger.touchID, event->tfinger.fingerID, pointer_x, pointer_y,
        pointer_x > page_left(app) && pointer_y > BAR && !g_hud_pop && !app->t->info && !(dom & 1));
    double timestamp = (double)event->tfinger.timestamp / 1000000.0;
    if (phase == ANDROID_TOUCH_DOWN) { app->step_rx = app->step_ry = 0; app->step_started = false; }
    if (phase == ANDROID_TOUCH_DOWN && app->touch.count == 1 && !app->touch.blocked && app->touch.can_scroll)
        android_fling_begin(&app->fling, timestamp);
    if (phase == ANDROID_TOUCH_CANCEL || app->touch.blocked) android_fling_reset(&app->fling);
    if (result.action == ANDROID_TOUCH_SCROLL && app->touch.count == 1 && app->touch_block) {   /* touch-action: pan-x or pan-y */
        if (app->touch_block & TOUCH_NO_X) result.dx = 0;
        if (app->touch_block & TOUCH_NO_Y) result.dy = 0;
        if (!result.dx && !result.dy) result.action = ANDROID_TOUCH_NONE;
    }
    if (result.action == ANDROID_TOUCH_SCROLL) {
        android_dom_touch(app, "scroll", event);
        app->fling_x = result.anchor_x; app->fling_y = result.anchor_y;
        if (!g_smooth) { android_step_scroll(app, result.anchor_x, result.anchor_y, result.dx, result.dy); android_fling_reset(&app->fling); }
        else if (android_scroll_apply(app, result.anchor_x, result.anchor_y, result.dx, result.dy)) {
            if (!app->fling.tracking) android_fling_begin(&app->fling, timestamp);
            else android_fling_sample(&app->fling, result.dx, result.dy, timestamp);
        } else android_fling_reset(&app->fling);
    }
    if (phase == ANDROID_TOUCH_UP) { if (g_smooth) android_fling_release(&app->fling, timestamp, now_ms()); else android_fling_reset(&app->fling); }
    if (result.action != ANDROID_TOUCH_TAP) return true;
    /* A handled touchend can suppress compatibility clicks. It still counts
       as interaction with the player for the control visibility timer. */
    if (dom & 4) {
        page_touch_controls(app, page_x(app, result.anchor_x), page_y(app, result.anchor_y));
        kbd_tap(app, result.anchor_x, result.anchor_y);
        return true;
    }
    SDL_Event click = {0};
    click.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.timestamp = event->tfinger.timestamp;
    click.button.windowID = event->tfinger.windowID;
    click.button.which = SDL_TOUCH_MOUSEID;
    click.button.button = SDL_BUTTON_LEFT;
    click.button.down = true;
    click.button.clicks = 1;
    click.button.x = result.anchor_x; click.button.y = result.anchor_y;
    *event = click;
    return false;
}
#endif
static Tab *tab_of_doc(App *a, Document *d) {
    for (int depth = 0; d && depth < 16; depth++) {
        for (int i = 0; i < a->ntabs; i++) if (a->tabs[i]->cur && a->tabs[i]->cur->d == d) return a->tabs[i];
        Page *f = NULL; for (Page *p = g_frames; p; p = p->fnext) if (p->d == d) { f = p; break; }
        d = f && f->frame_el ? f->frame_el->doc : NULL;
    }
    return NULL;
}
static void update_size(App *a) {
#ifdef __ANDROID__
    a->focus_reveal = true;
#endif
    for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true;
    int w, h; SDL_GetWindowSize(a->win, &w, &h); SDL_GetWindowSizeInPixels(a->win, &a->pw, &a->ph);
    float window_scale = w > 0 && a->pw > 0 ? (float)a->pw / (float)w : 1;
    a->scale = window_scale;
#ifdef __ANDROID__
    a->scale = g_pixel_scale_pct / 100.f;
    a->screen_w = a->screen_h = 0;
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(a->win));
    if (mode) {
        double screen_width = floor((double)mode->w * mode->pixel_density + 0.5);
        double screen_height = floor((double)mode->h * mode->pixel_density + 0.5);
        if (screen_width > 0 && screen_width <= 2147483647.0 && screen_height > 0 && screen_height <= 2147483647.0) {
            a->screen_w = (int)screen_width; a->screen_h = (int)screen_height;
        }
    }
#endif
    a->input_scale = window_scale / a->scale;
#ifdef __ANDROID__
    /* a whole number of CSS pixels: the device pixels a scale doesn't divide evenly stay unused at the right and bottom */
    a->vw = floorf((float)a->pw / a->scale); a->vh = floorf((float)a->ph / a->scale);
#else
    a->vw = (float)w * a->input_scale; a->vh = (float)h * a->input_scale;
#endif
    a->vonly = a->sdirty = a->osig_ok = a->gvid_ok = false;
    if (a->gpu) gpu_resize(a->gpu, a->pw, a->ph);
    a->t->relayout = a->dirty = true;
}

static void input_coordinates(App *app, SDL_Event *event) {
    switch (event->type) {
    case SDL_EVENT_MOUSE_MOTION:
        event->motion.x *= app->input_scale; event->motion.y *= app->input_scale;
        event->motion.xrel *= app->input_scale; event->motion.yrel *= app->input_scale;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        event->button.x *= app->input_scale; event->button.y *= app->input_scale;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        event->wheel.mouse_x *= app->input_scale; event->wheel.mouse_y *= app->input_scale;
        break;
    default: break;
    }
}

static App *g_app;
static uint64_t fnv(const char *s) { uint64_t h = 1469598103934665603ull; for (; s && *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull; return h; }
static uint64_t fnvn(const char *s, size_t n) { uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 1099511628211ull; return h; }
/* Does CSS text end between top-level rules (braces balanced outside strings and comments, last token '}')? */
static bool css_rule_boundary(const char *s, size_t n) {
    int depth = 0; char last = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { for (i += 2; i + 1 < n && !(s[i] == '*' && s[i + 1] == '/'); i++) {} if (i + 1 >= n) return false; i++; continue; }
        if (c == '"' || c == '\'') { for (i++; i < n && s[i] != c; i++) if (s[i] == '\\') i++; if (i >= n) return false; last = c; continue; }
        if (c == '\\') { i++; last = 'x'; continue; }
        if (c == '{') depth++; else if (c == '}' && --depth < 0) return false;
        if (!isspace((unsigned char)c)) last = c;
    }
    return depth == 0 && last == '}';
}
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
        /* sheets arriving together share one full recalc (restyle runs after net_poll or on a JS style read) */
        p->sheets_pending = true; g_app->t->relayout = g_app->dirty = true;
    }
    if (p && p->gen == l->gen && p->js)
        js_dispatch(p->js, l->n, r && r->status == 200 && r->body ? "load" : "error", "Event", false, false, 0, 0, 0, NULL);
    node_release(l->n);
    free(l);
}
typedef struct { uint64_t gen; char *u; Image *im; } ImgLoad;
/* bitmaps decode on the fetching thread, off the main loop; SVG decodes in img_done */
static void img_work(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq; ImgLoad *l = ud;
    if (r->status == 200 && r->body) l->im = image_decode_bitmap((const uint8_t *)r->body, r->body_len);
}
static void img_done(NetRequest *rq, NetResponse *r, void *ud) {
    (void)rq; ImgLoad *l = ud;
    g_img_epoch++;
    Image *im = l->im ? l->im : r && r->status == 200 && r->body ? image_decode((const uint8_t *)r->body, r->body_len) : NULL;
    if (getenv("LUMEN_DEBUG_IMG")) fprintf(stderr, "lumen: img decode -> %s\n", im ? "ok" : "NULL");
    SDL_LockMutex(icache_mu);
    ImgSlot *slot = hm_get(&icache, l->u);
    if (slot && !slot->im && im) { g_icache_bytes += (size_t)im->w * (size_t)im->h * 4; g_icache_n++; }
    if (slot && !slot->im) { slot->im = im; im = NULL; }
    if (slot && slot->im && !slot->enc && g_lite_active && r->body_len < (8u << 20)) { slot->enc = xmalloc(r->body_len); memcpy(slot->enc, r->body, r->body_len); slot->enclen = r->body_len; }
    if (slot) { slot->done = true; slot->evicted = slot->refetch = false; }
    SDL_UnlockMutex(icache_mu);
    if (getenv("LUMEN_DEBUG_IMG")) fprintf(stderr, "lumen: img done status=%d len=%zu decoded=%d slot=%d %.80s\n", r ? r->status : -1, r && r->body ? r->body_len : 0, slot && slot->im ? 1 : 0, slot ? 1 : 0, l->u);
    if (im) image_unref(im);
    if (g_app->t->cur && g_app->t->cur->gen == l->gen) {
        g_app->t->cur->img_check = true; g_app->t->relayout = g_app->dirty = true;
    }
    if (g_loading_page && g_loading_page != g_app->t->cur && g_loading_page->js) js_load_recheck(g_loading_page->js);
    else if (!l->gen) { g_app->dirty = true; g_app->vonly = false; }
    free(l->u); free(l);
}
static void img_refetch(const char *u) {
    NetRequest *rq = net_request_new("GET", u); ImgLoad *l = xcalloc(1, sizeof *l); l->gen = 0; l->u = xstrdup(u);
    rq->done = img_done; rq->work = img_work; rq->ud = l; rq->priority = 2; net_fetch(rq);
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
/* Start async loads for <img> sources and CSS background images that appeared after the initial load. */
/* Walks the whole tree, so it runs only after image-relevant changes. */
static bool img_fetch_done(Node *n) {
    char sb[2048]; float dn; const char *src = img_src(n, sb, sizeof sb, &dn); if (!src || !*src || !n->doc) return false;
    char *u = url_join(doc_base(n->doc), src); if (!u) return false;
    SDL_LockMutex(icache_mu); ImgSlot *s = hm_get(&icache, u); bool d = s && s->done; SDL_UnlockMutex(icache_mu);
    free(u); return d;
}
/* the current page's load event waits for its images (capped at 10 s) */
static bool h_load_blocked(JsCtx *c) {
    Page *p = g_loading_page;
    if (!p || p->js != c || !p->d || now_ms() - p->load_t0 >= 10000) return false;
    bool busy = false;
    SDL_LockMutex(icache_mu);
    for (Node *n = p->d->node.first; n && !busy; n = node_next_in_tree(n, &p->d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML || n->tag != A_img) continue;
        char sb[2048]; float dn; const char *sv = img_src(n, sb, sizeof sb, &dn); if (!sv || !*sv) continue;
        const char *lz = node_attr(n, "loading"); if (lz && !strcasecmp(lz, "lazy")) continue;
        char *u = url_join(doc_base(p->d), sv);
        ImgSlot *slot = u ? hm_get(&icache, u) : NULL;
        busy = slot && !slot->done;
        free(u);
    }
    SDL_UnlockMutex(icache_mu);
    return busy;
}
static bool sync_images(Page *p) {
    if (p->res_seen && p->seen_res == p->d->resource_version) return false;
    p->res_seen = true; p->seen_res = p->d->resource_version;
    if (p->d->ndimg) p->img_check = true;
    for (int di = -1; di < p->d->ndimg; di++)
    for (Node *n = di < 0 ? p->d->node.first : p->d->dimg[di]; n; n = di < 0 ? node_next_in_tree(n, &p->d->node) : NULL) {
        if (n->type != NODE_ELEMENT) continue;
        char *u = NULL;
        if (n->style && (n->style->mask_image || n->style->bg_image) && n->style->display != D_NONE) u = xstrdup(n->style->mask_image ? n->style->mask_image : n->style->bg_image);
        else if (n->ns == NS_HTML && n->tag == A_img) { char sb[2048]; float dn; const char *sv = img_src(n, sb, sizeof sb, &dn); if (!sv) continue; u = url_join(doc_base(p->d), sv); }
        else continue;
        if (!u || !*u || !strncmp(u, "blob:", 5)) { free(u); continue; }
        SDL_LockMutex(icache_mu);
        bool have = hm_get(&icache, u) != NULL;
        if (!have) { ImgSlot *slot = xcalloc(1, sizeof *slot); hm_put(&icache, u, slot); }
        SDL_UnlockMutex(icache_mu);
        if (!have) {
            if (getenv("LUMEN_DEBUG_IMG")) fprintf(stderr, "lumen: img fetch %.80s\n", u);
            NetRequest *rq = net_request_new("GET", u); ImgLoad *l = xcalloc(1, sizeof *l); l->gen = p->gen; l->u = u;
            { char ref[4200]; if (doc_referrer(p->d, u, n->tag == A_img ? node_attr(n, "referrerpolicy") : NULL, ref, sizeof ref)) headers_add(&rq->headers, "Referer", ref); }
            rq->done = img_done; rq->work = img_work; rq->ud = l; rq->priority = 2; net_fetch(rq);
        } else free(u);
    }
    return true;
}
/* Fire load/error once per (img node, resolved src) after its fetch completes. */
static void fire_img_events(Page *p) {
    if (!p || !p->js || !p->img_check) return;
    p->img_check = false;
    Node **ev = NULL; bool *ok = NULL; int ne = 0, cap = 0;
    for (int di = -1; di < p->d->ndimg; di++)
    for (Node *n = di < 0 ? p->d->node.first : p->d->dimg[di]; n; n = di < 0 ? node_next_in_tree(n, &p->d->node) : NULL) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML || n->tag != A_img) continue;
        char sb[2048]; float dn; const char *sv = img_src(n, sb, sizeof sb, &dn); if (!sv) continue;
        char *u = url_join(doc_base(p->d), sv); if (!u) continue;
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
    /* a detached image stays listed until its current src has fired (or it got connected) */
    int keep = 0;
    for (int i = 0; i < p->d->ndimg; i++) {
        Node *n = p->d->dimg[i]; bool pending = false;
        char sb[2048]; float dn; const char *sv = n->flags & NF_CONNECTED ? NULL : img_src(n, sb, sizeof sb, &dn);
        if (sv) {
            char *u = url_join(doc_base(p->d), sv);
            if (u) { char key[2048]; snprintf(key, sizeof key, "%p %s", (void *)n, u); pending = !hm_get(&p->img_fired, key); free(u); }
        }
        if (pending) p->d->dimg[keep++] = n; else node_release(n);
    }
    p->d->ndimg = keep;
    for (int i = 0; i < ne; i++) { js_dispatch(p->js, ev[i], ok[i] ? "load" : "error", "Event", false, false, 0, 0, 0, NULL); node_release(ev[i]); }
    free(ev); free(ok);
    if (g_loading_page == p && p->js) js_load_recheck(p->js);   /* after the images' own load events */
}
/* Reconcile engine sheets with <style>/<link rel=stylesheet> currently in the tree; seed only records them. */
static bool sync_sheets(Page *p, bool seed) {
    if (!seed && p->seen_sheet_ver == p->d->sheet_version) return false;
    p->seen_sheet_ver = p->d->sheet_version;
    SheetRef *cur = NULL; int nc = 0, cap = 0; bool changed = false;
    int n_tail = 0, n_full = 0, n_gone = 0; size_t parsed = 0;
    for (Node *n = p->d->node.first; n; n = node_next_deep(n, &p->d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML || (n->tag != A_style && !is_sheet_link(n) && !is_preload_link(n))) continue;
        char *t = n->tag == A_style ? node_text_content(n) : NULL;
        uint64_t h = t ? fnv(t) : fnv(node_attr(n, "href"));
        if (!t && is_sheet_link(n)) h ^= 0x9e3779b97f4a7c15ull;
        int k = 0; while (k < p->nsref && p->sref[k].n != n) k++;
        bool known = k < p->nsref;
        size_t len = t ? strlen(t) : 0, old = known ? p->sref[k].len : 0;
        if (!seed && known && t && old && len > old && p->sref[k].h != h && fnvn(t, old) == p->sref[k].h && css_rule_boundary(t, old)) {
            /* appended rules (CSS-in-JS text appends, insertRule at the end): parse only the new tail */
            StyleSheet *sh = css_parse_sheet(t + old, len - old, p->d->url, 1, &p->e->media); sh->owner = n; style_engine_add_sheet(p->e, sh); changed = true;
            n_tail++; parsed += len - old;
        } else if ((!seed && (!known || p->sref[k].h != h)) ||
            (seed && !known && is_preload_link(n) && !is_sheet_link(n))) {
            if (known) { style_engine_remove_owner(p->e, n); changed = true; }
            if (t) { StyleSheet *sh = css_parse_sheet(t, strlen(t), p->d->url, 1, &p->e->media); sh->owner = n; style_engine_add_sheet(p->e, sh); changed = true; n_full++; parsed += len; }
            else {
                char *u = url_join(doc_base(p->d), node_attr(n, "href"));
                if (u) { NetRequest *rq = net_request_new("GET", u); LinkLoad *l = xmalloc(sizeof *l); l->gen = p->gen; l->n = n; n->refcount++; rq->done = is_sheet_link(n) ? link_done : preload_done; rq->ud = l; net_fetch(rq); free(u); }
            }
        }
        free(t);
        if (nc == cap) { cap = cap ? cap * 2 : 16; cur = xrealloc(cur, sizeof *cur * (size_t)cap); }
        cur[nc++] = (SheetRef){ n, h, len };
    }
    if (!seed) for (int i = 0; i < p->nsref; i++) {
        int k = 0; while (k < nc && cur[k].n != p->sref[i].n) k++;
        if (k == nc) { style_engine_remove_owner(p->e, p->sref[i].n); changed = true; n_gone++; }
    }
    if (changed && g_load_trace_until > now_ms()) LOAD_TRACE("sheets tail=%d full=%d gone=%d parsed_kb=%zu refs=%d", n_tail, n_full, n_gone, parsed >> 10, nc);
    free(p->sref); p->sref = cur; p->nsref = nc;
    return changed;
}
static bool frame_restyle(Page *p) {
    if (p->d->dom_version == p->seen_ver) return false;
    p->seen_ver = p->d->dom_version;
    sync_sheets(p, false);
    style_recalc(p->e, &p->d->node, false); /* sheet changes restyle only what their rules can match */
    if (sync_images(p)) p->img_check = true;
    p->frelayout = true;
    return true;
}
static float frame_scale(Page *p) { return g_app->scale * tab_zoom(tab_of_doc(g_app, p->d)); }
static void frame_layout(Page *p) {
    if (page_viewport(p, p->fw, p->fh, frame_scale(p))) p->frelayout = true;
    if (!p->frelayout && p->L) return;
    if (!p->L) p->L = layout_new();
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
    float s = frame_scale(p); int pw = (int)ceilf(w * s), ph = (int)ceilf(h * s);
    if (p->cv.w != pw || p->cv.h != ph || p->cv.scale != s || !p->cv.px) { canvas_free(&p->cv); canvas_init(&p->cv, pw, ph, s); p->drawn_ver = ~0ull; }
    if (p->drawn_ver == p->d->dom_version && p->drawn_paint_ver == p->d->paint_version && p->drawn_epoch == g_img_epoch) return false;
    dl_clear(&p->fdl); dl_build(&p->fdl, p->L, 0, p->fsy, w, h);
    raster(&p->cv, &p->fdl, 0);   /* a frame's canvas is transparent: the page shows through unless its root or body has a background */
    p->drawn_ver = p->d->dom_version; p->drawn_paint_ver = p->d->paint_version; p->drawn_epoch = g_img_epoch;
    uint64_t content_version = p->img.content_version + 1;
    p->img = (Image){ .w = pw, .h = ph, .refs = 1 << 30, .px = p->cv.px, .scale = s, .content_version = content_version };
    /* The image pointer stays stable. Invalidate its enclosing frame's cached
     * raster too, without invalidating styles or layout. */
    if (p->frame_el->doc) p->frame_el->doc->paint_version++;
    doc_visual_changed();
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
/* first src entry FreeType can read: TrueType, OpenType, WOFF or WOFF2 (FreeType is built with
   brotli; Google serves icon fonts such as Material Icons only as WOFF2) */
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
        bool ok = !*fmt || strstr(fmt, "truetype") || strstr(fmt, "opentype") || strstr(fmt, "woff");
        if (ok && n) return u;
        free(u); p = nx;
    }
    return NULL;
}
static void sheet_fonts(StyleSheet *s) {
    for (int i = 0; i < s->font_faces.n; i++) {
        const char *b = s->font_faces.v[i];
        char *fam = ff_prop(b, "font-family"), *src = ff_prop(b, "src"), *wt = ff_prop(b, "font-weight"), *sty = ff_prop(b, "font-style");
        char *u = src ? ff_pick_src(src) : NULL;
        char *abs = u && fam && *fam ? url_join(s->base_url, u) : NULL;
        if (abs) {
            int w = 400;
            if (wt && str_ieq(wt, "bold")) w = 700;
            else if (wt && !strchr(wt, ' ') && atoi(wt) > 0) w = atoi(wt);
            bool it = sty && (!strncasecmp(sty, "italic", 6) || !strncasecmp(sty, "oblique", 7));
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
        NetRequest *rq = net_request_new("GET", u); rq->done = font_done; rq->ud = u; net_fetch(rq);
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
static void hf_viewport(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { Page *p = ud; *w = p->fw; *h = p->fh; *sx = 0; *sy = p->fsy; *dpr = frame_scale(p); }
static void hf_scroll_to(void *ud, float x, float y) { (void)x; Page *p = ud; p->fsy = y > 0 ? y : 0; p->drawn_ver = ~0ull; }
/* A hit on an inline <svg> box goes on to the shape under the point (layout x, y), per 'pointer-events' */
static Node *hit_node(Box *b, float x, float y) {
    if (b && b->node && b->node->type == NODE_ELEMENT && b->node->ns == NS_SVG && b->node->tag == A_svg) {
        float cx = b->x + b->b[3] + b->p[3], cy = b->y + b->b[0] + b->p[0];
        Node *s = svg_hit(b->node, x - cx, y - cy, b->w - (b->b[1] + b->b[3] + b->p[1] + b->p[3]), b->h - (b->b[0] + b->b[2] + b->p[0] + b->p[2]));
        if (s) return s;
    }
    return b ? b->node : NULL;
}
static Node *hf_hit(void *ud, float x, float y) { Page *p = ud; if (!p->L) return NULL; return hit_node(layout_hit(p->L, x, y + p->fsy, 0, p->fsy), x, y + p->fsy); }
static bool hf_caret(void *ud, float x, float y, Node **n, int *off) { Page *p = ud; return p->L && tsel_caret_point(p->L, x, y + p->fsy, p->fsy, n, off); }
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
    p->e->media.dpr = g_app->scale * tab_zoom(tab_of_doc(g_app, f->doc));
    for (Node *n = d->node.first; n; n = node_next_deep(n, &d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML) continue;
        if (n->tag == A_style) {
            char *t = node_text_content(n); StyleSheet *sh = css_parse_sheet(t, strlen(t), d->url, 1, &p->e->media); sh->owner = n; style_engine_add_sheet(p->e, sh); free(t);
        } else if (is_sheet_link(n)) {
            char *u = url_join(doc_base(d), node_attr(n, "href"));
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
    *ch = (JsHost){ .ud = p, .media = &p->e->media, .viewport = hf_viewport, .scroll_to = hf_scroll_to, .hit = hf_hit, .caret = hf_caret, .sync = hf_sync, .frame_open = h_frame_open, .frame_close = h_frame_close };
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
/* the target a link navigates: its own, else its document's <base target> */
static const char *link_target(Node *l) {
    const char *t = node_attr(l, "target");
    if (t || !l->doc) return t;
    for (Node *n = l->doc->node.first; n; n = node_next_in_tree(n, &l->doc->node))
        if (n->type == NODE_ELEMENT && n->tag == A_base && node_attr(n, "target")) return node_attr(n, "target");
    return NULL;
}
static void click_frame(App *a, Page *fp, float x, float y) {
    Box *b = layout_hit(fp->L, x, y + fp->fsy, 0, fp->fsy);
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
            const char *h = node_attr(n, "href"), *tg = link_target(n);
            if (!strncmp(h, "javascript:", 11) || h[0] == '#') return;
            char *u = url_join(fp->d->url, h);
            if (u && tg && str_ieq(tg, "_blank")) navigate(a, u, true);
            else if (u && !js_navigate_target(js, tg, u, NULL, 0, NULL)) js_frame_navigate(fp->frame_el, u);
            free(u); return;
        }
}
static void restyle(App *a) {
    Page *p = a->t->cur;
    if (!p) return;
    if (p->seen_paint_ver != p->d->paint_version) {
        p->seen_paint_ver = p->d->paint_version;
        if (a->t == (a->pan ? a->pan : a->tabs[a->ti])) { a->dirty = true; a->vonly = false; }
    }
    bool viewport_changed = page_viewport(p, page_width(a), page_height(a), page_scale(a));
    if (!viewport_changed && p->d->dom_version == p->seen_ver && !p->sheets_pending) return;
    p->seen_ver = p->d->dom_version;
    bool sheets = sync_sheets(p, false) | p->sheets_pending;
    p->sheets_pending = false;
    /* inset-only changes translate the current boxes instead of relaying out */
    p->e->allow_moves = p->L && !a->t->relayout && !viewport_changed && !sheets && p->d->layout_version == p->lay_ver && !g_no_paint_only;
    { double q = now_ms(); style_recalc(p->e, &p->d->node, false); g_tr += now_ms() - q; g_tr_n++; g_tr_slow += now_ms() - q >= 15; }
    p->e->allow_moves = false;
    if (sync_images(p)) p->img_check = true;
    if (viewport_changed || sheets || p->e->layout_dirty || p->d->layout_version != p->lay_ver || g_no_paint_only) a->t->relayout = true;
    for (int i = 0; i < p->e->nmoves && !a->t->relayout; i++)
        if (!layout_move(p->L, p->e->moves[i].node, p->e->moves[i].old)) a->t->relayout = true;
    p->e->nmoves = 0;
    p->e->layout_dirty = false; p->lay_ver = p->d->layout_version;
    if (a->t == (a->pan ? a->pan : a->tabs[a->ti])) a->dirty = true;
}
static void history_go(App *a, int d);
static void h_sync_in(void *ud, Document *d, bool layout) {
    App *a = ud; Page *p = a->t->cur;
    if (!p || p->d != d) return;
    restyle(a);
    if (!layout || !a->t->relayout) return;
    if (!p->L) p->L = layout_new();
    layout_run(p->L, p->d, page_width(a), page_height(a));
    a->t->relayout = false;
}
#ifdef __ANDROID__
/* The panning a touch on n may start: the touch-action of n and its ancestors up to the nearest
   scroll container combined (Google's map forbids both, so its drags pan the map, not the page). */
static unsigned touch_block_of(const Node *n) {
    unsigned block = 0;
    for (; n && n->type == NODE_ELEMENT; n = n->parent) {
        const ComputedStyle *s = n->style;
        if (!s) continue;
        block |= s->touch_block;
        if (s->overflow_x >= OV_SCROLL && s->overflow_x != OV_CLIP) break;
        if (s->overflow_y >= OV_SCROLL && s->overflow_y != OV_CLIP) break;
    }
    return block;
}
/* The viewport origin of frame fp's content box, through its enclosing frames; false once it is gone. */
static bool frame_origin(App *a, Page *fp, float *ox, float *oy) {
    *ox = *oy = 0;
    for (int depth = 0; fp; depth++) {
        Box *fb = fp->frame_el ? fp->frame_el->box : NULL;
        if (depth > 8 || !fb || !(fp->frame_el->flags & NF_CONNECTED)) return false;
        Document *pd = fp->frame_el->doc;
        Page *parent = NULL;
        for (Page *q = g_frames; q; q = q->fnext) if (q->d == pd) { parent = q; break; }
        if (!parent && (!a->t->cur || a->t->cur->d != pd)) return false;
        *ox += fb->x + fb->b[3] + fb->p[3];
        *oy += fb->y + fb->b[0] + fb->p[0] - (parent ? parent->fsy : a->t->sy);
        fp = parent;
    }
    return true;
}
static unsigned android_dom_touch(App *a, const char *phase, const SDL_Event *event) {
    Page *p = a->t->cur;
    bool start = !strcmp(phase, "start");
    if (start) a->touch_block = 0;
    if (!strcmp(phase, "reset")) touch_frames_reset();
    if (!p || !p->js || a->t->asleep) return 0;
    Node *target = NULL;
    float x = event ? page_x(a, event->tfinger.x * a->vw) : 0;
    float y = event ? page_y(a, event->tfinger.y * a->vh) : 0;
    uint64_t device = event ? event->tfinger.touchID : 0, finger = event ? event->tfinger.fingerID : 0;
    JsCtx *js = p->js; Document *d = p->d;
    Page *fp = NULL; int in = -1;
    for (int i = 0; event && !start && i < g_touch_nin; i++)
        if (g_touch_in[i].device == device && g_touch_in[i].finger == finger) { in = i; fp = g_touch_in[i].fp; break; }
    if (start) {
        if (x <= 0 || y <= 0 || g_hud_pop || a->t->info) return 0;
        h_sync_in(a, p->d, true);
        Box *b = p->L ? layout_hit(p->L, x, y + a->t->sy, 0, a->t->sy) : NULL;
        target = hit_node(b, x, y + a->t->sy);
        while (target && target->type != NODE_ELEMENT) target = target->parent;
        unsigned block = 0;
        /* a touch on an iframe goes to the element under it in the frame's document, in its coordinates */
        for (int depth = 0; target && target->tag == A_iframe && depth < 8; depth++) {
            Page *f = frame_find(target);
            float ox, oy;
            if (!f || !f->L || !f->d || !js_frame_ctx(target) || !frame_origin(a, f, &ox, &oy)) return 0;
            block |= touch_block_of(target);
            fp = f;
            b = layout_hit(f->L, x - ox, y - oy + f->fsy, 0, f->fsy);
            target = b ? b->node : NULL;
            while (target && target->type != NODE_ELEMENT) target = target->parent;
        }
        a->touch_block = block | touch_block_of(target);
        if (fp && target && g_touch_nin < (int)(sizeof g_touch_in / sizeof *g_touch_in)) {
            in = g_touch_nin++;
            g_touch_in[in].device = device; g_touch_in[in].finger = finger; g_touch_in[in].fp = fp;
        }
        else if (fp) return 0;
    }
    if (fp) {
        float ox, oy;
        if (!frame_alive(fp) || !fp->frame_el || !(js = js_frame_ctx(fp->frame_el)) || !frame_origin(a, fp, &ox, &oy)) {
            if (in >= 0) g_touch_in[in] = g_touch_in[--g_touch_nin];
            return 0;
        }
        x -= ox; y -= oy; d = fp->d;
        a->dirty = true; a->vonly = false;   /* the frame's raster follows its document */
    }
    uint64_t version = d->dom_version;
    unsigned flags = js_touch(js, target, phase, device, finger, x, y, event ? event->tfinger.pressure : 0);
    if (fp && in >= 0 && in < g_touch_nin && (!strcmp(phase, "end") || !strcmp(phase, "cancel")))
        g_touch_in[in] = g_touch_in[--g_touch_nin];
    if (!fp && a->t->cur == p && version != p->d->dom_version) { a->vonly = false; restyle(a); }
    return flags;
}
#endif
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
static void h_fullscreen_in(void *ud, bool on) { (void)ud; App *a = g_app; page_fs_set(a, on && a->t->cur ? a->t->cur : NULL); }
static int h_history_len_in(void *ud) { (void)ud; return g_app->t->nhist; }
static void h_viewport_in(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { (void)ud; *w = page_width(g_app); *h = page_height(g_app); *sx = 0; *sy = LCLAMP(g_app->t->sy, 0, max_scroll(g_app)); *dpr = page_scale(g_app); }
static void h_scroll_to_in(void *ud, float x, float y) {
    (void)ud; (void)x;
    App *a = g_app; Page *p = a->t->cur;
    if (!p) return;
    h_sync_in(a, p->d, true);
    float next = LCLAMP(isfinite(y) ? y : 0, 0, max_scroll(a));
    if (a->t->sy == next) return;
    a->t->sy = next; a->dirty = true;
}
static Node *h_hit_in(void *ud, float x, float y) { (void)ud; Page *p = g_app->t->cur; if (!p || !p->L) return NULL; return hit_node(layout_hit(p->L, x, y + g_app->t->sy, 0, g_app->t->sy), x, y + g_app->t->sy); }
static void h_sync(void *ud, Document *d, bool layout) { TAB_CALL(ud, h_sync_in(g_app, d, layout)); }
#if defined(__ANDROID__) && !defined(NDEBUG)
/* Local developer diagnostic: geometry/CSS only, never page text, URLs or form values. */
static void debug_layout_geometry(App *a) {
    Page *p = a->t->cur;
    if (!p) return;
    h_sync_in(a, p->d, true);
    p->e->debug_style_passes = 80;
    p->e->doc->debug_mutations = 600;
    char path[1200]; pref_path(path, sizeof path, "layout-geometry.txt");
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "viewport %.1f %.1f document %.1f scroll %.1f\n", page_width(a), page_height(a), p->L->doc_h, a->t->sy);
    for (Node *n = p->d->node.first; n; n = node_next_deep(n, &p->d->node)) {
        Box *b = n->box; const ComputedStyle *s = n->style;
        if (n->type != NODE_ELEMENT || !b || !s) continue;
        fprintf(f, "%p parent=%p <%s> class=%s box=%.1f,%.1f,%.1f,%.1f scroll=%.1f/%.1f scroll_x=%.1f/%.1f scroller=%d display=%d pos=%d overflow=%d,%d flex=%d,%.1f,%.1f height=%d:%.1f:%.1f min=%d:%.1f:%.1f max=%d:%.1f:%.1f\n",
            (void *)n, (void *)n->parent, n->tag, n->class_name ? n->class_name : "", b->x,b->y,b->w,b->h,n->scroll_y,b->scroll_h,n->scroll_x,b->scroll_w,b->scroller,s->display,s->position,s->overflow_x,s->overflow_y,s->flex_direction,s->flex_grow,s->flex_shrink,s->height.kind,s->height.px,s->height.pct,s->min_height.kind,s->min_height.px,s->min_height.pct,s->max_height.kind,s->max_height.px,s->max_height.pct);
    }
    fclose(f);
}
#endif
static Tab *tab_of_wid(App *a, uint32_t wid) { for (int i = 0; i < a->ntabs; i++) if (wid && a->tabs[i]->wid == wid) return a->tabs[i]; return NULL; }
/* window.open from the calling tab: the new tab loads now and shows once the script returns (popup_service) */
static uint32_t h_open_window_in(const char *u, const char *name, uint32_t opener) {
    App *a = g_app; Tab *o = a->t, *t = NULL;
    if (!o || a->pan) return 0;
    if (name && *name) for (int i = 0; i < a->ntabs && !t; i++) if (a->tabs[i]->wname && !strcmp(a->tabs[i]->wname, name) && !a->tabs[i]->close_req) t = a->tabs[i];
    if (!t) {
        if (!(t = tab_new(a))) return 0;
        t->ws = o->ws; t->scripted = true; t->wname = name && *name ? xstrdup(name) : NULL; publish_gens(a);
    }
    t->opener = opener; t->opener_wid = o->wid;
    a->t = t; navigate(a, u && *u ? u : "about:blank", true); a->t = o;
    g_show_wid = t->wid;
    return t->wid;
}
static int h_window_op_in(uint32_t wid, int op, const char *arg) {
    App *a = g_app; Tab *t = op == 4 ? a->t : tab_of_wid(a, wid);
    if (op == 0) return !t || t->close_req;
    if (!t || t->close_req) return 0;
    if (op == 1 || op == 4) { if (t->scripted) { t->close_req = true; a->dirty = true; } return 0; }   /* pages close only the tabs scripts opened */
    if (op == 2 && arg && *arg) { Tab *o = a->t; a->t = t; navigate(a, arg, true); a->t = o; return 0; }
    if (op == 3) g_show_wid = t->wid;
    return 0;
}
static uint32_t h_open_window(void *ud, const char *u, const char *name, uint32_t opener) { uint32_t r = 0; TAB_CALL(ud, r = h_open_window_in(u, name, opener)); return r; }
static int h_window_op(void *ud, uint32_t wid, int op, const char *arg) { int r = 0; TAB_CALL(ud, r = h_window_op_in(wid, op, arg)); return r; }
static void h_navigate(void *ud, const char *u) { TAB_CALL(ud, h_navigate_in(ud, u)); }
static void h_set_url(void *ud, const char *u, bool push) { TAB_CALL(ud, h_set_url_in(ud, u, push)); }
static void h_navigate_post(void *ud, const char *u, const char *body, size_t len, const char *ctype) { TAB_CALL(ud, h_navigate_post_in(ud, u, body, len, ctype)); }
static void h_history_go(void *ud, int d) { TAB_CALL(ud, h_history_go_in(ud, d)); }
static void h_fullscreen(void *ud, bool on) { TAB_CALL(ud, h_fullscreen_in(ud, on)); }
static int h_history_len(void *ud) { int r = 0; TAB_CALL(ud, r = h_history_len_in(ud)); return r; }
static char *h_internal(void *ud, const char *cmd) { char *r = NULL; TAB_CALL(ud, r = h_internal_in(cmd)); return r; }
static void h_viewport(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { TAB_CALL(ud, h_viewport_in(ud, w, h, sx, sy, dpr)); }
static void h_scroll_to(void *ud, float x, float y) { TAB_CALL(ud, h_scroll_to_in(ud, x, y)); }
static Node *h_hit(void *ud, float x, float y) { Node *r = NULL; TAB_CALL(ud, r = h_hit_in(ud, x, y)); return r; }
static bool h_caret_in(float x, float y, Node **n, int *off) { Page *p = g_app->t->cur; return p && p->L && tsel_caret_point(p->L, x, y + g_app->t->sy, g_app->t->sy, n, off); }
static bool h_caret(void *ud, float x, float y, Node **n, int *off) { bool r = false; TAB_CALL(ud, r = h_caret_in(x, y, n, off)); return r; }
static void page_start_js(App *a, Page *p) {
    if (getenv("LUMEN_NO_JS")) return;
    JsHost h = { a->t, &p->e->media, h_navigate, h_set_url, h_history_go, h_viewport, h_scroll_to, h_hit, h_history_len, h_navigate_post, h_sync, h_frame_open, h_frame_close,
                 p->internal_ui || env_hook(ENV_TESTDRIVER) ? h_internal : NULL, h_fullscreen, p->internal_ui ? NULL : h_open_window, h_window_op, h_caret };
    double t0 = now_ms();
    sync_sheets(p, true);
    /* the stylesheets the loader fetched before any script ran: their load (or error) events come after
       DOMContentLoaded (x.com starts its app from a capturing listener for its sheet's load) */
    Node **links = NULL; bool *lok = NULL; int nlinks = 0;
    for (Node *n = p->d->node.first; n; n = node_next_deep(n, &p->d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML || !is_sheet_link(n)) continue;
        bool ok = false; for (int i = 0; i < p->e->sheets.n && !ok; i++) ok = p->e->sheets.v[i]->owner == n;
        links = xrealloc(links, sizeof *links * (size_t)(nlinks + 1)); lok = xrealloc(lok, sizeof *lok * (size_t)(nlinks + 1));
        n->refcount++; links[nlinks] = n; lok[nlinks++] = ok;
    }
    p->js = js_new(p->d, &h);
    js_set_window(p->js, a->t->wid, a->t->opener);
    LOAD_TRACE("js_new_ms=%.0f", now_ms() - t0);
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
            double ts = now_ms(); size_t len = sc->len;
            if (sc->module) js_run_module(p->js, sc->n, sc->src, sc->len, sc->name); else js_run_script(p->js, sc->n, sc->src, sc->len, sc->name);
            free(sc->src); sc->src = NULL; sc->len = 0;
            if (now_ms() - ts >= 50) LOAD_TRACE("script=%d module=%d bytes=%zu ms=%.0f", i, sc->module, len, now_ms() - ts);
        }
        else { sc->n->flags |= NF_SCRIPT_STARTED; js_dispatch(p->js, sc->n, "error", "Event", false, false, 0, 0, 0, NULL); }
    }
    double t_dcl = now_ms();
    js_set_ready_state(p->js, 1);
    js_dispatch(p->js, &p->d->node, "DOMContentLoaded", "Event", true, false, 0, 0, 0, NULL);
    LOAD_TRACE("dcl_ms=%.0f", now_ms() - t_dcl);
    for (int i = 0; i < nlinks; i++) { if (links[i]->flags & NF_CONNECTED) js_dispatch(p->js, links[i], lok[i] ? "load" : "error", "Event", false, false, 0, 0, 0, NULL); node_release(links[i]); }
    free(links); free(lok);
    p->load_t0 = now_ms(); g_loading_page = p; sync_images(p);   /* parser-inserted images delay the load event */
    js_fire_load(p->js);   /* now, or once the page's iframes and images have loaded */
    fprintf(stderr, "lumen: ran %d scripts in %.0fms\n", p->nscripts, now_ms() - t0);
#if defined(__ANDROID__) && !defined(NDEBUG)
    __android_log_print(ANDROID_LOG_INFO, "LumenLoad", "scripts=%d run_ms=%.0f", p->nscripts, now_ms() - t0);   /* timing only, no URLs */
#endif
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
    const char *v = node_attr(n, "value"); return xstrdup(v ? v : "");
}
static int u8_u16_len(const char *s, int n) { int k = 0; for (int i = 0; i < n; i++) { unsigned char c = (unsigned char)s[i]; if ((c & 0xC0) != 0x80) k += c >= 0xF0 ? 2 : 1; } return k; }
/* native editing of <input>/<textarea> at its selection (Document.csel: anchor/focus codepoints of the value);
   a control that never had one keeps its caret at the end */
static bool word_byte(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }
static int u8_prev(const char *v, int i) { if (i > 0) i--; while (i > 0 && ((unsigned char)v[i] & 0xC0) == 0x80) i--; return i; }
static int u8_next(const char *v, int n, int i) { if (i < n) i++; while (i < n && ((unsigned char)v[i] & 0xC0) == 0x80) i++; return i; }
static void ctl_sel(Node *f, const char *v, int *an, int *fo) {
    const Document *d = f->doc; int n = u8_byte_cp(v, (int)strlen(v));
    if (d && d->csel_n == f) { *an = LCLAMP(d->csel[0], 0, n); *fo = LCLAMP(d->csel[1], 0, n); }
    else *an = *fo = n;
}
static void ctl_sel_set(App *a, Node *f, int an, int fo) {
    Document *d = f->doc;
    if (!d) return;
    d->csel_n = f; d->csel[0] = an; d->csel[1] = fo; d->paint_version++;
    a->caret_t = now_ms(); a->dirty = true; a->vonly = false;
}
/* the codepoint (or with word, the word) before (dir < 0) or after the caret */
static int ctl_step(const char *v, int cp, int dir, bool word) {
    int n = (int)strlen(v), i = u8_cp_byte(v, n, cp);
    if (dir < 0 && !word) i = u8_prev(v, i);
    else if (dir < 0) {
        while (i > 0 && !word_byte((unsigned char)v[u8_prev(v, i)])) i = u8_prev(v, i);
        while (i > 0 && word_byte((unsigned char)v[u8_prev(v, i)])) i = u8_prev(v, i);
    } else if (!word) i = u8_next(v, n, i);
    else {
        while (i < n && !word_byte((unsigned char)v[i])) i = u8_next(v, n, i);
        while (i < n && word_byte((unsigned char)v[i])) i = u8_next(v, n, i);
    }
    return u8_byte_cp(v, i);
}
/* the word around codepoint cp, else the character there */
static void text_word(const char *v, int cp, int *s, int *e) {
    int n = (int)strlen(v), i = u8_cp_byte(v, n, cp), lo = i, hi = i;
    while (lo > 0 && word_byte((unsigned char)v[u8_prev(v, lo)])) lo = u8_prev(v, lo);
    while (hi < n && word_byte((unsigned char)v[hi])) hi = u8_next(v, n, hi);
    if (lo == hi) { if (hi < n) hi = u8_next(v, n, hi); else lo = u8_prev(v, lo); }
    *s = u8_byte_cp(v, lo); *e = u8_byte_cp(v, hi);
}
/* the start (dir < 0) or end of cp's line */
static int text_line_edge(const char *v, int cp, int dir) {
    int n = (int)strlen(v), i = u8_cp_byte(v, n, cp);
    if (dir < 0) while (i > 0 && v[i - 1] != '\n') i--;
    else while (i < n && v[i] != '\n') i++;
    return u8_byte_cp(v, i);
}
static char *ctl_selected(Node *f) {
    char *v = ctl_value(f); int an, fo, n = (int)strlen(v); ctl_sel(f, v, &an, &fo);
    int s = u8_cp_byte(v, n, LMIN(an, fo)), e = u8_cp_byte(v, n, LMAX(an, fo));
    memmove(v, v + s, (size_t)(e - s)); v[e - s] = 0;
    return v;
}
/* replace the codepoints [lo, hi) with s and put the caret after it; an <input> drops line breaks, as in Chrome.
   beforeinput (inputType itype) may cancel it; the input event moves the control's OpaqueRanges first */
static void ctl_splice(App *a, Node *f, int lo, int hi, const char *s, const char *itype) {
    JsCtx *js = a->t->cur->js;
    bool ins = !strncmp(itype, "insert", 6);
    if (js && !js_dispatch_input(js, f, "beforeinput", itype, ins ? s : NULL, -1, -1, -1)) return;
    if (a->t->cur->js != js || !f->doc || f->doc->focus != f) return;
    char *v = ctl_value(f); int n = (int)strlen(v);
    if (lo > u8_byte_cp(v, n)) lo = u8_byte_cp(v, n);
    if (hi > u8_byte_cp(v, n)) hi = u8_byte_cp(v, n);
    int bl = u8_cp_byte(v, n, lo), bh = u8_cp_byte(v, n, hi);
    SB b; sb_init(&b); sb_put(&b, v, (size_t)bl);
    size_t k0 = b.n;
    for (const char *p = s; *p; p++) if (f->tag != A_input || (*p != '\n' && *p != '\r')) sb_putc(&b, *p);
    int caret = lo + (b.n > k0 ? u8_byte_cp(b.s + k0, (int)(b.n - k0)) : 0);
    int lo16 = u8_u16_len(v, bl), hi16 = lo16 + u8_u16_len(v + bl, bh - bl), n16 = u8_u16_len(b.s + k0, (int)(b.n - k0));
    sb_puts(&b, v + bh); free(v);
    ctl_sel_set(a, f, caret, caret);   /* before the input event, so its handlers see the new caret */
    free(f->value_override); f->value_override = sb_take(&b); doc_mark_state_dirty(f->doc, f, true);
    if (js) js_dispatch_input(js, f, "input", itype, ins ? s : NULL, lo16, hi16, n16);
    a->t->relayout = true; a->dirty = true;
}
static void ctl_insert(App *a, Node *f, const char *s, const char *itype) {
    char *v = ctl_value(f); int an, fo; ctl_sel(f, v, &an, &fo); free(v);
    ctl_splice(a, f, LMIN(an, fo), LMAX(an, fo), s, itype);
}
/* Backspace (dir < 0) / Delete: the selection, else the codepoint or word beside the caret */
static void ctl_delete(App *a, Node *f, int dir, bool word) {
    char *v = ctl_value(f); int an, fo; ctl_sel(f, v, &an, &fo);
    int lo = LMIN(an, fo), hi = LMAX(an, fo);
    if (lo == hi) { int t = ctl_step(v, lo, dir, word); if (dir < 0) lo = t; else hi = t; }
    free(v);
    if (lo != hi) ctl_splice(a, f, lo, hi, "", dir < 0 ? (word ? "deleteWordBackward" : "deleteContentBackward") : (word ? "deleteWordForward" : "deleteContentForward"));
}
/* caret movement keys; with shift the focus end moves and the anchor stays */
static void ctl_move(App *a, Node *f, SDL_Keycode k, bool shift, bool word) {
    char *v = ctl_value(f); int an, fo, n = u8_byte_cp(v, (int)strlen(v)); ctl_sel(f, v, &an, &fo);
    int to = fo;
    if ((k == SDLK_LEFT || k == SDLK_RIGHT) && an != fo && !shift) to = k == SDLK_LEFT ? LMIN(an, fo) : LMAX(an, fo);
    else if (k == SDLK_LEFT || k == SDLK_RIGHT) to = ctl_step(v, fo, k == SDLK_LEFT ? -1 : 1, word);
    else if (k == SDLK_HOME || k == SDLK_END) to = word || f->tag == A_input ? (k == SDLK_HOME ? 0 : n) : text_line_edge(v, fo, k == SDLK_HOME ? -1 : 1);
    else if ((k == SDLK_UP || k == SDLK_DOWN) && f->box) to = ctl_vmove(f->box, fo, k == SDLK_UP ? -1 : 1);
    free(v);
    ctl_sel_set(a, f, shift ? an : to, to);
}
static void ctl_select_all(App *a, Node *f) { char *v = ctl_value(f); ctl_sel_set(a, f, 0, u8_byte_cp(v, (int)strlen(v))); free(v); }
static void ctl_cut(App *a, Node *f) {
    char *t = ctl_selected(f);
    if (*t) { SDL_SetClipboardText(t); ctl_insert(a, f, "", "deleteByCut"); }
    free(t);
}
/* a paste: first the page's paste event (its DataTransfer has the clipboard's text and image); unless cancelled, the
   text goes into the control f or the editing host h. With neither, the event goes to the body. */
static void page_paste(App *a, Node *f, Node *h) {
    Page *p = a->t->cur; char *t = SDL_GetClipboardText();
    Node *target = f ? f : h ? h : p && p->d ? (p->d->body ? p->d->body : p->d->html) : NULL;
    bool go = !p || !p->js || !target || js_dispatch(p->js, target, "lumenpaste", "KeyboardEvent", false, true, 0, 0, 0, t ? t : "");
    if (go && t && *t) { if (f) ctl_insert(a, f, t, "insertFromPaste"); else if (h) js_dispatch(p->js, h, "lumenedit", "KeyboardEvent", false, false, 0, 0, 0, t); }
    SDL_free(t);
}
static void ctl_paste(App *a, Node *f) { page_paste(a, f, NULL); }

/* URL field editing at its selection (a->us: anchor/focus bytes of t->url) */
static void url_changed(App *a) { a->caret_t = now_ms(); a->dirty = true; a->vonly = false; }
static void url_begin(App *a) { if (!a->editing) { a->editing = true; a->uscroll = 0; SDL_StartTextInput(a->win); } url_changed(a); }
static void url_cancel(App *a) {
    a->editing = a->umenu = false; SDL_StopTextInput(a->win);
    if (a->t->hpos >= 0) snprintf(a->t->url, sizeof a->t->url, "%s", a->t->cur ? a->t->cur->url : a->t->hist[a->t->hpos]);
    url_changed(a);
}
static void url_select_all(App *a) { a->us[0] = 0; a->us[1] = (int)strlen(a->t->url); url_changed(a); }
static char *url_selected(App *a) {
    url_sel_clamp(a);
    int lo = LMIN(a->us[0], a->us[1]), hi = LMAX(a->us[0], a->us[1]);
    char *r = xmalloc((size_t)(hi - lo) + 1); memcpy(r, a->t->url + lo, (size_t)(hi - lo)); r[hi - lo] = 0;
    return r;
}
/* replace the selection with s on one line, caret after it; what does not fit the buffer is dropped */
static void url_insert(App *a, const char *s) {
    url_sel_clamp(a);
    char *u = a->t->url; size_t cap = sizeof a->t->url;
    int lo = LMIN(a->us[0], a->us[1]), hi = LMAX(a->us[0], a->us[1]);
    SB b; sb_init(&b); sb_put(&b, u, (size_t)lo);
    for (const char *p = s; *p; p++) if (*p != '\n' && *p != '\r') sb_putc(&b, *p == '\t' ? ' ' : *p);
    size_t caret = b.n; sb_puts(&b, u + hi);
    size_t n = b.n;
    if (n >= cap) { n = cap - 1; while (n && ((unsigned char)b.s[n] & 0xC0) == 0x80) n--; }
    memcpy(u, b.s, n); u[n] = 0; sb_free(&b);
    a->us[0] = a->us[1] = (int)LMIN(caret, n);
    url_changed(a);
}
static int url_step(const char *u, int i, int dir, bool word) { return u8_cp_byte(u, (int)strlen(u), ctl_step(u, u8_byte_cp(u, i), dir, word)); }
static void url_delete(App *a, int dir, bool word) {
    url_sel_clamp(a);
    if (a->us[0] == a->us[1]) a->us[1] = url_step(a->t->url, a->us[1], dir, word);
    url_insert(a, "");
}
static void url_move(App *a, SDL_Keycode k, bool shift, bool word) {
    url_sel_clamp(a);
    int n = (int)strlen(a->t->url), lo = LMIN(a->us[0], a->us[1]), hi = LMAX(a->us[0], a->us[1]), to = a->us[1];
    if ((k == SDLK_LEFT || k == SDLK_RIGHT) && lo != hi && !shift) to = k == SDLK_LEFT ? lo : hi;
    else if (k == SDLK_LEFT || k == SDLK_RIGHT) to = url_step(a->t->url, to, k == SDLK_LEFT ? -1 : 1, word);
    else if (k == SDLK_HOME || k == SDLK_UP) to = 0;
    else if (k == SDLK_END || k == SDLK_DOWN) to = n;
    a->us[1] = to; if (!shift) a->us[0] = to;
    url_changed(a);
}
static void url_word(App *a, int o) {
    const char *u = a->t->url; int n = (int)strlen(u), s, e;
    text_word(u, u8_byte_cp(u, o), &s, &e);
    a->us[0] = u8_cp_byte(u, n, s); a->us[1] = u8_cp_byte(u, n, e);
    url_changed(a);
}
/* the byte of t->url under window x; before editing the field shows it without its scheme */
static int url_at(App *a, float x) {
    const char *u = a->t->url, *shown = u;
    if (!a->editing) { const char *p = strstr(u, "://"); if (p) shown = p + 3; }
    return (int)(shown - u) + text_hit_x(a->ui, shown, x - (url_x(a) + 16) + a->uscroll);
}
static bool g_upress;   /* mouse selecting in the URL field */
/* first press selects the whole URL (on Android with a Copy/Paste/Share menu); a press in the editing field places the caret */
static void url_press(App *a, float x, int clicks) {
    if (!a->editing) {
        url_begin(a); url_select_all(a);
#ifdef __ANDROID__
        a->umenu = true;
#endif
        return;
    }
    int o = url_at(a, x);
    if (clicks >= 3) url_select_all(a);
    else if (clicks == 2) url_word(a, o);
    else { a->us[0] = a->us[1] = o; g_upress = true; url_changed(a); }
    a->umenu = false;
}
static void url_drag(App *a, float x) { int o = url_at(a, x); if (o != a->us[1]) { a->us[1] = o; url_changed(a); } }
static void focus_node(App *a, Node *n) {
    Document *d = a->t->cur->d; Node *old = d->focus; JsCtx *js = a->t->cur->js;
#ifdef __ANDROID__
    /* Show keyboard: taps elsewhere keep the text field the keyboard types into; another field becomes it */
    if (g_kbd_sticky && old && (is_text_ctl(old) || edit_host(old)) && !(n && (is_text_ctl(n) || edit_host(n)))) return;
    a->focus_reveal = true;
    a->dirty = true;
    if (old == n && !g_tap_kbd && (is_text_ctl(n) || edit_host(n))) SDL_StartTextInput(a->win);   /* else kbd_tap decides */
#endif
    if (old == n) return;
    doc_set_focus(d, n);
    if (js && old) { js_dispatch(js, old, "blur", "FocusEvent", false, false, 0, 0, 0, NULL); js_dispatch(js, old, "focusout", "FocusEvent", true, false, 0, 0, 0, NULL); }
    if (js && n) { js_dispatch(js, n, "focus", "FocusEvent", false, false, 0, 0, 0, NULL); js_dispatch(js, n, "focusin", "FocusEvent", true, false, 0, 0, 0, NULL); }
#ifdef __ANDROID__
    if (!g_tap_kbd && (is_text_ctl(n) || edit_host(n))) SDL_StartTextInput(a->win);
#else
    if (is_text_ctl(n) || edit_host(n)) SDL_StartTextInput(a->win);
#endif
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
/* the form's default button: its first submit button in tree order */
static Node *form_default_button(Node *f) {
    for (Node *n = f->first; n; n = node_next_in_tree(n, f)) {
        if (n->type != NODE_ELEMENT) continue;
        const char *ty = node_attr(n, "type");
        if (n->tag == A_button && (!ty || str_ieq(ty, "submit"))) return n;
        if (n->tag == A_input && ty && (str_ieq(ty, "submit") || str_ieq(ty, "image"))) return n;
    }
    return NULL;
}
/* Enter in a text field: implicit submission. With scripts, a click on the default button (or
   requestSubmit without one), so the page's handlers, the submitter and POST forms work as in Chrome. */
static void submit_form(App *a, Node *ctl) {
    Node *f = ctl;
    while (f && !(f->type == NODE_ELEMENT && f->tag == A_form)) f = f->parent;
    if (!f) return;
    JsCtx *js = a->t->cur->js;
    if (js) {
        Node *b = form_default_button(f);
        if (b && node_has_attr(b, "disabled")) return;
        if (b) js_dispatch(js, b, "click", "MouseEvent", true, true, 0, 0, 0, NULL);
        else js_eval(js, "{const e=document.activeElement,f=e&&e.form;if(f)f.requestSubmit()}", "lumen:submit");
        return;
    }
    const char *m = node_attr(f, "method");
    bool post = m && str_ieq(m, "post");
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
    if (post) {
        char *hm = strchr(u, '#'); if (hm) *hm = 0;
        navigate_ex(a, u, true, q.n ? q.s : "", q.n, "application/x-www-form-urlencoded");
        free(u); sb_free(&q); return;
    }
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
    if (f & 1) doc_mark_state_dirty(p->d, n, false); /* descendants: f & 2 */
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
static bool youtube_controls_target(Page *page, Node *node) {
    if (!page || !is_youtube(page->url)) return false;
    for (; node; node = node->parent) {
        if (node->tag == A_video) return true;
        const char *id = node_attr(node, "id");
        if (id && (!strcmp(id, "movie_player") || !strcmp(id, "player-container-id"))) return true;
    }
    return false;
}
static void page_move(App *a, float x, float y) {   /* pointer moves for page scripts (e.g. YouTube shows its controls on mousemove) */
    static double last; static Node *prev; double tn = now_ms();
    Page *p = a->t->cur; if (!p || !p->L) return;
    Node *n = hit_node(layout_hit(p->L, x, y + a->t->sy, 0, a->t->sy), x, y + a->t->sy);
    while (n && n->type != NODE_ELEMENT) n = n->parent;
    if (!n) n = p->d->html;
    if (page_hover(p, n)) restyle(a);
    if (!n || !p->js || tn - last < 30) return;
    last = tn;
    g_mv_p = youtube_controls_target(p, n) ? p : NULL;
    if (g_mv_p) { g_mv_t = g_mv_rep = tn; g_mv_x = x; g_mv_y = y; }
    if (n != prev) { prev = n; js_dispatch(p->js, n, "pointerover", "PointerEvent", true, true, x, y, 0, NULL); js_dispatch(p->js, n, "mouseover", "MouseEvent", true, true, x, y, 0, NULL); }
    js_dispatch(p->js, n, "pointermove", "PointerEvent", true, true, x, y, 0, NULL);
    js_dispatch(p->js, n, "mousemove", "MouseEvent", true, true, x, y, 0, NULL);
}
#ifdef __ANDROID__
static void page_touch_controls(App *a, float x, float y) {
    Page *p = a->t->cur;
    g_mv_p = NULL;
    if (!p || !p->L || !p->js || x < 0 || y < 0) return;
    Box *b = layout_hit(p->L, x, y + a->t->sy, 0, a->t->sy);
    if (!b || !youtube_controls_target(p, b->node)) return;
    /* The caller finishes compatibility clicks before changing visibility.
       Mobile YouTube hides the inner content, not the overlay itself. Do not
       cover ad controls or synthesize repeated mouse events for a touch.
       The page owns the expiry timer, which is destroyed on navigation. */
    js_eval(p->js,
        "setTimeout(function(){if(!document.getElementById('player-control-overlay'))return;"
        "var s=document.getElementById('__lumen_touch_vctl');"
        "if(!s){s=document.createElement('style');s.id='__lumen_touch_vctl';"
        "s.textContent='#player-container-id:not(:has(.ad-showing)) #player-control-overlay .player-controls-content{opacity:1!important;visibility:visible!important}';"
        "(document.head||document.documentElement).appendChild(s)}"
        "clearTimeout(s.__lumenTimer);s.__lumenTimer=setTimeout(function(){s.remove()},8000)},0)",
        "lumen:touch-controls");
}
#endif
static void page_move_keep(App *a) {   /* YouTube hides its controls ~3 s after the pointer stops; keep them up for 8 s */
    double tn = now_ms(); Page *p = g_mv_p;
    if (!p || tn - g_mv_t > 8000) { g_mv_p = NULL; return; }
    if (tn - g_mv_rep < 1000) return;
    g_mv_rep = tn;
    if (a->t->cur != p || !p->js || !p->L) { g_mv_p = NULL; return; }
    Box *b = layout_hit(p->L, g_mv_x, g_mv_y + a->t->sy, 0, a->t->sy); Node *n = b ? b->node : NULL;
    while (n && n->type != NODE_ELEMENT) n = n->parent;
    if (!youtube_controls_target(p, n)) { g_mv_p = NULL; return; }
    float j = ((long)(tn / 1000) & 1) ? 1 : 0;   /* YouTube ignores moves to the same spot */
    if (n) js_dispatch(p->js, n, "mousemove", "MouseEvent", true, true, g_mv_x + j, g_mv_y, 0, NULL);
}
#define VBH 34.f
static bool vbar_on(App *a, Node *n) {
    if (n->type != NODE_ELEMENT || !n->box || n->box->w < 120) return false;
    if (n->tag == A_audio) return node_attr(n, "controls") != NULL;
    return n->tag == A_video && (node_attr(n, "controls") || (vctl_on(a->t) && !is_youtube(a->t->url)));
}
/* under the video with "always show video controls", except in Lumen's own pages (the file chooser's
   preview), which leave no room for it; over its bottom otherwise */
static float vbar_y(App *a, const Box *b) {
    if (b->node && b->node->tag == A_audio) return b->y + LMAX(0, (b->h - VBH) / 2);
    return vctl_on(a->t) && !a->t->cur->internal_ui ? b->y + b->h : b->y + b->h - VBH;
}
static bool box_in_fixed(const Box *b) { for (; b; b = b->parent) if (b->fixed) return true; return false; }   /* placed in the viewport, not the page */
static void fmt_t(char *o, size_t n, double s) { int t = s >= 0 && s < 1e7 ? (int)s : 0; if (t >= 3600) snprintf(o, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60); else snprintf(o, n, "%d:%02d", t / 60, t % 60); }
static void vbar_layout(App *a, const Box *b, const MpState *s, char *tm, size_t n, float *tx0, float *tx1) {
    char c[16], d[16]; fmt_t(c, sizeof c, s->time); fmt_t(d, sizeof d, s->duration); snprintf(tm, n, "%s / %s", c, d);
    *tx0 = b->x + 44; *tx1 = b->x + b->w - text_width(a->ui, tm, strlen(tm), 0) - 26;
}
static bool video_bars(App *a, DisplayList *dl) {   /* control bar under each <video controls> (or every video when "always show video controls" is on) */
    bool playing = false; a->vbar_any = false; a->vbar_msv = media_state_version(); Document *d = a->t->cur->d; float rb = (a->ui->ascent - a->ui->descent) / 2;
    a->vbr[0] = a->vbr[1] = 1e9f; a->vbr[2] = a->vbr[3] = -1e9f;
    for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) {
        if (!vbar_on(a, n)) continue;
        Box *b = n->box; float y = vbar_y(a, b) - (box_in_fixed(b) ? 0 : a->t->sy);
        if (y < -VBH || y > page_height(a)) continue;
        MpState s; memset(&s, 0, sizeof s); if (!media_state_for(n, &s)) s.paused = true;
        playing |= !s.paused; a->vbar_any = true;
        char tm[48]; float tx0, tx1; vbar_layout(a, b, &s, tm, sizeof tm, &tx0, &tx1);
        push_rect(dl, b->x, y, b->w, VBH, 0, RGBA(32, 33, 36, 240));
        a->vbr[0] = LMIN(a->vbr[0], b->x); a->vbr[1] = LMIN(a->vbr[1], y); a->vbr[2] = LMAX(a->vbr[2], b->x + b->w); a->vbr[3] = LMAX(a->vbr[3], y + VBH);
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
    for (Node *n = p->d->node.first; n; n = node_next_in_tree(n, &p->d->node)) {
        if (!vbar_on(a, n)) continue;
        Box *b = n->box; float by = vbar_y(a, b) + (box_in_fixed(b) ? a->t->sy : 0);
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
static Box *sel_scope(Layout *L, float x, float y, float scroll_y) {
    Box *b = layout_hit(L, x, y, 0, scroll_y);
    while (b && (b->kind == BX_INLINE || b->kind == BX_TEXT)) b = b->parent;
    return b;
}
static void sel_press(App *a, float x, float y, int clicks) {
    bool had = g_tsel.on; g_tsel.on = false; g_press = g_drag = false;
    Page *p = a->t->cur;
    if (p && p->L && !a->t->asleep) {
        float px = page_x(a, x), py = page_y(a, y) + a->t->sy;
        Box *b = layout_hit(p->L, px, py, 0, a->t->sy);
        bool ok = !(b && b->st && b->st->user_select);
        for (Node *k = b ? b->node : NULL; ok && k; k = k->parent) if (is_text_ctl(k)) ok = false;
        if (ok && b && edit_host(b->node)) ok = false;
        if (ok) {
            g_press = true; g_px = px; g_py = py;
            Node *tn; int off;
            if (clicks >= 2 && tsel_point(p->L, px, py, a->t->sy, sel_scope(p->L, px, py, a->t->sy), &tn, &off)) {
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
    float px = page_x(a, x), py = page_y(a, y) + a->t->sy;
    if (!g_drag) {
        if (fabsf(px - g_px) + fabsf(py - g_py) < 4) return;
        Node *an; int ao;
        if (!tsel_point(p->L, g_px, g_py, a->t->sy, sel_scope(p->L, g_px, g_py, a->t->sy), &an, &ao)) { g_press = false; return; }
        g_drag = true; g_tsel = (TextSel){ false, p->L, an, an, ao, ao };
    }
    Node *fn; int fo;
    if (!tsel_point(p->L, px, py, a->t->sy, NULL, &fn, &fo)) return;
    if (fn != g_tsel.fn || fo != g_tsel.fo || !g_tsel.on) { g_tsel.fn = fn; g_tsel.fo = fo; g_tsel.on = fn != g_tsel.an || fo != g_tsel.ao; sel_dirty(a); }
}
static bool g_epress, g_cpress;
static void page_input_reset(void) {
    g_press = g_drag = g_epress = g_cpress = g_tsel.on = false;
    g_mv_p = NULL;
}
static bool edit_text_at(App *a, Node *h, float px, float py, Node **tn, int *off) {
    Page *p = a->t->cur;
    return h->box && tsel_point(p->L, px, py, a->t->sy, h->box, tn, off) && node_within(*tn, h);
}
static bool node_has_text(const Node *n) {
    if (n->type == NODE_TEXT) return n->text_len > 0;
    for (const Node *c = n->first; c; c = c->next) if (node_has_text(c)) return true;
    return false;
}
static Node *node_find_br(Node *n) {
    if (n->type == NODE_ELEMENT && n->tag && !strcmp(n->tag, "br")) return n;
    for (Node *c = n->first; c; c = c->next) { Node *r = node_find_br(c); if (r) return r; }
    return NULL;
}
/* a press on an empty line of editing host h (a block holding only a <br>, as editors make): the element the
   caret goes into at offset 0, the <br>'s parent as in Chrome; NULL when the press is on text */
static Node *edit_empty_line(Node *hit, Node *h) {
    if (!hit || hit == h || !node_within(hit, h) || node_has_text(hit)) return NULL;
    Node *c = hit;
    while (c->parent && c->parent != h && !node_has_text(c->parent)) c = c->parent;
    Node *br = node_find_br(c);
    return br ? br->parent : NULL;
}
static void edit_press(App *a, float x, float y, int clicks) {
    g_epress = false;
    Node *h = edit_focus(a); Page *p = a->t->cur;
    if (!h || !p || !p->L || a->t->asleep) return;
    float px = page_x(a, x), py = page_y(a, y) + a->t->sy;
    Box *b = layout_hit(p->L, px, py, 0, a->t->sy); Node *tn; int off;
    if (!b || edit_host(b->node) != edit_host(h)) return;
    Node *el = clicks == 1 ? edit_empty_line(b->node, h) : NULL;
    if (el) { tn = el; off = 0; }
    else if (!edit_text_at(a, h, px, py, &tn, &off)) return;
    js_dispatch(p->js, tn, clicks >= 3 ? "lumensel-all" : clicks == 2 ? "lumensel-word" : "lumensel-collapse", "MouseEvent", false, false, off, 0, 0, NULL);
    g_epress = clicks == 1;
    sel_dirty(a);
}
static void edit_drag(App *a, float x, float y) {
    static Node *ln; static int lo;
    Node *h = edit_focus(a), *tn; int off;
    if (!h || !a->t->cur || !a->t->cur->L || !edit_text_at(a, h, page_x(a, x), page_y(a, y) + a->t->sy, &tn, &off) || (tn == ln && off == lo)) return;
    ln = tn; lo = off;
    js_dispatch(a->t->cur->js, tn, "lumensel-extend", "MouseEvent", false, false, off, 0, 0, NULL);
    sel_dirty(a);
}
/* a press in the focused <input>/<textarea>: the caret goes under it, a double press selects the word, a triple all */
static void ctl_press(App *a, float x, float y, int clicks) {
    g_cpress = false;
    Node *f = page_focus(a); Page *p = a->t->cur;
    if (!f || !f->box || !p || !p->L || a->t->asleep || x < page_left(a) || y < BAR) return;
    float px = page_x(a, x), py = page_y(a, y) + a->t->sy;
    Box *b = layout_hit(p->L, px, py, 0, a->t->sy);
    if (!b || !b->node || !node_within(b->node, f)) return;
    int cp = ctl_hit(f->box, px, py, a->t->sy);
    if (clicks >= 3) { ctl_select_all(a, f); return; }
    if (clicks == 2) { char *v = ctl_value(f); int s, e; text_word(v, cp, &s, &e); free(v); ctl_sel_set(a, f, s, e); return; }
    ctl_sel_set(a, f, cp, cp);
    g_cpress = true;
}
static void ctl_drag(App *a, float x, float y) {
    Node *f = page_focus(a);
    if (!g_cpress || !f || !f->box || f->doc->csel_n != f) return;
    int cp = ctl_hit(f->box, page_x(a, x), page_y(a, y) + a->t->sy, a->t->sy);
    if (cp != f->doc->csel[1]) ctl_sel_set(a, f, f->doc->csel[0], cp);
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
static struct { unsigned char *img; size_t n; char *mime; } g_clip_pick;   /* a chosen history image: the next paste's */
static unsigned char *h_clip_image(char **mime, size_t *len) {
    if (g_clip_pick.img) {   /* a clipboard-history image chosen for this paste */
        unsigned char *r = g_clip_pick.img; *mime = g_clip_pick.mime; *len = g_clip_pick.n;
        g_clip_pick.img = NULL; g_clip_pick.mime = NULL; g_clip_pick.n = 0;
        return r;
    }
#ifdef __ANDROID__
    return android_clip_image(mime, len);
#else
    static const char *const types[] = { "image/png", "image/jpeg", "image/gif", "image/webp", "image/bmp" };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        if (!SDL_HasClipboardData(types[i])) continue;
        size_t n = 0; void *d = SDL_GetClipboardData(types[i], &n);
        if (d && n) { unsigned char *o = malloc(n); memcpy(o, d, n); SDL_free(d); *mime = strdup(types[i]); *len = n; return o; }
        SDL_free(d);
    }
    return NULL;
#endif
}
static void sel_copy(App *a) {
    Node *f = !a->editing ? page_focus(a) : NULL;
    if (a->editing) { char *s = url_selected(a); if (*s) SDL_SetClipboardText(s); free(s); return; }
    if (f) { char *s = ctl_selected(f); if (*s) SDL_SetClipboardText(s); free(s); return; }
    if (g_tsel.on && a->t->cur && a->t->cur->L == g_tsel.L) { char *s = tsel_text(a->t->cur->L); if (*s) SDL_SetClipboardText(s); free(s); }
}
#ifdef __ANDROID__
/* Touch text selection, as in Chrome: a long press selects the word under the
   finger (dragging on extends it), handles move either end, and a small bar
   offers Copy / Select all. */
enum { LONG_PRESS_MS = 500 };
/* what the touch selection is in: page text (g_tsel), the focused <input>/<textarea> (Document.csel),
   or the focused contenteditable (its JS selection, mirrored into g_tsel) */
enum { TK_PAGE, TK_CTL, TK_EDIT };
enum { BAR_MAX = 10, BAR_BW = 44, BAR_H = 44, POP_RH = 40, POP_MAX = 32, CLIP_MAX = 10 };
/* Settings: the edit bar's two slots, any two of Backspace, Enter, Undo, Clear and Hide (the others are in its
   More menu, with the text snippets; a snippet's trailing newline presses Enter after its text) */
static int g_bar_slot[2] = { TA_BKSP, TA_ENTER };
static void snip_defaults(void) {
    static const char *d[] = { "yes\n", "continue\n", "confirm\n", "include", "enter" };
    for (int i = 0; i < g_nsnip; i++) free(g_snip[i]);
    g_nsnip = 0;
    for (size_t i = 0; i < sizeof d / sizeof *d; i++) g_snip[g_nsnip++] = xstrdup(d[i]);
}
/* snippets as one line: '|' between them, '%', '|' and newlines percent-encoded */
static void snip_encode(char *out, size_t n) {
    size_t k = 0; out[0] = 0;
    for (int i = 0; i < g_nsnip && k + 4 < n; i++) {
        if (i) out[k++] = '|';
        for (const char *c = g_snip[i]; *c && k + 4 < n; c++)
            if (*c == '%' || *c == '|' || *c == '\n' || *c == '\r') k += (size_t)snprintf(out + k, n - k, "%%%02X", (unsigned char)*c);
            else out[k++] = *c;
    }
    out[k] = 0;
}
static void snip_decode(const char *v) {
    for (int i = 0; i < g_nsnip; i++) free(g_snip[i]);
    g_nsnip = 0;
    while (*v && g_nsnip < MAX_SNIP) {
        const char *e = strchr(v, '|'); size_t len = e ? (size_t)(e - v) : strlen(v);
        char *t = xmalloc(len + 1); size_t k = 0;
        for (size_t i = 0; i < len; i++) {
            unsigned x;
            if (v[i] == '%' && i + 2 < len + 1 && sscanf(v + i + 1, "%2x", &x) == 1) { t[k++] = (char)x; i += 2; } else t[k++] = v[i];
        }
        t[k] = 0;
        if (k) g_snip[g_nsnip++] = t; else free(t);
        if (!e) break;
        v = e + 1;
    }
}
static const char *ta_label(int act) {
    static const char *l[TA_NACT] = { "Cut", "Copy", "Paste", "Select all", "Keyboard", "Backspace", "Enter", "Undo", "Clear", "Hide menu", "More", "Redo" };
    return act >= 0 && act < TA_NACT ? l[act] : "";
}
static const char *ta_path(int act) {
    switch (act) {
    case TA_CUT: return "O 6.5 17.5 2.7 O 17.5 17.5 2.7 M8.4 15.4 L17 3.5 M15.6 15.4 L7 3.5";
    case TA_COPY: return "M9 8 L19 8 L19 20.5 L9 20.5 Z M6 16.5 L5 16.5 L5 3.5 L15 3.5 L15 5";
    case TA_PASTE: return "M6 5 L18 5 L18 21 L6 21 Z M9 3 L15 3 L15 7 L9 7 Z";
    case TA_ALL: return "M4 4 L7 4 M10.5 4 L13.5 4 M17 4 L20 4 L20 7 M20 10.5 L20 13.5 M20 17 L20 20 L17 20 M13.5 20 L10.5 20 M7 20 L4 20 L4 17 M4 13.5 L4 10.5 M4 7 L4 4 M8.5 8.5 L15.5 8.5 L15.5 15.5 L8.5 15.5 Z";
    case TA_KBD: return "M3 6 L21 6 L21 18 L3 18 Z M6.5 9.5 L7.5 9.5 M10 9.5 L11 9.5 M13 9.5 L14 9.5 M16.5 9.5 L17.5 9.5 M6.5 12.5 L7.5 12.5 M10 12.5 L11 12.5 M13 12.5 L14 12.5 M16.5 12.5 L17.5 12.5 M8 15.3 L16 15.3";
    case TA_BKSP: return "M9 5 L21 5 L21 19 L9 19 L3 12 Z M12 9 L18 15 M18 9 L12 15";
    case TA_ENTER: return "M19 5 L19 13 L5 13 M9 9 L5 13 L9 17";
    case TA_UNDO: return "M9 5 L4 10 L9 15 M4 10 L14 10 A 14 15 5 -90 180 M14 20 L9 20";
    case TA_CLEAR: return "O 12 12 9 M8.5 8.5 L15.5 15.5 M15.5 8.5 L8.5 15.5";
    case TA_HIDE: return "M5 9 L12 16 L19 9";
    case TA_MORE: return "O 5 12 1.2 O 12 12 1.2 O 19 12 1.2";
    case TA_REDO: return "M15 5 L20 10 L15 15 M20 10 L10 10 A 10 15 5 -90 -180 M10 20 L15 20";
    default: return NULL;
    }
}
static Image *ta_icon(App *a, int act) {
    static Image *ic[TA_NACT];
    if (act < 0 || act >= TA_NACT) return NULL;
    Image **slot = &ic[act];
    if (*slot && (*slot)->scale != a->scale) { image_unref(*slot); *slot = NULL; }
    if (!*slot) {
        int px = (int)(22 * a->scale + 0.5f); Image *im = xcalloc(1, sizeof *im);
        im->w = im->h = px; im->refs = 1; im->scale = a->scale; im->px = xcalloc((size_t)px * (size_t)px, 4);
        if (!path_icon_rgba(ta_path(act), px, 1.7f, 0x3c4043, im->px)) { free(im->px); free(im); return NULL; }
        *slot = im;
    }
    return *slot;
}
/* an icon bar centred on cx, above top (else below bot), page viewport px; bend: each button's right end */
static void bar_draw(App *a, DisplayList *dl, const int *act, int n, float cx, float top, float bot, float bar[4], float *bend, int held) {
    float w = (float)n * BAR_BW, h = BAR_H, vw = page_width(a), vh = page_height(a);
    float x = LCLAMP(cx - w / 2, 4, LMAX(4, vw - 4 - w)), y = top - 10 - h;
    if (y < 4) y = LMIN(bot + 10, vh - 4 - h);
    push_rect(dl, x - 1, y, w + 2, h + 2, 9, RGBA(0, 0, 0, 38));
    push_rect(dl, x, y, w, h, 8, RGBA(255, 255, 255, 255));
    for (int i = 0; i < n; i++) {
        float bx = x + (float)i * BAR_BW;
        if (i + 1 == held) push_rect(dl, bx + 2, y + 2, BAR_BW - 4, h - 4, 6, RGBA(232, 234, 237, 255));
        Image *im = ta_icon(a, act[i]);
        if (im) push_img(dl, im, bx + (BAR_BW - 22) / 2.0f, y + (h - 22) / 2, 22, 22);
        bend[i] = bx + BAR_BW;
    }
    bar[0] = x; bar[1] = y; bar[2] = x + w; bar[3] = y + h;
}
/* UI tests (tools/edit_bar_test.py): the bars' and popup's layout in viewport CSS px, logged when it changes */
static void geom_log(int ch, const char *sig) {   /* ch: 0 edit bar, 1 selection bar, 2 popup */
    static char last[3][512];
    if (!strcmp(sig, last[ch])) return;
    snprintf(last[ch], sizeof last[ch], "%s", sig);
    __android_log_print(ANDROID_LOG_INFO, "LumenBar", "%s", sig);
}
static void bar_geom_log(const char *kind, const int *act, int n, const float bar[4], const float *bend) {
    char sig[512]; int k = snprintf(sig, sizeof sig, "bar %s y0=%.0f y1=%.0f x0=%.0f", kind, bar[1], bar[3], bar[0]);
    for (int i = 0; i < n && k < (int)sizeof sig - 16; i++) k += snprintf(sig + k, sizeof sig - (size_t)k, " %d:%.0f", act[i], i < n - 1 ? bend[i] : bar[2]);
    geom_log(*kind == 'e' ? 0 : 1, sig);
}
static int bar_index(const float bar[4], const float *bend, int n, float px, float py) {
    if (!n || px < bar[0] || px >= bar[2] || py < bar[1] || py >= bar[3]) return 0;
    int i = 0; while (i < n - 1 && px >= bend[i]) i++;
    return i + 1;
}
/* an editable field's bar: Keyboard, [Cut,] Copy (the whole value with nothing selected), Paste, the two slots and
   More (with Cut on the edit bar, and Select all) */
static int edit_bar_acts(int *act, bool cutcopy, bool cut) {
    int n = 0;
    act[n++] = TA_KBD;
    if (cutcopy) { if (cut) act[n++] = TA_CUT; act[n++] = TA_COPY; }
    act[n++] = TA_PASTE; act[n++] = g_bar_slot[0]; act[n++] = g_bar_slot[1]; act[n++] = TA_MORE;
    return n;
}
/* the More menu (kind 1) or the clipboard history (kind 2) under the bar; act: TA_*, 100 + snippet, 200 + clip */
/* ni: the first ni actions are icon buttons on one line at the top; free: opened from the browser menu, so it stays
   without a focused field */
static struct { int kind, n, ni, hit; bool free; float x, y, w; int act[POP_MAX]; } g_epop;
static bool pop_shown(void) { return g_epop.kind != 0; }
static int pop_rows(void) { return (g_epop.ni ? 1 : 0) + g_epop.n - g_epop.ni; }
/* the last clipboard contents, newest first: text, or an image's bytes and type */
static struct { char *text; unsigned char *img; size_t n; char *mime; Image *thumb; } g_clips[CLIP_MAX]; static int g_nclips;
static struct {
    bool armed, ui, url; int mode, kind;   /* mode 1: the long-press finger extends from its word; 2: a handle is dragged */
    double down; float x, y;      /* armed long press, window px; url: it is on the URL field */
    uint64_t device, finger;      /* the contact this state belongs to */
    Node *wn[2]; int wo[2];       /* the long-pressed word */
    Node *cn; int cw[2], cs[2], fixed;   /* TK_CTL: the control, its word and the selection shown (codepoints), the held end */
    float gx, gy;                 /* handle grab: finger minus the caret point */
    float ax, ay;                 /* where the bar goes with no handles (an empty control), page viewport px */
    float bar[4], bend[BAR_MAX]; int bact[BAR_MAX], nb;   /* selection bar rect, its button ends and actions, page viewport px */
    int bar_hit;                  /* bar button held, 1-based */
    int umenu_hit;                /* URL menu button held, 1-based */
} g_ts;
static void tsel_touch_reset(void) { g_ts.armed = false; g_ts.mode = g_ts.bar_hit = g_ts.umenu_hit = g_kbm.hit = 0; }
static Node *tsel_ctl(App *a) {
    Node *f = page_focus(a);
    return f && f == g_ts.cn && f->doc->csel_n == f && f->doc->csel[0] == g_ts.cs[0] && f->doc->csel[1] == g_ts.cs[1] ? f : NULL;
}
static bool tsel_ui(App *a) {
    if (!g_ts.ui) return false;
    bool ok = g_ts.kind == TK_CTL ? tsel_ctl(a) != NULL : g_tsel.on && a->t->cur && g_tsel.L == a->t->cur->L && (g_ts.kind == TK_PAGE || edit_focus(a));
    if (!ok) g_ts.ui = false;
    return g_ts.ui;
}
static void tsel_clear(App *a) {
    if (g_ts.kind == TK_PAGE) g_tsel.on = false;
    g_ts.ui = false; g_ts.mode = g_ts.bar_hit = 0; sel_dirty(a);
}
static void tsel_ctl_set(App *a, Node *f, int an, int fo) { ctl_sel_set(a, f, an, fo); g_ts.cs[0] = an; g_ts.cs[1] = fo; }
/* tell the contenteditable's JS selection about g_tsel */
static void tsel_edit_push(App *a) {
    Page *p = a->t->cur;
    if (!p || !p->js || !g_tsel.an || !g_tsel.fn) return;
    js_dispatch(p->js, g_tsel.an, "lumensel-collapse", "MouseEvent", false, false, g_tsel.ao, 0, 0, NULL);
    js_dispatch(p->js, g_tsel.fn, "lumensel-extend", "MouseEvent", false, false, g_tsel.fo, 0, 0, NULL);
}
static void tsel_overlay(App *a, DisplayList *dl) {
    g_ts.bar[2] = g_ts.bar[0]; g_ts.nb = 0;
    if (!tsel_ui(a)) return;
    const TselCaret *c = g_tsel_caret;
    const float r = 11; const Color blue = RGBA(26, 115, 232, 255);
    for (int k = 0; k < 2; k++) {   /* teardrops hanging under the ends, the point at the caret */
        if (!c[k].ok) continue;
        DItem it; memset(&it, 0, sizeof it); it.op = DO_RECT; it.color = blue;
        it.x = k ? c[k].x : c[k].x - 2 * r; it.y = c[k].y + c[k].h; it.w = it.h = 2 * r;
        for (int i = 0; i < 4; i++) it.r[i] = i == (k ? 0 : 1) ? 0 : r;
        vec_push(dl->items, it);
    }
    if (g_ts.mode || (!c[0].ok && !c[1].ok && g_ts.kind != TK_CTL)) return;
    Node *f = g_ts.kind == TK_CTL ? tsel_ctl(a) : NULL;
    bool secret = f && f->tag == A_input && node_attr(f, "type") && !strcasecmp(node_attr(f, "type"), "password");
    int n = 0;
    if (g_ts.kind == TK_PAGE) { g_ts.bact[n++] = TA_COPY; g_ts.bact[n++] = TA_ALL; }
    else n = edit_bar_acts(g_ts.bact, !secret, true);
    TselCaret anchor = { true, g_ts.ax, g_ts.ay - 20, 40, 0 };
    const TselCaret *top = c[0].ok ? &c[0] : c[1].ok ? &c[1] : &anchor, *bot = c[1].ok ? &c[1] : c[0].ok ? &c[0] : &anchor;
    float cx = c[0].ok && c[1].ok && fabsf(c[0].y - c[1].y) < 1 ? (c[0].x + c[1].x) / 2 : top->x;
    bar_draw(a, dl, g_ts.bact, n, cx, top->y, bot->y + bot->h + 2 * r, g_ts.bar, g_ts.bend, g_ts.bar_hit);
    g_ts.nb = n;
    bar_geom_log("sel", g_ts.bact, n, g_ts.bar, g_ts.bend);
}
/* Keyboard on second tap: after a touch tap was handled. A tap in the focused field while the
   keyboard is down places the caret (done by the click) and shows the Paste bubble; a second tap
   within 30 px of it brings the keyboard up. With the keyboard up, taps only move the caret. */
static void kbd_tap(App *a, float x, float y) {
    Node *f = page_focus(a); Page *p = a->t->cur;
    if (!f) f = edit_focus(a);
    bool was = g_kbm.menu; g_kbm.menu = false;
    if (was) sel_dirty(a);
    double tn = now_ms();
    g_kbm.tx = x; g_kbm.ty = y; g_kbm.tt = tn;
    if (!g_tap_kbd || !f || !p || !p->L || x < page_left(a) || y < BAR) { g_kbm.n = NULL; return; }
    float px = page_x(a, x), py = page_y(a, y) + a->t->sy;
    Box *b = layout_hit(p->L, px, py, 0, a->t->sy);
    /* in the field, or on what a styled field lays over it (a label, an outline) */
    const Box *fb = f->box;
    bool over = fb && px >= fb->x - 4 && px < fb->x + fb->w + 4 && py >= fb->y - 4 && py < fb->y + fb->h + 4;
    if (!over && (!b || !b->node || !node_within(b->node, f))) return;   /* elsewhere on the page: the field keeps its focus */
    /* the keyboard comes up only from the bar's Keyboard button (or Show keyboard): a tap shows the bar while the
       keyboard is down, a second tap on the same spot while it is up */
    bool same = g_kbm.n == f && hypotf(x - g_kbm.x, y - g_kbm.y) <= 30;
    if (SDL_ScreenKeyboardShown(a->win) && !same) { g_kbm.n = f; g_kbm.x = x; g_kbm.y = y; return; }
    g_kbm.n = f; g_kbm.x = x; g_kbm.y = y;
    g_kbm.menu = true; g_kbm.mx = px; g_kbm.my = py; g_kbm.hit = 0;
    a->caret_t = now_ms();
}
static void kbm_overlay(App *a, DisplayList *dl) {
    g_kbm.nb = 0;
    Node *f = page_focus(a);
    if (!f) f = edit_focus(a);
    if (!g_kbm.menu || !f || f != g_kbm.n || tsel_ui(a)) { g_kbm.menu = false; geom_log(0, "bar edit hidden"); return; }
    bool any = true, secret = f->tag == A_input && node_attr(f, "type") && !strcasecmp(node_attr(f, "type"), "password");
    if (is_text_ctl(f)) { char *v = ctl_value(f); any = *v != 0; free(v); }
    int n = edit_bar_acts(g_kbm.act, any && !secret, false);
    float cy = g_kbm.my - a->t->sy, vh = page_height(a);
    if (cy < -BAR_H || cy > vh + BAR_H) return;   /* scrolled away */
    bar_draw(a, dl, g_kbm.act, n, g_kbm.mx, cy - 24, cy + 24, g_kbm.bar, g_kbm.bend, g_kbm.hit);
    g_kbm.nb = n;
    bar_geom_log("edit", g_kbm.act, n, g_kbm.bar, g_kbm.bend);
}
/* the bubble's button under window point x, y: 1-based, 0 for none */
static int kbm_hit(App *a, float x, float y) {
    if (!g_kbm.menu || !g_kbm.nb || x <= page_left(a) || y <= BAR) return 0;
    float px = page_x(a, x), py = page_y(a, y);
    return bar_index(g_kbm.bar, g_kbm.bend, g_kbm.nb, px, py);
}
/* bar keys go through the event queue, as the keyboard's do */
static void key_push(SDL_Keycode k) {
    SDL_Event e; SDL_zero(e); e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.down = true; e.key.which = BAR_KEY_ID; SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP; e.key.down = false; SDL_PushEvent(&e);
}
static void key_push_mod(SDL_Keycode k, SDL_Keymod mod) {
    SDL_Event e; SDL_zero(e); e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.down = true; e.key.mod = mod; e.key.which = BAR_KEY_ID; SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP; e.key.down = false; SDL_PushEvent(&e);
}
static void text_push(const char *t) {
    static char *ring[16]; static int ri;   /* the text outlives the queued event */
    free(ring[ri]); ring[ri] = xstrdup(t);
    SDL_Event e; SDL_zero(e); e.type = SDL_EVENT_TEXT_INPUT; e.text.text = ring[ri]; ri = (ri + 1) % 16; SDL_PushEvent(&e);
}
static void snip_type(const char *t) {
    size_t n = strlen(t); bool nl = n && t[n - 1] == '\n';
    char *b = xstrdup(t); if (nl) b[n - 1] = 0;
    if (*b) text_push(b);
    free(b);
    if (nl) key_push(SDLK_RETURN);
}
static void field_all(App *a, Node *f, Node *h) {
    Page *p = a->t->cur;
    if (f) { char *v = ctl_value(f); tsel_ctl_set(a, f, 0, u8_byte_cp(v, (int)strlen(v))); free(v); }
    else if (h && p->js) { js_dispatch(p->js, h, "lumensel-all", "MouseEvent", false, false, 0, 0, 0, NULL); edit_sel_sync(a); }
}
static void field_word_delete(App *a, Node *f, Node *h) {
    Page *p = a->t->cur;
    if (f) ctl_delete(a, f, -1, true);
    else if (h && p->js) {   /* as a key, so editors that handle Ctrl+Backspace themselves (Draft.js) see it */
        SDL_Event e; SDL_zero(e); e.type = SDL_EVENT_KEY_DOWN; e.key.key = SDLK_BACKSPACE; e.key.down = true; e.key.mod = SDL_KMOD_LCTRL; e.key.which = BAR_KEY_ID; SDL_PushEvent(&e);
        e.type = SDL_EVENT_KEY_UP; e.key.down = false; SDL_PushEvent(&e);
    }
    restyle(a); a->dirty = true;
}
static void pop_open(App *a, int kind, const float bar[4]);
static void clip_record(const char *text, const unsigned char *img, size_t n, const char *mime);
/* a bar button on field f (a text control) or h (an editing host); whole: Cut and Copy take the whole value */
static void bar_do(App *a, int act, Node *f, Node *h, bool whole) {
    Page *p = a->t->cur;
    if (!p || (!f && !h)) return;
    switch (act) {
    case TA_KBD: g_kbm.menu = false; SDL_StartTextInput(a->win); break;
    case TA_HIDE: g_kbm.menu = false; g_epop.kind = 0; break;
    case TA_MORE: pop_open(a, 1, g_ts.ui && tsel_ui(a) ? g_ts.bar : g_kbm.bar); return;
    case TA_PASTE: page_paste(a, f, f ? NULL : h); break;
    case TA_BKSP: key_push(SDLK_BACKSPACE); break;
    case TA_ENTER: key_push(SDLK_RETURN); break;
    case TA_UNDO: if (p->js) js_dispatch(p->js, f ? f : h, "lumenundo", "KeyboardEvent", false, false, 0, 0, 0, NULL); break;
    case TA_REDO: key_push_mod(SDLK_Z, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT); break;
    case TA_CLEAR:
        field_all(a, f, h);
        if (f) ctl_insert(a, f, "", "deleteContentBackward"); else if (p->js) js_dispatch(p->js, h, "lumeneditdel", "KeyboardEvent", false, false, 0, 0, 0, "Backspace");
        g_tsel.on = false;
        break;
    case TA_CUT: case TA_COPY: {
        if (whole) field_all(a, f, h);
        bool failed = false;   /* an editing host's text is its selection's, as the page sees it */
        char *t = f ? ctl_selected(f) : h && p->js ? js_eval_result(p->js, "String(getSelection())", "lumen-copy", &failed) : tsel_text(p->L);
        if (!t || failed) { free(t); t = tsel_text(p->L); }
        if (*t) { SDL_SetClipboardText(t); clip_record(t, NULL, 0, NULL); }
        free(t);
        if (act == TA_CUT) { if (f) ctl_insert(a, f, "", "deleteByCut"); else if (p->js) js_dispatch(p->js, h, "lumeneditdel", "KeyboardEvent", false, false, 0, 0, 0, "Backspace"); }
        if (whole && f && act == TA_COPY) { char *v = ctl_value(f); int e = u8_byte_cp(v, (int)strlen(v)); free(v); tsel_ctl_set(a, f, e, e); }
        if (whole) g_tsel.on = false;
        break;
    }
    default: break;
    }
    g_ts.ui = false;
    sel_dirty(a); restyle(a); a->dirty = true;
}
static void kbm_action(App *a, int act) {
    Page *p = a->t->cur; Node *f = page_focus(a), *h = f ? NULL : edit_focus(a);
    if (act != TA_MORE) g_kbm.menu = false;
    sel_dirty(a);
    if (!p || !p->L || (!f && !h)) return;
    if (act == TA_ALL && f) {   /* then the selection bar, as after a long press */
        char *v = ctl_value(f); int e = u8_byte_cp(v, (int)strlen(v)); free(v);
        g_tsel.on = false;
        tsel_ctl_set(a, f, 0, e);
        g_ts.kind = TK_CTL; g_ts.cn = f; g_ts.cw[0] = 0; g_ts.cw[1] = e;
        g_ts.ax = g_kbm.mx; g_ts.ay = g_kbm.my - a->t->sy;
        g_ts.mode = 0; g_ts.ui = true; g_ts.gx = g_ts.gy = 0;
        return;
    }
    if (act == TA_ALL) {
        if (p->js) js_dispatch(p->js, h, "lumensel-all", "MouseEvent", false, false, 0, 0, 0, NULL);
        edit_sel_sync(a);
        if (g_tsel.on && edit_focus(a) == h) { g_ts.kind = TK_EDIT; g_ts.mode = 0; g_ts.ui = true; g_ts.gx = g_ts.gy = 0; }
        return;
    }
    bar_do(a, act, f, h, true);
    if (act == TA_BKSP || act == TA_ENTER || act == TA_UNDO || act == TA_REDO || act == TA_CLEAR) g_kbm.menu = true;   /* the bar stays for more edits */
}
static void tsel_action(App *a, int act) {
    Page *p = a->t->cur; Node *f = g_ts.kind == TK_CTL ? tsel_ctl(a) : NULL, *h = g_ts.kind == TK_EDIT ? edit_focus(a) : NULL;
    if (!p || !p->L || (g_ts.kind == TK_CTL && !f) || (g_ts.kind == TK_EDIT && !h)) return;
    if (act == TA_ALL) {
        if (f) { char *v = ctl_value(f); tsel_ctl_set(a, f, 0, u8_byte_cp(v, (int)strlen(v))); free(v); }
        else if (h) js_dispatch(p->js, h, "lumensel-all", "MouseEvent", false, false, 0, 0, 0, NULL);
        else tsel_all(p->L);
        sel_dirty(a);
        return;
    }
    if (act == TA_COPY && !f && !h) { char *t = tsel_text(p->L); if (*t) SDL_SetClipboardText(t); free(t); tsel_clear(a); return; }
    if (act == TA_MORE) { bar_do(a, act, f, h, false); return; }
    bar_do(a, act, f, h, false);
    tsel_clear(a);
}
/* the More menu: the slot actions not on the bar, then the snippets; the clipboard history: its entries */
static void pop_open(App *a, int kind, const float bar[4]) {
    g_epop.kind = kind; g_epop.n = g_epop.ni = g_epop.hit = 0; g_epop.free = false;
    if (kind == 1) {   /* icons on the top line: Cut (the edit bar has none) and Select all for a field, Undo, Redo, Clear,
                          Hide; text rows below: the keys not on the bar, the snippets */
        static const int keys[] = { TA_BKSP, TA_ENTER };
        Node *f = page_focus(a), *h = f ? NULL : edit_focus(a);
        if (f || h) {
            bool secret = f && f->tag == A_input && node_attr(f, "type") && !strcasecmp(node_attr(f, "type"), "password");
            if (bar != g_ts.bar && !secret) g_epop.act[g_epop.n++] = TA_CUT;
            g_epop.act[g_epop.n++] = TA_ALL;
        }
        g_epop.act[g_epop.n++] = TA_UNDO; g_epop.act[g_epop.n++] = TA_REDO; g_epop.act[g_epop.n++] = TA_CLEAR; g_epop.act[g_epop.n++] = TA_HIDE;
        g_epop.ni = g_epop.n;
        for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) if (keys[i] != g_bar_slot[0] && keys[i] != g_bar_slot[1]) g_epop.act[g_epop.n++] = keys[i];
        for (int i = 0; i < g_nsnip && g_epop.n < POP_MAX; i++) g_epop.act[g_epop.n++] = 100 + i;
    } else {
        char *t = SDL_GetClipboardText();   /* what is on the clipboard now is the newest entry */
        if (t && *t) clip_record(t, NULL, 0, NULL);
        else { char *m = NULL; size_t n = 0; unsigned char *img = android_clip_image(&m, &n); if (img) clip_record(NULL, img, n, m); free(img); free(m); }
        SDL_free(t);
        for (int i = 0; i < g_nclips && g_epop.n < POP_MAX; i++) g_epop.act[g_epop.n++] = 200 + i;
    }
    if (!g_epop.n) { g_epop.kind = 0; return; }
    float w = 240, h = (float)pop_rows() * POP_RH, vw = page_width(a), vh = page_height(a);
    g_epop.w = w; g_epop.x = LCLAMP(bar[2] - w, 4, LMAX(4, vw - 4 - w));
    g_epop.y = bar[3] + 6 + h <= vh - 4 ? bar[3] + 6 : LMAX(4, bar[1] - 6 - h);
    sel_dirty(a);
}
static void pop_label(int act, char *out, size_t n) {
    if (act < 100) { snprintf(out, n, "%s", ta_label(act)); return; }
    if (act < 200) {
        const char *t = act - 100 < g_nsnip ? g_snip[act - 100] : ""; size_t l = strlen(t);
        snprintf(out, n, "%.*s%s", (int)(l && t[l - 1] == '\n' ? l - 1 : l), t, l && t[l - 1] == '\n' ? "  + Enter" : "");
        return;
    }
    int k = act - 200;
    if (k >= g_nclips) { out[0] = 0; return; }
    if (g_clips[k].img) { const char *mt = g_clips[k].mime ? g_clips[k].mime : "image"; snprintf(out, n, "%s, %zu KB", strncmp(mt, "image/", 6) ? mt : mt + 6, (g_clips[k].n + 1023) / 1024); return; }
    size_t j = 0;
    for (const char *c = g_clips[k].text; *c && j + 4 < n && j < 40; c++) out[j++] = *c == '\n' || *c == '\t' ? ' ' : *c;
    out[j] = 0;
    if (strlen(g_clips[k].text) > j) snprintf(out + j, n - j, "...");
}
static void pop_overlay(App *a, DisplayList *dl) {
    if (!g_epop.kind) { geom_log(2, "pop hidden"); return; }
    { char sig[512]; int k = snprintf(sig, sizeof sig, "pop kind=%d x=%.0f y=%.0f w=%.0f rh=%d acts=", g_epop.kind, g_epop.x, g_epop.y, g_epop.w, POP_RH);
      for (int i = 0; i < g_epop.n && k < (int)sizeof sig - 8; i++) k += snprintf(sig + k, sizeof sig - (size_t)k, "%s%d", i ? "," : "", g_epop.act[i]);
      if (k < (int)sizeof sig - 8) snprintf(sig + k, sizeof sig - (size_t)k, " ni=%d", g_epop.ni);
      geom_log(2, sig); }
    if (!g_epop.free && !page_focus(a) && !edit_focus(a)) { g_epop.kind = 0; return; }
    Font *ft = a->ui; float h = (float)pop_rows() * POP_RH;
    push_rect(dl, g_epop.x - 1, g_epop.y, g_epop.w + 2, h + 2, 9, RGBA(0, 0, 0, 38));
    push_rect(dl, g_epop.x, g_epop.y, g_epop.w, h, 8, RGBA(255, 255, 255, 255));
    float iw = g_epop.ni ? g_epop.w / (float)g_epop.ni : 0;
    for (int i = 0; i < g_epop.ni; i++) {   /* the icon line */
        float x = g_epop.x + (float)i * iw;
        if (i + 1 == g_epop.hit) push_rect(dl, x + 2, g_epop.y + 2, iw - 4, POP_RH - 4, 6, RGBA(232, 234, 237, 255));
        Image *im = ta_icon(a, g_epop.act[i]);
        if (im) push_img(dl, im, x + (iw - 22) / 2, g_epop.y + (POP_RH - 22) / 2.0f, 22, 22);
    }
    if (g_epop.ni && g_epop.n > g_epop.ni) push_rect(dl, g_epop.x + 8, g_epop.y + POP_RH, g_epop.w - 16, 1, 0, RGBA(218, 220, 224, 255));
    for (int i = g_epop.ni; i < g_epop.n; i++) {
        float y = g_epop.y + (float)(i - g_epop.ni + (g_epop.ni ? 1 : 0)) * POP_RH; char lb[96]; int act = g_epop.act[i];
        if (i + 1 == g_epop.hit) push_rect(dl, g_epop.x + 2, y + 2, g_epop.w - 4, POP_RH - 4, 6, RGBA(232, 234, 237, 255));
        if (i > g_epop.ni && (act >= 100) != (g_epop.act[i - 1] >= 100)) push_rect(dl, g_epop.x + 8, y, g_epop.w - 16, 1, 0, RGBA(218, 220, 224, 255));
        float tx = g_epop.x + 16;   /* text rows, aligned at the left; a clipboard image shows as a small picture */
        if (act >= 200 && act - 200 < g_nclips && g_clips[act - 200].img) {
            __typeof__(g_clips[0]) *c = &g_clips[act - 200];
            if (!c->thumb) c->thumb = image_decode(c->img, c->n);
            if (c->thumb && c->thumb->w > 0 && c->thumb->h > 0) {
                float s = LMIN(64.0f / (float)c->thumb->w, (POP_RH - 8.0f) / (float)c->thumb->h), tw = (float)c->thumb->w * s, th = (float)c->thumb->h * s;
                push_img(dl, c->thumb, tx, y + (POP_RH - th) / 2, tw, th);
                tx += 72;
            }
        }
        pop_label(act, lb, sizeof lb);
        push_text(dl, ft, lb, tx, y + POP_RH / 2.0f + (ft->ascent - ft->descent) / 2, g_epop.x + g_epop.w - 8 - tx, RGBA(32, 33, 36, 255));
    }
}
static int pop_hit(App *a, float x, float y) {
    if (!g_epop.kind || x <= page_left(a) || y <= BAR) return 0;
    float px = page_x(a, x), py = page_y(a, y);
    if (px < g_epop.x || px >= g_epop.x + g_epop.w || py < g_epop.y || py >= g_epop.y + (float)pop_rows() * POP_RH) return 0;
    int row = (int)((py - g_epop.y) / POP_RH);
    if (g_epop.ni && !row) return LMIN(g_epop.ni - 1, (int)((px - g_epop.x) * (float)g_epop.ni / g_epop.w)) + 1;
    return g_epop.ni + row - (g_epop.ni ? 1 : 0) + 1;
}
static void pop_action(App *a, int i) {
    Node *f = page_focus(a), *h = f ? NULL : edit_focus(a);
    int act = i >= 1 && i <= g_epop.n ? g_epop.act[i - 1] : -1;
    g_epop.kind = 0; sel_dirty(a);
    if (act < 0) return;
    if (!f && !h) {   /* opened from the browser menu with no field: keys and snippets go to the page as typed */
        if (act == TA_UNDO || act == TA_REDO) key_push_mod(SDLK_Z, SDL_KMOD_LCTRL | (act == TA_REDO ? SDL_KMOD_LSHIFT : 0));
        else if (act == TA_BKSP) key_push(SDLK_BACKSPACE);
        else if (act == TA_ENTER) key_push(SDLK_RETURN);
        else if (act >= 100 && act < 200 && act - 100 < g_nsnip) snip_type(g_snip[act - 100]);
        else if (act >= 200 && act - 200 < g_nclips && g_clips[act - 200].text) { SDL_SetClipboardText(g_clips[act - 200].text); page_paste(a, NULL, NULL); }
        a->dirty = true;
        return;
    }
    if (act == TA_ALL) { g_kbm.menu = true; kbm_action(a, TA_ALL); return; }   /* then the selection bar */
    if (act < 100) { bar_do(a, act, f, h, true); if (act != TA_HIDE && act != TA_CUT) g_kbm.menu = true; return; }
    if (act < 200) { if (act - 100 < g_nsnip) snip_type(g_snip[act - 100]); return; }
    int k = act - 200;
    if (k >= g_nclips) return;
    if (g_clips[k].img) {   /* the image is the next paste's; the clipboard keeps its text */
        free(g_clip_pick.img); free(g_clip_pick.mime);
        g_clip_pick.img = xmalloc(g_clips[k].n); memcpy(g_clip_pick.img, g_clips[k].img, g_clips[k].n);
        g_clip_pick.n = g_clips[k].n; g_clip_pick.mime = xstrdup(g_clips[k].mime ? g_clips[k].mime : "image/png");
        page_paste(a, f, f ? NULL : h);
    } else { SDL_SetClipboardText(g_clips[k].text); page_paste(a, f, f ? NULL : h); }
    restyle(a); a->dirty = true;
}
/* clipboard history: a new clip goes first, an equal older one is dropped */
static void clip_record(const char *text, const unsigned char *img, size_t n, const char *mime) {
    if ((!text || !*text) && !img) return;
    for (int i = 0; i < g_nclips; i++) {
        bool eq = img ? g_clips[i].img && g_clips[i].n == n && !memcmp(g_clips[i].img, img, n) : g_clips[i].text && !strcmp(g_clips[i].text, text);
        if (!eq) continue;
        if (i == 0) return;
        __typeof__(g_clips[0]) c = g_clips[i]; memmove(&g_clips[1], &g_clips[0], (size_t)i * sizeof c); g_clips[0] = c;
        return;
    }
    if (g_nclips == CLIP_MAX) { g_nclips--; free(g_clips[g_nclips].text); free(g_clips[g_nclips].img); free(g_clips[g_nclips].mime); if (g_clips[g_nclips].thumb) image_unref(g_clips[g_nclips].thumb); }
    memmove(&g_clips[1], &g_clips[0], (size_t)g_nclips * sizeof g_clips[0]);
    g_clips[0].text = text && !img ? xstrdup(text) : NULL;
    g_clips[0].img = NULL; g_clips[0].n = 0; g_clips[0].mime = NULL; g_clips[0].thumb = NULL;
    if (img) { g_clips[0].img = xmalloc(n); memcpy(g_clips[0].img, img, n); g_clips[0].n = n; g_clips[0].mime = xstrdup(mime ? mime : "image/png"); }
    g_nclips++;
}
/* a held bar button: Backspace held deletes a word, then another every 2 s; Paste held opens the history */
static void bar_hold_tick(App *a) {
    if (!g_kbm.hit && !g_ts.bar_hit) return;
    int act = g_kbm.hit ? g_kbm.act[g_kbm.hit - 1] : g_ts.bact[g_ts.bar_hit - 1];
    double t = now_ms();
    if (t < g_kbm.hold_next || (act != TA_BKSP && act != TA_PASTE)) return;
    Node *f = page_focus(a), *h = f ? NULL : edit_focus(a);
    if (act == TA_PASTE) {
        if (g_kbm.held) return;
        g_kbm.held = true;
        pop_open(a, 2, g_kbm.hit ? g_kbm.bar : g_ts.bar);
        return;
    }
    g_kbm.held = true; g_kbm.hold_next = t + 2000;
    field_word_delete(a, f, h);
}
static int bar_hold_wait_ms(int to) {
    if (!g_kbm.hit && !g_ts.bar_hit) return to;
    double left = g_kbm.hold_next - now_ms();
    return left <= 0 ? 0 : left + 1 < to ? (int)left + 1 : to;
}
/* move the dragged end to the finger (window px) */
static void tsel_extend(App *a, float x, float y) {
    Page *p = a->t->cur; Node *n; int o;
    if (!p || !p->L) return;
    float px = page_x(a, x) - g_ts.gx, py = page_y(a, y) - g_ts.gy + a->t->sy;
    if (g_ts.kind == TK_CTL) {
        Node *f = tsel_ctl(a);
        if (!f || !f->box) return;
        int cp = ctl_hit(f->box, px, py, a->t->sy), an, fo;
        if (g_ts.mode == 1) {
            if (cp >= g_ts.cw[1]) an = g_ts.cw[0], fo = cp;
            else if (cp <= g_ts.cw[0]) an = g_ts.cw[1], fo = cp;
            else an = g_ts.cw[0], fo = g_ts.cw[1];
        } else an = g_ts.fixed, fo = cp;
        if (an != fo && (an != g_ts.cs[0] || fo != g_ts.cs[1])) tsel_ctl_set(a, f, an, fo);
        return;
    }
    if (!tsel_point(p->L, px, py, a->t->sy, NULL, &n, &o)) return;
    if (g_ts.kind == TK_EDIT && (!edit_focus(a) || !node_within(n, edit_focus(a)))) return;
    TextSel was = g_tsel;
    if (g_ts.mode == 1) {   /* the word stays selected; the far end follows the finger */
        g_tsel = (TextSel){ true, p->L, g_ts.wn[1], n, g_ts.wo[1], o };
        tsel_prepare(p->L);
        if (!g_tsel_fwd) {
            g_tsel.an = g_ts.wn[0]; g_tsel.ao = g_ts.wo[0];
            tsel_prepare(p->L);
            if (g_tsel_fwd) { g_tsel.fn = g_ts.wn[1]; g_tsel.fo = g_ts.wo[1]; }   /* inside the word */
        }
    } else { g_tsel.fn = n; g_tsel.fo = o; }
    if (g_tsel.an == g_tsel.fn && g_tsel.ao == g_tsel.fo) { g_tsel = was; return; }   /* never collapse to nothing */
    if (memcmp(&was, &g_tsel, sizeof was)) { if (g_ts.kind == TK_EDIT) tsel_edit_push(a); sel_dirty(a); }
}
static void url_menu_action(App *a, int act) {
    if (act == UM_COPY) { char *s = url_selected(a); if (*s) SDL_SetClipboardText(s); free(s); a->umenu = false; }
    else if (act == UM_PASTE) { char *t = SDL_GetClipboardText(); if (t && *t) url_insert(a, t); SDL_free(t); a->umenu = false; }
    else if (act == UM_ALL) url_select_all(a);
    else if (act == UM_SHARE) {
        char *s = url_selected(a);
        const char *page = a->t->cur ? a->t->cur->url : a->t->hpos >= 0 ? a->t->hist[a->t->hpos] : "";
        android_share_text(*s ? s : page);
        free(s);
        url_cancel(a);
    }
    a->dirty = true; a->vonly = false;
}
static bool tsel_touch(App *a, AndroidTouchPhase phase, const SDL_Event *ev, float x, float y) {
    uint64_t device = ev->tfinger.touchID, finger = ev->tfinger.fingerID;
    if (!tsel_ui(a)) g_ts.mode = g_ts.bar_hit = 0;   /* the selection went away under the gesture */
    bool mine = device == g_ts.device && finger == g_ts.finger;
    if (phase == ANDROID_TOUCH_DOWN) {
        g_ts.armed = g_ts.url = false;
        if (g_ts.mode || g_ts.bar_hit || g_ts.umenu_hit || g_kbm.hit || g_epop.hit) return true;   /* other fingers wait for the selection gesture */
        if (a->touch.count) return false;
        int ph = pop_hit(a, x, y);
        if (ph) { g_ts.device = device; g_ts.finger = finger; g_epop.hit = ph; sel_dirty(a); return true; }
        if (g_epop.kind) { g_epop.kind = 0; sel_dirty(a); }
        int km = kbm_hit(a, x, y);
        g_kbm.hold_t = now_ms(); g_kbm.hold_next = g_kbm.hold_t + LONG_PRESS_MS; g_kbm.held = false;
        if (km) { g_ts.device = device; g_ts.finger = finger; g_kbm.hit = km; sel_dirty(a); return true; }
        if (g_kbm.menu) { g_kbm.menu = false; sel_dirty(a); }
        int um = url_menu_hit(x, y);
        if (um >= 0) { g_ts.device = device; g_ts.finger = finger; g_ts.umenu_hit = um + 1; return true; }
        if (a->umenu) { a->umenu = false; a->dirty = true; a->vonly = false; }
        if (y < BAR && bar_hit(a, x, y) == HB_URL) {
            g_ts.device = device; g_ts.finger = finger;
            g_ts.armed = g_ts.url = true; g_ts.down = now_ms(); g_ts.x = x; g_ts.y = y;
            return false;
        }
        if (x <= page_left(a) || y <= BAR || a->t->info || g_hud_pop) return false;
        g_ts.device = device; g_ts.finger = finger;
        float px = page_x(a, x), py = page_y(a, y);
        if (tsel_ui(a)) {
            if (g_ts.nb && px >= g_ts.bar[0] && px < g_ts.bar[2] && py >= g_ts.bar[1] && py < g_ts.bar[3]) {
                int i = 0; while (i < g_ts.nb - 1 && px >= g_ts.bend[i]) i++;
                g_ts.bar_hit = i + 1;
                return true;
            }
            for (int k = 1; k >= 0; k--) {
                const TselCaret *c = &g_tsel_caret[k];
                float hx = k ? c->x + 11 : c->x - 11, hy = c->y + c->h + 11;
                if (!c->ok || fabsf(px - hx) > 26 || py < c->y || py > hy + 26) continue;
                if (g_ts.kind == TK_CTL) g_ts.fixed = k ? LMIN(g_ts.cs[0], g_ts.cs[1]) : LMAX(g_ts.cs[0], g_ts.cs[1]);
                else {
                    tsel_prepare(a->t->cur->L);
                    if ((k == 0) == g_tsel_fwd) {   /* the held end becomes the focus */
                        Node *n = g_tsel.an; int o = g_tsel.ao;
                        g_tsel.an = g_tsel.fn; g_tsel.ao = g_tsel.fo; g_tsel.fn = n; g_tsel.fo = o;
                    }
                }
                g_ts.mode = 2; g_ts.gx = px - c->x; g_ts.gy = py - (c->y + c->h / 2);
                sel_dirty(a);
                return true;
            }
        }
        g_ts.armed = true; g_ts.down = now_ms(); g_ts.x = x; g_ts.y = y;
        return false;
    }
    if (g_epop.hit) {
        if (!mine) return true;
        if (phase == ANDROID_TOUCH_UP && pop_hit(a, x, y) == g_epop.hit) { int i = g_epop.hit; g_epop.hit = 0; pop_action(a, i); }
        if (phase != ANDROID_TOUCH_MOVE) { g_epop.hit = 0; sel_dirty(a); }
        return true;
    }
    if (g_kbm.hit) {
        if (!mine) return true;
        if (phase == ANDROID_TOUCH_UP && !g_kbm.held && kbm_hit(a, x, y) == g_kbm.hit) kbm_action(a, g_kbm.act[g_kbm.hit - 1]);
        if (phase != ANDROID_TOUCH_MOVE) { g_kbm.hit = 0; sel_dirty(a); }
        return true;
    }
    if (g_ts.umenu_hit) {
        if (!mine) return true;
        if (phase == ANDROID_TOUCH_UP && url_menu_hit(x, y) == g_ts.umenu_hit - 1) url_menu_action(a, g_umenu_act[g_ts.umenu_hit - 1]);
        if (phase != ANDROID_TOUCH_MOVE) g_ts.umenu_hit = 0;
        return true;
    }
    if (g_ts.bar_hit || g_ts.mode) {
        if (!mine) return true;
        if (g_ts.bar_hit && phase == ANDROID_TOUCH_UP) {
            float px = page_x(a, x), py = page_y(a, y);
            bool in = px >= g_ts.bar[0] && px < g_ts.bar[2] && py >= g_ts.bar[1] && py < g_ts.bar[3];
            int i = 0; while (i < g_ts.nb - 1 && px >= g_ts.bend[i]) i++;
            if (in && !g_kbm.held && i == g_ts.bar_hit - 1) tsel_action(a, g_ts.bact[i]);
        }
        if (g_ts.mode && phase == ANDROID_TOUCH_MOVE) tsel_extend(a, x, y);
        else if (phase != ANDROID_TOUCH_MOVE) { g_ts.mode = g_ts.bar_hit = 0; g_ts.gx = g_ts.gy = 0; sel_dirty(a); }
        return true;
    }
    if (g_ts.armed && (!mine || phase != ANDROID_TOUCH_MOVE || (x - g_ts.x) * (x - g_ts.x) + (y - g_ts.y) * (y - g_ts.y) >= 64))
        g_ts.armed = false;
    return false;
}
static int tsel_wait_ms(int to) {
    if (!g_ts.armed) return to;
    double left = LONG_PRESS_MS - (now_ms() - g_ts.down);
    return left <= 0 ? 0 : left + 1 < to ? (int)left + 1 : to;
}
/* the long press owns this contact now: the page sees it cancelled, and it neither scrolls nor clicks */
static void tsel_take_touch(App *a, bool page) {
    if (page) {
        SDL_Event cancel = {0};
        cancel.tfinger.touchID = g_ts.device; cancel.tfinger.fingerID = g_ts.finger;
        cancel.tfinger.x = g_ts.x / a->vw; cancel.tfinger.y = g_ts.y / a->vh;
        android_dom_touch(a, "cancel", &cancel);
    }
    android_touch_reset(&a->touch);
    android_fling_reset(&a->fling);
    g_press = g_drag = g_epress = false;
}
static bool img_ctx_menu(App *a, Node *n, float x, float y);
static void tsel_long_press(App *a) {
    if (!g_ts.armed || now_ms() - g_ts.down < LONG_PRESS_MS) return;
    g_ts.armed = false;
    if (g_ts.url) {   /* the URL field: select the word under the finger, with the Copy/Paste/Share menu */
        if (a->touch.count != 1 || a->touch.dragged) return;
        int o = url_at(a, g_ts.x);
        url_begin(a); url_word(a, o); a->umenu = true;
        tsel_take_touch(a, false);
        return;
    }
    Page *p = a->t->cur;
    if (a->touch.count != 1 || a->touch.blocked || a->touch.dragged || (a->touch_dom_flags & 1) ||
            a->touch_tab != a->t || !p || !p->L || a->t->asleep) return;
    float px = page_x(a, g_ts.x), py = page_y(a, g_ts.y);
    h_sync_in(a, p->d, true);
    Box *b = layout_hit(p->L, px, py + a->t->sy, 0, a->t->sy);
    Node *target = b ? b->node : NULL;
    while (target && target->type != NODE_ELEMENT) target = target->parent;
    if (target && p->js && !js_dispatch(p->js, target, "contextmenu", "MouseEvent", true, true, px, py, 2, NULL)) return;
    if (a->t->cur != p || !p->L) return;
    h_sync_in(a, p->d, true);
    b = layout_hit(p->L, px, py + a->t->sy, 0, a->t->sy);
    if (img_ctx_menu(a, b ? b->node : NULL, px, py + a->t->sy)) { tsel_take_touch(a, true); return; }   /* an image: its menu */
    Node *ctl = NULL;
    for (Node *k = b ? b->node : NULL; k; k = k->parent) if (is_text_ctl(k)) { ctl = k; break; }
    if (ctl) {   /* a text control: focus it and select the word, or offer Paste when it is empty */
        if (!ctl->box) return;
        int cp = ctl_hit(ctl->box, px, py + a->t->sy, a->t->sy), s = 0, e = 0;
        char *v = ctl_value(ctl);
        if (*v) text_word(v, cp, &s, &e);
        free(v);
        focus_node(a, ctl);
        if (page_focus(a) != ctl) return;
        tsel_take_touch(a, true);
        g_tsel.on = false;
        tsel_ctl_set(a, ctl, s, e);
        g_ts.kind = TK_CTL; g_ts.cn = ctl; g_ts.cw[0] = s; g_ts.cw[1] = e;
        g_ts.ax = px; g_ts.ay = py;
        g_ts.mode = s != e ? 1 : 0; g_ts.ui = true; g_ts.gx = g_ts.gy = 0;
        sel_dirty(a);
        return;
    }
    Node *host = b ? edit_host(b->node) : NULL;
    if (!host && b && b->st && b->st->user_select) return;
    if (host && edit_focus(a) != host) { focus_node(a, host); if (edit_focus(a) != host || !p->L) return; h_sync_in(a, p->d, true); }
    Node *n; int o;
    Box *scope = host && host->box ? host->box : sel_scope(p->L, px, py + a->t->sy, a->t->sy);
    if (!tsel_point_near(p->L, px, py + a->t->sy, a->t->sy, scope, 24, &n, &o)) return;
    if (host && !node_within(n, host)) return;
    tsel_word(p->L, n, o);
    if (!g_tsel.on || (g_tsel.an == g_tsel.fn && g_tsel.ao == g_tsel.fo)) { g_tsel.on = false; return; }
    tsel_take_touch(a, true);
    g_ts.kind = host ? TK_EDIT : TK_PAGE;
    if (host) tsel_edit_push(a);
    g_ts.wn[0] = g_tsel.an; g_ts.wo[0] = g_tsel.ao; g_ts.wn[1] = g_tsel.fn; g_ts.wo[1] = g_tsel.fo;
    g_ts.mode = 1; g_ts.ui = true; g_ts.gx = g_ts.gy = 0;
    sel_dirty(a);
}
#endif
static void js_quote(SB *b, const char *s) {
    sb_putc(b, '"');
    for (const unsigned char *c = (const unsigned char *)(s ? s : ""); *c; c++) {
        if (*c == '"' || *c == '\\') { sb_putc(b, '\\'); sb_putc(b, (char)*c); }
        else if (*c < 0x20) sb_printf(b, "\\u%04x", *c);
        else sb_putc(b, (char)*c);
    }
    sb_putc(b, '"');
}
static bool same_origin(const char *x, const char *y) {
    URL p, q; bool same = false;
    if (!url_parse(x, &p)) return false;
    if (url_parse(y, &q)) { same = !strcmp(p.scheme, q.scheme) && p.host && q.host && !strcmp(p.host, q.host) && p.port == q.port; url_free(&q); }
    url_free(&p); return same;
}
/* <a download>, blob: and data: links are saved, not navigated: http(s) through the download
   manager with the page's cookies, page-made data by the page's script context */
static void link_save(App *a, Node *n, const char *h) {
    Page *p = a->t->cur;
    char *u = url_join(p->d->url, h); if (!u) return;
    const char *name = node_attr(n, "download");
    if (!strncmp(u, "http:", 5) || !strncmp(u, "https:", 6)) dl_start(u, p->url, name && same_origin(u, p->d->url) ? name : NULL);
    else if (p->js) {
        SB b; sb_init(&b); sb_puts(&b, "__lumenSaveHref("); js_quote(&b, u); sb_putc(&b, ','); js_quote(&b, name); sb_putc(&b, ')');
        char *src = sb_take(&b); js_eval(p->js, src, "lumen:save"); free(src);
    }
    free(u);
}
static void click_page(App *a, float x, float y, unsigned touch_flags) {
    if (panel_shown(a) && x >= a->side && x < page_left(a) && y >= BAR) { a->pan_close = true; return; }
    if (x < page_left(a) || y < BAR) return;
    x = page_x(a, x); y = page_y(a, y);
    if (!a->t->cur || !a->t->cur->L || a->t->asleep) return;   /* a sleeping page is only a picture until its reload lands */
    h_sync_in(a, a->t->cur->d, true);   /* paint may be deferred; hit-test the current DOM, not detached boxes */
    if (vbar_click(a, x, y + a->t->sy)) return;
    Box *b = layout_hit(a->t->cur->L, x, y + a->t->sy, 0, a->t->sy);
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
        click_frame(a, fp, x - (fb->x + fb->b[3] + fb->p[3]), y + a->t->sy - (fb->y + fb->b[0] + fb->p[0]));
        return;
    }
    if (t && a->t->cur->js) {
        JsCtx *js = a->t->cur->js; float cy = y;
        Node *before = a->t->cur->d->focus;
        bool touch = (touch_flags & 8) != 0;
        if (!touch) js_dispatch(js, t, "pointerdown", "PointerEvent", true, true, x, cy, 0, NULL);
        bool md_ok = !(touch_flags & 2);
        if (md_ok) md_ok = touch ? js_touch_compat(js, t, "mousedown", "MouseEvent", x, cy)
            : js_dispatch(js, t, "mousedown", "MouseEvent", true, true, x, cy, 0, NULL);
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
            float px = x, py = y + a->t->sy;
            for (Node *n = a->t->cur->d->node.first; n; n = node_next_in_tree(n, &a->t->cur->d->node))
                if (is_text_ctl(n) && n->box && px >= n->box->x && px < n->box->x + n->box->w && py >= n->box->y && py < n->box->y + n->box->h) { ctl = n; break; }
        }
        if (is_text_ctl(ctl)) focus_node(a, ctl);
        else if (edit_host(t)) { if (!js_focused && md_ok) focus_node(a, edit_host(t)); }
        else if (!js_focused && md_ok && (page_focus(a) || edit_focus(a))) focus_node(a, NULL);   /* a tap outside blurs, hiding the keyboard */
        if (!touch) js_dispatch(js, t, "pointerup", "PointerEvent", true, true, x, cy, 0, NULL);
        if (!(touch_flags & 2)) {
            if (touch) js_touch_compat(js, t, "mouseup", "MouseEvent", x, cy);
            else js_dispatch(js, t, "mouseup", "MouseEvent", true, true, x, cy, 0, NULL);
        }
        bool ok = touch ? js_touch_compat(js, t, "click", "PointerEvent", x, cy)
            : js_dispatch(js, t, "click", "PointerEvent", true, true, x, cy, 0, NULL);
        restyle(a);
        if (!ok) return;
    }
    for (Node *n = t; n; n = n->parent)   /* not b: handlers and restyle() may have rebuilt the layout and freed it */
        if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) {
            const char *h = node_attr(n, "href");
            if (workspace_action(a, h)) return;
            if (!strncmp(h, "javascript:", 11)) return;
            if (fragment_link(a, h)) return;
            if (node_attr(n, "download") || !strncmp(h, "blob:", 5) || !strncmp(h, "data:", 5)) { link_save(a, n, h); return; }
            char *u = url_join(a->t->cur->d->url, h);
            if (u && a->t->cur->js && js_navigate_target(a->t->cur->js, link_target(n), u, NULL, 0, NULL)) { free(u); return; }   /* into a named frame */
            navigate(a, u, true); free(u); return;
        }
}

/* a link to another fragment of the shown document: the page's location.assign, which keeps the
   document and fires popstate and hashchange as Chrome does; without scripts it only scrolls */
static bool fragment_link(App *a, const char *h) {
    Page *p = a->t->cur;
    const char *cur = p->url ? p->url : p->d->url ? p->d->url : "";   /* the location, which pushState moves */
    char *u = url_join(cur, h), *hash = u ? strchr(u, '#') : NULL;
    size_t base = hash ? (size_t)(hash - u) : 0;
    bool same = hash && strlen(cur) >= base && !strncmp(cur, u, base) && (cur[base] == 0 || cur[base] == '#');
    if (same && p->js) {
        SB b; sb_init(&b); sb_puts(&b, "__lumenFragment("); sb_json(&b, u); sb_puts(&b, ")");
        char *src = sb_take(&b); js_eval(p->js, src, "lumen:fragment"); free(src);
        a->dirty = true; restyle(a);
    } else if (same) {
        Node *t = doc_get_element_by_id(p->d, hash + 1);
        if (t && t->box) { a->t->sy = t->box->y; a->dirty = true; }
    }
    free(u);
    return same;
}
static bool over_link(App *a, float x, float y) {
    if (x < page_left(a) || y < BAR) return false;
    x = page_x(a, x); y = page_y(a, y);
    if (!a->t->cur || !a->t->cur->L || a->t->asleep) return false;
    Box *b = layout_hit(a->t->cur->L, x, y + a->t->sy, 0, a->t->sy);
    for (Node *n = b ? b->node : NULL; n; n = n->parent) if (n->type == NODE_ELEMENT && n->tag == A_a && node_attr(n, "href")) return true;
    return false;
}

static void tab_select(App *a, int i) {
    if (a->pan) { int k = 0; while (k < a->ntabs && a->tabs[k] != a->pan) k++; if (k == i) return; panel_close(a, false); if (k < i) i--; }
    if (i < 0 || i >= a->ntabs) return;
    a->ti = i; a->t = a->tabs[i]; a->wi = a->t->ws; a->wslast[a->wi] = a->t; a->editing = false; a->t->relayout = true; a->dirty = true; a->vonly = false;
    if (a->t->asleep && !a->t->loading) navigate(a, a->t->url, false);
    Page *p = a->t->cur;
    SDL_SetWindowTitle(a->win, p && p->d->title && *p->d->title ? p->d->title : "Lumen");
    if (p && (a->t->loading || !p->L)) {
        /* switching shows at once: a frame with the tab already selected and a blank page, then the loop lays out
           the page (it is still loading, or was never laid out) */
        a->t->cur = NULL; render(a); a->t->cur = p;
        a->t->relayout = a->dirty = true; a->vonly = a->sdirty = a->osig_ok = a->gvid_ok = false;
    }
}
static void tab_open(App *a) {
    panel_close(a, false);
    if (!tab_new(a)) return;
    tab_select(a, a->ntabs - 1);
    navigate(a, "lumen://newtab", true);
#ifdef __ANDROID__
    SDL_StopTextInput(a->win);
#else
    a->editing = true; a->t->url[0] = 0; a->us[0] = a->us[1] = 0; SDL_StartTextInput(a->win);
#endif
}
static void tab_free(Tab *t) { if (t->cur) page_free(t->cur); for (int k = 0; k < t->nhist; k++) free(t->hist[k]); free(t->stitle); free(t->wname); free(t); }
static void tab_close(App *a, int i, bool *quit) {
    if (a->pan) { int k = 0; while (k < a->ntabs && a->tabs[k] != a->pan) k++; if (k == i) return; panel_close(a, false); if (k < i) i--; }
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
/* from the main loop, where no page script runs: the tabs window.open showed and window.close closed */
static void popup_service(App *a, bool *quit) {
    for (int i = 0; i < a->ntabs; i++) {
        Tab *t = a->tabs[i];
        if (!t->close_req || a->ntabs == 1 || t == a->pan) { t->close_req = false; continue; }
        uint32_t back = t == a->t ? t->opener_wid : 0;
        tab_close(a, i, quit); i--;
        Tab *o = tab_of_wid(a, back);
        for (int k = 0; o && k < a->ntabs; k++) if (a->tabs[k] == o) tab_select(a, k);
    }
    if (!g_show_wid) return;
    Tab *t = tab_of_wid(a, g_show_wid); g_show_wid = 0;
    if (t && t != a->t && !a->pan) for (int k = 0; k < a->ntabs; k++) if (a->tabs[k] == t) tab_select(a, k);
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
    snprintf(a->wsname[a->nws], sizeof a->wsname[0], "%s", name); a->wsicon[a->nws] = WORKSPACE_DEFAULT_ICON; a->wslast[a->nws++] = NULL;
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
/* Workspaces and their tabs survive a restart (session.txt beside the bookmarks): names, icons, each tab's
   history, address, zoom and title, the shown tab and each workspace's last one. Saved when they change
   (checked every 2 s, and at once when the app goes to the background), through a temporary file; restored
   on a normal start, where only the shown tab loads and the others sleep until first shown. Test launches
   (LUMEN_NO_SESSION, or several start addresses) neither restore nor save. */
static bool g_session = true;
static uint64_t g_session_sig;
static void clean_field(char *s) { for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' '; }
static bool session_tab(const App *a, const Tab *t) { return t->ws >= 0 && t->ws < a->nws && t != a->pan; }
static const char *saved_title(const Tab *t) { return t->cur && t->cur->d && t->cur->d->title && *t->cur->d->title ? t->cur->d->title : t->stitle; }
static uint64_t session_sig(const App *a) {
    uint64_t h = 1469598103934665603ull;
    HMIX(h, a->nws); HMIX(h, a->ti);
    for (int w = 0; w < a->nws; w++) { h = hmix(h, a->wsname[w], strlen(a->wsname[w]) + 1); HMIX(h, a->wsicon[w]); HMIX(h, a->wslast[w]); }
    for (int i = 0; i < a->ntabs; i++) {
        const Tab *t = a->tabs[i]; const char *title = saved_title(t);
        if (!session_tab(a, t)) continue;
        HMIX(h, t); HMIX(h, t->ws); HMIX(h, t->hpos); HMIX(h, t->nhist); HMIX(h, t->zoom_pct);
        for (int k = 0; k < t->nhist; k++) HMIX(h, t->hist[k]);   /* entries are replaced, never edited */
        h = hmix(h, t->url, strlen(t->url) + 1);
        if (title) h = hmix(h, title, strlen(title));
    }
    return h;
}
static void session_save(const App *a) {
    char p[1200], tmp[1300]; pref_path(p, sizeof p, "session.txt"); if (!*g_pref) return;
    snprintf(tmp, sizeof tmp, "%s.new", p);
    FILE *f = fopen(tmp, "w"); if (!f) return;
    int idx[MAX_TABS], n = 0;
    fprintf(f, "lumen-session 1\n");
    for (int w = 0; w < a->nws; w++) { char name[64]; snprintf(name, sizeof name, "%s", a->wsname[w]); clean_field(name); fprintf(f, "ws\t%d\t%s\n", a->wsicon[w], name); }
    for (int i = 0; i < a->ntabs; i++) {
        const Tab *t = a->tabs[i]; idx[i] = -1;
        if (!session_tab(a, t)) continue;
        idx[i] = n++;
        char title[256]; snprintf(title, sizeof title, "%s", saved_title(t) ? saved_title(t) : ""); clean_field(title);
        fprintf(f, "tab\t%d\t%d\t%d\t%s\n", t->ws, t->hpos, t->zoom_pct, title);
        for (int k = 0; k < t->nhist; k++) fprintf(f, "h\t%s\n", t->hist[k]);
        if (*t->url) fprintf(f, "url\t%s\n", t->url);
    }
    fprintf(f, "cur\t%d\n", a->ti >= 0 && a->ti < a->ntabs ? idx[a->ti] : -1);
    for (int w = 0; w < a->nws; w++) for (int i = 0; i < a->ntabs; i++) if (a->tabs[i] == a->wslast[w] && idx[i] >= 0) fprintf(f, "last\t%d\t%d\n", w, idx[i]);
    bool ok = !ferror(f); if (fclose(f)) ok = false;
    if (ok) rename(tmp, p); else remove(tmp);
}
/* the saved workspaces and tabs, every tab asleep: the index of the one to show, or -1 (no tabs saved) */
static int session_load(App *a) {
    char p[1200]; pref_path(p, sizeof p, "session.txt"); FILE *f = *g_pref ? fopen(p, "r") : NULL; if (!f) return -1;
    char *line = NULL; size_t cap = 0; int nws = 0, ntab = 0, sel = -1, last[16]; Tab *list[MAX_TABS], *t = NULL;
    for (int w = 0; w < 16; w++) last[w] = -1;
    bool ok = getline(&line, &cap, f) > 0 && !strncmp(line, "lumen-session 1\n", 16);
    while (ok && getline(&line, &cap, f) > 0) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, '\t'); if (!v) continue; *v++ = 0;
        if (!strcmp(line, "ws") && nws < 16 && !ntab) {
            char *name = strchr(v, '\t'); if (!name) continue; *name++ = 0;
            snprintf(a->wsname[nws], sizeof a->wsname[0], "%s", *name ? name : "Workspace"); a->wsicon[nws] = atoi(v); a->wslast[nws++] = NULL;
        } else if (!strcmp(line, "tab")) {
            int ws, hpos, zoom, used = 0; t = NULL;
            if (sscanf(v, "%d\t%d\t%d%n", &ws, &hpos, &zoom, &used) < 3 || ws < 0 || ws >= nws) continue;
            if (!(t = ntab ? tab_new(a) : a->tabs[0])) continue;   /* the first reuses the start tab */
            t->ws = ws; t->hpos = hpos; t->zoom_pct = LCLAMP(zoom, 0, 300);
            if (v[used] == '\t' && v[used + 1]) t->stitle = xstrdup(v + used + 1);
            list[ntab++] = t;
        } else if (!strcmp(line, "h") && t && t->nhist < 256) t->hist[t->nhist++] = xstrdup(v);
        else if (!strcmp(line, "url") && t) snprintf(t->url, sizeof t->url, "%s", v);
        else if (!strcmp(line, "cur")) sel = atoi(v);
        else if (!strcmp(line, "last")) { int w, k; if (sscanf(v, "%d\t%d", &w, &k) == 2 && w >= 0 && w < 16) last[w] = k; }
    }
    free(line); fclose(f);
    if (nws) a->nws = nws;
    if (!ntab) { a->tabs[0]->ws = 0; return -1; }
    for (int k = 0; k < ntab; k++) {
        t = list[k];
        if (t->nhist) t->hpos = LCLAMP(t->hpos, 0, t->nhist - 1); else t->hpos = -1;
        if (!*t->url) snprintf(t->url, sizeof t->url, "%s", t->nhist ? t->hist[t->hpos] : "lumen://newtab");
        t->asleep = true;
    }
    for (int w = 0; w < a->nws; w++) if (last[w] >= 0 && last[w] < ntab && list[last[w]]->ws == w) a->wslast[w] = list[last[w]];
    Tab *show = list[sel >= 0 && sel < ntab ? sel : 0];
    for (int i = 0; i < a->ntabs; i++) if (a->tabs[i] == show) return i;
    return 0;
}
/* the main loop's check: saves when anything saved changed */
static void session_service(App *a, bool background) {
    static double last; static bool was_bg;
    double now = now_ms(); bool force = background && !was_bg; was_bg = background;
    if (!g_session || (!force && now - last < 2000)) return;
    last = now;
    uint64_t sig = session_sig(a);
    if (sig != g_session_sig) { g_session_sig = sig; session_save(a); }
}
/* a lumen://workspace? link of Lumen's own pages, or the panel's close link; from the panel they wait
   for it to close (panel_service) */
static bool workspace_run(App *app, const char *url);
static bool workspace_action(App *app, const char *url) {
    if (!strcmp(url, "lumen://panel?close")) { if (panel_shown(app)) app->pan_close = true; return true; }
    if (strncmp(url, "lumen://workspace?", 18)) return false;
    if (!app->t->cur || !app->t->cur->internal_ui) return true;
    if (panel_shown(app)) { free(app->pan_go); app->pan_go = xstrdup(url); return true; }
    return workspace_run(app, url);
}
static bool workspace_run(App *app, const char *url) {
    const char *prefix = "lumen://workspace?";
    if (strncmp(url, prefix, strlen(prefix))) return false;
    const char *action = url + strlen(prefix);
    if (!strcmp(action, "new")) {
        if (app->nws >= 16 || app->ntabs >= MAX_TABS) return true;
        char name[64]; snprintf(name, sizeof name, "Workspace %d", app->nws + 1);
        ws_new(app, name);
    } else if (!strcmp(action, "close")) {
        if (app->nws <= 1) return true;
        ws_close(app, app->wi);
    } else if (!strncmp(action, "close=", 6)) {   /* Home: any workspace, staying on this tab unless it closes */
        const char *digits = action + 6;
        size_t length = strlen(digits);
        if (app->nws <= 1 || !length || length > 3 || strspn(digits, "0123456789") != length) return true;
        int index = atoi(digits);
        if (index >= app->nws) return true;
        Tab *here = app->t->ws == index ? NULL : app->t;
        ws_close(app, index);
        for (int i = 0; here && i < app->ntabs; i++) if (app->tabs[i] == here) tab_select(app, i);
    } else if (!strncmp(action, "bmtab=", 6) || !strncmp(action, "bmws=", 5)) {   /* Bookmarks: in a new tab, or in a new workspace */
        bool ws = action[2] == 'w';
        const char *digits = action + (ws ? 5 : 6);
        size_t length = strlen(digits);
        if (!length || length > 3 || strspn(digits, "0123456789") != length) return true;
        int index = atoi(digits);
        if (index >= g_nbm || is_js_url(g_bms[index].url) || app->ntabs >= MAX_TABS || (ws && app->nws >= 16)) return true;
        char *url = xstrdup(g_bms[index].url);
        if (ws) {
            snprintf(app->wsname[app->nws], sizeof app->wsname[0], "Workspace %d", app->nws + 1);
            app->wsicon[app->nws] = WORKSPACE_DEFAULT_ICON; app->wslast[app->nws] = NULL; app->wi = app->nws++;
        }
        if (tab_new(app)) { tab_select(app, app->ntabs - 1); SDL_StopTextInput(app->win); navigate(app, url, true); }
        free(url);
        return true;
    } else if (!strncmp(action, "dup=", 4)) {   /* Home: a copy of a workspace, its tabs with their history; each loads when first shown */
        const char *digits = action + 4;
        size_t length = strlen(digits);
        if (!length || length > 3 || strspn(digits, "0123456789") != length) return true;
        int index = atoi(digits), w = app->nws, n = app->ntabs;
        if (index >= app->nws || app->nws >= 16 || app->ntabs >= MAX_TABS) return true;
        snprintf(app->wsname[w], sizeof app->wsname[0], "%.58s copy", app->wsname[index]);
        app->wsicon[w] = app->wsicon[index]; app->wslast[w] = NULL; app->nws++;
        int keep = app->wi; app->wi = w;   /* tab_new puts tabs in the current workspace */
        for (int i = 0; i < n; i++) {
            Tab *from = app->tabs[i], *t;
            if (from->ws != index || !(t = tab_new(app))) continue;
            for (int k = 0; k < from->nhist; k++) t->hist[k] = xstrdup(from->hist[k]);
            t->nhist = from->nhist; t->hpos = from->hpos; t->desktop = from->desktop;
            snprintf(t->url, sizeof t->url, "%s", from->url);
            t->asleep = *t->url != 0;
        }
        app->wi = keep;
        ws_select(app, w);
    } else if (!strncmp(action, "open=", 5)) {   /* Home: a tab of any workspace */
        const char *digits = action + 5;
        size_t length = strlen(digits);
        if (!length || length > 3 || strspn(digits, "0123456789") != length) return true;
        int index = atoi(digits);
        if (index >= app->ntabs) return true;
        tab_select(app, index);
    } else {
        bool select_workspace = !strncmp(action, "select=", 7);
        if (!select_workspace && strncmp(action, "tab=", 4)) return true;
        const char *digits = action + (select_workspace ? 7 : 4);
        size_t length = strlen(digits);
        if (!length || length > 3 || strspn(digits, "0123456789") != length) return true;
        int index = atoi(digits);
        if (select_workspace) {
            if (index >= app->nws) return true;
            ws_select(app, index);
        } else {
            if (index >= app->ntabs || app->tabs[index]->ws != app->wi) return true;
            tab_select(app, index);
        }
    }
    SDL_StopTextInput(app->win);
    if (app->t->cur && app->t->cur->internal_ui) navigate(app, "lumen://newtab", false);
    return true;
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
    case MENU_WS_SIDEBAR: a->side = sidebar_width(a->side > 0 ? 0 : SIDEW); for (int i = 0; i < a->ntabs; i++) a->tabs[i]->relayout = true; break;
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
#ifdef __ANDROID__
    if (g_hud) {
        size_t heap = 0, ext = 0; js_mem_stats(&heap, &ext);
        android_ui_stats[0] = a->t->pct * 100; android_ui_stats[1] = heap / 1048576.f; android_ui_stats[2] = ext / 1048576.f; android_ui_stats[3] = (float)a->ntabs;
    }
#endif
    if (getenv("LUMEN_CPU_DEBUG")) for (int i = 0; i < a->ntabs; i++) fprintf(stderr, "lumen-cpu: tab %d %.0f%%%s\n", i, a->tabs[i]->pct * 100, a->tabs[i]->limited ? " limited" : "");
}
/* bookmarks t's page unless it already is: the notice to show */
static const char *bm_add(Tab *t) {
    if (!t->cur || !*t->url || !strncmp(t->url, "lumen:", 6)) return "Open a web page to bookmark it";
    if (bm_find(t->url) >= 0) return "Already in bookmarks";
    if (g_nbm >= MAX_BM) return "Bookmark limit reached";
    link_put(g_bms, &g_nbm, MAX_BM, t->url, t->cur->d->title);
    links_save("bookmarks.txt", g_bms, g_nbm);
    return "Added to bookmarks";
}
static void bm_toggle(App *a) {
    if (!a->t->cur || !*a->t->url || !strncmp(a->t->url, "lumen:", 6)) return;
    int i = bm_find(a->t->url);
    if (i >= 0) link_del(g_bms, &g_nbm, i); else link_put(g_bms, &g_nbm, MAX_BM, a->t->url, a->t->cur->d->title);
    links_save("bookmarks.txt", g_bms, g_nbm);
}
#if defined(__APPLE__) || defined(__ANDROID__)
/* the tab menu's choice c (MENU_TAB_*) for t */
static void tab_menu_apply(App *a, Tab *t, int c) {
    if (c == MENU_TAB_VCTL) t->vctl = !vctl_on(t);
    else if (c == MENU_TAB_LITE) t->lite = !lite_on(t);
    else if (c == MENU_TAB_CPU_DEFAULT || c == MENU_TAB_CPU_NEVER) { t->cpumode = c == MENU_TAB_CPU_NEVER; tab_unlimit(t); t->unlimit_until = 0; }
    else if (c > MENU_TAB_CPU_LIM) { t->cpumode = 2; t->limited = true; t->info = false; t->lim = (c - MENU_TAB_CPU_LIM) / 100.f; t->budget = 0; t->bud_t = now_ms(); }
    a->dirty = true; a->vonly = false;
}
static void tab_menu_default(char *def, size_t n) {
    if (g_cpu_on) snprintf(def, n, "Default (over %d%% for %d s \xE2\x86\x92 %d%%)", g_cpu_pct, g_cpu_secs, g_cpu_lim); else snprintf(def, n, "Default (limit off)");
}
#endif
#ifdef __ANDROID__
/* the tab menu (a double tap on the tab bar): Close tab, then the desktop tab menu's choices */
static Tab *g_tab_menu_tab;
static const int g_tab_menu_codes[] = { 0, MENU_TAB_CPU_DEFAULT, MENU_TAB_CPU_NEVER, MENU_TAB_CPU_LIM + 20, MENU_TAB_CPU_LIM + 40, MENU_TAB_CPU_LIM + 60, MENU_TAB_VCTL, MENU_TAB_LITE };
static void tab_menu(App *a, int i) {
    Tab *t = a->tabs[i]; char def[96], lim[3][32];
    tab_menu_default(def, sizeof def);
    def[0] = (char)tolower((unsigned char)def[0]);
    char cpu[112]; snprintf(cpu, sizeof cpu, "CPU limit: %s", def);
    for (int k = 0; k < 3; k++) snprintf(lim[k], sizeof lim[k], "CPU limit: %d%%", 20 * (k + 1));
    const char *items[] = { "Close tab", cpu, "CPU limit: never", lim[0], lim[1], lim[2], "Always show video controls", "Lite mode (release off-screen media to save RAM)" };
    int pct = (int)(t->lim * 100 + 0.5f);
    bool on[] = { false, t->cpumode == 0, t->cpumode == 1, t->cpumode == 2 && pct == 20, t->cpumode == 2 && pct == 40, t->cpumode == 2 && pct == 60, vctl_on(t), lite_on(t) };
    const char *title = t->cur && t->cur->d && t->cur->d->title && *t->cur->d->title ? t->cur->d->title : t->stitle ? t->stitle : *t->url ? t->url : "New Tab";
    g_tab_menu_tab = t;
    android_ui_tab_menu(title, items, on, (int)(sizeof items / sizeof *items));
}
#endif
static void tab_popup(App *a, int i, float x, float y) {
#ifdef __APPLE__
    Tab *t = a->tabs[i]; char def[96];
    tab_menu_default(def, sizeof def);
    int c = mac_tab_menu(a->win, x, y, t->cpumode, (int)(t->lim * 100 + 0.5f), vctl_on(t), lite_on(t), def);
    tab_menu_apply(a, t, c);
#else
    (void)a; (void)i; (void)x; (void)y;
#endif
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
#ifdef __ANDROID__
static void ui_action_post(int code, int value, char *s1, char *s2);
/* Lumen's own file chooser in the panel; its button for the system's picker sends it on (any thread) */
static void pick_files(bool multiple, const char *accept) { ui_action_post(UI_FILE_PICK, multiple, xstrdup(accept ? accept : ""), NULL); }
#else
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
#endif
static SDL_HitTestResult win_hit(SDL_Window *w, const SDL_Point *pt, void *ud) {
    (void)w;
    App *app = ud;
    float hit_x = (float)pt->x * app->input_scale, hit_y = (float)pt->y * app->input_scale;
    return hit_y < (g_compact ? TB : TABH) && bar_hit(app, hit_x, hit_y) == HB_NONE ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
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

/* short confirmations: an Android toast; elsewhere the notice bar */
static void ui_toast(App *a, const char *text) {
#ifdef __ANDROID__
    (void)a; android_ui_toast(text);
#else
    snprintf(g_notice, sizeof g_notice, "%s", text); a->dirty = true; a->vonly = false;
#endif
    fprintf(stderr, "lumen: %s\n", text);
}
static bool is_js_url(const char *u) { while (*u == ' ') u++; return !strncasecmp(u, "javascript:", 11); }
/* A javascript: URL typed in the address bar or opened from a bookmark runs on the page being viewed. */
static void run_js_url(App *a, const char *url) {
    while (*url == ' ') url++;
    Page *p = a->t->cur;
    a->editing = a->umenu = false;
    snprintf(a->t->url, sizeof a->t->url, "%s", p ? p->url : a->t->hpos >= 0 ? a->t->hist[a->t->hpos] : "");
    a->dirty = true; a->vonly = false;
    if (!p || !p->js || p->internal_ui) { ui_toast(a, "JavaScript runs on web pages only"); return; }
    char *src = url_decode(url + 11, strlen(url + 11)), *result = NULL, msg[300];
    bool failed = false;
    TAB_CALL(a->t, result = js_eval_result(p->js, src, "javascript:", &failed));
    if (failed) snprintf(msg, sizeof msg, "JavaScript error: %s", result ? result : "unknown");
    else if (result) snprintf(msg, sizeof msg, "JavaScript ran: %s", result);
    else snprintf(msg, sizeof msg, "JavaScript ran");
    ui_toast(a, msg);
    free(src); free(result);
    restyle(a);
}
static void desktop_sites_load(void) {
    char p[1200]; pref_path(p, sizeof p, "desktop_sites.txt"); FILE *f = *g_pref ? fopen(p, "r") : NULL; if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n \t")] = 0; if (*line) net_desktop_restore(line); }
    fclose(f);
}
/* bookmark files are one "url<TAB>title" per line: code in javascript: bookmarks keeps its line breaks escaped */
static char *bm_url_clean(const char *u) {
    while (*u == ' ' || *u == '\n' || *u == '\r' || *u == '\t') u++;
    SB b; sb_init(&b);
    for (; *u; u++) {
        if (*u == '\n') sb_puts(&b, "%0A"); else if (*u == '\r') sb_puts(&b, "%0D"); else if (*u == '\t') sb_puts(&b, "%09");
        else sb_putc(&b, *u);
    }
    char *r = sb_take(&b);
    size_t n = strlen(r); while (n && r[n - 1] == ' ') r[--n] = 0;
    if (*r && !is_js_url(r)) { char *full = normalize_url(r); free(r); r = full; }
    return r;
}
/* the bookmark at index (or a new one, -1) set to title and url: its notice, or NULL when url is empty */
static const char *bm_store(int index, const char *title, const char *raw) {
    char *url = raw ? bm_url_clean(raw) : NULL;
    if (!url || !*url) { free(url); return NULL; }
    if (!title || !*title) title = url;
    int i = index >= 0 && index < g_nbm ? index : bm_find(url);
    if (i >= 0) {
        free(g_bms[i].url); free(g_bms[i].title);
        g_bms[i].url = url; g_bms[i].title = xstrdup(title);
        for (char *c = g_bms[i].title; *c; c++) if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' ';
    } else if (g_nbm < MAX_BM) { link_put(g_bms, &g_nbm, MAX_BM, url, title); free(url); }
    else { free(url); return "Bookmark limit reached"; }
    links_save("bookmarks.txt", g_bms, g_nbm);
    return "Bookmark saved";
}
#ifdef __ANDROID__
static void desktop_sites_save(void) {
    char hosts[64 * 130]; net_desktop_hosts(hosts, sizeof hosts);
    char p[1200]; pref_path(p, sizeof p, "desktop_sites.txt"); FILE *f = *g_pref ? fopen(p, "w") : NULL; if (!f) return;
    for (char *h = strtok(hosts, " "); h; h = strtok(NULL, " ")) fprintf(f, "%s\n", h);
    fclose(f);
}
static bool page_is_web(const Tab *t) { return t->cur && !t->cur->internal_ui && !strncmp(t->url, "http", 4); }
/* "Desktop site" on a web page is for its site; on any other page ("*") for every site */
static bool desktop_on(const Tab *t) { return page_is_web(t) ? t->desktop : net_desktop_host("*"); }
static AndroidMenuState menu_state(App *a) {
    Tab *t = bar_tab(a);
    return (AndroidMenuState){
        .can_forward = t->hpos < t->nhist - 1, .desktop = desktop_on(t), .can_desktop = page_is_web(t) ? 1 : 2,
        .can_zoom = true, .zoom_pct = t->zoom_pct ? t->zoom_pct : g_page_zoom_pct,
        .bookmarked = bm_find(t->url) >= 0, .can_bookmark = t->cur && *t->url && strncmp(t->url, "lumen:", 6),
        .can_back = t->hpos > 0, .fullscreen = g_fullscreen,
    };
}
static void show_more_menu(App *a) { AndroidMenuState st = menu_state(a); android_ui_menu(&st); }
/* hand the toolbar row's state to its Android view (android_ui_toolbar drops repeats) */
static void bar_push(App *a) {
    Tab *t = bar_tab(a); AndroidBarState st; memset(&st, 0, sizeof st);
    float s = a->scale, x0 = bar_x0(a);
    st.left = (int)lroundf(x0 * s); st.top = (int)lroundf(TABH * s);
    st.width = (int)lroundf((a->vw - x0) * s); st.height = (int)lroundf(TB * s);
    g_slot_n = st.nslot = a->vw - x0 >= 600 ? 4 : 2;   /* landscape phones and tablets */
    for (int i = 0; i < st.nslot; i++) {
        int code = g_slot[i]; st.slot[i] = code;
        st.slot_enabled[i] = code == UI_BACK ? t->hpos > 0 : code == UI_FORWARD ? t->hpos < t->nhist - 1 : true;
        st.slot_lit[i] = code == UI_BM_TOGGLE ? bm_find(t->url) >= 0 : code == UI_DESKTOP ? desktop_on(t) : code == UI_FULLSCREEN && g_fullscreen;
    }
    st.loading = t->loading; st.menu = menu_state(a); st.hud = g_hud; st.touch_feedback = g_touch_fb;
    st.touch_rgb = g_touch_fb_rgb; st.touch_alpha = g_touch_fb_alpha; st.touch_size = g_touch_fb_size; st.touch_ms = g_touch_fb_ms;
    android_ui_toolbar(&st, t->url);
}
static void show_bookmarks(App *a) {
    const char *titles[MAX_BM], *urls[MAX_BM];
    for (int i = 0; i < g_nbm; i++) { titles[i] = g_bms[i].title; urls[i] = g_bms[i].url; }
    Tab *t = bar_tab(a); bool page = t->cur && *t->url && strncmp(t->url, "lumen:", 6);
    const char *title = page && t->cur->d->title && *t->cur->d->title ? t->cur->d->title : t->url;
    android_ui_bookmarks(titles, urls, g_nbm, page ? title : NULL, page ? t->url : NULL);
}
static void show_workspaces(App *a) {
    const char *names[16]; int tabs[16], v[MAX_TABS];
    for (int i = 0; i < a->nws; i++) { names[i] = a->wsname[i]; tabs[i] = ws_tabs(a, i, v); }
    android_ui_workspaces(names, tabs, a->nws, a->wi);
}
/* The menu's Bookmarks, History, Downloads, Workspaces and Settings: Lumen's new tab page at that
   section in a panel at the right below the toolbar, floating over a picture of the page it covers.
   The panel is a tab of no workspace that is shown (a->t) while a->ti stays on the covered tab; links
   it opens and workspace changes wait for the main loop (panel_service), which closes it first, as
   its own scripts may be running when they are asked for. */
static void panel_snapshot(App *a) {
    int bar_px = (int)(BAR * a->scale), w = a->frame.w, h = a->frame.h - bar_px;
    free(a->pan_bg); a->pan_bg = NULL;
    if (w <= 0 || h <= 0 || !a->frame.px) return;
    if (a->frame_page_stale) { compose_page(a, 0, h, bar_px, (int)(page_left(a) * a->scale)); a->frame_page_stale = false; }
    if (!(a->pan_bg = malloc((size_t)w * (size_t)h * 4))) return;
    a->pan_bgw = w; a->pan_bgh = h;
    for (int y = 0; y < h; y++) {
        const uint32_t *from = a->frame.px + (size_t)(y + bar_px) * (size_t)a->frame.stride; uint32_t *to = a->pan_bg + (size_t)y * (size_t)w;
        memcpy(to, from, (size_t)w * 4);
    }
}
/* the picture left of the panel as it was, undimmed; a hairline marks the panel's edge */
static void panel_backdrop(App *a, int bar_px, int width) {
    int x0 = (int)(a->side * a->scale), x1 = LMIN(x0 + width, a->frame.w), line = LMAX(1, (int)a->scale);
    for (int y = bar_px; y < a->frame.h && x1 > x0; y++) {
        uint32_t *row = a->frame.px + (size_t)y * (size_t)a->frame.stride;
        const uint32_t *pic = a->pan_bg && a->pan_bgw == a->frame.w && y - bar_px < a->pan_bgh ? a->pan_bg + (size_t)(y - bar_px) * (size_t)a->pan_bgw : NULL;
        for (int x = x0; x < x1; x++) row[x] = x1 - x <= line ? 0xffdadce0 : pic ? pic[x] : 0xffffffff;
    }
}
static void panel_open_at(App *a, const char *section, bool keep_picture) {
    char url[64]; snprintf(url, sizeof url, "lumen://newtab?s=%s", section);
    if (a->pan) {   /* open: the same section again closes it */
        if (!strcmp(a->pan->url, url)) panel_close(a, false); else navigate(a, url, false);
        return;
    }
    if (a->ntabs >= MAX_TABS) { navigate(a, url, true); return; }
    if (!keep_picture || !a->pan_bg) panel_snapshot(a);
    Tab *under = a->t, *p = tab_new(a);
    if (!p) return;
    p->ws = -1;
    a->pan = p; a->pan_under = under; a->t = p;
    if (a->editing) { a->editing = false; SDL_StopTextInput(a->win); }
    navigate(a, url, true);
    a->dirty = true; a->vonly = a->osig_ok = a->gvid_ok = false;
}
static void panel_open(App *a, const char *section) { panel_open_at(a, section, false); }
static void panel_close(App *a, bool keep_picture) {
    Tab *p = a->pan;
    if (!p) return;
    a->pan = NULL; a->pan_close = false;
    if (g_pick.wait) { g_pick.wait = false; js_files_picked(NULL, -1); }   /* the file chooser closed without a choice */
    int i = 0; while (i < a->ntabs && a->tabs[i] != p) i++;
    if (i < a->ntabs) {
        memmove(a->tabs + i, a->tabs + i + 1, sizeof(Tab *) * (size_t)(a->ntabs - i - 1)); a->ntabs--;
        if (a->ti > i) a->ti--;
    }
    a->t = a->ti >= 0 && a->ti < a->ntabs ? a->tabs[a->ti] : a->tabs[0];
    a->pan_under = NULL;
    if (a->pan_dead) tab_free(a->pan_dead);
    a->pan_dead = p;   /* freed by panel_service: its page may be on the stack */
    if (!keep_picture) { free(a->pan_bg); a->pan_bg = NULL; }
    if (a->t->cur && a->t->cur->js) js_touch(a->t->cur->js, NULL, "reset", 0, 0, 0, 0, 0);
    touch_frames_reset();
    android_touch_reset(&a->touch);
    android_fling_reset(&a->fling);
    a->t->relayout = true; a->dirty = true; a->vonly = a->osig_ok = a->gvid_ok = false;
    publish_gens(a);
}
/* the main loop, where no page script runs */
static void panel_service(App *a) {
    if (a->pan_dead) { tab_free(a->pan_dead); a->pan_dead = NULL; }
    if (!a->pan || (!a->pan_close && !a->pan_go)) { a->pan_close = false; return; }
    char *go = a->pan_go; a->pan_go = NULL;
    bool home = go && !strncmp(go, "lumen://workspace?close=", 24);   /* closing another workspace keeps Home up */
    panel_close(a, home);
    if (go && is_js_url(go)) run_js_url(a, go);
    else if (go && !workspace_run(a, go)) navigate(a, go, true);
    if (home && a->t->ws >= 0) panel_open_at(a, "home", true);
    free(go);
}
typedef struct { int code, value; char *s1, *s2; } UiAction;
static void ui_action_post(int code, int value, char *s1, char *s2) {   /* Android UI thread */
    UiAction *u = malloc(sizeof *u);
    if (!u) { free(s1); free(s2); return; }
    u->code = code; u->value = value; u->s1 = s1; u->s2 = s2;
    SDL_Event ev; SDL_zero(ev); ev.type = EV_UI; ev.user.data1 = u;
    if (!SDL_PushEvent(&ev)) { free(s1); free(s2); free(u); }
}
/* a menu action not in the toolbar takes the place of the toolbar button used least recently */
static void slot_used(App *a, int code) {
    if (!slot_action(code)) return;
    for (int i = 0; i < g_slot_n; i++) if (g_slot[i] == code) { g_slot_seq[i] = ++g_slot_clock; return; }
    int k = 0; for (int i = 1; i < g_slot_n; i++) if (g_slot_seq[i] < g_slot_seq[k]) k = i;
    /* a slot hidden on a narrow window that held it takes the action it replaces, so none shows twice */
    for (int i = g_slot_n; i < NSLOT; i++) if (g_slot[i] == code) { g_slot[i] = g_slot[k]; g_slot_seq[i] = g_slot_seq[k]; }
    g_slot[k] = code; g_slot_seq[k] = ++g_slot_clock;
    settings_save(); a->dirty = true; a->vonly = false;
}
/* the site of a tab's page, "" when it is not a web page */
static void tab_site(const Tab *t, char *out, size_t n) {
    URL pu; *out = 0;
    if (!page_is_web(t) || !url_parse(t->url, &pu)) return;
    if (pu.host) net_site_of(pu.host, out, n);
    url_free(&pu);
}
/* Site info's "Delete data": the cookies of the page's site and its subdomains go, the page reloads
   with empty storage, and other open pages of the site lose their localStorage and sessionStorage
   (Lumen keeps those in memory per page; it has no service workers, IndexedDB or Cache Storage). */
static void site_clear(App *a, const char *url) {
    URL su; char site[256] = "", msg[400];
    if (!url || !url_parse(url, &su)) return;
    if (su.host && *su.host && (!strcmp(su.scheme, "https") || !strcmp(su.scheme, "http"))) net_site_of(su.host, site, sizeof site);
    url_free(&su);
    if (!*site) return;
    int cookies = cookies_site(site, true);
    Tab *bar = bar_tab(a);
    for (int i = 0; i < a->ntabs; i++) {
        Tab *t = a->tabs[i]; char ts[256];
        tab_site(t, ts, sizeof ts);
        if (strcmp(ts, site)) continue;
        if (t == bar) { if (t->hpos >= 0) navigate(a, t->hist[t->hpos], false); }
        else if (t->cur && t->cur->js) TAB_CALL(t, js_eval(t->cur->js, "try{localStorage.clear();sessionStorage.clear()}catch(e){}", "lumen:site-data"));
    }
    if (cookies) snprintf(msg, sizeof msg, "Deleted data of %s (%d cookie%s)", site, cookies, cookies == 1 ? "" : "s");
    else snprintf(msg, sizeof msg, "Deleted data of %s", site);
    ui_toast(a, msg);
}
/* a long-pressed image's menu: Copy image (onto the clipboard and its history), Download image, Copy link address
   (an enclosing link), Copy image address, Share */
enum { CTX_COPY_IMG, CTX_SAVE_IMG, CTX_COPY_LINK, CTX_COPY_IMG_URL, CTX_SHARE };
static struct { char *img, *link; int act[5], n; } g_ctx;
/* the <img> under page point x, y in n's subtree (a hit can land on the link or figure around it) */
static Node *img_under(Node *n, float x, float y, int depth) {
    for (Node *c = n ? n->first : NULL; c && depth < 8; c = c->next) {
        if (c->type != NODE_ELEMENT) continue;
        const Box *b = c->box;
        if (c->tag == A_img && b && x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) return c;
        Node *r = img_under(c, x, y, depth + 1); if (r) return r;
    }
    return NULL;
}
static bool img_ctx_menu(App *a, Node *n, float x, float y) {
    Page *p = a->t->cur; Node *img = NULL, *link = NULL;
    for (Node *k = n; k; k = k->parent) { if (!img && k->tag == A_img) img = k; if (!link && k->tag == A_a && node_attr(k, "href")) link = k; }
    if (!img) img = img_under(link ? link : n, x, y, 0);
#if !defined(NDEBUG)
    __android_log_print(ANDROID_LOG_INFO, "LumenUi", "long press node=%s img=%d link=%d", n && n->tag ? n->tag : "-", img != NULL, link != NULL);
#endif
    const char *s = img ? node_attr(img, "src") : NULL, *u = s && *s ? node_src_url(img, s) : NULL;
    if (!u || !p || !p->d) return false;
    free(g_ctx.img); free(g_ctx.link);
    g_ctx.img = xstrdup(u); g_ctx.link = link ? url_join(p->d->url, node_attr(link, "href")) : NULL;
    Image *im = url_img(g_ctx.img);
    const char *items[5]; int k = 0;
    if (im && im->px) { items[k] = "Copy image"; g_ctx.act[k++] = CTX_COPY_IMG; }
    items[k] = "Download image"; g_ctx.act[k++] = CTX_SAVE_IMG;
    if (g_ctx.link) { items[k] = "Copy link address"; g_ctx.act[k++] = CTX_COPY_LINK; }
    items[k] = "Copy image address"; g_ctx.act[k++] = CTX_COPY_IMG_URL;
    items[k] = "Share image"; g_ctx.act[k++] = CTX_SHARE;
    g_ctx.n = k;
    const char *alt = node_attr(img, "alt");
    android_ui_list_menu(alt && *alt ? alt : g_ctx.link ? g_ctx.link : g_ctx.img, items, k);
    return true;
}
/* the image as a PNG under the cache's clip directory, for the clipboard and the share sheet */
static bool ctx_png(const char *u, char *path, size_t n) {
    Image *im = url_img(u); const char *cache = SDL_GetAndroidCachePath();
    if (!im || !im->px || im->w <= 0 || im->h <= 0 || !cache) return false;
    snprintf(path, n, "%s/clip", cache); mkdir(path, 0700);
    snprintf(path, n, "%s/clip/image.png", cache);
    return png_write(path, im->px, im->w, im->h, im->w);
}
static void ctx_do(App *a, int i) {
    Page *p = a->t->cur; char path[1024];
    if (i < 0 || i >= g_ctx.n || !g_ctx.img || !p) return;
    switch (g_ctx.act[i]) {
    case CTX_COPY_IMG: {
        if (!ctx_png(g_ctx.img, path, sizeof path)) { ui_toast(a, "The image is not loaded"); break; }
        FILE *f = fopen(path, "rb"); unsigned char *b = NULL; long len = 0;
        if (f && !fseek(f, 0, SEEK_END) && (len = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET)) { b = xmalloc((size_t)len); if (fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; } }
        if (f) fclose(f);
        if (b) clip_record(NULL, b, (size_t)len, "image/png");   /* Paste's history shows it */
        free(b);
        android_ui_clip_image_file(path);
        ui_toast(a, "Image copied");
        break; }
    case CTX_SAVE_IMG:
        if (!strncmp(g_ctx.img, "http:", 5) || !strncmp(g_ctx.img, "https:", 6)) dl_start(g_ctx.img, p->url, NULL);
        else if (p->js) {   /* data: and blob: images, by the page's script context */
            SB b; sb_init(&b); sb_puts(&b, "__lumenSaveHref("); js_quote(&b, g_ctx.img); sb_puts(&b, ",null)");
            char *src = sb_take(&b); js_eval(p->js, src, "lumen:save"); free(src);
        }
        break;
    case CTX_COPY_LINK: case CTX_COPY_IMG_URL: {
        const char *t = g_ctx.act[i] == CTX_COPY_LINK && g_ctx.link ? g_ctx.link : g_ctx.img;
        SDL_SetClipboardText(t); clip_record(t, NULL, 0, NULL); ui_toast(a, "Link copied");
        break; }
    case CTX_SHARE:
        if (ctx_png(g_ctx.img, path, sizeof path)) android_ui_share_image_file(path); else android_share_text(g_ctx.img);
        break;
    }
}
/* the browser menu's Copy: a field's selection, else the page's (native or as the page's script sees it) */
static void menu_copy(App *a) {
    Page *p = a->t->cur; Node *f = page_focus(a); bool failed = false;
    if (!p) return;
    char *t = f ? ctl_selected(f) : NULL;
    if ((!t || !*t) && g_tsel.on && p->L && g_tsel.L == p->L) { free(t); t = tsel_text(p->L); }
    if ((!t || !*t) && p->js) { free(t); t = js_eval_result(p->js, "String(getSelection())", "lumen-copy", &failed); if (failed) { free(t); t = NULL; } }
    if (t && *t) { SDL_SetClipboardText(t); clip_record(t, NULL, 0, NULL); ui_toast(a, "Copied"); } else ui_toast(a, "Nothing selected to copy");
    free(t);
}
static void ui_action(App *a, UiAction *u, bool *quit) {
    (void)quit;
#if defined(__ANDROID__) && !defined(NDEBUG)
    __android_log_print(ANDROID_LOG_INFO, "LumenUi", "action code=%d value=%d", u->code, u->value);
#endif
    slot_used(a, u->code);
    if (u->code == UI_FILE_PICK) g_pick.wait = false;   /* a new request replaces one still open */
    /* the panel's own sections switch it; bookmark edits refresh it; anything else is for the page under it */
    bool section = u->code == UI_BOOKMARKS || u->code == UI_HISTORY || u->code == UI_DOWNLOADS || u->code == UI_WORKSPACES || u->code == UI_SETTINGS;
    if (a->pan && !section && u->code != UI_BM_SAVE && u->code != UI_BM_DELETE && u->code != UI_BM_ADD) panel_close(a, false);
    Tab *t = bar_tab(a);
    switch (u->code) {
    case UI_NEW_TAB: tab_open(a); break;
    case UI_BACK: history_go(a, -1); break;
    case UI_RELOAD: if (t->hpos >= 0) navigate(a, t->hist[t->hpos], false); break;
    case UI_FULLSCREEN: g_fullscreen = u->value != 0; android_ui_fullscreen(g_fullscreen); break;
    case UI_FORWARD: history_go(a, 1); break;
    case UI_BOOKMARKS: panel_open(a, "bookmarks"); break;
    case UI_WORKSPACES: panel_open(a, "home"); break;
    case UI_DOWNLOADS: panel_open(a, "downloads"); break;
    case UI_HISTORY: panel_open(a, "history"); break;
    case UI_SETTINGS: panel_open(a, "settings"); break;
    case UI_KEYBOARD: g_kbd_sticky = true; g_kbd_show_at = now_ms() + 350; a->dirty = true; break;
    case UI_COPY: menu_copy(a); break;
    case UI_CTX_MENU: ctx_do(a, u->value); break;
    case UI_PASTE: page_paste(a, page_focus(a), page_focus(a) ? NULL : edit_focus(a)); restyle(a); a->dirty = true; break;   /* else the body */
    case UI_UNDO: case UI_REDO: key_push_mod(SDLK_Z, SDL_KMOD_LCTRL | (u->code == UI_REDO ? SDL_KMOD_LSHIFT : 0)); break;
    case UI_EDIT_MORE: { float vw = page_width(a), bar[4] = { vw - 4, -2, vw - 4, -2 }; pop_open(a, 1, bar); g_epop.free = g_epop.kind != 0; a->dirty = true; break; }
    case UI_FILE_PICK:
        g_pick.multi = u->value != 0; snprintf(g_pick.accept, sizeof g_pick.accept, "%s", u->s1 ? u->s1 : "");
        if (a->ntabs >= MAX_TABS) { android_ui_pick_files(g_pick.accept, g_pick.multi); break; }   /* no room for the panel */
        g_pick.wait = true; panel_open(a, "pick");
        break;
    case UI_DESKTOP: {
        bool on = u->value != 0;
        if (!page_is_web(t)) {
            net_set_desktop_host("*", on); desktop_sites_save();
            ui_toast(a, on ? "Desktop site on for all sites" : "Desktop site off for all sites");
            break;
        }
        URL pu;
        if (!url_parse(t->url, &pu)) break;
        net_set_desktop_host(pu.host, on); url_free(&pu);   /* under "*", off excludes only this site */
        desktop_sites_save();
        if (t->hpos >= 0) navigate(a, t->hist[t->hpos], false);
        break; }
    case UI_ZOOM:
        { int pct = LCLAMP(u->value, 20, 300), want = pct == g_page_zoom_pct ? 0 : pct;
          if (want != t->zoom_pct) { t->zoom_pct = want; page_zoom_changed(a); } }
        break;
    case UI_BM_TOGGLE: {
        bool was = bm_find(t->url) >= 0; bm_toggle(a);
        if ((bm_find(t->url) >= 0) != was) ui_toast(a, was ? "Bookmark removed" : "Bookmarked");
        break; }
    case UI_BM_OPEN: case UI_URL_GO:
        if (!u->s2 || !*u->s2) break;
        if (is_js_url(u->s2)) run_js_url(a, u->s2); else { SDL_StopTextInput(a->win); navigate(a, u->s2, true); }
        break;
    case UI_BM_SAVE: {
        const char *msg = bm_store(u->value, u->s1, u->s2);
        if (!msg) break;
        ui_toast(a, msg);
        show_bookmarks(a);
        break; }
    case UI_BM_ADD: bm_add(t); break;
    case UI_BM_DELETE: {
        int i = u->value >= 0 && u->value < g_nbm && u->s2 && !strcmp(g_bms[u->value].url, u->s2) ? u->value : u->s2 ? bm_find(u->s2) : -1;
        if (i < 0) break;
        link_del(g_bms, &g_nbm, i); links_save("bookmarks.txt", g_bms, g_nbm);
        ui_toast(a, "Bookmark deleted");
        show_bookmarks(a);
        break; }
    case UI_WS_SELECT: if (u->value != a->wi) ws_select(a, u->value); break;
    case UI_WS_NEW:
        if (a->nws >= 16 || a->ntabs >= MAX_TABS) { ui_toast(a, "Workspace limit reached"); break; }
        ws_new(a, u->s1 && *u->s1 ? u->s1 : "Workspace");
        break;
    case UI_WS_RENAME: if (u->value >= 0 && u->value < a->nws && u->s1 && *u->s1) snprintf(a->wsname[u->value], sizeof a->wsname[0], "%s", u->s1); break;
    case UI_WS_CLOSE: if (u->value >= 0 && u->value < a->nws && a->nws > 1) ws_close(a, u->value); break;
    case UI_TAB_MENU: {
        int i = 0; while (i < a->ntabs && a->tabs[i] != g_tab_menu_tab) i++;
        if (i == a->ntabs || u->value < 0 || u->value >= (int)(sizeof g_tab_menu_codes / sizeof *g_tab_menu_codes)) break;
        if (u->value) { tab_menu_apply(a, a->tabs[i], g_tab_menu_codes[u->value]); break; }
        Tab *mt = g_tab_menu_tab; g_tab_menu_tab = NULL;
        if (a->ntabs == 1) tab_open(a);   /* the last tab gives way to a new one rather than closing Lumen */
        for (i = 0; i < a->ntabs && a->tabs[i] != mt; i++) {}
        if (i < a->ntabs) tab_close(a, i, quit);
        break; }
    case UI_SITE_CLEAR: site_clear(a, u->s2); break;
    }
    if (panel_shown(a) && (u->code == UI_BM_SAVE || u->code == UI_BM_DELETE || (u->code == UI_BM_ADD && strstr(a->t->url, "s=bookmarks"))))
        navigate(a, "lumen://newtab?s=bookmarks", false);
    a->dirty = true; a->vonly = false;
}
#else
static void panel_close(App *a, bool keep_picture) { (void)a; (void)keep_picture; }
static void panel_backdrop(App *a, int bar_px, int width) { (void)a; (void)bar_px; (void)width; }
static void panel_service(App *a) { (void)a; }
#endif
/* Download events: a navigation that became a download leaves the tab where it was; the
   Downloads page follows progress; starts, completions and failures get a toast. */
static struct { uint64_t id; int state; } g_dl_seen[64]; static int g_dl_nseen, g_dl_seen_next;
static void dl_track(App *a, bool announce) {
    DlInfo v[32]; int n = dl_list(v, 32);
    for (int i = n - 1; i >= 0; i--) {
        int k = 0; while (k < g_dl_nseen && g_dl_seen[k].id != v[i].id) k++;
        bool known = k < g_dl_nseen;
        if (!known) { if (g_dl_nseen < 64) k = g_dl_nseen++; else { k = g_dl_seen_next; g_dl_seen_next = (g_dl_seen_next + 1) % 64; } g_dl_seen[k].id = v[i].id; g_dl_seen[k].state = -1; }
        if (g_dl_seen[k].state == v[i].state) continue;
        g_dl_seen[k].state = v[i].state;
        if (!announce) continue;
        char msg[320];
        if (v[i].state == DL_RUNNING && !known) snprintf(msg, sizeof msg, "Downloading %s", v[i].name);
        else if (v[i].state == DL_DONE) snprintf(msg, sizeof msg, "Downloaded %s", v[i].name);
        else if (v[i].state == DL_FAILED) snprintf(msg, sizeof msg, "Download failed: %s", v[i].name);
        else continue;
        ui_toast(a, msg);
    }
}
static void dl_event(App *a, const SDL_Event *ev) {
    if (ev->user.code == 1) {
        uint64_t gen = (uint64_t)(uintptr_t)ev->user.data1;
        for (int i = 0; i < a->ntabs; i++) {
            Tab *t = a->tabs[i];
            if (t->lgen != gen) continue;
            t->loading = false;
            if (t->cur && t->hpos > 0 && t->hpos == t->nhist - 1 && t->hgen[t->hpos] == gen) { free(t->hist[t->hpos]); t->nhist--; t->hpos--; }
            if (t->cur && !(a->editing && t == a->t)) snprintf(t->url, sizeof t->url, "%s", t->cur->url);
        }
    } else SDL_SetAtomicInt(&g_dl_posted, 0);
    dl_track(a, true);
    downloads_page_update(a);
    a->dirty = true; a->vonly = false;
}

#ifdef __ANDROID__
static bool SDLCALL android_graphics_lifecycle(void *userdata, SDL_Event *event) {
    App *app = userdata;
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
        app->android_suspended = true;
        gpu_suspend(app->gpu);
    } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND && app->android_suspended) {
        if (app->gpu) {
            GpuSurfSrc source = { .kind = GPU_SURF_ANDROID,
                .a = SDL_GetPointerProperty(SDL_GetWindowProperties(app->win), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL) };
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(app->win, &width, &height);
            if (!gpu_resume(app->gpu, &source, width, height)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Lumen: unable to restore the Android GPU surface");
                return true;
            }
        }
        app->android_suspended = false;
        app->dirty = true;
        app->vonly = false;
        app->gvid_ok = false;
    }
    return true;
}

static void android_handle_back(App *app) {
    if (tsel_ui(app)) { tsel_clear(app); return; }
    if (app->pan && !app->editing) { panel_close(app, false); return; }
    if (g_fs_page) { page_fs_exit(app); return; }
    /* Like Chrome, Back only drops the focus (and keyboard) while the keyboard is up: once the IME has hidden
       it, a focused field no longer swallows Back, so the next press goes back in history. */
    bool kbd_up = SDL_ScreenKeyboardShown(app->win);
    AndroidBackAction act = android_back_action(g_hud_pop, app->editing, kbd_up && (page_focus(app) || edit_focus(app)), app->t->hpos > 0);
    SDL_Log("Lumen: back: action %d, history %d of %d", (int)act, app->t->hpos, app->t->nhist);
    switch (act) {
    case ANDROID_BACK_DISMISS_POPUP:
        g_hud_pop = false;
        break;
    case ANDROID_BACK_CANCEL_ADDRESS:
        app->editing = false;
        SDL_StopTextInput(app->win);
        if (app->t->hpos >= 0) snprintf(app->t->url, sizeof app->t->url, "%s",
            app->t->cur ? app->t->cur->url : app->t->hist[app->t->hpos]);
        break;
    case ANDROID_BACK_CLEAR_FOCUS:
        g_kbd_sticky = false;
        SDL_StopTextInput(app->win);
        focus_node(app, NULL);
        break;
    case ANDROID_BACK_HISTORY:
        if (SDL_TextInputActive(app->win)) SDL_StopTextInput(app->win);
        history_go(app, -1);
        break;
    case ANDROID_BACK_BACKGROUND:
        SDL_MinimizeWindow(app->win);
        break;
    }
    app->dirty = true;
    app->vonly = false;
}
#else
#include <execinfo.h>
#endif
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
/* A memory fault on the main thread while handling a tab jumps back to the event loop instead of killing
   Lumen: that tab's page is abandoned (leaked, never touched again), the tab reloads, and a notice is shown. */
static sigjmp_buf g_recover;
static volatile sig_atomic_t g_guard;
static pthread_t g_main_thr;
static void crash_handler(int sig, siginfo_t *si, void *ucv) {
#ifndef __ANDROID__
    void *bt[64]; int n = backtrace(bt, 64);
#endif
    unsigned long pc = 0, lr = 0, load_address = 0;
#ifdef __APPLE__
    load_address = 0x100000000UL + (unsigned long)_dyld_get_image_vmaddr_slide(0);
#endif
#if defined(__APPLE__) && defined(__aarch64__)
    ucontext_t *uc = ucv; if (uc && uc->uc_mcontext) { pc = (unsigned long)uc->uc_mcontext->__ss.__pc; lr = (unsigned long)uc->uc_mcontext->__ss.__lr; }
#elif defined(__APPLE__) && defined(__x86_64__)
    ucontext_t *uc = ucv; if (uc && uc->uc_mcontext) pc = (unsigned long)uc->uc_mcontext->__ss.__rip;
#else
    (void)ucv;
#endif
    char buf[192]; int k = snprintf(buf, sizeof buf, "lumen: fatal signal %d at pc 0x%lx lr 0x%lx addr %p, load address 0x%lx\n", sig, pc, lr, si ? si->si_addr : NULL, load_address);
    write(2, buf, (size_t)k);
#ifndef __ANDROID__
    backtrace_symbols_fd(bt, n, 2);
#endif
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

static int deadline_wait_ms(int timeout, double remaining) {
    if (!(remaining < timeout)) return timeout;
    /* SDL waits in whole milliseconds. Rounding down spins the main loop
       during the fractional millisecond before each JavaScript deadline. */
    return remaining <= 0 ? 0 : (int)ceil(remaining);
}

int main(int argc, char **argv) {
    boot_begin();
    g_no_paint_only = getenv("LUMEN_NO_PAINT_ONLY") != NULL;
    g_main_thr = pthread_self();
    install_crash_handler();
    const char *start = argc > 1 ? argv[1] : "https://en.wikipedia.org/wiki/Web_browser";
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");   /* pages keep playing audio and video in the background */
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
#ifdef __ANDROID__
    android_platform_init();
#endif
    boot_mark("sdl");
    dom_init(); net_init(6); font_init(); css_sheet_added_hook = sheet_fonts;
    boot_mark("fonts");
    { const char *rt = getenv("LUMEN_RASTER_THREADS"); raster_set_threads(rt ? atoi(rt) : LMIN(SDL_GetNumLogicalCPUCores(), 8) - 1); }   /* large rasters use every core */
    icache_mu = SDL_CreateMutex(); EV_LOADED = SDL_RegisterEvents(1); EV_NET = SDL_RegisterEvents(1); EV_MENU = SDL_RegisterEvents(1); EV_DL = SDL_RegisterEvents(1); EV_UI = SDL_RegisterEvents(1); EV_TD = SDL_RegisterEvents(1);
#ifdef __ANDROID__
    android_launch_set_wakeup(wake);
#endif
    js_load_blocked = h_load_blocked;
    net_wakeup = wake; media_wakeup = wake; js_wakeup = wake; js_clip_set = h_clip_set; js_clip_get = h_clip_get; js_clip_image = h_clip_image; js_global_init(argv[0]);
    boot_mark("v8");
    paint_image_hook = node_img; paint_url_image_hook = url_img; svg_ext_ref_hook = svg_ext_ref; layout_image_size_hook = img_size; jsg_img_done_hook = img_fetch_done;
    App a; memset(&a, 0, sizeof a); g_app = &a; a.nws = 1; a.wsicon[0] = WORKSPACE_DEFAULT_ICON; snprintf(a.wsname[0], sizeof a.wsname[0], "Personal"); a.side = sidebar_width(getenv("LUMEN_NO_SIDEBAR") ? 0 : SIDEW); tab_new(&a);
    bool want_gpu = !getenv("LUMEN_NO_GPU");
#ifdef __ANDROID__
    renderer_read_preferences();
#endif
    int ww = 1280, wh = 840; { const char *e = getenv("LUMEN_WINDOW"); if (e) sscanf(e, "%dx%d", &ww, &wh); }   /* e.g. LUMEN_WINDOW=800x600 */
    a.win = SDL_CreateWindow("Lumen", ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (want_gpu && !strcmp(SDL_GetPlatform(), "macOS") ? SDL_WINDOW_METAL : 0));
    if (!a.win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    g_dialog_win = a.win; js_pick_files = pick_files;
    boot_mark("window");
    set_window_icon(a.win);
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
#ifdef __ANDROID__
        if (ok) a.gpu = gpu_create_backend(&src, a.pw, a.ph, g_renderer);
#else
        if (ok) a.gpu = gpu_create(&src, a.pw, a.ph, getenv("LUMEN_VULKAN") != NULL || strcmp(SDL_GetPlatform(), "macOS"));
#endif
        media_yuv = gpu_yuv_ok(a.gpu) && !getenv("LUMEN_NO_YUV");
        if (!a.gpu && a.mview) { SDL_Metal_DestroyView(a.mview); a.mview = NULL; }
    }
#ifdef __ANDROID__
    if (!SDL_AddEventWatch(android_graphics_lifecycle, &a)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Lumen: unable to register Android graphics lifecycle");
        gpu_destroy(a.gpu);
        SDL_DestroyWindow(a.win);
        SDL_Quit();
        return 1;
    }
#endif
    boot_mark("gpu");
    fprintf(stderr, "lumen: presenting with %s\n", gpu_backend_name(a.gpu));
#ifdef __ANDROID__
    SDL_Log("Lumen: requested renderer %s, active %s", renderer_key(g_renderer), gpu_backend_name(a.gpu));
#endif
    a.ui = font_get("system-ui", 400, false, 13.5f);
    {   /* file: pages and links never read Lumen's own data (cookies, caches, settings) */
#ifdef __ANDROID__
        const char *files = SDL_GetAndroidInternalStoragePath();
        char data[1024] = "", pdf[1100] = "";
        if (files) { snprintf(data, sizeof data, "%s", files); char *s = strrchr(data, '/'); if (s && s != data) *s = 0; }
        snprintf(pdf, sizeof pdf, "%s/cache/viewer-pdf.jpg", data);
        const char *allowed[] = { opened_dir(), pdf, NULL };
        if (*data) net_file_protect(data, allowed);
#else
        char *pref = SDL_GetPrefPath("lumen", "Lumen");
        if (pref) net_file_protect(pref, NULL);
        SDL_free(pref);
#endif
    }
    char cookie_path[1024] = "";
    { char *pref = SDL_GetPrefPath("lumen", "Lumen"); if (pref) { snprintf(cookie_path, sizeof cookie_path, "%scookies.txt", pref); cookies_load(cookie_path);
        char cache_dir[1100]; snprintf(cache_dir, sizeof cache_dir, "%shttpcache", pref); net_cache_init(cache_dir, 256u << 20);
        snprintf(cache_dir, sizeof cache_dir, "%sv8cache", pref); js_code_cache_init(cache_dir, 96u << 20); SDL_free(pref); } }
    boot_mark("caches");
    double cookies_saved_at = now_ms();
    {
        char *pref = SDL_GetPrefPath("lumen", "Lumen"); char sp[1024] = "";
        if (pref) { snprintf(g_pref, sizeof g_pref, "%s", pref); snprintf(sp, sizeof sp, "%ssettings.txt", pref); SDL_free(pref); }
        FILE *sf = *sp ? fopen(sp, "r") : NULL; char line[4096];
#ifdef __ANDROID__
        snip_defaults();
#endif
        while (sf && fgets(line, sizeof line, sf)) {
#ifdef __ANDROID__
            pixel_scale_load(line);
            if (!strncmp(line, "snippets=", 9)) { line[strcspn(line, "\r\n")] = 0; snip_decode(line + 9); continue; }
#endif
            char k[64]; int v; if (sscanf(line, "%63[^=]=%d", k, &v) != 2) continue;
            if (!strcmp(k, "page_zoom") && page_zoom_valid(v)) g_page_zoom_pct = v;
#ifdef __ANDROID__
            else if (!strncmp(k, "bar_slot", 8) && k[8] >= '0' && k[8] < '0' + NSLOT && !k[9] && slot_action(v)) g_slot[k[8] - '0'] = v;
            else if (!strcmp(k, "touch_feedback")) g_touch_fb = v != 0;
            else if ((!strcmp(k, "edit_slot0") || !strcmp(k, "edit_slot1")) && (v == TA_BKSP || v == TA_ENTER || v == TA_UNDO || v == TA_CLEAR || v == TA_HIDE)) g_bar_slot[k[9] - '0'] = v;
            else if (!strcmp(k, "touch_feedback_color")) g_touch_fb_rgb = v & 0xffffff;
            else if (!strcmp(k, "touch_feedback_opacity")) g_touch_fb_alpha = LCLAMP(v, 1, 100);
            else if (!strcmp(k, "touch_feedback_size")) g_touch_fb_size = LCLAMP(v, 4, 120);
            else if (!strcmp(k, "touch_feedback_ms")) g_touch_fb_ms = LCLAMP(v, 0, 5000);
#endif
            else if (!strcmp(k, "offscreen_media_eviction")) g_lowmem = v != 0; else if (!strcmp(k, "cpu_limit")) g_cpu_on = v != 0;
            else if (!strcmp(k, "cpu_pct")) g_cpu_pct = LCLAMP(v, 10, 100); else if (!strcmp(k, "cpu_secs")) g_cpu_secs = LCLAMP(v, 5, 600);
            else if (!strcmp(k, "cpu_lim")) g_cpu_lim = LCLAMP(v, 5, 95); else if (!strcmp(k, "video_controls")) g_vctl = v != 0;
            else if (!strcmp(k, "chatgpt_sleep")) g_tab_sleep = v != 0; else if (!strcmp(k, HUD_KEY)) g_hud = v != 0; else if (!strcmp(k, "smooth_scrolling")) g_smooth = v != 0; else if (!strcmp(k, "tap_keyboard")) g_tap_kbd = v != 0; else if (!strcmp(k, "hide_tabs")) g_hide_tabs = v != 0; else if (!strcmp(k, "desktop_default")) g_desktop_seeded = v != 0;
            else if (!strcmp(k, "scroll_px")) g_scroll_px = LCLAMP((v + 5) / 10 * 10, 10, 100000); else if (!strcmp(k, "user_agent")) g_ua = LCLAMP(v, 0, UA_COUNT - 1);
        }
        if (sf) fclose(sf);
        paint_profile_configure();
#ifdef __ANDROID__
        update_size(&a);
#endif
        net_set_user_agent(g_ua);
        links_load("bookmarks.txt", g_bms, &g_nbm, MAX_BM); links_load("history.txt", g_hv, &g_nhv, MAX_HV);
        desktop_sites_load();
#ifdef __ANDROID__
        if (!g_desktop_seeded && *g_pref) {   /* Desktop site is on for every site by default, existing installs included */
            net_set_desktop_host("*", true); desktop_sites_save(); g_desktop_seeded = true; settings_save();
        }
#endif
        download_store_init(g_pref);
        dl_track(&a, false);
        dl_notify = dl_changed;
#ifdef __ANDROID__
        android_ui_action = ui_action_post;
#endif
        const char *lm = getenv("LUMEN_LOWMEM"); if (lm) g_lowmem = atoi(lm) != 0;
        fprintf(stderr, "lumen: lite mode %s\n", g_lowmem ? "on" : "off");
        media_lowmem = g_lowmem;
    }
    boot_mark("settings");
    int shown = -1;
#ifdef __ANDROID__
    g_session = !getenv("LUMEN_NO_SESSION") && argc <= 2;   /* argv[1]: lumen://newtab, or the address the app was opened with */
#else
    g_session = !getenv("LUMEN_NO_SESSION") && argc <= 1;
#endif
    if (g_session && (shown = session_load(&a)) >= 0) {
        tab_select(&a, shown);
        if (strcmp(start, "lumen://newtab") && tab_new(&a)) { tab_select(&a, a.ntabs - 1); navigate(&a, start_url(start), true); }
        g_session_sig = session_sig(&a);
    } else {
        navigate(&a, start_url(start), true);
        for (int i = 2; i < argc; i++) { Tab *nt = tab_new(&a); if (!nt) break; a.t = nt; navigate(&a, start_url(argv[i]), true); }
        a.t = a.tabs[0]; a.ti = 0;
    }
    boot_mark("session");
    bool quit = false, cmd = false;
#ifdef __ANDROID__
    AndroidTextInputState android_text_state = {0};
    android_back_set_wakeup(wake);
#endif
    double boot_turn = now_ms();
    while (!quit) {
        { double t = now_ms(); boot_tick(t - boot_turn); boot_turn = t; }
        panel_service(&a);
        popup_service(&a, &quit);
#ifdef __ANDROID__
        android_media_playing = media_timeout_ms() >= 0;   /* LumenActivity keeps a media foreground service while it is set */
        session_service(&a, a.android_suspended);
#else
        session_service(&a, false);
#endif
        { int sig = sigsetjmp(g_recover, 1); if (sig) recover_from_crash(&a, sig); }
        g_guard = 1;
#ifdef __ANDROID__
        for (unsigned back_count = 0; back_count < 32 && android_back_take(); back_count++) android_handle_back(&a);
        char *launch_url;
        for (unsigned launch_count = 0; launch_count < 32 && (launch_url = android_launch_take()); launch_count++) {
            if (tab_new(&a)) {
                tab_select(&a, a.ntabs - 1);
                SDL_StopTextInput(a.win);
                navigate(&a, launch_url, true);
            } else {
                snprintf(g_notice, sizeof g_notice, "Tab limit reached; close a tab before opening another link.");
                a.dirty = true;
            }
            free(launch_url);
        }
#endif
        if (env_hook(ENV_CRASH_TEST)) {   /* test hook: fault once on the main thread after N ms */
            static double t0; static bool done; if (!t0) t0 = now_ms();
            if (!done && now_ms() - t0 > atof(env_hook(ENV_CRASH_TEST))) { done = true; *(volatile int *)(uintptr_t)8 = 1; }
        }
        SDL_Event ev;
        int to = a.t->loading ? 120 : 1000;
        int wk_src = 0, wk_prev = to;   /* what set the wait: the busy trace's wake counts */
#define WTAG(k) do { if (to < wk_prev) wk_src = (k); wk_prev = to; } while (0)
        for (int i = 0; i < a.ntabs; i++) { Tab *tb = a.tabs[i]; if (tb->cur && tb->cur->js) { double dl = js_next_deadline(tb->cur->js) - now_ms(); if (tb->limited && tb->budget < 0) dl = LMAX(dl, -tb->budget / tb->lim - (now_ms() - tb->bud_t)); to = deadline_wait_ms(to, dl); } }  WTAG(1);
        if (net_pending() && to > 50) to = 50;  WTAG(2);
        if (g_hud && a.side > 0) {
            static double lh; double th = now_ms();
            if (th - lh >= 2000) { lh = th; hud_sample(); a.dirty = true; a.vonly = false; }
            int nx = (int)(2000 - (th - lh)) + 1; if (nx < to) to = nx;  WTAG(3);
        }
        { int mf = media_tick(); if (mf & 1) { if (!a.dirty && !a.deferred) a.vonly = true; a.dirty = a.vframe = true; } if (mf & 2) a.t->relayout = true; }
#ifdef __ANDROID__
        Node *text_focus = page_focus(&a);
        if (!text_focus) text_focus = edit_focus(&a);
        /* the focused field was blurred or removed (e.g. an inline editor closing): hide the keyboard, as Chrome does */
        if (android_text_state.focus && !text_focus && !a.editing && !g_kbd_sticky && SDL_TextInputActive(a.win)) SDL_StopTextInput(a.win);
        if (android_text_input_start(&android_text_state, (uintptr_t)text_focus,
            a.t->cur ? a.t->cur->gen : 0, a.editing)) {
            a.focus_reveal = a.dirty = true;
            a.vonly = false;
            if (!g_tap_kbd && !SDL_TextInputActive(a.win)) SDL_StartTextInput(a.win);   /* else only a second tap brings the keyboard (kbd_tap) */
        }
#else
        if (!a.editing && (page_focus(&a) || edit_focus(&a)) && !SDL_TextInputActive(a.win)) SDL_StartTextInput(a.win);
#endif
        {
            static double last_bg; double tb = now_ms();
            if (tb - last_bg > 2000) {
                last_bg = tb;
                for (int i = 0; i < a.ntabs; i++) if (a.tabs[i]->cur && a.tabs[i]->cur->js) js_set_background(a.tabs[i]->cur->js, a.tabs[i] != a.t && a.tabs[i] != a.pan_under);
                /* tab sleep (Settings, off by default): an idle background chatgpt.com tab drops its JS (the bulk of its RAM) and keeps
                   its DOM/layout on screen; activating it reloads. LUMEN_TAB_SLEEP_MS: delay (overrides the setting), 0 = off; LUMEN_TAB_SLEEP=all: any site */
                const char *sm = env_hook(ENV_TAB_SLEEP_MS); double sleep_ms = sm ? atof(sm) : g_tab_sleep ? 60000 : 0; bool sleep_all = env_hook(ENV_TAB_SLEEP) && !strcmp(env_hook(ENV_TAB_SLEEP), "all");
                for (int i = 0; i < a.ntabs; i++) {
                    Tab *st = a.tabs[i]; Page *sp = st->cur;
                    bool popup = false; for (int k = 0; k < a.ntabs; k++) popup |= a.tabs[k]->opener_wid == st->wid && a.tabs[k]->scripted;   /* waits for its window.open tab */
                    if (st == a.t || st == a.pan_under || st->loading || !sp || !sp->js || !sp->url || popup) { st->bg_since = 0; continue; }
                    if (!st->bg_since) st->bg_since = tb;
                    bool site = sleep_all || !strncmp(sp->url, "https://chatgpt.com/", 20);
                    if (sleep_ms > 0 && site && tb - st->bg_since > sleep_ms && !js_busy(sp->js)) { js_free(sp->js); sp->js = NULL; st->asleep = true; }
                }
                if (a.t->asleep && !a.t->loading) navigate(&a, a.t->url, false);
                for (Page *p = g_frames; p; p = p->fnext) if (p->js) { Tab *ft = tab_of_doc(&a, p->d); js_set_background(p->js, ft && ft != a.t); }
            }
        }
        if (env_hook(ENV_MEM_STATS)) {
            static double last_stats; double tn = now_ms();
            if (tn - last_stats > 10000) { last_stats = tn; size_t fr, seg = media_mem_bytes(&fr); size_t jh = 0, je = 0; js_mem_stats(&jh, &je); fprintf(stderr, "lumen-mem: mse=%.1fMB vframes=%.1fMB images=%.1fMB/%d js_heap=%.1fMB js_external=%.1fMB canvases=%.1fMB\n", seg / 1048576.0, fr / 1048576.0, g_icache_bytes / 1048576.0, g_icache_n, jh / 1048576.0, je / 1048576.0, ((double)a.frame.w * a.frame.h + (double)a.page.w * a.page.h) * 4 / 1048576.0); }
            if (last_stats == tn) { extern long g_styles_live, g_nodes_live; extern size_t g_css_parsed_bytes; extern void worker_mem_stats(size_t *, size_t *, int *); size_t wh, wx; int wn; worker_mem_stats(&wh, &wx, &wn);
                fprintf(stderr, "lumen-mem2: malloc_in_use=%.1fMB nodes=%ld (%zuB) styles=%ld (%zuB) css_parsed=%.1fMB workers=%d heap=%.1fMB ext=%.1fMB\n", platform_heap_bytes() / 1048576.0, g_nodes_live, sizeof(Node), g_styles_live, sizeof(ComputedStyle), g_css_parsed_bytes / 1048576.0, wn, wh / 1048576.0, wx / 1048576.0); }
        }
        g_lite_active = a.t && lite_on(a.t); media_lowmem = g_lite_active;
        if (g_lite_active) {
            static double last_evict; double tn = now_ms();
            if (a.t->cur) media_mark_visible(vis_doc, a.t->cur->d);
            if (tn - last_evict > 2000) { last_evict = tn; img_evict(); }
        }
        { int mt = media_timeout_ms(); if (mt >= 0 && mt < to) to = mt; }  WTAG(4);
        if (g_sm_dx || g_sm_dy) {   /* smooth scrolling: move 30% of what is left each 16ms frame */
            float sx = fabsf(g_sm_dx) < 2 ? g_sm_dx : g_sm_dx * .3f, sy = fabsf(g_sm_dy) < 2 ? g_sm_dy : g_sm_dy * .3f;
            g_sm_dx -= sx; g_sm_dy -= sy; wheel_apply(&a, g_sm_x, g_sm_y, sx, sy);
            if (to > 16) to = 16; WTAG(5);
        }
        page_move_keep(&a); if (g_mv_p && to > 1000) to = 1000;  WTAG(6);
        if (a.vbar_any && !a.vbars && media_state_version() != a.vbar_msv) { a.dirty = true; a.vonly = false; }   /* a bar's play state changed (e.g. play() from a page script) */
        if (a.vbars) {   /* a playing video's bar clock: only the bars are repainted, not the page */
            static double lb; double tn = now_ms();
            if (tn - lb >= 500) { lb = tn; if (!(a.dirty && !a.vonly)) a.vonly = true; a.dirty = a.vbar_tick = true; g_wake[2]++; }
            if (to > 500) to = 500;  WTAG(7);
        }
        if (a.deferred) { double r = 250 - (now_ms() - a.last_full); if (r <= 0) { a.dirty = true; g_wake[3]++; } else if (r + 1 < to) to = (int)r + 1; }  WTAG(8);
        if (a.tip_until) { double r = a.tip_until - now_ms(); if (r <= 0) { a.tip_until = 0; a.dirty = true; a.vonly = false; } else if (r + 1 < to) to = (int)r + 1; }  WTAG(9);
        {
            /* the caret blinks for 10 s after focus or input, then stays shown, as in GTK: a focused
               field on an idle page (a chat composer) no longer repaints the page twice a second */
            bool want = false; static const Node *blink_node; static const Page *blink_page;
            Node *fn = a.editing ? NULL : page_focus(&a) ? page_focus(&a) : edit_focus(&a);
            if (fn && (fn != blink_node || a.t->cur != blink_page)) a.caret_t = now_ms();
            blink_node = fn; blink_page = a.t->cur;
            if (fn) {
                double since = now_ms() - a.caret_t, ph = fmod(since, 1060);
                want = ph < 530 || since >= 10000;
                int nx = (int)LMIN((want ? 530 : 1060) - ph, 10000 - since) + 1;
                if (since < 10000 && nx < to) to = nx;  WTAG(10);
            }
            if (want != dl_caret_on) { dl_caret_on = want; a.vonly = false; a.dirty = true; g_wake[0]++; }
        }
        { double now = now_ms(), gn; if (image_anim_tick(now, &gn)) { a.vonly = false; a.dirty = true; g_wake[1]++; } if (gn > 0 && gn - now < to) to = gn - now < 1 ? 1 : (int)(gn - now); }  WTAG(11);
        if (env_hook(ENV_AUTOSCROLL)) {
            /* test hook: "x,y,dy,start_ms,count" pushes one wheel tick every 16ms */
            static double t0; static int sent; float wx = 0, wy = 0, wdy = 0; double st = 0; int cnt = 0;
            sscanf(env_hook(ENV_AUTOSCROLL), "%f,%f,%f,%lf,%d", &wx, &wy, &wdy, &st, &cnt);
            if (!t0) t0 = now_ms();
            if (sent < cnt && now_ms() - t0 > st + sent * 16.0) { SDL_Event we = {0}; we.type = SDL_EVENT_MOUSE_WHEEL; we.wheel.y = wdy; we.wheel.mouse_x = wx / a.input_scale; we.wheel.mouse_y = wy / a.input_scale; SDL_PushEvent(&we); sent++; }
            if (sent < cnt && to > 16) to = 16;  WTAG(12);
        }
#ifdef __ANDROID__
        to = android_fling_wait_ms(&a.fling, now_ms(), to);  WTAG(13);
        to = bar_hold_wait_ms(tsel_wait_ms(to));
#ifdef __ANDROID__
        if (g_kbd_show_at) { double l = g_kbd_show_at - now_ms(); if (l < to) to = l > 0 ? (int)l + 1 : 0; }
#endif
        WTAG(14);
#endif
#ifdef __ANDROID__
        if (a.android_suspended && !android_media_playing && (to < 0 || to < 1000)) to = 1000;   /* in the background with nothing playing: wake once a second */
#endif
        double boot_wait = now_ms();
        bool boot_got = SDL_WaitEventTimeout(&ev, to);
#undef WTAG
        if (!boot_got) g_wk_to[wk_src]++;
        else { int k = 0; while (k < 8 && g_wk_ev[k][1] && g_wk_ev[k][0] != (int)ev.type) k++; if (k < 8) { g_wk_ev[k][0] = (int)ev.type; g_wk_ev[k][1]++; } }
        boot_turn += now_ms() - boot_wait;   /* waiting is not work */
        if (!boot_got) { if (a.t->loading || env_hook(ENV_SHOT)) a.dirty = true; }
        else do {
            bool wheel0 = ev.type == SDL_EVENT_MOUSE_WHEEL && !ev.wheel.x && !ev.wheel.y;
            if ((ev.type >= SDL_EVENT_KEY_DOWN && ev.type <= SDL_EVENT_TEXT_INPUT) || (ev.type >= SDL_EVENT_MOUSE_MOTION && ev.type <= SDL_EVENT_MOUSE_WHEEL) ||
                (ev.type >= SDL_EVENT_FINGER_DOWN && ev.type <= SDL_EVENT_FINGER_CANCELED)) doc_activity();   /* input: full-rate animation frames */
            input_coordinates(&a, &ev);
#ifdef __ANDROID__
            bool touch_tap = ev.type == SDL_EVENT_FINGER_UP;
#ifndef NDEBUG
            /* in-page route changes (taps) also get a load trace window */
            if (touch_tap) { if (a.t->cur && a.t->cur->js) js_debug_profile(a.t->cur->js); if (now_ms() + 30000 > g_load_trace_until) { net_trace_load(30); g_load_trace_until = now_ms() + 30000; } }
#endif
            if (android_touch_input(&a, &ev)) continue;
            touch_tap = touch_tap && ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
#endif
            if ((ev.type >= SDL_EVENT_KEY_DOWN && ev.type <= SDL_EVENT_TEXT_INPUT) || (ev.type >= SDL_EVENT_MOUSE_MOTION && ev.type <= SDL_EVENT_MOUSE_WHEEL && !wheel0)) a.last_input = now_ms();
            if (ev.type != EV_NET && ev.type != SDL_EVENT_MOUSE_WHEEL) a.vonly = false;
            js_key_mods = key_mods(SDL_GetModState());
            switch (ev.type) {
            case SDL_EVENT_QUIT: quit = true; break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: update_size(&a); break;
            case SDL_EVENT_WINDOW_EXPOSED: a.dirty = true; break;
            case SDL_EVENT_MOUSE_WHEEL: {
                if ((SDL_GetModState() & SDL_KMOD_CTRL) && ev.wheel.y) {
                    page_zoom_adjust(&a, (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y) > 0 ? 1 : -1);
                    break;
                }
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
                if (!h && ev.motion.y > BAR && ev.motion.x > page_left(&a)) page_move(&a, page_x(&a, ev.motion.x), page_y(&a, ev.motion.y));
                else if (a.t->cur && page_hover(a.t->cur, NULL)) restyle(&a);
                if (g_press && (ev.motion.state & SDL_BUTTON_LMASK)) sel_drag(&a, ev.motion.x, ev.motion.y);
                else if (g_epress && (ev.motion.state & SDL_BUTTON_LMASK)) edit_drag(&a, ev.motion.x, ev.motion.y);
                else if (g_cpress && (ev.motion.state & SDL_BUTTON_LMASK)) ctl_drag(&a, ev.motion.x, ev.motion.y);
                else if (g_upress && (ev.motion.state & SDL_BUTTON_LMASK)) url_drag(&a, ev.motion.x);
                SDL_SetCursor(SDL_CreateSystemCursor(h == HB_URL ? SDL_SYSTEM_CURSOR_TEXT : over_link(&a, ev.motion.x, ev.motion.y) || (h && h != HB_URL) ? SDL_SYSTEM_CURSOR_POINTER : SDL_SYSTEM_CURSOR_DEFAULT));
                break; }
            case SDL_EVENT_WINDOW_MOUSE_LEAVE: if (a.t->cur && page_hover(a.t->cur, NULL)) restyle(&a); break;
            case SDL_EVENT_MOUSE_BUTTON_UP: if (ev.button.button == SDL_BUTTON_LEFT) g_press = g_drag = g_cpress = g_upress = false; break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button != SDL_BUTTON_LEFT) break;
                { int bh = bar_hit(&a, ev.button.x, ev.button.y);
#if defined(__ANDROID__) && !defined(NDEBUG)
                  if (ev.button.y < BAR) __android_log_print(ANDROID_LOG_INFO, "LumenUi", "bar x=%.0f y=%.0f vw=%.0f x0=%.0f compact=%d hit=%d slots=%d,%d",
                      ev.button.x, ev.button.y, a.vw, bar_x0(&a), g_compact, bh, g_slot[0], g_slot[1]);
#endif
                  if (g_hud_pop) { float x = ev.button.x, y = ev.button.y; bool in = x >= g_pop[0] && x < g_pop[2] && y >= g_pop[1] && y < g_pop[3]; g_hud_pop = false; a.dirty = true; a.vonly = false; if (in || bh == HB_HUD) break; }
                  if (bh == HB_HUD) { g_hud_pop = true; a.dirty = true; a.vonly = false; break; }
#ifdef __ANDROID__
                  if (ev.button.y < TABH && bh != HB_NEWTAB) {   /* a double tap on the tab strip: the tab menu */
                      static double tap_t; static float tap_x;
                      double tn = now_ms(); bool twice = tn - tap_t < 450 && fabsf(ev.button.x - tap_x) < 60;
                      tap_t = twice ? 0 : tn; tap_x = ev.button.x;
                      if (twice) { tab_menu(&a, a.ti); break; }
                  }
#endif
                  if (bh >= HB_SLOT) {}   /* toolbar slots act below */
                  else if (bh >= HB_TABDOT) { int i = bh - HB_TABDOT; if (i != a.ti) tab_select(&a, i); a.t->info = true; a.dirty = true; a.vonly = false; break; }
                  else if (bh >= HB_INFO) { info_click(&a, bh - HB_INFO); break; }
                  else if (bh >= HB_WS) { int w = bh - HB_WS; if (w != a.wi) ws_select(&a, w); else ws_popup(&a, w, ev.button.x, ev.button.y); break; }
                  else if (bh == HB_WSNEW) { menu_cmd(&a, MENU_WS_NEW); break; }
                  else if (bh >= HB_TABX) { tab_close(&a, bh - HB_TABX, &quit); break; }
                  else if (bh >= HB_TAB) { int i = bh - HB_TAB; if (i == a.ti) tab_popup(&a, i, ev.button.x, ev.button.y); else tab_select(&a, i); break; }
                  else if (bh == HB_NEWTAB) { tab_open(&a); break; } }
                switch (bar_hit(&a, ev.button.x, ev.button.y)) {
                case HB_BACK: history_go(&a, -1); break;
                case HB_FWD: history_go(&a, 1); break;
                case HB_STAR: bm_toggle(&a); a.dirty = true; a.vonly = false; break;
#ifdef __ANDROID__
                case HB_MENU: if (a.editing) url_cancel(&a); show_more_menu(&a); a.dirty = true; break;
                case HB_SLOT: case HB_SLOT + 1: {
                    if (a.editing) url_cancel(&a);
                    int code = g_slot[bar_hit(&a, ev.button.x, ev.button.y) - HB_SLOT];
                    UiAction u = { code, code == UI_DESKTOP ? !desktop_on(bar_tab(&a)) : code == UI_FULLSCREEN ? !g_fullscreen : 0, NULL, NULL };
                    ui_action(&a, &u, &quit); a.dirty = true; a.vonly = false; break; }
#endif
                case HB_RELOAD: if (a.t->hpos >= 0) navigate(&a, a.t->hist[a.t->hpos], false); break;
                case HB_URL: url_press(&a, ev.button.x, ev.button.clicks); break;
                default: {
                    unsigned touch_flags = 0;
#ifdef __ANDROID__
                    if (touch_tap) touch_flags = a.touch_dom_flags;
#endif
                    if (a.editing) { a.editing = a.umenu = false; SDL_StopTextInput(a.win); a.dirty = true; } sel_press(&a, ev.button.x, ev.button.y, ev.button.clicks); click_page(&a, ev.button.x, ev.button.y, touch_flags); edit_press(&a, ev.button.x, ev.button.y, ev.button.clicks); ctl_press(&a, ev.button.x, ev.button.y, ev.button.clicks);
#ifdef __ANDROID__
                    if (touch_tap) { page_touch_controls(&a, page_x(&a, ev.button.x), page_y(&a, ev.button.y)); kbd_tap(&a, ev.button.x, ev.button.y); }
#endif
                }
                }
                break;
#ifdef __ANDROID__
            case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN: g_kbd_sticky = false; break;
            case SDL_EVENT_CLIPBOARD_UPDATE: {   /* the clipboard history: text, else an image */
                char *t = SDL_GetClipboardText();
                if (t && *t) clip_record(t, NULL, 0, NULL);
                else { char *m = NULL; size_t n = 0; unsigned char *img = android_clip_image(&m, &n); if (img) clip_record(NULL, img, n, m); free(img); free(m); }
                SDL_free(t);
                break;
            }
#endif
            case SDL_EVENT_TEXT_INPUT: a.caret_t = now_ms();
#ifdef __ANDROID__
                /* the soft keyboard's Enter also arrives as a Return key event, which does the edit */
                if (ev.text.text && ev.text.text[strspn(ev.text.text, "\r\n")] == 0) break;
#endif
                if (!a.editing && page_focus(&a)) {
                    Node *f = page_focus(&a);
                    if (!a.t->cur->js || js_dispatch(a.t->cur->js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text)) {
                        ctl_insert(&a, f, ev.text.text, "insertText");
                    }
                    if (a.t->cur->js) js_dispatch(a.t->cur->js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text);
                    restyle(&a);
                } else if (!a.editing && edit_focus(&a)) {
                    Node *f = edit_focus(&a); JsCtx *js = a.t->cur->js;
                    if (js_dispatch(js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text))
                        js_dispatch(js, f, "lumenedit", "KeyboardEvent", false, false, 0, 0, 0, ev.text.text);
                    js_dispatch(js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, ev.text.text);
                    restyle(&a); a.dirty = true;
                } else if (a.editing) { a.umenu = false; url_insert(&a, ev.text.text); }
                break;
            case SDL_EVENT_KEY_UP: if (ev.key.key == SDLK_LGUI || ev.key.key == SDLK_RGUI || ev.key.key == SDLK_LCTRL || ev.key.key == SDLK_RCTRL) cmd = false; break;
            case SDL_EVENT_KEY_DOWN: { a.caret_t = now_ms();
                js_key_mods = key_mods(ev.key.mod);
                SDL_Keycode k = ev.key.key; float page = page_height(&a) - 40;
#if defined(__ANDROID__) && !defined(NDEBUG)
                if (k == SDLK_F7) { net_debug_streams(120); break; }
                if (k == SDLK_F8) { debug_layout_geometry(&a); break; }
                if (k == SDLK_F9) {
                    net_debug_streams(0);
                    Page *p = a.t->cur;
                    if (p) { p->e->debug_style_passes = 0; p->d->debug_mutations = 0; }
                    break;
                }
#endif
#ifdef __ANDROID__
                /* Back arrives through onBackPressed (android_back_take); SDL's key event for the same press
                   would take a second step back */
                if (k == SDLK_AC_BACK) break;
#endif
                if (k == SDLK_LGUI || k == SDLK_RGUI || k == SDLK_LCTRL || k == SDLK_RCTRL) { cmd = true; break; }
                if (cmd || (ev.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
                    bool zoom_key = k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_MINUS || k == SDLK_0;
                    if (zoom_key) { page_zoom_adjust(&a, k == SDLK_0 ? 0 : k == SDLK_MINUS ? -1 : 1); break; }
                    bool reserved = k == SDLK_Q || k == SDLK_W || k == SDLK_T || k == SDLK_N || k == SDLK_L || k == SDLK_TAB || (k >= SDLK_1 && k <= SDLK_9);
                    if (cmd) js_key_mods |= (ev.key.mod & SDL_KMOD_CTRL) ? 2 : 8;
                    if (!reserved && !page_key(&a, k, ev.key.mod)) { restyle(&a); a.dirty = true; a.vonly = false; break; }
                    bool shift = ev.key.mod & SDL_KMOD_SHIFT;
                    if (a.editing && (k == SDLK_A || k == SDLK_X || k == SDLK_V || k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_BACKSPACE || k == SDLK_DELETE)) {
                        a.umenu = false;
                        if (k == SDLK_A) url_select_all(&a);
                        else if (k == SDLK_X) { char *t = url_selected(&a); if (*t) { SDL_SetClipboardText(t); url_insert(&a, ""); } free(t); }
                        else if (k == SDLK_V) { char *t = SDL_GetClipboardText(); if (t && *t) url_insert(&a, t); SDL_free(t); }
                        else if (k == SDLK_BACKSPACE || k == SDLK_DELETE) url_delete(&a, k == SDLK_DELETE ? 1 : -1, true);
                        else url_move(&a, k, shift, true);
                    }
                    else if (k == SDLK_A && !a.editing && page_focus(&a)) ctl_select_all(&a, page_focus(&a));
                    else if (k == SDLK_X && !a.editing && page_focus(&a)) { ctl_cut(&a, page_focus(&a)); restyle(&a); }
                    else if (k == SDLK_V && !a.editing && page_focus(&a)) { ctl_paste(&a, page_focus(&a)); restyle(&a); }
                    else if ((k == SDLK_BACKSPACE || k == SDLK_DELETE) && !a.editing && page_focus(&a)) { ctl_delete(&a, page_focus(&a), k == SDLK_DELETE ? 1 : -1, true); restyle(&a); }
                    else if ((k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_HOME || k == SDLK_END || k == SDLK_UP || k == SDLK_DOWN) && !a.editing && page_focus(&a))
                        ctl_move(&a, page_focus(&a), k == SDLK_UP ? SDLK_HOME : k == SDLK_DOWN ? SDLK_END : k, shift, k == SDLK_LEFT || k == SDLK_RIGHT);
                    else if (k == SDLK_V && !a.editing) { page_paste(&a, NULL, edit_focus(&a)); restyle(&a); }
                    else if (k == SDLK_Z && !shift && !a.editing && a.t->cur->js && (page_focus(&a) || edit_focus(&a))) {   /* the page did not handle Ctrl+Z: Lumen's own undo */
                        Node *f = page_focus(&a); js_dispatch(a.t->cur->js, f ? f : edit_focus(&a), "lumenundo", "KeyboardEvent", false, false, 0, 0, 0, NULL); restyle(&a);
                    }
                    else if (k == SDLK_X && !a.editing && edit_focus(&a) && g_tsel.on && a.t->cur->L == g_tsel.L) {
                        char *t = tsel_text(a.t->cur->L);
                        if (*t) { SDL_SetClipboardText(t); js_dispatch(a.t->cur->js, edit_focus(&a), "lumeneditdel", "KeyboardEvent", false, false, 0, 0, 0, "Backspace"); }
                        free(t); restyle(&a);
                    }
                    else if (k == SDLK_A && !a.editing && !edit_focus(&a) && a.t->cur && a.t->cur->L) { tsel_all(a.t->cur->L); g_tsel.L = a.t->cur->L; a.vonly = false; }
                    else if (k == SDLK_A && !a.editing && edit_focus(&a)) js_dispatch(a.t->cur->js, edit_focus(&a), "lumensel-all", "MouseEvent", false, false, 0, 0, 0, NULL);
                    else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && !a.editing && edit_focus(&a))
                        js_dispatch(a.t->cur->js, edit_focus(&a), k == SDLK_LEFT ? "lumensel-home" : "lumensel-end", "MouseEvent", false, false, 0, (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : 0, 0, NULL);
                    else if (k == SDLK_C) sel_copy(&a);
                    else if (k == SDLK_L) { url_begin(&a); url_select_all(&a); }
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
                if (k == SDLK_ESCAPE && g_fs_page && !a.editing) { page_fs_exit(&a); a.dirty = true; break; }
                if (a.editing) {
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { SDL_StopTextInput(a.win); if (is_js_url(a.t->url)) { char js[sizeof a.t->url]; snprintf(js, sizeof js, "%s", a.t->url); run_js_url(&a, js); } else navigate(&a, a.t->url, true); }
                    else if (k == SDLK_ESCAPE) url_cancel(&a);
                    else if (k == SDLK_BACKSPACE || k == SDLK_DELETE) url_delete(&a, k == SDLK_DELETE ? 1 : -1, false);
                    else if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_HOME || k == SDLK_END || k == SDLK_UP || k == SDLK_DOWN) url_move(&a, k, ev.key.mod & SDL_KMOD_SHIFT, false);
                    a.umenu = false;
                    a.dirty = true; break;
                }
                if (page_focus(&a)) {
                    Node *f = page_focus(&a); const char *kn = k == SDLK_RETURN || k == SDLK_KP_ENTER ? "Enter" : k == SDLK_BACKSPACE ? "Backspace" : k == SDLK_DELETE ? "Delete" : k == SDLK_ESCAPE ? "Escape" : k == SDLK_TAB ? "Tab" :
                        k == SDLK_LEFT ? "ArrowLeft" : k == SDLK_RIGHT ? "ArrowRight" : k == SDLK_UP ? "ArrowUp" : k == SDLK_DOWN ? "ArrowDown" : k == SDLK_HOME ? "Home" : k == SDLK_END ? "End" : NULL;
                    if (kn) {
                        bool ok = !a.t->cur->js || js_dispatch(a.t->cur->js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        if (ok && (k == SDLK_BACKSPACE || k == SDLK_DELETE)) ctl_delete(&a, f, k == SDLK_DELETE ? 1 : -1, false);
                        else if (ok && (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN || k == SDLK_HOME || k == SDLK_END)) ctl_move(&a, f, k, ev.key.mod & SDL_KMOD_SHIFT, false);
                        else if (ok && !strcmp(kn, "Enter") && f->tag == A_textarea) ctl_insert(&a, f, "\n", "insertLineBreak");
                        else if (ok && !strcmp(kn, "Enter") && f->tag == A_input) submit_form(&a, f);
                        else if (ok && !strcmp(kn, "Escape")) focus_node(&a, NULL);
                        if (a.t->cur && a.t->cur->js && page_focus(&a) == f) js_dispatch(a.t->cur->js, f, "keyup", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        restyle(&a); a.dirty = true;
                    } else if (k != SDLK_SPACE && (k < 32 || k >= 127) && !page_key(&a, k, ev.key.mod)) { restyle(&a); a.dirty = true; }
                    break;
                }
                if (edit_focus(&a)) {
                    Node *f = edit_focus(&a); JsCtx *js = a.t->cur->js;
                    const char *kn = k == SDLK_RETURN || k == SDLK_KP_ENTER ? "Enter" : k == SDLK_BACKSPACE ? "Backspace" : k == SDLK_DELETE ? "Delete" : k == SDLK_ESCAPE ? "Escape" : k == SDLK_TAB ? "Tab" :
                        k == SDLK_LEFT ? "ArrowLeft" : k == SDLK_RIGHT ? "ArrowRight" : k == SDLK_UP ? "ArrowUp" : k == SDLK_DOWN ? "ArrowDown" : k == SDLK_HOME ? "Home" : k == SDLK_END ? "End" : NULL;
                    if (kn) {
                        bool ok = js_dispatch(js, f, "keydown", "KeyboardEvent", true, true, 0, 0, 0, kn);
                        /* Backspace, Delete (Ctrl: a word) and Enter (Shift: a line break) edit through the editing commands */
                        if (ok && (!strcmp(kn, "Backspace") || !strcmp(kn, "Delete") || !strcmp(kn, "Enter"))) js_dispatch(js, f, "lumeneditdel", "KeyboardEvent", false, false, 0, 0, 0, kn);
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
                if (k == SDLK_DOWN) a.t->sy += 40; else if (k == SDLK_UP) a.t->sy -= 40;
                else if (k == SDLK_PAGEDOWN || k == SDLK_SPACE) a.t->sy += (ev.key.mod & SDL_KMOD_SHIFT) ? -page : page;
                else if (k == SDLK_PAGEUP) a.t->sy -= page;
                else if (k == SDLK_HOME) a.t->sy = 0; else if (k == SDLK_END) a.t->sy = max_scroll(&a);
                else if (k == SDLK_BACKSPACE) history_go(&a, (ev.key.mod & SDL_KMOD_SHIFT) ? 1 : -1);
                else break;
                a.dirty = true; break; }
            default:
                if (ev.type == EV_MENU) menu_cmd(&a, ev.user.code);
                if (ev.type == EV_DL) dl_event(&a, &ev);
                if (ev.type == EV_TD && a.t->cur && a.t->cur->js) { char s[48]; snprintf(s, sizeof s, "__lumenTdDone(%d)", (int)ev.user.code); js_eval(a.t->cur->js, s, "lumen:testdriver"); }
#ifdef __ANDROID__
                if (ev.type == EV_UI) { UiAction *u = ev.user.data1; ui_action(&a, u, &quit); free(u->s1); free(u->s2); free(u); a.dirty = true; a.vonly = false; }
#endif
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
                    bool rerender = a.t->cur && a.t->cur->internal_ui && p->internal_ui && !strcmp(a.t->cur->url, p->url);
                    float keep_sy = rerender ? a.t->sy : 0;
                    page_free(a.t->cur); a.t->cur = p; a.t->asleep = false; a.t->loading = false; a.t->sy = keep_sy; a.t->relayout = true; a.dirty = true;
                    free(a.t->stitle); a.t->stitle = NULL;
                    if (!p->internal_ui) a.t->desktop = url_desktop(p->url);
                    if (!a.editing) snprintf(a.t->url, sizeof a.t->url, "%s", p->url);
                    if (a.t->hpos >= 0) { free(a.t->hist[a.t->hpos]); a.t->hist[a.t->hpos] = xstrdup(p->url); }
                    char title[512]; snprintf(title, sizeof title, "%s", p->d->title && *p->d->title ? p->d->title : p->url);
                    if (strncmp(p->url, "lumen:", 6) && strncmp(p->url, "about:", 6) && strncmp(p->url, "data:", 5)) hist_add(p->url, title);
                    if (fg) SDL_SetWindowTitle(a.win, title);
                    sync_images(p);
                    page_start_js(&a, p);
                    a.t = act; double t0 = now_ms(); if (fg) render(&a); else a.dirty = true;
                    if (fg && !g_boot.loaded && !p->internal_ui) { g_boot.loaded = true; boot_mark("page_loaded"); g_boot.last_long = now_ms(); }
                    fprintf(stderr, "lumen: %s loaded in %.0fms, first frame %.1fms (layout+paint+present), %d boxes\n", p->url, p->load_ms, now_ms() - t0, p->L ? p->L->nboxes : 0);
#if defined(__ANDROID__) && !defined(NDEBUG)
                    __android_log_print(ANDROID_LOG_INFO, "LumenLoad", "loader_ms=%.0f first_frame_ms=%.1f boxes=%d", p->load_ms, now_ms() - t0, p->L ? p->L->nboxes : 0);
#endif
                }
            }
#ifdef __ANDROID__
            if (touch_tap) g_press = g_drag = g_epress = g_cpress = g_upress = false;
#endif
        } while (SDL_PollEvent(&ev));
#ifdef __ANDROID__
        android_fling_tick(&a);
        tsel_long_press(&a);
        bar_hold_tick(&a);
        if (g_kbd_show_at && now_ms() >= g_kbd_show_at) {
            g_kbd_show_at = 0;
            if (SDL_TextInputActive(a.win)) SDL_StopTextInput(a.win);
            SDL_StartTextInput(a.win);
        }
#endif
        js_release_pins();
        double lt0 = now_ms(), tr_js = 0, tr_restyle = 0, tr_img = 0, tr_bg = 0;
        net_poll();
        if (a.t->cur && a.t->cur->sheets_pending) restyle(&a);
        double lt1 = now_ms();
        { Tab *act = a.t; double tn = now_ms(); for (int i = 0; i < a.ntabs; i++) { a.t = a.tabs[i]; if (a.t->limited) { a.t->budget = LMIN(a.t->budget + a.t->lim * (tn - a.t->bud_t), 100); a.t->bud_t = tn; if (a.t->budget < 0) continue; } if (a.t->cur && a.t->cur->js) { double c0 = now_ms(); js_tick(a.t->cur->js); double c1 = now_ms(); restyle(&a); double c2 = now_ms(); fire_img_events(a.t->cur); double c = now_ms() - c0; tr_js += c1 - c0; tr_restyle += c2 - c1; tr_img += now_ms() - c2; a.t->cpu_ms += c; if (a.t != act) tr_bg += c; if (a.t->limited) a.t->budget -= c; } } a.t = act; if (a.t->cur && a.t->cur->js) frames_tick(&a); }
        edit_sel_sync(&a);
        double lt2 = now_ms();
        bool rendered = a.dirty;
        if (a.dirty) { double r0 = now_ms(); render(&a); a.t->cpu_ms += now_ms() - r0; }
        if (lt0 < g_load_trace_until && now_ms() - lt0 >= 100) LOAD_TRACE("stall_ms=%.0f net_poll_ms=%.0f js_style_ms=%.0f (js=%.0f restyle=%.0f img=%.0f) render_ms=%.0f", now_ms() - lt0, lt1 - lt0, lt2 - lt1, tr_js, tr_restyle, tr_img, now_ms() - lt2);
        if (lt0 < g_load_trace_until) {   /* where the main thread's time went per 2 s, short loops included */
            static double w0, w_net, w_js, w_rs, w_img, w_bg, w_rn; static int w_loops, w_frames;
            if (!w0) w0 = lt0;
            w_net += lt1 - lt0; w_js += tr_js; w_rs += tr_restyle; w_img += tr_img; w_bg += tr_bg; w_rn += now_ms() - lt2; w_loops++; w_frames += rendered;
            if (now_ms() - w0 >= 2000) {
                LOAD_TRACE("busy window_ms=%.0f loops=%d frames=%d net=%.0f js=%.0f restyle=%.0f img=%.0f bg_tabs=%.0f render=%.0f (style=%.0f passes=%d slow=%d layout=%.0f dl=%.0f raster=%.0f)",
                           now_ms() - w0, w_loops, w_frames, w_net, w_js, w_rs, w_img, w_bg, w_rn, g_tr, g_tr_n, g_tr_slow, g_tl, g_td, g_tx);
                { char wk[300]; int o = snprintf(wk, sizeof wk, "wakes timeout");
                  for (int k = 0; k < 16; k++) if (g_wk_to[k]) o += snprintf(wk + o, sizeof wk - (size_t)o, " s%d=%d", k, g_wk_to[k]);
                  o += snprintf(wk + o, sizeof wk - (size_t)o, " event");
                  for (int k = 0; k < 8 && g_wk_ev[k][1] && o < 260; k++) o += snprintf(wk + o, sizeof wk - (size_t)o, " 0x%x=%d", g_wk_ev[k][0], g_wk_ev[k][1]);
                  LOAD_TRACE("%s", wk); memset(g_wk_to, 0, sizeof g_wk_to); memset(g_wk_ev, 0, sizeof g_wk_ev); }
                if (!g_paint_profile.enabled && w_frames) {   /* which render path the frames took, and why page scrolls were not shifted */
                    LOAD_TRACE("paths blit=%u bdmg=%u (no %u/%u/%u/%u) sblit=%u sdirty=%u skip=%u idle=%u rect=%u dmg=%u full=%u why=%u/%u/%u/%u/%u/%u noblit=%u/%u/%u wake caret=%u anim=%u vbars=%u defer=%u sb_fail nosbox=%u ui=%u clip=%u x=%u d=%u area=%u",
                               g_pp[PP_BLIT], g_pp[PP_BDMG], g_sdmg_no[0], g_sdmg_no[1], g_sdmg_no[2], g_sdmg_no[3], g_pp[PP_SBLIT], g_pp[PP_SDIRTY], g_pp[PP_SKIP], g_pp[PP_IDLE], g_pp[PP_RECT], g_pp[PP_DMG], g_pp[PP_FULL],
                               g_pp_why[0], g_pp_why[1], g_pp_why[2], g_pp_why[3], g_pp_why[4], g_pp_why[5], g_pp_noblit[0], g_pp_noblit[1], g_pp_noblit[2],
                               g_wake[0], g_wake[1], g_wake[2], g_wake[3], g_pp[PP_NOSBOX], g_pp[PP_SB_UI], g_pp[PP_SB_CLIP], g_pp[PP_SB_X], g_pp[PP_SB_D], g_pp[PP_SB_AREA]);
                    memset(g_pp, 0, sizeof g_pp); memset(g_pp_why, 0, sizeof g_pp_why);
                }
                memset(g_pp_noblit, 0, sizeof g_pp_noblit); memset(g_wake, 0, sizeof g_wake); memset(g_sdmg_no, 0, sizeof g_sdmg_no);
                w0 = now_ms(); w_net = w_js = w_rs = w_img = w_bg = w_rn = 0; w_loops = w_frames = 0;
                if (!g_paint_profile.enabled) g_tr = g_tl = g_td = g_tx = 0;
                g_tr_n = g_tr_slow = 0;
            }
        }
        (void)tr_js; (void)tr_restyle; (void)tr_img; (void)tr_bg; (void)rendered;
        cpu_monitor(&a);
#ifdef __ANDROID__
        if (g_bare && !g_fs_page && !panel_shown(&a)) bare_set(&a, false);   /* the panel went away with the viewer up */
#endif
        if (g_fs_page && a.t->cur != g_fs_page) page_fs_exit(&a);   /* the fullscreen page was left or closed */
        { float want = !g_fs_page && (a.t->info || *g_notice) ? INFOH : 0; if (want != g_info_h) { g_info_h = want; for (int i = 0; i < a.ntabs; i++) a.tabs[i]->relayout = true; a.dirty = true; a.vonly = false; } }
        if (*cookie_path && now_ms() - cookies_saved_at > 5000) { cookies_save(cookie_path); cookies_saved_at = now_ms(); }
    }
    if (*cookie_path) cookies_save(cookie_path);
#ifdef __ANDROID__
    SDL_RemoveEventWatch(android_graphics_lifecycle, &a);
    android_launch_set_wakeup(NULL);
    android_back_set_wakeup(NULL);
    char *pending_url;
    while ((pending_url = android_launch_take())) free(pending_url);
#endif
    for (int i = 0; i < a.ntabs; i++) if (a.tabs[i]->cur) page_free(a.tabs[i]->cur);
    gpu_destroy(a.gpu);
    if (a.mview) SDL_Metal_DestroyView(a.mview);
    SDL_DestroyWindow(a.win); SDL_Quit();
    return 0;
}
