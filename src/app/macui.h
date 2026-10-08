#ifndef LUMEN_MACUI_H
#define LUMEN_MACUI_H
#include <stdbool.h>
#include <stddef.h>
#include <SDL3/SDL.h>

enum { MENU_WS_NEW = 1, MENU_WS_RENAME, MENU_WS_CLOSE, MENU_WS_NEXT, MENU_WS_PREV, MENU_WS_SIDEBAR, MENU_WS_REFRESH };
#define MENU_WS_ICON 1000   /* + icon index; MENU_WS_ICON + MAC_NICONS = letters */
#define MAC_NICONS 16

/* Transparent, full-size title bar so the tab strip sits beside the traffic lights. */
void mac_style_window(SDL_Window *w);
/* Adds the Workspace menu; selections arrive as SDL user events of type `ev` with `user.code` = MENU_*. */
void mac_install_menu(Uint32 ev);
/* Modal text prompt; false if cancelled or empty. */
bool mac_prompt(const char *title, const char *init, char *out, size_t n);
/* Workspace context menu at window point (x, y); returns MENU_* / MENU_WS_ICON + i, or 0. */
int mac_ws_menu(SDL_Window *w, float x, float y);
/* Renders workspace icon `idx` tinted `rgb` into px*px premultiplied ARGB. */
bool mac_icon_rgba(int idx, int px, uint32_t rgb, uint32_t *out);
/* Renders SF Symbol `name` tinted `rgb` into px*px premultiplied ARGB. */
bool mac_symbol_rgba(const char *name, int px, uint32_t rgb, uint32_t *out);
enum { MENU_TAB_CPU_DEFAULT = 2000, MENU_TAB_CPU_NEVER, MENU_TAB_VCTL, MENU_TAB_LITE };
#define MENU_TAB_CPU_LIM 2100   /* + percent */
/* Per-tab menu (CPU limit mode 0 default / 1 never / 2 fixed `lim`%, video controls); returns MENU_TAB_* or 0. */
int mac_tab_menu(SDL_Window *w, float x, float y, int mode, int lim, bool vctl, bool lite, const char *deflabel);
#endif
