#include <assert.h>
#include "../../src/gpu/gpu.c"

static int textures_created, textures_destroyed, textures_released, views_released, groups_released;
static WGPUTextureFormat upload_format;
static uint32_t upload_width, upload_height;

void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset,
                          const void *data, size_t size) {
    assert(queue && buffer && offset == 16 && size == 16);
    const uint32_t *metadata = data;
    for (unsigned index = 0; index < 4; index++) assert(metadata[index] == 0);
}

WGPUTexture wgpuDeviceCreateTexture(WGPUDevice device, const WGPUTextureDescriptor *descriptor) {
    assert(device && descriptor);
    assert(descriptor->usage == (WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst));
    assert(descriptor->size.depthOrArrayLayers == 1);
    upload_format = descriptor->format;
    upload_width = descriptor->size.width;
    upload_height = descriptor->size.height;
    return (WGPUTexture)(uintptr_t)(100 + ++textures_created);
}

WGPUTextureView wgpuTextureCreateView(WGPUTexture texture, const WGPUTextureViewDescriptor *descriptor) {
    assert(texture && !descriptor);
    return (WGPUTextureView)texture;
}

WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor *descriptor) {
    assert(device && descriptor && descriptor->layout && descriptor->entryCount == 3);
    assert(descriptor->entries[0].textureView && descriptor->entries[1].sampler);
    assert(descriptor->entries[2].buffer && descriptor->entries[2].size == 32);
    return (WGPUBindGroup)descriptor->entries[0].textureView;
}

void wgpuTextureDestroy(WGPUTexture texture) { assert(texture); textures_destroyed++; }
void wgpuTextureRelease(WGPUTexture texture) { assert(texture); textures_released++; }
void wgpuTextureViewRelease(WGPUTextureView view) { assert(view); views_released++; }
void wgpuBindGroupRelease(WGPUBindGroup group) { assert(group); groups_released++; }

int main(int argc, char **argv) {
    assert(argc == 2);
    Gpu state = {.dev = (WGPUDevice)(uintptr_t)1, .bgl = (WGPUBindGroupLayout)(uintptr_t)2,
                 .q = (WGPUQueue)(uintptr_t)7,
                 .samp = (WGPUSampler)(uintptr_t)3, .vsamp = (WGPUSampler)(uintptr_t)4,
                 .ubuf = (WGPUBuffer)(uintptr_t)5, .vubuf = (WGPUBuffer)(uintptr_t)6};
    if (!strcmp(argv[1], "shader")) {
        assert(strstr(WGSL, "textureSampleLevel(t, s, uv, 0.0).bgra"));
        assert(!strstr(WGSL_YUV, ".bgra"));
    } else if (!strcmp(argv[1], "page") || !strcmp(argv[1], "video")) {
        if (!strcmp(argv[1], "page")) ensure_tex(&state, 1080, 2112);
        else ensure_vtex(&state, 1080, 2112);
        assert(textures_created == 1 && upload_width == 1080 && upload_height == 2112);
        assert(upload_format == WGPUTextureFormat_RGBA8Unorm);
    } else {
        ensure_tex(&state, 1080, 2112);
        ensure_vtex(&state, 640, 360);
        ensure_tex(&state, 1080, 2112);
        ensure_vtex(&state, 640, 360);
        assert(textures_created == 2 && textures_destroyed == 0);
        if (!strcmp(argv[1], "resize")) {
            state.scroll_toolbar = 88; state.scroll_height = 2024; state.scroll_offset = 137;
            ensure_tex(&state, 2112, 1080);
            ensure_vtex(&state, 360, 640);
            assert(textures_created == 4 && textures_destroyed == 2 && textures_released == 2);
            assert(views_released == 2 && groups_released == 2);
            assert(state.tw == 2112 && state.th == 1080 && state.vtw == 360 && state.vth == 640);
            assert(!state.scroll_height && !state.scroll_toolbar && !state.scroll_offset);
        } else assert(!strcmp(argv[1], "reuse"));
    }
    return 0;
}
