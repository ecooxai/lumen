/* Shared transition-property grammar, mutation and deferred-value controls. */
#include "../src/css/css.h"
#include "../src/text/font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned width_changes, opacity_changes;
static void transition_changed(Node *node, const char *property, const char *from, const char *to) {
    (void)node;
    assert(strcmp(from, to));
    if (!strcmp(property, "width")) width_changes++;
    if (!strcmp(property, "opacity")) opacity_changes++;
}

int main(void) {
    dom_init(); font_init();
    unsigned checks = 0;
    const char *const valid[][2] = {
        {"none", "none"}, {"all", "all"}, {"-webkit-transform, opacity", "-webkit-transform, opacity"}, {"ALL, INVALID, SYNTAX, SRC", "all, INVALID, SYNTAX, SRC"},
        {"WIDTH, opacity, WIDTH", "width, opacity, width"}, {"--MiXeD, --mixed", "--MiXeD, --mixed"},
        {"one, two, three", "one, two, three"}, {"all, top, all", "all, top, all"},
        {"width/**/,/**/opacity", "width, opacity"}, {"w\\69 dth, opacity", "width, opacity"},
        { "\\31 foo, width", "\\31 foo, width" }, {"a\\,b, width", "a\\,b, width"}, {"a\\2c b, width", "a\\,b, width"},
        {"色, width", "色, width"}, {"INITIAL", "initial"}, {"inherit", "inherit"},
        {"unset", "unset"}, {"revert", "revert"}, {"revert-layer", "revert-layer"}
    };
    const char *const invalid[] = {
        "", " ", "one two three", "1, 2, 3", "none, one", "one, none", "none, none", "default", "DEFAULT",
        "initial, width", "width, inherit", "unset, top", "top, revert", "revert-layer, top", "Default, width",
        "n\\6f ne, width", "\\69 nitial, width", "width,", ",width", "width,,top", "wi/**/dth", "\"width\"",
        "width)", "width;opacity", "width !important", "calc(1)", "var()"
    };
    for (unsigned i = 0; i < sizeof valid / sizeof *valid; i++) {
        char *parsed = css_transition_property_parse(valid[i][0]);
        if (!parsed || strcmp(parsed, valid[i][1])) fprintf(stderr, "transition-property:%s got %s, expected %s\n", valid[i][0], parsed ? parsed : "NULL", valid[i][1]);
        assert(parsed && !strcmp(parsed, valid[i][1]));
        char *twice = css_transition_property_parse(parsed);
        assert(twice && !strcmp(twice, parsed));
        char condition[1024]; snprintf(condition, sizeof condition, "(transition-property:%s)", valid[i][0]);
        assert(css_supports(condition));
        if (strcmp(parsed, "initial") && strcmp(parsed, "inherit") && strcmp(parsed, "unset") &&
            strcmp(parsed, "revert") && strcmp(parsed, "revert-layer")) {
            ComputedStyle *stored = style_new_default();
            css_apply_decl(stored, NULL, "transition-property", valid[i][0], NULL, NULL);
            const char *actual = stored->tr_prop ? stored->tr_prop : "all";
            if (strcmp(actual, parsed)) fprintf(stderr, "stored transition list %s became %s, expected %s\n", valid[i][0], actual, parsed);
            assert(!strcmp(actual, parsed)); style_free(stored); checks++;
        }
        free(twice); free(parsed); checks += 3;
    }
    ComputedStyle *style = style_new_default();
    css_apply_decl(style, NULL, "transition-property", "opacity, width", NULL, NULL);
    assert(style->tr_prop && !strcmp(style->tr_prop, "opacity, width")); checks++;
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        char *parsed = css_transition_property_parse(invalid[i]);
        if (parsed) fprintf(stderr, "invalid transition-property:%s accepted as %s\n", invalid[i], parsed);
        assert(!parsed);
        char condition[1024]; snprintf(condition, sizeof condition, "(transition-property:%s)", invalid[i]);
        assert(!css_supports(condition));
        css_apply_decl(style, NULL, "transition-property", invalid[i], NULL, NULL);
        if (!style->tr_prop || strcmp(style->tr_prop, "opacity, width"))
            fprintf(stderr, "invalid literal transition-property:%s changed prior list to '%s'\n", invalid[i], style->tr_prop ? style->tr_prop : "<initial all>");
        assert(style->tr_prop && !strcmp(style->tr_prop, "opacity, width"));
        checks += 3;
        /* A semicolon and !important are invalid inside a standalone value,
         * but have their normal delimiter/priority meaning in declaration text. */
        if (!strchr(invalid[i], ';') && !strchr(invalid[i], '!')) {
            char declarations[2048]; snprintf(declarations, sizeof declarations, "transition-property:opacity;width:11px;transition-property:%s", invalid[i]);
            DeclList *list = css_parse_decls(declarations, strlen(declarations));
            assert(list && list->n == 2 && !strcmp(list->v[0].value, "opacity"));
            css_decls_free(list); checks++;
        }
    }
    const char *normal_declarations = "transition-property:width !important;color:blue";
    DeclList *normal = css_parse_decls(normal_declarations, strlen(normal_declarations));
    assert(normal && normal->n == 2 && normal->v[0].important && !strcmp(normal->v[0].value, "width"));
    css_decls_free(normal); checks++;
    const char *const deferred[] = {"var(--MiXeD)", "env(custom-choice, opacity)", "attr(data-choice type(<custom-ident>), opacity)", "--MiXeD(opacity)"};
    for (unsigned i = 0; i < sizeof deferred / sizeof *deferred; i++) {
        char *parsed = css_transition_property_parse(deferred[i]);
        assert(parsed && !strcmp(parsed, deferred[i])); free(parsed); checks++;
    }
    css_apply_decl(style, NULL, "--Choice", "opacity", NULL, NULL);
    css_apply_decl(style, NULL, "transition-property", "var(--Choice)", NULL, NULL);
    assert(style->tr_prop && !strcmp(style->tr_prop, "opacity")); checks++;
    css_apply_decl(style, NULL, "--Choice", "none, opacity", NULL, NULL);
    css_apply_decl(style, NULL, "transition-property", "var(--Choice)", NULL, NULL);
    assert(!style->tr_prop || !strcmp(style->tr_prop, "all")); checks++;
    /* An escaped comma belongs to one unknown identifier. It must neither
     * create a phantom width transition nor shift the following list entry. */
    ComputedStyle *before = style_new_default(), *after = style_new_default();
    css_apply_decl(before, NULL, "width", "10px", NULL, NULL);
    css_apply_decl(before, NULL, "opacity", "0.2", NULL, NULL);
    css_apply_decl(after, NULL, "width", "20px", NULL, NULL);
    css_apply_decl(after, NULL, "opacity", "0.4", NULL, NULL);
    Node node = {0}; node.type = NODE_ELEMENT; node.style = after;
    const char *const escaped_lists[] = {"a\\,width, opacity", "a\\2c width, opacity"};
    for (unsigned i = 0; i < sizeof escaped_lists / sizeof *escaped_lists; i++) {
        css_apply_decl(after, NULL, "transition-property", escaped_lists[i], NULL, NULL);
        width_changes = opacity_changes = 0;
        css_transition_scan(&node, before, after, transition_changed);
        if (width_changes || opacity_changes != 1) fprintf(stderr, "%s emitted width=%u opacity=%u transitions\n", escaped_lists[i], width_changes, opacity_changes);
        assert(width_changes == 0 && opacity_changes == 1); checks++;
    }
    style_free(before); style_free(after);
    assert(css_transition_property_parse(NULL) == NULL);
    assert(css_transition_property_valid("color", "unchanged-other-property")); checks += 2;
    style_free(style);
    printf("transition property native checks: %u PASS\n", checks);
    return 0;
}
