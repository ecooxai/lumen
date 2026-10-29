#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

enum { NODE_ELEMENT=1, A_video=10 };
typedef struct Node { int type, tag; const char *id; struct Node *parent; } Node;
typedef struct { Node *node; } Box;
typedef struct { Node *html; } Document;
typedef struct { const char *url; void *js, *L; Document *d; } Page;
typedef struct { Page *cur; float sy; } Tab;
typedef struct { Tab *t; } App;
static double g_mv_t,g_mv_rep,clock_ms;
static float g_mv_x,g_mv_y;
static Page *g_mv_p;
static Box box;
static int events;
static int holds;
static void js_eval(void *js,const char *source,const char *name) {
    assert(js && strstr(source,"player-control-overlay") && !strcmp(name,"lumen:touch-controls")); holds++;
}
static double now_ms(void) { return clock_ms; }
static const char *node_attr(Node *n,const char *name) { assert(!strcmp(name,"id")); return n->id; }
static Box *layout_hit(void *l,float x,float y,float sx,float sy) {(void)l;(void)x;(void)y;(void)sx;(void)sy;return &box;}
static bool page_hover(Page *p,Node *n) {(void)p;(void)n;return false;}
static void restyle(App *a) {(void)a;}
static void js_dispatch(void *js,Node *n,const char *event,const char *kind,bool a,bool b,float x,float y,int z,void *v) {
    (void)js;(void)n;(void)event;(void)kind;(void)a;(void)b;(void)x;(void)y;(void)z;(void)v;events++;
}
#include "controls.h"
int main(void) {
    assert(is_youtube("https://youtube.com/watch"));
    assert(is_youtube("https://www.youtube.com/watch"));
    assert(is_youtube("https://m.youtube.com/"));
    assert(!is_youtube("https://notyoutube.com/"));
    assert(!is_youtube("https://youtube.com.attacker.test/"));
    assert(!is_youtube(NULL));
    Node player={.type=NODE_ELEMENT,.id="movie_player"};
    Node button={.type=NODE_ELEMENT,.parent=&player};
    Node other={.type=NODE_ELEMENT};
    Document doc={.html=&other};
    Page page={.url="https://www.youtube.com/watch?v=test",.js=&doc,.L=&doc,.d=&doc};
    Tab tab={.cur=&page,.sy=40}; App app={.t=&tab}; box.node=&button;
    clock_ms=100;page_move(&app,20,30);assert(g_mv_p==&page);
    int previous=events;clock_ms=1099;page_move_keep(&app);assert(events==previous);
    clock_ms=1100;page_move_keep(&app);assert(events==previous+1);
    clock_ms=8101;page_move_keep(&app);assert(!g_mv_p && events==previous+1);
    clock_ms=9000;page_move(&app,20,30);box.node=&other;
    clock_ms=10000;page_move_keep(&app);assert(!g_mv_p);
    box.node=&button;clock_ms=11000;page_move(&app,20,30);tab.cur=NULL;
    clock_ms=12000;page_move_keep(&app);assert(!g_mv_p);tab.cur=&page;
    page.url="https://example.com/";clock_ms=13000;page_move(&app,20,30);assert(!g_mv_p);
    page.url="https://www.youtube.com/";box.node=&other;
    clock_ms=14000;page_move(&app,20,30);assert(!g_mv_p);
    page.url="https://m.youtube.com/watch?v=test";player.id="player-container-id";box.node=&button;
    previous=events;clock_ms=15000;page_touch_controls(&app,20,30);
    assert(!g_mv_p && events==previous && holds==1); /* No extra click/touch. */
    clock_ms=16000;page_move_keep(&app);assert(events==previous);
    clock_ms=23001;page_move_keep(&app);assert(!g_mv_p);
    clock_ms=24000;page_touch_controls(&app,20,30);assert(!g_mv_p);
    box.node=&other;page_touch_controls(&app,20,30);assert(!g_mv_p);
    box.node=&button;page_touch_controls(&app,20,-1);assert(!g_mv_p);
    page.url="https://example.com/";page_touch_controls(&app,20,30);assert(!g_mv_p);
    assert(holds==2); /* Outside-player taps must not inject the hold. */
    puts("YouTube player targeting, touch wiring, cadence, timeout and navigation cancellation pass");
}
