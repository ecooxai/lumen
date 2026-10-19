#ifndef LUMEN_ANDROID_UI_H
#define LUMEN_ANDROID_UI_H
/* Browser menus drawn with Android views (LumenActivity): the toolbar's more menu, the
 * bookmarks and workspaces panels, and toasts. Choices come back through android_ui_action. */
#ifdef __ANDROID__
#include <stdbool.h>
enum {
    UI_NEW_TAB = 1, UI_FORWARD, UI_BOOKMARKS, UI_DOWNLOADS, UI_HISTORY, UI_WORKSPACES, UI_SETTINGS,
    UI_DESKTOP,      /* value: 1 on, 0 off */
    UI_ZOOM,         /* value: percent */
    UI_BM_TOGGLE,    /* bookmark or unbookmark the current page */
    UI_BM_OPEN,      /* s2: URL (javascript: runs on the current page) */
    UI_BM_SAVE,      /* value: index or -1 for a new one; s1 title, s2 URL */
    UI_BM_DELETE,    /* value: index; s2: its URL */
    UI_WS_SELECT,    /* value: workspace */
    UI_WS_NEW,       /* s1: name */
    UI_WS_RENAME,    /* value: workspace; s1: name */
    UI_WS_CLOSE,     /* value: workspace */
    UI_BACK, UI_RELOAD,
    UI_FULLSCREEN,   /* value: 1 on (system bars hidden), 0 off */
    UI_URL_GO,       /* s2: what was typed in the address bar (javascript: runs on the current page) */
};
typedef struct { bool can_forward, desktop, can_desktop, can_zoom, bookmarked, can_bookmark, can_back, fullscreen; int zoom_pct; } AndroidMenuState;
/* the outline icon of a menu action, also drawn by BrowserIcons.java: on a 24 unit grid, "M x y" and
   "L x y" polylines, "Z" closes, "O cx cy r" circles, "A cx cy r start sweep" arcs (degrees,
   clockwise from 3 o'clock); NULL if the action has none */
const char *android_ui_icon(int code);
/* The toolbar row (address field, two action slots, more menu) is an Android view, drawn and
   touched on the UI thread so it answers while the engine thread is busy. Geometry in window
   pixels; sent only when something changed. */
typedef struct {
    int left, top, width, height;
    int nslot, slot[4]; bool slot_enabled[4], slot_lit[4];
    bool loading;
    AndroidMenuState menu;
    bool hud, touch_feedback;   /* Settings: the CPU readout next to the address field, the touch circles */
    int touch_rgb, touch_alpha, touch_size, touch_ms;   /* the circles' 0xRRGGBB, opacity %, diameter dp, hide delay ms */
} AndroidBarState;
void android_ui_toolbar(const AndroidBarState *state, const char *url);
void android_ui_fullscreen(bool on);
void android_ui_toast(const char *text);
/* for the CPU readout's details (LumenActivity.nativeTabStats), written by the engine thread about
   once a second while the readout is on: the current tab's CPU (% of one core), the JS heap and
   V8 external memory of all tabs (MB), the number of tabs */
extern volatile float android_ui_stats[4];
void android_ui_menu(const AndroidMenuState *state);
void android_ui_bookmarks(const char *const *titles, const char *const *urls, int n, const char *page_title, const char *page_url);
void android_ui_workspaces(const char *const *names, const int *tabs, int n, int current);
/* set by the app; called on the Android UI thread, takes ownership of s1 and s2 (malloc'd, may be NULL) */
extern void (*android_ui_action)(int code, int value, char *s1, char *s2);
#endif
#endif
