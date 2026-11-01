/* Chromium interoperability controls for typed computed sticky margins.
 * The draft-rule7/189 proposal and its reference differences are retained.
 * The original sticky_constraint_geometry322 fixture remains unchanged. */
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void yes(bool value,const char *label) {
    if(!value){fprintf(stderr,"%s\n",label);abort();}checks++;
}
static void number(float actual,float expected,const char *label) {
    if(!isfinite(actual)||fabsf(actual-expected)>.02f)
        fprintf(stderr,"%s: %g expected %g\n",label,actual,expected);
    yes(isfinite(actual)&&fabsf(actual-expected)<=.02f,label);
}
typedef struct {
    const char *name,*css;
    float spacer,normal_x,normal_y,margin_left,margin_right,margin_top,margin_bottom,position_x,position_y;
    bool block;
} Case;
static void run(const Case *c) {
    Document *d=doc_new("https://sticky-margin-box.test/");
    const char *html="<!doctype html><html><body><div id='port'><div id='inner'><span id='spacer'></span><span id='target'></span></div></div></body></html>";
    html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);
    e->media=(MediaCtx){1024,768,1,false};char css[2048];
    snprintf(css,sizeof css,
        "html,body{margin:0;padding:0;border:0}"
        "#port{position:absolute;left:40px;top:30px;width:800px;height:120px;"
        "margin:0;padding:0;border:0;overflow:hidden}"
        "#inner{width:200px;height:100px;margin:0 0 0 100px;padding:0;border:0;"
        "white-space:nowrap;font-size:0;line-height:0}"
        "#spacer{display:%s;vertical-align:top;width:%gpx;height:20px;margin:0;padding:0;border:0}"
        "#target{position:sticky;display:inline-block;vertical-align:top;"
        "width:20px;height:20px;margin:0;padding:0;border:0;"
        "top:auto;bottom:auto;left:auto;right:auto;background:#123456;%s}",
        c->block?"none":"inline-block",(double)c->spacer,c->css);
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
    style_recalc(e,&d->node,false);Layout *l=layout_new();layout_run(l,d,1024,768);
    Node *n=doc_get_element_by_id(d,"target"),*inner=doc_get_element_by_id(d,"inner"),*port_node=doc_get_element_by_id(d,"port");
    assert(n&&n->box&&inner&&inner->box&&port_node&&port_node->box);
    const Box *port=NULL;float px,py,pw,ph;
    yes(box_sticky_constraint_rect(n->box,&port,&px,&py,&pw,&ph)&&port==port_node->box,"actual sticky scroll owner");
    number(px,40,"constraint x");number(py,30,"constraint y");number(pw,800,"constraint width");number(ph,120,"constraint height");
    number(inner->box->x,140,"actual containing-block x");number(inner->box->w,200,"actual containing-block width");
    number(n->box->x,140+c->normal_x,"normal-flow x before sticky translation");number(n->box->y,30+c->normal_y,"normal-flow y");
    number(n->box->m[3],c->margin_left,"actual used leading margin");number(n->box->m[1],c->margin_right,"actual used trailing margin");
    number(n->box->m[0],c->margin_top,"actual used top margin");number(n->box->m[2],c->margin_bottom,"actual used bottom margin");
    number(box_scroll_left(port),0,"actual horizontal scroll");number(box_scroll_top(port),0,"actual vertical scroll");
    float dx,dy;box_sticky_offset(n->box,0,0,&dx,&dy);
    number(dx,c->position_x-c->normal_x,"actual sticky x translation");number(dy,c->position_y-c->normal_y,"actual sticky y translation");
    float r[4];yes(jsg_vrect(n,r,0,0),"actual DOM rectangle exists");
    number(r[0],140+c->position_x,"DOM x");number(r[1],30+c->position_y,"DOM y");number(r[2],20,"DOM width");number(r[3],20,"DOM height");
    Box *hit=layout_hit(l,r[0]+5,r[1]+5,0,0);yes(hit&&hit->node==n,"hit follows sticky position");
    DisplayList dl={0};dl_build(&dl,l,0,0,1024,768);const DItem *found=NULL;
    for(int i=0;i<dl.items.n;i++){DItem *it=&dl.items.v[i];if(it->color==n->style->bg_color&&it->w==20&&it->h==20){found=it;break;}}
    yes(found!=NULL,"actual sticky paint item exists");number(found->x,r[0],"paint x agrees with DOM");number(found->y,r[1],"paint y agrees with DOM");yes(found->fixed,"paint carries actual sticky translation");
    number(n->box->x,140+c->normal_x,"normal-flow x retained after consumers");number(n->box->y,30+c->normal_y,"normal-flow y retained after consumers");
    dl_clear(&dl);vec_free(dl.items);layout_free(l);style_engine_free(e);doc_free(d);
}
/* Isolate the new computed-margin basis with a definite vertical containing
   block. This does not assert completeness of vertical normal-flow layout. */
static void vertical_inline_basis(void) {
    Document *d=doc_new("https://sticky-vertical-margin.test/");
    const char *html="<!doctype html><html><body><div id='port'><div id='inner'><div id='target'></div></div></div></body></html>";
    html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);
    e->media=(MediaCtx){1024,768,1,false};
    const char *css="html,body{margin:0;padding:0;border:0}"
        "#port{position:absolute;left:40px;top:30px;width:800px;height:120px;margin:0;padding:0;border:0;overflow:hidden}"
        "#inner{width:200px;height:100px;margin:0 0 0 100px;padding:0;border:0;writing-mode:vertical-lr}"
        "#target{position:sticky;display:block;width:20px;height:20px;margin:0 20% 0 10%;padding:0;border:0;left:280px;background:#234567}";
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
    style_recalc(e,&d->node,false);Layout *l=layout_new();layout_run(l,d,1024,768);
    Node *n=doc_get_element_by_id(d,"target"),*inner=doc_get_element_by_id(d,"inner"),*port=doc_get_element_by_id(d,"port");
    assert(n&&n->box&&inner&&inner->box&&port&&port->box);
    yes(inner->style->writing_mode==1,"actual vertical-lr containing block");
    number(inner->box->x,140,"vertical CB physical x");number(inner->box->w,200,"vertical CB physical width");
    number(inner->box->h,100,"vertical CB inline size is its physical height");number(port->box->w,800,"vertical control distinct scrollport width");
    const float x=n->box->x,y=n->box->y;
    yes(x<300&&x+20<=inner->box->x+inner->box->w,"normal position permits decisive positive sticky clamp");
    float dx,dy;box_sticky_offset(n->box,0,0,&dx,&dy);
    number(x+dx,300,"vertical CB margin20% resolves to20 at right clamp");number(dy,0,"vertical control does not constrain y");
    float r[4];yes(jsg_vrect(n,r,0,0),"vertical actual DOM rectangle exists");
    number(r[0],300,"vertical actual DOM x");number(r[1],y,"vertical actual DOM y");number(r[2],20,"vertical DOM width");number(r[3],20,"vertical DOM height");
    Box *hit=layout_hit(l,r[0]+5,r[1]+5,0,0);yes(hit&&hit->node==n,"vertical hit follows sticky clamp");
    DisplayList dl={0};dl_build(&dl,l,0,0,1024,768);const DItem *found=NULL;
    for(int i=0;i<dl.items.n;i++){DItem *it=&dl.items.v[i];if(it->color==n->style->bg_color&&it->w==20&&it->h==20){found=it;break;}}
    yes(found!=NULL,"vertical paint item exists");number(found->x,r[0],"vertical paint x agrees with DOM");
    number(found->y,r[1],"vertical paint y agrees with DOM");yes(found->fixed,"vertical paint carries sticky translation");
    number(n->box->x,x,"vertical normal-flow x unchanged");number(n->box->y,y,"vertical normal-flow y unchanged");
    dl_clear(&dl);vec_free(dl.items);layout_free(l);style_engine_free(e);doc_free(d);
}
int main(void) {
    dom_init();
    const Case cases[]={
        {"positive explicit trailing margin","margin-left:20px;margin-right:40px;left:280px",110,130,0,20,40,0,0,140,0,false},
        {"positive explicit leading margin","margin-left:40px;margin-right:0;right:680px",10,50,0,40,0,0,0,40,0,false},
        {"negative trailing margin","margin-left:20px;margin-right:-20px;left:350px",110,130,0,20,-20,0,0,200,0,false},
        {"negative leading margin","margin-left:-20px;margin-right:0;right:750px",110,90,0,-20,0,0,0,-20,0,false},
        {"oversized explicit leading margin","margin-left:500px;margin-right:0;right:680px",10,510,0,500,0,0,0,500,0,false},
        {"auto margins positive travel","display:block;margin-left:auto;margin-right:auto;left:280px",0,90,0,90,90,0,0,180,0,true},
        {"auto margins negative travel","display:block;margin-left:auto;margin-right:auto;right:730px",0,90,0,90,90,0,0,0,0,true},
        {"percent margins use containing-block width","margin-left:10%;margin-right:20%;left:280px",110,130,0,20,40,0,0,140,0,false},
        {"typed calc margins use containing-block width","margin-left:calc(5% + 10px);margin-right:min(20%, 50px);left:280px",110,130,0,20,40,0,0,140,0,false},
        {"vertical percent margins still use containing-block width","margin-top:10%;margin-bottom:20%;top:80px",0,0,20,0,0,20,40,0,40,false}
    };
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){fprintf(stderr,"STICKY_MARGIN_CASE %s\n",cases[i].name);run(&cases[i]);}
    assert(checks==290);vertical_inline_basis();assert(checks==310);printf("PASS sticky_computed_margin %u native checks\n",checks);
}
