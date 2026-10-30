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
    node_set_attr(doc_get_element_by_id(d,"parent"),"style","width:180px;height:140px;border:1px solid");
    style_recalc(e,&d->node,true);layout_run(l,d,800,1600);size(d,"normal",140,120);
    layout_free(l);style_engine_free(e);doc_free(d);
    printf("CSS stretch: %u/%u native checks passed\n",checks-failures,checks);
    return failures?1:0;
}
