#ifndef LUMEN_CSS_ANIMATION_ALIASES_H
#define LUMEN_CSS_ANIMATION_ALIASES_H

#include <string.h>

/* WHATWG Compatibility 3.4.1: only these legacy animation/transition
 * names alias canonical properties. Other vendor prefixes stay untouched.
 * The result borrows the original name's storage. */
static inline const char *css_animation_property_name(const char *property) {
    if (!property || strncmp(property, "-webkit-", 8)) return property;
    const char *name = property + 8;
    static const char *const names[] = {
        "animation", "animation-delay", "animation-direction", "animation-duration",
        "animation-fill-mode", "animation-iteration-count", "animation-name",
        "animation-play-state", "animation-timing-function", "transition",
        "transition-delay", "transition-duration", "transition-property",
        "transition-timing-function"
    };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(name, names[i])) return name;
    return property;
}

#endif
