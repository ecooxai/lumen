/* Exercise the actual presenter with deterministic surface failures. Redirect
 * only the GPU calls made by presentation; ordinary creation code still compiles
 * against the same wgpu headers/library as the app. No window/GPU is required.
 * Link this translation unit with wgpu-native, not src/gpu/gpu.c.o.
 */
#define wgpuSurfaceGetCurrentTexture test_acquire
#define wgpuSurfaceConfigure test_configure
#define wgpuSurfacePresent test_present
#define wgpuDeviceCreateTexture test_create_texture
#define wgpuTextureCreateView test_create_view
#define wgpuTextureDestroy test_destroy_texture
#define wgpuTextureRelease test_release_texture
#define wgpuTextureViewRelease test_release_view
#define wgpuDeviceCreateBindGroup test_create_bind_group
#define wgpuBindGroupRelease test_release_bind_group
#define wgpuQueueWriteTexture test_write_texture
#define wgpuQueueWriteBuffer test_write_buffer
#define wgpuDeviceCreateCommandEncoder test_create_encoder
#define wgpuCommandEncoderBeginRenderPass test_begin_pass
#define wgpuRenderPassEncoderSetPipeline test_set_pipeline
#define wgpuRenderPassEncoderSetBindGroup test_set_bind_group
#define wgpuRenderPassEncoderDraw test_draw
#define wgpuRenderPassEncoderEnd test_end_pass
#define wgpuRenderPassEncoderRelease test_release_pass
#define wgpuCommandEncoderFinish test_finish_encoder
#define wgpuQueueSubmit test_submit
#define wgpuCommandBufferRelease test_release_command_buffer
#define wgpuCommandEncoderRelease test_release_encoder
#define now_ms test_now_ms
#include "../src/gpu/gpu.c"

typedef struct {
    int w, h, bpp;
    bool surface;
    unsigned char *pixels;
} TestTexture;
static struct {
    WGPUSurfaceGetCurrentTextureStatus status;
    bool failure_texture;
    unsigned acquisitions, configures, presents, submits;
    unsigned creates, releases, surface_live, uploads, buffer_writes;
    size_t staged_bytes;
} mock;
static int checks, failures;
#define CHECK(name, expression) do { \
    bool ok_ = (expression); checks++; \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
    if (!ok_) failures++; \
} while (0)

double test_now_ms(void) { return 0; }
static WGPUTexture texture_new(int w, int h, int bpp, bool surface) {
    TestTexture *t = calloc(1, sizeof *t);
    t->w = w; t->h = h; t->bpp = bpp; t->surface = surface;
    t->pixels = calloc((size_t)w * h, (size_t)bpp);
    if (!t->pixels) abort();
    if (surface) mock.surface_live++;
    return (WGPUTexture)t;
}
void test_acquire(WGPUSurface surface, WGPUSurfaceTexture *out) {
    (void)surface; mock.acquisitions++;
    out->status = mock.status;
    bool good = mock.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                mock.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
    out->texture = good || mock.failure_texture ? texture_new(1, 1, 4, true) : NULL;
}
void test_configure(WGPUSurface surface, WGPUSurfaceConfiguration const *config) {
    (void)surface; (void)config; mock.configures++;
}
WGPUStatus test_present(WGPUSurface surface) {
    (void)surface; mock.presents++; return WGPUStatus_Success;
}
WGPUTexture test_create_texture(WGPUDevice device, WGPUTextureDescriptor const *desc) {
    (void)device; mock.creates++;
    int bpp = desc->format == WGPUTextureFormat_R8Unorm ? 1 :
              desc->format == WGPUTextureFormat_RG8Unorm ? 2 : 4;
    return texture_new((int)desc->size.width, (int)desc->size.height, bpp, false);
}
WGPUTextureView test_create_view(WGPUTexture texture, WGPUTextureViewDescriptor const *desc) {
    (void)desc; return (WGPUTextureView)texture;
}
void test_destroy_texture(WGPUTexture texture) { (void)texture; }
void test_release_texture(WGPUTexture texture) {
    TestTexture *t = (TestTexture *)texture;
    if (!t) return;
    if (t->surface) mock.surface_live--;
    mock.releases++; free(t->pixels); free(t);
}
void test_release_view(WGPUTextureView view) { (void)view; }
WGPUBindGroup test_create_bind_group(WGPUDevice device, WGPUBindGroupDescriptor const *desc) {
    (void)device; (void)desc; return (WGPUBindGroup)(uintptr_t)1;
}
void test_release_bind_group(WGPUBindGroup group) { (void)group; }
void test_write_texture(WGPUQueue queue, WGPUTexelCopyTextureInfo const *dst,
        void const *data, size_t size, WGPUTexelCopyBufferLayout const *layout,
        WGPUExtent3D const *extent) {
    (void)queue; mock.uploads++; mock.staged_bytes += size;
    TestTexture *t = (TestTexture *)dst->texture;
    if (!t || dst->origin.x + extent->width > (unsigned)t->w ||
            dst->origin.y + extent->height > (unsigned)t->h) abort();
    for (unsigned y = 0; y < extent->height; y++) {
        size_t input = layout->offset + (size_t)y * layout->bytesPerRow;
        size_t count = (size_t)extent->width * t->bpp;
        if (input + count > size) abort();
        memcpy(t->pixels + ((size_t)(dst->origin.y + y) * t->w + dst->origin.x) * t->bpp,
               (unsigned char const *)data + input, count);
    }
}
void test_write_buffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset,
        void const *data, size_t size) {
    (void)queue; (void)buffer; (void)offset; (void)data;
    mock.buffer_writes++; mock.staged_bytes += size;
}
WGPUCommandEncoder test_create_encoder(WGPUDevice device, WGPUCommandEncoderDescriptor const *desc) {
    (void)device; (void)desc; return (WGPUCommandEncoder)(uintptr_t)1;
}
WGPURenderPassEncoder test_begin_pass(WGPUCommandEncoder encoder, WGPURenderPassDescriptor const *desc) {
    (void)encoder; (void)desc; return (WGPURenderPassEncoder)(uintptr_t)1;
}
void test_set_pipeline(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline) { (void)pass; (void)pipeline; }
void test_set_bind_group(WGPURenderPassEncoder pass, uint32_t index, WGPUBindGroup group,
        size_t count, uint32_t const *offsets) {
    (void)pass; (void)index; (void)group; (void)count; (void)offsets;
}
void test_draw(WGPURenderPassEncoder pass, uint32_t vertices, uint32_t instances,
        uint32_t first_vertex, uint32_t first_instance) {
    (void)pass; (void)vertices; (void)instances; (void)first_vertex; (void)first_instance;
}
void test_end_pass(WGPURenderPassEncoder pass) { (void)pass; }
void test_release_pass(WGPURenderPassEncoder pass) { (void)pass; }
WGPUCommandBuffer test_finish_encoder(WGPUCommandEncoder encoder, WGPUCommandBufferDescriptor const *desc) {
    (void)encoder; (void)desc; return (WGPUCommandBuffer)(uintptr_t)1;
}
void test_submit(WGPUQueue queue, size_t count, WGPUCommandBuffer const *buffers) {
    (void)queue; (void)count; (void)buffers; mock.submits++; mock.staged_bytes = 0;
}
void test_release_command_buffer(WGPUCommandBuffer buffer) { (void)buffer; }
void test_release_encoder(WGPUCommandEncoder encoder) { (void)encoder; }

static Gpu gpu_new(int w, int h) {
    memset(&mock, 0, sizeof mock);
    mock.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    return (Gpu){ .configured = true, .w = w, .h = h,
        .ypipe = (WGPURenderPipeline)(uintptr_t)1,
        .ybgl = (WGPUBindGroupLayout)(uintptr_t)1,
        .yubuf = (WGPUBuffer)(uintptr_t)1 };
}
static void gpu_clear(Gpu *g) {
    /* Only textures are allocated by the mock; pipeline/device handles are tags. */
    test_release_texture(g->tex); test_release_texture(g->vtex);
    test_release_texture(g->yt); test_release_texture(g->ct);
}
static bool texture_equals(WGPUTexture texture, void const *pixels, size_t size) {
    TestTexture *t = (TestTexture *)texture;
    return t && (size_t)t->w * t->h * t->bpp == size && !memcmp(t->pixels, pixels, size);
}
static void fill_rows(uint32_t *pixels, int w, int y0, int y1, uint32_t value) {
    for (int y = y0; y < y1; y++) for (int x = 0; x < w; x++) pixels[y * w + x] = value;
}
static void partial_recovery(void) {
    Gpu g = gpu_new(4, 8); uint32_t pixels[32] = {0};
    CHECK("initial page presents", gpu_present(&g, pixels, 4, 8, 4));
    unsigned writes = mock.uploads, buffers = mock.buffer_writes, creates = mock.creates;
    fill_rows(pixels, 4, 1, 2, 0xff112233);
    mock.status = WGPUSurfaceGetCurrentTextureStatus_Timeout;
    CHECK("timeout requests a retry", !gpu_present_rows(&g, pixels, 4, 8, 4, 1, 2) && gpu_present_pending(&g));
    fill_rows(pixels, 4, 6, 7, 0xff445566);
    mock.status = WGPUSurfaceGetCurrentTextureStatus_Outdated;
    CHECK("outdated surface requests a retry", !gpu_present_rows(&g, pixels, 4, 8, 4, 6, 7) && gpu_present_pending(&g));
    CHECK("failed acquisition queues no uploads or uniforms", mock.uploads == writes && mock.buffer_writes == buffers && !mock.staged_bytes);
    CHECK("failed acquisition allocates no page texture", mock.creates == creates);
    CHECK("outdated surface is reconfigured once", mock.configures == 1);
    mock.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    CHECK("zero-damage retry presents", gpu_present_rows(&g, pixels, 4, 8, 4, 0, 0));
    CHECK("retry includes every previously missed row", texture_equals(g.tex, pixels, sizeof pixels));
    CHECK("successful retry clears pending work", !gpu_present_pending(&g) && !mock.staged_bytes);
    writes = mock.uploads;
    CHECK("later unchanged frame presents", gpu_present_rows(&g, pixels, 4, 8, 4, 0, 0));
    CHECK("delivered damage is not uploaded again", mock.uploads == writes);
    CHECK("surface textures are released", mock.surface_live == 0);
    gpu_clear(&g);
}
static void blocked_video(void) {
    Gpu g = gpu_new(4, 8); uint32_t pixels[32] = {0}; uint8_t yuv[12] = {0};
    GpuVideo video = { .w = 4, .h = 2, .x1 = 4, .y1 = 2, .yuv = yuv };
    mock.status = (WGPUSurfaceGetCurrentTextureStatus)WGPUSurfaceGetCurrentTextureStatus_Occluded;
    bool blocked = true;
    for (int i = 0; i < 700; i++) {
        pixels[0] = (uint32_t)i; memset(yuv, i & 255, sizeof yuv);
        if (gpu_present_frame(&g, pixels, 4, 8, 4, 0, 1, &video)) blocked = false;
    }
    CHECK("occluded frames never report presentation", blocked && !mock.presents && !mock.submits);
    CHECK("occlusion keeps the native surface pending", gpu_present_pending(&g));
    CHECK("occluded video queues zero staging memory", !mock.uploads && !mock.buffer_writes && !mock.staged_bytes);
    CHECK("occluded video allocates no upload textures", !mock.creates);
    CHECK("occlusion preserves the valid surface configuration", !mock.configures);
    pixels[31] = 0xffabcdef; memset(yuv, 0x91, sizeof yuv);
    mock.status = WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
    CHECK("suboptimal success is usable", gpu_present_frame(&g, pixels, 4, 8, 4, 0, 0, &video));
    CHECK("first visible frame uploads the whole current page", texture_equals(g.tex, pixels, sizeof pixels));
    CHECK("first visible frame uses current luma", texture_equals(g.yt, yuv, 8));
    CHECK("first visible frame uses current chroma", texture_equals(g.ct, yuv + 8, 4));
    CHECK("one visible frame submits once", mock.submits == 1 && mock.presents == 1 && !mock.staged_bytes);
    CHECK("one page and two video planes upload", mock.uploads == 3 && mock.creates == 3);
    CHECK("visible video leaves no acquired texture retained", !mock.surface_live);
    gpu_clear(&g);
}
static void resize_recovery(void) {
    Gpu g = gpu_new(4, 8); uint32_t pixels[32] = {0}, resized[18] = {0};
    CHECK("resize case starts with a valid page", gpu_present(&g, pixels, 4, 8, 4));
    unsigned creates = mock.creates, writes = mock.uploads;
    mock.status = WGPUSurfaceGetCurrentTextureStatus_Lost; mock.failure_texture = true;
    CHECK("lost resize retains retry", !gpu_present_rows(&g, resized, 6, 3, 6, 0, 0) && gpu_present_pending(&g));
    CHECK("failed resize preserves the prior texture", g.tw == 4 && g.th == 8 && mock.creates == creates && mock.uploads == writes);
    CHECK("failed acquisition releases an attached texture", !mock.surface_live);
    fill_rows(pixels, 4, 0, 8, 0xff765432);
    mock.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal; mock.failure_texture = false;
    CHECK("returning to the prior dimensions presents", gpu_present_rows(&g, pixels, 4, 8, 4, 0, 0));
    CHECK("resize damage survives a return to old dimensions", texture_equals(g.tex, pixels, sizeof pixels));
    mock.status = WGPUSurfaceGetCurrentTextureStatus_Timeout;
    CHECK("another resize can be delayed", !gpu_present_rows(&g, resized, 6, 3, 6, 2, 3));
    fill_rows(resized, 6, 0, 3, 0xfffedcba);
    mock.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    CHECK("new dimensions recover with zero new rows", gpu_present_rows(&g, resized, 6, 3, 6, 0, 0));
    CHECK("new texture receives every row", texture_equals(g.tex, resized, sizeof resized));
    gpu_clear(&g);
}
static void bgra_video_recovery(void) {
    Gpu g = gpu_new(4, 8); uint32_t pixels[32] = {0}, old_video[4] = {1, 2, 3, 4}, latest[6] = {9, 8, 7, 6, 5, 4};
    GpuVideo video = { .px = old_video, .w = 2, .h = 2, .stride = 2, .x1 = 2, .y1 = 2 };
    mock.status = WGPUSurfaceGetCurrentTextureStatus_Timeout;
    CHECK("BGRA video waits for a drawable", !gpu_present_frame(&g, pixels, 4, 8, 4, 0, 8, &video));
    CHECK("blocked BGRA queues no frame or rectangle data", !mock.uploads && !mock.buffer_writes && !mock.creates);
    memset(old_video, 0, sizeof old_video);
    video.px = latest; video.w = video.stride = 3; video.x1 = 3;
    mock.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    CHECK("replacement BGRA frame presents", gpu_present_frame(&g, pixels, 4, 8, 4, 0, 0, &video));
    CHECK("recovery reads the current caller-owned video", texture_equals(g.vtex, latest, sizeof latest));
    CHECK("BGRA recovery submits a bounded upload", mock.uploads == 2 && mock.buffer_writes == 1 && !mock.staged_bytes);
    gpu_clear(&g);
}
static void unconfigured_recovery(void) {
    Gpu g = gpu_new(4, 8); uint32_t pixels[32] = {0};
    CHECK("unconfigured case starts with a valid page", gpu_present(&g, pixels, 4, 8, 4));
    g.configured = false; unsigned acquired = mock.acquisitions;
    fill_rows(pixels, 4, 3, 4, 0xffabcdef);
    CHECK("unconfigured surface skips acquisition", !gpu_present_rows(&g, pixels, 4, 8, 4, 3, 4) && mock.acquisitions == acquired);
    g.configured = true;
    CHECK("restored surface presents retained pixels", gpu_present_rows(&g, pixels, 4, 8, 4, 0, 0) && texture_equals(g.tex, pixels, sizeof pixels));
    gpu_clear(&g);
}
int main(void) {
    partial_recovery(); blocked_video(); resize_recovery(); bgra_video_recovery(); unconfigured_recovery();
    printf("GPU presentation: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
