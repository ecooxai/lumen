#ifndef LUMEN_FONT_H
#define LUMEN_FONT_H
#include "../base/util.h"

typedef struct Face Face;      /* one font file+index (FT_Face + hb_face) */
typedef struct Font {          /* a face at a pixel size */
    Face *face;
    float size;                /* css px */
    float ascent, descent, line_gap, x_height, underline_pos, underline_thick;
    float space_adv;
    bool synth_bold, synth_italic;
    void *hb;                  /* hb_font_t* */
    HMap glyphs;               /* gid -> Glyph* */
} Font;

typedef struct Glyph { int w, h, left, top; uint8_t *alpha; bool color; uint32_t *rgba; float adv; } Glyph;

typedef struct GlyphPos { uint32_t gid; float x, y, adv; uint32_t cluster; Font *font; } GlyphPos;
typedef struct ShapedRun { GlyphPos *g; int n; float width; } ShapedRun;

void font_init(void);
/* family: CSS font-family list; weight 100..900 */
Font *font_get(const char *family, int weight, bool italic, float size);
void text_shape(Font *f, const char *s, size_t len, float letter_spacing, ShapedRun *out);
void shaped_free(ShapedRun *r);
float text_width(Font *f, const char *s, size_t len, float letter_spacing);
/* @font-face: declare a face (url must be absolute), drain fetches requested by use, hand back the bytes */
void font_declare(const char *family, int weight, bool italic, const char *url);
char *font_next_request(void);
bool font_loaded(const char *url, const void *data, size_t len);
/* rasterize a glyph at scale (device pixel ratio) */
const Glyph *font_glyph(Font *f, uint32_t gid, float scale);
#endif
