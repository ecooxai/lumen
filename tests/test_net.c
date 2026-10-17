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
    {   /* Referrer-Policy */
        struct { const char *doc, *tgt, *pol, *want; } t[] = {
            { "https://a.test/p?q#f", "https://a.test/x", NULL, "https://a.test/p?q" },
            { "https://a.test/p?q", "https://b.test/x", NULL, "https://a.test/" },
            { "https://a.test/p", "http://b.test/x", NULL, NULL },
            { "https://u:pw@a.test:443/p", "https://a.test/x", "unsafe-url", "https://a.test/p" },
            { "https://a.test/p", "https://b.test/", "no-referrer", NULL },
            { "https://a.test/p", "https://b.test/", "same-origin", NULL },
            { "https://a.test/p", "https://a.test/", "same-origin", "https://a.test/p" },
            { "https://a.test/p", "https://a.test/", "origin", "https://a.test/" },
            { "https://a.test/p", "http://b.test/", "no-referrer-when-downgrade", NULL },
            { "https://a.test/p", "https://b.test/", "no-referrer-when-downgrade", "https://a.test/p" },
            { "https://a.test/p", "http://b.test/", "strict-origin", NULL },
            { "http://a.test:8080/p", "http://b.test/", "origin-when-cross-origin", "http://a.test:8080/" },
            { "https://a.test/p", "https://b.test/", "bogus, unsafe-url, nonsense", "https://a.test/p" },
            { "file:///x", "https://b.test/", "unsafe-url", NULL },
        };
        for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
            char *r = net_referrer(t[i].doc, t[i].tgt, t[i].pol);
            bool ok = t[i].want ? r && !strcmp(r, t[i].want) : !r;
            if (!ok) printf("referrer case %zu: got %s\n", i, r ? r : "(null)");
            CHECK(ok); free(r);
        }
    }
    {   /* CSP script-src */
        const char *d = "https://a.test/p";
        CHECK(csp_allows_script(NULL, d, NULL, NULL, "x", 1, true));
        CHECK(csp_allows_script("img-src 'none'", d, NULL, NULL, "x", 1, true));
        CHECK(!csp_allows_script("script-src 'self'", d, NULL, NULL, "x", 1, true));
        CHECK(csp_allows_script("script-src 'self' 'unsafe-inline'", d, NULL, NULL, "x", 1, true));
        CHECK(!csp_allows_script("script-src 'unsafe-inline' 'nonce-abc'", d, NULL, NULL, "x", 1, true));
        CHECK(csp_allows_script("script-src 'nonce-abc'", d, NULL, "abc", "x", 1, true));
        CHECK(!csp_allows_script("script-src 'nonce-abc'", d, NULL, "abd", "x", 1, true));
        CHECK(csp_allows_script("script-src 'sha256-bhHHL3z2vDgxUt0W3dWQOrprscmda2Y5pLsLg4GF+pI='", d, NULL, NULL, "alert(1)", 8, true));
        CHECK(!csp_allows_script("script-src 'sha256-bhHHL3z2vDgxUt0W3dWQOrprscmda2Y5pLsLg4GF+pI='", d, NULL, NULL, "alert(2)", 8, true));
        const char *pol = "default-src 'none'; script-src 'self' https://cdn.test *.ok.test";
        CHECK(csp_allows_script(pol, d, "https://a.test/x.js", NULL, NULL, 0, true));
        CHECK(csp_allows_script(pol, d, "https://cdn.test/y.js", NULL, NULL, 0, true));
        CHECK(csp_allows_script(pol, d, "https://s.ok.test/y.js", NULL, NULL, 0, true));
        CHECK(!csp_allows_script(pol, d, "https://ok.test/y.js", NULL, NULL, 0, true));
        CHECK(!csp_allows_script(pol, d, "https://evil.test/z.js", NULL, NULL, 0, true));
        CHECK(!csp_allows_script(pol, d, "https://cdn.test:8443/y.js", NULL, NULL, 0, true));
        CHECK(csp_allows_script("default-src https:", d, "https://any.test/y.js", NULL, NULL, 0, true));
        CHECK(!csp_allows_script("default-src https:", d, "http://any.test/y.js", NULL, NULL, 0, true));
        CHECK(!csp_allows_script("script-src 'nonce-n' 'strict-dynamic' https://cdn.test", d, "https://cdn.test/y.js", NULL, NULL, 0, true));
        CHECK(csp_allows_script("script-src 'nonce-n' 'strict-dynamic'", d, "https://e.test/y.js", NULL, NULL, 0, false));
        CHECK(csp_allows_script("script-src 'nonce-n' 'strict-dynamic'", d, "https://e.test/y.js", "n", NULL, 0, true));
        char *two = csp_append(csp_append(NULL, "script-src *"), "script-src 'self', img-src *");
        CHECK(csp_allows_script(two, d, "https://a.test/x.js", NULL, NULL, 0, true));
        CHECK(!csp_allows_script(two, d, "https://b.test/x.js", NULL, NULL, 0, true));
        free(two);
    }
    {   /* CORP / COEP */
        const char *d = "https://a.test/p";
        CHECK(!net_corp_blocked(d, "https://a.test/i.png", "same-origin", true));
        CHECK(net_corp_blocked(d, "https://cdn.a.test/i.png", "same-origin", false));
        CHECK(!net_corp_blocked(d, "https://cdn.a.test/i.png", "same-site", false));
        CHECK(net_corp_blocked(d, "https://b.test/i.png", "same-site", false));
        CHECK(net_corp_blocked(d, "http://cdn.a.test/i.png", " same-site ", false));
        CHECK(!net_corp_blocked(d, "https://b.test/i.png", "cross-origin", true));
        CHECK(!net_corp_blocked(d, "https://b.test/i.png", NULL, false));
        CHECK(net_corp_blocked(d, "https://b.test/i.png", NULL, true));
        CHECK(!net_corp_blocked("about:blank", "https://b.test/i.png", "same-origin", true));
        CHECK(csp_allows_attr(NULL, "x", 1));
        CHECK(!csp_allows_attr("script-src 'self'", "x", 1));
        CHECK(csp_allows_attr("script-src 'unsafe-inline'", "x", 1));
        CHECK(!csp_allows_attr("script-src 'unsafe-inline' 'nonce-a'", "x", 1));
        CHECK(csp_allows_attr("script-src 'nonce-a'; script-src-attr 'unsafe-inline'", "x", 1));
        CHECK(!csp_allows_attr("script-src-attr 'none'", "x", 1));
        CHECK(!csp_allows_attr("script-src 'sha256-LXEWQrcmsEQBYnyp+6wy9chTD7GQPMTbAiWHF5IaSIE='", "x", 1));
        CHECK(csp_allows_attr("script-src 'unsafe-hashes' 'sha256-LXEWQrcmsEQBYnyp+6wy9chTD7GQPMTbAiWHF5IaSIE='", "x", 1));
        CHECK(!csp_allows_attr("script-src 'unsafe-hashes' 'sha256-LXEWQrcmsEQBYnyp+6wy9chTD7GQPMTbAiWHF5IaSIE='", "y", 1));
        CHECK(csp_allows_script("script-src-attr 'none'", "https://a.test/", NULL, NULL, "x", 1, true));
        CHECK(csp_allows_eval(NULL, false));
        CHECK(csp_allows_eval("img-src *", false));
        CHECK(!csp_allows_eval("script-src 'self'", false));
        CHECK(csp_allows_eval("script-src 'unsafe-eval'", false));
        CHECK(!csp_allows_eval("default-src 'self'", true));
        CHECK(csp_allows_eval("script-src 'wasm-unsafe-eval'", true));
        CHECK(!csp_allows_eval("script-src 'wasm-unsafe-eval'", false));
        CHECK(!csp_allows_eval("script-src 'unsafe-eval'\nscript-src 'self'", false));
    }
    if (fails) return 1;
    printf("net tests ok\n");
    return 0;
}
