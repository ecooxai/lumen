/* Fetch response MIME policy. Header extraction is shared by classic script,
 * style and strict worker consumers; it does not alter transport/cache state. */
#include "net.h"

typedef enum { MIME_FAILURE, MIME_OTHER, MIME_JAVASCRIPT, MIME_CSS, MIME_SCRIPT_BLOCKED } MimeClass;
static bool mime_ws(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool mime_token(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        c == 0x60 || (c && strchr("!#$%&'*+-.^_|~", c));
}
static bool span_eq(const char *s, const char *end, const char *expected) {
    size_t n = (size_t)(end - s);
    return n == strlen(expected) && !strncasecmp(s, expected, n);
}
/* Fetch get/decode/split preserves quoted commas and escaped quote characters.
 * The returned span still includes quotes; quoted "nosniff" is not nosniff. */
static const char *header_value_end(const char *s) {
    bool quoted = false;
    for (; *s; s++) {
        if (quoted && *s == '\\' && s[1]) { s++; continue; }
        if (*s == '"') quoted = !quoted;
        else if (*s == ',' && !quoted) break;
    }
    return s;
}
static MimeClass parse_mime_class(const char *s, const char *end) {
    while (s < end && mime_ws((unsigned char)*s)) s++;
    while (end > s && mime_ws((unsigned char)end[-1])) end--;
    const char *slash = memchr(s, '/', (size_t)(end - s));
    if (!slash || slash == s) return MIME_FAILURE;
    for (const char *p = s; p < slash; p++) if (!mime_token((unsigned char)*p)) return MIME_FAILURE;
    const char *semi = memchr(slash + 1, ';', (size_t)(end - slash - 1));
    if (semi) end = semi;
    while (end > slash + 1 && mime_ws((unsigned char)end[-1])) end--;
    if (end == slash + 1) return MIME_FAILURE;
    for (const char *p = slash + 1; p < end; p++) if (!mime_token((unsigned char)*p)) return MIME_FAILURE;
    /* MIME extraction ignores the wildcard fallback entry. */
    if (span_eq(s, end, "*/*")) return MIME_FAILURE;
    if (span_eq(s, slash, "audio") || span_eq(s, slash, "image") || span_eq(s, slash, "video") ||
        span_eq(s, end, "text/csv")) return MIME_SCRIPT_BLOCKED;
    if (span_eq(s, end, "text/css")) return MIME_CSS;
    static const char *const javascript[] = {
        "application/ecmascript", "application/javascript", "application/x-ecmascript", "application/x-javascript",
        "text/ecmascript", "text/javascript", "text/javascript1.0", "text/javascript1.1", "text/javascript1.2",
        "text/javascript1.3", "text/javascript1.4", "text/javascript1.5", "text/jscript", "text/livescript",
        "text/x-ecmascript", "text/x-javascript"
    };
    for (size_t i = 0; i < sizeof javascript / sizeof *javascript; i++)
        if (span_eq(s, end, javascript[i])) return MIME_JAVASCRIPT;
    return MIME_OTHER;
}
static MimeClass response_mime_class(const NetResponse *response) {
    if (!response) return MIME_FAILURE;
    /* Join the ordered header list without dropping an empty first field.
       A quoted parameter can continue across a combined comma boundary. */
    SB joined = {0}; bool seen = false;
    for (int i = 0; i < response->headers.n; i++) {
        const Header *h = &response->headers.v[i];
        if (!str_ieq(h->name, "content-type")) continue;
        if (seen) sb_puts(&joined, ", ");
        sb_puts(&joined, h->value); seen = true;
    }
    MimeClass result = MIME_FAILURE;
    const char *p = joined.s ? joined.s : "";
    while (*p) {
        const char *end = header_value_end(p);
        MimeClass current = parse_mime_class(p, end);
        if (current != MIME_FAILURE) result = current;
        if (!*end) break;
        p = end + 1;
    }
    sb_free(&joined);
    return result;
}
static bool response_nosniff(const NetResponse *response) {
    /* Only the first split value matters. Its first header is sufficient;
       an empty first field must not be skipped in favor of a later one. */
    const char *s = headers_get(&response->headers, "x-content-type-options");
    if (!s) return false;
    const char *end = header_value_end(s);
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) end--;
    return span_eq(s, end, "nosniff");
}
bool net_response_mime_allowed(const NetResponse *response, NetMimeDestination destination) {
    if (!response) return false;
    MimeClass mime = response_mime_class(response);
    if (destination == NET_MIME_SCRIPT && mime == MIME_SCRIPT_BLOCKED) return false;
    if (!response_nosniff(response)) return true;
    return destination == NET_MIME_SCRIPT ? mime == MIME_JAVASCRIPT : mime == MIME_CSS;
}
bool net_response_mime_is_javascript(const NetResponse *response) {
    return response_mime_class(response) == MIME_JAVASCRIPT;
}
