#include "../../src/layout/layout.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
#define NEAR(actual, expected) (fabsf((actual) - (expected)) < 0.001f)

typedef struct { Layout *L; Document *d; } Page;
typedef struct { Page *cur; float sy; bool asleep; } Tab;
typedef struct { bool focus_reveal, editing, vonly, sdirty, osig_ok, gvid_ok, dirty; Tab *t; int fling; } App;
static bool editable;
static Node *page_focus(App *app) { return !editable && app->t->cur ? app->t->cur->d->focus : NULL; }
static Node *edit_focus(App *app) { return editable && app->t->cur ? app->t->cur->d->focus : NULL; }
static void android_fling_reset(int *fling) { *fling = 0; }

#include "focus_app_functions.h"

int main(int argc, char **argv) {
    CHECK(argc == 2);
    Document document = {0};
    ComputedStyle style = {.visibility = VIS_VISIBLE, .opacity = 1};
    Box root = {.w = 540, .h = 5000, .st = &style};
    Box input = {.x = 30, .y = 900, .w = 400, .h = 40, .parent = &root, .st = &style};
    Node focus = {.box = &input, .doc = &document};
    input.node = &focus; document.focus = &focus;
    Layout layout = {.root = &root, .doc = &document, .vw = 540, .vh = 586.5f, .doc_h = 5000};
    Page page = {.L = &layout, .d = &document};
    Tab tab = {.cur = &page};
    App app = {.t = &tab, .focus_reveal = true, .vonly = true, .sdirty = true,
        .osig_ok = true, .gvid_ok = true, .fling = 1};
    const char *scenario = argv[1];
    if (!strcmp(scenario, "pending")) {
        android_reveal_focus(&app);
        CHECK(!app.focus_reveal);
        CHECK(NEAR(tab.sy, 361.5f));
        CHECK(!app.vonly && !app.sdirty && !app.osig_ok && !app.gvid_ok);
        CHECK(app.fling == 0);
    } else if (!strcmp(scenario, "manual")) {
        app.focus_reveal = false;
        android_reveal_focus(&app);
        CHECK(tab.sy == 0 && app.fling == 1 && app.osig_ok);
    } else if (!strcmp(scenario, "address")) {
        app.editing = true;
        android_reveal_focus(&app);
        CHECK(!app.focus_reveal && tab.sy == 0);
    } else if (!strcmp(scenario, "removed")) {
        document.focus = NULL;
        android_reveal_focus(&app);
        CHECK(!app.focus_reveal && tab.sy == 0);
        app.focus_reveal = true; tab.cur = NULL;
        android_reveal_focus(&app);
        CHECK(!app.focus_reveal && tab.sy == 0);
    } else if (!strcmp(scenario, "editable") || !strcmp(scenario, "foreign-selection")) {
        editable = true; input.y = 0; input.h = 1800;
        Box selected = {.x = 30, .y = 1200, .w = 10, .h = 20, .parent = &input, .st = &style};
        Node selection = {.box = &selected, .doc = &document, .parent = &focus};
        selected.node = &selection; document.sel[1] = &selection;
        if (!strcmp(scenario, "foreign-selection")) { selection.parent = NULL; selected.parent = &root; }
        android_reveal_focus(&app);
        CHECK(!app.focus_reveal);
        CHECK(NEAR(tab.sy, !strcmp(scenario, "editable") ? 641.5f : 0));
    } else CHECK(false);
    return 0;
}
