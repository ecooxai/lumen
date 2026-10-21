/* Rounded clips: an image with border-radius: 50% (Google's avatars) and the content of a rounded
   overflow: hidden box are clipped to their corners (both painted square before); an iframe with
   frameborder=0 has no border; an outline follows the rounded corners. */
#include "../src/paint/paint.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static uint32_t px[4];
static Image img = { 2, 2, 1, px, 0 };
static Image *node_img(Node *n) { return n->tag == A_img ? &img : NULL; }

/* the clip pushed just before the first item of op */
static const DItem *clip_before(const DisplayList *dl, int op) {
    const DItem *c = NULL;
    for (int i = 0; i < dl->items.n; i++) {
        const DItem *it = &dl->items.v[i];
        if (it->op == DO_PUSH_CLIP) c = it;
        else if (it->op == DO_POP_CLIP) c = NULL;
        else if (it->op == op) return c;
    }
    return NULL;
}

int main(void) {
    dom_init();
    paint_image_hook = node_img;
    Document *d = doc_new("https://round-clip.test/");
    const char *html = "<html><body><img id=a width=40 height=40><div id=o><p id=t></p></div>"
        "<iframe id=f frameborder=0></iframe><iframe id=g></iframe></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0}#a{display:block;border-radius:50%;outline:2px solid #888}"
        "#o{width:100px;height:60px;overflow:hidden;border:4px solid;border-radius:20px}#t{margin:0;height:200px;background:red}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 540, 800);
    DisplayList dl = {0}; dl_build(&dl, l, 0, 0, 540, 800);
    const DItem *c = clip_before(&dl, DO_IMAGE);
    assert(c && fabsf(c->r[0] - 20) < .01f && fabsf(c->r[2] - 20) < .01f);
    c = NULL;
    for (int i = 0; i < dl.items.n; i++) if (dl.items.v[i].op == DO_PUSH_CLIP && dl.items.v[i].sc) c = &dl.items.v[i];
    assert(c && fabsf(c->r[0] - 16) < .01f && fabsf(c->r[3] - 16) < .01f);   /* 20px outer radius less the 4px border */
    c = NULL;
    for (int i = 0; i < dl.items.n; i++) if (dl.items.v[i].op == DO_BORDER && fabsf(dl.items.v[i].x + 2) < .01f) c = &dl.items.v[i];
    assert(c && fabsf(c->r[0] - 22) < .01f && fabsf(c->r[2] - 22) < .01f);   /* the 20px radius grown by the 2px outline */
    Node *f = doc_get_element_by_id(d, "f"), *g = doc_get_element_by_id(d, "g");
    assert(f->style->border_width[0] == 0 && g->style->border_width[0] == 2);
    char *v[5] = { css_get_computed_value(g, "border-top-width"), css_get_computed_value(g, "border-left-style"),
                   css_get_computed_value(f, "border-top-width"), css_get_computed_value(doc_get_element_by_id(d, "o"), "border-bottom-right-radius"),
                   css_get_computed_value(f, "box-shadow") };
    assert(!strcmp(v[0], "2px") && !strcmp(v[1], "inset") && !strcmp(v[2], "0px") && !strcmp(v[3], "20px") && !strcmp(v[4], "none"));
    for (int i = 0; i < 5; i++) free(v[i]);
    dl_clear(&dl); free(dl.items.v); layout_free(l); style_engine_free(e); doc_free(d);
    puts("Rounded image, outline and overflow clips passed");
}
