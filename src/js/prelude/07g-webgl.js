/*@shared webgl.js*/
// page only: ImageBitmap, createImageBitmap and canvas streams
/* ImageBitmap: an ImageData snapshot of the source taken by c2dBitmap (shared/canvas2d.js), with crop, resize
   and orientation applied; WebGL and WebGPU uploads read its pixels. */
class ImageBitmap {
    constructor(t) { if (t !== GLI) glIllegal(); }
    get width() { return this._.w; }
    get height() { return this._.h; }
    close() { this._.src = null; this._.w = this._.h = 0; }
}
const bmpSize = s => s instanceof ImageData ? [s.width, s.height] : s instanceof ImageBitmap ? [s._.w, s._.h]
    : s instanceof H.HTMLImageElement ? [s.naturalWidth, s.naturalHeight] : s instanceof H.HTMLVideoElement ? [s.videoWidth, s.videoHeight]
    : s instanceof H.HTMLCanvasElement ? [s.width, s.height] : null;
/* decoded natively off the main thread to straight-alpha pixels */
async function bmpDecode(buf) {
    const id = N.imgDecode(buf);
    let r = id ? undefined : null;
    while (r === undefined) { await new Promise(f => setTimeout(f, 4)); r = N.imgTake(id); }
    return r;
}
function createImageBitmap(src, ...a) {
    if (!arguments.length) return Promise.reject(new TypeError("Failed to execute 'createImageBitmap' on 'Window': 1 argument required, but only 0 present."));
    if (!(src instanceof Blob || src instanceof ImageData || src instanceof ImageBitmap || bmpSize(src) || src instanceof SVGImageElement || (typeof OffscreenCanvas !== 'undefined' && src instanceof OffscreenCanvas)))
        return Promise.reject(new TypeError("Failed to execute 'createImageBitmap' on 'Window': The provided value is not of type '(Blob or HTMLCanvasElement or HTMLImageElement or HTMLVideoElement or ImageBitmap or ImageData or OffscreenCanvas or SVGImageElement or VideoFrame)'."));
    return c2dBitmap(src, a, bmpDecode);
}

const canvasResized = cv => { if (cv.__ck === 'webgl' || cv.__ck === 'webgl2') cv.__cx._resize(cv.width, cv.height); else if (cv.__ck === '2d') cv.__cx._reset(cv.width, cv.height); };
/* canvas.captureStream(fps): a video track over the context's live source, opened once the canvas has a WebGL or WebGPU context */
function canvasStream(cv, fps) {
    const hub = {
        refs: 1, deviceId: '',
        get src() {
            const k = cv.__ck, x = cv.__cx;
            if (k === 'webgl' || k === 'webgl2') return x._.lost ? 0 : N.gl.capture(x._.id);
            if (k === 'webgpu') return x._.cid ? N.gpu.canvasCapture(x._.cid) : 0;
            return 0;
        },
        info() { return [cv.width, cv.height, fps > 0 ? +fps : 30, 0]; },
        release() { this.refs--; },
    };
    const t = new MediaStreamTrack(RTC_INTERNAL, 'video', '');
    t._.hub = hub;
    def(t, 'canvas', cv);
    def(t, 'requestFrame', () => {});
    return new MediaStream([t]);
}
