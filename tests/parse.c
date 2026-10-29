#include "../src/dom/dom.h"
#include "../src/net/net.h"
int main(int argc, char **argv) {
    dom_init(); net_init(2);
    const char *u = argv[1];
    NetResponse *r;
    if (strstr(u, "://")) { NetRequest *rq = net_request_new("GET", u); r = net_fetch_sync(rq); }
    else { r = xcalloc(1, sizeof *r); r->body = read_file(u, &r->body_len); }
    double t = now_ms();
    Document *d = doc_new(u);
    html_parse(d, r->body, r->body_len);
    double el = now_ms() - t;
    int count = 0; for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) count++;
    fprintf(stderr, "parsed %zu bytes in %.1f ms, %d nodes, title=%s\n", r->body_len, el, count, d->title ? d->title : "");
    if (argc > 2) dom_dump(&d->node, 0, stdout);
    return 0;
}
