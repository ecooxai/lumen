#ifndef LUMEN_WT_H
#define LUMEN_WT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* All handles are opaque monotonically allocated IDs; every operation validates
 * its owner and remains <= Number.MAX_SAFE_INTEGER. No callback receives a
 * JsCtx or page-owned pointer. */
typedef void (*LumeWtWake)(void);
typedef struct LumeWtEvent LumeWtEvent;

enum LumeWtStatus {
    LUME_WT_OK = 0,
    LUME_WT_INVALID = -1,
    LUME_WT_CLOSED = -2,
    LUME_WT_QUOTA = -3,
    LUME_WT_STATE = -4,
    LUME_WT_UNSUPPORTED = -5,
    LUME_WT_TIMEOUT = -6
};
enum LumeWtOperation {
    LUME_WT_DATAGRAM_WRITE = 1,
    LUME_WT_DATAGRAM_READ = 2,
    LUME_WT_CREATE_BI = 3,
    LUME_WT_CREATE_UNI = 4,
    LUME_WT_ACCEPT_BI = 5,
    LUME_WT_ACCEPT_UNI = 6,
    LUME_WT_STREAM_READ = 7,
    LUME_WT_STREAM_WRITE = 8,
    LUME_WT_STREAM_FINISH = 9,
    LUME_WT_STREAM_CANCEL_READ = 10,
    LUME_WT_STREAM_ABORT_WRITE = 11,
    LUME_WT_CLOSE = 12,
    LUME_WT_STATS = 13,
    LUME_WT_CANCEL_DATAGRAM_READ = 14,
    LUME_WT_CANCEL_ACCEPT_BI = 15,
    LUME_WT_CANCEL_ACCEPT_UNI = 16
};
typedef struct LumeWtCounts {
    uint64_t owners;
    uint64_t sessions;
    uint64_t streams;
    uint64_t tasks;
    uint64_t queued_event_bytes;
    uint64_t pending_send_bytes;
} LumeWtCounts;

/* Set once on the embedding's main thread, before creating owners. The wake
 * function may run on either of two worker threads and must only signal the
 * global event loop. It must remain valid for the process lifetime. */
int32_t lume_wt_initialize(LumeWtWake wake);
uint64_t lume_wt_owner_create(void);
/* Cancels ownership, suppresses future events, closes all dedicated endpoints,
 * and waits at most 2 seconds for owned jobs. On timeout, aborts remaining jobs
 * and returns LUME_WT_TIMEOUT. No page-pointer callback is possible afterward. */
int32_t lume_wt_owner_drop(uint64_t owner);

/* config is UTF-8 JSON, <=8192 bytes:
 * {url,origin,certificateHashes?:[[32 integer bytes],...],requireUnreliable?:bool}
 * origin MUST be inserted by trusted native document policy, never taken from
 * page options. Secure-context and connect-src policy belongs to the embedder;
 * this boundary independently rejects non-HTTPS, credentials, fragments,
 * malformed origins and invalid certificate hashes. No custom headers/cookies.
 * A successful return reserves a session; actual handshake completion is async.
 */
int32_t lume_wt_connect(uint64_t owner, const uint8_t *config, size_t length,
                        uint64_t *out_session);
/* Payload is copied before return, <=65536 bytes (a datagram also respects its
 * actual negotiated maximum). Every async command has a nonzero request_id,
 * except CLOSE. Stream/session IDs must belong to owner. CLOSE carries an UTF-8
 * local reason <=1024 bytes and a uint32 code. Read/accept operations are pull
 * credits: at most one pending operation per corresponding receive half.
 * CREATE_BI/CREATE_UNI code=0 checks actual QUIC credit immediately; code=1
 * waits for credit. Both wait for actual stream-preface I/O after allocation.
 * CANCEL_DATAGRAM_READ/CANCEL_ACCEPT_BI/CANCEL_ACCEPT_UNI permanently cancel
 * that receive route, reject its outstanding pull with AbortError, and finish
 * after its credit/newly accepted resources are released. Other routes and
 * sessions remain usable. All these operations require stream=0, empty payload.
 * Code must be zero except creation wait mode and stream cancel/abort codes. */
int32_t lume_wt_command(uint64_t owner, uint64_t session, uint64_t stream,
                        uint32_t operation, uint64_t request_id, uint32_t code,
                        const uint8_t *payload, size_t length);

/* Poll only from the owning JS thread. Each returned event is exclusively
 * owned by the caller, and MUST be freed once even if the page was destroyed.
 * JSON contains metadata only; read payloads are in the separate byte buffer.
 * Both buffers remain valid until event_free; they must not be retained. */
LumeWtEvent *lume_wt_poll(uint64_t owner);
const uint8_t *lume_wt_event_json(const LumeWtEvent *event, size_t *length);
const uint8_t *lume_wt_event_bytes(const LumeWtEvent *event, size_t *length);
void lume_wt_event_free(LumeWtEvent *event);
/* owner==0 requests aggregate resource counts; otherwise that owner's counts.
 * These are resource counts, never invented network or TLS statistics. */
int32_t lume_wt_counts(uint64_t owner, LumeWtCounts *out_counts);

#ifdef __cplusplus
}
#endif
#endif
