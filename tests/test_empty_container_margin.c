#include "layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error "Assertions must be enabled"
#endif
/* CSS 2.2 8.3.1 and 10.6.7: a through-collapsing child's margins
 * collapse with each other, but not with a BFC root's margins. The
 * resulting margin therefore contributes once to that root's auto height. */
typedef struct { const char *name, *parent_css, *children; float height; } Case;
static int run(const Case *c) {
    char html[2048], css[2048];
    Document *d=doc_new("https://empty-container-margin.test/"); assert(d);
    int n=snprintf(html,sizeof html,"<!doctype html><html><body><main id='stage'><div id='parent'>%s</div></main></body></html>",c->children);
    assert(n>0 && (size_t)n<sizeof html); html_parse(d,html,strlen(html));
    n=snprintf(css,sizeof css,"html,body,main,div{display:block;margin:0;padding:0;border:0;box-sizing:content-box}#stage{display:flow-root;position:relative;width:200px}#parent{width:100px;%s}.empty{height:0;margin:100px 0}.a{height:0;margin:40px 0 10px}.b{height:0;margin:20px 0 60px}.negative{height:0;margin:-20px 0 -10px}",c->parent_css);
    assert(n>0 && (size_t)n<sizeof css);
    StyleEngine *se=style_engine_new(d);assert(se);
    StyleSheet *ss=css_parse_sheet(css,strlen(css),d->url,1,&se->media);assert(ss);style_engine_add_sheet(se,ss);
    Layout *l=layout_new();assert(l);style_recalc(se,&d->node,true);layout_run(l,d,320,240);
    Node *parent=doc_get_element_by_id(d,"parent");assert(parent && parent->box);Box *b=parent->box;
    int ok=fabsf(b->h-c->height)<0.001f;
    fprintf(stderr,"%s: actual height=%g expected=%g bfc=%d result=%s\n",c->name,b->h,c->height,b->bfc,ok?"PASS":"FAIL");
    layout_free(l);style_engine_free(se);doc_free(d);return !ok;
}
int main(void) {
    static const Case cases[]={
      {"absolute", "position:absolute;left:0;top:0", "<div class='empty'></div>",100},
      {"fixed", "position:fixed;left:0;top:0", "<div class='empty'></div>",100},
      {"float", "float:left", "<div class='empty'></div>",100},
      {"overflow-hidden", "overflow:hidden", "<div class='empty'></div>",100},
      {"overflow-auto", "overflow:auto", "<div class='empty'></div>",100},
      {"flow-root", "display:flow-root", "<div class='empty'></div>",100},
      {"inline-block", "display:inline-block", "<div class='empty'></div>",100},
      {"flow-root-two-empty", "display:flow-root", "<div class='a'></div><div class='b'></div>",60},
      {"flow-root-negative", "display:flow-root", "<div class='negative'></div>",0},
      {"flow-root-no-child", "display:flow-root", "",0},
      {"flow-root-padding", "display:flow-root;padding:2px 0 3px", "<div class='empty'></div>",105},
      {"flow-root-border", "display:flow-root;border-top:2px solid;border-bottom:3px solid", "<div class='empty'></div>",105},
      {"escaping-normal-flow", "", "<div class='empty'></div>",0},
      {"escaping-two-empty", "", "<div class='a'></div><div class='b'></div>",0},
      {"explicit-height", "display:flow-root;height:20px", "<div class='empty'></div>",20},
      {"minimum-height", "display:flow-root;min-height:150px", "<div class='empty'></div>",150}
    };
    dom_init();font_init();int failures=0;
    for(size_t i=0;i<sizeof cases/sizeof cases[0];++i)failures+=run(&cases[i]);
    fprintf(stderr,"empty-container-margin: %zu cases, %d failures\n",sizeof cases/sizeof cases[0],failures);
    return failures?1:0;
}
