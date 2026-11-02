#include "../src/layout/layout.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error "This regression requires assertions enabled."
#endif

typedef struct {
    const char *name;
    int separators;
    const char *separator_css;
    int preceding_margin;
    float separator_y;
    float separator_h;
    float next_y;
    bool mutate;
} Case;

typedef struct {
    Document *doc;
    StyleEngine *style;
    Layout *layout;
    const char *phase;
} Fixture;

static Node *element(Fixture *f, const char *id)
{
    Node *n = doc_get_element_by_id(f->doc, id);
    assert(n);
    return n;
}

static Box *box(Fixture *f, const char *id)
{
    Node *n = element(f, id);
    assert(n->box);
    return n->box;
}

static void rect(Fixture *f, const char *id,
                 float x, float y, float w, float h)
{
    Box *b = box(f, id);
    fprintf(stderr,
            "%s: %s actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g)\n",
            f->phase, id, b->x, b->y, b->w, b->h, x, y, w, h);
    assert(b->x == x);
    assert(b->y == y);
    assert(b->w == w);
    assert(b->h == h);
}

static void childless(Fixture *f, const char *id)
{
    Box *b = box(f, id);
    assert(b->first == NULL);
    assert(b->last == NULL);
    assert(b->nlines == 0);
}

static void relayout(Fixture *f, bool full)
{
    style_recalc(f->style, &f->doc->node, full);
    layout_run(f->layout, f->doc, 320, 240);
}

/* All positions below are document-coordinate border boxes. */
static void surrounding_geometry(Fixture *f, float next_y)
{
    rect(f, "host",   0, 0,      200, next_y + 100);
    rect(f, "before", 0, 0,      200, 20);
    rect(f, "next",   0, next_y, 100, 100);
}

static void final_geometry(Fixture *f, const Case *c)
{
    surrounding_geometry(f, c->next_y);
    if (c->separators >= 1) {
        rect(f, "sep1", 0, c->separator_y, 200, c->separator_h);
        childless(f, "sep1");
    }
    if (c->separators == 2) {
        rect(f, "sep2", 0, c->separator_y, 200, c->separator_h);
        childless(f, "sep2");
    }
}

static void run_case(const Case *c)
{
    static const char *separators[] = {
        "",
        "<div id='sep1' class='separator'> \n\t </div>",
        "<div id='sep1' class='separator'> \n\t </div>"
        "<div id='sep2' class='separator'> \t\n </div>"
    };
    static const char *paragraph_ids[] = {"p1", "p2", "p3"};

    assert(c->separators >= 0 && c->separators <= 2);

    Fixture f = {0};
    Node *removed[3] = {NULL, NULL, NULL};
    char html[1024];
    char css[2048];

    f.phase = c->name;
    f.doc = doc_new("https://empty-block-margin.test/");
    assert(f.doc);

    int n = snprintf(
        html, sizeof html,
        "<!doctype html><html><body><main id='host'>"
        "<div id='before'></div>"
        "%s"
        "<div id='next'></div>"
        "</main></body></html>",
        separators[c->separators]);
    assert(n >= 0 && (size_t)n < sizeof html);
    html_parse(f.doc, html, strlen(html));

    n = snprintf(
        css, sizeof css,
        "html,body,main,div,p{"
            "display:block;"
            "margin:0;padding:0;border:0;"
            "box-sizing:content-box;"
        "}"
        "#host{width:200px;white-space:normal}"
        "#before{height:20px;margin-bottom:%dpx}"
        "#next{width:100px;height:100px}"
        ".separator{%s}"
        ".inserted{height:7px}",
        c->preceding_margin, c->separator_css);
    assert(n >= 0 && (size_t)n < sizeof css);

    f.style = style_engine_new(f.doc);
    assert(f.style);
    StyleSheet *sheet = css_parse_sheet(
        css, strlen(css), f.doc->url, 1, &f.style->media);
    assert(sheet);
    style_engine_add_sheet(f.style, sheet);

    f.layout = layout_new();
    assert(f.layout);
    relayout(&f, true);
    final_geometry(&f, c);

    if (c->mutate) {
        assert(c->separators == 1);
        Node *container = element(&f, "sep1");

        /*
         * Preserve the original whitespace text node. Insert three real
         * paragraphs through DOM APIs, then perform incremental styling
         * and real layout before removing them.
         *
         * Explicit heights and zero paragraph margins make every phase
         * independent of fonts and UA paragraph margin defaults.
         */
        for (int i = 0; i < 3; ++i) {
            removed[i] = node_new_element(f.doc, "p", NS_HTML);
            assert(removed[i]);
            node_set_attr(removed[i], "id", paragraph_ids[i]);
            node_set_attr(removed[i], "class", "inserted");
            node_append(container, removed[i]);
        }

        f.phase = "paragraphs inserted";
        relayout(&f, false);

        surrounding_geometry(&f, 57);
        rect(&f, "sep1", 0, 36, 200, 21);
        rect(&f, "p1",   0, 36, 200, 7);
        rect(&f, "p2",   0, 43, 200, 7);
        rect(&f, "p3",   0, 50, 200, 7);

        for (int i = 0; i < 3; ++i) {
            assert(removed[i]->parent == container);
            node_remove(removed[i]);
            assert(removed[i]->parent == NULL);
        }

        /*
         * Keep detached nodes alive until layout/style teardown.
         * Reacquire all boxes after layout; no old Box pointer survives
         * across either mutation.
         */
        f.phase = "paragraphs removed";
        relayout(&f, false);
        final_geometry(&f, c);
    }

    layout_free(f.layout);
    style_engine_free(f.style);
    for (int i = 0; i < 3; ++i)
        if (removed[i])
            node_free_tree(removed[i]);
    doc_free(f.doc);
}

int main(int argc, char **argv)
{
    static const Case cases[] = {
        /* name, count, separator CSS, margin, sep y/h, next y, mutate */
        {"no-separator",        0, "", 16, 36, 0, 36, false},
        {"one-empty",           1, "", 16, 36, 0, 36, false},
        {"two-empty",           2, "", 16, 36, 0, 36, false},
        {"insert-remove",       1, "", 16, 36, 0, 36, true},
        {"height-1px",          1, "height:1px",
                                      16, 36, 1, 37, false},

        /* One negative pending margin; all other margins are zero. */
        {"negative-no-separator", 0, "", -8, 12, 0, 12, false},
        {"negative-one-empty",    1, "", -8, 12, 0, 12, false},

        /*
         * Auto content height is zero; border + padding occupy 3px.
         * These prevent the separator's margins collapsing through.
         */
        {"border-padding", 1,
         "border-top:1px solid black;padding-bottom:2px",
                                      16, 36, 3, 39, false}
    };

    if (argc > 2) {
        fprintf(stderr, "usage: %s [case-name]\n", argv[0]);
        return 2;
    }

    if (argc == 2) {
        bool found = false;
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
            if (strcmp(argv[1], cases[i].name) == 0)
                found = true;
        if (!found) {
            fprintf(stderr, "unknown case: %s\n", argv[1]);
            return 2;
        }
    }

    dom_init();
    font_init();

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        if (argc == 2 && strcmp(argv[1], cases[i].name) != 0)
            continue;
        run_case(&cases[i]);
        fprintf(stderr, "PASS: %s\n", cases[i].name);
    }
    return 0;
}
