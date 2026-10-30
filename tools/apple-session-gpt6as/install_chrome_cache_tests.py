#!/usr/bin/env python3
"""Compare retained chrome against fresh original paints across invalidations."""
from pathlib import Path
import hashlib
import json

ADDITION = r'''
#ifndef LUMEN_CHROME_PAINT_CACHE
/* Reference behavior before retention exists: every call repaints. This keeps
 * the pixel oracle executable in RED while the reuse assertion must fail. */
typedef struct { bool valid; } ChromePaintCache;
static bool paint_browser_chrome_cached(ChromePaintCache *cache,
        Canvas *chrome, const DisplayList *dl, Color clear,
        int page_x, int page_y, uint64_t display_hash, bool floating_overlay) {
    (void)cache; (void)display_hash; (void)floating_overlay;
    paint_browser_chrome(chrome, dl, clear, page_x, page_y);
    return true;
}
#endif

static void chrome_cache_cases(void) {
    enum { CAP = 113 * 89, VW = 79, VH = 63 };
    uint32_t expected[CAP], actual[CAP], expected_new[CAP], actual_new[CAP];
    seed_pixels(expected, CAP); memcpy(actual, expected, sizeof actual);
    seed_pixels(expected_new, CAP); memcpy(actual_new, expected_new, sizeof actual_new);
    Canvas reference = view(expected,101,9,7,VW,VH,1);
    Canvas canvas = view(actual,101,9,7,VW,VH,1);
    DItem items[5] = {0};
    items[0] = rect(0,0,130,110,RGBA(240,242,248,255));
    items[1] = rect(4.25f,2.5f,56.75f,19.25f,RGBA(33,87,214,181));
    for (int corner=0;corner<4;corner++) items[1].r[corner]=4;
    items[2] = (DItem){.op=DO_PUSH_LAYER,.alpha=.65f};
    items[3] = rect(3,9,31,56,RGBA(179,73,119,255));
    items[4] = (DItem){.op=DO_POP_LAYER};
    DisplayList dl={0};dl.items.v=items;dl.items.n=5;
    ChromePaintCache cache={0};
    Color clear=RGBA(255,255,255,255);
    int page_x=19,page_y=17;uint64_t hash=1;
    bool pixels_equal=true, invalidations=true, reuse=true;
    unsigned unchanged=0, invalidated=0;
    for (unsigned frame=0;frame<20;frame++) {
        bool overlay=frame==4 || frame==5;
        bool should_reuse=frame==1 || frame==3 || frame==7 || frame==15 || frame==19;
        if (frame==2) {items[1].color=RGBA(171,47,115,133);hash++;}
        if (frame==8) {
            /* Recreated backing may reuse its old address and dimensions. */
            seed_pixels(expected,CAP);memcpy(actual,expected,sizeof actual);
            cache.valid=false;
        }
        if (frame==9) reference.scale=canvas.scale=1.25f;
        if (frame==10) {reference.px++;canvas.px++;}
        if (frame==11) {reference.w=canvas.w=VW-3;reference.h=canvas.h=VH-2;}
        if (frame==12) page_x++;
        if (frame==13) page_y++;
        if (frame==14) clear=RGBA(210,220,230,190);
        if (frame==16) reference.stride=canvas.stride=103;
        if (frame==17) {
            reference.px=expected_new+9+7*101;
            canvas.px=actual_new+9+7*101;
        }
        if (frame==18) {items[0].color=0;hash++;}
        paint_browser_chrome(&reference,&dl,clear,page_x,page_y);
        bool painted=paint_browser_chrome_cached(&cache,&canvas,&dl,clear,page_x,page_y,hash,overlay);
        if (should_reuse) {reuse &= !painted;unchanged++;}
        else {invalidations &= painted;invalidated++;}
        /* The page changes each frame, including fully transparent pixels. */
        copy_page(&reference,page_x,page_y);copy_page(&canvas,page_x,page_y);
        int row=page_y+(int)frame%(reference.h-page_y);
        for (int x=page_x;x<reference.w;x++) {
            Color pixel=(x+(int)frame)%5 ? 0xff000000u | (frame*37073u+x*7919u) : 0;
            reference.px[(size_t)row*reference.stride+x]=pixel;
            canvas.px[(size_t)row*canvas.stride+x]=pixel;
        }
        if (overlay) {
            DItem floating=rect((page_x-4)/reference.scale,(page_y-3)/reference.scale,
                                29/reference.scale,24/reference.scale,RGBA(15,48,91,181));
            for(int corner=0;corner<4;corner++)floating.r[corner]=3;
            DisplayList floats={0};floats.items.v=&floating;floats.items.n=1;
            raster_rect(&reference,&floats,clear,page_x-4,page_y-3,page_x+25,page_y+21);
            raster_rect(&canvas,&floats,clear,page_x-4,page_y-3,page_x+25,page_y+21);
            cache.valid=false; /* Same invalidation as the actual app overlay. */
        }
        if (memcmp(expected,actual,sizeof actual) || memcmp(expected_new,actual_new,sizeof actual_new)) {
            pixels_equal=false;fprintf(stderr,"Chrome cache pixel mismatch frame=%u\n",frame);
        }
    }
    check(pixels_equal,"retained chrome exactly matches20 fresh full-buffer paints and safe-area canaries");
    check(invalidations && invalidated==15,"chrome invalidates for content, overlay, new backing, scale, geometry, stride and clear");
    check(reuse && unchanged==5,"unchanged browser chrome avoids five redundant raster passes while page pixels change");
}
'''


def prepare(source):
    anchor = 'int main(void) {\n'
    if source.count(anchor) != 1:
        raise RuntimeError('Render test main anchor changed')
    return source.replace(anchor, ADDITION + anchor + '    chrome_cache_cases();\n', 1)


if __name__ == '__main__':
    path = Path('tests/test_render_overdraw.c')
    before = path.read_text()
    after = prepare(before)
    path.write_text(after)
    print(json.dumps({'path':str(path), 'beforeSHA256':hashlib.sha256(before.encode()).hexdigest(),
                      'afterSHA256':hashlib.sha256(after.encode()).hexdigest()}))
