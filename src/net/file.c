#include "file.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILE_BODY_LIMIT (64u * 1024u * 1024u)
#define DIRECTORY_LIMIT 10000

static void html_escape(SB *output, const char *text) {
    for (; *text; text++) {
        switch (*text) {
        case '&': sb_puts(output, "&amp;"); break;
        case '<': sb_puts(output, "&lt;"); break;
        case '>': sb_puts(output, "&gt;"); break;
        case '"': sb_puts(output, "&quot;"); break;
        case '\'': sb_puts(output, "&#39;"); break;
        default: sb_putc(output, *text); break;
        }
    }
}

static const char *file_type(const char *path) {
    static const struct { const char *extension, *type; } types[] = {
        {"html", "text/html"}, {"htm", "text/html"}, {"css", "text/css"},
        {"js", "text/javascript"}, {"mjs", "text/javascript"}, {"json", "application/json"},
        {"txt", "text/plain"}, {"md", "text/plain"}, {"svg", "image/svg+xml"},
        {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
        {"gif", "image/gif"}, {"webp", "image/webp"}, {"avif", "image/avif"},
        {"mp4", "video/mp4"}, {"webm", "video/webm"}, {"mov", "video/quicktime"},
        {"mp3", "audio/mpeg"}, {"m4a", "audio/mp4"}, {"wav", "audio/wav"},
        {"ogg", "audio/ogg"}, {"flac", "audio/flac"}, {"pdf", "application/pdf"},
        {"wasm", "application/wasm"}, {"obj", "model/obj"}, {"gltf", "model/gltf+json"},
        {"glb", "model/gltf-binary"}, {"woff", "font/woff"}, {"woff2", "font/woff2"},
        {"ttf", "font/ttf"}, {"otf", "font/otf"}
    };
    const char *extension = strrchr(path, '.');
    if (extension) for (size_t index = 0; index < ARRLEN(types); index++)
        if (str_ieq(extension + 1, types[index].extension)) return types[index].type;
    return "application/octet-stream";
}

static void file_error(NetResponse *response, int status, const char *message) {
    response->status = status;
    response->status_text = xstrdup(message);
    response->body = xstrdup(message);
    response->body_len = strlen(message);
    headers_add(&response->headers, "content-type", "text/plain; charset=utf-8");
}

typedef struct { char *name; bool directory; uint64_t size; } FileEntry;

static int entry_compare(const void *left, const void *right) {
    const FileEntry *first = left, *second = right;
    if (first->directory != second->directory) return first->directory ? -1 : 1;
    return strcmp(first->name, second->name);
}

static bool directory_body(NetResponse *response, int descriptor, const URL *url, const char *path) {
    DIR *directory = fdopendir(descriptor);
    if (!directory) { close(descriptor); return false; }
    VEC(FileEntry) entries = {0};
    struct dirent *entry;
    bool truncated = false;
    int read_error = 0;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) { read_error = errno; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat info;
        if (fstatat(dirfd(directory), entry->d_name, &info, AT_SYMLINK_NOFOLLOW)) continue;
        if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode)) continue;
        if (entries.n == DIRECTORY_LIMIT) { truncated = true; break; }
        FileEntry item = {xstrdup(entry->d_name), S_ISDIR(info.st_mode), (uint64_t)info.st_size};
        vec_push(entries, item);
    }
    closedir(directory);
    if (entries.n > 1) qsort(entries.v, (size_t)entries.n, sizeof *entries.v, entry_compare);
    SB body, base; sb_init(&body); sb_init(&base);
    sb_puts(&base, "file://"); sb_puts(&base, url->path);
    if (!base.n || base.s[base.n - 1] != '/') sb_putc(&base, '/');
    sb_puts(&body, "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Files · Lumen</title>"
        "<style>body{margin:0;padding:20px;font:14px system-ui;background:#f4f8fc;color:#202b3b}h1{font-size:18px;overflow-wrap:anywhere}"
        "table{width:100%;border-collapse:collapse;background:white}td{padding:6px 10px;border-bottom:1px solid #e4eaf1}"
        "a{color:#245ea8;text-decoration:none;overflow-wrap:anywhere}a:hover{text-decoration:underline}.size{text-align:right;white-space:nowrap;color:#64748b}</style>"
        "</head><body><h1>");
    html_escape(&body, path);
    sb_puts(&body, "</h1><table aria-label='Folder contents'><tr><td colspan=2><a href=\"");
    char *parent = url_join(base.s, "../");
    html_escape(&body, parent ? parent : "file:///"); free(parent);
    sb_puts(&body, "\">↑ Parent folder</a></td></tr>");
    for (int index = 0; index < entries.n; index++) {
        FileEntry *item = &entries.v[index];
        char *encoded = url_encode_component(item->name);
        sb_puts(&body, "<tr><td><a href=\""); html_escape(&body, base.s); html_escape(&body, encoded);
        if (item->directory) sb_putc(&body, '/');
        sb_puts(&body, "\">"); sb_puts(&body, item->directory ? "▸ " : "· "); html_escape(&body, item->name);
        sb_puts(&body, "</a></td><td class=size>");
        if (item->directory) sb_puts(&body, "Folder"); else sb_printf(&body, "%llu B", (unsigned long long)item->size);
        sb_puts(&body, "</td></tr>");
        free(encoded); free(item->name);
    }
    vec_free(entries);
    sb_puts(&body, "</table>");
    if (truncated) sb_puts(&body, "<p>Showing at most 10,000 entries. Open a subfolder to narrow the listing.</p>");
    if (read_error) sb_puts(&body, "<p>Some entries could not be read.</p>");
    sb_puts(&body, "</body></html>");
    free(response->url); response->url = sb_take(&base);
    response->body_len = body.n; response->body = sb_take(&body);
    headers_add(&response->headers, "content-type", "text/html; charset=utf-8");
    return true;
}

NetResponse *net_file_response(const URL *url, const char *href) {
    NetResponse *response = xcalloc(1, sizeof *response);
    response->url = xstrdup(href);
    if (url->host && *url->host && !str_ieq(url->host, "localhost")) {
        file_error(response, 403, "Remote file hosts are not supported"); return response;
    }
    if (!url->path || url->path[0] != '/' || strstr(url->path, "%00")) {
        file_error(response, 400, "Invalid file path"); return response;
    }
    char *path = url_decode(url->path, strlen(url->path));
    int descriptor = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        int status = errno == EACCES || errno == EPERM ? 403 : 404;
        file_error(response, status, status == 403 ? "Permission denied" : "File not found");
        free(path); return response;
    }
    struct stat info;
    if (fstat(descriptor, &info)) {
        close(descriptor); free(path); file_error(response, 500, "Cannot inspect file"); return response;
    }
    if (S_ISDIR(info.st_mode)) {
        bool ok = directory_body(response, descriptor, url, path);
        free(path);
        if (!ok) { file_error(response, 403, "Cannot open directory"); return response; }
    } else if (!S_ISREG(info.st_mode)) {
        close(descriptor); free(path); file_error(response, 403, "Only regular files and directories can be opened"); return response;
    } else if (info.st_size < 0 || (uint64_t)info.st_size > FILE_BODY_LIMIT) {
        close(descriptor); free(path); file_error(response, 413, "File exceeds the 64 MiB buffered-read limit"); return response;
    } else {
        size_t size = (size_t)info.st_size, length = 0;
        response->body = xmalloc(size + 1);
        while (length < size) {
            ssize_t count = read(descriptor, response->body + length, size - length);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            length += (size_t)count;
        }
        close(descriptor);
        if (length != size) {
            free(response->body); free(path); file_error(response, 500, "File changed or could not be read"); return response;
        }
        response->body[length] = 0; response->body_len = length;
        headers_add(&response->headers, "content-type", file_type(path));
        free(path);
    }
    response->status = 200; response->status_text = xstrdup("OK");
    headers_add(&response->headers, "x-content-type-options", "nosniff");
    return response;
}
