#!/usr/bin/env python3
"""Prepare exact per-raster glyph/color caches; does not persist across frames."""
from pathlib import Path
import hashlib
import json


def replace_once(source,before,after):
    if source.count(before)!=1:raise RuntimeError('Missing or ambiguous glyph source anchor: '+before[:80])
    return source.replace(before,after,1)


def prepare(source):
    source=replace_once(source,'typedef struct {\n    Canvas *c; float s;\n','''typedef struct { Font *font; uint32_t gid; float scale; const Glyph *glyph; } RasterGlyph;
typedef struct {
    Canvas *c; float s;
''')
    source=replace_once(source,'    uint32_t *px;\n} R;\n','''    uint32_t *px;
    /* Frame-local caches retain no fonts/glyphs beyond this raster call. Their
       fixed storage avoids per-glyph allocation and never changes font keys. */
    RasterGlyph glyph_cache[64];
    uint32_t text_palette[256]; Color text_palette_color; bool text_palette_valid;
} R;
''')
    source=replace_once(source,'static void text(R *r, const DItem *it) {\n','''static const Glyph *raster_glyph(R *r, const DGlyph *glyph, float scale) {
    RasterGlyph *entry = &r->glyph_cache[glyph->gid & 63u];
    if (entry->font != glyph->font || entry->gid != glyph->gid || entry->scale != scale) {
        entry->glyph = font_glyph(glyph->font, glyph->gid, scale);
        entry->font = glyph->font; entry->gid = glyph->gid; entry->scale = scale;
    }
    return entry->glyph;
}
static const uint32_t *text_palette(R *r, Color color) {
    if (!r->text_palette_valid || r->text_palette_color != color) {
        uint32_t opacity = COLOR_A(color);
        for (unsigned coverage = 0; coverage < 256; coverage++) {
            uint32_t alpha = coverage * opacity / 255;
            /* Keep the scalar renderer's exact order, division and rounding. */
            r->text_palette[coverage] = premul(color | 0xff000000u, alpha / 255.f);
        }
        r->text_palette_color = color; r->text_palette_valid = true;
    }
    return r->text_palette;
}
static void text(R *r, const DItem *it) {
''')
    source=replace_once(source,'    uint32_t ca = COLOR_A(it->color);\n','    uint32_t ca = COLOR_A(it->color); const uint32_t *palette = NULL;\n')
    source=replace_once(source,'        const Glyph *gl = font_glyph(dg->font, dg->gid, gs);\n','        const Glyph *gl = raster_glyph(r, dg, gs);\n')
    source=replace_once(source,'''        if (ox >= c->x1 || oy >= c->y1 || ox + gl->w <= c->x0 || oy + gl->h <= c->y0) continue;
        for (int y = LMAX(0, c->y0 - oy); y < gl->h && oy + y < c->y1; y++) {
''','''        if (ox >= c->x1 || oy >= c->y1 || ox + gl->w <= c->x0 || oy + gl->h <= c->y0) continue;
        if (!c->round && gl->alpha && !(gl->color && gl->rgba)) {
            if (!palette) palette = text_palette(r, it->color);
            int x0 = LMAX(0, c->x0 - ox), x1 = LMIN(gl->w, c->x1 - ox);
            for (int y = LMAX(0, c->y0 - oy); y < gl->h && oy + y < c->y1; y++) {
                uint32_t *row = r->px + (size_t)(oy + y) * (size_t)r->c->stride;
                const uint8_t *alpha = gl->alpha + (size_t)y * (size_t)gl->w;
                for (int x = x0; x < x1; x++) {
                    uint32_t pixel = palette[alpha[x]];
                    if (pixel) over(&row[ox + x], pixel);
                }
            }
            continue;
        }
        for (int y = LMAX(0, c->y0 - oy); y < gl->h && oy + y < c->y1; y++) {
''')
    return source


if __name__=='__main__':
    path=Path('src/paint/raster.c');before=path.read_text();after=prepare(before)
    path.write_text(after)
    print(json.dumps({'path':str(path),'beforeSHA256':hashlib.sha256(before.encode()).hexdigest(),
                      'afterSHA256':hashlib.sha256(after.encode()).hexdigest(),
                      'scope':'Bounded frame-local glyph/color caches; exact scalar pixels, font key semantics and frame cadence preserved.'}))
