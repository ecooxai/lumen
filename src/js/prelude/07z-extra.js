
// ---- extra interfaces: rarer node/event types, media helpers ----
class ProcessingInstruction extends CharacterData {}
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
class StyleSheet {}
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
