#include "../src/paint/paint.h"
#include "../src/net/net.h"
static HMap icache;
static Image *load_url(const char *u) {
    if (!u || !*u) return NULL;
    Image **c = (Image **)hm_get(&icache, u); if (c) return *c;
    NetResponse *r = net_fetch_sync(net_request_new("GET", u));
    Image *im = r && r->status == 200 ? image_decode((const uint8_t *)r->body, r->body_len) : NULL;
    Image **slot = xmalloc(sizeof *slot); *slot = im; hm_put(&icache, u, slot);
    return im;
}
static Image *node_img(Node *n) { const char *s = node_attr(n, n->tag == A_video ? "poster" : "src"); if (!s) return NULL; char *u = url_join(n->doc->url, s); Image *im = load_url(u); free(u); return im; }
static bool img_size(Node *n, float *w, float *h) { Image *im = node_img(n); if (!im) return false; *w = image_css_w(im); *h = image_css_h(im); return true; }
/* usage: render URL out.png [width] [height] [scroll_y] */
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: render URL out.png [w] [h] [scroll]\n"); return 1; }
    float W = argc > 3 ? (float)atof(argv[3]) : 1280, H = argc > 4 ? (float)atof(argv[4]) : 800, sy = argc > 5 ? (float)atof(argv[5]) : 0;
    dom_init(); net_init(4); font_init();
    double t0 = now_ms();
    NetResponse *r = net_fetch_sync(net_request_new("GET", argv[1]));
    Document *d = doc_new(r->url);
    html_parse(d, r->body, r->body_len);
    StyleEngine *e = style_engine_new(d);
    e->media.vw = W; e->media.vh = H;
    for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) {
        if (n->type != NODE_ELEMENT) continue;
        if (n->tag == A_style) { char *t = node_text_content(n); StyleSheet *s = css_parse_sheet(t, strlen(t), d->url, 1, &e->media); s->owner = n; style_engine_add_sheet(e, s); free(t); }
        else if (n->tag == A_link && node_attr(n, "rel") && strstr(node_attr(n, "rel"), "stylesheet") && node_attr(n, "href")) { char *u = url_join(d->url, node_attr(n, "href")); NetResponse *cr = net_fetch_sync(net_request_new("GET", u)); StyleSheet *s = css_parse_sheet(cr->body, cr->body_len, cr->url, 1, &e->media); s->owner = n; style_engine_add_sheet(e, s); free(u); }
    }
    paint_image_hook = node_img; paint_url_image_hook = load_url; layout_image_size_hook = img_size;
    double t1 = now_ms();
    style_recalc(e, &d->node, true);
    double t2 = now_ms();
    Layout *L = layout_new(); layout_run(L, d, W, H);
    double t3 = now_ms();
    DisplayList dl = {0}; dl_build(&dl, L, 0, sy, W, H);
    double t4 = now_ms();
    { double area[12] = {0}; int cnt[12] = {0};
      for (int i = 0; i < dl.items.n; i++) { DItem *it = &dl.items.v[i]; cnt[it->op]++; float w = LCLAMP(it->w, 0, W), h = LCLAMP(it->h, 0, H); area[it->op] += (double)w * h; if (it->op == DO_PUSH_LAYER) area[it->op] += W * H; }
      for (int k = 0; k < 11; k++) if (cnt[k]) fprintf(stderr, "op%d: %d items, %.1f Mpx\n", k, cnt[k], area[k] * 4 / 1e6); }
    Canvas c; canvas_init(&c, (int)(W * 2), (int)(H * 2), 2);
    raster(&c, &dl, RGBA(255, 255, 255, 255));
    double t5 = now_ms();
    png_write(argv[2], c.px, c.w, c.h, c.stride);
    printf("fetch+parse %.0fms style %.1fms layout %.1fms (%d boxes) display %.1fms (%d items) raster %.1fms doc %.0fx%.0f\n", t1 - t0, t2 - t1, t3 - t2, L->nboxes, t4 - t3, dl.items.n, t5 - t4, L->doc_w, L->doc_h);
    return 0;
}
