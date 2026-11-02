/* Text boxes share an element's complete style for shaping. Only element
 * boxes own positioning/opacity/transform phases. Measure real glyph pixels
 * and retain layout/hit behavior, including deferred inline owners. */
#include "../src/paint/paint.h"
#include "../src/js/jsglue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "affine_ink_font.h"

enum { W = 640, H = 480 };
static const uint32_t white = 0xffffffffu;
static unsigned checks;
static const char *case_name;
static void yes(bool value, const char *message) {
    checks++;
    if (!value) { fprintf(stderr, "%s: %s\n", case_name, message); abort(); }
}
static void near(float value, float expected, const char *message) {
    checks++;
    if (!isfinite(value) || fabsf(value - expected) > .025f) {
        fprintf(stderr, "%s: %s %g expected %g\n", case_name, message, value, expected); abort();
    }
}
typedef struct {
    Document *doc; StyleEngine *engine; Layout *layout;
    Node *owner, *text; DisplayList list; Canvas canvas;
} Fixture;
static Fixture make(const char *owner_style, const char *inline_style, bool inline_child) {
    Fixture f = {0}; f.doc = doc_new("https://text-paint-owner.test/");
    const char *html = inline_child
        ? "<!doctype html><html><body><div id=anchor><div id=owner><span id=inside>A</span></div></div></body></html>"
        : "<!doctype html><html><body><div id=anchor><div id=owner>A</div></div></body></html>";
    html_parse(f.doc, html, strlen(html)); f.engine = style_engine_new(f.doc);
    f.engine->media = (MediaCtx){W,H,1,false};
    const char *css = "html,body{margin:0;padding:0}#anchor{position:absolute;left:120px;top:120px;width:200px;height:200px}"
        "#owner{width:10px;height:20px;font:20px/20px LumenAffineInk;color:#102060;transform-origin:0 0}";
    style_engine_add_sheet(f.engine, css_parse_sheet(css, strlen(css), f.doc->url, 1, &f.engine->media));
    f.owner = doc_get_element_by_id(f.doc,"owner"); node_set_attr(f.owner,"style",owner_style);
    Node *inside = inline_child ? doc_get_element_by_id(f.doc,"inside") : f.owner;
    if (inline_child) node_set_attr(inside,"style",inline_style);
    f.text = inside->first;
    f.layout = layout_new(); style_recalc(f.engine,&f.doc->node,false); layout_run(f.layout,f.doc,W,H);
    canvas_init(&f.canvas,W,H,1); dl_build(&f.list,f.layout,0,0,W,H); raster(&f.canvas,&f.list,white);
    return f;
}
static void destroy(Fixture *f) {
    canvas_free(&f->canvas); dl_clear(&f->list); free(f->list.items.v);
    layout_free(f->layout); style_engine_free(f->engine); doc_free(f->doc);
}
static int glyph_count(const DItem *items, int n) {
    int count=0;
    for(int i=0;i<n;i++) {
        if(items[i].op==DO_TEXT) count+=items[i].ng;
        else if(items[i].op==DO_TRANSFORM && items[i].transform)
            count+=glyph_count(items[i].transform->items,items[i].transform->count);
    }
    return count;
}
static unsigned ink_count(const Canvas *canvas, int *left) {
    unsigned count=0; *left=W;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(canvas->px[y*W+x]!=white){count++;if(x<*left)*left=x;}
    return count;
}
/* Independent ninety-degree pixel permutation and source-over opacity over
 * opaque white. Two-channel-unit tolerance covers integer group rounding. */
static void compare(const Canvas *reference,const Canvas *actual,bool rotate,float opacity) {
    bool same=true;
    for(int y=0;same&&y<H;y++)for(int x=0;x<W;x++) {
        int sx=rotate?y:x,sy=rotate?239-x:y;
        uint32_t source=sx>=0&&sx<W&&sy>=0&&sy<H?reference->px[sy*W+sx]:white;
        uint32_t got=actual->px[y*W+x];
        if((got>>24)!=255){same=false;break;}
        for(int shift=0;shift<=16;shift+=8) {
            int expected=(int)lroundf(((source>>shift)&255)*opacity+255*(1-opacity));
            int channel=(got>>shift)&255;
            if(abs(channel-expected)>2){
                fprintf(stderr,"%s: pixel(%d,%d) %08x reference %08x opacity %g\n",case_name,x,y,got,source,opacity);
                same=false;break;
            }
        }
        if(!same)break;
    }
    yes(same,"actual glyph pixels match independent placement and group-opacity oracle");
}
int main(void) {
    dom_init();font_init();case_name="independent static text reference";
    const char *url="https://text-paint-owner.test/original.ttf";
    font_declare("LumenAffineInk",400,false,url);
    yes(font_loaded(url,affine_ink_font,sizeof affine_ink_font),"original deterministic font loads");
    Font *font=font_get("LumenAffineInk",400,false,20);ShapedRun run={0};text_shape(font,"A",1,0,&run);
    yes(run.n==1,"reference shapes one real glyph");
    const Glyph *glyph=font_glyph(run.g[0].font,run.g[0].gid,1);
    yes(glyph&&glyph->left==60&&glyph->w==10&&glyph->h==14,"independent far-bearing bitmap metrics");shaped_free(&run);
    Fixture reference=make("","",false);int left;
    yes(ink_count(&reference.canvas,&left)>=100&&left>=180,"static reference actually paints far-bearing ink");
    yes(glyph_count(reference.list.items.v,reference.list.items.n)==1,"static reference emits exactly one glyph");
    yes(reference.text&&reference.text->type==NODE_TEXT&&reference.text->box&&reference.text->box->st==reference.text->parent->style,"real text box shares parent style without owning the element");
    static const struct { const char *name,*owner,*inside;bool inline_child,rotate;float opacity; } cases[]={
        {"direct text under absolute element","position:absolute;left:0;top:0","",false,false,1},
        {"direct text under relative element","position:relative;left:0;top:0","",false,false,1},
        {"direct text under one opacity owner","opacity:.5","",false,false,.5f},
        {"direct text under isolated element","isolation:isolate","",false,false,1},
        {"direct text under transformed element","transform:rotate(90deg)","",false,true,1},
        {"text within real positioned inline owner","","position:relative",true,false,1},
        {"text within nested real opacity owners","opacity:.5","opacity:.5",true,false,.25f},
        {"positioned inline text within transformed block","transform:rotate(90deg)","position:relative",true,true,1},
    };
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        case_name=cases[i].name;Fixture f=make(cases[i].owner,cases[i].inside,cases[i].inline_child);
        Box *b=f.owner->box;assert(b);near(b->x,120,"normal x");near(b->y,120,"normal y");near(b->w,10,"normal width");near(b->h,20,"normal height");
        float rect[4];yes(jsg_vrect(f.owner,rect,0,0),"actual owner rectangle available");
        const float ordinary[4]={120,120,10,20},rotated[4]={100,120,20,10};
        for(int k=0;k<4;k++)near(rect[k],cases[i].rotate?rotated[k]:ordinary[k],"actual owner corner union");
        compare(&reference.canvas,&f.canvas,cases[i].rotate,cases[i].opacity);
        yes(ink_count(&f.canvas,&left)>=100,"glyph pixels survive each actual paint ownership phase");
        yes(glyph_count(f.list.items.v,f.list.items.n)==1,"text is emitted exactly once across nested paint phases");
        yes(f.text&&f.text->type==NODE_TEXT&&f.text->box&&f.text->box->st==f.text->parent->style,"text keeps its real shared shaping style");
        Box *hit=layout_hit(f.layout,cases[i].rotate?110:125,cases[i].rotate?125:130,0,0);
        yes(hit&&box_within(hit,b),"normal text advance remains in the owner hit subtree");
        destroy(&f);
    }
    assert(checks==118);destroy(&reference);printf("PASS css_text_paint_owner %u native checks\n",checks);return 0;
}
