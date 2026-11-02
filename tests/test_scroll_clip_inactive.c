/* A clipping element keeps its clipping box and stored offsets, but neither
   geometry nor programmatic scrolling may use them while both axes are
   non-scrollable. Existing computed overflow normalization is preserved. */
#include "../src/layout/layout.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void yes(bool value,const char *label){if(!value){fprintf(stderr,"%s\n",label);abort();}checks++;}
static void number(float actual,float expected,const char *label){if(!isfinite(actual)||fabsf(actual-expected)>.02f)fprintf(stderr,"%s: %g expected %g\n",label,actual,expected);yes(isfinite(actual)&&fabsf(actual-expected)<=.02f,label);}
typedef struct{Document *d;StyleEngine *e;Layout *l;Node *port,*target;}Fixture;
static void refresh(Fixture *f){style_recalc(f->e,&f->d->node,false);layout_run(f->l,f->d,640,480);}
static void snapshot(Fixture *f,bool active){
    yes(box_scroll_active(f->port->box)==active,"actual scroll-container activity");
    float scroll[4];yes(jsg_scroll(f->port,scroll),"clipping owner retains native scroll metrics");
    number(scroll[0],active?100:0,"DOM horizontal offset");number(scroll[1],active?60:0,"DOM vertical offset");
    float r[4];yes(jsg_vrect(f->target,r,0,0),"actual descendant rectangle exists");
    number(r[0],active?60:160,"actual descendant x");number(r[1],active?50:110,"actual descendant y");
    number(r[2],20,"actual descendant width");number(r[3],20,"actual descendant height");
    Box *hit=layout_hit(f->l,r[0]+5,r[1]+5,0,0);yes(hit&&hit->node==f->target,"hit uses effective offsets");
    number(f->target->box->x,160,"normal layout x unchanged");number(f->target->box->y,110,"normal layout y unchanged");
    number(f->port->scroll_x,100,"stored horizontal offset retained");number(f->port->scroll_y,60,"stored vertical offset retained");
}
static void run(const char *inactive){
    Fixture f={0};f.d=doc_new("https://scroll-clip.test/");
    const char *html="<!doctype html><html><body><div id='port'><div id='inner'><div id='target'></div></div></div></body></html>";
    html_parse(f.d,html,strlen(html));f.e=style_engine_new(f.d);f.e->media=(MediaCtx){640,480,1,false};
    const char *css="html,body{margin:0;padding:0;border:0}#port{position:absolute;left:40px;top:30px;width:200px;height:120px;overflow:hidden;margin:0;padding:0;border:0}#inner{position:relative;width:600px;height:400px;margin:0;padding:0;border:0}#target{position:absolute;left:120px;top:80px;width:20px;height:20px;background:#456789;margin:0;padding:0;border:0}";
    style_engine_add_sheet(f.e,css_parse_sheet(css,strlen(css),f.d->url,1,&f.e->media));f.l=layout_new();refresh(&f);
    f.port=doc_get_element_by_id(f.d,"port");f.target=doc_get_element_by_id(f.d,"target");assert(f.port&&f.target&&f.port->box&&f.target->box);
    yes(jsg_set_scroll(f.port,100,60),"hidden permits programmatic scrolling");snapshot(&f,true);
    node_set_attr(f.port,"style",inactive);refresh(&f);snapshot(&f,false);
    yes(!jsg_set_scroll(f.port,33,44),"inactive clip setter does not mutate stored offset");snapshot(&f,false);
    node_set_attr(f.port,"style","overflow:hidden");refresh(&f);snapshot(&f,true);
    layout_free(f.l);style_engine_free(f.e);doc_free(f.d);
}
int main(void){dom_init();run("overflow:clip");run("overflow-x:clip;overflow-y:visible");run("overflow-x:visible;overflow-y:clip");assert(checks==174);printf("PASS scroll_clip_inactive %u native checks\n",checks);}
