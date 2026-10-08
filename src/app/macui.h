#ifndef LUMEN_MACUI_H
#define LUMEN_MACUI_H
#include <stdbool.h>
#include <stddef.h>
#include <SDL3/SDL.h>

enum { MENU_WS_NEW = 1, MENU_WS_RENAME, MENU_WS_CLOSE, MENU_WS_NEXT, MENU_WS_PREV, MENU_WS_SIDEBAR };

/* Transparent, full-size title bar so the tab strip sits beside the traffic lights. */
void mac_style_window(SDL_Window *w);
/* Adds the Workspace menu; selections arrive as SDL user events of type `ev` with `user.code` = MENU_*. */
void mac_install_menu(Uint32 ev);
/* Modal text prompt; false if cancelled or empty. */
bool mac_prompt(const char *title, const char *init, char *out, size_t n);
#endif
