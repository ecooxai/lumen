/* Host-only is part of cookie uniqueness (6265bis storage model).
 * Uses only synthetic values in memory and an optional test-local roundtrip file. */
#include "../src/net/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;
static void check(const char *name, bool good) {
    checks++; if (!good) failed++;
    printf("{\"case\":\"%s\",\"pass\":%s}\n", name, good ? "true" : "false");
}
static char *wire(const char *address) {
    URL url; if (!url_parse(address, &url)) abort();
    char *s = cookies_get(&url, true); url_free(&url);
    return s ? s : xstrdup("");
}
static bool token(const char *s, const char *want) {
    size_t n = strlen(want);
    while (*s) {
        while (*s == ' ') s++;
        const char *end = strchr(s, ';'); if (!end) end = s + strlen(s);
        if ((size_t)(end - s) == n && !memcmp(s, want, n)) return true;
        s = *end ? end + 1 : end;
    }
    return false;
}
static int count(const char *s) {
    int n = *s ? 1 : 0; for (; *s; s++) n += *s == ';'; return n;
}
static bool state(const char *first, const char *second, const char *subdomain) {
    char *root = wire("https://x.com/"), *sub = wire("https://jf.x.com/");
    int wanted = (first != NULL) + (second != NULL);
    bool good = count(root) == wanted && (!first || token(root, first)) && (!second || token(root, second)) &&
        (subdomain ? count(sub) == 1 && token(sub, subdomain) : !*sub);
    free(root); free(sub); return good;
}
static bool root_order(const char *want) {
    char *s = wire("https://x.com/"); bool good = !strcmp(s, want); free(s); return good;
}
static bool visible(const char *one) {
    char *s = cookies_get_document("https://x.com/");
    bool good = one ? count(s) == 1 && token(s, one) : !*s;
    free(s); return good;
}
static void doc(const char *s) { cookies_set_document("https://x.com/", s); }
static void http(const char *s) {
    URL url; if (!url_parse("https://x.com/", &url)) abort();
    cookies_set_from_header(&url, s); url_free(&url);
}
static void both(bool domain_first) {
    cookies_clear();
    if (domain_first) { doc("scope=domain; Domain=x.com; Path=/; Secure"); doc("scope=host; Path=/; Secure"); }
    else { doc("scope=host; Path=/; Secure"); doc("scope=domain; Domain=x.com; Path=/; Secure"); }
}
int main(int argc, char **argv) {
    both(true); check("domain_then_host_coexist_and_domain_reaches_subdomain", state("scope=domain", "scope=host", "scope=domain"));
    both(false); check("host_then_domain_coexist_and_domain_reaches_subdomain", state("scope=domain", "scope=host", "scope=domain"));
    both(true); doc("scope=gone; Path=/; Max-Age=0"); check("host_delete_preserves_domain_scope", state("scope=domain", NULL, "scope=domain"));
    both(false); doc("scope=gone; Domain=x.com; Path=/; Max-Age=0"); check("domain_delete_preserves_host_scope", state("scope=host", NULL, NULL));
    both(true); doc("scope=host2; Path=/; Secure"); check("host_overwrite_preserves_domain_scope", state("scope=domain", "scope=host2", "scope=domain"));
    both(false); doc("scope=domain2; Domain=x.com; Path=/; Secure"); check("domain_overwrite_preserves_host_scope", state("scope=domain2", "scope=host", "scope=domain2"));
    both(true); doc("scope=domain2; Domain=.x.com; Path=/; Secure"); check("leading_dot_is_same_domain_scope", state("scope=domain2", "scope=host", "scope=domain2"));
    both(true); doc("scope=domain2; Domain=x.com; Path=/; Secure");
    check("changed_domain_value_order_matches_chromium", root_order("scope=host; scope=domain2"));
    both(false); doc("scope=host2; Path=/; Secure");
    check("changed_host_value_order_matches_chromium", root_order("scope=domain; scope=host2"));
    cookies_clear(); http("scope=private; Domain=x.com; Path=/; Secure; HttpOnly"); doc("scope=host; Path=/; Secure");
    check("domain_httponly_allows_distinct_host_only_cookie", state("scope=private", "scope=host", "scope=private") && visible("scope=host"));
    cookies_clear(); http("scope=private; Path=/; Secure; HttpOnly"); doc("scope=domain; Domain=x.com; Path=/; Secure");
    check("host_httponly_allows_distinct_domain_cookie", state("scope=private", "scope=domain", "scope=domain") && visible("scope=domain"));
    cookies_clear(); http("scope=private; Domain=x.com; Path=/; Secure; HttpOnly"); doc("scope=changed; Domain=x.com; Path=/; Secure");
    check("same_domain_httponly_still_protected", state("scope=private", NULL, "scope=private") && visible(NULL));
    cookies_clear(); http("scope=private; Path=/; Secure; HttpOnly"); doc("scope=changed; Path=/; Secure");
    check("same_host_httponly_still_protected", state("scope=private", NULL, NULL) && visible(NULL));
    if (argc == 2) {
        both(true); bool saved = cookies_save(argv[1]); cookies_clear(); cookies_load(argv[1]);
        check("roundtrip_retains_both_scopes", saved && state("scope=domain", "scope=host", "scope=domain"));
        doc("scope=host2; Path=/; Secure"); check("loaded_host_update_keeps_domain_scope", state("scope=domain", "scope=host2", "scope=domain"));
        doc("scope=gone; Domain=x.com; Path=/; Max-Age=0"); check("loaded_domain_delete_keeps_host_scope", state("scope=host2", NULL, NULL));
        FILE *input = fopen(argv[1], "w"); if (!input) abort();
        fputs("x.com\t0\t/\t1\t0\t-1\tscope\tdomain\nx.com\t1\t/\t1\t0\t-1\tscope\thost\n", input);
        if (fclose(input)) abort();
        cookies_clear(); cookies_load(argv[1]);
        check("imported_scope_flags_remain_distinct", state("scope=domain", "scope=host", "scope=domain"));
        doc("scope=host2; Path=/; Secure");
        check("imported_host_update_preserves_domain_scope", state("scope=domain", "scope=host2", "scope=domain"));
        cookies_clear(); cookies_load(argv[1]); doc("scope=gone; Path=/; Max-Age=0");
        check("imported_host_delete_preserves_domain_scope", state("scope=domain", NULL, "scope=domain"));
        remove(argv[1]);
    }
    cookies_clear();
    printf("{\"checks\":%d,\"passed\":%d,\"failed\":%d,\"account_free\":true,\"network_requests\":0}\n", checks, checks - failed, failed);
    return failed ? 1 : 0;
}
