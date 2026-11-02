/* Fixed descendants follow translations of their actual transformed containing
 * block. Moving an ordinary ancestor leaves viewport/external-CB boxes alone. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
extern int g_lb_hits;
static void position(Node *node, float x, float y) {
    assert(node && node->box);
    Box *box = node->box;
    if (fabsf(box->x - x) > .01f || fabsf(box->y - y) > .01f)
        fprintf(stderr, "%s: native box (%g,%g), expected (%g,%g)\n", node_attr(node, "id"), box->x, box->y, x, y);
    assert(fabsf(box->x - x) <= .01f && fabsf(box->y - y) <= .01f); checks += 2;
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://fixed-containing-block.test/");
    const char *html = "<html><body><div id='scope'>"
        "<div id='local'></div><div id='middle'><div id='nested'></div></div>"
        "<div id='moving'><div id='external'></div></div></div>"
        "<div id='ordinary'><div id='viewport'></div></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc);
    const char *css = "body{margin:0}"
        "#scope{position:absolute;left:100px;top:80px;transform:scale(1);width:200px;height:120px;padding:10px;border:5px solid}"
        "#local,#nested,#external,#viewport{position:fixed;width:10px;height:10px}"
        "#local{left:20px;top:30px}#nested{left:40px;top:50px}#external{left:60px;top:70px}"
        "#middle{position:relative;left:7px;top:11px;width:50px;height:30px}"
        "#moving{position:relative;left:70px;top:20px;width:30px;height:30px}"
        "#ordinary{position:absolute;left:350px;top:60px;width:40px;height:40px}"
        "#viewport{left:13px;top:17px}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    style_recalc(engine, &doc->node, true);
    Layout *layout = layout_new(); layout_run(layout, doc, 640, 480);
    Node *scope = doc_get_element_by_id(doc, "scope"), *local = doc_get_element_by_id(doc, "local");
    Node *nested = doc_get_element_by_id(doc, "nested"), *external = doc_get_element_by_id(doc, "external");
    Node *viewport = doc_get_element_by_id(doc, "viewport");
    position(scope, 100, 80);
    position(local, 125, 115); position(nested, 145, 135); position(external, 165, 155);
    position(viewport, 13, 17);
    assert(local->box->cb == scope->box && nested->box->cb == scope->box && external->box->cb == scope->box);
    assert(viewport->box->cb == layout->root); checks += 2;

    /* Reuse a measured subtree at a new position. This runs both native
       translation and the memo replay that restores absolute registrations. */
    Box *cached = scope->box;
    assert(cached->memo);
    int hits = g_lb_hits;
    layout_box(layout, cached, cached->x + 37 - cached->mk[0], cached->y + 53 - cached->mk[1],
               cached->mk[2], cached->mk[3], NULL, cached->mmode, cached->mk[4], cached->mk[5]);
    assert(g_lb_hits == hits + 1); checks++;
    position(local, 162, 168); position(nested, 182, 188); position(external, 202, 208);
    position(viewport, 13, 17);

    /* Neither an ordinary intermediate ancestor nor one outside the fixed
       containing block may drag these independently positioned descendants. */
    box_translate(doc_get_element_by_id(doc, "middle")->box, 19, 23);
    position(nested, 182, 188);
    box_translate(doc_get_element_by_id(doc, "moving")->box, 11, 29);
    position(external, 202, 208);
    box_translate(doc_get_element_by_id(doc, "ordinary")->box, 31, 41);
    position(viewport, 13, 17);

    /* Even translating the layout root must not move viewport-fixed boxes.
       The contained transformed-CB descendants still follow negative deltas. */
    box_translate(layout->root, -17, -19);
    position(local, 145, 149); position(nested, 165, 169); position(external, 185, 189);
    position(viewport, 13, 17);

    node_set_attr(scope, "style", "left:140px;top:120px");
    style_recalc(engine, &doc->node, false); layout_run(layout, doc, 640, 480);
    position(local, 165, 155); position(nested, 185, 175); position(external, 205, 195);
    position(viewport, 13, 17);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS fixed_containing_block %u native checks\n", checks);
    return 0;
}
