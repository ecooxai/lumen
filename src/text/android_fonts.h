#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ANDROID_FONT_MAX_AXES 16

typedef struct {
    char path[512];
    int collection_index;
    int weight;
    bool italic;
    unsigned axis_count;
    struct { uint32_t tag; float value; } axes[ANDROID_FONT_MAX_AXES];
} AndroidFontMatch;

bool android_font_match(const char *family, int weight, bool italic, uint32_t codepoint, AndroidFontMatch *result);
