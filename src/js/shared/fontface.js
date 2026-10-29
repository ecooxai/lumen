/* FontFace: loads url() or binary sources and registers the face with the native font registry
   (shared by the page prelude and the worker prelude) */
const FF_NAT = typeof N !== 'undefined' ? N : W;
let ffSeq = 0;
class FontFace {
    constructor(family, source, d = {}) {
        this.family = String(family); this.style = d.style || 'normal'; this.weight = String(d.weight || 'normal');
        this.stretch = d.stretch || 'normal'; this.unicodeRange = d.unicodeRange || 'U+0-10FFFF'; this.variant = d.variant || 'normal';
        this.featureSettings = d.featureSettings || 'normal'; this.display = d.display || 'auto';
        let res, rej; const p = new Promise((a, b) => { res = a; rej = b; }); p.catch(() => {});
        def(this, '_', { source, res, rej, p, started: false });
        this.status = 'unloaded';
        if (typeof source !== 'string') this.load();   /* binary data loads straight away */
    }
    get loaded() { return this._.p; }
    load() {
        const s = this._;
        if (s.started) return s.p;
        s.started = true; this.status = 'loading';
        const w = /^bold$/i.test(this.weight) ? 700 : parseInt(this.weight, 10) || 400, it = /italic|oblique/i.test(this.style);
        const fail = () => { this.status = 'error'; s.rej(new DOMException('A network error occurred.', 'NetworkError')); };
        const done = buf => {
            if (buf && FF_NAT.fontFace(this.family, w, it, 'fontface:' + (++ffSeq) + ':' + this.family, buf)) { this.status = 'loaded'; s.res(this); } else fail();
        };
        const src = s.source;
        if (typeof src !== 'string') { Promise.resolve().then(() => done(ArrayBuffer.isView(src) || src instanceof ArrayBuffer ? src : null)); return s.p; }
        const m = /url\(\s*(['"]?)([^'")]+)\1\s*\)/i.exec(src);
        if (!m) { Promise.resolve().then(fail); return s.p; }
        let u; try { u = new URL(m[2], typeof location !== 'undefined' ? String(location.href) : undefined).href; } catch (e) { Promise.resolve().then(fail); return s.p; }
        fetch(u).then(r => r.ok ? r.arrayBuffer() : null).then(done, fail);
        return s.p;
    }
}
