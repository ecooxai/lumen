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

/* Pixels outside the page rectangle survive the page copy. Reuse them only
 * after the caller has rebuilt and hashed the current chrome display list.
 * A floating overlay can overwrite these pixels, so it invalidates reuse both
 * while visible and on the first frame after it disappears. */
#define LUMEN_CHROME_PAINT_CACHE 1
typedef struct {
    const uint32_t *pixels;
    uint64_t display_hash;
    Color clear;
    int width, height, stride, page_x, page_y;
    float scale;
    bool valid;
} ChromePaintCache;

static inline bool paint_browser_chrome_cached(ChromePaintCache *cache,
        Canvas *chrome, const DisplayList *dl, Color clear,
        int page_x, int page_y, uint64_t display_hash, bool floating_overlay) {
    page_x = LCLAMP(page_x, 0, chrome->w);
    page_y = LCLAMP(page_y, 0, chrome->h);
    bool same = cache->valid && !floating_overlay &&
        cache->pixels == chrome->px && cache->display_hash == display_hash &&
        cache->clear == clear && cache->width == chrome->w &&
        cache->height == chrome->h && cache->stride == chrome->stride &&
        cache->scale == chrome->scale && cache->page_x == page_x &&
        cache->page_y == page_y;
    if (!same) paint_browser_chrome(chrome, dl, clear, page_x, page_y);
    *cache = (ChromePaintCache){.pixels=chrome->px,.display_hash=display_hash,
        .clear=clear,.width=chrome->w,.height=chrome->h,.stride=chrome->stride,
        .page_x=page_x,.page_y=page_y,.scale=chrome->scale,
        .valid=!floating_overlay};
    return !same;
}

#endif
