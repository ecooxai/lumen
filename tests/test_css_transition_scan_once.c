/* A style change emits one transition job per property, even when its
 * transition-property list repeats a name or also contains all. */
#include "../src/css/css.h"
#include "../src/text/font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, counts[4], observed_count;
static char observed[64][128];
static bool duplicate;
static Node *expected_node;
static const char *case_name;
static const char *const properties[] = {"width", "height", "opacity"};
static const char *expected_from[3], *expected_to[3];
static void changed(Node *node, const char *property, const char *from, const char *to) {
    assert(node == expected_node);
    for (unsigned k = 0; k < observed_count; k++) if (!strcmp(observed[k], property)) duplicate = true;
    assert(observed_count < 64 && strlen(property) < sizeof observed[0]);
    strcpy(observed[observed_count++], property);
    unsigned i = 0;
    while (i < 3 && strcmp(property, properties[i])) i++;
    counts[i]++;
    if (i < 3) {
        if (strcmp(from, expected_from[i]) || strcmp(to, expected_to[i]))
            fprintf(stderr, "%s: %s endpoints %s -> %s, expected %s -> %s\n",
                    case_name, property, from, to, expected_from[i], expected_to[i]);
        assert(!strcmp(from, expected_from[i]) && !strcmp(to, expected_to[i]));
    }
}
static void check(bool ok, const char *label) {
    if (!ok) fprintf(stderr, "%s: %s counts=%u,%u,%u,other=%u\n", case_name, label,
                     counts[0], counts[1], counts[2], counts[3]);
    assert(ok); checks++;
}
static void scan(Node *node, ComputedStyle *before, ComputedStyle *after,
                 const char *list, unsigned w, unsigned h, unsigned opacity) {
    css_apply_decl(after, NULL, "transition-property", list, NULL, NULL);
    char *retained = strdup(after->tr_prop ? after->tr_prop : "all");
    assert(retained); node->style = after; expected_node = node;
    memset(counts, 0, sizeof counts); observed_count = 0; duplicate = false;
    css_transition_scan(node, before, after, changed);
    check(counts[0] == w, "width scan count");
    check(counts[1] == h, "height scan count");
    check(counts[2] == opacity, "opacity scan count");
    check(!duplicate, "each actual changed property emits at most once");
    check(!strcmp(retained, after->tr_prop ? after->tr_prop : "all"), "authored list positions retained");
    free(retained);
}
int main(void) {
    dom_init(); font_init();
    ComputedStyle *before = style_new_default(), *after = style_new_default();
    const char *const from[] = {"10px", "20px", "0.2"};
    const char *const to[] = {"20px", "30px", "0.4"};
    for (unsigned i = 0; i < 3; i++) {
        css_apply_decl(before, NULL, properties[i], from[i], NULL, NULL);
        css_apply_decl(after, NULL, properties[i], to[i], NULL, NULL);
        expected_from[i] = from[i]; expected_to[i] = to[i];
    }
    Node node = {0}, other = {0}; node.type = other.type = NODE_ELEMENT;
    static const struct { const char *list; unsigned width, height, opacity; } rows[] = {
        {"opacity, opacity",0,0,1}, {"opacity, all",1,1,1},
        {"all, opacity",1,1,1}, {"all, opacity, all, opacity",1,1,1},
        {"width, opacity, width, height, opacity",1,1,1}, {"width, width",1,0,0},
        {"all",1,1,1}, {"none",0,0,0}, {"a\\,width, opacity",0,0,1},
        {"opacity, a\\,b, opacity",0,0,1}, {"WIDTH, OPACITY, width",1,0,1},
        {"height, all, width",1,1,1}
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        case_name = rows[i].list;
        scan(&node, before, after, rows[i].list, rows[i].width, rows[i].height, rows[i].opacity);
    }
    assert(checks == 60);
    /* The seen set belongs to one actual scan only. A subsequent author
     * change, its reversal, and another target each retain a fresh job. */
    case_name = "later author change retains a fresh notification";
    scan(&node, before, after, "opacity, opacity",0,0,1);
    for (unsigned i = 0; i < 3; i++) { expected_from[i] = to[i]; expected_to[i] = from[i]; }
    case_name = "reverse author change retains its own endpoint";
    scan(&node, after, before, "opacity, all",1,1,1);
    for (unsigned i = 0; i < 3; i++) { expected_from[i] = from[i]; expected_to[i] = to[i]; }
    case_name = "another element has independent notification identity";
    scan(&other, before, after, "all, opacity",1,1,1);
    assert(checks == 75);
    style_free(before); style_free(after);
    printf("PASS css_transition_scan_once %u native checks\n", checks);
    return 0;
}
