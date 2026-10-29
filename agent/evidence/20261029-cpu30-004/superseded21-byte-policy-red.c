#include "../src/dom/dom.h"
#include <stdio.h>
#include <string.h>

/* Top-level response-byte boundary controls, independent of the frame parser.
 * The en-US HTML fallback is intentional product policy; JSON/XML stay UTF-8. */
static unsigned checks, failures;
extern long g_nodes_live;
static void verify(const char *name, const char *bytes, const char *type,
                   WebEncoding fallback, bool explicit_fallback,
                   WebEncoding wanted, const char *text) {
    Document *document = doc_new("https://policy.example.test/document");
    bool okay = explicit_fallback
        ? html_parse_bytes_with_fallback(document, bytes, strlen(bytes), type, fallback)
        : html_parse_bytes(document, bytes, strlen(bytes), type);
    char *actual = node_text_content(document->body);
    okay = okay && document->encoding == wanted && actual && !strcmp(actual, text);
    ++checks; if (!okay) ++failures;
    printf("%s %s\n", okay ? "PASS" : "FAIL", name);
    if (!okay) fprintf(stderr, "  encoding=%s text=%s\n", web_encoding_name(document->encoding), actual ? actual : "(null)");
    free(actual); doc_free(document);
}

int main(void) {
    dom_init(); long initial_nodes = g_nodes_live;
    const char *utf8 = "<p id=t>\xc3\xa9</p>";
    const char *legacy = "<p id=t>\x80</p>";
    const char *eacute = "\xc3\xa9";
    const char *euro = "\xe2\x82\xac";
    const char *cyrillic = "\xd0\x93\xc2\xa9";
    verify("en-US unlabelled HTML defaults Windows1252", legacy, "text/html", WEB_ENC_UNKNOWN, false, WEB_ENC_WINDOWS_1252, euro);
    verify("absent response MIME retains HTML default", legacy, NULL, WEB_ENC_UNKNOWN, false, WEB_ENC_WINDOWS_1252, euro);
    verify("unlabelled plaintext uses text-document default", "\x80", "text/plain", WEB_ENC_UNKNOWN, false, WEB_ENC_WINDOWS_1252, euro);
    verify("unsupported header permits HTML default", legacy, "text/html;charset=not-an-encoding", WEB_ENC_UNKNOWN, false, WEB_ENC_WINDOWS_1252, euro);
    verify("explicit UTF8 header precedes default", utf8, "text/html;charset=utf-8", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("UTF8 metadata precedes default", "<meta charset=utf-8><p>\xc3\xa9</p>", "text/html", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("UTF8 BOM precedes explicit legacy header", "\xef\xbb\xbf<p>\xc3\xa9</p>", "text/html;charset=windows-1251", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("eligible legacy parent precedes locale fallback", utf8, "text/html", WEB_ENC_WINDOWS_1251, true, WEB_ENC_WINDOWS_1251, cyrillic);
    verify("eligible UTF8 parent precedes locale fallback", utf8, "text/html", WEB_ENC_UTF8, true, WEB_ENC_UTF8, eacute);
    verify("application JSON top-level default stays UTF8", "\"\xc3\xa9\"", "application/json", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, "\"\xc3\xa9\"");
    verify("text JSON top-level default stays UTF8", "\"\xc3\xa9\"", "text/json", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, "\"\xc3\xa9\"");
    verify("suffix JSON top-level default stays UTF8", "\"\xc3\xa9\"", "application/problem+json", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, "\"\xc3\xa9\"");
    verify("application XML top-level default stays UTF8", utf8, "application/xml", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("text XML top-level default stays UTF8", utf8, "text/xml", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("SVG top-level default stays UTF8", utf8, "image/svg+xml", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("suffix XML top-level default stays UTF8", utf8, "application/example+xml", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("XML MIME matching handles case and whitespace", utf8, " \tAPPLICATION/XML \t;other=1", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("XML explicit transport charset remains authoritative", utf8, "application/xml;charset=windows-1251", WEB_ENC_UNKNOWN, false, WEB_ENC_WINDOWS_1251, cyrillic);
    verify("XML BOM remains authoritative", "\xef\xbb\xbf<p>\xc3\xa9</p>", "application/xml;charset=windows-1251", WEB_ENC_UNKNOWN, false, WEB_ENC_UTF8, eacute);
    verify("XML declaration remains authoritative", "<?xml version='1.0' encoding='windows-1251'?><p>\xc3\xa9</p>", "application/xml", WEB_ENC_UNKNOWN, false, WEB_ENC_WINDOWS_1251, cyrillic);
    ++checks; bool released = g_nodes_live == initial_nodes; if (!released) ++failures;
    printf("%s all policy response documents are released\n", released ? "PASS" : "FAIL");
    printf("RESULTS html-default-boundary %s (%u/%u checks)\n", !failures && checks == 21 ? "PASS" : "FAIL", checks-failures, checks);
    return failures || checks != 21 ? 1 : 0;
}
