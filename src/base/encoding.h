/* The WHATWG Encoding Standard: labels, the decoders of all its encodings and the encoders forms and URLs use. */
#ifndef LUMEN_ENCODING_H
#define LUMEN_ENCODING_H
#include "util.h"

enum { ENCODING_UTF8 = 0, ENCODING_WINDOWS_1252 = 21, ENCODING_REPLACEMENT = 36, ENCODING_UTF16BE = 37, ENCODING_UTF16LE = 38,
       ENCODING_X_USER_DEFINED = 39 };

int enc_label(const char *label, size_t n);   /* "get an encoding": the encoding id, or -1 */
const char *enc_name(int enc);                /* its canonical name, e.g. "windows-1252" */
int enc_output(int enc);                      /* "get an output encoding": replacement and UTF-16 become UTF-8 */

typedef struct {
    uint8_t enc, fatal, lead, flag, b1, b2, b3, st, ost, bom, pn;
    uint8_t pend[3];
    uint32_t cp, needed, seen, lower, upper;
} EncDecoder;
void enc_decoder_init(EncDecoder *d, int enc, bool fatal, bool strip_bom);
/* decodes the next chunk into UTF-8 appended to out; last flushes the decoder (an unfinished sequence is an error).
   Returns false on an error when fatal; errors otherwise become U+FFFD. */
bool enc_decode(EncDecoder *d, const uint8_t *in, size_t n, bool last, SB *out);
char *enc_decode_all(int enc, const uint8_t *in, size_t n, size_t *out_len);   /* whole input, U+FFFD for errors */

typedef struct { uint8_t enc, st; } EncEncoder;
/* encodes one code point; returns -1 with the bytes in out[*n], or the code point the encoding cannot represent */
int32_t enc_encode_cp(EncEncoder *e, uint32_t cp, uint8_t out[8], int *n);
int enc_encode_end(EncEncoder *e, uint8_t out[8]);   /* bytes that return a stateful encoder to its initial state */
/* UTF-8 text in encoding enc (an output encoding); an unmappable code point N becomes "&#N;" */
char *enc_encode_html(int enc, const char *s, size_t n, size_t *out_len);
/* percent-encodes UTF-8 text after encoding it in enc: bytes for which encode_set(byte) is true become %XX, an unmappable
   code point N becomes %26%23N%3B and, with space_plus, a space becomes '+' */
void enc_percent_encode(int enc, const char *s, size_t n, bool (*encode_set)(unsigned char), bool space_plus, SB *out);

/* the encoding a Content-Type header's charset parameter names, or -1 */
int enc_from_content_type(const char *content_type);
/* HTML's prescan of a document's first 1024 bytes for a <meta> charset; -1 if there is none */
int enc_prescan(const uint8_t *s, size_t n);
/* a document's encoding: BOM, then the transport charset, the <meta> prescan, then UTF-8 if the bytes are valid UTF-8,
   else windows-1252. *bom_len gets the length of the BOM to skip. */
int enc_sniff(const uint8_t *s, size_t n, const char *content_type, int *bom_len);
bool enc_valid_utf8(const uint8_t *s, size_t n);
/* a document's bytes as UTF-8 (HTML sniffing, or for XML the BOM, charset and XML declaration); *enc gets the
   encoding. NULL when the bytes are UTF-8 without a BOM and are used as they are. */
char *enc_document(const uint8_t *s, size_t n, const char *content_type, bool xml, int *enc, size_t *out_len);

#endif
