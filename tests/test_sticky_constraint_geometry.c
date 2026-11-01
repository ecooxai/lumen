/* Native Chromium interoperability contract for sticky content constraints.
 * Geometry, paint, hit testing and CSSOM share actual scroll-container bounds.
 * This does not claim to implement the draft's different padding-box wording. */
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Document *doc; StyleEngine *engine; Layout *layout; } Fixture;
static unsigned checks;
static void yes(bool condition, const char *label) {
    if (!condition) { fprintf(stderr, "%s\n", label); abort(); }
    checks++;
}
static void number(float actual, float expected, const char *label) {
    if (!isfinite(actual) || fabsf(actual - expected) > .02f)
        fprintf(stderr, "%s: %g, expected %g\n", label, actual, expected);
    yes(isfinite(actual) && fabsf(actual - expected) <= .02f, label);
}
static Node *node(Fixture *f, const char *id) {
    Node *n = doc_get_element_by_id(f->doc, id); assert(n); return n;
}
static void refresh(Fixture *f, float width, float height) {
    f->engine->media.vw = width; f->engine->media.vh = height;
    style_recalc(f->engine, &f->doc->node, false);
    layout_run(f->layout, f->doc, width, height);
}
static void set_style(Fixture *f, const char *id, const char *style) {
    node_set_attr(node(f,id), "style", style); refresh(f,640,480);
}
static Fixture fixture(int padding) {
    Fixture f = {0}; f.doc = doc_new("https://sticky-constraint.test/");
    const char *html = "<!doctype html><html><body><div id='s'><div id='clip'><div id='near'>"
        "<div id='inner'><div id='spacer'></div><div id='target'></div></div>"
        "</div></div><div id='overflow'></div></div></body></html>";
    html_parse(f.doc, html, strlen(html)); f.engine = style_engine_new(f.doc);
    f.engine->media = (MediaCtx){640,480,1,false};
    char css[2048]; snprintf(css,sizeof css,
        "html,body{margin:0;padding:0;border:0}"
        "#s{position:absolute;left:40px;top:30px;width:300px;height:200px;"
        "border:5px solid black;padding:%dpx;overflow:hidden}"
        "#clip,#near,#inner{width:600px;height:600px;margin:0;padding:0;border:0}"
        "#clip{overflow:clip}#spacer{height:80px}#overflow{display:none}"
        "#target{position:sticky;top:10%%;left:10%%;width:20px;height:20px;"
        "margin:0;padding:0;border:0;background:#112233}",padding);
    style_engine_add_sheet(f.engine, css_parse_sheet(css,strlen(css),f.doc->url,1,&f.engine->media));
    f.layout = layout_new(); refresh(&f,640,480); return f;
}
static void finish(Fixture *f) { layout_free(f->layout); style_engine_free(f->engine); doc_free(f->doc); }
static void text(Node *n, const char *prop, const char *expected, bool animation) {
    char *actual = animation ? css_get_animation_value(n,prop) : css_get_computed_value(n,prop);
    if (!actual || strcmp(actual,expected)) fprintf(stderr,"%s: %s, expected %s\n",prop,actual?actual:"null",expected);
    yes(actual && !strcmp(actual,expected),prop); free(actual);
}
static void constraint(Fixture *f, const char *owner, float x, float y, float w, float h) {
    const Box *port = NULL; float ax,ay,aw,ah;
    bool valid = box_sticky_constraint_rect(node(f,"target")->box,&port,&ax,&ay,&aw,&ah);
    yes(valid && port == (owner ? node(f,owner)->box : NULL),"actual nearest sticky scroll owner");
    number(ax,x,"constraint x"); number(ay,y,"constraint y");
    number(aw,w,"constraint width"); number(ah,h,"constraint height");
}
static void geometry(Fixture *f, float x, float y, float page_x, float page_y) {
    Node *n=node(f,"target"); float r[4];
    yes(jsg_vrect(n,r,page_x,page_y),"real DOM rectangle exists");
    number(r[0],x,"DOM x"); number(r[1],y,"DOM y");
    number(r[2],20,"DOM width"); number(r[3],20,"DOM height");
    Box *hit=layout_hit(f->layout,r[0]+5+page_x,r[1]+5+page_y,page_x,page_y);
    yes(hit && hit->node==n,"hit testing follows actual sticky position");
    DisplayList dl={0}; dl_build(&dl,f->layout,page_x,page_y,f->layout->vw,f->layout->vh);
    const DItem *found=NULL;
    for(int i=0;i<dl.items.n;i++) {
        DItem *it=&dl.items.v[i];
        if(it->color==n->style->bg_color && it->w==20 && it->h==20) { found=it; break; }
    }
    yes(found!=NULL,"sticky paint item exists");
    number(found->x,x,"paint x"); number(found->y,y,"paint y");
    yes(found->fixed,"paint carries applied sticky translation");
    dl_clear(&dl); vec_free(dl.items);
}
static void snapshot(Fixture *f, const char *top, const char *left,
                     const char *animation_top, const char *animation_left,
                     float x,float y,float layout_x,float layout_y,float page_x,float page_y) {
    Node *n=node(f,"target");
    text(n,"top",top,false); text(n,"left",left,false);
    text(n,"bottom","auto",false); text(n,"right","auto",false);
    text(n,"top",animation_top,true); text(n,"left",animation_left,true);
    geometry(f,x,y,page_x,page_y);
    number(n->box->x,layout_x,"normal-flow x is unchanged by sticky scrolling");
    number(n->box->y,layout_y,"normal-flow y is unchanged by sticky scrolling");
}
int main(void) {
    dom_init();
    for(int padding=0;padding<=10;padding+=10) {
        Fixture f=fixture(padding);
        constraint(&f,"s",45+padding,35+padding,300,200);
        const float scrolls[]={0,100,150};
        for(unsigned i=0;i<sizeof scrolls/sizeof scrolls[0];i++) {
            jsg_set_scroll(node(&f,"s"),scrolls[i],scrolls[i]);
            snapshot(&f,"20px","30px","10%","10%",75+padding,
                fmaxf(115+padding-scrolls[i],55+padding),45+padding,115+padding,0,0);
        }
        set_style(&f,"s","width:400px;height:300px");
        constraint(&f,"s",45+padding,35+padding,400,300);
        snapshot(&f,"30px","40px","10%","10%",85+padding,65+padding,45+padding,115+padding,0,0);
        finish(&f);
    }
    assert(checks==164);
    Fixture f=fixture(10);
    set_style(&f,"near","overflow:hidden;width:180px;height:120px;padding:6px;border:4px solid transparent");
    jsg_set_scroll(node(&f,"near"),100,100);
    constraint(&f,"near",65,55,180,120);
    snapshot(&f,"12px","18px","10%","10%",83,67,65,135,0,0);
    set_style(&f,"near","overflow:visible;width:600px;height:600px");
    set_style(&f,"clip","overflow:hidden;width:240px;height:160px;padding:8px;border:2px solid transparent");
    jsg_set_scroll(node(&f,"clip"),100,100);
    constraint(&f,"clip",65,55,240,160);
    snapshot(&f,"16px","24px","10%","10%",89,71,65,135,0,0);
    set_style(&f,"clip","overflow:clip;width:240px;height:160px;padding:8px;border:2px solid transparent");
    /* The clipped box fits the outer port. Independent normal-flow content
       supplies actual scroll range without moving the earlier sticky target. */
    set_style(&f,"overflow","display:block;width:600px;height:600px");
    jsg_set_scroll(node(&f,"s"),100,100);
    number(box_scroll_left(node(&f,"s")->box),100,"outer horizontal scroll actually applied");
    number(box_scroll_top(node(&f,"s")->box),100,"outer vertical scroll actually applied");
    constraint(&f,"s",55,45,300,200);
    snapshot(&f,"20px","30px","10%","10%",85,65,65,135,0,0);
    finish(&f); assert(checks==235);
    f=fixture(10);
    set_style(&f,"s","position:fixed;overflow:visible");
    constraint(&f,NULL,0,0,640,480);
    snapshot(&f,"48px","64px","10%","10%",64,125,55,125,0,0);
    snapshot(&f,"48px","64px","10%","10%",64,125,55,125,100,100);
    refresh(&f,800,600);
    constraint(&f,NULL,0,0,800,600);
    snapshot(&f,"60px","80px","10%","10%",80,125,55,125,100,100);
    set_style(&f,"target","display:none");
    yes(node(&f,"target")->box==NULL,"display:none has no fabricated box");
    text(node(&f,"target"),"top","10%",false); text(node(&f,"target"),"left","10%",false);
    text(node(&f,"target"),"top","10%",true); text(node(&f,"target"),"left","10%",true);
    finish(&f); assert(checks==304);
    f=fixture(10);
    set_style(&f,"target","top:calc(10% + 2px);left:min(10%, 40px)");
    jsg_set_scroll(node(&f,"s"),100,100);
    snapshot(&f,"22px","30px","calc(10% + 2px)","min(10%, 40px)",85,67,55,125,0,0);
    finish(&f); assert(checks==322);
    printf("PASS sticky_constraint_geometry %u native checks\n",checks);
}
