#ifndef LUMEN_CSS_NUMERIC_COUNT_H
#define LUMEN_CSS_NUMERIC_COUNT_H
/* Typed CSS math for integer-valued positions. Unlike the legacy Length
 * evaluator, dimensions remain distinct through products and quotients.
 * Callers perform their own range clamp after evaluation and integer rounding.
 * https://drafts.csswg.org/css-values-4/#calc-type-checking */
enum { CN_LENGTH, CN_ANGLE, CN_TIME, CN_FREQUENCY, CN_RESOLUTION, CN_FLEX, CN_PERCENT, CN_DIMS };
typedef struct { double value; int power[CN_DIMS]; } CssNumber;
typedef struct { const char *p; float em, rem; const MediaCtx *media; unsigned depth, terms; } CssNumberParser;
static bool css_number_sum(CssNumberParser *p, CssNumber *out);
static void css_number_ws(CssNumberParser *p) { while (is_ws((unsigned char)*p->p)) p->p++; }
static bool css_number_scalar(const CssNumber *n) { for (int i = 0; i < CN_DIMS; i++) if (n->power[i]) return false; return true; }
static bool css_number_same(const CssNumber *a, const CssNumber *b) { return !memcmp(a->power, b->power, sizeof a->power); }
static bool css_number_angle(const CssNumber *n) { for (int i = 0; i < CN_DIMS; i++) if (n->power[i] != (i == CN_ANGLE)) return false; return true; }
static bool css_number_function(const char *name, CssNumber *args, int n, int strategy, CssNumber *out) {
    if (!n) return false;
    *out = args[0]; double a = args[0].value, b = n > 1 ? args[1].value : 1, v = a;
    bool same = true, scalar = true, nan = false;
    for (int i = 0; i < n; i++) { same &= css_number_same(&args[0], &args[i]); scalar &= css_number_scalar(&args[i]); nan |= isnan(args[i].value); }
    if (!strcmp(name, "calc") || !strcmp(name, "-webkit-calc")) { if (n != 1) return false; }
    else if (!strcmp(name, "min") || !strcmp(name, "max") || !strcmp(name, "hypot")) {
        if (!same) return false;
        bool lo = !strcmp(name, "min"), hi = !strcmp(name, "max");
        if (!lo && !hi) v = fabs(a);
        for (int i = 1; i < n; i++) v = lo ? fmin(v, args[i].value) : hi ? fmax(v, args[i].value) : hypot(v, args[i].value);
    } else if (!strcmp(name, "clamp")) { if (n != 3 || !same) return false; v = fmax(a, fmin(b, args[2].value)); }
    else if (!strcmp(name, "abs")) { if (n != 1) return false; v = fabs(a); }
    else if (!strcmp(name, "sign")) { if (n != 1) return false; memset(out->power, 0, sizeof out->power); v = a > 0 ? 1 : a < 0 ? -1 : a; }
    else if (!strcmp(name, "round")) {
        if ((n != 1 && n != 2) || !same || (n == 1 && !scalar)) return false;
        b = fabs(b);
        if (!b || (isinf(a) && isinf(b))) v = NAN;
        else if (isinf(a)) v = a;
        else if (isinf(b)) v = strategy == 1 ? (a > 0 ? INFINITY : copysign(0, a)) : strategy == 2 ? (a < 0 ? -INFINITY : copysign(0, a)) : copysign(0, a);
        else { double q = a / b; v = (strategy == 1 ? ceil(q) : strategy == 2 ? floor(q) : strategy == 3 ? trunc(q) : floor(q + .5)) * b; }
    } else if (!strcmp(name, "mod") || !strcmp(name, "rem")) {
        if (n != 2 || !same) return false;
        if (!b || isinf(a)) v = NAN;
        else if (isinf(b)) v = !strcmp(name, "mod") && signbit(a) != signbit(b) ? NAN : a;
        else v = !strcmp(name, "mod") ? a - b * floor(a / b) : fmod(a, b);
    } else if (!strcmp(name, "sin") || !strcmp(name, "cos") || !strcmp(name, "tan")) {
        if (n != 1 || (!scalar && !css_number_angle(&args[0]))) return false;
        if (!scalar) a *= M_PI / 180; memset(out->power, 0, sizeof out->power);
        v = !strcmp(name, "sin") ? sin(a) : !strcmp(name, "cos") ? cos(a) : tan(a);
    } else if (!strcmp(name, "asin") || !strcmp(name, "acos") || !strcmp(name, "atan") || !strcmp(name, "atan2")) {
        bool two = !strcmp(name, "atan2");
        if (two ? n != 2 || !same : n != 1 || !scalar) return false;
        memset(out->power, 0, sizeof out->power); out->power[CN_ANGLE] = 1;
        v = (two ? atan2(a, b) : !strcmp(name, "asin") ? asin(a) : !strcmp(name, "acos") ? acos(a) : atan(a)) * 180 / M_PI;
    } else if (!strcmp(name, "pow")) { if (n != 2 || !scalar) return false; v = pow(a, b); }
    else if (!strcmp(name, "sqrt") || !strcmp(name, "exp")) { if (n != 1 || !scalar) return false; v = !strcmp(name, "sqrt") ? sqrt(a) : exp(a); }
    else if (!strcmp(name, "log")) { if ((n != 1 && n != 2) || !scalar) return false; v = n == 1 ? log(a) : log(a) / log(b); }
    else return false;
    out->value = nan ? NAN : v; return true;
}
static bool css_number_atom(CssNumberParser *p, CssNumber *out) {
    css_number_ws(p); memset(out, 0, sizeof *out);
    if (++p->terms > 4096) return false;
    const char *s = p->p;
    if (*s == '(') { p->p++; if (!css_number_sum(p, out)) return false; css_number_ws(p); if (*p->p != ')') return false; p->p++; return true; }
    const char *q = s; if (*q == '+' || *q == '-') q++;
    bool digits = false;
    while (isdigit((unsigned char)*q)) { digits = true; q++; }
    if (*q == '.' && isdigit((unsigned char)q[1])) { q++; while (isdigit((unsigned char)*q)) { digits = true; q++; } }
    if (digits) {
        if ((*q == 'e' || *q == 'E') && (isdigit((unsigned char)q[1]) || ((q[1] == '+' || q[1] == '-') && isdigit((unsigned char)q[2])))) { q++; if (*q == '+' || *q == '-') q++; while (isdigit((unsigned char)*q)) q++; }
        char *end; out->value = strtod(s, &end); if (end != q) return false;
        const char *unit = q; while (isalpha((unsigned char)*q)) q++;
        char u[16]; size_t un = (size_t)(q - unit); if (un >= sizeof u) return false;
        for (size_t i = 0; i < un; i++) u[i] = (char)lc(unit[i]); u[un] = 0;
        if (*q == '%') { if (un) return false; out->power[CN_PERCENT] = 1; q++; }
        else if (un) {
            if (!strcmp(u, "deg") || !strcmp(u, "rad") || !strcmp(u, "grad") || !strcmp(u, "turn")) { out->power[CN_ANGLE] = 1; out->value *= !strcmp(u, "rad") ? 180 / M_PI : !strcmp(u, "grad") ? .9 : !strcmp(u, "turn") ? 360 : 1; }
            else if (!strcmp(u, "s") || !strcmp(u, "ms")) { out->power[CN_TIME] = 1; if (u[0] == 'm') out->value /= 1000; }
            else if (!strcmp(u, "hz") || !strcmp(u, "khz")) { out->power[CN_FREQUENCY] = 1; if (u[0] == 'k') out->value *= 1000; }
            else if (!strcmp(u, "dpi") || !strcmp(u, "dpcm") || !strcmp(u, "dppx") || !strcmp(u, "x")) { out->power[CN_RESOLUTION] = 1; out->value *= !strcmp(u, "dpi") ? 1.0 / 96 : !strcmp(u, "dpcm") ? 2.54 / 96 : 1; }
            else if (!strcmp(u, "fr")) out->power[CN_FLEX] = 1;
            else {
                size_t len = (size_t)(q - s); if (len >= 160) return false;
                char token[160]; memcpy(token, s, len); token[len] = 0; Length value;
                if (!css_parse_length(token, &value, p->em, p->rem, p->media) || value.kind != LK_LEN || value.pct || value.mm) return false;
                out->power[CN_LENGTH] = 1; out->value = value.px;
            }
        }
        p->p = q; return true;
    }
    q = s; while (isalpha((unsigned char)*q) || *q == '-' || (q > s && isdigit((unsigned char)*q))) q++;
    size_t len = (size_t)(q - s); if (!len || len >= 24) return false;
    char name[24]; for (size_t i = 0; i < len; i++) name[i] = (char)lc(s[i]); name[len] = 0; p->p = q;
    if (*q != '(') {
        if (!strcmp(name, "pi")) out->value = M_PI;
        else if (!strcmp(name, "e")) out->value = M_E;
        else if (!strcmp(name, "infinity")) out->value = INFINITY;
        else if (!strcmp(name, "-infinity")) out->value = -INFINITY;
        else if (!strcmp(name, "nan")) out->value = NAN;
        else return false;
        return true;
    }
    p->p++; int strategy = 0;
    if (!strcmp(name, "round")) {
        css_number_ws(p); const char *begin = p->p; while (isalpha((unsigned char)*p->p) || *p->p == '-') p->p++;
        size_t n = (size_t)(p->p - begin);
        if (n && *p->p != '(') {
            if (n == 7 && !strncasecmp(begin, "nearest", n)) strategy = 0;
            else if (n == 2 && !strncasecmp(begin, "up", n)) strategy = 1;
            else if (n == 4 && !strncasecmp(begin, "down", n)) strategy = 2;
            else if (n == 7 && !strncasecmp(begin, "to-zero", n)) strategy = 3;
            else { p->p = begin; n = 0; }
            if (n) { css_number_ws(p); if (*p->p != ',') return false; p->p++; }
        } else p->p = begin;
    }
    CssNumber args[64]; int n = 0;
    for (;;) {
        if (n == (int)ARRLEN(args) || !css_number_sum(p, &args[n++])) return false;
        css_number_ws(p); if (*p->p == ',') { p->p++; continue; }
        if (*p->p != ')') return false; p->p++; break;
    }
    return css_number_function(name, args, n, strategy, out);
}
static bool css_number_value(CssNumberParser *p, CssNumber *out) {
    if (++p->depth > 64) { p->depth--; return false; }
    bool ok = css_number_atom(p, out); p->depth--; return ok;
}
static bool css_number_product(CssNumberParser *p, CssNumber *out) {
    if (!css_number_value(p, out)) return false;
    for (;;) {
        const char *old = p->p; css_number_ws(p); char op = *p->p;
        if (op != '*' && op != '/') { p->p = old; return true; }
        p->p++; CssNumber r; if (!css_number_value(p, &r)) return false;
        for (int i = 0; i < CN_DIMS; i++) { out->power[i] += op == '*' ? r.power[i] : -r.power[i]; if (abs(out->power[i]) > 64) return false; }
        out->value = op == '*' ? out->value * r.value : out->value / r.value;
    }
}
static bool css_number_sum(CssNumberParser *p, CssNumber *out) {
    if (!css_number_product(p, out)) return false;
    for (;;) {
        const char *old = p->p; css_number_ws(p); char op = *p->p;
        if ((op != '+' && op != '-') || p->p == old || !is_ws((unsigned char)p->p[1])) { p->p = old; return true; }
        p->p++; CssNumber r; if (!css_number_product(p, &r) || !css_number_same(out, &r)) return false;
        out->value = op == '+' ? out->value + r.value : out->value - r.value;
    }
}
static bool css_number_parse(const char *text, float em, float rem, const MediaCtx *media, CssNumber *out) {
    CssNumberParser p = { .p = text, .em = em, .rem = rem, .media = media };
    if (!css_number_value(&p, out)) return false;
    css_number_ws(&p); return !*p.p;
}
#endif
