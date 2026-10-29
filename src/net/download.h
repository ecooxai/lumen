/* Download manager: files saved from links, attachments and blobs. Large files that the
 * server can serve in byte ranges arrive over several connections at once (each with the
 * page's cookies), and every download can be paused, resumed and retried from where it stopped. */
#ifndef LUMEN_DOWNLOAD_H
#define LUMEN_DOWNLOAD_H
#include "net.h"

enum { DL_RUNNING, DL_PAUSED, DL_DONE, DL_FAILED, DL_CANCELLED };

typedef struct DlInfo {
    uint64_t id;
    int state;
    int64_t total, done;      /* total -1 while unknown */
    double bps;               /* recent transfer rate, bytes/s */
    int conns;                /* connections transferring now */
    bool resumable;
    char name[256], mime[96], where[1024], error[96];
} DlInfo;

/* Where downloads are written; the app installs one (Android: MediaStore Downloads). */
typedef struct DlStore {
    /* Creates the output for a new file: returns a writable descriptor (written with pwrite), the
       store's handle for it in where and the name actually used in final_name; -1 on failure. */
    int (*create)(const char *name, const char *mime, char *where, size_t where_n, char *final_name, size_t final_n);
    /* closes fd; keep publishes the finished file, otherwise the partial one is removed */
    void (*finish)(const char *where, int fd, bool keep);
    void (*open)(const char *where, const char *mime);
} DlStore;
void dl_set_store(const DlStore *store);
/* Remembers finished downloads in path across runs. */
void dl_init(const char *path);
/* Any thread, whenever a download starts, changes state or makes progress (rate limited). */
extern void (*dl_notify)(uint64_t id, bool started);

uint64_t dl_start(const char *url, const char *referrer, const char *name_hint);
uint64_t dl_save_bytes(const char *name, const char *mime, const void *data, size_t n);

/* Navigations: the response becomes a download when its head says so (an attachment, or a type
   Lumen cannot show); started then runs on the fetching thread before the body arrives. Audio,
   video and images stop after the head instead and are reported by dl_probe_finish as viewable. */
typedef struct DlProbe DlProbe;
DlProbe *dl_probe(NetRequest *req, const char *referrer, void (*started)(uint64_t id, void *ud), void *ud);
/* After net_fetch_sync returned r: 1 it became a download (the body went to the file),
   2 viewable media (empty body; show a viewer page), 0 an ordinary page. Frees p. */
int dl_probe_finish(DlProbe *p, NetResponse *r);
/* The decision itself, from a response head. 1 download, 2 media viewer, 0 page. */
int dl_classify(int status, const char *content_type, const char *disposition);
/* File name for a response: Content-Disposition, then the hint, then the URL, plus an extension from the type. */
void dl_file_name(const char *disposition, const char *hint, const char *url, const char *mime, char *out, size_t n);

void dl_pause(uint64_t id);
void dl_resume(uint64_t id);    /* also retries a failed download */
void dl_cancel(uint64_t id);
void dl_forget(uint64_t id);    /* drops a finished, failed or cancelled entry from the list */
void dl_open(uint64_t id);
int dl_list(DlInfo *out, int max);   /* newest first */
bool dl_get(uint64_t id, DlInfo *out);
uint64_t dl_version(void);           /* changes with any progress or state change */
uint64_t dl_state_version(void);     /* changes when the list or any state changes */
int dl_active(void);                 /* running downloads */
#endif
