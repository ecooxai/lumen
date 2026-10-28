#include "../src/base/url.h"
#include <stdio.h>
#include <string.h>

static size_t passed, total;
static void check(bool result, const char *name) { total++; if (result) passed++; else fprintf(stderr, "FAIL URL host NUL: %s\n", name); }
static void parse_case(const char *input, size_t length, const char *expected, const char *name) {
    URL u; bool parsed = url_parse_n(input, length, NULL, &u);
    if (!parsed) { check(expected == NULL, name); return; }
    char *serialized = url_to_string(&u);
    check(expected && !strcmp(serialized, expected), name);
    free(serialized); url_free(&u);
}
int main(void) {
    static const char *const schemes[] = {"http", "https", "ws", "wss", "ftp", "file"};
    static const char *const bad_hosts[] = {"host%00", "ho%00st", "%00host", "host%00.example", "127.0.0.1%00"};
    char url[256];
    for (size_t i = 0; i < sizeof schemes / sizeof schemes[0]; i++) {
        for (size_t j = 0; j < sizeof bad_hosts / sizeof bad_hosts[0]; j++) {
            snprintf(url, sizeof url, "%s://%s/path", schemes[i], bad_hosts[j]);
            parse_case(url, strlen(url), NULL, "encoded NUL in special host");
        }
        int prefix = snprintf(url, sizeof url, "%s://ho", schemes[i]);
        memcpy(url + prefix, "\0st/path", 8);
        parse_case(url, (size_t)prefix + 8, NULL, "literal NUL in special host");
    }
    static const char *const valid[][2] = {
        {"http://example.com/a%00b", "http://example.com/a%00b"},
        {"http://example.com/?q=a%00b", "http://example.com/?q=a%00b"},
        {"http://example.com/#a%00b", "http://example.com/#a%00b"},
        {"http://a%00b:p%00q@example.com/", "http://a%00b:p%00q@example.com/"},
        {"custom://a%00b/path", "custom://a%00b/path"},
        {"custom://%00/path", "custom://%00/path"},
        {"custom://host%2500/path", "custom://host%2500/path"},
        {"data:text/plain,a%00b", "data:text/plain,a%00b"},
        {"mailto:a%00b@example.com", "mailto:a%00b@example.com"},
        {"https://b%C3%BCcher.example/", "https://xn--bcher-kva.example/"},
        {"http://%65xample.com/", "http://example.com/"},
        {"file:///a%00b", "file:///a%00b"}
    };
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; i++) parse_case(valid[i][0], strlen(valid[i][0]), valid[i][1], "valid held-out URL");
    parse_case("https://b%C3%BC%00cher.example/", strlen("https://b%C3%BC%00cher.example/"), NULL, "IDNA host plus NUL");
    parse_case("http://a%2500b/", strlen("http://a%2500b/"), NULL, "encoded percent remains forbidden in special host");
    const char raw_path[] = "https://example.com/a\0b";
    parse_case(raw_path, sizeof raw_path - 1, "https://example.com/a%00b", "literal NUL in path is encoded");
    const char raw_opaque[] = "custom://a\0b/path";
    parse_case(raw_opaque, sizeof raw_opaque - 1, NULL, "literal opaque-host NUL remains forbidden");
    const char *const keys[] = {"host", "hostname"};
    for (size_t key = 0; key < 2; key++) {
        for (size_t i = 0; i < sizeof bad_hosts / sizeof bad_hosts[0]; i++) {
            URL u;
            if (!url_parse("https://old.example:8443/path?q=1#x", &u)) { fprintf(stderr, "URL setter setup failed\n"); return 1; }
            url_set(&u, keys[key], bad_hosts[i], strlen(bad_hosts[i]));
            char *serialized = url_to_string(&u);
            check(!strcmp(serialized, "https://old.example:8443/path?q=1#x"), "failed host setter is atomic");
            free(serialized); url_free(&u);
        }
    }
    /* Host parsing failure precedes assignment, but a valid host may be
       retained when the following port state fails. Keep those boundaries. */
    static const char *const setter_cases[][4] = {
        {"http://old.example:8080/p?q=1#x", "host", "host%00", "http://old.example:8080/p?q=1#x"},
        {"http://old.example:8080/p?q=1#x", "hostname", "host%00", "http://old.example:8080/p?q=1#x"},
        {"file://old.example/p?q=1#x", "host", "host%00", "file://old.example/p?q=1#x"},
        {"file://old.example/p?q=1#x", "hostname", "host%00", "file://old.example/p?q=1#x"},
        {"custom://old.example:8080/p?q=1#x", "host", "host%00", "custom://host%00:8080/p?q=1#x"},
        {"custom://old.example:8080/p?q=1#x", "hostname", "host%00", "custom://host%00:8080/p?q=1#x"},
        {"https://old.example:8443/path?q=1#x", "host", "new.example:70000", "https://new.example:8443/path?q=1#x"},
        {"https://old.example:8443/path?q=1#x", "host", "new.example:abc", "https://new.example:8443/path?q=1#x"},
        {"https://old.example:8443/path?q=1#x", "host", "new.example:", "https://new.example:8443/path?q=1#x"},
        {"https://old.example:8443/path?q=1#x", "hostname", "new.example:123", "https://old.example:8443/path?q=1#x"},
        {"https://old.example:8443/path?q=1#x", "host", "bad%00:123", "https://old.example:8443/path?q=1#x"},
        {"https://old.example:8443/path?q=1#x", "port", "70000", "https://old.example:8443/path?q=1#x"},
        {"https://old.example:8443/path?q=1#x", "port", "123abc", "https://old.example:123/path?q=1#x"},
        {"https://old.example:8443/path?q=1#x", "port", "", "https://old.example/path?q=1#x"},
        {"file://old.example/path", "host", "localhost", "file:///path"},
        {"file://old.example/path", "hostname", "localhost:80", "file://old.example/path"},
        {"custom://old.example:8080/path", "host", "new%00host:70000", "custom://new%00host:8080/path"}
    };
    for (size_t i = 0; i < sizeof setter_cases / sizeof setter_cases[0]; i++) {
        URL u;
        if (!url_parse(setter_cases[i][0], &u)) return 1;
        url_set(&u, setter_cases[i][1], setter_cases[i][2], strlen(setter_cases[i][2]));
        char *serialized = url_to_string(&u);
        check(!strcmp(serialized, setter_cases[i][3]), setter_cases[i][2]);
        free(serialized); url_free(&u);
    }
    const char *const raw_setter_bases[] = {"http://old.example:8080/p?q=1#x", "file://old.example/p?q=1#x", "custom://old.example:8080/p?q=1#x"};
    const char raw_setter_host[] = "ho\0st";
    for (size_t i = 0; i < 3; i++) for (size_t key = 0; key < 2; key++) {
        URL u;
        if (!url_parse(raw_setter_bases[i], &u)) return 1;
        url_set(&u, keys[key], raw_setter_host, sizeof raw_setter_host - 1);
        char *serialized = url_to_string(&u);
        check(!strcmp(serialized, raw_setter_bases[i]), "literal NUL setter preserves old URL");
        free(serialized); url_free(&u);
    }
    char *decoded = url_decode("a%00b%FF", 8);
    check(decoded[0] == 'a' && decoded[1] == 0 && decoded[2] == 'b' && (unsigned char)decoded[3] == 255 && decoded[4] == 0, "exported percent decoder still preserves binary bytes");
    free(decoded);
    printf("URL host NUL native controls: %zu/%zu PASS\n", passed, total);
    return passed == total ? 0 : 1;
}
