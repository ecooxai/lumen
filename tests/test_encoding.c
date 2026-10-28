/* Independent byte/label regressions for the shared native FileReader decoder.
 * These are custom engine tests, never counted as upstream WPT assertions.
 */
#include "../src/base/encoding.h"

static unsigned checks, failures;
#define CHECK(name, condition) do { ++checks; if (!(condition)) { ++failures; \
    fprintf(stderr, "FAIL %s\n", name); } } while (0)

typedef struct {
    const char *name;
    WebEncoding encoding;
    const char *input; size_t input_length;
    const char *expected; size_t expected_length;
    bool bom;
} DecodeCase;
#define VECTOR(name, encoding, input, expected, bom) \
    { name, encoding, input, sizeof(input) - 1, expected, sizeof(expected) - 1, bom }

int main(void) {
    const DecodeCase cases[] = {
        VECTOR("UTF8 preserves embedded NUL", WEB_ENC_UTF8, "a\0b", "a\0b", true),
        VECTOR("UTF8 restores an invalid ASCII continuation", WEB_ENC_UTF8, "\xE1\x80" "A", "\uFFFD" "A", true),
        VECTOR("UTF8 truncated multibyte sequence is one error", WEB_ENC_UTF8, "\xF0\x90\x80", "\uFFFD", true),
        VECTOR("UTF8 excludes surrogate scalars", WEB_ENC_UTF8, "\xED\xA0\x80", "\uFFFD\uFFFD\uFFFD", true),
        VECTOR("UTF8 excludes overlong sequences", WEB_ENC_UTF8, "\xC0\xAF", "\uFFFD\uFFFD", true),
        VECTOR("UTF8 excludes code points above Unicode", WEB_ENC_UTF8, "\xF4\x90\x80\x80", "\uFFFD\uFFFD\uFFFD\uFFFD", true),
        VECTOR("UTF8 valid supplementary scalar", WEB_ENC_UTF8, "\xF0\x9F\x98\x80", "\U0001F600", true),
        VECTOR("UTF8 BOM overrides a legacy fallback", WEB_ENC_WINDOWS_1252, "\xEF\xBB\xBF\xCE\xA3", "\u03A3", true),
        VECTOR("UTF8 no-sniff mode preserves a BOM", WEB_ENC_UTF8, "\xEF\xBB\xBF" "a", "\uFEFF" "a", false),
        VECTOR("UTF16BE explicit input", WEB_ENC_UTF16BE, "\0" "A\x03\xA3", "A\u03A3", true),
        VECTOR("UTF16LE explicit input", WEB_ENC_UTF16LE, "A\0\xA3\x03", "A\u03A3", true),
        VECTOR("UTF16BE BOM overrides UTF8", WEB_ENC_UTF8, "\xFE\xFF\0" "A", "A", true),
        VECTOR("UTF16LE BOM overrides UTF16BE", WEB_ENC_UTF16BE, "\xFF\xFE" "A\0", "A", true),
        VECTOR("UTF16 supplementary pair", WEB_ENC_UTF16LE, "\x3D\xD8\0\xDE", "\U0001F600", true),
        VECTOR("UTF16 non-low unit is reconsumed", WEB_ENC_UTF16LE, "\0\xD8" "A\0", "\uFFFD" "A", true),
        VECTOR("UTF16 isolated low surrogate is replaced", WEB_ENC_UTF16LE, "\0\xDC", "\uFFFD", true),
        VECTOR("UTF16 lone high surrogate is replaced", WEB_ENC_UTF16LE, "\0\xD8", "\uFFFD", true),
        VECTOR("UTF16 high surrogate plus odd byte is one EOF error", WEB_ENC_UTF16LE, "\0\xD8\0", "\uFFFD", true),
        VECTOR("UTF16 standalone odd byte is replaced", WEB_ENC_UTF16BE, "A", "\uFFFD", true),
        VECTOR("Windows1252 uses browser punctuation mappings", WEB_ENC_WINDOWS_1252, "\x80\x93\x94", "\u20AC\u201C\u201D", true),
        VECTOR("IBM866 uses its official forward index", WEB_ENC_IBM866, "\x80", "\u0410", true),
        VECTOR("ISO8859-2 uses its official forward index", WEB_ENC_ISO_8859_2, "\xA1", "\u0104", true),
        VECTOR("Windows1251 uses its official forward index", WEB_ENC_WINDOWS_1251, "\xC0", "\u0410", true),
        VECTOR("KOI8-R uses its official forward index", WEB_ENC_KOI8_R, "\xE1", "\u0410", true),
        VECTOR("Macintosh uses its official forward index", WEB_ENC_MACINTOSH, "\x80", "\u00C4", true),
        VECTOR("GBK decodes its shared GB18030 two-byte path", WEB_ENC_GBK, "\xD6\xD0", "\u4E2D", true),
        VECTOR("GB18030 maps four-byte pointer zero", WEB_ENC_GB18030, "\x81\x30\x81\x30", "\xC2\x80", true),
        VECTOR("GBK decoder also accepts four-byte sequences", WEB_ENC_GBK, "\x81\x30\x81\x30", "\xC2\x80", true),
        VECTOR("GB18030 supplementary range", WEB_ENC_GB18030, "\x90\x30\x81\x30", "\U00010000", true),
        VECTOR("GB18030 retains the7457 compatibility mapping", WEB_ENC_GB18030, "\x81\x35\xF4\x37", "\uE7C7", true),
        VECTOR("GB18030 upper scalar boundary", WEB_ENC_GB18030, "\xE3\x32\x9A\x35", "\U0010FFFF", true),
        VECTOR("GB18030 pointer above the Unicode bound", WEB_ENC_GB18030, "\xE3\x32\x9A\x36", "\uFFFD", true),
        VECTOR("GB18030 excluded range starts at39420", WEB_ENC_GB18030, "\x84\x31\xA5\x30", "\uFFFD", true),
        VECTOR("GB18030 restores digit and invalid third byte", WEB_ENC_GB18030, "\x81\x30\x20", "\uFFFD" "0 ", true),
        VECTOR("GB18030 restores invalid fourth-byte sequence", WEB_ENC_GB18030, "\x81\x30\x81\x20", "\uFFFD" "0\uFFFD ", true),
        VECTOR("GB18030 digit prefix at EOF is one error", WEB_ENC_GB18030, "\x81\x30", "\uFFFD", true),
        VECTOR("GB18030 third-byte prefix at EOF is one error", WEB_ENC_GB18030, "\x81\x30\x81", "\uFFFD", true),
        VECTOR("GB18030 preserves a quote after an invalid lead", WEB_ENC_GB18030, "\x81\x22", "\uFFFD\"", true),
        VECTOR("GB18030 standalone euro compatibility byte", WEB_ENC_GB18030, "\x80", "\u20AC", true),
        VECTOR("Big5 standard pair", WEB_ENC_BIG5, "\xA4\x40", "\u4E00", true),
        VECTOR("Big5 two-codepoint compatibility mapping", WEB_ENC_BIG5, "\x88\x62", "\u00CA\u0304", true),
        VECTOR("Big5 restores invalid ASCII trail", WEB_ENC_BIG5, "\x81\x22", "\uFFFD\"", true),
        VECTOR("EUCJP standard pair", WEB_ENC_EUC_JP, "\xA4\xA2", "\u3042", true),
        VECTOR("EUCJP halfwidth katakana", WEB_ENC_EUC_JP, "\x8E\xA6", "\uFF66", true),
        VECTOR("EUCJP JIS0212 prefix restores ASCII", WEB_ENC_EUC_JP, "\x8F\xA1\x20", "\uFFFD ", true),
        VECTOR("EUCJP JIS0212 prefix at EOF", WEB_ENC_EUC_JP, "\x8F\xA1", "\uFFFD", true),
        VECTOR("ShiftJIS standard pair", WEB_ENC_SHIFT_JIS, "\x82\xA0", "\u3042", true),
        VECTOR("ShiftJIS Windows EUDC mapping", WEB_ENC_SHIFT_JIS, "\xF0\x40", "\uE000", true),
        VECTOR("ShiftJIS standalone80 is not an error", WEB_ENC_SHIFT_JIS, "\x80", "\xC2\x80", true),
        VECTOR("ShiftJIS restores a quoted ASCII delimiter", WEB_ENC_SHIFT_JIS, "\x82\x22", "\uFFFD\"", true),
        VECTOR("EUCKR standard pair", WEB_ENC_EUC_KR, "\xB0\xA1", "\uAC00", true),
        VECTOR("EUCKR restores invalid ASCII trail", WEB_ENC_EUC_KR, "\x81\x20", "\uFFFD ", true),
        VECTOR("ISO2022JP Japanese then ASCII", WEB_ENC_ISO_2022_JP, "\x1B$B$\"\x1B(B" "A", "\u3042" "A", true),
        VECTOR("ISO2022JP Roman yen and overline", WEB_ENC_ISO_2022_JP, "\x1B(J\\~", "\u00A5\u203E", true),
        VECTOR("ISO2022JP halfwidth katakana", WEB_ENC_ISO_2022_JP, "\x1B(I!_", "\uFF61\uFF9F", true),
        VECTOR("ISO2022JP adjacent escape sequences emit an error", WEB_ENC_ISO_2022_JP, "\x1B(B\x1B(B", "\uFFFD", true),
        VECTOR("ISO2022JP escape-start EOF", WEB_ENC_ISO_2022_JP, "\x1B", "\uFFFD", true),
        VECTOR("ISO2022JP escape EOF restores its prefix", WEB_ENC_ISO_2022_JP, "\x1B$", "\uFFFD$", true),
        VECTOR("ISO2022JP invalid escape restores both bytes", WEB_ENC_ISO_2022_JP, "\x1B$X", "\uFFFD$X", true),
        VECTOR("ISO2022JP pending trailing byte at EOF", WEB_ENC_ISO_2022_JP, "\x1B$B$", "\uFFFD", true),
        VECTOR("ISO2022JP prohibited shift control", WEB_ENC_ISO_2022_JP, "a\x0E" "b", "a\uFFFD" "b", true),
        VECTOR("replacement emits once for any nonempty input", WEB_ENC_REPLACEMENT, "ab\0cd", "\uFFFD", true),
        VECTOR("replacement emits nothing for empty input", WEB_ENC_REPLACEMENT, "", "", true),
        VECTOR("BOM overrides the replacement fallback", WEB_ENC_REPLACEMENT, "\xEF\xBB\xBF" "a", "a", true),
        VECTOR("x-user-defined maps high bytes to private use", WEB_ENC_X_USER_DEFINED, "\x80\xFF", "\uF780\uF7FF", true),
    };
    for (size_t index = 0; index < ARRLEN(cases); ++index) {
        const DecodeCase *entry = &cases[index];
        SB output; sb_init(&output);
        bool ok = web_encoding_decode_n(&output, entry->encoding, entry->input, entry->input_length,
                                         entry->bom, 4096);
        CHECK(entry->name, ok && output.n == entry->expected_length &&
              (!output.n || !memcmp(output.s, entry->expected, output.n)));
        sb_free(&output);
    }

    struct { const char *name, *label; WebEncoding expected; } labels[] = {
        {"UTF8 aliases are ASCII-case-insensitive", "  UNICODE-1-1-UTF-8\t", WEB_ENC_UTF8},
        {"Latin1 label selects Windows1252", "iso-8859-1", WEB_ENC_WINDOWS_1252},
        {"ASCII label selects Windows1252", "ascii", WEB_ENC_WINDOWS_1252},
        {"UTF16 label defaults to little endian", "utf-16", WEB_ENC_UTF16LE},
        {"ISO8859-8-I retains its canonical identity", "logical", WEB_ENC_ISO_8859_8_I},
        {"Unknown label remains explicit", "not-a-real-encoding", WEB_ENC_UNKNOWN},
        {"Replacement label is recognized", "iso-2022-kr", WEB_ENC_REPLACEMENT},
        {"x-user-defined label is recognized", "x-user-defined", WEB_ENC_X_USER_DEFINED},
    };
    for (size_t index = 0; index < ARRLEN(labels); ++index)
        CHECK(labels[index].name, web_encoding_label_n(labels[index].label, strlen(labels[index].label)) == labels[index].expected);
    CHECK("A raw NUL cannot truncate an encoding label", web_encoding_label_n("utf-8\0suffix", 12) == WEB_ENC_UNKNOWN);
    CHECK("Canonical name remains independent of the input alias", !strcmp(web_encoding_name(WEB_ENC_WINDOWS_1252), "windows-1252"));

    struct { const char *name, *mime; WebEncoding expected; } mimes[] = {
        {"MIME charset is ASCII-case-insensitive", "Text/Plain; CHARSET=WINDOWS-1252", WEB_ENC_WINDOWS_1252},
        {"MIME quoted charset is parsed", "text/plain; charset=\"utf-16be\"", WEB_ENC_UTF16BE},
        {"MIME quoted escapes are interpreted", "text/plain; charset=\"win\\dows-1252\"", WEB_ENC_WINDOWS_1252},
        {"First valid MIME charset wins", "text/plain; charset=utf-16le; charset=utf-8", WEB_ENC_UTF16LE},
        {"An unknown first charset is not replaced by a duplicate", "text/plain; charset=unknown; charset=utf-8", WEB_ENC_UNKNOWN},
        {"Empty unquoted charset is skipped", "text/plain; charset= ; charset=utf-16", WEB_ENC_UTF16LE},
        {"Invalid parameter name is skipped", "text/plain; charset =utf-8; charset=utf-16be", WEB_ENC_UTF16BE},
        {"Invalid MIME essence rejects charset", "not-a-mime; charset=utf-8", WEB_ENC_UNKNOWN},
        {"Unterminated quoted MIME value is collected", "text/plain; charset=\"utf-16be", WEB_ENC_UTF16BE},
        {"Empty quoted charset remains the first parameter", "text/plain; charset=\"\"; charset=utf-8", WEB_ENC_UNKNOWN},
    };
    for (size_t index = 0; index < ARRLEN(mimes); ++index)
        CHECK(mimes[index].name, web_encoding_mime_n(mimes[index].mime, strlen(mimes[index].mime)) == mimes[index].expected);

    SB output; sb_init(&output);
    CHECK("Output bound rejects a replacement atom without splitting it",
          !web_encoding_decode_n(&output, WEB_ENC_UTF8, "\xFF", 1, true, 2) && output.n == 0);
    CHECK("Output bound accepts an exact complete result",
          web_encoding_decode_n(&output, WEB_ENC_UTF8, "\xFF", 1, true, 3) && output.n == 3);
    CHECK("Invalid enum is rejected without a table access",
          !web_encoding_decode_n(&output, WEB_ENC_UNKNOWN, "a", 1, true, 20) && output.n == 3);
    CHECK("Null nonempty input is rejected",
          !web_encoding_decode_n(&output, WEB_ENC_UTF8, NULL, 1, true, 20) && output.n == 3);
    sb_free(&output);
    printf("encoding: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
