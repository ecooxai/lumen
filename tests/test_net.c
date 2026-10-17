/* Cookie SameSite/prefix rules, site comparison and HSTS (no network). */
#include "../src/net/net.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static bool has(const char *jar, const char *name) { return jar && strstr(jar, name); }
static char *get(const char *url, bool cross) { URL u; url_parse(url, &u); char *r = cookies_get(&u, true, cross); url_free(&u); return r; }
static void set(const char *url, const char *sc, bool cross) { URL u; url_parse(url, &u); cookies_set_from_header(&u, sc, cross); url_free(&u); }
int main(void) {
    CHECK(net_same_site("www.google.com", "accounts.google.com"));
    CHECK(!net_same_site("google.com", "youtube.com"));
    CHECK(net_same_site("a.bbc.co.uk", "www.bbc.co.uk"));
    CHECK(!net_same_site("bbc.co.uk", "itv.co.uk"));
    CHECK(!net_same_site("alice.github.io", "bob.github.io"));
    CHECK(!net_same_site("127.0.0.1", "127.0.0.2"));
    CHECK(net_url_same_site("https://m.youtube.com/x", "https://www.youtube.com/"));
    CHECK(!net_url_same_site("https://accounts.youtube.com/", "https://accounts.google.com/"));

    set("https://a.example/", "lax=1", false);
    set("https://a.example/", "strict=1; SameSite=Strict", false);
    set("https://a.example/", "none=1; SameSite=None; Secure", false);
    set("https://a.example/", "insecure_none=1; SameSite=None", false);
    set("https://a.example/", "__Secure-x=1", false);
    set("https://a.example/", "__Host-y=1; Secure; Path=/sub", false);
    set("https://a.example/", "__Host-z=1; Secure; Path=/", false);
    set("https://a.example/", "xsite=1", true);
    set("https://a.example/", "xsite_none=1; SameSite=None; Secure", true);
    char *same = get("https://a.example/", false), *cross = get("https://a.example/", true);
    CHECK(has(same, "lax=1") && has(same, "strict=1") && has(same, "none=1") && has(same, "__Host-z=1") && has(same, "xsite_none=1"));
    CHECK(!has(same, "insecure_none") && !has(same, "__Secure-x") && !has(same, "__Host-y") && !has(same, "xsite=1"));
    CHECK(has(cross, "none=1") && !has(cross, "lax=1") && !has(cross, "strict=1"));
    free(same); free(cross);
    char *d = cookies_get_document("https://a.example/", "https://other.test/");
    CHECK(has(d, "none=1") && !has(d, "lax=1")); free(d);
    d = cookies_get_document("https://a.example/", "https://www.a.example/");
    CHECK(has(d, "lax=1")); free(d);

    URL u; url_parse("https://secure.test/", &u);
    hsts_note(&u, "max-age=31536000; includeSubDomains"); url_free(&u);
    char *up = hsts_upgrade("http://secure.test/a?b"); CHECK(up && !strcmp(up, "https://secure.test/a?b")); free(up);
    up = hsts_upgrade("http://x.secure.test:80/"); CHECK(up && !strncmp(up, "https://x.secure.test/", 22)); free(up);
    CHECK(!hsts_upgrade("http://other.test/"));
    url_parse("http://plain.test/", &u); hsts_note(&u, "max-age=100"); url_free(&u);   /* ignored over http */
    CHECK(!hsts_upgrade("http://plain.test/"));
    url_parse("https://secure.test/", &u); hsts_note(&u, "max-age=0"); url_free(&u);
    CHECK(!hsts_upgrade("http://secure.test/"));
    up = net_secure_upgrade("https://page.test/", "http://cdn.test/a.png"); CHECK(up && !strcmp(up, "https://cdn.test/a.png")); free(up);
    up = net_secure_upgrade("https://page.test/", "ws://chat.test/s"); CHECK(up && !strcmp(up, "wss://chat.test/s")); free(up);
    CHECK(!net_secure_upgrade("https://page.test/", "http://localhost:8000/x"));
    CHECK(!net_secure_upgrade("http://page.test/", "http://cdn.test/a.png"));
    CHECK(!net_secure_upgrade(NULL, "http://cdn.test/a.png"));
    CHECK(!net_secure_upgrade("https://page.test/", "https://cdn.test/a.png"));
    if (fails) return 1;
    printf("net tests ok\n");
    return 0;
}
