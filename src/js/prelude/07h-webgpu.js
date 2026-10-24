// ---- WebGPU over N.gpu (src/js/webgpu.cc, wgpu-native): descriptors go to the native side as the page wrote them,
// objects inside them carry their native id under GPU_ID; validation errors reach error scopes or uncapturederror ----
const GPU_ID = Symbol('gpu');
const GPUI = Symbol('gpu internal');
const gpuIllegal = t => { if (t !== GPUI) throw new TypeError('Illegal constructor'); };
const gpuReg = typeof FinalizationRegistry === 'function' ? new FinalizationRegistry(id => N.gpu.release(id)) : null;
const gpuTickets = new SMap();      /* ticket -> (a, message) */
const gpuDevices = new SMap();      /* device id -> WeakRef(GPUDevice), for its errors and loss */
let gpuInited = false;
const gpuErr = (name, msg) => new DOMException(msg, name);
function gpuEvent(kind, t, a, msg, type) {
    if (kind === 0) { const f = gpuTickets.get(t); if (f) { gpuTickets.delete(t); f(a, msg); } return; }
    const r = gpuDevices.get(t), d = r && r.deref();
    if (!d) return;
    if (kind === 2) { gpuDevices.delete(t); d._.lose(a ? 'destroyed' : 'unknown', msg); return; }
    const C = type === 'out-of-memory' ? GPUOutOfMemoryError : type === 'validation' ? GPUValidationError : GPUInternalError;
    dispatch(d, new GPUUncapturedErrorEvent('uncapturederror', { error: new C(msg), cancelable: true }), true);
}
const gpuInit = () => { if (!gpuInited) { gpuInited = true; N.gpu.init(gpuEvent, GPU_ID); } };
/* a promise settled by the ticket the native call returned */
const gpuTicket = (t, f) => new Promise((res, rej) => gpuTickets.set(t, (a, m) => { try { res(f(a, m)); } catch (e) { rej(e); } }));
/* a wrapper of class C over native id; released when collected unless keep */
function gpuWrap(C, id, x, keep) {
    const o = Object.create(C.prototype);
    Object.defineProperty(o, GPU_ID, { value: id });
    def(o, '_', Object.assign({ label: '' }, x));
    if (gpuReg && !keep && id) gpuReg.register(o, id);
    return o;
}
const gpuLabel = (o, d) => { if (d && d.label !== undefined) o._.label = String(d.label); return o; };
const gpuLabeled = C => Object.defineProperty(C.prototype, 'label', { get() { return this._.label; }, set(v) { this._.label = String(v); }, configurable: true, enumerable: true });
const gpuRO = (C, keys) => { for (const k of keys) Object.defineProperty(C.prototype, k, { get() { return this._[k]; }, configurable: true, enumerable: true }); };
const gpuDict = d => d === undefined || d === null ? {} : typeof d === 'object' ? d : (() => { throw new TypeError('The descriptor is not a dictionary.'); })();

const GPU_FEATURES = 'core-features-and-limits depth-clip-control depth32float-stencil8 texture-compression-bc texture-compression-bc-sliced-3d texture-compression-etc2 texture-compression-astc texture-compression-astc-sliced-3d timestamp-query indirect-first-instance shader-f16 rg11b10ufloat-renderable bgra8unorm-storage float32-filterable float32-blendable clip-distances dual-source-blending subgroups texture-formats-tier1 texture-formats-tier2 primitive-index texture-component-swizzle'.split(' ');
const GPU_FEATURE_OF = new SMap(GPU_FEATURES.map(f => [f.replace(/-/g, ''), f]));
const GPU_LIMITS = 'maxTextureDimension1D maxTextureDimension2D maxTextureDimension3D maxTextureArrayLayers maxBindGroups maxBindGroupsPlusVertexBuffers maxBindingsPerBindGroup maxDynamicUniformBuffersPerPipelineLayout maxDynamicStorageBuffersPerPipelineLayout maxSampledTexturesPerShaderStage maxSamplersPerShaderStage maxStorageBuffersPerShaderStage maxStorageTexturesPerShaderStage maxUniformBuffersPerShaderStage maxUniformBufferBindingSize maxStorageBufferBindingSize minUniformBufferOffsetAlignment minStorageBufferOffsetAlignment maxVertexBuffers maxBufferSize maxVertexAttributes maxVertexBufferArrayStride maxInterStageShaderVariables maxColorAttachments maxColorAttachmentBytesPerSample maxComputeWorkgroupStorageSize maxComputeInvocationsPerWorkgroup maxComputeWorkgroupSizeX maxComputeWorkgroupSizeY maxComputeWorkgroupSizeZ maxComputeWorkgroupsPerDimension'.split(' ');

class GPUSupportedFeatures {
    constructor(t) { gpuIllegal(t); }
    get size() { return this._.size; }
    has(k) { return this._.has(String(k)); }
    keys() { return this._.values(); }
    values() { return this._.values(); }
    entries() { return this._.entries(); }
    forEach(f, t) { for (const k of this._) f.call(t, k, k, this); }
    [Symbol.iterator]() { return this._.values(); }
}
const gpuFeatures = names => { const o = Object.create(GPUSupportedFeatures.prototype); def(o, '_', new SSet((names || []).map(n => GPU_FEATURE_OF.get(n)).filter(Boolean))); return o; };
class WGSLLanguageFeatures extends GPUSupportedFeatures {}
class GPUSupportedLimits { constructor(t) { gpuIllegal(t); } }
gpuRO(GPUSupportedLimits, GPU_LIMITS);
const gpuLimits = l => { const o = Object.create(GPUSupportedLimits.prototype); def(o, '_', Object.freeze(Object.assign({}, l))); return o; };
class GPUAdapterInfo { constructor(t) { gpuIllegal(t); } }
gpuRO(GPUAdapterInfo, ['vendor', 'architecture', 'device', 'description', 'subgroupMinSize', 'subgroupMaxSize', 'isFallbackAdapter']);
const gpuInfo = i => { const o = Object.create(GPUAdapterInfo.prototype); def(o, '_', { vendor: i[0], architecture: i[1], device: '', description: '', isFallbackAdapter: !!i[2], subgroupMinSize: i[3], subgroupMaxSize: i[4] }); return o; };

class GPUError { constructor(m) { def(this, '_m', m === undefined ? '' : String(m)); } get message() { return this._m; } }
class GPUValidationError extends GPUError {}
class GPUOutOfMemoryError extends GPUError {}
class GPUInternalError extends GPUError {}
class GPUPipelineError extends DOMException {
    constructor(m, o) { super(m === undefined ? '' : String(m), 'GPUPipelineError'); def(this, '_r', o && o.reason === 'internal' ? 'internal' : 'validation'); }
    get reason() { return this._r; }
}
class GPUUncapturedErrorEvent extends Event {
    constructor(t, i) { if (!i || !(i.error instanceof GPUError)) throw new TypeError("Failed to construct 'GPUUncapturedErrorEvent': required member error is undefined."); super(t, i); def(this, '_e', i.error); }
    get error() { return this._e; }
}
class GPUDeviceLostInfo { constructor(t, r, m) { gpuIllegal(t); def(this, '_', { reason: r, message: m || '' }); } }
gpuRO(GPUDeviceLostInfo, ['reason', 'message']);
class GPUCompilationMessage { constructor(t, m) { gpuIllegal(t); def(this, '_', m); } }
gpuRO(GPUCompilationMessage, ['message', 'type', 'lineNum', 'linePos', 'offset', 'length']);
class GPUCompilationInfo { constructor(t, m) { gpuIllegal(t); def(this, '_', m); } get messages() { return this._; } }

const GPUBufferUsage = Object.freeze({ MAP_READ: 1, MAP_WRITE: 2, COPY_SRC: 4, COPY_DST: 8, INDEX: 16, VERTEX: 32, UNIFORM: 64, STORAGE: 128, INDIRECT: 256, QUERY_RESOLVE: 512 });
const GPUTextureUsage = Object.freeze({ COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, STORAGE_BINDING: 8, RENDER_ATTACHMENT: 16 });
const GPUShaderStage = Object.freeze({ VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 });
const GPUMapMode = Object.freeze({ READ: 1, WRITE: 2 });
const GPUColorWrite = Object.freeze({ RED: 1, GREEN: 2, BLUE: 4, ALPHA: 8, ALL: 15 });

class GPU {
    constructor(t) { gpuIllegal(t); }
    requestAdapter(o) {
        return new Promise((res) => {
            o = gpuDict(o);
            if (!N.gpu) { res(null); return; }
            gpuInit();
            const t = N.gpu.requestAdapter(o.powerPreference, !!o.forceFallbackAdapter);
            if (!t) { res(null); return; }
            gpuTickets.set(t, a => res(a ? gpuAdapter(a) : null));
        });
    }
    getPreferredCanvasFormat() { return 'bgra8unorm'; }
    get wgslLanguageFeatures() { return this._wf || (def(this, '_wf', Object.setPrototypeOf(gpuFeatures([]), WGSLLanguageFeatures.prototype)), this._wf); }
}
class GPUAdapter { constructor(t) { gpuIllegal(t); } }
gpuRO(GPUAdapter, ['features', 'limits', 'info', 'isFallbackAdapter']);
const gpuAdapter = id => {
    const info = gpuInfo(N.gpu.adapterInfo(id) || ['', '', false, 0, 0]);
    return gpuWrap(GPUAdapter, id, { features: gpuFeatures(N.gpu.features(id)), limits: gpuLimits(N.gpu.limits(id) || {}), info, isFallbackAdapter: info.isFallbackAdapter });
};
GPUAdapter.prototype.requestAdapterInfo = function () { return Promise.resolve(this._.info); };
GPUAdapter.prototype.requestDevice = function (d) {
    const ad = this;
    return new Promise((res, rej) => {
        d = gpuDict(d);
        const feats = d.requiredFeatures === undefined ? [] : [...d.requiredFeatures].map(String);
        for (const f of feats) if (!ad._.features.has(f)) throw new TypeError(`Failed to execute 'requestDevice' on 'GPUAdapter': Unsupported feature: ${f}`);
        const lim = {};
        if (d.requiredLimits !== undefined && d.requiredLimits !== null) for (const k of Object.keys(d.requiredLimits)) {
            const v = d.requiredLimits[k];
            if (v === undefined) continue;
            if (!GPU_LIMITS.includes(k)) throw gpuErr('OperationError', `The limit "${k}" is not recognized.`);
            const have = ad._.limits[k];
            if (k.startsWith('min') ? v < have : v > have) throw gpuErr('OperationError', `The limit "${k}" exceeds the adapter's.`);
            lim[k] = v;
        }
        const t = N.gpu.requestDevice(ad[GPU_ID], feats, lim);
        if (!t) throw gpuErr('OperationError', 'The device could not be created.');
        gpuTickets.set(t, (id, m) => {
            if (!id) { rej(gpuErr('OperationError', m || 'The device could not be created.')); return; }
            res(gpuDevice(id, ad, d));
        });
    });
};

class GPUDevice extends EventTarget { constructor(t) { gpuIllegal(t); super(); } }
gpuRO(GPUDevice, ['features', 'limits', 'adapterInfo', 'queue', 'lost']);
gpuLabeled(GPUDevice);
Object.defineProperty(GPUDevice.prototype, 'onuncapturederror', { get() { return this._.on || null; }, set(f) { this._.on = typeof f === 'function' ? f : null; }, configurable: true, enumerable: true });
const gpuDevice = (id, ad, d) => {
    const dev = Object.create(GPUDevice.prototype);
    Object.defineProperty(dev, GPU_ID, { value: id });
    let lose;
    const lost = new Promise(r => { lose = r; });
    def(dev, '_', { label: '', features: gpuFeatures(N.gpu.features(id)), limits: gpuLimits(N.gpu.limits(id) || {}), adapterInfo: ad._.info, lost, on: null, gone: false,
        lose(reason, m) { if (this.gone) return; this.gone = true; lose(new GPUDeviceLostInfo(GPUI, reason, m)); } });
    dev._.queue = gpuWrap(GPUQueue, id, { dev }, true);
    gpuLabel(dev, d);
    if (d.defaultQueue && d.defaultQueue.label !== undefined) dev._.queue._.label = String(d.defaultQueue.label);
    gpuDevices.set(id, new WeakRef(dev));
    if (gpuReg) gpuReg.register(dev, id);
    return dev;
};
const gpuMk = (dev, C, id, d, x) => gpuLabel(gpuWrap(C, id, Object.assign({ dev }, x)), d);
Object.assign(GPUDevice.prototype, {
    destroy() { const id = this[GPU_ID]; N.gpu.destroy(id); gpuDevices.delete(id); this._.lose('destroyed', ''); },
    createBuffer(d) {
        d = gpuDict(d);
        const id = N.gpu.createBuffer(this[GPU_ID], d);
        return gpuMk(this, GPUBuffer, id, d, { size: Number(d.size), usage: Number(d.usage), mapState: d.mappedAtCreation ? 'mapped' : 'unmapped', pend: null });
    },
    createTexture(d) {
        d = gpuDict(d);
        const id = N.gpu.createTexture(this[GPU_ID], d);
        const s = d.size, a = Array.isArray(s) || (s && typeof s[Symbol.iterator] === 'function') ? [...s] : null;
        const w = a ? a[0] : s.width, h = a ? a[1] ?? 1 : s.height ?? 1, z = a ? a[2] ?? 1 : s.depthOrArrayLayers ?? 1;
        return gpuMk(this, GPUTexture, id, d, { width: w, height: h, depthOrArrayLayers: z, mipLevelCount: d.mipLevelCount ?? 1, sampleCount: d.sampleCount ?? 1,
            dimension: d.dimension ?? '2d', format: d.format, usage: Number(d.usage), textureBindingViewDimension: d.textureBindingViewDimension });
    },
    createSampler(d) { d = gpuDict(d); return gpuMk(this, GPUSampler, N.gpu.createSampler(this[GPU_ID], d), d); },
    importExternalTexture() { throw gpuErr('NotSupportedError', 'External textures are not supported.'); },
    createBindGroupLayout(d) { d = gpuDict(d); return gpuMk(this, GPUBindGroupLayout, N.gpu.createBindGroupLayout(this[GPU_ID], d), d); },
    createPipelineLayout(d) { d = gpuDict(d); return gpuMk(this, GPUPipelineLayout, N.gpu.createPipelineLayout(this[GPU_ID], d), d); },
    createBindGroup(d) { d = gpuDict(d); return gpuMk(this, GPUBindGroup, N.gpu.createBindGroup(this[GPU_ID], d), d); },
    createShaderModule(d) { d = gpuDict(d); return gpuMk(this, GPUShaderModule, N.gpu.createShaderModule(this[GPU_ID], d), d); },
    createComputePipeline(d) { d = gpuDict(d); return gpuMk(this, GPUComputePipeline, N.gpu.createComputePipeline(this[GPU_ID], d, false), d); },
    createRenderPipeline(d) { d = gpuDict(d); return gpuMk(this, GPURenderPipeline, N.gpu.createRenderPipeline(this[GPU_ID], d, false), d); },
    createComputePipelineAsync(d) { return gpuPipeAsync(this, d, GPUComputePipeline, 'createComputePipeline'); },
    createRenderPipelineAsync(d) { return gpuPipeAsync(this, d, GPURenderPipeline, 'createRenderPipeline'); },
    createCommandEncoder(d) { d = gpuDict(d); return gpuMk(this, GPUCommandEncoder, N.gpu.createCommandEncoder(this[GPU_ID]), d); },
    createRenderBundleEncoder(d) { d = gpuDict(d); return gpuLabel(gpuWrap(GPURenderBundleEncoder, 0, { dev: this, cmds: [], done: false }), d); },
    createQuerySet(d) { d = gpuDict(d); return gpuMk(this, GPUQuerySet, N.gpu.createQuerySet(this[GPU_ID], d), d, { type: d.type, count: d.count }); },
    pushErrorScope(f) { N.gpu.pushErrorScope(this[GPU_ID], f); },
    popErrorScope() {
        return new Promise((res, rej) => {
            const r = N.gpu.popErrorScope(this[GPU_ID]);
            if (r === undefined) { if (this._.gone) res(null); else rej(gpuErr('OperationError', 'The error scope stack is empty.')); return; }
            if (r === null) { res(null); return; }
            res(new (r[0] === 'out-of-memory' ? GPUOutOfMemoryError : r[0] === 'validation' ? GPUValidationError : GPUInternalError)(r[1]));
        });
    },
});
/* the pipeline is built now, on this thread; the promise settles like the browsers' asynchronous creation */
const gpuPipeAsync = (dev, d, C, fn) => new Promise((res, rej) => {
    d = gpuDict(d);
    const r = N.gpu[fn](dev[GPU_ID], d, true);
    if (Array.isArray(r)) rej(new GPUPipelineError(r[0], { reason: r[1] ? 'internal' : 'validation' }));
    else res(gpuMk(dev, C, r, d));
});

class GPUBuffer { constructor(t) { gpuIllegal(t); } }
gpuRO(GPUBuffer, ['size', 'usage', 'mapState']);
gpuLabeled(GPUBuffer);
Object.assign(GPUBuffer.prototype, {
    mapAsync(mode, off, size) {
        const s = this._;
        if (s.mapState !== 'unmapped') return Promise.reject(gpuErr('OperationError', 'The buffer is already mapped or being mapped.'));
        const t = N.gpu.mapAsync(this[GPU_ID], mode, off || 0, size);
        if (!t) return Promise.reject(gpuErr('OperationError', 'The buffer could not be mapped.'));
        s.mapState = 'pending';
        const p = {};
        s.pend = p;
        return new Promise((res, rej) => {
            p.rej = rej;
            gpuTickets.set(t, ok => {
                if (s.pend !== p) return;   /* unmapped or destroyed meanwhile: already rejected */
                s.pend = null;
                if (ok) { s.mapState = 'mapped'; res(undefined); }
                else { s.mapState = 'unmapped'; rej(gpuErr('OperationError', 'The buffer could not be mapped.')); }
            });
        });
    },
    getMappedRange(off, size) {
        const r = this._.mapState === 'mapped' ? N.gpu.getMappedRange(this[GPU_ID], off || 0, size) : undefined;
        if (r === undefined) throw gpuErr('OperationError', 'The range is not mapped or overlaps one already returned.');
        return r;
    },
    unmap() { this._abort(); N.gpu.unmap(this[GPU_ID]); this._.mapState = 'unmapped'; },
    destroy() { this._abort(); N.gpu.destroy(this[GPU_ID]); this._.mapState = 'unmapped'; },
    _abort() { const s = this._; if (s.pend) { const p = s.pend; s.pend = null; p.rej(gpuErr('AbortError', 'The mapping was aborted.')); } },
});
class GPUTexture { constructor(t) { gpuIllegal(t); } }
gpuRO(GPUTexture, ['width', 'height', 'depthOrArrayLayers', 'mipLevelCount', 'sampleCount', 'dimension', 'format', 'usage', 'textureBindingViewDimension']);
gpuLabeled(GPUTexture);
Object.assign(GPUTexture.prototype, {
    createView(d) { d = gpuDict(d); return gpuMk(this._.dev, GPUTextureView, N.gpu.createView(this[GPU_ID], d), d, { tex: this }); },
    destroy() { N.gpu.destroy(this[GPU_ID]); },
});
const gpuPlain = names => { const r = []; for (const n of names) { const C = { [n]: class { constructor(t) { gpuIllegal(t); } } }[n]; gpuLabeled(C); r.push(C); } return r; };
const [GPUTextureView, GPUSampler, GPUBindGroupLayout, GPUPipelineLayout, GPUBindGroup, GPUCommandBuffer, GPURenderBundle, GPUExternalTexture] =
    gpuPlain(['GPUTextureView', 'GPUSampler', 'GPUBindGroupLayout', 'GPUPipelineLayout', 'GPUBindGroup', 'GPUCommandBuffer', 'GPURenderBundle', 'GPUExternalTexture']);
class GPUQuerySet { constructor(t) { gpuIllegal(t); } destroy() { N.gpu.destroy(this[GPU_ID]); } }
gpuRO(GPUQuerySet, ['type', 'count']);
gpuLabeled(GPUQuerySet);
class GPUShaderModule {
    constructor(t) { gpuIllegal(t); }
    getCompilationInfo() {
        const e = N.gpu.shaderInfo(this[GPU_ID]) || '', ms = [];
        if (e) {
            const m = /:(\d+):(\d+)/.exec(e);
            ms.push(new GPUCompilationMessage(GPUI, { message: e, type: 'error', lineNum: m ? +m[1] : 0, linePos: m ? +m[2] : 0, offset: 0, length: 0 }));
        }
        return Promise.resolve(new GPUCompilationInfo(GPUI, Object.freeze(ms)));
    }
}
gpuLabeled(GPUShaderModule);
const gpuPipeProto = { getBindGroupLayout(i) { return gpuWrap(GPUBindGroupLayout, N.gpu.getBindGroupLayout(this[GPU_ID], i), { dev: this._.dev }); } };
class GPUComputePipeline { constructor(t) { gpuIllegal(t); } }
class GPURenderPipeline { constructor(t) { gpuIllegal(t); } }
for (const C of [GPUComputePipeline, GPURenderPipeline]) { gpuLabeled(C); Object.assign(C.prototype, gpuPipeProto); }

class GPUCommandEncoder { constructor(t) { gpuIllegal(t); } }
gpuLabeled(GPUCommandEncoder);
const gpuDebug = { pushDebugGroup() {}, popDebugGroup() {}, insertDebugMarker() {} };
Object.assign(GPUCommandEncoder.prototype, gpuDebug, {
    beginRenderPass(d) { d = gpuDict(d); return gpuLabel(gpuWrap(GPURenderPassEncoder, N.gpu.beginRenderPass(this[GPU_ID], d), { dev: this._.dev }), d); },
    beginComputePass(d) { d = gpuDict(d); return gpuLabel(gpuWrap(GPUComputePassEncoder, N.gpu.beginComputePass(this[GPU_ID], d), { dev: this._.dev }), d); },
    copyBufferToBuffer(s, so, d, doff, n) {
        if (arguments.length <= 3) { N.gpu.copyBufferToBuffer(this[GPU_ID], s, 0, so, 0, d === undefined ? s.size : d); return; }
        N.gpu.copyBufferToBuffer(this[GPU_ID], s, so, d, doff, n === undefined ? Math.max(0, s.size - so) : n);
    },
    copyBufferToTexture(s, d, z) { N.gpu.copyBufferToTexture(this[GPU_ID], s, d, z); },
    copyTextureToBuffer(s, d, z) { N.gpu.copyTextureToBuffer(this[GPU_ID], s, d, z); },
    copyTextureToTexture(s, d, z) { N.gpu.copyTextureToTexture(this[GPU_ID], s, d, z); },
    clearBuffer(b, off, size) { N.gpu.clearBuffer(this[GPU_ID], b, off || 0, size); },
    resolveQuerySet(q, first, n, d, off) { N.gpu.resolveQuerySet(this[GPU_ID], q, first, n, d, off); },
    finish(d) { d = gpuDict(d); return gpuMk(this._.dev, GPUCommandBuffer, N.gpu.finish(this[GPU_ID]), d); },
});
/* the commands both render passes and render bundles take, as [name, ...args] for replaying a bundle */
const gpuRenderCmds = {
    setPipeline(p) { this._cmd('setPipeline', p); },
    setBindGroup(i, g, o, st, n) {
        if (o === undefined) this._cmd('setBindGroup', i, g, [], 0, 0);
        else if (ArrayBuffer.isView(o)) this._cmd('setBindGroup', i, g, Array.from(o.subarray(st || 0, n === undefined ? undefined : (st || 0) + n)), 0, n === undefined ? o.length - (st || 0) : n);
        else { const a = [...o]; this._cmd('setBindGroup', i, g, a, 0, a.length); }
    },
    setVertexBuffer(slot, b, off, size) { this._cmd('setVertexBuffer', slot, b ?? null, off || 0, size); },
    setIndexBuffer(b, f, off, size) { this._cmd('setIndexBuffer', b, f, off || 0, size); },
    draw(v, i, fv, fi) { this._cmd('draw', v, i ?? 1, fv || 0, fi || 0); },
    drawIndexed(n, i, fi, bv, fin) { this._cmd('drawIndexed', n, i ?? 1, fi || 0, bv || 0, fin || 0); },
    drawIndirect(b, off) { this._cmd('drawIndirect', b, off); },
    drawIndexedIndirect(b, off) { this._cmd('drawIndexedIndirect', b, off); },
};
class GPURenderPassEncoder { constructor(t) { gpuIllegal(t); } }
gpuLabeled(GPURenderPassEncoder);
Object.assign(GPURenderPassEncoder.prototype, gpuDebug, gpuRenderCmds, {
    _cmd(f, ...a) { N.gpu[f](this[GPU_ID], ...a); },
    setViewport(x, y, w, h, a, b) { N.gpu.setViewport(this[GPU_ID], x, y, w, h, a, b); },
    setScissorRect(x, y, w, h) { N.gpu.setScissorRect(this[GPU_ID], x, y, w, h); },
    setBlendConstant(c) { N.gpu.setBlendConstant(this[GPU_ID], c); },
    setStencilReference(r) { N.gpu.setStencilReference(this[GPU_ID], r); },
    beginOcclusionQuery(i) { N.gpu.beginOcclusionQuery(this[GPU_ID], i); },
    endOcclusionQuery() { N.gpu.endOcclusionQuery(this[GPU_ID]); },
    executeBundles(bs) {
        const id = this[GPU_ID];
        for (const b of bs) {
            if (!(b instanceof GPURenderBundle)) throw new TypeError("Failed to execute 'executeBundles': The provided value is not of type 'GPURenderBundle'.");
            for (const c of b._.cmds) N.gpu[c[0]](id, ...c.slice(1));
        }
    },
    end() { N.gpu.end(this[GPU_ID]); },
});
class GPUComputePassEncoder { constructor(t) { gpuIllegal(t); } }
gpuLabeled(GPUComputePassEncoder);
Object.assign(GPUComputePassEncoder.prototype, gpuDebug, {
    _cmd(f, ...a) { N.gpu[f](this[GPU_ID], ...a); },
    setPipeline: gpuRenderCmds.setPipeline,
    setBindGroup: gpuRenderCmds.setBindGroup,
    dispatchWorkgroups(x, y, z) { N.gpu.dispatchWorkgroups(this[GPU_ID], x, y ?? 1, z ?? 1); },
    dispatchWorkgroupsIndirect(b, off) { N.gpu.dispatchWorkgroupsIndirect(this[GPU_ID], b, off); },
    end() { N.gpu.end(this[GPU_ID]); },
});
/* render bundles are recorded here and replayed into the pass that executes them */
class GPURenderBundleEncoder { constructor(t) { gpuIllegal(t); } }
gpuLabeled(GPURenderBundleEncoder);
Object.assign(GPURenderBundleEncoder.prototype, gpuDebug, gpuRenderCmds, {
    _cmd(...c) { if (this._.done) throw gpuErr('InvalidStateError', 'The render bundle encoder has finished.'); this._.cmds.push(c); },
    finish(d) { this._.done = true; return gpuLabel(gpuWrap(GPURenderBundle, 0, { dev: this._.dev, cmds: this._.cmds }), gpuDict(d)); },
});

class GPUQueue { constructor(t) { gpuIllegal(t); } }
gpuLabeled(GPUQueue);
Object.assign(GPUQueue.prototype, {
    submit(cbs) { N.gpu.submit(this[GPU_ID], [...cbs]); },
    onSubmittedWorkDone() {
        const t = N.gpu.onSubmittedWorkDone(this[GPU_ID]);
        return t ? gpuTicket(t, () => undefined) : Promise.resolve();
    },
    writeBuffer(b, off, data, doff, size) { N.gpu.writeBuffer(this[GPU_ID], b, off, data, doff || 0, size); },
    writeTexture(d, data, layout, size) { N.gpu.writeTexture(this[GPU_ID], d, data, layout, size); },
    copyExternalImageToTexture(src, dst, size) {
        src = gpuDict(src);
        let s = src.source, flip = !!src.flipY;
        const o = src.origin, ox = o ? (Array.isArray(o) ? o[0] : o.x) || 0 : 0, oy = o ? (Array.isArray(o) ? o[1] : o.y) || 0 : 0;
        const w = Array.isArray(size) ? size[0] : size.width, h = Array.isArray(size) ? size[1] ?? 1 : size.height ?? 1;
        if (s instanceof ImageBitmap) { if (!s._.src) throw gpuErr('InvalidStateError', 'The ImageBitmap is closed.'); flip = flip !== s._.flipY; s = s._.src; }
        const premul = !!(dst && dst.premultipliedAlpha);
        if (s instanceof ImageData) N.gpu.copyExternalImageToTexture(this[GPU_ID], s.data, s.width, s.height, ox, oy, flip, dst, premul, w, h);
        else if (s instanceof Element) N.gpu.copyExternalImageToTexture(this[GPU_ID], s, 0, 0, ox, oy, flip, dst, premul, w, h);
        else throw new TypeError("Failed to execute 'copyExternalImageToTexture': The source is not an image, video, canvas, ImageBitmap or ImageData.");
    },
});

/* canvas.getContext('webgpu'): getCurrentTexture hands out the canvas's texture for this frame and presents it once the task's microtasks ran */
class GPUCanvasContext { constructor(t) { gpuIllegal(t); } get canvas() { return this._.cv; } }
Object.assign(GPUCanvasContext.prototype, {
    configure(c) {
        const s = this._;
        c = gpuDict(c);
        if (!(c.device instanceof GPUDevice)) throw new TypeError("Failed to execute 'configure' on 'GPUCanvasContext': required member device is undefined.");
        const usage = c.usage === undefined ? 0x10 : Number(c.usage), am = c.alphaMode === 'premultiplied' ? 'premultiplied' : 'opaque', vf = c.viewFormats === undefined ? [] : [...c.viewFormats];
        s.cid = N.gpu.canvasConfigure(s.cv, c.device, c.format, usage, am === 'opaque', vf, s.cid || 0) || 0;
        s.cfg = { device: c.device, format: c.format, usage, viewFormats: vf, colorSpace: c.colorSpace || 'srgb', toneMapping: c.toneMapping || { mode: 'standard' }, alphaMode: am };
        s.tex = null; s.texId = 0;
    },
    unconfigure() { const s = this._; if (s.cid) N.gpu.canvasUnconfigure(s.cid); s.cid = 0; s.cfg = null; s.tex = null; s.texId = 0; },
    getConfiguration() { const c = this._.cfg; return c ? Object.assign({}, c, { viewFormats: c.viewFormats.slice() }) : null; },
    getCurrentTexture() {
        const s = this._;
        if (!s.cfg) throw gpuErr('InvalidStateError', 'The canvas context is not configured.');
        if (s.tex) return s.tex;
        const w = s.cv.width, h = s.cv.height, id = N.gpu.canvasTexture(s.cid, w, h) || 0, c = s.cfg;
        s.tex = gpuWrap(GPUTexture, id, { dev: c.device, width: Math.max(w, 1), height: Math.max(h, 1), depthOrArrayLayers: 1, mipLevelCount: 1, sampleCount: 1, dimension: '2d', format: c.format, usage: c.usage }, true);
        queueMicrotask(s.present);
        return s.tex;
    },
});
function gpuCanvasContext(cv) {
    const g = Object.create(GPUCanvasContext.prototype), s = { cv, cid: 0, cfg: null, tex: null };
    s.present = () => { if (s.tex && s.cid) N.gpu.canvasPresent(s.cid); s.tex = null; };
    def(g, '_', s);
    return g;
}
const gpuNavigator = N.gpu ? Object.create(GPU.prototype) : undefined;
