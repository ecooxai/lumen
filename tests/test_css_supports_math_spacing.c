/* Native CSS.supports and @supports validate original component tokens before
 * comments or property-specific normalization can disguise math operators. */
#include "../src/css/css.h"
#include "../src/text/font.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    dom_init(); font_init();
    struct Case { const char *condition; bool expected; } cases[] = {
        {"(transform: rotate(calc(10deg/**/+/**/2deg)))", false},
        {"(transform: rotate(calc(10deg+2deg)))", false},
        {"(transform: rotate(calc(10deg +2deg)))", false},
        {"(transform: rotate(calc(10deg + 2deg)))", true},
        {"(transform: rotate(calc(10deg + -2deg)))", true},
        {"(width: calc(1px/**/+/**/2px))", false},
        {"(width: calc(1px+2px))", false},
        {"(width: calc(1px +2px))", false},
        {"(width: calc(1px +/**/2px))", false},
        {"(width: calc(1px + 2px))", true},
        {"(width: calc(1px/**/ + /**/2px))", true},
        {"(--tokens: calc(1px+2px))", true},
        {"(width: calc(var(--offset) +2px))", true},
        {"(width: calc(env(custom-offset, 1px) +2px))", true},
        {"(width: calc(attr(data-offset type(<length>), 1px) +2px))", true},
        {"(width: calc(--offset() +2px))", true},
        {"(width: if(style(--choice: yes): calc(1px+2px); else: 2px))", true},
        {"not (transform: rotate(calc(10deg/**/+/**/2deg)))", true},
        {"(width: 1px) and (transform: rotate(calc(10deg+2deg)))", false},
        {"(width: 1px) or (transform: rotate(calc(10deg+2deg)))", true},
        {"(color: green)", true},
        {"(width: if(else: calc(1px+2px)))", true},
        {"(width: if(supports(display: block): 1px; else: calc(1px+2px)))", true}
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        bool actual = css_supports(cases[i].condition);
        if (actual != cases[i].expected) fprintf(stderr, "%s got %d, expected %d\n", cases[i].condition, actual, cases[i].expected);
        assert(actual == cases[i].expected);
    }
    printf("PASS css_supports_math_spacing %zu native conditions\n", sizeof cases / sizeof *cases);
    return 0;
}
