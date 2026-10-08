#include "../src/layout/layout.h"
#include "../src/net/net.h"
int main(int argc, char **argv) {
    dom_init(); net_init(4); font_init();
    NetResponse *r = net_fetch_sync(net_request_new("GET", argv[1]));
    Document *d = doc_new(r->url);
    html_parse(d, r->body, r->body_len);
    StyleEngine *e = style_engine_new(d);
    for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) {
        if (n->type != NODE_ELEMENT) continue;
        if (n->tag == A_style) { char *t = node_text_content(n); StyleSheet *s = css_parse_sheet(t, strlen(t), d->url, 1, &e->media); s->owner = n; style_engine_add_sheet(e, s); free(t); }
        else if (n->tag == A_link && node_attr(n, "rel") && strstr(node_attr(n, "rel"), "stylesheet") && node_attr(n, "href")) { char *u = url_join(d->url, node_attr(n, "href")); NetResponse *cr = net_fetch_sync(net_request_new("GET", u)); StyleSheet *s = css_parse_sheet(cr->body, cr->body_len, cr->url, 1, &e->media); s->owner = n; style_engine_add_sheet(e, s); free(u); }
    }
    style_recalc(e, &d->node, true);
    Layout *L = layout_new();
    layout_run(L, d, 1280, 800);
    double t0 = now_ms(); layout_run(L, d, 1280, 800); double t1 = now_ms();
    printf("layout %d boxes: %.1fms (warm %.1fms) doc %.0fx%.0f arena %zuKB\n", L->nboxes, L->ms, t1 - t0, L->doc_w, L->doc_h, L->arena.total / 1024);
    if (argc > 2) layout_dump(L->root, 0, atoi(argv[2]));
    return 0;
}
