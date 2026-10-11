#include "download.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __ANDROID__
#include <SDL3/SDL.h>
void android_publish_download(const char *path, const char *name, const char *mime);
#endif

void (*download_notify)(const char *path);

static void mkdirs(char *p) {
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}

const char *download_dir(void) {
    static char dir[1024];
    if (*dir) return dir;
    const char *e = getenv("LUMEN_DOWNLOAD_DIR");
    if (e && *e) snprintf(dir, sizeof dir, "%s", e);
    else {
#ifdef __ANDROID__
        const char *x = SDL_GetAndroidExternalStoragePath();
        snprintf(dir, sizeof dir, "%s/Download", x ? x : SDL_GetAndroidInternalStoragePath());
#else
        const char *h = getenv("HOME");
        snprintf(dir, sizeof dir, "%s/Downloads", h && *h ? h : "/tmp");
#endif
    }
    mkdirs(dir);
    return dir;
}

bool download_wanted(const char *ctype, const char *disp) {
    if (disp) { while (*disp == ' ') disp++; if (!strncasecmp(disp, "attachment", 10)) return true; }
    if (!ctype || !*ctype) return false;
    char t[128]; size_t i = 0;
    while (ctype[i] && ctype[i] != ';' && i < sizeof t - 1) { t[i] = (char)tolower((unsigned char)ctype[i]); i++; }
    while (i && t[i - 1] == ' ') i--;
    t[i] = 0;
    static const char *show[] = { "text/", "image/", "application/xhtml+xml", "application/xml", "application/json", "application/javascript",
                                  "application/x-javascript", "application/ecmascript", "application/rss+xml", "application/atom+xml", "multipart/x-mixed-replace", NULL };
    for (int k = 0; show[k]; k++) if (!strncmp(t, show[k], strlen(show[k]))) return false;
    return true;
}

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void pct_decode(const char *s, size_t len, char *out, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < n; i++) {
        if (s[i] == '%' && i + 2 < len + 0 && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { out[o++] = (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2])); i += 2; }
        else out[o++] = s[i];
    }
    out[o] = 0;
}

static bool disp_param(const char *d, const char *key, char *out, size_t n) {
    size_t kl = strlen(key);
    for (const char *p = d; p && *p; p = strchr(p, ';') ? strchr(p, ';') + 1 : NULL) {
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, key, kl) || p[kl] != '=') continue;
        p += kl + 1;
        if (*p == '"') {
            size_t o = 0; p++;
            while (*p && *p != '"' && o + 1 < n) { if (*p == '\\' && p[1]) p++; out[o++] = *p++; }
            out[o] = 0;
        } else {
            const char *e = p; while (*e && *e != ';') e++;
            while (e > p && e[-1] == ' ') e--;
            if (!strcmp(key, "filename*")) { const char *q = strstr(p, "''"); if (q && q < e) p = q + 2; }
            pct_decode(p, (size_t)(e - p), out, n);
        }
        return *out != 0;
    }
    return false;
}

static void sanitize(char *s) {
    for (char *p = s; *p; p++) if (*p == '/' || *p == '\\' || (unsigned char)*p < 32 || strchr(":*?\"<>|", *p)) *p = '_';
    while (*s == '.' || *s == ' ') memmove(s, s + 1, strlen(s));
    size_t l = strlen(s); while (l && (s[l - 1] == ' ' || s[l - 1] == '.')) s[--l] = 0;
}

void download_filename(const char *url, const char *disp, const char *hint, char *out, size_t n) {
    out[0] = 0;
    if (disp && !disp_param(disp, "filename*", out, n)) disp_param(disp, "filename", out, n);
    if (!*out && hint && *hint) snprintf(out, n, "%s", hint);
    if (!*out && url && strncmp(url, "data:", 5) && strncmp(url, "blob:", 5)) {
        const char *s = strstr(url, "://"); s = s ? strchr(s + 3, '/') : NULL;
        if (s) {
            const char *e = s + strcspn(s, "?#"), *b = e;
            while (b > s && b[-1] != '/') b--;
            pct_decode(b, (size_t)(e - b), out, n);
        }
    }
    sanitize(out);
    if (!*out) snprintf(out, n, "download");
}

bool download_save(const char *url, const char *disp, const char *ctype, const char *hint, const void *data, size_t len, char *out, size_t n) {
    char name[256]; download_filename(url, disp, hint, name, sizeof name);
    const char *dir = download_dir();
    const char *dot = strrchr(name, '.'); size_t stem = dot && dot != name ? (size_t)(dot - name) : strlen(name);
    FILE *f = NULL;
    for (int i = 0; i < 1000 && !f; i++) {
        if (i) snprintf(out, n, "%s/%.*s (%d)%s", dir, (int)stem, name, i, name + stem);
        else snprintf(out, n, "%s/%s", dir, name);
        int fd = open(out, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) f = fdopen(fd, "wb");
        else if (errno != EEXIST) return false;
    }
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    if (!ok) { unlink(out); return false; }
#ifdef __ANDROID__
    const char *fn = strrchr(out, '/');
    android_publish_download(out, fn ? fn + 1 : out, ctype ? ctype : "");
#else
    (void)ctype;
#endif
    if (download_notify) download_notify(out);
    return true;
}
