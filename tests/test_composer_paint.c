/* Engine-level reproduction of an inherited round button background and an
 * SVG symbol whose currentColor comes from each use instance. No site assets. */
#include "../src/paint/paint.h"
#include <math.h>
#include <stdio.h>

static int checks, failures;
#define CHECK(name, condition) do { ++checks; if (!(condition)) { \
    ++failures; fprintf(stderr, "FAIL %s (line %d)\n", name, __LINE__); \
} } while (0)
static bool near(float a, float b) { return fabsf(a - b) < .02f; }
static bool color_near(Color a, Color b) {
    return abs((int)COLOR_A(a) - (int)COLOR_A(b)) <= 2 &&
        abs((int)COLOR_R(a) - (int)COLOR_R(b)) <= 2 &&
        abs((int)COLOR_G(a) - (int)COLOR_G(b)) <= 2 &&
        abs((int)COLOR_B(a) - (int)COLOR_B(b)) <= 2;
}
static Color center(Node *svg) {
    Image *im = svg_image(svg, 20, 20, 1);
    CHECK("SVG image exists at the requested dimensions", im && im->w == 20 && im->h == 20 && im->px);
    return im && im->px ? im->px[10 * im->w + 10] : 0;
}
static void radius_keywords(void) {
    ComputedStyle *parent = style_new_default(), *child = style_new_default();
    const char *corners[] = {"border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius", "border-bottom-left-radius"};
    css_apply_decl(parent, NULL, "border-radius", "11px 22% 33px 44%", NULL, NULL);
    css_apply_decl(child, parent, "border-radius", "inherit", NULL, NULL);
    for (int i = 0; i < 4; i++) CHECK("shorthand inherits each computed corner", near(child->border_radius[i].px, parent->border_radius[i].px) && near(child->border_radius[i].pct, parent->border_radius[i].pct));
    css_apply_decl(child, parent, "border-radius", "initial", NULL, NULL);
    for (int i = 0; i < 4; i++) CHECK("initial clears every radius", near(child->border_radius[i].px, 0) && near(child->border_radius[i].pct, 0));
    for (int i = 0; i < 4; i++) {
        css_apply_decl(child, parent, corners[i], "inherit", NULL, NULL);
        CHECK("longhand inherits its own corner", near(child->border_radius[i].px, parent->border_radius[i].px) && near(child->border_radius[i].pct, parent->border_radius[i].pct));
        css_apply_decl(child, parent, corners[i], "unset", NULL, NULL);
        CHECK("unset resets a non-inherited corner", near(child->border_radius[i].px, 0) && near(child->border_radius[i].pct, 0));
    }
    css_apply_decl(child, parent, "border-radius", "9px", NULL, NULL);
    css_apply_decl(child, parent, "border-radius", "unset", NULL, NULL);
    for (int i = 0; i < 4; i++) CHECK("unset resets the shorthand", near(child->border_radius[i].px, 0) && near(child->border_radius[i].pct, 0));
    style_free(child); style_free(parent);
}

static void gradient_inheritance(void) {
    Document *doc = doc_new("https://gradient-inheritance.test/");
    const char *html = "<html><body><svg style='display:none'><defs><linearGradient id='gradient'>"
        "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue'/></linearGradient></defs></svg>"
        "<svg id='direct-fill'><rect width='20' height='20' fill='url(#gradient)'/></svg>"
        "<svg id='group-fill'><g fill='url(#gradient)'><rect width='20' height='20'/></g></svg>"
        "<svg id='direct-stroke'><rect x='3' y='3' width='14' height='14' fill='none' stroke='url(#gradient)' stroke-width='3'/></svg>"
        "<svg id='group-stroke'><g fill='none' stroke='url(#gradient)' stroke-width='3'><rect x='3' y='3' width='14' height='14'/></g></svg>"
        "</body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *style = style_engine_new(doc); style_recalc(style, &doc->node, true);
    const char *direct_ids[] = {"direct-fill", "direct-stroke"}, *group_ids[] = {"group-fill", "group-stroke"};
    for (int kind = 0; kind < 2; kind++) {
        Image *direct = svg_image(doc_get_element_by_id(doc, direct_ids[kind]), 20, 20, 1);
        Image *group = svg_image(doc_get_element_by_id(doc, group_ids[kind]), 20, 20, 1);
        CHECK("gradient reference and inherited images exist", direct && group && direct->px && group->px);
        if (!direct || !group || !direct->px || !group->px) continue;
        int y = kind ? 3 : 10, left = kind ? 5 : 3, right = kind ? 15 : 16;
        Color a = direct->px[y * 20 + left], b = direct->px[y * 20 + right];
        CHECK("direct declaration paints a real red-to-blue gradient", COLOR_R(a) > 140 && COLOR_B(a) < 115 && COLOR_B(b) > 140 && COLOR_R(b) < 115);
        int mismatch = 0;
        for (int i = 0; i < 400; i++) if (!color_near(direct->px[i], group->px[i])) mismatch++;
        CHECK(kind ? "stroke paint server inherits through an ordinary group" : "fill paint server inherits through an ordinary group", mismatch == 0);
    }
    style_engine_free(style); doc_free(doc);
}

static void percentage_stroke_width(void) {
    static const struct {
        const char *name, *shape;
        int width, height, view_width, view_height;
    } cases[] = {
        {"square path", "<path d='M2 10H18' fill='none' stroke='red' stroke-width='%s'/>", 20,20,20,20},
        {"nonsquare path", "<path d='M2 10H38' fill='none' stroke='red' stroke-width='%s'/>", 40,20,40,20},
        {"group inheritance", "<g fill='none' stroke='red' stroke-width='%s'><path d='M2 10H18'/></g>", 20,20,20,20},
        {"use host", "<use href='#percentage-stroke-shape' fill='none' stroke='red' stroke-width='%s'/>", 20,20,20,20},
        {"nonsquare use host", "<use href='#percentage-stroke-shape' fill='none' stroke='red' stroke-width='%s'/>", 40,20,40,20},
        {"scaled viewBox", "<path d='M10 50H190' fill='none' stroke='red' stroke-width='%s'/>", 40,20,200,100},
    };
    for (size_t index = 0; index < ARRLEN(cases); index++) {
        /* SVG2 resolves percentages against the current SVG viewport's
         * normalized diagonal, independently of the HTML page dimensions. */
        double absolute = hypot((double)cases[index].view_width, (double)cases[index].view_height) / sqrt(2.0) * .1;
        char width[64], body[512]; snprintf(width, sizeof width, "%.12g", absolute);
        Document *doc = doc_new("https://percentage-stroke.test/"); SB html; sb_init(&html);
        sb_puts(&html, "<html><body><svg style='display:none'><symbol id='percentage-stroke-shape'><path d='M2 10H18'/></symbol></svg>");
        for (int variant = 0; variant < 2; variant++) {
            snprintf(body, sizeof body, cases[index].shape, variant ? width : "10%");
            sb_printf(&html, "<svg id='%s' viewBox='0 0 %d %d'>%s</svg>", variant ? "absolute" : "percentage",
                cases[index].view_width, cases[index].view_height, body);
        }
        sb_puts(&html, "</body></html>"); html_parse(doc, html.s, html.n); sb_free(&html);
        StyleEngine *style = style_engine_new(doc); style->media = (MediaCtx){900,700,1,false};
        style_recalc(style, &doc->node, true);
        Image *percentage = svg_image(doc_get_element_by_id(doc, "percentage"), cases[index].width, cases[index].height, 1);
        Image *reference = svg_image(doc_get_element_by_id(doc, "absolute"), cases[index].width, cases[index].height, 1);
        CHECK("percentage and absolute SVG stroke fixtures rasterize", percentage && reference && percentage->px && reference->px);
        int painted = 0, mismatch = 0;
        if (percentage && reference && percentage->px && reference->px) {
            int pixels = cases[index].width * cases[index].height;
            for (int pixel = 0; pixel < pixels; pixel++) {
                Color expected = reference->px[pixel];
                if (COLOR_A(expected) > 200 && COLOR_R(expected) > 200) painted++;
                if (!color_near(percentage->px[pixel], expected)) mismatch++;
            }
        }
        CHECK("absolute reference contains real visible stroke pixels", painted > 10);
        if (mismatch) fprintf(stderr, "percentage stroke case: %s, 10%% = %.9g units, %d mismatched pixels\n", cases[index].name, absolute, mismatch);
        CHECK("percentage stroke matches its viewport-resolved absolute width", painted > 10 && mismatch == 0);
        style_engine_free(style); doc_free(doc);
    }
}

static void reference_cache_chain(unsigned links, bool hidden_first) {
    Document *doc = doc_new("https://reference-cache.test/");
    const char *html = "<html><body><svg style='display:none'><defs id='sprite-defs'>"
        "<symbol id='cache-target'><rect id='cache-shape' width='20' height='20' fill='#dd2200'/></symbol>"
        "</defs></svg><svg id='cached-icon'></svg><svg id='fresh-icon'></svg></body></html>";
    html_parse(doc, html, strlen(html));
    Node *defs = doc_get_element_by_id(doc, "sprite-defs"), *shape = doc_get_element_by_id(doc, "cache-shape");
    Node *cached = doc_get_element_by_id(doc, "cached-icon"), *fresh = doc_get_element_by_id(doc, "fresh-icon");
    CHECK("reference cache fixture has separate external definitions", defs && shape && cached && fresh);
    if (!defs || !shape || !cached || !fresh) { doc_free(doc); return; }
    for (unsigned n = 0; n < links; n++) {
        char id[64], href[64];
        snprintf(id, sizeof id, "cache-chain-%u", n);
        if (n + 1 < links) snprintf(href, sizeof href, "#cache-chain-%u", n + 1);
        else snprintf(href, sizeof href, "#cache-target");
        Node *symbol = node_new_element(doc, "symbol", NS_SVG), *use = node_new_element(doc, "use", NS_SVG);
        node_set_attr(symbol, "id", id); node_set_attr(use, "href", href);
        node_append(symbol, use); node_append(defs, symbol);
    }
    Node *instances[] = {cached, fresh};
    for (int n = 0; n < 2; n++) {
        Node *deep = node_new_element(doc, "use", NS_SVG);
        node_set_attr(deep, "href", "#cache-chain-0");
        if (hidden_first) node_set_attr(deep, "opacity", "0");
        node_append(instances[n], deep);
        if (hidden_first) {
            Node *shallow = node_new_element(doc, "use", NS_SVG);
            node_set_attr(shallow, "href", "#cache-target"); node_append(instances[n], shallow);
        }
    }
    StyleEngine *style = style_engine_new(doc); style_recalc(style, &doc->node, true);
    Image *initial = svg_image(cached, 20, 20, 1);
    CHECK("nested reference fixture initially paints its visible red shape", initial && initial->px && color_near(initial->px[210], 0xffdd2200u));
    CHECK("unchanged nested references reuse their initial image", initial && svg_image(cached, 20, 20, 1) == initial);
    node_set_attr(shape, "fill", "#0088dd"); style_recalc(style, &doc->node, true);
    Image *changed = svg_image(cached, 20, 20, 1), *reference = svg_image(fresh, 20, 20, 1);
    CHECK("a fresh referenced instance paints the mutated blue shape", reference && reference->px && color_near(reference->px[210], 0xff0088ddu));
    int mismatch = 0;
    if (changed && reference && changed->px && reference->px) {
        for (int i = 0; i < 400; i++) if (!color_near(changed->px[i], reference->px[i])) mismatch++;
    } else mismatch = 400;
    if (mismatch) fprintf(stderr, "reference cache case: %u symbols, %s, %d stale pixels\n", links,
        hidden_first ? "deep first then visible shallow use" : "visible deep use", mismatch);
    CHECK(hidden_first ? "a deep first reference cannot hide a shallow instance mutation" :
        "a still-visible deep reference participates in cache invalidation", mismatch == 0);
    CHECK("unchanged nested references reuse the updated image", changed && svg_image(cached, 20, 20, 1) == changed);
    style_engine_free(style); doc_free(doc);
}

int main(void) {
    dom_init(); font_init(); radius_keywords(); gradient_inheritance(); percentage_stroke_width();
    /* Two graph lengths exercise a memo first populated near its traversal
     * limit. The visible-only chain also checks hashing versus draw depth. */
    reference_cache_chain(15, true);
    reference_cache_chain(32, true);
    reference_cache_chain(20, false);
    Document *doc = doc_new("https://composer-paint.test/");
    const char *html = "<html><body>"
        "<svg style='display:none'><symbol id='glyph' viewBox='0 0 20 20'>"
        "<g><path id='shape' fill='currentColor' d='M6 4H14V16H6Z'/></g></symbol>"
        "<symbol id='fixed' viewBox='0 0 20 20' style='color:#0d0d0d'>"
        "<path fill='currentColor' d='M6 4H14V16H6Z'/></symbol>"
        "<symbol id='sheet-glyph' viewBox='0 0 20 20'><path class='sheet-fill' d='M6 4H14V16H6Z'/></symbol>"
        "<symbol id='nested' viewBox='0 0 20 20'><use href='#glyph'/></symbol></svg>"
        "<div id='button'><svg id='icon' fill='none' viewBox='0 0 20 20'><use id='instance' href='#glyph'/></svg></div>"
        "<svg id='other' fill='none' viewBox='0 0 20 20'><use href='#glyph'/></svg>"
        "</body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *style = style_engine_new(doc); style->media = (MediaCtx){80, 80, 1, false};
    const char *css = "html,body{margin:0;padding:0;color:#0d0d0d}"
        "#button{position:absolute;left:8px;top:8px;width:44px;height:44px;border:0;padding:0;display:grid;place-items:center;border-radius:999px;color:white}"
        "#button::before{content:'';position:absolute;left:6px;top:6px;width:32px;height:32px;background:black;border-radius:inherit;z-index:0}"
        "#icon{position:relative;z-index:1;width:20px;height:20px}"
        "#other{position:absolute;left:55px;top:55px;width:20px;height:20px;color:#16a085}"
        ".sheet-fill{fill:currentColor}";
    style_engine_add_sheet(style, css_parse_sheet(css, strlen(css), doc->url, 1, &style->media));
    Layout *layout = layout_new(); DisplayList dl = {0}; Canvas canvas; canvas_init(&canvas, 80, 80, 1);
    Node *button = doc_get_element_by_id(doc, "button"), *icon = doc_get_element_by_id(doc, "icon");
    Node *other = doc_get_element_by_id(doc, "other"), *instance = doc_get_element_by_id(doc, "instance"), *shape = doc_get_element_by_id(doc, "shape");
    style_recalc(style, &doc->node, true); layout_run(layout, doc, 80, 80);
    CHECK("button pseudo element is generated", button && button->style && button->style->before);
    if (button && button->style && button->style->before) for (int i = 0; i < 4; i++) CHECK("pseudo background inherits the round host", near(button->style->before->border_radius[i].px, 999));
    CHECK("SVG source has the source document color", shape->style->color == 0xff0d0d0du);
    CHECK("first SVG instance inherits white", icon->style->color == 0xffffffffu);
    CHECK("first currentColor icon is white", color_near(center(icon), 0xffffffffu));
    CHECK("one symbol can render a different instance color", color_near(center(other), 0xff16a085u));
    dl_build(&dl, layout, 0, 0, 80, 80); raster(&canvas, &dl, 0xffffffffu);
    CHECK("round pseudo background does not fill its square corner", color_near(canvas.px[14 * 80 + 14], 0xffffffffu));
    CHECK("round button background still fills its middle edge", color_near(canvas.px[30 * 80 + 17], 0xff000000u));
    CHECK("white glyph remains visible above the button", color_near(canvas.px[30 * 80 + 30], 0xffffffffu));

    Image *cached = svg_image(icon, 20, 20, 1);
    CHECK("unchanged SVG reuses its cached image", svg_image(icon, 20, 20, 1) == cached);
    node_set_attr(icon, "style", "color:#0088ee"); style_recalc(style, &doc->node, true);
    CHECK("color-only restyle invalidates the SVG image", color_near(center(icon), 0xff0088eeu));
    CHECK("restyling one use does not recolor another", color_near(center(other), 0xff16a085u));
    node_set_attr(shape, "fill", "#ee7700"); style_recalc(style, &doc->node, true);
    CHECK("changing a referenced path invalidates the first cache", color_near(center(icon), 0xffee7700u));
    CHECK("changing a referenced path invalidates every use cache", color_near(center(other), 0xffee7700u));
    node_set_attr(shape, "fill", "currentColor"); node_set_attr(instance, "href", "#nested"); style_recalc(style, &doc->node, true);
    CHECK("nested symbol uses preserve the caller color", color_near(center(icon), 0xff0088eeu));
    node_set_attr(instance, "href", "#fixed"); style_recalc(style, &doc->node, true);
    CHECK("explicit symbol color overrides caller inheritance", color_near(center(icon), 0xff0d0d0du));
    node_set_attr(instance, "href", "#sheet-glyph"); style_recalc(style, &doc->node, true);
    CHECK("stylesheet currentColor also resolves in the use instance", color_near(center(icon), 0xff0088eeu));
    node_set_attr(instance, "style", "color:#cc2299"); style_recalc(style, &doc->node, true);
    CHECK("a color set on use itself reaches its shadow content", color_near(center(icon), 0xffcc2299u));

    dl_clear(&dl); canvas_free(&canvas); layout_free(layout); style_engine_free(style); doc_free(doc);
    printf("RESULTS composer-paint %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
