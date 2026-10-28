#!/usr/bin/env python3
"""Independent original-text oracle, cache invalidation and bounded lookup checks."""
from pathlib import Path
import hashlib
import json

ADDITION=r'''
static Font text_fonts[2];
static Glyph text_glyphs[5];
static uint8_t text_alpha[2][16 * 16];
static uint32_t text_rgba[16 * 16];
static unsigned glyph_lookups;
const Glyph *test_font_glyph(Font *font, uint32_t gid, float scale) {
    glyph_lookups++;
    if (gid == 0xffff) return NULL;
    if (gid == 0xfffe) return &text_glyphs[4];
    unsigned index = (gid ^ (gid >> 6)) + (font == &text_fonts[1] ? 1 : 0) + (scale > 1.5f ? 2 : 0);
    return &text_glyphs[index % 4];
}
/* Frozen original scalar text path. It intentionally performs every original
 * font lookup, opacity operation, clip calculation and integer rounding. */
static void reference_text(R *r, const DItem *it) {
    float s = r->s, gs = s * (it->ts > 0 ? it->ts : 1); const Clip *c = clip(r);
    uint32_t ca = COLOR_A(it->color);
    for (int i = 0; i < it->ng; i++) {
        const DGlyph *dg = &it->g[i];
        if (!dg->font) continue;
        const Glyph *gl = test_font_glyph(dg->font, dg->gid, gs);
        if (!gl || !gl->w) continue;
        int ox = (int)lroundf(dg->x * s) + gl->left, oy = (int)lroundf(dg->y * s) - gl->top;
        if (ox >= c->x1 || oy >= c->y1 || ox + gl->w <= c->x0 || oy + gl->h <= c->y0) continue;
        for (int y = LMAX(0, c->y0 - oy); y < gl->h && oy + y < c->y1; y++) {
            uint32_t *row = r->px + (size_t)(oy + y) * (size_t)r->c->stride;
            for (int x = LMAX(0, c->x0 - ox); x < gl->w && ox + x < c->x1; x++) {
                float cc = clip(r)->round ? clip_cov(r, ox + x + 0.5f, oy + y + 0.5f) : 1;
                if (gl->color && gl->rgba) { uint32_t p = gl->rgba[y * gl->w + x]; if (p >> 24) over(&row[ox + x], cc < 1 ? scale_px(p, (uint32_t)(cc * 256)) : p); }
                else if (gl->alpha) { uint32_t a = gl->alpha[y * gl->w + x]; if (a) { a = a * ca / 255; over(&row[ox + x], premul(it->color | 0xff000000u, a / 255.f * cc)); } }
            }
        }
    }
}
static void glyph_pixel_cases(void) {
    enum { W = 83, H = 69, VW = 61, VH = 47 };
    static const Color colors[] = {0x000000,0xffffff,0x0102fe,0x1747b3,0xff0080,0x80ff01,0x123456};
    uint32_t expected[W * H], actual[W * H];
    for (unsigned i = 0; i < 256; i++) {
        text_alpha[0][i] = i; text_alpha[1][i] = 255 - i;
        text_rgba[i] = premul(0xffe17935u, i / 255.f);
    }
    text_glyphs[0] = (Glyph){.w=16,.h=16,.left=-2,.top=11,.alpha=text_alpha[0]};
    text_glyphs[1] = (Glyph){.w=16,.h=16,.left=1,.top=9,.alpha=text_alpha[1]};
    text_glyphs[2] = (Glyph){.w=16,.h=16,.left=-1,.top=12,.color=true,.rgba=text_rgba};
    text_glyphs[3] = (Glyph){.w=16,.h=16,.left=0,.top=10,.color=true,.alpha=text_alpha[0]};
    Canvas reference = view(expected,W,7,11,VW,VH,1), canvas = view(actual,W,7,11,VW,VH,1);
    R ref = {.c=&reference,.s=1,.nclip=1,.px=reference.px};
    R got = {.c=&canvas,.s=1,.nclip=1,.px=canvas.px};
    bool equal = true;unsigned completed = 0;
    for (unsigned i = 0; i < 256 * 7 * 4; i++) {
        seed_pixels(expected,W*H);memcpy(actual,expected,sizeof actual);
        ref.s = got.s = 1 + ((i / (256 * 7)) % 4) * .25f;
        Clip clipping = {.x0=i%5,.y0=(i/5)%5,.x1=VW-((i/25)%5),.y1=VH-((i/125)%5),
                         .fx=.17f,.fy=.25f,.fw=VW-.34f,.fh=VH-.5f,.r={3,5,7,9},.round=i%11==0};
        ref.clips[0] = got.clips[0] = clipping;
        DGlyph glyphs[] = {
            {.gid=0,.x=-2.25f,.y=7.5f,.font=&text_fonts[i%2]},
            {.gid=1,.x=8.5f,.y=18.25f,.font=&text_fonts[(i+1)%2]},
            {.gid=65,.x=24.17f,.y=13.75f,.font=&text_fonts[i%2]},
            {.gid=2,.x=37.25f,.y=38.25f,.font=&text_fonts[(i+1)%2]},
            {.gid=3,.x=18.25f,.y=45.5f,.font=&text_fonts[i%2]},
            {.gid=0xffff,.x=12,.y=24,.font=&text_fonts[0]},
            {.gid=0xfffe,.x=12,.y=24,.font=&text_fonts[0]},
            {.gid=1,.x=12,.y=24,.font=NULL}
        };
        DItem item = {.op=DO_TEXT,.color=colors[(i/256)%7] | ((i%256)<<24),.g=glyphs,.ng=8,.ts=i%3==0?1.5f:1};
        reference_text(&ref,&item);text(&got,&item);completed++;
        if (memcmp(expected,actual,sizeof actual)) {
            equal=false;fprintf(stderr,"Glyph cache pixel mismatch case=%u color=%08x scale=%.9g round=%d\n",i,item.color,got.s,clipping.round);break;
        }
    }
    check(equal && completed==256*7*4,"7168 text/color/opacity/font/scale/rounded-clip cases match original scalar pixels");
    /* Cache repeated glyphs only within this single raster invocation. All
       colors and positions still pass through the real text path. */
    seed_pixels(expected,W*H);memcpy(actual,expected,sizeof actual);
    memset(&ref,0,sizeof ref);ref.c=&reference;ref.s=1;ref.nclip=1;ref.px=reference.px;
    memset(&got,0,sizeof got);got.c=&canvas;got.s=1;got.nclip=1;got.px=canvas.px;
    ref.clips[0]=got.clips[0]=(Clip){.x1=VW,.y1=VH};
    DGlyph repeated[100];
    for (unsigned i=0;i<100;i++) repeated[i]=(DGlyph){.gid=1,.x=4.25f,.y=18.5f,.font=&text_fonts[0]};
    DItem repeated_item={.op=DO_TEXT,.color=0xa53179bfu,.g=repeated,.ng=100};
    glyph_lookups=0;reference_text(&ref,&repeated_item);unsigned reference_lookups=glyph_lookups;
    glyph_lookups=0;text(&got,&repeated_item);unsigned actual_lookups=glyph_lookups;
    check(!memcmp(expected,actual,sizeof actual),"repeated translucent glyphs preserve exact accumulated pixels");
    check(reference_lookups==100 && actual_lookups==1,"one raster call reuses an exact repeated font/glyph/scale lookup");
    R fresh={.c=&canvas,.s=1,.nclip=1,.px=canvas.px};fresh.clips[0]=got.clips[0];
    glyph_lookups=0;text(&fresh,&repeated_item);
    check(glyph_lookups==1,"a fresh raster invocation never inherits a previous frame glyph pointer");
}
'''


def prepare(source):
    before='#include "../src/paint/raster.c"\n'
    after='#define font_glyph test_font_glyph\n#include "../src/paint/raster.c"\n#undef font_glyph\n'
    if source.count(before)!=1:raise RuntimeError('Raster inclusion anchor changed')
    source=source.replace(before,after,1)
    anchor='int main(void) {\n'
    if source.count(anchor)!=1:raise RuntimeError('Render test main anchor changed')
    return source.replace(anchor,ADDITION+anchor+'    glyph_pixel_cases();\n',1)


if __name__=='__main__':
    path=Path('tests/test_render_overdraw.c');before=path.read_text();after=prepare(before)
    path.write_text(after)
    print(json.dumps({'path':str(path),'beforeSHA256':hashlib.sha256(before.encode()).hexdigest(),
                      'afterSHA256':hashlib.sha256(after.encode()).hexdigest(),'scope':'Original scalar text oracle and per-raster cache invalidation/reuse regression.'}))
