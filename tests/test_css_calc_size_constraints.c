/* Calculated physical/logical size constraints: actual native layout,
 * computed snapshots and hit testing. Original304 controls remain intact. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static const char *case_name;
typedef struct { Document *doc; StyleEngine *engine; Layout *layout; Node *parent,*target,*first,*second,*leaf; char authored[2048]; } Fixture;
static const char *target_base="display:block;box-sizing:content-box;width:300px;height:100px;min-width:auto;max-width:none;min-height:auto;max-height:none;margin:0;padding:0;border:0 solid;font-size:0;line-height:0;background:green;";
static void yes(const char *label,bool value) {checks++;if(!value){fprintf(stderr,"%s: %s\n",case_name,label);abort();}}
static void near(const char *label,float value,float expected) {checks++;if(!isfinite(value)||fabsf(value-expected)>.04f){fprintf(stderr,"%s: %s %g expected %g\n",case_name,label,value,expected);abort();}}
static Box *box(Node *node){assert(node&&node->box);return node->box;}
static void refresh(Fixture *f){style_recalc(f->engine,&f->doc->node,false);layout_run(f->layout,f->doc,f->engine->media.vw,f->engine->media.vh);}
static void setup(Fixture *f,const char *decl){
    node_set_attr(f->parent,"style","position:absolute;left:40px;top:30px;width:400px;height:300px;margin:0;padding:0;border:0");
    snprintf(f->authored,sizeof f->authored,"%s%s",target_base,decl);node_set_attr(f->target,"style",f->authored);
    node_set_attr(f->first,"style","display:inline-block;vertical-align:top;width:120px;height:40px;margin:0;padding:0;border:0;font-size:0;line-height:0;pointer-events:none");
    node_set_attr(f->second,"style","display:inline-block;vertical-align:top;width:80px;height:40px;margin:0;padding:0;border:0;font-size:0;line-height:0;pointer-events:none");
    node_set_attr(f->leaf,"style","display:block;width:0;height:0;margin:0;padding:0;border:0;pointer-events:none");refresh(f);
}
static Length constraint(Node *node,const char *property){
    ComputedStyle*s=node->style;
    if(!strcmp(property,"min-width")||!strcmp(property,"min-inline-size"))return s->min_width;
    if(!strcmp(property,"max-width")||!strcmp(property,"max-inline-size"))return s->max_width;
    if(!strcmp(property,"min-height")||!strcmp(property,"min-block-size"))return s->min_height;
    assert(!strcmp(property,"max-height")||!strcmp(property,"max-block-size"));return s->max_height;
}
static void css_pixels(Node *node,const char *property,float expected){
    char*text=css_get_computed_value(node,property),*end=NULL;float value=text?strtof(text,&end):NAN;
    if(!text||end==text||strcmp(end,"px")){fprintf(stderr,"%s: %s non-pixel value '%s'\n",case_name,property,text?text:"(null)");abort();}
    near(property,value,expected);free(text);
}
static void measured(Fixture *f,Node *source,const char *property,bool calculated,float width,float height,float css_width,float css_height){
    yes("native sizing representation",len_calc_size(constraint(source,property))==calculated);
    near("real border width",box(f->target)->w,width);near("real border height",box(f->target)->h,height);
    css_pixels(f->target,"width",css_width);css_pixels(f->target,"height",css_height);
    char*animation=css_get_animation_value(source,property),*computed=css_get_computed_value(source,property);
    yes("animation snapshot preserves calculation",animation&&(!strncmp(animation,"calc-size(",10))==calculated);
    yes("computed constraint preserves calculation",computed&&(!strncmp(computed,"calc-size(",10))==calculated);
    free(animation);free(computed);
    Box*b=box(f->target),*hit=layout_hit(f->layout,b->x+1,b->y+1,0,0);
    yes("real constrained target receives native hit",hit&&hit->node==f->target);
    yes("measurement leaves authored declarations intact",!strcmp(node_attr(f->target,"style"),f->authored));
}

/* Twelve nested limits expose the old 2^depth probe recurrence. Counts are
 * actual layout_box entries, not elapsed-time thresholds or cache internals. */
enum { NESTED_DEPTH = 12 };
static void nested_state(Fixture *f, Node **levels, bool maximum,
                         float width, float leaf_height) {
    unsigned calls = 0;
    for (unsigned i = 0; i < NESTED_DEPTH; i++) {
        near("nested constraint uses its actual child height", box(levels[i])->h,
             leaf_height + NESTED_DEPTH - i);
        calls += (unsigned)box(levels[i])->dcalls;
    }
    calls += (unsigned)box(f->leaf)->dcalls;
    near("nested leaf wrapping height", box(f->leaf)->h, leaf_height);
    near("nested outer used width", box(f->target)->w, width);
    yes("nested computed intrinsic limit retained", len_calc_size(maximum ?
        f->target->style->max_height : f->target->style->min_height));
    Box *outer = box(f->target);
    Box *hit = layout_hit(f->layout, outer->x + 1, outer->y + outer->h - .5f, 0, 0);
    yes("nested outer final strip receives the native hit", hit && hit->node == f->target);
    yes("nested measurement work is bounded by depth squared",
        calls <= 4u * (NESTED_DEPTH + 2) * (NESTED_DEPTH + 2) &&
        box(f->leaf)->dcalls <= 3 * (NESTED_DEPTH + 2));
}
static void nested_constraints(void) {
    Fixture f = {0}; char html[8192];
    size_t n = (size_t)snprintf(html, sizeof html,
        "<!doctype html><html><body><div id='nested-parent'>");
    for (unsigned i = 0; i < NESTED_DEPTH; i++)
        n += (size_t)snprintf(html+n, sizeof html-n, "<div class='limit' id='n%u'>", i);
    n += (size_t)snprintf(html+n, sizeof html-n,
        "<div id='nested-leaf'><span id='nested-first'></span><span id='nested-second'></span></div>");
    for (unsigned i = 0; i < NESTED_DEPTH; i++)
        n += (size_t)snprintf(html+n, sizeof html-n, "</div>");
    n += (size_t)snprintf(html+n, sizeof html-n, "</div></body></html>");
    assert(n < sizeof html);
    f.doc = doc_new("https://calc-size-nested-constraints.test/");
    html_parse(f.doc, html, n); f.engine = style_engine_new(f.doc);
    f.engine->media = (MediaCtx){800,600,1,false}; f.layout = layout_new();
    const char *sheet =
        "html,body,div,span{margin:0;padding:0;border:0 solid;font-size:0;line-height:0}"
        "#nested-parent{position:absolute;left:40px;top:30px;width:300px}"
        ".limit{display:block;height:0;min-height:calc-size(min-content, size + 1px)}"
        "#nested-leaf{display:block}"
        "#nested-first,#nested-second{display:inline-block;vertical-align:top;height:10px;pointer-events:none}"
        "#nested-first{width:120px}#nested-second{width:80px}";
    style_engine_add_sheet(f.engine,css_parse_sheet(sheet,strlen(sheet),f.doc->url,1,&f.engine->media));
    Node *levels[NESTED_DEPTH];
    for (unsigned i = 0; i < NESTED_DEPTH; i++) {
        char id[16]; snprintf(id,sizeof id,"n%u",i); levels[i] = doc_get_element_by_id(f.doc,id);
        assert(levels[i]);
    }
    f.parent=doc_get_element_by_id(f.doc,"nested-parent"); f.target=levels[0];
    f.first=doc_get_element_by_id(f.doc,"nested-first");
    f.second=doc_get_element_by_id(f.doc,"nested-second");
    f.leaf=doc_get_element_by_id(f.doc,"nested-leaf");
    case_name="nested intrinsic minimum probes retain geometry with polynomial work";
    refresh(&f); nested_state(&f,levels,false,300,10);
    node_set_attr(f.first,"style","height:25px");
    refresh(&f); nested_state(&f,levels,false,300,25);
    node_set_attr(f.parent,"style","width:150px");
    refresh(&f); nested_state(&f,levels,false,150,35);
    node_set_attr(f.parent,"style","width:300px");
    refresh(&f); nested_state(&f,levels,false,300,25);
    case_name="nested intrinsic maximum probes follow actual content and inline basis";
    for (unsigned i = 0; i < NESTED_DEPTH; i++)
        node_set_attr(levels[i],"style","height:1000px;min-height:0;max-height:calc-size(max-content, size + 1px)");
    refresh(&f); nested_state(&f,levels,true,300,25);
    node_set_attr(f.second,"style","height:40px");
    node_set_attr(f.parent,"style","width:150px");
    refresh(&f); nested_state(&f,levels,true,150,65);
    assert(checks==612);

    /* Same tree, multiple forced widths and containing-block widths, then
     * return to a prior context. Ordinary layout memoization is disabled. */
    case_name="nested probe memo retains every actual width context in one pass";
    Box *outer=box(f.target); float x=outer->x,y=outer->y;
    int before=box(f.leaf)->dcalls; f.layout->measure_only=true;
    const float widths[]={300,150,300,300}, bases[]={150,150,150,450}, heights[]={40,65,40,40};
    for (unsigned i=0;i<4;i++) {
        layout_box(f.layout,outer,x,y,bases[i],-1,NULL,SZ_FORCED,widths[i],-1);
        near("same-pass context gives the actual constrained height",outer->h,heights[i]+NESTED_DEPTH);
        near("same-pass context gives the actual wrapped leaf",box(f.leaf)->h,heights[i]);
    }
    yes("returning to a measured width does not restart nested probes",
        box(f.leaf)->dcalls-before <= 2*(NESTED_DEPTH+2));
    assert(checks==621);

    case_name="temporary style identity and shallow boxes cannot reuse normal probe results";
    ComputedStyle *original=outer->st, temporary=*original;
    outer->st=&temporary; temporary.padding[0]=L_px(5); temporary.padding[2]=L_px(7);
    layout_box(f.layout,outer,x,y,150,-1,NULL,SZ_FORCED,300,-1);
    near("first temporary style has its actual vertical edges",outer->h,64);
    temporary.padding[0]=L_px(9); temporary.padding[2]=L_px(13);
    layout_box(f.layout,outer,x,y,150,-1,NULL,SZ_FORCED,300,-1);
    near("reused stack style address does not alias an earlier measurement",outer->h,74);
    outer->st=original;
    layout_box(f.layout,outer,x,y,150,-1,NULL,SZ_FORCED,300,-1);
    near("normal style can return to its own cached context",outer->h,52);
    near("normal final descendant geometry remains real",box(f.leaf)->h,40);
    Box empty=*outer; empty.first=empty.last=empty.abs_head=NULL;
    empty.grid_used=NULL; empty.memo=empty.intr_ok=false;
    layout_box(f.layout,&empty,0,0,150,-1,NULL,SZ_FORCED,300,-1);
    near("shallow empty measurement box does not borrow the owner's content",empty.h,1);
    f.layout->measure_only=false;
    assert(checks==626);
    layout_free(f.layout);style_engine_free(f.engine);doc_free(f.doc);
}

int main(void){
    dom_init();font_init();Fixture f={0};f.doc=doc_new("https://calc-size-constraints.test/");
    const char*html="<!doctype html><html><body><div id='parent'><div id='target'><span id='first'><i id='leaf'></i></span><span id='second'></span></div></div></body></html>";
    html_parse(f.doc,html,strlen(html));f.engine=style_engine_new(f.doc);f.engine->media=(MediaCtx){800,600,1,false};
    const char*sheet="html,body{margin:0;padding:0;border:0}#target,#first,#second,#leaf{margin:0;padding:0;border:0 solid}";
    style_engine_add_sheet(f.engine,css_parse_sheet(sheet,strlen(sheet),f.doc->url,1,&f.engine->media));
    f.parent=doc_get_element_by_id(f.doc,"parent");f.target=doc_get_element_by_id(f.doc,"target");f.first=doc_get_element_by_id(f.doc,"first");f.second=doc_get_element_by_id(f.doc,"second");f.leaf=doc_get_element_by_id(f.doc,"leaf");f.layout=layout_new();
    static const struct{const char*name,*decl,*property;float width,height,css_width,css_height;bool calculated;}cases[]={
    {"maximum width uses unconstrained minimum content", "max-width:calc-size(min-content, size + 10px)", "max-width", 130, 100, 130, 100, true},
    {"maximum width uses unconstrained maximum content", "max-width:calc-size(max-content, size / 2 + 10px)", "max-width", 110, 100, 110, 100, true},
    {"minimum width grows beyond preferred width", "width:40px;min-width:calc-size(min-content, size * 2)", "min-width", 240, 100, 240, 100, true},
    {"minimum width calculation may shrink its intrinsic basis", "width:40px;min-width:calc-size(max-content, size / 2)", "min-width", 100, 100, 100, 100, true},
    {"minimum fit-content ignores own preferred width", "width:40px;min-width:calc-size(fit-content, size + 20px)", "min-width", 220, 100, 220, 100, true},
    {"maximum fit-content ignores own preferred width", "max-width:calc-size(fit-content, size - 20px)", "max-width", 180, 100, 180, 100, true},
    {"maximum width numeric percentage basis remains live", "max-width:calc-size(50%, size / 2 + 10px)", "max-width", 110, 100, 110, 100, true},
    {"minimum width outer percentage uses its containing block", "width:20px;min-width:calc-size(any, 25% + 15px)", "min-width", 115, 100, 115, 100, true},
    {"ordinary automatic minimum supplies zero basis", "width:20px;min-width:calc-size(auto, size * 2 + 30px)", "min-width", 30, 100, 30, 100, true},
    {"minimum width preserves a numeric basis", "width:20px;min-width:calc-size(80px, size / 2 + 10px)", "min-width", 50, 100, 50, 100, true},
    {"negative maximum width keeps its padding and border", "padding:10px;border:2px solid;max-width:calc-size(min-content, -1 * size)", "max-width", 24, 124, 0, 100, true},
    {"content-box intrinsic minimum converts its property box once", "width:20px;padding:10px;border:2px solid;min-width:calc-size(min-content, size / 2 + 10px)", "min-width", 94, 124, 70, 100, true},
    {"border-box intrinsic minimum includes its border basis", "box-sizing:border-box;width:20px;padding:10px;border:2px solid;min-width:calc-size(min-content, size / 2 + 10px)", "min-width", 82, 100, 82, 100, true},
    {"content-box intrinsic maximum converts its property box once", "padding:10px;border:2px solid;max-width:calc-size(max-content, size / 2)", "max-width", 124, 124, 100, 100, true},
    {"border-box intrinsic maximum includes its border basis", "box-sizing:border-box;padding:10px;border:2px solid;max-width:calc-size(max-content, size / 2)", "max-width", 112, 100, 112, 100, true},
    {"minimum width wins after a conflicting maximum", "min-width:calc-size(min-content, size * 2);max-width:calc-size(max-content, size / 2)", "min-width", 240, 100, 240, 100, true},
    {"calculated minimum wins after an ordinary maximum", "min-width:calc-size(min-content, size + 20px);max-width:80px", "min-width", 140, 100, 140, 100, true},
    {"logical minimum inline size maps to the actual width", "width:40px;min-inline-size:calc-size(max-content, size / 2)", "min-inline-size", 100, 100, 100, 100, true},
    {"logical maximum inline size maps to the actual width", "max-inline-size:calc-size(min-content, size + 10px)", "max-inline-size", 130, 100, 130, 100, true},
    {"nested maximum-width basis is evaluated once", "max-width:calc-size(calc-size(min-content, size + 20px), size * 2)", "max-width", 280, 100, 280, 100, true},
    {"constraint font lengths use the target computed font", "font-size:10px;width:20px;min-width:calc-size(max-content, size * .25 + 2em)", "min-width", 70, 100, 70, 100, true},
    {"absolute forced width still receives its intrinsic minimum", "position:absolute;left:0;top:0;width:calc-size(stretch, size / 2);min-width:calc-size(min-content, size * 2)", "min-width", 240, 100, 240, 100, true},
    {"absolute maximum uses distinct inset and percentage bases", "position:absolute;left:20px;right:30px;top:0;max-width:calc-size(stretch, size / 2 + 10%)", "max-width", 215, 100, 215, 100, true},
    {"ordinary width constraints preserve minimum precedence", "width:150px;min-width:120px;max-width:80px", "min-width", 120, 100, 120, 100, false},
    {"maximum height measures content at the used inline width", "max-height:calc-size(min-content, size + 10px)", "max-height", 300, 50, 300, 50, true},
    {"minimum height grows beyond preferred height", "height:20px;min-height:calc-size(max-content, size * 2)", "min-height", 300, 80, 300, 80, true},
    {"maximum fit-content block size measures real content", "max-height:calc-size(fit-content, size / 2 + 10px)", "max-height", 300, 30, 300, 30, true},
    {"ordinary automatic minimum block size supplies zero", "height:20px;min-height:calc-size(auto, size + 30px)", "min-height", 300, 30, 300, 30, true},
    {"maximum height resolves numeric percentage basis", "max-height:calc-size(50%, size / 2 + 10px)", "max-height", 300, 85, 300, 85, true},
    {"minimum height resolves calculation percentages separately", "height:20px;min-height:calc-size(any, 20% + 15px)", "min-height", 300, 75, 300, 75, true},
    {"minimum height preserves a numeric basis", "height:20px;min-height:calc-size(80px, size / 2 + 10px)", "min-height", 300, 50, 300, 50, true},
    {"content-box intrinsic block minimum converts once", "height:10px;padding:10px;border:2px solid;min-height:calc-size(min-content, size / 2 + 10px)", "min-height", 324, 54, 300, 30, true},
    {"border-box intrinsic block minimum includes its border basis", "box-sizing:border-box;height:10px;padding:10px;border:2px solid;min-height:calc-size(min-content, size / 2 + 10px)", "min-height", 300, 42, 300, 42, true},
    {"content-box intrinsic block maximum converts once", "padding:10px;border:2px solid;max-height:calc-size(min-content, size / 2)", "max-height", 324, 44, 300, 20, true},
    {"border-box intrinsic block maximum includes its border basis", "box-sizing:border-box;padding:10px;border:2px solid;max-height:calc-size(min-content, size / 2)", "max-height", 300, 32, 300, 32, true},
    {"negative maximum height keeps its padding and border", "padding:10px;border:2px solid;max-height:calc-size(min-content, -1 * size)", "max-height", 324, 24, 300, 0, true},
    {"minimum height wins after a conflicting maximum", "min-height:calc-size(min-content, size * 2);max-height:calc-size(max-content, size / 2)", "min-height", 300, 80, 300, 80, true},
    {"logical minimum block size maps to the actual height", "height:20px;min-block-size:calc-size(max-content, size * 2)", "min-block-size", 300, 80, 300, 80, true},
    {"logical maximum block size maps to the actual height", "max-block-size:calc-size(min-content, size + 10px)", "max-block-size", 300, 50, 300, 50, true},
    {"intrinsic block constraint sees wrapping at the used width", "width:100px;height:20px;min-height:calc-size(min-content, size + 10px)", "min-height", 100, 90, 100, 90, true},
    {"absolute forced height still receives its intrinsic minimum", "position:absolute;left:0;top:0;height:calc-size(stretch, size / 3);min-height:calc-size(max-content, size * 3)", "min-height", 300, 120, 300, 120, true},
    {"ordinary height constraints preserve minimum precedence", "height:20px;min-height:80px;max-height:50px", "min-height", 300, 80, 300, 80, false},
    };
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){
        case_name=cases[i].name;setup(&f,cases[i].decl);
        measured(&f,f.target,cases[i].property,cases[i].calculated,cases[i].width,cases[i].height,cases[i].css_width,cases[i].css_height);
    }
    assert(checks==378);
    case_name="live numeric percentage maximum";
    setup(&f,"max-width:calc-size(50%, size / 2 + 10px)");
    measured(&f,f.target,"max-width",true,110,100,110,100);
    node_set_attr(f.parent,"style","position:absolute;left:40px;top:30px;width:600px;height:300px");refresh(&f);
    measured(&f,f.target,"max-width",true,160,100,160,100);
    assert(checks==396);
    case_name="live intrinsic child widths invalidate the constraint";
    setup(&f,"width:40px;min-width:calc-size(min-content, size * 2)");
    measured(&f,f.target,"min-width",true,240,100,240,100);
    node_set_attr(f.first,"style","display:inline-block;vertical-align:top;width:90px;height:40px;pointer-events:none");refresh(&f);
    measured(&f,f.target,"min-width",true,180,100,180,100);
    assert(checks==414);
    case_name="live content height and inline wrapping invalidate the probe";
    setup(&f,"height:20px;min-height:calc-size(min-content, size * 2)");
    measured(&f,f.target,"min-height",true,300,80,300,80);
    node_set_attr(f.first,"style","display:inline-block;vertical-align:top;width:120px;height:70px;pointer-events:none");refresh(&f);
    measured(&f,f.target,"min-height",true,300,140,300,140);
    snprintf(f.authored,sizeof f.authored,"%swidth:100px;height:20px;min-height:calc-size(min-content, size * 2)",target_base);node_set_attr(f.target,"style",f.authored);refresh(&f);
    measured(&f,f.target,"min-height",true,100,220,100,220);
    assert(checks==441);
    case_name="inherited constraint retains the parent's computed font length";
    setup(&f,"font-size:10px;width:20px;min-width:inherit");
    node_set_attr(f.parent,"style","position:absolute;left:40px;top:30px;width:400px;height:300px;font-size:20px;min-width:calc-size(any, 2em)");refresh(&f);
    measured(&f,f.target,"min-width",true,40,100,40,100);
    node_set_attr(f.parent,"style","position:absolute;left:40px;top:30px;width:400px;height:300px;font-size:30px;min-width:calc-size(any, 2em)");refresh(&f);
    measured(&f,f.target,"min-width",true,60,100,60,100);
    assert(checks==459);
    case_name="calculated child minimum participates in ancestor intrinsic width";
    setup(&f,"display:inline-block;width:max-content");
    node_set_attr(f.first,"style","display:inline-block;vertical-align:top;width:40px;height:40px;min-width:calc-size(50px, size * 2);pointer-events:none");refresh(&f);
    measured(&f,f.first,"min-width",true,180,100,180,100);near("actual child minimum",box(f.first)->w,100);
    case_name="calculated child maximum participates in ancestor intrinsic width";
    node_set_attr(f.first,"style","display:inline-block;vertical-align:top;width:200px;height:40px;max-width:calc-size(50px, size * 2);pointer-events:none");refresh(&f);
    measured(&f,f.first,"max-width",true,180,100,180,100);near("actual child maximum",box(f.first)->w,100);
    case_name="child intrinsic minimum measures its own content independently";
    node_set_attr(f.first,"style","display:inline-block;vertical-align:top;width:40px;height:40px;min-width:calc-size(min-content, size * 4);pointer-events:none");
    node_set_attr(f.leaf,"style","display:block;width:30px;height:1px;pointer-events:none");refresh(&f);
    measured(&f,f.first,"min-width",true,200,100,200,100);near("actual intrinsic child minimum",box(f.first)->w,120);
    assert(checks==489);
    case_name="intrinsic probe precedes final definite child percentage layout";
    setup(&f,"height:20px;min-height:calc-size(min-content, size + 10px)");
    node_set_attr(f.first,"style","display:block;width:120px;height:50%;pointer-events:none");
    node_set_attr(f.second,"style","display:block;width:80px;height:20px;pointer-events:none");
    node_set_attr(f.leaf,"style","display:block;width:30px;height:100px;pointer-events:none");refresh(&f);
    measured(&f,f.target,"min-height",true,300,130,300,130);near("definite child percentage follows constrained height",box(f.first)->h,65);
    case_name="automatic preferred height keeps child percentage indefinite";
    snprintf(f.authored,sizeof f.authored,"%sheight:auto;min-height:calc-size(min-content, size + 10px)",target_base);node_set_attr(f.target,"style",f.authored);refresh(&f);
    measured(&f,f.target,"min-height",true,300,130,300,130);near("automatic child percentage remains content-sized",box(f.first)->h,100);
    yes("automatic preferred size remains indefinite",!len_def(f.target->style->height,300));
    assert(checks==510);
    layout_free(f.layout);style_engine_free(f.engine);doc_free(f.doc);
    nested_constraints();
    assert(checks==626);
    printf("PASS css_calc_size_constraints %u native checks\n",checks);return 0;
}
