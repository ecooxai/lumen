#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu/gpu.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
static char g_pref[1024] = "./";
static int g_pixel_scale_pct = 200;
static GpuBackend g_renderer = GPU_BACKEND_VULKAN;
static bool g_lowmem, g_vctl, g_tab_sleep, g_hud, g_smooth;
static int g_cpu_on, g_cpu_pct, g_cpu_secs, g_cpu_lim, g_ua;
#define NODE_TEXT 3
typedef struct Node { int type; char *text; struct Node *first, *last; char id[40], classes[16]; } Node;
typedef struct { Node nodes[6]; } Document;
typedef struct { bool internal_ui; Document *d; } Page;
typedef struct { Page *cur; bool relayout; } Tab;
typedef struct { Tab *t; int pw, ph, screen_w, screen_h; float vw, vh; } App;
static char readout_text[256];
static int mutations;
static Node *doc_get_element_by_id(Document *document, const char *id) {
    for (int index = 0; index < 6; index++) if (!strcmp(document->nodes[index].id, id)) return &document->nodes[index];
    return NULL;
}
static void node_set_text_content(Node *node, const char *text) { (void)node; snprintf(readout_text, sizeof readout_text, "%s", text); mutations++; }
static const char *node_attr(Node *node, const char *name) { (void)name; return node->classes; }
static void node_set_attr(Node *node, const char *name, const char *value) { (void)name; snprintf(node->classes, sizeof node->classes, "%s", value); mutations++; }
#include "pixel_settings_functions.h"

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "parse")) {
        const char *valid[] = {"100", "150", "200", "250", "300"};
        for (int index = 0; index < 5; index++) CHECK(pixel_scale_parse(valid[index]) == 100 + index * 50);
        const char *invalid[] = {"", "0", "-100", "99", "400", "200junk", "200&x=1", " 200", "+200", "0200", "2.0", "9999999999999999999999999999999"};
        for (int index = 0; index < 12; index++) CHECK(pixel_scale_parse(invalid[index]) == 0);
    } else if (!strcmp(argv[1], "load")) {
        pixel_scale_load("pixel_scale=150\n"); CHECK(g_pixel_scale_pct == 150);
        pixel_scale_load("pixel_scale=300\r\n"); CHECK(g_pixel_scale_pct == 300);
        pixel_scale_load("pixel_scale=100"); CHECK(g_pixel_scale_pct == 100);
        g_pixel_scale_pct = 200;
        const char *invalid[] = {"pixel_scale=200junk\n", "pixel_scale=0\n", "pixel_scale=400\n", "pixel_scale=999999999999999999999999999999\n", "other=150\n"};
        for (int index = 0; index < 5; index++) { pixel_scale_load(invalid[index]); CHECK(g_pixel_scale_pct == 200); }
    } else if (!strcmp(argv[1], "save")) {
        g_pixel_scale_pct = 150;
        settings_save();
        g_pixel_scale_pct = 200;
        FILE *saved = fopen("settings.txt", "r"); CHECK(saved);
        char line[256];
        while (fgets(line, sizeof line, saved)) pixel_scale_load(line);
        fclose(saved); remove("settings.txt");
        CHECK(g_pixel_scale_pct == 150);
    } else if (!strcmp(argv[1], "readout")) {
        Document document = {0};
        Page page = {.internal_ui = true, .d = &document};
        Tab tab = {.cur = &page};
        App app = {.t = &tab, .pw = 1080, .ph = 2100, .screen_w = 1080, .screen_h = 2248, .vw = 720, .vh = 1400};
        Node text_node = {.type = NODE_TEXT, .text = readout_text};
        strcpy(document.nodes[0].id, "lumen-resolution");
        document.nodes[0].first = document.nodes[0].last = &text_node;
        for (int index = 1; index < 6; index++) {
            snprintf(document.nodes[index].id, sizeof document.nodes[index].id, "lumen-pixel-scale-%d", 50 + index * 50);
            strcpy(document.nodes[index].classes, index == 3 ? "btn on" : "btn");
        }
        g_pixel_scale_pct = 150;
        update_resolution_readout(&app);
        CHECK(!strcmp(document.nodes[2].classes, "btn on"));
        CHECK(!strcmp(document.nodes[3].classes, "btn"));
        CHECK(strstr(readout_text, "720.0 x 1400.0 px"));
        CHECK(tab.relayout && mutations == 3);
        tab.relayout = false;
        update_resolution_readout(&app);
        CHECK(!tab.relayout && mutations == 3);
        page.internal_ui = false; g_pixel_scale_pct = 300; app.vw = 360;
        update_resolution_readout(&app);
        CHECK(!tab.relayout && mutations == 3);
    } else CHECK(false);
    return 0;
}
