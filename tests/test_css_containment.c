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
static void tracks(Document *d, const char *id, int axis, int count, const float *want) {
    const GridUsedTracks *u=node(d,id)->box->grid_used;
    check("materialized grid track count",u&&u->count[axis]==count);
    if(u&&u->count[axis]==count) for(int i=0;i<count;i++) same(id,u->size[axis][i],want[i]);
}
static void offset(Document *d,const char *child,const char *parent,float x,float y) {
    const Box *a=node(d,child)->box,*b=node(d,parent)->box;
    same(child,a->x-b->x-b->b[3]-b->p[3],x);same(child,a->y-b->y-b->b[0]-b->p[0],y);
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
    check("columns typed shorthand",css_columns_valid("columns","71px 2"));
    check("columns reversed shorthand",css_columns_valid("columns","2 71px"));
    check("columns auto shorthand",css_columns_valid("columns","auto auto"));
    check("columns duplicate widths invalid",!css_columns_valid("columns","1px 2px"));
    check("column count zero invalid",!css_columns_valid("column-count","0"));
    check("column width percent invalid",!css_columns_valid("column-width","10%"));
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
      "<div id='sheet-important' class='base' style='contain:size;contain-intrinsic-inline-size:22px;contain-intrinsic-block-size:11px'></div>"
      "<div id='inline-important' class='base' style='contain:size;contain-intrinsic-width:39px!important;contain-intrinsic-block-size:11px'></div>"
      "<div id='ua-important' class='base' style='contain:size;contain-intrinsic-width:39px!important;contain-intrinsic-block-size:11px'></div>"
      "<div id='all-reset' style='contain-intrinsic-size:39px;all:initial;contain:size;display:inline-block'></div>"
      "<div id='all-after' style='all:initial;contain-intrinsic-size:39px 11px;contain:size;display:inline-block'></div>"
      "<div id='all-invalid' style='contain-intrinsic-size:39px 11px;all:60px;contain:size;display:inline-block'></div>"
      "<div id='all-variable' style='--reset:initial;contain-intrinsic-size:39px;all:var(--reset);contain:size;display:inline-block'></div>"
      "<div id='all-important' style='all:initial!important;contain-intrinsic-size:39px;contain:size;display:inline-block'></div>"
      "<div id='important-before-all' style='contain-intrinsic-size:39px 11px!important;all:initial;contain:size;display:inline-block'></div>"
      "<select id='select-one' style='padding:0;border:0'></select>"
      "<select id='select-multiple' multiple style='padding:0;border:0'></select>"
      "<select id='select-three' size='3' style='padding:0;border:0'></select>"
      "<select id='select-contained' size='3' style='contain:size;padding:0;border:0'></select>"
      "<select id='select-zero' multiple style='contain:size;contain-intrinsic-height:0;padding:0;border:0'></select>"
      "<select id='select-override' multiple style='contain:size;contain-intrinsic-height:23px;padding:0;border:0'></select>"
      "<select id='select-authored' multiple style='contain:size;height:29px;padding:0;border:0'></select>"
      "<button id='button-border' style='width:40px;height:30px;padding:2px;border:3px solid'></button>"
      "<button id='button-content' style='box-sizing:content-box;width:40px;height:30px;padding:2px;border:3px solid'></button>"
      "<button id='button-minimum' style='contain:size;min-width:30px;min-height:15px;padding:0;border:5px solid'></button>"
      "<div id='inherit-parent' style='writing-mode:vertical-lr;contain-intrinsic-inline-size:37px;contain-intrinsic-block-size:19px'>"
      "<div id='inherit-child' class='base' style='writing-mode:horizontal-tb;contain:size;contain-intrinsic-inline-size:inherit;contain-intrinsic-block-size:inherit'></div>"
      "<div id='all-inherit' style='all:inherit;contain:size;display:inline-block;writing-mode:horizontal-tb'></div></div>"
      "<div id='columns-max' style='display:inline-block;contain:size;columns:71px 2;column-gap:7px'></div>"
      "<div id='columns-min' style='display:inline-block;contain:size;columns:71px 2;column-gap:7px;width:min-content'></div>"
      "<div id='columns-zero' style='display:inline-block;contain:size;columns:71px 2;column-gap:7px;contain-intrinsic-size:0px'></div>"
      "<div id='columns-override' style='display:inline-block;contain:size;columns:71px 2;column-gap:7px;contain-intrinsic-size:51px 19px'></div>"
      "<div id='columns-normal-gap' style='display:inline-block;contain:size;columns:71px 2;font-size:10px'></div>"
      "<div id='columns-count' style='display:inline-block;contain:size;column-count:2;column-gap:7px'></div>"
      "<div id='columns-empty-natural' style='display:inline-block;columns:71px 2;column-gap:7px'></div>"
      "<div id='grid-none' class='cg'><div class='child'></div></div>"
      "<div id='grid-zero' class='cg' style='contain-intrinsic-size:0px'><div class='child'></div></div>"
      "<div id='flex' style='display:inline-flex'><div id='flex-item' class='cis'><div class='child'></div></div></div>"
      "<span id='inline-excluded' style='contain:size;contain-intrinsic-size:1px'>visible</span>"
      "<div id='table' style='display:table;contain:size;contain-intrinsic-size:1px'><div style='display:table-cell;width:40px;height:20px'></div></div>"
      "</body></html>";
    const char *css="body{margin:0}.base,.cis{display:inline-block}.cis{contain:size;contain-intrinsic-inline-size:73px;contain-intrinsic-block-size:31px}.bordered{padding:5px;border:3px solid}.child{width:220px;height:90px}.small{width:40px;height:20px}.cg{display:inline-grid;contain:size;grid-template-columns:45px;grid-template-rows:20px}";
    html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
    const char *author_priority="#sheet-important{contain-intrinsic-width:39px!important}#inline-important{contain-intrinsic-inline-size:22px!important}#ua-important{contain-intrinsic-width:23px!important}";
    const char *ua_priority="#ua-important{contain-intrinsic-inline-size:43px!important}";
    style_engine_add_sheet(e,css_parse_sheet(author_priority,strlen(author_priority),d->url,1,&e->media));
    style_engine_add_sheet(e,css_parse_sheet(ua_priority,strlen(ua_priority),d->url,0,&e->media));style_recalc(e,&d->node,true);
    Layout *l=layout_new();layout_run(l,d,800,2000);
    size(d,"inner",73,31);size(d,"border",89,47);size(d,"borderbox",89,47);size(d,"limits",80,47);
    size(d,"explicit",45,27);size(d,"ratio",90,45);size(d,"empty",0,0);size(d,"strict",13,7);size(d,"content",40,20);size(d,"none",40,20);
    size(d,"inline",73,20);size(d,"vertical-inline",40,31);size(d,"vertical-block",73,31);
    size(d,"canvas",73,31);size(d,"canvas-ratio",90,45);size(d,"canvas-natural",200,100);
    size(d,"video-normal",300,150);size(d,"video-authored",91,43);size(d,"video-attributes",92,44);size(d,"video-cis",73,31);size(d,"video-ratio",90,45);
    size(d,"physical-last",39,11);size(d,"logical-last",22,11);size(d,"basis-last",41,23);size(d,"priority",39,11);size(d,"inherit-child",37,19);size(d,"all-inherit",19,37);
    size(d,"sheet-important",39,11);size(d,"inline-important",39,11);size(d,"ua-important",43,11);
    size(d,"all-reset",0,0);size(d,"all-after",39,11);size(d,"all-invalid",39,11);size(d,"all-variable",0,0);size(d,"all-important",0,0);size(d,"important-before-all",39,11);
    float select_row=style_line_height(node(d,"select-one")->style,style_font(node(d,"select-one")->style));
    same("single-select own row",node(d,"select-one")->box->h,select_row);
    same("multiple-select default four rows",node(d,"select-multiple")->box->h,select_row*4);
    same("authored select display size",node(d,"select-three")->box->h,select_row*3);
    same("contained none retains own rows",node(d,"select-contained")->box->h,select_row*3);
    same("zero overrides native select rows",node(d,"select-zero")->box->h,0);
    same("intrinsic override replaces select rows",node(d,"select-override")->box->h,23);
    same("authored select height still wins",node(d,"select-authored")->box->h,29);
    size(d,"button-border",40,30);size(d,"button-content",50,40);size(d,"button-minimum",30,15);
    size(d,"columns-max",149,0);size(d,"columns-min",0,0);size(d,"columns-zero",0,0);size(d,"columns-override",51,19);
    size(d,"columns-normal-gap",152,0);size(d,"columns-count",7,0);size(d,"columns-empty-natural",149,0);
    value(d,"columns-max","columns","71px 2");value(d,"columns-count","columns","2");
    size(d,"grid-none",45,20);size(d,"grid-zero",0,0);size(d,"flex-item",73,31);size(d,"flex",73,31);size(d,"table",40,20);
    Box *inner=node(d,"inner")->box;check("contained descendants retained",inner->first!=NULL);check("overflow remains real content",inner->scroll_w>=220);
    check("non-atomic inline excluded",!layout_size_contained(node(d,"inline-excluded")->box,true));
    value(d,"basis-last","contain-intrinsic-width","41px");value(d,"basis-last","contain-intrinsic-inline-size","23px");
    value(d,"inherit-child","contain-intrinsic-size","37px 19px");
    node_set_attr(node(d,"inherit-parent"),"style","writing-mode:vertical-lr;contain-intrinsic-inline-size:53px;contain-intrinsic-block-size:29px");
    style_recalc(e,&d->node,false);layout_run(l,d,800,2000);size(d,"inherit-child",53,29);size(d,"all-inherit",29,53);
    node_set_attr(node(d,"select-three"),"size","5");style_recalc(e,&d->node,false);layout_run(l,d,800,2000);
    same("dynamic select display rows",node(d,"select-three")->box->h,select_row*5);
    node_set_attr(node(d,"basis-last"),"style","contain:size;contain-intrinsic-inline-size:23px;contain-intrinsic-block-size:41px;writing-mode:horizontal-tb;display:inline-block");
    style_recalc(e,&d->node,false);layout_run(l,d,800,2000);size(d,"basis-last",23,41);
    dispose(d,e,l);

    d=doc_new("https://css-containment.test/repeat");
    html="<!doctype html><html><body>"
      "<div id='fit' class='repeat' style='position:relative;grid-template-columns:repeat(auto-fit,20px);grid-template-rows:repeat(auto-fit,10px)'>"
      "<div id='fit-a'></div><div id='fit-b'></div><div id='fit-abs' style='position:absolute;grid-column:1/3;grid-row:1/2;inset:0'></div></div>"
      "<div id='fill' class='repeat' style='grid-template-columns:repeat(auto-fill,20px);grid-template-rows:repeat(auto-fill,10px)'><div></div><div></div></div>"
      "<div id='pattern' style='display:grid;width:95px;height:10px;column-gap:5px;grid-template-columns:repeat(auto-fill,10px 20px)'><div></div><div></div><div></div><div></div></div>"
      "<div id='zero-prefix' style='display:grid;width:45px;height:10px;column-gap:5px;grid-template-columns:0px repeat(auto-fill,10px)'></div>"
      "<div id='fraction-fit' style='display:grid;width:100px;height:10px;column-gap:5px;grid-template-columns:repeat(auto-fit,minmax(20px,1fr))'><div></div><div></div></div>"
      "<div id='fraction-fill' style='display:grid;width:100px;height:10px;column-gap:5px;grid-template-columns:repeat(auto-fill,minmax(20px,1fr))'><div></div><div></div></div>"
      "<div id='indefinite-rows' style='display:grid;width:60px;gap:2px;grid-template-columns:20px 20px;grid-template-rows:repeat(auto-fill,11px)'><div></div><div></div></div>"
      "<div id='contained-repeat' style='display:grid;width:max-content;contain:size;contain-intrinsic-size:70px 80px;gap:5px;grid-template-columns:repeat(auto-fit,15px);grid-template-rows:repeat(auto-fit,10px)'>"
      "<div></div><div></div><div></div><div></div><div></div><div id='contained-last'></div></div>"
      "<div id='empty-fit' class='repeat' style='grid-template-columns:repeat(auto-fit,20px);grid-template-rows:repeat(auto-fit,10px);justify-content:space-around;align-content:space-evenly'></div>"
      "<div id='empty-fit-intrinsic' style='display:inline-grid;contain:size;grid-template-columns:repeat(auto-fit,20px);grid-template-rows:repeat(auto-fit,10px);gap:5px'></div>"
      "<div id='bounded-repeat' style='display:grid;width:5000px;grid-template-columns:repeat(auto-fill,1px)'></div>"
      "</body></html>";
    html_parse(d,html,strlen(html));e=style_engine_new(d);css="body{margin:0}.repeat{display:grid;width:100px;height:70px;column-gap:5px;row-gap:4px}";
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));style_recalc(e,&d->node,true);l=layout_new();layout_run(l,d,800,2000);
    tracks(d,"fit",0,4,(float[]){20,20,0,0});tracks(d,"fit",1,5,(float[]){10,0,0,0,0});
    offset(d,"fit-a","fit",0,0);offset(d,"fit-b","fit",25,0);size(d,"fit-a",20,10);size(d,"fit-abs",45,10);
    tracks(d,"fill",0,4,(float[]){20,20,20,20});tracks(d,"fill",1,5,(float[]){10,10,10,10,10});
    tracks(d,"pattern",0,4,(float[]){10,20,10,20});tracks(d,"zero-prefix",0,4,(float[]){0,10,10,10});
    tracks(d,"fraction-fit",0,4,(float[]){47.5f,47.5f,0,0});tracks(d,"fraction-fill",0,4,(float[]){21.25f,21.25f,21.25f,21.25f});
    tracks(d,"indefinite-rows",1,1,(float[]){11});size(d,"indefinite-rows",60,11);
    size(d,"contained-repeat",70,80);tracks(d,"contained-repeat",0,3,(float[]){15,15,15});tracks(d,"contained-repeat",1,5,(float[]){10,10,0,0,0});
    offset(d,"contained-last","contained-repeat",40,15);size(d,"contained-last",15,10);
    tracks(d,"empty-fit",0,4,(float[]){0,0,0,0});tracks(d,"empty-fit",1,5,(float[]){0,0,0,0,0});
    size(d,"empty-fit-intrinsic",0,0);
    check("materialization remains bounded",node(d,"bounded-repeat")->box->grid_used->count[0]<=1000);
    value(d,"fit","grid-template-columns","20px 20px 0px 0px");
    node_set_attr(node(d,"fit"),"style","position:relative;width:60px;grid-template-columns:repeat(auto-fit,20px);grid-template-rows:repeat(auto-fit,10px)");
    style_recalc(e,&d->node,false);layout_run(l,d,800,2000);tracks(d,"fit",0,2,(float[]){20,20});size(d,"fit-abs",45,10);
    node_set_attr(node(d,"fit-b"),"style","display:none");style_recalc(e,&d->node,false);layout_run(l,d,800,2000);
    tracks(d,"fit",0,2,(float[]){20,0});size(d,"fit-abs",20,10);value(d,"fit","grid-template-columns","20px 0px");
    dispose(d,e,l);

    d=doc_new("https://css-containment.test/hidden");
    html="<!doctype html><html><body><div id='hidden' style='display:inline-block;content-visibility:hidden;contain-intrinsic-size:60px 40px;background:#008000;border:2px solid #0000ff;padding:3px'>"
      "<div id='hidden-child' style='width:90px;height:70px;background:#ff0000;visibility:visible'>HIDDEN</div>"
      "<div id='hidden-fixed' style='position:fixed;left:0;top:0;width:150px;height:90px;background:#ff00ff;z-index:999'></div></div>"
      "<canvas id='hidden-canvas' style='display:block;content-visibility:hidden;width:16px;height:16px;background:#008000'></canvas>"
      "<table id='hidden-table' style='content-visibility:hidden;border-spacing:0'><tr><td id='hidden-cell' style='content-visibility:hidden;padding:0'>"
      "<div id='table-child' style='width:20px;height:20px;background:#ff0000'></div></td></tr></table>"
      "<svg id='hidden-svg' style='display:block;content-visibility:hidden;width:20px;height:20px;background:#008000'><rect id='hidden-shape' width='20' height='20' fill='#ff0000'></rect></svg></body></html>";
    html_parse(d,html,strlen(html));e=style_engine_new(d);css="body{margin:0}";
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));style_recalc(e,&d->node,true);l=layout_new();layout_run(l,d,192,160);
    size(d,"hidden",70,50);size(d,"hidden-child",90,70);check("hidden computed contain remains none",node(d,"hidden")->style->size_containment==0);
    check("hidden creates formatting context",node(d,"hidden")->box->bfc);
    check("skipping owner keeps observable box",!layout_has_skipped_ancestor(node(d,"hidden")->box));
    check("hidden descendant has skipped ancestor",layout_has_skipped_ancestor(node(d,"hidden-child")->box));
    check("table excludes hidden containment",!layout_contents_hidden(node(d,"hidden-table")->box));
    check("table-cell excludes hidden containment",!layout_contents_hidden(node(d,"hidden-cell")->box));
    check("table contents remain observable",!layout_has_skipped_ancestor(node(d,"table-child")->box));
    check("hidden SVG child not refined as input target",svg_hit(node(d,"hidden-svg"),5,5,20,20)==NULL);
    check("hidden owns fixed containing block",node(d,"hidden-fixed")->box->cb==node(d,"hidden")->box);
    Box *owner=node(d,"hidden")->box;Box *h=layout_hit(l,owner->x+7,owner->y+7,0,0);check("hidden child cannot intercept hit",h==owner);
    h=layout_hit(l,owner->x+100,owner->y+7,0,0);check("hidden fixed layer cannot escape",!h||h->node!=node(d,"hidden-fixed"));
    Node *selection_node=NULL;int selection_offset=0;check("hidden text not selectable",!tsel_point_near(l,owner->x+7,owner->y+7,0,NULL,1,&selection_node,&selection_offset));
    DisplayList dl={0};Canvas canvas;canvas_init(&canvas,192,160,1);paint_image_hook=sample_image;image_calls=0;
    dl_build(&dl,l,0,0,192,160);raster(&canvas,&dl,0xffffffff);
    check("hidden owner background retained",pixel(&canvas,owner,7,7)==0xff008000);
    check("hidden owner border retained",pixel(&canvas,owner,0,7)==0xff0000ff);
    check("excluded table contents still painted",pixel(&canvas,node(d,"table-child")->box,5,5)==0xffff0000);
    Box *table_child=node(d,"table-child")->box;
    h=layout_hit(l,table_child->x+5,table_child->y+5,0,0);check("excluded table child still hit",h&&h->node==table_child->node);
    check("hidden SVG own background retained",pixel(&canvas,node(d,"hidden-svg")->box,5,5)==0xff008000);
    check("hidden replaced content suppressed",image_calls==0);check("hidden replaced owner background retained",pixel(&canvas,node(d,"hidden-canvas")->box,5,5)==0xff008000);
    node_set_attr(node(d,"hidden"),"style","display:inline-block;background:#008000;border:2px solid #0000ff;padding:3px");
    node_set_attr(node(d,"hidden-canvas"),"style","display:block;width:16px;height:16px;background:#008000");
    node_set_attr(node(d,"hidden-fixed"),"style","display:none");
    node_set_attr(node(d,"hidden-svg"),"style","display:block;width:20px;height:20px;background:#008000");
    style_recalc(e,&d->node,false);layout_run(l,d,192,160);size(d,"hidden",100,80);
    dl_build(&dl,l,0,0,192,160);raster(&canvas,&dl,0xffffffff);owner=node(d,"hidden")->box;
    check("visible child restored after invalidation",pixel(&canvas,owner,7,7)==0xffff0000);
    check("unhidden descendant no skipped ancestor",!layout_has_skipped_ancestor(node(d,"hidden-child")->box));
    check("visible SVG child input restored",svg_hit(node(d,"hidden-svg"),5,5,20,20)==doc_get_element_by_id(d,"hidden-shape"));
    check("visible SVG child paint restored",pixel(&canvas,node(d,"hidden-svg")->box,5,5)==0xffff0000);
    check("visible replaced content restored",image_calls>0);check("visible replaced pixels restored",pixel(&canvas,node(d,"hidden-canvas")->box,5,5)==0xffff0000);
    h=layout_hit(l,owner->x+7,owner->y+7,0,0);check("visible child receives hit",h&&h->node==node(d,"hidden-child"));
    dl_clear(&dl);vec_free(dl.items);canvas_free(&canvas);paint_image_hook=NULL;dispose(d,e,l);

    /* Retained backing offsets are distinct from the hidden owner's used
     * position. An ordinary overflow:hidden box remains scrollable. */
    {
        d=doc_new("https://css-containment.test/scroll-retention");
        html="<!doctype html><html><body><div id='scroll' style='width:120px;height:100px;overflow:hidden;background:#008000'>"
          "<div id='scroll-child' style='width:400px;height:400px;background:#ff0000'></div></div></body></html>";
        html_parse(d,html,strlen(html));e=style_engine_new(d);css="body{margin:0}";
        style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
        style_recalc(e,&d->node,true);l=layout_new();layout_run(l,d,160,140);
        Node *sc=node(d,"scroll"),*contents=node(d,"scroll-child");
        sc->scroll_x=80;sc->scroll_y=90;layout_run(l,d,160,140);
        check("ordinary overflow:hidden remains active",box_scroll_active(sc->box));
        same("visible scroll left",box_scroll_left(sc->box),80);same("visible scroll top",box_scroll_top(sc->box),90);
        node_set_attr(sc,"style","width:120px;height:100px;overflow:hidden;content-visibility:hidden;background:#008000");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("hidden backing left retained",sc->scroll_x,80);same("hidden backing top retained",sc->scroll_y,90);
        same("hidden used left",box_scroll_left(sc->box),0);same("hidden used top",box_scroll_top(sc->box),0);
        same("hidden painted y",box_scroll_y(sc->box),0);check("hidden owner rejects element scrolling",!box_scroll_active(sc->box));
        same("hidden scroll extent width",sc->box->scroll_w,sc->box->w);same("hidden scroll extent height",sc->box->scroll_h,sc->box->h);
        check("retained offset does not move hidden hit target",layout_hit(l,5,5,0,0)==sc->box);
        DisplayList retained_dl={0};Canvas retained_canvas;canvas_init(&retained_canvas,160,140,1);
        dl_build(&retained_dl,l,0,0,160,140);raster(&retained_canvas,&retained_dl,0xffffffff);
        check("retained hidden owner background",pixel(&retained_canvas,sc->box,5,5)==0xff008000);
        dl_clear(&retained_dl);vec_free(retained_dl.items);canvas_free(&retained_canvas);
        node_set_attr(sc,"style","width:120px;height:100px;overflow:visible;content-visibility:hidden");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("hidden non-scroller retains left",sc->scroll_x,80);same("hidden non-scroller retains top",sc->scroll_y,90);
        same("hidden non-scroller used left",box_scroll_left(sc->box),0);same("hidden non-scroller used top",box_scroll_top(sc->box),0);
        same("hidden descendants do not expand document width",l->doc_w,160);same("hidden document keeps minimum viewport height",l->doc_h,140);
        node_set_attr(sc,"style","width:120px;height:100px;overflow:visible");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("visible non-scroller keeps backing left",sc->scroll_x,80);same("visible non-scroller keeps backing top",sc->scroll_y,90);
        same("visible non-scroller used left",box_scroll_left(sc->box),0);same("visible non-scroller used top",box_scroll_top(sc->box),0);
        check("ordinary visible overflow expands document",l->doc_w>=400&&l->doc_h>=400);
        node_set_attr(sc,"style","width:120px;height:100px;overflow:hidden");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("restored scroll container left",box_scroll_left(sc->box),80);same("restored scroll container top",box_scroll_top(sc->box),90);
        node_set_attr(sc,"style","width:120px;height:100px;overflow:hidden;content-visibility:hidden");
        node_set_attr(contents,"style","width:150px;height:130px");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("hidden range change retains backing left",sc->scroll_x,80);same("hidden range change retains backing top",sc->scroll_y,90);
        node_set_attr(sc,"style","width:120px;height:100px;overflow:hidden");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("reveal clamps left to real range",box_scroll_left(sc->box),30);same("reveal clamps top to real range",box_scroll_top(sc->box),30);
        node_set_attr(contents,"style","width:400px;height:400px");style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("regrowth does not resurrect unclamped left",box_scroll_left(sc->box),30);same("regrowth does not resurrect unclamped top",box_scroll_top(sc->box),30);
        node_set_attr(sc,"style","display:flex;flex-direction:column-reverse;width:120px;height:100px;overflow:hidden");
        node_set_attr(contents,"style","width:400px;height:400px;flex-shrink:0");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);sc->scroll_x=80;sc->scroll_y=-90;layout_run(l,d,160,140);
        same("reverse scroller retains signed top",box_scroll_top(sc->box),-90);
        float painted_y=box_scroll_y(sc->box);
        same("reverse existing painted conversion",painted_y,LMAX(0,sc->box->scroll_h-sc->box->h)-90);
        node_set_attr(sc,"style","display:flex;flex-direction:column-reverse;width:120px;height:100px;overflow:hidden;content-visibility:hidden");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("hidden reverse backing top retained",sc->scroll_y,-90);same("hidden reverse effective top",box_scroll_top(sc->box),0);
        same("hidden reverse effective painted y",box_scroll_y(sc->box),0);
        node_set_attr(sc,"style","display:flex;flex-direction:column-reverse;width:120px;height:100px;overflow:hidden");
        style_recalc(e,&d->node,false);layout_run(l,d,160,140);
        same("reverse signed top restored",box_scroll_top(sc->box),-90);same("reverse painted origin restored",box_scroll_y(sc->box),painted_y);
        dispose(d,e,l);
    }
    printf("containment: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
