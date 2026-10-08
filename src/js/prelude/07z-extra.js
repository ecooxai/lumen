
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
