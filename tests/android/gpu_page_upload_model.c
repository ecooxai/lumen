#include <assert.h>
#include <limits.h>
#include "../../src/gpu/gpu.c"

static unsigned uploads;
static const uint32_t *expected_pixels;
static uint32_t expected_stride, expected_first, expected_last;

void wgpuQueueWriteTexture(WGPUQueue queue, const WGPUTexelCopyTextureInfo *destination,
                           const void *data, size_t size, const WGPUTexelCopyBufferLayout *layout,
                           const WGPUExtent3D *extent) {
    assert(queue && destination->texture);
    assert(destination->origin.x == 0 && destination->origin.y == 2 + expected_first);
    assert(destination->origin.z == 0 && destination->mipLevel == 0);
    assert(data == expected_pixels + expected_first * expected_stride);
    assert(layout->offset == 0 && layout->bytesPerRow == expected_stride * 4);
    assert(layout->rowsPerImage == expected_last - expected_first);
    assert(extent->width == 4 && extent->height == expected_last - expected_first);
    assert(extent->depthOrArrayLayers == 1);
    assert(size == ((size_t)(expected_last - expected_first - 1) * expected_stride + 4) * 4);
    uploads++;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    uint32_t pixels[36];
    for (size_t index = 0; index < 36; index++) pixels[index] = (uint32_t)index + 0xff000000u;
    Gpu state = {.configured = true, .tex = (WGPUTexture)(uintptr_t)1,
                 .q = (WGPUQueue)(uintptr_t)2, .tw = 4, .th = 8, .w = 4, .h = 8};
    expected_pixels = pixels;
    expected_stride = 4;
    expected_first = 1;
    expected_last = 5;
    bool accepted = true;
    if (!strcmp(argv[1], "padded")) expected_stride = 6;
    else if (!strcmp(argv[1], "bottom")) { expected_first = 5; expected_last = 6; }
    else if (!strcmp(argv[1], "missing")) { state.tex = NULL; accepted = false; }
    else if (!strcmp(argv[1], "resize")) { state.th = 7; accepted = false; }
    else if (!strcmp(argv[1], "surface-resize")) { state.h = 7; accepted = false; }
    else if (!strcmp(argv[1], "suspended")) { state.configured = false; accepted = false; }
    else if (!strcmp(argv[1], "invalid")) {
        assert(!gpu_upload_page(NULL, pixels, 4, 8, 4, 2, 1, 5));
        assert(!gpu_upload_page(&state, NULL, 4, 8, 4, 2, 1, 5));
        assert(!gpu_upload_page(&state, pixels, 4, 8, 3, 2, 1, 5));
        assert(!gpu_upload_page(&state, pixels, 4, 8, INT_MAX, 2, 1, 5));
        assert(!gpu_upload_page(&state, pixels, 4, 8, 4, -1, 1, 5));
        assert(!gpu_upload_page(&state, pixels, 4, 8, 4, 8, 0, 1));
        assert(!gpu_upload_page(&state, pixels, 4, 8, 4, 2, -1, 5));
        assert(!gpu_upload_page(&state, pixels, 4, 8, 4, 2, 1, 7));
        assert(!gpu_upload_page(&state, pixels, 4, 8, 4, 2, 3, 3));
        assert(!gpu_upload_page(&state, pixels, 4, 8, 4, 2, 5, 2));
        assert(!gpu_upload_page(&state, pixels, 0, 8, 4, 2, 1, 5));
        assert(!gpu_upload_page(&state, pixels, 4, -1, 4, 2, 1, 5));
        assert(uploads == 0);
        return 0;
    } else assert(!strcmp(argv[1], "partial"));
    assert(gpu_upload_page(&state, pixels, 4, 8, (int)expected_stride, 2,
                           (int)expected_first, (int)expected_last) == accepted);
    assert(uploads == (accepted ? 1u : 0u));
    for (size_t index = 0; index < 36; index++) assert(pixels[index] == (uint32_t)index + 0xff000000u);
    return 0;
}
