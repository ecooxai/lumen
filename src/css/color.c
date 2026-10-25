/* CSS Color 4/5 in double precision (see color.h) */
#include "color.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool css_named_color(const char *name, uint32_t *rgb);   /* style.c: named and system colours */

/* ---------------- conversion ---------------- */
static void mul3(const double m[9], double v[3]) { double x = v[0], y = v[1], z = v[2]; for (int i = 0; i < 3; i++) v[i] = m[i * 3] * x + m[i * 3 + 1] * y + m[i * 3 + 2] * z; }
static const double LSRGB_XYZ[9] = { 0.41239079926595934, 0.357584339383878, 0.1804807884018343, 0.21263900587151027, 0.715168678767756, 0.07219231536073371, 0.01933081871559182, 0.11919477979462598, 0.9505321522496607 };
static const double XYZ_LSRGB[9] = { 3.2409699419045226, -1.537383177570094, -0.4986107602930034, -0.9692436362808796, 1.8759675015077202, 0.04155505740717559, 0.05563007969699366, -0.20397695888897652, 1.0569715142428786 };
static const double LP3_XYZ[9] = { 0.4865709486482162, 0.26566769316909306, 0.1982172852343625, 0.2289745640697488, 0.6917385218365064, 0.079286914093745, 0, 0.04511338185890264, 1.043944368900976 };
static const double XYZ_LP3[9] = { 2.493496911941425, -0.9313836179191239, -0.40271078445071684, -0.8294889695615747, 1.7626640603183463, 0.023624685841943577, 0.03584583024378447, -0.07617238926804182, 0.9568845240076872 };
static const double LA98_XYZ[9] = { 0.5766690429101305, 0.1855582379065463, 0.1882286462349947, 0.29734497525053605, 0.6273635662554661, 0.0752914584939978, 0.02703136138641234, 0.07068885253582723, 0.9913375368376388 };
static const double XYZ_LA98[9] = { 2.0415879038107465, -0.5650069742788596, -0.34473135077832956, -0.9692436362808795, 1.8759675015077202, 0.04155505740717557, 0.013444280632031142, -0.11836239223101838, 1.0151749943912054 };
static const double L2020_XYZ[9] = { 0.6369580483012914, 0.14461690358620832, 0.1688809751641721, 0.2627002120112671, 0.6779980715188708, 0.05930171646986196, 0, 0.028072693049087428, 1.060985057710791 };
static const double XYZ_L2020[9] = { 1.7166511879712674, -0.35567078377639233, -0.25336628137365974, -0.6666843518324892, 1.6164812366349395, 0.01576854581391113, 0.017639857445310783, -0.042770613257808524, 0.9421031212354738 };
static const double LPRO_XYZ50[9] = { 0.7977666449006423, 0.13518129740053308, 0.0313477341283922, 0.2880748288194013, 0.711835234241873, 0.00008993693872564, 0, 0, 0.8251046025104602 };
static const double XYZ50_LPRO[9] = { 1.3457868816471583, -0.25557208737979464, -0.05110186497554526, -0.5446307051249019, 1.5082477428451468, 0.02052744743642139, 0, 0, 1.2119675456389452 };
static const double D65_D50[9] = { 1.0479297925449969, 0.022946870601609652, -0.05019226628920524, 0.02962780877005599, 0.9904344267538799, -0.017073799063418826, -0.009243040646204504, 0.015055191490298152, 0.7518742814281371 };
static const double D50_D65[9] = { 0.955473421488075, -0.02309845494876471, 0.06325924320057072, -0.0283697093338637, 1.0099953980813041, 0.021041441191917323, 0.012314014864481998, -0.020507649298898964, 1.330365926242124 };
static const double XYZ_LMS[9] = { 0.819022437996703, 0.3619062600528904, -0.1288737815209879, 0.0329836539323885, 0.9292868615863434, 0.0361446663506424, 0.0481771893596242, 0.2642395317527308, 0.6335478284694309 };
static const double LMS_XYZ[9] = { 1.2268798758459243, -0.5578149944602171, 0.2813910456659647, -0.0405757452148008, 1.112286803280317, -0.0717110580655164, -0.0763729366746601, -0.4214933324022432, 1.5869240198367816 };
static const double LMS_OK[9] = { 0.210454268309314, 0.7936177747023054, -0.0040720430116193, 1.9779985324311684, -2.4285922420485799, 0.450593709617411, 0.0259040424655478, 0.7827717124575296, -0.8086757549230774 };
static const double OK_LMS[9] = { 1.0, 0.3963377773761749, 0.2158037573099136, 1.0, -0.1055613458156586, -0.0638541728258133, 1.0, -0.0894841775298119, -1.2914855480194092 };
static const double D50W[3] = { 0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585 };

static double srgb_lin(double x) { double a = fabs(x); return copysign(a <= 0.04045 ? a / 12.92 : pow((a + 0.055) / 1.055, 2.4), x); }
static double srgb_gam(double x) { double a = fabs(x); return copysign(a > 0.0031308 ? 1.055 * pow(a, 1 / 2.4) - 0.055 : 12.92 * a, x); }
static void hsl_rgb(const double in[3], double o[3]) {
    double h = fmod(in[0], 360); if (h < 0) h += 360;
    double s = in[1] / 100, l = in[2] / 100;
    for (int i = 0; i < 3; i++) { double n = (double[]){ 0, 8, 4 }[i], k = fmod(n + h / 30, 12), a = s * fmin(l, 1 - l); o[i] = l - a * fmax(-1, fmin(fmin(k - 3, 9 - k), 1)); }
}
static void rgb_hsl(const double in[3], double o[3]) {
    double r = in[0], g = in[1], b = in[2], mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), h = NAN, s = 0, l = (mn + mx) / 2, d = mx - mn;
    if (d != 0) {
        s = (l == 0 || l == 1) ? 0 : (mx - l) / fmin(l, 1 - l);
        if (mx == r) h = (g - b) / d + (g < b ? 6 : 0); else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
        h *= 60;
    }
    if (s < 0) { if (!isnan(h)) h += 180; s = fabs(s); }
    if (!isnan(h) && h >= 360) h -= 360;
    o[0] = isnan(h) ? 0 : h; o[1] = s * 100; o[2] = l * 100;
}
/* any space to XYZ D65 */
static void to_xyz(int sp, const double in[3], double v[3]) {
    double t[3] = { in[0], in[1], in[2] };
    switch (sp) {
    case CSPACE_SRGB: for (int i = 0; i < 3; i++) t[i] = srgb_lin(t[i]); /* fallthrough */
    case CSPACE_SRGB_LINEAR: mul3(LSRGB_XYZ, t); break;
    case CSPACE_P3: for (int i = 0; i < 3; i++) t[i] = srgb_lin(t[i]); mul3(LP3_XYZ, t); break;
    case CSPACE_A98: for (int i = 0; i < 3; i++) t[i] = copysign(pow(fabs(t[i]), 563.0 / 256), t[i]); mul3(LA98_XYZ, t); break;
    case CSPACE_PROPHOTO: for (int i = 0; i < 3; i++) { double a = fabs(t[i]); t[i] = copysign(a <= 16.0 / 512 ? a / 16 : pow(a, 1.8), t[i]); } mul3(LPRO_XYZ50, t); mul3(D50_D65, t); break;
    case CSPACE_REC2020: for (int i = 0; i < 3; i++) { double a = fabs(t[i]); t[i] = copysign(a < 0.08124285829863151 ? a / 4.5 : pow((a + 0.09929682680944) / 1.09929682680944, 1 / 0.45), t[i]); } mul3(L2020_XYZ, t); break;
    case CSPACE_P3_LINEAR: mul3(LP3_XYZ, t); break;
    case CSPACE_XYZ50: mul3(D50_D65, t); break;
    case CSPACE_XYZ65: break;
    case CSPACE_LCH: case CSPACE_LAB: {
        if (sp == CSPACE_LCH) { double c = t[1], h = t[2] * M_PI / 180; t[1] = c * cos(h); t[2] = c * sin(h); }
        const double k = 24389.0 / 27, e = 216.0 / 24389;
        double f1 = (t[0] + 16) / 116, f0 = t[1] / 500 + f1, f2 = f1 - t[2] / 200;
        double x = pow(f0, 3) > e ? pow(f0, 3) : (116 * f0 - 16) / k, y = t[0] > k * e ? pow((t[0] + 16) / 116, 3) : t[0] / k, z = pow(f2, 3) > e ? pow(f2, 3) : (116 * f2 - 16) / k;
        t[0] = x * D50W[0]; t[1] = y * D50W[1]; t[2] = z * D50W[2]; mul3(D50_D65, t); break;
    }
    case CSPACE_OKLCH: case CSPACE_OKLAB:
        if (sp == CSPACE_OKLCH) { double c = t[1], h = t[2] * M_PI / 180; t[1] = c * cos(h); t[2] = c * sin(h); }
        mul3(OK_LMS, t); for (int i = 0; i < 3; i++) t[i] = t[i] * t[i] * t[i]; mul3(LMS_XYZ, t); break;
    case CSPACE_HSL: case CSPACE_HWB: {
        double r[3];
        if (sp == CSPACE_HSL) hsl_rgb(t, r);
        else {
            double w = t[1] / 100, b = t[2] / 100;
            if (w + b >= 1) { r[0] = r[1] = r[2] = w / (w + b); }
            else { double h3[3] = { t[0], 100, 50 }; hsl_rgb(h3, r); for (int i = 0; i < 3; i++) r[i] = r[i] * (1 - w - b) + w; }
        }
        for (int i = 0; i < 3; i++) t[i] = srgb_lin(r[i]);
        mul3(LSRGB_XYZ, t); break;
    }
    }
    memcpy(v, t, sizeof t);
}
static double hue_of(double a, double b) { double h = atan2(b, a) * 180 / M_PI; return h < 0 ? h + 360 : h; }
static void from_xyz(int sp, const double in[3], double v[3]) {
    double t[3] = { in[0], in[1], in[2] };
    switch (sp) {
    case CSPACE_SRGB: case CSPACE_SRGB_LINEAR: case CSPACE_HSL: case CSPACE_HWB:
        mul3(XYZ_LSRGB, t);
        if (sp == CSPACE_SRGB_LINEAR) break;
        for (int i = 0; i < 3; i++) t[i] = srgb_gam(t[i]);
        if (sp == CSPACE_HSL) { double o[3]; rgb_hsl(t, o); memcpy(t, o, sizeof o); }
        else if (sp == CSPACE_HWB) { double o[3]; rgb_hsl(t, o); double w = fmin(t[0], fmin(t[1], t[2])), bk = 1 - fmax(t[0], fmax(t[1], t[2])); t[0] = o[0]; t[1] = w * 100; t[2] = bk * 100; }
        break;
    case CSPACE_P3: mul3(XYZ_LP3, t); for (int i = 0; i < 3; i++) t[i] = srgb_gam(t[i]); break;
    case CSPACE_A98: mul3(XYZ_LA98, t); for (int i = 0; i < 3; i++) t[i] = copysign(pow(fabs(t[i]), 256.0 / 563), t[i]); break;
    case CSPACE_PROPHOTO: mul3(D65_D50, t); mul3(XYZ50_LPRO, t); for (int i = 0; i < 3; i++) { double a = fabs(t[i]); t[i] = copysign(a >= 1.0 / 512 ? pow(a, 1 / 1.8) : 16 * a, t[i]); } break;
    case CSPACE_REC2020: mul3(XYZ_L2020, t); for (int i = 0; i < 3; i++) { double a = fabs(t[i]); t[i] = copysign(a > 0.018053968510807 ? 1.09929682680944 * pow(a, 0.45) - 0.09929682680944 : 4.5 * a, t[i]); } break;
    case CSPACE_P3_LINEAR: mul3(XYZ_LP3, t); break;
    case CSPACE_XYZ50: mul3(D65_D50, t); break;
    case CSPACE_XYZ65: break;
    case CSPACE_LAB: case CSPACE_LCH: {
        mul3(D65_D50, t);
        const double k = 24389.0 / 27, e = 216.0 / 24389;
        double f[3]; for (int i = 0; i < 3; i++) { double x = t[i] / D50W[i]; f[i] = x > e ? cbrt(x) : (k * x + 16) / 116; }
        t[0] = 116 * f[1] - 16; t[1] = 500 * (f[0] - f[1]); t[2] = 200 * (f[1] - f[2]);
        if (sp == CSPACE_LCH) { double c = hypot(t[1], t[2]), h = hue_of(t[1], t[2]); t[1] = c; t[2] = h; }
        break;
    }
    case CSPACE_OKLAB: case CSPACE_OKLCH:
        mul3(XYZ_LMS, t); for (int i = 0; i < 3; i++) t[i] = cbrt(t[i]); mul3(LMS_OK, t);
        if (sp == CSPACE_OKLCH) { double c = hypot(t[1], t[2]), h = hue_of(t[1], t[2]); t[1] = c; t[2] = h; }
        break;
    }
    memcpy(v, t, sizeof t);
}
/* analogous channel classes for carrying missing components forward: 1 red, 2 green, 3 blue, 4 lightness,
   5 colourfulness, 6 hue, 7 opponent a, 8 opponent b, 0 none */
static int analog(int sp, int i) {
    switch (sp) {
    case CSPACE_LAB: case CSPACE_OKLAB: return (int[]){ 4, 7, 8 }[i];
    case CSPACE_LCH: case CSPACE_OKLCH: return (int[]){ 4, 5, 6 }[i];
    case CSPACE_HSL: return (int[]){ 6, 5, 4 }[i];
    case CSPACE_HWB: return i == 0 ? 6 : 0;
    default: return i + 1;
    }
}
void css_color_convert(const CssColor *in, int space, CssColor *out) {
    CssColor r = *in;
    if (in->space != space) {
        double c[3], x[3];
        for (int i = 0; i < 3; i++) c[i] = (in->none >> i & 1) ? 0 : in->c[i];
        to_xyz(in->space, c, x); from_xyz(space, x, r.c);
        r.space = (uint8_t)space; r.none = in->none & 8;
        for (int i = 0; i < 3; i++) if (in->none >> i & 1) { int a = analog(in->space, i); for (int j = 0; j < 3; j++) if (a && analog(space, j) == a) r.none |= (uint8_t)(1 << j); }
        /* both opponent axes missing: chroma and hue are too */
        if ((in->space == CSPACE_LAB || in->space == CSPACE_OKLAB) && (in->none & 6) == 6 && (space == CSPACE_LCH || space == CSPACE_OKLCH)) r.none |= 6;
        r.legacy = false;
    }
    for (int i = 0; i < 3; i++) if (r.none >> i & 1) r.c[i] = 0;
    if (r.none & 8) r.a = 0;
    *out = r;
}
uint32_t css_color_rgba8(const CssColor *c) {
    CssColor s; css_color_convert(c, CSPACE_SRGB, &s);
    int v[3]; for (int i = 0; i < 3; i++) { double x = isnan(s.c[i]) ? 0 : fmin(1, fmax(0, s.c[i])); v[i] = (int)lround(x * 255); }
    double a = isnan(s.a) ? 0 : fmin(1, fmax(0, s.a));
    return ((uint32_t)lround(a * 255) << 24) | ((uint32_t)v[0] << 16) | ((uint32_t)v[1] << 8) | (uint32_t)v[2];
}

/* ---------------- parsing ---------------- */
typedef struct { const char *p; const CssColor *cur; const char *const *kw; const double *kv; int nkw; uint8_t kwnone; } CP;
static void ws(CP *c) { while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r' || *c->p == '\f') c->p++; }
static bool eat(CP *c, char ch) { ws(c); if (*c->p != ch) return false; c->p++; return true; }
static int ident(CP *c, char *buf, int cap) {
    ws(c); int n = 0;
    while ((isalnum((unsigned char)*c->p) || *c->p == '-' || *c->p == '_') && n < cap - 1) { if (!n && isdigit((unsigned char)*c->p)) break; if (!n && *c->p == '-' && (isdigit((unsigned char)c->p[1]) || c->p[1] == '.')) break; buf[n++] = *c->p++; }
    buf[n] = 0; return n;
}
static bool word_is(CP *c, const char *w) {
    ws(c); size_t n = strlen(w);
    if (strncmp(c->p, w, n) || isalnum((unsigned char)c->p[n]) || c->p[n] == '-') return false;
    c->p += n; return true;
}
/* a numeric value: pct gives the value of 100%, angles become degrees; *isnone set for none or a keyword that is none */
static double expr_sum(CP *c, double pct, bool *ok, bool *isnone);
static double expr_unit(CP *c, double pct, bool *ok, bool *isnone) {
    ws(c);
    if (*c->p == '(') { c->p++; double v = expr_sum(c, pct, ok, isnone); if (!eat(c, ')')) *ok = false; return v; }
    char *e; double v = strtod(c->p, &e);
    if (e != c->p && (isdigit((unsigned char)*c->p) || *c->p == '.' || *c->p == '-' || *c->p == '+')) {
        c->p = e;
        if (*c->p == '%') { c->p++; return v / 100 * pct; }
        char u[8]; int n = 0; while (isalpha((unsigned char)*c->p) && n < 7) u[n++] = *c->p++; u[n] = 0;
        if (!n) return v;
        if (!strcmp(u, "deg")) return v; if (!strcmp(u, "rad")) return v * 180 / M_PI; if (!strcmp(u, "grad")) return v * 0.9; if (!strcmp(u, "turn")) return v * 360;
        if (!strcmp(u, "e") || !strncmp(u, "e", 1)) { *ok = false; return 0; }
        *ok = false; return 0;
    }
    char id[24]; if (!ident(c, id, sizeof id)) { *ok = false; return 0; }
    if (*c->p == '(') {
        c->p++;
        double a[3]; int n = 0; bool nn = false;
        if (!strcmp(id, "round")) { char m[16]; const char *save = c->p; if (ident(c, m, sizeof m) && (!strcmp(m, "nearest") || !strcmp(m, "up") || !strcmp(m, "down") || !strcmp(m, "to-zero")) && eat(c, ',')) {} else c->p = save; }
        do { if (n < 3) a[n++] = expr_sum(c, pct, ok, &nn); else expr_sum(c, pct, ok, &nn); } while (eat(c, ','));
        if (!eat(c, ')')) *ok = false;
        if (nn && isnone) *isnone = true;
        if (!strcmp(id, "calc")) return a[0];
        if (!strcmp(id, "min")) { double m = a[0]; for (int i = 1; i < n; i++) m = fmin(m, a[i]); return m; }
        if (!strcmp(id, "max")) { double m = a[0]; for (int i = 1; i < n; i++) m = fmax(m, a[i]); return m; }
        if (!strcmp(id, "clamp") && n == 3) return fmax(a[0], fmin(a[1], a[2]));
        if (!strcmp(id, "abs")) return fabs(a[0]);
        if (!strcmp(id, "sign")) return a[0] > 0 ? 1 : a[0] < 0 ? -1 : 0;
        if (!strcmp(id, "sqrt")) return sqrt(a[0]);
        if (!strcmp(id, "pow") && n == 2) return pow(a[0], a[1]);
        if (!strcmp(id, "round")) return n == 2 && a[1] ? round(a[0] / a[1]) * a[1] : round(a[0]);
        if (!strcmp(id, "mod") && n == 2) return a[0] - a[1] * floor(a[0] / a[1]);
        if (!strcmp(id, "rem") && n == 2) return fmod(a[0], a[1]);
        if (!strcmp(id, "sin")) return sin(a[0] * M_PI / 180);
        if (!strcmp(id, "cos")) return cos(a[0] * M_PI / 180);
        *ok = false; return 0;
    }
    if (!strcmp(id, "pi")) return M_PI;
    if (!strcmp(id, "infinity")) return INFINITY;
    if (!strcmp(id, "-infinity")) return -INFINITY;
    if (!strcmp(id, "nan")) return 0;
    if (!strcmp(id, "e")) return M_E;
    if (!strcmp(id, "none")) { if (isnone) *isnone = true; return 0; }
    for (int i = 0; i < c->nkw; i++) if (!strcmp(id, c->kw[i])) { if ((c->kwnone >> i & 1) && isnone) *isnone = true; return c->kv[i]; }
    *ok = false; return 0;
}
static double expr_prod(CP *c, double pct, bool *ok, bool *isnone) {
    double v = expr_unit(c, pct, ok, isnone);
    for (;;) { const char *s = c->p; ws(c); if (*c->p == '*') { c->p++; v *= expr_unit(c, 1, ok, isnone); } else if (*c->p == '/') { c->p++; v /= expr_unit(c, 1, ok, isnone); } else { c->p = s; return v; } }
}
static double expr_sum(CP *c, double pct, bool *ok, bool *isnone) {
    double v = expr_prod(c, pct, ok, isnone);
    for (;;) {
        const char *s = c->p; ws(c);
        if ((*c->p == '+' || *c->p == '-') && c->p > s && (c->p[1] == ' ' || c->p[1] == '\t' || c->p[1] == '\n')) { char op = *c->p++; double w = expr_prod(c, pct, ok, isnone); v = op == '+' ? v + w : v - w; }
        else { c->p = s; return v; }
    }
}
/* one channel at the top level: none, a number/percentage/angle, a keyword or a math function; a bare none
   (not inside calc) is missing */
static bool channel(CP *c, double pct, double *v, bool *none) {
    ws(c); const char *s = c->p; char id[24]; *none = false;
    if (ident(c, id, sizeof id) && *c->p != '(') {
        if (!strcmp(id, "none")) { *v = 0; *none = true; return true; }
        for (int i = 0; i < c->nkw; i++) if (!strcmp(id, c->kw[i])) { *v = c->kv[i]; *none = c->kwnone >> i & 1; return true; }
        return false;
    }
    c->p = s; bool ok = true;
    *v = expr_unit(c, pct, &ok, NULL);
    return ok;
}
static int space_named(const char *n) {
    static const char *const names[] = { "srgb", "srgb-linear", "display-p3", "a98-rgb", "prophoto-rgb", "rec2020", "xyz-d50", "xyz-d65", "lab", "lch", "oklab", "oklch", "hsl", "hwb", "display-p3-linear" };
    for (int i = 0; i < CSPACE_COUNT; i++) if (!strcmp(n, names[i])) return i;
    if (!strcmp(n, "xyz")) return CSPACE_XYZ65;
    return -1;
}
static bool parse_any(CP *c, CssColor *o);
static double norm_hue(double h) { h = fmod(h, 360); if (h < 0) h += 360; return h; }
/* rgb(), hsl(), hwb(), lab(), lch(), oklab(), oklch(), color(): absolute or relative ("from") */
static bool parse_fn(CP *c, const char *fn, CssColor *o) {
    int sp; bool rgbfn = !strcmp(fn, "rgb") || !strcmp(fn, "rgba"), hslfn = !strcmp(fn, "hsl") || !strcmp(fn, "hsla");
    if (rgbfn) sp = CSPACE_SRGB; else if (hslfn) sp = CSPACE_HSL; else if (!strcmp(fn, "color")) sp = -1; else sp = space_named(fn);
    if (sp == -2) return false;
    CssColor from; bool rel = word_is(c, "from");
    if (rel && !parse_any(c, &from)) return false;
    if (sp < 0) { char n[24]; if (!ident(c, n, sizeof n) || (sp = space_named(n)) < 0 || (sp >= CSPACE_LAB && sp != CSPACE_P3_LINEAR)) return false; }
    if (sp < 0 || sp >= CSPACE_COUNT) return false;
    double pr[3] = { 1, 1, 1 };
    switch (sp) {
    case CSPACE_SRGB: if (rgbfn) pr[0] = pr[1] = pr[2] = 255; break;
    case CSPACE_HSL: case CSPACE_HWB: pr[0] = 0; pr[1] = pr[2] = 100; break;
    case CSPACE_LAB: pr[0] = 100; pr[1] = pr[2] = 125; break;
    case CSPACE_LCH: pr[0] = 100; pr[1] = 150; pr[2] = 0; break;
    case CSPACE_OKLAB: pr[0] = 1; pr[1] = pr[2] = 0.4; break;
    case CSPACE_OKLCH: pr[0] = 1; pr[1] = 0.4; pr[2] = 0; break;
    }
    bool hue0 = sp == CSPACE_HSL || sp == CSPACE_HWB, hue2 = sp == CSPACE_LCH || sp == CSPACE_OKLCH;
    const char *kwn[4]; double kv[4]; CP sub = *c;
    if (rel) {
        CssColor f; css_color_convert(&from, sp, &f);
        static const char *const K[][3] = { { "r", "g", "b" }, { "r", "g", "b" }, { "r", "g", "b" }, { "r", "g", "b" }, { "r", "g", "b" }, { "r", "g", "b" }, { "x", "y", "z" }, { "x", "y", "z" }, { "l", "a", "b" }, { "l", "c", "h" }, { "l", "a", "b" }, { "l", "c", "h" }, { "h", "s", "l" }, { "h", "w", "b" }, { "r", "g", "b" } };
        for (int i = 0; i < 3; i++) { kwn[i] = K[sp][i]; kv[i] = f.c[i] * (sp == CSPACE_SRGB && rgbfn ? 255 : 1); }
        if (hue0) kv[0] = norm_hue(kv[0]); if (hue2) kv[2] = norm_hue(kv[2]);
        kwn[3] = "alpha"; kv[3] = f.a;
        sub.kw = kwn; sub.kv = kv; sub.nkw = 4; sub.kwnone = f.none;
    } else { sub.kw = NULL; sub.nkw = 0; sub.kwnone = 0; }
    double v[4] = { 0, 0, 0, 1 }; bool nn[4] = { 0 }; int n = 0; bool commas = false;
    for (; n < 3; n++) {
        if (n && !rel && eat(&sub, ',')) { if (n == 1) commas = true; else if (!commas) return false; }
        else if (n && commas) return false;
        if (!channel(&sub, pr[n] ? pr[n] : 1, &v[n], &nn[n])) return false;
    }
    if (commas ? eat(&sub, ',') : eat(&sub, '/')) { if (!channel(&sub, 1, &v[3], &nn[3])) return false; }
    else if (rel) { v[3] = kv[3]; nn[3] = sub.kwnone >> 3 & 1; }
    if (!eat(&sub, ')')) return false;
    if (commas && (nn[0] || nn[1] || nn[2] || nn[3] || (!rgbfn && !hslfn))) return false;
    c->p = sub.p;
    memset(o, 0, sizeof *o);
    o->space = (uint8_t)sp; o->legacy = !rel && (rgbfn || hslfn || sp == CSPACE_HWB);
    for (int i = 0; i < 3; i++) { o->c[i] = v[i]; if (nn[i]) o->none |= (uint8_t)(1 << i); }
    o->a = fmin(1, fmax(0, v[3])); if (nn[3]) o->none |= 8;
    if (sp == CSPACE_SRGB && rgbfn) for (int i = 0; i < 3; i++) { o->c[i] /= 255; if (!rel) o->c[i] = fmin(1, fmax(0, o->c[i])); }
    if (hue0) o->c[0] = norm_hue(o->c[0]);
    if (hue2) { o->c[2] = norm_hue(o->c[2]); if (o->c[1] < 0) o->c[1] = 0; }
    if (sp == CSPACE_LAB || sp == CSPACE_LCH) o->c[0] = fmin(100, fmax(0, o->c[0]));
    if (sp == CSPACE_OKLAB || sp == CSPACE_OKLCH) o->c[0] = fmin(1, fmax(0, o->c[0]));
    if (sp == CSPACE_HSL && !rel) { if (o->c[1] < 0) o->c[1] = 0; }
    return true;
}
/* a polar space's hue is powerless when its chroma/saturation is (about) zero; interpolation treats it as missing */
static void powerless(CssColor *x) {
    if ((x->space == CSPACE_LCH && x->c[1] <= 1e-4) || (x->space == CSPACE_OKLCH && x->c[1] <= 1e-6)) x->none |= 4;
    if ((x->space == CSPACE_HSL && x->c[1] <= 1e-4) || (x->space == CSPACE_HWB && x->c[1] + x->c[2] >= 100 - 1e-4)) x->none |= 1;
}
static bool parse_mix(CP *c, CssColor *o) {
    int sp = CSPACE_OKLAB, hm = 0;   /* hue method: 0 shorter, 1 longer, 2 increasing, 3 decreasing */
    if (word_is(c, "in")) {
        char n[24]; if (!ident(c, n, sizeof n) || (sp = space_named(n)) < 0) return false;
        static const char *const hms[] = { "shorter", "longer", "increasing", "decreasing" };
        const char *save = c->p; char h[16];
        if (ident(c, h, sizeof h)) {
            int k = -1; for (int i = 0; i < 4; i++) if (!strcmp(h, hms[i])) k = i;
            if (k < 0 || !word_is(c, "hue") || !(sp == CSPACE_HSL || sp == CSPACE_HWB || sp == CSPACE_LCH || sp == CSPACE_OKLCH)) { if (k >= 0) return false; c->p = save; }
            else hm = k;
        }
        if (!eat(c, ',')) return false;
    }
    CssColor col[2]; double pc[2]; bool hp[2]; int n = 0;
    for (; n < 2; n++) {
        hp[n] = false; pc[n] = 0; ws(c);
        bool okp = true; const char *s = c->p;
        if (isdigit((unsigned char)*c->p) || *c->p == '.' || !strncmp(c->p, "calc(", 5)) { pc[n] = expr_unit(c, 100, &okp, NULL); if (okp && c->p[-1] == '%') hp[n] = true; else if (okp && strncmp(s, "calc(", 5) == 0) hp[n] = true; else return false; }
        if (!parse_any(c, &col[n])) return false;
        if (!hp[n]) { ws(c); if (isdigit((unsigned char)*c->p) || *c->p == '.' || !strncmp(c->p, "calc(", 5)) { pc[n] = expr_unit(c, 100, &okp, NULL); if (!okp) return false; hp[n] = true; } }
        if (hp[n] && (pc[n] < 0 || pc[n] > 100)) return false;
        if (!eat(c, ',')) { n++; break; }
    }
    if (!eat(c, ')')) return false;
    double p0, p1, mult = 1;
    if (n == 1) { p0 = hp[0] ? pc[0] : 100; mult = p0 / 100; p1 = 0; col[1] = col[0]; p0 = 100; }
    else {
        p0 = hp[0] ? pc[0] : hp[1] ? 100 - pc[1] : 50; p1 = hp[1] ? pc[1] : 100 - p0;
        double sum = p0 + p1; if (sum <= 0) { p0 = p1 = 50; sum = 100; mult = 0; }
        else if (sum < 100) mult = sum / 100;
        p0 /= sum / 100; p1 /= sum / 100;
    }
    CssColor a, b; css_color_convert(&col[0], sp, &a); css_color_convert(&col[1], sp, &b);
    powerless(&a); powerless(&b);
    double t = p1 / 100;
    memset(o, 0, sizeof *o); o->space = (uint8_t)sp;
    /* missing components take the other colour's value */
    for (int i = 0; i < 4; i++) {
        bool na = a.none >> i & 1, nb = b.none >> i & 1;
        double *va = i < 3 ? &a.c[i] : &a.a, *vb = i < 3 ? &b.c[i] : &b.a;
        if (na && !nb) *va = *vb; else if (nb && !na) *vb = *va; else if (na && nb) o->none |= (uint8_t)(1 << i);
    }
    o->a = a.a + (b.a - a.a) * t;
    int hue = (sp == CSPACE_HSL || sp == CSPACE_HWB) ? 0 : (sp == CSPACE_LCH || sp == CSPACE_OKLCH) ? 2 : -1;
    for (int i = 0; i < 3; i++) {
        if (i == hue) {
            double h0 = a.c[i], h1 = b.c[i], d = h1 - h0;
            if (hm == 0) { if (d > 180) h0 += 360; else if (d < -180) h1 += 360; }
            else if (hm == 1) { if (d > 0 && d < 180) h0 += 360; else if (d > -180 && d <= 0) h1 += 360; }
            else if (hm == 2) { if (d < 0) h1 += 360; }
            else { if (d > 0) h0 += 360; }
            o->c[i] = norm_hue(h0 + (h1 - h0) * t);
        } else {
            double x0 = a.c[i] * a.a, x1 = b.c[i] * b.a, m = x0 + (x1 - x0) * t;
            o->c[i] = o->a > 0 ? m / o->a : a.c[i] + (b.c[i] - a.c[i]) * t;
        }
    }
    for (int i = 0; i < 3; i++) if (o->none >> i & 1) o->c[i] = 0;
    o->a *= mult;
    if (o->none & 8) o->a = 0;
    return true;
}
static bool parse_any(CP *c, CssColor *o) {
    ws(c);
    memset(o, 0, sizeof *o); o->a = 1;
    if (*c->p == '#') {
        const char *s = ++c->p; int n = 0; while (isxdigit((unsigned char)s[n])) n++;
        if (isalnum((unsigned char)s[n]) || (n != 3 && n != 4 && n != 6 && n != 8)) return false;
        unsigned d[8]; for (int i = 0; i < n; i++) { char ch = s[i]; d[i] = (unsigned)(isdigit((unsigned char)ch) ? ch - '0' : (ch | 32) - 'a' + 10); }
        c->p += n; o->legacy = true;
        if (n <= 4) { for (int i = 0; i < 3; i++) o->c[i] = d[i] * 17 / 255.0; if (n == 4) o->a = d[3] * 17 / 255.0; }
        else { for (int i = 0; i < 3; i++) o->c[i] = (d[2 * i] * 16 + d[2 * i + 1]) / 255.0; if (n == 8) o->a = (d[6] * 16 + d[7]) / 255.0; }
        return true;
    }
    char id[32]; if (!ident(c, id, sizeof id)) return false;
    if (*c->p == '(') {
        c->p++;
        if (!strcmp(id, "color-mix")) return parse_mix(c, o);
        if (!strcmp(id, "alpha")) {   /* alpha(from <color> [/ <alpha>]) */
            CssColor f; if (!word_is(c, "from") || !parse_any(c, &f)) return false;
            *o = f; o->legacy = false;
            if (eat(c, '/')) {
                const char *kw[1] = { "alpha" }; double kv[1] = { f.a };
                CP sub = *c; sub.kw = kw; sub.kv = kv; sub.nkw = 1; sub.kwnone = (uint8_t)(f.none >> 3 & 1);
                double v; bool nn; if (!channel(&sub, 1, &v, &nn)) return false;
                c->p = sub.p; o->a = fmin(1, fmax(0, v)); o->none = (uint8_t)((o->none & 7) | (nn ? 8 : 0));
            }
            return eat(c, ')');
        }
        if (!strcmp(id, "contrast-color")) {   /* white or black, whichever contrasts more */
            CssColor f; if (!parse_any(c, &f) || !eat(c, ')')) return false;
            CssColor x; css_color_convert(&f, CSPACE_XYZ65, &x);
            double y = fmax(0, x.c[1]), cw = 1.05 / (y + 0.05), cb = (y + 0.05) / 0.05;
            memset(o, 0, sizeof *o); o->legacy = true; o->a = 1;
            if (cw >= cb) o->c[0] = o->c[1] = o->c[2] = 1;
            return true;
        }
        if (!strcmp(id, "light-dark")) { CssColor d; if (!parse_any(c, o) || !eat(c, ',') || !parse_any(c, &d) || !eat(c, ')')) return false; return true; }
        return parse_fn(c, id, o);
    }
    if (!strcmp(id, "transparent")) { o->legacy = true; o->a = 0; return true; }
    if (!strcmp(id, "currentcolor")) { if (c->cur) *o = *c->cur; else o->legacy = true; return true; }
    uint32_t rgb; if (!css_named_color(id, &rgb)) return false;
    o->legacy = true; o->c[0] = (rgb >> 16 & 255) / 255.0; o->c[1] = (rgb >> 8 & 255) / 255.0; o->c[2] = (rgb & 255) / 255.0;
    return true;
}
bool css_color_parse(const char *s, CssColor *out, const CssColor *current) {
    size_t n = strlen(s); char stackbuf[256], *buf = n < sizeof stackbuf ? stackbuf : malloc(n + 1);
    if (!buf) return false;
    for (size_t i = 0; i <= n; i++) buf[i] = (char)tolower((unsigned char)s[i]);
    CP c = { buf, current, NULL, NULL, 0, 0 };
    bool ok = parse_any(&c, out);
    if (ok) { ws(&c); ok = !*c.p; }
    if (buf != stackbuf) free(buf);
    return ok;
}

/* ---------------- serialization ---------------- */
static int num(char *b, int cap, double v) {
    if (fabs(v) < 5e-7) v = 0;
    char t[48]; snprintf(t, sizeof t, "%.6f", v);
    char *e = t + strlen(t); while (e > t && e[-1] == '0') *--e = 0; if (e > t && e[-1] == '.') *--e = 0;
    if (!strcmp(t, "-0")) strcpy(t, "0");
    return snprintf(b, (size_t)cap, "%s", t);
}
void css_color_serialize(const CssColor *c, char *buf, int cap) {
    int n = 0;
#define PUT(...) do { if (n < cap) n += snprintf(buf + n, (size_t)(cap - n), __VA_ARGS__); } while (0)
#define NUM(v) do { if (n < cap) n += num(buf + n, cap - n, (v)); } while (0)
    bool anynone = (c->none & 15) != 0;
    if (c->legacy && !anynone) {
        uint32_t x = css_color_rgba8(c); int a8 = (int)(x >> 24);
        if (a8 == 255) { PUT("rgb(%u, %u, %u)", x >> 16 & 255, x >> 8 & 255, x & 255); return; }
        double a = c->a; char t[24];
        /* alpha: the shortest of 2 or 3 decimals that maps back to the same 8-bit value */
        double r2 = round(a * 100) / 100; if ((int)lround(r2 * 255) == a8) a = r2; else a = round(a * 1000) / 1000;
        num(t, sizeof t, a);
        PUT("rgba(%u, %u, %u, %s)", x >> 16 & 255, x >> 8 & 255, x & 255, t); return;
    }
    CssColor s = *c;
    if ((c->space == CSPACE_HSL || c->space == CSPACE_HWB) && !anynone) css_color_convert(c, CSPACE_SRGB, &s);
    static const char *const fn[] = { "srgb", "srgb-linear", "display-p3", "a98-rgb", "prophoto-rgb", "rec2020", "xyz-d50", "xyz-d65" };
    bool pctc = s.space == CSPACE_HSL || s.space == CSPACE_HWB;
    if (s.space == CSPACE_P3_LINEAR) PUT("color(display-p3-linear");
    else if (s.space < CSPACE_LAB) PUT("color(%s", fn[s.space]);
    else PUT("%s(", (const char *[]){ "lab", "lch", "oklab", "oklch", "hsl", "hwb" }[s.space - CSPACE_LAB]);
    for (int i = 0; i < 3; i++) {
        if (s.space < CSPACE_LAB || s.space == CSPACE_P3_LINEAR || i) PUT(" ");
        if (s.none >> i & 1) PUT("none"); else { NUM(s.c[i]); if (pctc && i) PUT("%%"); }
    }
    if (s.none & 8) PUT(" / none");
    else if (s.a < 1) { PUT(" / "); NUM(s.a); }
    PUT(")");
#undef PUT
#undef NUM
}
