#ifndef LUMEN_CHROME_PAINT_H
#define LUMEN_CHROME_PAINT_H

#include "../paint/paint.h"

/* The page copy replaces the bottom-right rectangle, including transparent
 * pixels. Only paint chrome outside that rectangle; floating UI is composited
 * after the page copy by the caller. Coordinates are local device pixels. */
static inline void paint_browser_chrome(Canvas *chrome, const DisplayList *dl,
                                       Color clear, int page_x, int page_y) {
    page_x = LCLAMP(page_x, 0, chrome->w);
    page_y = LCLAMP(page_y, 0, chrome->h);
    raster_rect(chrome, dl, clear, 0, 0, chrome->w, page_y);
    raster_rect(chrome, dl, clear, 0, page_y, page_x, chrome->h);
}

#endif
