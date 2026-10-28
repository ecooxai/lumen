#include "../src/base/encoding.h"
#include <assert.h>

/* Chunking must not alter the independently implemented complete decoder's
 * output. Exhausting byte pairs exercises every legacy pointer, error trail,
 * halfwidth character, and the Big5 two-codepoint mappings under sanitizers. */
static unsigned long cases;

static void compare(WebEncoding encoding, const unsigned char *input,
                    size_t length, size_t split) {
    SB whole, chunks; sb_init(&whole); sb_init(&chunks);
    WebEncodingDecoder state = {0};
    bool whole_error = false, first_error = false, last_error = false;
    size_t consumed = 99;
    assert(web_encoding_decode_checked_n(&whole, encoding, (const char *)input,
                                          length, false, 1024, false, &whole_error));
    assert(web_encoding_decode_stream_n(&chunks, encoding, &state,
        (const char *)input, split, true, 1024, false, &consumed, &first_error));
    assert(consumed == split && state.pending_count <= 3);
    assert(web_encoding_decode_stream_n(&chunks, encoding, &state,
        (const char *)input + split, length - split, false, 1024, false, &consumed, &last_error));
    assert(consumed == length - split && state.pending_count == 0);
    if (whole.n != chunks.n || (whole.n && memcmp(whole.s, chunks.s, whole.n))) {
        fprintf(stderr, "legacy chunk mismatch encoding=%s length=%zu split=%zu bytes=",
                web_encoding_name(encoding), length, split);
        for (size_t i = 0; i < length; ++i) fprintf(stderr, "%02X", input[i]);
        fputc('\n', stderr); abort();
    }
    assert(whole_error == (first_error || last_error));
    sb_free(&whole); sb_free(&chunks); ++cases;
}

static void fatal_reconsume(void) {
    WebEncodingDecoder state = {0};
    SB out; sb_init(&out); size_t consumed; bool error;
    const char first[] = {(char)0x81, 0x30, (char)0x81};
    assert(web_encoding_decode_stream_n(&out, WEB_ENC_GB18030, &state,
        first, sizeof first, true, 128, true, &consumed, &error));
    assert(consumed == 3 && !out.n && !error);
    const char second[] = {0x20, 65};
    assert(!web_encoding_decode_stream_n(&out, WEB_ENC_GB18030, &state,
        second, sizeof second, true, 128, true, &consumed, &error));
    assert(consumed == 1 && error && state.pending_count == 3);
    /* A fatal call throws away its own partial output; the caller retains
     * only unconsumed new bytes and prepends them to the following chunk. */
    sb_clear(&out);
    const char tail[] = {65, 66};
    assert(!web_encoding_decode_stream_n(&out, WEB_ENC_GB18030, &state,
        tail, sizeof tail, true, 128, true, &consumed, &error));
    assert(consumed == 0 && error && state.pending_count == 1);
    sb_clear(&out);
    assert(web_encoding_decode_stream_n(&out, WEB_ENC_GB18030, &state,
        tail, sizeof tail, false, 128, true, &consumed, &error));
    assert(consumed == 2 && !error && out.n == 3 && !memcmp(out.s, " AB", 3));
    sb_free(&out); ++cases;
}

static void fatal_iso_flag(void) {
    WebEncodingDecoder state = {0};
    SB out; sb_init(&out); size_t consumed; bool error;
    const char escape[] = {0x1B, 0x28, 0x42};
    assert(web_encoding_decode_stream_n(&out, WEB_ENC_ISO_2022_JP, &state,
        escape, sizeof escape, true, 128, true, &consumed, &error));
    for (int repetition = 0; repetition < 2; ++repetition) {
        assert(!web_encoding_decode_stream_n(&out, WEB_ENC_ISO_2022_JP, &state,
            escape, sizeof escape, true, 128, true, &consumed, &error));
        assert(error && consumed == 3 && state.output_flag == 1);
    }
    assert(web_encoding_decode_stream_n(&out, WEB_ENC_ISO_2022_JP, &state,
        "A", 1, false, 128, true, &consumed, &error));
    assert(!error && consumed == 1 && out.n == 1 && out.s[0] == 'A');
    sb_free(&out); ++cases;
}

static void bounds(void) {
    WebEncodingDecoder state = {0};
    SB out; sb_init(&out); size_t consumed; bool error;
    assert(!web_encoding_decode_stream_n(&out, WEB_ENC_SHIFT_JIS, &state,
        "A", 1, true, 0, false, &consumed, &error));
    assert(consumed == 1 && !error && out.n == 0);
    state.pending_count = 4;
    assert(!web_encoding_decode_stream_n(&out, WEB_ENC_SHIFT_JIS, &state,
        "A", 1, true, 128, false, &consumed, &error));
    assert(consumed == 0 && out.n == 0);
    memset(&state, 0, sizeof state);
    assert(!web_encoding_decode_stream_n(&out, WEB_ENC_SHIFT_JIS, &state,
        NULL, 1, true, 128, false, &consumed, &error));
    assert(consumed == 0);
    state.pending_count = 1; state.pending[0] = 256;
    assert(!web_encoding_decode_stream_n(&out, WEB_ENC_SHIFT_JIS, &state,
        "A", 1, true, 128, false, &consumed, &error));
    assert(consumed == 0);
    sb_free(&out); ++cases;
}

int main(void) {
    const WebEncoding encodings[] = {WEB_ENC_GBK, WEB_ENC_GB18030, WEB_ENC_BIG5,
        WEB_ENC_EUC_JP, WEB_ENC_ISO_2022_JP, WEB_ENC_SHIFT_JIS, WEB_ENC_EUC_KR};
    for (size_t encoding = 0; encoding < ARRLEN(encodings); ++encoding) {
        for (unsigned pair = 0; pair < 65536; ++pair) {
            unsigned char input[] = {(unsigned char)(pair >> 8), (unsigned char)pair};
            compare(encodings[encoding], input, sizeof input, 1);
        }
        const unsigned char corpus[][12] = {
            {0x81,0x30,0x81,0x20,65,0x81,0x30,0x20,0,0xFF,0x80,0x7F},
            {0x1B,0x28,0x49,0x50,0x1B,0x24,0x42,0x50,0x50,0x1B,0x24,0x50},
            {0x8F,0xA2,0x20,0xA1,0xA1,0x8F,0xA2,0xAF,0x8E,0xA1,0xFF,0},
            {0x88,0x62,0x88,0x64,0x88,0xA3,0x88,0xA5,0x1B,0x28,0x42,0x1B},
        };
        for (size_t row = 0; row < ARRLEN(corpus); ++row)
            for (size_t split = 0; split <= sizeof corpus[row]; ++split)
                compare(encodings[encoding], corpus[row], sizeof corpus[row], split);
    }
    fatal_reconsume(); fatal_iso_flag(); bounds();
    printf("RESULTS legacy-native PASS cases=%lu persistentStateBytes=%zu\n", cases, sizeof(WebEncodingDecoder));
    return 0;
}
