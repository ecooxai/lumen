#include "../src/css/css.h"
#include <assert.h>

static void test_match_cache(void) {
    Document *d = doc_new("https://selector-cache.test/");
    Node *root = node_new_element(d, "main", NS_HTML);
    node_append(&d->node, root);
    Node *nodes[100];
    for (int i = 0; i < 100; i++) {
        nodes[i] = node_new_element(d, i % 3 ? "span" : "section", NS_HTML);
        node_set_attr(nodes[i], "class", i % 2 ? "odd leaf" : "even leaf");
        node_append(i % 5 ? nodes[i - 1] : root, nodes[i]);
    }
    const char *selectors[] = {".root :is(.odd,.even) .leaf", ".leaf:not(:where(.missing,.missing *))", ".leaf:not(:where(.odd,.odd *))", ".odd:has(.even:not(:where(.missing,.missing *)))", ".odd:has(>.even) .leaf",
        "section:nth-child(2n+1) .leaf:not(.missing)", ":scope > .leaf", ".odd + section .leaf",
        ".root:hover .leaf", ".root:focus-within .leaf"};
    CssMatchCache *cache = NULL;
    for (int pass = 0; pass < 3; pass++) {
        node_set_attr(root, "class", pass == 1 ? "changed" : "root");
        root->flags ^= NF_HOVER;
        d->focus = pass == 2 ? nodes[99] : NULL;
        for (size_t k = 0; k < ARRLEN(selectors); k++) {
            SelList list = {0}; assert(css_parse_selector_list(selectors[k], &list));
            bool expected[100][2];
            for (int i = 0; i < 100; i++) for (int sc = 0; sc < 2; sc++)
                expected[i][sc] = css_match_selector(&list.v[0], nodes[i], sc ? nodes[0] : root);
            CssMatchCache *previous = css_match_cache_begin(&cache);
            for (int repeat = 0; repeat < 3; repeat++)
                for (int i = 0; i < 100; i++) for (int sc = 0; sc < 2; sc++)
                    assert(expected[i][sc] == css_match_selector(&list.v[0], nodes[i], sc ? nodes[0] : root));
            css_match_cache_end(previous);
            css_sellist_free(&list);
        }
    }
    css_match_cache_free(cache); doc_free(d);
}

static void test_rule_filter(void) {
    Document *d = doc_new("https://rule-filter.test/");
    const char *html = "<html><body><main class='outer'><div class='first'><b></b></div><section class='second'><span class='leaf'></span><i></i></section><p></p></main></body></html>";
    html_parse(d, html, strlen(html));
    const char *sels[] = {".missing *", ".outer :is(span,i)", ":is(.outer *, .outer > *)",
        ":is(.absent *, .outer *)", ":where(.absent,span)", ":is(.absent,*)",
        ".first + section *", ".first ~ section i", ".absent + section *",
        ".leaf:not(:where(.missing,.missing *))", ".leaf:not(:where(.outer,.outer *))",
        ".outer:has(.leaf:not(:where(.missing,.missing *)))", ".first:has(+ .second .leaf)",
        ".leaf:not(:is(.first + section *))", "section :nth-child(1 of :not(.absent))",
        ".outer :is(.first + section) *", ":not(.absent)", ":has(.leaf)",
        ":is(.first > b, .second > span)", ".outer > :is(section,p)", "main:has(.leaf) span", ":is(.leaf)", ":where(section > .leaf, main .leaf)", ":is(:where(.first),.first)", ":is(.first,.second)", ":where(section span, main span)", ":is(.missing span, .outer i)",
        ".outer > .first + section span", ".absent > .first + section span", ".outer .first ~ :is(section) i",
        ".missing .first ~ section i", ":is(.outer:has(> p), .outer:has(> div)) > .first + section > i",
        ".first + section .leaf + i", ".outer .first + .second i", ".outer > .absent + section span"};
    for (size_t k = 0; k < ARRLEN(sels); k++) {
        StyleEngine *e = style_engine_new(d);
        char css[512]; snprintf(css, sizeof css, "%s {background-color:rgb(11,22,33)}", sels[k]);
        style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
        style_recalc(e, &d->node, true);
        SelList list = {0}; assert(css_parse_selector_list(sels[k], &list));
        for (Node *n = d->html; n; n = node_next_in_tree(n, &d->node)) {
            if (n->type != NODE_ELEMENT) continue;
            bool matched = css_match_selector_list(&list, n);
            assert((n->style->bg_color == RGBA(11,22,33,255)) == matched);
        }
        css_sellist_free(&list); style_engine_free(e);
    }
    doc_free(d);
}

static void test_query_syntax_cache(void) {
    Document *d = doc_new("https://query-cache.test/");
    const char *html = "<html><body><main id='root'><b class='leaf'></b><i></i></main></body></html>";
    html_parse(d, html, strlen(html));
    Node *root = doc_get_element_by_id(d, "root"), *b = root->first;
    for (int pass = 0; pass < 200; pass++) {
        char query[64]; snprintf(query, sizeof query, ".unused%d", pass);
        assert(!css_query(root, query, false, NULL)); /* bounded-cache evictions */
        node_set_attr(b, "class", pass % 2 ? "changed" : "leaf");
        assert(css_query(root, ":scope > .leaf", false, NULL) == (pass % 2 ? NULL : b));
        assert(css_query(root, "[", false, NULL) == (Node *)-1);
        SelList scratch = {0};
        const SelList *parsed = css_query_syntax(d, ".changed:hover", &scratch);
        assert(parsed);
        b->flags |= NF_HOVER;
        assert(css_match_selector_list(parsed, b) == (pass % 2 != 0));
        b->flags &= ~(uint32_t)NF_HOVER;
        assert(!css_match_selector_list(parsed, b));
        css_sellist_free(&scratch);
    }
    char large[5000]; memset(large, ' ', sizeof large); memcpy(large, "b", 1); large[sizeof large - 1] = 0;
    assert(css_query(root, large, false, NULL) == b);
    NodeVec all = {0}; css_query(root, "b,i", true, &all);
    assert(all.n == 2); vec_free(all);
    doc_free(d);
}

static void test_style_cache_lifetime(void) {
    Document *d = doc_new("https://persistent-match.test/");
    const char *html = "<html><body><section class='group'><p id='p' class='leaf'>a</p></section><aside></aside></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = ".group .leaf {color:var(--ink,red)} .group:hover .leaf {background-color:blue} section:has(p:empty) + aside {color:blue}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    Node *p = doc_get_element_by_id(d, "p"), *group = p->parent;
    style_recalc(e, &d->node, true);
    for (int i = 0; i < 40; i++) {
        uint64_t version = d->selector_version;
        node_set_attr(group, "style", i % 2 ? "--ink:blue" : "--ink:green");
        assert(d->selector_version == version);
        style_recalc(e, &d->node, false);
        assert(p->style->color == (i % 2 ? RGBA(0,0,255,255) : RGBA(0,128,0,255)));
    }
    group->flags |= NF_HOVER; doc_mark_style_dirty(d, group);
    style_recalc(e, &d->node, false); assert(p->style->bg_color == RGBA(0,0,255,255));
    group->flags &= ~(uint32_t)NF_HOVER; doc_mark_style_dirty(d, group);
    style_recalc(e, &d->node, false); assert(p->style->bg_color != RGBA(0,0,255,255));
    node_set_attr(group, "class", "changed");
    style_recalc(e, &d->node, false); assert(p->style->color != RGBA(0,0,255,255));
    node_set_attr(group, "class", "group");
    style_recalc(e, &d->node, false); assert(p->style->color == RGBA(0,0,255,255));
    const char *css2 = ":is(section[style*='green']) .leaf {background-color:red}";
    style_engine_add_sheet(e, css_parse_sheet(css2, strlen(css2), d->url, 1, &e->media));
    for (int i = 0; i < 4; i++) {
        node_set_attr(group, "style", i % 2 ? "--ink:blue" : "--ink:green");
        style_recalc(e, &d->node, false);
        assert((p->style->bg_color == RGBA(255,0,0,255)) == (i % 2 == 0));
    }
    style_engine_free(e); doc_free(d);
}

static void test_inherited_custom_updates(void) {
    Document *d = doc_new("https://inherited-custom.test/");
    const char *html = "<html><body><section id='root'><p id='plain'>plain</p><p id='used'>used</p>"
        "<div id='own'><b id='nested'>nested</b></div><p id='explicit'>explicit</p>"
        "<p id='pseudo'>pseudo</p><p id='attr'>attr</p><p id='query'>query</p></section></body></html>";
    html_parse(d,html,strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "#root{--ink:red;--unused:1;--label:none} #used{color:var(--ink)}"
        "#own{--local:green} #nested{color:var(--local);background-color:var(--ink)}"
        "#explicit{background-color:inherit} #pseudo::before{content:var(--label)}"
        "section[style*='blue'] #attr{color:blue} @container style(--ink:blue){#query{color:blue}}";
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
    style_recalc(e,&d->node,true);
    Node *root=doc_get_element_by_id(d,"root"), *plain=doc_get_element_by_id(d,"plain");
    Node *used=doc_get_element_by_id(d,"used"), *nested=doc_get_element_by_id(d,"nested");
    const char *updates[] = {"--unused:2", "--ink:blue;--unused:3;--label:'hello';background-color:green",
        "--ink:green;--label:none;color:red", "--ink:red;--unused:4;--label:'again'", ""};
    for (size_t i=0;i<ARRLEN(updates);i++) {
        node_set_attr(root,"style",updates[i]);
        style_recalc(e,&d->node,false);
        if (!i) assert(e->stats_matched < 8); /* unreferenced variables avoid the plain history */
        assert(!strcmp(custom_get(plain->style->custom,"--unused"), i==0?"2":i==1?"3":i==3?"4":"1"));
        assert(!strcmp(custom_get(nested->style->custom,"--local"),"green"));
        assert(nested->style->color==RGBA(0,128,0,255));
        assert(used->style->color==(i==1?RGBA(0,0,255,255):i==2?RGBA(0,128,0,255):RGBA(255,0,0,255)));
        /* Compare incremental results with a complete, fresh cascade. */
        Color color[32],bg[32]; float font[32]; char *content[32]; int count=0;
        for(Node *n=d->html;n;n=node_next_in_tree(n,&d->node)) if(n->type==NODE_ELEMENT){
            assert(count<32);color[count]=n->style->color;bg[count]=n->style->bg_color;font[count]=n->style->font_size;
            content[count]=xstrdup(n->style->before&&n->style->before->content?n->style->before->content:"");count++;
        }
        style_recalc(e,&d->node,true);int j=0;
        for(Node *n=d->html;n;n=node_next_in_tree(n,&d->node)) if(n->type==NODE_ELEMENT){
            assert(color[j]==n->style->color&&bg[j]==n->style->bg_color&&font[j]==n->style->font_size);
            assert(!strcmp(content[j],n->style->before&&n->style->before->content?n->style->before->content:""));free(content[j]);j++;
        }
    }
    style_engine_free(e);doc_free(d);
}

static void test_deep_custom_rebase(void) {
    Document *d=doc_new("https://deep-custom.test/");
    const char *html="<html><body><section id='root'></section></body></html>";
    html_parse(d,html,strlen(html));
    Node *root=doc_get_element_by_id(d,"root"), *p=root;
    for(int i=0;i<40;i++) {
        Node *n=node_new_element(d,"div",NS_HTML);node_set_attr(n,"style","--local:green");node_append(p,n);p=n;
    }
    Node *leaf=node_new_element(d,"b",NS_HTML);node_append(p,leaf);
    StyleEngine *e=style_engine_new(d);
    const char *css="b{color:var(--ink)}";
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
    node_set_attr(root,"style","--ink:red;--unused:before");style_recalc(e,&d->node,true);
    for(int pass=0;pass<6;pass++) {
        node_set_attr(root,"style",pass%2?"--ink:blue;--unused:after":"--ink:red;--unused:after");
        style_recalc(e,&d->node,false);
        assert(leaf->style->color==(pass%2?RGBA(0,0,255,255):RGBA(255,0,0,255)));
        for(Node *n=root;n;n=node_next_in_tree(n,root)) if(n->type==NODE_ELEMENT)
            assert(!strcmp(custom_get(n->style->custom,"--unused"),"after"));
    }
    style_engine_free(e);doc_free(d);
}

static int descendant_style_events;
static void count_descendant_style(Node *n, const ComputedStyle *old, const ComputedStyle *now) {
    (void)old;(void)now;if(n->id&&!strcmp(n->id,"leaf"))descendant_style_events++;
}
static void test_inherited_context_changes(void) {
    Document *d=doc_new("https://inherited-context.test/");
    const char *html="<html><body><section id='root'><div style='font-size:12px'><p id='leaf'>text</p></div></section></body></html>";
    html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);
    const char *css="#leaf{width:2rem}";
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
    style_recalc(e,&d->node,true);Node *leaf=doc_get_element_by_id(d,"leaf"),*root=doc_get_element_by_id(d,"root");
    node_set_attr(d->html,"style","font-size:20px");style_recalc(e,&d->node,false);
    assert(leaf->style->width.px==40&&leaf->style->font_size==12);
    css_style_change_hook=count_descendant_style;descendant_style_events=0;
    node_set_attr(root,"style","display:none");style_recalc(e,&d->node,false);assert(descendant_style_events==1);
    descendant_style_events=0;node_set_attr(root,"style","");style_recalc(e,&d->node,false);assert(descendant_style_events==1);
    css_style_change_hook=NULL;style_engine_free(e);doc_free(d);
}

static void test_empty_dependency_selectors(void) {
    const char *selectors[] = {"p:empty", "p:not(:empty)", "section:has(p:empty) + aside",
        ":is(section:has(:empty)) + aside", ":not(:has(p:empty))", "section:not(:empty) p",
        "p:empty + .leaf", ":nth-child(1 of :not(:empty))", "section:has(p:empty) .leaf",
        "section > :not(:empty) ~ :not(:empty)", "section > :is(p:empty,b)",
        "section > :not(:is(p:empty)) + *", "section > :nth-child(1 of :not(:empty))"};
    for (size_t i = 0; i < ARRLEN(selectors); i++) {
        Document *d = doc_new("https://empty-dependency.test/");
        const char *html = "<html><body><section><p id='p'>a</p><b class='leaf'>b</b></section><aside>c</aside></body></html>";
        html_parse(d, html, strlen(html));
        StyleEngine *e = style_engine_new(d); char css[256];
        snprintf(css,sizeof css,"%s {background-color:rgb(11,22,33)}", selectors[i]);
        style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
        style_recalc(e,&d->node,true);
        SelList expected = {0}; assert(css_parse_selector_list(selectors[i],&expected));
        Node *text = doc_get_element_by_id(d,"p")->first;
        for (int pass = 0; pass < 4; pass++) {
            node_set_character_data(text, pass % 2 ? "a" : "", pass % 2);
            style_recalc(e,&d->node,false);
            for (Node *n=d->html;n;n=node_next_in_tree(n,&d->node)) if (n->type == NODE_ELEMENT)
                assert((n->style->bg_color == RGBA(11,22,33,255)) == css_match_selector_list(&expected,n));
        }
        Node *p = text->parent;
        for (int pass = 0; pass < 4; pass++) {
            if (pass % 2) node_append(p, text); else node_remove(text);
            style_recalc(e,&d->node,false);
            for (Node *n=d->html;n;n=node_next_in_tree(n,&d->node)) if (n->type == NODE_ELEMENT)
                assert((n->style->bg_color == RGBA(11,22,33,255)) == css_match_selector_list(&expected,n));
        }
        css_sellist_free(&expected);style_engine_free(e);doc_free(d);
    }
    Document *d = doc_new("https://empty-history.test/");
    const char *html = "<html><body><main id='history'></main><section data-spacing><p id='p'>a</p><b>b</b></section></body></html>";
    html_parse(d, html, strlen(html));
    Node *history = doc_get_element_by_id(d, "history"), *p = doc_get_element_by_id(d, "p");
    for (int i = 0; i < 1000; i++) node_append(history, node_new_element(d, "div", NS_HTML));
    StyleEngine *e = style_engine_new(d);
    const char *css = "[data-spacing] > :not(:empty) ~ :not(:empty){background-color:red}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    for (int i = 0; i < 4; i++) {
        node_set_character_data(p->first, i % 2 ? "a" : "", i % 2);
        style_recalc(e, &d->node, false);
        assert(e->stats_matched <= 2); /* only the edited paragraph and dependent sibling */
        assert((p->next->style->bg_color == RGBA(255,0,0,255)) == (i % 2 != 0));
    }
    style_engine_free(e); doc_free(d);
}

static void test_character_data_invalidation(void) {
    Document *d = doc_new("https://text-invalidation.test/");
    const char *html = "<html><body><section><p id='p'>a</p></section><aside></aside></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = "p:empty {color:red} section:has(p:empty) + aside {color:blue}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *p = doc_get_element_by_id(d, "p"), *t = p->first;
    Node *aside = css_query(&d->node, "aside", false, NULL);
    uint64_t layout = d->layout_version;
    const char bytes[] = {'b', 0, 'c'};
    node_set_character_data(t, bytes, sizeof bytes);
    style_recalc(e, &d->node, false);
    assert(e->stats_matched == 0 && d->layout_version > layout);
    assert(t->text_len == sizeof bytes && !memcmp(t->text, bytes, sizeof bytes));
    node_set_character_data(t, "", 0);
    style_recalc(e, &d->node, false);
    assert(p->style->color == RGBA(255,0,0,255));
    assert(aside->style->color == RGBA(0,0,255,255));
    assert(e->stats_matched == 2); /* parent and affected aside, not the whole history */
    node_set_character_data(t, " ", 1);
    style_recalc(e, &d->node, false);
    assert(p->style->color != RGBA(255,0,0,255));
    assert(aside->style->color != RGBA(0,0,255,255));
    Node *comment = node_new_comment(d, "", 0); node_append(p, comment);
    style_recalc(e, &d->node, false);
    node_set_character_data(comment, "comment", 7);
    style_recalc(e, &d->node, false);
    assert(e->stats_matched == 0);
    style_engine_free(e); doc_free(d);
}

static void test_attribute_subject_invalidation(void) {
    const char *selectors[] = {
        "[data-state] .target", ".target:not([data-state])",
        ".target:has([data-state])", "[data-state] + .target",
        ":is([data-state] ~ .target,.absent)",
        ".target:nth-child(1 of :not([data-state]))", "[DATA-STATE] .target",
        "[data-state] .\\[odd\\]", "[data-state] *", ":has([data-state])",
        ":not(:is([data-state])) .target", ":where(:not([data-state]))",
        ":where(main:has(> p[data-state]) .target)",
        ":where(main:has(> p:not([data-state])) .target)",
        ":not(:where(main:has(> p[data-state]) .target))",
        "main:has(> [data-state] + p) > .target",
        ":is(main:has([data-state]) > p,.absent)",
        "[data-state] .target:has([data-state])",
        ":where(main > :nth-child(1 of [data-state]) .target)",
        ":where(main:has(> p[data-state]) > p:not(:has([data-state])))"
    };
    for (size_t k = 0; k < ARRLEN(selectors); k++) {
        Document *d = doc_new("https://attribute-invalidation.test/");
        const char *html = "<html><body><main class='target [odd]' id='root'><p class='target [odd]' id='change'><b class='target [odd]'></b></p><p class='target [odd]'></p><i></i></main></body></html>";
        html_parse(d, html, strlen(html));
        StyleEngine *e = style_engine_new(d);
        char css[512]; snprintf(css, sizeof css, "%s{color:red;background-color:blue}", selectors[k]);
        style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
        style_recalc(e, &d->node, true);
        Node *change = doc_get_element_by_id(d, "change");
        for (int pass = 0; pass < 4; pass++) {
            Node *n = pass < 2 ? change : change->parent;
            if (pass % 2) node_remove_attr(n, "data-state"); else node_set_attr(n, "data-state", "on");
            assert(css_query(&d->node, "[data-state]", false, NULL) == (pass % 2 ? NULL : n));
            style_recalc(e, &d->node, false);
            Color colors[20], backgrounds[20]; int count = 0;
            for (Node *c = d->html; c; c = node_next_in_tree(c, &d->node)) if (c->type == NODE_ELEMENT) {
                assert(count < 20); colors[count] = c->style->color; backgrounds[count++] = c->style->bg_color;
            }
            style_recalc(e, &d->node, true); count = 0;
            for (Node *c = d->html; c; c = node_next_in_tree(c, &d->node)) if (c->type == NODE_ELEMENT) {
                assert(c->style->color == colors[count]); assert(c->style->bg_color == backgrounds[count++]);
            }
        }
        style_engine_free(e); doc_free(d);
    }
    Document *d = doc_new("https://attribute-cost.test/");
    const char *html = "<html><body><main id='root'><button class='indicator'></button><p id='label' style='--label:attr(ARIA-LABEL)'></p></main></body></html>";
    html_parse(d, html, strlen(html));
    Node *root = doc_get_element_by_id(d, "root"), *label = doc_get_element_by_id(d, "label");
    for (int i = 0; i < 1000; i++) node_append(root, node_new_element(d, "section", NS_HTML));
    StyleEngine *e = style_engine_new(d);
    const char *css = "[data-scroll-from-end] .indicator{color:red} #label::before{content:var(--label)} [data-self]{background-color:red}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    uint64_t version = d->layout_version;
    node_set_attr(root, "data-self", "");
    style_recalc(e, &d->node, false);
    assert(e->stats_matched == 1 && e->stats_reused >= 1000);
    assert(root->style->bg_color == RGBA(255,0,0,255));
    node_remove_attr(root, "data-self"); style_recalc(e, &d->node, false);
    assert(e->stats_matched == 1 && root->style->bg_color != RGBA(255,0,0,255));
    node_set_attr(root, "data-scroll-from-end", "1");
    style_recalc(e, &d->node, false);
    assert(e->stats_matched == 1 && root->first->style->color == RGBA(255,0,0,255));
    assert(d->layout_version == version);
    node_set_attr(root, "data-unreferenced", "x");
    style_recalc(e, &d->node, false); assert(e->stats_matched == 0);
    node_set_attr(label, "aria-label", "caption");
    style_recalc(e, &d->node, false);
    assert(label->style->before && !strcmp(label->style->before->content, "caption"));
    node_remove_attr(label, "aria-label");
    style_recalc(e, &d->node, false);
    assert(label->style->before && !strcmp(label->style->before->content, ""));
    /* Newly added rules cannot use a stale dependency index. */
    css = "[data-fresh] .indicator{color:blue}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    node_set_attr(root, "data-fresh", "");
    style_recalc(e, &d->node, false); assert(root->first->style->color == RGBA(0,0,255,255));
    Node *host = node_new_element(d, "div", NS_HTML);
    Node *shadow = node_new_fragment(d); shadow->host = host; shadow->refcount = 1; host->shadow_root = shadow;
    Node *sheet_node = node_new_element(d, "style", NS_HTML);
    node_append(shadow, sheet_node);
    Node *target = node_new_element(d, "span", NS_HTML); node_set_attr(target, "class", "shadow-target");
    node_append(shadow, target); node_append(root, host);
    css = ":host([aria-expanded]) .shadow-target{color:red}";
    StyleSheet *sheet = css_parse_sheet(css, strlen(css), d->url, 1, &e->media); sheet->owner = sheet_node;
    style_engine_add_sheet(e, sheet); style_recalc(e, &d->node, true);
    node_set_attr(host, "aria-expanded", "true"); style_recalc(e, &d->node, false);
    assert(target->style->color == RGBA(255,0,0,255));
    node_remove_attr(host, "aria-expanded"); style_recalc(e, &d->node, false);
    assert(target->style->color != RGBA(255,0,0,255));
    Node *light = node_new_element(d, "p", NS_HTML), *text = node_new_text(d, "a", 1);
    node_append(light, text); node_append(host, light);
    css = ":host(:has(p:empty)) .shadow-target{background-color:red}";
    sheet = css_parse_sheet(css, strlen(css), d->url, 1, &e->media); sheet->owner = sheet_node;
    style_engine_add_sheet(e, sheet); style_recalc(e, &d->node, true);
    for (int i = 0; i < 4; i++) {
        node_set_character_data(text, i % 2 ? "a" : "", i % 2);
        style_recalc(e, &d->node, false);
        assert((target->style->bg_color == RGBA(255,0,0,255)) == (i % 2 == 0));
    }
    css = ":host(:has(.inserted)) .shadow-target{background-color:blue}";
    sheet = css_parse_sheet(css, strlen(css), d->url, 1, &e->media); sheet->owner = sheet_node;
    style_engine_add_sheet(e, sheet); style_recalc(e, &d->node, true);
    Node *inserted = node_new_element(d, "i", NS_HTML); node_set_attr(inserted, "class", "inserted");
    node_append(light, inserted); style_recalc(e, &d->node, false);
    assert(target->style->bg_color == RGBA(0,0,255,255));
    node_remove(inserted); style_recalc(e, &d->node, false);
    assert(target->style->bg_color != RGBA(0,0,255,255)); node_free_tree(inserted);
    style_engine_free(e); doc_free(d);

    /* An unkeyed :where on the composer must not dirty the long history. */
    d = doc_new("https://attribute-container-cost.test/");
    html = "<html><body><main id='history'></main><div data-composer><p id='edit'></p><button></button></div></body></html>";
    html_parse(d, html, strlen(html));
    Node *history = doc_get_element_by_id(d, "history"), *edit = doc_get_element_by_id(d, "edit");
    for (int i = 0; i < 1000; i++) node_append(history, node_new_element(d, "section", NS_HTML));
    e = style_engine_new(d);
    css = ":where([data-composer]:has(> p[data-empty-paragraph]) > button){background-color:red}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    for (int i = 0; i < 20; i++) {
        if (i % 2) node_remove_attr(edit, "data-empty-paragraph");
        else node_set_attr(edit, "data-empty-paragraph", "");
        style_recalc(e, &d->node, false);
        assert(e->stats_matched <= 2);
        assert((edit->next->style->bg_color == RGBA(255,0,0,255)) == (i % 2 == 0));
    }
    /* Changing the container's own attribute still reaches its button. */
    node_remove_attr(edit->parent, "data-composer");
    style_recalc(e, &d->node, false);
    node_set_attr(edit, "data-empty-paragraph", "");
    style_recalc(e, &d->node, false);
    node_set_attr(edit->parent, "data-composer", "");
    style_recalc(e, &d->node, false);
    assert(edit->next->style->bg_color == RGBA(255,0,0,255));
    node_remove_attr(edit->parent, "data-composer");
    style_recalc(e, &d->node, false);
    assert(edit->next->style->bg_color != RGBA(255,0,0,255));
    style_engine_free(e); doc_free(d);
}

static void assert_incremental_matches_full(StyleEngine *e) {
    Node *nodes[128]; Color colors[128], backgrounds[128]; int count = 0;
    style_recalc(e, &e->doc->node, false);
    for (Node *n = e->doc->node.first; n; n = node_next_deep(n, &e->doc->node)) {
        if (n->type != NODE_ELEMENT) continue;
        assert(count < 128 && n->style);
        nodes[count] = n; colors[count] = n->style->color; backgrounds[count++] = n->style->bg_color;
    }
    style_recalc(e, &e->doc->node, true);
    for (int i = 0; i < count; i++) {
        assert(nodes[i]->style->color == colors[i]);
        assert(nodes[i]->style->bg_color == backgrounds[i]);
    }
}

static void test_child_list_invalidation(void) {
    const char *selectors[] = {
        ".group > p:first-child", ".group > p:last-child span", "p:only-child",
        "p:first-of-type", "p:last-of-type", "p:only-of-type",
        "p:nth-child(2n) span", "p:nth-last-child(2n+1)", "p:nth-of-type(2)", "p:nth-last-of-type(2)",
        "p:nth-child(2 of .chosen)", "p:nth-child(1 of :not(:empty)) span",
        ".group > p + p span", ".group > p ~ p", ".group > p + p ~ p span",
        ":not(.chosen + p) span", ":is(.group > p + p) span",
        ".group:has(.chosen) + aside", ".group:not(:has(.chosen)) span",
        ".group:has(> p:last-child) span", ".group:has(+ aside)",
        "p:empty + p span", ".group > :not(:empty) ~ :not(:empty)",
        ".group:focus-within + aside", ".group:focus-within span",
        ".other span", ".group .chosen span", ".other:lang(fr) span",
        ":has(.chosen)", ":not(:has(.chosen))", ":has(.chosen) + aside", ":has(.chosen) ~ section",
        "p:has(+ .chosen)", "p:has(~ .chosen)", "p:not(:has(+ .chosen)) span",
        ":is(:has(.chosen),.unused)", ":not(:is(:has(.chosen),.unused))",
        ":has(.chosen) p:has(span)", "p:has(span):not(:has(.unused))",
        ":not(:has(.chosen) > span)", "p:nth-child(1 of :has(span))",
        ":is(.group:has(.chosen) span)", ":where(.group:has(.chosen) > p,.other p)",
        ":where([id]:has(> p) [class]:not([hidden]))",
        ":is(:where([id]:has(.chosen) > p),aside)",
        ":where(.group > :not(:last-child)) span",
        ":where(.group > p + p) span",
        ".group:has(p:first-child) span", ".group:has(.chosen + p) span",
        ".group:has(p:has(span)) span", ".group:has([style]) span",
        ".group:has(p:not(.chosen)) span", ".group:has(p,aside) span",
        ".group:has(span:focus) span", ":where(.group:focus-within span)"
    };
    for (size_t k = 0; k < ARRLEN(selectors); k++) {
        Document *d = doc_new("https://tree-invalidation.test/");
        const char *html = "<html><body><main class='group' id='group'><p class='chosen'><span>a</span></p><p><span>b</span></p><p class='chosen'><span>c</span></p></main><aside></aside><section class='other' id='other' lang='fr'></section></body></html>";
        html_parse(d, html, strlen(html));
        StyleEngine *e = style_engine_new(d);
        char css[512]; snprintf(css, sizeof css, "%s{color:red;background-color:blue}", selectors[k]);
        style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
        Node *group = doc_get_element_by_id(d, "group"), *other = doc_get_element_by_id(d, "other");
        Node *first = group->first, *last = group->last;
        d->focus = first->first;
        style_recalc(e, &d->node, true);
        Node *added = node_new_element(d, "p", NS_HTML);
        node_set_attr(added, "class", "chosen");
        node_append(added, node_new_element(d, "span", NS_HTML));
        node_insert_before(group, added, group->first);
        assert_incremental_matches_full(e);
        node_append(group, added); assert_incremental_matches_full(e);
        node_append(other, first); assert_incremental_matches_full(e); /* moved ancestry and focus removal */
        node_remove(last); assert_incremental_matches_full(e);
        node_append(group, last); assert_incremental_matches_full(e);
        node_set_text_content(last, ""); assert_incremental_matches_full(e);
        node_append(last, node_new_text(d, "text", 4)); assert_incremental_matches_full(e);
        node_remove(added); assert_incremental_matches_full(e); node_free_tree(added);
        style_engine_free(e); doc_free(d);
    }
    Document *d = doc_new("https://streaming-tree-cost.test/");
    const char *html = "<html><body><main class='reply' id='reply'><p id='paragraph'></p></main><aside id='history'></aside></body></html>";
    html_parse(d, html, strlen(html));
    Node *reply = doc_get_element_by_id(d, "reply"), *paragraph = doc_get_element_by_id(d, "paragraph");
    Node *history = doc_get_element_by_id(d, "history");
    for (int i = 0; i < 1000; i++) {
        node_append(paragraph, node_new_element(d, "span", NS_HTML));
        node_append(history, node_new_element(d, "section", NS_HTML));
    }
    StyleEngine *e = style_engine_new(d);
    const char *css = ".reply > p:last-child{color:red} .reply > :not(:empty) ~ :not(:empty){background-color:blue} .other:has(span) > p{color:blue}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *span = node_new_element(d, "span", NS_HTML);
    node_set_attr(span, "class", "streamed"); node_append(paragraph, span);
    style_recalc(e, &d->node, false);
    assert(e->stats_matched <= 2); /* unchanged inherited styles may be skipped entirely */
    assert(span->style->color == RGBA(255,0,0,255));
    /* An unkeyed :has must not turn every subsequent insertion into a full
     * document cascade. Only possible :has subjects need recalculation. */
    css = ":has(.streamed){background-color:green} :is(.reply:has(.streamed) > p){background-color:green} "
        ":where(div[data-radix-popper-content-wrapper]:has(> div[data-side]) [data-radix-collection-item]:not([data-fill])){color:blue} "
        ":where(.space > :not(:last-child)){color:blue}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    for (int i = 0; i < 40; i++) {
        Node *next = node_new_element(d, "span", NS_HTML);
        node_append(next, node_new_text(d, "streaming", 9));
        node_append(paragraph, next); style_recalc(e, &d->node, false);
        assert(e->stats_matched <= 8); /* unaffected history may be skipped entirely */
        assert(next->style->color == RGBA(255,0,0,255));
    }
    Node *p = node_new_element(d, "p", NS_HTML); node_append(reply, p);
    style_recalc(e, &d->node, false);
    assert(paragraph->style->color != RGBA(255,0,0,255));
    assert(p->style->color == RGBA(255,0,0,255));
    /* The inherited color change must still reach every existing inline. */
    for (Node *n = paragraph->first; n; n = n->next) assert(n->style->color == paragraph->style->color);
    css = ":first-child{background-color:green}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *popup = node_new_element(d, "div", NS_HTML); node_append(d->body, popup);
    style_recalc(e, &d->node, false);
    assert(e->stats_matched < 10 && e->stats_reused >= 2000);
    node_remove(popup); style_recalc(e, &d->node, false);
    assert(e->stats_matched < 10); node_free_tree(popup);
    Node *detached = node_new_fragment(d);
    node_append(detached, node_new_element(d, "p", NS_HTML));
    style_recalc(e, &d->node, false);
    assert(e->stats_matched == 0); node_free_tree(detached);
    style_engine_free(e); doc_free(d);

    /* An unrelated insert must not restyle history through a body :has rule,
     * including descendants which cannot use inheritance-only reuse. Removing
     * an actual witness must still update inherited colors after it is freed. */
    d = doc_new("https://tree-witness-cost.test/");
    html = "<html><body><main id='reply'><p id='paragraph'></p></main><aside id='history'></aside></body></html>";
    html_parse(d, html, strlen(html));
    paragraph = doc_get_element_by_id(d, "paragraph"); history = doc_get_element_by_id(d, "history");
    for (int i = 0; i < 1000; i++) node_append(history, node_new_element(d, "section", NS_HTML));
    e = style_engine_new(d);
    css = "body:has(> .drag-ghost){color:red} section[style]{background-color:blue} :where(body:focus-within section){background-color:green} "
        "body:has(> .hover-ghost:hover){color:purple} body:has(> .active-ghost:active){color:purple}";
    d->focus = paragraph;
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    for (int i = 0; i < 40; i++) {
        node_append(paragraph, node_new_element(d, "span", NS_HTML));
        style_recalc(e, &d->node, false); assert(e->stats_matched <= 2);
    }
    for (int i = 0; i < 2; i++) {
        Node *state = node_new_element(d, "div", NS_HTML);
        node_set_attr(state, "class", i ? "active-ghost" : "hover-ghost");
        unsigned flag = i ? NF_ACTIVE : NF_HOVER;
        state->flags |= flag; node_append(d->body, state);
        style_recalc(e, &d->node, false);
        assert(history->first->style->color == RGBA(128,0,128,255));
        /* A coalesced state change must bypass the subtree-key filter. */
        state->flags &= ~flag; doc_mark_style_dirty(d, state);
        node_append(paragraph, node_new_element(d, "span", NS_HTML));
        style_recalc(e, &d->node, false);
        assert(history->first->style->color != RGBA(128,0,128,255));
        state->flags |= flag; doc_mark_style_dirty(d, state);
        node_append(paragraph, node_new_element(d, "span", NS_HTML));
        style_recalc(e, &d->node, false);
        assert(history->first->style->color == RGBA(128,0,128,255));
        node_remove(state); node_free_tree(state);
        style_recalc(e, &d->node, false);
        assert(history->first->style->color != RGBA(128,0,128,255));
    }
    Node *ghost = node_new_element(d, "div", NS_HTML); node_set_attr(ghost, "class", "drag-ghost");
    node_append(d->body, ghost); style_recalc(e, &d->node, false);
    assert(history->first->style->color == RGBA(255,0,0,255));
    node_remove(ghost); node_free_tree(ghost); style_recalc(e, &d->node, false);
    assert(history->first->style->color != RGBA(255,0,0,255));
    assert(history->first->style->bg_color == RGBA(0,128,0,255));
    node_remove(paragraph); node_free_tree(paragraph); style_recalc(e, &d->node, false);
    assert(!d->focus && history->first->style->bg_color != RGBA(0,128,0,255));
    style_engine_free(e); doc_free(d);
}

static void test_coalesced_selector_tree_changes(void) {
    Document *d = doc_new("https://coalesced-tree-cost.test/");
    const char *html = "<html><body><main id='reply'><p id='paragraph'></p></main><aside id='history'></aside><div id='toggle'></div></body></html>";
    html_parse(d, html, strlen(html));
    Node *paragraph = doc_get_element_by_id(d, "paragraph"), *history = doc_get_element_by_id(d, "history");
    Node *toggle = doc_get_element_by_id(d, "toggle");
    node_set_attr(history, "class", "prose");
    for (int i = 0; i < 1000; i++) node_append(history, node_new_element(d, "section", NS_HTML));
    StyleEngine *e = style_engine_new(d);
    const char *css = "body:has(> .drag-ghost){color:red} body:has(> #special){background-color:blue} "
        "section[style]{color:green} "
        ".prose :where(hr + *):not(:where([class~='not-prose'],[class~='not-prose'] *)){color:blue} "
        "#scope :where(hr + *):not([id]){color:red}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    for (int i = 0; i < 20; i++) {
        node_set_attr(toggle, "class", i % 2 ? "stable active" : "stable inactive");
        Node *span = node_new_element(d, "span", NS_HTML); node_append(paragraph, span);
        style_recalc(e, &d->node, false);
        assert(e->stats_matched < 30); /* no cascade through the thousand-message history */
        assert(history->first->style->color != RGBA(255,0,0,255));
        node_remove(span); node_free_tree(span); style_recalc(e, &d->node, false);
    }
    /* Adding a portal to body must not force styles through all old replies,
     * even alongside an unrelated class update that prevents inherited reuse. */
    for (int i = 0; i < 10; i++) {
        node_set_attr(toggle, "class", i % 2 ? "active" : "inactive");
        Node *portal = node_new_element(d, "div", NS_HTML); node_append(d->body, portal);
        style_recalc(e, &d->node, false); assert(e->stats_matched <= 2);
        node_remove(portal); node_free_tree(portal);
        style_recalc(e, &d->node, false); assert(e->stats_matched == 0);
        assert(history->first->style->color != RGBA(255,0,0,255));
    }
    for (int i = 0; i < 4; i++) {
        if (i % 2) node_remove_attr(toggle, "class"); else node_set_attr(toggle, "class", "drag-ghost");
        node_set_attr(toggle, "id", i % 2 ? "toggle" : "special");
        Node *span = node_new_element(d, "span", NS_HTML); node_append(paragraph, span);
        style_recalc(e, &d->node, false);
        assert((history->first->style->color == RGBA(255,0,0,255)) == (i % 2 == 0));
        assert((d->body->style->bg_color == RGBA(0,0,255,255)) == (i % 2 == 0));
        node_remove(span); node_free_tree(span); style_recalc(e, &d->node, false);
    }
    style_engine_free(e); doc_free(d);
    const char *selectors[] = {
        ".outer:has(.witness) + aside", ":not(.outer:has(.witness)) + aside",
        "main:has(:is(.witness,#special)) + aside", "main:has([class]) + aside",
        "main:has([id]) + aside", "main:focus-within + aside",
        "main:has([class~='witness']) + aside", "main:has([id='special']) + aside",
        "main:has([class~='WITNESS' i]) + aside", "main:has([id='SPECIAL' i]) + aside"
    };
    for (size_t k = 0; k < ARRLEN(selectors); k++) {
        d = doc_new("https://coalesced-tree-semantics.test/");
        html = "<html><body><main class='outer' id='container'><p class='witness' id='special'></p></main><aside></aside><footer></footer></body></html>";
        html_parse(d, html, strlen(html)); e = style_engine_new(d);
        char rule[256]; snprintf(rule, sizeof rule, "%s{color:red;background-color:blue}", selectors[k]);
        style_engine_add_sheet(e, css_parse_sheet(rule, strlen(rule), d->url, 1, &e->media));
        Node *container = doc_get_element_by_id(d, "container"), *witness = container->first;
        style_recalc(e, &d->node, true);
        for (int i = 0; i < 6; i++) {
            if (i < 2) node_set_attr(container, "class", i % 2 ? "outer" : "other");
            else if (i < 4) { if (i % 2) node_set_attr(witness, "class", "witness"); else node_remove_attr(witness, "class"); }
            else { if (i % 2) node_set_attr(witness, "id", "special"); else node_remove_attr(witness, "id"); }
            Node *span = node_new_element(d, "span", NS_HTML); node_append(container, span);
            assert_incremental_matches_full(e);
            node_remove(span); node_free_tree(span); assert_incremental_matches_full(e);
        }
        /* Generic state changes must retain the conservative fallback. */
        d->focus = witness; doc_mark_style_dirty(d, witness);
        node_append(container, node_new_element(d, "span", NS_HTML)); assert_incremental_matches_full(e);
        style_engine_free(e); doc_free(d);
    }
}

/* A subject's own class change marks it directly; coalescing it with an
 * unrelated child-list change must not widen the structural candidate. */
static void test_coalesced_subject_class_tree_changes(void) {
    Document *d = doc_new("https://coalesced-subject-cost.test/");
    const char *html = "<html><body><main id='reply'></main><div id='toggle'></div></body></html>";
    html_parse(d, html, strlen(html));
    Node *reply = doc_get_element_by_id(d, "reply"), *toggle = doc_get_element_by_id(d, "toggle");
    for (int i = 0; i < 1000; i++) node_append(d->body, node_new_element(d, "section", NS_HTML));
    StyleEngine *e = style_engine_new(d);
    const char *css = ".mark:first-child{color:red} .mark{background-color:blue}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    for (int i = 0; i < 6; i++) {
        node_set_attr(toggle, "class", i % 2 ? "" : "mark");
        Node *span = node_new_element(d, "span", NS_HTML); node_append(reply, span);
        style_recalc(e, &d->node, false);
        assert(e->stats_matched < 10);
        assert((toggle->style->bg_color == RGBA(0,0,255,255)) == (i % 2 == 0));
        node_remove(span); node_free_tree(span); style_recalc(e, &d->node, false);
    }
    style_engine_free(e); doc_free(d);
    const char *selectors[] = {".chosen:first-child", "p.chosen:nth-child(2)", ".chosen:first-child span",
        "#special:last-child", "p[data-x]:first-child", ".group > .chosen:first-child", ".chosen:not(:first-child)"};
    for (size_t k = 0; k < ARRLEN(selectors); k++) {
        d = doc_new("https://coalesced-subject-semantics.test/");
        html = "<html><body><main class='group'><p><span></span></p><p><span></span></p></main><aside></aside></body></html>";
        html_parse(d, html, strlen(html)); e = style_engine_new(d);
        char rule[256]; snprintf(rule, sizeof rule, "%s{color:red;background-color:blue}", selectors[k]);
        style_engine_add_sheet(e, css_parse_sheet(rule, strlen(rule), d->url, 1, &e->media));
        Node *group = d->body->first, *first = group->first, *second = first->next;
        style_recalc(e, &d->node, true);
        for (int i = 0; i < 8; i++) {
            Node *target = i & 2 ? second : first;
            if (i % 2) { node_remove_attr(target, "class"); node_remove_attr(target, "id"); node_remove_attr(target, "data-x"); }
            else { node_set_attr(target, "class", "chosen"); node_set_attr(target, "id", "special"); node_set_attr(target, "data-x", "1"); }
            Node *p = node_new_element(d, "p", NS_HTML);
            if (i & 4) node_append(group, p); else node_insert_before(group, p, group->first);
            assert_incremental_matches_full(e);
            node_remove(p); node_free_tree(p); assert_incremental_matches_full(e);
        }
        style_engine_free(e); doc_free(d);
    }
}

/* Adding or removing a sheet restyles only what its rules can match. */
static void test_sheet_change_targets_subjects(void) {
    Document *d = doc_new("https://sheet-delta-cost.test/");
    const char *html = "<html><body><main id='reply'><p class='msg'><b>x</b></p></main><div id='toggle'></div></body></html>";
    html_parse(d, html, strlen(html));
    for (int i = 0; i < 1000; i++) {
        Node *s = node_new_element(d, "section", NS_HTML); node_set_attr(s, "class", "old");
        node_append(d->body, s);
    }
    StyleEngine *e = style_engine_new(d);
    const char *base = ".old{color:green} .msg b{color:blue} #reply.on + div{padding-left:1px} .msg[data-x] b{padding-top:1px}";
    style_engine_add_sheet(e, css_parse_sheet(base, strlen(base), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *reply = doc_get_element_by_id(d, "reply"), *msg = reply->first, *toggle = doc_get_element_by_id(d, "toggle");
    for (int i = 0; i < 4; i++) {
        Node *owner = node_new_element(d, "style", NS_HTML); node_append(d->head, owner);
        const char *css = i % 2 ? "#toggle{color:red} .msg b:first-child{background-color:blue}" : ".msg{color:red} div.none, p.msg > b{background-color:blue}";
        StyleSheet *sh = css_parse_sheet(css, strlen(css), d->url, 1, &e->media); sh->owner = owner;
        style_engine_add_sheet(e, sh);
        /* mutations while the sheet change is pending use the current rules */
        node_set_attr(reply, "class", i % 2 ? "" : "on");
        if (i % 2) node_remove_attr(msg, "data-x"); else node_set_attr(msg, "data-x", "1");
        style_recalc(e, &d->node, false);
        assert(e->stats_matched < 10);
        assert((toggle->style->padding[3].px == 1) == (i % 2 == 0) && (msg->first->style->padding[0].px == 1) == (i % 2 == 0));
        assert(msg->first->style->bg_color == RGBA(0,0,255,255));
        assert((i % 2 ? toggle : msg)->style->color == RGBA(255,0,0,255));
        style_engine_remove_owner(e, owner);
        style_recalc(e, &d->node, false);
        assert(e->stats_matched < 10);
        assert(msg->first->style->bg_color != RGBA(0,0,255,255));
        assert(toggle->style->color != RGBA(255,0,0,255) && msg->style->color != RGBA(255,0,0,255));
        node_remove(owner); node_free_tree(owner);
    }
    style_engine_free(e); doc_free(d);
    const char *sheets[] = {".chosen span{color:red}", "#special{color:red}", "p{color:red}", ":root{color:red}",
        "*{background-color:blue}", ":is(.chosen) > span{color:red}", ".group :where(span){color:red}",
        ".group > p + p{color:red}", "[data-x]{color:red}", ":root{--c:red} span{color:var(--c)}",
        "span::before{content:'x';color:red}", ".chosen:has(span){background-color:blue}"};
    for (size_t k = 0; k < ARRLEN(sheets); k++) {
        d = doc_new("https://sheet-delta-semantics.test/");
        html = "<html><body><main class='group'><p class='chosen' id='special' data-x='1'><span></span></p><p><span></span></p></main><aside><span></span></aside></body></html>";
        html_parse(d, html, strlen(html)); e = style_engine_new(d);
        const char *base2 = "span{color:green} :root{--c:green}";
        style_engine_add_sheet(e, css_parse_sheet(base2, strlen(base2), d->url, 1, &e->media));
        style_recalc(e, &d->node, true);
        for (int i = 0; i < 3; i++) {
            Node *owner = node_new_element(d, "style", NS_HTML); node_append(d->head, owner);
            StyleSheet *sh = css_parse_sheet(sheets[k], strlen(sheets[k]), d->url, 1, &e->media); sh->owner = owner;
            style_engine_add_sheet(e, sh);
            if (i == 1) node_set_attr(d->body->first->first, "class", "other");
            if (i == 2) node_set_attr(d->body->first->first, "class", "chosen");
            assert_incremental_matches_full(e);
            style_engine_remove_owner(e, owner); assert_incremental_matches_full(e);
            node_remove(owner); node_free_tree(owner);
        }
        style_engine_free(e); doc_free(d);
    }
}

/* A restyled parent's children reuse their matched rules unless a selector
 * change reaches them: through their subtree, a sibling relation or :has().
 * An unrelated class change in the same batch must not force a full rematch. */
/* Focus moves restyle only the focused elements, their :focus-within
 * ancestors and dependents, without an unknown document-wide change. */
static void test_focus_state_changes(void) {
    const char *selectors[] = { "input:focus", ".p:focus-within", ".p:focus-within .c", ":focus + .c",
        ".p:focus-within > .q", ":is(.p:focus-within) em", "input:focus ~ em", "section:not(:focus-within) .c" };
    for (int si = 0; si < (int)(sizeof selectors / sizeof *selectors); si++) {
        Document *d = doc_new("https://focus.test/");
        const char *html = "<html><body><section class=p><div class=q><input id=a><b class=c></b><em></em></div><i class=c></i></section>"
            "<section class=p><input id=b><b class=c></b></section><span class=c></span></body></html>";
        html_parse(d, html, strlen(html));
        StyleEngine *e = style_engine_new(d);
        char css[160]; snprintf(css, sizeof css, "%s{color:red} .q{background-color:blue}", selectors[si]);
        style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
        style_recalc(e, &d->node, true);
        Node *a = doc_get_element_by_id(d, "a"), *b = doc_get_element_by_id(d, "b");
        doc_set_focus(d, a); assert(!d->selector_changes_unknown); assert_incremental_matches_full(e);
        doc_set_focus(d, b); assert_incremental_matches_full(e);
        doc_set_focus(d, NULL); assert_incremental_matches_full(e);
        doc_set_focus(d, a); doc_set_focus(d, b); assert_incremental_matches_full(e);
        style_engine_free(e); doc_free(d);
    }
    Document *d = doc_new("https://focus-cost.test/");
    SB html; sb_init(&html);
    sb_puts(&html, "<html><body><div class=p><input id=a></div>");
    for (int i = 0; i < 1000; i++) sb_puts(&html, "<section><b></b></section>");
    sb_puts(&html, "</body></html>");
    html_parse(d, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(d);
    const char *css = ".p:focus-within{color:red} input:focus{color:blue} section b{color:green} [data-lock] section{padding-top:1px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *a = doc_get_element_by_id(d, "a");
    /* A focus move with a body attribute change and a pending sheet. */
    doc_set_focus(d, a);
    node_set_attr(d->body, "style", "overflow:hidden");
    style_recalc(e, &d->node, false);
    assert(e->stats_matched < 20 && a->style->color == RGBA(0,0,255,255) && a->parent->style->color == RGBA(255,0,0,255));
    d->sheet_version++;
    node_set_attr(d->body, "data-lock", "");
    assert(!d->selector_changes_unknown);
    const char *extra = "[data-lock] b{padding-left:2px}";
    style_engine_add_sheet(e, css_parse_sheet(extra, strlen(extra), d->url, 1, &e->media));
    style_recalc(e, &d->node, false);
    Node *b = a->parent->next->first;
    assert(b->style->padding[3].px == 2 && b->parent->style->padding[0].px == 1 && b->style->color == RGBA(0,128,0,255));
    doc_set_focus(d, NULL);
    style_recalc(e, &d->node, false);
    assert(e->stats_matched < 20 && a->style->color != RGBA(0,0,255,255) && a->parent->style->color != RGBA(255,0,0,255));
    style_engine_free(e); doc_free(d);
    /* Unkeyed :focus-within dependents restyle only where their match flips. */
    d = doc_new("https://focus-within-cost.test/");
    sb_init(&html);
    sb_puts(&html, "<html><body><main><div class=p><input id=a><b class=c></b></div>");
    for (int i = 0; i < 1000; i++) sb_puts(&html, "<section><b></b></section>");
    sb_puts(&html, "</main></body></html>");
    html_parse(d, html.s, html.n); sb_free(&html);
    e = style_engine_new(d);
    css = ".p:focus-within > *{color:red} .x:focus-within *{color:blue} section b{color:green}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    a = doc_get_element_by_id(d, "a"); b = a->next;
    doc_set_focus(d, a);
    style_recalc(e, &d->node, false);
    assert(e->stats_matched < 20 && b->style->color == RGBA(255,0,0,255));
    doc_set_focus(d, NULL);
    style_recalc(e, &d->node, false);
    assert(e->stats_matched < 20 && b->style->color != RGBA(255,0,0,255));
    style_engine_free(e); doc_free(d);
}

/* Parent-keyed child rules and attribute alternatives leave the universal list. */
static void test_parent_keyed_rules(void) {
    Document *d = doc_new("https://parent.test/");
    SB html; sb_init(&html);
    sb_puts(&html, "<html><body><div class=fix><p></p><span></span></div><div data-h><i></i></div><div><p></p></div>"
        "<ul><li data-k><b></b></li><li><b></b></li></ul><pre></pre><q data-code></q><input><select></select>");
    sb_puts(&html, "</body></html>");
    html_parse(d, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(d);
    style_recalc(e, &d->node, true);
    int ua_universal = e->idx.universal.n;
    const char *css = ".md .fix > *{color:red} body > .fix > *{padding-top:1px} [data-h] > *:first-child{color:blue}"
        " :is(pre, [data-code]){color:green} :is([data-k], li[data-k]) > b{padding-left:2px}"
        " :is(input,input,input,input,input,input,input,input,input,select){color:rgb(1,2,3)}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    assert(e->idx.universal.n == ua_universal);
    Node *fix = d->body->first, *h = fix->next, *plain = h->next, *ul = plain->next, *pre = ul->next, *q = pre->next, *in = q->next, *sel = in->next;
    assert(fix->first->style->color != RGBA(255,0,0,255) && fix->first->style->padding[0].px == 1 && fix->first->next->style->padding[0].px == 1);
    assert(h->first->style->color == RGBA(0,0,255,255) && plain->first->style->color != RGBA(0,0,255,255) && plain->first->style->padding[0].px == 0);
    assert(pre->style->color == RGBA(0,128,0,255) && q->style->color == RGBA(0,128,0,255));
    assert(ul->first->first->style->padding[3].px == 2 && ul->first->next->first->style->padding[3].px == 0);
    assert(in->style->color == RGBA(1,2,3,255) && sel->style->color == RGBA(1,2,3,255));
    node_set_attr(d->body, "class", "md");
    node_set_attr(plain, "data-h", "");
    node_set_attr(ul->first->next, "data-k", "");
    style_recalc(e, &d->node, false);
    assert(fix->first->style->color == RGBA(255,0,0,255) && plain->first->style->color == RGBA(0,0,255,255));
    assert(ul->first->next->first->style->padding[3].px == 2);
    const char *extra = ".fix > span{padding-bottom:3px} [data-h] > i{padding-right:4px} :is(input,[data-code]){padding-left:5px}";
    style_engine_add_sheet(e, css_parse_sheet(extra, strlen(extra), d->url, 1, &e->media));
    style_recalc(e, &d->node, false);
    assert(fix->first->next->style->padding[2].px == 3 && h->first->style->padding[1].px == 4);
    assert(in->style->padding[3].px == 5 && q->style->padding[3].px == 5 && pre->style->padding[3].px == 0);
    style_engine_free(e); doc_free(d);
}

/* Explicit inherit of an inherited property keeps reuse; of others, not. */
static void test_explicit_inherit_reuse(void) {
    Document *d = doc_new("https://inherit.test/");
    SB html; sb_init(&html);
    sb_puts(&html, "<html><body><div class=p><i></i></div>");
    for (int i = 0; i < 1000; i++) sb_puts(&html, "<section><button><b></b></button></section>");
    sb_puts(&html, "</body></html>");
    html_parse(d, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(d);
    /* inherit of an unsupported property is ignored and reads nothing. */
    const char *css = "button{color:inherit;font:inherit;text-decoration:inherit;font-feature-settings:inherit;border-color:inherit}"
        " .lock{pointer-events:none;cursor:wait}"
        " .p{background-color:red} .on .p{background-color:blue;color:green} i{background-color:inherit}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *p = d->body->first, *it = p->first, *button = p->next->first;
    assert(it->style->bg_color == RGBA(255,0,0,255));
    node_set_attr(d->body, "class", "lock");
    style_recalc(e, &d->node, false);
    assert(e->stats_matched < 50);
    assert(button->style->pointer_events == 0 && button->first->style->pointer_events == 0);
    node_set_attr(d->body, "class", "on");
    style_recalc(e, &d->node, false);
    assert(it->style->bg_color == RGBA(0,0,255,255) && it->style->color == RGBA(0,128,0,255));
    assert(button->style->pointer_events != 0);
    style_engine_free(e); doc_free(d);
}

/* Deep custom chains (past CUSTOM_MAX_DEPTH, so flattened) under a large
 * root theme: incremental variable changes must match a full cascade. */
static void test_deep_custom_chains(void) {
    Document *d = doc_new("https://deep-custom.test/");
    SB html; sb_init(&html);
    sb_puts(&html, "<html><body><main>");
    for (int i = 0; i < 150; i++) sb_printf(&html, "<div class=l style='--lv:%d'>", i);
    sb_puts(&html, "<b></b>");
    for (int i = 0; i < 150; i++) sb_puts(&html, "</div>");
    sb_puts(&html, "</main></body></html>");
    html_parse(d, html.s, html.n); sb_free(&html);
    SB css; sb_init(&css);
    sb_puts(&css, ":root{--ink:red;");
    for (int i = 0; i < 400; i++) sb_printf(&css, "--t%d:%dpx;", i, i);
    sb_puts(&css, "} .l{color:var(--ink);padding-left:var(--lv)} .on{--ink:blue} .mid{--ink:green;--t7:9px} b{background-color:var(--ink);padding-top:var(--t7)}");
    StyleEngine *e = style_engine_new(d);
    style_engine_add_sheet(e, css_parse_sheet(css.s, css.n, d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *top = d->body->first->first, *mid = top, *b = top;
    for (int i = 0; i < 90; i++) mid = mid->first;
    while (b->first) b = b->first;
    const char *steps[][2] = {{"l on", NULL}, {NULL, "l mid"}, {"l", NULL}, {NULL, "l"}, {"l on", "l"}};
    for (size_t k = 0; k < ARRLEN(steps); k++) {
        if (steps[k][0]) node_set_attr(top, "class", steps[k][0]);
        if (steps[k][1]) node_set_attr(mid, "class", steps[k][1]);
        style_recalc(e, &d->node, false);
        Color got[256]; int count = 0; float pad[256];
        for (Node *n = top; n; n = n->first) { got[count] = n->style->color; pad[count++] = n->style->padding[3].px; }
        Color bbg = b->style->bg_color; float bpt = b->style->padding[0].px;
        style_recalc(e, &d->node, true);
        count = 0;
        for (Node *n = top; n; n = n->first) { assert(got[count] == n->style->color && pad[count] == n->style->padding[3].px); count++; }
        assert(bbg == b->style->bg_color && bpt == b->style->padding[0].px);
    }
    assert(b->style->bg_color == RGBA(0,0,255,255) && b->style->padding[0].px == 7);
    sb_free(&css);
    style_engine_free(e); doc_free(d);
}

/* A data- attribute set on a connected but not yet styled element keeps the
 * precise path: its own pending style covers it, the scan covers the rest. */
static void test_unstyled_attribute_change(void) {
    Document *d = doc_new("https://unstyled.test/");
    const char *src = "<html><body><main><p></p><p class=t></p></main><em></em></body></html>";
    html_parse(d, src, strlen(src));
    StyleEngine *e = style_engine_new(d);
    const char *css = "[data-open] + p{color:red} [data-open] b{color:blue} main:has([data-open]) ~ em{color:green} div[data-open]{color:navy}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *main_el = d->body->first, *first = main_el->first;
    Node *div = node_new_element(d, "div", NS_HTML), *b = node_new_element(d, "b", NS_HTML);
    node_append(div, b);
    node_insert_before(main_el, div, first);
    node_set_attr(div, "data-open", "");
    assert(!d->selector_changes_unknown);
    assert_incremental_matches_full(e);
    assert(div->style->color == RGBA(0,0,128,255) && b->style->color == RGBA(0,0,255,255) && first->style->color == RGBA(255,0,0,255));
    assert(d->body->first->next->style->color == RGBA(0,128,0,255));
    style_engine_free(e); doc_free(d);
}

/* A tree edit batched with class changes: selector-changed tree dependencies
 * stay in their subject bucket unless the subject class itself changed. */
static void test_tree_edit_with_class_change(void) {
    Document *d = doc_new("https://treeclass.test/");
    const char *src = "<html><body><main><p class=open></p><p class=item></p><p></p></main><section><i class=x></i><i></i></section></body></html>";
    html_parse(d, src, strlen(src));
    StyleEngine *e = style_engine_new(d);
    const char *css = ".open + .item{color:red} .open ~ p{background-color:blue} section:has(.x) .y{color:green} .x + .y{padding-left:2px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *main_el = d->body->first, *open = main_el->first, *item = open->next, *last = item->next;
    Node *section = main_el->next, *x = section->first, *other = x->next;
    for (int round = 0; round < 4; round++) {
        node_insert_before(main_el, node_new_element(d, "p", NS_HTML), round % 2 ? item : last);
        node_set_attr(open, "class", round % 2 ? "open" : "closed");
        node_set_attr(other, "class", round % 2 ? "" : "y");
        node_append(section, node_new_element(d, "b", NS_HTML));
        assert_incremental_matches_full(e);
    }
    assert(open->next->style->color != RGBA(255,0,0,255) && item->style->bg_color == RGBA(0,0,255,255));
    assert(other->style->color != RGBA(0,128,0,255));
    style_engine_free(e); doc_free(d);
}

/* Tree dependencies keyed by subject attributes and :is() alternatives. */
static void test_tree_attribute_subjects(void) {
    Document *d = doc_new("https://treeattr.test/");
    const char *src = "<html><body><main><u></u><i data-x></i><i class=a></i><i class=b></i><i data-state=open></i></main></body></html>";
    html_parse(d, src, strlen(src));
    StyleEngine *e = style_engine_new(d);
    const char *css = "em + [data-x]{color:red} em ~ :is(.a,.b){background-color:blue} main:has(> em) [data-state=open]{color:green}"
        " .k + [class~=b]{padding-left:3px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *main_el = d->body->first, *u = main_el->first, *x = u->next, *a = x->next, *b = a->next, *st = b->next;
    for (int round = 0; round < 4; round++) {
        Node *em = node_new_element(d, "em", NS_HTML);
        node_insert_before(main_el, em, x);
        if (round % 2) node_set_attr(a, "class", "k");
        assert_incremental_matches_full(e);
        assert(x->style->color == RGBA(255,0,0,255) && b->style->bg_color == RGBA(0,0,255,255) && st->style->color == RGBA(0,128,0,255));
        node_remove(em);
        if (round % 2) node_set_attr(a, "class", "a");
        assert_incremental_matches_full(e);
        assert(x->style->color != RGBA(255,0,0,255) && b->style->bg_color != RGBA(0,0,255,255) && st->style->color != RGBA(0,128,0,255));
    }
    style_engine_free(e); doc_free(d);
}

/* Ancestor class filters see past sibling hops: a class left of a
 * descendant/child combinator is an ancestor requirement, a class left of a
 * sibling combinator is not. Matching and missing ancestors both appear. */
static void test_ancestor_filter_after_siblings(void) {
    Document *d = doc_new("https://ancsib.test/");
    const char *src = "<html><body>"
        "<div class=box><i class=a></i><b class=t1></b><s class=t2></s></div>"
        "<div class=other><i class=a></i><b class=t1></b><s class=t2></s></div>"
        "<p class=k></p><p><u class=t3></u><em class=t4></em></p>"
        "<section class=wrap><p class=k></p><p><u class=t5></u></p></section>"
        "</body></html>";
    html_parse(d, src, strlen(src));
    StyleEngine *e = style_engine_new(d);
    const char *css = ".box > .a + b{color:red} .box .a ~ :is(s, .t2){background-color:blue}"
        " .k + p u{color:green} .k + p > :where(em){background-color:green}"
        " .wrap .k ~ p :is(.t5){padding-left:4px} .box > .a + :is(.zz, b) + s{padding-top:2px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *box = d->body->first, *other = box->next, *k = other->next, *p = k->next, *wrap = p->next;
    Node *b1 = box->first->next, *s1 = b1->next, *b2 = other->first->next, *s2 = b2->next;
    Node *u = p->first, *em = u->next, *t5 = wrap->first->next->first;
    assert(b1->style->color == RGBA(255,0,0,255) && b2->style->color != RGBA(255,0,0,255));
    assert(s1->style->bg_color == RGBA(0,0,255,255) && s2->style->bg_color != RGBA(0,0,255,255));
    assert(s1->style->padding[0].px == 2 && s2->style->padding[0].px == 0);
    assert(u->style->color == RGBA(0,128,0,255) && em->style->bg_color == RGBA(0,128,0,255));
    assert(t5->style->padding[3].px == 4);
    /* moving the ancestor class must restyle the far subjects */
    node_set_attr(box, "class", "other"); node_set_attr(other, "class", "box");
    assert_incremental_matches_full(e);
    assert(b2->style->color == RGBA(255,0,0,255) && b1->style->color != RGBA(255,0,0,255));
    assert(s2->style->bg_color == RGBA(0,0,255,255) && s2->style->padding[0].px == 2);
    node_set_attr(wrap, "class", "x");
    assert_incremental_matches_full(e);
    assert(t5->style->padding[3].px == 0);
    style_engine_free(e); doc_free(d);
}

/* :is()/:where() subjects with keyed branches are indexed under each key. */
static void test_multi_key_subjects(void) {
    Document *d = doc_new("https://multi.test/");
    SB html; sb_init(&html);
    sb_puts(&html, "<html><body><main><p class='a b'></p><p class=b></p><p class=c></p><section class=x><p class=d></p></section><input><textarea></textarea>");
    for (int i = 0; i < 1000; i++) sb_puts(&html, "<div><span></span></div>");
    sb_puts(&html, "</main></body></html>");
    html_parse(d, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(d);
    const char *css = ":where(.a,.b){padding-left:1px} :is(.a,.b):is(.c,.a){padding-top:1px} :is(.c, .x .d){color:blue}"
        " :is(input,textarea){background-color:red} .dark :is(.b,.d){color:green}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *ab = d->body->first->first, *b = ab->next, *c = b->next, *x = c->next, *dd = x->first, *in = x->next, *ta = in->next;
    assert(ab->style->padding[3].px == 1 && b->style->padding[3].px == 1 && c->style->padding[3].px == 0);
    assert(ab->style->padding[0].px == 1 && b->style->padding[0].px == 0 && c->style->padding[0].px == 0);
    assert(c->style->color == RGBA(0,0,255,255) && dd->style->color == RGBA(0,0,255,255) && b->style->color != RGBA(0,0,255,255));
    assert(in->style->bg_color == RGBA(255,0,0,255) && ta->style->bg_color == RGBA(255,0,0,255));
    for (int i = 0; i < 4; i++) {
        bool on = i % 2 == 0;
        node_set_attr(d->body, "class", on ? "dark" : "");
        style_recalc(e, &d->node, false);
        assert(e->stats_matched < 20);
        assert((b->style->color == RGBA(0,128,0,255)) == on && (dd->style->color == RGBA(0,128,0,255)) == on);
        assert(c->style->color == RGBA(0,0,255,255));
    }
    const char *extra = ":is(.c,textarea){padding-right:1px}";
    StyleSheet *sheet = css_parse_sheet(extra, strlen(extra), d->url, 1, &e->media);
    style_engine_add_sheet(e, sheet);
    style_recalc(e, &d->node, false);
    assert(e->stats_matched < 20 && c->style->padding[1].px == 1 && ta->style->padding[1].px == 1 && b->style->padding[1].px == 0);
    style_engine_free(e); doc_free(d);
}

/* An ancestor class change rematches only subjects of rules testing it. */
static void test_keyed_descendant_sets(void) {
    for (int whole = 0; whole < 2; whole++) {
        Document *d = doc_new("https://keyed.test/");
        SB html; sb_init(&html);
        sb_puts(&html, "<html><body><main>");
        for (int i = 0; i < 1000; i++) sb_puts(&html, i % 100 ? "<section><span></span></section>" : "<section><em data-k></em><svg><Path/></svg></section>");
        sb_puts(&html, "</main></body></html>");
        html_parse(d, html.s, html.n); sb_free(&html);
        StyleEngine *e = style_engine_new(d);
        const char *css = whole ? ".dark em{color:red} .dark :where(section > span){color:blue}" :
            ".dark em{color:red} .dark [data-k]{background-color:red} .dark path{color:green} #x .dark em{color:blue}";
        style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
        style_recalc(e, &d->node, true);
        Node *first = d->body->first->first, *em = first->first, *span = first->next->first, *path = em->next->first;
        for (int i = 0; i < 4; i++) {
            bool on = i % 2 == 0;
            node_set_attr(d->body, "class", on ? "dark" : "");
            style_recalc(e, &d->node, false);
            if (!whole) assert(e->stats_matched < 40);
            assert((em->style->color == RGBA(255,0,0,255)) == on);
            assert(whole || (em->style->bg_color == RGBA(255,0,0,255)) == on);
            assert(whole || (path->style->color == RGBA(0,128,0,255)) == on);
            assert(!whole || (span->style->color == RGBA(0,0,255,255)) == on);
            assert(whole || span->style->color != RGBA(0,0,255,255));
        }
        style_engine_free(e); doc_free(d);
    }
}

/* Independent inherited properties propagate without rematching descendants. */
static void test_independent_inherited_change(void) {
    Document *d = doc_new("https://indep.test/");
    SB html; sb_init(&html);
    sb_puts(&html, "<html><body><main><div class=keep><span></span></div><p class=q></p><pre></pre>");
    for (int i = 0; i < 1000; i++) sb_puts(&html, "<section><b></b></section>");
    sb_puts(&html, "</main></body></html>");
    html_parse(d, html.s, html.n); sb_free(&html);
    StyleEngine *e = style_engine_new(d);
    const char *css = ".keep{pointer-events:auto} .q::before{content:'x'} .q::after{content:'y';cursor:pointer}"
        " .lock{pointer-events:none;cursor:not-allowed;visibility:hidden} .wrap{white-space:nowrap}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *main_el = d->body->first, *keep = main_el->first, *q = keep->next, *pre = q->next, *b = pre->next->first;
    for (int i = 0; i < 4; i++) {
        bool on = i % 2 == 0;
        node_set_attr(d->body, "class", on ? "lock wrap" : "");
        style_recalc(e, &d->node, false);
        assert(e->stats_matched < 10);
        assert(main_el->style->pointer_events == !on && b->style->pointer_events == !on && b->style->visibility == (on ? VIS_HIDDEN : VIS_VISIBLE));
        assert(keep->style->pointer_events && keep->first->style->pointer_events);
        assert(q->style->before && q->style->before->pointer_events == !on && q->style->before->cursor == q->style->cursor);
        assert(q->style->after && q->style->after->cursor == CUR_POINTER && q->style->cursor == (on ? CUR_NOT_ALLOWED : CUR_DEFAULT));
        assert(b->style->white_space == (on ? WS_NOWRAP : WS_NORMAL) && pre->style->white_space == WS_PRE);
        static uint8_t snap[2100][5]; int count = 0;
        for (Node *n = d->node.first; n; n = node_next_deep(n, &d->node)) if (n->type == NODE_ELEMENT) {
            assert(count < 2100);
            uint8_t v[5] = { n->style->pointer_events, n->style->visibility, n->style->cursor, n->style->white_space,
                n->style->before ? n->style->before->pointer_events : 9 };
            memcpy(snap[count++], v, 5);
        }
        style_recalc(e, &d->node, true); count = 0;
        for (Node *n = d->node.first; n; n = node_next_deep(n, &d->node)) if (n->type == NODE_ELEMENT) {
            uint8_t v[5] = { n->style->pointer_events, n->style->visibility, n->style->cursor, n->style->white_space,
                n->style->before ? n->style->before->pointer_events : 9 };
            assert(!memcmp(snap[count++], v, 5));
        }
    }
    style_engine_free(e); doc_free(d);
}

/* Unkeyed subjects indexed by :root, attribute or parent type still match. */
static void test_local_rule_index(void) {
    Document *d = doc_new("https://local-index.test/");
    const char *html = "<html><body><details><summary></summary><p></p></details><div hidden></div>"
        "<section><b></b></section><svg viewBox='0 0 1 1'><g></g></svg><i data-x='1'></i></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = ":root{color:green} :root body{background-color:green} details:not([open]) > :not(summary){color:red}"
        " section > *{color:blue} [hidden]:not([hidden=until-found]){background-color:red} :-moz-focusring{color:black}"
        " body :-webkit-autofill{color:black} svg > g{color:red} [data-x]{background-color:blue}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *details = d->body->first, *div = details->next, *section = div->next, *svg = section->next, *it = svg->next;
    assert(d->html->style->color == RGBA(0,128,0,255) && d->body->style->bg_color == RGBA(0,128,0,255));
    assert(details->first->style->color == RGBA(0,128,0,255));
    assert(details->first->next->style->color == RGBA(255,0,0,255));
    assert(div->style->bg_color == RGBA(255,0,0,255) && section->first->style->color == RGBA(0,0,255,255));
    assert(svg->first->style->color == RGBA(255,0,0,255));
    assert(it->style->bg_color == RGBA(0,0,255,255) && section->style->color == RGBA(0,128,0,255));
    node_set_attr(details, "open", "");
    node_remove_attr(div, "hidden");
    node_remove_attr(it, "data-x");
    assert_incremental_matches_full(e);
    assert(details->first->next->style->color == RGBA(0,128,0,255) && div->style->bg_color != RGBA(255,0,0,255));
    assert(it->style->bg_color != RGBA(0,0,255,255));
    style_engine_free(e); doc_free(d);
}

static void test_forced_children_after_class_changes(void) {
    SB html; sb_init(&html);
    sb_puts(&html, "<html><body><div id='other'></div>");
    for (int i = 0; i < 1000; i++) sb_puts(&html, "<section><span></span></section>");
    sb_puts(&html, "</body></html>");
    Document *big = doc_new("https://forced-children.test/");
    html_parse(big, html.s, html.n);
    StyleEngine *be = style_engine_new(big);
    const char *css = ".tall{padding-top:1px} .x{color:red} section span{color:blue}";
    style_engine_add_sheet(be, css_parse_sheet(css, strlen(css), big->url, 1, &be->media));
    style_recalc(be, &big->node, true);
    for (int i = 0; i < 4; i++) {
        node_set_attr(big->body, "class", i & 1 ? "" : "tall");
        node_set_attr(doc_get_element_by_id(big, "other"), "class", i & 1 ? "" : "x");
        style_recalc(be, &big->node, false);
        assert(be->stats_matched < 10);
        assert(big->body->first->next->first->style->color == RGBA(0,0,255,255));
    }
    /* A changed class widens tree dependencies only on the changed element. */
    const char *divide = ":where(.divide > :not(:last-child)){padding-bottom:1px}";
    style_engine_add_sheet(be, css_parse_sheet(divide, strlen(divide), big->url, 1, &be->media));
    style_recalc(be, &big->node, false);
    Node *list = big->body->first->next;
    for (int i = 0; i < 4; i++) {
        node_set_attr(list, "class", i & 1 ? "" : "divide");
        node_append(list, node_new_element(big, "i", NS_HTML));
        style_recalc(be, &big->node, false);
        assert(be->stats_matched < 10);
        assert((list->first->style->padding[2].px == 1) == !(i & 1));
    }
    style_engine_free(be); doc_free(big); sb_free(&html);
    const char *selectors[] = {
        ".a:has(.b) span", ".a:has(.b) > p", ".a:has(> p.b) + aside", "main:not(:has(.b)) span",
        ".b ~ p span", ".b + p", ":is(.b) ~ * span", "main:has(.b) ~ aside span", ".b span",
        ":not(.b) > span", "p:not(.b) span", ".a .b span", "aside:has(~ footer.b)", "body:has(.b) span",
        "main:has(:not(.b)) span", ".a:has(.b:hover) span", "p:nth-child(1 of .b) span",
        ".b + p span", "main.tint + aside + footer", "main.tint + aside span", ":is(main.tint + aside) span",
        "main.tint ~ footer", "#other.x + *"};
    for (size_t k = 0; k < ARRLEN(selectors); k++) {
        Document *d = doc_new("https://forced-children.test/");
        const char *html = "<html><body class='root'><main class='a'><p><span></span></p><p><span></span></p></main><aside><span></span></aside><footer></footer><div id='other'></div></body></html>";
        html_parse(d, html, strlen(html));
        StyleEngine *e = style_engine_new(d);
        char rule[256]; snprintf(rule, sizeof rule, "%s{color:red;background-color:blue} .wide{letter-spacing:1px} .tint{color:green} .wide main span{background-color:green} .tint > p{color:blue} [class~=y] + footer{color:blue} #other.x{color:blue}", selectors[k]);
        style_engine_add_sheet(e, css_parse_sheet(rule, strlen(rule), d->url, 1, &e->media));
        style_recalc(e, &d->node, true);
        Node *main_ = d->body->first, *p1 = main_->first, *p2 = p1->next, *footer = main_->next->next;
        Node *other = doc_get_element_by_id(d, "other");
        Node *targets[] = {p1, p2, footer, p1->first};
        for (int i = 0; i < 16; i++) {
            Node *t = targets[i % 4];
            if (i & 4) node_remove_attr(t, "class"); else node_set_attr(t, "class", "b");
            /* the parent restyle that forces children, plus an unrelated class change */
            node_set_attr(i & 8 ? d->body : main_, "class", i & 1 ? (i & 8 ? "root wide" : "a tint") : (i & 8 ? "root" : "a"));
            node_set_attr(other, "class", i & 2 ? "x" : "y");
            assert_incremental_matches_full(e);
        }
        style_engine_free(e); doc_free(d);
    }
}

static void test_tree_subject_index(void) {
    Document *d = doc_new("https://tree-subject-index.test/");
    const char *empty = "<html><body></body></html>";
    html_parse(d, empty, strlen(empty));
    Node *last = NULL;
    for (int i = 0; i < 2000; i++) {
        last = node_new_element(d, "section", NS_HTML);
        node_set_attr(last, "class", "message"); node_append(d->body, last);
    }
    SB css; sb_init(&css);
    for (int i = 0; i < 1200; i++) sb_printf(&css, ".unused%d:has(:nth-child(2)){color:blue}", i);
    sb_puts(&css, ".message:has(> span){color:red}");
    StyleEngine *e = style_engine_new(d);
    style_engine_add_sheet(e, css_parse_sheet(css.s, css.n, d->url, 1, &e->media)); sb_free(&css);
    style_recalc(e, &d->node, true);
    Node *span = node_new_element(d, "span", NS_HTML); node_append(last, span);
    style_recalc(e, &d->node, false);
    assert(last->style->color == RGBA(255,0,0,255));
    assert(d->body->first->style->color != RGBA(255,0,0,255));
    /* Thousands of unrelated keyed dependencies must not form a Cartesian
     * scan with the old message elements. Hash collisions are permitted. */
    assert(e->stats_tree_tests < 20000);
    node_remove(span); node_free_tree(span); style_recalc(e, &d->node, false);
    assert(last->style->color != RGBA(255,0,0,255));
    style_engine_free(e); doc_free(d);

    const char *selectors[] = {".subject:has(*)", "#chosen:has(*)", "SECTION:has(*)",
        ":is(.subject,.subject):has(*)", ":has(> b)"};
    for (size_t k = 0; k < ARRLEN(selectors); k++) {
        d = doc_new("https://tree-subject-semantics.test/");
        const char *html = "<html><body><section class='subject' id='chosen'></section><aside></aside></body></html>";
        html_parse(d, html, strlen(html)); e = style_engine_new(d);
        char rule[256]; snprintf(rule, sizeof rule, "%s{background-color:red}", selectors[k]);
        style_engine_add_sheet(e, css_parse_sheet(rule, strlen(rule), d->url, 1, &e->media));
        Node *subject = d->body->first;
        style_recalc(e, &d->node, true);
        for (int pass = 0; pass < 4; pass++) {
            node_set_attr(subject, "class", pass % 2 ? "other" : "subject");
            node_set_attr(subject, "id", pass % 2 ? "other" : "chosen");
            Node *b = node_new_element(d, "b", NS_HTML); node_append(subject, b);
            style_recalc(e, &d->node, false);
            Color color = subject->style->bg_color;
            style_recalc(e, &d->node, true); assert(color == subject->style->bg_color);
            node_remove(b); node_free_tree(b); style_recalc(e, &d->node, false);
            color = subject->style->bg_color;
            style_recalc(e, &d->node, true); assert(color == subject->style->bg_color);
        }
        style_engine_free(e); doc_free(d);
    }
}

/* The page's whole-tree image scan runs only when this version moves. */
static void test_resource_version(void) {
    Document *d = doc_new("https://resource-version.test/");
    const char *html = "<html><body><div id='box'>a</div><img id='pic' src='a.png'></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = ".bg {background-image:url(b.png)} .hide {display:none}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *box = doc_get_element_by_id(d, "box"), *pic = doc_get_element_by_id(d, "pic");
    uint64_t v = d->resource_version;
    assert(v > 0);                                   /* first style of an <img> */
    for (int i = 0; i < 8; i++) {                    /* inline layout churn is not an image change */
        node_set_attr(box, "style", i % 2 ? "height:10px" : "height:20px");
        style_recalc(e, &d->node, false);
    }
    assert(d->resource_version == v);
    node_set_attr(box, "class", "bg"); style_recalc(e, &d->node, false);
    assert(d->resource_version > v); v = d->resource_version;
    node_set_attr(box, "class", "bg hide"); style_recalc(e, &d->node, false);
    assert(d->resource_version > v); v = d->resource_version;
    node_set_attr(pic, "src", "c.png");
    assert(d->resource_version > v); v = d->resource_version;
    Node *img = node_new_element(d, "img", NS_HTML); node_append(d->body, img); style_recalc(e, &d->node, false);
    assert(d->resource_version > v);
    style_engine_free(e); doc_free(d);
}
static void test_match_memo(void) {
    Document *d = doc_new("https://match-memo.test/");
    const char *html = "<html><body><div id='box' class='a' style='left:1px'>x</div><div id='q' style='left:1px'>y</div></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = ".a{position:relative;width:30px} .a::before{content:'b'} .b{width:40px}"
        "@media (min-width:900px){.a{width:50px}} [style*='top'] + #q{width:70px} [title=t] + #q{height:5px} .k + #q{min-width:6px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    e->media.vw = 800;
    style_recalc(e, &d->node, true);
    Node *box = doc_get_element_by_id(d, "box"), *q = doc_get_element_by_id(d, "q");
    assert(box->style->width.px == 30 && box->style->before && !box->match_memo);
    for (int i = 0; i < 4; i++) {                     /* inline churn reuses matched rules */
        char st[32]; snprintf(st, sizeof st, "left:%dpx", i + 2);
        node_set_attr(box, "style", st);
        style_recalc(e, &d->node, false);
        assert(box->style->width.px == 30 && box->style->before && box->style->inset[3].px == i + 2);
        if (i) assert(e->stats_memo_hits >= 2);
    }
    assert(box->match_memo);
    node_set_attr(box, "class", "a b"); node_set_attr(box, "style", "left:9px");
    style_recalc(e, &d->node, false);
    assert(box->style->width.px == 40 && box->style->inset[3].px == 9);
    e->media.vw = 1000; style_recalc(e, &d->node, false);  /* media changes matching */
    node_set_attr(box, "style", "left:8px"); style_recalc(e, &d->node, false);
    assert(box->style->width.px == 50);                /* the later @media rule now wins over .b */
    e->media.vw = 800; style_recalc(e, &d->node, false);
    node_set_attr(box, "style", "left:7px"); style_recalc(e, &d->node, false);
    assert(box->style->width.px == 40);
    node_set_attr(box, "class", "a"); style_recalc(e, &d->node, false);
    assert(box->style->width.px == 30);
    node_set_attr(q, "style", "left:3px"); style_recalc(e, &d->node, false);
    node_set_attr(box, "class", "a k"); style_recalc(e, &d->node, false);
    assert(q->style->min_width.px == 6);                /* sibling combinators follow class changes */
    node_set_attr(box, "title", "t"); style_recalc(e, &d->node, false);
    assert(q->style->height.px == 5);                  /* ... and other attributes */
    node_set_attr(box, "class", "a"); style_recalc(e, &d->node, false);
    assert(q->style->min_width.px == 0);
    node_set_attr(box, "style", "top:1px"); style_recalc(e, &d->node, false);
    assert(q->style->width.px == 70);                   /* [style] selectors are never memoized */
    style_engine_free(e); doc_free(d);
}
static void test_sibling_state(void) {
    Document *d = doc_new("https://sibling-state.test/");
    const char *html = "<html><body><input id='c' type='checkbox' class='peer'><span>s</span><div id='t' class='tgt'>t</div></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    const char *css = ".peer:checked ~ .tgt{width:9px} :focus + span{height:4px}";
    style_engine_add_sheet(e, css_parse_sheet(css, strlen(css), d->url, 1, &e->media));
    style_recalc(e, &d->node, true);
    Node *c = doc_get_element_by_id(d, "c"), *t = doc_get_element_by_id(d, "t");
    assert(t->style->width.px == 0);
    c->checked_override = 1; c->flags |= NF_CHECKED; doc_mark_dirty(d, c);
    style_recalc(e, &d->node, false);
    assert(t->style->width.px == 9);
    c->checked_override = -1; c->flags &= ~(uint32_t)NF_CHECKED; doc_mark_dirty(d, c);
    style_recalc(e, &d->node, false);
    assert(t->style->width.px == 0);
    assert(e->idx.sib_state);                          /* keyless :focus compound */
    style_engine_free(e); doc_free(d);
}
static void test_sheet_rank_cascade(void) {
    Document *d = doc_new("https://sheet-rank.test/");
    const char *html = "<html><head><style id='a'></style><style id='c'></style></head><body><p id='p' class='x'>a</p></body></html>";
    html_parse(d, html, strlen(html));
    StyleEngine *e = style_engine_new(d);
    Node *a = doc_get_element_by_id(d, "a"), *c = doc_get_element_by_id(d, "c"), *p = doc_get_element_by_id(d, "p");
    const char *blue = ".x{color:blue}", *red = ".x{color:red}", *green = "p.x{color:green}";
    StyleSheet *s = css_parse_sheet(blue, strlen(blue), d->url, 1, &e->media); s->owner = c; style_engine_add_sheet(e, s);
    style_recalc(e, &d->node, true); assert(p->style->color == RGBA(0,0,255,255));
    s = css_parse_sheet(red, strlen(red), d->url, 1, &e->media); s->owner = a; style_engine_add_sheet(e, s);   /* indexed between, cascades before c */
    assert(!e->idx_dirty);
    style_recalc(e, &d->node, false); assert(p->style->color == RGBA(0,0,255,255));
    s = css_parse_sheet(green, strlen(green), d->url, 1, &e->media); s->owner = a; style_engine_add_sheet(e, s);
    style_recalc(e, &d->node, false); assert(p->style->color == RGBA(0,128,0,255));
    style_engine_remove_owner(e, a); assert(!e->idx_dirty && e->dead_sheets.n == 2);
    style_recalc(e, &d->node, false); assert(p->style->color == RGBA(0,0,255,255));
    /* many sheets: late low-specificity rules must not gain specificity from their order */
    const char *strong = "body p.x{color:green}", *filler = "div{color:red}";
    s = css_parse_sheet(strong, strlen(strong), d->url, 1, &e->media); style_engine_add_sheet(e, s);
    for (int i = 0; i < 60; i++) style_engine_add_sheet(e, css_parse_sheet(filler, strlen(filler), d->url, 1, &e->media));
    style_engine_add_sheet(e, css_parse_sheet(red, strlen(red), d->url, 1, &e->media));
    style_recalc(e, &d->node, true); assert(p->style->color == RGBA(0,128,0,255));
    style_engine_invalidate(e); style_recalc(e, &d->node, true); assert(p->style->color == RGBA(0,128,0,255) && !e->dead_sheets.n);
    style_engine_free(e); doc_free(d);
}
int main(void) {
    test_tree_subject_index();
    dom_init();
    test_coalesced_selector_tree_changes();
    test_coalesced_subject_class_tree_changes();
    test_sheet_change_targets_subjects();
    test_sheet_rank_cascade();
    test_forced_children_after_class_changes();
    test_local_rule_index();
    test_independent_inherited_change();
    test_keyed_descendant_sets();
    test_multi_key_subjects();
    test_explicit_inherit_reuse();
    test_unstyled_attribute_change();
    test_deep_custom_chains();
    test_tree_edit_with_class_change();
    test_tree_attribute_subjects();
    test_ancestor_filter_after_siblings();
    test_parent_keyed_rules();
    test_focus_state_changes();
    test_child_list_invalidation();
    test_attribute_subject_invalidation();
    test_character_data_invalidation();
    test_style_cache_lifetime();
    test_resource_version();
    test_match_memo();
    test_sibling_state();
    test_empty_dependency_selectors();
    test_inherited_custom_updates();
    test_deep_custom_rebase();
    test_inherited_context_changes();
    test_match_cache();
    test_query_syntax_cache();
    test_rule_filter();
    Document *d = doc_new("https://style-tracking.test/");
    Node *body = node_new_element(d, "body", NS_HTML);
    node_append(&d->node, body);
    Node *text = node_new_text(d, "hello", 5);
    node_append(body, text);
    uint64_t v = d->sheet_version;
    for (int i = 0; i < 1000; i++) {
        node_set_text_content(text, "streamed message");
        node_set_attr(body, "class", i % 2 ? "message active" : "message");
        assert(node_has_class(body, "message"));
        assert(node_has_class(body, "active") == (i % 2 != 0));
    }
    assert(d->sheet_version == v);
    node_remove_attr(body, "CLASS");
    assert(!node_has_class(body, "message"));
    node_set_attr(body, "CLASS", "restored");
    assert(node_has_class(body, "restored"));
    for (int i = 0; i < 32; i++) { char name[32]; snprintf(name, sizeof name, "attr%d", i); node_set_attr(body, name, "value"); }
    assert(node_has_class(body, "restored"));
    node_set_attr(body, "class", "replacement");
    assert(!node_has_class(body, "restored") && node_has_class(body, "replacement"));
    node_set_attr(body, "class", " leading\tduplicate duplicate\nhover:bg-red unicode-\xc3\xa9 trailing ");
    assert(body->nclasses == 5);
    node_set_attr(body, "DATA-Test", "case insensitive");
    assert(!strcmp(node_attr_atom(body, atom("data-test")), "case insensitive"));
    node_remove_attr(body, "data-test");
    assert(!node_attr_atom(body, atom("data-test")));
    node_set_attr(body, "data-test", "replacement");
    assert(!strcmp(node_attr_atom(body, atom("data-test")), "replacement"));
    Node *svg = node_new_element(d,"svg",NS_SVG);
    node_set_attr(svg, "viewBox", "0 0 10 10");
    assert(node_attr_atom(svg,atom("viewBox")));
    assert(!node_attr_atom(svg,atom("viewbox")));
    node_free_tree(svg);
    assert(css_query(&d->node, ".leading.duplicate.hover\\:bg-red:not(.missing)", false, NULL) == body);
    assert(css_query(&d->node, ".unicode-\xc3\xa9", false, NULL) == body);
    Node *clone = node_clone(body, false, d);
    assert(node_has_class(clone, "trailing"));
    node_set_attr(clone, "class", "independent");
    assert(node_has_class(body, "trailing") && !node_has_class(clone, "trailing"));
    node_free_tree(clone);

    Node *host = node_new_element(d, "div", NS_HTML);
    host->shadow_root = node_new_fragment(d);
    host->shadow_root->host = host;
    host->shadow_root->refcount = 1;
    Node *sheet = node_new_element(d, "style", NS_HTML);
    node_append(host->shadow_root, sheet);
    node_set_text_content(sheet, "div {color:red}");
    v = d->sheet_version;
    node_append(body, host);
    assert(d->sheet_version > v);
    v = d->sheet_version;
    node_set_text_content(sheet->first, "div {color:blue}");
    assert(d->sheet_version > v);
    v = d->sheet_version;
    node_text_append(sheet->first, "p{}", 3);
    assert(d->sheet_version > v);
    v = d->sheet_version;
    node_remove(host);
    assert(d->sheet_version > v);
    v = d->sheet_version;
    node_append(body, host);
    assert(d->sheet_version > v);
    Node *link = node_new_element(d, "link", NS_HTML);
    node_set_attr(link, "rel", "preload");
    node_append(body, link);
    v = d->sheet_version;
    node_set_attr(link, "rel", "stylesheet");
    assert(d->sheet_version > v);
    v = d->sheet_version;
    node_set_attr(link, "href", "/replacement.css");
    assert(d->sheet_version > v);
    v = d->sheet_version;
    node_remove(link);
    assert(d->sheet_version > v);
    node_free_tree(link);
    doc_free(d);
    puts("Stylesheet invalidation and class cache passed");
}
