#include "css/css.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

int main(int argc, char **argv) {
    CHECK(argc == 3);
    FILE *input = fopen(argv[1], "rb");
    CHECK(input);
    char css[16384];
    size_t length = fread(css, 1, sizeof css - 1, input);
    CHECK(!ferror(input) && feof(input));
    fclose(input);
    css[length] = 0;
    dom_init();
    Document *document = doc_new("lumen://newtab");
    bool phone = !strcmp(argv[2], "phone");
    const char *html = phone
        ? "<html><body id=body class=phone><nav id=nav><h1 id=brand>Lumen</h1><a id=link>Home</a></nav><main id=main><a id=tile class=workspace><span id=icon class=workspace-icon><i></i><i></i><i></i><i></i></span></a></main></body></html>"
        : "<html><body id=body><nav id=nav><h1 id=brand>Lumen</h1><a id=link>Bookmarks</a></nav><main id=main><h2>Bookmarks</h2></main></body></html>";
    html_parse(document, html, strlen(html));
    StyleEngine *engine = style_engine_new(document);
    bool wide = !strcmp(argv[2], "wide");
    engine->media = (MediaCtx){wide || phone ? 740 : 336, 700, 2.75f, false};
    style_engine_add_sheet(engine, css_parse_sheet(css, length, document->url, 1, &engine->media));
    style_recalc(engine, &document->node, false);
    Node *body = doc_get_element_by_id(document, "body");
    Node *nav = doc_get_element_by_id(document, "nav");
    Node *main = doc_get_element_by_id(document, "main");
    Node *brand = doc_get_element_by_id(document, "brand");
    Node *link = doc_get_element_by_id(document, "link");
    CHECK(body && nav && main && brand && link);
    if (phone) {
        Node *tile = doc_get_element_by_id(document, "tile");
        Node *icon = doc_get_element_by_id(document, "icon");
        CHECK(tile && icon);
        CHECK(tile->style->width.px == 72 && tile->style->height.px == 72);
        CHECK(icon->style->width.px == 32 && icon->style->height.px == 32);
        CHECK(icon->style->display == D_BLOCK);
        CHECK(icon->style->margin[1].kind == LK_AUTO && icon->style->margin[3].kind == LK_AUTO);
        CHECK(body->style->display == D_BLOCK && nav->style->display == D_FLEX);
    }
    if (wide) {
        CHECK(body->style->display == D_FLEX);
        CHECK(nav->style->width.px == 190);
        CHECK(nav->style->min_height.px == 700);
        CHECK(main->style->padding[1].px == 40);
    } else {
        CHECK(body->style->display == D_BLOCK);
        CHECK(nav->style->display == D_FLEX);
        CHECK(nav->style->width.kind == LK_AUTO);
        CHECK(nav->style->min_height.px == 0);
        CHECK(brand->style->display == D_NONE);
        CHECK(main->style->padding[1].px == 16);
        CHECK(link->style->text_align == TA_CENTER);
        CHECK(link->style->min_height.px == 44);
        if (!strcmp(argv[2], "rotation")) {
            uint64_t version = document->dom_version;
            engine->media.vw = 740;
            engine->media.vh = 320;
            style_recalc(engine, &document->node, false);
            CHECK(body->style->display == D_FLEX);
            CHECK(nav->style->width.px == 190);
            CHECK(nav->style->min_height.px == 320);
            CHECK(brand->style->display != D_NONE);
            engine->media.vw = 336;
            engine->media.vh = 700;
            style_recalc(engine, &document->node, false);
            CHECK(body->style->display == D_BLOCK && nav->style->min_height.px == 0);
            CHECK(document->dom_version == version);
        }
    }
    style_engine_free(engine);
    doc_free(document);
    return 0;
}
