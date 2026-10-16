/* Inset-only style changes translate laid-out boxes; the result must match a full layout. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { Node *n; float x, y, w, h, sw, sh, fx, fy; } Geo;
static int snap(Node *n, Geo *g, int k) {
    for (Node *c = n->first; c; c = c->next) {
        if (c->type == NODE_ELEMENT && c->box) {
            Box *b = c->box;
            g[k++] = (Geo){ c, b->x, b->y, b->w, b->h, b->scroll_w, b->scroll_h, b->nfrags ? b->frags[0].x : 0, b->nfrags ? b->frags[0].y : 0 };
        }
        k = snap(c, g, k);
    }
    return k;
}
static Geo A[256], B[256];
static StyleEngine *e; static Document *d; static Layout *l;

/* Applies the change through the move path; returns whether every move applied. */
static bool moved(const char *id, const char *style) {
    node_set_attr(doc_get_element_by_id(d, id), "style", style);
    e->allow_moves = true; style_recalc(e, &d->node, false); e->allow_moves = false;
    bool ok = !e->layout_dirty && e->nmoves > 0;
    if (getenv("DBG")) fprintf(stderr, "%s dirty=%d moves=%d\n", id, e->layout_dirty, e->nmoves);
    for (int i = 0; i < e->nmoves && ok; i++) ok = layout_move(l, e->moves[i].node, e->moves[i].old);
    e->nmoves = 0; e->layout_dirty = false;
    if (!ok) { layout_run(l, d, 540, 400); return false; }
    /* compare against a full layout */
    int na = snap(&d->node, A, 0); float dw = l->doc_w, dh = l->doc_h;
    layout_run(l, d, 540, 400);
    int nb = snap(&d->node, B, 0);
    assert(na == nb && dw == l->doc_w && dh == l->doc_h);
    for (int i = 0; i < na; i++) {
        if (A[i].n != B[i].n || fabsf(A[i].x - B[i].x) > .01f || fabsf(A[i].y - B[i].y) > .01f || A[i].w != B[i].w || A[i].h != B[i].h ||
            fabsf(A[i].sw - B[i].sw) > .01f || fabsf(A[i].sh - B[i].sh) > .01f || fabsf(A[i].fx - B[i].fx) > .01f || fabsf(A[i].fy - B[i].fy) > .01f) {
            fprintf(stderr, "%s: <%s id=%s> move (%g,%g %gx%g s%gx%g) layout (%g,%g %gx%g s%gx%g)\n", id, A[i].n->tag, node_attr(A[i].n, "id"),
                A[i].x, A[i].y, A[i].w, A[i].h, A[i].sw, A[i].sh, B[i].x, B[i].y, B[i].w, B[i].h, B[i].sw, B[i].sh);
            assert(0);
        }
    }
    return true;
}

int main(void) {
    dom_init(); font_init();
    d = doc_new("https://inset-move.test/");
    const char *html = "<html><body><div id='box'>"
        "<div id='rel' style='top:4px'>relative <span id='in'>text</span><div id='inner' style='position:absolute;top:2px'>abs</div></div>"
        "<div id='pct' style='top:10%'>percent</div>"
        "<div id='abs' style='top:20px'>abs auto width</div>"
        "<div id='absw' style='left:10px;top:5px'>abs fixed width</div>"
        "<div id='both' style='left:1px;right:1px'>both</div>"
        "<div id='stat' style='top:3px'>static</div>"
        "<div id='fix' style='bottom:0'>fixed</div>"
        "<span id='span' style='left:2px'>inline relative</span>"
        "</div><div id='scroller'><div id='kid' style='top:0'>scrolled</div></div></body></html>";
    html_parse(d, html, strlen(html));
    e = style_engine_new(d);
    const char *css = "body{margin:0;font:16px sans-serif}#box{position:relative;height:300px}#rel,#pct,#kid,#span{position:relative}"
        "#abs,#absw,#both{position:absolute}#absw{width:120px}#fix{position:fixed}"
        "#scroller{height:100px;overflow:auto}#kid{height:80px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    l = layout_new(); layout_run(l, d, 540, 400); e->layout_dirty = false;

    assert(moved("rel", "top:40px;left:7px"));           /* relative, abs child follows */
    assert(moved("rel", "bottom:3px"));                   /* top -> bottom on a relative box */
    assert(moved("pct", "top:25%"));                      /* percentage of the containing block */
    assert(moved("abs", "top:90px"));                     /* vertical move of a shrink-to-fit box */
    assert(moved("abs", "top:-500px"));                   /* overflow shrinks above the document */
    assert(!moved("abs", "top:90px;left:3px"));           /* left would narrow the shrink-to-fit width */
    assert(moved("absw", "left:60px;top:5px"));           /* definite width can move horizontally */
    assert(moved("absw", "left:60px;top:1000px"));        /* extends the document height */
    assert(!moved("absw", "right:3px;top:1000px"));       /* left switched to right */
    assert(!moved("both", "left:5px;right:1px"));         /* both insets size the box */
    assert(moved("stat", "top:90px"));                    /* static ignores insets */
    assert(moved("fix", "bottom:30px"));
    assert(moved("span", "left:9px;top:3px"));            /* relative inline box */
    assert(moved("kid", "top:200px"));                    /* grows the scroller's range */
    Node *sc = doc_get_element_by_id(d, "scroller");
    sc->scroll_y = 150;
    assert(moved("kid", "top:0"));                        /* shrinks it: the scroll offset is clamped */
    assert(sc->scroll_y == 0);
    layout_free(l); style_engine_free(e); doc_free(d);
    puts("inset move: ok");
    return 0;
}
