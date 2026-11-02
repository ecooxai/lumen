/* Native query branches reach layout, hit testing, and viewport invalidation.
 * Existing164/23/16/scalar66 controls are independent and unchanged. */
#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void yes(const char *label,bool value){checks++;if(!value){fprintf(stderr,"%s\n",label);abort();}}
static void refresh(StyleEngine *e,Layout *l,Document *d){style_recalc(e,&d->node,false);layout_run(l,d,e->media.vw,e->media.vh);}
static void width(Node*n,float expected){
    if(!n->box||fabsf(n->box->w-expected)>.002f)fprintf(stderr,"width %g expected %g\n",n->box?(double)n->box->w:-1.,(double)expected);
    yes("actual native width",n->box&&isfinite(n->box->w)&&fabsf(n->box->w-expected)<.002f);
    char expect[40];snprintf(expect,sizeof expect,"%gpx",(double)expected);char*actual=css_get_computed_value(n,"width");
    yes("used CSSOM width",actual&&!strcmp(actual,expect));free(actual);
}
int main(void){
    dom_init();font_init();Document*d=doc_new("https://native-if-queries.test/");
    const char*html="<!doctype html><html><body><div id='parent'><div id='target'></div></div></body></html>";
    html_parse(d,html,strlen(html));StyleEngine*e=style_engine_new(d);e->media=(MediaCtx){800,600,1,false};
    const char*css="html,body{margin:0;padding:0;border:0}#parent{position:absolute;left:40px;top:30px;width:240px;height:100px}#target{display:block;height:10px;margin:0;padding:0;border:0;background:green}";
    style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));Layout*l=layout_new();refresh(e,l,d);
    Node*t=doc_get_element_by_id(d,"target"),*p=doc_get_element_by_id(d,"parent");assert(t&&p);
    static const struct{const char*query;int on,off,expected;} cases[]={
        {"supports(display:table-cell)",40,20,40},
        {"supports((display:table-cell))",41,21,41},
        {"supports(display:flex)",42,22,42},
        {"supports(display:inline flex)",43,23,43},
        {"supports((display:table-cell) and (display:list-item) and (display:contents))",44,24,44},
        {"supports(display:invalid)",45,25,25},
        {"supports(display)",46,26,26},
        {"supports(unknown-native-property:yes)",47,27,27},
        {"supports(selector(h2 > p))",48,28,48},
        {"supports((selector(h2 > p)))",49,29,49},
        {"supports(selector(:not-a-supported-pseudo))",50,30,30},
        {"supports(selector(h2, p))",51,31,31},
        {"supports(not future-query(test))",52,32,52},
        {"media(height <= 999999px)",53,33,53},
        {"media(min-color:1)",54,34,54},
        {"media((min-color:1) and (height <= 999999px))",55,35,55},
        {"media(not (not-a-media-feature))",56,36,36},
        {"media((min-color:1) or (not-a-media-feature))",57,37,57},
        {"style(--ok:yes) and media(min-color:1) and supports(display:contents)",58,38,58},
        {"var(--condition)",59,39,59},
        {"supports(width:30)",60,40,40},
        {"supports(width:calc(2px + 3px))",61,41,61},
    };
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){
        char style[1400];snprintf(style,sizeof style,"--ok:yes;--condition:supports(display:flex);--chosen:if(%s:%dpx;else:%dpx);width:var(--chosen)",cases[i].query,cases[i].on,cases[i].off);
        node_set_attr(t,"style",style);refresh(e,l,d);width(t,(float)cases[i].expected);
        Box*hit=layout_hit(l,t->box->x+3,t->box->y+3,0,0);yes("selected geometry reaches native hit testing",hit&&hit->node==t);
        yes("computed reads do not rewrite the declaration",!strcmp(node_attr(t,"style"),style));
    }
    assert(checks==88);
    const char*query="media((height < 100px) or ((height >= 200px) and (height < 300px)))";
    const int heights[]={50,100,200,300},expected[]={66,26,66,26};char style[700];
    snprintf(style,sizeof style,"--chosen:if(%s:66px;else:26px);width:var(--chosen)",query);
    node_set_attr(t,"style",style);
    for(unsigned i=0;i<4;i++){
        e->media.vh=(float)heights[i];refresh(e,l,d);width(t,(float)expected[i]);
        yes("visited true and false media queries retain viewport dependency",t->style->uses_vh);
    }
    snprintf(style,sizeof style,"--chosen:if(%s:67px;else:27px)",query);node_set_attr(p,"style",style);node_set_attr(t,"style","width:var(--chosen)");
    for(unsigned i=0;i<4;i++){
        e->media.vh=(float)heights[i];refresh(e,l,d);width(t,(float)expected[i]+1);
        yes("parent computed custom query retains viewport dependency",p->style->uses_vh);
    }
    assert(checks==112);
    for(int quirks=0;quirks<2;quirks++){
        d->quirks=quirks;node_set_attr(t,"style","width:1px");
        node_set_attr(t,"style","width:if(supports(width:30):68px;else:28px)");refresh(e,l,d);width(t,quirks?68:28);
    }
    yes("native display enum remains supported",css_supports_declaration("display","block",false));
    yes("native display multi-keyword remains supported",css_supports_declaration("display","inline flex",false));
    yes("invalid display is rejected",!css_supports_declaration("display","nonsense",false));
    yes("invalid typed dimension is rejected",!css_supports_declaration("border-top-width","1s",false));
    yes("math whitespace remains validated on original tokens",!css_supports_declaration("width","calc(1px+ 2px)",false));
    yes("negative flex factor remains rejected",!css_supports_declaration("flex-grow","-1",false));
    yes("normal unitless preferred width remains rejected",!css_supports_declaration("width","30",false));
    yes("actual quirks preferred width remains supported",css_supports_declaration("width","30",true));
    assert(checks==124); /* Every original query124 control remains unchanged. */
    d->quirks=false;
    static const struct{const char*selector;int expected;} selectors[]={
        {"h2:is(.ok, :where(p, span))",76},
        {":is(h2, :not-a-supported-pseudo)",36},
        {":where(:is(h2, :not-a-supported-pseudo), span)",36},
        {":nth-child(2 of :is(h2, :not-a-supported-pseudo))",36},
    };
    for(unsigned i=0;i<sizeof selectors/sizeof selectors[0];i++){
        char declaration[512];snprintf(declaration,sizeof declaration,"width:if(supports(selector(%s)):76px;else:36px)",selectors[i].selector);
        node_set_attr(t,"style",declaration);refresh(e,l,d);width(t,(float)selectors[i].expected);
        Box*hit=layout_hit(l,t->box->x+3,t->box->y+3,0,0);yes("strict selector result reaches native hit testing",hit&&hit->node==t);
        yes("strict selector computed reads preserve declaration",!strcmp(node_attr(t,"style"),declaration));
    }
    SelList list={0};bool parsed=css_parse_query_selector_list(":is(h2, :not-a-supported-pseudo)",&list);
    yes("querySelector retains its existing forgiving branch behavior",parsed&&list.n==1);css_sellist_free(&list);
    yes("supports entry rejects a recursively unsupported selector",!css_supports_selector(":where(:is(h2, :not-a-supported-pseudo), span)"));
    assert(checks==142); /* Every reviewed query142 control remains unchanged. */
    static const struct{const char*selector;int expected;} pseudo_boundaries[]={
        {"::before",76},
        {":is(h2, ::before)",36},
        {":where(::before)",36},
        {":nth-child(2 of ::before)",36},
    };
    for(unsigned i=0;i<sizeof pseudo_boundaries/sizeof pseudo_boundaries[0];i++){
        char declaration[512];snprintf(declaration,sizeof declaration,"width:if(supports(selector(%s)):76px;else:36px)",pseudo_boundaries[i].selector);
        node_set_attr(t,"style",declaration);refresh(e,l,d);width(t,(float)pseudo_boundaries[i].expected);
        Box*hit=layout_hit(l,t->box->x+3,t->box->y+3,0,0);yes("real-selector boundary reaches native hit testing",hit&&hit->node==t);
        yes("real-selector boundary preserves declaration",!strcmp(node_attr(t,"style"),declaration));
    }
    layout_free(l);style_engine_free(e);doc_free(d);assert(checks==158);
    printf("PASS css_if_queries %u native checks\n",checks);return 0;
}
