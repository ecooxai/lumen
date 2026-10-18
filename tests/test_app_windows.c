/* The real App/Tab/SDL loader host, with a hidden native window. This calls the
 * same queue, install, drain and teardown functions as the browser. Activation
 * is explicit at the native API boundary; no OS events or login are simulated. */
#define main lumen_application_main
#include "../src/app/main.c"
#undef main

static int checks, failures;
#define CHECK(name, condition) do { checks++; if (!(condition)) { \
    failures++; fprintf(stderr, "FAIL app-windows: %s (line %d)\n", name, __LINE__); \
} } while (0)

static bool evaluate(Tab *t, const char *body) {
    if (!t || !t->cur || !t->cur->js || !t->cur->d->body) return false;
    size_t size = strlen(body) + 512; char *source = xmalloc(size);
    snprintf(source, size,
        "document.body.setAttribute('data-host-result','unset');"
        "document.body.removeAttribute('data-host-error');"
        "try{const ok=(()=>{%s\n})();document.body.setAttribute('data-host-result',ok?'yes':'no');}"
        "catch(e){document.body.setAttribute('data-host-error',String(e));}", body);
    js_eval(t->cur->js, source, "test:actual-native-window-host"); free(source);
    const char *error = node_attr(t->cur->d->body, "data-host-error");
    if (error) fprintf(stderr, "app-windows script: %s\n", error);
    const char *value = node_attr(t->cur->d->body, "data-host-result");
    return value && !strcmp(value, "yes");
}
static void tick(App *a) {
    Tab *act = a->t;
    for (int i = 0; i < a->ntabs; i++) {
        a->t = a->tabs[i];
        if (a->t->cur && a->t->cur->js && !js_window_is_closed(a->t->window_id)) js_tick(a->t->cur->js);
    }
    a->t = act; js_release_pins();
}
static bool wait_loaded(App *a, Tab *t) {
    Uint64 end = SDL_GetTicks() + 5000;
    while (t->loading && SDL_GetTicks() < end) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 25) && event.type == EV_LOADED) app_install_loaded(a, event.user.data1);
    }
    js_release_pins();
    return !t->loading && t->cur && t->cur->gen == t->lgen && t->cur->js;
}
static JsWindowId reservation(App *a, const char *url) {
    for (unsigned i = 0; i < APP_WINDOW_LIMIT; i++) {
        AppWindowRequest *r = &a->windows.slots[i];
        if (r->reserved && (!url || (r->url && !strcmp(r->url, url)))) return r->id;
    }
    return 0;
}
static void retire(App *a, JsWindowId id) {
    int i = tab_window_index(a, id); bool quit = false;
    if (i >= 0 && a->tabs[i]->cur && a->tabs[i]->cur->js) {
        CHECK("auxiliary accepts script close", evaluate(a->tabs[i], "window.close();return window.closed;"));
        app_windows_drain(a, &quit);
    }
    CHECK("auxiliary closure keeps the main tab alive", !quit && tab_window_index(a, id) < 0 && js_window_is_closed(id));
}

static void actual_open_navigate_close(App *a, Tab *root) {
    JsWindowId root_id = root->window_id;
    CHECK("real loader installed a registered primary realm", root_id && root->cur && root->cur->js && js_window_id(root->cur->js) == root_id);
    CHECK("unactivated open is denied without visible-tab mutation",
        evaluate(root, "return window.open('about:blank','blocked')===null;") && a->ntabs == 1 && !app_windows_pending(&a->windows));
    js_window_activate(root->cur->js);
    CHECK("activated open returns a live initial blank handle", evaluate(root,
        "globalThis.popup=window.open('about:blank','login');return popup && !popup.closed && popup.location.href==='about:blank';"));
    JsWindowId child_id = reservation(a, "about:blank");
    CHECK("open reserves native capacity before any tab exists", child_id && a->ntabs == 1 && app_windows_count(&a->windows) == 2);
    CHECK("one activation admits only one new auxiliary", evaluate(root, "return window.open('about:blank','second')===null;") && app_windows_count(&a->windows) == 2);
    CHECK("opener relation prevents sleeping its destination", !js_window_can_sleep(root_id));
    bool quit = false; app_windows_drain(a, &quit);
    int i = tab_window_index(a, child_id); Tab *child = i >= 0 ? a->tabs[i] : NULL;
    CHECK("drain installs an actual blank Page and JS realm", !quit && child && child->cur && child->cur->js && !strcmp(child->cur->url, "about:blank"));
    if (!child || !child->cur || !child->cur->js) return;
    CHECK("blank has its supplied context ID and exactly one history entry", child->window_id == child_id && js_window_id(child->cur->js) == child_id && child->nhist == 1 && child->hpos == 0 && !child->loading);
    CHECK("focus uses the real native auxiliary tab", a->t == child && a->tabs[a->ti] == child && !a->editing);
    CHECK("blank startup exposes a real opener and complete document", evaluate(child, "return opener!==null && document.readyState==='complete';"));
    CHECK("install a destination message listener", evaluate(child,
        "globalThis.incoming='';addEventListener('message',e=>{incoming=e.data;});return true;"));
    CHECK("default-origin delivery reaches the inherited blank realm", evaluate(root, "popup.postMessage('inherited');return true;"));
    tick(a);
    CHECK("initial blank received a directed message", evaluate(child, "return incoming==='inherited';"));

    Page *blank = child->cur;
    CHECK("location replace only queues a native navigation", evaluate(root,
        "popup.location.replace('about:blank#next');return !popup.closed;") && child->cur == blank && !child->loading);
    app_windows_drain(a, &quit);
    CHECK("loader starts after the current realm returns", !quit && child->loading && child->cur == blank && child->nhist == 1);
    CHECK("actual SDL loader completion installs the requested document", wait_loaded(a, child) && !strcmp(child->cur->url, "about:blank#next"));
    CHECK("ordinary navigation preserves native window identity", child->window_id == child_id && js_window_id(child->cur->js) == child_id && child->nhist == 1 && child->hgen[0] == child->cur->gen);
    CHECK("old opener handle still refers to the same window", evaluate(root, "return popup!==null && !popup.closed;"));
    CHECK("install the opener callback", evaluate(root,
        "globalThis.reply='';addEventListener('message',e=>{reply=e.data;});return true;"));
    a->t = child; h_sync_in(a, child->cur->d, true);
    g_down = child->cur->d->body; node_retain(g_down); g_down_p = child->cur;
    g_control_press = g_down; g_control_page = child->cur; g_cpress = true;
    g_mv_p = child->cur; g_tsel.L = child->cur->L; g_press = true;
    a->last_page = child->cur; a->osig_ok = true;
    CHECK("message plus close finishes on the still-live source stack", evaluate(child,
        "opener.postMessage('completed','*');window.close();document.body.setAttribute('still-alive','yes');return window.closed;") &&
        child->cur && node_attr(child->cur->d->body, "still-alive") && a->ntabs == 2 && js_window_is_closed(child_id));
    app_windows_drain(a, &quit);
    CHECK("deferred close frees the tab and restores its opener", !quit && a->ntabs == 1 && a->t == root && tab_window_index(a, child_id) < 0);
    CHECK("teardown releases native retained input and paint references", !g_down && !g_down_p && !g_control_press && !g_control_page && !g_mv_p && !g_tsel.L && !g_cpress && !g_press && !a->last_page && !a->osig_ok);
    tick(a);
    CHECK("sender closure does not discard its already queued callback", evaluate(root, "return reply==='completed' && popup.closed;"));
    size_t messages = 1, bytes = 1; js_windows_pending(&messages, &bytes);
    CHECK("delivered callback has no retained directed-message bytes", messages == 0 && bytes == 0);
}

static void pending_cancellation_and_capacity(App *a, Tab *root) {
    uint64_t generation = load_gen;
    js_window_activate(root->cur->js);
    CHECK("open then close can finish before native materialization", evaluate(root,
        "globalThis.cancelled=window.open('about:blank','cancelled');cancelled.close();return cancelled.closed;") && a->ntabs == 1);
    bool quit = false; app_windows_drain(a, &quit);
    CHECK("cancelled reservation never builds an unused Page", !quit && a->ntabs == 1 && app_windows_count(&a->windows) == 1 && load_gen == generation);
    bool all = true;
    for (unsigned n = 1; n < APP_WINDOW_LIMIT; n++) {
        js_window_activate(root->cur->js);
        all &= evaluate(root, "globalThis.pending=window.open('about:blank','_blank');return pending!==null;");
    }
    CHECK("pending public windows occupy the native tab limit", all && app_windows_count(&a->windows) == APP_WINDOW_LIMIT && a->ntabs == 1);
    js_window_activate(root->cur->js);
    CHECK("full admission returns null without adding a visible tab", evaluate(root, "return window.open('about:blank','overflow')===null;") && a->ntabs == 1 && app_windows_count(&a->windows) == APP_WINDOW_LIMIT);
    CHECK("native new-tab also respects pending reservations", tab_new(a) == NULL && a->ntabs == 1);
    for (unsigned n = 0; n < APP_WINDOW_LIMIT; n++) {
        AppWindowRequest *r = &a->windows.slots[n];
        if (r->reserved) h_window_close(a, r->id);
    }
    app_windows_drain(a, &quit);
    CHECK("closing reservations returns all native capacity", !quit && a->ntabs == 1 && app_windows_count(&a->windows) == 1 && !app_windows_pending(&a->windows) && load_gen == generation);
}

static void coalesced_native_requests(App *a, Tab *root) {
    SDL_FlushEvents(EV_NET, EV_NET);
    bool all = true; char url[80], referrer[80];
    for (int n = 0; n < 10000; n++) {
        snprintf(url, sizeof url, "about:blank#coalesced-%d", n);
        snprintf(referrer, sizeof referrer, "https://source.test/%d", n);
        JsWindowNavigate request = {root->window_id, url, referrer, true};
        all &= h_window_navigate(a, &request);
    }
    memset(url, 'x', sizeof url); memset(referrer, 'y', sizeof referrer);
    AppWindowRequest *r = app_windows_find(&a->windows, root->window_id);
    CHECK("native callbacks copy and coalesce the last URL and referrer", all && r && !strcmp(r->url, "about:blank#coalesced-9999") && !strcmp(r->referrer, "https://source.test/9999"));
    int wakes = 0; SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, EV_NET, EV_NET) > 0) wakes++;
    CHECK("coalesced callbacks enqueue one native wake event", wakes == 1);
    uint64_t before = load_gen; int history = root->nhist; bool quit = false;
    app_windows_drain(a, &quit);
    CHECK("one drain starts one loader and preserves replacement history", !quit && load_gen == before + 1 && root->nhist == history && root->loading);
    CHECK("loader commits the copied destination and filtered referrer", wait_loaded(a, root) && !strcmp(root->cur->url, "about:blank#coalesced-9999") && root->cur->d->referrer && !strcmp(root->cur->d->referrer, "https://source.test/9999"));
}

static void startup_supersedes_later_snapshot(App *a, Tab *root) {
    /* Existing explicit pre-file hook lets a new blank realm act during its
     * real startup batch. It changes a different target already in the taken
     * snapshot, then closes itself before its own requested loader starts. */
    const char prelude[] = "if(window.name==='first-native'){"
        "const later=window.open('','later-native');later.location.replace('about:blank#superseded');window.close();}";
    SDL_CreateDirectory("out/apple-compat");
    char path[] = "out/apple-compat/gpt-6-astra-chatgpt-blank-start-XXXXXX";
    int fd = mkstemp(path);
    CHECK("create owned blank-startup fixture", fd >= 0);
    if (fd < 0) return;
    ssize_t written = write(fd, prelude, sizeof prelude - 1); close(fd);
    CHECK("write complete blank-startup fixture", written == (ssize_t)(sizeof prelude - 1));
    if (written != (ssize_t)(sizeof prelude - 1)) { unlink(path); return; }
    js_window_activate(root->cur->js);
    CHECK("reserve first startup target", evaluate(root,
        "globalThis.firstNative=window.open('about:blank#first-original','first-native');return firstNative!==null;"));
    JsWindowId first = reservation(a, "about:blank#first-original");
    js_window_activate(root->cur->js);
    CHECK("reserve later snapshot target", evaluate(root,
        "globalThis.laterNative=window.open('about:blank#later-original','later-native');return laterNative!==null;"));
    JsWindowId later = reservation(a, "about:blank#later-original");
    uint64_t before = load_gen; bool quit = false;
    setenv("LUMEN_PRE_FILE", path, 1); app_windows_drain(a, &quit);
    unsetenv("LUMEN_PRE_FILE"); unlink(path);
    int i = tab_window_index(a, later); Tab *target = i >= 0 ? a->tabs[i] : NULL;
    CHECK("startup close waits until its JS batch has returned", first && js_window_is_closed(first) && tab_window_index(a, first) >= 0 && !quit);
    CHECK("later snapshot observes the newer queued navigation", target && target->cur && !target->loading && !strcmp(target->cur->url, "about:blank") && load_gen == before + 2);
    AppWindowRequest *r = app_windows_find(&a->windows, later);
    CHECK("new callback URL is retained for the following drain", r && r->navigate && !strcmp(r->url, "about:blank#superseded"));
    app_windows_drain(a, &quit);
    CHECK("following drain closes the first tab and starts only the new URL", !quit && tab_window_index(a, first) < 0 && target && target->loading && load_gen == before + 3);
    if (target) {
        CHECK("real loader installs the superseding startup destination", wait_loaded(a, target) && !strcmp(target->cur->url, "about:blank#superseded"));
        retire(a, later);
    }
}

static void coop_rebind(App *a, Tab *root) {
    /* Loader response fixture: no remote URL is fetched. This enters the same
     * actual completion function as EV_LOADED with an explicit COOP policy. */
    JsWindowId old = root->window_id;
    JsWindowNavigate stale = {old, "about:blank#must-not-start", NULL, false};
    CHECK("queue old-group commands before a policy commit", h_window_navigate(a, &stale) && h_window_focus(a, old));
    Page *p = xcalloc(1, sizeof *p); p->url = xstrdup("https://account.test/isolated");
    p->d = doc_new(p->url); const char html[] = "<!doctype html><html><body></body></html>";
    html_parse(p->d, html, sizeof html - 1); p->e = style_engine_new(p->d);
    p->e->media = root->cur->e->media; style_recalc(p->e, &p->d->node, true); p->seen_ver = p->d->dom_version;
    p->coop = JS_COOP_SAME_ORIGIN; p->gen = root->lgen = ++load_gen; publish_gens(a);
    CHECK("actual commit installs a fresh context for a COOP split", app_install_loaded(a, p) == root && root->window_id != old && js_window_id(root->cur->js) == root->window_id && js_window_is_closed(old));
    CHECK("COOP commit replaces its native slot and discards old-group commands", app_windows_count(&a->windows) == 1 && !app_windows_find(&a->windows, old) && app_windows_find(&a->windows, root->window_id) && !app_windows_pending(&a->windows));
    uint64_t before = load_gen; bool quit = false; app_windows_drain(a, &quit);
    CHECK("discarded old-group URL cannot start after policy installation", !quit && load_gen == before && !root->loading && !strcmp(root->cur->url, "https://account.test/isolated"));
}

static void frame_click_reentry(App *a, Tab *root) {
    static const char *const phase[] = {"mousedown", "mouseup", "click"};
    static const char *const prefixes[] = {"mousedown ", "mousedown mouseup ", "mousedown mouseup click "};
    for (int action = 0; action < 4; action++) for (int when = 0; when < (action < 2 ? 1 : 3); when++) {
        /* Creation, the child realm, hit geometry, trusted dispatch, default
         * anchor handling and teardown all use the actual App host paths. */
        CHECK("create a real srcdoc iframe for native click dispatch", evaluate(root,
            "globalThis.nativeClickFrame=document.createElement('iframe');"
            "nativeClickFrame.id='native-click-frame';"
            "nativeClickFrame.srcdoc='<style>html,body{margin:0;padding:0}a{display:block;width:120px;height:40px}</style>'"
            "+'<a id=target href=about:blank#default>target</a>';"
            "document.body.appendChild(nativeClickFrame);"
            "return nativeClickFrame.contentDocument!==null;"));
        Node *frame = doc_get_element_by_id(root->cur->d, "native-click-frame");
        JsCtx *original = js_frame_ctx(frame); Page *page = frame_find(frame);
        Node *target = page ? doc_get_element_by_id(page->d, "target") : NULL;
        CHECK("srcdoc has a native frame Page and actual target node", frame && original && page && target);
        if (!frame || !original || !page || !target) return;
        node_retain(frame); node_retain(target);
        JsWindowId id = js_window_id(original);
        const char *mutation = action == 0 ? "" : action == 1 ? "e.preventDefault();" :
            action == 2 ? "location.assign('about:blank');" : "frameElement.remove();void parent.length;";
        const char *at = action < 2 ? "click" : phase[when];
        char script[1536];
        snprintf(script, sizeof script,
            "(()=>{const t=document.getElementById('target');"
            "for(const type of ['mousedown','mouseup','click'])t.addEventListener(type,e=>{"
            "t.setAttribute('data-events',(t.getAttribute('data-events')||'')+e.type+' ');"
            "if(e.type==='%s'){%s}});t.setAttribute('data-armed','yes');})();", at, mutation);
        js_eval(original, script, "test:actual-iframe-click-reentry");
        CHECK("iframe handlers were installed in the original realm", node_attr(target, "data-armed") && !strcmp(node_attr(target, "data-armed"), "yes"));
        frame_layout(page);
        Box *hit = page->L ? layout_hit(page->L, 10, 10) : NULL;
        CHECK("actual frame layout hits the fixture link", hit && hit->node && node_within(hit->node, target));
        if (hit && hit->node && node_within(hit->node, target)) click_frame(a, page, 10, 10);
        const char *events = node_attr(target, "data-events");
        const char *expected = action < 2 ? prefixes[2] : prefixes[when];
        if (!events || strcmp(events, expected)) fprintf(stderr, "iframe click case action=%d phase=%s events=%s expected=%s\n", action, at, events ? events : "<none>", expected);
        CHECK("retired frame cannot receive the remaining native click events", events && !strcmp(events, expected));
        JsCtx *current = js_frame_ctx(frame);
        if (action == 0 || action == 2) {
            CHECK("frame navigation commits a new realm with its stable identity", current && current != original && js_window_id(current) == id && !js_window_is_closed(id));
            CHECK("frame navigation leaves one live native Page", frame_find(frame) && frame_find(frame)->d == js_frame_doc(frame));
        } else if (action == 1) {
            CHECK("preventDefault keeps the original live frame", current == original && frame_find(frame) == page);
        } else {
            CHECK("handler removal detaches and retires the frame context", !(frame->flags & NF_CONNECTED) && !current && !frame_find(frame) && js_window_is_closed(id));
        }
        /* No top-tab loader or command may be started by a stale link default. */
        CHECK("iframe click does not mutate the top-tab loader or window queue", !root->loading && !app_windows_pending(&a->windows));
        if (frame->flags & NF_CONNECTED) node_remove(frame);
        tick(a); js_release_pins();
        CHECK("fixture removal frees the native frame Page", !frame_find(frame) && !js_frame_ctx(frame));
        node_release(target); node_release(frame);
    }
}

static void frame_close_reentry(App *a, Tab *root) {
    js_window_activate(root->cur->js);
    CHECK("create an actual auxiliary for a frame-triggered close", evaluate(root,
        "globalThis.frameClosingNative=window.open('about:blank','frame-closing-native');return frameClosingNative!==null;"));
    JsWindowId top_id = reservation(a, "about:blank"); bool quit = false;
    app_windows_drain(a, &quit);
    int index = tab_window_index(a, top_id); Tab *child = index >= 0 ? a->tabs[index] : NULL;
    CHECK("auxiliary has a live native Page before the close event", top_id && child && child->cur && child->cur->js && !quit);
    if (!child || !child->cur || !child->cur->js) return;
    CHECK("auxiliary creates an actual srcdoc frame", evaluate(child,
        "globalThis.closingFrame=document.createElement('iframe');closingFrame.id='closing-frame';"
        "closingFrame.srcdoc='<style>html,body{margin:0;padding:0}a{display:block;width:120px;height:40px}</style>'"
        "+'<a id=closing-target href=about:blank#must-not-follow>target</a>';"
        "document.body.appendChild(closingFrame);return closingFrame.contentDocument!==null;"));
    Node *frame = doc_get_element_by_id(child->cur->d, "closing-frame");
    JsCtx *original = js_frame_ctx(frame); Page *page = frame_find(frame);
    Node *target = page ? doc_get_element_by_id(page->d, "closing-target") : NULL;
    CHECK("frame-triggered close fixture has a native target", frame && original && page && target);
    if (!frame || !original || !page || !target) { retire(a, top_id); return; }
    node_retain(frame); node_retain(target); JsWindowId frame_id = js_window_id(original);
    js_eval(original,
        "(()=>{const t=document.getElementById('closing-target');for(const type of ['mousedown','mouseup','click'])"
        "t.addEventListener(type,e=>{t.setAttribute('data-events',(t.getAttribute('data-events')||'')+e.type+' ');"
        "if(e.type==='mousedown')top.close();});})();", "test:actual-frame-close-reentry");
    frame_layout(page); Box *hit = page->L ? layout_hit(page->L, 10, 10) : NULL;
    CHECK("closing frame uses real link hit geometry", hit && hit->node && node_within(hit->node, target));
    if (hit && hit->node && node_within(hit->node, target)) click_frame(a, page, 10, 10);
    const char *events = node_attr(target, "data-events");
    CHECK("logical window close stops remaining mouse phases", events && !strcmp(events, "mousedown "));
    CHECK("close immediately retires the top and frame identities", js_window_is_closed(top_id) && js_window_is_closed(frame_id));
    CHECK("closed frame receives no default navigation before native drain", js_frame_ctx(frame) == original && frame_find(frame) == page && app_windows_pending(&a->windows));
    CHECK("close callback returns before native Page teardown", a->ntabs == 2 && tab_window_index(a, top_id) >= 0);
    node_release(target); node_release(frame);
    app_windows_drain(a, &quit);
    CHECK("safe drain removes the closing auxiliary and its native frames", !quit && a->ntabs == 1 && a->t == root && tab_window_index(a, top_id) < 0 && !g_frames && !app_windows_pending(&a->windows));
}

static Tab *last_visible_successor(App *a, Tab *root) {
    JsWindowId old = root->window_id;
    js_window_activate(root->cur->js);
    CHECK("last visible tab reserves an auxiliary before native closure", evaluate(root,
        "globalThis.successorNative=window.open('about:blank','native-successor');return successorNative!==null;") && a->ntabs == 1);
    JsWindowId successor = reservation(a, "about:blank"); bool quit = false;
    tab_close(a, a->ti, &quit);
    CHECK("native close defers the empty-app decision for an accepted successor", successor && !quit && a->ntabs == 1 && js_window_is_closed(old) && tab_window_index(a, old) >= 0);
    app_windows_drain(a, &quit);
    int i = tab_window_index(a, successor); Tab *next = i >= 0 ? a->tabs[i] : NULL;
    CHECK("drain realizes the accepted successor despite an earlier close item", !quit && next && next->cur && next->cur->js && a->t == next);
    app_windows_drain(a, &quit);
    CHECK("following drain retires the old Page without quitting its successor", !quit && a->ntabs == 1 && a->t == next && tab_window_index(a, old) < 0);
    if (!next) return a->t;
    root = next;
    /* Two hops naturally alternate the occupied queue slot. This exercises a
     * closing source both before and after its create item in slot order. */
    for (int hop = 0; hop < 2; hop++) {
        old = root->window_id; js_window_activate(root->cur->js);
        CHECK("last script-created tab can open and close in one JS batch", evaluate(root,
            "globalThis.nextNative=window.open('about:blank','_blank');window.close();"
            "document.body.setAttribute('finished','yes');return nextNative!==null && window.closed;") && a->ntabs == 1);
        successor = reservation(a, "about:blank");
        CHECK("script close preserves the live accepted reservation", successor && js_window_is_closed(old) && !js_window_is_closed(successor));
        app_windows_drain(a, &quit);
        i = tab_window_index(a, successor); next = i >= 0 ? a->tabs[i] : NULL;
        CHECK("successor materializes before the app can become empty", !quit && next && next->cur && next->cur->js);
        app_windows_drain(a, &quit);
        CHECK("script-created successor owns the surviving native tab", !quit && a->ntabs == 1 && a->t == next && tab_window_index(a, old) < 0);
        if (!next) return a->t;
        root = next;
    }
    old = root->window_id; js_window_activate(root->cur->js);
    CHECK("a successor may also be explicitly canceled in the same batch", evaluate(root,
        "const canceled=window.open('about:blank','_blank');canceled.close();window.close();return canceled.closed && window.closed;"));
    app_windows_drain(a, &quit);
    CHECK("canceled successor cannot defer an empty-app close indefinitely", quit && js_window_is_closed(old) && !app_windows_pending(&a->windows));
    return root; /* last-tab Page is kept for the normal process teardown */
}

int main(int argc, char **argv) {
    bool frame_only = argc > 1 && !strcmp(argv[1], "--frame-reentry");
    bool frame_close_only = argc > 1 && !strcmp(argv[1], "--frame-close-reentry");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "native host SDL init failed: %s\n", SDL_GetError()); return 1; }
    unsetenv("LUMEN_NO_JS"); unsetenv("LUMEN_PRE_FILE");
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    icache_mu = SDL_CreateMutex(); EV_LOADED = SDL_RegisterEvents(1); EV_DL = SDL_RegisterEvents(1);
    EV_NET = SDL_RegisterEvents(1); EV_MENU = SDL_RegisterEvents(1);
    net_wakeup = media_wakeup = js_wakeup = wake;
    App a = {0}; g_app = &a; a.nws = 1; a.scale = 1; a.vw = a.pw = 800; a.vh = a.ph = 600;
    a.device = (DeviceInfo){.screen_w = 800, .screen_h = 600, .avail_w = 800, .avail_h = 600, .outer_w = 800, .outer_h = 600, .orientation = DEVICE_LANDSCAPE_PRIMARY, .any_fine = true};
    a.win = SDL_CreateWindow("Lumen native auxiliary host test", 800, 600, SDL_WINDOW_HIDDEN);
    if (!a.win) { fprintf(stderr, "native host window failed: %s\n", SDL_GetError()); return 1; }
    Tab *root = tab_new(&a);
    CHECK("create the actual primary App tab", root != NULL);
    if (!root) return 1;
    navigate(&a, "about:blank", true);
    CHECK("primary page uses the actual asynchronous SDL loader", wait_loaded(&a, root));
    if (root->cur && root->cur->js && frame_only) frame_click_reentry(&a, root);
    else if (root->cur && root->cur->js && frame_close_only) frame_close_reentry(&a, root);
    else if (root->cur && root->cur->js) {
        actual_open_navigate_close(&a, root);
        pending_cancellation_and_capacity(&a, root);
        coalesced_native_requests(&a, root);
        startup_supersedes_later_snapshot(&a, root);
        coop_rebind(&a, root);
        frame_click_reentry(&a, root);
        frame_close_reentry(&a, root);
        root = last_visible_successor(&a, root);
    }
    JsWindowId last = root->window_id; bool quit = false; tab_close(&a, a.ti, &quit);
    CHECK("closing the last native tab retires its ID before process teardown", quit && js_window_is_closed(last) && !app_windows_pending(&a.windows));
    for (unsigned i = 0; i < APP_WINDOW_LIMIT; i++) if (a.windows.slots[i].reserved) js_window_destroy(a.windows.slots[i].id);
    for (int i = 0; i < a.ntabs; i++) tab_free(&a, a.tabs[i]);
    a.ntabs = 0; app_windows_clear(&a.windows);
    size_t count = 1, bytes = 1; js_windows_pending(&count, &bytes);
    CHECK("native teardown leaves no message or reservation ownership", !count && !bytes && !app_windows_count(&a.windows));
    js_release_pins(); CHECK("worker drain finishes before native subsystem teardown", js_workers_shutdown(5000));
    while (g_nhv) link_del(g_hv, &g_nhv, g_nhv - 1);
    net_wakeup = media_wakeup = js_wakeup = NULL; net_shutdown();
    SDL_DestroyMutex(icache_mu); icache_mu = NULL; SDL_DestroyWindow(a.win); SDL_Quit(); g_app = NULL;
    printf("RESULTS app-windows %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
