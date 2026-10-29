/* getElementById stays right while elements connect, disconnect, change id and share ids,
   without rebuilding its map after every mutation; querySelector(All) answers ID subjects from it
   exactly as a tree walk would. */
#include "../src/dom/dom.h"
#include "../src/css/css.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Node *el(Document *d, const char *id) { Node *n = node_new_element(d, "div", NS_HTML); if (id) node_set_attr(n, "id", id); return n; }

/* css_query against matching every element under root in tree order */
static void check_query(Node *root, const char *sel) {
    SelList l = {0}; assert(css_parse_selector_list(sel, &l));
    NodeVec want = {0};
    for (Node *n = root->first; n; n = node_next_in_tree(n, root)) {
        bool m = false;
        for (int i = 0; i < l.n && !m; i++) m = n->type == NODE_ELEMENT && css_match_selector(&l.v[i], n, root->type == NODE_DOCUMENT ? NULL : root);
        if (m) vec_push(want, n);
    }
    NodeVec got = {0};
    Node *first = css_query(root, sel, false, NULL);
    css_query(root, sel, true, &got);
    if (got.n != want.n || first != (want.n ? want.v[0] : NULL)) { fprintf(stderr, "query %s: %d vs %d\n", sel, got.n, want.n); assert(0); }
    for (int i = 0; i < got.n; i++) assert(got.v[i] == want.v[i]);
    free(got.v); free(want.v); css_sellist_free(&l);
}
static void check_queries(Document *d, Node *scope) {
    static const char *sels[] = { "#a", "#b", "#dup", "#c", "#c2", "#c3", "#loose2", "div#a", "span#a", "div > #b", "#a #b", "body #b", "#b:first-child", "#nope", "p#dup", ".x#b" };
    for (size_t i = 0; i < sizeof sels / sizeof *sels; i++) { check_query(&d->node, sels[i]); if (scope) check_query(scope, sels[i]); }
}

int main(void) {
    dom_init();
    Document *d = doc_new("https://id-map.test/");
    const char *html = "<html><body><div id=a><span id=b></span></div><p id=dup></p><p id=dup></p></body></html>";
    html_parse(d, html, strlen(html));
    Node *a = doc_get_element_by_id(d, "a"), *b = doc_get_element_by_id(d, "b"), *dup = doc_get_element_by_id(d, "dup");
    assert(a && b && dup && dup->next && dup->next->id && !strcmp(dup->next->id, "dup"));
    Node *body = a->parent;
    check_queries(d, a); check_queries(d, body);
    /* insert a subtree: its ids appear; detached elements never do */
    Node *c = el(d, "c"), *c2 = el(d, "c2"); node_append(c, c2);
    Node *loose = el(d, "loose");
    node_append(body, c);
    assert(doc_get_element_by_id(d, "c") == c && doc_get_element_by_id(d, "c2") == c2 && !doc_get_element_by_id(d, "loose"));
    /* id changes */
    node_set_attr(c2, "id", "c3");
    assert(!doc_get_element_by_id(d, "c2") && doc_get_element_by_id(d, "c3") == c2);
    node_remove_attr(c2, "id");
    assert(!doc_get_element_by_id(d, "c3"));
    node_set_attr(loose, "id", "loose2");
    assert(!doc_get_element_by_id(d, "loose2"));
    check_queries(d, c); check_queries(d, loose);
    /* removal */
    node_remove(a);
    assert(!doc_get_element_by_id(d, "a") && !doc_get_element_by_id(d, "b"));
    /* duplicates: removing the first exposes the second; an earlier duplicate wins */
    Node *dup2 = dup->next;
    node_remove(dup);
    assert(doc_get_element_by_id(d, "dup") == dup2);
    check_queries(d, body); check_queries(d, a);
    Node *early = el(d, "c");
    node_insert_before(body, early, body->first);
    assert(doc_get_element_by_id(d, "c") == early);
    check_queries(d, early);
    node_remove(early);
    assert(doc_get_element_by_id(d, "c") == c);
    /* re-inserting a removed subtree brings its ids back */
    node_append(body, a);
    assert(doc_get_element_by_id(d, "b") == b);
    check_queries(d, a); check_queries(d, b);
    puts("id map ok");
    return 0;
}
