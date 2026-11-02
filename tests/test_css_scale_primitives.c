/* Real primitive scale text and shared affine raster/ink/damage behavior.
 * The original scale_text test remains byte-identical and independently runs. */
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include "affine_ink_font.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W=640,H=480 };
static const uint32_t white=0xffffffffu;
static unsigned checks;
static const char *case_name;
static void yes(bool value,const char *label) {
    checks++;if(!value){fprintf(stderr,"%s: %s\n",case_name,label);abort();}
}
static void near(float value,float expected,const char *label) {
    checks++;if(!isfinite(value)||fabsf(value-expected)>.025f){fprintf(stderr,"%s: %s %g expected %g\n",case_name,label,value,expected);abort();}
}
typedef struct {Document *doc;StyleEngine *engine;Layout *layout;Node *target;DisplayList list;Canvas canvas;float scroll;} Fixture;
static Fixture make(const char *style,float scroll) {
    Fixture f={0};f.doc=doc_new("https://scale-primitives.test/");
    char html[6000];size_t used=(size_t)snprintf(html,sizeof html,"<!doctype html><html><body><div id=target>A</div>");
    for(int i=0;i<70;i++)used+=(size_t)snprintf(html+used,sizeof html-used,"<i class=pad style='left:%dpx'></i>",i+1);
    snprintf(html+used,sizeof html-used,"</body></html>");html_parse(f.doc,html,strlen(html));f.engine=style_engine_new(f.doc);
    f.engine->media=(MediaCtx){W,H,1,false};
    const char *css="html,body{margin:0;padding:0}#target{position:absolute;left:100px;top:100px;width:80px;height:40px;"
        "font:20px/20px LumenAffineInk;color:#102060;transform-origin:0 0}"
        ".pad{position:absolute;top:450px;width:1px;height:1px;background:white}";
    style_engine_add_sheet(f.engine,css_parse_sheet(css,strlen(css),f.doc->url,1,&f.engine->media));
    f.target=doc_get_element_by_id(f.doc,"target");node_set_attr(f.target,"style",style);
    f.layout=layout_new();style_recalc(f.engine,&f.doc->node,false);layout_run(f.layout,f.doc,W,H);
    f.scroll=scroll;dl_build(&f.list,f.layout,0,scroll,W,H);canvas_init(&f.canvas,W,H,1);raster(&f.canvas,&f.list,white);return f;
}
static void body_style(Fixture *f,const char *style) {
    node_set_attr(f->doc->body,"style",style);style_recalc(f->engine,&f->doc->node,false);layout_run(f->layout,f->doc,W,H);
    dl_build(&f->list,f->layout,0,f->scroll,W,H);raster(&f->canvas,&f->list,white);
}
static void destroy(Fixture *f) {canvas_free(&f->canvas);dl_clear(&f->list);free(f->list.items.v);layout_free(f->layout);style_engine_free(f->engine);doc_free(f->doc);}
static const DItem *item(const Fixture *f,int op) {for(int i=0;i<f->list.items.n;i++)if(f->list.items.v[i].op==op)return &f->list.items.v[i];return NULL;}
static void rectangle(Fixture *f,float x,float y,float w,float h) {
    float r[4];yes(jsg_vrect(f->target,r,0,f->scroll),"real corner union available");
    near(r[0],x,"viewport x");near(r[1],y,"viewport y");near(r[2],w,"viewport width");near(r[3],h,"viewport height");
}
static bool pixels_equal(const Canvas *a,const Canvas *b) {
    if(a->w!=b->w||a->h!=b->h)return false;
    for(int y=0;y<a->h;y++)for(int x=0;x<a->w;x++)if(canvas_row(a,y)[x]!=canvas_row(b,y)[x])return false;
    return true;
}
static uint32_t sample_pixel(const Canvas *c,int x,int y) {return x<0||y<0||x>=c->w||y>=c->h?white:canvas_row(c,y)[x];}
/* Slow independent bilinear image oracle over opaque white. This uses the
 * literal CSS matrix of each case, never the production matrix/ink helpers.
 * Ignore the distant white padding primitives used to exercise item culling. */
static void compare_scaled(const Canvas *plain,const Canvas *actual,double sx,double sy,double tx,double ty) {
    bool same=true;
    for(int y=0;same&&y<H;y++)for(int x=0;x<W;x++) {
        double px=((x+.5)-tx)/sx-.5,py=((y+.5)-ty)/sy-.5;
        int ix=(int)floor(px),iy=(int)floor(py);double ax=px-ix,ay=py-iy;
        uint32_t p[4]={sample_pixel(plain,ix,iy),sample_pixel(plain,ix+1,iy),sample_pixel(plain,ix,iy+1),sample_pixel(plain,ix+1,iy+1)};
        uint32_t got=canvas_row(actual,y)[x];
        if((got>>24)!=255){same=false;break;}
        for(int shift=0;shift<=16;shift+=8) {
            double v0=((p[0]>>shift)&255)*(1-ax)+((p[1]>>shift)&255)*ax;
            double v1=((p[2]>>shift)&255)*(1-ax)+((p[3]>>shift)&255)*ax;
            int expected=(int)lround(v0*(1-ay)+v1*ay),value=(got>>shift)&255;
            if(abs(value-expected)>2){fprintf(stderr,"%s pixel(%d,%d) %08x expected channel %d\n",case_name,x,y,got,expected);same=false;break;}
        }
        if(!same)break;
    }
    yes(same,"actual scaled glyph pixels agree with independent inverse sampling");
}
static void damage(Fixture *f) {
    Canvas partial;canvas_init(&partial,W,H,1);
    for(int i=0;i<W*H;i++)partial.px[i]=0x11223344u;
    raster_rect(&partial,&f->list,white,120,90,165,150);
    bool same=true;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(partial.px[y*W+x]!=(x>=120&&x<165&&y>=90&&y<150?f->canvas.px[y*W+x]:0x11223344u))same=false;
    yes(same,"partial damage equals full paint and leaves every other pixel untouched");
    raster_set_threads(2);raster(&partial,&f->list,white);raster_set_threads(0);
    yes(pixels_equal(&partial,&f->canvas),"parallel bands match original full pixels");canvas_free(&partial);
    Canvas a,b;canvas_init(&a,800,600,1.25f);canvas_init(&b,800,600,1.25f);
    dl_prepare_ink(&f->list,1);raster(&a,&f->list,white);
    for(int y=0;y<600;y+=75)for(int x=0;x<800;x+=100)raster_rect(&b,&f->list,white,x,y,x+100,y+75);
    yes(pixels_equal(&a,&b),"fractional raster scale and tiled damage share actual ink");canvas_free(&a);canvas_free(&b);
}
static bool contains_ink(const Canvas *canvas,const float bounds[4]) {
    for(int y=0;y<canvas->h;y++)for(int x=0;x<canvas->w;x++)if(canvas_row(canvas,y)[x]!=white&&
        (x+.5<bounds[0]*canvas->scale||x+.5>bounds[2]*canvas->scale||y+.5<bounds[1]*canvas->scale||y+.5>bounds[3]*canvas->scale))return false;
    return true;
}
static void generated_damage(void) {
    case_name="application damage bounds include resized far-bearing ink";
    Fixture old=make("width:10px;transform:scale(2)",0),next=make("width:10px;transform:scale(2);color:#20a040",0);
    const DItem *a=item(&old,DO_TEXT),*b=item(&next,DO_TEXT);
    yes(a&&b&&a->precise_text_ink&&!a->transform&&a->g[0].font->size==40,"actual resized primitive requests exact cached damage ink");
    int left=W;unsigned count=0;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(old.canvas.px[y*W+x]!=white){left=LMIN(left,x);count++;}
    yes(a->g[0].x+2*a->g[0].font->size+2<left,"old font-size estimate would miss the actual far-bearing glyph");
    float before[4],after[4];paint_item_damage_bounds(a,before);paint_item_damage_bounds(b,after);
    yes(contains_ink(&old.canvas,before),"actual app bounds contain every old glyph pixel");
    yes(count>=500&&left>=220,"solid scaled glyph paints beyond its narrow layout box");
    yes(b&&b->precise_text_ink&&contains_ink(&next.canvas,after),"actual app bounds contain every new glyph pixel");
    Canvas update;canvas_init(&update,W,H,1);memcpy(update.px,old.canvas.px,(size_t)W*H*4);
    raster_rect(&update,&next.list,white,(int)floorf(LMIN(before[0],after[0])),(int)floorf(LMIN(before[1],after[1])),(int)ceilf(LMAX(before[2],after[2])),(int)ceilf(LMAX(before[3],after[3])));
    yes(pixels_equal(&update,&next.canvas),"app-generated damage repaints every changed far-bearing pixel");canvas_free(&update);
    Canvas old_fractional,new_fractional;canvas_init(&old_fractional,800,600,1.25f);canvas_init(&new_fractional,800,600,1.25f);
    dl_prepare_ink(&old.list,1.25f);dl_prepare_ink(&next.list,1.25f);
    raster(&old_fractional,&old.list,white);raster(&new_fractional,&next.list,white);
    paint_item_damage_bounds(a,before);paint_item_damage_bounds(b,after);
    yes(contains_ink(&old_fractional,before)&&contains_ink(&new_fractional,after),"app bounds refresh exact glyph ink at the real fractional canvas scale");
    raster_rect(&old_fractional,&next.list,white,(int)floorf(LMIN(before[0],after[0])*1.25f),(int)floorf(LMIN(before[1],after[1])*1.25f),(int)ceilf(LMAX(before[2],after[2])*1.25f),(int)ceilf(LMAX(before[3],after[3])*1.25f));
    yes(pixels_equal(&old_fractional,&new_fractional),"fractional app-generated damage repaints the complete changed glyph");
    canvas_free(&old_fractional);canvas_free(&new_fractional);destroy(&old);destroy(&next);
}

int main(void) {
    dom_init();font_init();case_name="original deterministic glyph";
    const char *url="https://scale-primitives.test/original.ttf";font_declare("LumenAffineInk",400,false,url);
    yes(font_loaded(url,affine_ink_font,sizeof affine_ink_font),"original font loads");
    Font *font=font_get("LumenAffineInk",400,false,20);ShapedRun run={0};text_shape(font,"A",1,0,&run);
    yes(run.n==1,"real one-glyph shaping");const Glyph *glyph=font_glyph(run.g[0].font,run.g[0].gid,1);
    yes(glyph&&glyph->left==60&&glyph->w==10&&glyph->h==14,"independent solid glyph metrics");shaped_free(&run);

    case_name="uniform scale preserves real top-level text and resizes its face";
    Fixture scaled=make("transform:scale(2);background:#e0e0e0",0);
    Fixture large=make("font-size:40px;line-height:40px;width:160px;height:80px;background:#e0e0e0",0);
    const DItem *t=item(&scaled,DO_TEXT),*rect=item(&scaled,DO_RECT);
    yes(t&&rect&&t->ng==1&&!t->transform&&!item(&scaled,DO_TRANSFORM),"simple uniform group remains real primitives");
    near(t->g[0].font->size,40,"uniform glyph face is actually resized");near(rect->x,100,"uniform rectangle origin");near(rect->w,160,"uniform rectangle width");
    rectangle(&scaled,100,100,160,80);
    yes(pixels_equal(&scaled.canvas,&large.canvas),"uniform scale equals independently laid out double-size solid glyph and box");
    Box *hit=layout_hit(scaled.layout,110,110,0,0);yes(hit&&box_within(hit,scaled.target->box),"actual uniform geometry hit");
    yes(scaled.list.items.n>=64,"real extra primitives exercise raster item culling");
    destroy(&large);destroy(&scaled);

    case_name="nonuniform text is one real primitive with an affine source";
    Fixture plain=make("",0),squeezed=make("transform:scale(.5,1.5)",0);
    t=item(&squeezed,DO_TEXT);
    yes(t&&t->ng==1&&t->transform&&t->transform->count==1&&t->transform->items[0].op==DO_TEXT&&!t->transform->items[0].transform,"one actual source command and no facade duplicate");
    near(t->g[0].font->size,20,"nonuniform source retains its original face");
    yes(!item(&squeezed,DO_TRANSFORM),"simple nonuniform text remains a direct command");
    rectangle(&squeezed,100,100,40,60);
    compare_scaled(&plain.canvas,&squeezed.canvas,.5,1.5,50,-50);
    yes(squeezed.canvas.px[110*W+132]==0xff102060u,"nonuniform glyph paints its actual transformed interior");
    yes(squeezed.canvas.px[110*W+163]==white,"untransformed source location stays empty");
    double ink[4];yes(paint_item_bounds(t,1,ink),"transformed primitive exposes actual damage ink");
    yes(ink[0]<=130&&ink[2]>=135&&ink[1]<=103&&ink[3]>=124,"ink bounds contain the mapped far-bearing bitmap");
    hit=layout_hit(squeezed.layout,110,110,0,0);yes(hit&&box_within(hit,squeezed.target->box),"same nonuniform matrix reaches the actual hit owner");
    yes(g_affine_windows>0,"nonuniform glyph uses the genuine bounded affine renderer");damage(&squeezed);
    destroy(&squeezed);destroy(&plain);

    case_name="origin translation and page scroll share the primitive matrix";
    plain=make("",20);squeezed=make("transform:translate(13px,7px) scale(.5,1.5);transform-origin:10px 5px",20);
    t=item(&squeezed,DO_TEXT);yes(t&&t->transform,"translated nonuniform primitive remains actual affine text");
    rectangle(&squeezed,118,84.5f,40,60);
    compare_scaled(&plain.canvas,&squeezed.canvas,.5,1.5,68,-35.5);damage(&squeezed);
    destroy(&squeezed);destroy(&plain);

    case_name="opacity ownership keeps the general group boundary";
    plain=make("opacity:.5",0);squeezed=make("opacity:.5;transform:scale(.5,1.5)",0);
    yes(item(&squeezed,DO_TRANSFORM)&&!item(&squeezed,DO_TEXT),"opacity group is never flattened");
    compare_scaled(&plain.canvas,&squeezed.canvas,.5,1.5,50,-50);damage(&squeezed);
    destroy(&squeezed);destroy(&plain);

    case_name="inner clip stays in its source frame";
    plain=make("width:65px;overflow:hidden",0);squeezed=make("width:65px;overflow:hidden;transform:scale(.5,1.5)",0);
    yes(item(&squeezed,DO_TRANSFORM)&&!item(&squeezed,DO_TEXT),"clip group is never flattened");
    compare_scaled(&plain.canvas,&squeezed.canvas,.5,1.5,50,-50);
    yes(squeezed.canvas.px[110*W+131]!=white&&squeezed.canvas.px[110*W+134]==white,"transformed clip retains its interior and rejects the clipped glyph tail");
    damage(&squeezed);destroy(&squeezed);destroy(&plain);
    case_name="primitive transformed ink inside an outer opacity layer";
    plain=make("",0);squeezed=make("transform:scale(.5,1.5)",0);
    body_style(&plain,"opacity:.5");body_style(&squeezed,"opacity:.5");
    t=item(&squeezed,DO_TEXT);yes(t&&t->transform&&item(&squeezed,DO_PUSH_LAYER),"outer opacity encloses the actual transformed text primitive");
    compare_scaled(&plain.canvas,&squeezed.canvas,.5,1.5,50,-50);damage(&squeezed);
    destroy(&squeezed);destroy(&plain);

    case_name="outer clipping stays in destination coordinates";
    squeezed=make("transform:scale(.5,1.5)",0);
    body_style(&squeezed,"position:relative;width:132px;height:200px;overflow:hidden");
    t=item(&squeezed,DO_TEXT);yes(t&&t->transform&&item(&squeezed,DO_PUSH_CLIP),"outer clip encloses actual affine text");
    yes(squeezed.canvas.px[110*W+131]!=white&&squeezed.canvas.px[110*W+133]==white,"destination clip preserves mapped interior and clips mapped tail");
    rectangle(&squeezed,100,100,40,60);damage(&squeezed);destroy(&squeezed);

    case_name="nested nonuniform primitive uses the shared outer affine compositor";
    plain=make("transform:scale(.5,1.5)",0);squeezed=make("transform:scale(.5,1.5)",0);
    body_style(&squeezed,"width:640px;height:480px;transform:rotate(90deg);transform-origin:160px 160px");
    yes(item(&squeezed,DO_TRANSFORM)&&!item(&squeezed,DO_TEXT),"outer rotation keeps the complete nested group");
    bool same=true;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(squeezed.canvas.px[y*W+x]!=sample_pixel(&plain.canvas,y,319-x))same=false;
    yes(same,"nested group equals independent ninety-degree pixel permutation");damage(&squeezed);
    destroy(&squeezed);destroy(&plain);
    assert(checks==74);generated_damage();assert(checks==82);printf("PASS css_scale_primitives %u native checks\n",checks);return 0;
}
