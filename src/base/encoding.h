#ifndef LUMEN_BASE_ENCODING_H
#define LUMEN_BASE_ENCODING_H
#include "util.h"

/* The zero value preserves the existing UTF-8 default for zero-initialized
 * documents and all modern URL API callers. Tables are shared read-only data. */
typedef enum {
    WEB_ENC_UNKNOWN = -1,
    WEB_ENC_UTF8 = 0,
    WEB_ENC_IBM866,
    WEB_ENC_ISO_8859_2, WEB_ENC_ISO_8859_3, WEB_ENC_ISO_8859_4,
    WEB_ENC_ISO_8859_5, WEB_ENC_ISO_8859_6, WEB_ENC_ISO_8859_7,
    WEB_ENC_ISO_8859_8, WEB_ENC_ISO_8859_8_I, WEB_ENC_ISO_8859_10,
    WEB_ENC_ISO_8859_13, WEB_ENC_ISO_8859_14, WEB_ENC_ISO_8859_15,
    WEB_ENC_ISO_8859_16, WEB_ENC_KOI8_R, WEB_ENC_KOI8_U,
    WEB_ENC_MACINTOSH, WEB_ENC_WINDOWS_874,
    WEB_ENC_WINDOWS_1250, WEB_ENC_WINDOWS_1251, WEB_ENC_WINDOWS_1252,
    WEB_ENC_WINDOWS_1253, WEB_ENC_WINDOWS_1254, WEB_ENC_WINDOWS_1255,
    WEB_ENC_WINDOWS_1256, WEB_ENC_WINDOWS_1257, WEB_ENC_WINDOWS_1258,
    WEB_ENC_X_MAC_CYRILLIC,
    WEB_ENC_GBK, WEB_ENC_GB18030, WEB_ENC_BIG5, WEB_ENC_EUC_JP,
    WEB_ENC_ISO_2022_JP, WEB_ENC_SHIFT_JIS, WEB_ENC_EUC_KR,
    WEB_ENC_REPLACEMENT, WEB_ENC_UTF16BE, WEB_ENC_UTF16LE,
    WEB_ENC_X_USER_DEFINED,
    WEB_ENC_COUNT
} WebEncoding;

/* Byte-counted label and MIME parsing; unknown labels remain explicit. */
WebEncoding web_encoding_label_n(const char *label, size_t length);
WebEncoding web_encoding_mime_n(const char *mime, size_t length);
const char *web_encoding_name(WebEncoding encoding);

/* Append replacement-decoded Unicode as UTF-8. An optional BOM override is
 * the Encoding Standard's legacy decode hook used by FileReader. max_output
 * bounds the total builder length; false denotes an invalid input contract or
 * output-size overflow, not a malformed byte sequence. Mutable decoder state
 * is local to this call; the lookup tables are immutable and process-shared. */
bool web_encoding_decode_n(SB *output, WebEncoding encoding,
                           const char *bytes, size_t length,
                           bool sniff_bom, size_t max_output);

/* As above, with a fatal error mode. had_error distinguishes malformed
 * sequences from a valid decoded U+FFFD and from output-size failure. */
bool web_encoding_decode_checked_n(SB *output, WebEncoding encoding,
                                   const char *bytes, size_t length,
                                   bool sniff_bom, size_t max_output,
                                   bool fatal, bool *had_error);

/* Fixed-size state for the legacy incremental decoder. Zero initialization
 * starts a new decoding session. pending holds only bytes the standard asks
 * to reconsume; input chunks are never retained by this structure. */
typedef struct {
    uint32_t leading, second, third, mode, output_mode, output_flag, jis0212;
    uint32_t pending_count, pending[3];
} WebEncodingDecoder;

/* Decode one chunk using persistent state; stream=true does not feed EOF.
 * A fatal error returns false with had_error=true, preserving decoder state
 * and its reconsume queue. consumed identifies the first unconsumed byte in
 * this input chunk. The caller owns that remaining tail until its next call.
 * Nonfatal replacement errors return true unless the output bound is exceeded.
 * UTF-8/UTF-16 use their separately tested Unicode streaming implementation. */
bool web_encoding_decode_stream_n(SB *output, WebEncoding encoding,
                                  WebEncodingDecoder *state,
                                  const char *bytes, size_t length, bool stream,
                                  size_t max_output, bool fatal,
                                  size_t *consumed, bool *had_error);

/* Output encodings map replacement and UTF-16 to UTF-8. Unknown/invalid input
 * remains UNKNOWN; it is not silently accepted as an encoding. */
WebEncoding web_encoding_output(WebEncoding encoding);

typedef bool (*WebEncodingByteSink)(void *context, unsigned char byte);
typedef bool (*WebEncodingErrorSink)(void *context, uint32_t codepoint);

/* Encode a complete scalar-value string supplied as valid UTF-8. One encoder
 * state spans the whole call, including ISO-2022-JP's final ASCII reset.
 * Byte and unrepresentable-codepoint events remain separate so URL and HTML
 * callers can apply their own specified error serialization without losing
 * state. A false sink result, malformed UTF-8 or invalid API input stops the
 * operation with false; callers must discard any partial output in that case.
 * The function has no heap allocation or global mutable encoder state. */
bool web_encoding_encode_n(WebEncoding encoding, const char *utf8, size_t length,
                           WebEncodingByteSink byte_sink,
                           WebEncodingErrorSink error_sink, void *context);

#endif
