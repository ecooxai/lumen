#ifndef LUMEN_MACUI_H
#define LUMEN_MACUI_H
#include <stdbool.h>
#include <stddef.h>
#include <SDL3/SDL.h>

#include "uicmd.h"

/* Transparent, full-size title bar so the tab strip sits beside the traffic lights. */
void mac_style_window(SDL_Window *w);
#ifdef LUMEN_IOS
#include "../base/device.h"
/* Main-thread UIKit refinement of the cached SDL display/input information. */
void ios_device_info(SDL_Window *w, DeviceInfo *out);
/* Opt-in view/layer diagnostics, recorded only in an explicit test profile. */
void ios_debug_geometry(SDL_Window *w, void *metal_view, const char *stage);
#endif
/* Adds the Workspace menu; selections arrive as SDL user events of type `ev` with `user.code` = MENU_*. */
void mac_install_menu(Uint32 ev);
/* Modal text prompt; false if cancelled or empty. */
bool mac_prompt(const char *title, const char *init, char *out, size_t n);
/* Workspace context menu at full-window point (x, y), including safe-area offsets;
   returns MENU_* / MENU_WS_ICON + i, or 0. */
int mac_ws_menu(SDL_Window *w, float x, float y);
/* Renders workspace icon `idx` tinted `rgb` into px*px premultiplied ARGB. */
bool mac_icon_rgba(int idx, int px, uint32_t rgb, uint32_t *out);
/* Renders SF Symbol `name` tinted `rgb` into px*px premultiplied ARGB. */
bool mac_symbol_rgba(const char *name, int px, uint32_t rgb, uint32_t *out);
/* Per-tab menu (CPU limit mode 0 default / 1 never / 2 fixed `lim`%, video controls); returns MENU_TAB_* or 0. */
int mac_tab_menu(SDL_Window *w, float x, float y, int mode, int lim, bool vctl, bool lite, const char *deflabel);
#endif
