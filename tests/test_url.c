#include "../src/base/url.h"
#include <assert.h>
static void t(const char *base, const char *rel, const char *exp) {
    char *r = url_join(base, rel);
    if (!r || strcmp(r, exp)) { printf("FAIL %s + %s => %s (want %s)\n", base, rel, r ? r : "(null)", exp); exit(1); }
    free(r);
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
    printf("url tests ok\n");
    return 0;
}
