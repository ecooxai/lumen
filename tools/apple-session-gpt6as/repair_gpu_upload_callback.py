#!/usr/bin/env python3
"""Replace an unsupported wgpu query with owned callback state; tests first."""
import argparse
import hashlib
import json
from pathlib import Path


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise RuntimeError('Missing or ambiguous source anchor: ' + before[:90])
    return source.replace(before, after, 1)


def source_patch(source):
    source = replace_once(source, '#include <stdio.h>\n', '#include <stdio.h>\n#include <stdatomic.h>\n')
    source = replace_once(source, '''/* Three bounded slots cover in-flight frames without waiting for GPU work.
 * A callback owns no presenter state, so cancellation/destruction cannot leave
 * a callback pointing at a released Gpu. */
typedef struct { WGPUBuffer buffer; size_t size; } GpuUpload;
''', '''/* Three bounded slots cover in-flight frames without waiting for GPU work.
 * A callback owns only a reference-counted completion token, never a Gpu or
 * buffer. This also permits cancellation after the presenter is destroyed. */
enum { UPLOAD_PENDING, UPLOAD_MAPPED, UPLOAD_FAILED };
typedef struct { atomic_uint refs; atomic_int state; } GpuUploadMap;
typedef struct { WGPUBuffer buffer; size_t size; GpuUploadMap *map; } GpuUpload;
''')
    source = replace_once(source, '''static void upload_map_done(WGPUMapAsyncStatus status, WGPUStringView message, void *u1, void *u2) {
    /* Map state is checked on the presentation thread; no owner pointer escapes. */
    (void)status; (void)message; (void)u1; (void)u2;
}
static void upload_release(GpuUpload *upload) {
    if (upload->buffer) { wgpuBufferDestroy(upload->buffer); wgpuBufferRelease(upload->buffer); }
    upload->buffer = NULL; upload->size = 0;
}
''', '''static void upload_map_release(GpuUploadMap *map) {
    if (map && atomic_fetch_sub_explicit(&map->refs, 1, memory_order_acq_rel) == 1) free(map);
}
static void upload_map_done(WGPUMapAsyncStatus status, WGPUStringView message, void *u1, void *u2) {
    /* Spontaneous callbacks may run on another thread or after destruction.
       Publish completion only; do not call wgpu or access presenter/buffer state. */
    (void)message; (void)u2;
    GpuUploadMap *map = u1;
    atomic_store_explicit(&map->state, status == WGPUMapAsyncStatus_Success ? UPLOAD_MAPPED : UPLOAD_FAILED,
                          memory_order_release);
    upload_map_release(map);
}
static int upload_state(const GpuUpload *upload) {
    /* The installed wgpu-native exports wgpuBufferGetMapState but panics when
       called. Completion callbacks are the supported source of mapping state. */
    return upload->map ? atomic_load_explicit(&upload->map->state, memory_order_acquire) : UPLOAD_FAILED;
}
static void upload_release(GpuUpload *upload) {
    if (upload->buffer) { wgpuBufferDestroy(upload->buffer); wgpuBufferRelease(upload->buffer); }
    upload_map_release(upload->map);
    upload->buffer = NULL; upload->size = 0; upload->map = NULL;
}
''')
    source = replace_once(source, 'wgpuBufferGetMapState(g->uploads[i].buffer) == WGPUBufferMapState_Pending', 'upload_state(&g->uploads[i]) == UPLOAD_PENDING')
    source = replace_once(source, 'wgpuBufferGetMapState(upload->buffer) == WGPUBufferMapState_Mapped', 'upload_state(upload) == UPLOAD_MAPPED')
    source = replace_once(source, 'wgpuBufferGetMapState(upload->buffer) == WGPUBufferMapState_Pending', 'upload_state(upload) == UPLOAD_PENDING')
    source = replace_once(source, '''        upload_release(upload);
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
''', '''        upload_release(upload);
        upload->map = malloc(sizeof *upload->map);
        if (!upload->map) return NULL;
        atomic_init(&upload->map->refs, 1);
        atomic_init(&upload->map->state, UPLOAD_MAPPED);
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
''')
    source = replace_once(source, '        if (!upload->buffer) return NULL;\n', '        if (!upload->buffer) { upload_release(upload); return NULL; }\n')
    source = replace_once(source, '''        map.mode = WGPUCallbackMode_AllowSpontaneous; map.callback = upload_map_done;
''', '''        map.mode = WGPUCallbackMode_AllowSpontaneous; map.callback = upload_map_done; map.userdata1 = upload->map;
        /* Acquire callback ownership before the API can invoke it synchronously. */
        atomic_fetch_add_explicit(&upload->map->refs, 1, memory_order_relaxed);
        atomic_store_explicit(&upload->map->state, UPLOAD_PENDING, memory_order_release);
''')
    return source


def tests_patch(source):
    source = replace_once(source, '#include "../src/gpu/gpu.c"\n', '#include "../src/gpu/gpu.c"\n#include <pthread.h>\n')
    source = replace_once(source, '    bool hold_maps, fail_next_map, fail_next_allocation;\n', '    bool hold_maps, fail_next_map, fail_next_allocation, defer_cancellation, callback_thread, inline_maps;\n')
    source = replace_once(source, 'static int checks, failures;\n', '''static WGPUBufferMapCallbackInfo cancelled_maps[16];
static unsigned cancelled_count, completed_callbacks;
typedef struct { WGPUBufferMapCallbackInfo callback; WGPUMapAsyncStatus status; } TestCompletion;
static void *complete_on_thread(void *opaque) {
    TestCompletion *completion = opaque;
    WGPUBufferMapCallbackInfo callback = completion->callback;
    callback.callback(completion->status, (WGPUStringView){0}, callback.userdata1, callback.userdata2);
    return NULL;
}
static void complete_callback(WGPUBufferMapCallbackInfo callback, WGPUMapAsyncStatus status) {
    if (!callback.callback) abort();
    TestCompletion completion = {callback, status};
    if (uploads.callback_thread) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, complete_on_thread, &completion) || pthread_join(thread, NULL)) abort();
    } else complete_on_thread(&completion);
    completed_callbacks++;
}
static int checks, failures;
''')
    source = replace_once(source, '''WGPUBufferMapState test_map_state(WGPUBuffer buffer) {
    return ((TestBuffer *)buffer)->state;
}
''', '''WGPUBufferMapState test_map_state(WGPUBuffer buffer) {
    (void)buffer;
    /* The real pinned backend exports this symbol but aborts when called.
       Keep the mock equally strict so a declaration is never mistaken for support. */
    fputs("unsupported pinned-backend call: wgpuBufferGetMapState\\n", stderr);
    abort();
}
''')
    source = replace_once(source, '''    b->state = WGPUBufferMapState_Pending; b->callback = callback; uploads.maps++;
    return (WGPUFuture){ .id = uploads.maps };
''', '''    b->state = WGPUBufferMapState_Pending; b->callback = callback; uploads.maps++;
    if (uploads.inline_maps) {
        b->state = WGPUBufferMapState_Mapped; b->callback.callback = NULL;
        complete_callback(callback, WGPUMapAsyncStatus_Success);
    }
    return (WGPUFuture){ .id = uploads.maps };
''')
    source = replace_once(source, '''void test_destroy_buffer(WGPUBuffer buffer) {
    TestBuffer *b = (TestBuffer *)buffer;
    if (b) b->state = WGPUBufferMapState_Unmapped;
}
''', '''void test_destroy_buffer(WGPUBuffer buffer) {
    TestBuffer *b = (TestBuffer *)buffer;
    if (!b) return;
    if (b->state == WGPUBufferMapState_Pending) {
        WGPUBufferMapCallbackInfo callback = b->callback; b->callback.callback = NULL;
        if (uploads.defer_cancellation) {
            if (cancelled_count == 16) abort();
            cancelled_maps[cancelled_count++] = callback;
        } else complete_callback(callback, WGPUMapAsyncStatus_Aborted);
    }
    b->state = WGPUBufferMapState_Unmapped;
}
''')
    source = replace_once(source, '''        if (b->callback.callback) b->callback.callback(failed ? WGPUMapAsyncStatus_Error : WGPUMapAsyncStatus_Success,
            (WGPUStringView){0}, b->callback.userdata1, b->callback.userdata2);
''', '''        WGPUBufferMapCallbackInfo callback = b->callback; b->callback.callback = NULL;
        complete_callback(callback, failed ? WGPUMapAsyncStatus_Error : WGPUMapAsyncStatus_Success);
''')
    source = replace_once(source, '    memset(&uploads, 0, sizeof uploads); memset(upload_buffers, 0, sizeof upload_buffers);\n', '''    memset(&uploads, 0, sizeof uploads); memset(upload_buffers, 0, sizeof upload_buffers);
    if (cancelled_count) abort();
    completed_callbacks = 0;
''')
    source = replace_once(source, '    for (unsigned i = 0; i < 16; i++) if (upload_buffers[i]) test_release_buffer((WGPUBuffer)upload_buffers[i]);\n', '''    for (unsigned i = 0; i < GPU_UPLOAD_SLOTS; i++) upload_release(&g->uploads[i]);
    if (uploads.live) abort();
''')
    source = replace_once(source, 'int main(void) {\n', '''static void upload_callback_lifetime(void) {
    Gpu g = gpu_new(13, 9); g.backend = WGPUBackendType_Metal;
    uint32_t pixels[13 * 9]; fill_rows(pixels, 13, 0, 9, 0xff223344);
    uploads.hold_maps = true; uploads.defer_cancellation = true;
    CHECK("pending callback case presents", gpu_present(&g, pixels, 13, 9, 13));
    gpu_clear(&g);
    CHECK("pending mapping releases its GPU buffer before deferred cancellation", !uploads.live && cancelled_count == 1);
    /* Erase all owner state before a foreign thread delivers the cancelled map.
       The ASan run checks the completion token survives this ordering. */
    memset(&g, 0xa5, sizeof g); uploads.callback_thread = true;
    for (unsigned i = 0; i < cancelled_count; i++) complete_callback(cancelled_maps[i], WGPUMapAsyncStatus_CallbackCancelled);
    cancelled_count = 0;
    CHECK("cancellation after owner destruction completes once", completed_callbacks == 1);
    g = gpu_new(13, 9); g.backend = WGPUBackendType_Metal; uploads.callback_thread = true;
    bool identical = true;
    for (unsigned frame = 0; frame < 20; frame++) {
        fill_rows(pixels, 13, 0, 9, 0xff000000 | frame);
        identical &= gpu_present(&g, pixels, 13, 9, 13) && texture_equals(g.tex, pixels, sizeof pixels);
    }
    CHECK("foreign-thread map completion preserves exact pixels", identical && completed_callbacks == 19);
    gpu_clear(&g);
    g = gpu_new(13, 9); g.backend = WGPUBackendType_Metal; uploads.inline_maps = true;
    identical = true;
    for (unsigned frame = 0; frame < 20; frame++) {
        fill_rows(pixels, 13, 0, 9, 0xff778800 | frame);
        identical &= gpu_present(&g, pixels, 13, 9, 13) && texture_equals(g.tex, pixels, sizeof pixels);
    }
    CHECK("synchronous completion holds token ownership before map returns", identical && completed_callbacks == 20 && uploads.creates == 1);
    gpu_clear(&g);
}
int main(void) {
    upload_callback_lifetime();
''')
    return source


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage', choices=['tests', 'source'], required=True)
    args = parser.parse_args()
    path = Path('tests/test_gpu_present.c' if args.stage == 'tests' else 'src/gpu/gpu.c')
    expected = {'tests': '44ed9276668f78cd5f140564404fe96035c1b0c8d14d45c20f4674dcbdf32495',
                'source': '5a5b6aea41608037e81a261e0fc01c9816eee47e25784f78ee4789c69fa7a055'}[args.stage]
    before = path.read_text()
    if hashlib.sha256(before.encode()).hexdigest() != expected:
        raise SystemExit('Source does not match the recorded failed candidate: ' + str(path))
    after = (tests_patch if args.stage == 'tests' else source_patch)(before)
    path.write_text(after)
    print(json.dumps({'path': str(path), 'beforeSHA256': expected, 'afterSHA256': hashlib.sha256(after.encode()).hexdigest(),
                      'scope': 'Pinned-backend crash regression and callback lifetime repair; no workload or frame cadence change.'}))
