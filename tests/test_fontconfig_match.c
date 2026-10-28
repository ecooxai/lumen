/* Linux Fontconfig differential oracle. Including the implementation keeps
 * its private matching helper private in the production library. This test
 * compares against the actual installed Fontconfig, not a second algorithm. */
#include "../src/text/font.c"
#if !defined(__APPLE__) && !defined(__ANDROID__)
static unsigned comparisons, failures, peak_fonts;
static const uint32_t points[] = {0x41,0x3a9,0x416,0x627,0x915,0x4e00,0xac00,0x1f600};
static FcPattern *request(const char *family, int weight, int slant, const char *lang, uint32_t cp) {
    FcPattern *p = FcPatternCreate();
    FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)family);
    FcPatternAddInteger(p, FC_WEIGHT, weight);
    FcPatternAddInteger(p, FC_SLANT, slant);
    FcPatternAddString(p, FC_LANG, (const FcChar8 *)lang);
    if (cp) {
        FcCharSet *chars = FcCharSetCreate(); FcCharSetAddChar(chars, cp);
        FcPatternAddCharSet(p, FC_CHARSET, chars); FcCharSetDestroy(chars);
    }
    return p;
}
static void compare(FcPattern *p, uint32_t cp) {
    pthread_mutex_lock(&font_mu);
    FcConfigSubstitute(NULL, p, FcMatchPattern); FcDefaultSubstitute(p);
    FcResult expected_status, actual_status;
    FcPattern *expected = FcFontMatch(NULL, p, &expected_status);
    FcPattern *actual = fontconfig_match(p, cp, &actual_status);
    bool same = expected_status == actual_status && ((!expected && !actual)
        || (expected && actual && FcPatternEqual(expected, actual)));
    comparisons++;
    if ((unsigned)fc_candidates.count > peak_fonts) peak_fonts = (unsigned)fc_candidates.count;
    if (!same) {
        failures++;
        FcChar8 *ef = NULL, *af = NULL; int ei = -1, ai = -1;
        if (expected) { FcPatternGetString(expected, FC_FILE, 0, &ef); FcPatternGetInteger(expected, FC_INDEX, 0, &ei); }
        if (actual) { FcPatternGetString(actual, FC_FILE, 0, &af); FcPatternGetInteger(actual, FC_INDEX, 0, &ai); }
        fprintf(stderr, "DIFFERENTIAL FAILURE U+%04X result %d/%d file %s/%s index %d/%d\n", cp,
                actual_status, expected_status, af ? (char *)af : "none", ef ? (char *)ef : "none", ai, ei);
    }
    if (actual) FcPatternDestroy(actual);
    if (expected) FcPatternDestroy(expected);
    FcPatternDestroy(p);
    pthread_mutex_unlock(&font_mu);
}
static void *thread_queries(void *value) {
    uintptr_t n = (uintptr_t)value;
    for (unsigned i = 0; i < 16; i++) compare(request("sans-serif", (n & 1) ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR,
        (n & 2) ? FC_SLANT_ITALIC : FC_SLANT_ROMAN, (n & 1) ? "ja" : "ko", points[i % ARRLEN(points)]), points[i % ARRLEN(points)]);
    return NULL;
}
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    if (FcGetVersion() != 21801) { puts("SKIP audited matcher version unavailable"); return 77; }
    unsetenv("LUMEN_FONTCONFIG_VALIDATE");
    const char *families[] = {"sans-serif", "serif", "monospace", "DejaVu Sans"};
    const char *langs[] = {"en", "ko", "ja", "zh-cn", "ar"};
    for (unsigned f = 0; f < ARRLEN(families); f++) for (unsigned l = 0; l < ARRLEN(langs); l++)
        for (int bold = 0; bold < 2; bold++) for (int italic = 0; italic < 2; italic++)
            for (unsigned c = 0; c < ARRLEN(points); c++) compare(request(families[f], bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR,
                italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN, langs[l], points[c]), points[c]);
    REQUIRE(fc_candidates.supported && fc_candidates.grouped && fc_candidates.group_count > 0);
    for (int mode = 0; mode < 4; mode++) {
        FcPattern *p = request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "ko", 0xac00);
        if (mode == 0) FcPatternAddString(p, FC_FONTFORMAT, (const FcChar8 *)"TrueType");
        if (mode == 1) FcPatternAddBool(p, FC_SCALABLE, FcFalse);
        if (mode == 2) FcPatternAddString(p, FC_FOUNDRY, (const FcChar8 *)"UnmatchedFoundryForOracle");
        if (mode == 3) FcPatternAddBool(p, FC_COLOR, FcTrue);
        compare(p, 0xac00);
    }
    FcPattern *p = request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "en", 0);
    compare(p, 0); /* no charset: unchanged original path */
    p = request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "ko", 0xac00);
    FcPatternDel(p, FC_CHARSET); FcCharSet *chars = FcCharSetCreate();
    FcCharSetAddChar(chars, 0xac00); FcCharSetAddChar(chars, 0x4e00);
    FcPatternAddCharSet(p, FC_CHARSET, chars); FcCharSetDestroy(chars);
    compare(p, 0xac00); /* multi-character request: unchanged original path */
    pthread_t workers[4];
    for (uintptr_t i = 0; i < ARRLEN(workers); i++) REQUIRE(!pthread_create(&workers[i], NULL, thread_queries, (void *)i));
    for (unsigned i = 0; i < ARRLEN(workers); i++) REQUIRE(!pthread_join(workers[i], NULL));
    FcConfig *original = FcConfigReference(FcConfigGetCurrent());
    REQUIRE(original);
    p = request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "en", 0x41);
    FcConfigSubstitute(NULL, p, FcMatchPattern); FcDefaultSubstitute(p);
    FcResult status; FcPattern *chosen = FcFontMatch(NULL, p, &status); FcChar8 *filename = NULL;
    REQUIRE(chosen && FcPatternGetString(chosen, FC_FILE, 0, &filename) == FcResultMatch);
    char *file = xstrdup((const char *)filename); FcPatternDestroy(chosen); FcPatternDestroy(p);
    p = request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "en", 0x41);
    FcPatternAddString(p, FC_FILE, (const FcChar8 *)file); compare(p, 0x41);
    p = request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "en", 0x41);
    FcPatternAddInteger(p, FC_INDEX, 0); compare(p, 0x41);
    FcConfig *temporary = FcConfigCreate(); REQUIRE(temporary && FcConfigSetCurrent(temporary));
    REQUIRE(FcConfigAppFontAddFile(temporary, (const FcChar8 *)file));
    compare(request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "en", 0x41), 0x41);
    unsigned long before_clear = fc_candidates.epoch;
    FcConfigAppFontClear(temporary);
    compare(request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "en", 0x41), 0x41);
    REQUIRE(fc_candidates.epoch > before_clear);
    unsigned long empty_epoch = fc_candidates.epoch;
    compare(request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "en", 0x41), 0x41);
    REQUIRE(fc_candidates.epoch == empty_epoch); /* unsupported empty set must not churn */
    REQUIRE(FcConfigAppFontAddFile(temporary, (const FcChar8 *)file));
    compare(request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "en", 0x41), 0x41);
    REQUIRE(fc_candidates.epoch > empty_epoch);
    REQUIRE(FcConfigSetCurrent(original));
    compare(request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "ko", 0xac00), 0xac00);
    unsigned long before_generation = fc_candidates.epoch;
    __atomic_add_fetch(&font_gen, 1, __ATOMIC_RELAXED);
    compare(request("sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN, "ko", 0xac00), 0xac00);
    REQUIRE(fc_candidates.epoch > before_generation);
    pthread_mutex_lock(&font_mu); fc_candidates_clear(); pthread_mutex_unlock(&font_mu);
    FcConfigDestroy(temporary); FcConfigDestroy(original); free(file);
    printf("Fontconfig oracle: %u comparisons, %u mismatches, peak snapshot %u patterns, version %d\n", comparisons, failures, peak_fonts, FcGetVersion());
    return failures ? 1 : 0;
}
#else
int main(void) { return 77; }
#endif
