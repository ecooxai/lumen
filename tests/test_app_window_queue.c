#include "../src/app/window_queue.h"
#include <stdio.h>

static int checks, failures;
#define CHECK(label, condition) do { checks++; if (!(condition)) { failures++; fprintf(stderr, "FAIL: %s (line %d)\n", label, __LINE__); } } while (0)
#define REQUIRE(label, condition) do { CHECK(label, condition); if (failures) goto done; } while (0)

int main(void) {
    AppWindowQueue q = {0}; AppWindowBatch batch = {0};
    CHECK("empty queue has no native slots", app_windows_count(&q) == 0);
    CHECK("zero is never a tracked window identity", !app_windows_track(&q, 0));
    REQUIRE("user tab occupies a native slot", app_windows_track(&q, 1));
    CHECK("tracking is distinct from queuing a command", !app_windows_pending(&q));
    CHECK("duplicate ID cannot occupy another native slot", !app_windows_track(&q, 1));
    CHECK("tracked user tab is already materialized", !app_windows_find(&q, 1)->reserved);

    char url[80] = "https://login.example/initial";
    char ref[80] = "https://app.example/";
    JsWindowOpen open = { .id = 2, .source = 900, .source_top = 1, .opener = 900,
        .name = "authentication", .url = url, .referrer = ref };
    REQUIRE("popup reserves capacity synchronously", app_windows_open(&q, &open));
    CHECK("pending popup is counted before native creation", app_windows_count(&q) == 2);
    CHECK("source top identity survives an iframe caller", app_windows_find(&q, 2)->source_top == 1);
    CHECK("popup reservation stays pending", app_windows_find(&q, 2)->reserved && app_windows_pending(&q));
    CHECK("duplicate pending popup cannot reserve twice", !app_windows_open(&q, &open));
    strcpy(url, "changed caller buffer"); strcpy(ref, "changed caller buffer");
    CHECK("host owns copied destination", !strcmp(app_windows_find(&q, 2)->url, "https://login.example/initial"));
    CHECK("host owns copied referrer", !strcmp(app_windows_find(&q, 2)->referrer, "https://app.example/"));

    for (int i = 0; i < 10000; i++) {
        snprintf(url, sizeof url, "https://login.example/step/%d", i);
        JsWindowNavigate nav = {2, url, i & 1 ? NULL : "https://app.example/", (i & 1) != 0};
        if (!app_windows_navigate(&q, &nav)) { CHECK("repeated location requests remain admissible", false); goto done; }
        if (!app_windows_focus(&q, i & 1 ? 1 : 2)) { CHECK("repeated focus requests remain admissible", false); goto done; }
    }
    CHECK("many requests still use exactly two slots", app_windows_count(&q) == 2);
    app_windows_take(&q, &batch);
    REQUIRE("navigation updates coalesce with the initial open", batch.count == 1);
    CHECK("snapshot retains real initial blank requirement", batch.items[0].create && batch.items[0].navigate);
    CHECK("snapshot keeps only the final requested location", !strcmp(batch.items[0].url, "https://login.example/step/9999"));
    CHECK("final replace and referrer policy win", batch.items[0].replace && batch.items[0].referrer == NULL);
    CHECK("focus coalesces independently to the final target", batch.focus == 1);
    CHECK("taking commands does not release reserved capacity", app_windows_count(&q) == 2 && app_windows_find(&q, 2)->reserved);
    CHECK("taken requests are no longer pending", !app_windows_pending(&q));

    for (JsWindowId id = 3; id <= APP_WINDOW_LIMIT; id++) {
        open.id = id; open.url = "about:blank"; open.referrer = NULL;
        if (!app_windows_open(&q, &open)) { CHECK("remaining capacity is reservable", false); goto done; }
    }
    CHECK("live plus pending windows reach the precise limit", app_windows_count(&q) == APP_WINDOW_LIMIT);
    open.id = APP_WINDOW_LIMIT + 1;
    CHECK("draining reservation cannot be overbooked by callbacks", !app_windows_open(&q, &open));
    CHECK("user tab creation respects popup reservations too", !app_windows_track(&q, 100));
    CHECK("realizing a reserved popup succeeds", app_windows_realize(&q, 2));
    CHECK("materialization does not consume or free an extra slot", app_windows_count(&q) == APP_WINDOW_LIMIT && !app_windows_find(&q, 2)->reserved);
    CHECK("materialization cannot run twice", !app_windows_realize(&q, 2));

    JsWindowNavigate next = {2, "https://login.example/next", NULL, false};
    CHECK("request created during drain belongs to the next batch", app_windows_navigate(&q, &next));
    CHECK("snapshot URL is unaffected by a later request", !strcmp(batch.items[0].url, "https://login.example/step/9999"));
    CHECK("next request keeps its own URL", !strcmp(app_windows_find(&q, 2)->url, next.url));
    app_windows_batch_free(&batch);
    CHECK("freeing a snapshot clears its ownership", batch.count == 0 && batch.focus == 0);
    app_windows_batch_free(&batch);

    CHECK("popup can request closure", app_windows_close(&q, 2));
    CHECK("closure releases an obsolete navigation payload", app_windows_find(&q, 2)->url == NULL && !app_windows_find(&q, 2)->navigate);
    CHECK("closing window cannot accept another navigation", !app_windows_navigate(&q, &next));
    CHECK("closing window cannot accept focus", !app_windows_focus(&q, 2));
    CHECK("repeated closure is idempotent", app_windows_close(&q, 2));
    app_windows_take(&q, &batch);
    CHECK("closing stays true while the native close is drained", app_windows_find(&q, 2)->closing);
    CHECK("taken close still rejects a later location request", !app_windows_navigate(&q, &next));
    CHECK("all reserved and closing slots survive snapshot transfer", app_windows_count(&q) == APP_WINDOW_LIMIT);
    app_windows_forget(&q, 2);
    CHECK("actual close invalidates native lookup", app_windows_find(&q, 2) == NULL);
    CHECK("actual close releases one capacity slot", app_windows_count(&q) == APP_WINDOW_LIMIT - 1);
    open.id = 500; open.url = "https://app.example/fresh";
    CHECK("a new ID can use the released slot", app_windows_open(&q, &open));
    CHECK("stale close cannot close the newly allocated window", !app_windows_close(&q, 2) && app_windows_find(&q, 500) != NULL);
    app_windows_batch_free(&batch);

    CHECK("pending creation can be closed before materialization", app_windows_close(&q, 500));
    app_windows_take(&q, &batch);
    REQUIRE("cancelled creation remains a single bounded request", batch.count == 1);
    CHECK("drain can see that creation is superseded by closure", batch.items[0].id == 500 && batch.items[0].create && batch.items[0].close);
    CHECK("cancelled creation has no retained navigation text", batch.items[0].url == NULL && batch.items[0].referrer == NULL);
    app_windows_forget(&q, 500); app_windows_batch_free(&batch);

    next = (JsWindowNavigate){1, "https://other.example/", "https://app.example/", false};
    CHECK("existing tab can queue a navigation", app_windows_navigate(&q, &next));
    CHECK("existing tab can queue focus", app_windows_focus(&q, 1));
    CHECK("COOP split replaces a native slot's identity", app_windows_rebind(&q, 1, 600));
    CHECK("old COOP handle is no longer native-addressable", app_windows_find(&q, 1) == NULL && app_windows_find(&q, 600) != NULL);
    CHECK("commands for old group cannot mutate its replacement", !app_windows_pending(&q));
    CHECK("COOP replacement retains its native slot", app_windows_count(&q) == APP_WINDOW_LIMIT - 1);
    CHECK("rebinding cannot alias another live ID", !app_windows_rebind(&q, 600, 3));
    CHECK("same-ID commit is harmless", app_windows_rebind(&q, 600, 600));
    CHECK("unknown native targets are rejected", !app_windows_focus(&q, 9999) && !app_windows_close(&q, 9999) && !app_windows_realize(&q, 9999));
    next.id = 9999; CHECK("unknown navigation cannot create a tab", !app_windows_navigate(&q, &next));
    app_windows_clear(&q);
    CHECK("shutdown clears all real and reserved slots", app_windows_count(&q) == 0 && !app_windows_pending(&q));

done:
    app_windows_batch_free(&batch); app_windows_clear(&q);
    printf("app window queue: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
