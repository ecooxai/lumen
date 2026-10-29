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
#ifdef __ANDROID__
#include "android_fonts.h"
#include "android_font_raster.h"
#endif
#if !defined(__APPLE__) && !defined(__ANDROID__)
#include <fontconfig/fontconfig.h>
#endif

struct Face { FT_Face ft; hb_face_t *hb; char *path; int index; int weight; bool italic; bool color;
#ifdef __ANDROID__
    bool android_system;
    unsigned android_axis_count;
    hb_variation_t android_axes[ANDROID_FONT_MAX_AXES];
#endif
};

static FT_Library ftlib;
static pthread_mutex_t font_mu = PTHREAD_MUTEX_INITIALIZER;
static HMap faces;   /* "path#index" -> Face* */
static HMap fonts;   /* "path#index@size/flags" -> Font* */
/* per-thread memo in front of font_get: layout asks for the same few fonts for every text box */
typedef struct { unsigned gen; uint32_t h; int w; bool it; float size; Font *f; char *fam; size_t fl; } FontMemo;
static unsigned font_gen = 1;
static _Thread_local FontMemo font_memo[256];
static HMap resolve_cache; /* "family|w|i" -> Face* */

/* @font-face declarations: fetched on first use, then swapped in (font-display: swap) */
typedef struct WebDecl { char *family, *url; int weight, state; bool italic; Face *face; struct WebDecl *next; } WebDecl;
static WebDecl *webdecls;
static VEC(char *) webreq;

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
#elif defined(__ANDROID__)
static const FamilyDef families[] = {
    { "sans-serif", "/system/fonts/Roboto-Regular.ttf", "/system/fonts/Roboto-Bold.ttf", "/system/fonts/Roboto-Italic.ttf", "/system/fonts/Roboto-BoldItalic.ttf" },
    { "system-ui", "/system/fonts/Roboto-Regular.ttf", "/system/fonts/Roboto-Bold.ttf", NULL, NULL },
    { "serif", "/system/fonts/NotoSerif-Regular.ttf", "/system/fonts/NotoSerif-Bold.ttf", "/system/fonts/NotoSerif-Italic.ttf", "/system/fonts/NotoSerif-BoldItalic.ttf" },
    { "monospace", "/system/fonts/DroidSansMono.ttf", NULL, NULL, NULL },
    { "emoji", "/system/fonts/NotoColorEmoji.ttf", NULL, NULL, NULL }
};
static const char *fallbacks[] = { "/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf", "/system/fonts/NotoSansCJK-Regular.ttc#0", "/system/fonts/NotoColorEmoji.ttf", NULL };
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

#ifdef __ANDROID__
static Face *android_system_face(const char *name, int weight, bool italic, uint32_t codepoint) {
    AndroidFontMatch selected;
    if (!android_font_match(name, weight, italic, codepoint, &selected)) return NULL;
    SB identity = {0};
    sb_printf(&identity, "android:%s#%d/%d/%d/w%d", selected.path, selected.collection_index, selected.weight, selected.italic, weight);
    for (unsigned axis = 0; axis < selected.axis_count; axis++) {
        sb_printf(&identity, "/%08x:%a", selected.axes[axis].tag, (double)selected.axes[axis].value);
    }
    Face *face = hm_get(&faces, identity.s);
    if (!face) {
        FT_Face ft;
        if (!FT_New_Face(ftlib, selected.path, selected.collection_index, &ft)) {
            face = xcalloc(1, sizeof *face);
            face->ft = ft;
            face->path = xstrdup(selected.path);
            face->index = selected.collection_index;
            face->hb = hb_ft_face_create_referenced(ft);
            face->color = FT_HAS_COLOR(ft);
            face->weight = selected.weight;
            face->italic = selected.italic;
            face->android_system = true;
            face->android_axis_count = selected.axis_count;
            for (unsigned axis = 0; axis < selected.axis_count; axis++) {
                face->android_axes[axis] = (hb_variation_t){selected.axes[axis].tag, selected.axes[axis].value};
            }
            hb_ot_var_axis_info_t weight_axis;
            if (hb_ot_var_find_axis_info(face->hb, HB_TAG('w', 'g', 'h', 't'), &weight_axis)) {
                unsigned axis = 0;
                while (axis < face->android_axis_count && face->android_axes[axis].tag != weight_axis.tag) axis++;
                if (axis < ANDROID_FONT_MAX_AXES) {
                    float value = LCLAMP((float)weight, weight_axis.min_value, weight_axis.max_value);
                    face->android_axes[axis] = (hb_variation_t){weight_axis.tag, value};
                    if (axis == face->android_axis_count) face->android_axis_count++;
                    face->weight = (int)lroundf(value);
                }
            }
        }
        hm_put(&faces, identity.s, face ? face : (Face *)1);
    }
    free(identity.s);
    return face == (Face *)1 ? NULL : face;
}
#elif !defined(__APPLE__)
/* Fontconfig 2.18.1 compares these properties before CHARSET (fcmatch.c,
 * d87ec67db56518e9e9c36ff38f151dfc9b728b2e, PRI_FILE through PRI_FOUNDRY).
 * For a singleton charset, a covering font strictly dominates a noncovering
 * font with identical preceding values. Keep every possible winner, in the
 * original order, then let Fontconfig perform its unchanged full comparison
 * and render preparation. Other versions and unsupported patterns use the
 * original matcher. All accesses are serialized by font_mu. */
enum { FC_CANDIDATE_LIMIT = 8192, FC_GROUP_LIMIT = 64 };
static const char *fc_prefix[] = {
    FC_FILE, "fontwrapper", FC_FONTFORMAT, FC_VARIABLE, "namedinstance",
    FC_SCALABLE, FC_COLOR, FC_FOUNDRY
};
typedef struct {
    FcConfig *config;
    FcFontSet *sets[2];
    int counts[2], count, group_count;
    FcPattern **fonts, **selected;
    FcCharSet **charsets;
    unsigned char *group;
    unsigned generation, prefix_mask;
    unsigned long epoch;
    bool ready, grouped, supported;
} FcCandidates;
static FcCandidates fc_candidates;

static void fc_candidates_clear(void) {
    unsigned long next = fc_candidates.epoch + (fc_candidates.ready || fc_candidates.config);
    for (int i = 0; i < fc_candidates.count; i++)
        FcPatternDestroy(fc_candidates.fonts[i]);
    free(fc_candidates.fonts); free(fc_candidates.selected);
    free(fc_candidates.charsets); free(fc_candidates.group);
    if (fc_candidates.config) FcConfigDestroy(fc_candidates.config);
    memset(&fc_candidates, 0, sizeof fc_candidates);
    fc_candidates.epoch = next;
}

/* Retain patterns as well as the config: clearing/replacing application fonts
 * must not leave borrowed charset pointers dangling or allow pointer reuse to
 * make a changed font set appear unchanged. One bounded snapshot is retained. */
static bool fc_candidates_refresh(void) {
    if (FcGetVersion() != 21801) return false;
    FcConfig *config = FcConfigGetCurrent();
    FcFontSet *sets[2] = {config ? FcConfigGetFonts(config, FcSetSystem) : NULL,
                          config ? FcConfigGetFonts(config, FcSetApplication) : NULL};
    int counts[2] = {sets[0] ? sets[0]->nfont : 0, sets[1] ? sets[1]->nfont : 0};
    unsigned generation = __atomic_load_n(&font_gen, __ATOMIC_RELAXED);
    bool same = fc_candidates.ready && config == fc_candidates.config
        && generation == fc_candidates.generation;
    for (int k = 0, offset = 0; same && k < 2; k++) {
        same = sets[k] == fc_candidates.sets[k] && counts[k] == fc_candidates.counts[k];
        for (int i = 0; same && i < counts[k]; i++)
            same = sets[k]->fonts[i] == fc_candidates.fonts[offset + i];
        offset += counts[k];
    }
    if (same) return fc_candidates.supported;
    fc_candidates_clear();
    /* Empty/oversized sets take the original matcher/memo path. Repeated
     * unsupported refreshes must not churn the epoch or its 512-entry memo;
     * entering or leaving an actual candidate snapshot still invalidates it. */
    if (!config || counts[0] < 0 || counts[1] < 0
        || counts[0] > FC_CANDIDATE_LIMIT || counts[1] > FC_CANDIDATE_LIMIT - counts[0])
        return false;
    int count = counts[0] + counts[1];
    if (!count) return false;
    FcCandidates *c = &fc_candidates;
    c->config = FcConfigReference(config);
    if (!c->config) return false;
    c->fonts = xcalloc((size_t)count, sizeof *c->fonts);
    c->selected = xcalloc((size_t)count, sizeof *c->selected);
    c->charsets = xcalloc((size_t)count, sizeof *c->charsets);
    c->group = xcalloc((size_t)count, sizeof *c->group);
    c->generation = generation; c->ready = c->supported = true;
    c->epoch++; /* A new supported-size snapshot invalidates legacy memo entries. */
    for (int k = 0; k < 2; k++) {
        c->sets[k] = sets[k]; c->counts[k] = counts[k];
        for (int i = 0; i < counts[k]; i++) {
            int n = c->count++;
            c->fonts[n] = sets[k]->fonts[i];
            FcPatternReference(c->fonts[n]);
            FcValue extra;
            if (FcPatternGetCharSet(c->fonts[n], FC_CHARSET, 0, &c->charsets[n]) != FcResultMatch
                || FcPatternGet(c->fonts[n], FC_CHARSET, 1, &extra) == FcResultMatch)
                c->supported = false;
        }
    }
    return c->supported;
}

static bool fc_prefix_equal(FcPattern *a, FcPattern *b, unsigned mask) {
    for (unsigned p = 0; p < ARRLEN(fc_prefix); p++) if (mask & (1u << p)) {
        for (int i = 0; ; i++) {
            FcValue av, bv; FcValueBinding ab, bb;
            FcResult ar = FcPatternGetWithBinding(a, fc_prefix[p], i, &av, &ab);
            FcResult br = FcPatternGetWithBinding(b, fc_prefix[p], i, &bv, &bb);
            if (ar != br) return false;
            if (ar != FcResultMatch) break;
            if (ab != bb || !FcValueEqual(av, bv)) return false;
        }
    }
    return true;
}

static bool fc_candidates_group(unsigned mask) {
    FcCandidates *c = &fc_candidates;
    if (c->grouped && c->prefix_mask == mask) return c->group_count > 0;
    c->grouped = true; c->prefix_mask = mask; c->group_count = 0;
    int representative[FC_GROUP_LIMIT], groups = 0;
    for (int i = 0; i < c->count; i++) {
        for (unsigned p = 0; p < ARRLEN(fc_prefix); p++) if (mask & (1u << p)) {
            FcPatternIter iter;
            if (FcPatternFindIter(c->fonts[i], &iter, fc_prefix[p])
                && FcPatternIterValueCount(c->fonts[i], &iter) > 64) return false;
        }
        int g = 0;
        while (g < groups && !fc_prefix_equal(c->fonts[i], c->fonts[representative[g]], mask)) g++;
        if (g == groups) {
            if (groups == FC_GROUP_LIMIT) return false;
            representative[groups++] = i;
        }
        c->group[i] = (unsigned char)g;
    }
    c->group_count = groups;
    return groups > 0;
}

static FcPattern *fontconfig_match(FcPattern *pattern, uint32_t codepoint, FcResult *result) {
    FcCharSet *requested = NULL; FcValue extra;
    if (!codepoint || !fc_candidates_refresh()
        || FcPatternGetCharSet(pattern, FC_CHARSET, 0, &requested) != FcResultMatch
        || FcCharSetCount(requested) != 1 || !FcCharSetHasChar(requested, codepoint)
        || FcPatternGet(pattern, FC_CHARSET, 1, &extra) == FcResultMatch
        || FcPatternGet(pattern, FC_FILE, 0, &extra) == FcResultMatch
        || FcPatternGet(pattern, FC_INDEX, 0, &extra) == FcResultMatch
        || FcPatternGet(pattern, FC_FT_FACE, 0, &extra) == FcResultMatch)
        return FcFontMatch(NULL, pattern, result);
    unsigned mask = 0;
    for (unsigned p = 0; p < ARRLEN(fc_prefix); p++)
        if (FcPatternGet(pattern, fc_prefix[p], 0, &extra) == FcResultMatch) mask |= 1u << p;
    if (!fc_candidates_group(mask)) return FcFontMatch(NULL, pattern, result);
    FcCandidates *c = &fc_candidates;
    bool covered[FC_GROUP_LIMIT] = {false};
    for (int i = 0; i < c->count; i++)
        if (FcCharSetHasChar(c->charsets[i], codepoint)) covered[c->group[i]] = true;
    int selected = 0;
    for (int i = 0; i < c->count; i++)
        if (!covered[c->group[i]] || FcCharSetHasChar(c->charsets[i], codepoint))
            c->selected[selected++] = c->fonts[i];
    if (selected == c->count) return FcFontMatch(NULL, pattern, result);
    FcFontSet set = {selected, selected, c->selected};
    FcFontSet *sets[] = {&set};
    FcPattern *match = FcFontSetMatch(c->config, sets, 1, pattern, result);
    const char *verify = getenv("LUMEN_FONTCONFIG_VALIDATE");
    if (verify && *verify && strcmp(verify, "0")) {
        FcResult original_result;
        FcPattern *original = FcFontMatch(NULL, pattern, &original_result);
        bool equal = *result == original_result && ((!match && !original)
            || (match && original && FcPatternEqual(match, original)));
        fprintf(stderr, "lumen-fontconfig-verify cp=U+%04X equal=%d result=%d original_result=%d candidates=%d fonts=%d groups=%d\n",
                codepoint, equal, *result, original_result, selected, c->count, c->group_count);
        if (!equal) {
            /* Diagnostic mismatches are explicit validation failures. Return
             * the original result and disable this snapshot's optimization. */
            if (match) FcPatternDestroy(match);
            c->supported = false;
            c->epoch++; /* Invalidate any previously memoized optimized winner. */
            *result = original_result;
            return original;
        }
        if (original) FcPatternDestroy(original);
    }
    return match;
}

static Face *fontconfig_face(const char *name, int weight, bool italic, uint32_t codepoint) {
    FcPattern *pattern = FcPatternCreate();
    if (!pattern) return NULL;
    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)name);
    FcPatternAddInteger(pattern, FC_WEIGHT, weight >= 600 ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pattern, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    if (codepoint) {
        FcCharSet *characters = FcCharSetCreate();
        if (!characters) { FcPatternDestroy(pattern); return NULL; }
        FcCharSetAddChar(characters, codepoint);
        FcPatternAddCharSet(pattern, FC_CHARSET, characters);
        FcCharSetDestroy(characters);
    }
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcPattern *match = fontconfig_match(pattern, codepoint, &result);
    Face *matched = NULL;
    if (match) {
        FcChar8 *filename = NULL; int index = 0;
        if (FcPatternGetString(match, FC_FILE, 0, &filename) == FcResultMatch) {
            FcPatternGetInteger(match, FC_INDEX, 0, &index);
            char spec[1024]; snprintf(spec, sizeof spec, "%s#%d", filename, index);
            matched = face_load(spec);
            if (matched && codepoint && !FT_Get_Char_Index(matched->ft, codepoint)) matched = NULL;
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return matched;
}
#endif

static Face *resolve_one(const char *name, int weight, bool italic, bool *synth_b, bool *synth_i) {
    char lname[128]; size_t n = 0;
    for (const char *p = name; *p && n < sizeof lname - 1; p++) lname[n++] = (char)lc((unsigned char)*p);
    lname[n] = 0;
    WebDecl *best = NULL; int bs = -1;
    for (WebDecl *d = webdecls; d; d = d->next) {
        if (strcmp(d->family, lname)) continue;
        int sc = (d->italic == italic) * 2 + ((d->weight >= 600) == (weight >= 600)) + (d->face ? 4 : 0);
        if (sc > bs) { bs = sc; best = d; }
    }
    for (WebDecl *d = webdecls; d; d = d->next)
        if (!d->state && !strcmp(d->family, lname) && d->italic == italic && (d->weight >= 600) == (weight >= 600)) { d->state = 1; vec_push(webreq, xstrdup(d->url)); }
    if (best && !best->state) { best->state = 1; vec_push(webreq, xstrdup(best->url)); }
    if (best && best->face) { *synth_b = weight >= 600 && best->weight < 600; *synth_i = italic && !best->italic; return best->face; }
    if (best) return NULL;
#ifdef __ANDROID__
    const char *generic = NULL;
    static const struct { const char *name, *generic; } android_families[] = {
        {"sans-serif", "sans-serif"}, {"system-ui", "sans-serif"}, {"ui-sans-serif", "sans-serif"},
        {"serif", "serif"}, {"ui-serif", "serif"}, {"monospace", "monospace"}, {"ui-monospace", "monospace"},
        {"cursive", "cursive"}, {"fantasy", "fantasy"}, {"emoji", "emoji"},
        {"arial", "sans-serif"}, {"helvetica", "sans-serif"}, {"roboto", "sans-serif"},
        {"times", "serif"}, {"times new roman", "serif"}, {"courier", "monospace"}, {"courier new", "monospace"}
    };
    for (unsigned index = 0; index < ARRLEN(android_families); index++) {
        if (!strcmp(lname, android_families[index].name)) { generic = android_families[index].generic; break; }
    }
    Face *matched = generic ? android_system_face(generic, weight, italic, 0) : NULL;
    if (matched) {
        *synth_b = weight >= 600 && matched->weight < 600;
        *synth_i = italic && !matched->italic;
        return matched;
    }
#endif
    for (size_t i = 0; i < ARRLEN(families); i++) {
        if (strcmp(families[i].name, lname)) continue;
        const FamilyDef *d = &families[i];
        bool bold = weight >= 600;
        const char *pick = bold && italic ? d->bolditalic : bold ? d->bold : italic ? d->italic : d->regular;
        if (!pick) { pick = bold && d->bold ? d->bold : italic && d->italic ? d->italic : d->regular; *synth_b = bold && pick != d->bold && pick != d->bolditalic; *synth_i = italic && pick != d->italic && pick != d->bolditalic; }
        Face *f = face_load(pick);
        if (f) return f;
    }
#if !defined(__APPLE__) && !defined(__ANDROID__)
    Face *matched = fontconfig_face(name, weight, italic, 0);
    if (matched) {
        *synth_b = weight >= 600 && matched->weight < 600;
        *synth_i = italic && !matched->italic;
        return matched;
    }
#endif
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
    f->zero_valid = false;
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
#ifdef __ANDROID__
    if (face->android_system) snprintf(key, sizeof key, "android:%p@%.2f/%d%d", (void *)face, size, sb, si);
#endif
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
#ifdef __ANDROID__
    if (face->android_system) hb_font_set_variations(hb, face->android_axes, face->android_axis_count);
#endif
    f->hb = hb;
    font_metrics(f);
    hm_put(&fonts, key, f);
    return f;
}

Font *font_resized(Font *f, float size) {
    if (!f || size <= 0 || fabsf(f->size - size) < 0.005f) return f;
    char key[64]; snprintf(key, sizeof key, "rs:%p@%.2f", (void *)f, size);
    pthread_mutex_lock(&font_mu);
    Font *r = hm_get(&fonts, key);
    if (!r) {
        r = xcalloc(1, sizeof *r);
        r->face = f->face; r->size = size; r->synth_bold = f->synth_bold; r->synth_italic = f->synth_italic;
        hb_font_t *hb = hb_font_create_sub_font(f->hb);   /* keeps the parent's variation coordinates (weight instances) */
        int sc = (int)lroundf(size * 64); hb_font_set_scale(hb, sc, sc); hb_font_set_ptem(hb, size * 0.75f);
        r->hb = hb; font_metrics(r);
        hm_put(&fonts, key, r);
    }
    pthread_mutex_unlock(&font_mu);
    return r;
}

static Font *font_get_slow(const char *family, int weight, bool italic, float size);
Font *font_get(const char *family, int weight, bool italic, float size) {
    if (size <= 0) size = 0.01f;
    if (!family || !*family) family = "sans-serif";
    size_t fl = strlen(family); uint32_t h = 2166136261u, sb;
    for (size_t i = 0; i < fl; i++) h = (h ^ (uint8_t)family[i]) * 16777619u;
    memcpy(&sb, &size, 4);
    uint32_t k = h ^ (uint32_t)weight * 2654435761u ^ sb * 40503u ^ (uint32_t)italic;
    FontMemo *m = &font_memo[(k ^ k >> 16) & 255];
    unsigned g = __atomic_load_n(&font_gen, __ATOMIC_RELAXED);
    if (m->f && m->gen == g && m->h == h && m->w == weight && m->it == italic && m->size == size && m->fl == fl && !memcmp(m->fam, family, fl)) return m->f;
    Font *f = font_get_slow(family, weight, italic, size);
    if (f) { if (m->fl < fl || !m->fam) { free(m->fam); m->fam = xmalloc(fl + 1); } memcpy(m->fam, family, fl + 1); m->fl = fl; m->gen = g; m->h = h; m->w = weight; m->it = italic; m->size = size; m->f = f; }
    return f;
}
static Font *font_get_slow(const char *family, int weight, bool italic, float size) {
    char ck[512]; snprintf(ck, sizeof ck, "%s|%d|%d", family, weight >= 600 ? 700 : 400, italic);
#ifdef __ANDROID__
    snprintf(ck, sizeof ck, "%s|%d|%d", family, weight, italic);
#endif
    pthread_mutex_lock(&font_mu);
    font_init(); /* CSS font units can precede the first text layout. */
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
        for (int index = 0; !face && fallbacks[index]; index++) face = face_load(fallbacks[index]);
        if (!face) { pthread_mutex_unlock(&font_mu); return NULL; }
        flags = (uintptr_t)sb | ((uintptr_t)si << 1);
        Face **slot = xmalloc(sizeof(Face *) * 2); slot[0] = face; slot[1] = (Face *)flags;
        hm_put(&resolve_cache, ck, slot);
    } else { Face **slot = (Face **)face; face = slot[0]; flags = (uintptr_t)slot[1]; }
    Font *f = font_for_face(face, size, flags & 1, flags & 2);
    if (face->ft->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS && hb_ot_var_has_data(face->hb)
#ifdef __ANDROID__
        && !face->android_system
#endif
    ) {
        /* per-weight instance for variable fonts */
        if (weight != 400) {
            char key[600]; snprintf(key, sizeof key, "%s#%d@%.2f/w%d", face->path, face->index, size, weight);
            Font *vf = hm_get(&fonts, key);
            if (!vf) {
                vf = xcalloc(1, sizeof *vf); *vf = *f; memset(&vf->glyphs, 0, sizeof vf->glyphs);
                vf->zero_valid = false; /* this instance has different variation coordinates */
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
#ifdef __ANDROID__
    typedef struct { uint32_t codepoint; int weight; bool italic, valid; Face *face; } AndroidFallbackMemo;
    static AndroidFallbackMemo memo[512];
    int weight = base->synth_bold ? 700 : base->face->weight;
    bool italic = base->synth_italic || base->face->italic;
    AndroidFallbackMemo *entry = &memo[(cp ^ (uint32_t)weight ^ ((uint32_t)italic << 8)) % ARRLEN(memo)];
    if (!entry->valid || entry->codepoint != cp || entry->weight != weight || entry->italic != italic) {
        *entry = (AndroidFallbackMemo){cp, weight, italic, true, android_system_face("sans-serif", weight, italic, cp)};
    }
    if (entry->face && FT_Get_Char_Index(entry->face->ft, cp)) {
        return font_for_face(entry->face, base->size, weight >= 600 && entry->face->weight < 600, italic && !entry->face->italic);
    }
#endif
    for (int i = 0; fallbacks[i]; i++) {
        Face *fc = face_load(fallbacks[i]);
        if (!fc || fc == base->face) continue;
        if (FT_Get_Char_Index(fc->ft, cp)) return font_for_face(fc, base->size, base->synth_bold, base->synth_italic);
    }
#if !defined(__APPLE__) && !defined(__ANDROID__)
    typedef struct { uint32_t codepoint; int weight; bool italic, valid; Face *face; unsigned long epoch; unsigned generation; } FallbackMemo;
    static FallbackMemo memo[512];
    int weight = base->synth_bold ? 700 : base->face->weight;
    bool italic = base->synth_italic || base->face->italic;
    fc_candidates_refresh();
    unsigned long epoch = fc_candidates.epoch;
    unsigned generation = __atomic_load_n(&font_gen, __ATOMIC_RELAXED);
    FallbackMemo *entry = &memo[(cp ^ (uint32_t)weight ^ ((uint32_t)italic << 8)) % ARRLEN(memo)];
    if (!entry->valid || entry->codepoint != cp || entry->weight != weight || entry->italic != italic
        || entry->epoch != epoch || entry->generation != generation) {
        Face *face = fontconfig_face("sans-serif", weight, italic, cp);
        *entry = (FallbackMemo){cp, weight, italic, true, face, fc_candidates.epoch, generation};
    }
    if (entry->face) return font_for_face(entry->face, base->size, weight >= 600 && entry->face->weight < 600, italic && !entry->face->italic);
#endif
    return NULL;
}

#ifdef __ANDROID__
static Font *android_combining_font(Font *base, Font *selected, uint32_t codepoint,
    const char *text, size_t length, size_t *end) {
    uint32_t cluster[32] = {codepoint};
    unsigned count = 1;
    size_t cursor = *end;
    while (cursor < length && count < ARRLEN(cluster)) {
        if ((unsigned char)text[cursor] < 0x80) break;
        uint32_t next;
        size_t bytes = (size_t)utf8_decode(text + cursor, length - cursor, &next);
        hb_unicode_general_category_t category = hb_unicode_general_category(hb_unicode_funcs_get_default(), next);
        if (category != HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK
            && category != HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK
            && category != HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK) break;
        cluster[count++] = next;
        cursor += bytes;
    }
    *end = cursor;
    for (unsigned index = 1; index < count; index++) {
        uint32_t mark = cluster[index];
        if ((mark >= 0xfe00 && mark <= 0xfe0f) || (mark >= 0xe0100 && mark <= 0xe01ef)
            || FT_Get_Char_Index(selected->face->ft, mark)) continue;
        Font *candidate = fallback_for(base, mark);
        if (!candidate || candidate == selected) continue;
        bool covers_cluster = true;
        for (unsigned member = 0; member < count; member++) {
            uint32_t scalar = cluster[member];
            if ((scalar >= 0xfe00 && scalar <= 0xfe0f) || (scalar >= 0xe0100 && scalar <= 0xe01ef)) continue;
            if (!FT_Get_Char_Index(candidate->face->ft, scalar)) { covers_cluster = false; break; }
        }
        if (covers_cluster) return candidate;
    }
    return selected;
}
#endif

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
#ifdef __ANDROID__
        want = android_combining_font(f, want, cp, s, len, &j);
#endif
        if (want != cur) { if (i > start) shape_segment(cur, s, start, i - start, ls, (void *)&g, &x); start = i; cur = want; }
        i = j;
    }
    if (len > start) shape_segment(cur, s, start, len - start, ls, (void *)&g, &x);
    pthread_mutex_unlock(&font_mu);
    out->g = g.v; out->n = g.n; out->width = x;
}
void shaped_free(ShapedRun *r) { free(r->g); r->g = NULL; r->n = 0; }
unsigned font_generation(void) { return __atomic_load_n(&font_gen, __ATOMIC_RELAXED); }

float font_zero_advance(Font *f, bool upright) {
    if (!f) return 0;
    pthread_mutex_lock(&font_mu);
    unsigned generation = font_generation();
    if (!f->zero_valid || f->zero_generation != generation) {
        Font *selected = f;
        if (!FT_Get_Char_Index(f->face->ft, '0')) {
            Font *fallback = fallback_for(f, '0');
            if (fallback) selected = fallback;
        }
        hb_codepoint_t glyph;
        if (hb_font_get_nominal_glyph(selected->hb, '0', &glyph)) {
            f->zero_advance[0] = hb_font_get_glyph_h_advance(selected->hb, glyph) / 64.f;
            if (selected->synth_bold) f->zero_advance[0] += selected->size / 32;
            f->zero_advance[1] = fabsf(hb_font_get_glyph_v_advance(selected->hb, glyph) / 64.f);
        } else {
            f->zero_advance[0] = f->size * .5f;
            f->zero_advance[1] = f->size;
        }
        f->zero_generation = generation;
        f->zero_valid = true;
    }
    float result = f->zero_advance[upright ? 1 : 0];
    pthread_mutex_unlock(&font_mu);
    return result;
}

float text_width(Font *f, const char *s, size_t len, float ls) {
    ShapedRun r; text_shape(f, s, len, ls, &r); float w = r.width; shaped_free(&r); return w;
}

/* "<gid hex>@<scale x100>": built by hand, snprintf showed up in scroll profiles */
static void glyph_key(char key[32], uint32_t gid, float scale) {
    static const char hx[] = "0123456789abcdef";
    char t[24]; int n = 0, o = 0;
    do { t[n++] = hx[gid & 15]; gid >>= 4; } while (gid);
    while (n) key[o++] = t[--n];
    key[o++] = '@';
    long k = lroundf(scale * 100); if (k < 0) { key[o++] = '-'; k = -k; }
    do { t[n++] = (char)('0' + k % 10); k /= 10; } while (k && n < 20);
    while (n) key[o++] = t[--n];
    key[o] = 0;
}
/* Fonts and their glyphs live for the process, so each thread remembers recent lookups without the
 * font lock: raster band threads painting text together otherwise queue on it. */
typedef struct { const Font *f; uint32_t gid; long scale; const Glyph *g; } GlyphSlot;
static _Thread_local GlyphSlot glyph_slots[512];
const Glyph *font_glyph(Font *f, uint32_t gid, float scale) {
    long sk = lroundf(scale * 100);
    GlyphSlot *slot = &glyph_slots[((uintptr_t)f >> 4 ^ gid * 0x9e3779b1u ^ (uintptr_t)sk * 0x85ebca6bu) & 511];
    if (slot->f == f && slot->gid == gid && slot->scale == sk) return slot->g;
    char key[32]; glyph_key(key, gid, scale);
    pthread_mutex_lock(&font_mu);
    Glyph *g = hm_get(&f->glyphs, key);
    if (g) { pthread_mutex_unlock(&font_mu); *slot = (GlyphSlot){f, gid, sk, g}; return g; }
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
#ifdef __ANDROID__
    if (f->face->android_system && f->face->color && !g->rgba && !g->alpha) {
        AndroidFontMatch match = {0};
        snprintf(match.path, sizeof match.path, "%s", f->face->path);
        match.collection_index = f->face->index;
        match.axis_count = f->face->android_axis_count;
        for (unsigned axis = 0; axis < match.axis_count; axis++) {
            match.axes[axis].tag = f->face->android_axes[axis].tag;
            match.axes[axis].value = f->face->android_axes[axis].value;
        }
        android_font_raster(&match, gid, px, g);
    }
#endif
    hm_put(&f->glyphs, key, g);
    pthread_mutex_unlock(&font_mu);
    *slot = (GlyphSlot){f, gid, sk, g};
    return g;
}

void font_declare(const char *family, int weight, bool italic, const char *url) {
    char lname[128]; size_t n = 0;
    for (const char *p = family; *p && n < sizeof lname - 1; p++) lname[n++] = (char)lc((unsigned char)*p);
    lname[n] = 0;
    pthread_mutex_lock(&font_mu);
    for (WebDecl *d = webdecls; d; d = d->next)
        if (d->weight == weight && d->italic == italic && !strcmp(d->family, lname) && !strcmp(d->url, url)) { pthread_mutex_unlock(&font_mu); return; }
    WebDecl *d = xcalloc(1, sizeof *d);
    d->family = xstrdup(lname); d->url = xstrdup(url); d->weight = weight; d->italic = italic;
    for (WebDecl *o = webdecls; o; o = o->next) if (!strcmp(o->url, url) && o->state) { d->state = o->state; d->face = o->face; break; }
    d->next = webdecls; webdecls = d;
    hm_free(&resolve_cache, free); memset(&resolve_cache, 0, sizeof resolve_cache); __atomic_add_fetch(&font_gen, 1, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&font_mu);
}

char *font_next_request(void) {
    pthread_mutex_lock(&font_mu);
    char *u = webreq.n ? webreq.v[--webreq.n] : NULL;
    pthread_mutex_unlock(&font_mu);
    return u;
}

int font_pending(void) {
    int n = 0;
    pthread_mutex_lock(&font_mu);
    for (WebDecl *d = webdecls; d; d = d->next) n += d->state == 1;
    pthread_mutex_unlock(&font_mu);
    return n;
}

/* FT_New_Memory_Face borrows its input until the face is destroyed. WOFF
   decoding can replace the stream, so the original buffer needs an explicit
   face-owned lifetime instead of relying on a stream pointer retaining it.
   Our HarfBuzz faces use create_referenced, never create_cached (which owns
   FT_Face.generic itself). The finalizer receives the owning FT_Face. */
static void memory_face_done(void *object) {
    FT_Face face = object;
    free(face->generic.data);
    face->generic.data = NULL;
}

bool font_loaded(const char *url, const void *data, size_t len) {
    pthread_mutex_lock(&font_mu);
    Face *f = NULL; FT_Face ft;
    void *buf = data && len ? xmalloc(len) : NULL;
    if (buf) memcpy(buf, data, len);
    if (buf && !FT_New_Memory_Face(ftlib, buf, (FT_Long)len, 0, &ft)) {
        ft->generic.data = buf;
        ft->generic.finalizer = memory_face_done;
        f = xcalloc(1, sizeof *f);
        char key[600]; snprintf(key, sizeof key, "web:%s", url);
        f->ft = ft; f->path = xstrdup(key);
        f->hb = hb_ft_face_create_referenced(ft);
        f->color = FT_HAS_COLOR(ft);
        f->weight = (ft->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400;
        f->italic = ft->style_flags & FT_STYLE_FLAG_ITALIC;
    } else free(buf);
    for (WebDecl *d = webdecls; d; d = d->next) if (!strcmp(d->url, url)) { d->state = 2; d->face = f; }
    if (f) { hm_free(&resolve_cache, free); memset(&resolve_cache, 0, sizeof resolve_cache); __atomic_add_fetch(&font_gen, 1, __ATOMIC_RELAXED); }
    pthread_mutex_unlock(&font_mu);
    return f != NULL;
}
