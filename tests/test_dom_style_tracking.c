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
        ":is(.first > b, .second > span)", ".outer > :is(section,p)", "main:has(.leaf) span", ":is(.leaf)", ":where(section > .leaf, main .leaf)", ":is(:where(.first),.first)", ":is(.first,.second)", ":where(section span, main span)", ":is(.missing span, .outer i)"};
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

int main(void) {
    test_tree_subject_index();
    dom_init();
    test_coalesced_selector_tree_changes();
    test_child_list_invalidation();
    test_attribute_subject_invalidation();
    test_character_data_invalidation();
    test_style_cache_lifetime();
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
