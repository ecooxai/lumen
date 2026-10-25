/* Web Audio: the graph renders in JS in 128-frame quanta; N.wa* pushes stereo float32 at 48 kHz to SDL. */
if (N.waOpen) (() => {
const RATE = 48000, Q = 128, LAT = RATE * 0.08;
const silence = new Float32Array(Q);

/* AudioParam argument checks: non-finite floats are TypeErrors (WebIDL), negative times RangeErrors */
const apNum = (v, m) => { v = +v; if (!Number.isFinite(v)) throw new TypeError(`Failed to execute '${m}' on 'AudioParam': The provided float value is non-finite.`); return v; };
const apTime = (t, m) => { t = apNum(t, m); if (t < 0) throw new RangeError(`Failed to execute '${m}' on 'AudioParam': The time provided (${t}) is less than the minimum bound (0).`); return t; };
class AudioParam {
    constructor(ctx, v, min = -3.4e38, max = 3.4e38) { this._c = ctx; this._v = v; this._dv = v; this._min = min; this._max = max; this._ev = []; this._t0 = 0; }
    get value() { return this._v; }
    get defaultValue() { return this._dv; } get minValue() { return this._min; } get maxValue() { return this._max; }
    get automationRate() { return this._rate || 'a-rate'; } set automationRate(v) { if (v === 'a-rate' || v === 'k-rate') this._rate = v; }
    set value(x) { x = +x; if (x === x) { this._v = x; this._ev = []; } }
    _add(e) {
        const ev = this._ev;
        if (!ev.length) this._t0 = this._c.currentTime;
        else if (ev.length > 64) this._at(this._c.currentTime);   /* a param nothing renders: settle what is past */
        let i = ev.length;
        while (i && ev[i - 1].t > e.t) i--;   /* after events at the same time, usually at the end */
        if (i === ev.length) ev.push(e); else ev.splice(i, 0, e);
        return this;
    }
    setValueAtTime(v, t) { v = apNum(v, 'setValueAtTime'); t = apTime(t, 'setValueAtTime'); return this._add({ k: 0, v, t }); }
    linearRampToValueAtTime(v, t) { v = apNum(v, 'linearRampToValueAtTime'); t = apTime(t, 'linearRampToValueAtTime'); return this._add({ k: 1, v, t }); }
    exponentialRampToValueAtTime(v, t) {
        v = apNum(v, 'exponentialRampToValueAtTime'); t = apTime(t, 'exponentialRampToValueAtTime');
        if (v === 0) throw new RangeError(`Failed to execute 'exponentialRampToValueAtTime' on 'AudioParam': The float target value provided (0) should not be in the range (-1.40130e-45, 1.40130e-45).`);
        return this._add({ k: 2, v, t });
    }
    setTargetAtTime(v, t, c) {
        v = apNum(v, 'setTargetAtTime'); t = apTime(t, 'setTargetAtTime'); c = apNum(c, 'setTargetAtTime');
        if (c < 0) throw new RangeError(`Failed to execute 'setTargetAtTime' on 'AudioParam': Time constant must be a non-negative number: ${c}`);
        return this._add({ k: 3, v, t, c: Math.max(c, 1e-6) });
    }
    setValueCurveAtTime(curve, t, d) {
        t = apTime(t, 'setValueCurveAtTime'); d = apNum(d, 'setValueCurveAtTime');
        if (!(d > 0)) throw new RangeError(`Failed to execute 'setValueCurveAtTime' on 'AudioParam': The duration provided (${d}) is less than or equal to the minimum bound (0).`);
        const c = Array.from(curve, x => apNum(x, 'setValueCurveAtTime')), n = c.length;
        if (n < 2) throw new DOMException(`Failed to execute 'setValueCurveAtTime' on 'AudioParam': The curve length provided (${n}) is less than the minimum bound (2).`, 'InvalidStateError');
        for (let i = 0; i < n; i++) this._add({ k: i ? 1 : 0, v: c[i], t: t + d * i / (n - 1) });
        return this;
    }
    cancelScheduledValues(t) { t = apTime(t, 'cancelScheduledValues'); this._ev = this._ev.filter(e => e.t < t); return this; }
    cancelAndHoldAtTime(t) { t = apTime(t, 'cancelAndHoldAtTime'); this._v = this._at(t); this._ev = []; return this; }
    _at(t) {
        const ev = this._ev;
        while (ev.length) {
            const e = ev[0];
            if (e.k === 0) { if (e.t > t) break; this._v = e.v; this._t0 = e.t; ev.shift(); continue; }
            if (e.k === 3) {
                if (e.t > t) break;
                const nx = ev[1], end = nx && nx.t <= t ? nx.t : t, v = e.v + (this._v - e.v) * Math.exp(-(end - e.t) / e.c);
                if (end === t) return v;
                this._v = v; this._t0 = end; ev.shift(); continue;
            }
            if (e.t <= t) { this._v = e.v; this._t0 = e.t; ev.shift(); continue; }
            const f = Math.max(0, (t - this._t0) / (e.t - this._t0));
            return e.k === 1 || this._v * e.v <= 0 ? this._v + (e.v - this._v) * f : this._v * Math.pow(e.v / this._v, f);
        }
        return this._v;
    }
}

class AudioNode extends EventTarget {
    constructor(ctx, ins = 1, outs = 1) {
        if (!(ctx instanceof BaseAudioContext)) throw new TypeError(`Failed to construct '${new.target.name}': parameter 1 is not of type 'BaseAudioContext'.`);
        super(); this._ctx = ctx; this._nin = ins; this._nout = outs; this._cc = 2; this._ccm = 'max'; this._ci = 'speakers'; this._in = new SSet(); this._out = new SSet(); this._q = -1; this._o = null; }
    get context() { return this._ctx; } get numberOfInputs() { return this._nin; } get numberOfOutputs() { return this._nout; }
    get channelCount() { return this._cc; }
    set channelCount(v) { v = +v; if (!(v >= 1 && v <= 32)) throw new DOMException(`Failed to set the 'channelCount' property on 'AudioNode': The channel count provided (${v}) is outside the range [1, 32].`, 'NotSupportedError'); this._cc = v; }
    get channelCountMode() { return this._ccm; } set channelCountMode(v) { v = String(v); if (['max', 'clamped-max', 'explicit'].includes(v)) this._ccm = v; }
    get channelInterpretation() { return this._ci; } set channelInterpretation(v) { v = String(v); if (['speakers', 'discrete'].includes(v)) this._ci = v; }
    connect(d) {
        if (d instanceof AudioParam) { (d._in || (d._in = new SSet())).add(this); this._out.add(d); return; }
        if (!(d instanceof AudioNode)) throw new TypeError('connect: not an AudioNode');
        d._in.add(this); this._out.add(d); return d;
    }
    disconnect(d) {
        for (const o of [...this._out]) if (d === undefined || o === d) { o._in.delete(this); this._out.delete(o); }
    }
    /* the node's own [L, R] for quantum f: two pairs taken in turn, so last quantum's output stays intact
       while a cycle reads it; consumers use an output within its quantum and copy what they keep */
    _buf(f) {
        const b = this._pp || (this._pp = [[new Float32Array(Q), new Float32Array(Q)], [new Float32Array(Q), new Float32Array(Q)]]);
        return b[(f / Q) & 1];
    }
    _mix(f) {   /* sum of inputs as [L, R] */
        let l = null, r = null;
        for (const s of this._in) {
            const o = s._pull(f);
            if (!o) continue;
            if (!l) { [l, r] = this._buf(f); l.fill(0); r.fill(0); }
            const a = o[0], b = o[1];
            for (let i = 0; i < Q; i++) { l[i] += a[i]; r[i] += b[i]; }
        }
        return l ? this._buf(f) : null;
    }
    _pull(f) { if (this._q !== f) { this._q = f; this._o = this._render(f); } return this._o; }
    _render(f) { return this._mix(f); }
}
const paramIn = (p, f) => {   /* audio-rate modulation summed into a param, averaged per quantum */
    let v = p._at(f / p._c.sampleRate);
    if (p._in) for (const s of p._in) { const o = s._pull(f); if (o) { let t = 0; for (let i = 0; i < Q; i++) t += o[0][i]; v += t / Q; } }
    return v;
};

class AudioDestinationNode extends AudioNode {
    constructor(ctx) { super(ctx, 1, 0); this.maxChannelCount = 2; }
}
class GainNode extends AudioNode {
    constructor(ctx, o = {}) { super(ctx); this.gain = new AudioParam(ctx, o.gain ?? 1); }
    _render(f) {
        const m = this._mix(f); if (!m) return null;
        const g = paramIn(this.gain, f), g1 = paramIn(this.gain, f + Q - 1);
        const [l, r] = m;
        for (let i = 0; i < Q; i++) { const k = g + (g1 - g) * i / Q; l[i] *= k; r[i] *= k; }
        return m;
    }
}
class StereoPannerNode extends AudioNode {
    constructor(ctx, o = {}) { super(ctx); this.pan = new AudioParam(ctx, o.pan ?? 0, -1, 1); }
    _render(f) {
        const m = this._mix(f); if (!m) return null;
        const p = Math.max(-1, Math.min(1, paramIn(this.pan, f))), x = (p + 1) * Math.PI / 4, gl = Math.cos(x), gr = Math.sin(x);
        const [l, r] = m;
        for (let i = 0; i < Q; i++) { const s = (l[i] + r[i]) / 2; l[i] = s * gl * Math.SQRT2; r[i] = s * gr * Math.SQRT2; }
        return m;
    }
}
class DelayNode extends AudioNode {
    constructor(ctx, o = {}) { super(ctx); this._max = o.maxDelayTime ?? 1; this.delayTime = new AudioParam(ctx, o.delayTime ?? 0, 0, this._max); this._b = [new Float32Array(Math.ceil(this._max * ctx.sampleRate) + Q + 1), null]; this._b[1] = new Float32Array(this._b[0].length); this._w = 0; }
    _render(f) {
        const m = this._mix(f) || [silence, silence], n = this._b[0].length, d = Math.min(Math.round(paramIn(this.delayTime, f) * this.context.sampleRate), n - Q - 1);
        const o = this._dbuf || (this._dbuf = [[new Float32Array(Q), new Float32Array(Q)], [new Float32Array(Q), new Float32Array(Q)]]), [l, r] = o[(f / Q) & 1];
        for (let i = 0; i < Q; i++) {
            const w = (this._w + i) % n; this._b[0][w] = m[0][i]; this._b[1][w] = m[1][i];
            const rd = (w - d + n) % n; l[i] = this._b[0][rd]; r[i] = this._b[1][rd];
        }
        this._w = (this._w + Q) % n;
        return [l, r];
    }
}
/* Filters, compressors and convolvers pass audio through unchanged for now. */
class BiquadFilterNode extends AudioNode {
    constructor(ctx, o = {}) { super(ctx); this.type = o.type || 'lowpass'; this.frequency = new AudioParam(ctx, o.frequency ?? 350); this.detune = new AudioParam(ctx, 0); this.Q = new AudioParam(ctx, o.Q ?? 1); this.gain = new AudioParam(ctx, o.gain ?? 0); }
    getFrequencyResponse(fr, mag, ph) { mag.fill(1); ph.fill(0); }
}
class DynamicsCompressorNode extends AudioNode {
    constructor(ctx) { super(ctx); this.threshold = new AudioParam(ctx, -24); this.knee = new AudioParam(ctx, 30); this.ratio = new AudioParam(ctx, 12); this.attack = new AudioParam(ctx, 0.003); this.release = new AudioParam(ctx, 0.25); this.reduction = 0; }
}
class ConvolverNode extends AudioNode { constructor(ctx) { super(ctx); this.buffer = null; this.normalize = true; } }
/* in-place radix-2 FFT; n is a power of two */
const fft = (re, im) => {
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++) {
        let b = n >> 1;
        for (; j & b; b >>= 1) j ^= b;
        j ^= b;
        if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (let len = 2; len <= n; len <<= 1) {
        const h = len >> 1, a = -2 * Math.PI / len, wr = Math.cos(a), wi = Math.sin(a);
        for (let i = 0; i < n; i += len) {
            let cr = 1, ci = 0;
            for (let k = 0; k < h; k++) {
                const u = i + k, v = u + h, xr = re[v] * cr - im[v] * ci, xi = re[v] * ci + im[v] * cr;
                re[v] = re[u] - xr; im[v] = im[u] - xi; re[u] += xr; im[u] += xi;
                const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
};
const blackmanWin = new SMap();   /* fftSize -> window, shared by every analyser */
const blackman = n => {
    let w = blackmanWin.get(n);
    if (!w) { w = new Float32Array(n); for (let i = 0; i < n; i++) { const x = 2 * Math.PI * i / n; w[i] = 0.42 - 0.5 * Math.cos(x) + 0.08 * Math.cos(2 * x); } blackmanWin.set(n, w); }
    return w;
};
class AnalyserNode extends AudioNode {
    constructor(ctx, o = {}) { super(ctx); this._fft = 2048; this.fftSize = o.fftSize || 2048; this.minDecibels = -100; this.maxDecibels = -30; this.smoothingTimeConstant = 0.8; this._last = new Float32Array(32768); this._ver = 0; this._specVer = -1; this._sm = null; }
    get fftSize() { return this._fft; }
    set fftSize(v) {
        v = v >>> 0;
        if (v < 32 || v > 32768 || (v & (v - 1))) throw new DOMException(`Failed to set the 'fftSize' property on 'AnalyserNode': The value provided (${v}) is not a power of two, or is outside the range [32, 32768].`, 'IndexSizeError');
        if (v !== this._fft) { this._fft = v; this._sm = null; this._specVer = -1; }
    }
    get frequencyBinCount() { return this._fft / 2; }
    _feed(p) {   /* microphone PCM straight from a MediaStreamAudioSourceNode */
        const L = this._last, n = Math.min(p.length, L.length);
        L.copyWithin(0, n); L.set(n < p.length ? p.subarray(p.length - n) : p, L.length - n); this._fed = true; this._ver++;
    }
    _render(f) {
        const m = this._mix(f), L = this._last;
        if (this._fed) return m;
        L.copyWithin(0, Q); this._ver++;
        if (m) for (let i = 0; i < Q; i++) L[L.length - Q + i] = (m[0][i] + m[1][i]) / 2; else L.fill(0, L.length - Q);
        return m;
    }
    _spectrum() {   /* Blackman window, FFT and smoothing over time, once per new block of samples */
        const n = this._fft, h = n >> 1;
        if (this._sm && this._specVer === this._ver) return this._sm;
        if (!this._re || this._re.length !== n) { this._re = new Float32Array(n); this._im = new Float32Array(n); }
        const re = this._re, im = this._im, w = blackman(n), s = this._last.length - n;
        for (let i = 0; i < n; i++) re[i] = this._last[s + i] * w[i];
        im.fill(0);
        fft(re, im);
        const sm = this._sm || (this._sm = new Float32Array(h)), k = Math.max(0, Math.min(1, +this.smoothingTimeConstant || 0));
        for (let i = 0; i < h; i++) { const v = Math.sqrt(re[i] * re[i] + im[i] * im[i]) / n; sm[i] = k * sm[i] + (1 - k) * (v === v && v !== Infinity ? v : 0); }
        this._specVer = this._ver;
        return sm;
    }
    getFloatTimeDomainData(a) { const n = Math.min(a.length, this._fft); a.set(this._last.subarray(this._last.length - n)); }
    getByteTimeDomainData(a) { const n = Math.min(a.length, this._fft), s = this._last.length - n; for (let i = 0; i < n; i++) a[i] = Math.max(0, Math.min(255, 128 + this._last[s + i] * 128)); }
    getFloatFrequencyData(a) { const sm = this._spectrum(), n = Math.min(a.length, sm.length); for (let i = 0; i < n; i++) a[i] = 20 * Math.log10(sm[i]); }
    getByteFrequencyData(a) {
        const sm = this._spectrum(), n = Math.min(a.length, sm.length), lo = this.minDecibels, span = this.maxDecibels - lo;
        for (let i = 0; i < n; i++) a[i] = Math.max(0, Math.min(255, 255 * (20 * Math.log10(sm[i]) - lo) / span));
    }
}

/* PCM waiting to be rendered, copied into a ring; the oldest samples are dropped past max */
class PcmQueue {
    constructor(max) { this.b = new Float32Array(Math.ceil(max)); this.r = 0; this.size = 0; this.out = null; }
    push(p) {
        const b = this.b, cap = b.length;
        if (p.length >= cap) { b.set(p.subarray(p.length - cap)); this.r = 0; this.size = cap; return; }
        const drop = Math.max(0, this.size + p.length - cap);
        this.r = (this.r + drop) % cap; this.size -= drop;
        const w = (this.r + this.size) % cap, k = Math.min(p.length, cap - w);
        b.set(k < p.length ? p.subarray(0, k) : p, w);
        if (k < p.length) b.set(p.subarray(k), 0);
        this.size += p.length;
    }
    take(n) {   /* n samples, zero padded, in a buffer reused by the next take; null if empty */
        if (!this.size) return null;
        const out = this.out && this.out.length === n ? this.out : (this.out = new Float32Array(n)), b = this.b, cap = b.length;
        const m = Math.min(n, this.size), k = Math.min(m, cap - this.r);
        out.set(b.subarray(this.r, this.r + k));
        if (k < m) out.set(b.subarray(0, m - k), k);
        if (m < n) out.fill(0, m);
        this.r = (this.r + m) % cap; this.size -= m;
        return out;
    }
}

/* a microphone track feeds the analysers, script processors, worklets and stream destinations downstream of it
   directly (level meters, dictation); it is not mixed into the output. Remote WebRTC audio is fed the same way
   and also rendered through the graph. */
const feedMic = (n, pcm, depth) => {
    if (n._feed) n._feed(pcm);
    if (depth > 8 || !n._out || n instanceof ScriptProcessorNode || n instanceof AudioWorkletNode) return;
    let p = pcm;
    if (n instanceof GainNode && n.gain.value !== 1) { const g = n.gain.value; p = new Float32Array(pcm.length); for (let i = 0; i < p.length; i++) p[i] = pcm[i] * g; }
    for (const o of n._out) if (o instanceof AudioNode) feedMic(o, p, depth + 1);
};
class MediaStreamAudioSourceNode extends AudioNode {
    constructor(ctx, o = {}) {
        super(ctx, 0, 1);
        const s = o.mediaStream;
        if (!(s instanceof MediaStream)) throw new TypeError("Failed to construct 'MediaStreamAudioSourceNode': required member mediaStream is undefined.");
        const t = s.getAudioTracks()[0];
        if (!t) throw new DOMException("Failed to construct 'MediaStreamAudioSourceNode': MediaStream has no audio track", 'InvalidStateError');
        this.mediaStream = s; this._remote = t._.hub instanceof RtcHub; this._pq = new PcmQueue(RATE * 0.3); this._primed = false;
        this._sub = pcm => {
            for (const n of this._out) if (n instanceof AudioNode) feedMic(n, pcm, 0);
            if (this._remote && this._out.size) { this._pq.push(pcm); ctx._activate(this); }
        };
        if (t._.hub) t._.hub.subs.add(this._sub);
    }
    _render() {
        const q = this._pq;
        if (!this._primed && q.size < RATE * 0.05) { if (!q.size) this.context._active.delete(this); return null; }   /* 50 ms of jitter buffer */
        if (q.size < Q) this._primed = false; else this._primed = true;
        const p = q.take(Q);
        return p ? [p, p] : null;
    }
}
class AudioProcessingEvent extends Event {
    constructor(t, i = {}) { super(t, i); this.playbackTime = +i.playbackTime || 0; this.inputBuffer = i.inputBuffer || null; this.outputBuffer = i.outputBuffer || null; }
}
/* onaudioprocess per bufferSize frames: microphone input arrives through _feed as it is captured, other input from the rendered graph */
class ScriptProcessorNode extends AudioNode {
    constructor(ctx, size, ins, outs) {
        size = +size || 1024; ins = ins === undefined ? 2 : ins | 0; outs = outs === undefined ? 2 : outs | 0;
        if (![256, 512, 1024, 2048, 4096, 8192, 16384].includes(size)) throw new DOMException(`Failed to execute 'createScriptProcessor' on 'BaseAudioContext': buffer size (${size}) must be 0 or a power of two between 256 and 16384.`, 'IndexSizeError');
        if (ins < 0 || outs < 0 || ins > 32 || outs > 32 || (!ins && !outs)) throw new DOMException("Failed to execute 'createScriptProcessor' on 'BaseAudioContext': number of input and output channels must be between 0 and 32, and not both zero.", 'IndexSizeError');
        super(ctx); this.bufferSize = size; this.channelCount = ins || 1; this.channelCountMode = 'explicit';
        this._nin = Math.max(ins, 1); this._nout = Math.max(outs, 1); this._acc = [new Float32Array(size), new Float32Array(size)]; this._n = 0; this._fed = false; this._ob = null; this._op = 0;
    }
    _feed(pcm) {
        this._fed = true;
        for (let i = 0; i < pcm.length;) {
            const k = Math.min(pcm.length - i, this.bufferSize - this._n), part = pcm.subarray(i, i + k);
            this._acc[0].set(part, this._n); this._acc[1].set(part, this._n); this._n += k; i += k;
            if (this._n === this.bufferSize) this._fire();
        }
    }
    _fire() {
        const sr = this.context.sampleRate, n = this.bufferSize;
        const ib = new AudioBuffer({ numberOfChannels: this._nin, length: n, sampleRate: sr }), ob = new AudioBuffer({ numberOfChannels: this._nout, length: n, sampleRate: sr });
        for (let c = 0; c < this._nin; c++) ib._ch[c].set(this._acc[Math.min(c, 1)]);
        this._n = 0; this._ob = ob; this._op = 0;
        this.dispatchEvent(new AudioProcessingEvent('audioprocess', { playbackTime: this.context.currentTime, inputBuffer: ib, outputBuffer: ob }));
    }
    _render(f) {
        if (!this._fed) {
            const m = this._mix(f);
            this._acc[0].set(m ? m[0] : silence, this._n); this._acc[1].set(m ? m[1] : silence, this._n); this._n += Q;
            if (this._n >= this.bufferSize) this._fire();
        }
        const ob = this._ob;
        if (!ob || this._op >= ob.length) return null;
        const o = this._buf(f), R = ob._ch[ob._ch.length > 1 ? 1 : 0];
        o[0].set(ob._ch[0].subarray(this._op, this._op + Q)); o[1].set(R.subarray(this._op, this._op + Q));
        this._op += Q;
        return o;
    }
}
/* a MediaStream of the microphone audio reaching it, for MediaRecorder */
class MediaStreamAudioDestinationNode extends AudioNode {
    constructor(ctx) {
        super(ctx, 1, 0);
        const t = new MediaStreamTrack(RTC_INTERNAL, 'audio', 'MediaStreamAudioDestinationNode'), hub = { refs: 1, subs: new SSet(), release() { if (--this.refs <= 0) this.subs.clear(); } };
        t._.hub = hub; this._hub = hub; this.stream = new MediaStream([t]);
    }
    _feed(pcm) { for (const f of [...this._hub.subs]) f(pcm); }
}
class AudioScheduledSourceNode extends AudioNode {
    constructor(ctx) { super(ctx, 0, 1); this._start = -1; this._stop = Infinity; this._done = false; }
    start(when = 0) {
        if (this._start >= 0) throw new DOMException('start() called twice', 'InvalidStateError');
        this._start = Math.max(0, +when || 0); this.context._activate(this);
    }
    stop(when = 0) {
        if (this._start < 0) throw new DOMException('stop() before start()', 'InvalidStateError');
        this._stop = Math.max(0, +when || 0);
    }
    _end() {
        if (this._done) return; this._done = true; this.context._active.delete(this);
        queueMicrotask(() => this.dispatchEvent(new Event('ended')));
    }
    _render(f) {
        if (this._start < 0 || this._done) return null;
        const sr = this.context.sampleRate, t0 = f / sr, t1 = (f + Q) / sr;
        if (t1 <= this._start) return null;
        if (t0 >= this._stop) { this._end(); return null; }
        const out = this._buf(f)[0].fill(0), a = Math.max(0, Math.ceil((this._start - t0) * sr)), b = Math.min(Q, Math.ceil((this._stop - t0) * sr));
        const r = this._gen(out, a, b, f);
        if (b < Q || r === false) this._end();
        return [out, out];
    }
}
class OscillatorNode extends AudioScheduledSourceNode {
    constructor(ctx, o = {}) { super(ctx); this._type = o.type || 'sine'; this.frequency = new AudioParam(ctx, o.frequency ?? 440); this.detune = new AudioParam(ctx, o.detune ?? 0); this._ph = 0; this._wave = null; }
    get type() { return this._type; }
    set type(t) { if (['sine', 'square', 'sawtooth', 'triangle'].includes(t)) this._type = t; }
    setPeriodicWave(w) { this._wave = w; this._type = 'custom'; }
    _gen(out, a, b, f) {
        const hz = paramIn(this.frequency, f) * Math.pow(2, paramIn(this.detune, f) / 1200), d = hz / this.context.sampleRate, t = this._type, w = this._wave;
        let ph = this._ph;
        for (let i = a; i < b; i++) {
            out[i] = t === 'sine' ? Math.sin(2 * Math.PI * ph) : t === 'square' ? (ph < 0.5 ? 1 : -1) : t === 'sawtooth' ? 2 * ph - 1 : t === 'triangle' ? 1 - 4 * Math.abs(ph - 0.5) : w ? w._at(ph) : 0;
            ph += d; ph -= Math.floor(ph);
        }
        this._ph = ph;
    }
}
class PeriodicWave {
    constructor(ctx, o = {}) { this._re = Float32Array.from(o.real || [0, 0]); this._im = Float32Array.from(o.imag || [0, 1]); this._norm = !o.disableNormalization; this._tab = null; }
    _at(ph) {
        if (!this._tab) {
            const N = 2048, t = new Float32Array(N); let mx = 0;
            for (let i = 0; i < N; i++) { let v = 0; for (let k = 1; k < this._re.length; k++) { const x = 2 * Math.PI * k * i / N; v += this._re[k] * Math.cos(x) + this._im[k] * Math.sin(x); } t[i] = v; mx = Math.max(mx, Math.abs(v)); }
            if (this._norm && mx) for (let i = 0; i < N; i++) t[i] /= mx;
            this._tab = t;
        }
        return this._tab[(ph * 2048) | 0];
    }
}
class ConstantSourceNode extends AudioScheduledSourceNode {
    constructor(ctx, o = {}) { super(ctx); this.offset = new AudioParam(ctx, o.offset ?? 1); }
    _gen(out, a, b, f) { out.fill(paramIn(this.offset, f), a, b); }
}
class AudioBufferSourceNode extends AudioScheduledSourceNode {
    constructor(ctx, o = {}) { super(ctx); this.buffer = o.buffer || null; this.loop = !!o.loop; this.loopStart = o.loopStart || 0; this.loopEnd = o.loopEnd || 0; this.playbackRate = new AudioParam(ctx, o.playbackRate ?? 1); this.detune = new AudioParam(ctx, 0); this._pos = -1; this._left = Infinity; }
    start(when = 0, offset = 0, dur) { super.start(when); this._off = Math.max(0, +offset || 0); if (dur !== undefined) this._left = Math.max(0, +dur) * this.context.sampleRate; }
    _render(f) {
        const buf = this.buffer;
        if (!buf || this._start < 0 || this._done) return super._render(f);
        const sr = this.context.sampleRate, t0 = f / sr;
        if ((f + Q) / sr <= this._start) return null;
        if (t0 >= this._stop) { this._end(); return null; }
        if (this._pos < 0) this._pos = this._off * buf.sampleRate;
        const [l, r] = this._buf(f), L = buf._ch[0], R = buf._ch[buf._ch.length > 1 ? 1 : 0], n = buf.length;
        const step = paramIn(this.playbackRate, f) * Math.pow(2, paramIn(this.detune, f) / 1200) * buf.sampleRate / sr;
        const ls = this.loopStart * buf.sampleRate, le = this.loopEnd > this.loopStart ? Math.min(n, this.loopEnd * buf.sampleRate) : n;
        const a = Math.max(0, Math.ceil((this._start - t0) * sr)), b = Math.min(Q, Math.ceil((this._stop - t0) * sr));
        let p = this._pos, ended = b < Q;
        l.fill(0); r.fill(0);
        for (let i = a; i < b; i++) {
            if (this.loop && p >= le) p = ls + (p - le) % Math.max(le - ls, 1);
            if (p >= n || p < 0 || this._left <= 0) { ended = true; break; }
            const j = p | 0, fr = p - j, j1 = j + 1 < n ? j + 1 : j;
            l[i] = L[j] + (L[j1] - L[j]) * fr; r[i] = R[j] + (R[j1] - R[j]) * fr;
            p += step; this._left--;
        }
        this._pos = p;
        if (ended) this._end();
        return [l, r];
    }
}

/* AudioWorklet: modules run on this thread with the worklet globals in scope, and processors run inside the graph.
   A worklet fed by a microphone processes each 128 frames as they are captured. */
let wkPort = null;   /* the port of the processor being constructed */
class AudioWorkletProcessor {
    constructor() {
        if (!wkPort) throw new TypeError("Failed to construct 'AudioWorkletProcessor': Illegal constructor");
        this.port = wkPort; wkPort = null;
    }
}
class AudioParamMap {
    constructor(m) { Object.defineProperty(this, '_m', { value: m }); }
    get size() { return this._m.size; }
    get(k) { return this._m.get(k); }
    has(k) { return this._m.has(k); }
    keys() { return this._m.keys(); }
    values() { return this._m.values(); }
    entries() { return this._m.entries(); }
    forEach(f, self) { this._m.forEach((v, k) => f.call(self, v, k, this)); }
    [Symbol.iterator]() { return this._m.entries(); }
}
class AudioWorklet {
    constructor(ctx) { Object.defineProperty(this, '_c', { value: ctx }); this._procs = new SMap(); this._mods = new SMap(); }
    addModule(url) {
        let u;
        try { u = new URL(String(url), document.baseURI || N.url()).href; }
        catch (e) { return Promise.reject(new DOMException(`Failed to execute 'addModule' on 'Worklet': The URL '${url}' is invalid.`, 'SyntaxError')); }
        let p = this._mods.get(u);
        if (!p) {
            p = fetch(u).then(r => { if (!r.ok) throw new TypeError('worklet module HTTP ' + r.status); return r.text(); }).then(src => { this._run(src); }).catch(e => {
                this._mods.delete(u);
                console.error(e);
                throw new DOMException("Failed to execute 'addModule' on 'Worklet': Unable to load a worklet's module.", 'AbortError');
            });
            this._mods.set(u, p);
        }
        return p;
    }
    _run(src) {
        const c = this._c, procs = this._procs, scope = Object.create(null);
        Object.defineProperties(scope, {
            registerProcessor: { value(name, ctor) {
                name = String(name);
                if (!name) throw new DOMException("Failed to execute 'registerProcessor' on 'AudioWorkletGlobalScope': The processor name cannot be empty.", 'NotSupportedError');
                if (procs.has(name)) throw new DOMException(`Failed to execute 'registerProcessor' on 'AudioWorkletGlobalScope': An AudioWorkletProcessor with name:"${name}" is already registered.`, 'NotSupportedError');
                if (typeof ctor !== 'function') throw new TypeError("Failed to execute 'registerProcessor' on 'AudioWorkletGlobalScope': The provided callback is not a function.");
                const d = ctor.parameterDescriptors;
                procs.set(name, { ctor, params: d ? [...d] : [] });
            } },
            AudioWorkletProcessor: { value: AudioWorkletProcessor },
            sampleRate: { get: () => c.sampleRate },
            currentFrame: { get: () => c._f },
            currentTime: { get: () => c._f / c.sampleRate },
        });
        /* module code: export keywords are dropped, the body runs once */
        let run;
        ttInternal++;
        try { run = new Function('lumenWorkletScope', 'with (lumenWorkletScope) {\n' + String(src).replace(/^([ \t]*)export\s+(default\s+)?/gm, '$1') + '\n}'); } finally { ttInternal--; }
        run(scope);
    }
}
class AudioWorkletNode extends AudioNode {
    constructor(ctx, name, o = {}) {
        if (!(ctx instanceof BaseAudioContext)) throw new TypeError("Failed to construct 'AudioWorkletNode': parameter 1 is not of type 'BaseAudioContext'.");
        const d = ctx.audioWorklet._procs.get(String(name));
        if (!d) throw new DOMException(`Failed to construct 'AudioWorkletNode': AudioWorkletNode cannot be created: The node name '${name}' is not defined in AudioWorkletGlobalScope.`, 'InvalidStateError');
        const ins = o.numberOfInputs ?? 1, outs = o.numberOfOutputs ?? 1;
        super(ctx, ins, outs);
        const ch = new MessageChannel(), pm = new SMap();
        for (const p of d.params) pm.set(p.name, new AudioParam(ctx, p.defaultValue ?? 0, p.minValue, p.maxValue));
        if (o.parameterData) for (const [k, v] of Object.entries(o.parameterData)) if (pm.has(k)) pm.get(k).value = v;
        this.port = ch.port1; this.parameters = new AudioParamMap(pm); this.onprocessorerror = null;
        this._outCh = o.outputChannelCount || null; this._keep = true; this._proc = null;
        this._acc = new Float32Array(Q); this._an = 0; this._fedAt = -1e9; this._ql = new PcmQueue(RATE / 2); this._qr = new PcmQueue(RATE / 2);
        wkPort = ch.port2;
        try { this._proc = new d.ctor({ numberOfInputs: ins, numberOfOutputs: outs, outputChannelCount: o.outputChannelCount, parameterData: o.parameterData || {}, processorOptions: o.processorOptions === undefined ? undefined : structuredClone(o.processorOptions) }); }
        catch (e) { this._fail(e); }
        wkPort = null;
        if (this._proc) { ctx._auto.add(this); ctx._activate(this); }
    }
    _fail(e) {
        this._proc = null; this.context._auto.delete(this); this.context._active.delete(this);
        console.error(e);
        setTimeout(() => this.dispatchEvent(new ErrorEvent('processorerror', { message: String(e && e.message || e), error: e })));
    }
    _process(input) {   /* input: the channels of input 0; returns output 0 as [L, R], valid until the next call */
        const p = this._proc;
        if (!p) return null;
        /* like Chrome, the same arrays are handed to every process() call; outputs start silent */
        const b = this._bufs || (this._bufs = { ins: [], outs: [], prm: {} }), ins = b.ins, outs = b.outs, prm = b.prm;
        for (let i = 0; i < this.numberOfInputs; i++) ins[i] = i ? (ins[i] || []) : input;
        for (let i = 0; i < this.numberOfOutputs; i++) {
            const n = (this._outCh && this._outCh[i]) || (this.numberOfInputs === 1 && this.numberOfOutputs === 1 ? Math.max(input.length, 1) : 1);
            let o = outs[i];
            if (!o || o.length !== n) { o = outs[i] = []; for (let c = 0; c < n; c++) o.push(new Float32Array(Q)); }
            else for (const c of o) c.fill(0);
        }
        for (const [k, a] of this.parameters._m) (prm[k] || (prm[k] = new Float32Array(1)))[0] = paramIn(a, this.context._f);
        try { this._keep = !!p.process(ins, outs, prm); } catch (e) { this._fail(e); return null; }
        const o = outs[0];
        return o && o.length ? [o[0], o[1] || o[0]] : null;
    }
    _feed(pcm) {
        this._fedAt = performance.now();
        let fwd = null, k0 = 0;
        for (let i = 0; i < pcm.length;) {
            const k = Math.min(pcm.length - i, Q - this._an);
            this._acc.set(pcm.subarray(i, i + k), this._an); this._an += k; i += k;
            if (this._an < Q) break;
            this._an = 0;
            const o = this._process(this._accIn || (this._accIn = [this._acc]));
            if (!o || !this._out.size) continue;   /* nothing downstream: process() ran for its side effects */
            this._ql.push(o[0]); this._qr.push(o[1]);   /* the queues copy */
            if (!fwd) fwd = new Float32Array(pcm.length + Q);
            fwd.set(o[0], k0); k0 += Q;
        }
        if (fwd && k0) { const out = fwd.subarray(0, k0); for (const n of this._out) if (n instanceof AudioNode) feedMic(n, out, 1); }
    }
    _render(f) {
        if (performance.now() - this._fedAt < 500) { const l = this._ql.take(Q); return l ? [l, this._qr.take(Q) || l] : null; }
        if (!this._proc) return null;
        const m = this._mix(f);
        if (!m && !this._keep) { this.context._active.delete(this); return null; }
        return this._process(m ? [m[0], m[1]] : []);
    }
}

class AudioBuffer {
    constructor(o = {}) {
        const n = o.length | 0, c = o.numberOfChannels || 1;
        if (n <= 0 || !o.sampleRate) throw new DOMException('invalid AudioBuffer options', 'NotSupportedError');
        this.sampleRate = +o.sampleRate; this.length = n; this._ch = Array.from({ length: c }, () => new Float32Array(n));
    }
    get numberOfChannels() { return this._ch.length; }
    get duration() { return this.length / this.sampleRate; }
    getChannelData(i) { if (!this._ch[i]) throw new DOMException('channel index', 'IndexSizeError'); return this._ch[i]; }
    copyFromChannel(d, i, o = 0) { d.set(this.getChannelData(i).subarray(o, o + d.length)); }
    copyToChannel(s, i, o = 0) { this.getChannelData(i).set(s.subarray(0, this.length - o), o); }
}

class BaseAudioContext extends EventTarget {
    constructor(rate) {
        super(); this.sampleRate = rate; this._f = 0; this._active = new SSet(); this._state = 'running';
        this.destination = new AudioDestinationNode(this); this.audioWorklet = new AudioWorklet(this); this._auto = new SSet();
    }
    get state() { return this._state; }
    get currentTime() { return this._f / this.sampleRate; }
    _setState(s) { if (this._state !== s) { this._state = s; this.dispatchEvent(new Event('statechange')); } }
    _activate(n) { this._active.add(n); }
    _quantum() { const o = this.destination._pull(this._f); for (const n of this._auto) n._pull(this._f); this._f += Q; return o; }   /* worklets run unconnected too */
    createGain() { return new GainNode(this); }
    createOscillator() { return new OscillatorNode(this); }
    createBufferSource() { return new AudioBufferSourceNode(this); }
    createConstantSource() { return new ConstantSourceNode(this); }
    createStereoPanner() { return new StereoPannerNode(this); }
    createDelay(m) { return new DelayNode(this, { maxDelayTime: m }); }
    createBiquadFilter() { return new BiquadFilterNode(this); }
    createDynamicsCompressor() { return new DynamicsCompressorNode(this); }
    createConvolver() { return new ConvolverNode(this); }
    createAnalyser() { return new AnalyserNode(this); }
    createPeriodicWave(real, imag, o = {}) { return new PeriodicWave(this, { real, imag, disableNormalization: o.disableNormalization }); }
    createBuffer(c, n, r) { return new AudioBuffer({ numberOfChannels: c, length: n, sampleRate: r }); }
    createMediaElementSource(el) { return new MediaElementAudioSourceNode(this, { mediaElement: el }); }
    createMediaStreamSource(s) { return new MediaStreamAudioSourceNode(this, { mediaStream: s }); }
    createScriptProcessor(size = 0, ins = 2, outs = 2) { return new ScriptProcessorNode(this, size, ins, outs); }
    decodeAudioData(data, ok, err) {
        return new Promise((res, rej) => setTimeout(() => {
            const pcm = data && N.audioDecode(data);
            if (!pcm) { const e = new DOMException('Unable to decode audio data', 'EncodingError'); if (err) err(e); rej(e); return; }
            const s = new Float32Array(pcm), n = s.length / 2, b = new AudioBuffer({ numberOfChannels: 2, length: n, sampleRate: RATE }), L = b._ch[0], R = b._ch[1];
            for (let i = 0; i < n; i++) { L[i] = s[2 * i]; R[i] = s[2 * i + 1]; }
            if (ok) ok(b); res(b);
        }, 0));
    }
}

class AudioContext extends BaseAudioContext {
    constructor(o = {}) {
        super(RATE); this.baseLatency = Q / RATE; this.outputLatency = LAT / RATE; this.sinkId = '';
        this._dev = -1; this._wall = performance.now(); this._timer = 0;
    }
    get currentTime() { this._sync(); return this._f / RATE; }
    _sync() {   /* idle clock: advance by wall time when nothing is rendering */
        const now = performance.now();
        if (!this._timer && this._state === 'running') this._f += Math.floor((now - this._wall) * RATE / 1000 / Q) * Q;
        if (!this._timer) this._wall = now - ((now - this._wall) % (Q * 1000 / RATE));
    }
    _activate(n) { super._activate(n); this._kick(); }
    _kick() {
        if (this._timer || this._state !== 'running') return;
        this._sync();
        if (this._dev < 0) this._dev = N.waOpen();
        this._wall = performance.now(); this._base = this._f;
        this._pump();
    }
    _pump() {
        this._timer = 0;
        if (this._state !== 'running') return;
        const target = this._base + (performance.now() - this._wall) * RATE / 1000 + LAT;
        const queued = this._dev >= 0 ? N.waQueued(this._dev) : 0;
        let n = queued > LAT * 2 ? 0 : Math.max(0, Math.ceil((target - this._f) / Q));
        if (n > 64) { this._f += (n - 8) * Q; n = 8; }   /* fell far behind (timer throttled): skip ahead */
        if (n) {
            const out = new Float32Array(n * Q * 2);
            for (let k = 0; k < n; k++) {
                const o = this._quantum(); if (!o) continue;
                const a = o[0], b = o[1], base = k * Q * 2;
                for (let i = 0; i < Q; i++) { out[base + 2 * i] = a[i]; out[base + 2 * i + 1] = b[i]; }
            }
            if (this._dev >= 0) N.waPush(this._dev, out);
        }
        if (this._active.size || (this._dev >= 0 && N.waQueued(this._dev) > 0)) this._timer = setTimeout(() => this._pump(), 20);
        else { this._wall = performance.now(); this._timer = 0; }
    }
    resume() {
        if (this._state === 'closed') return Promise.reject(new DOMException('context is closed', 'InvalidStateError'));
        if (this._state === 'suspended') { this._wall = performance.now(); if (this._dev >= 0) N.waPause(this._dev, false); this._setState('running'); if (this._active.size) this._kick(); }
        return Promise.resolve();
    }
    suspend() {
        if (this._state === 'closed') return Promise.reject(new DOMException('context is closed', 'InvalidStateError'));
        if (this._state === 'running') { this._sync(); clearTimeout(this._timer); this._timer = 0; if (this._dev >= 0) N.waPause(this._dev, true); this._setState('suspended'); }
        return Promise.resolve();
    }
    close() {
        if (this._state !== 'closed') { this._sync(); clearTimeout(this._timer); this._timer = 0; if (this._dev >= 0) N.waClose(this._dev); this._dev = -1; this._setState('closed'); }
        return Promise.resolve();
    }
    getOutputTimestamp() { return { contextTime: this.currentTime, performanceTime: performance.now() }; }
    createMediaStreamDestination() { return new MediaStreamAudioDestinationNode(this); }
}

class OfflineAudioContext extends BaseAudioContext {
    constructor(c, n, r) {
        const o = typeof c === 'object' ? c : { numberOfChannels: c, length: n, sampleRate: r };
        super(o.sampleRate || RATE); this.length = o.length; this._chn = o.numberOfChannels || 1; this._state = 'suspended';
    }
    startRendering() {
        return new Promise(res => setTimeout(() => {
            this._setState('running');
            const b = new AudioBuffer({ numberOfChannels: this._chn, length: this.length, sampleRate: this.sampleRate });
            for (let f = 0; f < this.length; f += Q) {
                const o = this._quantum(); if (!o) continue;
                const m = Math.min(Q, this.length - f);
                for (let c = 0; c < this._chn; c++) b._ch[c].set(o[Math.min(c, 1)].subarray(0, m), f);
            }
            this._setState('closed');
            const ev = new OfflineAudioCompletionEvent('complete', { renderedBuffer: b }); this.dispatchEvent(ev);
            res(b);
        }, 0));
    }
    suspend() { return Promise.resolve(); }
    resume() { return Promise.resolve(); }
}

/* node constructor options: a dictionary, and the AudioNodeOptions applied with the node's fixed-value rules */
const ctorCheck = (ctx, o, name) => {
    if (!(ctx instanceof BaseAudioContext)) throw new TypeError(`Failed to construct '${name}': parameter 1 is not of type 'BaseAudioContext'.`);
    if (o == null) return {};
    if (typeof o !== 'object' && typeof o !== 'function') throw new TypeError(`Failed to construct '${name}': The provided value is not of type '${name.replace(/Node$/, '')}Options'.`);
    return o;
};
function nodeOpts(n, o, fixed = {}) {
    const bad = m => new DOMException(`Failed to construct '${n.constructor.name}': ${m}`, 'InvalidStateError');
    if (o.channelCount !== undefined) {
        const c = +o.channelCount;
        if (fixed.count !== undefined && c !== fixed.count) throw bad(`channelCount cannot be changed from ${fixed.count} to ${c}`);
        if (!(c >= 1 && c <= 32)) throw new DOMException(`Failed to construct '${n.constructor.name}': The channel count provided (${c}) is outside the range [1, 32].`, 'NotSupportedError');
        n.channelCount = c;
    }
    if (o.channelCountMode !== undefined) {
        const v = String(o.channelCountMode);
        if (!['max', 'clamped-max', 'explicit'].includes(v)) throw new TypeError(`Failed to read the 'channelCountMode' property: The provided value '${v}' is not a valid enum value of type ChannelCountMode.`);
        if (fixed.mode !== undefined && v !== fixed.mode) throw bad(`channelCountMode cannot be changed from '${fixed.mode}' to '${v}'`);
        n.channelCountMode = v;
    }
    if (o.channelInterpretation !== undefined) {
        const v = String(o.channelInterpretation);
        if (!['speakers', 'discrete'].includes(v)) throw new TypeError(`Failed to read the 'channelInterpretation' property: The provided value '${v}' is not a valid enum value of type ChannelInterpretation.`);
        n.channelInterpretation = v;
    }
    return n;
}
class ChannelMergerNode extends AudioNode {
    constructor(ctx, o = {}) {
        o = ctorCheck(ctx, o, 'ChannelMergerNode');
        const n = o.numberOfInputs ?? 6;
        if (!(n >= 1 && n <= 32)) throw new DOMException(`Failed to construct 'ChannelMergerNode': The number of inputs provided (${n}) is outside the range [1, 32].`, 'IndexSizeError');
        super(ctx, n, 1); this.channelCount = 1; this.channelCountMode = 'explicit';
        nodeOpts(this, o, { count: 1, mode: 'explicit' });
    }
}
class ChannelSplitterNode extends AudioNode {
    constructor(ctx, o = {}) {
        o = ctorCheck(ctx, o, 'ChannelSplitterNode');
        const n = o.numberOfOutputs ?? 6;
        if (!(n >= 1 && n <= 32)) throw new DOMException(`Failed to construct 'ChannelSplitterNode': The number of outputs provided (${n}) is outside the range [1, 32].`, 'IndexSizeError');
        super(ctx, 1, n); this.channelCount = n; this.channelCountMode = 'explicit'; this.channelInterpretation = 'discrete';
        if (o.channelInterpretation !== undefined && o.channelInterpretation !== 'discrete') throw new DOMException("Failed to construct 'ChannelSplitterNode': channelInterpretation must be 'discrete'", 'InvalidStateError');
        nodeOpts(this, o, { count: n, mode: 'explicit' });
    }
}
class WaveShaperNode extends AudioNode {
    constructor(ctx, o = {}) { o = ctorCheck(ctx, o, 'WaveShaperNode'); super(ctx); this._curve = null; this.oversample = o.oversample ?? 'none'; if (o.curve) this.curve = o.curve; nodeOpts(this, o); }
    get curve() { return this._curve; }
    set curve(c) {
        if (c === null) { this._curve = null; return; }
        if (!(c instanceof Float32Array)) c = Float32Array.from(c);
        if (c.length < 2) throw new DOMException(`Failed to set the 'curve' property on 'WaveShaperNode': The curve length provided (${c.length}) is less than the minimum bound (2).`, 'InvalidStateError');
        this._curve = c;
    }
    _render(f) {
        const m = this._mix(f), c = this._curve; if (!m || !c) return m;
        const n = c.length;
        for (const ch of m) for (let i = 0; i < Q; i++) {
            const v = (n - 1) * (ch[i] + 1) / 2;
            if (v <= 0) ch[i] = c[0]; else if (v >= n - 1) ch[i] = c[n - 1];
            else { const k = Math.floor(v), t = v - k; ch[i] = c[k] + (c[k + 1] - c[k]) * t; }
        }
        return m;
    }
}
class IIRFilterNode extends AudioNode {
    constructor(ctx, o) {
        o = ctorCheck(ctx, o, 'IIRFilterNode');
        for (const k of ['feedforward', 'feedback']) if (o[k] === undefined) throw new TypeError(`Failed to construct 'IIRFilterNode': required member ${k} is undefined.`);
        const ff = Array.from(o.feedforward), fb = Array.from(o.feedback);
        for (const [a, nm] of [[ff, 'feedforward'], [fb, 'feedback']])
            if (a.length < 1 || a.length > 20) throw new DOMException(`Failed to construct 'IIRFilterNode': The ${nm} array length provided (${a.length}) is outside the range [1, 20].`, 'NotSupportedError');
        if (ff.every(x => x === 0)) throw new DOMException("Failed to construct 'IIRFilterNode': At least one feedforward coefficient must be non-zero.", 'InvalidStateError');
        if (fb[0] === 0) throw new DOMException("Failed to construct 'IIRFilterNode': First feedback coefficient cannot be zero.", 'InvalidStateError');
        super(ctx); nodeOpts(this, o); this._b = ff.map(x => x / fb[0]); this._a = fb.map(x => x / fb[0]); this._x = [[], []]; this._y = [[], []];
    }
    getFrequencyResponse(fr, mag, ph) {
        if (mag.length < fr.length || ph.length < fr.length) throw new DOMException("Failed to execute 'getFrequencyResponse' on 'IIRFilterNode': array lengths differ", 'InvalidAccessError');
        const ny = this.context.sampleRate / 2;
        for (let i = 0; i < fr.length; i++) {
            const f = fr[i];
            if (!(f >= 0 && f <= ny)) { mag[i] = NaN; ph[i] = NaN; continue; }
            const w = -Math.PI * f / ny;
            let nr = 0, ni = 0, dr = 0, di = 0;
            this._b.forEach((b, k) => { nr += b * Math.cos(w * k); ni += b * Math.sin(w * k); });
            this._a.forEach((a, k) => { dr += a * Math.cos(w * k); di += a * Math.sin(w * k); });
            const d = dr * dr + di * di, rr = (nr * dr + ni * di) / d, ri = (ni * dr - nr * di) / d;
            mag[i] = Math.hypot(rr, ri); ph[i] = Math.atan2(ri, rr);
        }
    }
    _render(f) {
        const m = this._mix(f); if (!m) return null;
        const b = this._b, a = this._a;
        m.forEach((ch, c) => {
            const x = this._x[c], y = this._y[c];
            for (let i = 0; i < Q; i++) {
                x.unshift(ch[i]); if (x.length > b.length) x.pop();
                let v = 0; for (let k = 0; k < x.length; k++) v += b[k] * x[k];
                for (let k = 1; k < a.length && k - 1 < y.length; k++) v -= a[k] * y[k - 1];
                y.unshift(v); if (y.length > a.length) y.pop();
                ch[i] = v;
            }
        });
        return m;
    }
}
class AudioListener {
    constructor(ctx) {
        for (const [k, v] of [['positionX', 0], ['positionY', 0], ['positionZ', 0], ['forwardX', 0], ['forwardY', 0], ['forwardZ', -1], ['upX', 0], ['upY', 1], ['upZ', 0]])
            this[k] = new AudioParam(ctx, v);
    }
    setPosition(x, y, z) { this.positionX.value = x; this.positionY.value = y; this.positionZ.value = z; }
    setOrientation(x, y, z, ux, uy, uz) { this.forwardX.value = x; this.forwardY.value = y; this.forwardZ.value = z; this.upX.value = ux; this.upY.value = uy; this.upZ.value = uz; }
}
/* PannerNode: parameters and checks; the signal passes through unspatialized */
class PannerNode extends AudioNode {
    constructor(ctx, o = {}) {
        o = ctorCheck(ctx, o, 'PannerNode');
        super(ctx); this.channelCountMode = 'clamped-max'; nodeOpts(this, o);
        for (const [k, v] of [['positionX', 0], ['positionY', 0], ['positionZ', 0], ['orientationX', 1], ['orientationY', 0], ['orientationZ', 0]])
            this[k] = new AudioParam(ctx, o[k] ?? v);
        this.panningModel = o.panningModel ?? 'equalpower'; this.distanceModel = o.distanceModel ?? 'inverse';
        this._ref = 1; this._max = 10000; this._roll = 1; this.coneInnerAngle = o.coneInnerAngle ?? 360; this.coneOuterAngle = o.coneOuterAngle ?? 360; this._cog = 0;
        for (const k of ['refDistance', 'maxDistance', 'rolloffFactor', 'coneOuterGain']) if (o[k] !== undefined) this[k] = o[k];
    }
    get refDistance() { return this._ref; }
    set refDistance(v) { v = +v; if (v < 0) throw new RangeError(`Failed to set the 'refDistance' property on 'PannerNode': The refDistance provided (${v}) is less than the minimum bound (0).`); this._ref = v; }
    get maxDistance() { return this._max; }
    set maxDistance(v) { v = +v; if (!(v > 0)) throw new RangeError(`Failed to set the 'maxDistance' property on 'PannerNode': The maxDistance provided (${v}) is less than or equal to the minimum bound (0).`); this._max = v; }
    get rolloffFactor() { return this._roll; }
    set rolloffFactor(v) { v = +v; if (v < 0) throw new RangeError(`Failed to set the 'rolloffFactor' property on 'PannerNode': The rolloffFactor provided (${v}) is less than the minimum bound (0).`); this._roll = v; }
    get coneOuterGain() { return this._cog; }
    set coneOuterGain(v) { v = +v; if (!(v >= 0 && v <= 1)) throw new DOMException(`Failed to set the 'coneOuterGain' property on 'PannerNode': The coneOuterGain provided (${v}) is outside the range [0, 1].`, 'InvalidStateError'); this._cog = v; }
    setPosition(x, y, z) { this.positionX.value = x; this.positionY.value = y; this.positionZ.value = z; }
    setOrientation(x, y, z) { this.orientationX.value = x; this.orientationY.value = y; this.orientationZ.value = z; }
}
/* the element keeps playing through its own output; the node itself renders silence */
class MediaElementAudioSourceNode extends AudioNode {
    constructor(ctx, o) {
        o = ctorCheck(ctx, o, 'MediaElementAudioSourceNode');
        if (!(o.mediaElement instanceof HTMLMediaElement)) throw new TypeError("Failed to construct 'MediaElementAudioSourceNode': Failed to read the 'mediaElement' property from 'MediaElementAudioSourceOptions': Required member is undefined.");
        if (!(ctx instanceof AudioContext)) throw new TypeError("Failed to construct 'MediaElementAudioSourceNode': parameter 1 is not of type 'AudioContext'.");
        super(ctx, 0, 1); this._el = o.mediaElement; nodeOpts(this, o);
    }
    get mediaElement() { return this._el; }
    _render() { return null; }
}
class OfflineAudioCompletionEvent extends Event {
    constructor(type, init) {
        if (!init || !(init.renderedBuffer instanceof AudioBuffer)) throw new TypeError("Failed to construct 'OfflineAudioCompletionEvent': required member renderedBuffer is undefined.");
        super(type, init); this._rb = init.renderedBuffer;
    }
    get renderedBuffer() { return this._rb; }
}
/* a node's AudioParams are readonly prototype attributes; the constructor's assignment is the only one kept */
for (const [C, ns] of [[AudioBufferSourceNode, 'detune playbackRate'], [BiquadFilterNode, 'detune frequency gain Q'], [ConstantSourceNode, 'offset'],
    [DelayNode, 'delayTime'], [DynamicsCompressorNode, 'attack knee ratio release threshold'], [GainNode, 'gain'], [OscillatorNode, 'detune frequency'],
    [StereoPannerNode, 'pan'], [PannerNode, 'positionX positionY positionZ orientationX orientationY orientationZ'],
    [AudioListener, 'positionX positionY positionZ forwardX forwardY forwardZ upX upY upZ']])
    for (const n of ns.split(' ')) {
        const k = Symbol(n);
        Object.defineProperty(C.prototype, n, { get() { return this[k]; }, set(v) { if (this[k] === undefined && v instanceof AudioParam) this[k] = v; }, enumerable: true, configurable: true });
    }
methods(BaseAudioContext.prototype, {
    createChannelMerger(n = 6) { return new ChannelMergerNode(this, { numberOfInputs: n }); },
    createChannelSplitter(n = 6) { return new ChannelSplitterNode(this, { numberOfOutputs: n }); },
    createWaveShaper() { return new WaveShaperNode(this); },
    createIIRFilter(feedforward, feedback) { return new IIRFilterNode(this, { feedforward, feedback }); },
    createPanner() { return new PannerNode(this); },
});
Object.defineProperty(BaseAudioContext.prototype, 'listener', { get() { return this._listener || (this._listener = new AudioListener(this)); }, configurable: true });

Object.assign(globalThis, {
    MediaElementAudioSourceNode, ChannelMergerNode, ChannelSplitterNode, WaveShaperNode, IIRFilterNode, PannerNode, AudioListener, OfflineAudioCompletionEvent,
    AudioContext, webkitAudioContext: AudioContext, OfflineAudioContext, webkitOfflineAudioContext: OfflineAudioContext, BaseAudioContext,
    AudioNode, AudioParam, AudioBuffer, AudioDestinationNode, AudioScheduledSourceNode, GainNode, OscillatorNode, AudioBufferSourceNode,
    ConstantSourceNode, StereoPannerNode, DelayNode, BiquadFilterNode, DynamicsCompressorNode, ConvolverNode, AnalyserNode, PeriodicWave, MediaStreamAudioSourceNode,
    ScriptProcessorNode, AudioProcessingEvent, MediaStreamAudioDestinationNode, AudioWorklet, AudioWorkletNode, AudioParamMap,
});
})();
