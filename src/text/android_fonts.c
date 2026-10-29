#include "android_fonts.h"
#ifdef __ANDROID__
#include <android/font.h>
#include <android/font_matcher.h>
#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

static struct {
    AFontMatcher *(*create)(void);
    void (*destroy)(AFontMatcher *);
    void (*set_style)(AFontMatcher *, uint16_t, bool);
    AFont *(*match)(const AFontMatcher *, const char *, const uint16_t *, uint32_t, uint32_t *);
    void (*close)(AFont *);
    const char *(*path)(const AFont *);
    uint16_t (*weight)(const AFont *);
    bool (*italic)(const AFont *);
    size_t (*index)(const AFont *);
    size_t (*axis_count)(const AFont *);
    uint32_t (*axis_tag)(const AFont *, uint32_t);
    float (*axis_value)(const AFont *, uint32_t);
    bool available;
} font_api;
static pthread_once_t font_api_once = PTHREAD_ONCE_INIT;

static void load_font_api(void) {
    void *library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) return;
#define FONT_SYMBOL(field, symbol) do { font_api.field = (__typeof__(font_api.field))dlsym(library, symbol); if (!font_api.field) goto unavailable; } while (0)
    FONT_SYMBOL(create, "AFontMatcher_create");
    FONT_SYMBOL(destroy, "AFontMatcher_destroy");
    FONT_SYMBOL(set_style, "AFontMatcher_setStyle");
    FONT_SYMBOL(match, "AFontMatcher_match");
    FONT_SYMBOL(close, "AFont_close");
    FONT_SYMBOL(path, "AFont_getFontFilePath");
    FONT_SYMBOL(weight, "AFont_getWeight");
    FONT_SYMBOL(italic, "AFont_isItalic");
    FONT_SYMBOL(index, "AFont_getCollectionIndex");
    FONT_SYMBOL(axis_count, "AFont_getAxisCount");
    FONT_SYMBOL(axis_tag, "AFont_getAxisTag");
    FONT_SYMBOL(axis_value, "AFont_getAxisValue");
#undef FONT_SYMBOL
    font_api.available = true;
    return;
unavailable:
    dlclose(library);
}

bool android_font_match(const char *family, int weight, bool italic, uint32_t codepoint, AndroidFontMatch *result) {
    if (!family || !*family || !result || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    pthread_once(&font_api_once, load_font_api);
    if (!font_api.available) return false;
    AFontMatcher *matcher = font_api.create();
    if (!matcher) return false;
    font_api.set_style(matcher, (uint16_t)(weight < 1 ? 1 : weight > 1000 ? 1000 : weight), italic);
    uint16_t text[2];
    uint32_t length = 1;
    if (!codepoint) codepoint = 'A';
    if (codepoint <= 0xffff) text[0] = (uint16_t)codepoint;
    else {
        codepoint -= 0x10000;
        text[0] = (uint16_t)(0xd800 + (codepoint >> 10));
        text[1] = (uint16_t)(0xdc00 + (codepoint & 0x3ff));
        length = 2;
    }
    AFont *font = font_api.match(matcher, !strcmp(family, "system-ui") ? "sans-serif" : family, text, length, NULL);
    font_api.destroy(matcher);
    if (!font) return false;
    bool valid = false;
    AndroidFontMatch selected = {0};
    const char *path = font_api.path(font);
    size_t collection_index = font_api.index(font);
    size_t axis_count = font_api.axis_count(font);
    if (!path || path[0] != '/' || strlen(path) >= sizeof selected.path || collection_index > INT_MAX || axis_count > ANDROID_FONT_MAX_AXES) goto done;
    strcpy(selected.path, path);
    selected.collection_index = (int)collection_index;
    selected.weight = font_api.weight(font);
    selected.italic = font_api.italic(font);
    selected.axis_count = (unsigned)axis_count;
    for (unsigned axis = 0; axis < selected.axis_count; axis++) {
        selected.axes[axis].tag = font_api.axis_tag(font, axis);
        selected.axes[axis].value = font_api.axis_value(font, axis);
        if (!selected.axes[axis].tag || !isfinite(selected.axes[axis].value)) goto done;
        for (unsigned previous = 0; previous < axis; previous++) {
            if (selected.axes[previous].tag == selected.axes[axis].tag) goto done;
        }
    }
    *result = selected;
    valid = true;
done:
    font_api.close(font);
    return valid;
}
#else
bool android_font_match(const char *family, int weight, bool italic, uint32_t codepoint, AndroidFontMatch *result) {
    (void)family;
    (void)weight;
    (void)italic;
    (void)codepoint;
    (void)result;
    return false;
}
#endif
