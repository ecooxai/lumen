/* Font discovery, shaping (HarfBuzz) and glyph rasterization (FreeType) */
#include "font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_SYNTHESIS_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include <math.h>
#include <pthread.h>
#include <sys/stat.h>

struct Face { FT_Face ft; hb_face_t *hb; char *path; int index; int weight; bool italic; bool color; };

static FT_Library ftlib;
static pthread_mutex_t font_mu = PTHREAD_MUTEX_INITIALIZER;
static HMap faces;   /* "path#index" -> Face* */
static HMap fonts;   /* "path#index@size/flags" -> Font* */
static HMap resolve_cache; /* "family|w|i" -> Face* */

typedef struct { const char *name; const char *regular, *bold, *italic, *bolditalic; } FamilyDef;
#define SUP "/System/Library/Fonts/Supplemental/"
#define SYS "/System/Library/Fonts/"
#ifdef __APPLE__
static const FamilyDef families[] = {
    { "sans-serif", SYS "Helvetica.ttc#0", SYS "Helvetica.ttc#1", SYS "Helvetica.ttc#2", SYS "Helvetica.ttc#3" },
    { "helvetica", SYS "Helvetica.ttc#0", SYS "Helvetica.ttc#1", SYS "Helvetica.ttc#2", SYS "Helvetica.ttc#3" },
    { "arial", SUP "Arial.ttf", SUP "Arial Bold.ttf", SUP "Arial Italic.ttf", SUP "Arial Bold Italic.ttf" },
    { "helvetica neue", SYS "HelveticaNeue.ttc#0", SYS "HelveticaNeue.ttc#1", SYS "HelveticaNeue.ttc#2", SYS "HelveticaNeue.ttc#3" },
    { "system-ui", SYS "SFNS.ttf", SYS "SFNS.ttf", SYS "SFNSItalic.ttf", SYS "SFNSItalic.ttf" },
    { "-apple-system", SYS "SFNS.ttf", SYS "SFNS.ttf", SYS "SFNSItalic.ttf", SYS "SFNSItalic.ttf" },
    { "blinkmacsystemfont", SYS "SFNS.ttf", SYS "SFNS.ttf", SYS "SFNSItalic.ttf", SYS "SFNSItalic.ttf" },
    { "ui-sans-serif", SYS "SFNS.ttf", SYS "SFNS.ttf", SYS "SFNSItalic.ttf", SYS "SFNSItalic.ttf" },
    { "serif", SUP "Times New Roman.ttf", SUP "Times New Roman Bold.ttf", SUP "Times New Roman Italic.ttf", SUP "Times New Roman Bold Italic.ttf" },
    { "times new roman", SUP "Times New Roman.ttf", SUP "Times New Roman Bold.ttf", SUP "Times New Roman Italic.ttf", SUP "Times New Roman Bold Italic.ttf" },
    { "times", SYS "Times.ttc#0", SYS "Times.ttc#1", SYS "Times.ttc#2", SYS "Times.ttc#3" },
    { "georgia", SUP "Georgia.ttf", SUP "Georgia Bold.ttf", SUP "Georgia Italic.ttf", SUP "Georgia Bold Italic.ttf" },
    { "monospace", SYS "Menlo.ttc#0", SYS "Menlo.ttc#1", SYS "Menlo.ttc#2", SYS "Menlo.ttc#3" },
    { "menlo", SYS "Menlo.ttc#0", SYS "Menlo.ttc#1", SYS "Menlo.ttc#2", SYS "Menlo.ttc#3" },
    { "ui-monospace", SYS "SFNSMono.ttf", SYS "SFNSMono.ttf", SYS "SFNSMonoItalic.ttf", SYS "SFNSMonoItalic.ttf" },
    { "sfmono-regular", SYS "SFNSMono.ttf", SYS "SFNSMono.ttf", SYS "SFNSMonoItalic.ttf", SYS "SFNSMonoItalic.ttf" },
    { "consolas", SYS "Menlo.ttc#0", SYS "Menlo.ttc#1", SYS "Menlo.ttc#2", SYS "Menlo.ttc#3" },
    { "courier new", SUP "Courier New.ttf", SUP "Courier New Bold.ttf", SUP "Courier New Italic.ttf", SUP "Courier New Bold Italic.ttf" },
    { "courier", SYS "Courier.ttc#0", SYS "Courier.ttc#1", SYS "Courier.ttc#2", SYS "Courier.ttc#3" },
    { "monaco", SYS "Monaco.ttf", NULL, NULL, NULL },
    { "verdana", SUP "Verdana.ttf", SUP "Verdana Bold.ttf", SUP "Verdana Italic.ttf", SUP "Verdana Bold Italic.ttf" },
    { "tahoma", SUP "Tahoma.ttf", SUP "Tahoma Bold.ttf", NULL, NULL },
    { "trebuchet ms", SUP "Trebuchet MS.ttf", SUP "Trebuchet MS Bold.ttf", SUP "Trebuchet MS Italic.ttf", SUP "Trebuchet MS Bold Italic.ttf" },
    { "avenir", SYS "Avenir.ttc#0", SYS "Avenir.ttc#4", SYS "Avenir.ttc#1", SYS "Avenir.ttc#5" },
    { "lucida grande", SYS "LucidaGrande.ttc#0", SYS "LucidaGrande.ttc#1", NULL, NULL },
    { "emoji", SYS "Apple Color Emoji.ttc#0", NULL, NULL, NULL },
};
static const char *fallbacks[] = { SYS "Helvetica.ttc#0", SUP "Arial Unicode.ttf", SYS "Apple Color Emoji.ttc#0", SYS "Hiragino Sans GB.ttc#0", SYS "AppleSDGothicNeo.ttc#0", SYS "Apple Symbols.ttf", SYS "LastResort.otf", NULL };
#else
static const FamilyDef families[] = {
    { "sans-serif", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf" },
    { "arial", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf" },
    { "helvetica", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf" },
    { "serif", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", NULL, NULL },
    { "times new roman", "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf", "/usr/share/fonts/truetype/liberation/LiberationSerif-BoldItalic.ttf" },
    { "monospace", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", NULL, NULL },
    { "system-ui", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", NULL, NULL },
    { "emoji", "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", NULL, NULL, NULL },
};
static const char *fallbacks[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc#0", "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", NULL };
#endif

void font_init(void) { if (!ftlib) FT_Init_FreeType(&ftlib); }

static Face *face_load(const char *spec) {
    Face *f = hm_get(&faces, spec);
    if (f) return f == (Face *)1 ? NULL : f;
    char path[512]; int idx = 0;
    snprintf(path, sizeof path, "%s", spec);
    char *h = strrchr(path, '#'); if (h) { *h = 0; idx = atoi(h + 1); }
    FT_Face ft;
    if (FT_New_Face(ftlib, path, idx, &ft)) { hm_put(&faces, spec, (Face *)1); return NULL; }
    f = xcalloc(1, sizeof *f);
    f->ft = ft; f->path = xstrdup(path); f->index = idx;
    f->hb = hb_ft_face_create_referenced(ft);
    f->color = FT_HAS_COLOR(ft);
    f->weight = (ft->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400;
    f->italic = ft->style_flags & FT_STYLE_FLAG_ITALIC;
    hm_put(&faces, spec, f);
    return f;
}

static Face *resolve_one(const char *name, int weight, bool italic, bool *synth_b, bool *synth_i) {
    char lname[128]; size_t n = 0;
    for (const char *p = name; *p && n < sizeof lname - 1; p++) lname[n++] = (char)lc((unsigned char)*p);
    lname[n] = 0;
    for (size_t i = 0; i < ARRLEN(families); i++) {
        if (strcmp(families[i].name, lname)) continue;
        const FamilyDef *d = &families[i];
        bool bold = weight >= 600;
        const char *pick = bold && italic ? d->bolditalic : bold ? d->bold : italic ? d->italic : d->regular;
        if (!pick) { pick = bold && d->bold ? d->bold : italic && d->italic ? d->italic : d->regular; *synth_b = bold && pick != d->bold && pick != d->bolditalic; *synth_i = italic && pick != d->italic && pick != d->bolditalic; }
        Face *f = face_load(pick);
        if (f) return f;
    }
    /* try a matching file in the supplemental dir: "<Name>.ttf" */
#ifdef __APPLE__
    char path[512];
    const char *suffix = weight >= 600 && italic ? " Bold Italic" : weight >= 600 ? " Bold" : italic ? " Italic" : "";
    snprintf(path, sizeof path, SUP "%s%s.ttf", name, suffix);
    struct stat sb;
    if (!stat(path, &sb)) return face_load(path);
    snprintf(path, sizeof path, SUP "%s.ttf", name);
    if (!stat(path, &sb)) { *synth_b = weight >= 600; *synth_i = italic; return face_load(path); }
#endif
    return NULL;
}

static void font_metrics(Font *f) {
    FT_Face ft = f->face->ft;
    float upem = ft->units_per_EM ? ft->units_per_EM : 1000;
    float k = f->size / upem;
    TT_OS2 *os2 = FT_Get_Sfnt_Table(ft, FT_SFNT_OS2);
    TT_HoriHeader *hh = FT_Get_Sfnt_Table(ft, FT_SFNT_HHEA);
    if (hh && (hh->Ascender || hh->Descender)) { f->ascent = hh->Ascender * k; f->descent = -hh->Descender * k; f->line_gap = hh->Line_Gap * k; }
    else if (os2 && os2->version != 0xFFFF) { f->ascent = os2->sTypoAscender * k; f->descent = -os2->sTypoDescender * k; f->line_gap = os2->sTypoLineGap * k; }
    else { f->ascent = ft->ascender * k; f->descent = -ft->descender * k; f->line_gap = (ft->height - ft->ascender + ft->descender) * k; }
    if (f->line_gap < 0) f->line_gap = 0;
    f->x_height = os2 && os2->version >= 2 && os2->sxHeight ? os2->sxHeight * k : f->ascent * 0.5f;
    f->underline_pos = ft->underline_position ? -ft->underline_position * k : f->size * 0.1f;
    f->underline_thick = ft->underline_thickness ? LMAX(1, ft->underline_thickness * k) : 1;
    hb_codepoint_t sp; f->space_adv = f->size * 0.25f;
    if (hb_font_get_nominal_glyph(f->hb, ' ', &sp)) f->space_adv = hb_font_get_glyph_h_advance(f->hb, sp) / 64.f;
}

static Font *font_for_face(Face *face, float size, bool sb, bool si) {
    char key[600]; snprintf(key, sizeof key, "%s#%d@%.2f/%d%d", face->path, face->index, size, sb, si);
    Font *f = hm_get(&fonts, key);
    if (f) return f;
    f = xcalloc(1, sizeof *f);
    f->face = face; f->size = size; f->synth_bold = sb; f->synth_italic = si;
    hb_font_t *hb = hb_font_create(face->hb);
    int sc = (int)lroundf(size * 64);
    hb_font_set_scale(hb, sc, sc);
    hb_font_set_ptem(hb, size * 0.75f);
    /* variable system font: set weight axis */
    if (hb_ot_var_has_data(face->hb)) { hb_variation_t v = { HB_TAG('w', 'g', 'h', 't'), 400 }; hb_font_set_variations(hb, &v, 1); }
    f->hb = hb;
    font_metrics(f);
    hm_put(&fonts, key, f);
    return f;
}

Font *font_get(const char *family, int weight, bool italic, float size) {
    if (size <= 0) size = 0.01f;
    if (!family || !*family) family = "sans-serif";
    char ck[512]; snprintf(ck, sizeof ck, "%s|%d|%d", family, weight >= 600 ? 700 : 400, italic);
    pthread_mutex_lock(&font_mu);
    Face *face = hm_get(&resolve_cache, ck); bool sb = false, si = false;
    uintptr_t flags = 0;
    if (!face) {
        const char *p = family;
        while (*p && !face) {
            while (*p == ' ' || *p == ',') p++;
            char name[128]; size_t n = 0; char q = 0;
            if (*p == '"' || *p == '\'') q = *p++;
            while (*p && (q ? *p != q : *p != ',') && n < sizeof name - 1) name[n++] = *p++;
            if (q && *p == q) p++;
            while (*p && *p != ',') p++;
            while (n && name[n - 1] == ' ') n--;
            name[n] = 0;
            if (n) face = resolve_one(name, weight, italic, &sb, &si);
        }
        if (!face) face = resolve_one("sans-serif", weight, italic, &sb, &si);
        if (!face) face = face_load(fallbacks[0]);
        if (!face) { pthread_mutex_unlock(&font_mu); return NULL; }
        flags = (uintptr_t)sb | ((uintptr_t)si << 1);
        Face **slot = xmalloc(sizeof(Face *) * 2); slot[0] = face; slot[1] = (Face *)flags;
        hm_put(&resolve_cache, ck, slot);
    } else { Face **slot = (Face **)face; face = slot[0]; flags = (uintptr_t)slot[1]; }
    Font *f = font_for_face(face, size, flags & 1, flags & 2);
    if (face->ft->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS && hb_ot_var_has_data(face->hb)) {
        /* per-weight instance for variable fonts */
        if (weight != 400) {
            char key[600]; snprintf(key, sizeof key, "%s#%d@%.2f/w%d", face->path, face->index, size, weight);
            Font *vf = hm_get(&fonts, key);
            if (!vf) {
                vf = xcalloc(1, sizeof *vf); *vf = *f; memset(&vf->glyphs, 0, sizeof vf->glyphs);
                hb_font_t *hb = hb_font_create(face->hb);
                int sc = (int)lroundf(size * 64); hb_font_set_scale(hb, sc, sc);
                hb_variation_t v = { HB_TAG('w', 'g', 'h', 't'), (float)weight }; hb_font_set_variations(hb, &v, 1);
                vf->hb = hb; vf->synth_bold = false;
                hm_put(&fonts, key, vf);
            }
            f = vf;
        }
    }
    pthread_mutex_unlock(&font_mu);
    return f;
}

static Font *fallback_for(Font *base, uint32_t cp) {
    for (int i = 0; fallbacks[i]; i++) {
        Face *fc = face_load(fallbacks[i]);
        if (!fc || fc == base->face) continue;
        if (FT_Get_Char_Index(fc->ft, cp)) return font_for_face(fc, base->size, base->synth_bold, base->synth_italic);
    }
    return NULL;
}

static void shape_segment(Font *f, const char *s, size_t off, size_t len, float ls, VEC(GlyphPos) *out, float *x) {
    hb_buffer_t *buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, s, (int)(off + len), (unsigned)off, (int)len);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(f->hb, buf, NULL, 0);
    unsigned n; hb_glyph_info_t *gi = hb_buffer_get_glyph_infos(buf, &n); hb_glyph_position_t *gp = hb_buffer_get_glyph_positions(buf, &n);
    bool rtl = hb_buffer_get_direction(buf) == HB_DIRECTION_RTL;
    (void)rtl;
    for (unsigned i = 0; i < n; i++) {
        GlyphPos g = { gi[i].codepoint, *x + gp[i].x_offset / 64.f, -gp[i].y_offset / 64.f, gp[i].x_advance / 64.f + ls, gi[i].cluster, f };
        if (f->synth_bold) g.adv += f->size / 32;
        vec_push(*out, g);
        *x += g.adv;
    }
    hb_buffer_destroy(buf);
}

void text_shape(Font *f, const char *s, size_t len, float ls, ShapedRun *out) {
    VEC(GlyphPos) g = {0};
    float x = 0;
    if (!f) { out->g = NULL; out->n = 0; out->width = 0; return; }
    pthread_mutex_lock(&font_mu);
    /* split into runs by font coverage */
    size_t i = 0, start = 0; Font *cur = f;
    while (i < len) {
        uint32_t cp; size_t j = i + (size_t)utf8_decode(s + i, len - i, &cp);
        Font *want = f;
        if (cp >= 0x20 && cp != 0xA0 && cp != 0x200B && cp != 0x200D && cp != 0xFE0F && !(cp >= 0xFE00 && cp <= 0xFE0F) && !FT_Get_Char_Index(f->face->ft, cp)) {
            /* keep combining marks / ZWJ sequences with the previous run */
            Font *fb = fallback_for(f, cp); if (fb) want = fb;
        } else if (cur != f && (cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF))) want = cur;
        if (want != cur) { if (i > start) shape_segment(cur, s, start, i - start, ls, (void *)&g, &x); start = i; cur = want; }
        i = j;
    }
    if (len > start) shape_segment(cur, s, start, len - start, ls, (void *)&g, &x);
    pthread_mutex_unlock(&font_mu);
    out->g = g.v; out->n = g.n; out->width = x;
}
void shaped_free(ShapedRun *r) { free(r->g); r->g = NULL; r->n = 0; }

float text_width(Font *f, const char *s, size_t len, float ls) {
    ShapedRun r; text_shape(f, s, len, ls, &r); float w = r.width; shaped_free(&r); return w;
}

const Glyph *font_glyph(Font *f, uint32_t gid, float scale) {
    char key[32]; snprintf(key, sizeof key, "%u@%.2f", gid, scale);
    pthread_mutex_lock(&font_mu);
    Glyph *g = hm_get(&f->glyphs, key);
    if (g) { pthread_mutex_unlock(&font_mu); return g; }
    g = xcalloc(1, sizeof *g);
    FT_Face ft = f->face->ft;
    float px = f->size * scale;
    if (f->face->color && ft->num_fixed_sizes > 0) {
        int best = 0; for (int i = 0; i < ft->num_fixed_sizes; i++) if (abs(ft->available_sizes[i].height - (int)px) < abs(ft->available_sizes[best].height - (int)px)) best = i;
        FT_Select_Size(ft, best);
    } else FT_Set_Char_Size(ft, 0, (FT_F26Dot6)lroundf(px * 64), 72, 72);
    if (hb_ot_var_has_data(f->face->hb)) {
        unsigned nc = 0; const int *coords = hb_font_get_var_coords_normalized(f->hb, &nc);
        if (nc) { FT_Fixed fc[16]; for (unsigned i = 0; i < nc && i < 16; i++) fc[i] = (FT_Fixed)coords[i] * 4; FT_Set_Var_Blend_Coordinates(ft, nc < 16 ? nc : 16, fc); }
        else FT_Set_Var_Blend_Coordinates(ft, 0, NULL);
    }
    FT_Int32 lf = FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT | (f->face->color ? FT_LOAD_COLOR : 0);
    if (!FT_Load_Glyph(ft, gid, lf)) {
        if (f->synth_bold) FT_GlyphSlot_Embolden(ft->glyph);
        if (f->synth_italic && ft->glyph->format == FT_GLYPH_FORMAT_OUTLINE) { FT_Matrix m = { 0x10000, 0x0366A, 0, 0x10000 }; FT_Outline_Transform(&ft->glyph->outline, &m); }
        if (!FT_Render_Glyph(ft->glyph, FT_RENDER_MODE_LIGHT)) {
            FT_Bitmap *bm = &ft->glyph->bitmap;
            g->w = (int)bm->width; g->h = (int)bm->rows; g->left = ft->glyph->bitmap_left; g->top = ft->glyph->bitmap_top;
            g->adv = ft->glyph->advance.x / 64.f;
            if (bm->pixel_mode == FT_PIXEL_MODE_BGRA) {
                /* scale bitmap emoji to requested size */
                float k = px / (float)(ft->size->metrics.y_ppem ? ft->size->metrics.y_ppem : px);
                int W = LMAX(1, (int)lroundf(g->w * k)), H = LMAX(1, (int)lroundf(g->h * k));
                g->rgba = xmalloc((size_t)W * H * 4); g->color = true;
                for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                    int sx = LMIN(g->w - 1, (int)(x / k)), sy = LMIN(g->h - 1, (int)(y / k));
                    uint8_t *p = bm->buffer + sy * bm->pitch + sx * 4; /* BGRA premultiplied */
                    g->rgba[y * W + x] = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
                }
                g->left = (int)lroundf(g->left * k); g->top = (int)lroundf(g->top * k); g->w = W; g->h = H;
            } else if (g->w && g->h) {
                g->alpha = xmalloc((size_t)g->w * g->h);
                for (int y = 0; y < g->h; y++) {
                    uint8_t *row = bm->buffer + y * bm->pitch;
                    if (bm->pixel_mode == FT_PIXEL_MODE_MONO) for (int x = 0; x < g->w; x++) g->alpha[y * g->w + x] = (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
                    else memcpy(g->alpha + y * g->w, row, (size_t)g->w);
                }
            }
        }
    }
    hm_put(&f->glyphs, key, g);
    pthread_mutex_unlock(&font_mu);
    return g;
}
