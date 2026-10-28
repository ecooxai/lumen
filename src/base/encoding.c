/* Encoding Standard decoders, using pinned official index data.
 * https://encoding.spec.whatwg.org/#decoders
 * Byte errors emit U+FFFD and reconsume required ASCII bytes. No locale,
 * platform iconv alias, mutable global decoder, or per-document map is used.
 */
#include "encoding.h"

typedef struct { const char *label; WebEncoding encoding; } WebEncodingLabel;
typedef struct { const char *name; const uint32_t *single_byte; } WebEncodingMetadata;
typedef struct { uint32_t pointer, codepoint; } WebEncodingRange;

#include "encoding_decode_data.inc"

#define MISSING 0xFFFFFFFFu
#define EMIT(cp) do { if (!emit_checked(output, (cp), max_output, fatal, had_error)) return false; } while (0)

static bool ascii_space(unsigned char c) {
    return c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D || c == 0x20;
}
static bool http_space(unsigned char c) {
    return c == 0x09 || c == 0x0A || c == 0x0D || c == 0x20;
}
static bool token(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '!' || c == '#' || c == '$' ||
        c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
        c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}
static bool ascii_equal(const char *s, size_t n, const char *literal) {
    if (strlen(literal) != n) return false;
    for (size_t i = 0; i < n; ++i) if (lc((unsigned char)s[i]) != literal[i]) return false;
    return true;
}

WebEncoding web_encoding_label_n(const char *label, size_t length) {
    if (!label) return WEB_ENC_UNKNOWN;
    while (length && ascii_space((unsigned char)*label)) { ++label; --length; }
    while (length && ascii_space((unsigned char)label[length - 1])) --length;
    if (!length || length > 80) return WEB_ENC_UNKNOWN;
    char key[81];
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)label[i];
        if (!c || c >= 0x80) return WEB_ENC_UNKNOWN;
        key[i] = (char)lc(c);
    }
    key[length] = 0;
    size_t lo = 0, hi = ARRLEN(kEncodingLabels);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int order = strcmp(key, kEncodingLabels[mid].label);
        if (!order) return kEncodingLabels[mid].encoding;
        if (order < 0) hi = mid; else lo = mid + 1;
    }
    return WEB_ENC_UNKNOWN;
}

const char *web_encoding_name(WebEncoding encoding) {
    return encoding >= 0 && encoding < WEB_ENC_COUNT ? kEncodingMetadata[encoding].name : NULL;
}

/* MIME Sniffing's MIME parser: validate the essence, then retain the first
 * syntactically valid charset parameter. Quoted backslash escapes are decoded;
 * an unknown first charset does not authorize a later duplicate parameter.
 * Only a bounded label buffer is needed, even for a very long media type.
 */
WebEncoding web_encoding_mime_n(const char *mime, size_t length) {
    if (!mime) return WEB_ENC_UNKNOWN;
    while (length && http_space((unsigned char)*mime)) { ++mime; --length; }
    while (length && http_space((unsigned char)mime[length - 1])) --length;
    size_t pos = 0, begin = 0;
    while (pos < length && mime[pos] != '/') {
        if (!token((unsigned char)mime[pos])) return WEB_ENC_UNKNOWN;
        ++pos;
    }
    if (!pos || pos == length) return WEB_ENC_UNKNOWN;
    begin = ++pos;
    while (pos < length && mime[pos] != ';') ++pos;
    size_t end = pos;
    while (end > begin && http_space((unsigned char)mime[end - 1])) --end;
    if (end == begin) return WEB_ENC_UNKNOWN;
    for (size_t i = begin; i < end; ++i) if (!token((unsigned char)mime[i])) return WEB_ENC_UNKNOWN;
    while (pos < length) {
        ++pos; /* semicolon */
        while (pos < length && http_space((unsigned char)mime[pos])) ++pos;
        begin = pos;
        while (pos < length && mime[pos] != ';' && mime[pos] != '=') ++pos;
        end = pos;
        if (pos == length) break;
        if (mime[pos] == ';') continue;
        ++pos;
        bool valid_name = end > begin;
        for (size_t i = begin; i < end; ++i) valid_name &= token((unsigned char)mime[i]);
        const bool charset = valid_name && ascii_equal(mime + begin, end - begin, "charset");
        char label[81]; size_t used = 0; bool valid_value = true;
        if (pos < length && mime[pos] == '"') {
            ++pos;
            while (pos < length) {
                unsigned char c = (unsigned char)mime[pos++];
                if (c == '"') break;
                if (c == '\\' && pos < length) c = (unsigned char)mime[pos++];
                valid_value &= c == 0x09 || (c >= 0x20 && c != 0x7F);
                if (used < sizeof label) label[used] = (char)c;
                ++used;
            }
            while (pos < length && mime[pos] != ';') ++pos;
        } else {
            size_t value_begin = pos;
            while (pos < length && mime[pos] != ';') ++pos;
            size_t value_end = pos;
            while (value_end > value_begin && http_space((unsigned char)mime[value_end - 1])) --value_end;
            used = value_end - value_begin;
            if (!used) continue;
            for (size_t i = 0; i < used; ++i) {
                unsigned char c = (unsigned char)mime[value_begin + i];
                valid_value &= c == 0x09 || (c >= 0x20 && c != 0x7F);
                if (i < sizeof label) label[i] = (char)c;
            }
        }
        if (charset && valid_value)
            return used <= 80 ? web_encoding_label_n(label, used) : WEB_ENC_UNKNOWN;
    }
    return WEB_ENC_UNKNOWN;
}

static bool emit(SB *output, uint32_t codepoint, size_t maximum) {
    if (codepoint == MISSING) codepoint = 0xFFFD;
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;
    char bytes[4];
    int n = utf8_encode(codepoint, bytes);
    if (n < 1 || n > 4 || output->n > maximum || (size_t)n > maximum - output->n) return false;
    /* SB grows geometrically; keep even the next doubling representable. */
    if (output->n > SIZE_MAX / 2 - 5) return false;
    sb_put(output, bytes, (size_t)n);
    return true;
}

/* MISSING denotes a decoder error; a literal decoded U+FFFD remains valid. */
static bool emit_checked(SB *output, uint32_t codepoint, size_t maximum,
                         bool fatal, bool *had_error) {
    if (codepoint == MISSING) {
        if (had_error) *had_error = true;
        if (fatal) return false;
    }
    return emit(output, codepoint, maximum);
}

static uint32_t lookup(const uint32_t *table, size_t count, uint32_t pointer) {
    return pointer < count ? table[pointer] : MISSING;
}

static uint32_t gb_range(uint32_t pointer) {
    if ((pointer > 39419 && pointer < 189000) || pointer > 1237575) return MISSING;
    if (pointer == 7457) return 0xE7C7;
    size_t lo = 0, hi = ARRLEN(kDecodeGb18030Ranges);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kDecodeGb18030Ranges[mid].pointer <= pointer) lo = mid + 1; else hi = mid;
    }
    if (!lo) return MISSING;
    const WebEncodingRange *range = &kDecodeGb18030Ranges[lo - 1];
    return range->codepoint + pointer - range->pointer;
}

static bool decode_utf8(SB *output, const unsigned char *bytes, size_t length, size_t max_output, bool fatal, bool *had_error) {
    size_t pos = 0;
    while (pos < length) {
        unsigned char first = bytes[pos++];
        if (first < 0x80) { EMIT(first); continue; }
        unsigned need = 0, low = 0x80, high = 0xBF;
        uint32_t point;
        if (first >= 0xC2 && first <= 0xDF) { need = 1; point = first & 0x1F; }
        else if (first >= 0xE0 && first <= 0xEF) {
            need = 2; point = first & 0x0F;
            if (first == 0xE0) low = 0xA0;
            if (first == 0xED) high = 0x9F;
        } else if (first >= 0xF0 && first <= 0xF4) {
            need = 3; point = first & 7;
            if (first == 0xF0) low = 0x90;
            if (first == 0xF4) high = 0x8F;
        } else { EMIT(MISSING); continue; }
        bool complete = true;
        for (unsigned index = 0; index < need; ++index) {
            if (pos == length || bytes[pos] < low || bytes[pos] > high) {
                complete = false; break; /* offending byte is reconsumed */
            }
            point = (point << 6) | (bytes[pos++] & 0x3F);
            low = 0x80; high = 0xBF;
        }
        EMIT(complete ? point : MISSING);
    }
    return true;
}

static bool decode_utf16(SB *output, const unsigned char *bytes, size_t length,
                         bool big_endian, size_t max_output, bool fatal, bool *had_error) {
    size_t pos = 0; uint32_t high = 0;
    while (length - pos >= 2) {
        uint32_t unit = big_endian ? ((uint32_t)bytes[pos] << 8) | bytes[pos + 1] :
                                    ((uint32_t)bytes[pos + 1] << 8) | bytes[pos];
        pos += 2;
        if (high) {
            uint32_t previous = high; high = 0;
            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                EMIT(0x10000 + ((previous - 0xD800) << 10) + unit - 0xDC00);
                continue;
            }
            EMIT(MISSING); /* reconsume the complete non-low code unit */
        }
        if (unit >= 0xD800 && unit <= 0xDBFF) high = unit;
        else EMIT(unit >= 0xDC00 && unit <= 0xDFFF ? MISSING : unit);
    }
    /* A pending high surrogate and one trailing byte share the EOF error. */
    if (high || pos < length) EMIT(MISSING);
    return true;
}

static bool decode_gb18030(SB *output, const unsigned char *bytes, size_t length, size_t max_output, bool fatal, bool *had_error) {
    size_t pos = 0;
    while (pos < length) {
        unsigned first = bytes[pos++];
        if (first < 0x80) { EMIT(first); continue; }
        if (first == 0x80) { EMIT(0x20AC); continue; }
        if (first < 0x81 || first > 0xFE || pos == length) { EMIT(MISSING); continue; }
        unsigned second = bytes[pos];
        if (second >= 0x30 && second <= 0x39) {
            if (length - pos < 2) { pos = length; EMIT(MISSING); continue; }
            unsigned third = bytes[pos + 1];
            if (third < 0x81 || third > 0xFE) { EMIT(MISSING); continue; }
            if (length - pos < 3) { pos = length; EMIT(MISSING); continue; }
            unsigned fourth = bytes[pos + 2];
            if (fourth < 0x30 || fourth > 0x39) { EMIT(MISSING); continue; }
            uint32_t pointer = (first - 0x81) * 12600 + (second - 0x30) * 1260 +
                               (third - 0x81) * 10 + fourth - 0x30;
            pos += 3; EMIT(gb_range(pointer)); continue;
        }
        uint32_t point = MISSING;
        if ((second >= 0x40 && second <= 0x7E) || (second >= 0x80 && second <= 0xFE))
            point = lookup(kDecodeGb18030, ARRLEN(kDecodeGb18030),
                           (first - 0x81) * 190 + second - (second < 0x7F ? 0x40 : 0x41));
        if (point != MISSING || second >= 0x80) ++pos;
        EMIT(point);
    }
    return true;
}

static bool decode_big5(SB *output, const unsigned char *bytes, size_t length, size_t max_output, bool fatal, bool *had_error) {
    size_t pos = 0;
    while (pos < length) {
        unsigned first = bytes[pos++];
        if (first < 0x80) { EMIT(first); continue; }
        if (first < 0x81 || first > 0xFE || pos == length) { EMIT(MISSING); continue; }
        unsigned second = bytes[pos]; uint32_t pointer = MISSING;
        if ((second >= 0x40 && second <= 0x7E) || (second >= 0xA1 && second <= 0xFE))
            pointer = (first - 0x81) * 157 + second - (second < 0x7F ? 0x40 : 0x62);
        if (pointer == 1133 || pointer == 1135 || pointer == 1164 || pointer == 1166) {
            ++pos; EMIT(pointer < 1164 ? 0xCA : 0xEA);
            EMIT(pointer == 1133 || pointer == 1164 ? 0x304 : 0x30C); continue;
        }
        uint32_t point = lookup(kDecodeBig5, ARRLEN(kDecodeBig5), pointer);
        if (point != MISSING || second >= 0x80) ++pos;
        EMIT(point);
    }
    return true;
}

static bool decode_euc_jp(SB *output, const unsigned char *bytes, size_t length, size_t max_output, bool fatal, bool *had_error) {
    size_t pos = 0;
    while (pos < length) {
        unsigned first = bytes[pos++];
        if (first < 0x80) { EMIT(first); continue; }
        if (!(first == 0x8E || first == 0x8F || (first >= 0xA1 && first <= 0xFE)) ||
            pos == length) { EMIT(MISSING); continue; }
        unsigned second = bytes[pos]; bool jis0212 = false;
        if (first == 0x8E && second >= 0xA1 && second <= 0xDF) {
            ++pos; EMIT(0xFF61 + second - 0xA1); continue;
        }
        if (first == 0x8F && second >= 0xA1 && second <= 0xFE) {
            first = second; ++pos; jis0212 = true;
            if (pos == length) { EMIT(MISSING); continue; }
            second = bytes[pos];
        }
        uint32_t point = MISSING;
        if (first >= 0xA1 && first <= 0xFE && second >= 0xA1 && second <= 0xFE) {
            uint32_t pointer = (first - 0xA1) * 94 + second - 0xA1;
            point = jis0212 ? lookup(kDecodeJis0212, ARRLEN(kDecodeJis0212), pointer) :
                              lookup(kDecodeJis0208, ARRLEN(kDecodeJis0208), pointer);
        }
        if (point != MISSING || second >= 0x80) ++pos;
        EMIT(point);
    }
    return true;
}

static bool decode_shift_jis(SB *output, const unsigned char *bytes, size_t length, size_t max_output, bool fatal, bool *had_error) {
    size_t pos = 0;
    while (pos < length) {
        unsigned first = bytes[pos++];
        if (first <= 0x80) { EMIT(first); continue; }
        if (first >= 0xA1 && first <= 0xDF) { EMIT(0xFF61 + first - 0xA1); continue; }
        if (!((first >= 0x81 && first <= 0x9F) || (first >= 0xE0 && first <= 0xFC)) ||
            pos == length) { EMIT(MISSING); continue; }
        unsigned second = bytes[pos]; uint32_t pointer = MISSING;
        if ((second >= 0x40 && second <= 0x7E) || (second >= 0x80 && second <= 0xFC))
            pointer = (first - (first < 0xA0 ? 0x81 : 0xC1)) * 188 +
                       second - (second < 0x7F ? 0x40 : 0x41);
        uint32_t point = pointer >= 8836 && pointer <= 10715 ? 0xE000 + pointer - 8836 :
                         lookup(kDecodeJis0208, ARRLEN(kDecodeJis0208), pointer);
        if (point != MISSING || second >= 0x80) ++pos;
        EMIT(point);
    }
    return true;
}

static bool decode_euc_kr(SB *output, const unsigned char *bytes, size_t length, size_t max_output, bool fatal, bool *had_error) {
    size_t pos = 0;
    while (pos < length) {
        unsigned first = bytes[pos++];
        if (first < 0x80) { EMIT(first); continue; }
        if (first < 0x81 || first > 0xFE || pos == length) { EMIT(MISSING); continue; }
        unsigned second = bytes[pos];
        uint32_t point = second >= 0x41 && second <= 0xFE ?
            lookup(kDecodeEucKr, ARRLEN(kDecodeEucKr), (first - 0x81) * 190 + second - 0x41) : MISSING;
        if (point != MISSING || second >= 0x80) ++pos;
        EMIT(point);
    }
    return true;
}

static bool decode_iso2022_jp(SB *output, const unsigned char *bytes, size_t length, size_t max_output, bool fatal, bool *had_error) {
    enum { ASCII, ROMAN, KATAKANA, LEADING, TRAILING, ESCAPE_START, ESCAPE };
    int state = ASCII, output_state = ASCII; unsigned leading = 0;
    bool escaped_output = false;
    size_t pos = 0;
    for (;;) {
        const bool eof = pos == length;
        unsigned byte = eof ? 256 : bytes[pos++];
        switch (state) {
            case ASCII: case ROMAN: case KATAKANA: case LEADING:
                if (eof) return true;
                if (byte == 0x1B) { state = ESCAPE_START; break; }
                escaped_output = false;
                if (state == LEADING) {
                    if (byte >= 0x21 && byte <= 0x7E) { leading = byte; state = TRAILING; }
                    else EMIT(MISSING);
                } else if (state == KATAKANA) {
                    EMIT(byte >= 0x21 && byte <= 0x5F ? 0xFF61 + byte - 0x21 : MISSING);
                } else if (byte <= 0x7F && byte != 0x0E && byte != 0x0F) {
                    EMIT(state == ROMAN && byte == 0x5C ? 0xA5 :
                         state == ROMAN && byte == 0x7E ? 0x203E : byte);
                } else EMIT(MISSING);
                break;
            case TRAILING:
                if (byte == 0x1B) { state = ESCAPE_START; EMIT(MISSING); break; }
                state = LEADING;
                EMIT(byte >= 0x21 && byte <= 0x7E ?
                     lookup(kDecodeJis0208, ARRLEN(kDecodeJis0208), (leading - 0x21) * 94 + byte - 0x21) : MISSING);
                break;
            case ESCAPE_START:
                if (byte == 0x24 || byte == 0x28) { leading = byte; state = ESCAPE; break; }
                if (!eof) --pos;
                escaped_output = false; state = output_state; EMIT(MISSING);
                break;
            case ESCAPE: {
                int next = leading == 0x28 && byte == 0x42 ? ASCII :
                           leading == 0x28 && byte == 0x4A ? ROMAN :
                           leading == 0x28 && byte == 0x49 ? KATAKANA :
                           leading == 0x24 && (byte == 0x40 || byte == 0x42) ? LEADING : -1;
                leading = 0;
                if (next >= 0) {
                    state = output_state = next;
                    if (escaped_output) EMIT(MISSING);
                    escaped_output = true;
                } else {
                    /* Reconsume the preceding 0x24/0x28 and current byte. */
                    pos -= eof ? 1 : 2;
                    escaped_output = false; state = output_state; EMIT(MISSING);
                }
                break;
            }
        }
    }
}

/* Incremental legacy decoder state is owned by the caller, never by a global
 * decoder cache. Only three reconsumed bytes can be pending in the WHATWG
 * legacy algorithms. Mapping tables remain the immutable tables above. */
enum { LEGACY_ASCII, LEGACY_ROMAN, LEGACY_KATAKANA, LEGACY_LEADING,
       LEGACY_TRAILING, LEGACY_ESCAPE_START, LEGACY_ESCAPE };
enum { LEGACY_CONTINUE, LEGACY_POINT, LEGACY_PAIR, LEGACY_FINISHED,
       LEGACY_ERROR, LEGACY_INVALID };

static bool legacy_restore(WebEncodingDecoder *state, const uint32_t *bytes,
                           uint32_t count) {
    if (count > 3 || state->pending_count > 3 - count) return false;
    memmove(state->pending + count, state->pending,
            state->pending_count * sizeof state->pending[0]);
    memcpy(state->pending, bytes, count * sizeof state->pending[0]);
    state->pending_count += count;
    return true;
}

static int legacy_mapped(WebEncodingDecoder *state, unsigned byte,
                         uint32_t point, uint32_t output[2]) {
    if (point != MISSING) { output[0] = point; return LEGACY_POINT; }
    if (byte < 0x80 && !legacy_restore(state, (uint32_t[]){ byte }, 1))
        return LEGACY_INVALID;
    return LEGACY_ERROR;
}

static int legacy_iso_step(WebEncodingDecoder *state, unsigned byte,
                           uint32_t output[2]) {
    const bool eof = byte == 256;
    switch (state->mode) {
        case LEGACY_ASCII: case LEGACY_ROMAN:
        case LEGACY_KATAKANA: case LEGACY_LEADING:
            if (eof) return LEGACY_FINISHED;
            if (byte == 0x1B) { state->mode = LEGACY_ESCAPE_START; return LEGACY_CONTINUE; }
            state->output_flag = 0;
            if (state->mode == LEGACY_LEADING) {
                if (byte < 0x21 || byte > 0x7E) return LEGACY_ERROR;
                state->leading = byte; state->mode = LEGACY_TRAILING;
                return LEGACY_CONTINUE;
            }
            if (state->mode == LEGACY_KATAKANA) {
                if (byte < 0x21 || byte > 0x5F) return LEGACY_ERROR;
                output[0] = 0xFF61 + byte - 0x21; return LEGACY_POINT;
            }
            if (byte > 0x7F || byte == 0x0E || byte == 0x0F) return LEGACY_ERROR;
            output[0] = state->mode == LEGACY_ROMAN && byte == 0x5C ? 0xA5 :
                        state->mode == LEGACY_ROMAN && byte == 0x7E ? 0x203E : byte;
            return LEGACY_POINT;
        case LEGACY_TRAILING:
            if (byte == 0x1B) { state->mode = LEGACY_ESCAPE_START; return LEGACY_ERROR; }
            state->mode = LEGACY_LEADING;
            output[0] = byte >= 0x21 && byte <= 0x7E ?
                lookup(kDecodeJis0208, ARRLEN(kDecodeJis0208),
                       (state->leading - 0x21) * 94 + byte - 0x21) : MISSING;
            return output[0] == MISSING ? LEGACY_ERROR : LEGACY_POINT;
        case LEGACY_ESCAPE_START:
            if (byte == 0x24 || byte == 0x28) {
                state->leading = byte; state->mode = LEGACY_ESCAPE;
                return LEGACY_CONTINUE;
            }
            if (!eof && !legacy_restore(state, (uint32_t[]){ byte }, 1)) return LEGACY_INVALID;
            state->output_flag = 0; state->mode = state->output_mode;
            return LEGACY_ERROR;
        case LEGACY_ESCAPE: {
            const unsigned leading = state->leading;
            state->leading = 0;
            int next = leading == 0x28 && byte == 0x42 ? LEGACY_ASCII :
                       leading == 0x28 && byte == 0x4A ? LEGACY_ROMAN :
                       leading == 0x28 && byte == 0x49 ? LEGACY_KATAKANA :
                       leading == 0x24 && (byte == 0x40 || byte == 0x42) ? LEGACY_LEADING : -1;
            if (next >= 0) {
                const bool repeated = state->output_flag != 0;
                state->mode = state->output_mode = (uint32_t)next;
                state->output_flag = 1;
                return repeated ? LEGACY_ERROR : LEGACY_CONTINUE;
            }
            if (!legacy_restore(state, (uint32_t[]){ leading, byte }, eof ? 1 : 2))
                return LEGACY_INVALID;
            state->output_flag = 0; state->mode = state->output_mode;
            return LEGACY_ERROR;
        }
        default: return LEGACY_INVALID;
    }
}

static int legacy_step(WebEncodingDecoder *state, WebEncoding encoding,
                       unsigned byte, uint32_t output[2]) {
    if (encoding == WEB_ENC_ISO_2022_JP) return legacy_iso_step(state, byte, output);
    if (byte == 256) {
        if (state->leading || state->second || state->third) {
            state->leading = state->second = state->third = 0;
            return LEGACY_ERROR;
        }
        return LEGACY_FINISHED;
    }
    if (kEncodingMetadata[encoding].single_byte) {
        output[0] = byte < 0x80 ? byte : kEncodingMetadata[encoding].single_byte[byte - 0x80];
        return output[0] == MISSING ? LEGACY_ERROR : LEGACY_POINT;
    }
    if (encoding == WEB_ENC_X_USER_DEFINED) {
        output[0] = byte < 0x80 ? byte : 0xF700 + byte; return LEGACY_POINT;
    }
    if (encoding == WEB_ENC_GBK || encoding == WEB_ENC_GB18030) {
        if (state->third) {
            if (byte < 0x30 || byte > 0x39) {
                if (!legacy_restore(state, (uint32_t[]){ state->second, state->third, byte }, 3))
                    return LEGACY_INVALID;
                state->leading = state->second = state->third = 0;
                return LEGACY_ERROR;
            }
            const uint32_t pointer = (state->leading - 0x81) * 12600 +
                (state->second - 0x30) * 1260 + (state->third - 0x81) * 10 + byte - 0x30;
            state->leading = state->second = state->third = 0;
            output[0] = gb_range(pointer);
            return output[0] == MISSING ? LEGACY_ERROR : LEGACY_POINT;
        }
        if (state->second) {
            if (byte >= 0x81 && byte <= 0xFE) { state->third = byte; return LEGACY_CONTINUE; }
            if (!legacy_restore(state, (uint32_t[]){ state->second, byte }, 2)) return LEGACY_INVALID;
            state->leading = state->second = 0; return LEGACY_ERROR;
        }
        if (state->leading) {
            if (byte >= 0x30 && byte <= 0x39) { state->second = byte; return LEGACY_CONTINUE; }
            const unsigned leading = state->leading; state->leading = 0;
            const uint32_t point = (byte >= 0x40 && byte <= 0x7E) || (byte >= 0x80 && byte <= 0xFE) ?
                lookup(kDecodeGb18030, ARRLEN(kDecodeGb18030),
                       (leading - 0x81) * 190 + byte - (byte < 0x7F ? 0x40 : 0x41)) : MISSING;
            return legacy_mapped(state, byte, point, output);
        }
        if (byte < 0x80 || byte == 0x80) { output[0] = byte == 0x80 ? 0x20AC : byte; return LEGACY_POINT; }
        if (byte >= 0x81 && byte <= 0xFE) { state->leading = byte; return LEGACY_CONTINUE; }
        return LEGACY_ERROR;
    }
    if (encoding == WEB_ENC_EUC_JP) {
        if (state->leading == 0x8E && byte >= 0xA1 && byte <= 0xDF) {
            state->leading = 0; output[0] = 0xFF61 + byte - 0xA1; return LEGACY_POINT;
        }
        if (state->leading == 0x8F && byte >= 0xA1 && byte <= 0xFE) {
            state->jis0212 = 1; state->leading = byte; return LEGACY_CONTINUE;
        }
        if (state->leading) {
            const unsigned leading = state->leading; state->leading = 0;
            uint32_t point = MISSING;
            if (leading >= 0xA1 && leading <= 0xFE && byte >= 0xA1 && byte <= 0xFE) {
                const uint32_t pointer = (leading - 0xA1) * 94 + byte - 0xA1;
                point = state->jis0212 ? lookup(kDecodeJis0212, ARRLEN(kDecodeJis0212), pointer) :
                                        lookup(kDecodeJis0208, ARRLEN(kDecodeJis0208), pointer);
            }
            state->jis0212 = 0;
            return legacy_mapped(state, byte, point, output);
        }
        if (byte < 0x80) { output[0] = byte; return LEGACY_POINT; }
        if (byte == 0x8E || byte == 0x8F || (byte >= 0xA1 && byte <= 0xFE)) {
            state->leading = byte; return LEGACY_CONTINUE;
        }
        return LEGACY_ERROR;
    }
    if (state->leading) {
        const unsigned leading = state->leading; state->leading = 0;
        uint32_t point = MISSING;
        if (encoding == WEB_ENC_BIG5) {
            uint32_t pointer = MISSING;
            if ((byte >= 0x40 && byte <= 0x7E) || (byte >= 0xA1 && byte <= 0xFE))
                pointer = (leading - 0x81) * 157 + byte - (byte < 0x7F ? 0x40 : 0x62);
            if (pointer == 1133 || pointer == 1135 || pointer == 1164 || pointer == 1166) {
                output[0] = pointer < 1164 ? 0xCA : 0xEA;
                output[1] = pointer == 1133 || pointer == 1164 ? 0x304 : 0x30C;
                return LEGACY_PAIR;
            }
            point = lookup(kDecodeBig5, ARRLEN(kDecodeBig5), pointer);
        } else if (encoding == WEB_ENC_SHIFT_JIS) {
            uint32_t pointer = MISSING;
            if ((byte >= 0x40 && byte <= 0x7E) || (byte >= 0x80 && byte <= 0xFC))
                pointer = (leading - (leading < 0xA0 ? 0x81 : 0xC1)) * 188 +
                          byte - (byte < 0x7F ? 0x40 : 0x41);
            point = pointer >= 8836 && pointer <= 10715 ? 0xE000 + pointer - 8836 :
                    lookup(kDecodeJis0208, ARRLEN(kDecodeJis0208), pointer);
        } else if (encoding == WEB_ENC_EUC_KR && byte >= 0x41 && byte <= 0xFE) {
            point = lookup(kDecodeEucKr, ARRLEN(kDecodeEucKr), (leading - 0x81) * 190 + byte - 0x41);
        }
        return legacy_mapped(state, byte, point, output);
    }
    if (byte < 0x80 || (encoding == WEB_ENC_SHIFT_JIS && byte == 0x80)) {
        output[0] = byte; return LEGACY_POINT;
    }
    if (encoding == WEB_ENC_SHIFT_JIS) {
        if (byte >= 0xA1 && byte <= 0xDF) { output[0] = 0xFF61 + byte - 0xA1; return LEGACY_POINT; }
        if ((byte >= 0x81 && byte <= 0x9F) || (byte >= 0xE0 && byte <= 0xFC)) {
            state->leading = byte; return LEGACY_CONTINUE;
        }
    } else if ((encoding == WEB_ENC_BIG5 || encoding == WEB_ENC_EUC_KR) && byte >= 0x81 && byte <= 0xFE) {
        state->leading = byte; return LEGACY_CONTINUE;
    }
    return LEGACY_ERROR;
}

bool web_encoding_decode_stream_n(SB *output, WebEncoding encoding,
                                  WebEncodingDecoder *state,
                                  const char *input, size_t length, bool stream,
                                  size_t max_output, bool fatal,
                                  size_t *consumed, bool *had_error) {
    if (consumed) *consumed = 0;
    if (had_error) *had_error = false;
    if (!output || !state || !consumed || (!input && length) || encoding < 0 || encoding >= WEB_ENC_COUNT ||
        encoding == WEB_ENC_UTF8 || encoding == WEB_ENC_UTF16LE || encoding == WEB_ENC_UTF16BE ||
        encoding == WEB_ENC_REPLACEMENT || output->n > max_output ||
        state->pending_count > 3 || state->leading > 255 || state->second > 255 || state->third > 255 ||
        state->mode > LEGACY_ESCAPE || state->output_mode > LEGACY_LEADING ||
        state->output_flag > 1 || state->jis0212 > 1) return false;
    for (uint32_t i = 0; i < state->pending_count; ++i) if (state->pending[i] > 255) return false;
    const unsigned char *bytes = (const unsigned char *)input;
    for (;;) {
        unsigned byte;
        if (state->pending_count) {
            byte = state->pending[0]; --state->pending_count;
            memmove(state->pending, state->pending + 1, state->pending_count * sizeof state->pending[0]);
        } else if (*consumed < length) byte = bytes[(*consumed)++];
        else if (stream) return true;
        else byte = 256;
        uint32_t points[2] = { 0, 0 };
        const int result = legacy_step(state, encoding, byte, points);
        if (result == LEGACY_FINISHED) return true;
        if (result == LEGACY_INVALID) return false;
        if (result == LEGACY_CONTINUE) continue;
        if (result == LEGACY_ERROR) {
            if (had_error) *had_error = true;
            if (fatal || !emit(output, MISSING, max_output)) return false;
        } else {
            if (!emit(output, points[0], max_output)) return false;
            if (result == LEGACY_PAIR && !emit(output, points[1], max_output)) return false;
        }
    }
}

bool web_encoding_decode_checked_n(SB *output, WebEncoding encoding,
                           const char *input, size_t length,
                           bool sniff_bom, size_t max_output, bool fatal, bool *had_error) {
    if (had_error) *had_error = false;
    if (!output || (!input && length) || encoding < 0 || encoding >= WEB_ENC_COUNT ||
        output->n > max_output) return false;
    const unsigned char *bytes = (const unsigned char *)input;
    if (sniff_bom) {
        if (length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            encoding = WEB_ENC_UTF8; bytes += 3; length -= 3;
        } else if (length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
            encoding = WEB_ENC_UTF16BE; bytes += 2; length -= 2;
        } else if (length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
            encoding = WEB_ENC_UTF16LE; bytes += 2; length -= 2;
        }
    }
    if (encoding == WEB_ENC_UTF8) return decode_utf8(output, bytes, length, max_output, fatal, had_error);
    if (encoding == WEB_ENC_UTF16BE || encoding == WEB_ENC_UTF16LE)
        return decode_utf16(output, bytes, length, encoding == WEB_ENC_UTF16BE, max_output, fatal, had_error);
    if (kEncodingMetadata[encoding].single_byte) {
        for (size_t i = 0; i < length; ++i) {
            unsigned byte = bytes[i];
            EMIT(byte < 0x80 ? byte : kEncodingMetadata[encoding].single_byte[byte - 0x80]);
        }
        return true;
    }
    switch (encoding) {
        case WEB_ENC_GBK: case WEB_ENC_GB18030:
            return decode_gb18030(output, bytes, length, max_output, fatal, had_error);
        case WEB_ENC_BIG5: return decode_big5(output, bytes, length, max_output, fatal, had_error);
        case WEB_ENC_EUC_JP: return decode_euc_jp(output, bytes, length, max_output, fatal, had_error);
        case WEB_ENC_ISO_2022_JP: return decode_iso2022_jp(output, bytes, length, max_output, fatal, had_error);
        case WEB_ENC_SHIFT_JIS: return decode_shift_jis(output, bytes, length, max_output, fatal, had_error);
        case WEB_ENC_EUC_KR: return decode_euc_kr(output, bytes, length, max_output, fatal, had_error);
        case WEB_ENC_REPLACEMENT:
            if (length) EMIT(MISSING);
            return true;
        case WEB_ENC_X_USER_DEFINED:
            for (size_t i = 0; i < length; ++i) EMIT(bytes[i] < 0x80 ? bytes[i] : 0xF700 + bytes[i]);
            return true;
        default: return false;
    }
}

bool web_encoding_decode_n(SB *output, WebEncoding encoding,
                           const char *input, size_t length,
                           bool sniff_bom, size_t max_output) {
    return web_encoding_decode_checked_n(output, encoding, input, length,
        sniff_bom, max_output, false, NULL);
}

#undef EMIT
#undef MISSING


/* Append to the separately gated native encoding.c. This uses that file's
 * original read-only forward tables, plus compact sorted pointer indexes.
 * WHATWG Encoding: https://encoding.spec.whatwg.org/#encoders
 * No WPT data, iconv aliases, locale-dependent conversion or per-realm maps.
 */
#include "encoding_encode_index.inc"

typedef struct {
    WebEncoding encoding;
    unsigned iso_state; /* 0 ASCII, 1 Roman, 2 JIS0208 */
    WebEncodingByteSink byte;
    WebEncodingErrorSink error;
    void *context;
} WebEncoder;

#define ENCODE_MISSING 0xFFFFFFFFu

WebEncoding web_encoding_output(WebEncoding encoding) {
    if (encoding < 0 || encoding >= WEB_ENC_COUNT) return WEB_ENC_UNKNOWN;
    if (encoding == WEB_ENC_REPLACEMENT || encoding == WEB_ENC_UTF16BE ||
        encoding == WEB_ENC_UTF16LE) return WEB_ENC_UTF8;
    return encoding;
}

static bool encode_bytes(WebEncoder *encoder, const unsigned char *bytes, size_t length) {
    for (size_t index = 0; index < length; ++index)
        if (!encoder->byte(encoder->context, bytes[index])) return false;
    return true;
}

static bool encode_one(WebEncoder *encoder, uint32_t byte) {
    return byte <= 0xFF && encoder->byte(encoder->context, (unsigned char)byte);
}

static bool encode_two(WebEncoder *encoder, uint32_t first, uint32_t second) {
    return encode_one(encoder, first) && encode_one(encoder, second);
}

static bool encode_error(WebEncoder *encoder, uint32_t point) {
    return encoder->error(encoder->context, point);
}

/* Pointer indexes are sorted by (forward codepoint, pointer), and contain no
 * missing entries. A binary search finds an equal range. The standard's Big5
 * and Shift_JIS exceptions select within that range without copying tables. */
static uint32_t encode_pointer(const uint32_t *forward, const uint16_t *order,
                               size_t count, uint32_t point, unsigned kind) {
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (forward[order[middle]] < point) low = middle + 1;
        else high = middle;
    }
    uint32_t found = ENCODE_MISSING;
    const bool big5_last = kind == 1 && (point == 0x2550 || point == 0x255E ||
        point == 0x2561 || point == 0x256A || point == 0x5341 || point == 0x5345);
    while (low < count && forward[order[low]] == point) {
        uint32_t pointer = order[low++];
        if (kind == 1 && pointer < (0xA1 - 0x81) * 157u) continue;
        if (kind == 2 && pointer >= 8272 && pointer <= 8835) continue;
        found = pointer;
        if (!big5_last) break;
    }
    return found;
}

static uint32_t encode_gb_range(uint32_t point) {
    if (point == 0xE7C7) return 7457;
    size_t low = 0, high = ARRLEN(kDecodeGb18030Ranges);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (kDecodeGb18030Ranges[middle].codepoint <= point) low = middle + 1;
        else high = middle;
    }
    if (!low) return ENCODE_MISSING;
    const WebEncodingRange *range = &kDecodeGb18030Ranges[low - 1];
    return range->pointer + point - range->codepoint;
}

/* These asymmetries are the Encoding Standard's explicit compatibility table
 * for GB18030-2005 private-use characters after the 2022 index changes. */
static uint32_t encode_gb_compatibility(uint32_t point) {
    static const struct { uint32_t point; uint16_t bytes; } entries[] = {
        {0xE78D, 0xA6D9}, {0xE78E, 0xA6DA}, {0xE78F, 0xA6DB},
        {0xE790, 0xA6DC}, {0xE791, 0xA6DD}, {0xE792, 0xA6DE},
        {0xE793, 0xA6DF}, {0xE794, 0xA6EC}, {0xE795, 0xA6ED},
        {0xE796, 0xA6F3}, {0xE81E, 0xFE59}, {0xE826, 0xFE61},
        {0xE82B, 0xFE66}, {0xE82C, 0xFE67}, {0xE832, 0xFE6D},
        {0xE843, 0xFE7E}, {0xE854, 0xFE90}, {0xE864, 0xFEA0},
    };
    for (size_t index = 0; index < ARRLEN(entries); ++index)
        if (entries[index].point == point) return entries[index].bytes;
    return ENCODE_MISSING;
}

static bool encode_iso_escape(WebEncoder *encoder, unsigned next) {
    static const unsigned char escapes[][3] = {
        {0x1B, 0x28, 0x42}, {0x1B, 0x28, 0x4A}, {0x1B, 0x24, 0x42},
    };
    encoder->iso_state = next;
    return encode_bytes(encoder, escapes[next], 3);
}

static bool encode_iso_point(WebEncoder *encoder, uint32_t point) {
    /* At most one state transition is needed before a byte/error result.
     * The loop expresses the standard's restore-and-reconsume operation. */
    for (;;) {
        if (encoder->iso_state != 2 && (point == 0x0E || point == 0x0F || point == 0x1B))
            return encode_error(encoder, 0xFFFD);
        if (encoder->iso_state == 0 && point <= 0x7F) return encode_one(encoder, point);
        if (encoder->iso_state == 1) {
            if (point <= 0x7F && point != 0x5C && point != 0x7E) return encode_one(encoder, point);
            if (point == 0xA5) return encode_one(encoder, 0x5C);
            if (point == 0x203E) return encode_one(encoder, 0x7E);
        }
        if (point <= 0x7F && encoder->iso_state != 0) {
            if (!encode_iso_escape(encoder, 0)) return false;
            continue;
        }
        if ((point == 0xA5 || point == 0x203E) && encoder->iso_state != 1) {
            if (!encode_iso_escape(encoder, 1)) return false;
            continue;
        }
        if (point == 0x2212) point = 0xFF0D;
        if (point >= 0xFF61 && point <= 0xFF9F)
            point = kEncodeIso2022JPKatakana[point - 0xFF61];
        uint32_t pointer = encode_pointer(kDecodeJis0208, kEncodeJis0208Order,
                                          ARRLEN(kEncodeJis0208Order), point, 0);
        if (pointer == ENCODE_MISSING) {
            if (encoder->iso_state == 2) {
                if (!encode_iso_escape(encoder, 0)) return false;
                continue;
            }
            return encode_error(encoder, point);
        }
        if (pointer >= 8836) return false; /* pinned table/index contract */
        if (encoder->iso_state != 2) {
            if (!encode_iso_escape(encoder, 2)) return false;
            continue;
        }
        return encode_two(encoder, pointer / 94 + 0x21, pointer % 94 + 0x21);
    }
}

static bool encode_point(WebEncoder *encoder, uint32_t point) {
    const WebEncoding encoding = encoder->encoding;
    if (encoding == WEB_ENC_ISO_2022_JP) return encode_iso_point(encoder, point);
    if (point <= 0x7F) return encode_one(encoder, point);
    if (encoding == WEB_ENC_UTF8) {
        char bytes[4];
        int length = utf8_encode(point, bytes);
        return length >= 1 && length <= 4 &&
            encode_bytes(encoder, (const unsigned char *)bytes, (size_t)length);
    }
    const uint32_t *single = kEncodingMetadata[encoding].single_byte;
    if (single) {
        for (uint32_t pointer = 0; pointer < 128; ++pointer)
            if (single[pointer] == point) return encode_one(encoder, pointer + 0x80);
        return encode_error(encoder, point);
    }
    uint32_t pointer;
    switch (encoding) {
    case WEB_ENC_GBK:
    case WEB_ENC_GB18030:
        if (point == 0xE5E5) return encode_error(encoder, point);
        if (encoding == WEB_ENC_GBK && point == 0x20AC) return encode_one(encoder, 0x80);
        pointer = encode_gb_compatibility(point);
        if (pointer != ENCODE_MISSING) return encode_two(encoder, pointer >> 8, pointer & 0xFF);
        pointer = encode_pointer(kDecodeGb18030, kEncodeGb18030Order,
                                 ARRLEN(kEncodeGb18030Order), point, 0);
        if (pointer != ENCODE_MISSING) {
            uint32_t trailing = pointer % 190;
            return encode_two(encoder, pointer / 190 + 0x81,
                               trailing + (trailing < 0x3F ? 0x40 : 0x41));
        }
        if (encoding == WEB_ENC_GBK) return encode_error(encoder, point);
        pointer = encode_gb_range(point);
        if (pointer == ENCODE_MISSING || pointer > 1237575) return false;
        return encode_one(encoder, pointer / 12600 + 0x81) &&
            encode_one(encoder, pointer % 12600 / 1260 + 0x30) &&
            encode_one(encoder, pointer % 1260 / 10 + 0x81) &&
            encode_one(encoder, pointer % 10 + 0x30);
    case WEB_ENC_BIG5:
        pointer = encode_pointer(kDecodeBig5, kEncodeBig5Order,
                                 ARRLEN(kEncodeBig5Order), point, 1);
        if (pointer == ENCODE_MISSING) return encode_error(encoder, point);
        return encode_two(encoder, pointer / 157 + 0x81,
                           pointer % 157 + (pointer % 157 < 0x3F ? 0x40 : 0x62));
    case WEB_ENC_EUC_JP:
    case WEB_ENC_SHIFT_JIS:
        if (point == 0xA5) return encode_one(encoder, 0x5C);
        if (point == 0x203E) return encode_one(encoder, 0x7E);
        if (encoding == WEB_ENC_SHIFT_JIS && point == 0x80) return encode_one(encoder, 0x80);
        if (point >= 0xFF61 && point <= 0xFF9F) {
            if (encoding == WEB_ENC_EUC_JP && !encode_one(encoder, 0x8E)) return false;
            return encode_one(encoder, point - 0xFF61 + 0xA1);
        }
        if (point == 0x2212) point = 0xFF0D;
        pointer = encode_pointer(kDecodeJis0208, kEncodeJis0208Order,
                                 ARRLEN(kEncodeJis0208Order), point,
                                 encoding == WEB_ENC_SHIFT_JIS ? 2 : 0);
        if (pointer == ENCODE_MISSING) return encode_error(encoder, point);
        if (encoding == WEB_ENC_EUC_JP) {
            if (pointer >= 8836) return false;
            return encode_two(encoder, pointer / 94 + 0xA1, pointer % 94 + 0xA1);
        }
        return encode_two(encoder, pointer / 188 + (pointer / 188 < 0x1F ? 0x81 : 0xC1),
                           pointer % 188 + (pointer % 188 < 0x3F ? 0x40 : 0x41));
    case WEB_ENC_EUC_KR:
        pointer = encode_pointer(kDecodeEucKr, kEncodeEucKrOrder,
                                 ARRLEN(kEncodeEucKrOrder), point, 0);
        if (pointer == ENCODE_MISSING) return encode_error(encoder, point);
        return encode_two(encoder, pointer / 190 + 0x81, pointer % 190 + 0x41);
    case WEB_ENC_X_USER_DEFINED:
        if (point >= 0xF780 && point <= 0xF7FF) return encode_one(encoder, point - 0xF780 + 0x80);
        return encode_error(encoder, point);
    default:
        return false;
    }
}

/* Validate scalar UTF-8 directly. This API does not reinterpret malformed
 * internal strings as a different encoding or read beyond the supplied span. */
static bool encode_next_scalar(const unsigned char *bytes, size_t length,
                               size_t *position, uint32_t *point) {
    unsigned char first = bytes[(*position)++];
    if (first <= 0x7F) { *point = first; return true; }
    unsigned remaining, lower = 0x80, upper = 0xBF;
    if (first >= 0xC2 && first <= 0xDF) { remaining = 1; *point = first & 0x1F; }
    else if (first >= 0xE0 && first <= 0xEF) {
        remaining = 2; *point = first & 0x0F;
        if (first == 0xE0) lower = 0xA0;
        if (first == 0xED) upper = 0x9F;
    } else if (first >= 0xF0 && first <= 0xF4) {
        remaining = 3; *point = first & 7;
        if (first == 0xF0) lower = 0x90;
        if (first == 0xF4) upper = 0x8F;
    } else return false;
    while (remaining--) {
        if (*position == length || bytes[*position] < lower || bytes[*position] > upper) return false;
        *point = (*point << 6) | (bytes[(*position)++] & 0x3F);
        lower = 0x80; upper = 0xBF;
    }
    return true;
}

bool web_encoding_encode_n(WebEncoding encoding, const char *utf8, size_t length,
                           WebEncodingByteSink byte_sink,
                           WebEncodingErrorSink error_sink, void *context) {
    encoding = web_encoding_output(encoding);
    if (encoding == WEB_ENC_UNKNOWN || (!utf8 && length) || !byte_sink || !error_sink) return false;
    WebEncoder encoder = {encoding, 0, byte_sink, error_sink, context};
    const unsigned char *bytes = (const unsigned char *)utf8;
    size_t position = 0;
    while (position < length) {
        uint32_t point;
        if (!encode_next_scalar(bytes, length, &position, &point) || !encode_point(&encoder, point)) return false;
    }
    return encoding != WEB_ENC_ISO_2022_JP || encoder.iso_state == 0 || encode_iso_escape(&encoder, 0);
}

#undef ENCODE_MISSING
