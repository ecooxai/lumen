#!/usr/bin/env python3
"""Retain exact browser-chrome pixels after rebuilding unchanged display lists."""
from pathlib import Path
import hashlib
import json


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise RuntimeError('Missing or ambiguous chrome-cache anchor: ' + before[:90])
    return source.replace(before, after, 1)


ADDITION = r'''
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
'''


def prepare_header(source):
    return replace_once(source, '\n#endif\n', ADDITION + '\n#endif\n')


def prepare_main(source):
    source = replace_once(source,
        '    Canvas frame, page; DisplayList pdl, cdl, hdl;\n',
        '    Canvas frame, page; DisplayList pdl, cdl, hdl; ChromePaintCache chrome_cache;\n')
    source = replace_once(source,
        'if (a->frame.w != a->pw || a->frame.h != a->ph) { canvas_free(&a->frame); canvas_init(&a->frame, a->pw, a->ph, a->scale); }',
        'if (a->frame.w != a->pw || a->frame.h != a->ph) { canvas_free(&a->frame); canvas_init(&a->frame, a->pw, a->ph, a->scale); a->chrome_cache.valid = false; }')
    source = replace_once(source,
        '        paint_browser_chrome(&chrome, &a->cdl, RGBA(255, 255, 255, 255), sidebar_px, toolbar_px);',
        '        paint_browser_chrome_cached(&a->chrome_cache, &chrome, &a->cdl, RGBA(255, 255, 255, 255), sidebar_px, toolbar_px, a->chash, a->hdl.items.n != 0);')
    source = replace_once(source,
        '    if (!gskip && a->hdl.items.n) { float s = chrome.scale;',
        '    if (!gskip && a->hdl.items.n) { a->chrome_cache.valid = false; float s = chrome.scale;')
    return source


if __name__ == '__main__':
    changes = []
    for name, prepare in [('src/app/chrome_paint.h', prepare_header), ('src/app/main.c', prepare_main)]:
        path = Path(name)
        before = path.read_text()
        after = prepare(before)
        changes.append((path, before, after))
    for path, before, after in changes:
        path.write_text(after)
        print(json.dumps({'path':str(path), 'beforeSHA256':hashlib.sha256(before.encode()).hexdigest(),
                          'afterSHA256':hashlib.sha256(after.encode()).hexdigest()}))
