/* The network side of RTCPeerConnection (N.rtc*, src/js/rtc.cc). 07r-rtc.js keeps the spec state machine
   and writes the SDP; when the remote description comes from outside this page, a native connection takes
   the same ICE credentials, certificate and mids and carries the data channels and Opus audio. */
const rtcNat = new Map();   /* native pc id -> RTCPeerConnection */
let rtcNatInit = false;
const rtcCand = s => String(s).replace(/^a=/, '');
/* received audio for Web Audio (MediaStreamAudioSourceNode subscribes to .subs like a mic hub) */
class RtcHub {
    constructor(h) { this.h = h; this.refs = 1; this.subs = new Set(); this.tapping = false; this.timer = setInterval(() => this.poll(), 40); }
    poll() {
        if (!this.h) return;
        if (!this.subs.size) { if (this.tapping) { this.tapping = false; N.rtcRead(this.h, false); } return; }
        this.tapping = true;
        const pcm = N.rtcRead(this.h);
        if (pcm) for (const f of [...this.subs]) try { f(pcm); } catch (e) { setTimeout(() => { throw e; }); }
    }
    release() { if (--this.refs > 0) return; clearInterval(this.timer); if (this.tapping && this.h) N.rtcRead(this.h, false); this.subs.clear(); }
}
/* <audio>/<video> elements playing a MediaStream: the native side plays its remote audio tracks */
const rtcPlayers = new Map();   /* MediaStreamTrack -> Set of elements */
function rtcPlayUpdate(t) {
    const els = [...(rtcPlayers.get(t) || [])];
    if (!els.length) rtcPlayers.delete(t);
    if (t._.rh) N.rtcPlay(t._.rh, els.length > 0 && t._.readyState === 'live', els.reduce((v, e) => Math.max(v, e.muted ? 0 : e.volume), 0));
}
function mediaStreamPlay(el, on) {
    const s = el.__so;
    if (!(s instanceof MediaStream)) return;
    for (const t of s.getAudioTracks()) {
        let set = rtcPlayers.get(t);
        if (!set) rtcPlayers.set(t, set = new Set());
        if (on) set.add(el); else set.delete(el);
        rtcPlayUpdate(t);
    }
}
function rtcNatEvent(id, type, h, a, b, n) {
    const pc = rtcNat.get(id);
    if (!pc || pc._.closed) return;
    const me = pc._, nat = me.nat;
    switch (type) {
    case 'cand': {
        if (me.iceGatheringState === 'complete') return;
        const parsed = rtcParseSdp((me.pendLocal || me.curLocal || { sdp: '' }).sdp);
        const idx = parsed ? Math.max(0, parsed.media.findIndex(m => m.mid === b)) : 0;
        const c = new RTCIceCandidate({ candidate: rtcCand(a), sdpMid: b || null, sdpMLineIndex: idx, usernameFragment: me.ufrag });
        me.localCands.push(c); me.ice._.local.push(c);
        rtcFire(pc, new RTCPeerConnectionIceEvent('icecandidate', { candidate: c }));
        return;
    }
    case 'gather':
        if (me.iceGatheringState === 'complete' || a === 'new' || a === me.iceGatheringState) return;
        me.iceGatheringState = a; me.ice._set('gatheringState', a, 'gatheringstatechange'); rtcEv(pc, 'icegatheringstatechange');
        if (a === 'complete') rtcFire(pc, new RTCPeerConnectionIceEvent('icecandidate', { candidate: null }));
        return;
    case 'ice':
        if (a === 'connected' || a === 'completed') {
            const st = N.rtcStats(id);
            if (st && st[3] && st[4]) {
                const mid = (me.localCands[0] && me.localCands[0].sdpMid) || '0';
                me.ice._.pair = { local: new RTCIceCandidate({ candidate: rtcCand(st[3]), sdpMid: mid }), remote: new RTCIceCandidate({ candidate: rtcCand(st[4]), sdpMid: mid }) };
                rtcEv(me.ice, 'selectedcandidatepairchange');
            }
            me.ice._.lp = { usernameFragment: me.ufrag, password: me.pwd };
        }
        if (a !== 'new') pc._iceState(a);
        return;
    case 'state':
        if (a === 'connecting') { me.dtls._set('connecting'); pc._connState('connecting'); }
        else if (a === 'connected') {
            me.connected = true;
            me.dtls._set('connected');
            if (me.sctp) { me.sctp._.maxChannels = 65535; me.sctp._set('connected'); }
            for (const t of me.transceivers) if (t._.nh && rtcHasRecv(t._.current || t._.direction)) t._.receiver._.track._mute(false);
            pc._connState('connected');
        } else if (a === 'disconnected' || a === 'failed') pc._connState(a);
        else if (a === 'closed') { me.dtls._set('closed'); if (me.sctp) me.sctp._set('closed'); }
        return;
    case 'track': {   /* a remote offer's section (we answer) */
        const t = me.transceivers.find(x => x._.mid === a);
        if (t && !t._.nh) pc._natTrack(t, h);
        return;
    }
    case 'dc': {
        const dc = new RTCDataChannel(RTC_INTERNAL, pc, { label: a, ordered: !b[1], maxPacketLifeTime: null, maxRetransmits: null, protocol: b[0], negotiated: false, id: n >= 0 ? n : null, priority: 'low' });
        dc._.nh = h; nat.dcs.set(h, dc);
        me.channels.push(dc);
        rtcFire(pc, new RTCDataChannelEvent('datachannel', { channel: dc }));
        return;
    }
    }
    const dc = nat.dcs.get(h);
    if (!dc) return;
    if (type === 'open') { if (n >= 0) dc._.id = n; dc._open(); }
    else if (type === 'msg') {
        if (dc._.readyState !== 'open') return;
        dc._.recv++; dc._.br += typeof a === 'string' ? rtcU8(a) : a.byteLength;
        rtcFire(dc, new MessageEvent('message', { data: typeof a !== 'string' && dc._.binaryType === 'blob' ? new Blob([a]) : a }));
    } else if (type === 'low') {
        const info = N.rtcDcInfo(h, -1);
        dc._.bufferedAmount = info ? info[0] : 0;
        if (dc._.readyState === 'open') rtcEv(dc, 'bufferedamountlow');
    } else if (type === 'close') {
        nat.dcs.delete(h);
        if (dc._.readyState === 'open') { dc._.readyState = 'closing'; rtcEv(dc, 'closing'); }
        dc._.bufferedAmount = 0;
        dc._closed();
    } else if (type === 'dcerror') dc._.err = a;
}
Object.assign(RTCPeerConnection.prototype, {
    _natWanted() {
        const me = this._;
        if (me.nat || typeof N.rtcNew !== 'function' || !me.cert._.crt) return false;
        return me.cfg.lumenNative || !rtcPeers.get(me.remoteUfrag);
    },
    _natStart() {
        const me = this._;
        if (!rtcNatInit) { rtcNatInit = true; N.rtcInit(rtcNatEvent); }
        const servers = [];
        for (const s of me.cfg.iceServers) for (const u of s.urls) servers.push([u, s.username ?? null, s.credential ?? null]);
        const id = N.rtcNew(servers, me.cfg.iceTransportPolicy === 'relay', me.cert._.crt, me.cert._.key);
        if (!id) return false;
        me.nat = { id, dcs: new Map() };
        rtcNat.set(id, this);
        for (const dc of me.channels) if (dc._.readyState === 'connecting' && (dc._.negotiated || me.pendLocal || me.curLocal)) this._natDc(dc);
        return true;
    },
    _natStop(why) {
        const me = this._;
        if (!me.nat) return;
        if (why) console.warn('RTCPeerConnection: native transport unavailable: ' + why);
        N.rtcClose(me.nat.id); rtcNat.delete(me.nat.id);
        for (const t of me.transceivers) {
            if (t._.natSub) { t._.natSub.hub.subs.delete(t._.natSub.fn); t._.natSub = null; }
            const r = t._.receiver._.track;
            if (r._.hub instanceof RtcHub) { r._.hub.h = 0; if (r._.readyState === 'ended') r._.hub.release(); }
            r._.rh = 0; rtcPlayers.delete(r);
            t._.nh = 0;
        }
        me.nat = null;
    },
    _natDc(dc) {
        const me = this._;
        if (!me.nat || dc._.nh) return;
        const h = N.rtcDc(me.nat.id, dc._.label, dc._.protocol, !dc._.ordered, dc._.maxRetransmits ?? -1, dc._.maxPacketLifeTime ?? -1, dc._.negotiated, dc._.negotiated ? dc._.id : -1);
        if (!h) return;
        dc._.nh = h; me.nat.dcs.set(h, dc);
    },
    _natTrack(t, h) {
        const me = this._;
        t._.nh = h;
        if (t._.kind !== 'audio') return;
        N.rtcTrackSetup(h, t._.sender._.ssrc, me.cname);
        const r = t._.receiver._.track;
        r._.rh = h;
        if (!r._.hub) r._.hub = new RtcHub(h); else if (r._.hub instanceof RtcHub) r._.hub.h = h;
        rtcPlayUpdate(r);
        this._natSender(t);
    },
    _natSender(t) {
        if (t._.natSub) { t._.natSub.hub.subs.delete(t._.natSub.fn); t._.natSub = null; }
        const track = t._.sender._.track, h = t._.nh;
        if (!h || !track || !track._.hub || !track._.hub.subs) return;
        const fn = pcm => { if (t._.nh === h) N.rtcSendPCM(h, pcm, track.enabled && track._.readyState === 'live'); };
        track._.hub.subs.add(fn);
        t._.natSub = { hub: track._.hub, fn };
    },
    /* the remote description arrived: an offer goes to the native side as is; for an answer the native side first makes its own offer with our sections */
    _natRemote(type, sdp) {
        const me = this._;
        if (!this._natStart()) return;
        const id = me.nat.id;
        let err = '';
        if (type === 'offer') err = N.rtcRemote(id, sdp, 'offer');
        else {
            const local = rtcParseSdp((me.pendLocal || me.curLocal).sdp);
            for (const m of local.media) {
                if (m.kind === 'application' || m.port === 0) continue;
                const t = me.transceivers.find(x => x._.mid === m.mid);
                const snd = t && t._.sender, msid = m.msids[0] || ['-', ''];
                const h = N.rtcAddTrack(id, m.mid, m.kind, m.dir, snd ? snd._.ssrc : 0, me.cname, msid[0], msid[1] || '');
                if (h && t) this._natTrack(t, h);
            }
            if (local.media.some(m => m.kind === 'application' && m.port !== 0) && !me.nat.dcs.size) N.rtcDc(id, '', '', false, -1, -1, true, 65534);   /* keeps the data section in the native offer */
            err = N.rtcLocal(id, 'offer', me.ufrag, me.pwd) || N.rtcRemote(id, sdp, type);
        }
        if (err) { this._natStop(err); return; }
        /* the native side routes RTP by the SSRCs the remote description declares (no content is logged) */
        const remote = rtcParseSdp(sdp);
        if (remote && remote.media.some(m => m.kind === 'audio' && m.port !== 0 && rtcHasSend(m.dir) && !m.ssrcs.length)) console.warn('RTCPeerConnection: a remote audio section declares no SSRC');
    },
    _natLocalAnswer() {
        const me = this._;
        if (!me.nat) return;
        const err = N.rtcLocal(me.nat.id, 'answer', me.ufrag, me.pwd);
        if (err) { this._natStop(err); return; }
        for (const t of me.transceivers) if (t._.nh) this._natSender(t);
    },
});
Object.assign(RTCDataChannel.prototype, {
    _natSend(data, bin, str, size) {
        const h = this._.nh;
        const go = payload => { if (this._.nh === h && this._.readyState === 'open') this._.bufferedAmount = N.rtcDcSend(h, payload, bin) || 0; else this._.bufferedAmount = 0; if (this._.bufferedAmount > this._.threshold) N.rtcDcInfo(h, this._.threshold); };
        this._.sent++; this._.bs += size;
        if (data instanceof Blob) { this._.bufferedAmount += size; this._.q = this._.q.then(() => data.arrayBuffer()).then(go); }
        else go(bin ? data : str);
    },
});
