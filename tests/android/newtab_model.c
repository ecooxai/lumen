#include "base/util.h"

#define MAX_TABS 32
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
typedef struct { char *title; } Document;
typedef struct { bool internal_ui; char *url; Document *d; } Page;
typedef struct { Page *cur; int ws; char url[2048]; } Tab;
typedef struct {
    int nws, wi, ntabs, ti, sel_all;
    float vw, side;
    char wsname[16][64];
    Tab *t, *tabs[MAX_TABS];
    bool editing;
    void *win;
} App;
typedef struct { char *url, *title; } Link;
static bool g_lowmem, g_cpu_on, g_vctl, g_tab_sleep, g_hud, g_smooth, g_compact = true;
static int g_cpu_pct, g_cpu_secs, g_cpu_lim, g_ua, g_nbm, g_nhv;
static Link g_bms[1], g_hv[1];
#define UA_COUNT 1
static const char *net_user_agent_name(int index) { return "Lumen"; }
static int created, selected = -1, closed = -1, input_started, input_stopped, navigated;
static Tab spare;
static Tab *tab_new(App *app) { if (app->ntabs == MAX_TABS) return NULL; app->tabs[app->ntabs++] = &spare; return &spare; }
static void tab_select(App *app, int index) { selected = index; app->ti = index; app->t = app->tabs[index]; app->editing = false; }
static void ws_select(App *app, int index) { selected = index; app->wi = index; }
static void ws_new(App *app, const char *name) { created++; app->nws++; }
static void ws_close(App *app, int index) { closed = index; app->nws--; app->wi = index > 0 ? index - 1 : 0; }
static void navigate(App *app, const char *url, bool push) { navigated++; snprintf(app->t->url, sizeof app->t->url, "%s", url); }
static void SDL_StartTextInput(void *window) { input_started++; }
static void SDL_StopTextInput(void *window) { input_stopped++; }
static float bar_x0(App *app) { return app->side; }
#include "newtab_functions.h"

int main(int argc, char **argv) {
    CHECK(argc == 2);
    Document document = {.title = "Example"};
    Page page = {.internal_ui = true, .url = "lumen://newtab", .d = &document};
    Tab first = {.cur = &page, .url = "lumen://newtab"};
    App app = {.nws = 2, .ntabs = 1, .t = &first, .tabs = {&first}, .vw = 320};
    strcpy(app.wsname[0], "Personal"); strcpy(app.wsname[1], "<script>&\"bad");
    const char *scenario = argv[1];
    if (!strcmp(scenario, "default")) {
        char *html = internal_page(&app, "lumen://newtab");
#ifdef __ANDROID__
        CHECK(strstr(html, "<h2>Workspaces</h2>"));
        CHECK(strstr(html, "lumen://workspace?new"));
        CHECK(strstr(html, "lumen://workspace?select=1"));
#else
        CHECK(strstr(html, "<h2>Bookmarks</h2>"));
#endif
        free(html);
    } else if (!strcmp(scenario, "sections")) {
        const char *sections[] = {"bookmarks", "history", "settings"};
        const char *headings[] = {"<h2>Bookmarks</h2>", "<h2>History</h2>", "<h2>Settings</h2>"};
        for (int index = 0; index < 3; index++) {
            char url[80]; snprintf(url, sizeof url, "lumen://newtab?s=%s", sections[index]);
            char *html = internal_page(&app, url); CHECK(strstr(html, headings[index])); free(html);
        }
    } else if (!strcmp(scenario, "icons")) {
        char *html = internal_page(&app, "lumen://newtab");
        CHECK(strstr(html, "workspace-icon"));
        CHECK(strstr(html, "aria-label=\"&lt;script&gt;&amp;&quot;bad\""));
        CHECK(!strstr(html, ">Personal<") && !strstr(html, "<script>&"));
        CHECK(strstr(html, "aria-current=\"true\""));
        free(html);
    } else if (!strcmp(scenario, "sidebar")) {
#ifdef __ANDROID__
        CHECK(sidebar_width(84) == 0 && sidebar_width(0) == 0);
#else
        CHECK(sidebar_width(84) == 84 && sidebar_width(0) == 0);
#endif
    } else if (!strcmp(scenario, "toolbar")) {
        CHECK(url_w(&app) >= 128);
        app.vw = 360; CHECK(url_w(&app) >= 168);
    } else if (!strcmp(scenario, "keyboard")) {
        real_tab_open(&app);
#ifdef __ANDROID__
        CHECK(!app.editing && input_started == 0 && input_stopped == 1);
        CHECK(!strcmp(app.t->url, "lumen://newtab"));
#else
        CHECK(app.editing && input_started == 1 && !*app.t->url);
#endif
    } else if (!strcmp(scenario, "actions")) {
        CHECK(workspace_action(&app, "lumen://workspace?select=1")); CHECK(selected == 1);
        CHECK(workspace_action(&app, "lumen://workspace?new")); CHECK(created == 1);
        CHECK(workspace_action(&app, "lumen://workspace?close")); CHECK(closed == 1);
        app.t = &first;
        CHECK(workspace_action(&app, "lumen://workspace?tab=0")); CHECK(selected == 0);
    } else if (!strcmp(scenario, "untrusted")) {
        page.internal_ui = false;
        CHECK(workspace_action(&app, "lumen://workspace?new")); CHECK(created == 0);
        page.internal_ui = true;
        const char *invalid[] = {"select=-1", "select=2", "select=1junk", "select=9999999999999999999", "select=", "select=1&new", "tab=1", "tab=-1", "new&select=1"};
        for (int index = 0; index < 9; index++) {
            char url[100]; snprintf(url, sizeof url, "lumen://workspace?%s", invalid[index]);
            CHECK(workspace_action(&app, url));
        }
        CHECK(created == 0 && selected == -1 && closed == -1);
        CHECK(!workspace_action(&app, "https://example.org"));
    } else if (!strcmp(scenario, "limits")) {
        app.nws = 16; CHECK(workspace_action(&app, "lumen://workspace?new")); CHECK(created == 0);
        app.nws = 1; app.ntabs = MAX_TABS;
        CHECK(workspace_action(&app, "lumen://workspace?new")); CHECK(created == 0);
        CHECK(workspace_action(&app, "lumen://workspace?close")); CHECK(closed == -1);
    } else { CHECK(false); }
    return 0;
}
