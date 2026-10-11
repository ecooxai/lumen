/* WebGPU for pages: a shared wgpu-native device, object handles for JS, command lists replayed natively.
   Canvas output is read back into the <canvas> backing store. */
#include "js_int.h"
#include <cstring>
#include <string>
#include <vector>
#include <webgpu.h>
#include <wgpu.h>
extern "C" {
struct CanvasBuf;
CanvasBuf *cvbuf_for(Node *n, bool create);
void cvbuf_resize(CanvasBuf *c, int w, int h);
void cvbuf_touch(CanvasBuf *c);
uint32_t *cvbuf_px(CanvasBuf *c, int *w, int *h);
}
using FCI = v8::FunctionCallbackInfo<v8::Value>;
using VL = v8::Local<v8::Value>;
#define FN(nm) static void n_##nm(const FCI &a)
#define CTX JsCtx *c = jctx(a.GetIsolate()); v8::Isolate *iso = c->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext(); (void)c
#define NUM(i) a[i]->NumberValue(ctx).FromMaybe(0)

enum { T_BUF = 1, T_TEX, T_VIEW, T_SAMP, T_SHADER, T_BGL, T_PL, T_BG, T_RP, T_CP };
static WGPUInstance gi; static WGPUAdapter ga; static WGPUDevice gd; static WGPUQueue gq;
static int ginit; static std::string ginfo, glast_err;
static std::vector<void *> H(1); static std::vector<uint8_t> HT(1);
static int put(void *p, int t) { if (!p) return 0; H.push_back(p); HT.push_back((uint8_t)t); return (int)H.size() - 1; }
template <class T> static T get(int id, int t) { return id > 0 && id < (int)H.size() && HT[id] == t ? (T)H[id] : nullptr; }
static WGPUStringView sv(const std::string &s) { return WGPUStringView{ s.data(), s.size() }; }

static VL P(v8::Local<v8::Context> ctx, VL o, const char *k) {
    if (o.IsEmpty() || !o->IsObject()) return v8::Undefined(v8::Isolate::GetCurrent());
    return o.As<v8::Object>()->Get(ctx, jstr(v8::Isolate::GetCurrent(), k)).FromMaybe(VL(v8::Undefined(v8::Isolate::GetCurrent())));
}
static double D(v8::Local<v8::Context> ctx, VL o, const char *k, double def) { VL v = P(ctx, o, k); return v->IsNumber() || v->IsBoolean() ? v->NumberValue(ctx).FromMaybe(def) : def; }
static std::string Sx(v8::Local<v8::Context> ctx, VL o, const char *k) { VL v = P(ctx, o, k); if (!v->IsString()) return ""; v8::String::Utf8Value u(v8::Isolate::GetCurrent(), v); return *u ? *u : ""; }
static int len(v8::Local<v8::Context> ctx, VL v) { return v->IsArray() ? (int)v.As<v8::Array>()->Length() : 0; }
static VL at(v8::Local<v8::Context> ctx, VL v, int i) { return v.As<v8::Array>()->Get(ctx, i).FromMaybe(VL(v8::Undefined(v8::Isolate::GetCurrent()))); }

struct EN { const char *s; int v; };
static int lk(const EN *t, const std::string &s, int def) { for (; t->s; t++) if (s == t->s) return t->v; return def; }
static const EN TFMT[] = { { "rgba8unorm", WGPUTextureFormat_RGBA8Unorm }, { "rgba8unorm-srgb", WGPUTextureFormat_RGBA8UnormSrgb }, { "bgra8unorm", WGPUTextureFormat_BGRA8Unorm }, { "bgra8unorm-srgb", WGPUTextureFormat_BGRA8UnormSrgb },
    { "r8unorm", WGPUTextureFormat_R8Unorm }, { "rg8unorm", WGPUTextureFormat_RG8Unorm }, { "r16float", WGPUTextureFormat_R16Float }, { "rg16float", WGPUTextureFormat_RG16Float }, { "rgba16float", WGPUTextureFormat_RGBA16Float },
    { "r32float", WGPUTextureFormat_R32Float }, { "rg32float", WGPUTextureFormat_RG32Float }, { "rgba32float", WGPUTextureFormat_RGBA32Float }, { "r32uint", WGPUTextureFormat_R32Uint }, { "r32sint", WGPUTextureFormat_R32Sint },
    { "rgba8uint", WGPUTextureFormat_RGBA8Uint }, { "rgba32uint", WGPUTextureFormat_RGBA32Uint }, { "rgb10a2unorm", WGPUTextureFormat_RGB10A2Unorm }, { "rgba8snorm", WGPUTextureFormat_RGBA8Snorm },
    { "depth16unorm", WGPUTextureFormat_Depth16Unorm }, { "depth24plus", WGPUTextureFormat_Depth24Plus }, { "depth24plus-stencil8", WGPUTextureFormat_Depth24PlusStencil8 }, { "depth32float", WGPUTextureFormat_Depth32Float }, { "stencil8", WGPUTextureFormat_Stencil8 }, { nullptr, 0 } };
static const EN VFMT[] = { { "float32", WGPUVertexFormat_Float32 }, { "float32x2", WGPUVertexFormat_Float32x2 }, { "float32x3", WGPUVertexFormat_Float32x3 }, { "float32x4", WGPUVertexFormat_Float32x4 },
    { "uint32", WGPUVertexFormat_Uint32 }, { "uint32x2", WGPUVertexFormat_Uint32x2 }, { "uint32x3", WGPUVertexFormat_Uint32x3 }, { "uint32x4", WGPUVertexFormat_Uint32x4 }, { "sint32", WGPUVertexFormat_Sint32 }, { "sint32x2", WGPUVertexFormat_Sint32x2 }, { "sint32x3", WGPUVertexFormat_Sint32x3 }, { "sint32x4", WGPUVertexFormat_Sint32x4 },
    { "unorm8x2", WGPUVertexFormat_Unorm8x2 }, { "unorm8x4", WGPUVertexFormat_Unorm8x4 }, { "snorm8x2", WGPUVertexFormat_Snorm8x2 }, { "snorm8x4", WGPUVertexFormat_Snorm8x4 }, { "uint8x2", WGPUVertexFormat_Uint8x2 }, { "uint8x4", WGPUVertexFormat_Uint8x4 }, { "sint8x2", WGPUVertexFormat_Sint8x2 }, { "sint8x4", WGPUVertexFormat_Sint8x4 },
    { "unorm16x2", WGPUVertexFormat_Unorm16x2 }, { "unorm16x4", WGPUVertexFormat_Unorm16x4 }, { "snorm16x2", WGPUVertexFormat_Snorm16x2 }, { "snorm16x4", WGPUVertexFormat_Snorm16x4 }, { "uint16x2", WGPUVertexFormat_Uint16x2 }, { "uint16x4", WGPUVertexFormat_Uint16x4 }, { "sint16x2", WGPUVertexFormat_Sint16x2 }, { "sint16x4", WGPUVertexFormat_Sint16x4 },
    { "float16x2", WGPUVertexFormat_Float16x2 }, { "float16x4", WGPUVertexFormat_Float16x4 }, { "unorm8", WGPUVertexFormat_Unorm8 }, { "uint8", WGPUVertexFormat_Uint8 }, { "uint16", WGPUVertexFormat_Uint16 }, { "unorm16", WGPUVertexFormat_Unorm16 }, { "float16", WGPUVertexFormat_Float16 }, { nullptr, 0 } };
static const EN BF[] = { { "zero", WGPUBlendFactor_Zero }, { "one", WGPUBlendFactor_One }, { "src", WGPUBlendFactor_Src }, { "one-minus-src", WGPUBlendFactor_OneMinusSrc }, { "src-alpha", WGPUBlendFactor_SrcAlpha }, { "one-minus-src-alpha", WGPUBlendFactor_OneMinusSrcAlpha },
    { "dst", WGPUBlendFactor_Dst }, { "one-minus-dst", WGPUBlendFactor_OneMinusDst }, { "dst-alpha", WGPUBlendFactor_DstAlpha }, { "one-minus-dst-alpha", WGPUBlendFactor_OneMinusDstAlpha }, { "src-alpha-saturated", WGPUBlendFactor_SrcAlphaSaturated }, { "constant", WGPUBlendFactor_Constant }, { "one-minus-constant", WGPUBlendFactor_OneMinusConstant }, { nullptr, 0 } };
static const EN BO[] = { { "add", WGPUBlendOperation_Add }, { "subtract", WGPUBlendOperation_Subtract }, { "reverse-subtract", WGPUBlendOperation_ReverseSubtract }, { "min", WGPUBlendOperation_Min }, { "max", WGPUBlendOperation_Max }, { nullptr, 0 } };
static const EN CMP[] = { { "never", WGPUCompareFunction_Never }, { "less", WGPUCompareFunction_Less }, { "equal", WGPUCompareFunction_Equal }, { "less-equal", WGPUCompareFunction_LessEqual }, { "greater", WGPUCompareFunction_Greater }, { "not-equal", WGPUCompareFunction_NotEqual }, { "greater-equal", WGPUCompareFunction_GreaterEqual }, { "always", WGPUCompareFunction_Always }, { nullptr, 0 } };
static const EN TOPO[] = { { "point-list", WGPUPrimitiveTopology_PointList }, { "line-list", WGPUPrimitiveTopology_LineList }, { "line-strip", WGPUPrimitiveTopology_LineStrip }, { "triangle-list", WGPUPrimitiveTopology_TriangleList }, { "triangle-strip", WGPUPrimitiveTopology_TriangleStrip }, { nullptr, 0 } };
static const EN SOPS[] = { { "keep", WGPUStencilOperation_Keep }, { "zero", WGPUStencilOperation_Zero }, { "replace", WGPUStencilOperation_Replace }, { "invert", WGPUStencilOperation_Invert }, { "increment-clamp", WGPUStencilOperation_IncrementClamp }, { "decrement-clamp", WGPUStencilOperation_DecrementClamp }, { "increment-wrap", WGPUStencilOperation_IncrementWrap }, { "decrement-wrap", WGPUStencilOperation_DecrementWrap }, { nullptr, 0 } };
static const EN VDIM[] = { { "1d", WGPUTextureViewDimension_1D }, { "2d", WGPUTextureViewDimension_2D }, { "2d-array", WGPUTextureViewDimension_2DArray }, { "cube", WGPUTextureViewDimension_Cube }, { "cube-array", WGPUTextureViewDimension_CubeArray }, { "3d", WGPUTextureViewDimension_3D }, { nullptr, 0 } };
static int tfmt(const std::string &s) { return lk(TFMT, s, WGPUTextureFormat_Undefined); }

static void on_err(WGPUDevice const *, WGPUErrorType t, WGPUStringView m, void *, void *) {
    glast_err.assign(m.data ? m.data : "", m.data ? (m.length == WGPU_STRLEN ? strlen(m.data) : m.length) : 0);
    fprintf(stderr, "webgpu: %.300s\n", glast_err.c_str());
}
static void on_ad(WGPURequestAdapterStatus st, WGPUAdapter ad, WGPUStringView, void *u, void *) { if (st == WGPURequestAdapterStatus_Success) *(WGPUAdapter *)u = ad; }
static void on_dev(WGPURequestDeviceStatus st, WGPUDevice d, WGPUStringView, void *u, void *) { if (st == WGPURequestDeviceStatus_Success) *(WGPUDevice *)u = d; }
static bool ensure() {
    if (ginit) return gd != nullptr;
    ginit = 1;
    const char *nb = getenv("LUMEN_GPU_BACKEND");
    if (getenv("LUMEN_NO_GPU") || getenv("LUMEN_NO_WEBGPU") || (nb && !strcmp(nb, "cpu"))) return false;
    WGPUInstanceExtras ex = {}; ex.chain.sType = (WGPUSType)WGPUSType_InstanceExtras;
    ex.backends = nb && !strcmp(nb, "vulkan") ? WGPUInstanceBackend_Vulkan : nb && (!strcmp(nb, "gl") || !strcmp(nb, "opengl")) ? WGPUInstanceBackend_GL : WGPUInstanceBackend_Primary;
    WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT; id.nextInChain = &ex.chain;
    if (!(gi = wgpuCreateInstance(&id))) return false;
    WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT; ao.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo ac = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT; ac.mode = WGPUCallbackMode_AllowProcessEvents; ac.callback = on_ad; ac.userdata1 = &ga;
    wgpuInstanceRequestAdapter(gi, &ao, ac); wgpuInstanceProcessEvents(gi);
    if (!ga) return false;
    WGPUAdapterInfo inf = WGPU_ADAPTER_INFO_INIT; wgpuAdapterGetInfo(ga, &inf);
    ginfo.assign(inf.device.data ? inf.device.data : "gpu", inf.device.data ? (inf.device.length == WGPU_STRLEN ? strlen(inf.device.data) : inf.device.length) : 3);
    bool sw = inf.adapterType == WGPUAdapterType_CPU; wgpuAdapterInfoFreeMembers(inf);
    if (sw && !getenv("LUMEN_ALLOW_SW_GPU")) { fprintf(stderr, "webgpu: %s is a software rasterizer, WebGPU disabled\n", ginfo.c_str()); return false; }
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT; dd.uncapturedErrorCallbackInfo.callback = on_err;
    WGPURequestDeviceCallbackInfo dc = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT; dc.mode = WGPUCallbackMode_AllowProcessEvents; dc.callback = on_dev; dc.userdata1 = &gd;
    wgpuAdapterRequestDevice(ga, &dd, dc); wgpuInstanceProcessEvents(gi);
    if (!gd) return false;
    gq = wgpuDeviceGetQueue(gd);
    fprintf(stderr, "webgpu: device on %s\n", ginfo.c_str());
    return true;
}
FN(gpuInit) { CTX; if (ensure()) a.GetReturnValue().Set(jstr(iso, ginfo.c_str())); else a.GetReturnValue().SetNull(); }
FN(gpuErr) { CTX; std::string e; e.swap(glast_err); a.GetReturnValue().Set(jstr(iso, e.c_str())); }
FN(gpuDrop) {
    CTX; int id = (int)NUM(0); if (id <= 0 || id >= (int)H.size() || !H[id]) return;
    void *p = H[id];
    switch (HT[id]) {
    case T_BUF: wgpuBufferDestroy((WGPUBuffer)p); wgpuBufferRelease((WGPUBuffer)p); break;
    case T_TEX: wgpuTextureDestroy((WGPUTexture)p); wgpuTextureRelease((WGPUTexture)p); break;
    case T_VIEW: wgpuTextureViewRelease((WGPUTextureView)p); break;
    case T_SAMP: wgpuSamplerRelease((WGPUSampler)p); break;
    case T_SHADER: wgpuShaderModuleRelease((WGPUShaderModule)p); break;
    case T_BGL: wgpuBindGroupLayoutRelease((WGPUBindGroupLayout)p); break;
    case T_PL: wgpuPipelineLayoutRelease((WGPUPipelineLayout)p); break;
    case T_BG: wgpuBindGroupRelease((WGPUBindGroup)p); break;
    case T_RP: wgpuRenderPipelineRelease((WGPURenderPipeline)p); break;
    case T_CP: wgpuComputePipelineRelease((WGPUComputePipeline)p); break;
    }
    H[id] = nullptr; HT[id] = 0;
}
/* gpuBuffer(size, usage, mappedAtCreation) */
FN(gpuBuffer) { CTX; if (!ensure()) return; WGPUBufferDescriptor d = WGPU_BUFFER_DESCRIPTOR_INIT; d.size = ((uint64_t)NUM(0) + 3) & ~3ull; d.usage = (WGPUBufferUsage)NUM(1); d.mappedAtCreation = a[2]->BooleanValue(iso);
    a.GetReturnValue().Set(put(wgpuDeviceCreateBuffer(gd, &d), T_BUF)); }
FN(gpuWriteBuffer) { CTX; WGPUBuffer b = get<WGPUBuffer>((int)NUM(0), T_BUF); if (!b || !a[2]->IsArrayBufferView()) return;
    auto v = a[2].As<v8::ArrayBufferView>(); size_t n = v->ByteLength(); if (a[3]->IsNumber()) n = std::min(n, (size_t)NUM(3));
    wgpuQueueWriteBuffer(gq, b, (uint64_t)NUM(1), (const uint8_t *)v->Buffer()->Data() + v->ByteOffset(), n & ~(size_t)3); }
static void on_map(WGPUMapAsyncStatus st, WGPUStringView, void *u, void *) { *(int *)u = st == WGPUMapAsyncStatus_Success ? 1 : -1; }
static bool map_sync(WGPUBuffer b, WGPUMapMode m, size_t off, size_t n) {
    int done = 0; WGPUBufferMapCallbackInfo ci = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT; ci.mode = WGPUCallbackMode_AllowProcessEvents; ci.callback = on_map; ci.userdata1 = &done;
    wgpuBufferMapAsync(b, m, off, n, ci);
    for (int i = 0; !done && i < 100000; i++) { wgpuDevicePoll(gd, true, nullptr); wgpuInstanceProcessEvents(gi); }
    return done == 1;
}
/* gpuMapRead(id, off, size) -> ArrayBuffer copy (buffer is unmapped again) */
FN(gpuMapRead) { CTX; WGPUBuffer b = get<WGPUBuffer>((int)NUM(0), T_BUF); if (!b) return; size_t off = (size_t)NUM(1), n = (size_t)NUM(2);
    if (!map_sync(b, WGPUMapMode_Read, off, n)) { a.GetReturnValue().SetNull(); return; }
    auto ab = v8::ArrayBuffer::New(iso, n); const void *p = wgpuBufferGetConstMappedRange(b, off, n); if (p) memcpy(ab->Data(), p, n);
    wgpuBufferUnmap(b); a.GetReturnValue().Set(ab); }
/* gpuUnmapWrite(id, off, bytes, mappedAtCreation) */
FN(gpuUnmapWrite) { CTX; WGPUBuffer b = get<WGPUBuffer>((int)NUM(0), T_BUF); if (!b) return; size_t off = (size_t)NUM(1);
    bool created = a[3]->BooleanValue(iso);
    if (a[2]->IsArrayBufferView() || a[2]->IsArrayBuffer()) {
        const uint8_t *src; size_t n;
        if (a[2]->IsArrayBuffer()) { auto ab = a[2].As<v8::ArrayBuffer>(); src = (const uint8_t *)ab->Data(); n = ab->ByteLength(); }
        else { auto v = a[2].As<v8::ArrayBufferView>(); src = (const uint8_t *)v->Buffer()->Data() + v->ByteOffset(); n = v->ByteLength(); }
        if (created || map_sync(b, WGPUMapMode_Write, off, n)) { void *p = wgpuBufferGetMappedRange(b, off, n); if (p) memcpy(p, src, n); }
    }
    wgpuBufferUnmap(b); }
/* gpuTexture(w, h, layers, format, usage, mips, samples, dim) */
FN(gpuTexture) { CTX; if (!ensure()) return; WGPUTextureDescriptor d = WGPU_TEXTURE_DESCRIPTOR_INIT;
    d.size = { (uint32_t)std::max(1.0, NUM(0)), (uint32_t)std::max(1.0, NUM(1)), (uint32_t)std::max(1.0, NUM(2)) };
    d.format = (WGPUTextureFormat)tfmt(jcstr(iso, a[3])); d.usage = (WGPUTextureUsage)NUM(4); d.mipLevelCount = (uint32_t)std::max(1.0, NUM(5)); d.sampleCount = (uint32_t)std::max(1.0, NUM(6));
    std::string dim = jcstr(iso, a[7]); d.dimension = dim == "3d" ? WGPUTextureDimension_3D : dim == "1d" ? WGPUTextureDimension_1D : WGPUTextureDimension_2D;
    a.GetReturnValue().Set(put(wgpuDeviceCreateTexture(gd, &d), T_TEX)); }
/* gpuView(tex, desc) */
FN(gpuView) { CTX; WGPUTexture t = get<WGPUTexture>((int)NUM(0), T_TEX); if (!t) return; VL o = a[1];
    WGPUTextureViewDescriptor d = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT; std::string f = Sx(ctx, o, "format"), dm = Sx(ctx, o, "dimension"), asp = Sx(ctx, o, "aspect");
    if (!f.empty()) d.format = (WGPUTextureFormat)tfmt(f); if (!dm.empty()) d.dimension = (WGPUTextureViewDimension)lk(VDIM, dm, 0);
    d.baseMipLevel = (uint32_t)D(ctx, o, "baseMipLevel", 0); d.mipLevelCount = (uint32_t)D(ctx, o, "mipLevelCount", WGPU_MIP_LEVEL_COUNT_UNDEFINED);
    d.baseArrayLayer = (uint32_t)D(ctx, o, "baseArrayLayer", 0); d.arrayLayerCount = (uint32_t)D(ctx, o, "arrayLayerCount", WGPU_ARRAY_LAYER_COUNT_UNDEFINED);
    d.aspect = asp == "depth-only" ? WGPUTextureAspect_DepthOnly : asp == "stencil-only" ? WGPUTextureAspect_StencilOnly : WGPUTextureAspect_All;
    a.GetReturnValue().Set(put(wgpuTextureCreateView(t, &d), T_VIEW)); }
static WGPUAddressMode am(const std::string &s) { return s == "repeat" ? WGPUAddressMode_Repeat : s == "mirror-repeat" ? WGPUAddressMode_MirrorRepeat : WGPUAddressMode_ClampToEdge; }
FN(gpuSampler) { CTX; if (!ensure()) return; VL o = a[0]; WGPUSamplerDescriptor d = WGPU_SAMPLER_DESCRIPTOR_INIT;
    d.addressModeU = am(Sx(ctx, o, "addressModeU")); d.addressModeV = am(Sx(ctx, o, "addressModeV")); d.addressModeW = am(Sx(ctx, o, "addressModeW"));
    d.magFilter = Sx(ctx, o, "magFilter") == "linear" ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest; d.minFilter = Sx(ctx, o, "minFilter") == "linear" ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    d.mipmapFilter = Sx(ctx, o, "mipmapFilter") == "linear" ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
    d.lodMinClamp = (float)D(ctx, o, "lodMinClamp", 0); d.lodMaxClamp = (float)D(ctx, o, "lodMaxClamp", 32); d.maxAnisotropy = (uint16_t)D(ctx, o, "maxAnisotropy", 1);
    std::string cmp = Sx(ctx, o, "compare"); if (!cmp.empty()) d.compare = (WGPUCompareFunction)lk(CMP, cmp, 0);
    a.GetReturnValue().Set(put(wgpuDeviceCreateSampler(gd, &d), T_SAMP)); }
/* gpuShader(code, stage): stage 0 = WGSL, else GLSL 450 for WGPUShaderStage bits */
FN(gpuShader) { CTX; if (!ensure()) return; std::string code = jcstr(iso, a[0]); int st = (int)NUM(1);
    WGPUShaderModuleDescriptor d = WGPU_SHADER_MODULE_DESCRIPTOR_INIT; WGPUShaderSourceWGSL w = WGPU_SHADER_SOURCE_WGSL_INIT; WGPUShaderSourceGLSL g = {};
    if (!st) { w.code = sv(code); d.nextInChain = &w.chain; }
    else { g.chain.sType = (WGPUSType)WGPUSType_ShaderSourceGLSL; g.stage = (WGPUShaderStage)st; g.code = sv(code); d.nextInChain = &g.chain; }
    glast_err.clear();
    WGPUShaderModule m = wgpuDeviceCreateShaderModule(gd, &d);
    a.GetReturnValue().Set(put(m, T_SHADER)); }
FN(gpuBGL) { CTX; if (!ensure()) return; VL es = a[0]; int n = len(ctx, es); std::vector<WGPUBindGroupLayoutEntry> v(n);
    for (int i = 0; i < n; i++) { VL e = at(ctx, es, i); WGPUBindGroupLayoutEntry &x = v[i]; x = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        x.binding = (uint32_t)D(ctx, e, "binding", 0); x.visibility = (WGPUShaderStage)D(ctx, e, "visibility", 0);
        VL b = P(ctx, e, "buffer"), s = P(ctx, e, "sampler"), t = P(ctx, e, "texture"), st = P(ctx, e, "storageTexture");
        if (b->IsObject()) { std::string ty = Sx(ctx, b, "type"); x.buffer.type = ty == "storage" ? WGPUBufferBindingType_Storage : ty == "read-only-storage" ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Uniform; x.buffer.hasDynamicOffset = D(ctx, b, "hasDynamicOffset", 0) != 0; x.buffer.minBindingSize = (uint64_t)D(ctx, b, "minBindingSize", 0); }
        else if (s->IsObject()) { std::string ty = Sx(ctx, s, "type"); x.sampler.type = ty == "comparison" ? WGPUSamplerBindingType_Comparison : ty == "non-filtering" ? WGPUSamplerBindingType_NonFiltering : WGPUSamplerBindingType_Filtering; }
        else if (t->IsObject()) { std::string ty = Sx(ctx, t, "sampleType"), dm = Sx(ctx, t, "viewDimension"); x.texture.sampleType = ty == "depth" ? WGPUTextureSampleType_Depth : ty == "unfilterable-float" ? WGPUTextureSampleType_UnfilterableFloat : ty == "uint" ? WGPUTextureSampleType_Uint : ty == "sint" ? WGPUTextureSampleType_Sint : WGPUTextureSampleType_Float;
            x.texture.viewDimension = (WGPUTextureViewDimension)lk(VDIM, dm.empty() ? "2d" : dm, WGPUTextureViewDimension_2D); x.texture.multisampled = D(ctx, t, "multisampled", 0) != 0; }
        else if (st->IsObject()) { std::string ac = Sx(ctx, st, "access"), dm = Sx(ctx, st, "viewDimension"); x.storageTexture.access = ac == "read-only" ? WGPUStorageTextureAccess_ReadOnly : ac == "read-write" ? WGPUStorageTextureAccess_ReadWrite : WGPUStorageTextureAccess_WriteOnly;
            x.storageTexture.format = (WGPUTextureFormat)tfmt(Sx(ctx, st, "format")); x.storageTexture.viewDimension = (WGPUTextureViewDimension)lk(VDIM, dm.empty() ? "2d" : dm, WGPUTextureViewDimension_2D); } }
    WGPUBindGroupLayoutDescriptor d = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT; d.entryCount = n; d.entries = v.data();
    a.GetReturnValue().Set(put(wgpuDeviceCreateBindGroupLayout(gd, &d), T_BGL)); }
FN(gpuPL) { CTX; if (!ensure()) return; VL ls = a[0]; int n = len(ctx, ls); std::vector<WGPUBindGroupLayout> v(n);
    for (int i = 0; i < n; i++) v[i] = get<WGPUBindGroupLayout>((int)at(ctx, ls, i)->NumberValue(ctx).FromMaybe(0), T_BGL);
    WGPUPipelineLayoutDescriptor d = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT; d.bindGroupLayoutCount = n; d.bindGroupLayouts = v.data();
    a.GetReturnValue().Set(put(wgpuDeviceCreatePipelineLayout(gd, &d), T_PL)); }
/* gpuBG(layout, [{binding, buffer, offset, size, sampler, view}]) */
FN(gpuBG) { CTX; WGPUBindGroupLayout l = get<WGPUBindGroupLayout>((int)NUM(0), T_BGL); if (!l) return; VL es = a[1]; int n = len(ctx, es); std::vector<WGPUBindGroupEntry> v(n);
    for (int i = 0; i < n; i++) { VL e = at(ctx, es, i); WGPUBindGroupEntry &x = v[i]; x = WGPU_BIND_GROUP_ENTRY_INIT; x.binding = (uint32_t)D(ctx, e, "binding", 0);
        x.buffer = get<WGPUBuffer>((int)D(ctx, e, "buffer", 0), T_BUF); x.offset = (uint64_t)D(ctx, e, "offset", 0); x.size = D(ctx, e, "size", -1) < 0 ? WGPU_WHOLE_SIZE : (uint64_t)D(ctx, e, "size", 0);
        x.sampler = get<WGPUSampler>((int)D(ctx, e, "sampler", 0), T_SAMP); x.textureView = get<WGPUTextureView>((int)D(ctx, e, "view", 0), T_VIEW); }
    WGPUBindGroupDescriptor d = WGPU_BIND_GROUP_DESCRIPTOR_INIT; d.layout = l; d.entryCount = n; d.entries = v.data();
    a.GetReturnValue().Set(put(wgpuDeviceCreateBindGroup(gd, &d), T_BG)); }
static void stencil_face(v8::Local<v8::Context> ctx, VL f, WGPUStencilFaceState *s) {
    if (!f->IsObject()) return; std::string c = Sx(ctx, f, "compare"); if (!c.empty()) s->compare = (WGPUCompareFunction)lk(CMP, c, WGPUCompareFunction_Always);
    s->failOp = (WGPUStencilOperation)lk(SOPS, Sx(ctx, f, "failOp"), WGPUStencilOperation_Keep); s->depthFailOp = (WGPUStencilOperation)lk(SOPS, Sx(ctx, f, "depthFailOp"), WGPUStencilOperation_Keep); s->passOp = (WGPUStencilOperation)lk(SOPS, Sx(ctx, f, "passOp"), WGPUStencilOperation_Keep);
}
FN(gpuRP) { CTX; if (!ensure()) return; VL o = a[0];
    WGPURenderPipelineDescriptor d = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT; d.layout = get<WGPUPipelineLayout>((int)D(ctx, o, "layout", 0), T_PL);
    VL vx = P(ctx, o, "vertex"), fr = P(ctx, o, "fragment"), pr = P(ctx, o, "primitive"), ds = P(ctx, o, "depthStencil"), ms = P(ctx, o, "multisample");
    std::string ve = Sx(ctx, vx, "entryPoint"), fe = Sx(ctx, fr, "entryPoint");
    d.vertex.module = get<WGPUShaderModule>((int)D(ctx, vx, "module", 0), T_SHADER); if (!ve.empty()) d.vertex.entryPoint = sv(ve);
    VL bufs = P(ctx, vx, "buffers"); int nb = len(ctx, bufs); std::vector<WGPUVertexBufferLayout> vb(nb); std::vector<std::vector<WGPUVertexAttribute>> va(nb);
    for (int i = 0; i < nb; i++) { VL b = at(ctx, bufs, i); vb[i] = WGPU_VERTEX_BUFFER_LAYOUT_INIT; if (!b->IsObject()) { vb[i].stepMode = WGPUVertexStepMode_Undefined; continue; }
        vb[i].arrayStride = (uint64_t)D(ctx, b, "arrayStride", 0); vb[i].stepMode = Sx(ctx, b, "stepMode") == "instance" ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
        VL at_ = P(ctx, b, "attributes"); int na = len(ctx, at_); va[i].resize(na);
        for (int j = 0; j < na; j++) { VL t = at(ctx, at_, j); va[i][j] = WGPU_VERTEX_ATTRIBUTE_INIT; va[i][j].format = (WGPUVertexFormat)lk(VFMT, Sx(ctx, t, "format"), WGPUVertexFormat_Float32x4); va[i][j].offset = (uint64_t)D(ctx, t, "offset", 0); va[i][j].shaderLocation = (uint32_t)D(ctx, t, "shaderLocation", 0); }
        vb[i].attributeCount = na; vb[i].attributes = va[i].data(); }
    d.vertex.bufferCount = nb; d.vertex.buffers = vb.data();
    std::string topo = Sx(ctx, pr, "topology"), cull = Sx(ctx, pr, "cullMode"), sif = Sx(ctx, pr, "stripIndexFormat");
    d.primitive.topology = (WGPUPrimitiveTopology)lk(TOPO, topo.empty() ? "triangle-list" : topo, WGPUPrimitiveTopology_TriangleList);
    d.primitive.cullMode = cull == "front" ? WGPUCullMode_Front : cull == "back" ? WGPUCullMode_Back : WGPUCullMode_None;
    d.primitive.frontFace = Sx(ctx, pr, "frontFace") == "cw" ? WGPUFrontFace_CW : WGPUFrontFace_CCW;
    d.primitive.stripIndexFormat = sif == "uint16" ? WGPUIndexFormat_Uint16 : sif == "uint32" ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Undefined;
    WGPUDepthStencilState dss = WGPU_DEPTH_STENCIL_STATE_INIT;
    if (ds->IsObject()) { dss.format = (WGPUTextureFormat)tfmt(Sx(ctx, ds, "format")); dss.depthWriteEnabled = D(ctx, ds, "depthWriteEnabled", 0) ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        std::string dc = Sx(ctx, ds, "depthCompare"); dss.depthCompare = (WGPUCompareFunction)lk(CMP, dc.empty() ? "always" : dc, WGPUCompareFunction_Always);
        dss.stencilReadMask = (uint32_t)D(ctx, ds, "stencilReadMask", 0xffffffff); dss.stencilWriteMask = (uint32_t)D(ctx, ds, "stencilWriteMask", 0xffffffff);
        dss.stencilFront.compare = dss.stencilBack.compare = WGPUCompareFunction_Always;
        stencil_face(ctx, P(ctx, ds, "stencilFront"), &dss.stencilFront); stencil_face(ctx, P(ctx, ds, "stencilBack"), &dss.stencilBack);
        dss.depthBias = (int32_t)D(ctx, ds, "depthBias", 0); dss.depthBiasSlopeScale = (float)D(ctx, ds, "depthBiasSlopeScale", 0); d.depthStencil = &dss; }
    d.multisample.count = (uint32_t)D(ctx, ms, "count", 1); d.multisample.mask = 0xffffffff; d.multisample.alphaToCoverageEnabled = D(ctx, ms, "alphaToCoverageEnabled", 0) != 0;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT; VL ts = P(ctx, fr, "targets"); int nt = len(ctx, ts); std::vector<WGPUColorTargetState> ct(nt); std::vector<WGPUBlendState> bl(nt);
    if (fr->IsObject()) { fs.module = get<WGPUShaderModule>((int)D(ctx, fr, "module", 0), T_SHADER); if (!fe.empty()) fs.entryPoint = sv(fe);
        for (int i = 0; i < nt; i++) { VL t = at(ctx, ts, i); ct[i] = WGPU_COLOR_TARGET_STATE_INIT; ct[i].format = (WGPUTextureFormat)tfmt(Sx(ctx, t, "format")); ct[i].writeMask = (WGPUColorWriteMask)D(ctx, t, "writeMask", 0xf);
            VL b = P(ctx, t, "blend"); if (b->IsObject()) { VL bc = P(ctx, b, "color"), ba = P(ctx, b, "alpha");
                auto comp = [&](VL x, WGPUBlendComponent *o) { o->operation = (WGPUBlendOperation)lk(BO, Sx(ctx, x, "operation"), WGPUBlendOperation_Add); o->srcFactor = (WGPUBlendFactor)lk(BF, Sx(ctx, x, "srcFactor"), WGPUBlendFactor_One); o->dstFactor = (WGPUBlendFactor)lk(BF, Sx(ctx, x, "dstFactor"), WGPUBlendFactor_Zero); };
                comp(bc, &bl[i].color); comp(ba, &bl[i].alpha); ct[i].blend = &bl[i]; } }
        fs.targetCount = nt; fs.targets = ct.data(); d.fragment = &fs; }
    glast_err.clear();
    a.GetReturnValue().Set(put(wgpuDeviceCreateRenderPipeline(gd, &d), T_RP)); }
FN(gpuCP) { CTX; if (!ensure()) return; VL o = a[0]; WGPUComputePipelineDescriptor d = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    d.layout = get<WGPUPipelineLayout>((int)D(ctx, o, "layout", 0), T_PL); VL cp = P(ctx, o, "compute"); std::string e = Sx(ctx, cp, "entryPoint");
    d.compute.module = get<WGPUShaderModule>((int)D(ctx, cp, "module", 0), T_SHADER); if (!e.empty()) d.compute.entryPoint = sv(e);
    a.GetReturnValue().Set(put(wgpuDeviceCreateComputePipeline(gd, &d), T_CP)); }
FN(gpuPipeBGL) { CTX; int id = (int)NUM(0); uint32_t i = (uint32_t)NUM(1); WGPUBindGroupLayout l = nullptr;
    if (auto r = get<WGPURenderPipeline>(id, T_RP)) l = wgpuRenderPipelineGetBindGroupLayout(r, i); else if (auto cpp = get<WGPUComputePipeline>(id, T_CP)) l = wgpuComputePipelineGetBindGroupLayout(cpp, i);
    a.GetReturnValue().Set(put(l, T_BGL)); }
/* gpuWriteTexture(tex, mip, x, y, z, bytes, offset, bytesPerRow, rowsPerImage, w, h, d) */
FN(gpuWriteTexture) { CTX; WGPUTexture t = get<WGPUTexture>((int)NUM(0), T_TEX); if (!t || !a[5]->IsArrayBufferView()) return; auto v = a[5].As<v8::ArrayBufferView>();
    WGPUTexelCopyTextureInfo dst = { t, (uint32_t)NUM(1), { (uint32_t)NUM(2), (uint32_t)NUM(3), (uint32_t)NUM(4) }, WGPUTextureAspect_All };
    WGPUTexelCopyBufferLayout l = { (uint64_t)NUM(6), (uint32_t)NUM(7), (uint32_t)NUM(8) ? (uint32_t)NUM(8) : (uint32_t)NUM(10) };
    WGPUExtent3D ext = { (uint32_t)NUM(9), (uint32_t)NUM(10), (uint32_t)std::max(1.0, NUM(11)) };
    wgpuQueueWriteTexture(gq, &dst, (const uint8_t *)v->Buffer()->Data() + v->ByteOffset(), v->ByteLength(), &l, &ext); }
/* gpuExec(cmds): flat command list recorded by GPUCommandEncoder, replayed and submitted */
FN(gpuExec) { CTX; if (!gd || !a[0]->IsArray()) return; VL cl = a[0]; int n = len(ctx, cl);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(gd, nullptr); WGPURenderPassEncoder rp = nullptr; WGPUComputePassEncoder cp = nullptr;
    auto I = [&](VL c, int i) { return at(ctx, c, i)->NumberValue(ctx).FromMaybe(0); };
    for (int k = 0; k < n; k++) { VL c = at(ctx, cl, k); std::string op = jcstr(iso, at(ctx, c, 0));
        if (op == "rp") { VL cas = at(ctx, c, 1), dsa = at(ctx, c, 2); int nc = len(ctx, cas); std::vector<WGPURenderPassColorAttachment> ca(nc);
            for (int i = 0; i < nc; i++) { VL x = at(ctx, cas, i); ca[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT; if (!x->IsObject()) continue;
                ca[i].view = get<WGPUTextureView>((int)D(ctx, x, "view", 0), T_VIEW); ca[i].resolveTarget = get<WGPUTextureView>((int)D(ctx, x, "resolveTarget", 0), T_VIEW);
                ca[i].loadOp = Sx(ctx, x, "loadOp") == "clear" ? WGPULoadOp_Clear : WGPULoadOp_Load; ca[i].storeOp = Sx(ctx, x, "storeOp") == "discard" ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
                VL cv = P(ctx, x, "clearValue"); if (cv->IsArray()) ca[i].clearValue = { I(cv, 0), I(cv, 1), I(cv, 2), I(cv, 3) }; else if (cv->IsObject()) ca[i].clearValue = { D(ctx, cv, "r", 0), D(ctx, cv, "g", 0), D(ctx, cv, "b", 0), D(ctx, cv, "a", 0) }; }
            WGPURenderPassDescriptor d = WGPU_RENDER_PASS_DESCRIPTOR_INIT; d.colorAttachmentCount = nc; d.colorAttachments = ca.data(); WGPURenderPassDepthStencilAttachment da = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            if (dsa->IsObject()) { da.view = get<WGPUTextureView>((int)D(ctx, dsa, "view", 0), T_VIEW); std::string dl = Sx(ctx, dsa, "depthLoadOp"), sl = Sx(ctx, dsa, "stencilLoadOp");
                da.depthLoadOp = dl == "clear" ? WGPULoadOp_Clear : dl == "load" ? WGPULoadOp_Load : WGPULoadOp_Undefined; da.depthStoreOp = dl.empty() ? WGPUStoreOp_Undefined : Sx(ctx, dsa, "depthStoreOp") == "discard" ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
                da.depthClearValue = (float)D(ctx, dsa, "depthClearValue", 1); da.depthReadOnly = D(ctx, dsa, "depthReadOnly", 0) != 0;
                da.stencilLoadOp = sl == "clear" ? WGPULoadOp_Clear : sl == "load" ? WGPULoadOp_Load : WGPULoadOp_Undefined; da.stencilStoreOp = sl.empty() ? WGPUStoreOp_Undefined : Sx(ctx, dsa, "stencilStoreOp") == "discard" ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
                da.stencilClearValue = (uint32_t)D(ctx, dsa, "stencilClearValue", 0); d.depthStencilAttachment = &da; }
            rp = wgpuCommandEncoderBeginRenderPass(enc, &d); }
        else if (op == "cp") { cp = wgpuCommandEncoderBeginComputePass(enc, nullptr); }
        else if (op == "end") { if (rp) { wgpuRenderPassEncoderEnd(rp); wgpuRenderPassEncoderRelease(rp); rp = nullptr; } if (cp) { wgpuComputePassEncoderEnd(cp); wgpuComputePassEncoderRelease(cp); cp = nullptr; } }
        else if (op == "pipe") { if (rp) wgpuRenderPassEncoderSetPipeline(rp, get<WGPURenderPipeline>((int)I(c, 1), T_RP)); else if (cp) wgpuComputePassEncoderSetPipeline(cp, get<WGPUComputePipeline>((int)I(c, 1), T_CP)); }
        else if (op == "bg") { VL dy = at(ctx, c, 3); int nd = len(ctx, dy); std::vector<uint32_t> off(nd); for (int i = 0; i < nd; i++) off[i] = (uint32_t)I(dy, i);
            WGPUBindGroup g = get<WGPUBindGroup>((int)I(c, 2), T_BG); if (rp) wgpuRenderPassEncoderSetBindGroup(rp, (uint32_t)I(c, 1), g, nd, off.data()); else if (cp) wgpuComputePassEncoderSetBindGroup(cp, (uint32_t)I(c, 1), g, nd, off.data()); }
        else if (op == "vb" && rp) { double sz = I(c, 4); wgpuRenderPassEncoderSetVertexBuffer(rp, (uint32_t)I(c, 1), get<WGPUBuffer>((int)I(c, 2), T_BUF), (uint64_t)I(c, 3), sz < 0 ? WGPU_WHOLE_SIZE : (uint64_t)sz); }
        else if (op == "ib" && rp) { double sz = I(c, 4); wgpuRenderPassEncoderSetIndexBuffer(rp, get<WGPUBuffer>((int)I(c, 1), T_BUF), I(c, 2) == 32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16, (uint64_t)I(c, 3), sz < 0 ? WGPU_WHOLE_SIZE : (uint64_t)sz); }
        else if (op == "draw" && rp) wgpuRenderPassEncoderDraw(rp, (uint32_t)I(c, 1), (uint32_t)I(c, 2), (uint32_t)I(c, 3), (uint32_t)I(c, 4));
        else if (op == "drawi" && rp) wgpuRenderPassEncoderDrawIndexed(rp, (uint32_t)I(c, 1), (uint32_t)I(c, 2), (uint32_t)I(c, 3), (int32_t)I(c, 4), (uint32_t)I(c, 5));
        else if (op == "vp" && rp) wgpuRenderPassEncoderSetViewport(rp, (float)I(c, 1), (float)I(c, 2), (float)I(c, 3), (float)I(c, 4), (float)I(c, 5), (float)I(c, 6));
        else if (op == "sc" && rp) wgpuRenderPassEncoderSetScissorRect(rp, (uint32_t)I(c, 1), (uint32_t)I(c, 2), (uint32_t)I(c, 3), (uint32_t)I(c, 4));
        else if (op == "blend" && rp) { WGPUColor col = { I(c, 1), I(c, 2), I(c, 3), I(c, 4) }; wgpuRenderPassEncoderSetBlendConstant(rp, &col); }
        else if (op == "sref" && rp) wgpuRenderPassEncoderSetStencilReference(rp, (uint32_t)I(c, 1));
        else if (op == "disp" && cp) wgpuComputePassEncoderDispatchWorkgroups(cp, (uint32_t)I(c, 1), (uint32_t)I(c, 2), (uint32_t)I(c, 3));
        else if (op == "b2b") wgpuCommandEncoderCopyBufferToBuffer(enc, get<WGPUBuffer>((int)I(c, 1), T_BUF), (uint64_t)I(c, 2), get<WGPUBuffer>((int)I(c, 3), T_BUF), (uint64_t)I(c, 4), (uint64_t)I(c, 5));
        else if (op == "clearbuf") { double sz = I(c, 3); wgpuCommandEncoderClearBuffer(enc, get<WGPUBuffer>((int)I(c, 1), T_BUF), (uint64_t)I(c, 2), sz < 0 ? WGPU_WHOLE_SIZE : (uint64_t)sz); }
        else if (op == "t2t" || op == "b2t" || op == "t2b") { /* [op, src.., dst.., w, h, d] texture = (tex, mip, x, y, z), buffer = (buf, offset, bytesPerRow, rowsPerImage) */
            WGPUExtent3D ext = { (uint32_t)I(c, 11), (uint32_t)I(c, 12), (uint32_t)std::max(1.0, I(c, 13)) };
            auto tx = [&](int b) { return WGPUTexelCopyTextureInfo{ get<WGPUTexture>((int)I(c, b), T_TEX), (uint32_t)I(c, b + 1), { (uint32_t)I(c, b + 2), (uint32_t)I(c, b + 3), (uint32_t)I(c, b + 4) }, WGPUTextureAspect_All }; };
            auto bx = [&](int b) { return WGPUTexelCopyBufferInfo{ { (uint64_t)I(c, b + 1), (uint32_t)I(c, b + 2), (uint32_t)I(c, b + 3) ? (uint32_t)I(c, b + 3) : ext.height }, get<WGPUBuffer>((int)I(c, b), T_BUF) }; };
            if (op == "t2t") { auto s = tx(1), d = tx(6); wgpuCommandEncoderCopyTextureToTexture(enc, &s, &d, &ext); }
            else if (op == "b2t") { auto s = bx(1); auto d = tx(6); wgpuCommandEncoderCopyBufferToTexture(enc, &s, &d, &ext); }
            else { auto s = tx(1); auto d = bx(6); wgpuCommandEncoderCopyTextureToBuffer(enc, &s, &d, &ext); } }
    }
    if (rp) { wgpuRenderPassEncoderEnd(rp); wgpuRenderPassEncoderRelease(rp); } if (cp) { wgpuComputePassEncoderEnd(cp); wgpuComputePassEncoderRelease(cp); }
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr); wgpuQueueSubmit(gq, 1, &cb); wgpuCommandBufferRelease(cb); wgpuCommandEncoderRelease(enc); }
/* gpuPresent(tex, canvasEl, opaque): copy the canvas texture into the element's backing store */
FN(gpuPresent) { CTX; WGPUTexture t = get<WGPUTexture>((int)NUM(0), T_TEX); CanvasBuf *cb = cvbuf_for(junwrap(a[1]), true); if (!t || !cb) return; bool opaque = a[2]->BooleanValue(iso);
    uint32_t w = wgpuTextureGetWidth(t), h = wgpuTextureGetHeight(t), bpr = (w * 4 + 255) & ~255u; bool rgba = wgpuTextureGetFormat(t) == WGPUTextureFormat_RGBA8Unorm || wgpuTextureGetFormat(t) == WGPUTextureFormat_RGBA8UnormSrgb;
    static WGPUBuffer rb; static uint64_t rbs; uint64_t need = (uint64_t)bpr * h;
    if (!rb || rbs < need) { if (rb) { wgpuBufferDestroy(rb); wgpuBufferRelease(rb); } WGPUBufferDescriptor d = WGPU_BUFFER_DESCRIPTOR_INIT; d.size = need; d.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst; rb = wgpuDeviceCreateBuffer(gd, &d); rbs = need; }
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(gd, nullptr);
    WGPUTexelCopyTextureInfo s = { t, 0, { 0, 0, 0 }, WGPUTextureAspect_All }; WGPUTexelCopyBufferInfo d = { { 0, bpr, h }, rb }; WGPUExtent3D ext = { w, h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(enc, &s, &d, &ext); WGPUCommandBuffer cbuf = wgpuCommandEncoderFinish(enc, nullptr); wgpuQueueSubmit(gq, 1, &cbuf); wgpuCommandBufferRelease(cbuf); wgpuCommandEncoderRelease(enc);
    if (!map_sync(rb, WGPUMapMode_Read, 0, need)) return;
    const uint8_t *p = (const uint8_t *)wgpuBufferGetConstMappedRange(rb, 0, need);
    int W, Hh; uint32_t *px = cvbuf_px(cb, &W, &Hh); if ((uint32_t)W != w || (uint32_t)Hh != h) { cvbuf_resize(cb, (int)w, (int)h); px = cvbuf_px(cb, &W, &Hh); }
    if (p) for (uint32_t y = 0; y < h; y++) { const uint32_t *src = (const uint32_t *)(p + (size_t)y * bpr); uint32_t *dst = px + (size_t)y * w;
        for (uint32_t x = 0; x < w; x++) { uint32_t v = src[x]; if (rgba) v = (v & 0xff00ff00u) | ((v & 0xff) << 16) | ((v >> 16) & 0xff); if (opaque) v |= 0xff000000u; dst[x] = v; } }
    wgpuBufferUnmap(rb); cvbuf_touch(cb); }
void js_install_webgpu(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(gpuInit); REG(gpuErr); REG(gpuDrop); REG(gpuBuffer); REG(gpuWriteBuffer); REG(gpuMapRead); REG(gpuUnmapWrite); REG(gpuTexture); REG(gpuView); REG(gpuSampler); REG(gpuShader);
    REG(gpuBGL); REG(gpuPL); REG(gpuBG); REG(gpuRP); REG(gpuCP); REG(gpuPipeBGL); REG(gpuWriteTexture); REG(gpuExec); REG(gpuPresent);
#undef REG
}
