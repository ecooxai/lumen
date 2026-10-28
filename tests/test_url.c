#include "../src/base/url.h"
#include <assert.h>
static void t(const char *base, const char *rel, const char *exp) {
    char *r = url_join(base, rel);
    if (!r || strcmp(r, exp)) { printf("FAIL %s + %s => %s (want %s)\n", base, rel, r ? r : "(null)", exp); exit(1); }
    free(r);
}
typedef struct {
    const char *input;
    const char *href;   /* NULL means parsing must fail. */
    const char *origin;
} Case;

static const Case cases[] = {
    {"https://xn--ab-j1t/x", "https://xn--ab-j1t/x", "https://xn--ab-j1t"},
    {"https://xn--ab-m1t/x", "https://xn--ab-m1t/x", "https://xn--ab-m1t"},
    {"https://xn--u-ccb/x", "https://xn--u-ccb/x", "https://xn--u-ccb"},
    {"https://XN--AB-J1T/x", "https://xn--ab-j1t/x", "https://xn--ab-j1t"},
    {"https://%78n--ab-j1t/x", "https://xn--ab-j1t/x", "https://xn--ab-j1t"},
    {"https://xn--/x", "https://xn--/x", "https://xn--"},
    {"https://b\xc3\xbc" "cher.example/x", "https://xn--bcher-kva.example/x", "https://xn--bcher-kva.example"},
    {"non-special://test:@test/x", "non-special://test@test/x", "null"},
    {"non-special://:@test/x", "non-special://test/x", "null"},
    {"foo://", "foo://", "null"},
    {"foo:/path", "foo:/path", "null"},
    {"file:///path", "file:///path", "null"},
    {"data:text/plain,hello", "data:text/plain,hello", "null"},
    {"https://example.test:443/a", "https://example.test/a", "https://example.test"},
    {"https://example.test:8443/a", "https://example.test:8443/a", "https://example.test:8443"},
    {"ftp://example.test/a", "ftp://example.test/a", "ftp://example.test"},
    {"ws://example.test/a", "ws://example.test/a", "ws://example.test"},
    {"wss://example.test/a", "wss://example.test/a", "wss://example.test"},
    {"blob:https://example.com:443/", "blob:https://example.com:443/", "https://example.com"},
    {"blob:http://example.org:88/", "blob:http://example.org:88/", "http://example.org:88"},
    {"blob:ftp://example.org/", "blob:ftp://example.org/", "null"},
    {"blob:blob:https://example.org/", "blob:blob:https://example.org/", "null"},
    {"blob:file:///path", "blob:file:///path", "null"},
    {"blob:invalid", "blob:invalid", "null"},
    {"http://f:b/c", NULL, NULL},
    {"http://f: /c", NULL, NULL},
    {"http://f:fifty-two/c", NULL, NULL},
    {"file://example:1/", NULL, NULL},
    {"file://example:test/", NULL, NULL},
    {"file://example%/", NULL, NULL},
    {"https://xn--ab-j1t%20.example/x", NULL, NULL},
    {"https://example%25.test/x", NULL, NULL},
    {"https://example%2f.test/x", NULL, NULL},
    {"https://example%7f.test/x", NULL, NULL},
    {"https://example%01.test/x", NULL, NULL},
};

static int compatibility_regressions(void) {
    unsigned passed = 0;
    const unsigned count = (unsigned)(sizeof cases / sizeof cases[0]);
    for (unsigned i = 0; i < count; ++i) {
        const Case *test = &cases[i];
        URL url;
        bool parsed = url_parse(test->input, &url);
        bool ok = parsed == (test->href != NULL);
        if (parsed) {
            char *href = url_to_string(&url);
            char *origin = url_origin(&url);
            ok = ok && href && origin && !strcmp(href, test->href) && !strcmp(origin, test->origin);
            if (!ok) fprintf(stderr, "FAIL %u: %s -> href=%s origin=%s\n", i + 1, test->input, href ? href : "(null)", origin ? origin : "(null)");
            free(href);
            free(origin);
            url_free(&url);
        } else if (!ok) {
            fprintf(stderr, "FAIL %u: rejected %s\n", i + 1, test->input);
        }
        if (ok) ++passed;
    }
    printf("native URL compatibility: %u/%u cases passed\n", passed, count);
    return passed == count ? 0 : 1;
}

int main(void) {
    t(NULL, "https://www.YouTube.com", "https://www.youtube.com/");
    t("https://a.com/b/c/d?x=1", "../e", "https://a.com/b/e");
    t("https://a.com/b/c/d?x=1", "/z?q", "https://a.com/z?q");
    t("https://a.com/b/c/d?x=1", "//cdn.x.org/s.js", "https://cdn.x.org/s.js");
    t("https://a.com/b/c/d?x=1", "?y=2", "https://a.com/b/c/d?y=2");
    t("https://a.com/b/c/d?x=1", "#f", "https://a.com/b/c/d?x=1#f");
    t("https://a.com:443/b/", "./x/./y/../z", "https://a.com/b/x/z");
    t("http://a.com:8080/", "x", "http://a.com:8080/x");
    t("https://a.com/", "data:text/html,hi", "data:text/html,hi");
    t("file:///Users/x/a.html", "b.css", "file:///Users/x/b.css");
    t("file:///Users/x/a.html", "/c.css", "file:///c.css");
    if (compatibility_regressions()) return 1;
    printf("url tests ok\n");
    return 0;
}
