#include "../src/dom/dom.h"
#include <assert.h>
extern long g_nodes_live;

/* Exercise the real byte parser with exact-sized, non-NUL-terminated input.
 * Encoding assertions concern complete early declarations or genuinely
 * truncated input. Declarations beyond the prescan window are memory-safety
 * cases only: this fixture does not enshrine the missing parser restart. */
static unsigned long calls, semantic;
static void parse(const unsigned char *bytes, size_t length, WebEncoding expected,
                  const char *label) {
    const long baseline = g_nodes_live;
    unsigned char *input = length ? malloc(length) : NULL;
    assert(!length || input);
    if (length) memcpy(input, bytes, length);
    Document *document = doc_new("https://bounds.example/");
    assert(html_parse_bytes(document, (const char *)input, length, "text/html"));
    if (expected != WEB_ENC_UNKNOWN) {
        if (document->encoding != expected) {
            fprintf(stderr, "HTML boundary mismatch: %s length=%zu wanted=%s got=%s\n",
                    label, length, web_encoding_name(expected),
                    web_encoding_name(document->encoding));
            abort();
        }
        ++semantic;
    }
    assert(document->encoding >= WEB_ENC_UTF8 && document->encoding < WEB_ENC_COUNT);
    free(input);
    doc_free(document);
    assert(g_nodes_live == baseline);
    ++calls;
}
static void text(const char *input, WebEncoding expected) {
    parse((const unsigned char *)input, strlen(input), expected, input);
}
int main(void) {
    dom_init();
    text("", WEB_ENC_UTF8);
    text("<", WEB_ENC_UTF8);
    text("<!-", WEB_ENC_UTF8);
    text("<!--<meta charset=windows-1252>", WEB_ENC_UTF8);
    text("<meta", WEB_ENC_UTF8);
    text("<meta charset=", WEB_ENC_UTF8);
    text("<meta charset=windows-1252", WEB_ENC_UTF8);
    text("<meta charset=\"windows-1252", WEB_ENC_UTF8);
    text("<meta charset=\"windows-1252\"", WEB_ENC_UTF8);
    text("<a x=\"<meta charset=windows-1252>", WEB_ENC_UTF8);
    text("<?xml encoding='windows-1252'", WEB_ENC_UTF8);
    text("<?xml encoding='windows-1252", WEB_ENC_UTF8);
    text("<meta http-equiv=x http-equiv=content-type content='charset=windows-1252'>", WEB_ENC_UTF8);
    text("<meta http-equiv=content-type content='charset=bogus' content='charset=windows-1252'>", WEB_ENC_UTF8);
    text("<meta http-equiv=content-type content='charset=bogus' charset=windows-1252>", WEB_ENC_WINDOWS_1252);
    text("<meta http-equiv=content-type content='charset; charset=windows-1252'>", WEB_ENC_WINDOWS_1252);
    text("<!-- ignored --><meta charset=windows-1252>", WEB_ENC_WINDOWS_1252);
    text("<a x='<meta charset=utf-8>'><meta charset=windows-1252>", WEB_ENC_WINDOWS_1252);
    const char *tag = "<meta charset=windows-1252>";
    unsigned char around[1088];
    for (size_t end = 1000; end <= 1050; ++end) {
        memset(around, ' ', sizeof around);
        memcpy(around + end - strlen(tag), tag, strlen(tag));
        parse(around, end, end <= 1024 ? WEB_ENC_WINDOWS_1252 : WEB_ENC_UNKNOWN,
              "complete tag around 1024-byte boundary");
    }
    const char *patterns[] = {
        "<meta charset='windows-1252'>tail",
        "<meta http-equiv='content-type' content='text/html;charset=windows-1252'>tail",
        "<!--<meta charset=windows-1252>--><meta charset=utf-8>",
        "<div x='fake <meta charset=windows-1252>'><meta charset=utf-8>",
        "<?xml version='1.0' encoding='windows-1252'?>tail",
        "<meta/content='charset=unknown' http-equiv=content-type charset='Big5'>tail",
        "<meta charset='utf-16le'><!--truncated",
        "<a x=\"tail\\\" <meta charset=windows-1252>",
    };
    for (size_t pattern = 0; pattern < ARRLEN(patterns); ++pattern) {
        const unsigned char *source = (const unsigned char *)patterns[pattern];
        const size_t n = strlen(patterns[pattern]);
        for (size_t prefix = 0; prefix <= n; ++prefix)
            parse(source, prefix, WEB_ENC_UNKNOWN, "every truncation");
        for (size_t offset = 990; offset <= 1032; ++offset) {
            memset(around, ' ', sizeof around);
            const size_t available = sizeof around - offset;
            const size_t copied = n < available ? n : available;
            memcpy(around + offset, source, copied);
            for (size_t prefix = 0; prefix <= copied; ++prefix)
                parse(around, offset + prefix, WEB_ENC_UNKNOWN, "truncated near scan boundary");
        }
        for (size_t position = 0; position < n; position += 3) {
            for (unsigned byte = 0; byte < 256; ++byte) {
                memcpy(around, source, n);
                around[position] = (unsigned char)byte;
                parse(around, n, WEB_ENC_UNKNOWN, "delimiter and non-ASCII mutation");
            }
        }
    }
    const unsigned char xml16[][6] = {{'<',0,'?',0,'x',0}, {0,'<',0,'?',0,'x'}};
    for (size_t endian = 0; endian < 2; ++endian)
        for (size_t length = 0; length <= 6; ++length)
            parse(xml16[endian], length, length == 6 ? (endian ? WEB_ENC_UTF16BE : WEB_ENC_UTF16LE) : WEB_ENC_UTF8, "UTF16 XML prefix truncation");
    Document *contract = doc_new("https://bounds.example/");
    assert(!html_parse_bytes(NULL, "", 0, "text/html"));
    assert(!html_parse_bytes(contract, NULL, 1, "text/html"));
    assert(!html_parse_bytes(contract, "", SIZE_MAX, "text/html"));
    doc_free(contract);
    printf("RESULTS html-encoding-bounds PASS parseCalls=%lu semanticChecks=%lu contractChecks=3\n", calls, semantic);
    return 0;
}
