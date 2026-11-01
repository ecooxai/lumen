#include "../src/layout/layout.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void yes(bool value,const char *label){if(!value)fprintf(stderr,"atomic-nowrap failed: %s\n",label);assert(value);checks++;}
static void number(float actual,float expected,const char *label){if(!isfinite(actual)||fabsf(actual-expected)>.02f)fprintf(stderr,"%s=%g expected%g\n",label,actual,expected);yes(isfinite(actual)&&fabsf(actual-expected)<=.02f,label);}
typedef struct{const char *name,*ws,*child,*nest;bool image,br,wrap;} Case;
static void run(const Case *c){
 Document *d=doc_new("https://atomic-nowrap.test/");char html[1800],first[200],target[300],children[1000];
 snprintf(first,sizeof first,"<span id='first'></span>");snprintf(target,sizeof target,"<%s id='target'%s>",c->image?"img":"span",c->image?"":"");if(!c->image)strcat(target,"</span>");
 if(c->nest&&str_starts(c->nest,"together-"))snprintf(children,sizeof children,"<span style='white-space:%s'>%s%s</span>",c->nest+9,first,target);
 else if(c->nest&&!strcmp(c->nest,"target-normal"))snprintf(children,sizeof children,"%s<span style='white-space:normal'>%s</span>",first,target);
 else if(c->nest&&!strcmp(c->nest,"separate-nowrap"))snprintf(children,sizeof children,"<span style='white-space:nowrap'>%s</span><span style='white-space:nowrap'>%s</span>",first,target);
 else snprintf(children,sizeof children,"%s%s%s",first,c->br?"<br>":"",target);
 snprintf(html,sizeof html,"<!doctype html><html><body><div id='box'>%s</div></body></html>",children);html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);char css[1500];
 snprintf(css,sizeof css,"html,body{margin:0}#box{position:absolute;left:40px;top:30px;width:50px;margin:0;padding:0;border:0;font-size:0;line-height:0;white-space:%s}#first{display:inline-block;vertical-align:top;width:30px;height:20px;margin:0;padding:0;border:0}#target{display:inline-block;vertical-align:top;width:40px;height:20px;margin:0 0 0 10px;padding:0;border:0;background:#123456;%s%s}",c->ws,c->child?"white-space:":"",c->child?c->child:"");
 style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));style_recalc(e,&d->node,true);Layout *l=layout_new();layout_run(l,d,640,480);
 Node *box=doc_get_element_by_id(d,"box"),*a=doc_get_element_by_id(d,"first"),*b=doc_get_element_by_id(d,"target");assert(box&&box->box&&a&&a->box&&b&&b->box);
 yes(box->box->fmt==FMT_INLINE,"actual inline formatting context");yes(box->box->nlines==(c->wrap?2:1),"actual native line count");
 number(a->box->x,40,"first x");number(a->box->y,30,"first y");number(b->box->x,c->wrap?50:80,"target x");number(b->box->y,c->wrap?50:30,"target y");number(b->box->w,40,"target width");number(b->box->h,20,"target height");number(box->box->h,c->wrap?40:20,"actual block height");yes(b->box->kind==BX_ATOMIC,"actual atomic box");
 float r[4];yes(jsg_vrect(b,r,0,0),"actual DOM rect");number(r[0],b->box->x,"DOM x follows layout");number(r[1],b->box->y,"DOM y follows layout");Box *hit=layout_hit(l,r[0]+5,r[1]+5,0,0);yes(hit&&hit->node==b,"hit follows actual atomic position");
 layout_free(l);style_engine_free(e);doc_free(d);
}
static void space_boundary(const char *mode,bool wrap){
 Document *d=doc_new("https://atomic-space-boundary.test/");char html[512];snprintf(html,sizeof html,"<!doctype html><html><body><div id='box'><span id='text' style='white-space:%s'>X </span><span id='target'></span></div></body></html>",mode);html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);
 const char *css="html,body{margin:0}#box{position:absolute;left:40px;top:30px;width:20px;margin:0;padding:0;border:0;font:10px monospace;line-height:20px;white-space:nowrap}#target{display:inline-block;vertical-align:top;width:40px;height:20px;margin-left:10px;background:#123456}";
 style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));style_recalc(e,&d->node,true);Layout *l=layout_new();layout_run(l,d,640,480);Node *box=doc_get_element_by_id(d,"box"),*a=doc_get_element_by_id(d,"text"),*b=doc_get_element_by_id(d,"target");assert(box&&a&&b&&box->box&&a->box&&b->box);
 yes(box->box->nlines==(wrap?2:1),"space opportunity line count");number(b->box->x,wrap?50:a->box->x+a->box->w+10,"space opportunity target x");number(b->box->y,wrap?50:30,"space opportunity target y");number(box->box->h,wrap?40:20,"space opportunity block height");number(b->box->w,40,"space target width");number(b->box->h,20,"space target height");float r[4];yes(jsg_vrect(b,r,0,0),"space boundary DOM rect");number(r[0],b->box->x,"space boundary DOM x");number(r[1],wrap?50:30,"space boundary DOM y");Box *hit=layout_hit(l,r[0]+5,r[1]+5,0,0);yes(hit&&hit->node==b,"space boundary actual hit");layout_free(l);style_engine_free(e);doc_free(d);
}
int main(void){
 dom_init();font_init();
 const Case cases[]={
  {"normal","normal",NULL,NULL,false,false,true},{"nowrap","nowrap",NULL,NULL,false,false,false},{"pre","pre",NULL,NULL,false,false,false},{"pre-wrap","pre-wrap",NULL,NULL,false,false,true},
  {"normal internal whitespace","nowrap","normal",NULL,false,false,false},{"nowrap internal whitespace","normal","nowrap",NULL,false,false,true},
  {"nested nowrap","normal",NULL,"together-nowrap",false,false,false},{"nested normal","nowrap",NULL,"together-normal",false,false,true},
  {"outer nowrap","nowrap",NULL,"target-normal",false,false,false},{"outer normal","normal",NULL,"separate-nowrap",false,false,true},
  {"replaced nowrap","nowrap",NULL,NULL,true,false,false},{"replaced normal","normal",NULL,NULL,true,false,true},{"forced BR","nowrap",NULL,NULL,false,true,true}
 };
 for(unsigned i=0;i<sizeof cases/sizeof*cases;i++){fprintf(stderr,"ATOMIC_WRAP_CASE %s\n",cases[i].name);run(&cases[i]);}assert(checks==182);
 Document *d=doc_new("https://atomic-text-boundary.test/");const char *html="<!doctype html><html><body><div id='box'><span id='text' style='white-space:normal'>X</span><span id='target'></span></div></body></html>";html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);
 const char *css="html,body{margin:0}#box{position:absolute;left:40px;top:30px;width:20px;margin:0;padding:0;border:0;font:10px monospace;line-height:20px;white-space:nowrap}#target{display:inline-block;vertical-align:top;width:40px;height:20px;margin-left:10px;background:#123456}";
 style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));style_recalc(e,&d->node,true);Layout *l=layout_new();layout_run(l,d,640,480);Node *box=doc_get_element_by_id(d,"box"),*a=doc_get_element_by_id(d,"text"),*b=doc_get_element_by_id(d,"target");assert(box&&a&&b&&box->box&&a->box&&b->box);
 yes(box->box->nlines==1,"text boundary preserves one line");number(b->box->y,30,"text-to-atomic y");number(box->box->h,20,"text line height");number(b->box->x,a->box->x+a->box->w+10,"atomic follows actual shaped advance");
 float r[4];yes(jsg_vrect(b,r,0,0),"text boundary DOM rect");number(r[0],b->box->x,"text boundary DOM x");number(r[1],30,"text boundary DOM y");Box *hit=layout_hit(l,r[0]+5,r[1]+5,0,0);yes(hit&&hit->node==b,"text boundary hit");assert(checks==190);
 const char *modes[]={"normal","nowrap","pre"};
 for(unsigned i=0;i<3;i++){char style[128];snprintf(style,sizeof style,"white-space:%s",modes[i]);node_set_attr(box,"style",style);style_recalc(e,&d->node,false);layout_run(l,d,640,480);bool wrap=i==0;yes(box->box->nlines==(wrap?2:1),"live whitespace mutation updates lines");number(b->box->x,wrap?50:a->box->x+a->box->w+10,"live mutation target x");number(b->box->y,wrap?50:30,"live mutation target y");number(box->box->h,wrap?40:20,"live mutation block height");}
 assert(checks==202);layout_free(l);style_engine_free(e);doc_free(d);space_boundary("normal",true);space_boundary("nowrap",false);assert(checks==222);printf("PASS atomic_nowrap %u native checks\n",checks);return 0;
}
