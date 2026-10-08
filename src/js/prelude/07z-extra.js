
// ---- extra interfaces: rarer node/event types, media helpers ----
class ProcessingInstruction extends CharacterData { get target() { return N.name(this); } }
const copyInit = (ev, init, keys) => { if (init) for (const k of keys) if (k in init) ev[k] = init[k]; };
class TouchEvent extends UIEvent {
    constructor(t, i = {}) {
        super(t, i);
        this.touches = i.touches || []; this.targetTouches = i.targetTouches || []; this.changedTouches = i.changedTouches || [];
        this.altKey = this.ctrlKey = this.metaKey = this.shiftKey = false;
        copyInit(this, i, ['altKey', 'ctrlKey', 'metaKey', 'shiftKey']);
    }
}
class CompositionEvent extends UIEvent { constructor(t, i = {}) { super(t, i); this.data = i.data || ''; } }
class ClipboardEvent extends Event { constructor(t, i = {}) { super(t, i); this.clipboardData = i.clipboardData || null; } }
class DragEvent extends MouseEvent { constructor(t, i = {}) { super(t, i); this.dataTransfer = i.dataTransfer || null; } }
class StorageEvent extends Event {
    constructor(t, i = {}) {
        super(t, i);
        this.key = i.key ?? null; this.oldValue = i.oldValue ?? null; this.newValue = i.newValue ?? null;
        this.url = i.url || ''; this.storageArea = i.storageArea || null;
    }
}
class PromiseRejectionEvent extends Event { constructor(t, i = {}) { super(t, i); this.promise = i.promise; this.reason = i.reason; } }
class SubmitEvent extends Event { constructor(t, i = {}) { super(t, i); this.submitter = i.submitter || null; } }

class IdleDeadline {}
class TimeRanges {
    constructor(r = []) { this._r = r; }
    get length() { return this._r.length; }
    start(i) { if (!(i < this._r.length)) throw new DOMException('Index out of range', 'IndexSizeError'); return this._r[i][0]; }
    end(i) { if (!(i < this._r.length)) throw new DOMException('Index out of range', 'IndexSizeError'); return this._r[i][1]; }
}
class MediaError { constructor(code, message = '') { this.code = code; this.message = message; } }
Object.assign(MediaError, { MEDIA_ERR_ABORTED: 1, MEDIA_ERR_NETWORK: 2, MEDIA_ERR_DECODE: 3, MEDIA_ERR_SRC_NOT_SUPPORTED: 4 });

// ---- canvas 2D: state-tracking stub (no pixels yet) so canvas-using scripts don't crash ----
class ImageData {
    constructor(a, b, c) {
        if (a instanceof Uint8ClampedArray) { this.data = a; this.width = b; this.height = c ?? (a.length / 4 / b); }
        else { this.width = a; this.height = b; this.data = new Uint8ClampedArray(a * b * 4); }
    }
}
class Path2D { constructor() {} }
const noop = () => {};
class CanvasGradient { addColorStop() {} }
class CanvasPattern { setTransform() {} }
class CanvasRenderingContext2D {
    constructor(canvas) {
        this.canvas = canvas; this._stack = [];
        Object.assign(this, { fillStyle: '#000000', strokeStyle: '#000000', lineWidth: 1, lineCap: 'butt', lineJoin: 'miter', miterLimit: 10,
            font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic', globalAlpha: 1, globalCompositeOperation: 'source-over',
            shadowBlur: 0, shadowColor: 'rgba(0, 0, 0, 0)', shadowOffsetX: 0, shadowOffsetY: 0, imageSmoothingEnabled: true, filter: 'none', direction: 'inherit' });
    }
    save() { this._stack.push({ fillStyle: this.fillStyle, strokeStyle: this.strokeStyle, lineWidth: this.lineWidth, font: this.font, globalAlpha: this.globalAlpha }); }
    restore() { const s = this._stack.pop(); if (s) Object.assign(this, s); }
    measureText(t) {
        const px = parseFloat((/(\d+(?:\.\d+)?)px/.exec(this.font) || [0, 10])[1]);
        const w = String(t).length * px * 0.55;
        return { width: w, actualBoundingBoxLeft: 0, actualBoundingBoxRight: w, actualBoundingBoxAscent: px * 0.8, actualBoundingBoxDescent: px * 0.2, fontBoundingBoxAscent: px * 0.8, fontBoundingBoxDescent: px * 0.2 };
    }
    createLinearGradient() { return new CanvasGradient(); }
    createRadialGradient() { return new CanvasGradient(); }
    createConicGradient() { return new CanvasGradient(); }
    createPattern() { return new CanvasPattern(); }
    createImageData(w, h) { return w instanceof ImageData ? new ImageData(w.width, w.height) : new ImageData(Math.abs(w) | 0, Math.abs(h) | 0); }
    getImageData(x, y, w, h) { return new ImageData(Math.abs(w) | 0, Math.abs(h) | 0); }
    getTransform() { return new DOMMatrix(); }
    isPointInPath() { return false; }
    isPointInStroke() { return false; }
    getLineDash() { return this._dash || []; }
    setLineDash(d) { this._dash = Array.from(d); }
    getContextAttributes() { return { alpha: true, desynchronized: false, colorSpace: 'srgb', willReadFrequently: false }; }
}
for (const m of ['fillRect', 'strokeRect', 'clearRect', 'beginPath', 'closePath', 'moveTo', 'lineTo', 'bezierCurveTo', 'quadraticCurveTo', 'arc', 'arcTo',
    'ellipse', 'rect', 'roundRect', 'fill', 'stroke', 'clip', 'fillText', 'strokeText', 'drawImage', 'putImageData', 'scale', 'rotate', 'translate',
    'transform', 'setTransform', 'resetTransform', 'reset', 'drawFocusIfNeeded'])
    CanvasRenderingContext2D.prototype[m] = noop;
for (const m of ['moveTo', 'lineTo', 'bezierCurveTo', 'quadraticCurveTo', 'arc', 'arcTo', 'ellipse', 'rect', 'roundRect', 'closePath', 'addPath'])
    Path2D.prototype[m] = noop;

// ---- Media Source Extensions (fragmented MP4) ----
class SourceBufferList extends EventTarget {
    constructor() { super(); this._l = []; }
    get length() { return this._l.length; }
    item(i) { return this._l[i] ?? null; }
    [Symbol.iterator]() { return this._l[Symbol.iterator](); }
    _sync() { for (let i = 0; i < 32; i++) delete this[i]; this._l.forEach((b, i) => Object.defineProperty(this, i, { value: b, configurable: true, enumerable: true })); }
}
class SourceBuffer extends EventTarget {
    constructor(ms, idx, mime) {
        super();
        Object.assign(this, { _ms: ms, _i: idx, _mime: mime, updating: false, mode: 'segments', timestampOffset: 0, appendWindowStart: 0, appendWindowEnd: Infinity });
    }
    get buffered() { return this._ms._id ? mediaRanges(N.mediaBuffered(this._ms._id, this._i)) : new TimeRanges([]); }
    _op(fn) {
        if (this._ms.readyState === 'closed') throw new DOMException('MediaSource is closed', 'InvalidStateError');
        if (this.updating) throw new DOMException('SourceBuffer is updating', 'InvalidStateError');
        if (this._ms.readyState === 'ended') { this._ms.readyState = 'open'; this._ms.dispatchEvent(new Event('sourceopen')); }
        this.updating = true; this.dispatchEvent(new Event('updatestart'));
        setTimeout(() => {
            if (!this.updating) return;
            const ok = fn();
            this.updating = false;
            if (ok === false) { this.dispatchEvent(new Event('error')); this._ms.endOfStream('decode'); }
            else this.dispatchEvent(new Event('update'));
            this.dispatchEvent(new Event('updateend'));
        }, 0);
    }
    appendBuffer(data) {
        const copy = data instanceof ArrayBuffer ? data.slice(0) : new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice();
        this._op(() => N.mediaAppend(this._ms._id, this._i, copy));
    }
    remove(s, e) { this._op(() => { N.mediaRemove(this._ms._id, this._i, +s, +e); }); }
    abort() { if (this.updating) { this.updating = false; this.dispatchEvent(new Event('abort')); this.dispatchEvent(new Event('updateend')); } }
    changeType(t) { this._mime = String(t); }
}
class MediaSource extends EventTarget {
    constructor() { super(); this.readyState = 'closed'; this._dur = NaN; this._id = 0; this.sourceBuffers = new SourceBufferList(); this.activeSourceBuffers = this.sourceBuffers; }
    static isTypeSupported(t) { return N.mediaCanPlay(String(t), true) > 0; }
    static get canConstructInDedicatedWorker() { return false; }
    get duration() { return this._dur; }
    set duration(v) { this._dur = +v; if (this._id) N.mediaSetDuration(this._id, this._dur); }
    addSourceBuffer(t) {
        if (this.readyState !== 'open') throw new DOMException('MediaSource is not open', 'InvalidStateError');
        if (!MediaSource.isTypeSupported(t)) throw new DOMException('Unsupported type: ' + t, 'NotSupportedError');
        const i = N.mediaAddBuffer(this._id, String(t));
        if (i < 0) throw new DOMException('Too many SourceBuffers', 'QuotaExceededError');
        const sb = new SourceBuffer(this, i, String(t));
        this.sourceBuffers._l.push(sb); this.sourceBuffers._sync();
        return sb;
    }
    removeSourceBuffer(sb) { const l = this.sourceBuffers._l, k = l.indexOf(sb); if (k >= 0) { l.splice(k, 1); this.sourceBuffers._sync(); } }
    endOfStream(err) {
        if (this.readyState !== 'open') return;
        this.readyState = 'ended';
        if (!err) {
            let end = 0;
            for (const sb of this.sourceBuffers._l) { const b = sb.buffered; if (b.length) end = Math.max(end, b.end(b.length - 1)); }
            if (end > 0 && !(end <= this._dur)) this.duration = end;
            N.mediaEos(this._id);
        }
        setTimeout(() => this.dispatchEvent(new Event('sourceended')), 0);
    }
    setLiveSeekableRange() {}
    clearLiveSeekableRange() {}
    _attach(el, id) {
        this._el = el; this._id = id;
        if (!Number.isNaN(this._dur)) N.mediaSetDuration(id, this._dur);
        setTimeout(() => { this.readyState = 'open'; this.dispatchEvent(new Event('sourceopen')); }, 0);
    }
}
Promise.resolve().then(() => document.addEventListener('DOMContentLoaded', () => {
    for (const el of document.querySelectorAll('video, audio'))
        if (!el.__mp && (el.getAttribute('src') || el.querySelector('source[src]'))) { el.load(); if (el.autoplay) el.play(); }
}));
