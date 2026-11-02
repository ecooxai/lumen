/* Native lazy conditional substitution: real computed values, layout,
 * inherited dependency invalidation, and persistent custom-property cycles.
 * Registered style values, ranges, media/supports and attr are separate work. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void yes(const char *label, bool condition) {
    checks++;
    if (!condition) { fprintf(stderr, "%s\n", label); abort(); }
}
static void refresh(StyleEngine *engine, Layout *layout, Document *doc) {
    style_recalc(engine, &doc->node, false);
    layout_run(layout, doc, engine->media.vw, engine->media.vh);
}
static void set_style(Node *node, const char *css, StyleEngine *engine, Layout *layout, Document *doc) {
    node_set_attr(node, "style", css); refresh(engine, layout, doc);
}
static void text(Node *node, const char *property, const char *expected) {
    char *actual = css_get_computed_value(node, property);
    if (!actual || strcmp(actual, expected)) fprintf(stderr, "%s: %s, expected %s\n",
        property, actual ? actual : "(null)", expected);
    yes(property, actual && !strcmp(actual, expected)); free(actual);
}
static void width(Node *node, float expected) {
    if (!node->box || !isfinite(node->box->w) || fabsf(node->box->w - expected) > .002f)
        fprintf(stderr, "native width: %g expected %g\n", node->box ? (double)node->box->w : -1.0, (double)expected);
    yes("real native width", node->box && isfinite(node->box->w) && fabsf(node->box->w - expected) < .002f);
    char value[64]; snprintf(value, sizeof value, "%gpx", (double)expected);
    text(node, "width", value);
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://native-if-unregistered.test/");
    const char *html = "<!doctype html><html><body><div id='parent'><div id='target'></div></div>"
        "<div id='other'></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc); engine->media = (MediaCtx){800,600,1,false};
    const char *css = "html,body{margin:0;padding:0;border:0}#parent,#other{width:240px}"
        "#target{display:block;height:10px;margin:0;padding:0;border:0}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    Layout *layout = layout_new(); refresh(engine, layout, doc);
    Node *target = doc_get_element_by_id(doc, "target"), *parent = doc_get_element_by_id(doc, "parent"),
        *other = doc_get_element_by_id(doc, "other");
    assert(target && parent && other && target->box);
    static const struct { const char *css; float expected; } cases[] = {
        {"--mode:wide;--v:if(style(--mode:wide):40px;else:20px);width:var(--v)",40},
        {"--mode:narrow;--v:if(style(--mode:wide):40px;else:20px);width:var(--v)",20},
        {"--mode:wide;--v:if(style(--missing):1px;else:41px;style(--mode):2px);width:var(--v)",41},
        {"--a:on;--c:on;--v:if(style(--a) and ((not style(--b)) or style(--c)):42px;else:2px);width:var(--v)",42},
        {"--a:on;--b:off;--v:if(style((--a:on) and ((not (--b:on)) or (--c:on))):43px;else:3px);width:var(--v)",43},
        {"--mode:on;--v:if(style(--mode):44px;else:var(--v));width:var(--v,4px)",44},
        {"--mode:on;--v:if(style(--mode):45px;style(--v):5px);width:var(--v,5px)",45},
        {"--mode:on;--v:if(style(--mode):var(--v);else:6px);width:var(--v,46px)",46},
        {"--v:if(style(--v):7px;else:8px);width:var(--v,47px)",47},
        {"--bad:var(--bad);--v:if(style(--bad):8px;else:48px);width:var(--v)",48},
        {"--v:if(style(--missing):9px);width:var(--v,49px)",240},
        {"--empty: ;--v:if(style(--empty):50px;else:10px);width:var(--v)",50},
        {"--token:calc(1 + 2);--v:if(style(--token:3):11px;else:51px);width:var(--v)",51},
        {"--v:if(style(--missing:initial):52px;else:12px);width:var(--v)",52},
        {"--mode:wide;width:if(style(--mode:wide):54px;else:14px)",54},
        {"--v:if(not future-condition(example):19px;else:59px);width:var(--v)",59},
        {"--mode:on;--condition:style(--mode);--v:if(var(--condition):61px;else:21px);width:var(--v)",61},
        {"--otherwise:else;--v:if(var(--otherwise):62px;else:22px);width:var(--v)",62},
        {"--mode:on;--v:if(style(--mode) and invalid:23px;else:63px);width:var(--v)",63},
        {"--bad:var(--bad);--v:if(var(--bad):24px;else:64px);width:var(--v)",64},
        {"--a:on;--v:if(style(--a):if(style(--missing):25px;else:65px);else:26px);width:var(--v)",65},
        {"--v:if(style(--v:initial):26px;else:27px);width:var(--v,66px)",66},
        {"--a:on;--v:if(style((--a) or (--v)):27px;else:28px);width:var(--v,67px)",67},
        {"--a:initial;--v:if(style((--a) and (--v)):28px;else:29px);width:var(--v,68px)",68},
        {"--mode:on;--v:if({style(--mode)}:{69px};else:29px);width:var(--v)",69},
        {"--v:if(else:70px;bad,condition:30px);width:var(--v,170px)",170},
        {"--v:if(else:71px;else:value!);width:var(--v,171px)",171},
        {"--v:if(else:72px;else:{value!});width:var(--v,172px)",172},
        {"--mode:on;--v:if(style(--mode):{73px});width:var(--v)",73},
        {"--mode:on;width:calc(if(style(--mode):40px;else:10px) + 34px)",74},
        {"--mode:on;--v:if(style(--mode):75px;style(--v):var(--v));width:var(--v)",75},
        {"--token:abc;--v:if(style(--token:abc):76px;else:36px);width:var(--v)",76}
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        set_style(target, cases[i].css, engine, layout, doc); width(target, cases[i].expected);
    }
    yes("all fixed grammar/geometry cases executed", checks == 64);

    /* Parent query values and condition dependencies must use the actual
       parent, including when the local compared property is unchanged. */
    set_style(parent, "--mode:green", engine, layout, doc);
    set_style(target, "--mode:blue;--v:if(style(--mode:inherit):53px;else:13px);width:var(--v)", engine, layout, doc);
    width(target,13);
    set_style(parent, "--mode:blue", engine, layout, doc); width(target,53);
    set_style(parent, "--mode:green", engine, layout, doc); width(target,13);
    set_style(target, "--mode:green;--v:if(style(--mode:unset):53px;else:13px);width:var(--v)", engine, layout, doc);
    width(target,53);

    /* A local raw if() must invalidate on inherited names even when those
       names do not appear as var() functions in its authored source. */
    set_style(parent, "--mode:wide", engine, layout, doc);
    set_style(target, "--v:if(style(--mode:wide):55px;else:15px);width:var(--v)", engine, layout, doc);
    width(target,55);
    set_style(parent, "--mode:narrow", engine, layout, doc); width(target,15);
    set_style(parent, "--mode:wide", engine, layout, doc); width(target,55);

    /* Inherited resolved tokens belong to the parent's substitution context. */
    set_style(parent, "--mode:wide;--v:if(style(--mode:wide):56px;else:16px)", engine, layout, doc);
    set_style(target, "--mode:narrow;width:var(--v)", engine, layout, doc); width(target,56);
    set_style(parent, "--mode:narrow;--v:if(style(--mode:wide):56px;else:16px)", engine, layout, doc);
    width(target,16);

    set_style(parent, "--mode:wide", engine, layout, doc);
    set_style(other, "--mode:narrow", engine, layout, doc);
    set_style(target, "--v:if(style(--mode:wide):57px;else:17px);width:var(--v)", engine, layout, doc);
    width(target,57);
    node_append(other,target); refresh(engine,layout,doc); width(target,17);
    node_append(parent,target); refresh(engine,layout,doc); width(target,57);

    set_style(target, "--mode:on;--v:if(style(--mode):a)if(style(--mode):b);width:18px", engine,layout,doc);
    text(target,"--v","a/**/b"); width(target,18);
    set_style(target, "--mode:on;--v:if(style(--mode):\"a;b:c\";else:unused);width:21px", engine,layout,doc);
    text(target,"--v","\"a;b:c\""); width(target,21);
    set_style(target, "--mode:on;--v:if(style(--mode):true_value );width:21px", engine,layout,doc);
    text(target,"--v","true_value "); width(target,21);
    set_style(target, "--mode:on;--v:if(style(--mode):{a,b});width:21px", engine,layout,doc);
    text(target,"--v","a,b"); width(target,21);
    char left[282], right[282], declaration[1400];
    memset(left,'q',280); memset(right,'q',280); left[280]='a'; right[280]='b'; left[281]=right[281]=0;
    snprintf(declaration,sizeof declaration,"--token:%s;--v:if(style(--token:%s):20px;style(--token:%s):60px);width:var(--v)",left,right,left);
    set_style(target,declaration,engine,layout,doc); width(target,60);
    text(target,"--v","60px");

    layout_free(layout); style_engine_free(engine); doc_free(doc);
    assert(checks == 104);
    printf("PASS css_if_unregistered %u native checks\n",checks);
    return 0;
}
