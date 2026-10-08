/* Headless JS harness: load a page, run its classic scripts and event loop for N ms */
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include "../src/paint/paint.h"
#include "../src/net/net.h"
#include "../src/base/url.h"
#include "../src/js/js.h"
#include "../src/js/jsglue.h"
#include "../src/media/media.h"

static void vp(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) {
    (void)ud; *w = 1280; *h = 800; *sx = *sy = 0; *dpr = 1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: jsrun URL|FILE [ms]\n"); return 2; }
    double ms = argc > 2 ? atof(argv[2]) : 1000;
    dom_init(); net_init(4); font_init(); js_global_init(argv[0]);
    char url[PATH_MAX + 16];
    char abs[PATH_MAX];
    if (strstr(argv[1], "://")) snprintf(url, sizeof url, "%s", argv[1]);
    else if (realpath(argv[1], abs)) snprintf(url, sizeof url, "file://%s", abs);
    else { fprintf(stderr, "jsrun: cannot resolve %s\n", argv[1]); return 1; }
    NetResponse *r = net_fetch_sync(net_request_new("GET", url));
    if (!r || r->status == 0) { fprintf(stderr, "jsrun: fetch failed: %s\n", r && r->error ? r->error : "?"); return 1; }
    Document *d = doc_new(r->url ? r->url : url);
    html_parse(d, r->body ? r->body : "", r->body ? r->body_len : 0);
    net_response_free(r);
    StyleEngine *se = style_engine_new(d); se->media.vw = 1280; se->media.vh = 800;
    for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) {
        if (n->type != NODE_ELEMENT || n->ns != NS_HTML) continue;
        const char *rel = node_attr(n, "rel");
        if (n->tag == A_style) {
            char *t = node_text_content(n); StyleSheet *s = css_parse_sheet(t, strlen(t), d->url, 1, &se->media); s->owner = n; style_engine_add_sheet(se, s); free(t);
        } else if (n->tag == A_link && rel && strstr(rel, "stylesheet") && node_attr(n, "href")) {
            char *u = url_join(d->url, node_attr(n, "href"));
            NetResponse *cr = u ? net_fetch_sync(net_request_new("GET", u)) : NULL;
            if (cr && cr->status == 200) { StyleSheet *s = css_parse_sheet(cr->body, cr->body_len, cr->url, 1, &se->media); s->owner = n; style_engine_add_sheet(se, s); }
            if (cr) net_response_free(cr);
            free(u);
        }
    }
    style_recalc(se, &d->node, true);
    uint64_t seen_ver = d->dom_version;
    JsHost h = {0};
    h.viewport = vp;
    double t0 = now_ms();
    JsCtx *js = js_new(d, &h);
    double t1 = now_ms();
    if (getenv("JSRUN_PRE")) js_eval(js, getenv("JSRUN_PRE"), "jsrun:pre");
    int nscripts = 0;
    for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) {
        if (n->type != NODE_ELEMENT || n->tag != A_script || n->ns != NS_HTML || (n->flags & NF_SCRIPT_STARTED) || !jsg_classic_script(n)) continue;
        nscripts++;
        const char *src = node_attr(n, "src");
        if (src) {
            char *u = url_join(d->url, src);
            NetResponse *sr = u ? net_fetch_sync(net_request_new("GET", u)) : NULL;
            if (sr && sr->status >= 200 && sr->status < 300) js_run_script(js, n, sr->body ? sr->body : "", sr->body_len, u);
            else { n->flags |= NF_SCRIPT_STARTED; js_dispatch(js, n, "error", "Event", false, false, 0, 0, 0, NULL); }
            if (sr) net_response_free(sr);
            free(u);
        } else {
            char *t = node_text_content(n);
            js_run_script(js, n, t, strlen(t), d->url);
            free(t);
        }
    }
    js_set_ready_state(js, 1);
    js_dispatch(js, &d->node, "DOMContentLoaded", "Event", true, false, 0, 0, 0, NULL);
    js_set_ready_state(js, 2);
    js_dispatch_window(js, "load");
    double t2 = now_ms();
    double end = t2 + ms;
    const char *until = getenv("JSRUN_UNTIL_ATTR");   /* stop early once <html> carries this attribute */
    while (now_ms() < end) {
        if (until && d->html && node_attr(d->html, until)) break;
        net_poll();
        media_tick();
        js_tick(js);
        if (d->dom_version != seen_ver) { seen_ver = d->dom_version; style_recalc(se, &d->node, false); }
        double dl = js_next_deadline(js) - now_ms();
        if (dl > 5) dl = 5;
        if (dl > 0) usleep((useconds_t)(dl * 1000));
    }
    fprintf(stderr, "jsrun: context %.1fms, %d scripts %.1fms, dom_version %llu\n", t1 - t0, nscripts, t2 - t1, (unsigned long long)d->dom_version);
    if (getenv("JSRUN_DUMP") && d->body) { char *s = node_serialize(d->body, true); puts(s); free(s); }
    if (getenv("JSRUN_POST")) js_eval(js, getenv("JSRUN_POST"), "jsrun:post");
    js_free(js);
    style_engine_free(se);
    doc_free(d);
    return 0;
}
