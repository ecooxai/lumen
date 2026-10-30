/* Native sizing regressions: independent physical dimensions and destination
 * retention, including a negative definite inset remainder and anonymous CBs. */
#include "../src/layout/layout.h"
#include <math.h>
#include <stdio.h>
#include <assert.h>
static unsigned checks, failures;
static void same(const char *name, float actual, float expected) {
    checks++;
    if (!(fabsf(actual - expected) < .01f)) {
        failures++; fprintf(stderr, "%s: expected %g, got %g\n", name, expected, actual);
    }
}
static void size(Document *d, const char *id, float w, float h) {
    Node *n = doc_get_element_by_id(d, id);
    if (!n || !n->box) { failures += 2; checks += 2; fprintf(stderr, "missing box %s\n", id); return; }
    same(id, n->box->w, w); same(id, n->box->h, h);
}
static void offset(Document *d, const char *parent, const char *id, float x, float y) {
    Node *p=doc_get_element_by_id(d,parent),*n=doc_get_element_by_id(d,id);
    if(!p||!p->box||!n||!n->box){checks+=2;failures+=2;return;}
    same(id,n->box->x-p->box->x-p->box->b[3],x);
    same(id,n->box->y-p->box->y-p->box->b[0],y);
}
int main(void) {
    dom_init(); font_init();
    assert(sizeof(Length) == 28);
    ComputedStyle *st = style_new_default(), *parent = style_new_default();
    Box cb = { .kind=BX_BLOCK, .fmt=FMT_FLOW, .st=parent }, child = { .kind=BX_BLOCK, .st=st, .parent=&cb };
    child.m[3] = -30; child.m[1] = -10;
    same("negative definite remainder", layout_stretch_border_size(&child,&cb,true,-20,true),20);
    same("indefinite is distinct", layout_stretch_border_size(&child,&cb,true,-20,false),-1);
    child.p[3]=child.p[1]=6;
    same("inner size floor", layout_stretch_border_size(&child,&cb,true,-100,true),12);
    child.m[0]=10;child.m[2]=20;
    same("block margin simulation",layout_stretch_border_size(&child,&cb,false,100,true),100);
    cb.b[0]=1;
    same("one bordered edge",layout_stretch_border_size(&child,&cb,false,100,true),90);
    cb.bfc=true;
    same("independent containing block",layout_stretch_border_size(&child,&cb,false,100,true),70);
    st->margin[0]=L_auto();child.m[0]=50;
    same("distributed auto remains zero",layout_stretch_border_size(&child,&cb,false,100,true),80);
    parent->writing_mode=1;cb.bfc=false;cb.b[1]=1;child.m[3]=7;child.m[1]=3;
    same("parent block axis",layout_stretch_border_size(&child,&cb,true,50,true),47);
    css_apply_decl(parent,NULL,"writing-mode","sideways-rl",NULL,NULL);
    same("inherited sideways sizing axis",layout_stretch_border_size(&child,&cb,true,50,true),47);
    const char *sizing[]={"width","height","min-width","max-width","min-height","max-height","inline-size","block-size","flex-basis"};
    const Length *destinations[]={&st->width,&st->height,&st->min_width,&st->max_width,&st->min_height,&st->max_height,&st->width,&st->height,&st->flex_basis};
    for(size_t i=0;i<sizeof sizing/sizeof *sizing;i++) {
        css_apply_decl(st,NULL,sizing[i],"stretch",NULL,NULL);
        same(sizing[i],destinations[i]->kind,LK_STRETCH);
    }
    css_apply_decl(st,NULL,"margin-left","7px",NULL,NULL);
    css_apply_decl(st,NULL,"margin-left","stretch",NULL,NULL);same("invalid margin retention",st->margin[3].px,7);
    css_apply_decl(st,NULL,"padding-left","7px",NULL,NULL);
    css_apply_decl(st,NULL,"padding-left","-webkit-fill-available",NULL,NULL);same("invalid padding retention",st->padding[3].px,7);
    css_apply_decl(st,NULL,"font-size","7px",NULL,NULL);
    css_apply_decl(st,NULL,"font-size","stretch",NULL,NULL);same("invalid font retention",st->font_size,7);
    css_apply_decl(st,NULL,"width","-webkit-fill-available",NULL,NULL);same("legacy sizing kind",st->width.kind,LK_STRETCH);
    style_free(st);style_free(parent);
    Document *d=doc_new("https://css-stretch.test/");
    const char *html="<html><body>"
      "<div id='parent' style='width:120px;height:100px;border:1px solid'><div id='normal' class='box'></div></div>"
      "<div style='width:120px;height:100px;border:1px solid'><div id='border-box' class='box' style='box-sizing:border-box'></div></div>"
      "<div style='width:120px;height:100px'><div id='collapse' style='height:stretch;margin:10px'></div></div>"
      "<div style='position:relative;width:100px;height:80px;border:1px solid'><div id='negative' style='position:absolute;left:70px;right:50px;top:60px;bottom:40px;margin:-30px -10px -10px -30px;width:stretch;height:stretch'></div></div>"
      "<div style='width:200px;height:50px;border:1px solid'><canvas id='ratio' width='200' height='100' class='canvas'></canvas></div>"
      "<div style='width:200px;height:50px;border:1px solid'><canvas id='ratio-min' width='200' height='100' class='canvas' style='width:1px;min-width:fit-content'></canvas></div>"
      "<div style='width:200px;height:50px;border:1px solid'><canvas id='ratio-max' width='200' height='100' class='canvas' style='width:100px;max-width:fit-content'></canvas></div>"
      "<div style='width:200px'><div style='float:left;width:100px;height:75px'></div><div id='float-min' class='bfc' style='width:0;min-width:stretch'></div><div id='float-max' class='bfc' style='width:1000px;max-width:stretch'></div><div id='float-margin' class='bfc' style='width:stretch;margin-left:10px;padding:0 10px;border:solid;border-width:0 10px'></div></div>"
      "<div style='width:100px;height:100px;border:1px solid'><div></div><span style='border:2px solid'><div id='anonymous' style='width:50px;margin:25px;height:stretch'></div></span></div>"
      "<div id='ltr-cb' style='position:relative;width:100px;height:80px;border:1px solid'><div id='ltr-floor' class='absfloor'></div></div>"
      "<div id='rtl-cb' style='position:relative;width:100px;height:80px;border:1px solid;direction:rtl'><div id='rtl-floor' class='absfloor'></div></div>"
      "<div id='vertical-cb' style='position:relative;width:100px;height:80px;border:1px solid;writing-mode:vertical-rl;direction:rtl'><div id='vertical-floor' class='absfloor'></div></div>"
      "<div style='height:100px'><div id='plain-ratio' style='height:stretch;aspect-ratio:2/1'></div></div>"
      "<div style='height:200px'><div id='float-ratio' style='float:left;height:stretch;aspect-ratio:2/1'></div></div>"
      "<div id='static-cb' style='position:relative;width:50px;height:40px;padding:3px 5px;border:2px solid'><div id='static-size' style='position:absolute;width:stretch;height:20px;margin-left:2px;margin-right:3px;padding:2px;border:3px solid'></div></div>"
      "<div style='width:200px'><div style='float:left;width:100px;height:100px'></div><canvas id='float-canvas' width='1' height='1' style='display:block;width:stretch'></canvas></div>"
      "<div id='empty-cb' style='width:100px;height:100px'><div style='display:flow-root'></div><div id='after-bfc' style='height:stretch;margin:10px;border:3px solid'></div></div>"
      "<div style='display:flex;width:50px;height:40px'><div id='flex-max' style='width:75px;max-width:stretch;margin:0 3px 0 2px;padding:2px;border:3px solid'><div style='width:100px;height:10px'></div></div></div>"
      "<div style='display:flex;width:50px;height:40px'><div id='flex-explicit-min' style='width:75px;min-width:80px;max-width:stretch;padding:2px;border:3px solid'></div></div>"
      "<div style='display:flex;width:200px;height:200px'><canvas id='flex-canvas-min' width='100' height='100' style='height:stretch;flex-basis:0'></canvas></div>"
      "<div style='display:flex;width:200px;height:200px'><canvas id='flex-canvas-base' width='100' height='100' style='height:stretch;min-width:0;flex-basis:auto'></canvas></div>"
      "<div id='static-rtl-cb' style='position:relative;width:100px;height:80px;padding:5px;border:3px solid;direction:rtl'><div id='static-rtl' style='position:absolute;width:stretch;height:stretch'></div></div>"
      "<div id='static-v-cb' style='position:relative;width:100px;height:80px;padding:5px;border:3px solid;writing-mode:vertical-lr;direction:rtl'><div id='static-v' style='position:absolute;width:stretch;height:stretch'></div></div>"
      "<div id='lower-cb' style='display:flow-root;width:200px'><div style='float:left;width:150px;height:20px'></div><div id='lower-slot' style='display:flow-root;width:stretch;height:10px;padding:0 50px'></div></div>"
      "<div style='display:flow-root;width:100px'><div id='probe-parent' style='height:100px'><div style='float:left;width:30px;height:30px'></div><div id='probe-bfc' style='display:flow-root;position:relative;width:90px;height:stretch;margin-top:50px'><div id='probe-abs' style='position:absolute;left:0;top:0;width:5px;height:5px'></div></div></div></div>"
      "<div style='display:flow-root'><span id='ws-a' style='float:left'>AA</span><span id='ws-tail' style='float:left'>AA </span><span id='ws-inner' style='float:left'>A A</span><span id='ws-pre' style='float:left;white-space:pre'>AA </span><span id='ws-boundary' style='float:left'>AA<span> </span></span><span id='ws-boundary-inner' style='float:left'>A<span> </span>A</span></div>"
      "</body></html>";
    const char *css="body{margin:0}.box{width:stretch;height:stretch;margin:10px 20px;border:2px solid;padding:3px}.canvas{height:stretch;margin:5px;border:2px solid;padding:3px}.bfc{display:flow-root;height:25px}.absfloor{position:absolute;left:70px;right:50px;top:60px;bottom:40px;margin:-30px -10px -10px -30px;padding:25px;width:stretch;height:stretch}";
    html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));style_recalc(e,&d->node,true);
    Layout *l=layout_new();layout_run(l,d,800,1600);
    size(d,"normal",80,80);size(d,"border-box",80,80);size(d,"collapse",100,100);
    size(d,"negative",40,40);size(d,"ratio",70,40);size(d,"ratio-min",70,40);size(d,"ratio-max",70,40);
    size(d,"float-min",100,25);size(d,"float-max",100,25);size(d,"float-margin",100,25);size(d,"anonymous",50,50);
    size(d,"ltr-floor",50,50);offset(d,"ltr-cb","ltr-floor",40,30);
    size(d,"rtl-floor",50,50);offset(d,"rtl-cb","rtl-floor",10,30);
    size(d,"vertical-floor",50,50);offset(d,"vertical-cb","vertical-floor",10,0);
    size(d,"plain-ratio",200,100);size(d,"float-ratio",400,200);
    size(d,"static-size",50,30);offset(d,"static-cb","static-size",7,3);
    size(d,"float-canvas",100,100);size(d,"after-bfc",80,100);offset(d,"empty-cb","after-bfc",10,10);
    same("flex automatic minimum capped",doc_get_element_by_id(d,"flex-max")->box->w,45);
    same("explicit flex minimum wins",doc_get_element_by_id(d,"flex-explicit-min")->box->w,90);
    size(d,"flex-canvas-min",200,200);size(d,"flex-canvas-base",200,200);
    size(d,"static-rtl",105,85);offset(d,"static-rtl-cb","static-rtl",0,5);
    size(d,"static-v",105,85);offset(d,"static-v-cb","static-v",5,0);
    size(d,"lower-slot",200,10);offset(d,"lower-cb","lower-slot",0,20);
    size(d,"probe-abs",5,5);offset(d,"probe-bfc","probe-abs",0,0);
    int probe_regs=0;Box *probe_abs=doc_get_element_by_id(d,"probe-abs")->box;
    for(int i=0;i<l->nalog;i++) if(l->alog[i]==probe_abs) probe_regs++;
    same("probe absolute registered once",probe_regs,1);
    same("probe flag restored",l->measure_only,0);
    same("bounded probe layout calls",doc_get_element_by_id(d,"probe-bfc")->box->dcalls<=4,1);
    float plain=doc_get_element_by_id(d,"ws-a")->box->w, interior=doc_get_element_by_id(d,"ws-inner")->box->w;
    same("collapsible trailing space",doc_get_element_by_id(d,"ws-tail")->box->w,plain);
    same("interior space remains",interior>plain,1);
    same("preformatted trailing space remains",doc_get_element_by_id(d,"ws-pre")->box->w>plain,1);
    same("trailing inline-boundary space",doc_get_element_by_id(d,"ws-boundary")->box->w,plain);
    same("interior inline-boundary space",doc_get_element_by_id(d,"ws-boundary-inner")->box->w,interior);
    node_set_attr(doc_get_element_by_id(d,"parent"),"style","width:180px;height:140px;border:1px solid");
    style_recalc(e,&d->node,true);layout_run(l,d,800,1600);size(d,"normal",140,120);
    layout_free(l);style_engine_free(e);doc_free(d);
    d=doc_new("https://css-stretch.test/grid-clearance");
    const char *regressions="<!doctype html><html><body style='margin:0'>"
      "<div style='display:grid;position:relative;width:50px;height:40px;padding:3px 5px;border:2px solid'>"
      "<div id='grid-static-size' style='position:absolute;width:stretch;height:stretch;margin:1px 3px 2px 2px;border:3px solid;padding:2px'></div></div>"
      "<div style='display:grid;position:relative;width:50px;height:40px;padding:3px 5px;border:2px solid'>"
      "<div id='grid-static-max' style='position:absolute;width:100px;height:100px;max-width:stretch;max-height:stretch;margin:1px 3px 2px 2px;box-sizing:border-box;border:3px solid;padding:2px'></div></div>"
      "<div id='clear-stage' style='display:flow-root;width:200px'>"
      "<div style='float:left;width:100px;height:30px'></div>"
      "<div id='clear-parent' style='height:100px'>"
      "<div id='clear-wide' style='display:flow-root;width:200px;height:stretch;margin-top:50px'></div>"
      "</div></div>"
      "<div id='clear-stage-short' style='display:flow-root;width:200px'>"
      "<div style='float:left;width:100px;height:30px'></div>"
      "<div id='clear-parent-short' style='height:100px'>"
      "<div id='clear-wide-short' style='display:flow-root;width:200px;height:stretch;margin-top:10px'></div>"
      "</div></div></body></html>";
    html_parse(d,regressions,strlen(regressions));e=style_engine_new(d);style_recalc(e,&d->node,true);l=layout_new();layout_run(l,d,800,600);
    size(d,"grid-static-size",55,43);size(d,"grid-static-max",55,43);
    /* Independently verified in Chromium156 and immutable222: the50px
       collapsed margin already clears an external30px float; the10px case
       still intersects it and must suppress collapse before moving below. */
    offset(d,"clear-stage","clear-parent",0,50);offset(d,"clear-stage","clear-wide",0,50);
    offset(d,"clear-stage-short","clear-parent-short",0,0);offset(d,"clear-stage-short","clear-wide-short",0,30);
    layout_free(l);style_engine_free(e);doc_free(d);
    /* Fixed tracks contribute their own size during intrinsic measurement;
       trimming trailing text whitespace must not accidentally change it. */
    const char *grid_modes[] = { "float:left", "position:absolute", "width:max-content", "width:fit-content" };
    for (size_t i = 0; i < sizeof grid_modes / sizeof *grid_modes; i++) {
        d = doc_new("https://css-stretch.test/intrinsic-grid");
        SB page; sb_init(&page);
        sb_printf(&page, "<!doctype html><div style='position:relative;width:300px;height:20px'>"
            "<div id='gp' style='display:grid;grid-template-columns:50%% 100px;%s'>"
            "<div id='gp-first' style='grid-column:1;grid-row:1'></div>"
            "<div id='gp-second' style='grid-column:2;grid-row:1'><div style='width:40px;height:10px'></div></div>"
            "</div></div>", grid_modes[i]);
        html_parse(d, page.s, strlen(page.s)); sb_free(&page);
        e = style_engine_new(d); style_recalc(e, &d->node, true);
        l = layout_new(); layout_run(l, d, 800, 600);
        size(d, "gp", 100, 10); size(d, "gp-first", 50, 10);
        offset(d, "gp", "gp-second", 50, 0);
        layout_free(l); style_engine_free(e); doc_free(d);
    }
    for (int gap = 0; gap <= 7; gap += 7) {
        d = doc_new("https://css-stretch.test/intrinsic-grid-contribution");
        SB page; sb_init(&page);
        sb_printf(&page, "<!doctype html><div id='gp' style='display:grid;width:max-content;grid-template-columns:50%% 100px;column-gap:%dpx'>"
            "<div id='gp-first' style='grid-column:1;grid-row:1'><div style='width:40px;height:10px'></div></div>"
            "<div id='gp-second' style='grid-column:2;grid-row:1'></div></div>", gap);
        html_parse(d, page.s, strlen(page.s)); sb_free(&page);
        e = style_engine_new(d); style_recalc(e, &d->node, true);
        l = layout_new(); layout_run(l, d, 800, 600);
        size(d, "gp", 140 + gap, 10); size(d, "gp-first", (140 + gap) / 2.f, 10);
        offset(d, "gp", "gp-second", (140 + gap) / 2.f + gap, 0);
        layout_free(l); style_engine_free(e); doc_free(d);
    }
    d=doc_new("https://css-stretch.test/quirks");
    const char *quirks="<html><body style='margin:0'><div></div><iframe id='quirks-child' style='display:inline-block;width:40px;height:stretch;box-sizing:border-box;border:5px solid'></iframe></body></html>";
    html_parse(d,quirks,strlen(quirks));e=style_engine_new(d);style_recalc(e,&d->node,true);l=layout_new();layout_run(l,d,800,600);
    same("quirks document",d->quirks,1);size(d,"quirks-child",40,600);
    layout_free(l);style_engine_free(e);doc_free(d);
    printf("CSS stretch: %u/%u native checks passed\n",checks-failures,checks);
    return failures?1:0;
}
