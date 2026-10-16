#include "../src/css/css.h"
#include <assert.h>
#include <math.h>
#include <unistd.h>
int main(int argc,char **argv) {
    assert(argc==2);alarm(30);dom_init();
    Document *d=doc_new("https://starting-style.test/");
    Node *n=node_new_element(d,"dialog",NS_HTML);node_append(&d->node,n);node_set_attr(n,"open","");
    const char *css="dialog[open]{opacity:1}@starting-style{dialog[open]{opacity:0}}";
    if(!strcmp(argv[1],"nested"))css="dialog[open]{opacity:1;@starting-style{opacity:0!important}}";
    else if(!strcmp(argv[1],"conditional"))css="dialog[open]{opacity:1}@media(min-width:1px){@starting-style{dialog[open]{opacity:0!important}}}";
    else assert(!strcmp(argv[1],"top")||!strcmp(argv[1],"reopen"));
    StyleEngine *e=style_engine_new(d);style_engine_add_sheet(e,css_parse_sheet(css,strlen(css),d->url,1,&e->media));
    style_recalc(e,&d->node,true);assert(n->style&&fabsf(n->style->opacity-1)<.001f);
    if(!strcmp(argv[1],"reopen")){for(int i=0;i<3;i++){node_remove_attr(n,"open");style_recalc(e,&d->node,false);assert(n->style->display==D_NONE);node_set_attr(n,"open","");style_recalc(e,&d->node,false);assert(n->style->display!=D_NONE&&fabsf(n->style->opacity-1)<.001f);}}
    style_engine_free(e);doc_free(d);printf("%s passed\n",argv[1]);
}
