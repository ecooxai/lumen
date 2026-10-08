#include "../src/css/css.h"
#include "../src/net/net.h"
int main(int argc, char **argv) {
    dom_init(); net_init(4);
    NetRequest *rq = net_request_new("GET", argv[1]);
    NetResponse *r = net_fetch_sync(rq);
    Document *d = doc_new(r->url);
    double t0 = now_ms();
    html_parse(d, r->body, r->body_len);
    double t1 = now_ms();
    StyleEngine *e = style_engine_new(d);
    int nsheets = 0; size_t css_bytes = 0;
    for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) {
        if (n->type != NODE_ELEMENT) continue;
        if (n->tag == A_style) { char *t = node_text_content(n); StyleSheet *s = css_parse_sheet(t, strlen(t), d->url, 1, &e->media); s->owner = n; style_engine_add_sheet(e, s); css_bytes += strlen(t); free(t); nsheets++; }
        else if (n->tag == A_link && node_attr(n, "rel") && strstr(node_attr(n, "rel"), "stylesheet") && node_attr(n, "href")) {
            char *u = url_join(d->url, node_attr(n, "href")); NetRequest *q = net_request_new("GET", u); NetResponse *cr = net_fetch_sync(q);
            StyleSheet *s = css_parse_sheet(cr->body, cr->body_len, cr->url, 1, &e->media); s->owner = n; style_engine_add_sheet(e, s); css_bytes += cr->body_len; nsheets++; free(u);
        }
    }
    double t2 = now_ms();
    style_recalc(e, &d->node, true);
    double t3 = now_ms();
    int rules = 0; for (int i = 0; i < e->sheets.n; i++) rules += e->sheets.v[i]->rules.n;
    printf("parse %.1fms, %d sheets (%zu bytes, %d rules) parse+fetch %.1fms, style %.1fms (%d elements)\n", t1 - t0, nsheets, css_bytes, rules, t2 - t1, t3 - t2, e->stats_matched);
    if (argc > 2) { NodeVec out = {0}; css_query(&d->node, argv[2], true, &out); for (int i = 0; i < out.n && i < 10; i++) { char *dsp = css_get_computed_value(out.v[i], "display"); char *col = css_get_computed_value(out.v[i], "color"); char *bg = css_get_computed_value(out.v[i], "background-color"); char *fs = css_get_computed_value(out.v[i], "font-size"); printf("  <%s id=%s class=%.30s> display=%s color=%s bg=%s fs=%s\n", out.v[i]->tag, out.v[i]->id ? out.v[i]->id : "", node_attr(out.v[i], "class") ? node_attr(out.v[i], "class") : "", dsp, col, bg, fs); } }
    return 0;
}
