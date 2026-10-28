/* General URL encoding boundary regressions. These custom checks are separate
 * from official WPT counts and use the same native parser as the browser. */
#include "../src/base/url.h"

static unsigned checks, failures;
#define CHECK(name, condition) do { ++checks; if (!(condition)) { ++failures; \
    fprintf(stderr, "FAIL %s\n", name); } } while (0)

typedef struct {
    const char *name, *input;
    size_t length;
    WebEncoding encoding;
    const char *expected;
} Case;
#define VECTOR(name, input, encoding, expected) \
    {name, input, sizeof(input) - 1, encoding, expected}

static bool href_is(const URL *url, const char *expected) {
    char *href = url_to_string(url);
    bool same = href && !strcmp(href, expected);
    if (!same) fprintf(stderr, "  actual: %s\n  expected: %s\n", href ? href : "(null)", expected);
    free(href);
    return same;
}

int main(void) {
    const Case cases[] = {
        VECTOR("HTTP uses legacy query but UTF8 fragment", "http://example.test/?\u2020#\u2020",
               WEB_ENC_WINDOWS_1252, "http://example.test/?%86#%E2%80%A0"),
        VECTOR("HTTPS leaves credentials and path in UTF8", "https://\u00E9@example.test/\u00E9?\u00E9",
               WEB_ENC_WINDOWS_1252, "https://%C3%A9@example.test/%C3%A9?%E9"),
        VECTOR("FTP is a special legacy query consumer", "ftp://example.test/?\u00E9",
               WEB_ENC_WINDOWS_1252, "ftp://example.test/?%E9"),
        VECTOR("File is a special legacy query consumer", "file:///tmp/a?\u00E9",
               WEB_ENC_WINDOWS_1252, "file:///tmp/a?%E9"),
        VECTOR("WS forces UTF8 despite document encoding", "ws://example.test/?\u00E9",
               WEB_ENC_WINDOWS_1252, "ws://example.test/?%C3%A9"),
        VECTOR("WSS forces UTF8 despite document encoding", "wss://example.test/?\u00E9",
               WEB_ENC_WINDOWS_1252, "wss://example.test/?%C3%A9"),
        VECTOR("Opaque data query uses UTF8", "data:text/plain,a?\u00E9",
               WEB_ENC_WINDOWS_1252, "data:text/plain,a?%C3%A9"),
        VECTOR("Non-special authority query uses UTF8", "custom://example.test/a?\u00E9",
               WEB_ENC_WINDOWS_1252, "custom://example.test/a?%C3%A9"),
        VECTOR("Query bytes retain existing percent escapes", "http://example.test/?%E9&x=\u00E9",
               WEB_ENC_WINDOWS_1252, "http://example.test/?%E9&x=%E9"),
        VECTOR("Unsupported scalar becomes a percent-encoded numeric reference", "http://example.test/?\U0001F600",
               WEB_ENC_WINDOWS_1252, "http://example.test/?%26%23128512%3B"),
        VECTOR("Legacy scalar error does not alter the Unicode fragment", "http://example.test/?\u00A2#\u00A2",
               WEB_ENC_ISO_8859_2, "http://example.test/?%26%23162%3B#%C2%A2"),
        VECTOR("ISO2022JP state spans the complete query", "http://example.test/?\u3042\u3044" "A#\u3042",
               WEB_ENC_ISO_2022_JP, "http://example.test/?%1B$B$%22$$%1B(BA#%E3%81%82"),
        VECTOR("ISO2022JP error resets before numeric serialization", "http://example.test/?\u3042\U0001F600" "A",
               WEB_ENC_ISO_2022_JP, "http://example.test/?%1B$B$%22%1B(B%26%23128512%3BA"),
        VECTOR("ISO2022JP Roman state and literal reverse solidus remain distinct", "http://example.test/?\u203E\\",
               WEB_ENC_ISO_2022_JP, "http://example.test/?%1B(J~%1B(B\\"),
        VECTOR("Query preserves byte-counted NUL between ASCII", "http://example.test/?a\0b",
               WEB_ENC_WINDOWS_1252, "http://example.test/?a%00b"),
        VECTOR("Query applies the special apostrophe encode set", "http://example.test/?'\u00E9",
               WEB_ENC_WINDOWS_1252, "http://example.test/?%27%E9"),
        VECTOR("Non-special query preserves apostrophe with UTF8", "custom:/?'\u00E9",
               WEB_ENC_WINDOWS_1252, "custom:/?'%C3%A9"),
        VECTOR("UTF16LE document selects UTF8 output", "http://example.test/?\u03A3",
               WEB_ENC_UTF16LE, "http://example.test/?%CE%A3"),
        VECTOR("UTF16BE document selects UTF8 output", "http://example.test/?\u03A3",
               WEB_ENC_UTF16BE, "http://example.test/?%CE%A3"),
        VECTOR("Replacement encoding selects UTF8 output", "http://example.test/?\u03A3",
               WEB_ENC_REPLACEMENT, "http://example.test/?%CE%A3"),
        VECTOR("Empty query and fragment remain present", "http://example.test/?#",
               WEB_ENC_ISO_2022_JP, "http://example.test/?#"),
        VECTOR("Legacy parse preprocessing removes only ASCII tabs and newlines", "http://example.test/?\t\u00E9\r\n#\u00E9",
               WEB_ENC_WINDOWS_1252, "http://example.test/?%E9#%C3%A9"),
    };
    for (size_t index = 0; index < ARRLEN(cases); ++index) {
        const Case *entry = &cases[index]; URL url = {0};
        bool parsed = url_parse_encoding_n(entry->input, entry->length, entry->encoding, &url);
        CHECK(entry->name, parsed && href_is(&url, entry->expected));
        url_free(&url);
    }

    URL base = {0}, url = {0};
    bool base_ok = url_parse("http://example.test/path?%86#old", &base);
    CHECK("a relative fragment retains the already serialized base query", base_ok &&
          url_resolve_encoding_n(&base, "#\u2020", sizeof("#\u2020") - 1, WEB_ENC_WINDOWS_1252, &url) &&
          href_is(&url, "http://example.test/path?%86#%E2%80%A0"));
    url_free(&url);
    CHECK("a relative query is encoded once using the explicit document encoding", base_ok &&
          url_resolve_encoding_n(&base, "?\u2020", sizeof("?\u2020") - 1, WEB_ENC_WINDOWS_1252, &url) &&
          href_is(&url, "http://example.test/path?%86"));
    url_free(&url); url_free(&base);

    CHECK("the unchanged modern URL parser retains UTF8", url_parse("http://example.test/?\u2020", &url) &&
          href_is(&url, "http://example.test/?%E2%80%A0"));
    url_free(&url);
    bool initialized = url_parse_encoding_n("http://example.test/?\u2020", sizeof("http://example.test/?\u2020") - 1,
                                            WEB_ENC_WINDOWS_1252, &url);
    CHECK("hyperlink search assignment still uses the UTF8 basic-parser default", initialized &&
          url_set_component_n(&url, URL_SET_SEARCH, "?\u2020", sizeof("?\u2020") - 1) &&
          href_is(&url, "http://example.test/?%E2%80%A0"));
    CHECK("hyperlink fragment assignment is also UTF8", initialized &&
          url_set_component_n(&url, URL_SET_HASH, "#\u2020", sizeof("#\u2020") - 1) &&
          href_is(&url, "http://example.test/?%E2%80%A0#%E2%80%A0"));
    CHECK("component mutation keeps valid-host invalid-port partial commits", initialized &&
          !url_set_component_n(&url, URL_SET_HOST, "other.test:999999", sizeof("other.test:999999") - 1) &&
          href_is(&url, "http://other.test/?%E2%80%A0#%E2%80%A0"));
    url_free(&url);
    char *joined = url_join_encoding_n("http://example.test/base/", "next?\u2020", sizeof("next?\u2020") - 1,
                                       WEB_ENC_WINDOWS_1252);
    CHECK("HTML convenience join carries the explicit encoding", joined &&
          !strcmp(joined, "http://example.test/base/next?%86"));
    free(joined);
    CHECK("invalid scalar UTF8 in a legacy query does not publish a partial record",
          !url_parse_encoding_n("http://example.test/?\xED\xA0\x80", sizeof("http://example.test/?\xED\xA0\x80") - 1,
                                WEB_ENC_WINDOWS_1252, &url) && !url.scheme && !url.host && !url.query);
    CHECK("invalid encoding enum is rejected before any record publication",
          !url_parse_encoding_n("http://example.test/", sizeof("http://example.test/") - 1,
                                WEB_ENC_UNKNOWN, &url) && !url.scheme);
    CHECK("null nonempty URL input is rejected",
          !url_parse_encoding_n(NULL, 1, WEB_ENC_WINDOWS_1252, &url) && !url.scheme);
    CHECK("null URL output is rejected", !url_parse_encoding_n("x", 1, WEB_ENC_UTF8, NULL));
    printf("url-encoding: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
