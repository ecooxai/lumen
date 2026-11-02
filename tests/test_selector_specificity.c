#include "css/css.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;

static void expect_mode(const char *text, unsigned a, unsigned b,
                        unsigned c, bool query) {
    SelList list = {0};
    bool ok = query ? css_parse_query_selector_list(text, &list)
                    : css_parse_selector_list(text, &list);
    checks++;
    if (!ok || list.n != 1) {
        fprintf(stderr, "%s parser: expected one selector: %s\n",
                query ? "query" : "stylesheet", text);
        failures++;
    } else {
        uint32_t got = list.v[0].spec;
        uint32_t want = ((uint32_t)a << 20) |
                        ((uint32_t)b << 10) | (uint32_t)c;
        if (got != want) {
            fprintf(stderr,
                    "%s parser: %s: expected (%u,%u,%u), got (%u,%u,%u)\n",
                    query ? "query" : "stylesheet", text, a, b, c,
                    (unsigned)(got >> 20),
                    (unsigned)((got >> 10) & 1023),
                    (unsigned)(got & 1023));
            failures++;
        }
    }
    css_sellist_free(&list);
}

static void expect(const char *text, unsigned a, unsigned b, unsigned c) {
    expect_mode(text, a, b, c, false);
    expect_mode(text, a, b, c, true);
}

static void repeated(const char *prefix, const char *unit, size_t count,
                     const char *suffix, unsigned a, unsigned b, unsigned c) {
    size_t p = strlen(prefix), u = strlen(unit), s = strlen(suffix);
    char *text = malloc(p + u * count + s + 1);
    if (!text) {
        fputs("selector specificity: allocation failed\n", stderr);
        exit(2);
    }
    memcpy(text, prefix, p);
    char *next = text + p;
    for (size_t i = 0; i < count; i++, next += u)
        memcpy(next, unit, u);
    memcpy(next, suffix, s + 1);
    expect(text, a, b, c);
    free(text);
}

int main(void) {
    static const struct {
        const char *text;
        unsigned a, b, c;
    } cases[] = {
        {"*",                                      0, 0, 0},
        {"div",                                    0, 0, 1},
        {"#id",                                    1, 0, 0},
        {".foo",                                   0, 1, 0},
        {"[data-x]",                               0, 1, 0},
        {"div#id.foo[data-x]:hover::before",         1, 3, 2},
        {":scope",                                 0, 1, 0},
        {":scope > .foo",                          0, 2, 0},

        /* Every relative combinator gets a zero-weight implicit anchor. */
        {":has(*)",                                0, 0, 0},
        {":has(.foo)",                             0, 1, 0},
        {":has(> .foo)",                           0, 1, 0},
        {":has(+ .foo)",                           0, 1, 0},
        {":has(~ .foo)",                           0, 1, 0},
        {":has(+ .foo > span.bar)",                 0, 2, 1},
        {":has(span + span)",                      0, 0, 2},
        {":has(:scope > .foo)",                     0, 2, 0},
        {":scope:has(> .foo)",                      0, 2, 0},
        {"main div:has(.foo)",                      0, 1, 2},
        {"main div.baz",                            0, 1, 2},
        {":has(.a):has(.b)",                        0, 2, 0},
        {":has(.a, .b)",                            0, 1, 0},
        {":has(.a.b span, #id)",                    1, 0, 0},
        {":has(#id, .a.b span)",                    1, 0, 0},
        {":has(:where(#id.foo))",                   0, 0, 0},
        {":has(:where(#id), .foo)",                 0, 1, 0},

        /* Select one whole maximum tuple; do not sum or maximize fields. */
        {":is(.a, .b)",                             0, 1, 0},
        {":is(#id, .a.b span)",                     1, 0, 0},
        {":is(.a.b span, #id)",                     1, 0, 0},
        {":is(.a.b, div.a)",                        0, 2, 0},
        {":is(.a, div.a)",                          0, 1, 1},
        {".outer:is(#id.a, div.b.c)",                1, 2, 0},
        {":not(.a, #id)",                           1, 0, 0},
        {":not(:has(+ .foo))",                      0, 1, 0},
        {":is(:has(+ .foo), div)",                  0, 1, 0},
        {":where(#id.foo, div)",                    0, 0, 0},
        {":where(:scope, :has(#id))",                0, 0, 0},
        {"div.foo:where(#id)",                      0, 1, 1},

        /* Child-index pseudo weight plus the maximum of-list tuple. */
        {":nth-child(odd)",                         0, 1, 0},
        {":nth-last-child(2n + 1)",                 0, 1, 0},
        {":nth-of-type(odd)",                       0, 1, 0},
        {":nth-last-of-type(2)",                    0, 1, 0},
        {":nth-child(2n of *)",                     0, 1, 0},
        {":nth-child(3n of foo, bar, target)",       0, 1, 1},
        {":nth-child(even of .foo, #bar, target)",   1, 1, 0},
        {":nth-child(even of target.foo, .foo#bar, "
         "target.foo#bar, target#bar)",              1, 2, 1},
        {":nth-last-child(3n of foo, bar, target)",  0, 1, 1},
        {":nth-last-child(even of .foo, #bar, target)",
                                                    1, 1, 0},
        {":nth-last-child(even of target.foo, .foo#bar, "
         "target.foo#bar, target#bar)",              1, 2, 1},
        {":nth-child(odd of .a, .b)",                0, 2, 0},
        {":nth-last-child(odd of #id, .a.b span)",   1, 1, 0},
        {":nth-child(odd of :where(#id.foo))",       0, 1, 0},
        {":nth-last-child(odd of :where(#id.foo))",  0, 1, 0},
        {":nth-child(odd of :has(> .foo))",          0, 2, 0},
        {":where(:nth-child(odd of #id))",           0, 0, 0},
        {":is(:nth-child(odd of .foo), #id)",        1, 0, 0},

        /* Valid shadow argument forms; these are metadata-only checks. */
        {":host",                                  0, 1, 0},
        {":host(*)",                               0, 1, 0},
        {":host(.foo)",                            0, 2, 0},
        {":host(div#id.foo)",                       1, 2, 1},
        {":host(:is(#id, .a.b))",                   1, 1, 0},
        {":host(:where(#id.foo))",                  0, 1, 0},
        {":not(:is(:host(#id)))",                   1, 1, 0},
        {":not(:is(:host(.foo)))",                  0, 2, 0},
        {":host-context(*)",                       0, 1, 0},
        {":host-context(.foo)",                    0, 2, 0},
        {":host-context(div#id.foo)",               1, 2, 1},
        {":host-context(:is(#id, .a.b))",           1, 1, 0},
        {":host-context(:where(#id.foo))",          0, 1, 0},
        {":where(:host(#id), :host-context(#id))",   0, 0, 0},
        {"::slotted(*)",                           0, 0, 1},
        {"::slotted(.foo)",                        0, 1, 1},
        {"::slotted(div#id.foo)",                   1, 1, 2},
        {"slot::slotted(.foo)",                     0, 1, 2},
        {"::slotted(:is(#id, .a.b))",               1, 0, 1},
        {"::slotted(:where(#id.foo))",              0, 0, 1},
        {"::slotted(.foo)::before",                 0, 1, 2},
    };

    dom_init();
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
        expect(cases[i].text, cases[i].a, cases[i].b, cases[i].c);

    /* Preserve existing stylesheet-only :matches behavior. */
    expect_mode(":matches(#id, .a.b)", 1, 0, 0, false);

    /* Independent component saturation: neighboring fields must not carry. */
    repeated("i", "#id", 1022, ".c", 1022, 1, 1);
    repeated("i", "#id", 1023, ".c", 1023, 1, 1);
    repeated("i", "#id", 1024, ".c", 1023, 1, 1);
    repeated("i#id", ".c", 1022, "", 1, 1022, 1);
    repeated("i#id", ".c", 1023, "", 1, 1023, 1);
    repeated("i#id", ".c", 1024, "", 1, 1023, 1);
    repeated("#id.c ", "i ", 1022, "*", 1, 1, 1022);
    repeated("#id.c ", "i ", 1023, "*", 1, 1, 1023);
    repeated("#id.c ", "i ", 1024, "*", 1, 1, 1023);

    /* Adding an already saturated argument must also clamp independently. */
    repeated("i#outer:is(", "#id", 1024, ")", 1023, 0, 1);
    repeated("#outer:nth-child(odd of ", ".c", 1024, ")", 1, 1023, 0);
    repeated("#outer:nth-last-child(odd of ", ".c", 1024, ")", 1, 1023, 0);
    repeated(":host(", ".c", 1024, ")", 0, 1023, 0);
    repeated(":is(", "i ", 1024, "*)::before", 0, 0, 1023);
    repeated("::slotted(:is(", "i ", 1024, "*))", 0, 0, 1023);
    repeated(":is(#id,", ".c", 1024, ")", 1, 0, 0);

    printf("selector specificity: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
