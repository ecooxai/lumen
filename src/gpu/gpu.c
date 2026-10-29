/* wgpu-native presenter: uploads the composited frame and blits it to the surface */
#include "gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <webgpu.h>
#include <wgpu.h>
#ifdef __ANDROID__
#include <android/native_window.h>
#include <android/log.h>
#endif

struct Gpu {
    WGPUInstance inst; WGPUSurface surf; WGPUAdapter adapter; WGPUDevice dev; WGPUQueue q;
    WGPUTextureFormat fmt; WGPURenderPipeline pipe; WGPUBindGroupLayout bgl; WGPUSampler samp;
    WGPUTexture tex; WGPUTextureView view; WGPUBindGroup bg; int tw, th; int w, h; bool configured;
    WGPUBackendType backend;
    char name[128];   /* the adapter's device name */
    bool full;   /* the device has the adapter's WebGPU features and limits: pages may share it */
    int scroll_toolbar, scroll_height, scroll_offset;
    WGPUSampler vsamp; WGPUBuffer ubuf, vubuf; WGPUTexture vtex; WGPUTextureView vview; WGPUBindGroup vbg; int vtw, vth;
    WGPURenderPipeline ypipe; WGPUBindGroupLayout ybgl; WGPUBuffer yubuf; WGPUTexture yt, ct; WGPUTextureView ytv, ctv; WGPUBindGroup ybg; int ytw, yth;
    uint64_t vserial, yserial; /* Content currently uploaded to each video texture. */
    struct { WGPUTexture tex; WGPUTextureView view; WGPUBindGroup bg; WGPUBuffer ubuf; int w, h; uint64_t serial; } xl[GPU_EXTRA_LAYERS];
    WGPUBindGroup gbg[1 + GPU_EXTRA_LAYERS]; const void *gview[1 + GPU_EXTRA_LAYERS];   /* bind groups of shared texture views: the primary layer's, then the extras' */
    GpuVideo xv[GPU_EXTRA_LAYERS]; int nxv;   /* extra layers for the next present */
#ifdef __ANDROID__
    ANativeWindow *native_window;
#endif
};

static const char *WGSL =
    "@group(0) @binding(0) var t: texture_2d<f32>;\n"
    "@group(0) @binding(1) var s: sampler;\n"
    "struct Params { rect: vec4f, scroll: vec4u };\n"
    "@group(0) @binding(2) var<uniform> params: Params;\n"
    "struct V { @builtin(position) p: vec4f, @location(0) uv: vec2f };\n"
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> V {\n"
    "  let uv = vec2f(f32(i & 1u), f32(i >> 1u));\n"
    "  var o: V; o.p = vec4f(mix(params.rect.xy, params.rect.zw, uv), 0.0, 1.0); o.uv = uv; return o; }\n"
    "@fragment fn fs(v: V) -> @location(0) vec4f {\n"
    "  var uv = v.uv;\n"
    "  if (params.scroll.y > 0u) {\n"
    "    let height = textureDimensions(t).y;\n"
    "    let row = min(u32(uv.y * f32(height)), height - 1u);\n"
    "    if (row >= params.scroll.x && row - params.scroll.x < params.scroll.y) {\n"
    "      var mapped = row - params.scroll.x + params.scroll.z;\n"
    "      if (mapped >= params.scroll.y) { mapped -= params.scroll.y; }\n"
    "      uv.y = (f32(params.scroll.x + mapped) + 0.5) / f32(height);\n"
    "    }\n"
    "  }\n"
    "  let c = textureSampleLevel(t, s, uv, 0.0);\n"
    "  var o = select(c.bgra, c, (params.scroll.w & 1u) != 0u);\n"   /* uploads hold 0xAARRGGBB words; shared textures are in order */
    "  if ((params.scroll.w & 2u) != 0u) { o.a = 1.0; }\n"
    "  return o; }\n";

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

void gpu_log(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_WARN, "LumenGPU", format, arguments);
#else
    vfprintf(stderr, format, arguments);
#endif
    va_end(arguments);
}

/* set once pages have WebGPU devices: wgpu's messages may then quote their shaders, so only their size is logged */
int gpu_log_quiet;

static void on_log(WGPULogLevel level, WGPUStringView message, void *userdata) {
    (void)userdata;
    if (gpu_log_quiet) { gpu_log("wgpu: level %d message, %d bytes\n", (int)level, (int)(message.data ? message.length : 0)); return; }
    gpu_log("wgpu: %.*s\n", (int)(message.data ? message.length : 0), message.data ? message.data : "");
}

static void on_adapter(WGPURequestAdapterStatus st, WGPUAdapter a, WGPUStringView msg, void *u1, void *u2) {
    (void)u2; if (st == WGPURequestAdapterStatus_Success) *(WGPUAdapter *)u1 = a;
    else gpu_log("gpu: adapter request failed: %.*s\n", (int)(msg.data ? msg.length : 0), msg.data ? msg.data : "");
}
static void on_device(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView msg, void *u1, void *u2) {
    (void)u2; if (st == WGPURequestDeviceStatus_Success) *(WGPUDevice *)u1 = d;
    else gpu_log("gpu: device request failed: %.*s\n", (int)(msg.data ? msg.length : 0), msg.data ? msg.data : "");
}
bool (*gpu_error_hook)(int type, const char *msg, size_t len);
static Gpu *g_live;   /* the presenter pages may share */
static void on_error(WGPUDevice const *d, WGPUErrorType t, WGPUStringView msg, void *u1, void *u2) {
    (void)d; (void)u1; (void)u2;
    if (gpu_error_hook && gpu_error_hook((int)t, msg.data ? msg.data : "", msg.data ? (msg.length == WGPU_STRLEN ? strlen(msg.data) : msg.length) : 0)) return;
    gpu_log("gpu: error %d: %.*s\n", (int)t, (int)(msg.data ? msg.length : 0), msg.data ? msg.data : "");
}

/* Android destroys the window as the app goes to the background, possibly while a frame is being drawn;
   its queries then fail. Configuring a surface on it is a fatal error in wgpu (a panic that aborts
   Lumen), so nothing is configured until the foreground brings a new window (gpu_resume). */
static bool window_alive(Gpu *g) {
#ifdef __ANDROID__
    if (g->native_window && ANativeWindow_getWidth(g->native_window) < 0) return false;
#else
    (void)g;
#endif
    return true;
}

static void configure(Gpu *g) {
    if (!g->surf || g->w <= 0 || g->h <= 0 || !window_alive(g)) {
        if (g->surf && g->configured) wgpuSurfaceUnconfigure(g->surf);
        g->configured = false;
        return;
    }
    WGPUSurfaceConfiguration c = WGPU_SURFACE_CONFIGURATION_INIT;
    c.device = g->dev; c.format = g->fmt; c.usage = WGPUTextureUsage_RenderAttachment;
    c.width = (uint32_t)g->w; c.height = (uint32_t)g->h;
    c.alphaMode = WGPUCompositeAlphaMode_Auto; c.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(g->surf, &c); g->configured = true;
}

static WGPUSurface create_surface(Gpu *gpu, const GpuSurfSrc *source) {
    if (!source || !source->a) return NULL;
    WGPUSurfaceSourceMetalLayer ml = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
    WGPUSurfaceSourceXlibWindow xl = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
    WGPUSurfaceSourceWaylandSurface wl = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
    WGPUSurfaceSourceAndroidNativeWindow an = WGPU_SURFACE_SOURCE_ANDROID_NATIVE_WINDOW_INIT;
    WGPUSurfaceDescriptor sd = WGPU_SURFACE_DESCRIPTOR_INIT;
    switch (source->kind) {
    case GPU_SURF_METAL: ml.layer = source->a; sd.nextInChain = &ml.chain; break;
    case GPU_SURF_XLIB: xl.display = source->a; xl.window = source->win; sd.nextInChain = &xl.chain; break;
    case GPU_SURF_WAYLAND: wl.display = source->a; wl.surface = source->b; sd.nextInChain = &wl.chain; break;
    case GPU_SURF_ANDROID:
        an.window = source->a; sd.nextInChain = &an.chain;
#ifdef __ANDROID__
        gpu->native_window = source->a;
        ANativeWindow_acquire(gpu->native_window);
#endif
        break;
    default: return NULL;
    }
    WGPUSurface surface = wgpuInstanceCreateSurface(gpu->inst, &sd);
#ifdef __ANDROID__
    if (!surface && gpu->native_window) {
        ANativeWindow_release(gpu->native_window);
        gpu->native_window = NULL;
    }
#endif
    return surface;
}

void gpu_suspend(Gpu *gpu) {
    if (!gpu) return;
    if (gpu->configured) wgpuSurfaceUnconfigure(gpu->surf);
    gpu->configured = false;
    if (gpu->surf) { wgpuSurfaceRelease(gpu->surf); gpu->surf = NULL; }
#ifdef __ANDROID__
    if (gpu->native_window) {
        ANativeWindow_release(gpu->native_window);
        gpu->native_window = NULL;
    }
#endif
}

bool gpu_resume(Gpu *gpu, const GpuSurfSrc *source, int width, int height) {
    if (!gpu) return false;
    gpu_suspend(gpu);
    if (width <= 0 || height <= 0) return false;
    if (!(gpu->surf = create_surface(gpu, source))) return false;
    WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
    bool compatible = false;
    if (wgpuSurfaceGetCapabilities(gpu->surf, gpu->adapter, &capabilities) == WGPUStatus_Success) {
        for (size_t index = 0; index < capabilities.formatCount; index++)
            if (capabilities.formats[index] == gpu->fmt) compatible = true;
    }
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    if (!compatible) { gpu_suspend(gpu); return false; }
    gpu->w = width; gpu->h = height;
    configure(gpu);
    return gpu->configured;
}

static WGPUInstanceBackend gpu_backend_flags(GpuBackend backend) {
    switch (backend) {
    case GPU_BACKEND_AUTO: return WGPUInstanceBackend_Primary;
    case GPU_BACKEND_VULKAN: return WGPUInstanceBackend_Vulkan;
    case GPU_BACKEND_GL: return WGPUInstanceBackend_GL;
    default: return 0;
    }
}

static int g_pick;
void gpu_set_pick(int pick) { g_pick = pick; }
const char *gpu_adapter_name(Gpu *g) { return g ? g->name : ""; }
static void sv_copy(char *out, size_t cap, WGPUStringView v) {
    size_t n = !v.data ? 0 : v.length == WGPU_STRLEN ? strlen(v.data) : v.length;
    if (n >= cap) n = cap - 1;
    if (n) memcpy(out, v.data, n);
    out[n] = 0;
}
/* the adapter of the chosen vendor (g_pick), or for auto a discrete NVIDIA / AMD one before others, among those that can
   present to the window; Vulkan before GL on the same GPU. NULL: none matched, the default request decides */
static WGPUAdapter pick_adapter(Gpu *g, WGPUInstanceBackend backends) {
    WGPUInstanceEnumerateAdapterOptions o = { 0 }; o.backends = backends;
    size_t n = wgpuInstanceEnumerateAdapters(g->inst, &o, NULL);
    if (!n) return NULL;
    WGPUAdapter *v = calloc(n, sizeof *v); if (!v) return NULL;
    n = wgpuInstanceEnumerateAdapters(g->inst, &o, v);
    uint32_t want = g_pick == 1 ? 0x8086 : g_pick == 2 ? 0x10de : g_pick == 3 ? 0x1002 : 0;
    int best = -1, bs = 0;
    for (size_t i = 0; i < n; i++) {
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT; int sc = 0;
        if (wgpuAdapterGetInfo(v[i], &info) == WGPUStatus_Success) {
            WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
            bool ok = info.adapterType != WGPUAdapterType_CPU && wgpuSurfaceGetCapabilities(g->surf, v[i], &caps) == WGPUStatus_Success && caps.formatCount;
            if (caps.formatCount) wgpuSurfaceCapabilitiesFreeMembers(caps);
            bool big = info.vendorID == 0x10de || info.vendorID == 0x1002 || info.adapterType == WGPUAdapterType_DiscreteGPU;
            if (ok && (!want || info.vendorID == want)) sc = (want || big ? 4 : 2) + (info.backendType == WGPUBackendType_Vulkan);
            char nm[128]; sv_copy(nm, sizeof nm, info.device);
            gpu_log("gpu: adapter %s vendor %04x type %d backend %d%s\n", nm, info.vendorID, (int)info.adapterType, (int)info.backendType, ok ? "" : " (cannot present)");
            wgpuAdapterInfoFreeMembers(info);
        }
        if (sc > bs) { bs = sc; best = (int)i; }
    }
    WGPUAdapter r = best >= 0 ? v[best] : NULL;
    for (size_t i = 0; i < n; i++) if (v[i] != r) wgpuAdapterRelease(v[i]);
    free(v);
    return r;
}

Gpu *gpu_create(const GpuSurfSrc *src, int w, int h, bool prefer_vulkan) {
    return gpu_create_backend(src, w, h, prefer_vulkan ? GPU_BACKEND_VULKAN : GPU_BACKEND_AUTO);
}

Gpu *gpu_create_backend(const GpuSurfSrc *src, int w, int h, GpuBackend backend) {
    if (getenv("LUMEN_NO_GPU") || !gpu_backend_flags(backend)) return NULL;
    wgpuSetLogCallback(on_log, NULL);
    wgpuSetLogLevel(getenv("LUMEN_WGPU_LOG") && !strcmp(getenv("LUMEN_WGPU_LOG"), "info") ? WGPULogLevel_Info : WGPULogLevel_Warn);   /* LUMEN_WGPU_LOG=info: adapter and surface details */
    Gpu *g = calloc(1, sizeof *g); g->w = w; g->h = h;
    WGPUInstanceExtras ex = { 0 }; ex.chain.sType = (WGPUSType)WGPUSType_InstanceExtras;
    ex.backends = gpu_backend_flags(backend);
    /* the GL backend makes window surfaces only on the display it was given */
    if (src && src->kind == GPU_SURF_XLIB) { ex.displayHandle.type = WGPUNativeDisplayHandleType_Xlib; ex.displayHandle.data.xlib.display = src->a; }
    else if (src && src->kind == GPU_SURF_WAYLAND) { ex.displayHandle.type = WGPUNativeDisplayHandleType_Wayland; ex.displayHandle.data.wayland.display = src->a; }
#ifndef NDEBUG
    ex.flags = WGPUInstanceFlag_Validation;
#endif
    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT; id.nextInChain = &ex.chain;
    if (!(g->inst = wgpuCreateInstance(&id))) goto fail;
    if (!(g->surf = create_surface(g, src))) goto fail;

    g->adapter = pick_adapter(g, ex.backends);
    if (!g->adapter) {
        WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        ao.compatibleSurface = g->surf; ao.powerPreference = WGPUPowerPreference_HighPerformance;
        WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        ac.mode = WGPUCallbackMode_AllowProcessEvents; ac.callback = on_adapter; ac.userdata1 = &g->adapter;
        wgpuInstanceRequestAdapter(g->inst, &ao, ac);
        wgpuInstanceProcessEvents(g->inst);
    }
    if (!g->adapter) goto fail;
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(g->adapter, &info) == WGPUStatus_Success) {
        g->backend = info.backendType; sv_copy(g->name, sizeof g->name, info.device);
        gpu_log("gpu: using %s\n", g->name);
        if (info.adapterType == WGPUAdapterType_CPU) { wgpuAdapterInfoFreeMembers(info); goto fail; }
        wgpuAdapterInfoFreeMembers(info);
    }

    /* the adapter's WebGPU features and limits, so pages' devices can be this one; the defaults if that fails */
    WGPUSupportedFeatures sf = { 0 };
    wgpuAdapterGetFeatures(g->adapter, &sf);
    WGPUFeatureName feats[64]; size_t nf = 0;
    for (size_t i = 0; i < sf.featureCount && nf < 64; i++) if ((uint32_t)sf.features[i] < 0x10000u) feats[nf++] = sf.features[i];
    wgpuSupportedFeaturesFreeMembers(sf);
    WGPULimits lim = WGPU_LIMITS_INIT;
    bool share = !getenv("LUMEN_WEBGPU_SEPARATE") && wgpuAdapterGetLimits(g->adapter, &lim) == WGPUStatus_Success;
    for (int attempt = share ? 0 : 1; attempt < 2 && !g->dev; attempt++) {
        WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
        dd.uncapturedErrorCallbackInfo.callback = on_error;
        if (!attempt) { dd.requiredFeatureCount = nf; dd.requiredFeatures = feats; dd.requiredLimits = &lim; }
        WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        dc.mode = WGPUCallbackMode_AllowProcessEvents; dc.callback = on_device; dc.userdata1 = &g->dev;
        wgpuAdapterRequestDevice(g->adapter, &dd, dc);
        wgpuInstanceProcessEvents(g->inst);
        g->full = g->dev && !attempt;
    }
    if (g->dev) gpu_log("gpu: device %s\n", g->full ? "has the adapter's features and limits: pages share it" : "has the defaults: pages make their own");
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
    WGPUBufferDescriptor ub = WGPU_BUFFER_DESCRIPTOR_INIT; ub.size = 32; ub.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    g->ubuf = wgpuDeviceCreateBuffer(g->dev, &ub); g->vubuf = wgpuDeviceCreateBuffer(g->dev, &ub);
    ub.size = 48; g->yubuf = wgpuDeviceCreateBuffer(g->dev, &ub);
    float full[8] = { -1, 1, 1, -1, 0, 0, 0, 0 }; wgpuQueueWriteBuffer(g->q, g->ubuf, 0, full, sizeof full);
    configure(g);
    g_live = g;
    return g;
fail:
    gpu_log("gpu: unavailable, using CPU presentation\n");
    gpu_destroy(g);
    return NULL;
}

void gpu_resize(Gpu *g, int w, int h) { if (g && (w != g->w || h != g->h)) { g->w = w; g->h = h; configure(g); } }

static WGPUTexture mk_tex(Gpu *g, int w, int h, WGPUTextureFormat f);
static WGPUBindGroup make_bg(Gpu *g, WGPUTextureView v, WGPUSampler s, WGPUBuffer b) {
    WGPUBindGroupEntry e[3] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
    e[0].binding = 0; e[0].textureView = v; e[1].binding = 1; e[1].sampler = s; e[2].binding = 2; e[2].buffer = b; e[2].size = 32;
    WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT; bd.layout = g->bgl; bd.entryCount = 3; bd.entries = e;
    return wgpuDeviceCreateBindGroup(g->dev, &bd);
}
static void ensure_vtex(Gpu *g, int w, int h) {
    if (g->vtex && g->vtw == w && g->vth == h) return;
    g->vserial = 0;
    if (g->vbg) wgpuBindGroupRelease(g->vbg);
    if (g->vview) wgpuTextureViewRelease(g->vview);
    if (g->vtex) { wgpuTextureDestroy(g->vtex); wgpuTextureRelease(g->vtex); }
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.size = (WGPUExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    g->vtex = wgpuDeviceCreateTexture(g->dev, &td);
    g->vview = wgpuTextureCreateView(g->vtex, NULL);
    g->vbg = make_bg(g, g->vview, g->vsamp, g->vubuf);
    g->vtw = w; g->vth = h;
}
static void free_xtex(Gpu *g, int i) {
    if (g->xl[i].bg) wgpuBindGroupRelease(g->xl[i].bg);
    if (g->xl[i].view) wgpuTextureViewRelease(g->xl[i].view);
    if (g->xl[i].tex) { wgpuTextureDestroy(g->xl[i].tex); wgpuTextureRelease(g->xl[i].tex); }
    g->xl[i].bg = NULL; g->xl[i].view = NULL; g->xl[i].tex = NULL; g->xl[i].serial = 0;
}
static bool ensure_xubuf(Gpu *g, int i) {
    if (!g->xl[i].ubuf) {
        WGPUBufferDescriptor ub = WGPU_BUFFER_DESCRIPTOR_INIT; ub.size = 32; ub.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        if (!(g->xl[i].ubuf = wgpuDeviceCreateBuffer(g->dev, &ub))) return false;
    }
    return true;
}
static bool ensure_xtex(Gpu *g, int i, int w, int h) {
    if (!ensure_xubuf(g, i)) return false;
    if (g->xl[i].tex && g->xl[i].w == w && g->xl[i].h == h) return true;
    free_xtex(g, i);
    g->xl[i].tex = mk_tex(g, w, h, WGPUTextureFormat_RGBA8Unorm);
    if (!g->xl[i].tex) return false;
    g->xl[i].view = wgpuTextureCreateView(g->xl[i].tex, NULL);
    g->xl[i].bg = make_bg(g, g->xl[i].view, g->vsamp, g->xl[i].ubuf);
    g->xl[i].w = w; g->xl[i].h = h;
    return g->xl[i].bg != NULL;
}
/* a shared texture view layer: slot 0 the primary layer, 1.. the extras; its bind group is made again only
   when the view changes (it holds the view, so the address cannot come back as another one meanwhile) */
static WGPUBindGroup view_bg(Gpu *g, int k, const GpuVideo *v, WGPUBuffer ub) {
    if (g->gview[k] != v->view || !g->gbg[k]) {
        if (g->gbg[k]) wgpuBindGroupRelease(g->gbg[k]);
        g->gbg[k] = make_bg(g, (WGPUTextureView)v->view, g->vsamp, ub);
        g->gview[k] = g->gbg[k] ? v->view : NULL;
    }
    return g->gbg[k];
}
static bool view_ok(Gpu *g, const GpuVideo *v) { return v && v->view && v->view_dev == (const void *)g->dev && v->w > 0 && v->h > 0; }
static void layer_rect(Gpu *g, const GpuVideo *v, WGPUBuffer ub, int w, int h) {
    union { float f[8]; uint32_t u[8]; } r = { { v->x0 / w * 2 - 1, 1 - v->y0 / h * 2, v->x1 / w * 2 - 1, 1 - v->y1 / h * 2, 0, 0, 0, 0 } };
    if (v->view) r.u[7] = 1u | (v->view_flags & GPU_VIEW_OPAQUE ? 2u : 0u);
    wgpuQueueWriteBuffer(g->q, ub, 0, r.f, sizeof r);
}
void gpu_set_extra_layers(Gpu *g, const GpuVideo *v, int n) {
    if (!g) return;
    g->nxv = n < 0 ? 0 : n > GPU_EXTRA_LAYERS ? GPU_EXTRA_LAYERS : n;
    for (int i = 0; i < g->nxv; i++) g->xv[i] = v[i];
}
static void free_ytex(Gpu *g) {
    g->yserial = 0;
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
static void reset_page_scroll(Gpu *gpu) {
    if (!gpu->scroll_height) return;
    uint32_t metadata[4] = {0};
    wgpuQueueWriteBuffer(gpu->q, gpu->ubuf, 16, metadata, sizeof metadata);
    gpu->scroll_toolbar = gpu->scroll_height = gpu->scroll_offset = 0;
}

static void ensure_tex(Gpu *g, int w, int h) {
    if (g->tex && g->tw == w && g->th == h) return;
    reset_page_scroll(g);
    if (g->bg) wgpuBindGroupRelease(g->bg);
    if (g->view) wgpuTextureViewRelease(g->view);
    if (g->tex) { wgpuTextureDestroy(g->tex); wgpuTextureRelease(g->tex); }
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.size = (WGPUExtent3D){ (uint32_t)w, (uint32_t)h, 1 };
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    g->tex = wgpuDeviceCreateTexture(g->dev, &td);
    g->view = wgpuTextureCreateView(g->tex, NULL);
    g->bg = make_bg(g, g->view, g->samp, g->ubuf);
    g->tw = w; g->th = h;
}

static bool page_upload_valid(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                              int toolbar, int first_row, int last_row) {
    if (!gpu || !gpu->configured || !gpu->tex || !gpu->q || !pixels || width <= 0 || height <= 0 ||
        gpu->tw != width || gpu->th != height || gpu->w != width || gpu->h != height ||
        stride < width || (uint32_t)stride > UINT32_MAX / 4 || toolbar < 0 || toolbar >= height ||
        first_row < 0 || last_row <= first_row || last_row > height - toolbar) return false;
    if ((size_t)(height - toolbar) > SIZE_MAX / ((size_t)stride * 4)) return false;
    return !gpu->scroll_height || (gpu->scroll_toolbar == toolbar && gpu->scroll_height == height - toolbar);
}

static void upload_page_cols(Gpu *gpu, const uint32_t *pixels, int x0, int x1, int height, int stride,
                             int toolbar, int first_row, int last_row, int source_offset) {
    int width = x1 - x0;
    pixels += x0;
    while (first_row < last_row) {
        int mapped = (int)(((int64_t)first_row + gpu->scroll_offset) % (height - toolbar));
        int source = (int)(((int64_t)first_row + source_offset) % (height - toolbar));
        int rows = last_row - first_row;
        if (rows > height - toolbar - source) rows = height - toolbar - source;
        if (rows > height - toolbar - mapped) rows = height - toolbar - mapped;
        size_t row_bytes = (size_t)stride * 4;
        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = gpu->tex;
        destination.origin = (WGPUOrigin3D){(uint32_t)x0, (uint32_t)(toolbar + mapped), 0};
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = (uint32_t)row_bytes;
        layout.rowsPerImage = (uint32_t)rows;
        WGPUExtent3D extent = {(uint32_t)width, (uint32_t)rows, 1};
        size_t bytes = (size_t)(rows - 1) * row_bytes + (size_t)width * 4;
        wgpuQueueWriteTexture(gpu->q, &destination, pixels + (size_t)source * (size_t)stride,
                             bytes, &layout, &extent);
        first_row += rows;
    }
}
static void upload_page_rows(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                             int toolbar, int first_row, int last_row, int source_offset) {
    upload_page_cols(gpu, pixels, 0, width, height, stride, toolbar, first_row, last_row, source_offset);
}

bool gpu_upload_page_ring(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                     int toolbar, int first_row, int last_row, int source_offset) {
    if (source_offset < 0 || (int64_t)source_offset >= (int64_t)height - toolbar) return false;
    if (!page_upload_valid(gpu, pixels, width, height, stride, toolbar, first_row, last_row)) return false;
    upload_page_rows(gpu, pixels, width, height, stride, toolbar, first_row, last_row, source_offset);
    return true;
}

bool gpu_upload_page_rect_ring(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                               int toolbar, int x0, int x1, int first_row, int last_row, int source_offset) {
    if (source_offset < 0 || (int64_t)source_offset >= (int64_t)height - toolbar) return false;
    if (!page_upload_valid(gpu, pixels, width, height, stride, toolbar, first_row, last_row)) return false;
    if (x0 < 0) x0 = 0;
    if (x1 > width) x1 = width;
    if (x1 > x0) upload_page_cols(gpu, pixels, x0, x1, height, stride, toolbar, first_row, last_row, source_offset);
    return true;
}

bool gpu_scroll_page_ring(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                     int toolbar, int delta, int fixed_first, int fixed_last, int source_offset) {
    if (source_offset < 0 || (int64_t)source_offset >= (int64_t)height - toolbar) return false;
    if (!page_upload_valid(gpu, pixels, width, height, stride, toolbar, 0,
                           toolbar >= 0 && height > toolbar ? height - toolbar : 0) || !gpu->ubuf) return false;
    int page_height = height - toolbar;
    if (!delta || delta <= -page_height || delta >= page_height || fixed_first < 0 ||
        fixed_last < fixed_first || fixed_last > page_height) return false;
    int offset = (int)(((int64_t)gpu->scroll_offset + delta) % page_height);
    if (offset < 0) offset += page_height;
    uint32_t metadata[4] = {(uint32_t)toolbar, (uint32_t)page_height, (uint32_t)offset, 0};
    wgpuQueueWriteBuffer(gpu->q, gpu->ubuf, 16, metadata, sizeof metadata);
    gpu->scroll_toolbar = toolbar; gpu->scroll_height = page_height; gpu->scroll_offset = offset;
    int first = delta > 0 ? page_height - delta : 0;
    int last = delta > 0 ? page_height : -delta;
    if (fixed_last > fixed_first) {
        if (fixed_first <= last && fixed_last >= first) {
            if (fixed_first < first) first = fixed_first;
            if (fixed_last > last) last = fixed_last;
        } else upload_page_rows(gpu, pixels, width, height, stride, toolbar, fixed_first, fixed_last, source_offset);
    }
    upload_page_rows(gpu, pixels, width, height, stride, toolbar, first, last, source_offset);
    return true;
}

/* Linear-source entry points retained for other presenters and callers. */
bool gpu_upload_page(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                     int toolbar, int first_row, int last_row) {
    return gpu_upload_page_ring(gpu, pixels, width, height, stride, toolbar, first_row, last_row, 0);
}
bool gpu_scroll_page(Gpu *gpu, const uint32_t *pixels, int width, int height, int stride,
                     int toolbar, int delta, int fixed_first, int fixed_last) {
    return gpu_scroll_page_ring(gpu, pixels, width, height, stride, toolbar, delta, fixed_first, fixed_last, 0);
}

bool gpu_present(Gpu *g, const uint32_t *px, int w, int h, int stride) { return gpu_present_frame(g, px, w, h, stride, 0, h, NULL); }
bool gpu_present_rows(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1) { return gpu_present_frame(g, px, w, h, stride, y0, y1, NULL); }
bool gpu_present_frame(Gpu *g, const uint32_t *px, int w, int h, int stride, int y0, int y1, const GpuVideo *v) {
    if (!g) return false;
    if (!g->configured) { g->nxv = 0; return false; }
    if (!g->tex || g->tw != w || g->th != h) { y0 = 0; y1 = h; }
    /* rows above the page ring (the tab strip) upload in place; anything lower ends the ring */
    if (y1 > y0 && g->scroll_height && !(y0 == 0 && y1 <= (int)g->scroll_toolbar)) { reset_page_scroll(g); y0 = 0; y1 = h; }
    ensure_tex(g, w, h);
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    if (y1 > y0) {
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = g->tex; dst.origin = (WGPUOrigin3D){ 0, (uint32_t)y0, 0 };
        WGPUTexelCopyBufferLayout lay = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay.bytesPerRow = (uint32_t)stride * 4; lay.rowsPerImage = (uint32_t)(y1 - y0);
        WGPUExtent3D ext = { (uint32_t)w, (uint32_t)(y1 - y0), 1 };
        wgpuQueueWriteTexture(g->q, &dst, px + (size_t)y0 * (size_t)stride, (size_t)stride * 4 * (size_t)(y1 - y0), &lay, &ext);
    }
    bool gvw = view_ok(g, v), yv = !gvw && v && v->yuv && gpu_yuv_ok(g) && v->w > 0 && v->h > 0, vid = !gvw && !yv && v && !v->view && v->px && v->w > 0 && v->h > 0;
    WGPUBindGroup vbg = gvw ? view_bg(g, 0, v, g->vubuf) : NULL;
    if (gvw && vbg) layer_rect(g, v, g->vubuf, w, h);
    gvw = vbg != NULL;
    if (yv) {
        ensure_ytex(g, v->w, v->h);
        if (!v->serial || v->serial != g->yserial) {
            int cw = (v->w + 1) / 2, ch = (v->h + 1) / 2;
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = g->yt;
            WGPUTexelCopyBufferLayout lay = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay.bytesPerRow = (uint32_t)v->w; lay.rowsPerImage = (uint32_t)v->h;
            WGPUExtent3D ext = { (uint32_t)v->w, (uint32_t)v->h, 1 };
            wgpuQueueWriteTexture(g->q, &dst, v->yuv, (size_t)v->w * (size_t)v->h, &lay, &ext);
            dst.texture = g->ct; lay.bytesPerRow = (uint32_t)cw * 2; lay.rowsPerImage = (uint32_t)ch; ext = (WGPUExtent3D){ (uint32_t)cw, (uint32_t)ch, 1 };
            wgpuQueueWriteTexture(g->q, &dst, v->yuv + (size_t)v->w * (size_t)v->h, (size_t)cw * 2 * (size_t)ch, &lay, &ext);
            g->yserial = v->serial;
        }
        bool b709 = v->mat & 1, full = v->mat & 2;
        float u[12] = { v->x0 / w * 2 - 1, 1 - v->y0 / h * 2, v->x1 / w * 2 - 1, 1 - v->y1 / h * 2,
                        b709 ? 1.5748f : 1.402f, b709 ? 0.1873f : 0.344136f, b709 ? 0.4681f : 0.714136f, b709 ? 1.8556f : 1.772f,
                        full ? 0 : 16.f / 255, full ? 1 : 255.f / 219, full ? 1 : 255.f / 224, 0 };
        wgpuQueueWriteBuffer(g->q, g->yubuf, 0, u, sizeof u);
    }
    if (vid) {
        ensure_vtex(g, v->w, v->h);
        if (!v->serial || v->serial != g->vserial) {
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = g->vtex;
            WGPUTexelCopyBufferLayout lay = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay.bytesPerRow = (uint32_t)v->stride * 4; lay.rowsPerImage = (uint32_t)v->h;
            WGPUExtent3D ext = { (uint32_t)v->w, (uint32_t)v->h, 1 };
            wgpuQueueWriteTexture(g->q, &dst, v->px, (size_t)v->stride * 4 * (size_t)v->h, &lay, &ext);
            g->vserial = v->serial;
        }
        layer_rect(g, v, g->vubuf, w, h);
    }
    int nx = 0;   /* extra layers: uploaded only when their frame changed, drawn after the primary one */
    WGPUBindGroup xbg[GPU_EXTRA_LAYERS] = { NULL };
    for (int i = 0; i < g->nxv; i++) {
        const GpuVideo *x = &g->xv[i];
        if (x->view) {
            if (!view_ok(g, x) || !ensure_xubuf(g, i) || !(xbg[i] = view_bg(g, 1 + i, x, g->xl[i].ubuf))) continue;
            layer_rect(g, x, g->xl[i].ubuf, w, h);
            nx++;
            continue;
        }
        if (!x->px || x->w <= 0 || x->h <= 0 || !ensure_xtex(g, i, x->w, x->h)) continue;
        if (!x->serial || x->serial != g->xl[i].serial) {
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT; dst.texture = g->xl[i].tex;
            WGPUTexelCopyBufferLayout lay = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT; lay.bytesPerRow = (uint32_t)x->stride * 4; lay.rowsPerImage = (uint32_t)x->h;
            WGPUExtent3D ext = { (uint32_t)x->w, (uint32_t)x->h, 1 };
            wgpuQueueWriteTexture(g->q, &dst, x->px, (size_t)x->stride * 4 * (size_t)x->h, &lay, &ext);
            g->xl[i].serial = x->serial;
        }
        layer_rect(g, x, g->xl[i].ubuf, w, h);
        xbg[i] = g->xl[i].bg; nx++;
    }
    for (int k = 0; k <= GPU_EXTRA_LAYERS; k++)   /* views no longer shown are let go of */
        if (g->gbg[k] && !(k ? k <= g->nxv && xbg[k - 1] == g->gbg[k] : gvw)) { wgpuBindGroupRelease(g->gbg[k]); g->gbg[k] = NULL; g->gview[k] = NULL; }
    g->nxv = 0;

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
    if (vid || gvw) { wgpuRenderPassEncoderSetBindGroup(pass, 0, gvw ? vbg : g->vbg, 0, NULL); wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0); }
    for (int i = 0; nx && i < GPU_EXTRA_LAYERS; i++) if (xbg[i]) { wgpuRenderPassEncoderSetBindGroup(pass, 0, xbg[i], 0, NULL); wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0); }
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

bool gpu_shared(GpuShared *o) {
    Gpu *g = g_live;
    if (!g || !g->full || !o) return false;
    o->inst = g->inst; o->adapter = g->adapter; o->dev = g->dev; o->queue = g->q;
    return true;
}

void gpu_destroy(Gpu *g) {
    if (!g) return;
    if (g_live == g) g_live = NULL;
    for (int i = 0; i <= GPU_EXTRA_LAYERS; i++) if (g->gbg[i]) wgpuBindGroupRelease(g->gbg[i]);
    if (g->bg) wgpuBindGroupRelease(g->bg);
    if (g->view) wgpuTextureViewRelease(g->view);
    if (g->tex) { wgpuTextureDestroy(g->tex); wgpuTextureRelease(g->tex); }
    if (g->samp) wgpuSamplerRelease(g->samp);
    free_ytex(g);
    for (int i = 0; i < GPU_EXTRA_LAYERS; i++) { free_xtex(g, i); if (g->xl[i].ubuf) wgpuBufferRelease(g->xl[i].ubuf); }
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
    gpu_suspend(g);
    if (g->q) wgpuQueueRelease(g->q);
    if (g->dev) wgpuDeviceRelease(g->dev);
    if (g->adapter) wgpuAdapterRelease(g->adapter);
    if (g->inst) wgpuInstanceRelease(g->inst);
    free(g);
}
