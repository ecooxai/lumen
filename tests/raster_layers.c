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

int main(void) {
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
    for (int punch=0;punch<2;punch++) {
        expected.punch=partial.punch=punch ? &picture : NULL;
        for (int i=0;i<STRIDE*H;i++) ref[i]=0xdeadbeef;
        allocation_limit=SIZE_MAX; raster(&expected,&dl,RGBA(255,255,255,255));
        for (int height=1;height<=H;height+=5) {
            for (int y=0;y<H;y+=height) {
                int bottom=LMIN(H,y+height);
                for (int i=0;i<STRIDE*H;i++) actual[i]=0xdeadbeef;
                largest_allocation=0; allocation_limit=(size_t)STRIDE*(bottom-y)*4;
                raster_rect(&partial,&dl,RGBA(255,255,255,255),7,y,W-3,bottom);
                assert(largest_allocation==allocation_limit);
                for (int row=0;row<H;row++) for (int x=0;x<STRIDE;x++) {
                    uint32_t want=row>=y && row<bottom && x>=7 && x<W-3 ? ref[row*STRIDE+x] : 0xdeadbeef;
                    assert(actual[row*STRIDE+x]==want);
                }
            }
        }
    }
    /* Unbalanced and overflowed groups must free exactly their own storage. */
    DItem deep[40];
    for(int i=0;i<20;i++) deep[i]=(DItem){.op=DO_PUSH_LAYER,.alpha=.5f};
    for(int i=20;i<40;i++) deep[i]=(DItem){.op=DO_POP_LAYER};
    dl.items.v=deep; allocation_limit=STRIDE*4;
    dl.items.n=40; raster_rect(&partial,&dl,0,0,H-1,W,H);
    dl.items.n=20; raster_rect(&partial,&dl,0,0,H-1,W,H);
    puts("Layer pixels, padded stride, all primitives, nested clips, punch, bounds and allocation limits pass");
}
