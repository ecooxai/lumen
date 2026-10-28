/* Exercise actual FFmpeg frame conversion, queue selection and compositor tokens.
 * The explicit presentation calls are compositor-event fixtures; actual GPU/SDL
 * success is a separate browser acceptance gate. No counter setters are used. */
#include "../src/media/media.c"

static int checks, failures;
#define CHECK(name, condition) do { bool ok_=(condition); checks++; printf("%s %s\n",ok_?"PASS":"FAIL",name); if(!ok_)failures++; } while(0)
static MpVideoQuality quality(MediaPlayer *m) { MpVideoQuality q;mp_video_quality(m,&q);return q; }
static bool is_quality(MediaPlayer *m,uint32_t total,uint32_t dropped,uint32_t corrupted) {
    MpVideoQuality q=quality(m);return q.total==total && q.dropped==dropped && q.corrupted==corrupted;
}
static bool feed(MediaPlayer *m,int pts,int flags,int errors,double skip,uint64_t epoch) {
    AVFrame *f=av_frame_alloc();
    if(!f)return false;
    f->format=AV_PIX_FMT_BGRA;f->width=f->height=4;f->pts=f->best_effort_timestamp=pts;
    f->flags=flags;f->decode_error_flags=errors;
    if(av_frame_get_buffer(f,32)<0){av_frame_free(&f);return false;}
    for(int y=0;y<4;y++)for(int x=0;x<4;x++){
        uint8_t *p=f->data[0]+y*f->linesize[0]+x*4;p[0]=23;p[1]=97;p[2]=211;p[3]=255;
    }
    Stream s={0};s.m=m;
    DecCtx d={0};d.epoch=epoch;d.skip=skip;
    bool ok=emit_video(&s,&d,f,(AVRational){1,1000});
    sws_freeContext(d.sws);av_frame_free(&f);return ok;
}
static void paint_image_fixture(Canvas *cv,Image *im,float x,float y,float w,float h) {
    DItem item={.op=DO_IMAGE,.x=x,.y=y,.w=w,.h=h,.img=im};
    DisplayList dl={0};dl.items.v=&item;dl.items.n=1;
    raster(cv,&dl,0);
}
static void advance(MediaPlayer *m,double t) {
    m->playing=true;m->waiting=false;m->base_t=t;m->base_wall=now_ms();media_tick();m->playing=false;
}
int main(void) {
    if(!SDL_Init(0)){fprintf(stderr,"SDL initialization failed: %s\n",SDL_GetError());return 1;}
    Canvas cv;canvas_init(&cv,100,100,1);
    MediaPlayer *m=mp_new(NULL);
    CHECK("a newly loaded player starts with zero quality counters",is_quality(m,0,0,0));
    CHECK("three actual converted FFmpeg frames enter the queue",feed(m,0,0,0,0,m->epoch)&&feed(m,40,0,0,0,m->epoch)&&feed(m,80,0,0,0,m->epoch)&&m->vqn==3);
    CHECK("future queued frames do not count as displayed",is_quality(m,0,0,0));
    media_tick();Image *first=m->cur;
    CHECK("selecting a frame alone does not count as displayed",first&&m->vqn==2&&is_quality(m,0,0,0));
    paint_image_fixture(&cv,first,10,10,80,80);
    CHECK("an admitted raster draw records a token without presentation",cv.media_frames.n==1&&is_quality(m,0,0,0));
    media_canvas_submission(&cv,true);
    CHECK("successful surface submission counts the admitted frame exactly once",cv.media_frames.n==0&&is_quality(m,1,0,0));
    paint_image_fixture(&cv,first,10,10,80,80);media_canvas_submission(&cv,true);media_frame_presented(first);
    CHECK("repeat software and GPU acknowledgments do not inflate totals",is_quality(m,1,0,0));
    advance(m,.09);
    CHECK("actual late queue supersession records one dropped frame",m->vqn==0&&m->cur!=first&&is_quality(m,2,1,0));
    Image *latest=m->cur;
    paint_image_fixture(&cv,first,10,10,80,80);media_canvas_submission(&cv,true);
    CHECK("a stale painted frame cannot acknowledge the current frame",is_quality(m,2,1,0));
    paint_image_fixture(&cv,latest,10,10,80,80);
    CHECK("abandoned or failed surface submission leaves counts unchanged",cv.media_frames.n==1&&is_quality(m,2,1,0));
    media_canvas_submission(&cv,false);
    CHECK("a failed submission retains the token and requires complete retry upload",cv.media_frames.n==1&&cv.media_retry&&is_quality(m,2,1,0));
    media_canvas_submission(&cv,true);
    CHECK("retry success acknowledges the actual latest frame",!cv.media_retry&&cv.media_frames.n==0&&is_quality(m,3,1,0));
    CHECK("future seek-cancelled frames are actually queued",feed(m,200,0,0,0,m->epoch)&&feed(m,240,0,0,0,m->epoch));
    mp_seek(m,.3);media_frame_presented(latest);
    CHECK("seeking preserves cumulative counts without counting queue cancellation",m->vqn==0&&is_quality(m,3,1,0));
    CHECK("post-seek frame is accepted by the current epoch",feed(m,300,0,0,.3,m->epoch));
    media_tick();media_frame_presented(m->cur);
    CHECK("the new seek frame can be presented without dropping the old one",is_quality(m,4,1,0));
    mp_free(m);

    /* Tokens follow real cached-surface composition, not individual texels. */
    m=mp_new(NULL);feed(m,0,0,0,0,m->epoch);media_tick();
    Canvas child,parent;canvas_init(&child,100,100,1);canvas_init(&parent,100,100,1);
    paint_image_fixture(&child,m->cur,60,20,30,30);
    Image cached={.w=100,.h=100,.refs=1<<30,.px=child.px,.sampled_canvas=&child};
    paint_image_fixture(&parent,&cached,200,200,100,100);
    media_canvas_submission(&parent,true);
    CHECK("a rejected offscreen child draw does not submit its source token",child.media_frames.n==1&&parent.media_frames.n==0&&is_quality(m,0,0,0));
    paint_image_fixture(&parent,&cached,0,0,100,100);
    CHECK("cached child draw propagates its token without premature counting",parent.media_frames.n==1&&is_quality(m,0,0,0));
    Image nested={.w=100,.h=100,.refs=1<<30,.px=parent.px,.sampled_canvas=&parent};
    paint_image_fixture(&cv,&nested,0,0,100,100);media_canvas_submission(&cv,true);
    CHECK("a successful nested composition acknowledges one source frame",is_quality(m,1,0,0));
    media_canvas_prune(&child);media_canvas_prune(&parent);
    CHECK("acknowledged child-cache tokens are pruned without growing history",child.media_frames.n==0&&parent.media_frames.n==0);
    canvas_free(&parent);canvas_free(&child);mp_free(m);
    DisplayList dl={0};

    m=mp_new(NULL);feed(m,0,0,0,0,m->epoch);media_tick();
    paint_image_fixture(&cv,m->cur,200,200,80,80);media_canvas_submission(&cv,true);
    CHECK("offscreen raster attempts create no submission token",cv.media_frames.n==0&&is_quality(m,0,0,0));
    DItem transparent[]={ {.op=DO_PUSH_LAYER,.alpha=0},
        {.op=DO_IMAGE,.x=10,.y=10,.w=80,.h=80,.img=m->cur}, {.op=DO_POP_LAYER} };
    dl.items.v=transparent;dl.items.n=3;raster(&cv,&dl,0);media_canvas_submission(&cv,true);
    CHECK("a rejected zero-opacity draw does not submit a video frame",cv.media_frames.n==0&&is_quality(m,0,0,0));
    feed(m,40,0,0,0,m->epoch);advance(m,.05);
    CHECK("an unpresented frame superseded later is a real drop",is_quality(m,1,1,0));
    mp_free(m);

    m=mp_new(NULL);
    CHECK("a new media load resets counters rather than inheriting old ones",is_quality(m,0,0,0));
    feed(m,0,AV_FRAME_FLAG_CORRUPT,0,0,m->epoch);
    feed(m,40,0,FF_DECODE_ERROR_INVALID_BITSTREAM,0,m->epoch);
    CHECK("actual FFmpeg corruption flags are counted once independently of display",m->vqn==2&&is_quality(m,0,0,2));
    mp_seek(m,.2);
    feed(m,100,AV_FRAME_FLAG_CORRUPT,0,.2,m->epoch);
    feed(m,200,AV_FRAME_FLAG_DISCARD|AV_FRAME_FLAG_CORRUPT,0,.2,m->epoch);
    bool cancelled=feed(m,200,AV_FRAME_FLAG_CORRUPT,0,.2,m->epoch-1);
    CHECK("seek preroll, dependency-only and cancelled decoder output do not count",!cancelled&&m->vqn==0&&is_quality(m,0,0,2));
    mp_free(m);

    m=mp_new(NULL);media_lowmem=true;m->painted=-100000;
    feed(m,0,0,0,0,m->epoch);media_tick();media_frame_presented(m->cur);
    CHECK("a hidden low-memory placeholder cannot become a displayed frame",m->cur&&m->cur->w==1&&!m->cur->media_owner&&is_quality(m,0,0,0));
    media_lowmem=false;feed(m,40,0,0,0,m->epoch);advance(m,.05);media_frame_presented(m->cur);
    CHECK("placeholder supersession is a drop and real presentation counts separately",is_quality(m,2,1,0));
    paint_image_fixture(&cv,m->cur,10,10,80,80);mp_free(m);
    m=mp_new(NULL);media_canvas_prune(&cv);
    CHECK("dead-owner tokens are removed before cache reuse",cv.media_frames.n==0);
    media_canvas_submission(&cv,true);
    CHECK("stale surface tokens cannot count toward a new player",is_quality(m,0,0,0));
    mp_free(m);canvas_free(&cv);
    CHECK("all players and canvas token storage are released",g_np==0&&cv.media_frames.v==NULL);
    SDL_Quit();
    printf("RESULTS video-quality %d/%d (%d failures)\n",checks-failures,checks,failures);
    return failures?1:0;
}
