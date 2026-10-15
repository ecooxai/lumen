#include "../src/paint/paint.h"
#include <assert.h>
#include <stdio.h>

const Glyph *font_glyph(Font *font, uint32_t gid, float scale) {
    (void)font; (void)gid; (void)scale; abort();
}
void image_yuv_materialize(Image *image) { (void)image; abort(); }

typedef struct { Canvas page; DisplayList pdl; bool gvid_ok; } App;
static void scroll(App *a, int blit) {
    int ph=a->page.h, pwp=a->page.w, scroll_delta=0;
    int rx0=-1,ry0=-1,rx1=-1,ry1=-1;
    struct { bool blit; } profile_frame={0};
    PaintRegions scroll_fixed=paint_scroll_regions(&a->pdl.fixed,a->page.scale,pwp,ph,blit);
    assert(scroll_fixed.count==2);
    int rows=0;
    for(int i=0;i<scroll_fixed.count;i++) rows+=(int)(scroll_fixed.rect[i][3]-scroll_fixed.rect[i][1]);
    assert(rows*2<ph); /* split header/footer must not force a full paint */
#include "scroll_raster.h"
    assert(scroll_delta==blit && profile_frame.blit);
    assert(rx0==0 && ry0==0 && rx1==pwp && ry1==ph);
}

int main(void) {
    /* Bridging ranges merge transitively; capacity overflow never loses damage. */
    PaintRegions regions={0};
    for(int i=0;i<PAINT_REGION_MAX+1;i++) {
        float box[4]={0,i*10,10,i*10+2}; paint_regions_add(&regions,box);
    }
    assert(regions.count==1 && regions.rect[0][1]==0 && regions.rect[0][3]==82);
    regions=(PaintRegions){0};
    paint_regions_add(&regions,(float[]){0,0,10,2});
    paint_regions_add(&regions,(float[]){0,5,10,7});
    paint_regions_add(&regions,(float[]){0,2,10,5});
    assert(regions.count==1 && regions.rect[0][3]==7);
    enum {W=80,H=120,STRIDE=87};
    uint32_t pixels[STRIDE*H], reference[STRIDE*H];
    for(int percent=100;percent<=300;percent+=50) {
        float scale=percent/100.f;
        DItem items[50]={0}; int count=0;
        for(int i=0;i<40;i++) items[count++]=(DItem){.op=DO_RECT,.x=0,.y=(i*10-140)/scale,
            .w=W/scale,.h=10/scale,.color=RGBA(i*5,180-i*3,40+i*2,255)};
        items[count++]=(DItem){.op=DO_PUSH_LAYER,.alpha=.6f};
        items[count++]=(DItem){.op=DO_RECT,.w=W/scale,.h=10/scale,.color=RGBA(30,60,100,220)};
        items[count++]=(DItem){.op=DO_RECT,.x=10/scale,.y=110/scale,.w=70/scale,.h=10/scale,.color=RGBA(230,80,40,180)};
        items[count++]=(DItem){.op=DO_POP_LAYER};
        App app={.page={.w=W,.h=H,.stride=STRIDE,.scale=scale,.px=pixels},
            .pdl={.items={.v=items,.n=count}}};
        paint_regions_add(&app.pdl.fixed,(float[]){0,0,W/scale,10/scale});
        paint_regions_add(&app.pdl.fixed,(float[]){10/scale,110/scale,W/scale,H/scale});
        Canvas full=app.page; full.px=reference;
        for(int i=0;i<STRIDE*H;i++) pixels[i]=reference[i]=0xdeadbeef;
        raster(&app.page,&app.pdl,0xffffffff);
        int position=0;
        for(int step=0;step<100;step++) {
            const int shifts[]={3,7,-3,-7};int delta=shifts[step%4];position+=delta;
            for(int i=0;i<40;i++) items[i].y=(i*10-140-position)/scale;
            scroll(&app,delta);
            raster(&full,&app.pdl,0xffffffff);
            for(int y=0;y<H;y++) for(int x=0;x<W;x++) {
                if(pixels[y*STRIDE+x]!=reference[y*STRIDE+x]) {
                    fprintf(stderr,"mismatch scale=%g step=%d at %d,%d\n",scale,step,x,y);abort();
                }
            }
        }
    }
    puts("500 scroll frames match full raster with separate translucent fixed header/footer");
}
