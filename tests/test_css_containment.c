/* Spec-derived native controls for intrinsic inner sizes and observable
 * hidden-content behavior. No browser-result or upstream-expectation edits. */
#include "../src/paint/paint.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static unsigned checks, failures;
static void check(const char *name, bool ok) {
    checks++; if (!ok) { failures++; fprintf(stderr, "%s: failed\n", name); }
}
static void same(const char *name, float got, float want) {
    checks++; if (!(fabsf(got - want) < .02f)) { failures++; fprintf(stderr, "%s: expected %g, got %g\n", name, want, got); }
}
static Node *node(Document *d, const char *id) { Node *n = doc_get_element_by_id(d, id); assert(n && n->box); return n; }
static void size(Document *d, const char *id, float w, float h) {
    Box *b = node(d,id)->box; same(id,b->w,w); same(id,b->h,h);
}
static void value(Document *d, const char *id, const char *prop, const char *want) {
    char *v = css_get_computed_value(node(d,id),prop); check(prop,!strcmp(v,want)); free(v);
}
static void dispose(Document *d, StyleEngine *e, Layout *l) { layout_free(l); style_engine_free(e); doc_free(d); }
static Image *sample_image(Node *n);
static unsigned image_calls;
static uint32_t red_pixel = 0xffff0000;
static Image red_image = {.w=1,.h=1,.refs=1,.px=&red_pixel};
static Image *sample_image(Node *n) { if (n->tag == A_canvas) { image_calls++; return &red_image; } return NULL; }
static uint32_t pixel(Canvas *c, Box *b, int dx, int dy) {
    int x=(int)roundf(b->x)+dx,y=(int)roundf(b->y)+dy; assert(x>=0&&x<c->w&&y>=0&&y<c->h); return canvas_row(c,y)[x];
}
int main(void) {
    dom_init(); font_init();
    ComputedStyle *st=style_new_default();
    const char *valid[]={"none","0","7px","auto none","auto 2em","2px 3px","auto 2px auto 3px","calc(2px + 3px)","calc(-2px)"};
    for(size_t i=0;i<sizeof valid/sizeof *valid;i++) check(valid[i],css_containment_valid("contain-intrinsic-size",valid[i]));
    const char *invalid[]={"auto","-1px","1","10%","calc(10%)","calc(2)","none auto","1px auto","auto auto 1px","1px 2px 3px","auto 1px auto 2px auto","nonegarbage","calc(1s)","stretch"};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;i++) {
        check(invalid[i],!css_containment_valid("contain-intrinsic-size",invalid[i]));
        css_apply_decl(st,NULL,"contain-intrinsic-width","17px",NULL,NULL);
        css_apply_decl(st,NULL,"contain-intrinsic-width",invalid[i],NULL,NULL);
        same("invalid longhand retains previous",st->contain_intrinsic[0].length,17);
    }
    check("supports logical longhand",css_supports("(contain-intrinsic-inline-size: 12px)"));
    check("supports rejects percentage",!css_supports("(contain-intrinsic-block-size: 12%)"));
    check("contain duplicate rejected",!css_containment_valid("contain","size size"));
    check("contain conflicting axes rejected",!css_containment_valid("contain","size inline-size"));
    check("contain combined accepted",css_containment_valid("contain","paint size style layout"));
    style_free(st);
    Document *d=doc_new("https://css-containment.test/");
    const char *html="<!doctype html><html><body>"
      "<div id='inner' class='cis'><div class='child'></div></div>"
      "<div id='border' class='cis bordered'><div class='child'></div></div>"
      "<div id='borderbox' class='cis bordered' style='box-sizing:border-box'><div class='child'></div></div>"
      "<div id='limits' class='cis bordered' style='box-sizing:border-box;min-width:80px;max-width:80px'><div class='child'></div></div>"
      "<div id='explicit' class='cis' style='width:45px;height:27px'><div class='child'></div></div>"
      "<div id='ratio' class='cis' style='width:90px;height:auto;aspect-ratio:2'><div class='child'></div></div>"
      "<div id='empty' class='base' style='contain:size'><div class='small'></div></div>"
      "<div id='strict' class='base' style='contain:strict;contain-intrinsic-size:13px 7px'><div class='small'></div></div>"
      "<div id='content' class='base' style='contain:content;contain-intrinsic-size:13px 7px'><div class='small'></div></div>"
      "<div id='none' class='base' style='contain:none;contain-intrinsic-size:13px 7px'><div class='small'></div></div>"
      "<div id='inline' class='base' style='contain:inline-size;contain-intrinsic-size:73px 31px'><div class='small'></div></div>"
      "<div id='vertical-inline' class='base' style='writing-mode:vertical-lr;contain:inline-size;contain-intrinsic-size:73px 31px'><div class='small'></div></div>"
      "<div id='vertical-block' style='writing-mode:vertical-lr;contain:size;contain-intrinsic-inline-size:31px;contain-intrinsic-block-size:73px'></div>"
      "<canvas id='canvas' class='cis' width='200' height='100'></canvas>"
      "<canvas id='canvas-ratio' class='cis' width='200' height='100' style='width:90px;aspect-ratio:2'></canvas>"
      "<canvas id='canvas-natural' width='200' height='100'></canvas>"
      "<video id='video-normal'></video><video id='video-authored' style='width:91px;height:43px'></video>"
      "<video id='video-attributes' width='92' height='44'></video>"
      "<video id='video-cis' class='cis'></video><video id='video-ratio' style='width:90px;aspect-ratio:2'></video>"
      "<div id='physical-last' class='base' style='contain:size;contain-intrinsic-inline-size:22px;contain-intrinsic-width:39px;contain-intrinsic-block-size:11px'></div>"
      "<div id='logical-last' class='base' style='contain:size;contain-intrinsic-width:39px;contain-intrinsic-inline-size:22px;contain-intrinsic-block-size:11px'></div>"
      "<div id='basis-last' class='base' style='contain:size;contain-intrinsic-inline-size:23px;contain-intrinsic-block-size:41px;writing-mode:vertical-rl'></div>"
      "<div id='priority' class='base' style='contain:size;contain-intrinsic-width:39px!important;contain-intrinsic-inline-size:22px;contain-intrinsic-block-size:11px'></div>"
      "<div id='inherit-parent' style='writing-mode:vertical-lr;contain-intrinsic-inline-size:37px;contain-intrinsic-block-size:19px'>"
      "<div id='inherit-child' class='base' style='writing-mode:horizontal-tb;contain:size;contain-intrinsic-inline-size:inherit;contain-intrinsic-block-size:inherit'></div></div>"
      "<div id='grid-none' class='cg'><div class='child'></div></div>"
      "<div id='grid-zero' class='cg' style='contain-intrinsic-size:0px'><div class='child'></div></div>"
      "<div id='flex' style='display:inline-flex'><div id='flex-item' class='cis'><div class='child'></div></div></div>"
      "<span id='inline-excluded' style='contain:size;contain-intrinsic-size:1px'>visible</span>"
      "<div id='table' style='display:table;contain:size;contain-intrinsic-size:1px'><div style='display:table-cell;width:40px;height:20px'></div></div>"
      "</body></html>";
    const char *css="body{margin:0}.base,.cis{display:inline-block}.cis{contain:size;contain-intrinsic-inline-size:73px;contain-intrinsic-block-size:31px}.bordered{padding:5px;border:3px solid}.child{width:220px;height:90px}.small{width:40px;height:20px}.cg{display:inline-grid;contain:size;grid-template-columns:45px;grid-template-rows:20px}";
    html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));style_recalc(e,&d->node,true);
    Layout *l=layout_new();layout_run(l,d,800,2000);
    size(d,"inner",73,31);size(d,"border",89,47);size(d,"borderbox",89,47);size(d,"limits",80,47);
    size(d,"explicit",45,27);size(d,"ratio",90,45);size(d,"empty",0,0);size(d,"strict",13,7);size(d,"content",40,20);size(d,"none",40,20);
    size(d,"inline",73,20);size(d,"vertical-inline",40,31);size(d,"vertical-block",73,31);
    size(d,"canvas",73,31);size(d,"canvas-ratio",90,45);size(d,"canvas-natural",200,100);
    size(d,"video-normal",300,150);size(d,"video-authored",91,43);size(d,"video-attributes",92,44);size(d,"video-cis",73,31);size(d,"video-ratio",90,45);
    size(d,"physical-last",39,11);size(d,"logical-last",22,11);size(d,"basis-last",41,23);size(d,"priority",39,11);size(d,"inherit-child",37,19);
    size(d,"grid-none",45,20);size(d,"grid-zero",0,0);size(d,"flex-item",73,31);size(d,"flex",73,31);size(d,"table",40,20);
    Box *inner=node(d,"inner")->box;check("contained descendants retained",inner->first!=NULL);check("overflow remains real content",inner->scroll_w>=220);
    check("non-atomic inline excluded",!layout_size_contained(node(d,"inline-excluded")->box,true));
    value(d,"basis-last","contain-intrinsic-width","41px");value(d,"basis-last","contain-intrinsic-inline-size","23px");
    value(d,"inherit-child","contain-intrinsic-size","37px 19px");
    node_set_attr(node(d,"inherit-parent"),"style","writing-mode:vertical-lr;contain-intrinsic-inline-size:53px;contain-intrinsic-block-size:29px");
    style_recalc(e,&d->node,false);layout_run(l,d,800,2000);size(d,"inherit-child",53,29);
    node_set_attr(node(d,"basis-last"),"style","contain:size;contain-intrinsic-inline-size:23px;contain-intrinsic-block-size:41px;writing-mode:horizontal-tb;display:inline-block");
    style_recalc(e,&d->node,false);layout_run(l,d,800,2000);size(d,"basis-last",23,41);
    dispose(d,e,l);

    d=doc_new("https://css-containment.test/hidden");
    html="<!doctype html><html><body><div id='hidden' style='display:inline-block;content-visibility:hidden;contain-intrinsic-size:60px 40px;background:#008000;border:2px solid #0000ff;padding:3px'>"
      "<div id='hidden-child' style='width:90px;height:70px;background:#ff0000;visibility:visible'>HIDDEN</div>"
      "<div id='hidden-fixed' style='position:fixed;left:0;top:0;width:150px;height:90px;background:#ff00ff;z-index:999'></div></div>"
      "<canvas id='hidden-canvas' style='display:block;content-visibility:hidden;width:16px;height:16px;background:#008000'></canvas></body></html>";
    html_parse(d,html,strlen(html));e=style_engine_new(d);css="body{margin:0}";
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));style_recalc(e,&d->node,true);l=layout_new();layout_run(l,d,192,160);
    size(d,"hidden",70,50);size(d,"hidden-child",90,70);check("hidden computed contain remains none",node(d,"hidden")->style->size_containment==0);
    check("hidden creates formatting context",node(d,"hidden")->box->bfc);
    check("hidden owns fixed containing block",node(d,"hidden-fixed")->box->cb==node(d,"hidden")->box);
    Box *owner=node(d,"hidden")->box;Box *h=layout_hit(l,owner->x+7,owner->y+7,0,0);check("hidden child cannot intercept hit",h==owner);
    h=layout_hit(l,owner->x+100,owner->y+7,0,0);check("hidden fixed layer cannot escape",!h||h->node!=node(d,"hidden-fixed"));
    Node *selection_node=NULL;int selection_offset=0;check("hidden text not selectable",!tsel_point_near(l,owner->x+7,owner->y+7,0,NULL,1,&selection_node,&selection_offset));
    DisplayList dl={0};Canvas canvas;canvas_init(&canvas,192,160,1);paint_image_hook=sample_image;image_calls=0;
    dl_build(&dl,l,0,0,192,160);raster(&canvas,&dl,0xffffffff);
    check("hidden owner background retained",pixel(&canvas,owner,7,7)==0xff008000);
    check("hidden owner border retained",pixel(&canvas,owner,0,7)==0xff0000ff);
    check("hidden replaced content suppressed",image_calls==0);check("hidden replaced owner background retained",pixel(&canvas,node(d,"hidden-canvas")->box,5,5)==0xff008000);
    node_set_attr(node(d,"hidden"),"style","display:inline-block;background:#008000;border:2px solid #0000ff;padding:3px");
    node_set_attr(node(d,"hidden-canvas"),"style","display:block;width:16px;height:16px;background:#008000");
    node_set_attr(node(d,"hidden-fixed"),"style","display:none");
    style_recalc(e,&d->node,false);layout_run(l,d,192,160);size(d,"hidden",100,80);
    dl_build(&dl,l,0,0,192,160);raster(&canvas,&dl,0xffffffff);owner=node(d,"hidden")->box;
    check("visible child restored after invalidation",pixel(&canvas,owner,7,7)==0xffff0000);
    check("visible replaced content restored",image_calls>0);check("visible replaced pixels restored",pixel(&canvas,node(d,"hidden-canvas")->box,5,5)==0xffff0000);
    h=layout_hit(l,owner->x+7,owner->y+7,0,0);check("visible child receives hit",h&&h->node==node(d,"hidden-child"));
    dl_clear(&dl);vec_free(dl.items);canvas_free(&canvas);paint_image_hook=NULL;dispose(d,e,l);
    printf("containment: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
