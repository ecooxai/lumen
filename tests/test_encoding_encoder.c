/* Independent encoding byte/error regressions. Custom engine checks remain
 * separate from the unchanged official WPT assertion denominator. */
#include "../src/base/encoding.h"

static unsigned checks, failures;
#define CHECK(name, condition) do { ++checks; if (!(condition)) { ++failures; \
    fprintf(stderr, "FAIL %s\n", name); } } while (0)

typedef struct {
    SB output;
    size_t maximum;
    unsigned byte_calls, error_calls;
    bool reject_errors;
} Recorder;

static bool put_bytes(Recorder *recorder, const char *bytes, size_t length) {
    if (recorder->output.n > recorder->maximum || length > recorder->maximum - recorder->output.n) return false;
    sb_put(&recorder->output, bytes, length);
    return true;
}
static bool record_byte(void *opaque, unsigned char byte) {
    Recorder *recorder = opaque;
    ++recorder->byte_calls;
    char value = (char)byte;
    return put_bytes(recorder, &value, 1);
}
static bool record_error(void *opaque, uint32_t point) {
    Recorder *recorder = opaque;
    ++recorder->error_calls;
    if (recorder->reject_errors) return false;
    char message[32];
    int length = snprintf(message, sizeof message, "<%u>", point);
    return length > 0 && (size_t)length < sizeof message && put_bytes(recorder, message, (size_t)length);
}

typedef struct {
    const char *name;
    WebEncoding encoding;
    const char *input; size_t input_length;
    const char *expected; size_t expected_length;
    unsigned errors;
} EncodeCase;
#define VECTOR(name, encoding, input, expected, errors) \
    {name, encoding, input, sizeof(input) - 1, expected, sizeof(expected) - 1, errors}

int main(void) {
    const EncodeCase cases[] = {
        VECTOR("UTF8 embedded NUL is byte-counted", WEB_ENC_UTF8, "a\0b", "a\0b", 0),
        VECTOR("UTF8 preserves supplementary scalar", WEB_ENC_UTF8, "\U0001F600", "\xF0\x9F\x98\x80", 0),
        VECTOR("UTF8 preserves a literal BOM scalar", WEB_ENC_UTF8, "\uFEFF" "a", "\xEF\xBB\xBF" "a", 0),
        VECTOR("UTF16LE output hook selects UTF8", WEB_ENC_UTF16LE, "\u03A3", "\xCE\xA3", 0),
        VECTOR("UTF16BE output hook selects UTF8", WEB_ENC_UTF16BE, "\u03A3", "\xCE\xA3", 0),
        VECTOR("replacement output hook selects UTF8", WEB_ENC_REPLACEMENT, "\u03A3", "\xCE\xA3", 0),
        VECTOR("Windows1252 punctuation reverse mapping", WEB_ENC_WINDOWS_1252, "\u20AC\u201C\u201D", "\x80\x93\x94", 0),
        VECTOR("IBM866 Cyrillic reverse mapping", WEB_ENC_IBM866, "\u0410", "\x80", 0),
        VECTOR("ISO8859-2 letter reverse mapping", WEB_ENC_ISO_8859_2, "\u0104", "\xA1", 0),
        VECTOR("Windows1251 Cyrillic reverse mapping", WEB_ENC_WINDOWS_1251, "\u0410", "\xC0", 0),
        VECTOR("KOI8-R Cyrillic reverse mapping", WEB_ENC_KOI8_R, "\u0410", "\xE1", 0),
        VECTOR("Macintosh diaeresis reverse mapping", WEB_ENC_MACINTOSH, "\u00C4", "\x80", 0),
        VECTOR("Single-byte unmappable values report and resume", WEB_ENC_WINDOWS_1252, "\U0001F600" "A", "<128512>A", 1),
        VECTOR("ISO8859-2 missing cent is an explicit error", WEB_ENC_ISO_8859_2, "\u00A2", "<162>", 1),
        VECTOR("Windows1252 dagger uses byte86", WEB_ENC_WINDOWS_1252, "\u2020", "\x86", 0),
        VECTOR("GBK standard Chinese pair", WEB_ENC_GBK, "\u4E2D", "\xD6\xD0", 0),
        VECTOR("GB18030 standard Chinese pair", WEB_ENC_GB18030, "\u4E2D", "\xD6\xD0", 0),
        VECTOR("GBK euro has its explicit one-byte mapping", WEB_ENC_GBK, "\u20AC", "\x80", 0),
        VECTOR("GB18030 euro uses its canonical two-byte mapping", WEB_ENC_GB18030, "\u20AC", "\xA2\xE3", 0),
        VECTOR("GBK rejects scalar requiring a four-byte range", WEB_ENC_GBK, "\xC2\x80", "<128>", 1),
        VECTOR("GB18030 four-byte pointer zero", WEB_ENC_GB18030, "\xC2\x80", "\x81\x30\x81\x30", 0),
        VECTOR("GB18030 supplementary range", WEB_ENC_GB18030, "\U00010000", "\x90\x30\x81\x30", 0),
        VECTOR("GB18030 upper scalar boundary", WEB_ENC_GB18030, "\U0010FFFF", "\xE3\x32\x9A\x35", 0),
        VECTOR("GB18030 pointer7457 compatibility mapping", WEB_ENC_GB18030, "\uE7C7", "\x81\x35\xF4\x37", 0),
        VECTOR("GB18030 private-use asymmetry has explicit bytes", WEB_ENC_GB18030, "\uE78D\uE864", "\xA6\xD9\xFE\xA0", 0),
        VECTOR("GBK preserves the same private-use compatibility bytes", WEB_ENC_GBK, "\uE78D", "\xA6\xD9", 0),
        VECTOR("GB18030 disallows nonroundtripping E5E5", WEB_ENC_GB18030, "\uE5E5", "<58853>", 1),
        VECTOR("GBK also disallows nonroundtripping E5E5", WEB_ENC_GBK, "\uE5E5", "<58853>", 1),
        VECTOR("Big5 standard Chinese pair", WEB_ENC_BIG5, "\u4E00", "\xA4\x40", 0),
        VECTOR("Big5 duplicate ten uses last permitted pointer", WEB_ENC_BIG5, "\u5341", "\xA4\x51", 0),
        VECTOR("Big5 avoids early HKSCS dagger pointer", WEB_ENC_BIG5, "\u2020", "<8224>", 1),
        VECTOR("Big5 reports unrepresentable supplementary scalar", WEB_ENC_BIG5, "\U0010FFFF", "<1114111>", 1),
        VECTOR("EUCJP Japanese pair", WEB_ENC_EUC_JP, "\u3042", "\xA4\xA2", 0),
        VECTOR("EUCJP halfwidth kana prefix", WEB_ENC_EUC_JP, "\uFF66", "\x8E\xA6", 0),
        VECTOR("EUCJP yen and overline aliases", WEB_ENC_EUC_JP, "\u00A5\u203E", "\\~", 0),
        VECTOR("ShiftJIS Japanese pair", WEB_ENC_SHIFT_JIS, "\u3042", "\x82\xA0", 0),
        VECTOR("ShiftJIS halfwidth kana byte", WEB_ENC_SHIFT_JIS, "\uFF66", "\xA6", 0),
        VECTOR("ShiftJIS standalone80 is preserved", WEB_ENC_SHIFT_JIS, "\xC2\x80", "\x80", 0),
        VECTOR("ShiftJIS yen and overline aliases", WEB_ENC_SHIFT_JIS, "\u00A5\u203E", "\\~", 0),
        VECTOR("ShiftJIS minus remaps to fullwidth hyphen", WEB_ENC_SHIFT_JIS, "\u2212", "\x81\x7C", 0),
        VECTOR("ShiftJIS private EUDC decoding is intentionally not reversible", WEB_ENC_SHIFT_JIS, "\uE000", "<57344>", 1),
        VECTOR("EUCKR Hangul pair", WEB_ENC_EUC_KR, "\uAC00", "\xB0\xA1", 0),
        VECTOR("EUCKR dagger pair", WEB_ENC_EUC_KR, "\u2020", "\xA2\xD3", 0),
        VECTOR("ISO2022JP Japanese state resets at EOF", WEB_ENC_ISO_2022_JP, "\u3042", "\x1B$B$\"\x1B(B", 0),
        VECTOR("ISO2022JP Roman state spans consecutive scalars", WEB_ENC_ISO_2022_JP, "\u00A5\u00A5", "\x1B(J\\\\\x1B(B", 0),
        VECTOR("ISO2022JP overline then literal reverse-solidus", WEB_ENC_ISO_2022_JP, "\u203E\\", "\x1B(J~\x1B(B\\", 0),
        VECTOR("ISO2022JP Japanese then ASCII resets before ASCII", WEB_ENC_ISO_2022_JP, "\u3042" "A", "\x1B$B$\"\x1B(B" "A", 0),
        VECTOR("ISO2022JP halfwidth full stop uses official fullwidth index", WEB_ENC_ISO_2022_JP, "\uFF61", "\x1B$B!#\x1B(B", 0),
        VECTOR("ISO2022JP prohibited shifts report replacement scalar", WEB_ENC_ISO_2022_JP, "\x0E\x0F\x1B" "A", "<65533><65533><65533>A", 3),
        VECTOR("ISO2022JP error after Japanese first returns to ASCII", WEB_ENC_ISO_2022_JP, "\u3042\U0001F600" "B", "\x1B$B$\"\x1B(B<128512>B", 1),
        VECTOR("ISO2022JP error in Roman preserves its state", WEB_ENC_ISO_2022_JP, "\u00A5\U0001F600\u00A5", "\x1B(J\\<128512>\\\x1B(B", 1),
        VECTOR("ISO2022JP shift after Japanese reports replacement", WEB_ENC_ISO_2022_JP, "\u3042\x0E" "A", "\x1B$B$\"\x1B(B<65533>A", 1),
        VECTOR("x-user-defined private-use boundaries", WEB_ENC_X_USER_DEFINED, "\uF780\uF7FF", "\x80\xFF", 0),
        VECTOR("x-user-defined excludes nearby private-use values", WEB_ENC_X_USER_DEFINED, "\uF77F\uF800", "<63359><63488>", 2),
    };
    for (size_t index = 0; index < ARRLEN(cases); ++index) {
        const EncodeCase *entry = &cases[index];
        Recorder recorder = {0}; sb_init(&recorder.output); recorder.maximum = 4096;
        bool ok = web_encoding_encode_n(entry->encoding, entry->input, entry->input_length,
                                         record_byte, record_error, &recorder);
        CHECK(entry->name, ok && recorder.output.n == entry->expected_length &&
              recorder.error_calls == entry->errors &&
              (!recorder.output.n || !memcmp(recorder.output.s, entry->expected, recorder.output.n)));
        sb_free(&recorder.output);
    }

    for (int encoding = 0; encoding < WEB_ENC_COUNT; ++encoding) {
        Recorder recorder = {0}; sb_init(&recorder.output); recorder.maximum = 64;
        const char input[] = "A~\\ %=\0Z";
        bool ok = web_encoding_encode_n((WebEncoding)encoding, input, sizeof(input) - 1,
                                         record_byte, record_error, &recorder);
        char name[128]; snprintf(name, sizeof name, "%s preserves ordinary ASCII and NUL", web_encoding_name((WebEncoding)encoding));
        CHECK(name, ok && recorder.error_calls == 0 && recorder.output.n == sizeof(input) - 1 &&
                    !memcmp(recorder.output.s, input, sizeof(input) - 1));
        sb_free(&recorder.output);
    }

    const struct { const char *bytes; size_t length; } malformed[] = {
        {"\xC0\xAF", 2}, {"\xED\xA0\x80", 3}, {"\xF4\x90\x80\x80", 4},
        {"\x80", 1}, {"\xE2\x80", 2}, {"\xE2" "A", 2},
    };
    for (size_t index = 0; index < ARRLEN(malformed); ++index) {
        Recorder recorder = {0}; sb_init(&recorder.output); recorder.maximum = 64;
        CHECK("Malformed internal scalar input is rejected without an encoder error event",
              !web_encoding_encode_n(WEB_ENC_UTF8, malformed[index].bytes, malformed[index].length,
                                      record_byte, record_error, &recorder) && recorder.output.n == 0 && !recorder.error_calls);
        sb_free(&recorder.output);
    }

    Recorder recorder = {0}; sb_init(&recorder.output); recorder.maximum = 1;
    CHECK("Byte sink rejection stops before later input", !web_encoding_encode_n(WEB_ENC_UTF8, "abc", 3,
          record_byte, record_error, &recorder) && recorder.byte_calls == 2 && recorder.output.n == 1);
    sb_clear(&recorder.output); recorder.byte_calls = recorder.error_calls = 0;
    recorder.maximum = 100; recorder.reject_errors = true;
    CHECK("Error sink rejection stops before later input", !web_encoding_encode_n(WEB_ENC_WINDOWS_1252, "\U0001F600" "A", 5,
          record_byte, record_error, &recorder) && recorder.error_calls == 1 && recorder.byte_calls == 0);
    CHECK("Unknown encoding is rejected before callbacks", !web_encoding_encode_n(WEB_ENC_UNKNOWN, "a", 1,
          record_byte, record_error, &recorder) && recorder.byte_calls == 0 && recorder.error_calls == 1);
    CHECK("Null nonempty input is rejected", !web_encoding_encode_n(WEB_ENC_UTF8, NULL, 1,
          record_byte, record_error, &recorder));
    CHECK("Null empty input is a valid empty scalar string", web_encoding_encode_n(WEB_ENC_UTF8, NULL, 0,
          record_byte, record_error, &recorder));
    CHECK("Missing byte sink is rejected", !web_encoding_encode_n(WEB_ENC_UTF8, "", 0, NULL, record_error, &recorder));
    CHECK("Missing error sink is rejected", !web_encoding_encode_n(WEB_ENC_UTF8, "", 0, record_byte, NULL, &recorder));
    CHECK("Invalid output-encoding enum remains unknown", web_encoding_output((WebEncoding)WEB_ENC_COUNT) == WEB_ENC_UNKNOWN);
    sb_free(&recorder.output);
    printf("encoding-encoder: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
