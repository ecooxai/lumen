#include "../src/base/encoding.h"
#include <stdio.h>
#include <string.h>

static size_t passed, total;
static void check(const char *label, size_t length, const char *expected, const char *kind) {
    int encoding = enc_label(label, length);
    bool ok = expected ? encoding >= 0 && str_ieq(enc_name(encoding), expected) : encoding == -1;
    total++;
    if (ok) passed++;
    else fprintf(stderr, "FAIL encoding label (%s): length=%zu result=%d\n", kind, length, encoding);
}

int main(void) {
    static const char *const aliases[][2] = {
        {"utf-8", "utf-8"}, {"unicode-1-1-utf-8", "utf-8"}, {"utf8", "utf-8"},
        {"ascii", "windows-1252"}, {"latin1", "windows-1252"}, {"iso-8859-1", "windows-1252"},
        {"us-ascii", "windows-1252"}, {"windows-1252", "windows-1252"},
        {"gbk", "gbk"}, {"gb2312", "gbk"}, {"gb18030", "gb18030"},
        {"shift_jis", "shift_jis"}, {"sjis", "shift_jis"}, {"euc-jp", "euc-jp"},
        {"euc-kr", "euc-kr"}, {"big5", "big5"}, {"utf-16", "utf-16le"},
        {"utf-16le", "utf-16le"}, {"utf-16be", "utf-16be"}, {"x-user-defined", "x-user-defined"},
        {"koi8-r", "koi8-r"}, {"macintosh", "macintosh"}, {"windows-1251", "windows-1251"},
        {"iso-2022-jp", "iso-2022-jp"}
    };
    for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; i++) {
        const char *label = aliases[i][0], *expected = aliases[i][1]; size_t n = strlen(label);
        char buffer[128];
        check(label, n, expected, "canonical alias");
        for (size_t j = 0; j < n; j++) buffer[j] = label[j] >= 'a' && label[j] <= 'z' ? label[j] - 32 : label[j];
        check(buffer, n, expected, "ASCII uppercase bounded input");
        memcpy(buffer, "\t\n\r\f ", 5); memcpy(buffer + 5, label, n); memcpy(buffer + 5 + n, " \f\r\n\t", 5);
        check(buffer, n + 10, expected, "ASCII whitespace trim");
        memcpy(buffer, label, n); buffer[n] = 0; memcpy(buffer + n + 1, "ignored", 7);
        check(buffer, n, expected, "explicit length excludes suffix");
        check(buffer, n + 1, NULL, "NUL suffix");
        check(buffer, n + 8, NULL, "NUL plus trailing bytes");
        buffer[0] = 0; memcpy(buffer + 1, label, n); check(buffer, n + 1, NULL, "NUL prefix");
        memcpy(buffer, label, n); buffer[n / 2] = 0; check(buffer, n, NULL, "NUL in label");
        memcpy(buffer, label, n); buffer[n] = '\v'; check(buffer, n + 1, NULL, "vertical tab is not trim whitespace");
    }
    check("", 0, NULL, "empty");
    check(" \t\n\r\f", 5, NULL, "only ASCII whitespace");
    check("utf-8\302\240", 7, NULL, "non-breaking space is not trim whitespace");
    check("utf-8\0 ", 7, NULL, "trim does not hide NUL");
    check("unknown-encoding", 16, NULL, "unknown label");
    printf("encoding label native controls: %zu/%zu PASS\n", passed, total);
    return passed == total ? 0 : 1;
}
