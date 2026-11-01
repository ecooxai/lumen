/* Relational selectors search the final subject of the complete relative path,
 * not merely the first sibling. Independent expected results protect both the
 * indexed and unfiltered matchers; visit budgets keep simple queries bounded. */
#include "dom/dom.h"
#include "css/css.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
static Node *element(Document *d, Node *parent, const char *tag, const char *cls) {
    Node *n = node_new_element(d, tag, NS_HTML);
    if (cls) node_set_attr(n, "class", cls);
    if (parent) node_append(parent, n);
    return n;
}
static void expect(Node *anchor, const char *text, bool want) {
    SelList selectors = {0};
    assert(css_parse_selector_list(text, &selectors));
    CssMatchCache *cache = NULL;
    for (int indexed = 0; indexed < 2; indexed++) {
        css_has_unfiltered = !indexed;
        for (int cached = 0; cached < 2; cached++) {
            CssMatchCache *previous = cached ? css_match_cache_begin(&cache) : NULL;
            for (int repeat = 0; repeat < 2; repeat++) {
                bool got = css_match_selector_list(&selectors, anchor);
                checks++;
                if (got != want) {
                    if (!repeat && !cached && !indexed)
                        fprintf(stderr, "%s on .%s: expected %d, got %d\n", text,
                                node_attr(anchor, "class"), want, got);
                    failures++;
                }
            }
            if (cached) css_match_cache_end(previous);
        }
    }
    css_match_cache_free(cache);
    css_sellist_free(&selectors);
    css_has_unfiltered = false;
}
static void visit_budget(Node *anchor, const char *text, uint64_t maximum) {
    SelList selectors = {0};
    assert(css_parse_selector_list(text, &selectors));
    uint64_t before = css_has_stats[1];
    assert(!css_match_selector_list(&selectors, anchor));
    uint64_t visits = css_has_stats[1] - before;
    checks++;
    if (visits > maximum) {
        fprintf(stderr, "%s visited %llu nodes, maximum %llu\n", text,
                (unsigned long long)visits, (unsigned long long)maximum);
        failures++;
    }
    css_sellist_free(&selectors);
}
static void exercise_tree(Document *d, Node *root) {
    Node *previous = element(d, root, "section", "previous");
    element(d, previous, "b", "only-before");
    Node *anchor = element(d, root, "div", "anchor");
    element(d, anchor, "b", "own-child");
    Node *a = element(d, root, "section", "a");
    Node *b_child = element(d, a, "div", "b");
    Node *deep = element(d, b_child, "span", "c");
    Node *d_child = element(d, a, "div", "d");
    element(d, d_child, "span", "e");
    Node *b = element(d, root, "section", "b");
    element(d, b, "span", "c");
    Node *last = element(d, root, "footer", "last");
    element(d, last, "span", "late");
    expect(anchor, ":has(+ .a)", true);
    expect(anchor, ":has(+ .b)", false);
    expect(anchor, ":has(~ .b)", true);
    expect(anchor, ":has(+ .a > .b)", true);
    expect(anchor, ":has(+ .a .c)", true);
    expect(anchor, ":has(+ .a > .c)", false);
    expect(anchor, ":has(+ .a .late)", false);
    expect(anchor, ":has(+ .a + .b)", true);
    expect(anchor, ":has(+ .a + .last)", false);
    expect(anchor, ":has(+ .a ~ .last)", true);
    expect(anchor, ":has(+ .a + .b + .last)", true);
    expect(anchor, ":has(+ .a + .b > .c)", true);
    expect(anchor, ":has(~ .a + .b > .c)", true);
    expect(anchor, ":has(+ .a ~ .last > .late)", true);
    expect(anchor, ":has(~ .b + .last > .late)", true);
    expect(anchor, ":has(~ .last + .b)", false);
    expect(anchor, ":has(~ .a > .b .c)", true);
    expect(anchor, ":has(+ .a > .b + .d > .e)", true);
    expect(anchor, ":has(+ .a > .b ~ .d .e)", true);
    expect(anchor, ":has(+ :is(.a,.absent) > .b)", true);
    expect(anchor, ":has(+ .a :not(.missing))", true);
    expect(anchor, ":has(+ .absent .c, + .a > .b)", true);
    expect(anchor, ":has(+ .absent .c, ~ .absent > .b)", false);
    expect(anchor, ":not(:has(+ .a > .b))", false);
    expect(anchor, ":has(+ .a > .b):has(~ .last)", true);
    expect(anchor, ":has(~ .previous .only-before)", false);
    expect(anchor, ":has(~ .a .own-child)", false);
    expect(last, ":has(~ *)", false);
    expect(last, ":has(+ * *)", false);
    /* Removing and reinserting a genuine witness must invalidate candidates. */
    node_remove(deep);
    expect(anchor, ":has(+ .a .c)", false);
    expect(anchor, ":has(~ .b .c)", true);
    node_append(b_child, deep);
    expect(anchor, ":has(+ .a .c)", true);
    node_set_attr(deep, "class", "changed");
    expect(anchor, ":has(+ .a .c)", false);
    node_set_attr(deep, "class", "c");
    expect(anchor, ":has(+ .a .c)", true);
    node_remove(a);
    expect(anchor, ":has(+ .a .c)", false);
    expect(anchor, ":has(+ .b > .c)", true);
    node_append(root, a);
    expect(anchor, ":has(+ .a .c)", false);
    expect(anchor, ":has(~ .a .c)", true);
    expect(anchor, ":has(~ .a + .b)", false);
}
int main(void) {
    dom_init();
    Document *d = doc_new("https://has-relative.test/");
    Node *root = element(d, &d->node, "main", "root");
    exercise_tree(d, root);
    Node *detached = element(d, NULL, "main", "detached");
    exercise_tree(d, detached);
    /* A sibling-subtree scan must not leak out to its parent's next sibling. */
    Node *outer = element(d, root, "article", "outer");
    Node *anchor = element(d, outer, "div", "bounded");
    Node *a = element(d, outer, "section", "a");
    element(d, a, "b", "witness");
    Node *outside = element(d, root, "section", "outside");
    element(d, outside, "b", "escape");
    expect(anchor, ":has(+ .a .witness)", true);
    expect(anchor, ":has(~ .outside .escape)", false);
    expect(anchor, ":has(+ .a .escape)", false);
    /* Thousands of later descendants cannot make an adjacent-only query scan
     * unrelated subtrees, nor make a sibling-only path walk descendants. */
    Node *later = element(d, outer, "aside", "later");
    for (int i = 0; i < 1000; i++) element(d, later, "i", "noise");
    visit_budget(anchor, ":has(+ .absent)", 1);
    visit_budget(anchor, ":has(+ .a .absent)", 1);
    visit_budget(anchor, ":has(+ .a + .absent)", 1);
    visit_budget(anchor, ":has(~ .absent)", 2);
    visit_budget(anchor, ":has(+ .a ~ .absent)", 1);
    /* Attach the detached fixture for the document's normal ownership cleanup. */
    node_append(root, detached);
    doc_free(d);
    printf("relative sibling selectors: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
