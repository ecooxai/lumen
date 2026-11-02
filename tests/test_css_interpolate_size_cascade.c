/* Actual declaration cascade and descendant invalidation for the new
 * inherited policy. A late all projection must not reset unrelated borders. */
#include "../src/layout/layout.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static const char *case_name;
static void yes(bool ok, const char *message) {
    checks++;
    if (!ok) { fprintf(stderr, "%s: %s\n", case_name, message); abort(); }
}
static void policy(Node *node, const char *expected) {
    char *value = jsg_computed(node, "interpolate-size", 0);
    bool ok = value && !strcmp(value, expected);
    if (!ok) fprintf(stderr, "%s: policy %s expected %s\n", case_name, value ? value : "(null)", expected);
    yes(ok, "actual computed inherited policy"); free(value);
}
int main(void) {
    dom_init(); font_init();
    Document *doc = doc_new("https://interpolate-cascade.test/");
    const char *html = "<!doctype html><html><body><div id=parent><div id=target><div id=child></div></div></div></body></html>";
    html_parse(doc, html, strlen(html));
    StyleEngine *engine = style_engine_new(doc);
    engine->media = (MediaCtx){640,480,1,false};
    const char *css = "html,body{margin:0;padding:0}#parent{width:200px;height:160px;interpolate-size:allow-keywords}"
        "#child{width:1px;height:1px}";
    style_engine_add_sheet(engine, css_parse_sheet(css, strlen(css), doc->url, 1, &engine->media));
    Node *target = doc_get_element_by_id(doc,"target"), *child = doc_get_element_by_id(doc,"child");
    Layout *layout = layout_new();
    static const struct { const char *name, *declarations, *expected; } cases[] = {
        {"later all initial resets policy", "interpolate-size:allow-keywords;all:initial", "numeric-only"},
        {"later policy follows all initial", "all:initial;interpolate-size:allow-keywords", "allow-keywords"},
        {"important policy survives normal all", "interpolate-size:allow-keywords!important;all:initial", "allow-keywords"},
        {"important all resets normal policy", "interpolate-size:allow-keywords;all:initial!important", "numeric-only"},
        {"later important policy follows important all", "all:initial!important;interpolate-size:allow-keywords!important", "allow-keywords"},
        {"later important all resets important policy", "interpolate-size:allow-keywords!important;all:initial!important", "numeric-only"},
        {"all inherit uses actual parent policy", "interpolate-size:initial;all:inherit", "allow-keywords"},
        {"all unset uses inherited policy", "interpolate-size:initial;all:unset", "allow-keywords"},
        {"later initial follows all unset", "all:unset;interpolate-size:initial", "numeric-only"},
        {"invalid all retains valid policy", "interpolate-size:allow-keywords;all:invalid-reset", "allow-keywords"},
        {"pending all initial resets earlier policy", "interpolate-size:allow-keywords;all:var(--Missing,initial)", "numeric-only"},
        {"later policy follows pending all initial", "all:var(--Missing,initial);interpolate-size:allow-keywords", "allow-keywords"},
        {"pending all unset inherits", "interpolate-size:initial;all:var(--Missing,unset)", "allow-keywords"},
        {"invalid computed all takes inherited unset", "interpolate-size:initial;all:var(--Missing)", "allow-keywords"},
        {"important policy survives invalid computed normal all", "interpolate-size:initial!important;all:var(--Missing)", "numeric-only"},
        {"important pending all resets normal policy", "interpolate-size:allow-keywords;all:var(--Missing,initial)!important", "numeric-only"},
    };
    for (unsigned i=0; i<sizeof cases/sizeof cases[0]; i++) {
        case_name=cases[i].name;
        char declarations[640];
        snprintf(declarations,sizeof declarations,"%s;display:block!important;width:17px!important;height:11px!important;padding:0!important;margin:0!important;border:7px solid red!important",cases[i].declarations);
        node_set_attr(target,"style",declarations);
        style_recalc(engine,&doc->node,false); layout_run(layout,doc,640,480);
        policy(target,cases[i].expected); policy(child,cases[i].expected);
        yes(target->style && target->style->border_width[0]==7, "late policy projection preserves later unrelated border declaration");
        float rect[4];
        yes(jsg_vrect(target,rect,0,0) && isfinite(rect[2]) && fabsf(rect[2]-31)<.01f, "actual border-box width preserves unrelated dimensions");
    }
    assert(checks==64);
    layout_free(layout); style_engine_free(engine); doc_free(doc);
    printf("PASS css_interpolate_size_cascade %u native checks\n",checks); return 0;
}
