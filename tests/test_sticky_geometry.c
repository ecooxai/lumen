#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>

static void near(float a, float b) { if (fabsf(a-b) > .1f) { fprintf(stderr, "expected %.1f, got %.1f\n", b, a); abort(); } }
static void check(Layout *l, Node *n, float top, float sx, float sy) {
    float r[4]; assert(jsg_vrect(n, r, sx, sy)); near(r[1], top);
    Box *hit = layout_hit(l, r[0]+10+sx, r[1]+5+sy, sx, sy);
    assert(hit && hit->node == n);
    DisplayList dl = {0}; dl_build(&dl, l, sx, sy, l->vw, l->vh);
    bool painted = false;
    for (int i = 0; i < dl.items.n; i++) {
        DItem *it = &dl.items.v[i];
        if (it->color == n->style->bg_color && it->w == n->box->w && it->h == n->box->h) {
            near(it->y, top); assert(it->fixed); painted = true;
        }
    }
    assert(painted); dl_clear(&dl); vec_free(dl.items);
}
int main(void) {
    dom_init();
    Document *d = doc_new("https://sticky.test/");
    const char *html = "<html><body><div id='s'><div id='a'></div><div id='b'></div><div id='f'></div></div></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0}#s{width:300px;height:200px;overflow:auto}#a{height:40px;position:sticky;top:0;background:#112233}#b{height:1000px}#f{height:30px;position:sticky;bottom:0;background:#223344}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true); Layout *l = layout_new(); layout_run(l,d,540,800);
    Node *s=doc_get_element_by_id(d,"s"), *a=doc_get_element_by_id(d,"a"), *f=doc_get_element_by_id(d,"f");
    /* Chromium reference: header 0/footer 170 at all four offsets. */
    float ys[] = {0,100,300,870};
    for (size_t i=0;i<ARRLEN(ys);i++) { jsg_set_scroll(s,0,ys[i]); check(l,a,0,0,0);check(l,f,170,0,0); }
    near(a->box->y,0); near(f->box->y,1040); /* layout stays in normal flow */
    node_set_attr(s,"style","overflow:visible;height:1070px");
    node_set_attr(a,"style","top:10px");
    style_recalc(e,&d->node,false);layout_run(l,d,540,800);
    check(l,a,10,0,100); check(l,f,770,0,100);
    /* Ancestor transforms must not change the page scroll used by hit tests. */
    node_set_attr(s,"style","overflow:visible;height:1070px;transform:translateY(20px)");
    style_recalc(e,&d->node,false);layout_run(l,d,540,800);
    check(l,a,30,0,100); check(l,f,790,0,100);
    /* A smaller containing block stops the header before its own bottom. */
    node_set_attr(s,"style","overflow:visible;height:200px");
    style_recalc(e,&d->node,false);layout_run(l,d,540,800);
    float r[4];assert(jsg_vrect(a,r,0,180));near(r[1],-20);
    /* An element taller than its scrollport keeps its start inset. */
    node_set_attr(s,"style","overflow:auto;height:20px");
    style_recalc(e,&d->node,false);layout_run(l,d,540,800);
    jsg_set_scroll(s,0,100);assert(jsg_vrect(a,r,0,0));near(r[1],10);
    /* Horizontal sticky offset agrees between geometry, painting and touch. */
    node_set_attr(s,"style","overflow:auto;height:200px");
    node_set_attr(a,"style","width:40px;margin-left:300px;left:10px;top:0");
    node_set_attr(doc_get_element_by_id(d,"b"),"style","width:800px");
    style_recalc(e,&d->node,false);layout_run(l,d,540,800);
    jsg_set_scroll(s,350,100);assert(jsg_vrect(a,r,0,0));near(r[0],10);check(l,a,0,0,0);
    layout_free(l); style_engine_free(e);doc_free(d);
    puts("Sticky scroll geometry, painting, hit tests and containing-block bounds passed");
}
