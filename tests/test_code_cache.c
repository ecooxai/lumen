/* V8 code cache: large external scripts and modules leave bytecode on disk named by a
 * source hash; a later context consumes it (the files are not rewritten), and inline
 * scripts are never cached. */
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/paint/paint.h"
#include "../src/net/net.h"
#include "../src/base/url.h"
#include "../src/js/js.h"
#include "../src/js/jsglue.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static void vp(void *ud, float *w, float *h, float *sx, float *sy, float *dpr) { (void)ud; *w = 800; *h = 600; *sx = *sy = 0; *dpr = 1; }
static char g_dir[64], g_cache[96];

/* A script of at least 20 KB whose functions add up to a known value. */
static void write_js(const char *name, const char *head, const char *tail, int id) {
    char p[160]; snprintf(p, sizeof p, "%s/%s", g_dir, name);
    FILE *f = fopen(p, "w"); CHECK(f);
    fputs(head, f);
    for (int i = 0; i < 400; i++) fprintf(f, "function f%d_%d(x) { const pad = 'lumen code cache padding %d'; return x + %d + pad.length * 0; }\n", id, i, i, i % 7);
    fprintf(f, "%s", tail);
    fclose(f);
}

typedef struct { ino_t ino[8]; int n; } Files;
static Files list(void) {
    Files r = { {0}, 0 };
    DIR *d = opendir(g_cache); CHECK(d);
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.' || strchr(e->d_name, '~')) continue;
        char p[512]; snprintf(p, sizeof p, "%s/%s", g_cache, e->d_name);
        struct stat st; CHECK(stat(p, &st) == 0);
        CHECK(r.n < 8); r.ino[r.n++] = st.st_ino;
    }
    closedir(d);
    return r;
}

static const char *run_page(void) {
    static char result[128];
    char url[160]; snprintf(url, sizeof url, "file://%s/page.html", g_dir);
    Document *d = doc_new(url);
    SB inline_src; sb_init(&inline_src);
    sb_puts(&inline_src, "<script src='big.js'></script><script type='module' src='mod.mjs'></script><script>window.inl = 1;");
    for (int i = 0; i < 600; i++) sb_printf(&inline_src, "function inline%d() { return 'private inline data %d'; }\n", i, i);
    sb_puts(&inline_src, "</script>");
    html_parse(d, inline_src.s, inline_src.n);
    sb_free(&inline_src);
    JsHost h = {0}; h.viewport = vp;
    JsCtx *js = js_new(d, &h);
    for (int pass = 0; pass < 2; pass++) for (Node *n = d->node.first; n; n = node_next_in_tree(n, &d->node)) {
        if (n->type != NODE_ELEMENT || n->tag != A_script || (n->flags & NF_SCRIPT_STARTED)) continue;
        bool mod = jsg_module_script(n);
        if (mod != (pass == 1)) continue;
        const char *src = node_attr(n, "src");
        char *u = src ? url_join(d->url, src) : NULL;
        if (mod) js_run_module(js, n, NULL, 0, u);
        else if (src) {
            NetResponse *r = net_fetch_sync(net_request_new("GET", u));
            CHECK(r && r->status == 200);
            js_run_script(js, n, r->body, r->body_len, u);
            net_response_free(r);
        } else { char *t = node_text_content(n); js_run_script(js, n, t, strlen(t), d->url); free(t); }
        free(u);
    }
    js_eval(js, "document.documentElement.setAttribute('data-r', [window.big, window.mod, window.inl].join())", "test");
    snprintf(result, sizeof result, "%s", node_attr(d->html, "data-r") ? node_attr(d->html, "data-r") : "");
    js_free(js);
    doc_free(d);
    return result;
}

int main(int argc, char **argv) {
    (void)argc;
    alarm(30);
    snprintf(g_dir, sizeof g_dir, "/tmp/lumen-code-cache-XXXXXX");
    CHECK(mkdtemp(g_dir));
    snprintf(g_cache, sizeof g_cache, "%s/v8cache", g_dir);
    dom_init(); font_init(); js_global_init(argv[0]);
    js_code_cache_init(g_cache, 8 << 20);
    write_js("big.js", "", "window.big = f1_10(1) + f1_399(2);\n", 1);
    write_js("mod.mjs", "import { child } from './child.mjs';\n", "window.mod = child(f2_3(10));\n", 2);
    write_js("child.mjs", "", "export function child(x) { return f3_5(x) * 2; }\n", 3);
    const char *want = "6,36,1";
    const char *got = run_page();
    if (strcmp(got, want)) fprintf(stderr, "got '%s'\n", got);
    CHECK(!strcmp(got, want));
    /* bytecode is written off the main thread */
    Files first = { {0}, 0 };
    for (int i = 0; i < 200 && (first = list()).n < 3; i++) usleep(10000);
    CHECK(first.n == 3);   /* big.js, mod.mjs, child.mjs; not the inline script */
    CHECK(!strcmp(run_page(), want));
    usleep(100000);
    Files second = list();
    CHECK(second.n == 3);
    for (int i = 0; i < 3; i++) {   /* consumed, so not replaced */
        bool same = false;
        for (int k = 0; k < 3; k++) same |= first.ino[i] == second.ino[k];
        CHECK(same);
    }
    puts("code cache: ok");
    return 0;
}
