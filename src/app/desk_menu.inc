/* Desktop menus, drawn by Lumen over the page as Android's BrowserMenus are drawn with views: the
 * toolbar's more menu, right-click menus for pages, links, images, fields, tabs and workspaces, site
 * info and short confirmations (toasts). Choices go through ui_action like Android's. Included by
 * main.c (not Android). */
enum { DM_ROW_H = 34, DM_ICON_H = 44, DM_TITLE_H = 26, DM_SUB_H = 20, DM_SEP_H = 9, DM_MAX = 40 };
typedef struct {
    int code, value, line;       /* line: 0 a text row, else the icon line it sits on */
    bool enabled, check, on, sep, accent;
    const char *icon;            /* NULL: android_ui_icon(code) */
    char label[160]; char *s2;
    float x, y, w, h;
} DmRow;
static struct { bool open; int n, hover; float x, y, w, h, ax, ay; bool right; char title[160], sub[300]; DmRow r[DM_MAX]; } g_dm;
static float g_dm_mx, g_dm_my;   /* where the last right click was */
static bool dm_menu_up(void) { return g_dm.open && g_dm.right; }   /* the toolbar's menu is open */

/* a path icon rastered at the window's scale, cached by path, colour and size */
static Image *path_img(App *a, const char *path, uint32_t rgb, int dip) {
    static struct { const char *p; uint32_t rgb; int dip; float scale; Image *im; } c[96]; static int next;
    if (!path) return NULL;
    for (int i = 0; i < 96; i++) if (c[i].im && c[i].p == path && c[i].rgb == rgb && c[i].dip == dip && c[i].scale == a->scale) return c[i].im;
    int px = (int)(dip * a->scale + 0.5f); Image *im = xcalloc(1, sizeof *im);
    im->w = im->h = px; im->refs = 1; im->scale = a->scale; im->px = xcalloc((size_t)px * (size_t)px, 4);
    if (!path_icon_rgba(path, px, 1.7f, rgb, im->px)) { free(im->px); free(im); return NULL; }
    int k = next; next = (next + 1) % 96;
    if (c[k].im) image_unref(c[k].im);
    c[k].p = path; c[k].rgb = rgb; c[k].dip = dip; c[k].scale = a->scale; c[k].im = im;
    return im;
}
static void dm_close(App *a) {
    if (!g_dm.open && !g_dm.n) return;
    for (int i = 0; i < g_dm.n; i++) { free(g_dm.r[i].s2); g_dm.r[i].s2 = NULL; }
    g_dm.open = false; g_dm.n = 0; g_dm.hover = -1; g_dm.title[0] = g_dm.sub[0] = 0;
    g_dm_logo = false;
    a->dirty = true; a->vonly = false;
}
static DmRow *dm_add(int code, int value, int line, const char *label, bool enabled) {
    if (g_dm.n >= DM_MAX) return NULL;
    DmRow *r = &g_dm.r[g_dm.n++]; memset(r, 0, sizeof *r);
    r->code = code; r->value = value; r->line = line; r->enabled = enabled;
    snprintf(r->label, sizeof r->label, "%s", label ? label : "");
    return r;
}
static void dm_sep(void) { if (g_dm.n < DM_MAX) g_dm.r[g_dm.n].sep = true; }
static DmRow *dm_add_sep(int code, int value, int line, const char *label, bool enabled) {
    bool sep = g_dm.n < DM_MAX && g_dm.r[g_dm.n].sep;
    DmRow *r = dm_add(code, value, line, label, enabled);
    if (r) r->sep = sep && g_dm.n > 1;
    return r;
}
static Font *dm_bold(void) { static Font *f; if (!f) f = font_get("system-ui", 700, false, 14.f); return f; }
/* lays the rows out at (x, y): x is the right edge when right is set; kept inside the window */
static void dm_show(App *a, float x, float y, bool right) {
    Font *f = a->ui; float w = 220;
    for (int i = 0; i < g_dm.n; i++) if (!g_dm.r[i].line) w = LMAX(w, text_width(f, g_dm.r[i].label, strlen(g_dm.r[i].label), 0) + 70 + (g_dm.r[i].check ? 30 : 0));
    if (*g_dm.title) w = LMAX(w, LMIN(420, text_width(dm_bold(), g_dm.title, strlen(g_dm.title), 0) + 40));
    w = LMIN(w, LMIN(420, a->vw - 8));
    float h = 6;
    if (*g_dm.title) h += DM_TITLE_H + (*g_dm.sub ? DM_SUB_H : 0) + DM_SEP_H;
    for (int i = 0; i < g_dm.n;) {
        DmRow *r = &g_dm.r[i];
        if (r->sep) h += DM_SEP_H;
        if (r->line) {
            int j = i; while (j < g_dm.n && g_dm.r[j].line == r->line && (j == i || !g_dm.r[j].sep)) j++;
            float bw = (w - 8) / (float)(j - i);
            for (int k = i; k < j; k++) { g_dm.r[k].x = 4 + (float)(k - i) * bw; g_dm.r[k].y = h; g_dm.r[k].w = bw; g_dm.r[k].h = DM_ICON_H; }
            h += DM_ICON_H; i = j;
        } else { r->x = 4; r->y = h; r->w = w - 8; r->h = DM_ROW_H; h += DM_ROW_H; i++; }
    }
    h += 6;
    g_dm.ax = x; g_dm.ay = y; g_dm.right = right;
    float x0 = right ? x - w : x;
    if (x0 + w > a->vw - 4) x0 = a->vw - 4 - w;
    if (y + h > a->vh - 4) y = LMAX(4, a->vh - 4 - h);
    g_dm.x = LMAX(4, x0); g_dm.y = y; g_dm.w = w; g_dm.h = h;
    g_dm.open = g_dm.n > 0; g_dm.hover = -1;
    a->dirty = true; a->vonly = false;
}
static int dm_hit(float x, float y) {
    if (!g_dm.open) return -1;
    for (int i = 0; i < g_dm.n; i++) {
        DmRow *r = &g_dm.r[i];
        if (r->enabled && r->code != UI_NONE_ROW && x >= g_dm.x + r->x && x < g_dm.x + r->x + r->w && y >= g_dm.y + r->y && y < g_dm.y + r->y + r->h) return i;
    }
    return -1;
}
static bool dm_inside(float x, float y) { return g_dm.open && x >= g_dm.x && x < g_dm.x + g_dm.w && y >= g_dm.y && y < g_dm.y + g_dm.h; }
static void dm_box(float *b) { b[0] = g_dm.x - 3; b[1] = g_dm.y - 2; b[2] = g_dm.x + g_dm.w + 3; b[3] = g_dm.y + g_dm.h + 4; }
static void dm_build(App *a, DisplayList *dl) {
    float box[4] = { 1e9f, 1e9f, -1e9f, -1e9f }; bool any = false;
    Font *f = a->ui; float rb = (f->ascent - f->descent) / 2;
    if (g_dm.open) {
        float x = g_dm.x, y = g_dm.y, w = g_dm.w, h = g_dm.h;
        push_rect(dl, x - 3, y - 1, w + 6, h + 5, 12, RGBA(0, 0, 0, 18));
        push_rect(dl, x - 1, y, w + 2, h + 2, 11, RGBA(0, 0, 0, 30));
        push_rect(dl, x, y, w, h, 10, RGBA(255, 255, 255, 255));
        float ty = y + 6;
        if (*g_dm.title) {
            Font *bf = dm_bold();
            push_text(dl, bf, g_dm.title, x + 16, ty + DM_TITLE_H / 2.f + (bf->ascent - bf->descent) / 2, w - 32, RGBA(32, 33, 36, 255));
            ty += DM_TITLE_H;
            if (*g_dm.sub) {
                static Font *sf; if (!sf) sf = font_get("system-ui", 400, false, 12.f);
                push_text(dl, sf, g_dm.sub, x + 16, ty + DM_SUB_H / 2.f + (sf->ascent - sf->descent) / 2, w - 32, RGBA(95, 99, 104, 255));
                ty += DM_SUB_H;
            }
            push_rect(dl, x, ty + DM_SEP_H / 2.f, w, 1, 0, RGBA(232, 234, 237, 255));
        }
        for (int i = 0; i < g_dm.n; i++) {
            DmRow *r = &g_dm.r[i]; float rx = x + r->x, ry = y + r->y;
            if (r->sep) push_rect(dl, x, ry - DM_SEP_H / 2.f - 0.5f, w, 1, 0, RGBA(232, 234, 237, 255));
            bool en = r->enabled || r->code == UI_NONE_ROW;
            uint32_t ic = !r->enabled && r->code != UI_NONE_ROW ? 0xbdc1c6 : r->accent ? 0x1a73e8 : r->code == UI_NONE_ROW && r->icon == ui_site_icon(2) ? 0xd93025 : r->line ? 0x202124 : 0x5f6368;
            Color tc = !en ? RGBA(189, 193, 198, 255) : r->accent ? RGBA(26, 115, 232, 255) : RGBA(32, 33, 36, 255);
            if (i == g_dm.hover) push_rect(dl, rx + 2, ry + 2, r->w - 4, r->h - 4, r->line ? 8 : 6, RGBA(232, 234, 237, 255));
            const char *path = r->icon ? r->icon : android_ui_icon(r->code);
            if (r->line) {
                Image *im = *r->label ? NULL : path_img(a, path, ic, 22);
                if (im) push_img(dl, im, rx + (r->w - 22) / 2, ry + (r->h - 22) / 2, 22, 22);
                else { float lw = text_width(f, r->label, strlen(r->label), 0); push_text(dl, f, r->label, rx + (r->w - lw) / 2, ry + r->h / 2 + rb, r->w, tc); }
                continue;
            }
            Image *im = path_img(a, path, ic, 20);
            if (im) push_img(dl, im, rx + 12, ry + (r->h - 20) / 2, 20, 20);
            push_text(dl, f, r->label, rx + 44, ry + r->h / 2 + rb, r->w - 52 - (r->check ? 30 : 0), tc);
            if (r->check) {   /* a check box at the right end */
                float cx = rx + r->w - 28, cy = ry + (r->h - 16) / 2;
                if (r->on) {
                    push_rect(dl, cx, cy, 16, 16, 3, RGBA(26, 115, 232, 255));
                    static const char *tick = "M5.5 12.5 L10 17 L18.5 7.5";
                    Image *t = path_img(a, tick, 0xffffff, 16);
                    if (t) push_img(dl, t, cx, cy, 16, 16);
                } else {
                    DItem it; memset(&it, 0, sizeof it); it.op = DO_BORDER; it.x = cx; it.y = cy; it.w = it.h = 16;
                    for (int k = 0; k < 4; k++) { it.r[k] = 3; it.bw[k] = 1.5f; it.bc[k] = RGBA(95, 99, 104, 255); it.bs[k] = 1; }
                    vec_push(dl->items, it);
                }
            }
        }
        dm_box(box); any = true;
    }
    if (g_toast_until && *g_toast) {
        float tw = LMIN(text_width(f, g_toast, strlen(g_toast), 0), a->vw - 80), w = tw + 32, h = 36, x = (a->vw - w) / 2, y = a->vh - h - 28;
        push_rect(dl, x, y, w, h, 8, RGBA(32, 33, 36, 240));
        push_text(dl, f, g_toast, x + 16, y + h / 2 + rb, tw, RGBA(255, 255, 255, 255));
        box[0] = LMIN(box[0], x); box[1] = LMIN(box[1], y); box[2] = LMAX(box[2], x + w); box[3] = LMAX(box[3], y + h); any = true;
    }
    if (any) memcpy(g_pop, box, sizeof box);
}
static void dm_click(App *a, int i, bool *quit) {
    if (i < 0 || i >= g_dm.n) return;
    DmRow r = g_dm.r[i]; char *s2 = r.s2; g_dm.r[i].s2 = NULL;
    dm_close(a);
    if (r.code < 0) {   /* a workspace menu command for workspace r.value */
        if (r.value != a->wi && r.value >= 0 && r.value < a->nws) ws_select(a, r.value);
        menu_cmd(a, -r.code);
    } else if (r.code > 0 && r.code != UI_NONE_ROW) {
        UiAction u = { r.code, r.value, NULL, s2 }; s2 = NULL;
        ui_action(a, &u, quit);
        free(u.s1); free(u.s2);
        if (r.code == UI_ZOOM) dm_more(a);   /* the zoom levels stay up, as on Android */
    }
    free(s2);
    a->dirty = true; a->vonly = false;
}
/* Escape, the arrows and Enter while a menu is up; false lets the key go on (and closes the menu) */
static bool dm_key(App *a, SDL_Keycode k, bool *quit) {
    if (!g_dm.open) return false;
    if (k == SDLK_ESCAPE) { dm_close(a); return true; }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { if (g_dm.hover >= 0) dm_click(a, g_dm.hover, quit); else dm_close(a); return true; }
    int d = k == SDLK_DOWN || k == SDLK_RIGHT ? 1 : k == SDLK_UP || k == SDLK_LEFT ? -1 : 0;
    if (!d) { dm_close(a); return false; }
    for (int s = 0, i = g_dm.hover; s < g_dm.n; s++) {
        i = i < 0 ? (d > 0 ? 0 : g_dm.n - 1) : (i + d + g_dm.n) % g_dm.n;
        if (g_dm.r[i].enabled && g_dm.r[i].code != UI_NONE_ROW) { g_dm.hover = i; break; }
    }
    a->dirty = true; a->vonly = false;
    return true;
}

/* the toolbar's menu, as Android's: navigation and editing icon lines, tabs, zoom, page modes, Lumen's pages */
static void dm_more(App *a) {
    dm_close(a);
    AndroidMenuState st = menu_state(a);
    static const int nav[] = { UI_BACK, UI_FORWARD, UI_RELOAD, UI_BM_TOGGLE, UI_DOWNLOADS }, edit[] = { UI_CUT, UI_COPY, UI_PASTE, UI_UNDO, UI_REDO };
    for (int i = 0; i < 5; i++) {
        int c = nav[i]; DmRow *r = dm_add(c, 0, 1, "", c == UI_BACK ? st.can_back : c == UI_FORWARD ? st.can_forward : c == UI_BM_TOGGLE ? st.can_bookmark : true);
        if (r && c == UI_BM_TOGGLE) r->accent = st.bookmarked;
    }
    for (int i = 0; i < 5; i++) dm_add(edit[i], 0, 2, "", true);
    dm_sep(); dm_add_sep(UI_NEW_TAB, 0, 0, "New tab", true);
    dm_add(UI_WORKSPACES, 0, 0, "Workspaces", true);
    char zl[48]; snprintf(zl, sizeof zl, "Zoom  %d%%", st.zoom_pct);
    dm_sep(); dm_add_sep(UI_NONE_ROW, 0, 0, zl, true)->icon = android_ui_icon(UI_ZOOM);
    dm_add(UI_ZOOM_STEP, -1, 3, "\xE2\x88\x92", st.zoom_pct > 25);
    static const int lv[] = { 50, 100, 150, 200 };
    for (int i = 0; i < 4; i++) { char t[8]; snprintf(t, sizeof t, "%d", lv[i]); DmRow *r = dm_add(UI_ZOOM, lv[i], 3, t, true); if (r) r->accent = st.zoom_pct == lv[i]; }
    dm_add(UI_ZOOM_STEP, 1, 3, "+", st.zoom_pct < 300);
    DmRow *r;
    dm_sep(); r = dm_add_sep(UI_DESKTOP, !st.desktop, 0, st.can_desktop == 2 ? "Desktop site (all sites)" : "Desktop site", true); if (r) { r->check = true; r->on = st.desktop; }
    r = dm_add(UI_FULLSCREEN, !st.fullscreen, 0, "Fullscreen", true); if (r) { r->check = true; r->on = st.fullscreen; }
    dm_sep(); dm_add_sep(UI_SETTINGS, 0, 0, "Settings", true);
    dm_sep(); dm_add_sep(UI_BOOKMARKS, 0, 0, "Bookmarks", true);
    dm_add(UI_HISTORY, 0, 0, "History", true);
    dm_add(UI_DOWNLOADS, 0, 0, "Downloads", true);
    dm_sep(); dm_add_sep(UI_EXIT, 0, 0, "Exit", true);
    snprintf(g_dm.title, sizeof g_dm.title, "%s", "");
    dm_show(a, bar_x0(a) + bar_w(a) - 6, TABH + TB + 2, true);
    if (g_dm.w < 280) dm_show(a, bar_x0(a) + bar_w(a) - 6, TABH + TB + 2, true), g_dm.x = LMAX(4, bar_x0(a) + bar_w(a) - 6 - 280), g_dm.w = 280, dm_show(a, g_dm.x, TABH + TB + 2, false);
}
/* a list of choices at the last right click; row i goes back as code with value i */
static void dm_list(App *a, const char *title, const char *const *items, int n, int code) {
    dm_close(a);
    snprintf(g_dm.title, sizeof g_dm.title, "%s", title ? title : "");
    for (int i = 0; i < n; i++) {
        DmRow *r = dm_add(code, i, 0, items[i], true);
        if (r) r->icon = code == UI_CTX_MENU ? (g_ctx.act[i] == CTX_COPY_IMG ? android_ui_icon(UI_COPY) : g_ctx.act[i] == CTX_SAVE_IMG ? android_ui_icon(UI_DOWNLOADS) : android_ui_icon(UI_COPY_TEXT)) : NULL;
    }
    dm_show(a, g_dm_mx, g_dm_my, false);
}
static void dm_tab_menu(App *a, const char *title, const char *const *items, const bool *on, int n) {
    dm_close(a);
    snprintf(g_dm.title, sizeof g_dm.title, "%s", title);
    for (int i = 0; i < n; i++) {
        if (i == 1 || i == 6) dm_sep();
        DmRow *r = dm_add_sep(UI_TAB_MENU, i, 0, items[i], true);
        if (r && i) { r->check = true; r->on = on[i]; }
        if (r) r->icon = i == 0 ? "M6 6 L18 18 M18 6 L6 18" : "";
    }
    dm_show(a, g_dm_mx, g_dm_my, false);
}
/* the logo's window menu: minimize, maximize / restore, close */
static void dm_win_menu(App *a) {
    dm_close(a);
    bool max = SDL_GetWindowFlags(a->win) & SDL_WINDOW_MAXIMIZED;
    dm_add(UI_WIN_MIN, 0, 0, "Minimize", true)->icon = WIN_IC[0];
    dm_add(UI_WIN_MAX, 0, 0, max ? "Restore" : "Maximize", true)->icon = WIN_IC[max ? 3 : 1];
    dm_sep(); dm_add_sep(UI_EXIT, 0, 0, "Close", true)->icon = WIN_IC[2];
    dm_show(a, 4, (g_compact ? TB : TABH) + 2, false);
    g_dm_logo = true;
}
static int site_kind(const char *url) {
    if (!url || !*url) return 0;
    return !strncasecmp(url, "https://", 8) ? 1 : !strncasecmp(url, "http://", 7) ? 2 : 3;
}
/* Site info, from the icon at the address field's left end, as on Android */
static void dm_site_info(App *a) {
    Tab *t = bar_tab(a); const char *url = t->url; int kind = site_kind(url);
    if (!kind) return;
    dm_close(a);
    bool web = kind <= 2; char site[256] = ""; int cookies = 0; URL u;
    const char *host = NULL;
    if (url_parse(url, &u)) {
        if (u.host && *u.host) { snprintf(g_dm.title, sizeof g_dm.title, "%s", u.host); host = g_dm.title; if (web) { net_site_of(u.host, site, sizeof site); cookies = cookies_site(site, false); } }
        url_free(&u);
    }
    if (!host || !web) snprintf(g_dm.title, sizeof g_dm.title, "%s", !strncmp(url, "lumen:", 6) ? "Lumen page" : !strncmp(url, "file:", 5) ? "File on this computer" : host ? host : "This page");
    snprintf(g_dm.sub, sizeof g_dm.sub, "%s", url);
    dm_add(UI_NONE_ROW, 0, 0, kind == 1 ? "Connection is secure" : kind == 2 ? "Connection is not secure" : "This page is not on the web", true)->icon = ui_site_icon(kind);
    if (web) {
        char line[320]; snprintf(line, sizeof line, "%s for %s", cookies == 0 ? "No cookies" : cookies == 1 ? "1 cookie" : "Cookies", *site ? site : "this site");
        if (cookies > 1) snprintf(line, sizeof line, "%d cookies for %s", cookies, *site ? site : "this site");
        dm_add(UI_NONE_ROW, 0, 0, line, true)->icon = ui_site_icon(4);
        dm_sep(); DmRow *r = dm_add_sep(UI_SITE_CLEAR_ASK, 0, 0, "Delete data", true);
        if (r) { r->accent = true; r->s2 = xstrdup(url); }
    }
    dm_show(a, bar_x0(a) + url_x(a), TABH + TB + 2, false);
    if (g_dm.w < 340 && a->vw > 360) { float x = g_dm.x; g_dm.w = 340; for (int i = 0; i < g_dm.n; i++) g_dm.r[i].w = 332; g_dm.x = LMIN(x, a->vw - 4 - 340); }
}
static void dm_site_clear_ask(App *a, const char *url) {
    URL u; char site[256] = "";
    if (!url || !url_parse(url, &u)) return;
    if (u.host && *u.host) net_site_of(u.host, site, sizeof site);
    url_free(&u);
    if (!*site) return;
    dm_close(a);
    snprintf(g_dm.title, sizeof g_dm.title, "Delete data of %s?", site);
    snprintf(g_dm.sub, sizeof g_dm.sub, "%s", "Its cookies and storage go, this page reloads and you are signed out.");
    DmRow *r = dm_add(UI_SITE_CLEAR, 0, 0, "Delete", true);
    if (r) { r->accent = true; r->s2 = xstrdup(url); }
    dm_add(0, 0, 0, "Cancel", true)->icon = "M6 6 L18 18 M18 6 L6 18";
    dm_show(a, bar_x0(a) + url_x(a), TABH + TB + 2, false);
}
static struct { unsigned char *b; size_t n; } g_clip_png;
static const void *clip_png_data(void *ud, const char *mime, size_t *size) {
    (void)ud;
    if (!mime || strcmp(mime, "image/png") || !g_clip_png.b) { *size = 0; return NULL; }
    *size = g_clip_png.n; return g_clip_png.b;
}
/* the PNG onto the system clipboard; takes png when it returns true */
static bool clip_png_set(unsigned char *png, size_t n) {
    static const char *types[] = { "image/png" };
    free(g_clip_png.b); g_clip_png.b = png; g_clip_png.n = n;
    if (SDL_SetClipboardData(clip_png_data, NULL, NULL, types, 1)) return true;
    g_clip_png.b = NULL; g_clip_png.n = 0;
    return false;
}
/* the link (a[href]) around page point x, y, as an absolute URL (malloc'd) */
static char *link_at(App *a, float x, float y) {
    Page *p = a->t->cur;
    if (!p || !p->L || !p->d) return NULL;
    Box *b = layout_hit(p->L, x, y + a->t->sy, 0, a->t->sy);
    for (Node *k = b ? b->node : NULL; k; k = k->parent) if (k->tag == A_a && node_attr(k, "href")) {
        const char *h = node_attr(k, "href");
        if (!*h || is_js_url(h)) return NULL;
        return url_join(p->d->url, h);
    }
    return NULL;
}
/* a right click on the page: the page hears contextmenu first; then the image, link, field or page menu */
static void dm_page_menu(App *a, float wx, float wy) {
    Page *p = a->t->cur;
    if (!p || !p->L || a->t->asleep) return;
    float px = page_x(a, wx), py = page_y(a, wy);
    h_sync_in(a, p->d, true);
    Box *b = layout_hit(p->L, px, py + a->t->sy, 0, a->t->sy);
    Node *target = b ? b->node : NULL;
    while (target && target->type != NODE_ELEMENT) target = target->parent;
    if (target && p->js && !js_dispatch(p->js, target, "contextmenu", "MouseEvent", true, true, px, py, 2, NULL)) return;
    if (a->t->cur != p || !p->L) return;
    h_sync_in(a, p->d, true);
    b = layout_hit(p->L, px, py + a->t->sy, 0, a->t->sy);
    Node *hit = b ? b->node : NULL;
    g_dm_mx = wx; g_dm_my = wy;
    char *link = link_at(a, px, py);
    if (img_ctx_menu(a, hit, px, py + a->t->sy)) {   /* an image: its rows, then opening it or its link */
        dm_sep(); DmRow *r = dm_add_sep(UI_LINK_TAB, 0, 0, "Open image in new tab", true);
        if (r) r->s2 = xstrdup(g_ctx.img);
        if (link && (r = dm_add(UI_LINK_TAB, 0, 0, "Open link in new tab", true))) { r->s2 = link; link = NULL; }
        dm_show(a, wx, wy, false);
        free(link);
        return;
    }
    dm_close(a);
    {   /* a right click on a field focuses it, as in Chrome, keeping its selection */
        Node *ctl = NULL;
        for (Node *k = hit; k && !ctl; k = k->parent) if (is_text_ctl(k)) ctl = k;
        if (!ctl && p->js) ctl = edit_host(hit);
        if (ctl && ctl != page_focus(a) && ctl != edit_focus(a)) { focus_node(a, ctl); restyle(a); }
        if (a->t->cur != p || !p->L) return;
    }
    Node *pf = page_focus(a), *ef = edit_focus(a), *fo = pf ? pf : ef; bool in_field = false;
    for (Node *k = hit; k && fo; k = k->parent) if (k == fo) { in_field = true; break; }
    char *sel = pf ? ctl_selected(pf) : NULL; bool failed = false;
    if ((!sel || !*sel) && p->js) { free(sel); sel = js_eval_result(p->js, "String(getSelection())", "lumen-menu", &failed); if (failed) { free(sel); sel = NULL; } }
    bool has_sel = sel && *sel;
    if (link) {
        DmRow *r = dm_add(UI_LINK_TAB, 0, 0, "Open link in new tab", true); if (r) r->s2 = xstrdup(link);
        r = dm_add(UI_COPY_TEXT, 0, 0, "Copy link address", true); if (r) r->s2 = xstrdup(link);
        dm_sep();
    }
    if (in_field) {
        dm_add_sep(UI_UNDO, 0, 0, "Undo", true); dm_add(UI_REDO, 0, 0, "Redo", true);
        dm_sep(); dm_add_sep(UI_CUT, 0, 0, "Cut", has_sel); dm_add(UI_COPY, 0, 0, "Copy", has_sel); dm_add(UI_PASTE, 0, 0, "Paste", true);
        dm_sep(); dm_add_sep(UI_SELECT_ALL, 0, 0, "Select all", true);
    } else {
        if (has_sel) { dm_add_sep(UI_COPY, 0, 0, "Copy", true); dm_sep(); }
        Tab *t = a->t;
        dm_add_sep(UI_BACK, 0, 1, "", t->hpos > 0); dm_add(UI_FORWARD, 0, 1, "", t->hpos < t->nhist - 1); dm_add(UI_RELOAD, 0, 1, "", true);
        AndroidMenuState st = menu_state(a);
        DmRow *r = dm_add(UI_BM_TOGGLE, 0, 1, "", st.can_bookmark); if (r) r->accent = st.bookmarked;
        dm_add_sep(UI_SELECT_ALL, 0, 0, "Select all", true);
        if (*t->url && (r = dm_add(UI_COPY_TEXT, 0, 0, "Copy page address", true))) r->s2 = xstrdup(t->url);
        if (site_kind(t->url)) dm_add(UI_SITE_INFO, 0, 0, "Site information", true);
    }
    free(sel); free(link);
    dm_show(a, wx, wy, false);
}
/* a right click on a workspace in the sidebar */
static void dm_ws_menu(App *a, int w, float x, float y) {
    dm_close(a);
    if (w >= 0 && w < a->nws) {
        snprintf(g_dm.title, sizeof g_dm.title, "%s", a->wsname[w]);
        dm_add(-MENU_WS_RENAME, w, 0, "Rename workspace", true)->icon = "M4 20 L8 20 L19 9 L15 5 L4 16 Z M13 7 L17 11";
        dm_add(UI_WS_CLOSE, w, 0, "Close workspace", a->nws > 1)->icon = "M6 6 L18 18 M18 6 L6 18";
        dm_sep();
    }
    dm_add_sep(UI_WS_NEW, 0, 0, "New workspace", a->nws < 16)->icon = android_ui_icon(UI_NEW_TAB);
    dm_add(UI_WORKSPACES, 0, 0, "All workspaces", true);
    dm_show(a, x, y, false);
}
/* a right click anywhere in the window */
static void dm_right_click(App *a, float x, float y) {
    g_dm_mx = x; g_dm_my = y;
    int bh = bar_hit(a, x, y);
    if (bh >= HB_TAB && bh < HB_WS) { int i = (bh - HB_TAB) % 100; if (i >= 0 && i < a->ntabs) { if (i != a->ti) tab_select(a, i); tab_menu(a, i); } return; }
    if (bh >= HB_WS && bh < HB_INFO) { dm_ws_menu(a, bh - HB_WS, x, y); return; }
    if (bh == HB_WSNEW) { dm_ws_menu(a, -1, x, y); return; }
    if (y < BAR || x < page_left(a)) { dm_close(a); return; }
    dm_page_menu(a, x, y);
}
