#ifndef LUMEN_UICMD_H
#define LUMEN_UICMD_H
/* Menu command ids shared by the platform menus (macui.m) and the in-window menus */
enum { MENU_WS_NEW = 1, MENU_WS_RENAME, MENU_WS_CLOSE, MENU_WS_NEXT, MENU_WS_PREV, MENU_WS_SIDEBAR, MENU_WS_REFRESH };
#define MENU_WS_ICON 1000   /* + icon index; MENU_WS_ICON + MAC_NICONS = letters */
#define MAC_NICONS 16
enum { MENU_TAB_CPU_DEFAULT = 2000, MENU_TAB_CPU_NEVER, MENU_TAB_VCTL, MENU_TAB_LITE };
#define MENU_TAB_CPU_LIM 2100   /* + percent */
#endif
