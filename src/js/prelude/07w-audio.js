/* Web Audio: the graph renders in JS in 128-frame quanta; N.wa* pushes stereo float32 at 48 kHz to SDL. */
if (N.waOpen) (() => {
const RATE = 48000, Q = 128, LAT = RATE * 0.08;
const silence = new Float32Array(Q);

class AudioParam {
    constructor(ctx, v, min = -3.4e38, max = 3.4e38) { this._c = ctx; this._v = v; this.defaultValue = v; this.minValue = min; this.maxValue = max; this._ev = []; this._t0 = 0; }
    get value() { return this._v; }
    set value(x) { x = +x; if (x === x) { this._v = x; this._ev = []; } }
    _add(e) {
        if (!this._ev.length) this._t0 = this._c.currentTime;
        this._ev.push(e); this._ev.sort((a, b) => a.t - b.t); return this;
    }
    setValueAtTime(v, t) { return this._add({ k: 0, v: +v, t: +t }); }
    linearRampToValueAtTime(v, t) { return this._add({ k: 1, v: +v, t: +t }); }
    exponentialRampToValueAtTime(v, t) { return this._add({ k: 2, v: +v, t: +t }); }
    setTargetAtTime(v, t, c) { return this._add({ k: 3, v: +v, t: +t, c: Math.max(+c, 1e-6) }); }
    setValueCurveAtTime(curve, t, d) {
        const n = curve.length; for (let i = 0; i < n; i++) this._add({ k: i ? 1 : 0, v: curve[i], t: +t + d * i / Math.max(n - 1, 1) });
        return this;
    }
    cancelScheduledValues(t) { this._ev = this._ev.filter(e => e.t < t); return this; }
    cancelAndHoldAtTime(t) { this._v = this._at(t); this._ev = []; return this; }
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
    constructor(ctx, ins = 1, outs = 1) { super(); this.context = ctx; this.numberOfInputs = ins; this.numberOfOutputs = outs; this.channelCount = 2; this.channelCountMode = 'max'; this.channelInterpretation = 'speakers'; this._in = new Set(); this._out = new Set(); this._q = -1; this._o = null; }
    connect(d) {
        if (d instanceof AudioParam) { (d._in || (d._in = new Set())).add(this); this._out.add(d); return; }
        if (!(d instanceof AudioNode)) throw new TypeError('connect: not an AudioNode');
        d._in.add(this); this._out.add(d); return d;
    }
    disconnect(d) {
        for (const o of [...this._out]) if (d === undefined || o === d) { o._in.delete(this); this._out.delete(o); }
    }
    _mix(f) {   /* sum of inputs as [L, R] */
        let l = null, r = null;
        for (const s of this._in) {
            const o = s._pull(f);
            if (!o) continue;
            if (!l) { l = new Float32Array(Q); r = new Float32Array(Q); }
            const a = o[0], b = o[1];
            for (let i = 0; i < Q; i++) { l[i] += a[i]; r[i] += b[i]; }
        }
        return l ? [l, r] : null;
    }
    _pull(f) { if (this._q !== f) { this._q = f; this._o = this._render(f); } return this._o; }
    _render(f) { return this._mix(f); }
}
const paramIn = (p, f) => {   /* audio-rate modulation summed into a param, averaged per quantum */
    let v = p._at(f / RATE);
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
    constructor(ctx, o = {}) { super(ctx); this._max = o.maxDelayTime ?? 1; this.delayTime = new AudioParam(ctx, o.delayTime ?? 0, 0, this._max); this._b = [new Float32Array(Math.ceil(this._max * RATE) + Q + 1), null]; this._b[1] = new Float32Array(this._b[0].length); this._w = 0; }
    _render(f) {
        const m = this._mix(f) || [silence, silence], n = this._b[0].length, d = Math.min(Math.round(paramIn(this.delayTime, f) * RATE), n - Q - 1);
        const l = new Float32Array(Q), r = new Float32Array(Q);
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
class AnalyserNode extends AudioNode {
    constructor(ctx, o = {}) { super(ctx); this.fftSize = o.fftSize || 2048; this.minDecibels = -100; this.maxDecibels = -30; this.smoothingTimeConstant = 0.8; this._last = new Float32Array(32768); }
    get frequencyBinCount() { return this.fftSize / 2; }
    _render(f) {
        const m = this._mix(f), L = this._last;
        L.copyWithin(0, Q);
        if (m) for (let i = 0; i < Q; i++) L[L.length - Q + i] = (m[0][i] + m[1][i]) / 2; else L.fill(0, L.length - Q);
        return m;
    }
    getFloatTimeDomainData(a) { const n = Math.min(a.length, this.fftSize); a.set(this._last.subarray(this._last.length - n)); }
    getByteTimeDomainData(a) { const n = Math.min(a.length, this.fftSize), s = this._last.length - n; for (let i = 0; i < n; i++) a[i] = Math.max(0, Math.min(255, 128 + this._last[s + i] * 128)); }
    getFloatFrequencyData(a) {
        const n = Math.min(a.length, this.frequencyBinCount), N2 = n * 2, s = this._last.length - N2;
        for (let k = 0; k < n; k++) {   /* plain DFT with a Hann window; analysers are rare and small */
            let re = 0, im = 0;
            for (let i = 0; i < N2; i++) { const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N2), x = this._last[s + i] * w, ph = 2 * Math.PI * k * i / N2; re += x * Math.cos(ph); im -= x * Math.sin(ph); }
            a[k] = 20 * Math.log10(Math.sqrt(re * re + im * im) / N2 + 1e-12);
        }
    }
    getByteFrequencyData(a) {
        const f = new Float32Array(Math.min(a.length, this.frequencyBinCount)); this.getFloatFrequencyData(f);
        for (let i = 0; i < f.length; i++) a[i] = Math.max(0, Math.min(255, 255 * (f[i] - this.minDecibels) / (this.maxDecibels - this.minDecibels)));
    }
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
        const t0 = f / RATE, t1 = (f + Q) / RATE;
        if (t1 <= this._start) return null;
        if (t0 >= this._stop) { this._end(); return null; }
        const out = new Float32Array(Q), a = Math.max(0, Math.ceil((this._start - t0) * RATE)), b = Math.min(Q, Math.ceil((this._stop - t0) * RATE));
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
        const hz = paramIn(this.frequency, f) * Math.pow(2, paramIn(this.detune, f) / 1200), d = hz / RATE, t = this._type, w = this._wave;
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
    start(when = 0, offset = 0, dur) { super.start(when); this._off = Math.max(0, +offset || 0); if (dur !== undefined) this._left = Math.max(0, +dur) * RATE; }
    _render(f) {
        const buf = this.buffer;
        if (!buf || this._start < 0 || this._done) return super._render(f);
        const t0 = f / RATE;
        if ((f + Q) / RATE <= this._start) return null;
        if (t0 >= this._stop) { this._end(); return null; }
        if (this._pos < 0) this._pos = this._off * buf.sampleRate;
        const l = new Float32Array(Q), r = new Float32Array(Q), L = buf._ch[0], R = buf._ch[buf._ch.length > 1 ? 1 : 0], n = buf.length;
        const step = paramIn(this.playbackRate, f) * Math.pow(2, paramIn(this.detune, f) / 1200) * buf.sampleRate / RATE;
        const ls = this.loopStart * buf.sampleRate, le = this.loopEnd > this.loopStart ? Math.min(n, this.loopEnd * buf.sampleRate) : n;
        const a = Math.max(0, Math.ceil((this._start - t0) * RATE)), b = Math.min(Q, Math.ceil((this._stop - t0) * RATE));
        let p = this._pos, ended = b < Q;
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
        super(); this.sampleRate = rate; this._f = 0; this._active = new Set(); this._state = 'running';
        this.destination = new AudioDestinationNode(this); this.listener = {}; this.audioWorklet = { addModule: () => Promise.reject(new DOMException('AudioWorklet is not supported', 'NotSupportedError')) };
    }
    get state() { return this._state; }
    get currentTime() { return this._f / this.sampleRate; }
    _setState(s) { if (this._state !== s) { this._state = s; this.dispatchEvent(new Event('statechange')); } }
    _activate(n) { this._active.add(n); }
    _quantum() { const o = this.destination._pull(this._f); this._f += Q; return o; }
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
    createMediaElementSource() { throw new DOMException('createMediaElementSource is not supported', 'NotSupportedError'); }
    createMediaStreamSource() { throw new DOMException('createMediaStreamSource is not supported', 'NotSupportedError'); }
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
    createMediaStreamDestination() { throw new DOMException('createMediaStreamDestination is not supported', 'NotSupportedError'); }
}

class OfflineAudioContext extends BaseAudioContext {
    constructor(c, n, r) {
        const o = typeof c === 'object' ? c : { numberOfChannels: c, length: n, sampleRate: r };
        super(o.sampleRate || RATE); this.length = o.length; this._chn = o.numberOfChannels || 1; this._state = 'suspended';
        if (this.sampleRate !== RATE) console.warn('OfflineAudioContext renders at 48000 Hz');
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
            const ev = new Event('complete'); ev.renderedBuffer = b; this.dispatchEvent(ev);
            res(b);
        }, 0));
    }
    suspend() { return Promise.resolve(); }
    resume() { return Promise.resolve(); }
}

Object.assign(globalThis, {
    AudioContext, webkitAudioContext: AudioContext, OfflineAudioContext, webkitOfflineAudioContext: OfflineAudioContext, BaseAudioContext,
    AudioNode, AudioParam, AudioBuffer, AudioDestinationNode, AudioScheduledSourceNode, GainNode, OscillatorNode, AudioBufferSourceNode,
    ConstantSourceNode, StereoPannerNode, DelayNode, BiquadFilterNode, DynamicsCompressorNode, ConvolverNode, AnalyserNode, PeriodicWave,
});
})();
