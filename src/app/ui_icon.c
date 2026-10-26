/* The outline icons of the browser's actions (android_ui.h): Android's BrowserIcons draws them
 * from android_ui_icon, the desktop build rasters them with path_icon_rgba. */
#include "android_ui.h"
#include <stddef.h>

/* the site info icons: secure, not secure, other pages; the cookie line */
const char *ui_site_icon(int kind) {
    switch (kind) {
    case 1: return "M6.5 11 L17.5 11 L17.5 20 L6.5 20 Z M8.8 11 L8.8 8.2 A 12 8.2 3.2 180 180 M15.2 8.2 L15.2 11 M12 14.3 L12 16.6";
    case 2: return "M12 3.8 L21 19.5 L3 19.5 Z M12 9.5 L12 14 O 12 16.9 0.35";
    case 3: return "O 12 12 8.5 M12 11 L12 16.2 O 12 7.9 0.35";
    case 4: return "O 12 12 8.5 O 9 9 0.5 O 14.8 9.8 0.5 O 9.6 14.6 0.5 O 14.6 15 0.5 O 12 12.2 0.3";
    case 5: return "O 10.5 10.5 6 M14.8 14.8 L19.5 19.5";
    default: return NULL;
    }
}

const char *android_ui_icon(int code) {
    switch (code) {
    case UI_BACK: return "M20 12 L4.5 12 M10.5 6 L4.5 12 L10.5 18";
    case UI_FORWARD: return "M4 12 L19.5 12 M13.5 6 L19.5 12 L13.5 18";
    case UI_RELOAD: return "A 12 12 7.5 -40 -280 M18.6 2.8 L18.2 7.3 L13.7 7";
    case UI_NEW_TAB: return "M12 4.5 L12 19.5 M4.5 12 L19.5 12";
    case UI_BM_TOGGLE: return "M12.0 3.6 L14.3 9.4 L20.6 9.8 L15.7 13.8 L17.3 19.9 L12.0 16.5 L6.7 19.9 L8.3 13.8 L3.4 9.8 L9.7 9.4 Z";
    case UI_BOOKMARKS: return "M6 3.5 L18 3.5 L18 20.5 L12 15.5 L6 20.5 Z";
    case UI_HISTORY: return "O 12 12 8.5 M12 7 L12 12 L15.5 14";
    case UI_DOWNLOADS: return "M12 4 L12 15.5 M7 10.5 L12 15.5 L17 10.5 M5 20 L19 20";
    case UI_WORKSPACES: return "M4 4 L10.5 4 L10.5 10.5 L4 10.5 Z M13.5 4 L20 4 L20 10.5 L13.5 10.5 Z M4 13.5 L10.5 13.5 L10.5 20 L4 20 Z M13.5 13.5 L20 13.5 L20 20 L13.5 20 Z";
    case UI_DESKTOP: return "M3 5 L21 5 L21 16 L3 16 Z M12 16 L12 20 M8 20 L16 20";
    case UI_FULLSCREEN: return "M4 9 L4 4 L9 4 M15 4 L20 4 L20 9 M20 15 L20 20 L15 20 M9 20 L4 20 L4 15";
    case UI_KEYBOARD: return "M3 6 L21 6 L21 18 L3 18 Z M6.5 9.5 L7.5 9.5 M10 9.5 L11 9.5 M13 9.5 L14 9.5 M16.5 9.5 L17.5 9.5 M6.5 12.5 L7.5 12.5 M10 12.5 L11 12.5 M13 12.5 L14 12.5 M16.5 12.5 L17.5 12.5 M8 15.3 L16 15.3";
    case UI_COPY: return "M9 8 L19 8 L19 20.5 L9 20.5 Z M6 16.5 L5 16.5 L5 3.5 L15 3.5 L15 5";
    case UI_PASTE: return "M6 5 L18 5 L18 21 L6 21 Z M9 3 L15 3 L15 7 L9 7 Z";
    case UI_UNDO: return "M9 5 L4 10 L9 15 M4 10 L14 10 A 14 15 5 -90 180 M14 20 L9 20";
    case UI_REDO: return "M15 5 L20 10 L15 15 M20 10 L10 10 A 10 15 5 -90 -180 M10 20 L15 20";
    case UI_EDIT_MORE: return "O 5 12 1.2 O 12 12 1.2 O 19 12 1.2";
    case UI_ZOOM: return "O 10.5 10.5 6.5 M15.2 15.2 L20 20 M7.5 10.5 L13.5 10.5 M10.5 7.5 L10.5 13.5";
    case UI_SETTINGS: return "O 12 12 3 O 12 12 6.5 M18.0 14.5 L20.5 15.5 M14.5 18.0 L15.5 20.5 M9.5 18.0 L8.5 20.5 M6.0 14.5 L3.5 15.5 "
                             "M6.0 9.5 L3.5 8.5 M9.5 6.0 L8.5 3.5 M14.5 6.0 L15.5 3.5 M18.0 9.5 L20.5 8.5";
    case UI_CUT: return "O 6.5 17.5 2.7 O 17.5 17.5 2.7 M8.4 15.4 L17 3.5 M15.6 15.4 L7 3.5";
    case UI_SELECT_ALL: return "M4 4 L7 4 M10.5 4 L13.5 4 M17 4 L20 4 L20 7 M20 10.5 L20 13.5 M20 17 L20 20 L17 20 M13.5 20 L10.5 20 M7 20 L4 20 L4 17 M4 13.5 L4 10.5 M4 7 L4 4 M8.5 8.5 L15.5 8.5 L15.5 15.5 L8.5 15.5 Z";
    case UI_LINK_TAB: return "M4 6 L20 6 L20 19 L4 19 Z M4 9.5 L20 9.5 M12 11.5 L12 17 M9.25 14.25 L14.75 14.25";
    case UI_COPY_TEXT: return "M10 14 L14 10 M8.5 11.5 L6.5 13.5 A 9 15 3.5 135 180 L12.5 17.5 M15.5 12.5 L17.5 10.5 A 15 9 3.5 -45 180 L11.5 6.5";
    case UI_SITE_INFO: return "O 12 12 8.5 M12 11 L12 16.2 O 12 7.9 0.35";
    case UI_SITE_CLEAR_ASK: case UI_SITE_CLEAR: return "M4.5 7 L19.5 7 M9.5 7 L9.5 4.5 L14.5 4.5 L14.5 7 M6.5 7 L7.5 20 L16.5 20 L17.5 7 M10.3 10.5 L10.3 16.5 M13.7 10.5 L13.7 16.5";
    case UI_EXIT: return "M10 4 L4 4 L4 20 L10 20 M9 12 L20 12 M16 8 L20 12 L16 16";
    default: return NULL;
    }
}
