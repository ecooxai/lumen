# Native RTC data-channel facade contract

The facade and its semantic fixtures are installed. The native owner confirmed the v0.24.6 header and froze the primitive/event contract on 2026-10-21. Engine integration and independent-process network acceptance are separate gates. It is a data channel implementation over a real
libdatachannel backend, not a continuation of the in-process JavaScript transport.
RTP media, certificate import, ICE restart and SDP munging remain explicit
unsupported operations until separately implemented and tested.

## Native boundary

The pinned v0.24.6 header supports staged offer/answer generation. The agreed interface is:

- `rtcNativeAvailable(): boolean`
- `rtcCreate(configurationJSON, callback): peerId`
- `rtcGenerateDescription(peerId, type, requestId): void`
- `rtcSetLocalDescription(peerId, type, sdp, requestId): void`
- `rtcSetRemoteDescription(peerId, type, sdp, requestId): void`
- `rtcGetLocalDescription(peerId): {type, sdp} | null`
- `rtcAddCandidate(peerId, candidate, mid, requestId): void`
- `rtcCreateChannel(peerId, optionsJSON): channelId`
- `rtcChannelInfo(channelId): metadata`
- `rtcSend(channelId, string | ArrayBuffer): void`
- `rtcSetBufferedAmountLowThreshold(channelId, amount): void`
- `rtcCloseChannel(channelId): void`
- `rtcClosePeer(peerId): void`
- `rtcStats(peerId): {connectionState, iceConnectionState, iceGatheringState, channels, selectedCandidatePair}`; channel payload counters are measured, while unprovided transport counters remain absent

Callbacks carry owned copies of strings and bytes and run on the owning V8
thread. Navigation first invalidates document/peer generation, then deletes
native resources; no callbacks enter a dead isolate. The facade independently
ignores callbacks after `close()` and rejects its pending promises.

## Required behavior

1. `createOffer()` and `createAnswer()` generate descriptions without starting
   gathering or changing signaling state. `setLocalDescription()` is a separate
   operation. Omitted local description creates the appropriate offer or answer.
2. Supplied SDP must match an actually generated local description. Any changes
   the backend cannot apply are rejected, never replaced silently.
3. `localDescription.sdp` includes actual gathered candidates after gathering
   completes. The final candidate event is null. No synthetic ICE/DTLS success.
4. Native asynchronous operations are serialized by the facade. Synchronous
   native errors become promise rejections; close rejects queued/pending work.
5. Data channel options are validated before native allocation. Binary payloads
   are copied so later caller mutation does not alter queued bytes. Blob sends
   are explicitly unsupported in this first data-only backend. Detached buffers are rejected.
6. Incoming channel events are delivered before that channel's open/message
   events, and no event leaks after navigation or close.
7. Every byte counter has a known source. Application data channel bytes are not
   reported as UDP/ICE/DTLS transport bytes. Unknown transport counters are absent.
8. Unsupported RTP methods fail explicitly. Existing local MediaStream and
   MediaStreamTrack capture types remain independent of the network facade.
9. Ordinary lifecycle is event driven; no background polling interval is added.

## Evidence boundary

Mock binding fixture tests verify API/lifecycle behavior only. They are not
network proof. Acceptance additionally requires the real engine connecting to
an independent Chrome peer and echoing a fresh nonce in both directions, plus
native teardown and resource measurements. WAN STUN/TURN, camera/audio RTP,
multiple negotiation cycles and browser conformance remain separate coverage.

References: https://www.w3.org/TR/webrtc/ and the pinned upstream libdatachannel
v0.24.6 C API/header when available from the native dependency fetch.

The callback envelope includes `description`, `operation`, request-correlated `error`, native `state`, `candidate`, `datachannel`, `channel-open/message/close/error`, and `channel-bufferedamountlow`. These callbacks carry `requestId` or `channelId` as appropriate. The code uses private class methods so a page cannot invoke an arbitrary N primitive through a public dispatcher.

A native `error` with `fatal:true` is delivered only after native has closed the peer handle. The facade rejects pending operations with a generic OperationError, rejects queued and new work with InvalidStateError, cancels dispatch tasks, closes channels, and enters terminal closed signaling/ICE/connection states. It dispatches channel error/close and peer state changes, ignores all late callbacks, and never calls the dead native handle again. Ordinary explicit close remains idempotent and calls native close exactly once.

The first pinned C API cannot distinguish a zero packet lifetime from retransmit-zero in its reliability representation. `maxPacketLifeTime:0` is rejected with NotSupportedError; `maxRetransmits:0` remains valid. Text sends containing NUL are rejected because the C API's text sender uses a null-terminated input. Received text retains the native callback's explicit byte length. No unsupported option is silently rewritten.

The C reliability getter can also leave incoming unreliable channels ambiguous when both numeric limits are zero. The native adapter reports `reliabilityKnown:false` and null numeric limits in that case rather than inventing zero retransmissions; locally created channels retain their exact requested settings. Positive incoming limits are reported when measurable.

## Native resource bounds

The adapter limits each document to 32 peers and each peer to 128 channels. Each application message is limited to 1 MiB. Its pending outgoing application payload budget is 8 MiB aggregated across all channels owned by the document, using the library's actual buffered amounts. A separate copied callback-event queue is limited to 8 MiB and 1,024 events per document; saturation closes the affected peer through the fatal lifecycle contract. Final local channel insertion rechecks capacity under the same lock used by incoming-channel allocation. These are application/event budgets, not a promise that total process RSS equals those numbers: protocol state, native/library allocations, certificates, threads and the browser itself require separate measured resource evidence.
