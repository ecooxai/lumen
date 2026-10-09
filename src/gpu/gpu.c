/* wgpu-native presenter: uploads the composited frame and blits it to the surface */
#include "gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu.h>
#include <wgpu.h>

struct Gpu {
    WGPUInstance inst; WGPUSurface surf; WGPUAdapter adapter; WGPUDevice dev; WGPUQueue q;
    WGPUTextureFormat fmt; WGPURenderPipeline pipe; WGPUBindGroupLayout bgl; WGPUSampler samp;
    WGPUTexture tex; WGPUTextureView view; WGPUBindGroup bg; int tw, th; int w, h; bool configured;
    WGPUBackendType backend;
    WGPUSampler vsamp; WGPUBuffer ubuf, vubuf; WGPUTexture vtex; WGPUTextureView vview; WGPUBindGroup vbg; int vtw, vth;
    WGPURenderPipeline ypipe; WGPUBindGroupLayout ybgl; WGPUBuffer yubuf; WGPUTexture yt, ct; WGPUTextureView ytv, ctv; WGPUBindGroup ybg; int ytw, yth;
};

static const char *WGSL =
    "@group(0) @binding(0) var t: texture_2d<f32>;\n"
    "@group(0) @binding(1) var s: sampler;\n"
    "@group(0) @binding(2) var<uniform> r: vec4f;\n"
    "struct V { @builtin(position) p: vec4f, @location(0) uv: vec2f };\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> V {\n"
    "  let uv = vec2f(f32(i & 1u), f32(i >> 1u));\n"
    "  var o: V; o.p = vec4f(mix(r.xy, r.zw, uv), 0.0, 1.0); o.uv = uv; return o; }\n"
    "@fragment fn fs(v: V) -> @location(0) vec4f { return textureSample(t, s, v.uv); }\n";

static const char *WGSL_YUV =
    "@group(0) @binding(0) var ty: texture_2d<f32>;\n"
    "@group(0) @binding(1) var s: sampler;\n"
    "@group(0) @binding(2) var<uniform> r: array<vec4f, 3>;\n"
    "@group(0) @binding(3) var tc: texture_2d<f32>;\n"
    "struct V { @builtin(position) p: vec4f, @location(0) uv: vec2f };\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> V {\n"
    "  let uv = vec2f(f32(i & 1u), f32(i >> 1u));\n"
    "  var o: V; o.p = vec4f(mix(r[0].xy, r[0].zw, uv), 0.0, 1.0); o.uv = uv; return o; }\n"
    "@fragment fn fs(v: V) -> @location(0) vec4f {\n"
    "  let y = (textureSample(ty, s, v.uv).r - r[2].x) * r[2].y;\n"
    "  let c = (textureSample(tc, s, v.uv).rg - vec2f(128.0 / 255.0)) * r[2].z;\n"
    "  return vec4f(clamp(vec3f(y + r[1].x * c.y, y - r[1].y * c.x - r[1].z * c.y, y + r[1].w * c.x), vec3f(0.0), vec3f(1.0)), 1.0); }\n";

static WGPUStringView sv(const char *s) { return (WGPUStringView){ s, WGPU_STRLEN }; }

static void on_adapter(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView msg, void *u1, void *u2) {
    (void)u2; if (st == WGPURequestAdapterStatus_Success) *(WGPUAdapter *)u1 = a;
    else fprintf(stderr, "gpu: adapter request failed: %.*s\n", (int)(msg.data ? msg.length : 0), msg.data ? msg.data : "");
}
static void on_device(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView msg, void *u1, void *u2) {
    (void)u2; if (st == WGPURequestDeviceStatus_Success) *(WGPUDevice *)u1 = d;
    else fprintf(stderr, "gpu: device request failed: %.*s\n", (int)(msg.data ? msg.length : 0), msg.data ? msg.data : "");
}
static void on_error(WGPUDevice const *d, WGPUErrorType t, WGPUStringView msg, void *u1, void *u2) {
    (void)d; (void)u1; (void)u2; fprintf(stderr, "gpu: error %d: %.*s\n", (int)t, (int)msg.length, msg.data);
}

static void configure(Gpu *g) {
    if (g->w <= 0 || g->h <= 0) { g->configured = false; return; }
    WGPUSurfaceConfiguration c = WGPU_SURFACE_CONFIGURATION_INIT;
    c.device = g->dev; c.format = g->fmt; c.usage = WGPUTextureUsage_RenderAttachment;
    c.width = (uint32_t)g->w; c.height = (uint32_t)g->h;
    c.alphaMode = WGPUCompositeAlphaMode_Auto; c.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(g->surf, &c); g->configured = true;
}

Gpu *gpu_create(const GpuSurfSrc *src, int w, int h, bool prefer_vulkan) {
    if (getenv("LUMEN_NO_GPU")) return NULL;
    Gpu *g = calloc(1, sizeof *g); g->w = w; g->h = h;
    WGPUInstanceExtras ex = { 0 }; ex.chain.sType = (WGPUSType)WGPUSType_InstanceExtras;
    ex.backends = prefer_vulkan ? WGPUInstanceBackend_Vulkan : WGPUInstanceBackend_Primary;
    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT; id.nextInChain = &ex.chain;
    if (!(g->inst = wgpuCreateInstance(&id))) goto fail;

    WGPUSurfaceSourceMetalLayer ml = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
    WGPUSurfaceSourceXlibWindow xl = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
    WGPUSurfaceSourceWaylandSurface wl = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
    WGPUSurfaceSourceAndroidNativeWindow an = WGPU_SURFACE_SOURCE_ANDROID_NATIVE_WINDOW_INIT;
    WGPUSurfaceDescriptor sd = WGPU_SURFACE_DESCRIPTOR_INIT;
    switch (src->kind) {
    case GPU_SURF_METAL: ml.layer = src->a; sd.nextInChain = &ml.chain; break;
    case GPU_SURF_XLIB: xl.display = src->a; xl.window = src->win; sd.nextInChain = &xl.chain; break;
    case GPU_SURF_WAYLAND: wl.display = src->a; wl.surface = src->b; sd.nextInChain = &wl.chain; break;
    case GPU_SURF_ANDROID: an.window = src->a; sd.nextInChain = &an.chain; break;
    }
    if (!(g->surf = wgpuInstanceCreateSurface(g->inst, &sd))) goto fail;

    WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    ao.compatibleSurface = g->surf; ao.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    ac.mode = WGPUCallbackMode_AllowProcessEvents; ac.callback = on_adapter; ac.userdata1 = &g->adapter;
    wgpuInstanceRequestAdapter(g->inst, &ao, ac);
    wgpuInstanceProcessEvents(g->inst);
    if (!g->adapter) goto fail;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(g->adapter, &info) == WGPUStatus_Success) {
        g->backend = info.backendType;
        if (info.adapterType == WGPUAdapterType_CPU) { wgpuAdapterInfoFreeMembers(info); goto fail; }
        wgpuAdapterInfoFreeMembers(info);
    }

    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.uncapturedErrorCallbackInfo.callback = on_error;
    WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dc.mode = WGPUCallbackMode_AllowProcessEvents; dc.callback = on_device; dc.userdata1 = &g->dev;
    wgpuAdapterRequestDevice(g->adapter, &dd, dc);
    wgpuInstanceProcessEvents(g->inst);
    if (!g->dev) goto fail;
    g->q = wgpuDeviceGetQueue(g->dev);

    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(g->surf, g->adapter, &caps) != WGPUStatus_Success || !caps.formatCount) goto fail;
    g->fmt = caps.formats[0];
    for (size_t i = 0; i < caps.formatCount; i++)
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) { g->fmt = caps.formats[i]; break; }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    WGPUShaderSourceWGSL ws = WGPU_SHADER_SOURCE_WGSL_INIT; ws.code = sv(WGSL);
    WGPUShaderModuleDescriptor smd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT; smd.nextInChain = &ws.chain;
    WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g->dev, &smd);
    if (!sm) goto fail;
    WGPUBlendComponent bc = { .operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One, .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha };
    WGPUBlendState bl = { .color = bc, .alpha = bc };
    WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT; ct.format = g->fmt; ct.blend = &bl;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT; fs.module = sm; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &ct;
    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs"); pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
    g->pipe = wgpuDeviceCreateRenderPipeline(g->dev, &pd);
    wgpuShaderModuleRelease(sm);
    if (!g->pipe) goto fail;
    g->bgl = wgpuRenderPipelineGetBindGroupLayout(g->pipe, 0);
    ws.code = sv(WGSL_YUV); sm = wgpuDeviceCreateShaderModule(g->dev, &smd);
    if (sm) { pd.vertex.module = fs.module = sm; g->ypipe = wgpuDeviceCreateRenderPipeline(g->dev, &pd); wgpuShaderModuleRelease(sm); }
    if (g->ypipe) g->ybgl = wgpuRenderPipelineGetBindGroupLayout(g->ypipe, 0);
    WGPUSamplerDescriptor smp = WGPU_SAMPLER_DESCRIPTOR_INIT;
    g->samp = wgpuDeviceCreateSampler(g->dev, &smp);
    WGPUSamplerDescriptor vsmp = WGPU_SAMPLER_DESCRIPTOR_INIT; vsmp.magFilter = WGPUFilterMode_Linear; vsmp.minFilter = WGPUFilterMode_Linear;
    g->vsamp = wgpuDeviceCreateSampler(g->dev, &vsmp);
    WGPUBufferDescriptor ub = WGPU_BUFFER_DESCRIPTOR_INIT; ub.size = 16; ub.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    g->ubuf = wgpuDeviceCreateBuffer(g->dev, &ub); g->vubuf = wgpuDeviceCreateBuffer(g->dev, &ub);
    ub.size = 48; g->yubuf = wgpuDeviceCreateBuffer(g->dev, &ub);
    float full[4] = { -1, 1, 1, -1 }; wgpuQueueWriteBuffer(g->q, g->ubuf, 0, full, sizeof full);
    configure(g);
    return g;
fail:
    fprintf(stderr, "gpu: unavailable, using CPU presentation\n");
    gpu_destroy(g);
    return NULL;
}

void gpu_resize(Gpu *g, int w, int h) { if (g && (w != g->w || h != g->h)) { g->w = w; g->h = h; configure(g); } }

static WGPUBindGroup make_bg(Gpu *g, WGPUTextureView v, WGPUSampler s, WGPUBuffer b) {
    WGPUBindGroupEntry e[3] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
    e[0].binding = 0; e[0].textureView = v; e[1].binding = 1; e[1].sampler = s; e[2].binding = 2; e[2].buffer = b; e[2].size = 16;
    WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT; bd.layout = g->bgl; bd.entryCount = 3; bd.entries = e;
    return wgpuDeviceCreateBindGroup(g->dev, &bd);
}
static void ensure_vtex(Gpu *g, int w, int h) {
    if (g->vtex && g->vtw == w && g->vth == h) return;
    if (g->vbg) wgpuBindGroupRelease(g->vbg);
    if (g->vview) wgpuTextureViewRelease(g->vview);
    if (g->vtex) { wgpuTextureDestroy(g->vtex); wgpuTextureRelease(g->vtex); }
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.size = (WGPUExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    td.format = WGPUTextureFormat_BGRA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    g->vtex = wgpuDeviceCreateTexture(g->dev, &td);
    g->vview = wgpuTextureCreateView(g->vtex, NULL);
    g->vbg = make_bg(g, g->vview, g->vsamp, g->vubuf);
    g->vtw = w; g->vth = h;
}
static void free_ytex(Gpu *g) {
    if (g->ybg) wgpuBindGroupRelease(g->ybg);
    if (g->ytv) wgpuTextureViewRelease(g->ytv);
    if (g->ctv) wgpuTextureViewRelease(g->ctv);
    if (g->yt) { wgpuTextureDestroy(g->yt); wgpuTextureRelease(g->yt); }
    if (g->ct) { wgpuTextureDestroy(g->ct); wgpuTextureRelease(g->ct); }
    g->ybg = NULL; g->ytv = g->ctv = NULL; g->yt = g->ct = NULL;
}
static WGPUTexture mk_tex(Gpu *g, int w, int h, WGPUTextureFormat f) {
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT; td.size = (WGPUExtent3D){ (uint32_t)w, (uint32_t)h, 1 }; td.format = f;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    return wgpuDeviceCreateTexture(g->dev, &td);
}
static void ensure_ytex(Gpu *g, int w, int h) {
    if (g->yt && g->ytw == w && g->yth == h) return;
    free_ytex(g);
    g->yt = mk_tex(g, w, h, WGPUTextureFormat_R8Unorm); g->ct = mk_tex(g, (w + 1) / 2, (h + 1) / 2, WGPUTextureFormat_RG8Unorm);
    g->ytv = wgpuTextureCreateView(g->yt, NULL); g->ctv = wgpuTextureCreateView(g->ct, NULL);
    WGPUBindGroupEntry e[4] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
    e[0].binding = 0; e[0].textureView = g->ytv; e[1].binding = 1; e[1].sampler = g->vsamp; e[2].binding = 2; e[2].buffer = g->yubuf; e[2].size = 48; e[3].binding = 3; e[3].textureView = g->ctv;
    WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT; bd.layout = g->ybgl; bd.entryCount = 4; bd.entries = e;
    g->ybg = wgpuDeviceCreateBindGroup(g->dev, &bd); g->ytw = w; g->yth = h;
}
bool gpu_yuv_ok(Gpu *g) { return g && g->ypipe && g->ybgl && g->yubuf; }
static void ensure_tex(Gpu *g, int w, int h) {
    if (g->tex && g->tw == w && g->th == h) return;
    if (g->bg) wgpuBindGroupRelease(g->bg);
    if (g->view) wgpuTextureViewRelease(g->view);
    if (g->tex) { wgpuTextureDestroy(g->tex); wgpuTextureRelease(g->tex); }
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.size = (WGPUExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    td.format = WGPUTextureFormat_BGRA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    g->tex = wgpuDeviceCreateTexture(g->dev, &td);
    g->view = wgpuTextureCreateView(g->tex, NULL);
    g->bg = make_bg(g, g->view, g->samp, g->ubuf);
    g->tw = w; g->th = h;
}

bool gpu_present(Gpu *g, const uint32_t *px, int w, int h, int stride) { return gpu_present_frame(g, px, w, h, stride, 0, h, NULL); }
bool gpu_present_rows(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1) { return gpu_present_frame(g, px, w, h, stride, y0, y1, NULL); }
bool gpu_present_frame(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1, const GpuVideo *v) {
    if (!g || !g->configured) return false;
    if (!g->tex || g->tw != w || g->th != h) { y0 = 0; y1 = h; }
    ensure_tex(g, w, h);
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    if (y1 > y0) {
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = g->tex; dst.origin = (WGPUOrigin3D){ 0, (uint32_t)y0, 0 };
        WGPUTexelCopyBufferLayout lay = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay.bytesPerRow = (uint32_t)stride * 4; lay.rowsPerImage = (uint32_t)(y1 - y0);
        WGPUExtent3D ext = { (uint32_t)w, (uint32_t)(y1 - y0), 1 };
        wgpuQueueWriteTexture(g->q, &dst, px + (size_t)y0 * (size_t)stride, (size_t)stride * 4 * (size_t)(y1 - y0), &lay, &ext);
    }
    bool yv = v && v->yuv && gpu_yuv_ok(g) && v->w > 0 && v->h > 0, vid = !yv && v && v->px && v->w > 0 && v->h > 0;
    if (yv) {
        ensure_ytex(g, v->w, v->h);
        int cw = (v->w + 1) / 2, ch = (v->h + 1) / 2;
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = g->yt;
        WGPUTexelCopyBufferLayout lay = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay.bytesPerRow = (uint32_t)v->w; lay.rowsPerImage = (uint32_t)v->h;
        WGPUExtent3D ext = { (uint32_t)v->w, (uint32_t)v->h, 1 };
        wgpuQueueWriteTexture(g->q, &dst, v->yuv, (size_t)v->w * (size_t)v->h, &lay, &ext);
        dst.texture = g->ct; lay.bytesPerRow = (uint32_t)cw * 2; lay.rowsPerImage = (uint32_t)ch; ext = (WGPUExtent3D){ (uint32_t)cw, (uint32_t)ch, 1 };
        wgpuQueueWriteTexture(g->q, &dst, v->yuv + (size_t)v->w * (size_t)v->h, (size_t)cw * 2 * (size_t)ch, &lay, &ext);
        bool b709 = v->mat & 1, full = v->mat & 2;
        float u[12] = { v->x0 / w * 2 - 1, 1 - v->y0 / h * 2, v->x1 / w * 2 - 1, 1 - v->y1 / h * 2,
                        b709 ? 1.5748f : 1.402f, b709 ? 0.1873f : 0.344136f, b709 ? 0.4681f : 0.714136f, b709 ? 1.8556f : 1.772f,
                        full ? 0 : 16.f / 255, full ? 1 : 255.f / 219, full ? 1 : 255.f / 224, 0 };
        wgpuQueueWriteBuffer(g->q, g->yubuf, 0, u, sizeof u);
    }
    if (vid) {
        ensure_vtex(g, v->w, v->h);
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = g->vtex;
        WGPUTexelCopyBufferLayout lay = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay.bytesPerRow = (uint32_t)v->stride * 4; lay.rowsPerImage = (uint32_t)v->h;
        WGPUExtent3D ext = { (uint32_t)v->w, (uint32_t)v->h, 1 };
        wgpuQueueWriteTexture(g->q, &dst, v->px, (size_t)v->stride * 4 * (size_t)v->h, &lay, &ext);
        float r[4] = { v->x0 / w * 2 - 1, 1 - v->y0 / h * 2, v->x1 / w * 2 - 1, 1 - v->y1 / h * 2 };
        wgpuQueueWriteBuffer(g->q, g->vubuf, 0, r, sizeof r);
    }

    WGPUSurfaceTexture st = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(g->surf, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        if (st.texture) wgpuTextureRelease(st.texture);
        configure(g);
        return st.status == WGPUSurfaceGetCurrentTextureStatus_Timeout || st.status == WGPUSurfaceGetCurrentTextureStatus_Outdated || st.status == WGPUSurfaceGetCurrentTextureStatus_Lost;
    }
    WGPUTextureView tv = wgpuTextureCreateView(st.texture, NULL);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g->dev, NULL);
    WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    ca.view = tv; ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store; ca.clearValue = (WGPUColor){ 1, 1, 1, 1 };
    WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    if (yv) { wgpuRenderPassEncoderSetPipeline(pass, g->ypipe); wgpuRenderPassEncoderSetBindGroup(pass, 0, g->ybg, 0, NULL); wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0); }
    wgpuRenderPassEncoderSetPipeline(pass, g->pipe);
    if (vid) { wgpuRenderPassEncoderSetBindGroup(pass, 0, g->vbg, 0, NULL); wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0); }
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g->bg, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, NULL);
    wgpuQueueSubmit(g->q, 1, &cb);
    wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(enc);
    wgpuSurfacePresent(g->surf);
    wgpuTextureViewRelease(tv); wgpuTextureRelease(st.texture);
    return true;
}

const char *gpu_backend_name(Gpu *g) {
    if (!g) return "cpu";
    switch (g->backend) {
    case WGPUBackendType_Metal: return "wgpu/metal";
    case WGPUBackendType_Vulkan: return "wgpu/vulkan";
    case WGPUBackendType_OpenGL: case WGPUBackendType_OpenGLES: return "wgpu/gl";
    default: return "wgpu";
    }
}

void gpu_destroy(Gpu *g) {
    if (!g) return;
    if (g->bg) wgpuBindGroupRelease(g->bg);
    if (g->view) wgpuTextureViewRelease(g->view);
    if (g->tex) { wgpuTextureDestroy(g->tex); wgpuTextureRelease(g->tex); }
    if (g->samp) wgpuSamplerRelease(g->samp);
    free_ytex(g);
    if (g->ypipe) wgpuRenderPipelineRelease(g->ypipe);
    if (g->ybgl) wgpuBindGroupLayoutRelease(g->ybgl);
    if (g->yubuf) wgpuBufferRelease(g->yubuf);
    if (g->vbg) wgpuBindGroupRelease(g->vbg);
    if (g->vview) wgpuTextureViewRelease(g->vview);
    if (g->vtex) { wgpuTextureDestroy(g->vtex); wgpuTextureRelease(g->vtex); }
    if (g->vsamp) wgpuSamplerRelease(g->vsamp);
    if (g->ubuf) wgpuBufferRelease(g->ubuf);
    if (g->vubuf) wgpuBufferRelease(g->vubuf);
    if (g->bgl) wgpuBindGroupLayoutRelease(g->bgl);
    if (g->pipe) wgpuRenderPipelineRelease(g->pipe);
    if (g->configured) wgpuSurfaceUnconfigure(g->surf);
    if (g->q) wgpuQueueRelease(g->q);
    if (g->dev) wgpuDeviceRelease(g->dev);
    if (g->adapter) wgpuAdapterRelease(g->adapter);
    if (g->surf) wgpuSurfaceRelease(g->surf);
    if (g->inst) wgpuInstanceRelease(g->inst);
    free(g);
}
