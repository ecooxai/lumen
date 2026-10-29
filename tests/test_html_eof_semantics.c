#include "../src/dom/dom.h"
#include <assert.h>
extern long g_nodes_live;

/* Observable tree data for every split of each malformed/end-of-input token.
 * This is independent of the unchanged bounds/mutation sanitizer fixture. */
struct Case { const char *input; int type; const char *data; };
static const struct Case cases[] = {
    {"<!", NODE_COMMENT, ""},
    {"<!x", NODE_COMMENT, "x"},
    {"<!xyz", NODE_COMMENT, "xyz"},
    {"<!>", NODE_COMMENT, ""},
    {"<!xyz>", NODE_COMMENT, "xyz"},
    {"<?", NODE_COMMENT, "?"},
    {"<?abc", NODE_COMMENT, "?abc"},
    {"<?abc>", NODE_COMMENT, "?abc"},
    {"</#", NODE_COMMENT, "#"},
    {"</#xyz", NODE_COMMENT, "#xyz"},
    {"</#>", NODE_COMMENT, "#"},
    {"<!-", NODE_COMMENT, "-"},
    {"<!--", NODE_COMMENT, ""},
    {"<!--a", NODE_COMMENT, "a"},
    {"<!--a-->", NODE_COMMENT, "a"},
    {"<!---->", NODE_COMMENT, ""},
    {"<!doctype html", NODE_DOCTYPE, "html"},
    {"<!doctype html>", NODE_DOCTYPE, "html"},
    {"<!DOCTYPE hTmL", NODE_DOCTYPE, "html"},
    {"<!DOCTYPE svg", NODE_DOCTYPE, "svg"},
    {"<!DOCTYPE svg>", NODE_DOCTYPE, "svg"},
    {"<", NODE_TEXT, "<"},
    {"</", NODE_TEXT, "</"},
    {"</>", NODE_TEXT, ""},
};
int main(void) {
    dom_init(); unsigned long splits = 0;
    for (size_t index = 0; index < ARRLEN(cases); ++index) {
        const struct Case *test = &cases[index];
        size_t length = strlen(test->input);
        for (size_t split = 0; split <= length; ++split) {
            const long baseline = g_nodes_live;
            Document *document = doc_new("https://eof.example/");
            HtmlParser *parser = html_parser_new(document, NULL);
            html_parser_feed(parser, test->input, split);
            html_parser_feed(parser, "", 0);
            html_parser_feed(parser, test->input + split, length - split);
            html_parser_finish(parser);
            html_parser_free(parser);
            char *value = NULL;
            if (test->type == NODE_TEXT) value = node_text_content(&document->node);
            else {
                Node *first = document->node.first;
                if (!first || first->type != test->type) {
                    fprintf(stderr, "EOF token type mismatch input=%s split=%zu\n", test->input, split);
                    abort();
                }
                value = xstrndup(first->text ? first->text : "", first->text_len);
            }
            if (strcmp(value, test->data)) {
                fprintf(stderr, "EOF token data mismatch input=%s split=%zu expected=%s actual=%s\n",
                        test->input, split, test->data, value);
                abort();
            }
            free(value); doc_free(document);
            assert(g_nodes_live == baseline);
            ++splits;
        }
    }
    printf("RESULTS html-eof-semantics PASS cases=%zu splitChecks=%lu\n", ARRLEN(cases), splits);
    return 0;
}
