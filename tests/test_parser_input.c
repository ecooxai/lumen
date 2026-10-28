#include "../src/dom/dom.h"
#include <stdio.h>
#include <string.h>

/* Observable parser and response-document behavior. These expectations do not
 * inspect tokenizer state or depend on an upload fixture's boundary spelling. */
enum { EXPECTED_CHECKS = 48 };
static unsigned checks, failures, script_calls;
extern long g_nodes_live;

static void check(bool ok, const char *name) {
    ++checks;
    if (!ok) ++failures;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

static bool text_is(Node *node, const char *expected) {
    if (!node) return false;
    char *text = node_text_content(node);
    bool ok = text && !strcmp(text, expected);
    free(text);
    return ok;
}

static Document *parse(const char *source) {
    Document *document = doc_new("https://parser.example.test/page");
    html_parse(document, source, strlen(source));
    return document;
}

static void html_text(const char *name, const char *source, const char *expected) {
    Document *document = parse(source);
    check(text_is(doc_get_element_by_id(document, "t"), expected), name);
    doc_free(document);
}

static void html_attr(const char *name, const char *source, const char *expected) {
    Document *document = parse(source);
    Node *node = doc_get_element_by_id(document, "t");
    const char *value = node ? node_attr(node, "data-x") : NULL;
    check(value && !strcmp(value, expected), name);
    doc_free(document);
}

static void fragment_text(const char *name, const char *tag,
                          const char *source, const char *expected) {
    Document *document = doc_new("https://parser.example.test/fragment");
    Node *context = node_new_element(document, tag, NS_HTML);
    Node *fragment = html_parse_fragment(document, context, source, strlen(source));
    check(text_is(fragment, expected), name);
    node_free_tree(fragment);
    node_free_tree(context);
    doc_free(document);
}

static void stream_text(const char *name, const char *const *pieces,
                        const size_t *lengths, size_t count, const char *expected,
                        bool attribute) {
    Document *document = doc_new("https://parser.example.test/stream");
    HtmlParser *parser = html_parser_new(document, NULL);
    for (size_t i = 0; i < count; ++i)
        html_parser_feed(parser, pieces[i], lengths ? lengths[i] : strlen(pieces[i]));
    html_parser_finish(parser);
    Node *node = doc_get_element_by_id(document, "t");
    const char *value = attribute && node ? node_attr(node, "data-x") : NULL;
    check(attribute ? value && !strcmp(value, expected) : text_is(node, expected), name);
    html_parser_free(parser);
    doc_free(document);
}

static Document *response(const char *bytes, size_t length, const char *type,
                           bool *decoded) {
    Document *document = doc_new("https://parser.example.test/response");
    *decoded = html_parse_bytes(document, bytes, length, type);
    return document;
}

static bool has_plain_pre(Document *document) {
    return document->html && document->head && document->body &&
           document->body->first && document->body->first == document->body->last &&
           node_is(document->body->first, A_pre);
}

static void plain_text(const char *name, const char *bytes, size_t length,
                       const char *type, const char *expected) {
    bool decoded = false;
    Document *document = response(bytes, length, type, &decoded);
    check(decoded && text_is(document->body, expected), name);
    doc_free(document);
}

static void count_script(Document *document, Node *script, void *unused) {
    (void)document; (void)script; (void)unused;
    ++script_calls;
}

int main(void) {
    dom_init();
    long initial_nodes = g_nodes_live;

    /* 1--15: raw input normalization before every relevant tokenizer state. */
    html_text("full document collapses raw CRLF", "<div id=t>A\r\nB</div>", "A\nB");
    html_text("full document converts lone CR", "<div id=t>A\rB</div>", "A\nB");
    html_text("full document preserves existing LF", "<div id=t>A\nB</div>", "A\nB");
    html_text("full document normalizes consecutive and mixed newlines", "<div id=t>A\r\r\nB\nC\rD</div>", "A\n\nB\nC\nD");
    html_attr("quoted attribute collapses raw CRLF", "<div id=t data-x='A\r\nB'></div>", "A\nB");
    html_attr("quoted attribute converts lone CR", "<div id=t data-x='A\rB'></div>", "A\nB");
    {
        Document *document = parse("<div id=t data-x=A\r\ndata-y=B></div>");
        Node *node = doc_get_element_by_id(document, "t");
        const char *x = node ? node_attr(node, "data-x") : NULL;
        const char *y = node ? node_attr(node, "data-y") : NULL;
        check(x && y && !strcmp(x, "A") && !strcmp(y, "B"), "raw CRLF separates unquoted attributes");
        doc_free(document);
    }
    {
        Document *document = parse("<div id=t><!--A\r\nB--></div>");
        Node *node = doc_get_element_by_id(document, "t");
        Node *comment = node ? node->first : NULL;
        check(comment && comment->type == NODE_COMMENT && comment->text && !strcmp(comment->text, "A\nB"), "comment input collapses raw CRLF");
        doc_free(document);
    }
    html_text("script data collapses raw CRLF", "<script id=t>A\r\nB</script>", "A\nB");
    html_text("style raw text collapses raw CRLF", "<style id=t>A\r\nB</style>", "A\nB");
    html_text("textarea RCDATA collapses raw CRLF", "<textarea id=t>A\r\nB</textarea>", "A\nB");
    html_text("title RCDATA collapses raw CRLF", "<title id=t>A\r\nB</title>", "A\nB");
    html_text("pre ignores one normalized initial newline", "<pre id=t>\r\nA\r\nB</pre>", "A\nB");
    html_text("textarea ignores one normalized initial newline", "<textarea id=t>\r\nA\r\nB</textarea>", "A\nB");
    html_text("plaintext element normalizes input without interpreting markup", "<plaintext id=t>A\r\n<b>B</b>&amp;", "A\n<b>B</b>&amp;");

    /* 16--24: fragment coverage and controls that must keep literal CR. */
    fragment_text("fragment input collapses raw CRLF", "div", "A\r\nB", "A\nB");
    fragment_text("fragment input converts lone CR", "div", "A\rB", "A\nB");
    fragment_text("RCDATA fragment separates raw CRLF from a character reference", "textarea", "A\r\nB&#13;C", "A\nB\rC");
    html_text("decimal character reference retains CR", "<div id=t>A&#13;B</div>", "A\rB");
    html_text("hexadecimal character reference retains CR", "<div id=t>A&#x0d;B</div>", "A\rB");
    html_attr("attribute character reference retains CR", "<div id=t data-x='A&#13;B'></div>", "A\rB");
    html_text("character-reference CR followed by raw LF stays two characters", "<div id=t>A&#13;\nB</div>", "A\r\nB");
    {
        Document *document = parse("<div id=t></div>");
        Node *node = doc_get_element_by_id(document, "t");
        if (node) node_set_text_content(node, "A\r\nB");
        check(text_is(node, "A\r\nB"), "direct DOM text mutation preserves CRLF");
        doc_free(document);
    }
    {
        Document *document = parse("<div id=t></div>");
        Node *node = doc_get_element_by_id(document, "t");
        if (node) node_set_attr(node, "data-x", "A\r\nB");
        const char *value = node ? node_attr(node, "data-x") : NULL;
        check(value && !strcmp(value, "A\r\nB"), "direct DOM attribute mutation preserves CRLF");
        doc_free(document);
    }

    /* 25--32: a CR at a feed boundary remains part of the preprocessing state. */
    {
        const char *parts[] = {"<div id=t>A\r", "\nB</div>"};
        stream_text("stream collapses CRLF across feeds", parts, NULL, 2, "A\nB", false);
    }
    {
        const char *parts[] = {"<div id=t>A\r", "", "\nB</div>"};
        stream_text("empty feed does not break a pending CRLF pair", parts, NULL, 3, "A\nB", false);
    }
    {
        const char *parts[] = {"<div id=t>A\r"};
        stream_text("stream converts terminal CR before EOF", parts, NULL, 1, "A\n", false);
    }
    {
        const char *parts[] = {"<div id=t>A\r", "\r", "\nB</div>"};
        stream_text("stream keeps consecutive CRs as separate newlines", parts, NULL, 3, "A\n\nB", false);
    }
    {
        const char *parts[] = {"<div id=t data-x='A\r", "\nB'>text</div>"};
        stream_text("attribute CRLF collapses across feeds", parts, NULL, 2, "A\nB", true);
    }
    {
        const char *parts[] = {"<script id=t>A\r", "\nB</script>"};
        stream_text("script data CRLF collapses across feeds", parts, NULL, 2, "A\nB", false);
    }
    {
        const char *parts[] = {"<div id=t>A\r", "\0", "\nB</div>"};
        const size_t sizes[] = {strlen(parts[0]), 1, strlen(parts[2])};
        stream_text("NUL between CR and LF keeps both normalized line breaks", parts, sizes, 3, "A\n\xEF\xBF\xBD\nB", false);
    }
    {
        const char source[] = "<div id=t>A\0\r\nB</div>";
        Document *document = doc_new("https://parser.example.test/nul");
        html_parse(document, source, sizeof(source) - 1);
        check(text_is(doc_get_element_by_id(document, "t"), "A\xEF\xBF\xBD\nB"), "NUL replacement and CRLF normalization preserve following bytes");
        doc_free(document);
    }

    /* 33--47: the actual byte-response path selects a text document by MIME
     * essence, after its shared charset/BOM decoder has produced Unicode. */
    {
        bool decoded = false;
        Document *document = response("hello", 5, "text/plain", &decoded);
        check(decoded && has_plain_pre(document), "text/plain creates html head body and one pre element");
        doc_free(document);
    }
    plain_text("text/plain preserves markup literally", "<b>A</b>", 8, "text/plain", "<b>A</b>");
    plain_text("text/plain preserves entity spellings literally", "&amp;&#13;", 10, "text/plain", "&amp;&#13;");
    plain_text("text/plain normalizes mixed raw newlines", "A\r\nB\rC", 6, "text/plain", "A\nB\nC");
    plain_text("text/plain preserves the response initial newline", "\nA", 2, "text/plain", "\nA");
    {
        bool decoded = false;
        const char source[] = "<!doctype invalid><p>A";
        Document *document = response(source, sizeof(source) - 1, "text/plain", &decoded);
        check(decoded && !document->quirks && has_plain_pre(document), "text/plain remains no-quirks despite doctype-shaped text");
        doc_free(document);
    }
    plain_text("MIME essence matching is case insensitive and parameter aware", "<b>A</b>", 8, " \tTEXT/PLAIN \t; charset=UTF-8", "<b>A</b>");
    {
        bool decoded = false;
        Document *document = response("<b id=t>A</b>", 13, "text/plain-extra; charset=UTF-8", &decoded);
        check(decoded && node_is(doc_get_element_by_id(document, "t"), A_b), "a text/plain prefix does not select plaintext parsing");
        doc_free(document);
    }
    {
        const char bytes[] = {'A', (char)0x80, '\r', '\n', 'B'};
        bool decoded = false;
        Document *document = response(bytes, sizeof bytes, "text/plain; charset=windows-1252", &decoded);
        check(decoded && document->encoding == WEB_ENC_WINDOWS_1252 && text_is(document->body, "A\xE2\x82\xAC\nB") && has_plain_pre(document), "text/plain uses the transport decoder before newline normalization");
        doc_free(document);
    }
    {
        const char bytes[] = "\xEF\xBB\xBF" "A\xC3\xA9\r\nB";
        bool decoded = false;
        Document *document = response(bytes, sizeof(bytes) - 1, "text/plain; charset=windows-1252", &decoded);
        check(decoded && document->encoding == WEB_ENC_UTF8 && text_is(document->body, "A\xC3\xA9\nB"), "text/plain BOM overrides the transport charset");
        doc_free(document);
    }
    {
        const unsigned char bytes[] = {0xFF, 0xFE, 'A', 0, '\r', 0, '\n', 0, 'B', 0};
        bool decoded = false;
        Document *document = response((const char *)bytes, sizeof bytes, "text/plain", &decoded);
        check(decoded && document->encoding == WEB_ENC_UTF16LE && text_is(document->body, "A\nB"), "text/plain normalizes CRLF after UTF16LE decoding");
        doc_free(document);
    }
    {
        const unsigned char bytes[] = {0xFE, 0xFF, 0, 'A', 0, '\r', 0, '\n', 0, 'B'};
        bool decoded = false;
        Document *document = response((const char *)bytes, sizeof bytes, "text/plain", &decoded);
        check(decoded && document->encoding == WEB_ENC_UTF16BE && text_is(document->body, "A\nB"), "text/plain normalizes CRLF after UTF16BE decoding");
        doc_free(document);
    }
    {
        bool decoded = false;
        Document *document = response(NULL, 0, "text/plain", &decoded);
        check(decoded && has_plain_pre(document) && text_is(document->body, ""), "empty text/plain creates an empty pre document");
        doc_free(document);
    }
    {
        const char source[] = "<script>doNotExecute()</script>";
        html_script_hook = count_script;
        bool decoded = false;
        Document *document = response(source, sizeof(source) - 1, "text/plain", &decoded);
        check(decoded && script_calls == 0 && text_is(document->body, source), "text/plain never invokes the parser script hook");
        html_script_hook = NULL;
        doc_free(document);
    }
    {
        const char source[] = "<!doctype html><b id=t>A\r\nB</b>";
        bool decoded = false;
        Document *document = response(source, sizeof(source) - 1, "text/html; charset=UTF-8", &decoded);
        check(decoded && node_is(doc_get_element_by_id(document, "t"), A_b) && text_is(doc_get_element_by_id(document, "t"), "A\nB") && !document->quirks && document->encoding == WEB_ENC_UTF8, "text/html still parses elements with normalized input and encoding metadata");
        doc_free(document);
    }
    /* 48: every document, fragment, and parser fixture was released. */
    check(g_nodes_live == initial_nodes, "all parser fixture native nodes are released");
    bool ok = failures == 0 && checks == EXPECTED_CHECKS;
    printf("RESULTS parser-input %s (%u/%u checks)\n", ok ? "PASS" : "FAIL", checks - failures, checks);
    if (checks != EXPECTED_CHECKS)
        fprintf(stderr, "Expected %u checks, recorded %u\n", EXPECTED_CHECKS, checks);
    return ok ? 0 : 1;
}
