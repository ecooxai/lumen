/* Native WebRTC transport (libdatachannel: ICE, DTLS, SCTP, SRTP; Opus audio) behind a C interface.
   libdatachannel is a C++ library built against the platform C++ runtime, which on Android is not the
   one V8 and the engine use, so only C types cross this boundary (src/js/rtc.cc is the JS side).
   Handles: peer connections, data channels and tracks share one id space; 0 means none or failure.
   Network callbacks queue events per owner; the owner's thread takes them with rtcc_take. */
#ifndef LUMEN_RTC_CORE_H
#define LUMEN_RTC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int pc, h;
    const char *type;    /* cand gather ice state track dc open msg low close dcerror */
    const char *a;       /* text or message bytes */
    size_t alen;
    const char *b;
    bool bin;            /* msg: binary; dc: unordered */
    double n;            /* dc/open: stream id or -1 */
} RtccEvent;

/* new peer connection; urls/users/creds hold n ICE servers (users/creds entries may be NULL) */
int rtcc_new(void *owner, const char *const *urls, const char *const *users, const char *const *creds, int n,
             bool relay_only, const char *cert_pem, const char *key_pem);
/* a local section for the native offer; ssrc/cname/msid/track id describe what we send */
int rtcc_add_track(int pc, const char *mid, const char *kind, const char *dir, uint32_t ssrc, const char *cname,
                   const char *msid, const char *track_id);
/* RTP packetizing, sender reports, NACK and receiver reports for an audio track */
void rtcc_track_setup(int track, uint32_t ssrc, const char *cname);
/* these return NULL or an error message valid until the next call */
const char *rtcc_local(int pc, const char *type, const char *ufrag, const char *pwd);
const char *rtcc_remote(int pc, const char *sdp, const char *type);
void rtcc_cand(int pc, const char *cand, const char *mid);
/* max_retransmits / max_lifetime_ms / id: -1 for none */
int rtcc_dc(int pc, const char *label, const char *protocol, bool unordered, int max_retransmits, int max_lifetime_ms,
            bool negotiated, int id);
/* returns the buffered amount, or -1 if the channel is gone */
double rtcc_dc_send(int dc, const void *data, size_t len, bool binary);
/* low_threshold -1 leaves it; returns false if the channel is gone */
bool rtcc_dc_info(int dc, double low_threshold, double *buffered, int *stream);
void rtcc_dc_close(int dc);
/* mono 48 kHz PCM, sent as 20 ms Opus frames; a disabled track sends silence */
void rtcc_send_pcm(int track, const float *pcm, size_t n, bool enabled);
/* play received audio on the default output */
void rtcc_play(int track, bool on, float volume);
/* tap off: stop keeping PCM. Tap on: keep it and copy up to cap samples into out (returns the count, -1 if the track is gone) */
long rtcc_read(int track, bool tap, float *out, size_t cap);
/* local/remote: selected candidate pair, NUL-terminated, empty if none */
bool rtcc_stats(int pc, double *sent, double *received, double *rtt_ms, char *local, size_t local_size, char *remote,
                size_t remote_size);
void rtcc_close(int pc);
void rtcc_owner_free(void *owner);
/* events waiting for owner */
size_t rtcc_pending(void *owner);
/* the next event of a live connection; its strings stay valid until the next call */
bool rtcc_take(void *owner, RtccEvent *e);

#ifdef __cplusplus
}
#endif

#endif
