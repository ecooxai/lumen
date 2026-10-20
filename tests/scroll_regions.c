#include "../src/paint/paint.h"
#include <assert.h>
#include <stdio.h>

const Glyph *font_glyph(Font *font, uint32_t gid, float scale) {
    (void)font; (void)gid; (void)scale; abort();
}
void image_yuv_materialize(Image *image) { (void)image; abort(); }

typedef struct { Canvas page; DisplayList pdl; bool gvid_ok; } App;
enum { PP_BLIT, PP_BDMG, PP_N }; static unsigned g_pp[PP_N];
enum { ENV_CHECK_DAMAGE }; static const char *env_hook(int i) { (void)i; return NULL; }
#include "video_scroll.h"
static void scroll(App *a, int blit, const DItem *vit) {
    bool scroll_video = vit != NULL;
    int ph=a->page.h, pwp=a->page.w, scroll_delta=0;
    int rx0=-1,ry0=-1,rx1=-1,ry1=-1;
    struct { bool blit; } profile_frame={0};
    bool sdmg=false, chrome_changed=false, part=true;
    PaintRegions scroll_fixed=paint_scroll_regions(&a->pdl.fixed,a->page.scale,pwp,ph,blit);
    assert(scroll_fixed.count>=2);
    int rows=0;
    for(int i=0;i<scroll_fixed.count;i++) rows+=(int)(scroll_fixed.rect[i][3]-scroll_fixed.rect[i][1]);
    assert(rows*2<ph); /* split header/footer must not force a full paint */
#include "scroll_raster.h"
    assert(scroll_delta==blit && profile_frame.blit && part && g_pp[PP_BLIT]);
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
    /* Fixed boxes: a header band and a full-height side column stay apart (also when the scroll
       extends them into each other); a box inside another, or a band continuing one, merges. */
    regions=(PaintRegions){0};
    paint_fixed_add(&regions,(float[]){0,0,1000,56});
    paint_fixed_add(&regions,(float[]){0,56,240,900});
    assert(regions.count==2);
    PaintRegions shifted=paint_scroll_regions(&regions,1,1000,900,-12);
    assert(shifted.count==2);
    paint_fixed_add(&regions,(float[]){10,10,100,40});
    paint_fixed_add(&regions,(float[]){0,900,240,1000});
    assert(regions.count==2 && regions.rect[0][3]+regions.rect[1][3]==56+1000);
    /* Full: each new box joins the region it grows least; every box stays covered. */
    regions=(PaintRegions){0};
    for(int i=0;i<3*PAINT_REGION_MAX;i++) {
        float box[4]={(i%5)*200,(i/5)*150,(i%5)*200+20,(i/5)*150+10}; paint_fixed_add(&regions,box);
        assert(regions.count<=PAINT_REGION_MAX);
    }
    for(int i=0;i<3*PAINT_REGION_MAX;i++) {
        float x=(i%5)*200+10,y=(i/5)*150+5; int in=0;
        for(int j=0;j<regions.count;j++) in|=x>=regions.rect[j][0]&&x<regions.rect[j][2]&&y>=regions.rect[j][1]&&y<regions.rect[j][3];
        assert(in);
    }
    /* Union area counts overlaps once: a header shifted down covers the strip exposed above it. */
    regions=(PaintRegions){0};
    paint_fixed_add(&regions,(float[]){0,0,100,50});
    paint_fixed_add(&regions,(float[]){0,0,10,100});
    assert(regions.count==2 && paint_regions_union_area(&regions,NULL)==5000+500);
    assert(paint_regions_union_area(&regions,(float[]){0,0,100,20})==5500);
    assert(paint_regions_union_area(&regions,(float[]){50,90,60,110})==5700);
    /* Geometry is evaluated in the same rounded pixel coordinates as the hole
       and video quad, including negative/offscreen coordinates. */
    DItem video={.op=DO_IMAGE,.x=4.2f,.y=-7.7f,.w=40,.h=20};
    float previous[]={4.2f,-4.2f,40,20};
    assert(video_scroll_matches(&video,previous,2,7));
    video.fixed=true;
    assert(!video_scroll_matches(&video,previous,2,7));
    video.y=previous[1];
    assert(video_scroll_matches(&video,previous,2,7));
    video.w+=1;
    assert(!video_scroll_matches(&video,previous,2,7));
    video.w-=1;video.x+=1;
    assert(!video_scroll_matches(&video,previous,2,7));
    video.x=NAN;
    assert(!video_scroll_matches(&video,previous,2,7));
    video.x=previous[0];
    assert(!video_scroll_matches(&video,previous,INFINITY,7));
    assert(!video_scroll_matches(&video,previous,0,7));
    enum {W=80,H=120,STRIDE=87};
    uint32_t pixels[STRIDE*H], reference[STRIDE*H];
    for(int mode=0;mode<4;mode++) for(int percent=100;percent<=300;percent+=50) {
        float scale=percent/100.f;
        DItem items[60]={0}; int count=0;
        for(int i=0;i<40;i++) items[count++]=(DItem){.op=DO_RECT,.x=0,.y=(i*10-140)/scale,
            .w=W/scale,.h=10/scale,.color=RGBA(i*5,180-i*3,40+i*2,255)};
        Image image={.w=2,.h=2};
        DItem *vit=NULL;
        if(mode) {
            /* Flow video crosses the viewport boundary; fixed video requires
               repairing both its old shifted hole and its current hole. */
            items[count++]=(DItem){.op=DO_PUSH_CLIP,.x=6/scale,.y=0,.w=66/scale,.h=H/scale};
            vit=&items[count++];
            *vit=(DItem){.op=DO_IMAGE,.img=&image,.x=3.25f/scale,
                .y=(mode==1?105.25f:35.25f)/scale,.w=71.2f/scale,.h=18.4f/scale,.fixed=mode==2};
            items[count++]=(DItem){.op=DO_POP_CLIP};
            items[count++]=(DItem){.op=DO_RECT,.x=20/scale,.y=40/scale,
                .w=40/scale,.h=4/scale,.color=RGBA(50,80,100,128)};
        }
        items[count++]=(DItem){.op=DO_PUSH_LAYER,.alpha=.6f};
        items[count++]=(DItem){.op=DO_RECT,.w=W/scale,.h=10/scale,.color=RGBA(30,60,100,220)};
        items[count++]=(DItem){.op=DO_RECT,.x=10/scale,.y=110/scale,.w=70/scale,.h=10/scale,.color=RGBA(230,80,40,180)};
        items[count++]=(DItem){.op=DO_POP_LAYER};
        App app={.page={.w=W,.h=H,.stride=STRIDE,.scale=scale,.px=pixels},
            .pdl={.items={.v=items,.n=count}}};
        paint_regions_add(&app.pdl.fixed,(float[]){0,0,W/scale,10/scale});
        paint_regions_add(&app.pdl.fixed,(float[]){10/scale,110/scale,W/scale,H/scale});
        if(vit && vit->fixed) paint_regions_add(&app.pdl.fixed,(float[]){vit->x,vit->y,vit->x+vit->w,vit->y+vit->h});
        app.page.punch=vit?vit->img:NULL;
        Canvas full=app.page; full.px=reference;
        for(int i=0;i<STRIDE*H;i++) pixels[i]=reference[i]=0xdeadbeef;
        raster(&app.page,&app.pdl,0xffffffff);
        app.gvid_ok=vit && app.page.punched;
        app.page.punch=NULL;
        int position=0;
        for(int step=0;step<100;step++) {
            const int shifts[]={3,7,-3,-7};int delta=shifts[step%4];position+=delta;
            for(int i=0;i<40;i++) items[i].y=(i*10-140-position)/scale;
            if(vit) {
                float old[]={vit->x,vit->y,vit->w,vit->h};
                if(!vit->fixed) vit->y=((mode==1?105.25f:35.25f)-position)/scale;
                assert(video_scroll_matches(vit,old,scale,delta));
                /* The translucent overlay follows the document. */
                items[43].y=(40-position)/scale;
            }
            scroll(&app,delta,vit);
            assert(app.gvid_ok==(vit!=NULL));
            assert(!app.page.punch);
            raster(&full,&app.pdl,0xffffffff);
            for(int y=0;y<H;y++) for(int x=0;x<W;x++) {
                if(pixels[y*STRIDE+x]!=reference[y*STRIDE+x]) {
                    fprintf(stderr,"mismatch mode=%d scale=%g step=%d at %d,%d\n",mode,scale,step,x,y);abort();
                }
            }
            for(int y=0;y<H;y++) for(int x=W;x<STRIDE;x++) assert(pixels[y*STRIDE+x]==0xdeadbeef);
        }
    }
    puts("2000 scroll frames match full raster with video holes, clips, translucent overlays and fixed header/footer");
}
