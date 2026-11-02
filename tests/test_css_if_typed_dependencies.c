/* Candidate303 actual typed values, final property bases and guarded cycles.
 * Original158/164/23/16/66 and full upstream families remain separate controls. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static const char *case_name;
static void yes(const char *label, bool value) {
    checks++;
    if (!value) { fprintf(stderr,"check %u (%s): %s\n",checks,case_name?case_name:"setup",label); abort(); }
}
static void refresh(StyleEngine *e, Layout *l, Document *d) {
    style_recalc(e,&d->node,false);layout_run(l,d,e->media.vw,e->media.vh);
}
static void computed(Node *node,const char *property,const char *expected) {
    char *actual=css_get_computed_value(node,property);
    if(!actual||strcmp(actual,expected))fprintf(stderr,"%s actual %s expected %s\n",property,actual?actual:"NULL",expected);
    yes("actual computed property",actual&&!strcmp(actual,expected));free(actual);
}
static void measure(Node *node,Layout *layout,const char *declaration,float expected) {
    if(!node->box||fabsf(node->box->w-expected)>.002f)fprintf(stderr,"width actual %g expected %g\n",node->box?(double)node->box->w:-1.,(double)expected);
    yes("actual used native width",node->box&&isfinite(node->box->w)&&fabsf(node->box->w-expected)<.002f);
    char value[40];snprintf(value,sizeof value,"%gpx",(double)expected);computed(node,"width",value);
    Box *hit=layout_hit(layout,node->box->x+3,node->box->y+3,0,0);
    yes("selected native geometry participates in hit testing",hit&&hit->node==node);
    yes("computed reads preserve authored declaration",!strcmp(node_attr(node,"style"),declaration));
}
static const char *target_base="display:block;height:10px;margin:0;padding:0;border:0;background:green;font-size:30px;line-height:45px;";
static void set_target(Node *node,const char *tail,char *buffer,size_t capacity) {
    int length=snprintf(buffer,capacity,"%s%s",target_base,tail);assert(length>=0&&(size_t)length<capacity);node_set_attr(node,"style",buffer);
}
static void overflow(Node *root,const char *declaration) {
    computed(root,"overflow","visible");
    yes("root horizontal overflow uses initial visible",root->style->overflow_x==OV_VISIBLE);
    yes("root vertical overflow uses initial visible",root->style->overflow_y==OV_VISIBLE);
    yes("root viewport cycle preserves specified declaration",!strcmp(node_attr(root,"style"),declaration));
}
int main(void) {
    dom_init();font_init();Document *d=doc_new("https://native-if-typed.test/");
    const char *html="<!doctype html><html><body><div id='parent'><div id='target'></div></div><div id='other'></div></body></html>";
    html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);e->media=(MediaCtx){800,600,1,false};
    const char *sheet="@property --q303-number{syntax:\"<number>\";inherits:true;initial-value:3}@property --q303-string{syntax:\"<string>\";inherits:false;initial-value:\"\"}@property --q303-color{syntax:\"<color>\";inherits:false;initial-value:blue}@property --q303-size{syntax:\"<length>\";inherits:false;initial-value:3px}@property --q303-inherited{syntax:\"<length>\";inherits:true;initial-value:3px}@property --q303-percentage{syntax:\"<percentage>\";inherits:false;initial-value:30%}@property --q303-angle{syntax:\"<angle>\";inherits:false;initial-value:3deg}@property --q303-time{syntax:\"<time>\";inherits:false;initial-value:3s}@property --q303-resolution{syntax:\"<resolution>\";inherits:false;initial-value:3dpi}@property --q303-integer{syntax:\"<integer>\";inherits:false;initial-value:2}"
        "html,body{margin:0;padding:0;border:0}#parent,#other{position:absolute;left:40px;top:30px;width:240px;height:200px;font-size:20px;line-height:2;color:blue;--q303-size:30px;--q303-inherited:30px;--q303-number:7}#other{left:320px;--q303-inherited:50px}";
    style_engine_add_sheet(e,css_parse_sheet(sheet,strlen(sheet),d->url,1,&e->media));Layout *l=layout_new();
    Node *t=doc_get_element_by_id(d,"target"),*p=doc_get_element_by_id(d,"parent"),*other=doc_get_element_by_id(d,"other");assert(t&&p&&other);
    static const struct{const char *name,*query,*declaration;float expected;} cases[]={
    {"registered number compares the computed math value", "style(--q303-number: 3)", "--q303-number:calc(1 + 2)", 80},
    {"unregistered math remains a token value", "style(--q303-plain: 3)", "--q303-plain:calc(1 + 2)", 40},
    {"registered string comparison uses the decoded string", "style(--q303-string: \"ab\")", "--q303-string:\"a\\62\"", 80},
    {"registered color comparison uses its computed color", "style(--q303-color: #008000)", "--q303-color:green", 80},
    {"registered length computes against the final own font", "style(--q303-size: 30px)", "--q303-size:1em", 80},
    {"registered query length uses the final own font", "style(--q303-size: 1em)", "--q303-size:30px", 80},
    {"registered percentage retains its percentage type", "style(--q303-percentage: 30%)", "--q303-percentage:30%", 80},
    {"registered percentage does not acquire a length hint", "style(--q303-percentage: 30px)", "--q303-percentage:30%", 40},
    {"registered angle converts to the canonical unit", "style(--q303-angle: 180deg)", "--q303-angle:.5turn", 80},
    {"registered time converts to the canonical unit", "style(--q303-time: .5s)", "--q303-time:500ms", 80},
    {"registered resolution converts to the canonical unit", "style(--q303-resolution: 1dppx)", "--q303-resolution:96dpi", 80},
    {"registered integer rounds a computed math result", "style(--q303-integer: 3)", "--q303-integer:calc(2.5)", 80},
    {"registered integer rejects a noninteger literal token", "style(--q303-integer: 2)", "--q303-integer:2.0", 80},
    {"invalid registered inherited value takes inherited fallback", "style(--q303-number: 7)", "--q303-number:invalid", 80},
    {"noninherited registration starts at its own initial value", "style(--q303-size: 3px)", "", 80},
    {"inherited registration keeps the parent computed value", "style(--q303-inherited: 30px)", "", 80},
    {"registered unset observes noninheritance", "style(--q303-size: 3px)", "--q303-size:unset", 80},
    {"registered explicit inherit overrides noninheritance", "style(--q303-size: 30px)", "--q303-size:inherit", 80},
    {"registered initial value is false in boolean context", "style(--q303-number)", "--q303-number:3", 40},
    {"registered noninitial value is true in boolean context", "style(--q303-number)", "--q303-number:4", 80},
    {"range evaluates typed arithmetic", "style(calc(1 + 2) > 2)", "", 80},
    {"range permits unitless zero against a length", "style(0 = 0px)", "", 80},
    {"range keeps percentage zero distinct from a number", "style(0 = 0%)", "", 40},
    {"range compares compatible absolute length units", "style(1in = 96px)", "", 80},
    {"range accepts an increasing chain", "style(1 < 2 <= 3)", "", 80},
    {"range accepts a decreasing chain", "style(3 >= 2 > 1)", "", 80},
    {"range rejects a reversed chain", "style(1 < 2 > 1)", "", 40},
    {"range rejects incompatible dimensions", "style(1px > 1s)", "", 40},
    {"range rejects a compound dimensional result", "style(calc(1px * 1s) = calc(1px * 1s))", "", 40},
    {"range reads an unregistered custom property operand", "style(--q303-plain >= 3)", "--q303-plain:3", 80},
    {"range resolves own em after declaration ordering", "style(1em = 30px)", "", 80},
    {"range resolves the actual own line height", "style(1lh = 45px)", "", 80},
    {"range reads a computed registered custom property", "style(--q303-size = 30px)", "--q303-size:1em", 80},
    {"range rejects a nonnumeric custom property", "style(--q303-plain > 1)", "--q303-plain:green", 40},
    {"query sees a later winning font size", "style(1em = 40px)", "font-size:40px", 80},
    {"query sees font shorthand size and line height", "style((1em = 32px) and (1lh = 48px))", "font:32px/48px sans-serif", 80},
    {"range infinity participates without a float clamp", "style(calc(infinity) > 999999)", "", 80},
    {"range top-level NaN values compute to zero", "style(calc(NaN) = calc(NaN))", "", 80},
    {"registered query inherit uses the parent value", "style(--q303-size: inherit)", "--q303-size:30px", 80},
    {"registered query initial uses the initial value", "style(--q303-size: initial)", "--q303-size:3px", 80},
    };
    char declaration[2300],tail[1900];
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        case_name=cases[i].name;
        snprintf(tail,sizeof tail,"--q303-chosen:if(%s:80px;else:40px);width:var(--q303-chosen);%s",cases[i].query,cases[i].declaration);
        set_target(t,tail,declaration,sizeof declaration);refresh(e,l,d);measure(t,l,declaration,cases[i].expected);
    }
    assert(checks==160);
    case_name="own font mutation reaches cached query";
    const int fonts[]={30,40,30},widths[]={80,40,80};
    for(unsigned i=0;i<3;i++) {
        snprintf(tail,sizeof tail,"width:if(style(1em = 30px):80px;else:40px);font-size:%dpx",fonts[i]);
        set_target(t,tail,declaration,sizeof declaration);refresh(e,l,d);measure(t,l,declaration,(float)widths[i]);
    }
    case_name="registered inherited mutation";
    set_target(t,"width:if(style(--q303-inherited = 30px):80px;else:40px)",declaration,sizeof declaration);
    const char *parent_values[]={"--q303-inherited:30px","--q303-inherited:50px","--q303-inherited:30px"};
    for(unsigned i=0;i<3;i++){node_set_attr(p,"style",parent_values[i]);refresh(e,l,d);measure(t,l,declaration,(float)widths[i]);}
    node_set_attr(p,"style","");
    case_name="reparenting uses actual new inheritance";
    refresh(e,l,d);measure(t,l,declaration,80);node_append(other,t);refresh(e,l,d);measure(t,l,declaration,40);node_append(p,t);refresh(e,l,d);measure(t,l,declaration,80);
    assert(checks==196);
    case_name="font-size own em cycle";
    set_target(t,"font-size:if(style(1em = 30px):50px;else:60px);width:2em",declaration,sizeof declaration);refresh(e,l,d);computed(t,"font-size","20px");measure(t,l,declaration,40);
    case_name="overridden cyclic font is not visited";
    set_target(t,"font-size:if(style(1em = 30px):50px;else:60px);font-size:30px;width:2em",declaration,sizeof declaration);refresh(e,l,d);computed(t,"font-size","30px");measure(t,l,declaration,60);
    case_name="line-height own lh cycle";
    set_target(t,"line-height:if(style(1lh > 1px):50px;else:60px);height:1lh;width:50px",declaration,sizeof declaration);refresh(e,l,d);
    yes("cyclic inherited line-height retains unitless factor",t->style->line_height_factor==2);computed(t,"height","60px");yes("actual inherited unitless line-height consumer",t->box&&fabsf(t->box->h-60)<.002f);
    Box *line_hit=layout_hit(l,t->box->x+3,t->box->y+40,0,0);yes("line-height used geometry hit",line_hit&&line_hit->node==t);yes("cyclic line-height retains declaration",!strcmp(node_attr(t,"style"),declaration));
    case_name="registered currentColor retains computed keyword";
    set_target(t,"--q303-color:currentColor;color:if(style(--q303-color: blue):green;else:red);width:80px",declaration,sizeof declaration);refresh(e,l,d);computed(t,"color","rgb(255, 0, 0)");measure(t,l,declaration,80);
    case_name="logical viewport and own font dependency cycle";
    set_target(t,"writing-mode:if(style(1em = 800px):vertical-rl;else:horizontal-tb);font-size:100vi;width:2em",declaration,sizeof declaration);refresh(e,l,d);computed(t,"writing-mode","horizontal-tb");computed(t,"font-size","20px");measure(t,l,declaration,40);
    assert(checks==222);
    static const struct{const char *name,*css;int expected;} cascade[]={
        {"later all initial","font-size:30px;all:initial",16},
        {"important font over normal all","font-size:30px!important;all:initial",30},
        {"important all over normal font","all:initial!important;font-size:30px",16},
        {"invalid pending font inherits","font-size:var(--q303-missing)",20},
    };
    for(unsigned i=0;i<sizeof cascade/sizeof cascade[0];i++) {
        case_name=cascade[i].name;
        const char *priority=i==2?"!important":"";
        snprintf(tail,sizeof tail,"%s;display:block%s;height:10px%s;margin:0%s;padding:0%s;border:0%s;width:if(style(1em = %dpx):80px;else:40px)%s",cascade[i].css,priority,priority,priority,priority,priority,cascade[i].expected,priority);
        set_target(t,tail,declaration,sizeof declaration);refresh(e,l,d);char expected[30];snprintf(expected,sizeof expected,"%dpx",cascade[i].expected);computed(t,"font-size",expected);measure(t,l,declaration,80);
    }
    assert(checks==242);
    case_name="actual registration metadata native entry";
    css_register_property_full("--q303-native-length","5px","<length>",false);
    node_set_attr(p,"style","--q303-native-length:70px");
    set_target(t,"width:var(--q303-native-length)",declaration,sizeof declaration);refresh(e,l,d);measure(t,l,declaration,5);
    set_target(t,"--q303-native-length:2em;width:var(--q303-native-length)",declaration,sizeof declaration);refresh(e,l,d);measure(t,l,declaration,60);
    node_set_attr(p,"style","");
    case_name="missing and explicit empty registration initial";
    css_register_property_full("--q303-native-missing",NULL,"*",false);css_register_property_full("--q303-native-empty","","*",false);
    set_target(t,"width:var(--q303-native-missing,80px)",declaration,sizeof declaration);refresh(e,l,d);measure(t,l,declaration,80);
    set_target(t,"width:var(--q303-native-empty,80px)",declaration,sizeof declaration);refresh(e,l,d);measure(t,l,declaration,240);
    assert(checks==258);
    case_name="root viewport unit cycle is invalid at computed value time";
    css_register_property_full("--q303-native-root-vw","0px","<length>",false);
    const char *root_style="--q303-native-root-vw:100vw;overflow:if(style(--q303-native-root-vw = 800px):scroll;else:hidden)";
    node_set_attr(d->html,"style",root_style);
    set_target(t,"width:60px",declaration,sizeof declaration);
    refresh(e,l,d);overflow(d->html,root_style);e->media.vw=700;refresh(e,l,d);overflow(d->html,root_style);
    node_set_attr(d->html,"style","");
    assert(checks==266);
    case_name="descendant viewport query remains live without root cycle";
    set_target(t,"width:if(style(100vw = 800px):80px;else:40px);overflow:if(style(100vw = 800px):hidden;else:visible)",declaration,sizeof declaration);
    const float viewport_widths[]={800,700,800};
    for(unsigned i=0;i<3;i++){
        e->media.vw=viewport_widths[i];refresh(e,l,d);measure(t,l,declaration,(float)widths[i]);
        computed(t,"overflow",i==1?"visible":"hidden");yes("visited viewport query retains viewport invalidation",t->style->uses_vh);
    }
    assert(checks==284);
    /* Review supplement: ordinary declaration consumers must not acquire
     * the own-line cycle edges of style-query operands. Original284 remains
     * above, apart from the separately versioned important-all setup. */
    static const struct{const char *name,*css;} line_consumers[]={
        {"plain parent lh","line-height:2lh"},
        {"variable parent lh","--q303-line:2lh;line-height:var(--q303-line)"},
        {"font shorthand parent lh","font:30px/2lh sans-serif"}
    };
    for(unsigned i=0;i<sizeof line_consumers/sizeof line_consumers[0];i++){
        case_name=line_consumers[i].name;
        snprintf(tail,sizeof tail,"%s;height:1lh;width:50px",line_consumers[i].css);
        set_target(t,tail,declaration,sizeof declaration);refresh(e,l,d);
        computed(t,"line-height","80px");computed(t,"font-size","30px");computed(t,"height","80px");
        yes("actual line height uses parent forty",fabsf(t->style->line_height-80)<.002f);
        yes("ordinary lh controls actual used height",t->box&&fabsf(t->box->h-80)<.002f);
        Box *hit=layout_hit(l,t->box->x+3,t->box->y+60,0,0);yes("ordinary lh height participates in hit testing",hit&&hit->node==t);
        yes("ordinary lh reads preserve declaration",!strcmp(node_attr(t,"style"),declaration));
    }
    assert(checks==305);
    case_name="font size consumes parent line height";
    set_target(t,"font-size:1lh;width:2em",declaration,sizeof declaration);refresh(e,l,d);
    computed(t,"font-size","40px");computed(t,"line-height","45px");measure(t,l,declaration,80);
    assert(checks==311);
    case_name="root rlh resolves initial line metrics without self cycle";
    const char *root_line="line-height:0rlh";node_set_attr(d->html,"style",root_line);node_set_attr(p,"style","line-height:inherit");
    set_target(t,"line-height:inherit;height:1lh;width:50px",declaration,sizeof declaration);refresh(e,l,d);
    computed(d->html,"line-height","0px");yes("root line height is actual zero",d->html->style->line_height==0);
    computed(t,"line-height","0px");computed(t,"height","0px");yes("root zero line controls descendant used height",t->box&&t->box->h==0);
    yes("root rlh retains original declaration",!strcmp(node_attr(d->html,"style"),root_line));
    yes("root rlh consumer retains original declaration",!strcmp(node_attr(t,"style"),declaration));
    assert(checks==318);
    case_name="own line query keeps parent-based line height independent of font size";
    node_set_attr(d->html,"style","");node_set_attr(p,"style","");
    set_target(t,"font-size:if(style(1lh = 80px):30px;else:60px);line-height:2lh;width:2em;height:1lh",declaration,sizeof declaration);refresh(e,l,d);
    computed(t,"font-size","30px");computed(t,"line-height","80px");computed(t,"height","80px");measure(t,l,declaration,60);
    assert(checks==325);
    /* Explicit post-reference version: final numeric censorship and
     * computed currentcolor keyword, never a used-color snapshot. */
    case_name="range final NaN boundary";
    set_target(t,"width:if(style(calc(NaN + 1) = 0):80px;else:40px)",declaration,sizeof declaration);refresh(e,l,d);measure(t,l,declaration,80);
    case_name="registered final NaN boundary";
    set_target(t,"--q303-number:calc(NaN + 1);width:if(style(--q303-number: 0):80px;else:40px)",declaration,sizeof declaration);refresh(e,l,d);computed(t,"--q303-number","0");measure(t,l,declaration,80);
    static const struct{const char *name,*query;float expected;} color_boundaries[]={
        {"registered currentColor computed equality","style(--q303-color: currentColor)",80},
        {"currentColor differs from absolute used color","style(--q303-color: blue)",40}
    };
    for(unsigned i=0;i<sizeof color_boundaries/sizeof color_boundaries[0];i++){
        case_name=color_boundaries[i].name;
        for(unsigned j=0;j<2;j++){
            const char *color=j?"green":"blue";
            snprintf(tail,sizeof tail,"--q303-color:currentColor;color:%s;width:if(%s:80px;else:40px)",color,color_boundaries[i].query);
            set_target(t,tail,declaration,sizeof declaration);refresh(e,l,d);
            computed(t,"--q303-color","currentcolor");computed(t,"color",j?"rgb(0, 128, 0)":"rgb(0, 0, 255)");measure(t,l,declaration,color_boundaries[i].expected);
        }
    }
    assert(checks==358);
    layout_free(l);style_engine_free(e);doc_free(d);
    printf("PASS css_if_typed_dependencies %u native checks\n",checks);return 0;
}
