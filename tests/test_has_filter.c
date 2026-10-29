/* :has() tests the document's elements carrying its subject's name instead of walking subtrees, and
   walks only children for `> x`; every element matches each selector exactly as with full subtree
   walks, through class, ID, attribute and child-list changes, mixed-case names, lists too long to
   use, detached trees and repeated tests of the same element. */
#include "../src/dom/dom.h"
#include "../src/css/css.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *sels[] = {
    ":has(.a)", ":has(> .a)", ":has(.a .b)", ":has(> .a .b)", ":has(> .a > .b)", ":has(.a + .b)", ":has(.a ~ p)", ":has(#x)",
    ":has([data-k])", ":has([DATA-K=v])", ":has(span)", ":has(SPAN.a)", ":has(svg)", ":has(rect)", ":has(*)", ":has(.a:first-child)",
    ":has(:not(.a))", ":has(.nope)", ":has(.q)", ":has(p.a)", ":has([data-k] b)", ":has(.A)", ":has(#X)", ":has(DIV > .zz)", ":has(> span, .b)", "div:has(.a):has(.b)", ":has(:is(.a, #x))", ".a:has(+ .b)", ":has(~ .a)",
};
static int compare_tree(Node *root) {
    int matched = 0;
    for (size_t i = 0; i < sizeof sels / sizeof *sels; i++) {
        SelList l = {0}; assert(css_parse_selector_list(sels[i], &l));
        for (int rep = 0; rep < 2; rep++)
            for (Node *n = root; n; n = node_next_in_tree(n, root)) {
                if (n->type != NODE_ELEMENT) continue;
                css_has_unfiltered = true; bool want = css_match_selector_list(&l, n);
                css_has_unfiltered = false; bool got = css_match_selector_list(&l, n);
                if (want != got) { fprintf(stderr, "%s on <%s>: %d vs %d\n", sels[i], n->tag, got, want); assert(0); }
                matched += got;
            }
        css_sellist_free(&l);
    }
    return matched;
}
static int compare(Document *d) { return compare_tree(&d->node); }
static Node *el(Document *d, Node *parent, const char *tag, const char *cls) {
    Node *n = node_new_element(d, tag, NS_HTML); if (cls) node_set_attr(n, "class", cls); if (parent) node_append(parent, n); return n;
}

int main(void) {
    dom_init();
    Document *d = doc_new("https://has-filter.test/");
    const char *html = "<html><body><div id=top><section><div class=a><span>t</span></div><p class=b></p></section>"
                       "<ul><li><svg><rect></rect></svg></li><li data-k=v><b id=x></b></li></ul></div><main><div></div></main></body></html>";
    html_parse(d, html, strlen(html));
    int base = compare(d);
    Node *main_el = d->body->last, *inner = main_el->first;
    Node *deep = el(d, el(d, inner, "div", NULL), "span", "zz");
    int added = compare(d); assert(added >= base);
    node_set_attr(deep, "class", "a b"); compare(d);
    node_set_attr(deep, "id", "x"); compare(d);
    node_set_attr(deep, "Data-K", "v"); compare(d);
    node_remove(deep); compare(d);
    node_append(inner, deep); compare(d);
    node_set_attr(deep, "class", "zz"); node_remove_attr(deep, "id"); compare(d);
    for (int i = 0; i < 300; i++) el(d, main_el, i % 3 ? "div" : "p", i % 7 ? "q" : "a");   /* many filtered elements share the cache */
    compare(d);
    Node *loose = el(d, el(d, NULL, "div", "a"), "div", NULL);   /* detached */
    el(d, el(d, loose, "section", "b"), "span", "a"); compare_tree(loose->parent);
    /* an absent name costs no walk; `> x` visits only children */
    SelList l = {0}; assert(css_parse_selector_list(":has(.nope), :has(> .q)", &l));
    uint64_t n0 = css_has_stats[1]; css_has_unfiltered = true; assert(!css_match_selector_list(&l, d->body));
    uint64_t full = css_has_stats[1] - n0; n0 = css_has_stats[1]; css_has_unfiltered = false; assert(!css_match_selector_list(&l, d->body));
    assert(css_has_stats[1] - n0 < full / 10 && css_match_selector_list(&l, main_el));
    css_sellist_free(&l);
    doc_free(d);
    puts(":has() name filter matches full subtree walks");
}
