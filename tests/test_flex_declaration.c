#include "../src/layout/layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks;
static void same(float a, float b) { checks++; assert(fabsf(a - b) < .001f); }
static void flag(bool ok) { checks++; assert(ok); }
static void triple(const ComputedStyle *s, float grow, float shrink, int kind,
                   float px, float pct, bool pctu) {
    same(s->flex_grow, grow); same(s->flex_shrink, shrink);
    flag(s->flex_basis.kind == kind); same(s->flex_basis.px, px);
    same(s->flex_basis.pct, pct); flag(s->flex_basis.pctu == pctu);
}
static void layout_controls(void) {
    for (int row = 0; row < 2; row++) {
        Document *d = doc_new("https://flex-declaration.test/");
        SB html = {0};
        sb_printf(&html, "<html><body><div id=p style='display:flex;flex-direction:%s;width:300px;height:300px;align-items:flex-start'>"
          "<div id=c style='flex:0 0 content;width:100px;height:100px;font:20px monospace;line-height:20px;white-space:nowrap'>xx</div></div>"
          "<div id=r style='display:inline-block;font:20px monospace;line-height:20px;white-space:nowrap'>xx</div></body></html>", row ? "row" : "column");
        html_parse(d,html.s,html.n);sb_free(&html);
        StyleEngine *e=style_engine_new(d);style_recalc(e,&d->node,true);
        Layout *l=layout_new();layout_run(l,d,800,600);
        Node *c=doc_get_element_by_id(d,"c"),*r=doc_get_element_by_id(d,"r");
        flag(c && c->box && r && r->box);
        same(row ? c->box->w : c->box->h, row ? r->box->w : 20);
        node_set_attr(c,"style","flex:0 0 content;width:200px;height:200px;font:20px monospace;line-height:20px;white-space:nowrap");
        style_recalc(e,&d->node,false);layout_run(l,d,800,600);
        same(row ? c->box->w : c->box->h,row ? r->box->w : 20);
        node_set_attr(c,"style","flex:0 0 auto;width:200px;height:200px;font:20px monospace;line-height:20px;white-space:nowrap");
        style_recalc(e,&d->node,false);layout_run(l,d,800,600);
        same(row ? c->box->w : c->box->h,200);
        layout_free(l);style_engine_free(e);doc_free(d);
    }
    Document *d=doc_new("https://flex-declaration.test/");
    const char *html="<div id=p style='flex:2 3 41px'><div id=c style='flex:inherit'></div></div>";
    html_parse(d,html,strlen(html));StyleEngine *e=style_engine_new(d);style_recalc(e,&d->node,true);
    Node *p=doc_get_element_by_id(d,"p"),*c=doc_get_element_by_id(d,"c");
    triple(c->style,2,3,LK_LEN,41,0,false);
    node_set_attr(p,"style","flex:5 6 51px");style_recalc(e,&d->node,false);
    triple(c->style,5,6,LK_LEN,51,0,false);
    style_engine_free(e);doc_free(d);
}
int main(void) {
    dom_init();font_init();
    ComputedStyle *s = style_new_default(), *parent = style_new_default();
    const struct { const char *text; float grow, shrink; int kind; float px, pct; bool pctu; } valid[] = {
        {"none",0,0,LK_AUTO,0,0,false}, {"auto",1,1,LK_AUTO,0,0,false},
        {"1",1,1,LK_LEN,0,0,true}, {"2 3",2,3,LK_LEN,0,0,true},
        {"4 5 6px",4,5,LK_LEN,6,0,false}, {"6px 4 5",4,5,LK_LEN,6,0,false},
        {"7% 8",8,1,LK_LEN,0,7,true}, {"8 auto",8,1,LK_AUTO,0,0,false},
        {"content",1,1,LK_CONTENT,0,0,false}, {"1 2 0",1,2,LK_LEN,0,0,false},
        {"calc(1) calc(2 + 1) calc(3px)",1,3,LK_LEN,3,0,false},
        {"calc(-1) calc(-2) calc(-3px)",0,0,LK_LEN,0,0,false},
        {"2 3 calc(10px + 25%)",2,3,LK_LEN,10,25,true},
        {"calc(8px / 2px) 3 5px",4,3,LK_LEN,5,0,false},
        {"calc(20% / 10%) 3 5px",2,3,LK_LEN,5,0,false}
    };
    for (unsigned i=0;i<sizeof valid/sizeof valid[0];i++) {
        flag(css_flex_valid("flex",valid[i].text));
        css_apply_decl(s,NULL,"flex",valid[i].text,NULL,NULL);
        triple(s,valid[i].grow,valid[i].shrink,valid[i].kind,valid[i].px,valid[i].pct,valid[i].pctu);
    }
    const char *invalid[] = {"none 1","2 3 4","5px 7%","9 none","1 2 calc(0)","1 2 calc(3 - 3)",
        "-1","1 -2","1 2 -3px","1 20px 2","1 2 3px 4","1 2 3pxjunk","1 2 autojunk",
        "1px 2px","calc(1px) 2 3px","1 2 calc(1s)","1 2 calc(2px * 3px)","1 2 none",
        "NaN","Infinity","1e1000","1 2 3deg","1 2 3fr"};
    for (unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
        flag(!css_flex_valid("flex",invalid[i]));
        css_apply_decl(s,NULL,"flex","7 8 9px",NULL,NULL);
        css_apply_decl(s,NULL,"flex",invalid[i],NULL,NULL);
        triple(s,7,8,LK_LEN,9,0,false);
    }
    css_apply_decl(parent,NULL,"flex","2 3 41px",NULL,NULL);
    css_apply_decl(s,parent,"flex","inherit",NULL,NULL);
    triple(s,2,3,LK_LEN,41,0,false); flag(s->parent_sensitive);
    for (unsigned i=0;i<3;i++) {
        const char *reset[]={"initial","unset","revert"};
        css_apply_decl(s,parent,"flex","7 8 9px",NULL,NULL);
        css_apply_decl(s,parent,"flex",reset[i],NULL,NULL);
        triple(s,0,1,LK_AUTO,0,0,false);
    }
    const char *props[]={"flex-grow","flex-shrink","flex-basis"};
    for (unsigned i=0;i<3;i++) {
        css_apply_decl(s,parent,"flex","0 0 0px",NULL,NULL);
        css_apply_decl(s,parent,props[i],"inherit",NULL,NULL);
        if(i==0)same(s->flex_grow,2);
        if(i==1)same(s->flex_shrink,3);
        if(i==2)same(s->flex_basis.px,41);
    }
    css_apply_decl(s,parent,"flex","7 8 9px",NULL,NULL);
    css_apply_decl(s,parent,"flex","var(--missing)",NULL,NULL);
    triple(s,0,1,LK_AUTO,0,0,false);
    css_apply_decl(s,parent,"--bad","2 3 4",NULL,NULL);
    css_apply_decl(s,parent,"flex","7 8 9px",NULL,NULL);
    css_apply_decl(s,parent,"flex","var(--bad)",NULL,NULL);
    triple(s,0,1,LK_AUTO,0,0,false);
    css_apply_decl(s,parent,"flex","7 8 9px",NULL,NULL);
    css_apply_decl(s,parent,"flex-grow","-1",NULL,NULL);
    css_apply_decl(s,parent,"flex-shrink","2px",NULL,NULL);
    css_apply_decl(s,parent,"flex-basis","-3px",NULL,NULL);
    triple(s,7,8,LK_LEN,9,0,false);
    flag(css_supports("(flex: 1 2 0)"));
    flag(!css_supports("(flex: 1 2 calc(0))"));
    flag(!css_supports("(flex-grow: -1)"));
    flag(css_supports("(flex-grow: calc(-1))"));
    style_free(s);style_free(parent);
    layout_controls();
    printf("%d native flex declaration controls passed\n",checks);
    return 0;
}
