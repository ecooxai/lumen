#include "../src/paint/paint.h"
#include <assert.h>

static size_t allocation_limit = SIZE_MAX, largest_allocation;
static void *layer_calloc(size_t count, size_t size) {
    assert(size && count <= SIZE_MAX / size);
    size_t bytes = count * size;
    assert(bytes <= allocation_limit);
    if (bytes > largest_allocation) largest_allocation = bytes;
    return xcalloc(count, size);
}
#define xcalloc layer_calloc
#include "../src/paint/raster.c"
#undef xcalloc

const Glyph *font_glyph(Font *font, uint32_t gid, float scale) {
    (void)font; (void)scale;
    static uint8_t alpha[4] = {255, 170, 90, 0};
    static uint32_t rgba[4] = {0xff102030, 0x80102030, 0x400a141e, 0};
    static Glyph mono = {.w=2, .h=2, .alpha=alpha};
    static Glyph color = {.w=2, .h=2, .color=true, .rgba=rgba};
    return gid ? &color : &mono;
}
void image_yuv_materialize(Image *image) { (void)image; abort(); }

/* Compare the optimized gradient spans with an independent scalar
   pixel traversal, including fractional edges, repeating/hard stops, damage,
   and translucent source/destination pixels. Direction is mathematically
   exact, so 180deg must not acquire a horizontal component from sinf(pi). */
static void check_gradient_spans(void) {
    enum { W=43, H=37, STRIDE=47 };
    uint32_t actual[H*STRIDE], expected[H*STRIDE];
    Gradient g={.nstops=4};
    g.stops[0].pos=.15f; g.stops[0].color=RGBA(20,80,190,0);
    g.stops[1].pos=.4f; g.stops[1].color=RGBA(230,20,80,255);
    g.stops[2].pos=.4f; g.stops[2].color=RGBA(50,210,30,180);
    g.stops[3].pos=.9f; g.stops[3].color=RGBA(180,40,220,90);
    const float angles[]={0,180,-180,360,540,90,270,-90,450};
    for(int variant=0;variant<100;variant++) {
        float scale=.75f+(variant%4)*.5f;
        g.angle=angles[variant%9];g.repeating=variant%3==0;
        DItem item={.op=DO_GRADIENT,.x=-2.3f+(variant%7),.y=-4.8f+(variant%9),
                    .w=20.8f+(variant%3)*.3f,.h=22.1f,.grad=&g};
        if(variant%10==0) item.w=.1f;
        if(variant%11==0) item.h=.2f;
        Canvas canvas={.w=W,.h=H,.stride=STRIDE,.scale=scale,.px=actual};
        R r={.c=&canvas,.s=scale,.nclip=1,.px=actual,.stride=STRIDE,.height=H};
        r.clips[0]=(Clip){3,2,W-4,H-3,0,0,W,H,{0},false};
        for(int i=0;i<H*STRIDE;i++) expected[i]=actual[i]=0x80604020;
        gradient(&r,&item);
        float x0=item.x*scale,y0=item.y*scale,x1=(item.x+item.w)*scale,y1=(item.y+item.h)*scale;
        bool horizontal=variant%9>=5;
        float len=horizontal ? fabsf(x1-x0) : fabsf(y1-y0);if(len<1e-3f)len=1;
        float cy=(y0+y1)/2,cx=(x0+x1)/2;
        float dir=variant%9==0 || variant%9==3 || variant%9==6 || variant%9==7 ? -1 : 1;
        for(int y=2;y<H-3;y++) for(int x=3;x<W-4;x++) {
            float cov=span_cov(x0,x1,x)*span_cov(y0,y1,y);
            if(cov>0) over(&expected[y*STRIDE+x],premul(grad_at(&g,((horizontal ? x+.5f-cx : y+.5f-cy)*dir)/len+.5f),cov));
        }
        assert(memcmp(actual,expected,sizeof actual)==0);
    }
    /* Wide rows around quantization boundaries must have identical colors. */
    uint32_t wide[4096];
    g.angle=180;g.repeating=false;g.nstops=2;
    g.stops[0].pos=0;g.stops[0].color=RGBA(0,0,0,255);
    g.stops[1].pos=1;g.stops[1].color=RGBA(254,254,254,255);
    Canvas c={.w=4096,.h=1,.stride=4096,.scale=1,.px=wide};
    R r={.c=&c,.s=1,.nclip=1,.px=wide,.stride=4096,.height=1};
    r.clips[0]=(Clip){0,0,4096,1,0,0,4096,1,{0},false};
    DItem it={.op=DO_GRADIENT,.w=4096,.h=1,.grad=&g};
    gradient(&r,&it);
    for(int x=0;x<4096;x++) assert(wide[x]==RGBA(127,127,127,255));
}

/* The scalar oracle samples the source directly for every destination pixel.
   It shares only the unchanged packed-pixel blend/coverage helpers, not the
   new scanline cache or its replacement policy. */
static void check_image_scanlines(void) {
    enum { W=61,H=57,STRIDE=67 };
    uint32_t actual[H*STRIDE],expected[H*STRIDE],source[23*19];
    for(int i=0;i<23*19;i++) source[i]=premul(RGBA(i*7%256,i*13%256,i*23%256,i*31%256),1);
    for(int variant=0;variant<180;variant++) {
        Image im={.w=variant%9==0?1:23,.h=variant%7==0?1:19,.px=source};
        DItem item={.op=DO_IMAGE,.x=-3.8f+variant%11,.y=-7.3f+variant%13,
                   .w=4.7f+(variant%17)*4,.h=3.1f+(variant%19)*3,.img=&im};
        float scale=.75f+(variant%4)*.5f;
        Canvas ca={.w=W,.h=H,.stride=STRIDE,.scale=scale,.px=actual};
        R r={.c=&ca,.s=scale,.nclip=1,.px=actual,.stride=STRIDE,.height=H,
             .origin_x=variant%2,.origin_y=variant%2?2:0};
        r.clips[0]=(Clip){2,3,W-3,H-4,2.3f,3.1f,W-5.5f,H-7.2f,{4,5,6,7},variant%3==0};
        for(int i=0;i<H*STRIDE;i++) actual[i]=expected[i]=0x80604020;
        image(&r,&item);
        float x0=item.x*scale,y0=item.y*scale,x1=(item.x+item.w)*scale,y1=(item.y+item.h)*scale;
        float kx=im.w/(x1-x0),ky=im.h/(y1-y0);
        bool down=kx>1.5f || ky>1.5f;
        for(int y=3;y<H-4;y++) for(int x=2;x<W-3;x++) {
            float cov=span_cov(x0,x1,x)*span_cov(y0,y1,y);
            if(cov<=0) continue;
            float sx=(x+.5f-x0)*kx-.5f,sy=(y+.5f-y0)*ky-.5f;
            uint32_t pixel;
            if(down) pixel=source[LCLAMP((int)(sy+.5f),0,im.h-1)*im.w+LCLAMP((int)(sx+.5f),0,im.w-1)];
            else {
                int ix=(int)floorf(sx),iy=(int)floorf(sy);
                uint32_t wx=(uint32_t)((sx-ix)*256),wy=(uint32_t)((sy-iy)*256);
                int xa=LCLAMP(ix,0,im.w-1),xb=LCLAMP(ix+1,0,im.w-1);
                int ya=LCLAMP(iy,0,im.h-1)*im.w,yb=LCLAMP(iy+1,0,im.h-1)*im.w;
                uint32_t top=scale_px(source[ya+xa],256-wx)+scale_px(source[ya+xb],wx);
                uint32_t bot=scale_px(source[yb+xa],256-wx)+scale_px(source[yb+xb],wx);
                pixel=scale_px(top,256-wy)+scale_px(bot,wy);
            }
            cov*=clip_cov(&r,x+.5f,y+.5f);
            if(cov<.999f) pixel=scale_px(pixel,(uint32_t)(cov*256));
            over(&expected[(y-r.origin_y)*STRIDE+x-r.origin_x],pixel);
        }
        assert(memcmp(actual,expected,sizeof actual)==0);
    }
}

/* Every interpolation weight, vector tail length and representative alpha
   endpoint must preserve the scalar source-over result and surrounding pixels. */
static void check_image_vertical(void) {
    uint32_t top[71],bottom[71],actual[73],expected[73];
    const uint32_t colors[]={0,0xffffffff,0x01010101,0x7f604020,0x80604020,0xfe987654,0xff987654};
    for(int weight=0;weight<=256;weight++) for(int count=0;count<=71;count++) {
        for(int x=0;x<71;x++) { top[x]=colors[(x+weight)%7];bottom[x]=colors[(x*3+weight/7)%7]; }
        for(int x=0;x<73;x++) actual[x]=expected[x]=colors[x%7];
        for(int x=0;x<count;x++) over(&expected[x+1],scale_px(top[x],256-weight)+scale_px(bottom[x],weight));
        image_vertical(actual+1,top,bottom,count,weight);
        assert(memcmp(actual,expected,sizeof actual)==0);
    }
    /* Exercise every alpha, different lane alignment and random premultiplied
       colors. This catches byte-lane/carry errors in the ARM channel loop. */
    uint32_t seed = 0x6194be31;
    for (int alpha = 0; alpha < 256; alpha++) for (int weight = 0; weight <= 256; weight++) {
        for (int x = 0; x < 71; x++) {
            uint32_t *targets[] = { &top[x], &bottom[x], &actual[x + 1] };
            for (int j = 0; j < 3; j++) {
                seed = seed * 1664525u + 1013904223u;
                uint32_t a = j == 0 ? (uint32_t)alpha : seed >> 24;
                *targets[j] = (a << 24) | (((seed >> 16 & 255) * a / 255) << 16) |
                              (((seed >> 8 & 255) * a / 255) << 8) | ((seed & 255) * a / 255);
            }
        }
        actual[0] = actual[72] = 0x98765432;
        memcpy(expected, actual, sizeof actual);
        for (int x = 0; x < 71; x++) over(&expected[x + 1], scale_px(top[x], 256 - weight) + scale_px(bottom[x], weight));
        image_vertical(actual + 1, top, bottom, 71, weight);
        assert(memcmp(actual, expected, sizeof actual) == 0);
    }
}

int main(int argc, char **argv) {
    (void)argv;
    check_gradient_spans();
    check_image_scanlines();
    check_image_vertical();
    enum { W=47, H=61, STRIDE=53 };
    Font font = {0};
    DGlyph glyphs[] = {{.x=12, .y=25, .font=&font}, {.gid=1, .x=15, .y=25, .font=&font}};
    uint32_t pixels[4] = {0xff123456, 0x80706050, 0xff654321, 0x40010203};
    Image picture = {.w=2, .h=2, .px=pixels};
    Gradient gradient = {.nstops=2};
    gradient.stops[0].pos=0; gradient.stops[0].color=RGBA(120,30,20,220);
    gradient.stops[1].pos=1; gradient.stops[1].color=RGBA(20,120,30,80);
    DItem items[] = {
        {.op=DO_RECT,.w=W,.h=H,.color=RGBA(80,100,120,255)},
        {.op=DO_PUSH_LAYER,.alpha=.6f},
        {.op=DO_RECT,.x=3.2f,.y=5.4f,.w=35,.h=45,.r={4,4,4,4},.color=RGBA(220,80,60,200)},
        {.op=DO_PUSH_CLIP,.x=5,.y=8,.w=32,.h=43,.r={3,3,3,3}},
        {.op=DO_PUSH_LAYER,.alpha=.4f},
        {.op=DO_SHADOW,.x=10,.y=12,.w=20,.h=30,.blur=3,.color=RGBA(10,30,60,140)},
        {.op=DO_GRADIENT,.x=9,.y=10,.w=25,.h=35,.grad=&gradient},
        {.op=DO_BORDER,.x=8,.y=9,.w=26,.h=36,.r={3,3,3,3},.bw={2,2,2,2},.bc={0xff123456,0xff123456,0xff123456,0xff123456}},
        {.op=DO_IMAGE,.x=10,.y=18,.w=23,.h=21,.img=&picture},
        {.op=DO_TEXT,.x=12,.y=25,.w=10,.h=3,.g=glyphs,.ng=2,.color=RGBA(20,40,60,255)},
        {.op=DO_POP_LAYER}, {.op=DO_POP_CLIP}, {.op=DO_POP_LAYER},
        {.op=DO_RECT,.x=4,.y=50,.w=30,.h=4,.color=RGBA(40,90,130,200)}
    };
    DisplayList dl={.items={.v=items,.n=sizeof items/sizeof *items}};
    uint32_t ref[STRIDE*H], actual[STRIDE*H];
    Canvas expected={.w=W,.h=H,.stride=STRIDE,.scale=1,.px=ref};
    Canvas partial=expected; partial.px=actual;
    for (int offset=0;offset<H;offset+=7) for (int punch=0;punch<2;punch++) {
        partial.row_offset=offset;
        expected.punch=partial.punch=punch ? &picture : NULL;
        for (int i=0;i<STRIDE*H;i++) ref[i]=0xdeadbeef;
        allocation_limit=SIZE_MAX; raster(&expected,&dl,RGBA(255,255,255,255));
        for (int height=1;height<=H;height+=5) {
            for (int y=0;y<H;y+=height) {
                int bottom=LMIN(H,y+height);
                for (int i=0;i<STRIDE*H;i++) actual[i]=0xdeadbeef;
                largest_allocation=0; allocation_limit=(size_t)STRIDE*(bottom-y)*4;
                raster_rect(&partial,&dl,RGBA(255,255,255,255),7,y,W-3,bottom);
                assert(largest_allocation<=allocation_limit);
                for (int row=0;row<H;row++) for (int x=0;x<STRIDE;x++) {
                    uint32_t want=row>=y && row<bottom && x>=7 && x<W-3 ? ref[row*STRIDE+x] : 0xdeadbeef;
                    assert(canvas_row(&partial,row)[x]==want);
                }
            }
        }
    }
    /* Golden full-frame pixels from the previous rasterizer, independently of
       the new layer-bound calculation. Include glyph ink outside item bounds,
       negative shadow spread, rounded clipping, fractional scale and punch. */
    const uint64_t golden[] = {
        0xb65b6cea775e2817ull,
        0xd6079c45e0f0a8ceull,
        0xb2f87b699956cd25ull,
        0x07e1b03d0d2fbfb1ull,
        0x8a7d59cf9d8182b9ull,
        0x28d91d342093d558ull,
        0xd0356de1f26548e6ull,
        0xe2444f9dec120cfdull,
        0xfdb228f65e307ffaull,
        0xb548dc067d18afbbull,
        0xe7ae8ae60d4af34bull,
        0x1a7e4de10013b585ull,
    };
    for (int variant=0; variant<12; variant++) {
        expected.scale = .75f + (variant % 4) * .5f;
        expected.punch = variant >= 6 ? &picture : NULL;
        items[4].alpha = .2f + (variant % 3) * .3f;
        items[5].spread = variant % 2 ? -1.5f : 3.5f;
        items[9].x=0; items[9].y=0; items[9].w=0; items[9].h=0;
        glyphs[0].x = variant % 2 ? -1 : 42;
        glyphs[1].y = variant % 3 ? 60 : 2;
        allocation_limit=SIZE_MAX;
        for (int i=0;i<STRIDE*H;i++) ref[i]=0xdeadbeef;
        raster(&expected,&dl,RGBA(255,255,255,255));
        uint64_t hash=1469598103934665603ull;
        for (int i=0;i<STRIDE*H;i++) { hash^=ref[i];hash*=1099511628211ull; }
        if (argc>1) printf("0x%016llxull,\n",(unsigned long long)hash);
        else assert(hash==golden[variant]);
    }
    if (argc>1) return 0;
    /* Full paints of tiny groups must use tiny buffers, including nesting and
       offscreen/empty groups, regardless of the viewport or padded stride. */
    DItem small[] = {
        {.op=DO_PUSH_LAYER,.alpha=.7f},
        {.op=DO_RECT,.x=35,.y=51,.w=4,.h=3,.color=0xff123456},
        {.op=DO_PUSH_LAYER,.alpha=.3f},
        {.op=DO_RECT,.x=36,.y=52,.w=2,.h=1,.color=0xffabcdef},
        {.op=DO_POP_LAYER}, {.op=DO_POP_LAYER},
        {.op=DO_PUSH_LAYER,.alpha=.5f},
        {.op=DO_RECT,.x=100,.y=-10,.w=4,.h=3,.color=0xff123456},
        {.op=DO_POP_LAYER}, {.op=DO_PUSH_LAYER,.alpha=.5f}, {.op=DO_POP_LAYER}
    };
    dl.items.v=small; dl.items.n=sizeof small/sizeof *small;
    partial.scale=1; partial.punch=NULL;
    allocation_limit=4*3*4;largest_allocation=0;
    raster(&partial,&dl,0);
    assert(largest_allocation==4*3*4);
    /* Unbalanced and overflowed groups must free exactly their own storage. */
    DItem deep[40];
    for(int i=0;i<20;i++) deep[i]=(DItem){.op=DO_PUSH_LAYER,.alpha=.5f};
    for(int i=20;i<40;i++) deep[i]=(DItem){.op=DO_POP_LAYER};
    dl.items.v=deep; allocation_limit=STRIDE*4;
    dl.items.n=40; raster_rect(&partial,&dl,0,0,H-1,W,H);
    dl.items.n=20; raster_rect(&partial,&dl,0,0,H-1,W,H);
    puts("Layer pixels, padded stride, all primitives, nested clips, punch, bounds and allocation limits pass");
}
