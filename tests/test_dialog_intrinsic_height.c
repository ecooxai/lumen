/* Actual HTML -> UA/author cascade -> layout regression for modal sizing. */
#include "../src/layout/layout.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int checks, failures;
static void check(bool value, const char *name, float vw, float vh) {
    checks++;
    if (!value) { failures++; fprintf(stderr,"FAIL %.0fx%.0f: %s\n",vw,vh,name); }
}
static bool near(float a, float b) { return fabsf(a-b)<0.05f; }
static void viewport(float vw, float vh) {
    const char *html="<!doctype html><html><body>"
        "<dialog open id='natural'><form><div class='content'></div></form></dialog>"
        "<dialog open id='explicit'><form><div class='content'></div></form></dialog>"
        "<dialog open id='capped'><form><div class='content'></div></form></dialog>"
        "<div id='auto-fill'><div class='content'></div></div>"
        "<div id='fixed-fit'><div class='content'></div></div>"
        "<div id='container'><div id='absolute-fit'><div class='content'></div></div></div>"
        "<dialog id='closed'><div class='content'></div></dialog>"
        "</body></html>";
    const char *css="*{box-sizing:border-box}html,body{margin:0;font-size:18px}"
        "dialog{width:280px;max-width:calc(100vw - 48px);padding:18px;border:3px solid black}"
        "form{margin:0;padding:0;border:0}.content{height:120px}"
        "#explicit{height:180px}#capped{max-height:200px}#capped .content{height:1000px}"
        "#auto-fill{position:fixed;top:10px;bottom:20px;width:100px;height:auto;margin:0;padding:0;border:0}"
        "#fixed-fit,#absolute-fit{width:100px;height:fit-content;margin:auto;padding:5px;border:2px solid black}"
        "#fixed-fit{position:fixed;top:10px;bottom:20px}"
        "#fixed-fit .content,#absolute-fit .content{height:80px}"
        "#container{position:relative;width:300px;height:420px}"
        "#absolute-fit{position:absolute;top:12px;bottom:18px}";
    Document *d=doc_new("https://dialog-layout.example/");html_parse(d,html,strlen(html));
    Node *natural=doc_get_element_by_id(d,"natural"), *explicit=doc_get_element_by_id(d,"explicit"), *capped=doc_get_element_by_id(d,"capped");
    natural->flags|=NF_MODAL;explicit->flags|=NF_MODAL;capped->flags|=NF_MODAL;
    StyleEngine *se=style_engine_new(d);se->media=(MediaCtx){vw,vh,1,false};
    style_engine_add_sheet(se,css_parse_sheet(css,strlen(css),d->url,1,&se->media));
    style_recalc(se,&d->node,true);Layout *L=layout_new();layout_run(L,d,vw,vh);
    Node *fill=doc_get_element_by_id(d,"auto-fill"),*fixed=doc_get_element_by_id(d,"fixed-fit"),*absolute=doc_get_element_by_id(d,"absolute-fit"),*container=doc_get_element_by_id(d,"container"),*closed=doc_get_element_by_id(d,"closed");
#define CHECK(value,name) check((value),(name),vw,vh)
    CHECK(natural->box && explicit->box && capped->box && fill->box && fixed->box && absolute->box && container->box,"all actual layout boxes exist");
    if (!(natural->box && explicit->box && capped->box && fill->box && fixed->box && absolute->box && container->box)) goto done;
    CHECK(natural->style->position==P_FIXED,"modal UA fixed positioning applies");
    CHECK(natural->style->height.kind==LK_FIT_CONTENT,"modal UA intrinsic height remains intact");
    CHECK(near(natural->box->h,162),"default modal height is content plus padding and border");
    CHECK(near(natural->box->y,(vh-162)/2),"default modal centers vertically without stretching");
    CHECK(near(natural->box->w,280) && near(natural->box->x,(vw-280)/2),"author width and horizontal centering remain intact");
    CHECK(near(explicit->box->h,180),"explicit author height remains authoritative");
    CHECK(near(explicit->box->y,(vh-180)/2),"explicit author height remains vertically centered");
    CHECK(near(capped->box->h,200),"intrinsic modal respects maximum height");
    CHECK(near(capped->box->y,(vh-200)/2),"maximum-height modal remains centered");
    CHECK(capped->box->scroll_h>900,"oversized modal preserves scrollable content");
    CHECK(near(fill->box->h,vh-30),"ordinary auto height still stretches between insets");
    CHECK(near(fill->box->y,10),"ordinary auto height keeps its top inset");
    CHECK(near(fixed->box->h,94),"ordinary fixed fit-content retains intrinsic height");
    CHECK(near(fixed->box->y,10+(vh-30-94)/2),"ordinary fixed intrinsic box centers between insets");
    CHECK(near(absolute->box->h,94),"absolute fit-content retains intrinsic height");
    CHECK(near(absolute->box->y,container->box->y+12+(420-30-94)/2),"absolute intrinsic box respects positioned containing block");
    CHECK(!closed->box,"closed dialog remains absent from layout");
    /* Repeat the real layout with the same document to exercise memoized sizing. */
    layout_run(L,d,vw,vh);
    CHECK(near(natural->box->h,162) && near(natural->box->y,(vh-162)/2),"repeat layout preserves compact modal geometry");
    CHECK(near(fill->box->h,vh-30),"repeat layout preserves auto-height fill");
    printf("GEOMETRY %.0fx%.0f natural=%.2f explicit=%.2f capped=%.2f fill=%.2f fixed=%.2f absolute=%.2f\n",vw,vh,natural->box->h,explicit->box->h,capped->box->h,fill->box->h,fixed->box->h,absolute->box->h);
done:
    layout_free(L);style_engine_free(se);doc_free(d);
#undef CHECK
}
int main(void) {
    dom_init();font_init();viewport(402,734);viewport(820,1116);
    printf("DIALOG_INTRINSIC_HEIGHT_RESULT checks=%d passed=%d failed=%d\n",checks,checks-failures,failures);
    return failures ? 1 : 0;
}
