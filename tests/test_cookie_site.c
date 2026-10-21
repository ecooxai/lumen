/* Site info's "Delete data": a host's site (its registrable domain, approximated) and the site's
 * cookies, counted and deleted with those of its subdomains, others kept. */
#include "../src/net/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

static bool site_is(const char *host, const char *want) {
    char site[256]; net_site_of(host, site, sizeof site);
    if (strcmp(site, want)) fprintf(stderr, "%s -> %s, want %s\n", host, site, want);
    return !strcmp(site, want);
}

int main(void) {
    CHECK(site_is("www.google.com", "google.com"));
    CHECK(site_is("Mail.Google.COM", "google.com"));
    CHECK(site_is("chatgpt.com", "chatgpt.com"));
    CHECK(site_is("www.bbc.co.uk", "bbc.co.uk"));
    CHECK(site_is("a.b.example.com.au", "example.com.au"));
    CHECK(site_is("www.example.de", "example.de"));
    CHECK(site_is("co.uk", "co.uk"));
    CHECK(site_is("localhost", "localhost"));
    CHECK(site_is("192.168.1.20", "192.168.1.20"));
    CHECK(site_is("::1", "::1"));

    cookies_set_document("https://www.google.com/", "a=1");
    cookies_set_document("https://www.google.com/", "b=2; domain=google.com");
    cookies_set_document("https://accounts.google.com/", "c=3");
    cookies_set_document("https://notgoogle.com/", "d=4");
    cookies_set_document("https://example.com/", "e=5");
    CHECK(cookies_site("google.com", false) == 3);
    CHECK(cookies_site("example.com", false) == 1);
    CHECK(cookies_site("google.com", true) == 3);
    CHECK(cookies_site("google.com", false) == 0);
    char *left = cookies_get_document("https://www.google.com/");
    CHECK(left && !*left);
    free(left);
    left = cookies_get_document("https://notgoogle.com/");
    CHECK(left && !strcmp(left, "d=4"));
    free(left);
    CHECK(cookies_site("example.com", false) == 1 && cookies_site("", true) == 0);
    puts("ok");
    return 0;
}
