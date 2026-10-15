#include "../src/paint/paint.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void packing(int w, int h, unsigned flips) {
    int cw=(w+1)/2, ch=(h+1)/2;
    uint8_t *storage[3]; const uint8_t *planes[3]; int stride[3];
    for (int k=0;k<3;k++) {
        int width=k?cw:w, height=k?ch:h, pitch=width+7+k;
        storage[k]=malloc((size_t)pitch*height);
        memset(storage[k], 0xee, (size_t)pitch*height);
        stride[k]=(flips&(1u<<k))?-pitch:pitch;
        planes[k]=storage[k]+(stride[k]<0?(size_t)(height-1)*pitch:0);
        for(int y=0;y<height;y++) for(int x=0;x<width;x++)
            ((uint8_t *)planes[k])[(ptrdiff_t)y*stride[k]+x]=(uint8_t)(k*71+y*17+x);
    }
    Image *im=image_yuv420p(w,h,planes,stride,3);
    assert(im && im->w==w && im->h==h && im->refs==1 && im->yuv_mat==3 && !im->px);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) assert(im->yuv[(size_t)y*w+x]==(uint8_t)(y*17+x));
    for(int y=0;y<ch;y++) for(int x=0;x<cw;x++) for(int k=1;k<3;k++)
        assert(im->yuv[(size_t)w*h+((size_t)y*cw+x)*2+k-1]==(uint8_t)(k*71+y*17+x));
    image_unref(im); for(int k=0;k<3;k++) free(storage[k]);
}
static void color(int mat, uint8_t y, uint8_t u, uint8_t v, unsigned expected) {
    uint8_t luma[4]={y,y,y,y}; const uint8_t *p[3]={luma,&u,&v}; int stride[3]={2,1,1};
    Image *im=image_yuv420p(2,2,p,stride,mat); assert(im && !im->px);
    image_yuv_materialize(im);
    for(int i=0;i<4;i++) {
        assert(im->px[i]>>24==255);
        for(int shift=0;shift<24;shift+=8) {
            int actual=(im->px[i]>>shift)&255, target=(expected>>shift)&255;
            assert(abs(actual-target)<=2);
        }
    }
    uint32_t *pixels=im->px; image_yuv_materialize(im); assert(im->px==pixels);
    image_unref(im);
}
int main(void) {
    for(int w=1;w<=33;w++) for(int h=1;h<=17;h++) for(unsigned flips=0;flips<8;flips++) packing(w,h,flips);
    packing(1280,720,0); packing(1920,1080,7);
    for(int mat=0;mat<4;mat++) {
        color(mat,mat&2?0:16,128,128,0); color(mat,mat&2?255:235,128,128,0xffffff);
    }
    color(0,81,90,240,0xff0000); color(1,63,102,240,0xff0000);
    color(2,76,85,255,0xff0000); color(3,54,99,255,0xff0000);
    uint8_t byte=0;const uint8_t *p[3]={&byte,&byte,&byte};int s[3]={1,1,1};
    assert(!image_yuv420p(0,1,p,s,0));assert(!image_yuv420p(1,-1,p,s,0));
    assert(!image_yuv420p(65536,65536,p,s,0));assert(!image_yuv420p(100000,100000,p,s,0));assert(!image_yuv420p(2,2,p,s,0));
    assert(!image_yuv420p(1,1,NULL,s,0));assert(!image_yuv420p(1,1,p,NULL,0));
    p[2]=NULL;assert(!image_yuv420p(1,1,p,s,0));
    puts("PASS 4490 plane layouts, 12 color conversions, invalid dimensions/planes/strides");
}
