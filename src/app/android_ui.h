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
};
typedef struct { bool can_forward, desktop, can_desktop, can_zoom, bookmarked, can_bookmark; int zoom_pct; } AndroidMenuState;
void android_ui_toast(const char *text);
void android_ui_menu(const AndroidMenuState *state);
void android_ui_bookmarks(const char *const *titles, const char *const *urls, int n, const char *page_title, const char *page_url);
void android_ui_workspaces(const char *const *names, const int *tabs, int n, int current);
/* set by the app; called on the Android UI thread, takes ownership of s1 and s2 (malloc'd, may be NULL) */
extern void (*android_ui_action)(int code, int value, char *s1, char *s2);
#endif
#endif
