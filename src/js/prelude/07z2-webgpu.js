/* WebGPU: thin JS objects over native handles (N.gpu*). Command encoders record a list replayed natively on submit. */
const GPUBufferUsage = Object.freeze({ MAP_READ: 1, MAP_WRITE: 2, COPY_SRC: 4, COPY_DST: 8, INDEX: 16, VERTEX: 32, UNIFORM: 64, STORAGE: 128, INDIRECT: 256, QUERY_RESOLVE: 512 });
const GPUTextureUsage = Object.freeze({ COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, STORAGE_BINDING: 8, RENDER_ATTACHMENT: 16 });
const GPUShaderStage = Object.freeze({ VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 });
const GPUMapMode = Object.freeze({ READ: 1, WRITE: 2 });
const GPUColorWrite = Object.freeze({ RED: 1, GREEN: 2, BLUE: 4, ALPHA: 8, ALL: 15 });
const gpuId = o => o == null ? 0 : typeof o === 'number' ? o : o._id || 0;
const gpuReg = new FinalizationRegistry(id => N.gpuDrop(id));
class GPUObjectBase { constructor(id, label) { this._id = id; this.label = label || ''; if (id) gpuReg.register(this, id, this); } _drop() { if (this._id) { gpuReg.unregister(this); N.gpuDrop(this._id); this._id = 0; } } }
class GPUSupportedLimits { constructor() { Object.assign(this, { maxTextureDimension1D: 8192, maxTextureDimension2D: 8192, maxTextureDimension3D: 2048, maxTextureArrayLayers: 256, maxBindGroups: 4, maxBindingsPerBindGroup: 1000, maxDynamicUniformBuffersPerPipelineLayout: 8, maxDynamicStorageBuffersPerPipelineLayout: 4, maxSampledTexturesPerShaderStage: 16, maxSamplersPerShaderStage: 16, maxStorageBuffersPerShaderStage: 8, maxStorageTexturesPerShaderStage: 4, maxUniformBuffersPerShaderStage: 12, maxUniformBufferBindingSize: 65536, maxStorageBufferBindingSize: 134217728, minUniformBufferOffsetAlignment: 256, minStorageBufferOffsetAlignment: 256, maxVertexBuffers: 8, maxBufferSize: 268435456, maxVertexAttributes: 16, maxVertexBufferArrayStride: 2048, maxInterStageShaderVariables: 16, maxColorAttachments: 8, maxColorAttachmentBytesPerSample: 32, maxComputeWorkgroupStorageSize: 16384, maxComputeInvocationsPerWorkgroup: 256, maxComputeWorkgroupSizeX: 256, maxComputeWorkgroupSizeY: 256, maxComputeWorkgroupSizeZ: 64, maxComputeWorkgroupsPerDimension: 65535 }); } }
class GPUSupportedFeatures extends Set {}
class GPUAdapterInfo { constructor(d) { this.vendor = ''; this.architecture = ''; this.device = d || ''; this.description = d || ''; } }
class GPUBuffer extends GPUObjectBase {
    constructor(dev, d) { super(N.gpuBuffer(d.size, d.usage, !!d.mappedAtCreation), d.label); this.size = d.size; this.usage = d.usage; this.mapState = d.mappedAtCreation ? 'mapped' : 'unmapped'; this._created = !!d.mappedAtCreation; this._maps = []; if (d.mappedAtCreation) this._mapOff = 0, this._mapMode = GPUMapMode.WRITE; }
    async mapAsync(mode, off = 0, size) { size = size ?? this.size - off; this.mapState = 'pending'; await null; this._mapMode = mode; this._mapOff = off; this._mapSize = size; this._readData = mode & GPUMapMode.READ ? N.gpuMapRead(this._id, off, size) : null; this.mapState = 'mapped'; }
    getMappedRange(off, size) { off = off ?? this._mapOff ?? 0; size = size ?? this.size - off; let ab; if (this._readData) ab = this._readData.slice(off - this._mapOff, off - this._mapOff + size); else ab = new ArrayBuffer(size); this._maps.push([off, ab]); return ab; }
    unmap() { if (this.mapState !== 'mapped') return; if (this._mapMode & GPUMapMode.WRITE || this._created) { for (const [off, ab] of this._maps) N.gpuUnmapWrite(this._id, off, new Uint8Array(ab), this._created); if (!this._maps.length) N.gpuUnmapWrite(this._id, 0, null, this._created); } this._maps = []; this._readData = null; this._created = false; this.mapState = 'unmapped'; }
    destroy() { this._drop(); }
}
class GPUTexture extends GPUObjectBase {
    constructor(dev, d, id) { const s = Array.isArray(d.size) ? { width: d.size[0], height: d.size[1] ?? 1, depthOrArrayLayers: d.size[2] ?? 1 } : d.size; super(id || N.gpuTexture(s.width, s.height || 1, s.depthOrArrayLayers || 1, d.format, d.usage, d.mipLevelCount || 1, d.sampleCount || 1, d.dimension || '2d'), d.label);
        Object.assign(this, { width: s.width, height: s.height || 1, depthOrArrayLayers: s.depthOrArrayLayers || 1, format: d.format, usage: d.usage, mipLevelCount: d.mipLevelCount || 1, sampleCount: d.sampleCount || 1, dimension: d.dimension || '2d' }); }
    createView(d = {}) { const v = new GPUTextureView(N.gpuView(this._id, d), d.label); v._tex = this; return v; }
    destroy() { this._drop(); }
}
class GPUTextureView extends GPUObjectBase {}
class GPUSampler extends GPUObjectBase {}
class GPUShaderModule extends GPUObjectBase { async getCompilationInfo() { return { messages: this._err ? [{ message: this._err, type: 'error', lineNum: 0, linePos: 0, offset: 0, length: 0 }] : [] }; } }
class GPUBindGroupLayout extends GPUObjectBase {}
class GPUPipelineLayout extends GPUObjectBase {}
class GPUBindGroup extends GPUObjectBase {}
class GPURenderPipeline extends GPUObjectBase { getBindGroupLayout(i) { return new GPUBindGroupLayout(N.gpuPipeBGL(this._id, i)); } }
class GPUComputePipeline extends GPURenderPipeline {}
class GPUCommandBuffer { constructor(l) { this._cmds = l; this.label = ''; } }
const texArgs = t => [gpuId(t.texture), t.mipLevel || 0, ...(Array.isArray(t.origin) ? [t.origin[0] || 0, t.origin[1] || 0, t.origin[2] || 0] : [t.origin?.x || 0, t.origin?.y || 0, t.origin?.z || 0])];
const bufArgs = b => [gpuId(b.buffer), b.offset || 0, b.bytesPerRow || 0, b.rowsPerImage || 0];
const extArgs = s => Array.isArray(s) ? [s[0], s[1] ?? 1, s[2] ?? 1] : [s.width, s.height ?? 1, s.depthOrArrayLayers ?? 1];
class GPUPassBase {
    constructor(l) { this._l = l; this.label = ''; }
    setPipeline(p) { this._l.push(['pipe', gpuId(p)]); }
    setBindGroup(i, g, dyn = [], s, n) { this._l.push(['bg', i, gpuId(g), dyn && s != null ? Array.from(dyn).slice(s, s + n) : Array.from(dyn || [])]); }
    end() { this._l.push(['end']); }
    pushDebugGroup() {} popDebugGroup() {} insertDebugMarker() {}
}
class GPURenderPassEncoder extends GPUPassBase {
    setVertexBuffer(s, b, o = 0, z) { this._l.push(['vb', s, gpuId(b), o, z ?? -1]); }
    setIndexBuffer(b, f, o = 0, z) { this._l.push(['ib', gpuId(b), f === 'uint32' ? 32 : 16, o, z ?? -1]); }
    draw(vc, ic = 1, fv = 0, fi = 0) { this._l.push(['draw', vc, ic, fv, fi]); }
    drawIndexed(c, ic = 1, fi = 0, bv = 0, fin = 0) { this._l.push(['drawi', c, ic, fi, bv, fin]); }
    setViewport(x, y, w, h, a, b) { this._l.push(['vp', x, y, w, h, a, b]); }
    setScissorRect(x, y, w, h) { this._l.push(['sc', x, y, w, h]); }
    setBlendConstant(c) { const v = Array.isArray(c) ? c : [c.r, c.g, c.b, c.a]; this._l.push(['blend', ...v]); }
    setStencilReference(r) { this._l.push(['sref', r]); }
    executeBundles() {} beginOcclusionQuery() {} endOcclusionQuery() {}
}
class GPUComputePassEncoder extends GPUPassBase { dispatchWorkgroups(x, y = 1, z = 1) { this._l.push(['disp', x, y, z]); } dispatchWorkgroupsIndirect() {} }
class GPUCommandEncoder {
    constructor() { this._l = []; this._present = new Set(); this.label = ''; }
    beginRenderPass(d) { const ca = (d.colorAttachments || []).map(c => c && ({ view: gpuId(c.view), resolveTarget: gpuId(c.resolveTarget), loadOp: c.loadOp, storeOp: c.storeOp, clearValue: c.clearValue ? (Array.isArray(c.clearValue) ? c.clearValue : { r: c.clearValue.r, g: c.clearValue.g, b: c.clearValue.b, a: c.clearValue.a }) : [0, 0, 0, 0] }));
        for (const c of d.colorAttachments || []) for (const v of [c?.view, c?.resolveTarget]) if (v && v._tex && v._tex._cvctx) this._present.add(v._tex._cvctx);
        const z = d.depthStencilAttachment; this._l.push(['rp', ca, z ? { ...z, view: gpuId(z.view) } : null]); return new GPURenderPassEncoder(this._l); }
    beginComputePass() { this._l.push(['cp']); return new GPUComputePassEncoder(this._l); }
    copyBufferToBuffer(s, so, d, dof, n) { this._l.push(['b2b', gpuId(s), so, gpuId(d), dof, n]); }
    copyBufferToTexture(s, d, e) { this._l.push(['b2t', ...bufArgs(s), 0, ...texArgs(d), ...extArgs(e)]); }
    copyTextureToBuffer(s, d, e) { this._l.push(['t2b', ...texArgs(s), ...bufArgs(d), 0, ...extArgs(e)]); }
    copyTextureToTexture(s, d, e) { this._l.push(['t2t', ...texArgs(s), ...texArgs(d), ...extArgs(e)]); }
    clearBuffer(b, o = 0, z) { this._l.push(['clearbuf', gpuId(b), o, z ?? -1]); }
    resolveQuerySet() {} writeTimestamp() {} pushDebugGroup() {} popDebugGroup() {} insertDebugMarker() {}
    finish() { const cb = new GPUCommandBuffer(this._l); cb._present = this._present; return cb; }
}
class GPUQueue {
    constructor() { this.label = ''; }
    submit(cbs) { const pres = new Set(); for (const cb of cbs) { N.gpuExec(cb._cmds); for (const c of cb._present || []) pres.add(c); } for (const c of pres) c._present(); }
    writeBuffer(b, off, data, doff = 0, size) { const bpe = ArrayBuffer.isView(data) ? data.BYTES_PER_ELEMENT || 1 : 1; const u = ArrayBuffer.isView(data) ? new Uint8Array(data.buffer, data.byteOffset + doff * bpe, size != null ? size * bpe : data.byteLength - doff * bpe) : new Uint8Array(data, doff, size); N.gpuWriteBuffer(gpuId(b), off, u); }
    writeTexture(dst, data, layout, size) { const u = ArrayBuffer.isView(data) ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength) : new Uint8Array(data); N.gpuWriteTexture(...texArgs(dst), u, layout.offset || 0, layout.bytesPerRow || 0, layout.rowsPerImage || 0, ...extArgs(size)); }
    copyExternalImageToTexture(src, dst, size) {
        const s = src.source, w = extArgs(size)[0], h = extArgs(size)[1], cv = document.createElement('canvas'); cv.width = w; cv.height = h;
        const x = cv.getContext('2d'); x.drawImage(s, 0, 0, w, h); const d = x.getImageData(0, 0, w, h).data;
        let px = d; if (/^bgra/.test(dst.texture.format)) { px = new Uint8Array(d.length); for (let i = 0; i < d.length; i += 4) { px[i] = d[i + 2]; px[i + 1] = d[i + 1]; px[i + 2] = d[i]; px[i + 3] = d[i + 3]; } }
        N.gpuWriteTexture(...texArgs(dst), px, 0, w * 4, h, w, h, 1);
    }
    onSubmittedWorkDone() { return Promise.resolve(); }
}
class GPUDevice extends EventTarget {
    constructor(ad) { super(); this.adapter = ad; this.queue = new GPUQueue(); this.features = new GPUSupportedFeatures(); this.limits = new GPUSupportedLimits(); this.label = ''; this._lost = null; this.lost = new Promise(r => { this._lost = r; }); this._scopes = []; this.onuncapturederror = null; }
    _check(o) { const e = N.gpuErr(); if (e) { const err = new GPUValidationError(e); if (this._scopes.length) { const s = this._scopes[this._scopes.length - 1]; if (!s.err) s.err = err; } else { const ev = new Event('uncapturederror'); ev.error = err; this.dispatchEvent(ev); if (typeof this.onuncapturederror === 'function') this.onuncapturederror(ev); } if (o) o._err = e; } return o; }
    createBuffer(d) { return this._check(new GPUBuffer(this, d)); }
    createTexture(d) { return this._check(new GPUTexture(this, d)); }
    createSampler(d = {}) { return new GPUSampler(N.gpuSampler(d), d.label); }
    createShaderModule(d) { return this._check(new GPUShaderModule(N.gpuShader(String(d.code), 0), d.label)); }
    createBindGroupLayout(d) { return this._check(new GPUBindGroupLayout(N.gpuBGL(d.entries || []), d.label)); }
    createPipelineLayout(d) { return new GPUPipelineLayout(N.gpuPL((d.bindGroupLayouts || []).map(gpuId)), d.label); }
    createBindGroup(d) { return this._check(new GPUBindGroup(N.gpuBG(gpuId(d.layout), (d.entries || []).map(e => { const r = e.resource; return r instanceof GPUSampler ? { binding: e.binding, sampler: gpuId(r) } : r instanceof GPUTextureView ? { binding: e.binding, view: gpuId(r) } : r instanceof GPUBuffer ? { binding: e.binding, buffer: gpuId(r) } : { binding: e.binding, buffer: gpuId(r.buffer), offset: r.offset || 0, size: r.size ?? -1 }; })), d.label)); }
    _pd(d) { const m = s => s && { ...s, module: gpuId(s.module), entryPoint: s.entryPoint || '' }; return { ...d, layout: d.layout === 'auto' ? 0 : gpuId(d.layout), vertex: m(d.vertex), fragment: m(d.fragment), compute: m(d.compute) }; }
    createRenderPipeline(d) { return this._check(new GPURenderPipeline(N.gpuRP(this._pd(d)), d.label)); }
    createComputePipeline(d) { return this._check(new GPUComputePipeline(N.gpuCP(this._pd(d)), d.label)); }
    async createRenderPipelineAsync(d) { const p = this.createRenderPipeline(d); if (!p._id) throw new GPUPipelineError(p._err || 'pipeline creation failed'); return p; }
    async createComputePipelineAsync(d) { const p = this.createComputePipeline(d); if (!p._id) throw new GPUPipelineError(p._err || 'pipeline creation failed'); return p; }
    createCommandEncoder() { return new GPUCommandEncoder(); }
    createRenderBundleEncoder() { throw new DOMException('render bundles are not supported', 'NotSupportedError'); }
    createQuerySet(d) { return { type: d.type, count: d.count, destroy() {} }; }
    pushErrorScope(f) { this._scopes.push({ f, err: null }); }
    async popErrorScope() { const s = this._scopes.pop(); if (!s) throw new DOMException('no error scope', 'OperationError'); return s.err; }
    destroy() { this._lost && this._lost({ reason: 'destroyed', message: '' }); }
}
class GPUError { constructor(m) { this.message = m; } }
class GPUValidationError extends GPUError {}
class GPUOutOfMemoryError extends GPUError {}
class GPUInternalError extends GPUError {}
class GPUPipelineError extends DOMException { constructor(m) { super(m, 'OperationError'); this.reason = 'validation'; } }
class GPUAdapter {
    constructor(name) { this.features = new GPUSupportedFeatures(); this.limits = new GPUSupportedLimits(); this.info = new GPUAdapterInfo(name); this.isFallbackAdapter = false; }
    async requestDevice() { return new GPUDevice(this); }
    async requestAdapterInfo() { return this.info; }
}
class GPU {
    async requestAdapter() { const n = N.gpuInit(); return n == null ? null : new GPUAdapter(n); }
    getPreferredCanvasFormat() { return 'bgra8unorm'; }
    get wgslLanguageFeatures() { return new Set(); }
}
class GPUCanvasContext {
    constructor(canvas) { this.canvas = canvas; this._cfg = null; this._tex = null; }
    configure(c) { this._cfg = { usage: GPUTextureUsage.RENDER_ATTACHMENT, alphaMode: 'opaque', ...c }; this._tex = null; }
    unconfigure() { this._cfg = null; this._tex = null; }
    getConfiguration() { return this._cfg; }
    getCurrentTexture() {
        if (!this._cfg) throw new DOMException('context is not configured', 'InvalidStateError');
        const w = Math.max(1, this.canvas.width), h = Math.max(1, this.canvas.height);
        if (!this._tex || this._tex.width !== w || this._tex.height !== h) { this._tex?._drop(); this._tex = new GPUTexture(null, { size: [w, h], format: this._cfg.format, usage: this._cfg.usage | GPUTextureUsage.COPY_SRC | GPUTextureUsage.RENDER_ATTACHMENT }); this._tex._cvctx = this; }
        return this._tex;
    }
    _present() { if (this._tex) N.gpuPresent(this._tex._id, this.canvas, this._cfg.alphaMode !== 'premultiplied'); }
}
const LUMEN_GPU = new GPU();
{
    const P = H.HTMLCanvasElement.prototype, base = P.getContext;
    P.getContext = function (t, o) {
        if (t === 'webgpu') return this._ctxgpu || (this._ctxgpu = new GPUCanvasContext(this));
        if ((t === 'webgl' || t === 'experimental-webgl' || t === 'webgl2') && typeof LumenWebGL === 'function') return this._ctxgl || (this._ctxgl = LumenWebGL(this, t === 'webgl2', o || {}));
        return base.call(this, t, o);
    };
}
Object.assign(globalThis, { GPU, GPUAdapter, GPUAdapterInfo, GPUDevice, GPUQueue, GPUBuffer, GPUTexture, GPUTextureView, GPUSampler, GPUShaderModule, GPUBindGroupLayout, GPUPipelineLayout, GPUBindGroup,
    GPURenderPipeline, GPUComputePipeline, GPUCommandEncoder, GPUCommandBuffer, GPURenderPassEncoder, GPUComputePassEncoder, GPUCanvasContext, GPUSupportedLimits, GPUSupportedFeatures,
    GPUBufferUsage, GPUTextureUsage, GPUShaderStage, GPUMapMode, GPUColorWrite, GPUError, GPUValidationError, GPUOutOfMemoryError, GPUInternalError, GPUPipelineError });
