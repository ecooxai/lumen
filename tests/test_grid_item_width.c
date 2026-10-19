/* A grid item's percentage width resolves against its grid area (chatgpt.com's modal dialogs),
   also with minified track lists that put no space after a function: minmax(0,1fr)10px. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static Document *d;
static Box *box(const char *id) { Node *n = doc_get_element_by_id(d, id); assert(n && n->box); return n->box; }

int main(void) {
    dom_init(); font_init();
    d = doc_new("https://grid-item.test/");
    const char *html = "<html><body>"
        "<div class=fix><div class=g id=g1><div class=dlg id=a><span><header>Settings</header></span></div></div></div>"
        "<div class=fix><div class=g><div id=b style='grid-column-start:2;grid-row-start:2;width:100%'>x</div></div></div>"
        "<div class=fix><div class=g><div id=c style='grid-column:2;grid-row:2;width:100%'>x</div></div></div>"
        "<div class=fix><div class=g style='grid-template-columns:10px 1fr 10px'><div id=e style='grid-column:2;grid-row:2;width:100%'>x</div></div></div>"
        "<div class=fix><div class=g id=gm style='grid-template-columns:10px minmax(0,1fr)10px;grid-template-rows:minmax(20px,.8fr)auto minmax(20px,1fr)'>"
        "<div id=m style='grid-column:2;grid-row:2;width:100%'>x</div></div></div>"
        "</body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0;font:16px sans-serif}.fix{position:fixed;inset:0}"
        ".g{height:100%;width:100%;overflow-y:auto;display:grid;grid-template-columns:10px minmax(0,1fr) 10px;"
        "grid-template-rows:minmax(20px,.8fr) auto minmax(20px,1fr)}"
        ".dlg{position:relative;grid-column:auto;grid-column-start:2;grid-row:auto;grid-row-start:2;height:600px;width:100%;max-width:680px;"
        "inset-inline-start:50%;transform:translateX(-50%);display:flex;flex-direction:column;overflow:hidden;max-height:85vh}"
        "header{display:flex;justify-content:space-between}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 1080, 540);
    const char *ids[] = {"a", "b", "c", "e", "m"};
    int bad = 0;
    for (int i = 0; i < 5; i++) {
        Box *b = box(ids[i]);
        float want = i ? 1060 : 680;
        fprintf(stderr, "%s: x %g w %g h %g (want w %g)\n", ids[i], b->x, b->w, b->h, want);
        bad += fabsf(b->w - want) > .5f;
    }
    Node *gm = doc_get_element_by_id(d, "gm");
    char *cols = css_get_computed_value(gm, "grid-template-columns"), *rows = css_get_computed_value(gm, "grid-template-rows");
    fprintf(stderr, "minified: [%s] [%s]\n", cols, rows);
    assert(!strcmp(cols, "10px 1fr 10px") && !strcmp(rows, "minmax(20px, 0.8fr) auto minmax(20px, 1fr)"));
    free(cols); free(rows);
    assert(!bad);
    return 0;
}
