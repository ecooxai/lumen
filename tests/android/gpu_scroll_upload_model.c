#include <assert.h>
#include <limits.h>
#include "../../src/gpu/gpu.c"
#include "prepare_present_test.h"

static uint32_t texture_pixels[1024], page_pixels[1024], scroll_uniform[4];
static uint32_t ring_pixels[1024];
static const uint32_t *active_source=page_pixels;
static bool circular_source;
static unsigned writes, uploaded_rows, uniform_writes;
static int active_width, active_height, active_stride, active_toolbar;
static bool regular_upload;
static bool present_video;
static unsigned video_writes, video_uniforms, draw_count;
static WGPUBindGroup drawn[2], selected_group;
static Gpu *present_gpu;
static uint32_t linear_pixels[1024];
static float expected_rect[4] = {-1, 1, 1, -1}, video_parameters[12];
static uint8_t rgba_uploaded[128], y_uploaded[128], uv_uploaded[128];

WGPUTexture wgpuDeviceCreateTexture(WGPUDevice device, const WGPUTextureDescriptor *descriptor) {
    assert(device && descriptor && descriptor->size.width > 0);
    static uintptr_t next = 50;
    return (WGPUTexture)next++;
}

WGPUTextureView wgpuTextureCreateView(WGPUTexture texture, const WGPUTextureViewDescriptor *descriptor) {
    assert(texture && !descriptor);
    return (WGPUTextureView)texture;
}

WGPUBindGroup wgpuDeviceCreateBindGroup(WGPUDevice device, const WGPUBindGroupDescriptor *descriptor) {
    assert(device && descriptor && (descriptor->entries[2].size == 32 || descriptor->entries[2].size == 48));
    return (WGPUBindGroup)(uintptr_t)5;
}

void wgpuTextureDestroy(WGPUTexture texture) { assert(texture); }
void wgpuTextureRelease(WGPUTexture texture) { assert(texture); }
void wgpuTextureViewRelease(WGPUTextureView view) { assert(view); }
void wgpuBindGroupRelease(WGPUBindGroup group) { assert(group); }

void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer, uint64_t offset,
                          const void *data, size_t size) {
    if (present_video && (buffer == present_gpu->vubuf || buffer == present_gpu->yubuf)) {
        assert(offset == 0);
        assert(size == (buffer == present_gpu->vubuf ? 32 : 48));
        const float *u=data;
        for(int i=0;i<4;i++) assert(u[i]==expected_rect[i]);
        memcpy(video_parameters, data, size);
        if(buffer==present_gpu->vubuf) for(int i=4;i<8;i++) assert(u[i]==0);
        video_uniforms++;return;
    }
    assert(queue && buffer && offset == 16 && size == sizeof scroll_uniform);
    memcpy(scroll_uniform, data, size);
    uniform_writes++;
}

void wgpuQueueWriteTexture(WGPUQueue queue, const WGPUTexelCopyTextureInfo *destination,
                           const void *data, size_t size, const WGPUTexelCopyBufferLayout *layout,
                           const WGPUExtent3D *extent) {
    if (present_video && destination->texture != present_gpu->tex) {
        assert(destination->texture==present_gpu->vtex || destination->texture==present_gpu->yt || destination->texture==present_gpu->ct);
        assert(destination->origin.y==0 && data && size);
        assert(size <= 128);
        memcpy(destination->texture==present_gpu->vtex ? rgba_uploaded :
               destination->texture==present_gpu->yt ? y_uploaded : uv_uploaded, data, size);
        video_writes++;return;
    }
    assert(queue && destination->texture && destination->origin.x == 0);
    assert((regular_upload || destination->origin.y >= (unsigned)active_toolbar) && destination->origin.z == 0);
    assert(destination->mipLevel == 0 && layout->offset == 0);
    assert(extent->width == (unsigned)active_width && extent->height > 0);
    assert(destination->origin.y + extent->height <= (unsigned)active_height);
    assert(extent->depthOrArrayLayers == 1 && layout->bytesPerRow == (unsigned)active_stride * 4);
    assert(layout->rowsPerImage == extent->height);
    assert(size == ((size_t)(extent->height - 1) * (size_t)active_stride + (size_t)active_width) * 4);
    const uint32_t *source = data;
    if (regular_upload) {
        assert(source == linear_pixels && extent->height == (unsigned)active_height && destination->origin.y == 0);
    } else assert(source >= active_source && source + (size + 3) / 4 <= active_source + 1024);
    for (unsigned row = 0; row < extent->height; row++)
        memcpy(texture_pixels + (destination->origin.y + row) * (unsigned)active_width,
               source + row * (unsigned)active_stride, (size_t)active_width * 4);
    writes++;
    uploaded_rows += extent->height;
}

/* Exercise the actual presenter, including both video formats, rather than
   truncating it before the video upload and draw operations. */
void wgpuSurfaceUnconfigure(WGPUSurface s) { (void)s;abort(); }
void wgpuSurfaceConfigure(WGPUSurface surface,const WGPUSurfaceConfiguration *config) { (void)surface;(void)config;abort(); }
void wgpuSurfaceGetCurrentTexture(WGPUSurface surface,WGPUSurfaceTexture *out) {
    assert(surface);out->status=WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    out->texture=(WGPUTexture)(uintptr_t)30;
}
WGPUCommandEncoder wgpuDeviceCreateCommandEncoder(WGPUDevice device,const WGPUCommandEncoderDescriptor *d) {
    assert(device && !d);return (WGPUCommandEncoder)(uintptr_t)31;
}
WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder e,const WGPURenderPassDescriptor *d) {
    assert(e && d->colorAttachmentCount==1);return (WGPURenderPassEncoder)(uintptr_t)32;
}
void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder p,WGPURenderPipeline pipeline) { assert(p && pipeline); }
void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder p,uint32_t index,WGPUBindGroup group,size_t n,const uint32_t *offsets) {
    assert(p && index==0 && !n && !offsets);selected_group=group;
}
void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder p,uint32_t vertices,uint32_t instances,uint32_t first,uint32_t instance) {
    assert(p && vertices==4 && instances==1 && !first && !instance && draw_count<2);
    drawn[draw_count++]=selected_group;
}
void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder p) { assert(p); }
void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder p) { assert(p); }
WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder e,const WGPUCommandBufferDescriptor *d) {
    assert(e && !d);return (WGPUCommandBuffer)(uintptr_t)33;
}
void wgpuQueueSubmit(WGPUQueue q,size_t n,const WGPUCommandBuffer *c) { assert(q && n==1 && *c); }
void wgpuCommandBufferRelease(WGPUCommandBuffer c) { assert(c); }
void wgpuCommandEncoderRelease(WGPUCommandEncoder e) { assert(e); }
WGPUStatus wgpuSurfacePresent(WGPUSurface s) { assert(s);return WGPUStatus_Success; }

static Gpu initialize(int width, int page_height, int toolbar, int padding) {
    active_width = width;
    active_height = toolbar + page_height;
    active_stride = width + padding;
    active_toolbar = toolbar;
    for (int row = 0; row < active_height; row++) for (int column = 0; column < width; column++)
        texture_pixels[row * width + column] = 0xff000000u + (uint32_t)(row * width + column);
    for (int row = 0; row < page_height; row++) for (int column = 0; column < active_stride; column++)
        page_pixels[row * active_stride + column] = column < width
            ? texture_pixels[(row + toolbar) * width + column] : 0xabcdef01u;
    memset(scroll_uniform, 0, sizeof scroll_uniform);
    writes = uploaded_rows = uniform_writes = 0;
    regular_upload = false;
    return (Gpu){.configured = true, .tex = (WGPUTexture)(uintptr_t)1,
                 .q = (WGPUQueue)(uintptr_t)2, .ubuf = (WGPUBuffer)(uintptr_t)3,
                 .w = width, .tw = width, .h = active_height, .th = active_height};
}

static void verify(const Gpu *gpu) {
    assert(scroll_uniform[0] == (unsigned)active_toolbar);
    assert(scroll_uniform[1] == (unsigned)(active_height - active_toolbar));
    assert(scroll_uniform[2] == (unsigned)gpu->scroll_offset && scroll_uniform[3] == 0);
    for (int row = 0; row < active_toolbar; row++) for (int column = 0; column < active_width; column++)
        assert(texture_pixels[row * active_width + column] == 0xff000000u + (uint32_t)(row * active_width + column));
    for (int row = 0; row < active_height - active_toolbar; row++) {
        int mapped = active_toolbar + (row + gpu->scroll_offset) % (active_height - active_toolbar);
        for (int column = 0; column < active_width; column++)
            assert(texture_pixels[mapped * active_width + column] == page_pixels[row * active_stride + column]);
        for (int column = active_width; column < active_stride; column++)
            assert(page_pixels[row * active_stride + column] == 0xabcdef01u);
    }
}

static void step(Gpu *gpu, int delta, int fixed_first, int fixed_last) {
    int height = active_height - active_toolbar;
    int amount = delta > 0 ? delta : -delta;
    if (delta > 0)
        memmove(page_pixels, page_pixels + amount * active_stride,
                (size_t)(height - amount) * (size_t)active_stride * 4);
    else
        memmove(page_pixels + amount * active_stride, page_pixels,
                (size_t)(height - amount) * (size_t)active_stride * 4);
    int first = delta > 0 ? height - amount : 0;
    int last = delta > 0 ? height : amount;
    for (int row = 0; row < height; row++) {
        if (!((row >= first && row < last) || (row >= fixed_first && row < fixed_last))) continue;
        for (int column = 0; column < active_width; column++)
            page_pixels[row * active_stride + column] = 0xff400000u + uniform_writes * 1000u + (unsigned)(row * active_width + column);
    }
    unsigned before = uploaded_rows;
    if (circular_source) {
        int offset=(uniform_writes*3+height-1)%height;
        for(int y=0;y<height;y++) memcpy(ring_pixels+((y+offset)%height)*active_stride,
                                       page_pixels+y*active_stride,(size_t)active_stride*4);
        active_source=ring_pixels;
        assert(gpu_scroll_page_ring(gpu,ring_pixels,active_width,active_height,active_stride,
                                   active_toolbar,delta,fixed_first,fixed_last,offset));
        active_source=page_pixels;
    } else assert(gpu_scroll_page(gpu, page_pixels, active_width, active_height, active_stride,
                           active_toolbar, delta, fixed_first, fixed_last));
    int overlap_first = first > fixed_first ? first : fixed_first;
    int overlap_last = last < fixed_last ? last : fixed_last;
    unsigned expected = (unsigned)(amount + fixed_last - fixed_first - (overlap_last > overlap_first ? overlap_last - overlap_first : 0));
    assert(uploaded_rows - before == expected);
    verify(gpu);
}

static void present_cached(Gpu *gpu, GpuVideo *video, unsigned expected_writes) {
    video_writes=video_uniforms=draw_count=0;
    assert(gpu_present_frame(gpu,linear_pixels,4,9,4,0,0,video));
    assert(video_writes==expected_writes && video_uniforms==1);
    assert(draw_count==2 && drawn[0]==(video->yuv?gpu->ybg:gpu->vbg) && drawn[1]==gpu->bg);
    verify(gpu);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *scenario = argv[1];
    Gpu gpu = initialize(4, 7, 2, !strcmp(scenario, "padded") ? 3 : 0);
    if (!strcmp(scenario, "video") || !strcmp(scenario, "video-cache")) {
        /* Transparent and translucent page texels survive wrapping; the video
           is drawn first with independent coordinates/uniforms. */
        for(int row=0;row<7;row++) for(int x=0;x<4;x++) {
            uint32_t c=x==1?0:x==2?0x80402010:0xff223344;
            page_pixels[row*4+x]=texture_pixels[(row+2)*4+x]=c;
        }
        gpu.dev=(WGPUDevice)(uintptr_t)10;gpu.surf=(WGPUSurface)(uintptr_t)11;
        gpu.pipe=(WGPURenderPipeline)(uintptr_t)12;gpu.ypipe=(WGPURenderPipeline)(uintptr_t)13;
        gpu.bg=(WGPUBindGroup)(uintptr_t)14;gpu.vbg=(WGPUBindGroup)(uintptr_t)15;gpu.ybg=(WGPUBindGroup)(uintptr_t)16;
        gpu.vtex=(WGPUTexture)(uintptr_t)17;gpu.yt=(WGPUTexture)(uintptr_t)18;gpu.ct=(WGPUTexture)(uintptr_t)19;
        gpu.ybgl=(WGPUBindGroupLayout)(uintptr_t)22;
        gpu.vubuf=(WGPUBuffer)(uintptr_t)20;gpu.yubuf=(WGPUBuffer)(uintptr_t)21;
        gpu.vtw=gpu.vth=gpu.ytw=gpu.yth=2;
        present_gpu=&gpu;present_video=true;
        uint32_t rgba[16]={0xff112233,0xff445566,0xff778899,0xffaabbcc};uint8_t yuv[32]={16,32,64,128,128,128};
        GpuVideo video={.px=rgba,.w=2,.h=2,.stride=2,.x1=4,.y1=9};
        for(int i=0;i<12;i++) {
            step(&gpu,i%3==0?-2:1,0,0);
            unsigned before=writes,uniform_before=uniform_writes;
            video_writes=video_uniforms=draw_count=0;
            video.yuv=i%2?yuv:NULL;
            assert(gpu_present_frame(&gpu,linear_pixels,4,9,4,0,0,&video));
            assert(video_writes==(video.yuv?2u:1u) && video_uniforms==1);
            assert(draw_count==2 && drawn[0]==(video.yuv?gpu.ybg:gpu.vbg) && drawn[1]==gpu.bg);
            assert(writes==before && uniform_writes==uniform_before);
            verify(&gpu);
        }
        if (!strcmp(scenario, "video-cache")) {
            for(int format=0;format<2;format++) {
                video.yuv=format?yuv:NULL;
                video.serial=100+(unsigned)format*10;
                unsigned count=format?2:1;
                present_cached(&gpu,&video,count);
                /* Scrolling and resize of the displayed rectangle update
                   uniforms, without re-uploading the paused decoded frame. */
                for(int i=0;i<60;i++) {
                    step(&gpu,i%2?1:-1,0,0);
                    video.x0=(float)(i%2); video.y0=(float)(i%3);
                    video.x1=3;video.y1=8;
                    expected_rect[0]=video.x0/4*2-1;expected_rect[1]=1-video.y0/9*2;
                    expected_rect[2]=video.x1/4*2-1;expected_rect[3]=1-video.y1/9*2;
                    present_cached(&gpu,&video,0);
                }
                /* Reused source addresses with a new decoded frame must upload. */
                rgba[0]++; yuv[0]++; video.serial++;
                present_cached(&gpu,&video,count);
                assert(format ? y_uploaded[0]==yuv[0] && uv_uploaded[0]==yuv[4] :
                       !memcmp(rgba_uploaded,rgba,16));
                present_cached(&gpu,&video,0);
                if(format) {
                    float previous=video_parameters[4];video.mat=3;
                    present_cached(&gpu,&video,0);
                    assert(video_parameters[4]!=previous && video_parameters[8]==0 && video_parameters[9]==1);
                }
                /* Callers without identity remain conservative, including
                   switching back from unknown data to an earlier identity. */
                uint64_t serial=video.serial;video.serial=0;
                present_cached(&gpu,&video,count);present_cached(&gpu,&video,count);
                video.serial=serial;present_cached(&gpu,&video,count);
                /* Texture replacement invalidates content even if the caller
                   retains the same serial. Both planes use odd-size extents. */
                video.w=video.h=video.stride=3;
                present_cached(&gpu,&video,count);present_cached(&gpu,&video,0);
                video.w=video.h=video.stride=2;
                present_cached(&gpu,&video,count);
                draw_count=0;
                assert(gpu_present_frame(&gpu,linear_pixels,4,9,4,0,0,NULL));
                assert(draw_count==1 && drawn[0]==gpu.bg);
                present_cached(&gpu,&video,0);
                video.x0=video.y0=0;video.x1=4;video.y1=9;
                expected_rect[0]=-1;expected_rect[1]=1;expected_rect[2]=1;expected_rect[3]=-1;
            }
        }
        present_gpu=NULL;present_video=false;
    } else if (!strcmp(scenario, "up")) step(&gpu, 2, 0, 0);
    else if (!strcmp(scenario, "down")) step(&gpu, -2, 0, 0);
    else if (!strcmp(scenario, "padded")) {
        step(&gpu, 2, 0, 0); step(&gpu, -3, 1, 4); step(&gpu, 3, 0, 2);
    } else if (!strcmp(scenario, "fixed")) {
        step(&gpu, 2, 4, 7); step(&gpu, -3, 0, 4); step(&gpu, 1, 2, 4);
    } else if ((!strcmp(scenario, "matrix") || !strcmp(scenario,"cpu-ring"))) {
        circular_source=!strcmp(scenario,"cpu-ring");
        for (int height = 2; height <= 17; height++) for (int delta = 1 - height; delta < height; delta++) {
            if (!delta) continue;
            for (int first = 0; first <= height; first++) for (int last = first; last <= height; last++) {
                gpu = initialize(3, height, 2, 2);
                step(&gpu, delta, first, last);
                step(&gpu, delta, 0, 0);
                step(&gpu, -delta, first, last);
            }
        }
    } else if (!strcmp(scenario,"cpu-ring-full")) {
        step(&gpu,2,0,0);
        for(int offset=0;offset<7;offset++) {
            for(int y=0;y<7;y++) for(int x=0;x<4;x++) {
                page_pixels[y*4+x]^=0x00ffff00u;
                ring_pixels[((y+offset)%7)*4+x]=page_pixels[y*4+x];
            }
            active_source=ring_pixels;
            unsigned old_rows=uploaded_rows;
            assert(gpu_upload_page_ring(&gpu,ring_pixels,4,9,4,2,0,7,offset));
            assert(uploaded_rows-old_rows==7);
            verify(&gpu);
        }
        unsigned old_writes=writes,old_uniforms=uniform_writes;
        assert(!gpu_upload_page_ring(&gpu,ring_pixels,4,9,4,2,0,7,-1));
        assert(!gpu_upload_page_ring(&gpu,ring_pixels,4,9,4,2,0,7,7));
        assert(!gpu_scroll_page_ring(&gpu,ring_pixels,4,9,4,2,2,0,0,-1));
        assert(!gpu_scroll_page_ring(&gpu,ring_pixels,4,9,4,2,2,0,0,7));
        assert(writes==old_writes && uniform_writes==old_uniforms);
        active_source=page_pixels;
    } else if (!strcmp(scenario, "full-page")) {
        step(&gpu, 2, 0, 0);
        for (int index = 0; index < 28; index++) page_pixels[index] ^= 0x00ffffffu;
        unsigned before = uploaded_rows;
        assert(gpu_upload_page(&gpu, page_pixels, 4, 9, 4, 2, 0, 7));
        assert(uploaded_rows - before == 7);
        verify(&gpu);
    } else if (!strcmp(scenario, "present-reset") || !strcmp(scenario, "present-skip")) {
        step(&gpu, 2, 0, 0);
        unsigned before = writes, uniform_before = uniform_writes;
        bool reset = !strcmp(scenario, "present-reset");
        memcpy(linear_pixels, texture_pixels, (size_t)active_toolbar * (size_t)active_width * 4);
        memcpy(linear_pixels + active_toolbar * active_width, page_pixels, 7 * 4 * 4);
        regular_upload = true;
        assert(prepare_present_test(&gpu, linear_pixels, 4, 9, 4, reset ? 3 : 0, reset ? 4 : 0, NULL));
        if (reset) {
            assert(writes == before + 1 && uniform_writes == uniform_before + 1);
            assert(!gpu.scroll_height && !gpu.scroll_offset);
            assert(!memcmp(texture_pixels, linear_pixels, 9 * 4 * 4));
        } else {
            assert(writes == before && uniform_writes == uniform_before);
            verify(&gpu);
        }
    } else if (!strcmp(scenario, "reset")) {
        step(&gpu, 2, 0, 0);
        reset_page_scroll(&gpu);
        assert(gpu.scroll_height == 0 && gpu.scroll_toolbar == 0 && gpu.scroll_offset == 0);
        for (unsigned index = 0; index < 4; index++) assert(scroll_uniform[index] == 0);
        unsigned before = uniform_writes;
        reset_page_scroll(&gpu);
        assert(uniform_writes == before);
    } else if (!strcmp(scenario, "invalid")) {
        assert(!gpu_scroll_page(NULL, page_pixels, 4, 9, 4, 2, 1, 0, 0));
        assert(!gpu_scroll_page(&gpu, NULL, 4, 9, 4, 2, 1, 0, 0));
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 3, 2, 1, 0, 0));
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, INT_MAX, 2, 1, 0, 0));
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, -1, 1, 0, 0));
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 9, 1, 0, 0));
        const int deltas[] = {0, 7, -7, INT_MIN, INT_MAX};
        for (size_t index = 0; index < sizeof deltas / sizeof *deltas; index++)
            assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, deltas[index], 0, 0));
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, 1, -1, 0));
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, 1, 0, 8));
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, 1, 4, 3));
        assert(!writes && !uniform_writes && !gpu.scroll_height);
        step(&gpu, 2, 0, 0);
        unsigned before = writes, uniform_before = uniform_writes;
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 1, 1, 0, 0));
        assert(!gpu_upload_page(&gpu, page_pixels, 4, 9, 4, 1, 0, 8));
        assert(writes == before && uniform_writes == uniform_before);
        verify(&gpu);
    } else if (!strcmp(scenario, "unavailable")) {
        gpu.tex = NULL;
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, 1, 0, 0));
        gpu = initialize(4, 7, 2, 0); gpu.tw = 3;
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, 1, 0, 0));
        gpu = initialize(4, 7, 2, 0); gpu.h = 8;
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, 1, 0, 0));
        gpu = initialize(4, 7, 2, 0); gpu.configured = false;
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, 1, 0, 0));
        gpu = initialize(4, 7, 2, 0); gpu.ubuf = NULL;
        assert(!gpu_scroll_page(&gpu, page_pixels, 4, 9, 4, 2, 1, 0, 0));
        assert(!writes && !uniform_writes);
    } else if (!strcmp(scenario, "shader")) {
        assert(strstr(WGSL, "scroll: vec4u"));
        assert(strstr(WGSL, "textureSampleLevel"));
        assert(strstr(WGSL, "mapped -= params.scroll.y"));
        assert(!strstr(WGSL, "%"));
        assert(!strstr(WGSL, "textureSample("));
    } else assert(false);
    return 0;
}
