/* Logical sizes act as the physical ones: chatgpt.com Pages gives every paragraph of a document
   flex:none and inline-size:100% inside a flex row, which must fill the row and wrap its text. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static Document *d;
static Box *box(const char *id) { Node *n = doc_get_element_by_id(d, id); assert(n && n->box); return n->box; }


/* The mapping basis is a cascaded prerequisite, not declaration-order state. */
static void cascade_case(const char *label, const char *parent, const char *inl,
                         const char *sheet, bool pseudo, int mode,
                         float min_width, float min_height) {
    Document *c = doc_new("https://logical-size.test/");
    SB html = {0};
    sb_printf(&html, "<html><body style='%s'><div id=x style='%s'></div></body></html>", parent, inl);
    html_parse(c, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(c);
    style_engine_add_sheet(e, css_parse_sheet(sheet, strlen(sheet), c->url, 1, &e->media));
    style_recalc(e, &c->node, true);
    Node *x = doc_get_element_by_id(c, "x");
    ComputedStyle *st = x && x->style ? (pseudo ? x->style->before : x->style) : NULL;
    bool ok = st && st->writing_mode == mode &&
        st->min_width.kind == LK_LEN && fabsf(st->min_width.px - min_width) < .01f &&
        st->min_height.kind == LK_LEN && fabsf(st->min_height.px - min_height) < .01f;
    if (!ok) fprintf(stderr, "%s: mode %d minimums %g/%g expected %d %g/%g\n", label,
        st ? st->writing_mode : -1, st ? st->min_width.px : -1, st ? st->min_height.px : -1,
        mode, min_width, min_height);
    assert(ok);
    style_engine_free(e); doc_free(c);
}
static void cascade_controls(void) {
    const char *dims = "#x{min-inline-size:31px;min-block-size:37px}";
    cascade_case("inline late mode", "", "min-inline-size:31px;min-block-size:37px;writing-mode:vertical-rl", "", false, 2, 37, 31);
    cascade_case("inline early mode", "", "writing-mode:vertical-rl;min-inline-size:31px;min-block-size:37px", "", false, 2, 37, 31);
    cascade_case("sheet before inline mode", "", "writing-mode:vertical-rl", dims, false, 2, 37, 31);
    cascade_case("sheet late mode", "", "", "#x{min-inline-size:31px;min-block-size:37px;writing-mode:vertical-lr}", false, 1, 37, 31);
    cascade_case("inherited vertical mode", "writing-mode:vertical-rl", "", dims, false, 2, 37, 31);
    cascade_case("explicit inherit wins", "writing-mode:vertical-rl", "writing-mode:inherit", "#x{writing-mode:horizontal-tb;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("unset wins", "writing-mode:vertical-rl", "writing-mode:unset", "#x{writing-mode:horizontal-tb;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("initial wins", "writing-mode:vertical-rl", "writing-mode:initial", dims, false, 0, 31, 37);
    cascade_case("important sheet mode", "", "writing-mode:horizontal-tb", "#x{min-inline-size:31px;min-block-size:37px;writing-mode:vertical-rl!important}", false, 2, 37, 31);
    cascade_case("variable mode", "", "--wm:vertical-rl;writing-mode:var(--wm)", dims, false, 2, 37, 31);
    cascade_case("physical later", "", "writing-mode:vertical-rl", "#x{min-inline-size:31px;min-block-size:37px;min-height:23px}", false, 2, 37, 23);
    cascade_case("logical later", "", "writing-mode:vertical-rl", "#x{min-height:23px;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("important logical", "", "writing-mode:vertical-rl;min-height:23px", "#x{min-inline-size:31px!important;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("important physical", "", "writing-mode:vertical-rl;min-inline-size:31px", "#x{min-height:23px!important;min-block-size:37px}", false, 2, 37, 23);
    cascade_case("pseudo own mode", "", "", "#x::before{content:'';min-inline-size:31px;min-block-size:37px;writing-mode:vertical-rl}", true, 2, 37, 31);
    cascade_case("pseudo inherited mode", "", "writing-mode:vertical-rl", "#x::before{content:'';min-inline-size:31px;min-block-size:37px}", true, 2, 37, 31);
    cascade_case("pseudo mode reset", "", "writing-mode:vertical-rl", "#x::before{content:'';min-inline-size:31px;min-block-size:37px;writing-mode:initial}", true, 0, 31, 37);
    cascade_case("invalid literal ignored", "", "writing-mode:bogus", "#x{writing-mode:vertical-rl;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("invalid vertical prefix ignored", "", "writing-mode:vertical-bogus", "#x{writing-mode:vertical-rl;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("invalid variable unsets", "writing-mode:vertical-rl", "--wm:bogus;writing-mode:var(--wm)", "#x{writing-mode:horizontal-tb;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("missing variable unsets", "writing-mode:vertical-rl", "writing-mode:var(--missing)", "#x{writing-mode:horizontal-tb;min-inline-size:31px;min-block-size:37px}", false, 2, 37, 31);
    cascade_case("mixed case keyword", "", "writing-mode:VeRtIcAl-rL", dims, false, 2, 37, 31);
    puts("22 logical cascade controls passed");
}


static unsigned edge_checks;
static void edge_check(bool ok, const char *label, const char *property,
                       const char *actual, const char *expected) {
    edge_checks++;
    if (!ok) {
        fprintf(stderr, "%s %s: %s expected %s\n", label, property, actual, expected);
        assert(ok);
    }
}
static void edge_case(const char *group, const char *label, const char *parent,
                      const char *inl, const char *sheet, bool pseudo,
                      const float expected[4], const int logical_sides[4]) {
    Document *c = doc_new("https://logical-edges.test/");
    SB html = {0};
    sb_printf(&html, "<!doctype html><html><body style='%s'><div id=x style='%s'></div></body></html>", parent, inl);
    html_parse(c, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(c);
    style_engine_add_sheet(e, css_parse_sheet(sheet, strlen(sheet), c->url, 1, &e->media));
    style_recalc(e, &c->node, true);
    Node *x = doc_get_element_by_id(c, "x");
    const ComputedStyle *st = x && x->style ? (pseudo ? x->style->before : x->style) : NULL;
    assert(st);
    const Length *edges = !strcmp(group, "margin") ? st->margin : !strcmp(group, "padding") ? st->padding : st->inset;
    static const char *const physical[] = {"top", "right", "bottom", "left"};
    static const char *const logical[] = {"block-start", "block-end", "inline-start", "inline-end"};
    char name[80], want[80], got[80];
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof name, !strcmp(group, "inset") ? "%s%s" : "%s-%s",
                 !strcmp(group, "inset") ? "" : group, physical[i]);
        snprintf(want, sizeof want, "%gpx", expected[i]);
        snprintf(got, sizeof got, "%gpx (kind %d)", edges[i].px, edges[i].kind);
        edge_check(edges[i].kind == LK_LEN && fabsf(edges[i].px - expected[i]) < .01f, label, name, got, want);
        char *v = pseudo ? css_get_computed_pseudo(x, 1, name) : css_get_computed_value(x, name);
        edge_check(!strcmp(v, want), label, name, v, want); free(v);
        snprintf(name, sizeof name, "%s-%s", group, logical[i]);
        snprintf(want, sizeof want, "%gpx", expected[logical_sides[i]]);
        v = pseudo ? css_get_computed_pseudo(x, 1, name) : css_get_computed_value(x, name);
        edge_check(!strcmp(v, want), label, name, v, want); free(v);
    }
    for (int axis = 0; axis < 2; axis++) {
        snprintf(name, sizeof name, "%s-%s", group, axis ? "inline" : "block");
        float a = expected[logical_sides[axis * 2]], z = expected[logical_sides[axis * 2 + 1]];
        if (a == z) snprintf(want, sizeof want, "%gpx", a);
        else snprintf(want, sizeof want, "%gpx %gpx", a, z);
        char *v = pseudo ? css_get_computed_pseudo(x, 1, name) : css_get_computed_value(x, name);
        edge_check(!strcmp(v, want), label, name, v, want); free(v);
    }
    style_engine_free(e); doc_free(c);
}
static void edge_controls(void) {
    /* Explicit expected physical tuples from CSS Writing Modes, independent of
       the implementation's mapping helper. Order is top/right/bottom/left. */
    static const struct {
        const char *mode, *direction, *orientation;
        float edges[4]; int logical_sides[4];
    } modes[] = {
        {"horizontal-tb", "ltr", "mixed", {11,41,23,37}, {0,2,3,1}},
        {"horizontal-tb", "rtl", "mixed", {11,37,23,41}, {0,2,1,3}},
        {"vertical-rl", "ltr", "mixed", {37,11,41,23}, {1,3,0,2}},
        {"vertical-rl", "rtl", "mixed", {41,11,37,23}, {1,3,2,0}},
        {"vertical-lr", "ltr", "mixed", {37,23,41,11}, {3,1,0,2}},
        {"vertical-lr", "rtl", "mixed", {41,23,37,11}, {3,1,2,0}},
        {"vertical-rl", "rtl", "upright", {37,11,41,23}, {1,3,0,2}},
        {"vertical-lr", "rtl", "upright", {37,23,41,11}, {3,1,0,2}},
    };
    const char *groups[] = {"margin", "padding", "inset"};
    for (int group = 0; group < 3; group++) {
        const char *g = groups[group]; char decl[320], basis[240], inl[640], sheet[900], label[180], parent[640];
        snprintf(decl, sizeof decl, "%s-block:11px 23px;%s-inline:37px 41px;", g, g);
        for (size_t k = 0; k < sizeof modes / sizeof *modes; k++) {
            snprintf(basis, sizeof basis, "writing-mode:%s;direction:%s;text-orientation:%s;",
                     modes[k].mode, modes[k].direction, modes[k].orientation);
            snprintf(inl, sizeof inl, "%s%s", decl, basis);
            snprintf(label, sizeof label, "%s late basis %zu", g, k);
            edge_case(g, label, "", inl, "", false, modes[k].edges, modes[k].logical_sides);
            snprintf(sheet, sizeof sheet, "#x{%s}", decl);
            snprintf(label, sizeof label, "%s sheet before inline basis %zu", g, k);
            edge_case(g, label, "", basis, sheet, false, modes[k].edges, modes[k].logical_sides);
            snprintf(label, sizeof label, "%s inherited basis %zu", g, k);
            edge_case(g, label, basis, "", sheet, false, modes[k].edges, modes[k].logical_sides);
            snprintf(sheet, sizeof sheet, "#x::before{content:'';%s%s}", decl, basis);
            snprintf(label, sizeof label, "%s pseudo own basis %zu", g, k);
            edge_case(g, label, "", "writing-mode:horizontal-tb;direction:ltr", sheet, true, modes[k].edges, modes[k].logical_sides);
        }
        /* Logical inherit follows the parent's logical names across axes. */
        snprintf(parent, sizeof parent, "%swriting-mode:horizontal-tb;direction:ltr", decl);
        snprintf(sheet, sizeof sheet, "#x{%s-block:inherit;%s-inline:inherit}", g, g);
        edge_case(g, "logical inheritance horizontal to vertical", parent, "writing-mode:vertical-rl;direction:rtl", sheet, false, modes[3].edges, modes[3].logical_sides);
        snprintf(parent, sizeof parent, "%swriting-mode:vertical-rl;direction:rtl", decl);
        edge_case(g, "logical inheritance vertical to horizontal", parent, "writing-mode:horizontal-tb;direction:ltr", sheet, false, modes[0].edges, modes[0].logical_sides);
        /* Interleave a physical property without reordering either cascade. */
        const char *right = !strcmp(g, "inset") ? "right" : !strcmp(g, "margin") ? "margin-right" : "padding-right";
        snprintf(inl, sizeof inl, "%s%s:99px;writing-mode:horizontal-tb;direction:rtl", decl, right);
        float later[4] = {11,99,23,41};
        edge_case(g, "physical declaration later", "", inl, "", false, later, modes[1].logical_sides);
        snprintf(inl, sizeof inl, "%s:99px;%swriting-mode:horizontal-tb;direction:rtl", right, decl);
        edge_case(g, "logical declaration later", "", inl, "", false, modes[1].edges, modes[1].logical_sides);
        snprintf(sheet, sizeof sheet, "#x{%s-block:11px 23px;%s-inline:37px 41px!important;direction:rtl!important}", g, g);
        snprintf(inl, sizeof inl, "%s:99px;direction:ltr", right);
        edge_case(g, "important logical and basis", "", inl, sheet, false, modes[1].edges, modes[1].logical_sides);
        snprintf(inl, sizeof inl, "%s%s-inline:17px bogus;writing-mode:horizontal-tb;direction:rtl", decl, g);
        edge_case(g, "invalid pair remains atomic", "", inl, "", false, modes[1].edges, modes[1].logical_sides);
        snprintf(inl, sizeof inl, "%s--dir:rtl;direction:var(--dir);writing-mode:horizontal-tb", decl);
        edge_case(g, "variable direction", "", inl, "", false, modes[1].edges, modes[1].logical_sides);
    }
    printf("%u logical edge checks passed\n", edge_checks);
}


static void mutation_value(Node *n, const char *prop, const char *want, const char *label) {
    char *got = css_get_computed_value(n, prop);
    edge_check(!strcmp(got, want), label, prop, got, want); free(got);
}
static void edge_mutation_controls(void) {
    static const char *const props[] = {
        "margin-inline-start", "padding-block-end", "inset-inline-end",
        "min-inline-size", "max-block-size", "inline-size", "block-size"
    };
    for (size_t i = 0; i < sizeof props / sizeof *props; i++) {
        Document *c = doc_new("https://logical-mutation.test/");
        SB h = {0};
        sb_printf(&h, "<html><body><div id=p style='writing-mode:vertical-rl;direction:rtl;%s:11px'>"
                      "<div id=x style='writing-mode:horizontal-tb;direction:ltr;%s:inherit'></div></div></body></html>", props[i], props[i]);
        html_parse(c, h.s, h.n); sb_free(&h);
        StyleEngine *e = style_engine_new(c); style_recalc(e, &c->node, true);
        Node *parent = doc_get_element_by_id(c, "p"), *x = doc_get_element_by_id(c, "x");
        mutation_value(x, props[i], "11px", "before inherited logical mutation");
        char value[240]; snprintf(value, sizeof value, "writing-mode:vertical-rl;direction:rtl;%s:29px", props[i]);
        node_set_attr(parent, "style", value); style_recalc(e, &c->node, false);
        mutation_value(x, props[i], "29px", "after inherited logical mutation");
        style_engine_free(e); doc_free(c);
    }
    Document *c = doc_new("https://logical-mutation.test/");
    const char *html = "<html><body><div id=p style='writing-mode:vertical-rl;direction:rtl;text-orientation:mixed;inset:1px 2px 3px 4px'>"
                       "<div id=x style='margin-inline:11px 23px;inset:inherit'></div></div></body></html>";
    html_parse(c, html, strlen(html));
    StyleEngine *e = style_engine_new(c); style_recalc(e, &c->node, true);
    Node *parent = doc_get_element_by_id(c, "p"), *x = doc_get_element_by_id(c, "x");
    mutation_value(x, "margin-top", "23px", "before inherited orientation mutation");
    mutation_value(x, "margin-bottom", "11px", "before inherited orientation mutation");
    node_set_attr(parent, "style", "writing-mode:vertical-rl;direction:rtl;text-orientation:upright;inset:5px 6px 7px 8px");
    style_recalc(e, &c->node, false);
    mutation_value(x, "margin-top", "11px", "after inherited orientation mutation");
    mutation_value(x, "margin-bottom", "23px", "after inherited orientation mutation");
    mutation_value(x, "top", "5px", "after inherited inset shorthand mutation");
    mutation_value(x, "right", "6px", "after inherited inset shorthand mutation");
    mutation_value(x, "bottom", "7px", "after inherited inset shorthand mutation");
    mutation_value(x, "left", "8px", "after inherited inset shorthand mutation");
    /* Isolate an inset-only parent mutation from orientation invalidation. */
    node_set_attr(parent, "style", "writing-mode:vertical-rl;direction:rtl;text-orientation:upright;inset:9px 10px 12px 14px");
    style_recalc(e, &c->node, false);
    mutation_value(x, "top", "9px", "after isolated inherited inset mutation");
    mutation_value(x, "left", "14px", "after isolated inherited inset mutation");
    style_engine_free(e); doc_free(c);
    printf("%u logical edge and mutation checks passed\n", edge_checks);
}


/* Border longhands must affect the real physical borders, not just CSSOM.
   Fixed TRBL expectations below are independent of the engine mapping helper. */
static int border_checks;
static void border_check(bool ok, const char *label) {
    border_checks++; if (!ok) { fprintf(stderr, "logical border: %s\n", label); abort(); }
}
static const char *border_decl =
    "border-block-start-style:solid;border-block-start-width:11px;border-block-start-color:red;"
    "border-block-end-style:solid;border-block-end-width:13px;border-block-end-color:green;"
    "border-inline-start-style:solid;border-inline-start-width:17px;border-inline-start-color:blue;"
    "border-inline-end-style:solid;border-inline-end-width:19px;border-inline-end-color:purple;";
static void border_controls(void) {
    static const struct { const char *basis; float expected[4]; } cases[] = {
        {"writing-mode:horizontal-tb;direction:ltr", {11,19,13,17}},
        {"writing-mode:horizontal-tb;direction:rtl", {11,17,13,19}},
        {"writing-mode:vertical-rl;direction:ltr", {17,11,19,13}},
        {"writing-mode:vertical-lr;direction:ltr", {17,13,19,11}},
        {"writing-mode:vertical-rl;direction:rtl", {19,11,17,13}},
        {"writing-mode:vertical-lr;direction:rtl", {19,13,17,11}},
        {"writing-mode:vertical-rl;direction:rtl;text-orientation:upright", {17,11,19,13}}
    };
    static const char *props[] = {"border-block-start-width","border-block-end-width","border-inline-start-width","border-inline-end-width"};
    static const float values[] = {11,13,17,19};
    for (size_t i=0;i<ARRLEN(cases);i++) {
        Document *c=doc_new("https://logical-border.test/"); SB html={0};
        sb_printf(&html,"<!doctype html><html><body><div id=x style='%s%s'></div></body></html>",border_decl,cases[i].basis);
        html_parse(c,html.s,html.n);sb_free(&html);StyleEngine *e=style_engine_new(c);style_recalc(e,&c->node,true);
        Node *n=doc_get_element_by_id(c,"x");assert(n&&n->style);
        for(int j=0;j<4;j++) {
            float w=cases[i].expected[j];Color color=w==11?RGBA(255,0,0,255):w==13?RGBA(0,128,0,255):w==17?RGBA(0,0,255,255):RGBA(128,0,128,255);
            border_check(fabsf(n->style->border_width[j]-w)<.01f,"fixed physical width");
            border_check(n->style->border_style[j]==BS_SOLID,"physical solid style");
            border_check(n->style->border_color[j]==color,"fixed physical color");
            char *v=css_get_computed_value(n,props[j]);border_check(fabsf(strtof(v,NULL)-values[j])<.01f,"logical computed width");free(v);
        }
        style_engine_free(e);doc_free(c);
    }
    Document *c=doc_new("https://logical-border.test/");SB html={0};
    sb_printf(&html,"<!doctype html><html><body style='%s'><div id=x style='writing-mode:vertical-rl;direction:rtl;"
        "border-block-start-width:inherit;border-block-end-width:inherit;border-inline-start-width:inherit;border-inline-end-width:inherit;"
        "border-block-start-style:inherit;border-block-end-style:inherit;border-inline-start-style:inherit;border-inline-end-style:inherit;"
        "border-block-start-color:inherit;border-block-end-color:inherit;border-inline-start-color:inherit;border-inline-end-color:inherit'></div></body></html>",border_decl);
    html_parse(c,html.s,html.n);sb_free(&html);StyleEngine *e=style_engine_new(c);style_recalc(e,&c->node,true);Node *n=doc_get_element_by_id(c,"x");
    static const float inherited[]={19,11,17,13};
    for(int j=0;j<4;j++){border_check(fabsf(n->style->border_width[j]-inherited[j])<.01f,"corresponding parent logical width");border_check(n->style->border_style[j]==BS_SOLID,"corresponding parent logical style");}
    border_check(n->style->border_color[2]==RGBA(0,0,255,255),"corresponding parent logical color");
    SB parent={0};sb_printf(&parent,"%sborder-inline-start-width:23px;border-inline-start-color:orange",border_decl);node_set_attr(c->body,"style",parent.s);sb_free(&parent);style_recalc(e,&c->node,false);
    border_check(fabsf(n->style->border_width[2]-23)<.01f,"parent-only border width mutation");
    border_check(n->style->border_color[2]==RGBA(255,165,0,255),"parent-only border color mutation");
    style_engine_free(e);doc_free(c);
    static const struct { const char *style; float left; } precedence[]={
        {"border-style:solid;border-inline-start-width:17px;border-left-width:23px",23},
        {"border-style:solid;border-left-width:23px;border-inline-start-width:17px",17},
        {"border-style:solid;border-width:7px;border-width:initial",3},
        {"border-style:solid;border-inline-start-width:17px;border-inline-start-width:initial",3}
    };
    for(size_t i=0;i<ARRLEN(precedence);i++) {
        c=doc_new("https://logical-border.test/");sb_printf(&html,"<!doctype html><html><body><div id=x style='%s'></div></body></html>",precedence[i].style);html_parse(c,html.s,html.n);sb_free(&html);
        e=style_engine_new(c);style_recalc(e,&c->node,true);n=doc_get_element_by_id(c,"x");border_check(fabsf(n->style->border_width[3]-precedence[i].left)<.01f,"physical/logical precedence and initial shorthand");style_engine_free(e);doc_free(c);
    }
    /* CSS-wide shorthands affect all four real physical sides. */
    static const struct { const char *style; float width; Color color; } wide[] = {
        {"color:rgb(1,2,3);border:7px solid red;border-width:initial",3,RGBA(255,0,0,255)},
        {"color:rgb(1,2,3);border:7px solid red;border-width:unset",3,RGBA(255,0,0,255)},
        {"color:rgb(1,2,3);border:7px solid red;border-style:initial",0,RGBA(255,0,0,255)},
        {"color:rgb(1,2,3);border:7px solid red;border-color:initial",7,RGBA(1,2,3,255)},
        {"color:rgb(1,2,3);border:7px solid red;border:initial;border-style:solid",3,RGBA(1,2,3,255)}
    };
    for(size_t i=0;i<ARRLEN(wide);i++) {
        c=doc_new("https://logical-border.test/");sb_printf(&html,"<html><body><div id=x style='%s'></div></body></html>",wide[i].style);html_parse(c,html.s,html.n);sb_free(&html);
        e=style_engine_new(c);style_recalc(e,&c->node,true);n=doc_get_element_by_id(c,"x");
        for(int j=0;j<4;j++){border_check(fabsf(n->style->border_width[j]-wide[i].width)<.01f,"all-side CSS-wide width");border_check(n->style->border_color[j]==wide[i].color,"all-side CSS-wide color");}
        style_engine_free(e);doc_free(c);
    }
    for(int none=0;none<2;none++) {
        c=doc_new("https://logical-border.test/");sb_printf(&html,"<html><body style='border-width:2px 4px 6px 8px;border-style:%s;border-color:red green blue orange'><div id=x style='color:purple;border-style:solid;border-width:inherit;border-color:inherit'></div></body></html>",none?"none":"solid");html_parse(c,html.s,html.n);sb_free(&html);
        e=style_engine_new(c);style_recalc(e,&c->node,true);n=doc_get_element_by_id(c,"x");
        static const Color colors[]={RGBA(255,0,0,255),RGBA(0,128,0,255),RGBA(0,0,255,255),RGBA(255,165,0,255)};
        for(int j=0;j<4;j++){border_check(fabsf(n->style->border_width[j]-(none?0:2*(j+1)))<.01f,"inherit parent's computed physical width");border_check(n->style->border_color[j]==colors[j],"inherit parent's physical color");}
        style_engine_free(e);doc_free(c);
    }
    /* Exact geometry behind the new upstream loss: a300px containing block
       and two real medium3px borders leave294px of content. */
    c=doc_new("https://logical-border.test/");
    const char *geometry="<!doctype html><html><body><div style='width:300px'><div id=x style='border-block-start-style:solid;border-block-end-style:solid;border-inline-start-style:solid;border-inline-end-style:solid'></div></div></body></html>";
    html_parse(c,geometry,strlen(geometry));e=style_engine_new(c);style_recalc(e,&c->node,true);Layout *layout=layout_new();layout_run(layout,c,800,600);n=doc_get_element_by_id(c,"x");
    char *physical=css_get_computed_value(n,"width"),*logical=css_get_computed_value(n,"inline-size");
    border_check(fabsf(strtof(physical,NULL)-294)<.01f,"real physical content width294px");
    border_check(fabsf(strtof(logical,NULL)-294)<.01f,"logical content width294px");free(physical);free(logical);
    node_set_attr(n,"style","border-block-start-style:solid;border-block-end-style:solid;border-inline-start-style:solid;border-inline-end-style:solid;inline-size:initial");style_recalc(e,&c->node,false);layout_run(layout,c,800,600);
    logical=css_get_computed_value(n,"inline-size");border_check(fabsf(strtof(logical,NULL)-294)<.01f,"initial inline-size retains294px geometry");free(logical);
    layout_free(layout);style_engine_free(e);doc_free(c);
    printf("%d logical border controls passed\n",border_checks);
}

int main(void) {
    dom_init(); font_init();
    cascade_controls();
    border_controls();
    edge_controls();
    edge_mutation_controls();
    d = doc_new("https://logical-size.test/");
    const char *html = "<html><body>"
        "<div class=row><p id=p>A long paragraph of words that needs several lines inside a narrow column of text here.</p></div>"
        "<div id=b style='block-size:40px;inline-size:50%;min-block-size:60px;max-inline-size:100px'>x</div>"
        "</body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body{margin:0;font:16px sans-serif}.row{display:flex;align-items:flex-start;width:200px}"
        ".row>*{flex:none;inline-size:100%;min-inline-size:0;margin-block:0}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Layout *l = layout_new(); layout_run(l, d, 1080, 540);
    Box *p = box("p"), *b = box("b");
    fprintf(stderr, "p: w %g h %g; b: w %g h %g\n", p->w, p->h, b->w, b->h);
    assert(fabsf(p->w - 200) < .5f && p->h > 40);
    assert(fabsf(b->w - 100) < .5f && fabsf(b->h - 60) < .5f);
    char *w = css_get_computed_value(doc_get_element_by_id(d, "b"), "width");
    assert(!strcmp(w, "100px"));
    free(w);
    return 0;
}
